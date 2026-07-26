#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <pthread.h>

struct PendingAlert {
    std::string payload;
    int attempt;
};

// WebhookAlert: Sends alerts to Discord/Slack via HTTP POST
// Uses libcurl for HTTPS requests
// Runs asynchronously in background thread to avoid blocking main loop
// Retries failed alerts with configurable max attempts and backoff
class WebhookAlert
{
public:
    explicit WebhookAlert(const std::string &webhook_url, int max_retries = 3, int retry_delay_ms = 1000, bool ssl_verify = true);
    ~WebhookAlert();

    void enqueue(const std::string &payload);

    void start();

    void stop();

    void worker_loop();

    unsigned long dropped_count() const { return dropped_count_.load(); }

private:
    bool send_post(const std::string &payload);

    std::string webhook_url_;
    int max_retries_;
    int retry_delay_ms_;
    bool ssl_verify_;
    std::queue<PendingAlert> alert_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    pthread_t worker_thread_;
    std::atomic<bool> running_;
    std::atomic<unsigned long> dropped_count_;
};
