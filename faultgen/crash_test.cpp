/**
 * Diagnostic Target: Process Crashes (Null Pointer, Stack Overflow, Heap Corruption)
 * Detection Tools:
 *   - Linux GDB: Post-mortem core dump analysis (`gdb ./crash_test core`, `bt full`, `info registers`)
 *   - Linux Valgrind: Memcheck (`valgrind ./crash_test --type heap`)
 *   - Windows WinDbg: Crash dump analysis (`windbg -z crash.dmp`, `!analyze -v`, `kP`, `r`)
 *   - Clang/GCC AddressSanitizer / UndefinedBehaviorSanitizer
 *
 * Description:
 * Selectively triggers explicit process crashes to generate deterministic crash dumps
 * for debugger practice and verification of post-mortem diagnostic workflows.
 */

#include <cstring>
#include <iostream>
#include <string>

// Prevent tail-call optimization so recursion consumes stack frames
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#elif defined(_MSC_VER)
__declspec(noinline)
#endif
void recursive_stack_overflow(int depth) {
    volatile char buffer[4096];
    std::memset(const_cast<char*>(buffer), 0xAA, sizeof(buffer));
    // Recursive invocation without base case
    recursive_stack_overflow(depth + 1);
}

void trigger_null_dereference() {
    std::cout << "[FaultGen: crash_test] Dereferencing nullptr...\n";
    volatile int* ptr = nullptr;
    *ptr = 0xDEADBEEF; // Triggers SIGSEGV / EXCEPTION_ACCESS_VIOLATION
}

void trigger_stack_overflow() {
    std::cout << "[FaultGen: crash_test] Inducing infinite stack recursion...\n";
    recursive_stack_overflow(1);
}

void trigger_heap_corruption() {
    std::cout << "[FaultGen: crash_test] Corrupting heap metadata via out-of-bounds write...\n";
    char* buf = new char[32];
    // Write 256 bytes into a 32-byte heap buffer, corrupting adjacent chunk headers
    std::memset(buf, 0xFF, 256);
    delete[] buf; // Trigger heap integrity check or ASan abort
}

int main(int argc, char* argv[]) {
    std::string type = "null";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--type" && i + 1 < argc) {
            type = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: crash_test --type <null|stack|heap>\n";
            return 0;
        }
    }

    if (type == "null") {
        trigger_null_dereference();
    } else if (type == "stack") {
        trigger_stack_overflow();
    } else if (type == "heap") {
        trigger_heap_corruption();
    } else {
        std::cerr << "Unknown crash type: " << type << ". Expected: null, stack, heap\n";
        return 1;
    }

    return 0;
}
