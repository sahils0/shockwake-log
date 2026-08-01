#include "config.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <filesystem>

namespace fs = std::filesystem;

// pid-unique config dir so concurrent runs (ctest -j, parallel fallback
// binaries) never collide on the same temp config files.
static std::string cfg_dir(const char* name) {
    return "/tmp/swl_cfg_" + std::to_string(getpid()) + "_" + name;
}

static int run_parse_args(const char* argv[], int argc) {
    char* mutable_argv[32];
    for (int i = 0; i < argc; i++)
        mutable_argv[i] = const_cast<char*>(argv[i]);
    mutable_argv[argc] = nullptr;
    Config c = parse_args(argc, mutable_argv);
    return c.subcommand == SubCommand::MONITOR ? 0 :
           c.subcommand == SubCommand::INIT ? 1 :
           c.subcommand == SubCommand::STATUS ? 2 :
           c.subcommand == SubCommand::INCIDENTS ? 3 :
           c.subcommand == SubCommand::LOGS ? 4 :
           c.subcommand == SubCommand::STOP ? 5 :
           c.subcommand == SubCommand::CLEAN ? 6 : -1;
}

void test_default_triggers() {
    const char* argv[] = {"swl", "--log", "/tmp/nonexistent_test.log"};
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("--log"), const_cast<char*>("/tmp/nonexistent_test.log")};
    Config c = parse_args(3, ma);
    assert(!c.triggers.empty());
    assert(c.triggers[0] == "FATAL");
    assert(c.triggers[1] == "ERROR");
    assert(c.window_size == 100);
    assert(c.trailing_lines == 20);
    assert(c.max_line_length == 8192);
    assert(c.cooldown_seconds == 60);
    assert(c.max_retries == 3);
    assert(c.retry_delay_ms == 1000);
    assert(c.ssl_verify == true);
    std::cout << "PASS: test_default_triggers\n";
}

void test_custom_flags() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--window"), const_cast<char*>("50"),
                  const_cast<char*>("--trailing"), const_cast<char*>("10"),
                  const_cast<char*>("--cooldown"), const_cast<char*>("30"),
                  const_cast<char*>("--retries"), const_cast<char*>("5"),
                  const_cast<char*>("--retry-delay"), const_cast<char*>("2000"),
                  const_cast<char*>("--max-line-length"), const_cast<char*>("4096")};
    Config c = parse_args(15, ma);
    assert(c.log_path == "/tmp/test.log");
    assert(c.window_size == 50);
    assert(c.trailing_lines == 10);
    assert(c.cooldown_seconds == 30);
    assert(c.max_retries == 5);
    assert(c.retry_delay_ms == 2000);
    assert(c.max_line_length == 4096);
    std::cout << "PASS: test_custom_flags\n";
}

void test_custom_triggers() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--triggers"), const_cast<char*>("FATAL,ERROR,WARN,CRITICAL")};
    Config c = parse_args(5, ma);
    assert(c.triggers.size() == 4);
    assert(c.triggers[0] == "FATAL");
    assert(c.triggers[1] == "ERROR");
    assert(c.triggers[2] == "WARN");
    assert(c.triggers[3] == "CRITICAL");
    std::cout << "PASS: test_custom_triggers\n";
}

void test_excludes_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--exclude"), const_cast<char*>("DEBUG,healthcheck,metrics")};
    Config c = parse_args(5, ma);
    assert(c.excludes.size() == 3);
    assert(c.excludes[0] == "DEBUG");
    assert(c.excludes[1] == "healthcheck");
    assert(c.excludes[2] == "metrics");
    std::cout << "PASS: test_excludes_flag\n";
}

void test_ssl_verify_true() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--ssl-verify"), const_cast<char*>("true")};
    Config c = parse_args(5, ma);
    assert(c.ssl_verify == true);
    std::cout << "PASS: test_ssl_verify_true\n";
}

