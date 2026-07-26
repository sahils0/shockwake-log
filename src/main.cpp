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
#include <iomanip>
#include <unordered_map>
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
    struct tm tm_buf;
    localtime_r(&time, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_buf);
    return buf;
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

// ─── Subcommand: init ──────────────────────────────────────────────

int cmd_init()
{
    std::string conf = ".swl.conf";
    if (fs::exists(conf)) {
        std::cout << "warning: " << conf << " already exists. skipping.\n";
        return 0;
    }

    std::ofstream out(conf);
    out << "# swl configuration\n"
        << "# Generated by: swl init\n\n"
        << "# log file to monitor (auto-discovered if omitted)\n"
        << "# log = /var/log/syslog\n\n"
        << "# triggers: comma-separated keywords or regex patterns\n"
        << "triggers = FATAL,ERROR,WARN\n\n"
        << "# exclusions: lines matching these are skipped\n"
        << "# excludes = DEBUG,healthcheck\n\n"
        << "# webhook url (slack, discord, etc.)\n"
        << "# webhook = https://hooks.slack.com/services/YOUR/WEBHOOK/URL\n\n"
        << "# ring buffer: lines to keep for context around triggers\n"
        << "window = 100\n\n"
        << "# lines to capture after the trigger line\n"
        << "trailing = 20\n\n"
        << "# directory for incident reports\n"
        << "incidents = ./incidents\n\n"
        << "# cooldown: min seconds between alerts for same trigger (0 = no limit)\n"
        << "cooldown = 60\n\n"
        << "# max webhook retry attempts on failure (0 = no retry)\n"
        << "retries = 3\n\n"
        << "# delay between retries in milliseconds\n"
        << "retry_delay = 1000\n\n"
        << "# drop to this user after opening log (optional, requires root)\n"
        << "# user = syslog\n\n"
        << "# disable ssl certificate verification for self-signed certs\n"
        << "# ssl_verify = true\n\n"
        << "# max characters per log line (longer lines are truncated)\n"
        << "# max_line_length = 8192\n";
    out.close();

    std::cout << "created " << conf << " — edit it, then run:\n"
              << "  swl\n";
    return 0;
}

// ─── Helpers for status ─────────────────────────────────────────────

static double get_process_uptime(pid_t pid)
{
    // read /proc/<pid>/stat field 22 (starttime in clock ticks since boot)
    std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream f(stat_path);
    if (!f.is_open()) return -1;

    std::string line;
    std::getline(f, line);
    f.close();

    // field 22: skip to after the closing ')' of comm (field 2 is comm)
    size_t rp = line.rfind(')');
    if (rp == std::string::npos) return -1;

    std::istringstream iss(line.substr(rp + 2));
    for (int i = 3; i <= 21; i++) { std::string skip; iss >> skip; }
    long starttime_ticks;
    iss >> starttime_ticks;

    // get uptime from /proc/uptime
    std::ifstream uf("/proc/uptime");
    double uptime_secs = 0;
    uf >> uptime_secs;
    uf.close();

    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    double start_secs = static_cast<double>(starttime_ticks) / ticks_per_sec;

    return uptime_secs - start_secs;
}

static int count_incidents(const std::string &dir)
{
    if (!fs::exists(dir) || !fs::is_directory(dir)) return 0;
    int count = 0;
    for (const auto &entry : fs::directory_iterator(dir))
        if (entry.is_regular_file())
            count++;
    return count;
}

