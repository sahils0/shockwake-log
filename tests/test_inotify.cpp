#include "inotify_watcher.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

// pid-unique temp paths so concurrent runs (ctest -j, parallel fallback
// binaries) never collide on the same files.
static std::string tp(const char *name) {
  return "/tmp/swl_inotify_" + std::to_string(getpid()) + "_" + name;
}

static void cleanup(const char *path) {
  std::remove(path);
}

void test_watch_file() {
  std::string test_file = tp("basic.log");
  cleanup(test_file.c_str());

  {
    std::ofstream out(test_file);
    out << "initial content\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ofstream out(test_file, std::ios::app);
    out << "new line\n";
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("new line") != std::string::npos);
  std::cout << "PASS: test_watch_file\n";

  watcher.stop();
  cleanup(test_file.c_str());
}

void test_rotation_mv_touch() {
  std::string test_file = tp("rotate_mv.log");
  std::string rotated_file = tp("rotate_mv.log.1");
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());

  {
    std::ofstream out(test_file);
    out << "original line\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file, rotated_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::rename(test_file.c_str(), rotated_file.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    {
      std::ofstream out(test_file);
      out << "rotated content line\n";
    }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("rotated content") != std::string::npos);
  std::cout << "PASS: test_rotation_mv_touch\n";

  watcher.stop();
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());
}

void test_rotation_copy_truncate() {
  std::string test_file = tp("rotate_cp.log");
  std::string rotated_file = tp("rotate_cp.log.1");
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());

  {
    std::ofstream out(test_file);
    out << "line1\nline2\nline3\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file, rotated_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    {
      std::ifstream in(test_file, std::ios::binary);
      std::ofstream out(rotated_file, std::ios::binary);
      out << in.rdbuf();
    }
    {
      std::ofstream out(test_file, std::ios::out | std::ios::trunc);
      out << "post-truncation content\n";
    }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("post-truncation content") != std::string::npos);
  std::cout << "PASS: test_rotation_copy_truncate\n";

  watcher.stop();
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());
}

void test_multiple_rotations() {
  std::string test_file = tp("multirot.log");
  std::string r1 = tp("multirot.log.1");
  std::string r2 = tp("multirot.log.2");
  std::string r3 = tp("multirot.log.3");
  cleanup(test_file.c_str());
  cleanup(r1.c_str());
  cleanup(r2.c_str());
  cleanup(r3.c_str());

  {
    std::ofstream out(test_file);
    out << "initial\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread w1([test_file, r1]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::rename(test_file.c_str(), r1.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::ofstream out(test_file); out << "rotation1\n"; }
  });
  std::string c1 = watcher.wait_for_changes();
  w1.join();
  assert(c1.find("rotation1") != std::string::npos);

  std::thread w2([test_file, r2]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::rename(test_file.c_str(), r2.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::ofstream out(test_file); out << "rotation2\n"; }
  });
  std::string c2 = watcher.wait_for_changes();
  w2.join();
  assert(c2.find("rotation2") != std::string::npos);

  std::thread w3([test_file, r3]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::rename(test_file.c_str(), r3.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::ofstream out(test_file); out << "rotation3\n"; }
  });
  std::string c3 = watcher.wait_for_changes();
  w3.join();
  assert(c3.find("rotation3") != std::string::npos);

  std::cout << "PASS: test_multiple_rotations\n";

  watcher.stop();
  cleanup(test_file.c_str());
  cleanup(r1.c_str());
  cleanup(r2.c_str());
  cleanup(r3.c_str());
}

void test_rotation_then_continue() {
  std::string test_file = tp("rotcont.log");
  std::string rotated_file = tp("rotcont.log.1");
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());

  {
    std::ofstream out(test_file);
    out << "before rotation\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread w1([test_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ofstream out(test_file, std::ios::app);
    out << "pre-rotation line\n";
  });
  std::string c1 = watcher.wait_for_changes();
  w1.join();
  assert(c1.find("pre-rotation line") != std::string::npos);

  std::thread w2([test_file, rotated_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::rename(test_file.c_str(), rotated_file.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::ofstream out(test_file); out << "post-rotation line\n"; }
  });
  std::string c2 = watcher.wait_for_changes();
  w2.join();
  assert(c2.find("post-rotation line") != std::string::npos);

  std::thread w3([test_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ofstream out(test_file, std::ios::app);
    out << "final line\n";
  });
  std::string c3 = watcher.wait_for_changes();
  w3.join();
  assert(c3.find("final line") != std::string::npos);

  std::cout << "PASS: test_rotation_then_continue\n";

  watcher.stop();
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());
}

void test_rapid_appends_after_rotation() {
  std::string test_file = tp("rapidrot.log");
  std::string rotated_file = tp("rapidrot.log.1");
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());

  {
    std::ofstream out(test_file);
    out << "initial\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file, rotated_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::rename(test_file.c_str(), rotated_file.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::ofstream out(test_file); out << "rotated\n"; }
    for (int i = 0; i < 5; i++) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      std::ofstream out(test_file, std::ios::app);
      out << "append " << i << "\n";
    }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  std::cout << "PASS: test_rapid_appends_after_rotation\n";

  watcher.stop();
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());
}

