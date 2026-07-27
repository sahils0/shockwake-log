#include "config.h"
#include "inotify_watcher.h"
#include "log_scanner.h"
#include "ring_buffer.h"
#include "status_dashboard.h"
#include "subcommands.h"
#include "utilities.h"
#include "webhook_alert.h"

#include <atomic>
#include <csignal>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>

namespace fs = std::filesystem;

static void signal_handler(int) { g_running = false; }

int cmd_monitor(Config &config) {
  if (config.log_path.empty()) {
    std::cerr
        << "error: no log file specified and none found via auto-discovery.\n";
    std::cerr << "use --help for usage.\n";
    return 1;
  }

  if (!config.webhook_url.empty())
    std::cout << "[swl] webhook enabled: " << config.webhook_url << "\n";
  else
    std::cout << "[swl] no webhook configured — alerts will be logged locally "
                 "only.\n";

  if (!config.config_path.empty())
    std::cout << "[swl] config loaded: " << config.config_path << "\n";

  struct stat st;
  if (stat(config.log_path.c_str(), &st) != 0) {
    std::cerr << "error: log file does not exist: " << config.log_path << "\n";
    return 1;
  }

  std::string pid_file =
      config.pid_file.empty() ? "./.swl.pid" : config.pid_file;

  std::signal(SIGTERM, signal_handler);
  std::signal(SIGINT, signal_handler);

  {
    std::ofstream pf(pid_file);
    pf << getpid();
  }

  auto cleanup_pid = [&pid_file]() { fs::remove(pid_file); };

  InotifyWatcher watcher(config.log_path);
  RingBuffer buffer(config.window_size);
  LogScanner scanner;
  scanner.set_triggers(config.triggers);
  scanner.set_excludes(config.excludes);

  bool has_webhook = !config.webhook_url.empty();
  WebhookAlert alerter(config.webhook_url, config.max_retries,
                       config.retry_delay_ms, config.ssl_verify);
  if (has_webhook)
    alerter.start();

  if (!config.drop_user.empty()) {
    if (!drop_privileges(config.drop_user)) {
      if (has_webhook)
        alerter.stop();
      cleanup_pid();
      return 1;
    }
    std::cout << "[swl] dropped privileges to: " << config.drop_user << "\n";
  }

  std::cout << "[swl] starting...\n";
  std::cout << "  log file:    " << config.log_path << "\n";
  std::cout << "  webhook:     "
            << (has_webhook ? "enabled" : "disabled (local only)") << "\n";
  if (has_webhook)
    std::cout << "  retries:     " << config.max_retries
              << " (delay: " << config.retry_delay_ms << "ms)\n";
  print_config_summary(config, "  ");

  std::cout << "[swl] watching for changes...\n\n";

  time_t start_time = time(nullptr);
  time_t last_health_log = start_time;
  std::unordered_map<std::string, time_t> last_alert_time;
  std::unordered_map<std::string, unsigned long> suppressed_count;

  while (g_running) {
    std::string new_content = watcher.wait_for_changes();
    if (new_content.empty())
      continue;

    time_t now = time(nullptr);
    if (now - last_health_log >= 60) {
      std::cout << "[health] uptime=" << get_uptime(start_time)
                << " alerts=" << g_alert_count.load()
                << " lines=" << g_lines_processed.load() << "\n";
      last_health_log = now;
    }

    std::istringstream stream(new_content);
    std::string line;
    while (std::getline(stream, line)) {
      if (config.max_line_length > 0 && line.size() > config.max_line_length)
        line.resize(config.max_line_length);

      buffer.push(line);
      g_lines_processed++;

      if (scanner.scan(line)) {
        std::string keyword = scanner.matched_keyword();

        time_t now2 = time(nullptr);
        if (config.cooldown_seconds > 0 && last_alert_time.count(keyword)) {
          int elapsed = static_cast<int>(now2 - last_alert_time[keyword]);
          if (elapsed < config.cooldown_seconds) {
            suppressed_count[keyword]++;
            continue;
          }
        }

        std::cout << "[trigger] " << keyword << " detected!\n";

        auto pre_context = buffer.snapshot();
        auto post_context = buffer.tail(config.trailing_lines);
        if (!pre_context.empty())
          pre_context.pop_back();
        if (!post_context.empty())
          post_context.pop_back();

        std::string incident_file =
            write_incident(config, keyword, pre_context, post_context);
        if (has_webhook) {
          std::string payload =
              build_webhook_payload(keyword, config.log_path, incident_file);
          alerter.enqueue(payload);
          std::cout << "[alert] webhook fired.\n\n";
        } else {
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

int main(int argc, char *argv[]) {
  Config config = parse_args(argc, argv);

  switch (config.subcommand) {
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
