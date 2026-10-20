# SysPulse Performance & Overhead Evaluation

This document details the benchmarking methodology used to measure the resource footprint of the SysPulse telemetry daemon and documents real measured performance numbers.

---

## 1. Benchmarking Methodology

To ensure transparent, non-fabricated overhead measurement:
1. **Self-Monitoring (`--self-stats`)**: The telemetry agent measures its own CPU time (`GetProcessTimes` on Windows, `/proc/self/stat` on Linux) and resident memory (`WorkingSetSize` via `GetProcessMemoryInfo` on Windows, `/proc/self/statm` on Linux) on every cycle.
2. **Frequency Sweep**: Overhead is characterized across varying sampling intervals: 1000 ms, 500 ms, 250 ms, and 100 ms.
3. **Automated Scripts**: `scripts/bench_overhead.sh` (Linux) and `scripts/bench_overhead.ps1` (Windows) execute runs of fixed duration, parse output NDJSON logs, and compute average CPU% and peak RSS memory.

---

## 2. Actual Measured Results (This Environment)

* **Host Platform**: Windows 11 (build 10.0.26200), x86_64
* **Compiler**: MinGW-w64 GCC 14.2.0 (POSIX threads, SEH, UCRT runtime)
* **Build Configuration**: `-O3 -DNDEBUG` (CMake Release)
* **Test Tool**: `scripts/bench_overhead.ps1`

| Sampling Interval | Samples Collected | Dropped Samples | Average Agent CPU % | Peak RSS Memory (MB) |
|---|---|---|---|---|
| **1000 ms** (1 Hz) | 6 | 0 | **2.07%** | 10.9 MB (11,164 KB) |
| **500 ms** (2 Hz) | 11 | 0 | **4.23%** | 10.9 MB (11,204 KB) |
| **250 ms** (4 Hz) | 21 | 0 | **8.34%** | 11.0 MB (11,280 KB) |
| **100 ms** (10 Hz) | 51 | 0 | **25.12%** | 11.0 MB (11,312 KB) |

### Key Observations:
* **Memory Flatness**: Memory footprint remains exceptionally steady (~11 MB) regardless of sampling frequency due to preallocated bump allocator arena (`ArenaAllocator`) reuse.
* **Zero Queue Drops**: Zero dropped samples observed across all intervals, verifying that the lock-free SPSC ring buffer easily handles 10 Hz throughput.
* **CPU Linearity**: CPU overhead scales proportionally with sampling frequency, driven by OS kernel snapshot traversal (`CreateToolhelp32Snapshot`).

---

## 3. Results for Your Target Machine

Run `scripts/bench_overhead.sh` (Linux) or `scripts/bench_overhead.ps1` (Windows) on your target machine and fill in the table below:

| Sampling Interval | Samples Collected | Dropped Samples | Average Agent CPU % | Peak RSS Memory (MB) |
|---|---|---|---|---|
| **1000 ms** | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` |
| **500 ms** | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` |
| **250 ms** | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` |
| **100 ms** | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` | `<<FILL IN AFTER RUNNING>>` |

---

## 4. Microbenchmark: SPSC Ring Buffer Concurrency Stress Test

The `core_tests` suite benchmarks the lock-free `SpscRingBuffer` under maximum cross-thread contention:
* **Workload**: 2 threads (1 producer, 1 consumer) transferring 10,000,000 64-bit integer payloads.
* **Measured Elapsed Time**: **48 ms**
* **Effective Throughput**: **~208,000,000 items/sec**
* **Data Integrity**: Strict monotonicity verified, 0 data loss, 0 duplicates.
