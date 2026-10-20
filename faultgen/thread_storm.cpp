/**
 * Diagnostic Target: Thread Thrashing & Context Switching Storm
 * Detection Tools:
 *   - SysPulse Telemetry: `thread_count` volatility and `context_switches` spikes
 *   - Linux: `perf sched record`, `vmstat 1` (cs column)
 *   - Windows ETW: Microsoft-Windows-Kernel-Process / Thread Create/Delete events
 *   - Windows WPA (Windows Performance Analyzer): CPU Usage (Context Switches) graph
 *
 * Description:
 * Rapidly creates short-lived worker threads that perform brief synchronization
 * and immediately terminate, driving high thread creation overhead and context switches.
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    uint32_t target_threads = 20;
    uint32_t rate_per_sec = 100;
    uint32_t duration_s = 10;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            target_threads = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--rate" && i + 1 < argc) {
            rate_per_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-s" && i + 1 < argc) {
            duration_s = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: thread_storm [--threads <N>] [--rate <threads/sec>] [--duration-s <sec>]\n";
            return 0;
        }
    }

    std::cout << "[FaultGen: thread_storm] Launching thread storm: " << rate_per_sec
              << " spawns/sec (max batch " << target_threads << ") for " << duration_s << "s...\n";

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_s);
    size_t total_spawned = 0;

    while (std::chrono::steady_clock::now() < end_time) {
        std::vector<std::thread> batch;
        batch.reserve(target_threads);

        for (uint32_t i = 0; i < target_threads; ++i) {
            batch.emplace_back([]() {
                // Short-lived busy work + yield to force context switch
                std::this_thread::yield();
                volatile int acc = 0;
                for (int j = 0; j < 1000; ++j) acc += j;
                (void)acc;
            });
            total_spawned++;
        }

        for (auto& t : batch) {
            if (t.joinable()) t.join();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / (rate_per_sec / target_threads + 1)));
    }

    std::cout << "[FaultGen: thread_storm] Finished. Spawned and joined "
              << total_spawned << " total threads.\n";
    return 0;
}
