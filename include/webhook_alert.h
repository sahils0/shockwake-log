#pragma once

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

// WebhookAlert: Sends alerts to Discord/Slack via HTTP POST
// Uses libcurl for HTTPS requests
// Runs asynchronously in background thread to avoid blocking main loop
class WebhookAlert
{
public:
    explicit WebhookAlert(const std::string &webhook_url);
    ~WebhookAlert();

    void enqueue(const std::string &payload);

    void start();

    void stop();

private:
    void worker_loop();
    bool send_post(const std::string &payload);

    std::string webhook_url_;
    std::queue<std::string> alert_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_;
    std::atomic<bool> running_;
};