static void status_render(const Config &config, pid_t pid, double uptime, int incident_count)
{
    std::cout << "\033[2J\033[H"; // clear screen, cursor home

    std::cout << "=== swl status ===\n\n";

    // running state
    if (pid > 0)
        std::cout << "  state:         running (pid " << pid << ")\n";
    else
        std::cout << "  state:         not running\n";

    // uptime
    if (pid > 0 && uptime >= 0)
    {
        int days = static_cast<int>(uptime) / 86400;
        int hours = (static_cast<int>(uptime) % 86400) / 3600;
        int mins = (static_cast<int>(uptime) % 3600) / 60;
        int secs = static_cast<int>(uptime) % 60;
        std::cout << "  uptime:        ";
        if (days > 0) std::cout << days << "d ";
        std::cout << std::setfill('0') << std::setw(2) << hours << ":"
                  << std::setw(2) << mins << ":"
                  << std::setw(2) << std::setfill('0') << secs << "\n";
    }
    else if (pid > 0)
    {
        std::cout << "  uptime:        (could not determine)\n";
    }
    else
    {
        std::cout << "  uptime:        --\n";
    }

    // incidents
    std::cout << "  incidents:     " << incident_count << " total\n\n";

    // config
    struct stat st;
    bool file_exists = !config.log_path.empty() && (stat(config.log_path.c_str(), &st) == 0);

    if (!config.config_path.empty())
        std::cout << "  config:        " << config.config_path << "\n";
    std::cout << "  log file:      " << (config.log_path.empty() ? "(none - will auto-discover)" : config.log_path);
    if (file_exists)
        std::cout << " (" << st.st_size << " bytes)";
    else if (!config.log_path.empty())
        std::cout << " (not found)";
    std::cout << "\n";
    std::cout << "  webhook:       " << (config.webhook_url.empty() ? "(none - local logging only)" : config.webhook_url) << "\n";
    std::cout << "  window:        " << config.window_size << " lines\n";
    std::cout << "  trailing:      " << config.trailing_lines << " lines\n";
    std::cout << "  incidents dir: " << config.incident_dir << "\n";
    std::cout << "  triggers:      ";
    for (size_t i = 0; i < config.triggers.size(); i++)
    {
        std::cout << config.triggers[i];
        if (i < config.triggers.size() - 1) std::cout << ", ";
    }
    std::cout << "\n";
    if (!config.excludes.empty())
    {
        std::cout << "  excludes:      ";
        for (size_t i = 0; i < config.excludes.size(); i++)
        {
            std::cout << config.excludes[i];
            if (i < config.excludes.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
    }
    std::cout << "  cooldown:      " << (config.cooldown_seconds > 0 ? std::to_string(config.cooldown_seconds) + "s" : "none") << "\n";

    std::cout << "\n============================\n";
    std::cout << "  press ctrl+c to exit\n";
    std::cout << std::flush;
}

// ─── Subcommand: status ────────────────────────────────────────────

int cmd_status(const Config &config)
{
    std::string pid_file = config.pid_file;
    if (pid_file.empty()) pid_file = "./.swl.pid";

    pid_t pid = 0;
    if (!pid_file.empty() && fs::exists(pid_file))
    {
        std::ifstream pf(pid_file);
        std::string pid_str;
        std::getline(pf, pid_str);
        pf.close();
        if (!pid_str.empty())
        {
            pid_t check = std::stoi(pid_str);
            if (kill(check, 0) == 0)
                pid = check;
        }
    }

    int incident_count = count_incidents(config.incident_dir);

    if (pid <= 0)
    {
        // not running — one-shot print
        status_render(config, 0, -1, incident_count);
        return 0;
    }

    // live dashboard — refresh every second
    std::signal(SIGINT, [](int) {
        std::cout << "\033[?25h"; // show cursor
        _exit(0);
    });
    std::cout << "\033[?25l"; // hide cursor

    while (true)
    {
        double uptime = get_process_uptime(pid);
        incident_count = count_incidents(config.incident_dir);

        // check if process is still alive
        if (kill(pid, 0) != 0)
        {
            status_render(config, 0, -1, incident_count);
            break;
        }

        status_render(config, pid, uptime, incident_count);
        sleep(1);
    }

    std::cout << "\033[?25h"; // show cursor
    return 0;
}

// ─── Subcommand: incidents ─────────────────────────────────────────

int cmd_incidents(const Config &config)
{
    std::string dir = config.incident_dir;

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cout << "no incidents yet.\n";
        return 0;
    }

    std::vector<fs::directory_entry> log_files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (ext == ".log") log_files.push_back(entry);
        }
    }

    if (log_files.empty()) {
        std::cout << "no incidents yet.\n";
        return 0;
    }

    // Sort by modification time (newest first)
    std::sort(log_files.begin(), log_files.end(), [](const auto& a, const auto& b) {
        return fs::last_write_time(a) > fs::last_write_time(b);
    });

    std::cout << "recent incidents:\n\n";
    size_t count = std::min(log_files.size(), static_cast<size_t>(10));
    for (size_t i = 0; i < count; i++) {
        auto ftime = fs::last_write_time(log_files[i]);
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        std::time_t tt = std::chrono::system_clock::to_time_t(sctp);

        char timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&tt));

        struct stat st;
        stat(log_files[i].path().c_str(), &st);

        std::cout << "  " << timebuf << "  " << st.st_size << " bytes  " << log_files[i].path().string() << "\n";
    }
    std::cout << "\nTotal: " << log_files.size() << " incident(s)\n";
    return 0;
}

