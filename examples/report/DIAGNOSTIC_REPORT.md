# SysPulse Automated Telemetry & Anomaly Diagnostic Report

## Executive Summary

- **Collection Duration**: 8.0 seconds (41 discrete collection cycles)
- **Time Window**: `2026-10-20 05:11:55.004969+00:00` to `2026-10-20 05:12:03.004166200+00:00`
- **Total Monitored PIDs**: 1
- **Detected Anomalies**: 0 CPU spikes, 10 memory rate surges
- **Flagged Memory Leaks**: 1 process(es)

## System Telemetry Overview

![System Telemetry](system_telemetry.png)

## Memory Leak Diagnostics

| PID | Process Name | Leak Slope (MB/s) | $R^2$ Score | Total Growth | Confidence |
|---|---|---|---|---|---|
| `6044` | `mem_leak.exe` | **+9.561 MB/s** | `0.9993` | +55.51 MB | `99%` |


![Memory Leaks](memory_leaks.png)

## Root Cause & Action Items

1. **Memory Leaks**: Inspect highlighted processes using GDB/Valgrind (Linux) or WinDbg `!heap` (Windows).
2. **CPU Spikes**: Cross-reference timestamps with thread dump captures or ETW trace events.