void test_ssl_verify_false_variants() {
    {
        char* ma[] = {const_cast<char*>("swl"),
                      const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                      const_cast<char*>("--ssl-verify"), const_cast<char*>("false")};
        Config c = parse_args(5, ma);
        assert(c.ssl_verify == false);
    }
    {
        char* ma[] = {const_cast<char*>("swl"),
                      const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                      const_cast<char*>("--ssl-verify"), const_cast<char*>("1")};
        Config c = parse_args(5, ma);
        assert(c.ssl_verify == true);
    }
    {
        char* ma[] = {const_cast<char*>("swl"),
                      const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                      const_cast<char*>("--ssl-verify"), const_cast<char*>("yes")};
        Config c = parse_args(5, ma);
        assert(c.ssl_verify == true);
    }
    std::cout << "PASS: test_ssl_verify_false_variants\n";
}

void test_no_ssl_verify_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--no-ssl-verify")};
    Config c = parse_args(4, ma);
    assert(c.ssl_verify == false);
    std::cout << "PASS: test_no_ssl_verify_flag\n";
}

void test_positional_arg_logfile() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("/var/log/syslog")};
    Config c = parse_args(2, ma);
    assert(c.log_path == "/var/log/syslog");
    std::cout << "PASS: test_positional_arg_logfile\n";
}

void test_webhook_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--webhook"), const_cast<char*>("https://hooks.slack.com/test")};
    Config c = parse_args(5, ma);
    assert(c.webhook_url == "https://hooks.slack.com/test");
    std::cout << "PASS: test_webhook_flag\n";
}

void test_webhook_url_with_equals() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--webhook"), const_cast<char*>("https://example.com/hook?a=1&b=2")};
    Config c = parse_args(5, ma);
    assert(c.webhook_url == "https://example.com/hook?a=1&b=2");
    std::cout << "PASS: test_webhook_url_with_equals\n";
}

void test_subcommand_init() {
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("init")};
    Config c = parse_args(2, ma);
    assert(c.subcommand == SubCommand::INIT);
    std::cout << "PASS: test_subcommand_init\n";
}

void test_subcommand_status() {
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("status")};
    Config c = parse_args(2, ma);
    assert(c.subcommand == SubCommand::STATUS);
    std::cout << "PASS: test_subcommand_status\n";
}

void test_subcommand_incidents() {
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("incidents")};
    Config c = parse_args(2, ma);
    assert(c.subcommand == SubCommand::INCIDENTS);
    std::cout << "PASS: test_subcommand_incidents\n";
}

void test_subcommand_logs() {
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("logs")};
    Config c = parse_args(2, ma);
    assert(c.subcommand == SubCommand::LOGS);
    std::cout << "PASS: test_subcommand_logs\n";
}

void test_subcommand_stop() {
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("stop")};
    Config c = parse_args(2, ma);
    assert(c.subcommand == SubCommand::STOP);
    std::cout << "PASS: test_subcommand_stop\n";
}

void test_subcommand_clean() {
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("clean")};
    Config c = parse_args(2, ma);
    assert(c.subcommand == SubCommand::CLEAN);
    std::cout << "PASS: test_subcommand_clean\n";
}

void test_config_file_loading() {
    std::string cdir = cfg_dir("basic");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "# comment line\n"
        << "log = /var/log/test.log\n"
        << "webhook = https://hooks.example.com/test\n"
        << "triggers = FATAL,ERROR,WARN\n"
        << "excludes = DEBUG,healthcheck\n"
        << "window = 50\n"
        << "trailing = 15\n"
        << "cooldown = 30\n"
        << "incidents = /tmp/swl_test_incidents\n"
        << "retries = 5\n"
        << "retry_delay = 2000\n"
        << "max_line_length = 4096\n"
        << "ssl_verify = false\n"
        << "user = syslog\n"
        << "\n"
        << "  \n"
        << "key without equals\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.log_path == "/tmp/test.log");
    assert(c.webhook_url == "https://hooks.example.com/test");
    assert(c.triggers.size() == 3);
    assert(c.triggers[0] == "FATAL");
    assert(c.excludes.size() == 2);
    assert(c.excludes[0] == "DEBUG");
    assert(c.window_size == 50);
    assert(c.trailing_lines == 15);
    assert(c.cooldown_seconds == 30);
    assert(c.incident_dir == "/tmp/swl_test_incidents");
    assert(c.max_retries == 5);
    assert(c.retry_delay_ms == 2000);
    assert(c.max_line_length == 4096);
    assert(c.ssl_verify == false);
    assert(c.drop_user == "syslog");

    fs::remove_all(cdir);
    std::cout << "PASS: test_config_file_loading\n";
}