// ─── Subcommand: logs ──────────────────────────────────────────────

int cmd_logs(const Config &config)
{
    std::string dir = config.incident_dir;

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        std::cerr << "no incidents yet. start monitoring first.\n";
        return 1;
    }

    // Find most recent incident file
    std::string latest;
    fs::file_time_type latest_time{};

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        if (ext != ".log") continue;

        auto ftime = fs::last_write_time(entry);
        if (latest.empty() || ftime > latest_time) {
            latest = entry.path().string();
            latest_time = ftime;
        }
    }

    if (latest.empty()) {
        std::cerr << "no incidents yet. start monitoring first.\n";
        return 1;
    }

    std::cout << "following: " << latest << "\n";
    std::cout << "(Press Ctrl+C to stop)\n\n";

    // Open and tail the file
    std::ifstream file(latest);
    if (!file.is_open()) {
        std::cerr << "error: cannot open " << latest << "\n";
        return 1;
    }

    std::signal(SIGINT, [](int) {
        std::cout << std::flush;
        _exit(0);
    });

    // Seek to end — only follow NEW content (like tail -f)
    file.seekg(0, std::ios::end);

    while (g_running) {
        // Save position before attempting getline (tells where we are if it fails)
        std::streampos pos_before = file.tellg();

        std::string line;
        if (std::getline(file, line)) {
            std::cout << line << "\n" << std::flush;
        } else {
            // getline failed (EOF) — clear error and check if file grew
            file.clear();

            file.seekg(0, std::ios::end);
            std::streampos end_pos = file.tellg();

            if (pos_before >= std::streampos(0) && pos_before < end_pos) {
                // File grew — seek back to old position and read new content
                file.seekg(pos_before);
            } else {
                // No new content — wait before retrying
                usleep(500000);
            }
        }
    }

    return 0;
}

// ─── Subcommand: stop ──────────────────────────────────────────────

int cmd_stop(const Config &config)
{
    std::string pid_file = config.pid_file;

    if (pid_file.empty() || !fs::exists(pid_file)) {
        std::cerr << "no pid file found. is swl running?\n";
        std::cerr << "if running as a systemd service: sudo systemctl stop swl\n";
        return 1;
    }

    std::ifstream pf(pid_file);
    std::string pid_str;
    std::getline(pf, pid_str);
    pf.close();

    if (pid_str.empty()) {
        std::cerr << "pid file is empty. removing stale file.\n";
        fs::remove(pid_file);
        return 1;
    }

    pid_t pid = std::stoi(pid_str);

    if (kill(pid, 0) != 0) {
        std::cerr << "process " << pid << " is not running. cleaning up.\n";
        fs::remove(pid_file);
        return 0;
    }

    std::cout << "stopping swl (pid " << pid << ")...\n";
    kill(pid, SIGTERM);
    sleep(1);

    if (kill(pid, 0) == 0) {
        std::cout << "process still alive, sending sigkill...\n";
        kill(pid, SIGKILL);
    }

    fs::remove(pid_file);
    std::cout << "stopped.\n";
    return 0;
}

