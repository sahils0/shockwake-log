// test_subcommands.cpp — Tests for subcommands (clean/stop/init/incidents/
// logs/status) and privilege dropping.
#include "status_dashboard.h"
#include "subcommands.h"
#include "utilities.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <pwd.h>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

// Release builds define NDEBUG which compiles out assert(); use an always-on
// check so these regression tests actually verify in every build type.
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "FAIL: " << #x << " (line " << __LINE__ << ")\n";           \
      std::exit(1);                                                            \
    }                                                                          \
  } while (0)

static bool proc_alive(pid_t pid) {
  if (kill(pid, 0) == 0)
    return true;
  return errno != ESRCH;
}

static pid_t spawn_writer(const std::string &dir, bool ignore_term) {
  int pipefd[2];
  if (pipe(pipefd) != 0)
    return -1;

  pid_t pid = fork();
  if (pid == 0) {
    close(pipefd[0]);
    if (ignore_term)
      signal(SIGTERM, SIG_IGN);
    ssize_t w = write(pipefd[1], "r", 1);
    (void)w;
    close(pipefd[1]);
    for (;;) {
      std::ofstream out(dir + "/incident_child.log", std::ios::app);
      if (out)
        out << "child " << getpid() << " alive\n";
      out.close();
      usleep(20000);
    }
  }

  close(pipefd[1]);
  char c;
  ssize_t n = read(pipefd[0], &c, 1);
  close(pipefd[0]);
  if (n != 1)
    return -1;
  return pid;
}

static void write_pid_file(const std::string &path, const std::string &content) {
  std::ofstream pf(path);
  pf << content << "\n";
  pf.close();
}

static std::string tbase(const char *name) {
  return "/tmp/swl_t_sub_" + std::to_string(getpid()) + "_" + name;
}

static void test_wait_for_process_exit_dead_pid() {
  CHECK(wait_for_process_exit(2147483646, 500));
  std::cout << "PASS: test_wait_for_process_exit_dead_pid\n";
}

static void test_wait_for_process_exit_live_pid() {
  pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    for (;;)
      usleep(100000);
  }

  bool exited = wait_for_process_exit(pid, 300);
  CHECK(!exited);
  CHECK(proc_alive(pid));

  kill(pid, SIGKILL);
  usleep(200000);
  CHECK(!proc_alive(pid));
  std::cout << "PASS: test_wait_for_process_exit_live_pid\n";
}

static void test_wait_for_process_exit_after_death() {
  pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    usleep(200000);
    _exit(0);
  }

  bool exited = wait_for_process_exit(pid, 3000);
  CHECK(exited);
  std::cout << "PASS: test_wait_for_process_exit_after_death\n";
}

static void test_cmd_clean_waits_for_monitor_death() {
  std::string dir = tbase("clean1");
  std::string pid_file = tbase("clean1.pid");
  fs::remove_all(dir);
  fs::remove(pid_file);
  fs::create_directories(dir);
  {
    std::ofstream inc(dir + "/incident_old.log");
    inc << "old incident\n";
  }

  pid_t pid = spawn_writer(dir, false);
  CHECK(pid > 0);
  CHECK(proc_alive(pid));

  write_pid_file(pid_file, std::to_string(pid));

  Config c;
  c.pid_file = pid_file;
  c.incident_dir = dir;
  int rc = cmd_clean(c);

  CHECK(rc == 0);
  CHECK(!proc_alive(pid));
  CHECK(!fs::exists(pid_file));
  CHECK(!fs::exists(dir));

  fs::remove_all(dir);
  fs::remove(pid_file);
  std::cout << "PASS: test_cmd_clean_waits_for_monitor_death\n";
}

static void test_cmd_clean_kill_escalation() {
  std::string dir = tbase("clean2");
  std::string pid_file = tbase("clean2.pid");
  fs::remove_all(dir);
  fs::remove(pid_file);
  fs::create_directories(dir);

  pid_t pid = spawn_writer(dir, true);
  CHECK(pid > 0);
  CHECK(proc_alive(pid));

  write_pid_file(pid_file, std::to_string(pid));

  Config c;
  c.pid_file = pid_file;
  c.incident_dir = dir;
  int rc = cmd_clean(c);

  CHECK(rc == 0);
  CHECK(!proc_alive(pid));
  CHECK(!fs::exists(pid_file));
  CHECK(!fs::exists(dir));

  fs::remove_all(dir);
  fs::remove(pid_file);
  std::cout << "PASS: test_cmd_clean_kill_escalation\n";
}

static void test_cmd_clean_no_pid_file() {
  std::string dir = tbase("clean3");
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    std::ofstream inc(dir + "/incident_a.log");
    inc << "stale incident\n";
  }

  Config c;
  c.pid_file = tbase("clean3.pid");
  c.incident_dir = dir;
  int rc = cmd_clean(c);

  CHECK(rc == 0);
  CHECK(!fs::exists(dir));

  fs::remove_all(dir);
  std::cout << "PASS: test_cmd_clean_no_pid_file\n";
}

