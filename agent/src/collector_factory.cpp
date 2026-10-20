#include "syspulse/collector.hpp"
#include "syspulse/proc_collector.hpp"

#ifdef _WIN32
    #include "syspulse/windows_collectors.hpp"
#endif

namespace syspulse {

std::unique_ptr<ICollector> ICollector::create(const std::string& proc_root) {
#ifdef _WIN32
    // If proc_root is customized (e.g. for testing proc fixtures on Windows), use ProcCollector!
    if (proc_root != "/proc") {
        return std::make_unique<ProcCollector>(proc_root);
    }
    return std::make_unique<WinProcessCollector>();
#else
    return std::make_unique<ProcCollector>(proc_root);
#endif
}

} // namespace syspulse
