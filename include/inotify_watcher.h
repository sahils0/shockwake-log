#pragma once

#include <atomic>
#include <string>
#include <sys/types.h>

class InotifyWatcher {
public:
  explicit InotifyWatcher(const std::string &filepath);
  ~InotifyWatcher();

  std::string wait_for_changes();
  void stop();

private:
  void setup_watches();
  bool reattach_file_watch();

  std::string filepath_;
  std::string dir_path_;
  std::string filename_;
  std::atomic<bool> running_;
  int inotify_fd_;
  int file_watch_fd_;
  int dir_watch_fd_;
  int file_fd_;
  int wake_pipe_[2];
  off_t read_offset_;
  ino_t current_inode_;
};
