#include "syspulse/windows_collectors.hpp"

#ifdef _WIN32

#include <chrono>

namespace syspulse {

// Convert wide string to UTF-8
static std::string wide_to_utf8(const wchar_t* wstr) {
    if (!wstr) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(size - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, nullptr, nullptr);
    return result;
}

WinProcessCollector::WinProcessCollector() = default;

Result<SystemMetrics, std::string> WinProcessCollector::collect_system() {
    return pdh_.collect_system();
}

Result<std::vector<ProcessMetrics>, std::string> WinProcessCollector::collect_processes() {
    std::vector<ProcessMetrics> processes;

    // CreateToolhelp32Snapshot: Takes a snapshot of all processes running in the system.
    // Why chosen: Fast, available since Windows 2000, requires minimal privileges,
    // and captures PID, PPID, thread count, and executable name in a single pass.
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return Error<std::vector<ProcessMetrics>>("Failed to create Toolhelp32 snapshot: " + std::to_string(GetLastError()));
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return Error<std::vector<ProcessMetrics>>("Failed to retrieve first process from snapshot");
    }

    // Determine number of online logical processors for CPU% normalization
    SYSTEM_INFO sys_info{};
    GetSystemInfo(&sys_info);
    DWORD num_cores = sys_info.dwNumberOfProcessors > 0 ? sys_info.dwNumberOfProcessors : 1;

    auto now_epoch = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now_epoch).count());

    std::map<int64_t, PrevWinProcCpu> next_proc_cpu;

    do {
        // Skip System Idle Process (PID 0)
        if (entry.th32ProcessID == 0) continue;

        ProcessMetrics pm;
        pm.pid = entry.th32ProcessID;
        pm.ppid = entry.th32ParentProcessID;
        pm.name = wide_to_utf8(entry.szExeFile);
        pm.thread_count = entry.cntThreads;

        // OpenProcess with PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
        // Why chosen: PROCESS_QUERY_LIMITED_INFORMATION is accessible even for elevated processes
        // when running as standard user, avoiding ACCESS_DENIED (5) errors.
        HANDLE proc_handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
        if (proc_handle != nullptr) {
            // 1. GetProcessMemoryInfo: Queries memory footprint via PSAPI.
            // WorkingSetSize: physical RAM pages allocated to process.
            // PrivateUsage: pagefile committed memory (private bytes).
            PROCESS_MEMORY_COUNTERS_EX pmc{};
            if (GetProcessMemoryInfo(proc_handle, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                pm.rss_bytes = pmc.WorkingSetSize;
                pm.virtual_bytes = pmc.PrivateUsage;
                pm.page_faults = pmc.PageFaultCount;
            }

            // 2. GetProcessHandleCount: Queries count of open kernel handles.
            // Crucial for diagnosing handle leaks in Windows services.
            DWORD handle_count = 0;
            if (GetProcessHandleCount(proc_handle, &handle_count)) {
                pm.handle_or_fd_count = handle_count;
            }

            // 3. GetProcessIoCounters: Queries I/O transfer volume.
            IO_COUNTERS io_counters{};
            if (GetProcessIoCounters(proc_handle, &io_counters)) {
                pm.io_read_bytes = io_counters.ReadTransferCount;
                pm.io_write_bytes = io_counters.WriteTransferCount;
            }

            // 4. GetProcessTimes: Queries user and kernel CPU execution time (100ns units).
            FILETIME creation_ft, exit_ft, kernel_ft, user_ft;
            if (GetProcessTimes(proc_handle, &creation_ft, &exit_ft, &kernel_ft, &user_ft)) {
                ULARGE_INTEGER kt, ut;
                kt.LowPart = kernel_ft.dwLowDateTime;
                kt.HighPart = kernel_ft.dwHighDateTime;
                ut.LowPart = user_ft.dwLowDateTime;
                ut.HighPart = user_ft.dwHighDateTime;

                uint64_t total_cpu_100ns = kt.QuadPart + ut.QuadPart;
                next_proc_cpu[pm.pid] = PrevWinProcCpu{total_cpu_100ns, now_ns};

                auto it = prev_proc_cpu_.find(pm.pid);
                if (it != prev_proc_cpu_.end() && now_ns > it->second.sample_ts_ns) {
                    uint64_t delta_cpu_ns = (total_cpu_100ns - it->second.total_cpu_100ns) * 100;
                    uint64_t delta_wall_ns = now_ns - it->second.sample_ts_ns;
                    pm.cpu_pct = (100.0 * static_cast<double>(delta_cpu_ns)) / static_cast<double>(delta_wall_ns * num_cores);
                } else {
                    pm.cpu_pct = 0.0;
                }
            }

            CloseHandle(proc_handle);
        }

        processes.push_back(std::move(pm));
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    prev_proc_cpu_ = std::move(next_proc_cpu);

    return Ok(processes);
}

} // namespace syspulse

#endif // _WIN32
