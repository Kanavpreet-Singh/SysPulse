#include "syspulse/sampler.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <psapi.h>
#else
    #include <unistd.h>
    #include <sys/times.h>
#endif

namespace syspulse {

static std::string detect_hostname() {
    char buf[256];
#ifdef _WIN32
    DWORD size = sizeof(buf);
    if (GetComputerNameA(buf, &size)) {
        return std::string(buf);
    }
#else
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        return std::string(buf);
    }
#endif
    return "localhost";
}

Sampler::Sampler(const Config& config,
                 std::shared_ptr<ICollector> collector,
                 std::shared_ptr<SpscRingBuffer<Sample>> ring_buffer)
    : config_(config),
      collector_(std::move(collector)),
      ring_buffer_(std::move(ring_buffer)),
      scratch_arena_(1024 * 1024), // 1MB scratch arena for per-cycle operations
      hostname_(detect_hostname()) {
}

Sampler::~Sampler() {
    stop();
    join();
}

void Sampler::start() {
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&Sampler::run, this);
}

void Sampler::stop() {
    running_.store(false, std::memory_order_release);
}

void Sampler::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Sampler::run() {
    const auto interval = std::chrono::milliseconds(config_.interval_ms);
    const auto start_wall_time = std::chrono::steady_clock::now();
    auto next_tick = start_wall_time;

    while (running_.load(std::memory_order_relaxed)) {
        next_tick += interval;

        // Perform collection cycle
        collect_cycle();

        // Check overall duration limit if configured
        if (config_.duration_s > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start_wall_time;
            if (elapsed >= std::chrono::seconds(config_.duration_s)) {
                running_.store(false, std::memory_order_release);
                break;
            }
        }

        // Sleep until next tick, avoiding cumulative drift
        auto now = std::chrono::steady_clock::now();
        if (next_tick > now) {
            std::this_thread::sleep_until(next_tick);
        } else {
            // Fell behind interval, resynchronize next_tick to current time
            next_tick = now;
        }
    }
}

void Sampler::collect_cycle() {
    // 1. Reset per-cycle scratch arena
    scratch_arena_.reset();

    Sample sample;
    auto now_epoch = std::chrono::system_clock::now().time_since_epoch();
    sample.timestamp_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now_epoch).count());
    sample.hostname = hostname_;

    // 2. Collect system metrics
    auto sys_res = collector_->collect_system();
    if (sys_res.is_ok()) {
        sample.system = sys_res.value();
    }

    // 3. Collect process metrics
    auto proc_res = collector_->collect_processes();
    if (proc_res.is_ok()) {
        auto procs = std::move(proc_res.value());

        // Apply filters: PID or Name Regex
        if (config_.target_pid.has_value() || config_.name_regex.has_value()) {
            std::vector<ProcessMetrics> filtered;
            filtered.reserve(procs.size());

            for (auto& p : procs) {
                if (config_.target_pid.has_value() && p.pid != config_.target_pid.value()) {
                    continue;
                }
                if (config_.name_regex.has_value() && !std::regex_search(p.name, config_.name_regex.value())) {
                    continue;
                }
                filtered.push_back(std::move(p));
            }
            procs = std::move(filtered);
        }

        // Sort descending by CPU% and limit to Top-N
        if (config_.top_n > 0 && procs.size() > config_.top_n) {
            std::partial_sort(procs.begin(),
                              procs.begin() + config_.top_n,
                              procs.end(),
                              [](const ProcessMetrics& a, const ProcessMetrics& b) {
                                  return a.cpu_pct > b.cpu_pct;
                              });
            procs.resize(config_.top_n);
        }

        sample.processes = std::move(procs);
    }

    // 4. Record self-stats if requested
    if (config_.self_stats) {
        sample.self_stats = measure_self_overhead();
    }

    // 5. Push sample to lock-free SPSC ring buffer
    if (!ring_buffer_->try_push(std::move(sample))) {
        samples_dropped_.fetch_add(1, std::memory_order_relaxed);
    } else {
        samples_collected_.fetch_add(1, std::memory_order_relaxed);
    }
}

AgentSelfStats Sampler::measure_self_overhead() {
    AgentSelfStats stats;
    stats.ring_buffer_dropped_samples = samples_dropped_.load(std::memory_order_relaxed);
    stats.arena_high_water_mark = scratch_arena_.high_water_mark();

    auto now_epoch = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t now_ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now_epoch).count());

#ifdef _WIN32
    // Windows: GetProcessMemoryInfo provides WorkingSetSize (resident memory in bytes)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        stats.rss_bytes = pmc.WorkingSetSize;
    }

    // Windows: GetProcessTimes provides user and kernel times in 100ns units
    FILETIME creation_time, exit_time, kernel_time, user_time;
    if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time, &kernel_time, &user_time)) {
        ULARGE_INTEGER kt, ut;
        kt.LowPart = kernel_time.dwLowDateTime;
        kt.HighPart = kernel_time.dwHighDateTime;
        ut.LowPart = user_time.dwLowDateTime;
        ut.HighPart = user_time.dwHighDateTime;

        uint64_t current_cpu_100ns = kt.QuadPart + ut.QuadPart;
        if (prev_self_cpu_time_ > 0 && now_ns > prev_self_sample_ts_) {
            uint64_t delta_cpu_ns = (current_cpu_100ns - prev_self_cpu_time_) * 100;
            uint64_t delta_wall_ns = now_ns - prev_self_sample_ts_;
            stats.cpu_pct = (100.0 * static_cast<double>(delta_cpu_ns)) / static_cast<double>(delta_wall_ns);
        }
        prev_self_cpu_time_ = current_cpu_100ns;
        prev_self_sample_ts_ = now_ns;
    }
#else
    // Linux: Read /proc/self/statm for resident pages and /proc/self/stat for utime + stime
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        uint64_t size_pages = 0, resident_pages = 0;
        if (statm >> size_pages >> resident_pages) {
            long page_size = sysconf(_SC_PAGESIZE);
            stats.rss_bytes = resident_pages * (page_size > 0 ? page_size : 4096);
        }
    }

    std::ifstream stat("/proc/self/stat");
    if (stat.is_open()) {
        std::string s;
        // Skip first 13 fields to reach utime (14) and stime (15)
        for (int i = 0; i < 13 && (stat >> s); ++i) {}
        uint64_t utime = 0, stime = 0;
        if (stat >> utime >> stime) {
            uint64_t total_ticks = utime + stime;
            long clk_tck = sysconf(_SC_CLK_TCK);
            if (clk_tck <= 0) clk_tck = 100;

            if (prev_self_cpu_time_ > 0 && now_ns > prev_self_sample_ts_) {
                uint64_t delta_ticks = total_ticks - prev_self_cpu_time_;
                double delta_sec = static_cast<double>(now_ns - prev_self_sample_ts_) / 1e9;
                stats.cpu_pct = (100.0 * (static_cast<double>(delta_ticks) / static_cast<double>(clk_tck))) / delta_sec;
            }
            prev_self_cpu_time_ = total_ticks;
            prev_self_sample_ts_ = now_ns;
        }
    }
#endif

    return stats;
}

} // namespace syspulse