void test_delete_then_recreate() {
  std::string test_file = tp("delrecreate.log");
  cleanup(test_file.c_str());

  {
    std::ofstream out(test_file);
    out << "original\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::remove(test_file.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::ofstream out(test_file); out << "recreated\n"; }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("recreated") != std::string::npos);
  std::cout << "PASS: test_delete_then_recreate\n";

  watcher.stop();
  cleanup(test_file.c_str());
}

void test_stop_idempotent() {
  std::string test_file = tp("stopidem.log");
  cleanup(test_file.c_str());

  {
    std::ofstream out(test_file);
    out << "data\n";
  }

  InotifyWatcher watcher(test_file);
  watcher.stop();
  watcher.stop();

  std::cout << "PASS: test_stop_idempotent\n";
  cleanup(test_file.c_str());
}

void test_nonexistent_file() {
  bool threw = false;
  try {
    InotifyWatcher watcher(tp("nonexistent_xyz_999.log"));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  std::cout << "PASS: test_nonexistent_file\n";
}

void test_subdirectory_rotation() {
  std::string dir = tp("subdir");
  std::string test_file = dir + "/app.log";
  std::string rotated_file = dir + "/app.log.1";

  mkdir(dir.c_str(), 0755);
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());

  {
    std::ofstream out(test_file);
    out << "subdir initial\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file, rotated_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::rename(test_file.c_str(), rotated_file.c_str());
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    { std::ofstream out(test_file); out << "subdir rotated\n"; }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("subdir rotated") != std::string::npos);
  std::cout << "PASS: test_subdirectory_rotation\n";

  watcher.stop();
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());
  rmdir(dir.c_str());
}

void test_logrotate_realistic() {
  std::string test_file = tp("logrotate.log");
  std::string rotated_file = tp("logrotate.log.1");
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());

  {
    std::ofstream out(test_file);
    out << "line1\nline2\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file, rotated_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    {
      std::ifstream in(test_file, std::ios::binary);
      std::ofstream out(rotated_file, std::ios::binary);
      out << in.rdbuf();
    }
    {
      std::ofstream out(test_file, std::ios::out | std::ios::trunc);
      out << "after logrotate\n";
    }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("after logrotate") != std::string::npos);
  std::cout << "PASS: test_logrotate_realistic\n";

  watcher.stop();
  cleanup(test_file.c_str());
  cleanup(rotated_file.c_str());
}

void test_wait_after_stop_returns_empty() {
  std::string test_file = tp("waitstop.log");
  cleanup(test_file.c_str());

  {
    std::ofstream out(test_file);
    out << "data\n";
  }

  InotifyWatcher watcher(test_file);
  watcher.stop();

  std::string content = watcher.wait_for_changes();
  assert(content.empty());
  std::cout << "PASS: test_wait_after_stop_returns_empty\n";

  cleanup(test_file.c_str());
}

void test_large_data_write() {
  std::string test_file = tp("largedata.log");
  cleanup(test_file.c_str());

  {
    std::ofstream out(test_file);
    out << "initial\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::ofstream out(test_file, std::ios::app);
    for (int i = 0; i < 50; i++)
      out << "line" << i << "\n";
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("line0") != std::string::npos);
  std::cout << "PASS: test_large_data_write\n";

  watcher.stop();
  cleanup(test_file.c_str());
}

void test_in_place_truncate() {
  std::string test_file = tp("truncate.log");
  cleanup(test_file.c_str());

  {
    std::ofstream out(test_file);
    out << "original content\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    {
      std::ofstream out(test_file, std::ios::out | std::ios::trunc);
      out << "truncated content\n";
    }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("truncated content") != std::string::npos);
  std::cout << "PASS: test_in_place_truncate\n";

  watcher.stop();
  cleanup(test_file.c_str());
}

void test_rapid_appends_no_loss() {
  std::string test_file = tp("rapidappend.log");
  cleanup(test_file.c_str());

  {
    std::ofstream out(test_file);
    out << "start\n";
  }

  InotifyWatcher watcher(test_file);

  std::thread writer([test_file]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (int i = 0; i < 50; i++) {
      std::ofstream out(test_file, std::ios::app);
      out << "rapid " << i << "\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });

  std::string content = watcher.wait_for_changes();
  writer.join();

  assert(!content.empty());
  assert(content.find("rapid") != std::string::npos);
  std::cout << "PASS: test_rapid_appends_no_loss\n";

  watcher.stop();
  cleanup(test_file.c_str());
}

int main() {
  test_watch_file();
  test_rotation_mv_touch();
  test_rotation_copy_truncate();
  test_multiple_rotations();
  test_rotation_then_continue();
  test_rapid_appends_after_rotation();
  test_delete_then_recreate();
  test_stop_idempotent();
  test_nonexistent_file();
  test_subdirectory_rotation();
  test_logrotate_realistic();
  test_wait_after_stop_returns_empty();
  test_large_data_write();
  test_in_place_truncate();
  test_rapid_appends_no_loss();

  std::cout << "\nAll 15 tests passed!\n";
  return 0;
}