static void test_cmd_clean_stale_pid_file() {
  std::string dir = tbase("clean4");
  std::string pid_file = tbase("clean4.pid");
  fs::remove_all(dir);
  fs::remove(pid_file);
  fs::create_directories(dir);
  write_pid_file(pid_file, "2147483646");

  Config c;
  c.pid_file = pid_file;
  c.incident_dir = dir;
  int rc = cmd_clean(c);

  CHECK(rc == 0);
  CHECK(!fs::exists(pid_file));
  CHECK(!fs::exists(dir));

  fs::remove_all(dir);
  fs::remove(pid_file);
  std::cout << "PASS: test_cmd_clean_stale_pid_file\n";
}

static void test_cmd_clean_no_incident_dir() {
  std::string pid_file = tbase("clean5.pid");
  fs::remove(pid_file);
  write_pid_file(pid_file, "2147483646");

  Config c;
  c.pid_file = pid_file;
  c.incident_dir = tbase("clean5_inc");
  int rc = cmd_clean(c);

  CHECK(rc == 0);
  CHECK(!fs::exists(pid_file));
  CHECK(!fs::exists(tbase("clean5_inc")));

  fs::remove(pid_file);
  std::cout << "PASS: test_cmd_clean_no_incident_dir\n";
}

static void test_cmd_clean_empty_config() {
  Config c;
  int rc = cmd_clean(c);
  CHECK(rc == 0);
  std::cout << "PASS: test_cmd_clean_empty_config\n";
}

static void test_cmd_init_creates_config() {
  std::string dir = tbase("init");
  fs::remove_all(dir);
  fs::create_directories(dir);

  fs::path cwd = fs::current_path();
  fs::current_path(dir);
  int rc = cmd_init();
  fs::current_path(cwd);

  CHECK(rc == 0);
  CHECK(fs::exists(dir + "/.swl.conf"));

  fs::current_path(dir);
  rc = cmd_init();
  fs::current_path(cwd);
  CHECK(rc == 0);

  fs::remove_all(dir);
  std::cout << "PASS: test_cmd_init_creates_config\n";
}

static void test_cmd_incidents_no_dir() {
  Config c;
  c.incident_dir = tbase("inc_none");
  CHECK(cmd_incidents(c) == 0);
  std::cout << "PASS: test_cmd_incidents_no_dir\n";
}

static void test_cmd_incidents_listing() {
  std::string dir = tbase("inc1");
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    std::ofstream out(dir + "/incident_a.log");
    out << "a\n";
  }
  {
    std::ofstream out(dir + "/incident_b.log");
    out << "b\n";
  }
  {
    std::ofstream out(dir + "/ignored.txt");
    out << "not an incident\n";
  }

  Config c;
  c.incident_dir = dir;

  std::ostringstream buf;
  auto *old = std::cout.rdbuf(buf.rdbuf());
  int rc = cmd_incidents(c);
  std::cout.rdbuf(old);

  CHECK(rc == 0);
  CHECK(buf.str().find("Total: 2 incident(s)") != std::string::npos);

  fs::remove_all(dir);
  std::cout << "PASS: test_cmd_incidents_listing\n";
}

static void test_cmd_logs_no_dir() {
  Config c;
  c.incident_dir = tbase("logs_none");
  CHECK(cmd_logs(c) == 1);
  std::cout << "PASS: test_cmd_logs_no_dir\n";
}

static void test_cmd_logs_empty_dir() {
  std::string dir = tbase("logs_empty");
  fs::remove_all(dir);
  fs::create_directories(dir);

  Config c;
  c.incident_dir = dir;
  CHECK(cmd_logs(c) == 1);

  fs::remove_all(dir);
  std::cout << "PASS: test_cmd_logs_empty_dir\n";
}

static void test_cmd_logs_follow() {
  std::string dir = tbase("logs1");
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    std::ofstream out(dir + "/incident_latest.log");
    out << "existing line\n";
  }

  Config c;
  c.incident_dir = dir;

  std::ostringstream buf;
  auto *old = std::cout.rdbuf(buf.rdbuf());

  std::thread t([&c]() { cmd_logs(c); });

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  {
    std::ofstream out(dir + "/incident_latest.log", std::ios::app);
    out << "new line\n";
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  g_running = false;
  t.join();
  std::cout.rdbuf(old);
  std::signal(SIGINT, SIG_DFL);

  CHECK(buf.str().find("new line") != std::string::npos);
  g_running = true;
  fs::remove_all(dir);
  std::cout << "PASS: test_cmd_logs_follow\n";
}

