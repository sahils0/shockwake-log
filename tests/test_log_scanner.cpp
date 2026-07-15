#include "log_scanner.h"
#include <cassert>
#include <iostream>

void test_scan_finds_keyword()
{
    LogScanner ls;
    ls.set_triggers({"FATAL", "ERROR", "500"});

    assert(ls.scan("2024-01-15 FATAL: something broke") == true);
    assert(ls.matched_keyword() == "FATAL");
    std::cout << "PASS: test_scan_finds_keyword\n";
}

void test_scan_no_match()
{
    LogScanner ls;
    ls.set_triggers({"FATAL", "ERROR"});

    assert(ls.scan("2024-01-15 INFO: all good") == false);
    std::cout << "PASS: test_scan_no_match\n";
}

void test_scan_multiple_triggers()
{
    LogScanner ls;
    ls.set_triggers({"FATAL", "ERROR", "500"});

    assert(ls.scan("status code: 500 returned") == true);
    assert(ls.matched_keyword() == "500");
    std::cout << "PASS: test_scan_multiple_triggers\n";
}

void test_scan_empty_triggers()
{
    LogScanner ls;

    assert(ls.scan("anything") == false);
    std::cout << "PASS: test_scan_empty_triggers\n";
}

int main()
{
    test_scan_finds_keyword();
    test_scan_no_match();
    test_scan_multiple_triggers();
    test_scan_empty_triggers();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
