#include "config.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <pwd.h>
#include <unistd.h>

namespace fs = std::filesystem;

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

static void parse_csv(const std::string& value, std::vector<std::string>& out) {
    out.clear();
    std::string remaining = value;
    size_t pos;
    while ((pos = remaining.find(',')) != std::string::npos) {
        std::string token = trim(remaining.substr(0, pos));
        if (!token.empty()) out.push_back(token);
        remaining.erase(0, pos + 1);
    }
    std::string last = trim(remaining);
    if (!last.empty()) out.push_back(last);
}

static bool load_config_file(Config& config, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

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
        } else if (key == "excludes") {
            parse_csv(value, config.excludes);
        } else if (key == "retries") {
            config.max_retries = std::stoi(value);
        } else if (key == "retry_delay") {
            config.retry_delay_ms = std::stoi(value);
        } else if (key == "cooldown") {
            config.cooldown_seconds = std::stoi(value);
        } else if (key == "max_line_length") {
            config.max_line_length = std::stoul(value);
        } else if (key == "ssl_verify") {
            config.ssl_verify = (value == "true" || value == "1" || value == "yes");
        }
    }
    return true;
}

static std::string get_home_dir() {
    const char* home = getenv("HOME");
    if (home) return home;
    struct passwd* pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;
    return "";
}

static std::string find_config_file() {
    std::string home = get_home_dir();

    std::vector<std::string> candidates;

    // Current directory
    candidates.push_back("./.swl.conf");
    candidates.push_back("./swl.conf");

    // User config dir
    if (!home.empty()) {
        candidates.push_back(home + "/.config/swl/config");
    }

    // System config
    candidates.push_back("/etc/swl/config");

    for (const auto& path : candidates) {
        if (fs::exists(path)) return path;
    }
    return "";
}

static std::string discover_log_file() {
    // Local logs first (current directory)
    std::vector<std::string> local_patterns = {
        "./*.log", "./logs/*.log", "./log/*.log"
    };

    for (const auto& pattern : local_patterns) {
        // Use glob-like manual check since C++ filesystem doesn't support glob
        std::string dir;
        std::string prefix;
        size_t slash_pos = pattern.rfind('/');
        if (slash_pos != std::string::npos) {
            dir = pattern.substr(0, slash_pos);
            prefix = pattern.substr(slash_pos + 1);
        } else {
            dir = ".";
            prefix = pattern;
        }

        if (!fs::exists(dir) || !fs::is_directory(dir)) continue;

        for (const auto& entry : fs::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            std::string name = entry.path().filename().string();
            if (name.size() > 4 && name.substr(name.size() - 4) == ".log") {
                if (fs::exists(entry.path()) && (static_cast<int>(fs::status(entry.path()).permissions()) & static_cast<int>(fs::perms::owner_read))) {
                    return entry.path().string();
                }
            }
        }
    }

    // System logs
    std::vector<std::string> system_paths = {
        "/var/log/syslog",
        "/var/log/messages",
        "/var/log/kern.log",
        "/var/log/auth.log",
        "/var/log/nginx/error.log",
        "/var/log/docker.log",
    };

    for (const auto& path : system_paths) {
        if (fs::exists(path) && (static_cast<int>(fs::status(path).permissions()) & static_cast<int>(fs::perms::owner_read))) {
            return path;
        }
    }

    return "";
}

static std::string find_pid_file() {
    // Check common locations
    std::vector<std::string> candidates = {
        "./.swl.pid",
    };

    for (const auto& path : candidates) {
        if (fs::exists(path)) return path;
    }
    return "";
}

static SubCommand detect_subcommand(const std::string& arg) {
    if (arg == "init") return SubCommand::INIT;
    if (arg == "status") return SubCommand::STATUS;
    if (arg == "incidents") return SubCommand::INCIDENTS;
    if (arg == "logs") return SubCommand::LOGS;
    if (arg == "clean") return SubCommand::CLEAN;
    if (arg == "stop") return SubCommand::STOP;
    return SubCommand::MONITOR;
}

