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
#include <atomic>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::atomic<bool> g_running{true};
static std::atomic<unsigned long> g_alert_count{0};
static std::atomic<unsigned long> g_lines_processed{0};

static void signal_handler(int)
{
    g_running = false;
}

std::string get_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", localtime(&time));
    return buf;
}

std::string get_uptime(time_t start)
{
    time_t now = time(nullptr);
    int diff = static_cast<int>(now - start);
    int days = diff / 86400;
    int hours = (diff % 86400) / 3600;
    int mins = (diff % 3600) / 60;
    int secs = diff % 60;

    std::ostringstream ss;
    if (days > 0)
        ss << days << "d ";
    if (hours > 0 || days > 0)
        ss << hours << "h ";
    if (mins > 0 || hours > 0 || days > 0)
        ss << mins << "m ";
    ss << secs << "s";
    return ss.str();
}

std::string escape_json(const std::string &s)
{
    std::string result;
    for (char c : s)
    {
        switch (c)
        {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            result += c;
        }
    }
    return result;
}

void write_incident(const Config &config, const std::string &keyword,
                    const std::vector<std::string> &context,
                    const std::vector<std::string> &trailing)
{
    fs::create_directories(config.incident_dir);

    std::string filename = config.incident_dir + "/incident_" + get_timestamp() + ".log";
    std::ofstream out(filename);

    out << "=== SHOCKWAKE-LOG INCIDENT REPORT ===\n";
    out << "Timestamp: " << get_timestamp() << "\n";
    out << "Trigger: " << keyword << "\n";
    out << "Log File: " << config.log_path << "\n";
    out << "=====================================\n\n";

    out << "--- PRE-TRIGGER CONTEXT ---\n";
    for (const auto &line : context)
        out << line << "\n";

    out << "\n--- TRIGGER LINE ---\n";
    out << ">>> " << keyword << " DETECTED <<<\n";

    out << "\n--- POST-TRIGGER CONTEXT ---\n";
    for (const auto &line : trailing)
        out << line << "\n";

    out.close();
    std::cout << "[INCIDENT] Saved: " << filename << "\n";
}

std::string build_webhook_payload(const std::string &keyword,
                                  const std::string &log_file,
                                  const std::string &incident_file)
{
    std::ostringstream json;
    json << "{\"content\": \"";
    json << "\xF0\x9F\x9A\xA8 **SHOCKWAKE-LOG ALERT**\\n";
    json << "Trigger: " << escape_json(keyword) << "\\n";
    json << "Log: " << escape_json(log_file) << "\\n";
    json << "Incident: " << escape_json(incident_file) << "\"";
    json << "}";
    return json.str();
}

bool drop_privileges(const std::string &username)
{
    struct passwd *pw = getpwnam(username.c_str());
    if (!pw)
    {
        std::cerr << "Error: Unknown user: " << username << "\n";
        return false;
    }

    uid_t current_uid = getuid();
    if (current_uid == pw->pw_uid)
    {
        std::cout << "[SHOCKWAKE-LOG] Already running as " << username << "\n";
        return true;
    }

    if (current_uid != 0)
    {
        std::cerr << "Warning: Not root, cannot drop privileges to " << username << "\n";
        return true;
    }

    if (setgroups(0, nullptr) != 0)
    {
        std::cerr << "Error: setgroups failed: " << strerror(errno) << "\n";
        return false;
    }

    if (setgid(pw->pw_gid) != 0)
    {
        std::cerr << "Error: setgid failed: " << strerror(errno) << "\n";
        return false;
    }

    if (setuid(pw->pw_uid) != 0)
    {
        std::cerr << "Error: setuid failed: " << strerror(errno) << "\n";
        return false;
    }

    return true;
}

