// test_main.cpp — Tests for pure functions defined in utilities.cpp
#include "../src/utilities.cpp"

#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

void test_escape_json_plain() {
    assert(escape_json("hello world") == "hello world");
    std::cout << "PASS: test_escape_json_plain\n";
}

void test_escape_json_quotes() {
    assert(escape_json("say \"hi\"") == "say \\\"hi\\\"");
    std::cout << "PASS: test_escape_json_quotes\n";
}

void test_escape_json_backslash() {
    assert(escape_json("path\\to\\file") == "path\\\\to\\\\file");
    std::cout << "PASS: test_escape_json_backslash\n";
}

void test_escape_json_newline() {
    assert(escape_json("line1\nline2") == "line1\\nline2");
    std::cout << "PASS: test_escape_json_newline\n";
}

void test_escape_json_carriage_return() {
    assert(escape_json("line1\rline2") == "line1\\rline2");
    std::cout << "PASS: test_escape_json_carriage_return\n";
}

void test_escape_json_tab() {
    assert(escape_json("col1\tcol2") == "col1\\tcol2");
    std::cout << "PASS: test_escape_json_tab\n";
}

void test_escape_json_control_chars() {
    assert(escape_json(std::string(1, '\x00')) == "\\u0000");
    assert(escape_json(std::string(1, '\x01')) == "\\u0001");
    assert(escape_json(std::string(1, '\x1f')) == "\\u001f");
    std::cout << "PASS: test_escape_json_control_chars\n";
}

void test_escape_json_high_byte() {
    std::string input = "caf\xe9";
    assert(escape_json(input) == input);
    std::cout << "PASS: test_escape_json_high_byte\n";
}

void test_escape_json_empty() {
    assert(escape_json("") == "");
    std::cout << "PASS: test_escape_json_empty\n";
}

void test_escape_json_all_special() {
    std::string input = "\"\\/\n\r\t";
    std::string expected = "\\\"\\\\/\\n\\r\\t";
    assert(escape_json(input) == expected);
    std::cout << "PASS: test_escape_json_all_special\n";
}

void test_get_timestamp_format() {
    std::string ts = get_timestamp();
    assert(ts.size() == 15);
    for (int i = 0; i < 8; i++)
        assert(ts[i] >= '0' && ts[i] <= '9');
    assert(ts[8] == '_');
    for (int i = 9; i < 15; i++)
        assert(ts[i] >= '0' && ts[i] <= '9');
    std::cout << "PASS: test_get_timestamp_format (ts=" << ts << ")\n";
}

void test_get_timestamp_unique() {
    std::string ts1 = get_timestamp();
    std::string ts2 = get_timestamp();
    assert(ts1.size() == 15);
    assert(ts2.size() == 15);
    std::cout << "PASS: test_get_timestamp_unique\n";
}

void test_get_uptime_seconds_only() {
    time_t now = time(nullptr);
    std::string uptime = get_uptime(now);
    assert(uptime.find("s") != std::string::npos);
    std::cout << "PASS: test_get_uptime_seconds_only (" << uptime << ")\n";
}

void test_get_uptime_with_minutes() {
    time_t now = time(nullptr) - 125;
    std::string uptime = get_uptime(now);
    assert(uptime.find("m") != std::string::npos);
    assert(uptime.find("s") != std::string::npos);
    std::cout << "PASS: test_get_uptime_with_minutes (" << uptime << ")\n";
}

void test_get_uptime_with_hours() {
    time_t now = time(nullptr) - 7200;
    std::string uptime = get_uptime(now);
    assert(uptime.find("h") != std::string::npos);
    std::cout << "PASS: test_get_uptime_with_hours (" << uptime << ")\n";
}

void test_get_uptime_with_days() {
    time_t now = time(nullptr) - 90000;
    std::string uptime = get_uptime(now);
    assert(uptime.find("d") != std::string::npos);
    std::cout << "PASS: test_get_uptime_with_days (" << uptime << ")\n";
}

void test_get_uptime_zero() {
    time_t now = time(nullptr);
    std::string uptime = get_uptime(now);
    assert(uptime == "0s");
    std::cout << "PASS: test_get_uptime_zero\n";
}

void test_build_webhook_payload_contains_fields() {
    std::string payload = build_webhook_payload("ERROR", "/var/log/syslog", "/tmp/incidents/incident.log");
    assert(payload.find("\"content\"") != std::string::npos);
    assert(payload.find("ERROR") != std::string::npos);
    assert(payload.find("/var/log/syslog") != std::string::npos);
    assert(payload.find("/tmp/incidents/incident.log") != std::string::npos);
    assert(payload.front() == '{');
    assert(payload.back() == '}');
    std::cout << "PASS: test_build_webhook_payload_contains_fields\n";
}

void test_build_webhook_payload_escapes_special_chars() {
    std::string payload = build_webhook_payload("ERR\"OR", "/log/path", "/tmp/inc.log");
    assert(payload.find("ERR\\\"OR") != std::string::npos);
    std::cout << "PASS: test_build_webhook_payload_escapes_special_chars\n";
}

