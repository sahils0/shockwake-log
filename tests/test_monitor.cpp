// test_monitor.cpp — Tests for cmd_monitor (the core monitoring loop).
#include "monitor.h"
#include "utilities.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "FAIL: " << #x << " (line " << __LINE__ << ")\n";           \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

static std::string tbase(const char *name) {
  return "/tmp/swl_t_mon_" + std::to_string(getpid()) + "_" + name;
}

template <typename F>
static bool wait_until(F pred, int timeout_ms) {
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred())
      return true;
    usleep(10000);
  }
  return pred();
}

static int count_incident_files(const std::string &dir) {
  if (!fs::exists(dir))
    return 0;
  int count = 0;
  for (const auto &entry : fs::directory_iterator(dir))
    if (entry.is_regular_file() && entry.path().extension() == ".log")
      count++;
  return count;
}

static void test_no_log_path() {
  Config c;
  CHECK(cmd_monitor(c) == 1);
  std::cout << "PASS: test_no_log_path\n";
}

static void test_nonexistent_log_file() {
  std::string log = tbase("missing.log");
  fs::remove(log);
  Config c;
  c.log_path = log;
  CHECK(cmd_monitor(c) == 1);
  std::cout << "PASS: test_nonexistent_log_file\n";
}

static void test_drop_user_failure_cleans_pid() {
  std::string log = tbase("drop.log");
  std::string pid = tbase("drop.pid");
  fs::remove(log);
  fs::remove(pid);
  {
    std::ofstream out(log);
    out << "seed\n";
  }

  Config c;
  c.log_path = log;
  c.pid_file = pid;
  c.drop_user = "swl_nonexistent_user_xyz";
  int rc = cmd_monitor(c);
  CHECK(rc == 1);
  CHECK(!fs::exists(pid));
  std::cout << "PASS: test_drop_user_failure_cleans_pid\n";

  fs::remove(log);
  fs::remove(pid);
}

static void test_end_to_end() {
  g_running = true;
  g_alert_count = 0;
  g_lines_processed = 0;

  std::string log = tbase("e2e.log");
  std::string pid = tbase("e2e.pid");
  std::string inc = tbase("e2e_inc");
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  {
    std::ofstream out(log);
  }

  Config c;
  c.log_path = log;
  c.pid_file = pid;
  c.incident_dir = inc;
  c.triggers = {"ERROR"};
  c.cooldown_seconds = 3600;
  c.window_size = 5;
  c.trailing_lines = 3;

  std::thread monitor([&c]() { cmd_monitor(c); });

  CHECK(wait_until([&pid]() { return fs::exists(pid); }, 5000));

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  {
    std::ofstream out(log, std::ios::app);
    out << "INFO: healthy\n";
    out << "ERROR: disk full\n";
    out << "ERROR: again\n";
  }

  CHECK(wait_until([&inc]() { return count_incident_files(inc) == 1; }, 5000));
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  CHECK(count_incident_files(inc) == 1);
  CHECK(g_alert_count == 1);

  g_running = false;
  {
    std::ofstream out(log, std::ios::app);
    out << "end of test\n";
  }
  monitor.join();

  CHECK(!fs::exists(pid));

  std::string incident;
  for (const auto &entry : fs::directory_iterator(inc))
    if (entry.is_regular_file() && entry.path().extension() == ".log")
      incident = entry.path().string();
  CHECK(!incident.empty());

  std::ifstream in(incident);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  CHECK(content.find("Trigger: ERROR") != std::string::npos);
  CHECK(content.find("INFO: healthy") != std::string::npos);
  CHECK(content.find("pre-trigger context") != std::string::npos);
  CHECK(content.find("post-trigger context") != std::string::npos);
  CHECK(content.find(">>> ERROR detected <<<") != std::string::npos);

  std::signal(SIGTERM, SIG_DFL);
  std::signal(SIGINT, SIG_DFL);
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  std::cout << "PASS: test_end_to_end\n";
}

