#pragma once

#include <cstdint>
#include <optional>
#include <regex>
#include <string>

namespace syspulse {

enum class OutputFormat {
    Ndjson,
    Csv
};

struct Config {
    uint32_t interval_ms{500};
    uint32_t duration_s{60};
    std::string out_path{"samples.ndjson"};
    OutputFormat format{OutputFormat::Ndjson};
    uint32_t top_n{20};
    std::string filter_name{};
    std::optional<std::regex> name_regex{};
    std::optional<int64_t> target_pid{};
    bool self_stats{false};
    uint64_t max_file_size_bytes{50 * 1024 * 1024}; // 50MB rotation threshold
    std::string proc_root{"/proc"};                 // Configurable procfs root (for fixtures/testing)
};

} // namespace syspulse
