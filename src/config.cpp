#include "config.h"
#include <iostream>
#include <cstring>

void print_usage(const char* program) {
    std::cerr << "Usage: " << program << " [options]\n\n"
              << "Required:\n"
              << "  --log <path>          Path to the log file to monitor\n"
              << "  --webhook <url>       Webhook URL for alerts\n\n"
              << "Optional:\n"
              << "  --triggers <k1,k2>    Comma-separated keywords (default: FATAL,ERROR)\n"
              << "  --window <n>          Lines to keep in buffer (default: 50)\n"
              << "  --trailing <n>        Lines to capture after trigger (default: 10)\n"
              << "  --incidents <dir>     Directory for incident reports (default: ./incidents)\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; i++) {
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
        } else if (strcmp(argv[i], "--triggers") == 0 && i + 1 < argc) {
            std::string triggers_str = argv[++i];
            size_t pos = 0;
            while ((pos = triggers_str.find(',')) != std::string::npos) {
                config.triggers.push_back(triggers_str.substr(0, pos));
                triggers_str.erase(0, pos + 1);
            }
            config.triggers.push_back(triggers_str);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
    }

    // Defaults
    if (config.incident_dir.empty())
        config.incident_dir = "./incidents";
    if (config.triggers.empty())
        config.triggers = {"FATAL", "ERROR"};

    return config;
}
