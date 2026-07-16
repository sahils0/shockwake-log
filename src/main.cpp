#include "config.h"
#include "ring_buffer.h"
#include "inotify_watcher.h"
#include "log_scanner.h"
#include "webhook_alert.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <stdexcept>
#include <sys/stat.h>

namespace fs = std::filesystem;

std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", localtime(&time));
    return buf;
}

std::string escape_json(const std::string& s) {
    std::string result;
    for (char c : s) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:   result += c;
        }
    }
    return result;
}

void write_incident(const Config& config, const std::string& keyword,
                    const std::vector<std::string>& context,
                    const std::vector<std::string>& trailing) {
    fs::create_directories(config.incident_dir);

    std::string filename = config.incident_dir + "/incident_" + get_timestamp() + ".log";
    std::ofstream out(filename);

    out << "=== SHOCKWAKE-LOG INCIDENT REPORT ===\n";
    out << "Timestamp: " << get_timestamp() << "\n";
    out << "Trigger: " << keyword << "\n";
    out << "Log File: " << config.log_path << "\n";
    out << "=====================================\n\n";

    out << "--- PRE-TRIGGER CONTEXT ---\n";
    for (const auto& line : context)
        out << line << "\n";

    out << "\n--- TRIGGER LINE ---\n";
    out << ">>> " << keyword << " DETECTED <<<\n";

    out << "\n--- POST-TRIGGER CONTEXT ---\n";
    for (const auto& line : trailing)
        out << line << "\n";

    out.close();
    std::cout << "[INCIDENT] Saved: " << filename << "\n";
}

std::string build_webhook_payload(const std::string& keyword,
                                   const std::string& log_file,
                                   const std::string& incident_file) {
    std::ostringstream json;
    json << "{\"content\": \"";
    json << "🚨 **SHOCKWAKE-LOG ALERT**\\n";
    json << "Trigger: " << escape_json(keyword) << "\\n";
    json << "Log: " << escape_json(log_file) << "\\n";
    json << "Incident: " << escape_json(incident_file) << "\"";
    json << "}";
    return json.str();
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: No arguments provided. Use --help for usage.\n";
        return 1;
    }

    Config config = parse_args(argc, argv);

    if (config.log_path.empty()) {
        std::cerr << "Error: --log is required.\n";
        return 1;
    }
    if (config.webhook_url.empty()) {
        std::cerr << "Error: --webhook is required.\n";
        return 1;
    }

    std::cout << "[SHOCKWAKE-LOG] Starting...\n";
    std::cout << "  Log file:    " << config.log_path << "\n";
    std::cout << "  Webhook:     " << config.webhook_url << "\n";
    std::cout << "  Window:      " << config.window_size << " lines\n";
    std::cout << "  Trailing:    " << config.trailing_lines << " lines\n";
    std::cout << "  Triggers:    ";
    for (size_t i = 0; i < config.triggers.size(); i++) {
        std::cout << config.triggers[i];
        if (i < config.triggers.size() - 1) std::cout << ", ";
    }
    std::cout << "\n";

    // Initialize components
    RingBuffer buffer(config.window_size);
    LogScanner scanner;
    scanner.set_triggers(config.triggers);

    struct stat st;
    if (stat(config.log_path.c_str(), &st) != 0) {
        std::cerr << "Error: Log file does not exist: " << config.log_path << "\n";
        return 1;
    }

    InotifyWatcher watcher(config.log_path);

    WebhookAlert alerter(config.webhook_url);
    alerter.start();

    std::cout << "[SHOCKWAKE-LOG] Watching for changes...\n\n";

    // Main loop
    while (true) {
        std::string new_content = watcher.wait_for_changes();
        if (new_content.empty())
            continue;

        // Split content into lines
        std::istringstream stream(new_content);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.empty()) continue;

            buffer.push(line);

            if (scanner.scan(line)) {
                std::string keyword = scanner.matched_keyword();
                std::cout << "[TRIGGER] " << keyword << " detected!\n";

                // Get context
                auto pre_context = buffer.snapshot();
                auto post_context = buffer.tail(config.trailing_lines);

                // Write incident report
                write_incident(config, keyword, pre_context, post_context);

                // Fire webhook alert
                std::string incident_file = config.incident_dir + "/incident_" + get_timestamp() + ".log";
                std::string payload = build_webhook_payload(keyword, config.log_path, incident_file);
                alerter.enqueue(payload);

                std::cout << "[ALERT] Webhook fired.\n\n";
            }
        }
    }

    alerter.stop();
    return 0;
}
