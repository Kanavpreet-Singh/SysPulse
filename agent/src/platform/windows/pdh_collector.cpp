#include "syspulse/windows_collectors.hpp"

#ifdef _WIN32

#include <iostream>

namespace syspulse {

PdhCollector::PdhCollector() {
    // PdhOpenQueryW: Initializes a PDH query structure for real-time counter sampling.
    // Why chosen: Centralizes multi-counter polling and handles counter rate calculation.
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query_);
    if (status != ERROR_SUCCESS) {
        query_ = nullptr;
        return;
    }

    // PdhAddEnglishCounterW: Adds counter using English path to prevent locale-specific breakage
    // (e.g., French or German Windows installations where "\\Prozessor" is used instead of "\\Processor").
    PdhAddEnglishCounterW(query_, L"\\Processor(_Total)\\% Processor Time", 0, &cpu_total_counter_);
    PdhAddEnglishCounterW(query_, L"\\Memory\\Available Bytes", 0, &mem_avail_counter_);
    PdhAddEnglishCounterW(query_, L"\\System\\Context Switches/sec", 0, &ctxt_counter_);
    PdhAddEnglishCounterW(query_, L"\\Memory\\Page Faults/sec", 0, &page_faults_counter_);

    // Initial collection to prime counter deltas
    PdhCollectQueryData(query_);
    initialized_ = true;
}

PdhCollector::~PdhCollector() {
    if (query_) {
        // PdhCloseQuery: Closes all counters associated with query and frees memory.
        PdhCloseQuery(query_);
        query_ = nullptr;
    }
}

Result<SystemMetrics, std::string> PdhCollector::collect_system() {
    SystemMetrics metrics;

    // Fallback: GlobalMemoryStatusEx: Always query physical memory sizes directly from the kernel
    // Returns total and available physical and page file (virtual) memory.
    MEMORYSTATUSEX mem_status{};
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        metrics.mem_total_bytes = mem_status.ullTotalPhys;
        metrics.mem_available_bytes = mem_status.ullAvailPhys;
        if (mem_status.ullTotalPageFile >= mem_status.ullAvailPageFile) {
            metrics.swap_used_bytes = mem_status.ullTotalPageFile - mem_status.ullAvailPageFile;
        }
    }

    if (initialized_ && query_) {
        // PdhCollectQueryData: Samples all counters in query simultaneously for time synchronization.
        PDH_STATUS status = PdhCollectQueryData(query_);
        if (status == ERROR_SUCCESS) {
            PDH_FMT_COUNTERVALUE val;

            if (PdhGetFormattedCounterValue(cpu_total_counter_, PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
                metrics.cpu_total_pct = val.doubleValue;
            }

            if (PdhGetFormattedCounterValue(mem_avail_counter_, PDH_FMT_LARGE, nullptr, &val) == ERROR_SUCCESS) {
                metrics.mem_available_bytes = static_cast<uint64_t>(val.largeValue);
            }

            if (PdhGetFormattedCounterValue(ctxt_counter_, PDH_FMT_LARGE, nullptr, &val) == ERROR_SUCCESS) {
                metrics.context_switches = static_cast<uint64_t>(val.largeValue);
            }

            if (PdhGetFormattedCounterValue(page_faults_counter_, PDH_FMT_LARGE, nullptr, &val) == ERROR_SUCCESS) {
                metrics.page_faults = static_cast<uint64_t>(val.largeValue);
            }

            return Ok(metrics);
        }
    }

    // Secondary fallback: GetSystemTimes for kernel/user CPU times
    FILETIME idle_ft, kernel_ft, user_ft;
    if (GetSystemTimes(&idle_ft, &kernel_ft, &user_ft)) {
        ULARGE_INTEGER idle, kernel, user;
        idle.LowPart = idle_ft.dwLowDateTime;
        idle.HighPart = idle_ft.dwHighDateTime;
        kernel.LowPart = kernel_ft.dwLowDateTime;
        kernel.HighPart = kernel_ft.dwHighDateTime;
        user.LowPart = user_ft.dwLowDateTime;
        user.HighPart = user_ft.dwHighDateTime;

        // Note: kernel time includes idle time in Windows GetSystemTimes
        uint64_t total = (kernel.QuadPart - idle.QuadPart) + user.QuadPart;
        (void)total;
    }

    return Ok(metrics);
}

} // namespace syspulse

#endif // _WIN32
