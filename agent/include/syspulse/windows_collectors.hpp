#pragma once

#include "collector.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#include <map>
#include <string>
#include <vector>

namespace syspulse {

/**
 * Windows Performance Data Helper (PDH) System Metrics Collector.
 * Queries system processor, memory, and context switch counters.
 */
class PdhCollector {
public:
    PdhCollector();
    ~PdhCollector();

    PdhCollector(const PdhCollector&) = delete;
    PdhCollector& operator=(const PdhCollector&) = delete;

    Result<SystemMetrics, std::string> collect_system();

private:
    PDH_HQUERY query_{nullptr};
    PDH_HCOUNTER cpu_total_counter_{nullptr};
    std::vector<PDH_HCOUNTER> per_core_counters_{};
    PDH_HCOUNTER mem_avail_counter_{nullptr};
    PDH_HCOUNTER ctxt_counter_{nullptr};
    PDH_HCOUNTER page_faults_counter_{nullptr};
    bool initialized_{false};
};

struct PrevWinProcCpu {
    uint64_t total_cpu_100ns{0};
    uint64_t sample_ts_ns{0};
};

/**
 * Windows Process Collector using Toolhelp32 snapshotting and Win32 process APIs:
 * - CreateToolhelp32Snapshot / Process32First / Process32Next
 * - OpenProcess (PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ)
 * - GetProcessTimes
 * - GetProcessMemoryInfo (PROCESS_MEMORY_COUNTERS_EX)
 * - GetProcessHandleCount
 * - GetProcessIoCounters (IO_COUNTERS)
 */
class WinProcessCollector : public ICollector {
public:
    WinProcessCollector();
    ~WinProcessCollector() override = default;

    Result<SystemMetrics, std::string> collect_system() override;
    Result<std::vector<ProcessMetrics>, std::string> collect_processes() override;

private:
    PdhCollector pdh_;
    std::map<int64_t, PrevWinProcCpu> prev_proc_cpu_{};
    uint64_t prev_sys_idle_{0};
    uint64_t prev_sys_kernel_{0};
    uint64_t prev_sys_user_{0};
};

/**
 * Event Tracing for Windows (ETW) Real-time Session.
 * Subscribes to Microsoft-Windows-Kernel-Process and Microsoft-Windows-Kernel-Memory.
 */
class EtwSession {
public:
    EtwSession();
    ~EtwSession();

    Result<void, std::string> start(const std::wstring& session_name = L"SysPulseEtwSession");
    void stop();

    std::vector<EventRecord> drain_events();

private:
    TRACEHANDLE session_handle_{0};
    TRACEHANDLE trace_handle_{INVALID_PROCESSTRACE_HANDLE};
    std::wstring session_name_{};
    bool running_{false};
};

} // namespace syspulse

#endif // _WIN32