void print_usage(const char* program) {
    std::string name = fs::path(program).filename().string();

    std::cerr << "swl — Real-time log anomaly detection with contextual debugging.\n\n"
              << "Usage:\n"
              << "  " << name << "                         Auto-discover and monitor logs (sensible defaults)\n"
              << "  " << name << " <logfile>               Monitor a specific log file\n"
              << "  " << name << " [options]               Monitor with custom options\n\n"
              << "Subcommands:\n"
              << "  " << name << " init                  Generate config file in current directory\n"
              << "  " << name << " status                Print current config and exit\n"
              << "  " << name << " incidents             List recent incident reports\n"
              << "  " << name << " logs                  Follow latest incident file\n"
              << "  " << name << " stop                  Stop a running instance\n"
              << "  " << name << " clean                 Stop and remove generated files\n\n"
              << "Options:\n"
              << "  --log <path>          Log file to monitor (auto-discovered if omitted)\n"
              << "  --webhook <url>       Webhook URL for alerts (omit for local-only logging)\n"
              << "  --config <file>       Path to config file (auto-loaded from standard locations)\n"
              << "  --triggers <k1,k2>    Comma-separated keywords or regex (default: FATAL,ERROR)\n"
              << "  --exclude <e1,e2>     Exclusion patterns — lines matching these are skipped\n"
              << "  --window <n>          Lines to keep in context buffer (default: 100)\n"
              << "  --trailing <n>        Lines to capture after trigger (default: 20)\n"
              << "  --incidents <dir>     Directory for incident reports (default: ./incidents)\n"
              << "  --cooldown <sec>      Min seconds between alerts per trigger (default: 60)\n"
              << "  --retries <n>         Max webhook retry attempts (default: 3, 0 = no retry)\n"
              << "  --retry-delay <ms>    Delay between retries in ms (default: 1000)\n"
              << "  --no-ssl-verify       Disable SSL certificate verification (for self-signed certs)\n"
              << "  --max-line-length <n> Max characters per log line (default: 8192, 0 = unlimited)\n"
              << "  --user <user>         Drop to this user after opening log (requires root)\n"
              << "  --pid-file <path>     PID file for stop/clean commands\n"
              << "  --help, -h            Show this help\n\n"
              << "Quick Start:\n"
              << "  " << name << " init                    # Generate config file\n"
              << "  " << name << "                         # Start monitoring with auto-discovery\n"
              << "  " << name << " /var/log/syslog         # Monitor specific file\n"
              << "  " << name << " --webhook URL /var/log/app.log\n\n"
              << "Config File (auto-loaded from):\n"
              << "  ./.swl.conf, ~/.config/swl/config, /etc/swl/config\n\n"
              << "Config File Example:\n"
              << "  log = /var/log/syslog\n"
              << "  webhook = https://hooks.slack.com/services/xxx\n"
              << "  triggers = FATAL,ERROR,WARN\n"
              << "  excludes = DEBUG,healthcheck\n"
              << "  window = 100\n"
              << "  trailing = 20\n"
              << "  incidents = ./incidents\n"
              << "  cooldown = 60\n"
              << "  retries = 3\n"
              << "  retry_delay = 1000\n"
              << "  user = syslog\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;

    if (argc < 2) {
        // No args: auto-discover and run with defaults
        config.log_path = discover_log_file();
        config.config_path = find_config_file();
        if (!config.config_path.empty()) {
            load_config_file(config, config.config_path);
        }
        if (config.triggers.empty())
            config.triggers = {"FATAL", "ERROR"};
        if (config.incident_dir.empty())
            config.incident_dir = "./incidents";
        return config;
    }

    // Check if first arg is a subcommand or a flag or a file
    std::string first = argv[1];
    if (first == "--help" || first == "-h") {
        print_usage(argv[0]);
        exit(0);
    }

    // Detect subcommand (must be first non-flag arg)
    if (!first.empty() && first[0] != '-') {
        SubCommand sc = detect_subcommand(first);
        if (sc != SubCommand::MONITOR) {
            config.subcommand = sc;
            // Parse remaining args for subcommands that need them
            for (int i = 2; i < argc; i++) {
                if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
                    config.config_path = argv[++i];
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
                } else if (strcmp(argv[i], "--triggers") == 0 && i + 1 < argc) {
                    parse_triggers(config, argv[++i]);
                } else if (strcmp(argv[i], "--exclude") == 0 && i + 1 < argc) {
                    parse_csv(argv[++i], config.excludes);
                } else if (strcmp(argv[i], "--cooldown") == 0 && i + 1 < argc) {
                    config.cooldown_seconds = std::stoi(argv[++i]);
                } else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc) {
                    config.drop_user = argv[++i];
                } else if (strcmp(argv[i], "--retries") == 0 && i + 1 < argc) {
                    config.max_retries = std::stoi(argv[++i]);
                } else if (strcmp(argv[i], "--retry-delay") == 0 && i + 1 < argc) {
                    config.retry_delay_ms = std::stoi(argv[++i]);
                } else if (strcmp(argv[i], "--ssl-verify") == 0 && i + 1 < argc) {
                    std::string val = argv[++i];
                    config.ssl_verify = (val == "true" || val == "1" || val == "yes");
                } else if (strcmp(argv[i], "--no-ssl-verify") == 0) {
                    config.ssl_verify = false;
                } else if (strcmp(argv[i], "--max-line-length") == 0 && i + 1 < argc) {
                    config.max_line_length = std::stoul(argv[++i]);
                } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
                    config.pid_file = argv[++i];
                } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
                    print_usage(argv[0]);
                    exit(0);
                }
            }

            // For status: load config if available
            if (config.subcommand == SubCommand::STATUS) {
                if (config.config_path.empty())
                    config.config_path = find_config_file();
                if (!config.config_path.empty())
                    load_config_file(config, config.config_path);
                if (config.triggers.empty())
                    config.triggers = {"FATAL", "ERROR"};
                if (config.incident_dir.empty())
                    config.incident_dir = "./incidents";
            }

            if (config.pid_file.empty())
                config.pid_file = find_pid_file();

            return config;
        }
    }

    // Monitor mode: parse all flags
    // First pass: find config file
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
            config.config_path = argv[++i];
        }
    }

    // Auto-load config from standard locations if not explicitly provided
    if (config.config_path.empty()) {
        config.config_path = find_config_file();
    }

    if (!config.config_path.empty()) {
        load_config_file(config, config.config_path);
    }

    // Second pass: parse CLI flags (override config file)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) { i++; continue; }
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
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
        } else if (strcmp(argv[i], "--exclude") == 0 && i + 1 < argc) {
            parse_csv(argv[++i], config.excludes);
        } else if (strcmp(argv[i], "--retries") == 0 && i + 1 < argc) {
            config.max_retries = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--retry-delay") == 0 && i + 1 < argc) {
            config.retry_delay_ms = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--cooldown") == 0 && i + 1 < argc) {
            config.cooldown_seconds = std::stoi(argv[++i]);
        } else if (strcmp(argv[i], "--ssl-verify") == 0 && i + 1 < argc) {
            std::string val = argv[++i];
            config.ssl_verify = (val == "true" || val == "1" || val == "yes");
        } else if (strcmp(argv[i], "--no-ssl-verify") == 0) {
            config.ssl_verify = false;
        } else if (strcmp(argv[i], "--max-line-length") == 0 && i + 1 < argc) {
            config.max_line_length = std::stoul(argv[++i]);
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            config.pid_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (argv[i][0] != '-') {
            // Treat as logfile (positional arg)
            config.log_path = argv[i];
        }
    }

    // Auto-discover log file if not provided
    if (config.log_path.empty()) {
        config.log_path = discover_log_file();
    }

    // Apply defaults
    if (config.incident_dir.empty())
        config.incident_dir = "./incidents";
    if (config.triggers.empty())
        config.triggers = {"FATAL", "ERROR"};
    if (config.pid_file.empty())
        config.pid_file = find_pid_file();

    return config;
}
