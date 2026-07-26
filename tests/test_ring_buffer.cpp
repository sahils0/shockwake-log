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

void test_capacity_zero_clamps_to_one()
{
    RingBuffer rb(0);
    rb.push("first");
    auto snap = rb.snapshot();
    assert(snap.size() == 1);
    assert(snap[0] == "first");

    rb.push("second");
    snap = rb.snapshot();
    assert(snap.size() == 1);
    assert(snap[0] == "second");
    std::cout << "PASS: test_capacity_zero_clamps_to_one\n";
}

void test_capacity_one()
{
    RingBuffer rb(1);
    rb.push("a");
    auto snap = rb.snapshot();
    assert(snap.size() == 1);
    assert(snap[0] == "a");

    rb.push("b");
    snap = rb.snapshot();
    assert(snap.size() == 1);
    assert(snap[0] == "b");
    std::cout << "PASS: test_capacity_one\n";
}

void test_empty_buffer_snapshot()
{
    RingBuffer rb(5);
    auto snap = rb.snapshot();
    assert(snap.size() == 0);
    std::cout << "PASS: test_empty_buffer_snapshot\n";
}

void test_empty_buffer_tail()
{
    RingBuffer rb(5);
    auto t = rb.tail(3);
    assert(t.size() == 0);
    std::cout << "PASS: test_empty_buffer_tail\n";
}

void test_tail_zero()
{
    RingBuffer rb(5);
    rb.push("a");
    rb.push("b");
    auto t = rb.tail(0);
    assert(t.size() == 0);
    std::cout << "PASS: test_tail_zero\n";
}

void test_tail_exact_count()
{
    RingBuffer rb(5);
    rb.push("a");
    rb.push("b");
    rb.push("c");
    auto t = rb.tail(3);
    assert(t.size() == 3);
    assert(t[0] == "a");
    assert(t[1] == "b");
    assert(t[2] == "c");
    std::cout << "PASS: test_tail_exact_count\n";
}

void test_multiple_full_rotations()
{
    RingBuffer rb(3);
    for (int i = 0; i < 9; i++)
        rb.push("line" + std::to_string(i));

    auto snap = rb.snapshot();
    assert(snap.size() == 3);
    assert(snap[0] == "line6");
    assert(snap[1] == "line7");
    assert(snap[2] == "line8");
    std::cout << "PASS: test_multiple_full_rotations\n";
}

void test_snapshot_preserves_order()
{
    RingBuffer rb(10);
    for (int i = 0; i < 5; i++)
        rb.push("line" + std::to_string(i));

    auto snap = rb.snapshot();
    assert(snap.size() == 5);
    for (int i = 0; i < 5; i++)
        assert(snap[i] == "line" + std::to_string(i));
    std::cout << "PASS: test_snapshot_preserves_order\n";
}

void test_push_empty_string()
{
    RingBuffer rb(3);
    rb.push("");
    rb.push("not empty");
    rb.push("");

    auto snap = rb.snapshot();
    assert(snap.size() == 3);
    assert(snap[0] == "");
    assert(snap[1] == "not empty");
    assert(snap[2] == "");
    std::cout << "PASS: test_push_empty_string\n";
}

void test_push_long_string()
{
    RingBuffer rb(2);
    std::string long_line(10000, 'X');
    rb.push(long_line);

    auto snap = rb.snapshot();
    assert(snap.size() == 1);
    assert(snap[0] == long_line);
    std::cout << "PASS: test_push_long_string\n";
}

void test_tail_after_overflow()
{
    RingBuffer rb(3);
    for (int i = 0; i < 6; i++)
        rb.push("line" + std::to_string(i));

    auto t = rb.tail(2);
    assert(t.size() == 2);
    assert(t[0] == "line4");
    assert(t[1] == "line5");
    std::cout << "PASS: test_tail_after_overflow\n";
}

int main()
{
    test_push_and_snapshot();
    test_overflow_overwrites_oldest();
    test_tail();
    test_tail_larger_than_count();
    test_capacity_zero_clamps_to_one();
    test_capacity_one();
    test_empty_buffer_snapshot();
    test_empty_buffer_tail();
    test_tail_zero();
    test_tail_exact_count();
    test_multiple_full_rotations();
    test_snapshot_preserves_order();
    test_push_empty_string();
    test_push_long_string();
    test_tail_after_overflow();
    std::cout << "\nAll 15 tests passed!\n";
    return 0;
}
