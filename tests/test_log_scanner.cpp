#include "log_scanner.h"
#include <cassert>
#include <iostream>

void test_scan_finds_keyword() {
    LogScanner ls;
    ls.set_triggers({"FATAL", "ERROR", "500"});

    assert(ls.scan("2024-01-15 FATAL: something broke") == true);
    assert(ls.matched_keyword() == "FATAL");
    std::cout << "PASS: test_scan_finds_keyword\n";
}

void test_scan_no_match() {
    LogScanner ls;
    ls.set_triggers({"FATAL", "ERROR"});

    assert(ls.scan("2024-01-15 INFO: all good") == false);
    std::cout << "PASS: test_scan_no_match\n";
}

void test_scan_multiple_triggers() {
    LogScanner ls;
    ls.set_triggers({"FATAL", "ERROR", "500"});

    assert(ls.scan("status code: 500 returned") == true);
    assert(ls.matched_keyword() == "500");
    std::cout << "PASS: test_scan_multiple_triggers\n";
}

void test_scan_empty_triggers() {
    LogScanner ls;

    assert(ls.scan("anything") == false);
    std::cout << "PASS: test_scan_empty_triggers\n";
}

void test_regex_oom_pattern() {
    LogScanner ls;
    ls.set_triggers({"OOM\\s+killed"});

    assert(ls.scan("kernel: Out of memory: Kill process 1234") == false);
    assert(ls.scan("kernel: OOM killed process 1234") == true);
    assert(ls.matched_keyword() == "OOM\\s+killed");
    std::cout << "PASS: test_regex_oom_pattern\n";
}

void test_regex_status_code() {
    LogScanner ls;
    ls.set_triggers({"status\\s*=\\s*5\\d{2}"});

    assert(ls.scan("response: status = 503") == true);
    assert(ls.matched_keyword() == "status\\s*=\\s*5\\d{2}");
    assert(ls.scan("response: status = 200") == false);
    std::cout << "PASS: test_regex_status_code\n";
}

void test_regex_case_insensitive() {
    LogScanner ls;
    ls.set_triggers({"connection.*timed?.?out"});

    assert(ls.scan("ERROR: Connection timed out") == true);
    assert(ls.scan("connection refused") == false);
    std::cout << "PASS: test_regex_case_insensitive\n";
}

void test_mixed_plain_and_regex() {
    LogScanner ls;
    ls.set_triggers({"FATAL", "status\\s*=\\s*5\\d{2}"});

    assert(ls.scan("FATAL: something broke") == true);
    assert(ls.matched_keyword() == "FATAL");
    assert(ls.scan("response: status = 502") == true);
    assert(ls.matched_keyword() == "status\\s*=\\s*5\\d{2}");
    assert(ls.scan("INFO: all good") == false);
    std::cout << "PASS: test_mixed_plain_and_regex\n";
}

void test_regex_invalid_falls_back_to_plain() {
    LogScanner ls;
    ls.set_triggers({"[invalid", "ERROR"});

    assert(ls.scan("something ERROR happened") == true);
    assert(ls.matched_keyword() == "ERROR");
    std::cout << "PASS: test_regex_invalid_falls_back_to_plain\n";
}

void test_regex_dot_star() {
    LogScanner ls;
    ls.set_triggers({".*ERROR.*"});

    assert(ls.scan("anything with ERROR in it") == true);
    assert(ls.scan("no match here") == false);
    std::cout << "PASS: test_regex_dot_star\n";
}

int main() {
    test_scan_finds_keyword();
    test_scan_no_match();
    test_scan_multiple_triggers();
    test_scan_empty_triggers();
    test_regex_oom_pattern();
    test_regex_status_code();
    test_regex_case_insensitive();
    test_mixed_plain_and_regex();
    test_regex_invalid_falls_back_to_plain();
    test_regex_dot_star();

    std::cout << "\nAll 10 tests passed!\n";
    return 0;
}
