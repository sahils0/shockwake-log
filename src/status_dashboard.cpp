#include "status_dashboard.h"
#include "utilities.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

static double get_process_uptime(pid_t pid) {
  std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
  std::ifstream stat_file(stat_path);
  if (!stat_file.is_open())
    return 0;

  std::string dummy;
  std::getline(stat_file, dummy);
  stat_file.close();

  size_t sp = dummy.find_last_of(')');
  if (sp == std::string::npos)
    return 0;
  std::istringstream iss(dummy.substr(sp + 2));

  std::string state;
  int ppid;
  long uptime_secs;
  iss >> state >> ppid;
  for (int i = 4; i < 22; i++)
    iss >> dummy;
  iss >> uptime_secs;

  long sys_uptime;
  std::ifstream up("/proc/uptime");
  up >> sys_uptime;
  up.close();

  long proc_start = sys_uptime - uptime_secs;
  return static_cast<double>(proc_start);
}

static void status_render(const Config &config, pid_t pid, double uptime,
                          int incident_count) {
  struct stat st;
  time_t mod_time = 0;
  if (stat(config.log_path.c_str(), &st) == 0)
    mod_time = st.st_mtime;

  time_t now = time(nullptr);
  double diff = difftime(now, mod_time);

  struct tm tm_buf;
  localtime_r(&mod_time, &tm_buf);
  char timebuf[64];
  strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_buf);

  std::string uptime_str = get_uptime(static_cast<time_t>(uptime));

  std::cout << "\n";
  std::cout << "=== swl status ===\n\n";

  std::string proc_status = (pid > 0) ? "running" : "stopped";
  std::string proc_color = (pid > 0) ? "\033[32m" : "\033[31m";
  std::string reset_color = "\033[0m";

  std::cout << "  [status]   " << proc_color << proc_status << reset_color
            << "\n";
  if (pid > 0)
    std::cout << "  [pid]      " << pid << "\n";

  std::cout << "\n";

  std::cout << "  [log]      " << config.log_path << "\n";
  std::cout << "  [last]     " << timebuf << " (" << static_cast<long>(diff)
            << "s ago)\n";
  if (mod_time == 0)
    std::cout << "  [warning]  log file does not exist\n";

  std::cout << "\n";

  std::cout << "  [triggers] ";
  for (size_t i = 0; i < config.triggers.size(); i++) {
    std::cout << config.triggers[i];
    if (i < config.triggers.size() - 1)
      std::cout << ", ";
  }
  std::cout << "\n";

  std::cout << "  [webhook]  "
            << (config.webhook_url.empty() ? "not configured"
                                           : config.webhook_url)
            << "\n";

  std::cout << "\n";

  if (pid > 0)
    std::cout << "  [uptime]   " << uptime_str << "\n";
  std::cout << "  [incidents] " << incident_count << " file(s)\n";

  std::cout << "\n";

  std::cout << "  [config]   " << config.config_path << "\n";
  if (!config.pid_file.empty())
    std::cout << "  [pidfile]  " << config.pid_file << "\n";

  print_config_summary(config, "  [options]  ");

  std::cout << "\n  run `swl --help` for available options\n\n";
}

int cmd_status(const Config &config) {
  pid_t pid = 0;
  if (!config.pid_file.empty())
    pid = read_pid_file(config.pid_file);

  double uptime = (pid > 0) ? get_process_uptime(pid) : 0;
  int incident_count = count_incidents(config.incident_dir);

  status_render(config, pid, uptime, incident_count);
  return 0;
}
