// test_subcommands.cpp — Tests for cmd_clean shutdown + wait_for_process_exit
#include "subcommands.h"
#include "utilities.h"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
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

  std::cout << "\nAll 9 tests passed!\n";
  return 0;
}
