#pragma once

#include "config.hpp"
#include "result.hpp"

namespace syspulse {

Result<Config, std::string> parse_cli(int argc, char* argv[]);
void print_usage(const char* prog_name);

} // namespace syspulse
