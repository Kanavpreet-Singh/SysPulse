#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace syspulse {

/**
 * System-wide metrics snapshot.
 */
struct SystemMetrics {
    double cpu_total_pct{0.0};
    std::vector<double> per_core_pct{};
    uint64_t mem_total_bytes{0};
    uint64_t mem_available_bytes{0};
    uint64_t swap_used_bytes{0};
    uint64_t context_switches{0};
    uint64_t page_faults{0};
};

/**
 * Per-process metrics snapshot.
 */
struct ProcessMetrics {
    int64_t pid{0};
    int64_t ppid{0};
    std::string name{};
    double cpu_pct{0.0};
    uint64_t rss_bytes{0};         // Linux: RSS * page_size, Windows: WorkingSetSize
    uint64_t virtual_bytes{0};     // Linux: VmSize, Windows: PagefileUsage / PrivateUsage
    uint32_t thread_count{0};
    uint32_t handle_or_fd_count{0};// Linux: open file descriptors, Windows: HandleCount
    uint64_t io_read_bytes{0};
    uint64_t io_write_bytes{0};
    uint64_t page_faults{0};
};

/**
 * Self-diagnostics: Telemetry agent's own footprint.
 */
struct AgentSelfStats {
    double cpu_pct{0.0};
    uint64_t rss_bytes{0};
    size_t ring_buffer_dropped_samples{0};
    size_t arena_high_water_mark{0};
};

/**
 * Event record for ETW or system events (e.g. process start/exit).
 */
struct EventRecord {
    uint64_t timestamp_ns{0};
    std::string provider_name{};
    std::string event_type{};      // e.g. "ProcessStart", "ProcessStop", "PageFault"
    int64_t pid{0};
    std::string details{};
};

/**
 * Complete consolidated sample emitted periodically.
 */
struct Sample {
    uint64_t timestamp_ns{0};      // UTC nanoseconds since UNIX epoch
    std::string hostname{};
    SystemMetrics system{};
    std::vector<ProcessMetrics> processes{};
    AgentSelfStats self_stats{};
    std::vector<EventRecord> events{};
};

// nlohmann::json serialization support
inline void to_json(nlohmann::json& j, const SystemMetrics& m) {
    j = nlohmann::json{
        {"cpu_total_pct", m.cpu_total_pct},
        {"per_core_pct", m.per_core_pct},
        {"mem_total_bytes", m.mem_total_bytes},
        {"mem_available_bytes", m.mem_available_bytes},
        {"swap_used_bytes", m.swap_used_bytes},
        {"context_switches", m.context_switches},
        {"page_faults", m.page_faults}
    };
}

inline void from_json(const nlohmann::json& j, SystemMetrics& m) {
    j.at("cpu_total_pct").get_to(m.cpu_total_pct);
    if (j.contains("per_core_pct")) j.at("per_core_pct").get_to(m.per_core_pct);
    j.at("mem_total_bytes").get_to(m.mem_total_bytes);
    j.at("mem_available_bytes").get_to(m.mem_available_bytes);
    if (j.contains("swap_used_bytes")) j.at("swap_used_bytes").get_to(m.swap_used_bytes);
    if (j.contains("context_switches")) j.at("context_switches").get_to(m.context_switches);
    if (j.contains("page_faults")) j.at("page_faults").get_to(m.page_faults);
}

inline void to_json(nlohmann::json& j, const ProcessMetrics& p) {
    j = nlohmann::json{
        {"pid", p.pid},
        {"ppid", p.ppid},
        {"name", p.name},
        {"cpu_pct", p.cpu_pct},
        {"rss_bytes", p.rss_bytes},
        {"virtual_bytes", p.virtual_bytes},
        {"thread_count", p.thread_count},
        {"handle_or_fd_count", p.handle_or_fd_count},
        {"io_read_bytes", p.io_read_bytes},
        {"io_write_bytes", p.io_write_bytes},
        {"page_faults", p.page_faults}
    };
}

inline void from_json(const nlohmann::json& j, ProcessMetrics& p) {
    j.at("pid").get_to(p.pid);
    j.at("ppid").get_to(p.ppid);
    j.at("name").get_to(p.name);
    j.at("cpu_pct").get_to(p.cpu_pct);
    j.at("rss_bytes").get_to(p.rss_bytes);
    j.at("virtual_bytes").get_to(p.virtual_bytes);
    j.at("thread_count").get_to(p.thread_count);
    j.at("handle_or_fd_count").get_to(p.handle_or_fd_count);
    if (j.contains("io_read_bytes")) j.at("io_read_bytes").get_to(p.io_read_bytes);
    if (j.contains("io_write_bytes")) j.at("io_write_bytes").get_to(p.io_write_bytes);
    if (j.contains("page_faults")) j.at("page_faults").get_to(p.page_faults);
}

inline void to_json(nlohmann::json& j, const AgentSelfStats& s) {
    j = nlohmann::json{
        {"cpu_pct", s.cpu_pct},
        {"rss_bytes", s.rss_bytes},
        {"ring_buffer_dropped_samples", s.ring_buffer_dropped_samples},
        {"arena_high_water_mark", s.arena_high_water_mark}
    };
}

inline void from_json(const nlohmann::json& j, AgentSelfStats& s) {
    if (j.contains("cpu_pct")) j.at("cpu_pct").get_to(s.cpu_pct);
    if (j.contains("rss_bytes")) j.at("rss_bytes").get_to(s.rss_bytes);
    if (j.contains("ring_buffer_dropped_samples")) j.at("ring_buffer_dropped_samples").get_to(s.ring_buffer_dropped_samples);
    if (j.contains("arena_high_water_mark")) j.at("arena_high_water_mark").get_to(s.arena_high_water_mark);
}

inline void to_json(nlohmann::json& j, const EventRecord& e) {
    j = nlohmann::json{
        {"timestamp_ns", e.timestamp_ns},
        {"provider_name", e.provider_name},
        {"event_type", e.event_type},
        {"pid", e.pid},
        {"details", e.details}
    };
}

inline void from_json(const nlohmann::json& j, EventRecord& e) {
    j.at("timestamp_ns").get_to(e.timestamp_ns);
    j.at("provider_name").get_to(e.provider_name);
    j.at("event_type").get_to(e.event_type);
    j.at("pid").get_to(e.pid);
    if (j.contains("details")) j.at("details").get_to(e.details);
}

inline void to_json(nlohmann::json& j, const Sample& s) {
    j = nlohmann::json{
        {"timestamp_ns", s.timestamp_ns},
        {"hostname", s.hostname},
        {"system", s.system},
        {"processes", s.processes},
        {"self_stats", s.self_stats},
        {"events", s.events}
    };
}

inline void from_json(const nlohmann::json& j, Sample& s) {
    j.at("timestamp_ns").get_to(s.timestamp_ns);
    j.at("hostname").get_to(s.hostname);
    j.at("system").get_to(s.system);
    if (j.contains("processes")) j.at("processes").get_to(s.processes);
    if (j.contains("self_stats")) j.at("self_stats").get_to(s.self_stats);
    if (j.contains("events")) j.at("events").get_to(s.events);
}

} // namespace syspulse
