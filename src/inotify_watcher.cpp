#include "inotify_watcher.h"
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <stdexcept>

InotifyWatcher::InotifyWatcher(const std::string& filepath)
    : filepath_(filepath)
    , running_(true)
{
    inotify_fd_ = inotify_init();
    if (inotify_fd_ < 0)
        throw std::runtime_error("Failed to initialize inotify");

    file_fd_ = open(filepath_.c_str(), O_RDONLY);
    if (file_fd_ < 0) {
        close(inotify_fd_);
        throw std::runtime_error("Failed to open file: " + filepath_);
    }

    watch_fd_ = inotify_add_watch(inotify_fd_, filepath_.c_str(), IN_MODIFY);
    if (watch_fd_ < 0) {
        close(file_fd_);
        close(inotify_fd_);
        throw std::runtime_error("Failed to add inotify watch");
    }
}

InotifyWatcher::~InotifyWatcher()
{
    stop();
}

std::string InotifyWatcher::wait_for_changes()
{
    if (!running_)
        return "";

    // Block until kernel signals file modification
    char event_buf[sizeof(struct inotify_event) + 256];
    read(inotify_fd_, event_buf, sizeof(event_buf));

    // Read new bytes from current position
    // Use lseek to find end of file, then read from where we left off
    // For simplicity, read entire file each time (still fast due to OS caching)
    lseek(file_fd_, 0, SEEK_SET);
    std::vector<char> buffer(4096);
    ssize_t bytes_read = read(file_fd_, buffer.data(), buffer.size());

    if (bytes_read <= 0)
        return "";

    return std::string(buffer.data(), bytes_read);
}

void InotifyWatcher::stop()
{
    running_ = false;
    if (watch_fd_ >= 0) {
        inotify_rm_watch(inotify_fd_, watch_fd_);
        watch_fd_ = -1;
    }
    if (file_fd_ >= 0) {
        close(file_fd_);
        file_fd_ = -1;
    }
    if (inotify_fd_ >= 0) {
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
}
