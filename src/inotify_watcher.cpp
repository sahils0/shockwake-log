#include "inotify_watcher.h"
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <stdexcept>
#include <sys/stat.h>
#include <filesystem>

InotifyWatcher::InotifyWatcher(const std::string& filepath)
    : filepath_(filepath)
    , running_(true)
    , inotify_fd_(-1)
    , file_watch_fd_(-1)
    , dir_watch_fd_(-1)
    , file_fd_(-1)
    , read_offset_(0)
    , current_inode_(0)
{
    std::filesystem::path p(filepath);
    dir_path_ = p.parent_path().string();
    if (dir_path_.empty()) dir_path_ = ".";
    filename_ = p.filename().string();

    inotify_fd_ = inotify_init();
    if (inotify_fd_ < 0)
        throw std::runtime_error("Failed to initialize inotify");

    file_fd_ = open(filepath_.c_str(), O_RDONLY);
    if (file_fd_ < 0) {
        close(inotify_fd_);
        throw std::runtime_error("Failed to open file: " + filepath_);
    }

    struct stat st;
    if (stat(filepath_.c_str(), &st) == 0)
        current_inode_ = st.st_ino;

    setup_watches();
}

void InotifyWatcher::setup_watches() {
    file_watch_fd_ = inotify_add_watch(inotify_fd_, filepath_.c_str(), IN_MODIFY);
    if (file_watch_fd_ < 0) {
        close(file_fd_);
        close(inotify_fd_);
        throw std::runtime_error("Failed to add inotify watch on file");
    }

    dir_watch_fd_ = inotify_add_watch(inotify_fd_, dir_path_.c_str(),
                                       IN_CREATE | IN_MOVED_TO | IN_MOVED_FROM | IN_DELETE);
    if (dir_watch_fd_ < 0)
        dir_watch_fd_ = -1;
}

bool InotifyWatcher::reattach_file_watch() {
    if (file_watch_fd_ >= 0) {
        inotify_rm_watch(inotify_fd_, file_watch_fd_);
        file_watch_fd_ = -1;
    }
    if (file_fd_ >= 0) {
        close(file_fd_);
        file_fd_ = -1;
    }

    file_fd_ = open(filepath_.c_str(), O_RDONLY);
    if (file_fd_ < 0)
        return false;

    struct stat st;
    if (stat(filepath_.c_str(), &st) == 0)
        current_inode_ = st.st_ino;

    read_offset_ = 0;

    file_watch_fd_ = inotify_add_watch(inotify_fd_, filepath_.c_str(), IN_MODIFY);
    if (file_watch_fd_ < 0) {
        close(file_fd_);
        file_fd_ = -1;
        return false;
    }

    return true;
}

bool InotifyWatcher::check_inode_changed() {
    struct stat st;
    if (stat(filepath_.c_str(), &st) != 0)
        return false;
    return st.st_ino != current_inode_;
}

std::string InotifyWatcher::wait_for_changes() {
    if (!running_)
        return "";

    while (true) {
        char event_buf[4096];
        ssize_t len = read(inotify_fd_, event_buf, sizeof(event_buf));
        if (len <= 0)
            return "";

        bool needs_reattach = false;
        bool file_moved_away = false;
        bool had_file_event = false;
        const char* ptr = event_buf;
        while (ptr < event_buf + len) {
            const auto* event = reinterpret_cast<const struct inotify_event*>(ptr);

            if (event->wd == file_watch_fd_ && (event->mask & IN_MODIFY))
                had_file_event = true;

            if (event->wd == dir_watch_fd_ && event->len > 0) {
                if (filename_ == event->name) {
                    if (event->mask & (IN_CREATE | IN_MOVED_TO))
                        needs_reattach = true;
                    if (event->mask & (IN_MOVED_FROM | IN_DELETE))
                        file_moved_away = true;
                }
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }

        if (file_moved_away) {
            if (file_watch_fd_ >= 0) {
                inotify_rm_watch(inotify_fd_, file_watch_fd_);
                file_watch_fd_ = -1;
            }
            if (file_fd_ >= 0) {
                close(file_fd_);
                file_fd_ = -1;
            }
            read_offset_ = 0;
        }

        if (needs_reattach) {
            reattach_file_watch();
            break;
        }

        if (file_moved_away)
            continue;

        if (!had_file_event)
            continue;

        break;
    }

    while (true) {
        if (file_fd_ < 0) {
            file_fd_ = open(filepath_.c_str(), O_RDONLY);
            if (file_fd_ < 0)
                return "";
            read_offset_ = 0;
        }

        lseek(file_fd_, read_offset_, SEEK_SET);
        std::vector<char> buffer(4096);
        ssize_t bytes_read = read(file_fd_, buffer.data(), buffer.size());

        if (bytes_read > 0) {
            read_offset_ += bytes_read;
            return std::string(buffer.data(), bytes_read);
        }

        char event_buf[4096];
        ssize_t len = read(inotify_fd_, event_buf, sizeof(event_buf));
        if (len <= 0)
            return "";

        const char* ptr = event_buf;
        while (ptr < event_buf + len) {
            const auto* event = reinterpret_cast<const struct inotify_event*>(ptr);

            if (event->wd == dir_watch_fd_ && event->len > 0) {
                if (filename_ == event->name) {
                    if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                        reattach_file_watch();
                    }
                }
            }

            if (event->wd == file_watch_fd_ && (event->mask & IN_MODIFY)) {
                if (file_fd_ >= 0) {
                    struct stat st;
                    if (stat(filepath_.c_str(), &st) == 0 && st.st_ino == current_inode_) {
                        if (st.st_size >= 0 && static_cast<off_t>(st.st_size) < read_offset_)
                            read_offset_ = 0;
                    }
                }
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }
}

void InotifyWatcher::stop() {
    running_ = false;
    if (file_watch_fd_ >= 0) {
        inotify_rm_watch(inotify_fd_, file_watch_fd_);
        file_watch_fd_ = -1;
    }
    if (dir_watch_fd_ >= 0) {
        inotify_rm_watch(inotify_fd_, dir_watch_fd_);
        dir_watch_fd_ = -1;
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

InotifyWatcher::~InotifyWatcher() {
    stop();
}
