/**
 * Diagnostic Target: Kernel Handle / File Descriptor Leak
 * Detection Tools:
 *   - Windows Sysinternals: Process Explorer / Handle (`handle.exe -p <pid>`)
 *   - WinDbg: `!handle 0 f` (dump all handles with object types and reference counts)
 *   - SysPulse Telemetry: `handle_or_fd_count` monotonic growth
 *
 * Description:
 * Rapidly creates kernel synchronization objects (Events / Mutexes on Windows, or
 * eventfd/pipes on POSIX) without calling CloseHandle/close, causing handle table exhaustion.
 */

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <fcntl.h>
    #include <unistd.h>
#endif

int main(int argc, char* argv[]) {
    uint32_t rate_per_sec = 200;
    uint32_t duration_s = 15;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--rate" && i + 1 < argc) {
            rate_per_sec = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-s" && i + 1 < argc) {
            duration_s = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: handle_leak [--rate <handles/sec>] [--duration-s <sec>]\n";
            return 0;
        }
    }

    std::cout << "[FaultGen: handle_leak] Leaking handles at " << rate_per_sec
              << " handles/sec for " << duration_s << "s...\n";

    size_t leaked_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(duration_s);

    while (std::chrono::steady_clock::now() < end_time) {
        for (uint32_t i = 0; i < rate_per_sec / 10; ++i) {
#ifdef _WIN32
            // CreateEventW: Creates an anonymous kernel event object without closing the handle
            HANDLE h = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (h != nullptr && h != INVALID_HANDLE_VALUE) {
                leaked_count++;
            }
#else
            // POSIX fallback: create anonymous pipe and leak read/write descriptors
            int fds[2];
            if (pipe(fds) == 0) {
                leaked_count += 2;
            }
#endif
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[FaultGen: handle_leak] Leaked " << leaked_count
              << " handles. Retaining handles and sleeping for 30s...\n";
    std::this_thread::sleep_for(std::chrono::seconds(30));

    return 0;
}