static void test_max_line_length_truncation() {
  g_running = true;
  g_alert_count = 0;
  g_lines_processed = 0;

  std::string log = tbase("long.log");
  std::string pid = tbase("long.pid");
  std::string inc = tbase("long_inc");
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  {
    std::ofstream out(log);
  }

  Config c;
  c.log_path = log;
  c.pid_file = pid;
  c.incident_dir = inc;
  c.triggers = {"TAIL"};
  c.max_line_length = 8;
  c.cooldown_seconds = 0;

  std::thread monitor([&c]() { cmd_monitor(c); });

  CHECK(wait_until([&pid]() { return fs::exists(pid); }, 5000));

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  {
    std::ofstream out(log, std::ios::app);
    out << "TAILAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n";
  }

  CHECK(wait_until([&inc]() { return count_incident_files(inc) == 1; }, 5000));
  CHECK(g_lines_processed == 1);

  g_running = false;
  {
    std::ofstream out(log, std::ios::app);
    out << "end\n";
  }
  monitor.join();

  std::string incident;
  for (const auto &entry : fs::directory_iterator(inc))
    if (entry.is_regular_file() && entry.path().extension() == ".log")
      incident = entry.path().string();

  std::ifstream in(incident);
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  CHECK(content.find("Trigger: TAIL") != std::string::npos);

  std::signal(SIGTERM, SIG_DFL);
  std::signal(SIGINT, SIG_DFL);
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  std::cout << "PASS: test_max_line_length_truncation\n";
}

static void test_second_instance_refused() {
  std::string log = tbase("lock.log");
  std::string pid = tbase("lock.pid");
  std::string inc = tbase("lock_inc");
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  {
    std::ofstream out(log);
    out << "seed\n";
  }
  {
    std::ofstream out(pid);
    out << "123456\n";
  }

  Config c;
  c.log_path = log;
  c.pid_file = pid;
  c.incident_dir = inc;
  c.triggers = {"ERROR"};

  int lock_fd = open(pid.c_str(), O_RDWR, 0644);
  CHECK(lock_fd >= 0);
  CHECK(flock(lock_fd, LOCK_EX | LOCK_NB) == 0);

  CHECK(cmd_monitor(c) == 1);

  std::string content;
  {
    std::ifstream in(pid);
    std::stringstream ss;
    ss << in.rdbuf();
    content = ss.str();
  }
  CHECK(content.find("123456") != std::string::npos);

  flock(lock_fd, LOCK_UN);
  close(lock_fd);
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  std::cout << "PASS: test_second_instance_refused\n";
}

static void test_no_incident_without_trigger() {
  g_running = true;
  g_alert_count = 0;
  g_lines_processed = 0;

  std::string log = tbase("calm.log");
  std::string pid = tbase("calm.pid");
  std::string inc = tbase("calm_inc");
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  {
    std::ofstream out(log);
  }

  Config c;
  c.log_path = log;
  c.pid_file = pid;
  c.incident_dir = inc;
  c.triggers = {"ERROR"};

  std::thread monitor([&c]() { cmd_monitor(c); });

  CHECK(wait_until([&pid]() { return fs::exists(pid); }, 5000));

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  {
    std::ofstream out(log, std::ios::app);
    out << "INFO: all good\n";
    out << "DEBUG: trace\n";
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  CHECK(count_incident_files(inc) == 0);
  CHECK(g_alert_count == 0);
  CHECK(g_lines_processed >= 2);

  g_running = false;
  {
    std::ofstream out(log, std::ios::app);
    out << "end\n";
  }
  monitor.join();

  std::signal(SIGTERM, SIG_DFL);
  std::signal(SIGINT, SIG_DFL);
  fs::remove(log);
  fs::remove(pid);
  fs::remove_all(inc);
  std::cout << "PASS: test_no_incident_without_trigger\n";
}

int main() {
  test_no_log_path();
  test_nonexistent_log_file();
  test_drop_user_failure_cleans_pid();
  test_end_to_end();
  test_max_line_length_truncation();
  test_second_instance_refused();
  test_no_incident_without_trigger();

  std::cout << "\nAll 7 tests passed!\n";
  return 0;
}
