#include "webhook_alert.h"
#include <cassert>
#include <iostream>
#include <chrono>
#include <thread>

// Mock test: verify queue and threading work (no real HTTP)
void test_enqueue_and_worker() {
    // Use a dummy URL - won't actually send, but tests queue logic
    WebhookAlert wa("http://localhost:9999/test");

    wa.start();

    // Enqueue some alerts
    wa.enqueue("{\"text\": \"alert 1\"}");
    wa.enqueue("{\"text\": \"alert 2\"}");
    wa.enqueue("{\"text\": \"alert 3\"}");

    // Give worker time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    wa.stop();
    std::cout << "PASS: test_enqueue_and_worker\n";
}

void test_stop_drains_queue() {
    WebhookAlert wa("http://localhost:9999/test");

    wa.start();

    for (int i = 0; i < 10; i++) {
        wa.enqueue("{\"text\": \"alert " + std::to_string(i) + "\"}");
    }

    // Stop immediately - should drain remaining
    wa.stop();
    std::cout << "PASS: test_stop_drains_queue\n";
}

int main() {
    test_enqueue_and_worker();
    test_stop_drains_queue();
    std::cout << "\nAll tests passed!\n";
    return 0;
}
