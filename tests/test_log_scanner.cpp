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

void test_plain_exclude_suppresses_trigger() {
    LogScanner ls;
    ls.set_triggers({"ERROR", "FATAL"});
    ls.set_excludes({"DEBUG"});

    assert(ls.scan("ERROR: something failed") == true);
    assert(ls.scan("DEBUG: something happened") == false);
    assert(ls.scan("FATAL: critical error") == true);
    std::cout << "PASS: test_plain_exclude_suppresses_trigger\n";
}

void test_regex_exclude_suppresses_trigger() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});
    ls.set_excludes({"healthcheck.*ok"});

    assert(ls.scan("ERROR: disk full") == true);
    assert(ls.scan("healthcheck: status ok") == false);
    std::cout << "PASS: test_regex_exclude_suppresses_trigger\n";
}

void test_exclude_checked_before_trigger() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});
    ls.set_excludes({"ERROR"});

    assert(ls.scan("ERROR: this should be excluded") == false);
    std::cout << "PASS: test_exclude_checked_before_trigger\n";
}

void test_multiple_excludes() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});
    ls.set_excludes({"DEBUG", "metrics", "healthcheck"});

    assert(ls.scan("ERROR: disk full") == true);
    assert(ls.scan("DEBUG: tracing") == false);
    assert(ls.scan("metrics: cpu 50%") == false);
    assert(ls.scan("healthcheck: ok") == false);
    std::cout << "PASS: test_multiple_excludes\n";
}

void test_set_excludes_replaces_previous() {
    LogScanner ls;
    ls.set_excludes({"DEBUG"});
    ls.set_excludes({"METRICS"});

    ls.set_triggers({"ERROR"});
    assert(ls.scan("ERROR: fail") == true);
    assert(ls.scan("ERROR: DEBUG trace") == true);
    assert(ls.scan("METRICS: ERROR cpu") == false);
    std::cout << "PASS: test_set_excludes_replaces_previous\n";
}

void test_set_triggers_replaces_previous() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});
    assert(ls.scan("ERROR: fail") == true);

    ls.set_triggers({"CRITICAL"});
    assert(ls.scan("ERROR: fail") == false);
    assert(ls.scan("CRITICAL: disaster") == true);
    std::cout << "PASS: test_set_triggers_replaces_previous\n";
}

void test_matched_keyword_after_no_match() {
    LogScanner ls;
    ls.set_triggers({"ERROR", "FATAL"});

    ls.scan("ERROR: first");
    assert(ls.matched_keyword() == "ERROR");

    ls.scan("INFO: nothing");
    assert(ls.matched_keyword() == "ERROR");

    ls.scan("FATAL: second");
    assert(ls.matched_keyword() == "FATAL");
    std::cout << "PASS: test_matched_keyword_after_no_match\n";
}

void test_scan_empty_string() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});

    assert(ls.scan("") == false);
    std::cout << "PASS: test_scan_empty_string\n";
}

void test_regex_word_boundary() {
    LogScanner ls;
    ls.set_triggers({"\\bERR\\b"});

    assert(ls.scan("this is an ERR message") == true);
    assert(ls.scan("ERROR: no match") == false);
    std::cout << "PASS: test_regex_word_boundary\n";
}

void test_regex_alternation() {
    LogScanner ls;
    ls.set_triggers({"FATAL|CRITICAL"});

    assert(ls.scan("FATAL: disk full") == true);
    assert(ls.scan("CRITICAL: memory") == true);
    assert(ls.scan("ERROR: something") == false);
    std::cout << "PASS: test_regex_alternation\n";
}

void test_pcre_shorthand_d() {
    LogScanner ls;
    ls.set_triggers({"\\d{3}"});

    assert(ls.scan("status: 404") == true);
    assert(ls.matched_keyword() == "\\d{3}");
    assert(ls.scan("no digits here") == false);
    std::cout << "PASS: test_pcre_shorthand_d\n";
}

