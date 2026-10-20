# SysPulse Debugging & Systems Diagnostics Playbook

This document provides exact, reproducible step-by-step diagnostic workflows for investigating memory leaks, memory corruptions, and process crashes across Linux and Windows using GDB, Valgrind, and WinDbg.

---

## 1. Linux Diagnostics with GDB (GNU Debugger)

### Workflow 1.1: Post-Mortem Core Dump Analysis on `crash_test`
When a process terminates abnormally (e.g. `SIGSEGV` or `SIGABRT`), the Linux kernel can emit an ELF core dump recording the complete virtual memory image and thread register states.

```bash
# 1. Enable core dumps without file size limit
ulimit -c unlimited

# 2. Trigger a null pointer dereference crash
./build/crash_test --type null

# 3. Open core dump with GDB
gdb ./build/crash_test core

# 4. Interactive GDB diagnostic commands:
(gdb) bt full            # Display backtrace with local variables in all stack frames
(gdb) info registers     # Inspect CPU registers ($rip, $rsp, $rax, etc.)
(gdb) x/1i $rip          # Disassemble the exact failing instruction
(gdb) list               # Show source code around the crash site
(gdb) thread apply all bt # Print backtraces across all active threads
(gdb) quit
```

#### Findings:
`<<FILL IN AFTER RUNNING>>`

---

### Workflow 1.2: Hardware Watchpoints for Memory Leaks in `mem_leak`
Track exact instructions modifying pointer references or heap tracking indices.

```bash
# 1. Launch mem_leak under GDB
gdb --args ./build/mem_leak --rate-mb 5 --interval-ms 100 --duration-s 5

# 2. Set breakpoint at main and run
(gdb) break main
(gdb) run

# 3. Set a hardware watchpoint on the leaked bytes counter
(gdb) watch total_leaked_bytes

# 4. Continue execution - GDB pauses execution every time the value changes
(gdb) continue
Hardware watchpoint 2: total_leaked_bytes
Old value = 0
New value = 524288
(gdb) bt
(gdb) quit
```

#### Findings:
`<<FILL IN AFTER RUNNING>>`

---

## 2. Memory Profiling with Valgrind

### Workflow 2.1: Valgrind Memcheck (Heap Leak & Invalid Access Detection)
Memcheck instruments all memory reads, writes, and allocations with shadow memory tracking.

```bash
# Run Memcheck with full leak detailing and track-origins enabled
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=memcheck.log \
         ./build/mem_leak --rate-mb 10 --duration-s 3

# Inspect the resulting log
cat memcheck.log
```

Expected Output Structure:
* `definitely lost`: Memory where no pointer points to the allocation block.
* `indirectly lost`: Structure nodes whose root pointer was lost.
* `still reachable`: Allocations not freed prior to `exit()`.

#### Findings:
`<<FILL IN AFTER RUNNING>>`

---

### Workflow 2.2: Valgrind Massif (Heap Memory Profiler)
Massif takes periodic snapshots of heap usage over time, measuring memory growth velocity.

```bash
# Profile heap usage over time
valgrind --tool=massif --massif-out-file=massif.out ./build/mem_leak --rate-mb 15 --duration-s 5

# Print visual ASCII call graph showing allocation peaks
ms_print massif.out | head -n 40
```

#### Findings:
`<<FILL IN AFTER RUNNING>>`

---

## 3. Windows Diagnostics with WinDbg

### Workflow 3.1: Crash Dump Analysis (`!analyze -v`)
When Windows processes fault with `EXCEPTION_ACCESS_VIOLATION` (`0xC0000005`) or heap corruption:

1. Launch WinDbg (Windows Debugger).
2. Open crash dump: **File** -> **Open Dump File** (`crash_test.dmp`) or CLI:
   ```cmd
   windbg.exe -z crash_test.dmp
   ```
3. Configure Microsoft public symbol server:
   ```windbg
   .sympath srv*C:\Symbols*https://msdl.microsoft.com/download/symbols
   .reload /f
   ```
4. Run automated heuristic analysis:
   ```windbg
   !analyze -v
   ```
   Inspect `FAULTING_IP`, `EXCEPTION_CODE_STR`, and `STACK_TEXT`.
5. Examine call stack and parameters:
   ```windbg
   kP                     # Call stack with formatted parameters
   r                      # CPU register context
   dv                     # Display local variable values
   ```

#### Findings:
`<<FILL IN AFTER RUNNING>>`

---

### Workflow 3.2: WinDbg Heap & Virtual Memory Triage (`!heap`, `!address`)
Diagnose Windows native heap usage and private committed virtual memory.

1. Enable Page Heap on the target application:
   ```cmd
   gflags.exe /p /enable mem_leak.exe /full
   ```
2. Attach WinDbg to `mem_leak.exe`:
   ```windbg
   !heap -s               # Summary of all process heaps (Default Process Heap, CRT Heap)
   !heap -stat -h 0       # Allocation statistics by size bucket
   !address -summary      # Memory classification: Committed, Reserved, Free, Image, Stack, Heap
   !handle 0 f            # Dump open handle table (for verifying handle_leak)
   ```

#### Findings:
`<<FILL IN AFTER RUNNING>>`
