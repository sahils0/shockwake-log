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

static void cleanup(const char* path) {
    std::remove(path);
}

void test_watch_file() {
    const char* test_file = "/tmp/shockwake_test_basic.log";
    cleanup(test_file);

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
    cleanup(test_file);
}

void test_rotation_mv_touch() {
    const char* test_file = "/tmp/shockwake_test_rotate_mv.log";
    const char* rotated_file = "/tmp/shockwake_test_rotate_mv.log.1";
    cleanup(test_file);
    cleanup(rotated_file);

    {
        std::ofstream out(test_file);
        out << "original line\n";
    }

    InotifyWatcher watcher(test_file);

    std::thread writer([test_file, rotated_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        std::rename(test_file, rotated_file);
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
    cleanup(test_file);
    cleanup(rotated_file);
}

void test_rotation_copy_truncate() {
    const char* test_file = "/tmp/shockwake_test_rotate_cp.log";
    const char* rotated_file = "/tmp/shockwake_test_rotate_cp.log.1";
    cleanup(test_file);
    cleanup(rotated_file);

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
    cleanup(test_file);
    cleanup(rotated_file);
}

void test_multiple_rotations() {
    const char* test_file = "/tmp/shockwake_test_multirot.log";
    cleanup(test_file);
    cleanup("/tmp/shockwake_test_multirot.log.1");
    cleanup("/tmp/shockwake_test_multirot.log.2");
    cleanup("/tmp/shockwake_test_multirot.log.3");

    {
        std::ofstream out(test_file);
        out << "initial\n";
    }

    InotifyWatcher watcher(test_file);

    std::thread w1([test_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::rename(test_file, "/tmp/shockwake_test_multirot.log.1");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        { std::ofstream out(test_file); out << "rotation1\n"; }
    });
    std::string c1 = watcher.wait_for_changes();
    w1.join();
    assert(c1.find("rotation1") != std::string::npos);

    std::thread w2([test_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::rename(test_file, "/tmp/shockwake_test_multirot.log.2");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        { std::ofstream out(test_file); out << "rotation2\n"; }
    });
    std::string c2 = watcher.wait_for_changes();
    w2.join();
    assert(c2.find("rotation2") != std::string::npos);

    std::thread w3([test_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::rename(test_file, "/tmp/shockwake_test_multirot.log.3");
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        { std::ofstream out(test_file); out << "rotation3\n"; }
    });
    std::string c3 = watcher.wait_for_changes();
    w3.join();
    assert(c3.find("rotation3") != std::string::npos);

    std::cout << "PASS: test_multiple_rotations\n";

    watcher.stop();
    cleanup(test_file);
    cleanup("/tmp/shockwake_test_multirot.log.1");
    cleanup("/tmp/shockwake_test_multirot.log.2");
    cleanup("/tmp/shockwake_test_multirot.log.3");
}

void test_rotation_then_continue() {
    const char* test_file = "/tmp/shockwake_test_rotcont.log";
    const char* rotated_file = "/tmp/shockwake_test_rotcont.log.1";
    cleanup(test_file);
    cleanup(rotated_file);

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
        std::rename(test_file, rotated_file);
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
    cleanup(test_file);
    cleanup(rotated_file);
}

void test_rapid_appends_after_rotation() {
    const char* test_file = "/tmp/shockwake_test_rapidrot.log";
    const char* rotated_file = "/tmp/shockwake_test_rapidrot.log.1";
    cleanup(test_file);
    cleanup(rotated_file);

    {
        std::ofstream out(test_file);
        out << "initial\n";
    }

    InotifyWatcher watcher(test_file);

    std::thread writer([test_file, rotated_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::rename(test_file, rotated_file);
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
    cleanup(test_file);
    cleanup(rotated_file);
}

void test_delete_then_recreate() {
    const char* test_file = "/tmp/shockwake_test_delrecreate.log";
    cleanup(test_file);

    {
        std::ofstream out(test_file);
        out << "original\n";
    }

    InotifyWatcher watcher(test_file);

    std::thread writer([test_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::remove(test_file);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        { std::ofstream out(test_file); out << "recreated\n"; }
    });

    std::string content = watcher.wait_for_changes();
    writer.join();

    assert(!content.empty());
    assert(content.find("recreated") != std::string::npos);
    std::cout << "PASS: test_delete_then_recreate\n";

    watcher.stop();
    cleanup(test_file);
}

void test_stop_idempotent() {
    const char* test_file = "/tmp/shockwake_test_stopidem.log";
    cleanup(test_file);

    {
        std::ofstream out(test_file);
        out << "data\n";
    }

    InotifyWatcher watcher(test_file);
    watcher.stop();
    watcher.stop();

    std::cout << "PASS: test_stop_idempotent\n";
    cleanup(test_file);
}

void test_nonexistent_file() {
    bool threw = false;
    try {
        InotifyWatcher watcher("/tmp/shockwake_nonexistent_xyz_999.log");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    assert(threw);
    std::cout << "PASS: test_nonexistent_file\n";
}

void test_subdirectory_rotation() {
    const char* dir = "/tmp/shockwake_test_subdir";
    const char* test_file = "/tmp/shockwake_test_subdir/app.log";
    const char* rotated_file = "/tmp/shockwake_test_subdir/app.log.1";

    mkdir(dir, 0755);
    cleanup(test_file);
    cleanup(rotated_file);

    {
        std::ofstream out(test_file);
        out << "subdir initial\n";
    }

    InotifyWatcher watcher(test_file);

    std::thread writer([test_file, rotated_file]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::rename(test_file, rotated_file);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        { std::ofstream out(test_file); out << "subdir rotated\n"; }
    });

    std::string content = watcher.wait_for_changes();
    writer.join();

    assert(!content.empty());
    assert(content.find("subdir rotated") != std::string::npos);
    std::cout << "PASS: test_subdirectory_rotation\n";

    watcher.stop();
    cleanup(test_file);
    cleanup(rotated_file);
    rmdir(dir);
}

void test_logrotate_realistic() {
    const char* test_file = "/tmp/shockwake_test_logrotate.log";
    const char* rotated_file = "/tmp/shockwake_test_logrotate.log.1";
    cleanup(test_file);
    cleanup(rotated_file);

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
    cleanup(test_file);
    cleanup(rotated_file);
}

void test_wait_after_stop_returns_empty() {
    const char* test_file = "/tmp/shockwake_test_waitstop.log";
    cleanup(test_file);

    {
        std::ofstream out(test_file);
        out << "data\n";
    }

    InotifyWatcher watcher(test_file);
    watcher.stop();

    std::string content = watcher.wait_for_changes();
    assert(content.empty());
    std::cout << "PASS: test_wait_after_stop_returns_empty\n";

    cleanup(test_file);
}

void test_large_data_write() {
    const char* test_file = "/tmp/shockwake_test_largedata.log";
    cleanup(test_file);

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
    cleanup(test_file);
}

void test_in_place_truncate() {
    const char* test_file = "/tmp/shockwake_test_truncate.log";
    cleanup(test_file);

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
    cleanup(test_file);
}

void test_rapid_appends_no_loss() {
    const char* test_file = "/tmp/shockwake_test_rapidappend.log";
    cleanup(test_file);

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
    cleanup(test_file);
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