void test_pcre_shorthand_w() {
    LogScanner ls;
    ls.set_triggers({"\\w+@\\w+"});

    assert(ls.scan("user@example.com") == true);
    assert(ls.matched_keyword() == "\\w+@\\w+");
    assert(ls.scan("no email here") == false);
    std::cout << "PASS: test_pcre_shorthand_w\n";
}

void test_pcre_shorthand_S() {
    LogScanner ls;
    ls.set_triggers({"\\S+error"});

    assert(ls.scan("xerror found") == true);
    assert(ls.matched_keyword() == "\\S+error");
    assert(ls.scan("   error found") == false);
    std::cout << "PASS: test_pcre_shorthand_S\n";
}

void test_pcre_shorthand_D() {
    LogScanner ls;
    ls.set_triggers({"\\D+"});

    assert(ls.scan("all letters") == true);
    assert(ls.matched_keyword() == "\\D+");
    assert(ls.scan("12345") == false);
    std::cout << "PASS: test_pcre_shorthand_D\n";
}

void test_pcre_shorthand_W() {
    LogScanner ls;
    ls.set_triggers({"\\W"});

    assert(ls.scan("has space") == true);
    assert(ls.matched_keyword() == "\\W");
    assert(ls.scan("nospaces") == false);
    std::cout << "PASS: test_pcre_shorthand_W\n";
}

void test_exclude_with_regex_trigger() {
    LogScanner ls;
    ls.set_triggers({"\\bERR\\b"});
    ls.set_excludes({"suppressed"});

    assert(ls.scan("this is an ERR message") == true);
    assert(ls.scan("ERR suppressed message") == false);
    std::cout << "PASS: test_exclude_with_regex_trigger\n";
}

void test_plain_trigger_case_sensitive() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});

    assert(ls.scan("ERROR: disk full") == true);
    assert(ls.scan("error: disk full") == false);
    assert(ls.scan("Error: disk full") == false);
    std::cout << "PASS: test_plain_trigger_case_sensitive\n";
}

void test_regex_trigger_case_insensitive() {
    LogScanner ls;
    ls.set_triggers({".*error.*"});

    assert(ls.scan("ERROR: disk full") == true);
    assert(ls.scan("error: disk full") == true);
    assert(ls.scan("Error: disk full") == true);
    std::cout << "PASS: test_regex_trigger_case_insensitive\n";
}

void test_exclude_prevents_all_matches() {
    LogScanner ls;
    ls.set_excludes({"ERROR"});
    ls.set_triggers({"ERROR"});

    assert(ls.scan("ERROR: fail") == false);
    std::cout << "PASS: test_exclude_prevents_all_matches\n";
}

void test_empty_exclude_list() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});
    ls.set_excludes({});

    assert(ls.scan("ERROR: fail") == true);
    std::cout << "PASS: test_empty_exclude_list\n";
}

void test_regex_exclude_invalid_falls_back_to_plain() {
    LogScanner ls;
    ls.set_triggers({"ERROR"});
    ls.set_excludes({"[invalid"});

    assert(ls.scan("ERROR: fail") == true);
    assert(ls.scan("[invalid: test") == false);
    std::cout << "PASS: test_regex_exclude_invalid_falls_back_to_plain\n";
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
    test_plain_exclude_suppresses_trigger();
    test_regex_exclude_suppresses_trigger();
    test_exclude_checked_before_trigger();
    test_multiple_excludes();
    test_set_excludes_replaces_previous();
    test_set_triggers_replaces_previous();
    test_matched_keyword_after_no_match();
    test_scan_empty_string();
    test_regex_word_boundary();
    test_regex_alternation();
    test_pcre_shorthand_d();
    test_pcre_shorthand_w();
    test_pcre_shorthand_S();
    test_pcre_shorthand_D();
    test_pcre_shorthand_W();
    test_exclude_with_regex_trigger();
    test_plain_trigger_case_sensitive();
    test_regex_trigger_case_insensitive();
    test_exclude_prevents_all_matches();
    test_empty_exclude_list();
    test_regex_exclude_invalid_falls_back_to_plain();

    std::cout << "\nAll 31 tests passed!\n";
    return 0;
}
