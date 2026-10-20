/**
 * Diagnostic Target: Monotonic Memory Leak (Heap Exhaustion)
 * Detection Tools:
 *   - Valgrind Memcheck (`valgrind --leak-check=full ./mem_leak`)
 *   - Valgrind Massif (`valgrind --tool=massif ./mem_leak`)
 *   - Clang/GCC AddressSanitizer (`-fsanitize=address`)
 *   - WinDbg Heap Analysis (`!heap -p -a <addr>`, UMH / Pageheap)
 *   - SysPulse Telemetry Analysis (`syspulse_analysis.leaks` linear regression OLS)
 *
 * Description:
 * Deliberately leaks heap memory at a steady, configurable rate (MB/sec)
 * and touches the memory so the OS commits physical pages to the resident working set (RSS).
 */

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    double rate_mb_s = 10.0;
    uint32_t interval_ms = 200;
    uint32_t duration_s = 15;
    uint32_t stop_after_mb = 0; // 0 = unlimited

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--rate-mb" && i + 1 < argc) {
            rate_mb_s = std::stod(argv[++i]);
        } else if (arg == "--interval-ms" && i + 1 < argc) {
            interval_ms = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-s" && i + 1 < argc) {
            duration_s = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--stop-after-mb" && i + 1 < argc) {
            stop_after_mb = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: mem_leak [options]\n"
                      << "  --rate-mb <MB/s>       Rate of heap memory leak in MB/sec (default: 10)\n"
                      << "  --interval-ms <ms>     Allocation cycle interval in ms (default: 200)\n"
                      << "  --duration-s <sec>     Total runtime in seconds (default: 15)\n"
                      << "  --stop-after-mb <MB>   Stop leaking after reaching target MB, hold memory\n";
            return 0;
        }
    }

    std::cout << "[FaultGen: mem_leak] Leaking memory at " << rate_mb_s << " MB/s (every "
              << interval_ms << "ms) for " << duration_s << "s...\n";

    // Unfreed pointers vector simulating an unbounded growing internal cache
    std::vector<void*> leaked_blocks;
    size_t bytes_per_interval = static_cast<size_t>((rate_mb_s * 1024.0 * 1024.0 * interval_ms) / 1000.0);
    if (bytes_per_interval == 0) bytes_per_interval = 1024;

    size_t total_leaked_bytes = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_s);

    while (std::chrono::steady_clock::now() < end_time) {
        if (stop_after_mb == 0 || (total_leaked_bytes < stop_after_mb * 1024ULL * 1024ULL)) {
            // Deliberate leak: allocate raw heap block
            char* block = new char[bytes_per_interval];
            // Touch all pages to guarantee OS physical memory commitment into RSS
            for (size_t offset = 0; offset < bytes_per_interval; offset += 4096) {
                block[offset] = static_cast<char>(offset & 0xFF);
            }
            leaked_blocks.push_back(block);
            total_leaked_bytes += bytes_per_interval;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }

    std::cout << "[FaultGen: mem_leak] Total allocated: "
              << (total_leaked_bytes / (1024 * 1024)) << " MB. Exiting without free().\n";

    // Intentionally omit delete/free so memory leak tools (ASan, Valgrind) detect it
    return 0;
}
