#include "config.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static void parse_triggers(Config& config, const std::string& value) {
    config.triggers.clear();
    std::string remaining = value;
    size_t pos;
    while ((pos = remaining.find(',')) != std::string::npos) {
        std::string token = trim(remaining.substr(0, pos));
        if (!token.empty()) config.triggers.push_back(token);
        remaining.erase(0, pos + 1);
    }
    std::string last = trim(remaining);
    if (!last.empty()) config.triggers.push_back(last);
}

static bool load_config_file(Config& config, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open config file: " << path << "\n";
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "log") {
            config.log_path = value;
        } else if (key == "webhook") {
            config.webhook_url = value;
        } else if (key == "triggers") {
            parse_triggers(config, value);
        } else if (key == "window") {
            config.window_size = std::stoul(value);
        } else if (key == "trailing") {
            config.trailing_lines = std::stoul(value);
        } else if (key == "incidents") {
            config.incident_dir = value;
        } else if (key == "user") {
            config.drop_user = value;
        } else {
            std::cerr << "Warning: Unknown config key: " << key << "\n";
        }
    }
    return true;
}

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [options]\n\n"
              << "Required (unless --config is used):\n"
              << "  --log <path>          Path to the log file to monitor\n"
              << "  --webhook <url>       Webhook URL for alerts\n\n"
              << "Optional:\n"
              << "  --config <file>       Path to config file (key=value format)\n"
              << "  --triggers <k1,k2>    Comma-separated keywords or regex (default: FATAL,ERROR)\n"
              << "  --window <n>          Lines to keep in buffer (default: 50)\n"
              << "  --trailing <n>        Lines to capture after trigger (default: 10)\n"
              << "  --incidents <dir>     Directory for incident reports (default: ./incidents)\n"
              << "  --user <user>         Drop to this user after opening log file\n"
              << "  --status              Print current config and exit\n\n"
              << "Config file example:\n"
              << "  log = /var/log/syslog\n"
              << "  webhook = https://hooks.slack.com/services/xxx\n"
              << "  triggers = error,fail,fatal,panic,oom\n"
              << "  window = 100\n"
              << "  trailing = 20\n"
              << "  incidents = /var/log/shockwake-incidents\n"
              << "  user = syslog\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;
    std::string config_path;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    if (!config_path.empty()) {
        if (!load_config_file(config, config_path))
            exit(1);
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) { i++; continue; }
        if (strcmp(argv[i], "--status") == 0) {
            config.status_mode = true;
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            config.log_path = argv[++i];
        } else if (strcmp(argv[i], "--webhook") == 0 && i + 1 < argc) {
            config.webhook_url = argv[++i];
        } else if (strcmp(argv[i], "--window") == 0 && i + 1 < argc) {
            config.window_size = std::stoul(argv[++i]);
        } else if (strcmp(argv[i], "--trailing") == 0 && i + 1 < argc) {
            config.trailing_lines = std::stoul(argv[++i]);
        } else if (strcmp(argv[i], "--incidents") == 0 && i + 1 < argc) {
            config.incident_dir = argv[++i];
        } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
            config.drop_user = argv[++i];
        } else if (strcmp(argv[i], "--triggers") == 0 && i + 1 < argc) {
            parse_triggers(config, argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
    }

    if (config.incident_dir.empty())
        config.incident_dir = "./incidents";
    if (config.triggers.empty())
        config.triggers = {"FATAL", "ERROR"};

    return config;
}
