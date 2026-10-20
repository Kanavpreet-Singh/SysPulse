#pragma once

#include "collector.hpp"
#include <map>
#include <string>

namespace syspulse {

struct PrevProcState {
    uint64_t total_ticks{0};
};

/**
 * Linux procfs telemetry collector.
 * Reads /proc/stat, /proc/meminfo, /proc/vmstat, and /proc/[pid] entries.
 * Accepts a configurable proc_root path for deterministic unit testing with mock fixtures.
 */
class ProcCollector : public ICollector {
public:
    explicit ProcCollector(std::string proc_root = "/proc");
    ~ProcCollector() override = default;

    Result<SystemMetrics, std::string> collect_system() override;
    Result<std::vector<ProcessMetrics>, std::string> collect_processes() override;

    // Helper exposed for testing parser logic directly
    static bool parse_proc_stat_line(const std::string& line,
                                     int64_t& out_pid,
                                     std::string& out_name,
                                     int64_t& out_ppid,
                                     uint32_t& out_threads,
                                     uint64_t& out_utime,
                                     uint64_t& out_stime,
                                     uint64_t& out_vsize,
                                     uint64_t& out_rss_pages,
                                     uint64_t& out_page_faults);

private:
    std::string proc_root_;
    uint64_t prev_sys_total_jiffies_{0};
    uint64_t prev_sys_idle_jiffies_{0};
    std::vector<std::pair<uint64_t, uint64_t>> prev_core_jiffies_{}; // (total, idle) per core
    std::map<int64_t, PrevProcState> prev_proc_state_{};
    long page_size_{4096};
    long clk_tck_{100};
};

} // namespace syspulse
