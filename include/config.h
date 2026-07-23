#pragma once

#include <string>
#include <vector>

struct Config {
    std::string log_path;
    std::string webhook_url;
    std::string incident_dir;
    std::string drop_user;
    size_t window_size = 50;
    size_t trailing_lines = 10;
    bool status_mode = false;
    std::vector<std::string> triggers;
    std::vector<std::string> excludes;
};

Config parse_args(int argc, char *argv[]);
