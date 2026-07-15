#include "ring_buffer.h"
#include <cassert>
#include <iostream>

void test_push_and_snapshot()
{
    RingBuffer rb(3);
    rb.push("line1");
    rb.push("line2");

    auto snap = rb.snapshot();
    assert(snap.size() == 2);
    assert(snap[0] == "line1");
    assert(snap[1] == "line2");
    std::cout << "PASS: test_push_and_snapshot\n";
}

void test_overflow_overwrites_oldest()
{
    RingBuffer rb(3);
    rb.push("line1");
    rb.push("line2");
    rb.push("line3");
    rb.push("line4"); // overwrites line1

    auto snap = rb.snapshot();
    assert(snap.size() == 3);
    assert(snap[0] == "line2");
    assert(snap[1] == "line3");
    assert(snap[2] == "line4");
    std::cout << "PASS: test_overflow_overwrites_oldest\n";
}

void test_tail()
{
    RingBuffer rb(5);
    rb.push("a");
    rb.push("b");
    rb.push("c");
    rb.push("d");
    rb.push("e");

    auto t = rb.tail(3);
    assert(t.size() == 3);
    assert(t[0] == "c");
    assert(t[1] == "d");
    assert(t[2] == "e");
    std::cout << "PASS: test_tail\n";
}

void test_tail_larger_than_count()
{
    RingBuffer rb(5);
    rb.push("only");

    auto t = rb.tail(10); // ask for 10 but only 1 exists
    assert(t.size() == 1);
    assert(t[0] == "only");
    std::cout << "PASS: test_tail_larger_than_count\n";
}

int main()
{
    test_push_and_snapshot();
    test_overflow_overwrites_oldest();
    test_tail();
    test_tail_larger_than_count();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