void test_config_file_missing() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>("/tmp/nonexistent_config_xyz.conf"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.log_path == "/tmp/test.log");
    assert(c.triggers.size() == 2);
    assert(c.triggers[0] == "FATAL");
    assert(c.triggers[1] == "ERROR");
    std::cout << "PASS: test_config_file_missing\n";
}

void test_config_empty_triggers_get_defaults() {
    std::string cdir = cfg_dir("empty");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "triggers =\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.triggers.size() == 2);
    assert(c.triggers[0] == "FATAL");
    assert(c.triggers[1] == "ERROR");

    fs::remove_all(cdir);
    std::cout << "PASS: test_config_empty_triggers_get_defaults\n";
}

void test_config_file_comments_and_blank_lines() {
    std::string cdir = cfg_dir("comments");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "# this is a comment\n"
        << "\n"
        << "   \n"
        << "# another comment\n"
        << "window = 200\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.window_size == 200);

    fs::remove_all(cdir);
    std::cout << "PASS: test_config_file_comments_and_blank_lines\n";
}

void test_config_value_with_equals() {
    std::string cdir = cfg_dir("equals");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "webhook = https://example.com/hook?key=abc&secret=xyz\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.webhook_url == "https://example.com/hook?key=abc&secret=xyz");

    fs::remove_all(cdir);
    std::cout << "PASS: test_config_value_with_equals\n";
}

void test_config_ssl_verify_true_variants() {
    std::string cdir = cfg_dir("ssl_true");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    for (const char* val : {"true", "1", "yes"}) {
        std::ofstream cfg(cpath);
        cfg << "ssl_verify = " << val << "\n";
        cfg.close();

        char* ma[] = {const_cast<char*>("swl"),
                      const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                      const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
        Config c = parse_args(5, ma);
        assert(c.ssl_verify == true);
    }
    fs::remove_all(cdir);
    std::cout << "PASS: test_config_ssl_verify_true_variants\n";
}

void test_config_ssl_verify_false_variants() {
    std::string cdir = cfg_dir("ssl_false");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    for (const char* val : {"false", "0", "no", "True", "YES", "on"}) {
        std::ofstream cfg(cpath);
        cfg << "ssl_verify = " << val << "\n";
        cfg.close();

        char* ma[] = {const_cast<char*>("swl"),
                      const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                      const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
        Config c = parse_args(5, ma);
        assert(c.ssl_verify == false);
    }
    fs::remove_all(cdir);
    std::cout << "PASS: test_config_ssl_verify_false_variants\n";
}

void test_cli_overrides_config() {
    std::string cdir = cfg_dir("override");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "window = 999\n"
        << "trailing = 888\n"
        << "cooldown = 777\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--window"), const_cast<char*>("50"),
                  const_cast<char*>("--trailing"), const_cast<char*>("10")};
    Config c = parse_args(9, ma);
    assert(c.window_size == 50);
    assert(c.trailing_lines == 10);
    assert(c.cooldown_seconds == 777);

    fs::remove_all(cdir);
    std::cout << "PASS: test_cli_overrides_config\n";
}

void test_subcommand_with_flags() {
    char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("stop"),
                  const_cast<char*>("--pid-file"), const_cast<char*>("/tmp/test.pid")};
    Config c = parse_args(4, ma);
    assert(c.subcommand == SubCommand::STOP);
    assert(c.pid_file == "/tmp/test.pid");
    std::cout << "PASS: test_subcommand_with_flags\n";
}

void test_incidents_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--incidents"), const_cast<char*>("/tmp/my_incidents")};
    Config c = parse_args(5, ma);
    assert(c.incident_dir == "/tmp/my_incidents");
    std::cout << "PASS: test_incidents_flag\n";
}

void test_user_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--user"), const_cast<char*>("syslog")};
    Config c = parse_args(5, ma);
    assert(c.drop_user == "syslog");
    std::cout << "PASS: test_user_flag\n";
}

