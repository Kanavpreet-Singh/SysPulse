#include "syspulse/config.hpp"
#include "result.hpp"
#include <iostream>
#include <sstream>

namespace syspulse {

Result<Config, std::string> parse_cli(int argc, char* argv[]) {
    Config cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            return Error<Config>("HELP_REQUESTED");
        } else if (arg == "--interval-ms") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --interval-ms");
            cfg.interval_ms = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--duration-s") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --duration-s");
            cfg.duration_s = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--out") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --out");
            cfg.out_path = argv[++i];
        } else if (arg == "--format") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --format");
            std::string fmt = argv[++i];
            if (fmt == "ndjson") {
                cfg.format = OutputFormat::Ndjson;
            } else if (fmt == "csv") {
                cfg.format = OutputFormat::Csv;
            } else {
                return Error<Config>("Unknown format '" + fmt + "'. Expected 'ndjson' or 'csv'");
            }
        } else if (arg == "--top-n") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --top-n");
            cfg.top_n = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--filter-name") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --filter-name");
            cfg.filter_name = argv[++i];
            try {
                cfg.name_regex = std::regex(cfg.filter_name, std::regex_constants::icase);
            } catch (const std::regex_error& e) {
                return Error<Config>("Invalid regex pattern for --filter-name: " + std::string(e.what()));
            }
        } else if (arg == "--pid") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --pid");
            cfg.target_pid = std::stoll(argv[++i]);
        } else if (arg == "--self-stats") {
            cfg.self_stats = true;
        } else if (arg == "--max-file-size-mb") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --max-file-size-mb");
            cfg.max_file_size_bytes = std::stoull(argv[++i]) * 1024 * 1024;
        } else if (arg == "--proc-root") {
            if (i + 1 >= argc) return Error<Config>("Missing value for --proc-root");
            cfg.proc_root = argv[++i];
        } else {
            return Error<Config>("Unrecognized command-line argument: " + arg);
        }
    }

    return Ok(cfg);
}

void print_usage(const char* prog_name) {
    std::cout << "SysPulse - Telemetry Agent in Modern C++17\n"
              << "Usage: " << prog_name << " [options]\n\n"
              << "Options:\n"
              << "  --interval-ms <ms>       Sampling interval in milliseconds (default: 500)\n"
              << "  --duration-s <sec>       Execution duration in seconds (default: 60, 0 = infinite)\n"
              << "  --out <path>             Output file path (default: samples.ndjson)\n"
              << "  --format <ndjson|csv>    Output format (default: ndjson)\n"
              << "  --top-n <count>          Capture top N processes sorted by CPU% (default: 20, 0 = all)\n"
              << "  --filter-name <regex>    Filter processes by regex name pattern\n"
              << "  --pid <pid>              Filter specific process by PID\n"
              << "  --self-stats             Record agent's own CPU% and RSS memory overhead\n"
              << "  --max-file-size-mb <mb>  Log rotation threshold in megabytes (default: 50)\n"
              << "  --proc-root <path>       Procfs directory path (default: /proc)\n"
              << "  --help, -h               Show this help message\n";
}

} // namespace syspulse
