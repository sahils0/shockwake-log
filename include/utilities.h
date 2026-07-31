#pragma once

#include "config.h"
#include <atomic>
#include <ctime>
#include <string>
#include <vector>

extern std::atomic<bool> g_running;
extern std::atomic<unsigned long> g_alert_count;
extern std::atomic<unsigned long> g_lines_processed;

std::string get_timestamp();
std::string get_uptime(time_t start);
std::string escape_json(const std::string &s);
std::string build_webhook_payload(const std::string &keyword,
                                  const std::string &log_file,
                                  const std::string &incident_file);
std::string write_incident(const Config &config, const std::string &keyword,
                           const std::vector<std::string> &context,
                           const std::vector<std::string> &trailing);
bool drop_privileges(const std::string &username);
int count_incidents(const std::string &dir);
pid_t read_pid_file(const std::string &pid_file);
bool wait_for_process_exit(pid_t pid, int timeout_ms);
void print_config_summary(const Config &config, const std::string &prefix);