void test_pid_file_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--pid-file"), const_cast<char*>("/tmp/my.pid")};
    Config c = parse_args(5, ma);
    assert(c.pid_file == "/tmp/my.pid");
    std::cout << "PASS: test_pid_file_flag\n";
}

void test_help_exits_zero() {
    pid_t pid = fork();
    if (pid == 0) {
        char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("--help")};
        parse_args(2, ma);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    std::cout << "PASS: test_help_exits_zero\n";
}

void test_version_exits_zero() {
    pid_t pid = fork();
    if (pid == 0) {
        char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("--version")};
        parse_args(2, ma);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    std::cout << "PASS: test_version_exits_zero\n";
}

void test_dash_h_flag() {
    pid_t pid = fork();
    if (pid == 0) {
        char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("-h")};
        parse_args(2, ma);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    std::cout << "PASS: test_dash_h_flag\n";
}

void test_dash_V_flag() {
    pid_t pid = fork();
    if (pid == 0) {
        char* ma[] = {const_cast<char*>("swl"), const_cast<char*>("-V")};
        parse_args(2, ma);
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    std::cout << "PASS: test_dash_V_flag\n";
}

void test_unknown_flag_ignored() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--bogus-flag")};
    Config c = parse_args(4, ma);
    assert(c.log_path == "/tmp/test.log");
    std::cout << "PASS: test_unknown_flag_ignored\n";
}

void test_triggers_with_spaces() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--triggers"), const_cast<char*>(" FATAL , ERROR , WARN ")};
    Config c = parse_args(5, ma);
    assert(c.triggers.size() == 3);
    assert(c.triggers[0] == "FATAL");
    assert(c.triggers[1] == "ERROR");
    assert(c.triggers[2] == "WARN");
    std::cout << "PASS: test_triggers_with_spaces\n";
}

void test_excludes_with_spaces() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--exclude"), const_cast<char*>(" DEBUG , healthcheck ")};
    Config c = parse_args(5, ma);
    assert(c.excludes.size() == 2);
    assert(c.excludes[0] == "DEBUG");
    assert(c.excludes[1] == "healthcheck");
    std::cout << "PASS: test_excludes_with_spaces\n";
}

void test_triggers_double_comma() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--triggers"), const_cast<char*>("ERROR,,WARN")};
    Config c = parse_args(5, ma);
    assert(c.triggers.size() == 2);
    assert(c.triggers[0] == "ERROR");
    assert(c.triggers[1] == "WARN");
    std::cout << "PASS: test_triggers_double_comma\n";
}

void test_window_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--window"), const_cast<char*>("250")};
    Config c = parse_args(5, ma);
    assert(c.window_size == 250);
    std::cout << "PASS: test_window_flag\n";
}

void test_trailing_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--trailing"), const_cast<char*>("50")};
    Config c = parse_args(5, ma);
    assert(c.trailing_lines == 50);
    std::cout << "PASS: test_trailing_flag\n";
}

void test_max_line_length_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--max-line-length"), const_cast<char*>("16384")};
    Config c = parse_args(5, ma);
    assert(c.max_line_length == 16384);
    std::cout << "PASS: test_max_line_length_flag\n";
}

void test_retries_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--retries"), const_cast<char*>("10")};
    Config c = parse_args(5, ma);
    assert(c.max_retries == 10);
    std::cout << "PASS: test_retries_flag\n";
}

void test_retry_delay_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--retry-delay"), const_cast<char*>("500")};
    Config c = parse_args(5, ma);
    assert(c.retry_delay_ms == 500);
    std::cout << "PASS: test_retry_delay_flag\n";
}

void test_cooldown_flag() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--cooldown"), const_cast<char*>("120")};
    Config c = parse_args(5, ma);
    assert(c.cooldown_seconds == 120);
    std::cout << "PASS: test_cooldown_flag\n";
}

