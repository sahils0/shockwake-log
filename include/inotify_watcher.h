#pragma once

#include <string>
#include <functional>

// InotifyWatcher: Uses Linux inotify to watch log file for changes
// Sleeps until kernel signals IN_MODIFY, then reads new bytes only
// Non-blocking, event-driven approach (no polling)
class InotifyWatcher {
public:
    explicit InotifyWatcher(const std::string& filepath);
    ~InotifyWatcher();

    // Block until file is modified, return new content
    std::string wait_for_changes();

    // Stop watching (for clean shutdown)
    void stop();

private:
    int inotify_fd_;
    int watch_fd_;
    bool running_;
    std::string filepath_;
    int file_fd_;
};
