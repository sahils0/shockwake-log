#pragma once

#include <string>
#include <vector>

// Config holds CLI arguments and runtime settings
// Parsed from command line: log path, webhook URL, window size, trigger keywords
struct Config
{
    std::string log_path;
    std::string webhook_url;
    std::string incident_dir;
    size_t window_size = 50;
    size_t trailing_lines = 10;
    std::vector<std::string> triggers;
};

Config parse_args(int argc, char *argv[]);
