#include "inotify_watcher.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

void test_watch_file() {
    // Create temp file
    const std::string test_file = "/tmp/test_shockwake.log";
    {
        std::ofstream out(test_file);
        out << "initial content\n";
    }

    InotifyWatcher watcher(test_file);

    // Write to file in background thread
    std::thread writer([&test_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::ofstream out(test_file, std::ios::app);
        out << "new line\n";
    });

    // This should block until file changes
    std::string content = watcher.wait_for_changes();
    writer.join();

    assert(!content.empty());
    std::cout << "PASS: test_watch_file\n";

    // Cleanup
    watcher.stop();
    std::remove(test_file.c_str());
}

int main() {
    test_watch_file();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