void test_bad_numeric_config_file() {
    std::string cdir = cfg_dir("bad_num");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "window = not_a_number\n"
        << "trailing = abc\n"
        << "retries = xyz\n"
        << "cooldown = hello\n"
        << "retry_delay = 12.34.56\n"
        << "max_line_length = big\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.window_size == 100);
    assert(c.trailing_lines == 20);
    assert(c.max_retries == 3);
    assert(c.cooldown_seconds == 60);
    assert(c.retry_delay_ms == 1000);
    assert(c.max_line_length == 8192);

    fs::remove_all(cdir);
    std::cout << "PASS: test_bad_numeric_config_file\n";
}

void test_bad_numeric_cli_args() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--window"), const_cast<char*>("notanumber"),
                  const_cast<char*>("--retries"), const_cast<char*>("abc")};
    Config c = parse_args(7, ma);
    assert(c.window_size == 100);
    assert(c.max_retries == 3);
    std::cout << "PASS: test_bad_numeric_cli_args\n";
}

void test_window_zero_clamped() {
    std::string cdir = cfg_dir("window0");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "window = 0\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.window_size == 1);

    fs::remove_all(cdir);
    std::cout << "PASS: test_window_zero_clamped\n";
}

void test_negative_retries_clamped() {
    std::string cdir = cfg_dir("neg");
    std::string cpath = cdir + "/test.conf";
    fs::create_directories(cdir);
    std::ofstream cfg(cpath);
    cfg << "retries = -5\n"
        << "cooldown = -10\n"
        << "retry_delay = -100\n";
    cfg.close();

    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--config"), const_cast<char*>(cpath.c_str()),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log")};
    Config c = parse_args(5, ma);
    assert(c.max_retries == 0);
    assert(c.cooldown_seconds == 0);
    assert(c.retry_delay_ms == 0);

    fs::remove_all(cdir);
    std::cout << "PASS: test_negative_retries_clamped\n";
}

void test_webhook_invalid_url_accepted() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--webhook"), const_cast<char*>("not-a-url")};
    Config c = parse_args(5, ma);
    assert(c.webhook_url == "not-a-url");
    std::cout << "PASS: test_webhook_invalid_url_accepted\n";
}

void test_webhook_http_url_accepted() {
    char* ma[] = {const_cast<char*>("swl"),
                  const_cast<char*>("--log"), const_cast<char*>("/tmp/test.log"),
                  const_cast<char*>("--webhook"), const_cast<char*>("http://localhost:8080/hook")};
    Config c = parse_args(5, ma);
    assert(c.webhook_url == "http://localhost:8080/hook");
    std::cout << "PASS: test_webhook_http_url_accepted\n";
}

int main() {
    test_default_triggers();
    test_custom_flags();
    test_custom_triggers();
    test_excludes_flag();
    test_ssl_verify_true();
    test_ssl_verify_false_variants();
    test_no_ssl_verify_flag();
    test_positional_arg_logfile();
    test_webhook_flag();
    test_webhook_url_with_equals();
    test_subcommand_init();
    test_subcommand_status();
    test_subcommand_incidents();
    test_subcommand_logs();
    test_subcommand_stop();
    test_subcommand_clean();
    test_config_file_loading();
    test_config_file_missing();
    test_config_empty_triggers_get_defaults();
    test_config_file_comments_and_blank_lines();
    test_config_value_with_equals();
    test_config_ssl_verify_true_variants();
    test_config_ssl_verify_false_variants();
    test_cli_overrides_config();
    test_subcommand_with_flags();
    test_incidents_flag();
    test_user_flag();
    test_pid_file_flag();
    test_help_exits_zero();
    test_version_exits_zero();
    test_dash_h_flag();
    test_dash_V_flag();
    test_unknown_flag_ignored();
    test_triggers_with_spaces();
    test_excludes_with_spaces();
    test_triggers_double_comma();
    test_window_flag();
    test_trailing_flag();
    test_max_line_length_flag();
    test_retries_flag();
    test_retry_delay_flag();
    test_cooldown_flag();
    test_bad_numeric_config_file();
    test_bad_numeric_cli_args();
    test_window_zero_clamped();
    test_negative_retries_clamped();
    test_webhook_invalid_url_accepted();
    test_webhook_http_url_accepted();

    std::cout << "\nAll 49 tests passed!\n";
    return 0;
}