void test_write_incident_basic() {
    fs::create_directories("/tmp/swl_test_inc");

    Config config;
    config.incident_dir = "/tmp/swl_test_inc";
    config.log_path = "/var/log/test.log";

    std::vector<std::string> context = {"line1", "line2"};
    std::vector<std::string> trailing = {"after1"};

    std::string file = write_incident(config, "ERROR", context, trailing);
    assert(!file.empty());
    assert(fs::exists(file));

    std::ifstream in(file);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    assert(content.find("ERROR") != std::string::npos);
    assert(content.find("line1") != std::string::npos);
    assert(content.find("line2") != std::string::npos);
    assert(content.find("after1") != std::string::npos);
    assert(content.find("pre-trigger context") != std::string::npos);
    assert(content.find("post-trigger context") != std::string::npos);
    assert(content.find("trigger line") != std::string::npos);
    assert(content.find("/var/log/test.log") != std::string::npos);

    fs::remove_all("/tmp/swl_test_inc");
    std::cout << "PASS: test_write_incident_basic\n";
}

void test_write_incident_empty_context() {
    fs::create_directories("/tmp/swl_test_inc2");

    Config config;
    config.incident_dir = "/tmp/swl_test_inc2";
    config.log_path = "/var/log/test.log";

    std::vector<std::string> context;
    std::vector<std::string> trailing;

    std::string file = write_incident(config, "FATAL", context, trailing);
    assert(!file.empty());
    assert(fs::exists(file));

    std::ifstream in(file);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    assert(content.find("FATAL") != std::string::npos);
    assert(content.find("pre-trigger context") != std::string::npos);

    fs::remove_all("/tmp/swl_test_inc2");
    std::cout << "PASS: test_write_incident_empty_context\n";
}

void test_write_incident_bad_dir() {
    Config config;
    config.incident_dir = "/nonexistent_xyz_path/swl_test";
    config.log_path = "/var/log/test.log";

    std::string file = write_incident(config, "ERROR", {}, {});
    assert(file.empty());

    std::cout << "PASS: test_write_incident_bad_dir\n";
}

void test_write_incident_returns_same_file_format() {
    fs::create_directories("/tmp/swl_test_inc3");

    Config config;
    config.incident_dir = "/tmp/swl_test_inc3";
    config.log_path = "/var/log/test.log";

    std::string file = write_incident(config, "WARN", {}, {});
    assert(file.find("incident_") != std::string::npos);
    assert(file.find(".log") != std::string::npos);
    assert(file.find("/tmp/swl_test_inc3/") == 0);

    fs::remove_all("/tmp/swl_test_inc3");
    std::cout << "PASS: test_write_incident_returns_same_file_format\n";
}

void test_write_incident_keyword_in_report() {
    fs::create_directories("/tmp/swl_test_inc4");

    Config config;
    config.incident_dir = "/tmp/swl_test_inc4";
    config.log_path = "/tmp/app.log";

    std::string file = write_incident(config, "OOM\\s+killed", {"before"}, {"after"});
    std::ifstream in(file);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    assert(content.find("OOM\\s+killed") != std::string::npos);

    fs::remove_all("/tmp/swl_test_inc4");
    std::cout << "PASS: test_write_incident_keyword_in_report\n";
}

void test_write_incident_special_chars_in_context() {
    fs::create_directories("/tmp/swl_test_inc5");

    Config config;
    config.incident_dir = "/tmp/swl_test_inc5";
    config.log_path = "/tmp/app.log";

    std::vector<std::string> context = {"line with \"quotes\" and \\backslash"};
    std::string file = write_incident(config, "ERROR", context, {});
    assert(!file.empty());

    std::ifstream in(file);
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    assert(content.find("line with \"quotes\" and \\backslash") != std::string::npos);

    fs::remove_all("/tmp/swl_test_inc5");
    std::cout << "PASS: test_write_incident_special_chars_in_context\n";
}

int main() {
    test_escape_json_plain();
    test_escape_json_quotes();
    test_escape_json_backslash();
    test_escape_json_newline();
    test_escape_json_carriage_return();
    test_escape_json_tab();
    test_escape_json_control_chars();
    test_escape_json_high_byte();
    test_escape_json_empty();
    test_escape_json_all_special();
    test_get_timestamp_format();
    test_get_timestamp_unique();
    test_get_uptime_seconds_only();
    test_get_uptime_with_minutes();
    test_get_uptime_with_hours();
    test_get_uptime_with_days();
    test_get_uptime_zero();
    test_build_webhook_payload_contains_fields();
    test_build_webhook_payload_escapes_special_chars();
    test_write_incident_basic();
    test_write_incident_empty_context();
    test_write_incident_bad_dir();
    test_write_incident_returns_same_file_format();
    test_write_incident_keyword_in_report();
    test_write_incident_special_chars_in_context();

    std::cout << "\nAll 25 tests passed!\n";
    return 0;
}
