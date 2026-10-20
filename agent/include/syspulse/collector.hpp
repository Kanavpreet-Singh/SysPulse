#pragma once

#include "model.hpp"
#include "result.hpp"
#include <memory>
#include <string>
#include <vector>

namespace syspulse {

/**
 * Abstract collector interface for OS telemetry.
 */
class ICollector {
public:
    virtual ~ICollector() = default;

    /**
     * Collect system-wide metrics (CPU, memory, swap, page faults, context switches).
     */
    virtual Result<SystemMetrics, std::string> collect_system() = 0;

    /**
     * Collect per-process metrics across all running processes.
     */
    virtual Result<std::vector<ProcessMetrics>, std::string> collect_processes() = 0;

    /**
     * Factory function to instantiate the active platform collector.
     * On Linux, proc_root defaults to "/proc".
     * On Windows, instantiates WinProcessCollector / PdhCollector.
     */
    static std::unique_ptr<ICollector> create(const std::string& proc_root = "/proc");
};

} // namespace syspulse
