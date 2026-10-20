# SysPulse: Cross-Platform Systems Telemetry & Diagnostics Agent

[![SysPulse CI](https://github.com/Kanavpreet-Singh/SysPulse/actions/workflows/ci.yml/badge.svg)](https://github.com/Kanavpreet-Singh/SysPulse/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20Windows-lightgrey.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A high-performance, cross-platform (Linux + Windows) process, CPU, and memory telemetry daemon written in modern C++17, accompanied by an OS fault-injection suite and a pure Python (no scikit-learn) diagnostic pipeline for automated anomaly and memory leak detection.

---

## Architecture & Design Highlights

* **Lock-Free Concurrency**: Dedicated single-producer single-consumer (`spsc_ring_buffer.hpp`) circular queue with power-of-two capacity, `alignas(64)` cache-line separation against false sharing, and explicit `std::memory_order_acquire` / `std::memory_order_release` ordering. Microbenchmarked at **>200,000,000 items/sec** under cross-thread contention.
* **Zero-Allocation Hot Path**: Preallocated bump `ArenaAllocator` serving per-cycle telemetry allocations, resetting in $O(1)$ time to avoid heap fragmentation and jitter.
* **Robust Error Semantics**: Custom `Result<T, E>` monadic error-handling type enforcing explicit, exception-free failure paths across OS boundaries.
* **Platform Neutrality**: Abstract `ICollector` decoupling OS-specific kernel interfaces:
  * **Linux**: High-speed parsing of `/proc/stat`, `/proc/meminfo`, `/proc/vmstat`, `/proc/[pid]/stat`, `statm`, `io`, and `status`. Handles spaces and parentheses in process names (`comm`) and vanishing PIDs.
  * **Windows**: Real-time Performance Data Helper (PDH) queries, Toolhelp32 process snapshotting, and native Win32 queries (`GetProcessTimes`, `GetProcessMemoryInfo`, `GetProcessHandleCount`, `GlobalMemoryStatusEx`).
  * **ETW (Event Tracing for Windows)**: Real-time kernel session consuming `Microsoft-Windows-Kernel-Process` (process start/stop lifecycle) and `Microsoft-Windows-Kernel-Memory` parsed via Trace Data Helper (TDH).
* **Automated Python Diagnostics**:
  * **Anomalies**: Robust $z$-scores via Median Absolute Deviation (MAD) and Exponentially Weighted Moving Average (EWMA) residuals.
  * **Memory Leaks**: Ordinary Least Squares linear regression (`numpy.polyfit`) computing growth velocity ($MB/s$) and $R^2$ goodness-of-fit.
* **Azure Data Explorer (KQL)**: Production-ready schemas, ingestion mappings, and 9 analytical queries for fleet-wide telemetry analysis.

---

## Repository Layout

```
SysPulse/
├── CMakeLists.txt          # Multi-platform CMake build configuration
├── Dockerfile              # Multi-stage build and runtime container
├── core/                   # SPSC ring buffer, Arena allocator, Result<T,E>
│   ├── arena_allocator.{hpp,cpp}
│   ├── spsc_ring_buffer.hpp
│   ├── result.hpp
│   └── tests/              # Core unit and 10M-item stress tests
├── agent/                  # Telemetry agent implementation
│   ├── include/syspulse/   # Platform-neutral headers
│   ├── src/                # Sampler, Writer, Config, CLI
│   ├── src/platform/linux/ # Linux procfs collector
│   ├── src/platform/windows/ # PDH, Toolhelp32, and ETW collectors
│   └── tests/              # Procfs parsing tests with synthetic fixtures
├── faultgen/               # Workload fault injection binaries
│   ├── cpu_spin.cpp        # N threads at X% duty cycle
│   ├── mem_leak.cpp        # Linear heap memory leak
│   ├── handle_leak.cpp     # Kernel handle / file descriptor leak
│   ├── thread_storm.cpp    # Thread churn and context switch storm
│   └── crash_test.cpp      # Null deref, stack overflow, heap corruption
├── analysis/               # Python diagnostics and anomaly engine
│   ├── syspulse_analysis/  # loader, features, anomaly, leaks, report, cli
│   └── tests/              # Pytest test suite with synthetic data
├── kql/                    # Kusto schemas, mappings, and analytical queries
├── docs/                   # ARCHITECTURE, DEBUGGING, PERFORMANCE, ETW, DECISIONS
└── scripts/                # Automated experiment runners and overhead benchmarks
```

---

## Building and Running

### Prerequisites
* **C++ Compiler**: GCC >= 11, Clang >= 14, or MSVC >= 2019 (C++17 support)
* **CMake**: >= 3.20
* **Python**: >= 3.10 with `pandas`, `numpy`, `matplotlib`, `pytest`

### Build (Linux / macOS / MinGW)
```bash
cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Run C++ Unit Tests
```bash
ctest --test-dir build --output-on-failure
```

### Run Python PyTest Suite
```bash
python -m pip install -e analysis/
pytest analysis/tests -v
```

### Run Telemetry Agent
```bash
./build/syspulse_agent --interval-ms 500 --duration-s 60 --out samples.ndjson --self-stats
```

### Run Diagnostic Analysis Pipeline
```bash
python -m syspulse_analysis report samples.ndjson --out report/
```

### Docker Multi-Stage Build
```bash
docker build -t syspulse:latest .
docker run --rm -v /proc:/proc:ro -v $(pwd)/out:/var/log/syspulse syspulse:latest
```

---

## Example Output & Live Verification

During end-to-end fault injection with `mem_leak` configured to leak 10 MB/s, the SysPulse agent and analysis engine detected:

```
[SysPulse Analysis] Loading telemetry from: examples/mem_leak_samples.ndjson
[SysPulse Analysis] Loaded 41 records. Engineering features...
[SysPulse Analysis] Running robust anomaly detection...
  -> Detected 10 anomalies (0 CPU spikes)
[SysPulse Analysis] Running linear regression memory leak detection...
  -> Flagged 1 leaking processes
     PID 6044 (mem_leak.exe): +9.561 MB/s (R^2=0.9993, confidence=0.999)
[SysPulse Analysis] Generating diagnostic report into: examples/report
[SysPulse Analysis] Diagnostic report complete: examples/report/DIAGNOSTIC_REPORT.md
```

Detailed Markdown diagnostic summaries and Matplotlib time-series plots are saved under [`examples/report/`](examples/report/DIAGNOSTIC_REPORT.md).

---

## Measured Performance & Overhead

*Measured on Windows 11 (x86_64), GCC 14.2.0 MinGW-w64 (`scripts/bench_overhead.ps1`):*

| Sampling Interval | Samples / Sec | Avg CPU % | Peak RSS Memory | Dropped Samples |
|---|---|---|---|---|
| **1000 ms** | 1 Hz | 2.07% | 10.9 MB | 0 |
| **500 ms** | 2 Hz | 4.23% | 10.9 MB | 0 |
| **250 ms** | 4 Hz | 8.34% | 11.0 MB | 0 |
| **100 ms** | 10 Hz | 25.12% | 11.0 MB | 0 |

*(For results on Linux, run `scripts/bench_overhead.sh` and update [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md)).*

---

## Honest Limitations

1. **ETW Privileges on Windows**: Real-time ETW kernel trace sessions require Administrator privileges or membership in the *Performance Log Users* security group. In unprivileged environments, SysPulse falls back to Toolhelp32 snapshotting.
2. **Short-Lived Processes on Linux**: Without eBPF / `fork` connectors, polling `/proc` at 500 ms will miss processes whose lifecycle starts and finishes entirely within the sampling interval.
3. **Containerized Procfs Virtualization**: When executed inside Docker, `/proc` reflects host-wide statistics unless isolated PID namespaces are explicitly mounted.

---

## Resume Bullets (Evidence-Backed)

* *Engineered a cross-platform (Linux/Windows) systems telemetry agent in modern C++17 utilizing an acquire-release lock-free SPSC ring buffer microbenchmarked at >200M msgs/sec and a bump arena allocator to achieve zero heap allocations on the periodic sampling hot path.*
* *Implemented native kernel telemetry collectors spanning Linux procfs jiffies delta calculations and Windows PDH/Toolhelp32 APIs, alongside real-time ETW sessions for sub-second process lifecycle tracking decoded with TDH.*
* *Constructed an automated Python diagnostic pipeline utilizing Median Absolute Deviation (MAD) robust z-scores and NumPy ordinary least squares linear regression to detect memory leaks and CPU anomalies with >0.95 $R^2$ confidence without third-party ML libraries.*
* *Authored reproducible systems diagnostic playbooks for GDB, Valgrind (Memcheck/Massif), and WinDbg (`!analyze -v`, `!heap`, `!address`), validated against deliberate heap corruption and memory leak fault injectors.*
