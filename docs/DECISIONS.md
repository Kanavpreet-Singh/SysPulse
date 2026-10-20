# Architectural and Implementation Decisions

This document records the architectural choices, engineering trade-offs, and platform-specific designs made during the development of SysPulse.

---

## 1. Core Concurrency: SPSC Ring Buffer over Mutex Queue

* **Context**: The telemetry agent separates metric sampling (periodic, low jitter required) from metric serialization and disk I/O (variable latency, potential file system stalls).
* **Decision**: Implement a wait-free Single-Producer Single-Consumer (`spsc_ring_buffer`) circular queue with power-of-two capacity and explicit `std::memory_order_acquire` / `std::memory_order_release` semantics. Cache-line alignment (`alignas(64)`) is used on both head and tail pointers to prevent false sharing between sampler and writer threads.
* **Trade-off**: Limits topology to strictly 1 sampler thread and 1 writer thread. If multi-collector threads are needed in the future, multiple SPSC rings or an MPSC queue would be required. However, for a single sampling daemon, SPSC eliminates lock contention and priority inversion.

---

## 2. Scratch Memory: Linear Bump Arena Allocator

* **Context**: Each collection cycle scans system counters and up to hundreds of processes. Using standard `malloc`/`free` or `std::vector` reallocations in the hot loop introduces heap fragmentation and unpredictable latency spikes.
* **Decision**: Implement an `ArenaAllocator` that preallocates a contiguous memory block and bumps an offset aligned to `alignof(std::max_align_t)`. At the end of each sampling cycle, the arena is reset in O(1) time without system calls.
* **Trade-off**: Objects allocated in the arena cannot be individually freed; their lifetime is tied to the cycle epoch. This matches telemetry sampling cycles perfectly.

---

## 3. Explicit Error Handling: `Result<T, E>` Type

* **Context**: C++17 does not include `std::expected` (introduced in C++23). Telemetry collection frequently encounters transient errors (process terminates mid-scan, permission denied on restricted PIDs, counter temporarily unavailable). Using C++ exceptions adds unwinding overhead and can obscure recoverable edge cases.
* **Decision**: Implement a lightweight, value-semantic `Result<T, E>` template using `std::variant<T, E>`. Functions return explicit error messages or structured error codes.
* **Trade-off**: Requires callers to check `is_ok()` or chain operations. This promotes defensive, transparent systems programming.

---

## 4. Cross-Platform Abstraction: `ICollector`

* **Context**: Linux exposes metrics via the pseudo-filesystems `/proc/stat`, `/proc/meminfo`, and `/proc/[pid]/*`. Windows exposes metrics via the Performance Data Helper (PDH), Toolhelp32 snapshotting, and Win32 process query APIs (`GetProcessTimes`, `GetProcessMemoryInfo`).
* **Decision**: Define a platform-neutral `ICollector` interface returning standardized `SystemMetrics` and `std::vector<ProcessMetrics>`. Platform implementations (`ProcCollector` on Linux, `PdhCollector` and `WinProcessCollector` on Windows) are encapsulated behind compile-time `#ifdef` guards and a factory method `ICollector::create()`.
* **Testing Strategy**: To test the Linux `/proc` parsing logic on non-Linux development machines (and in unit test runners), `ProcCollector` accepts a configurable procfs root directory (defaulting to `"/proc"`), enabling comprehensive testing against synthetic procfs test fixtures.

---

## 5. Linux CPU Jiffies Delta Calculation

* **Context**: Linux `/proc/stat` and `/proc/[pid]/stat` report cumulative CPU times in clock ticks (jiffies). Converting raw ticks to a percentage requires computing the difference between two successive sample snapshots.
* **Decision**: Maintain a cache of the previous sample's jiffies (`prev_total_jiffies` and `map<pid_t, prev_proc_jiffies>`).
  $$\text{Proc CPU \%} = 100.0 \times \frac{\Delta (\text{utime} + \text{stime})}{\Delta \text{system\_total\_jiffies}} \times \text{online\_cores}$$
  Processes that appear for the first time report $0.0\%$ CPU until their second sample to prevent spurious $100\%$ spikes.
* **Comm Name Parsing Edge Case**: The executable name in `/proc/[pid]/stat` is enclosed in parentheses and can contain arbitrary whitespace or closing parentheses (e.g., `(worker (1))`). We locate the last occurrence of `)` in the buffer and parse the remainder of the fields with standard offsets.

---

## 6. Windows Platform Telemetry Strategy

* **Context**: Windows performance telemetry requires querying both system-wide and per-process counters.
* **Decision**:
  1. **System Metrics**: Query PDH (Performance Data Helper) counters for `\Processor(_Total)\% Processor Time` and `\Memory\Available Bytes`. Fallback to `GetSystemTimes` and `GlobalMemoryStatusEx` for minimal overhead and zero PDH query overhead when PDH initialization fails.
  2. **Process Metrics**: Use `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)` to enumerate processes, followed by `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, ...)` to obtain precise working set (`GetProcessMemoryInfo`), CPU time (`GetProcessTimes`), and handle counts (`GetProcessHandleCount`).
  3. **ETW (Event Tracing for Windows)**: A standalone `EtwSession` connects to kernel providers (`Microsoft-Windows-Kernel-Process` and `Microsoft-Windows-Kernel-Memory`) via real-time trace sessions (`EVENT_TRACE_REAL_TIME_MODE`) using `StartTrace`, `EnableTraceEx2`, `OpenTrace`, and `ProcessTrace` with `TDH` (Trace Data Helper) decoding.

---

## 7. Python Analysis Pipeline (No Scikit-Learn Rule)

* **Context**: The analysis pipeline must detect anomalies and memory leaks without heavy external dependencies like `scikit-learn`.
* **Decision**:
  1. **Robust Anomaly Detection**: Median Absolute Deviation (MAD) robust z-score:
     $$\text{MAD} = \text{median}(|x - \text{median}(x)|), \quad z = \frac{0.6745 \times (x - \text{median}(x))}{\text{MAD}}$$
     residual analysis against an Exponentially Weighted Moving Average (EWMA) baseline.
  2. **Memory Leak Detection**: Ordinary Least Squares linear regression using `numpy.polyfit(t, rss, 1)`. Calculate slope ($MB/s$), coefficient of determination ($R^2$), and verify sustained monotonic growth over a configurable post-warmup window.
  3. **Reporting**: Render self-contained visual graphs via `matplotlib` and generate a structured Markdown diagnostic summary.

---

## 8. Serialization & Rotation

* **Context**: High-frequency telemetry can produce large volumes of data.
* **Decision**: Support both newline-delimited JSON (`NDJSON`) and comma-separated values (`CSV`). Writer thread checks file size every $N$ samples and triggers rotation (`sample.1.ndjson`, etc.) if configured.
