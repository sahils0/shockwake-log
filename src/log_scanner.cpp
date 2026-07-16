#include "log_scanner.h"

bool LogScanner::is_regex_pattern(const std::string& trigger) {
    for (size_t i = 0; i < trigger.size(); i++) {
        char c = trigger[i];
        if (c == '\\' || c == '*' || c == '+' || c == '?' ||
            c == '[' || c == ']' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '^' || c == '$' || c == '|')
            return true;
        if (c == '.' && i + 1 < trigger.size()) {
            char next = trigger[i + 1];
            if (next == '*' || next == '+' || next == '?')
                return true;
        }
    }
    return false;
}

void LogScanner::set_triggers(const std::vector<std::string>& triggers) {
    plain_triggers_.clear();
    regex_triggers_.clear();

    for (const auto& trigger : triggers) {
        if (is_regex_pattern(trigger)) {
            try {
                regex_triggers_.emplace_back(trigger, std::regex(trigger, std::regex::icase));
            } catch (const std::regex_error&) {
                plain_triggers_.push_back(trigger);
            }
        } else {
            plain_triggers_.push_back(trigger);
        }
    }
}

bool LogScanner::scan(std::string_view line) const {
    for (const auto& trigger : plain_triggers_) {
        if (line.find(trigger) != std::string_view::npos) {
            last_match_ = trigger;
            return true;
        }
    }

    std::string line_str(line);
    for (const auto& [pattern, regex] : regex_triggers_) {
        if (std::regex_search(line_str, regex)) {
            last_match_ = pattern;
            return true;
        }
    }

    return false;
}

const std::string& LogScanner::matched_keyword() const {
    return last_match_;
}
