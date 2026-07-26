#pragma once

#include <string>
#include <vector>

enum class SubCommand {
    MONITOR,    // default: run monitoring loop
    INIT,       // generate config file
    STATUS,     // print config and exit
    INCIDENTS,  // list incident reports
    LOGS,       // follow latest incident file
    CLEAN,      // stop and remove generated files
    STOP,       // stop a running instance
};

struct Config {
    SubCommand subcommand = SubCommand::MONITOR;
    std::string log_path;
    std::string webhook_url;
    std::string incident_dir;
    std::string drop_user;
    std::string config_path;
    std::string pid_file;
    size_t window_size = 100;
    size_t trailing_lines = 20;
    size_t max_line_length = 8192;
    int max_retries = 3;
    int retry_delay_ms = 1000;
    int cooldown_seconds = 60;
    bool ssl_verify = true;
    std::vector<std::string> triggers;
    std::vector<std::string> excludes;
};

Config parse_args(int argc, char *argv[]);
