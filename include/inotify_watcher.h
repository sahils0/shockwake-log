#pragma once

#include <string>
#include <sys/types.h>

class InotifyWatcher {
public:
    explicit InotifyWatcher(const std::string& filepath);
    ~InotifyWatcher();

    // Block until file is modified, return new content
    std::string wait_for_changes();

    // Stop watching (for clean shutdown)
    void stop();

private:
    void setup_watches();
    bool reattach_file_watch();
    bool check_inode_changed();

    std::string filepath_;
    std::string dir_path_;
    std::string filename_;
    bool running_;
    int inotify_fd_;
    int file_watch_fd_;
    int dir_watch_fd_;
    int file_fd_;
    off_t read_offset_;
    ino_t current_inode_;
};
