#include "syspulse/proc_collector.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
    #include <unistd.h>
#endif

namespace syspulse {

ProcCollector::ProcCollector(std::string proc_root)
    : proc_root_(std::move(proc_root)) {
#ifndef _WIN32
    // sysconf(_SC_PAGESIZE): Returns system page size in bytes (typically 4096).
    // Used to convert resident page counts from /proc/[pid]/statm into byte working set.
    long ps = sysconf(_SC_PAGESIZE);
    if (ps > 0) page_size_ = ps;

    // sysconf(_SC_CLK_TCK): Returns clock ticks (jiffies) per second (typically 100 on Linux).
    // Used to convert CPU ticks into seconds for rate-of-change telemetry calculations.
    long ct = sysconf(_SC_CLK_TCK);
    if (ct > 0) clk_tck_ = ct;
#else
    page_size_ = 4096;
    clk_tck_ = 100;
#endif
}

bool ProcCollector::parse_proc_stat_line(const std::string& line,
                                         int64_t& out_pid,
                                         std::string& out_name,
                                         int64_t& out_ppid,
                                         uint32_t& out_threads,
                                         uint64_t& out_utime,
                                         uint64_t& out_stime,
                                         uint64_t& out_vsize,
                                         uint64_t& out_rss_pages,
                                         uint64_t& out_page_faults) {
    if (line.empty()) return false;

    // Step 1: Extract PID (digits before the first '(')
    auto first_paren = line.find('(');
    if (first_paren == std::string::npos) return false;

    try {
        out_pid = std::stoll(line.substr(0, first_paren));
    } catch (...) {
        return false;
    }

    // Step 2: Extract comm name between the first '(' and the LAST ')'
    // Comm names in Linux may contain whitespace or parentheses, e.g. "worker (1)".
    // The kernel terminates the comm field at the last ')'.
    auto last_paren = line.rfind(')');
    if (last_paren == std::string::npos || last_paren <= first_paren) return false;

    out_name = line.substr(first_paren + 1, last_paren - first_paren - 1);

    // Step 3: Parse remaining space-separated fields after last_paren
    if (last_paren + 2 >= line.size()) return false;
    std::string rest = line.substr(last_paren + 2);
    std::istringstream iss(rest);

    // Fields after comm (1-indexed kernel numbering):
    // 3: state (char)
    // 4: ppid (int)
    // 5: pgrp, 6: session, 7: tty_nr, 8: tpgid, 9: flags
    // 10: minflt, 11: cminflt, 12: majflt, 13: cmajflt
    // 14: utime, 15: stime, 16: cutime, 17: cstime
    // 18: priority, 19: nice
    // 20: num_threads
    // 21: itrealvalue, 22: starttime
    // 23: vsize, 24: rss
    char state;
    int64_t pgrp, session, tty_nr, tpgid;
    uint64_t flags, minflt, cminflt, majflt, cmajflt;
    uint64_t cutime, cstime;
    int64_t priority, nice;
    uint64_t itrealvalue, starttime;
    int64_t rss_pages_signed = 0;

    if (!(iss >> state >> out_ppid >> pgrp >> session >> tty_nr >> tpgid >> flags
              >> minflt >> cminflt >> majflt >> cmajflt
              >> out_utime >> out_stime >> cutime >> cstime
              >> priority >> nice >> out_threads >> itrealvalue >> starttime
              >> out_vsize >> rss_pages_signed)) {
        return false;
    }

    out_rss_pages = (rss_pages_signed > 0) ? static_cast<uint64_t>(rss_pages_signed) : 0;
    out_page_faults = minflt + majflt;
    return true;
}

Result<SystemMetrics, std::string> ProcCollector::collect_system() {
    SystemMetrics metrics;

    // 1. Parse /proc/stat for CPU utilization & context switches
    // Mathematical justification:
    // Linux ticks represent elapsed CPU time spent in states:
    // idle_jiffies = idle + iowait
    // busy_jiffies = user + nice + system + irq + softirq + steal
    // total_jiffies = idle_jiffies + busy_jiffies
    // CPU % = 100.0 * (delta_total - delta_idle) / delta_total
    std::filesystem::path stat_path = std::filesystem::path(proc_root_) / "stat";
    std::ifstream stat_file(stat_path);
    if (stat_file.is_open()) {
        std::string line;
        std::vector<std::pair<uint64_t, uint64_t>> current_core_jiffies;

        while (std::getline(stat_file, line)) {
            if (line.compare(0, 4, "cpu ") == 0) {
                std::istringstream iss(line.substr(4));
                uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
                uint64_t irq = 0, softirq = 0, steal = 0;
                iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

                uint64_t idle_all = idle + iowait;
                uint64_t total_all = user + nice + system + idle_all + irq + softirq + steal;

                if (prev_sys_total_jiffies_ > 0 && total_all > prev_sys_total_jiffies_) {
                    uint64_t delta_total = total_all - prev_sys_total_jiffies_;
                    uint64_t delta_idle = (idle_all >= prev_sys_idle_jiffies_) ? (idle_all - prev_sys_idle_jiffies_) : 0;
                    metrics.cpu_total_pct = 100.0 * static_cast<double>(delta_total - delta_idle) / static_cast<double>(delta_total);
                }
                prev_sys_total_jiffies_ = total_all;
                prev_sys_idle_jiffies_ = idle_all;
            } else if (line.compare(0, 3, "cpu") == 0 && std::isdigit(line[3])) {
                std::istringstream iss(line.substr(line.find(' ')));
                uint64_t user = 0, nice = 0, system = 0, idle = 0, iowait = 0;
                uint64_t irq = 0, softirq = 0, steal = 0;
                iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

                uint64_t idle_c = idle + iowait;
                uint64_t total_c = user + nice + system + idle_c + irq + softirq + steal;
                current_core_jiffies.emplace_back(total_c, idle_c);
            } else if (line.compare(0, 5, "ctxt ") == 0) {
                // ctxt: Total number of context switches across all CPUs
                metrics.context_switches = std::stoull(line.substr(5));
            }
        }

        // Calculate per-core utilization
        if (!prev_core_jiffies_.empty() && prev_core_jiffies_.size() == current_core_jiffies.size()) {
            metrics.per_core_pct.resize(current_core_jiffies.size(), 0.0);
            for (size_t i = 0; i < current_core_jiffies.size(); ++i) {
                uint64_t dt = current_core_jiffies[i].first - prev_core_jiffies_[i].first;
                uint64_t di = current_core_jiffies[i].second - prev_core_jiffies_[i].second;
                if (dt > 0) {
                    metrics.per_core_pct[i] = 100.0 * static_cast<double>(dt - di) / static_cast<double>(dt);
                }
            }
        }
        prev_core_jiffies_ = std::move(current_core_jiffies);
    }

    // 2. Parse /proc/meminfo for memory and swap metrics (reported in kB)
    std::filesystem::path meminfo_path = std::filesystem::path(proc_root_) / "meminfo";
    std::ifstream mem_file(meminfo_path);
    if (mem_file.is_open()) {
        std::string key;
        uint64_t val;
        std::string unit;
        uint64_t swap_total = 0, swap_free = 0;

        while (mem_file >> key >> val >> unit) {
            if (key == "MemTotal:") {
                metrics.mem_total_bytes = val * 1024;
            } else if (key == "MemAvailable:") {
                metrics.mem_available_bytes = val * 1024;
            } else if (key == "SwapTotal:") {
                swap_total = val * 1024;
            } else if (key == "SwapFree:") {
                swap_free = val * 1024;
            }
        }
        if (swap_total >= swap_free) {
            metrics.swap_used_bytes = swap_total - swap_free;
        }
    }

    // 3. Parse /proc/vmstat for system-wide page faults
    std::filesystem::path vmstat_path = std::filesystem::path(proc_root_) / "vmstat";
    std::ifstream vm_file(vmstat_path);
    if (vm_file.is_open()) {
        std::string key;
        uint64_t val;
        while (vm_file >> key >> val) {
            if (key == "pgfault") {
                metrics.page_faults = val;
                break;
            }
        }
    }

    return Ok(metrics);
}

Result<std::vector<ProcessMetrics>, std::string> ProcCollector::collect_processes() {
    std::vector<ProcessMetrics> processes;
    std::error_code ec;

    std::filesystem::path root(proc_root_);
    if (!std::filesystem::exists(root, ec)) {
        return Error<std::vector<ProcessMetrics>>("Procfs directory does not exist: " + proc_root_);
    }

    std::map<int64_t, PrevProcState> next_proc_state;
    size_t online_cores = prev_core_jiffies_.empty() ? 1 : prev_core_jiffies_.size();

    // Iterate through all entries in procfs
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;

        std::string dirname = entry.path().filename().string();
        // Skip non-PID directories
        if (!std::all_of(dirname.begin(), dirname.end(), ::isdigit)) {
            continue;
        }

        int64_t pid = std::stoll(dirname);
        std::filesystem::path pid_dir = entry.path();

        // 1. Parse /proc/[pid]/stat
        std::ifstream stat_file(pid_dir / "stat");
        if (!stat_file.is_open()) {
            // Process terminated mid-scan or permission denied; safely ignore
            continue;
        }

        std::string stat_line;
        if (!std::getline(stat_file, stat_line)) {
            continue;
        }

        ProcessMetrics pm;
        uint64_t utime = 0, stime = 0, vsize = 0, rss_pages = 0;
        if (!parse_proc_stat_line(stat_line, pm.pid, pm.name, pm.ppid,
                                  pm.thread_count, utime, stime, vsize,
                                  rss_pages, pm.page_faults)) {
            continue;
        }

        pm.virtual_bytes = vsize;
        pm.rss_bytes = rss_pages * page_size_;

        // 2. Parse /proc/[pid]/statm for exact resident pages if available
        std::ifstream statm_file(pid_dir / "statm");
        if (statm_file.is_open()) {
            uint64_t sm_size = 0, sm_resident = 0;
            if (statm_file >> sm_size >> sm_resident) {
                pm.rss_bytes = sm_resident * page_size_;
            }
        }

        // 3. Parse /proc/[pid]/io for disk read/write transfer bytes
        std::ifstream io_file(pid_dir / "io");
        if (io_file.is_open()) {
            std::string line;
            while (std::getline(io_file, line)) {
                if (line.compare(0, 11, "read_bytes:") == 0) {
                    pm.io_read_bytes = std::stoull(line.substr(12));
                } else if (line.compare(0, 12, "write_bytes:") == 0) {
                    pm.io_write_bytes = std::stoull(line.substr(13));
                }
            }
        }

        // 4. File descriptors: count /proc/[pid]/fd or read FDSize from status
        std::filesystem::path fd_path = pid_dir / "fd";
        uint32_t fd_count = 0;
        std::error_code fd_ec;
        if (std::filesystem::exists(fd_path, fd_ec)) {
            for (const auto& f : std::filesystem::directory_iterator(fd_path, fd_ec)) {
                (void)f;
                fd_count++;
            }
            pm.handle_or_fd_count = fd_count;
        } else {
            // Fallback: parse /proc/[pid]/status
            std::ifstream status_file(pid_dir / "status");
            if (status_file.is_open()) {
                std::string s_line;
                while (std::getline(status_file, s_line)) {
                    if (s_line.compare(0, 7, "FDSize:") == 0) {
                        pm.handle_or_fd_count = static_cast<uint32_t>(std::stoul(s_line.substr(8)));
                        break;
                    }
                }
            }
        }

        // 5. Compute CPU% using delta of total ticks
        uint64_t current_ticks = utime + stime;
        next_proc_state[pid] = PrevProcState{current_ticks};

        auto it = prev_proc_state_.find(pid);
        if (it != prev_proc_state_.end() && prev_sys_total_jiffies_ > 0) {
            uint64_t delta_proc = (current_ticks >= it->second.total_ticks) ? (current_ticks - it->second.total_ticks) : 0;
            // CPU% = 100.0 * (delta_proc / delta_sys_total) * online_cores
            // If delta_sys_total isn't tracked per cycle, approximate with clock ticks:
            double delta_sec = static_cast<double>(delta_proc) / static_cast<double>(clk_tck_);
            pm.cpu_pct = delta_sec * 100.0 * static_cast<double>(online_cores);
        } else {
            pm.cpu_pct = 0.0; // Initial sample warmup
        }

        processes.push_back(std::move(pm));
    }

    prev_proc_state_ = std::move(next_proc_state);
    return Ok(processes);
}

} // namespace syspulse
