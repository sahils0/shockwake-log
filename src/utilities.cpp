#include "utilities.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <cstring>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <signal.h>

namespace fs = std::filesystem;

std::atomic<bool> g_running{true};
std::atomic<unsigned long> g_alert_count{0};
std::atomic<unsigned long> g_lines_processed{0};

std::string get_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() % 1000000;
    struct tm tm_buf;
    localtime_r(&time, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_buf);
    return std::string(buf) + "." + std::to_string(us);
}

std::string get_uptime(time_t start)
{
    time_t now = time(nullptr);
    long diff = static_cast<long>(now - start);
    long days = diff / 86400;
    long hours = (diff % 86400) / 3600;
    long mins = (diff % 3600) / 60;
    long secs = diff % 60;

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
    result.reserve(s.size());
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20) {
                char hex[8];
                snprintf(hex, sizeof(hex), "\\u%04x", c);
                result += hex;
            } else {
                result += static_cast<char>(c);
            }
        }
    }
    return result;
}

std::string write_incident(const Config &config, const std::string &keyword,
                    const std::vector<std::string> &context,
                    const std::vector<std::string> &trailing)
{
    std::error_code ec;
    fs::create_directories(config.incident_dir, ec);
    if (ec) {
        std::cerr << "[incident] error: cannot create directory " << config.incident_dir << ": " << ec.message() << "\n";
        return "";
    }

    std::string filename = config.incident_dir + "/incident_" + get_timestamp() + ".log";
    std::ofstream out(filename);

    if (!out.is_open()) {
        std::cerr << "[incident] error: cannot write to " << filename << "\n";
        return "";
    }

    out << "=== swl incident report ===\n";
    out << "Timestamp: " << get_timestamp() << "\n";
    out << "Trigger: " << keyword << "\n";
    out << "Log File: " << config.log_path << "\n";
    out << "=====================================\n\n";

    out << "--- pre-trigger context ---\n";
    for (const auto &line : context)
        out << line << "\n";

    out << "\n--- trigger line ---\n";
    out << ">>> " << keyword << " detected <<<\n";

    out << "\n--- post-trigger context ---\n";
    for (const auto &line : trailing)
        out << line << "\n";

    out.close();
    std::cout << "[incident] saved: " << filename << "\n";
    return filename;
}

std::string build_webhook_payload(const std::string &keyword,
                                  const std::string &log_file,
                                  const std::string &incident_file)
{
    std::ostringstream json;
    json << "{\"content\": \"";
    json << "\xF0\x9F\x9A\xA8 **swl alert**\\n";
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
        std::cerr << "error: unknown user: " << username << "\n";
        return false;
    }

    uid_t current_uid = getuid();
    if (current_uid == pw->pw_uid)
    {
        std::cout << "[swl] already running as " << username << "\n";
        return true;
    }

    if (current_uid != 0)
    {
        std::cerr << "warning: not root, cannot drop privileges to " << username << "\n";
        return true;
    }

    if (setgroups(0, nullptr) != 0)
    {
        std::cerr << "error: setgroups failed: " << strerror(errno) << "\n";
        return false;
    }

    if (setgid(pw->pw_gid) != 0)
    {
        std::cerr << "error: setgid failed: " << strerror(errno) << "\n";
        return false;
    }

    if (setuid(pw->pw_uid) != 0)
    {
        std::cerr << "error: setuid failed: " << strerror(errno) << "\n";
        return false;
    }

    return true;
}

int count_incidents(const std::string &dir)
{
    if (!fs::exists(dir) || !fs::is_directory(dir)) return 0;
    int count = 0;
    for (const auto &entry : fs::directory_iterator(dir))
        if (entry.is_regular_file())
            count++;
    return count;
}

pid_t read_pid_file(const std::string &pid_file)
{
    if (pid_file.empty() || !fs::exists(pid_file))
        return 0;

    std::ifstream pf(pid_file);
    std::string pid_str;
    std::getline(pf, pid_str);
    pf.close();

    if (pid_str.empty())
        return 0;

    try {
        pid_t pid = std::stoi(pid_str);
        if (kill(pid, 0) == 0)
            return pid;
    } catch (...) {}
    return 0;
}

void print_config_summary(const Config &config, const std::string &prefix)
{
    std::cout << prefix << "window:      " << config.window_size << " lines\n";
    std::cout << prefix << "trailing:    " << config.trailing_lines << " lines\n";
    std::cout << prefix << "triggers:    ";
    for (size_t i = 0; i < config.triggers.size(); i++)
    {
        std::cout << config.triggers[i];
        if (i < config.triggers.size() - 1) std::cout << ", ";
    }
    std::cout << "\n";
    if (!config.excludes.empty())
    {
        std::cout << prefix << "excludes:    ";
        for (size_t i = 0; i < config.excludes.size(); i++)
        {
            std::cout << config.excludes[i];
            if (i < config.excludes.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
    }
    std::cout << prefix << "cooldown:    " << (config.cooldown_seconds > 0 ? std::to_string(config.cooldown_seconds) + "s" : "none") << "\n";
}
