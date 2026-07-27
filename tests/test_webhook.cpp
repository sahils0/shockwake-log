#include "webhook_alert.h"
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

void test_dropped_count_initially_zero() {
    WebhookAlert wa("http://localhost:9999/test");
    assert(wa.dropped_count() == 0);
    std::cout << "PASS: test_dropped_count_initially_zero\n";
}

void test_single_retry_drops() {
    WebhookAlert wa("http://localhost:9999/test", 1, 100);
    wa.start();

    wa.enqueue("{\"text\": \"alert 1\"}");
    wa.enqueue("{\"text\": \"alert 2\"}");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    wa.stop();

    assert(wa.dropped_count() == 2);
    std::cout << "PASS: test_single_retry_drops (dropped=" << wa.dropped_count() << ")\n";
}

void test_no_retries_drops_immediately() {
    WebhookAlert wa("http://localhost:9999/test", 1, 50);
    wa.start();

    wa.enqueue("{\"text\": \"single attempt\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    wa.stop();

    assert(wa.dropped_count() >= 1);
    std::cout << "PASS: test_no_retries_drops_immediately (dropped=" << wa.dropped_count() << ")\n";
}

void test_stop_with_pending_queue() {
    WebhookAlert wa("http://localhost:9999/test", 1, 50);
    wa.start();

    for (int i = 0; i < 10; i++)
        wa.enqueue("{\"text\": \"alert " + std::to_string(i) + "\"}");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    wa.stop();

    assert(wa.dropped_count() >= 1);
    std::cout << "PASS: test_stop_with_pending_queue (dropped=" << wa.dropped_count() << ")\n";
}

void test_start_stop_start_stop() {
    WebhookAlert wa("http://localhost:9999/test", 1, 50);

    wa.start();
    wa.enqueue("{\"text\": \"round 1\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    wa.stop();
    unsigned long d1 = wa.dropped_count();
    assert(d1 >= 1);

    wa.start();
    wa.enqueue("{\"text\": \"round 2\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    wa.stop();
    unsigned long d2 = wa.dropped_count();

    assert(d2 > d1);
    std::cout << "PASS: test_start_stop_start_stop (d1=" << d1 << " d2=" << d2 << ")\n";
}

void test_constructor_params() {
    WebhookAlert wa("http://localhost:9999/test", 1, 50, false);
    assert(wa.dropped_count() == 0);
    wa.start();
    wa.enqueue("{\"text\": \"test\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    wa.stop();
    assert(wa.dropped_count() >= 1);
    std::cout << "PASS: test_constructor_params (dropped=" << wa.dropped_count() << ")\n";
}

void test_enqueue_after_stop() {
    WebhookAlert wa("http://localhost:9999/test", 1, 50);
    wa.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    wa.stop();

    wa.enqueue("{\"text\": \"after stop\"}");
    assert(wa.dropped_count() == 0);
    std::cout << "PASS: test_enqueue_after_stop\n";
}

int main() {
    test_dropped_count_initially_zero();
    test_single_retry_drops();
    test_no_retries_drops_immediately();
    test_stop_with_pending_queue();
    test_start_stop_start_stop();
    test_constructor_params();
    test_enqueue_after_stop();

    std::cout << "\nAll 7 tests passed!\n";
    return 0;
}