void print_status(const Config &config)
{
    struct stat st;
    bool file_exists = (stat(config.log_path.c_str(), &st) == 0);

    std::cout << "=== shockwake-log status ===\n";
    std::cout << "  Log file:      " << config.log_path;
    if (file_exists)
    {
        std::cout << " (" << st.st_size << " bytes)";
    }
    else
    {
        std::cout << " (not found)";
    }
    std::cout << "\n";
    std::cout << "  Webhook:       " << (config.webhook_url.empty() ? "(none - local logging only)" : config.webhook_url) << "\n";
    std::cout << "  Window:        " << config.window_size << " lines\n";
    std::cout << "  Trailing:      " << config.trailing_lines << " lines\n";
    std::cout << "  Incidents:     " << config.incident_dir << "\n";
    std::cout << "  Triggers:      ";
    for (size_t i = 0; i < config.triggers.size(); i++)
    {
        std::cout << config.triggers[i];
        if (i < config.triggers.size() - 1)
            std::cout << ", ";
    }
    std::cout << "\n";
    if (!config.excludes.empty())
    {
        std::cout << "  Excludes:      ";
        for (size_t i = 0; i < config.excludes.size(); i++)
        {
            std::cout << config.excludes[i];
            if (i < config.excludes.size() - 1)
                std::cout << ", ";
        }
        std::cout << "\n";
    }
    if (!config.drop_user.empty())
        std::cout << "  Drop user:     " << config.drop_user << "\n";
    std::cout << "============================\n";
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        std::cerr << "Error: No arguments provided. Use --help for usage.\n";
        return 1;
    }

    Config config = parse_args(argc, argv);

    if (config.log_path.empty())
    {
        std::cerr << "Error: --log is required.\n";
        return 1;
    }
    if (!config.webhook_url.empty())
        std::cout << "[SHOCKWAKE-LOG] Webhook enabled: " << config.webhook_url << "\n";
    else
        std::cout << "[SHOCKWAKE-LOG] No webhook configured — alerts will be logged locally only.\n";

    if (config.status_mode)
    {
        print_status(config);
        return 0;
    }

    struct stat st;
    if (stat(config.log_path.c_str(), &st) != 0)
    {
        std::cerr << "Error: Log file does not exist: " << config.log_path << "\n";
        return 1;
    }

    InotifyWatcher watcher(config.log_path);

    RingBuffer buffer(config.window_size);
    LogScanner scanner;
    scanner.set_triggers(config.triggers);
    scanner.set_excludes(config.excludes);

    bool has_webhook = !config.webhook_url.empty();
    WebhookAlert alerter(config.webhook_url);
    if (has_webhook)
        alerter.start();

    if (!config.drop_user.empty())
    {
        if (!drop_privileges(config.drop_user))
        {
            if (has_webhook)
                alerter.stop();
            return 1;
        }
        std::cout << "[SHOCKWAKE-LOG] Dropped privileges to: " << config.drop_user << "\n";
    }

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    std::cout << "[SHOCKWAKE-LOG] Starting...\n";
    std::cout << "  Log file:    " << config.log_path << "\n";
    std::cout << "  Webhook:     " << (has_webhook ? "enabled" : "disabled (local only)") << "\n";
    std::cout << "  Window:      " << config.window_size << " lines\n";
    std::cout << "  Trailing:    " << config.trailing_lines << " lines\n";
    std::cout << "  Triggers:    ";
    for (size_t i = 0; i < config.triggers.size(); i++)
    {
        std::cout << config.triggers[i];
        if (i < config.triggers.size() - 1)
            std::cout << ", ";
    }
    std::cout << "\n";
    if (!config.excludes.empty())
    {
        std::cout << "  Excludes:    ";
        for (size_t i = 0; i < config.excludes.size(); i++)
        {
            std::cout << config.excludes[i];
            if (i < config.excludes.size() - 1)
                std::cout << ", ";
        }
        std::cout << "\n";
    }
    std::cout << "[SHOCKWAKE-LOG] Watching for changes...\n\n";

    time_t start_time = time(nullptr);
    time_t last_health_log = start_time;

    while (g_running)
    {
        std::string new_content = watcher.wait_for_changes();
        if (new_content.empty())
            continue;

        time_t now = time(nullptr);
        if (now - last_health_log >= 60)
        {
            std::cout << "[HEALTH] uptime=" << get_uptime(start_time)
                      << " alerts=" << g_alert_count.load()
                      << " lines=" << g_lines_processed.load() << "\n";
            last_health_log = now;
        }

        std::istringstream stream(new_content);
        std::string line;
        while (std::getline(stream, line))
        {
            if (line.empty())
                continue;

            buffer.push(line);
            g_lines_processed++;

            if (scanner.scan(line))
            {
                std::string keyword = scanner.matched_keyword();
                std::cout << "[TRIGGER] " << keyword << " detected!\n";

                auto pre_context = buffer.snapshot();
                auto post_context = buffer.tail(config.trailing_lines);

                write_incident(config, keyword, pre_context, post_context);

                std::string incident_file = config.incident_dir + "/incident_" + get_timestamp() + ".log";
                if (has_webhook)
                {
                    std::string payload = build_webhook_payload(keyword, config.log_path, incident_file);
                    alerter.enqueue(payload);
                    std::cout << "[ALERT] Webhook fired.\n\n";
                }
                else
                {
                    std::cout << "[ALERT] Incident logged locally.\n\n";
                }

                g_alert_count++;
            }
        }
    }

    std::cout << "\n[SHOCKWAKE-LOG] Shutting down...\n";
    std::cout << "[SHOCKWAKE-LOG] Uptime: " << get_uptime(start_time)
              << " | Alerts: " << g_alert_count.load()
              << " | Lines: " << g_lines_processed.load() << "\n";

    if (has_webhook)
        alerter.stop();
    return 0;
}
