#include "syspulse/cli.hpp"
#include "syspulse/collector.hpp"
#include "syspulse/config.hpp"
#include "syspulse/model.hpp"
#include "syspulse/sampler.hpp"
#include "spsc_ring_buffer.hpp"
#include "syspulse/writer.hpp"
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

static std::atomic<bool> g_shutdown_requested{false};

#ifdef _WIN32
static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT || dwCtrlType == CTRL_CLOSE_EVENT) {
        g_shutdown_requested.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}
#endif

#ifndef _WIN32
static void PosixSignalHandler(int signum) {
    (void)signum;
    g_shutdown_requested.store(true, std::memory_order_release);
}
#endif

int main(int argc, char* argv[]) {
    auto parse_res = syspulse::parse_cli(argc, argv);
    if (parse_res.is_err()) {
        if (parse_res.error() == "HELP_REQUESTED") {
            syspulse::print_usage(argv[0]);
            return 0;
        }
        std::cerr << "Error: " << parse_res.error() << "\n\n";
        syspulse::print_usage(argv[0]);
        return 1;
    }

    const auto& config = parse_res.value();

    std::cout << "[SysPulse] Initializing agent...\n"
              << "  Sampling interval: " << config.interval_ms << " ms\n"
              << "  Duration: " << (config.duration_s == 0 ? "Infinite" : (std::to_string(config.duration_s) + " s")) << "\n"
              << "  Output path: " << config.out_path << "\n"
              << "  Format: " << (config.format == syspulse::OutputFormat::Ndjson ? "NDJSON" : "CSV") << "\n"
              << "  Top N processes: " << config.top_n << "\n"
              << "  Self stats: " << (config.self_stats ? "Enabled" : "Disabled") << "\n";

    // Register signal handlers for graceful shutdown
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
#else
    std::signal(SIGINT, PosixSignalHandler);
    std::signal(SIGTERM, PosixSignalHandler);
#endif

    // Create platform collector
    auto collector = syspulse::ICollector::create(config.proc_root);
    if (!collector) {
        std::cerr << "[SysPulse] Failed to create system telemetry collector\n";
        return 1;
    }

    // Allocate SPSC ring buffer for inter-thread communication
    constexpr size_t RING_BUFFER_CAPACITY = 2048;
    auto ring_buffer = std::make_shared<syspulse::SpscRingBuffer<syspulse::Sample>>(RING_BUFFER_CAPACITY);

    // Initialize pipeline components
    syspulse::Writer writer(config, ring_buffer);
    syspulse::Sampler sampler(config, std::move(collector), ring_buffer);

    writer.start();
    sampler.start();

    std::cout << "[SysPulse] Collection running. Press Ctrl+C to terminate.\n";

    while (!g_shutdown_requested.load(std::memory_order_relaxed)) {
        if (config.duration_s > 0) {
            // Sampler handles duration internally and stops itself
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // Check if sampler finished its duration
        if (sampler.samples_collected() > 0 && config.duration_s > 0) {
            // Break when duration has elapsed
            static auto start_ts = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::steady_clock::now() - start_ts;
            if (elapsed >= std::chrono::seconds(config.duration_s)) {
                break;
            }
        }
    }

    std::cout << "\n[SysPulse] Initiating graceful shutdown and buffer flush...\n";
    sampler.stop();
    sampler.join();

    writer.stop();
    writer.join();

    std::cout << "[SysPulse] Finished.\n"
              << "  Samples collected: " << sampler.samples_collected() << "\n"
              << "  Samples written:   " << writer.samples_written() << "\n"
              << "  Samples dropped:   " << sampler.samples_dropped() << "\n";

    return 0;
}