static void test_cmd_stop_no_pid_file() {
  Config c;
  c.pid_file = tbase("stop_none.pid");
  CHECK(cmd_stop(c) == 1);
  std::cout << "PASS: test_cmd_stop_no_pid_file\n";
}

static void test_cmd_stop_stale_pid_file() {
  std::string pid_file = tbase("stop_stale.pid");
  fs::remove(pid_file);
  write_pid_file(pid_file, "2147483646");

  Config c;
  c.pid_file = pid_file;
  CHECK(cmd_stop(c) == 1);
  CHECK(!fs::exists(pid_file));

  std::cout << "PASS: test_cmd_stop_stale_pid_file\n";
}

static void test_cmd_stop_live_process() {
  std::string dir = tbase("stop1");
  std::string pid_file = tbase("stop1.pid");
  fs::remove_all(dir);
  fs::remove(pid_file);
  fs::create_directories(dir);

  pid_t pid = spawn_writer(dir, false);
  CHECK(pid > 0);
  CHECK(proc_alive(pid));
  write_pid_file(pid_file, std::to_string(pid));

  Config c;
  c.pid_file = pid_file;
  int rc = cmd_stop(c);

  CHECK(rc == 0);
  CHECK(!proc_alive(pid));
  CHECK(!fs::exists(pid_file));
  CHECK(fs::exists(dir));

  fs::remove_all(dir);
  std::cout << "PASS: test_cmd_stop_live_process\n";
}

static void test_cmd_status_basic() {
  Config c;
  c.log_path = "/dev/null";
  c.pid_file = tbase("status_none.pid");
  CHECK(cmd_status(c) == 0);
  std::cout << "PASS: test_cmd_status_basic\n";
}

static void test_cmd_status_live() {
  std::string dir = tbase("status1");
  std::string pid_file = tbase("status1.pid");
  fs::remove_all(dir);
  fs::remove(pid_file);
  fs::create_directories(dir);
  {
    std::ofstream out(dir + "/incident_x.log");
    out << "x\n";
  }

  Config c;
  c.log_path = "/dev/null";
  c.pid_file = pid_file;
  c.incident_dir = dir;
  write_pid_file(pid_file, std::to_string(getpid()));

  std::ostringstream buf;
  auto *old = std::cout.rdbuf(buf.rdbuf());
  CHECK(cmd_status(c) == 0);
  std::cout.rdbuf(old);

  std::string output = buf.str();
  size_t pos = output.find("[uptime]");
  CHECK(pos != std::string::npos);
  std::string line = output.substr(pos);
  size_t nl = line.find('\n');
  if (nl != std::string::npos)
    line = line.substr(0, nl);
  CHECK(line.find('d') == std::string::npos);
  CHECK(line.rfind("s") == line.size() - 1);

  fs::remove_all(dir);
  fs::remove(pid_file);
  std::cout << "PASS: test_cmd_status_live\n";
}

static void test_drop_privileges_unknown_user() {
  CHECK(!drop_privileges("swl_nonexistent_user_xyz"));
  std::cout << "PASS: test_drop_privileges_unknown_user\n";
}

static void test_drop_privileges_current_user() {
  struct passwd *pw = getpwuid(getuid());
  CHECK(pw != nullptr);
  CHECK(drop_privileges(pw->pw_name));
  std::cout << "PASS: test_drop_privileges_current_user\n";
}

static void test_drop_privileges_other_user() {
  if (getuid() == 0) {
    std::cout << "SKIP: test_drop_privileges_other_user (running as root)\n";
    return;
  }
  if (!getpwnam("nobody")) {
    std::cout << "SKIP: test_drop_privileges_other_user (no nobody user)\n";
    return;
  }
  CHECK(drop_privileges("nobody"));
  std::cout << "PASS: test_drop_privileges_other_user\n";
}

int main() {
  signal(SIGCHLD, SIG_IGN);

  test_wait_for_process_exit_dead_pid();
  test_wait_for_process_exit_live_pid();
  test_wait_for_process_exit_after_death();
  test_cmd_clean_waits_for_monitor_death();
  test_cmd_clean_kill_escalation();
  test_cmd_clean_no_pid_file();
  test_cmd_clean_stale_pid_file();
  test_cmd_clean_no_incident_dir();
  test_cmd_clean_empty_config();
  test_cmd_init_creates_config();
  test_cmd_incidents_no_dir();
  test_cmd_incidents_listing();
  test_cmd_logs_no_dir();
  test_cmd_logs_empty_dir();
  test_cmd_logs_follow();
  test_cmd_stop_no_pid_file();
  test_cmd_stop_stale_pid_file();
  test_cmd_stop_live_process();
  test_cmd_status_basic();
  test_cmd_status_live();
  test_drop_privileges_unknown_user();
  test_drop_privileges_current_user();
  test_drop_privileges_other_user();

  std::cout << "\nAll 23 tests passed!\n";
  return 0;
}
