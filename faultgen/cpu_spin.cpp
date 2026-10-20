/**
 * Diagnostic Target: CPU Saturation / High Load
 * Detection Tools: Top / Htop / Perf / SysPulse Anomaly Detection (EWMA / z-score)
 *
 * Description:
 * Spawns N worker threads that spin in a busy loop for a specified duty cycle
 * (e.g. 80% busy, 20% sleep), consuming predictable CPU capacity.
 */

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

static std::atomic<bool> g_running{true};

void worker_thread(int duty_pct) {
    constexpr auto CYCLE_MS = std::chrono::milliseconds(100);
    auto busy_duration = std::chrono::milliseconds(100 * duty_pct / 100);
    auto sleep_duration = CYCLE_MS - busy_duration;

    while (g_running.load(std::memory_order_relaxed)) {
        auto start = std::chrono::steady_clock::now();
        // Busy spin
        while (std::chrono::steady_clock::now() - start < busy_duration) {
            #if defined(__GNUC__) || defined(__clang__)
                asm volatile("" : "+r"(start));
            #endif
        }
        // Sleep remainder of cycle
        if (sleep_duration.count() > 0) {
            std::this_thread::sleep_for(sleep_duration);
        }
    }
}

int main(int argc, char* argv[]) {
    int threads = 2;
    int duty_pct = 80;
    int duration_s = 15;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            threads = std::stoi(argv[++i]);
        } else if (arg == "--duty" && i + 1 < argc) {
            duty_pct = std::stoi(argv[++i]);
        } else if (arg == "--duration-s" && i + 1 < argc) {
            duration_s = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: cpu_spin --threads <N> --duty <1-100> --duration-s <sec>\n";
            return 0;
        }
    }

    if (duty_pct < 1) duty_pct = 1;
    if (duty_pct > 100) duty_pct = 100;

    std::cout << "[FaultGen: cpu_spin] Spinning " << threads << " thread(s) at "
              << duty_pct << "% duty cycle for " << duration_s << "s...\n";

    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int i = 0; i < threads; ++i) {
        pool.emplace_back(worker_thread, duty_pct);
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_s));
    g_running.store(false, std::memory_order_release);

    for (auto& t : pool) {
        if (t.joinable()) t.join();
    }

    std::cout << "[FaultGen: cpu_spin] Completed.\n";
    return 0;
}
