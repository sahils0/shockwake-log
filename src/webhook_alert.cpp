#include "webhook_alert.h"
#include <curl/curl.h>
#include <iostream>

// Callback to discard response body
static size_t write_callback(void *, size_t size, size_t nmemb, void *)
{
    return size * nmemb;
}

WebhookAlert::WebhookAlert(const std::string &webhook_url)
    : webhook_url_(webhook_url), running_(false)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

WebhookAlert::~WebhookAlert()
{
    stop();
    curl_global_cleanup();
}

void WebhookAlert::enqueue(const std::string &payload)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        alert_queue_.push(payload);
    }
    queue_cv_.notify_one();
}

void WebhookAlert::start()
{
    running_ = true;
    worker_ = std::thread(&WebhookAlert::worker_loop, this);
}

void WebhookAlert::stop()
{
    running_ = false;
    queue_cv_.notify_one();
    if (worker_.joinable())
        worker_.join();
}

void WebhookAlert::worker_loop()
{
    while (running_)
    {
        std::string payload;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]()
                           { return !alert_queue_.empty() || !running_; });

            if (!running_ && alert_queue_.empty())
                return;

            payload = alert_queue_.front();
            alert_queue_.pop();
        }
        send_post(payload);
    }
}

bool WebhookAlert::send_post(const std::string &payload)
{
    CURL *curl = curl_easy_init();
    if (!curl)
        return false;

    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, webhook_url_.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}