// ─── Subcommand: clean ─────────────────────────────────────────────

int cmd_clean(const Config &config)
{
    // Stop if running
    std::string pid_file = config.pid_file;
    if (!pid_file.empty() && fs::exists(pid_file)) {
        std::ifstream pf(pid_file);
        std::string pid_str;
        std::getline(pf, pid_str);
        pf.close();

        if (!pid_str.empty()) {
            pid_t pid = std::stoi(pid_str);
            if (kill(pid, 0) == 0) {
                std::cout << "stopping monitor (pid " << pid << ")...\n";
                kill(pid, SIGTERM);
                sleep(1);
            }
        }
        fs::remove(pid_file);
    }

    // Remove incidents
    std::string dir = config.incident_dir;
    if (fs::exists(dir)) {
        std::cout << "cleaning " << dir << "...\n";
        fs::remove_all(dir);
    }

    std::cout << "done.\n";
    return 0;
}

// ─── Subcommand: monitor ───────────────────────────────────────────

int cmd_monitor(Config &config)
{
    if (config.log_path.empty()) {
        std::cerr << "error: no log file specified and none found via auto-discovery.\n";
        std::cerr << "use --help for usage.\n";
        return 1;
    }

    if (!config.webhook_url.empty())
        std::cout << "[swl] webhook enabled: " << config.webhook_url << "\n";
    else
        std::cout << "[swl] no webhook configured — alerts will be logged locally only.\n";

    if (!config.config_path.empty())
        std::cout << "[swl] config loaded: " << config.config_path << "\n";

    struct stat st;
    if (stat(config.log_path.c_str(), &st) != 0) {
        std::cerr << "error: log file does not exist: " << config.log_path << "\n";
        return 1;
    }

    // Write PID file
    std::string pid_file = config.pid_file.empty() ? "./.swl.pid" : config.pid_file;
    {
        std::ofstream pf(pid_file);
        pf << getpid();
    }

    // Clean up PID on exit
    auto cleanup_pid = [&pid_file]() {
        fs::remove(pid_file);
    };

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);

    InotifyWatcher watcher(config.log_path);
    RingBuffer buffer(config.window_size);
    LogScanner scanner;
    scanner.set_triggers(config.triggers);
    scanner.set_excludes(config.excludes);

    bool has_webhook = !config.webhook_url.empty();
    WebhookAlert alerter(config.webhook_url, config.max_retries, config.retry_delay_ms, config.ssl_verify);
    if (has_webhook)
        alerter.start();

    if (!config.drop_user.empty())
    {
        if (!drop_privileges(config.drop_user))
        {
            if (has_webhook) alerter.stop();
            cleanup_pid();
            return 1;
        }
        std::cout << "[swl] dropped privileges to: " << config.drop_user << "\n";
    }

    std::cout << "[swl] starting...\n";
    std::cout << "  log file:    " << config.log_path << "\n";
    std::cout << "  webhook:     " << (has_webhook ? "enabled" : "disabled (local only)") << "\n";
    if (has_webhook)
        std::cout << "  retries:     " << config.max_retries << " (delay: " << config.retry_delay_ms << "ms)\n";
    std::cout << "  window:      " << config.window_size << " lines\n";
    std::cout << "  trailing:    " << config.trailing_lines << " lines\n";
    std::cout << "  triggers:    ";
    for (size_t i = 0; i < config.triggers.size(); i++)
    {
        std::cout << config.triggers[i];
        if (i < config.triggers.size() - 1) std::cout << ", ";
    }
    std::cout << "\n";
    if (!config.excludes.empty())
    {
        std::cout << "  excludes:    ";
        for (size_t i = 0; i < config.excludes.size(); i++)
        {
            std::cout << config.excludes[i];
            if (i < config.excludes.size() - 1) std::cout << ", ";
        }
        std::cout << "\n";
    }
    std::cout << "  cooldown:    " << (config.cooldown_seconds > 0 ? std::to_string(config.cooldown_seconds) + "s" : "none") << "\n";
    std::cout << "[swl] watching for changes...\n\n";

    time_t start_time = time(nullptr);
    time_t last_health_log = start_time;
    std::unordered_map<std::string, time_t> last_alert_time;
    std::unordered_map<std::string, unsigned long> suppressed_count;

    while (g_running)
    {
        std::string new_content = watcher.wait_for_changes();
        if (new_content.empty())
            continue;

        time_t now = time(nullptr);
        if (now - last_health_log >= 60)
        {
            std::cout << "[health] uptime=" << get_uptime(start_time)
                      << " alerts=" << g_alert_count.load()
                      << " lines=" << g_lines_processed.load() << "\n";
            last_health_log = now;
        }

        std::istringstream stream(new_content);
        std::string line;
        while (std::getline(stream, line))
        {
            if (config.max_line_length > 0 && line.size() > config.max_line_length)
                line.resize(config.max_line_length);

            buffer.push(line);
            g_lines_processed++;

            if (scanner.scan(line))
            {
                std::string keyword = scanner.matched_keyword();

                time_t now2 = time(nullptr);
                if (config.cooldown_seconds > 0 && last_alert_time.count(keyword))
                {
                    int elapsed = static_cast<int>(now2 - last_alert_time[keyword]);
                    if (elapsed < config.cooldown_seconds)
                    {
                        suppressed_count[keyword]++;
                        continue;
                    }
                }

                std::cout << "[trigger] " << keyword << " detected!\n";

                auto pre_context = buffer.snapshot();
                auto post_context = buffer.tail(config.trailing_lines);
                // Remove trigger line from both contexts (shown separately)
                if (!pre_context.empty())
                    pre_context.pop_back();
                if (!post_context.empty())
                    post_context.pop_back();

                std::string incident_file = write_incident(config, keyword, pre_context, post_context);
                if (has_webhook)
                {
                    std::string payload = build_webhook_payload(keyword, config.log_path, incident_file);
                    alerter.enqueue(payload);
                    std::cout << "[alert] webhook fired.\n\n";
                }
                else
                {
                    std::cout << "[alert] incident logged locally.\n\n";
                }

                g_alert_count++;
                last_alert_time[keyword] = time(nullptr);
            }
        }
    }

    std::cout << "\n[swl] shutting down...\n";
    std::cout << "[swl] uptime: " << get_uptime(start_time)
              << " | alerts: " << g_alert_count.load()
              << " | lines: " << g_lines_processed.load() << "\n";
    if (has_webhook && alerter.dropped_count() > 0)
        std::cout << "[swl] dropped alerts: " << alerter.dropped_count() << "\n";

    unsigned long total_suppressed = 0;
    for (const auto &[kw, count] : suppressed_count)
        total_suppressed += count;
    if (total_suppressed > 0)
        std::cout << "[swl] suppressed by cooldown: " << total_suppressed << "\n";

    if (has_webhook)
        alerter.stop();

    cleanup_pid();
    return 0;
}

// ─── Main dispatch ─────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    Config config = parse_args(argc, argv);

    switch (config.subcommand)
    {
    case SubCommand::INIT:
        return cmd_init();
    case SubCommand::STATUS:
        return cmd_status(config);
    case SubCommand::INCIDENTS:
        return cmd_incidents(config);
    case SubCommand::LOGS:
        return cmd_logs(config);
    case SubCommand::STOP:
        return cmd_stop(config);
    case SubCommand::CLEAN:
        return cmd_clean(config);
    case SubCommand::MONITOR:
    default:
        return cmd_monitor(config);
    }
}
