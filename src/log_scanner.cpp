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

void LogScanner::set_excludes(const std::vector<std::string>& excludes) {
    plain_excludes_.clear();
    regex_excludes_.clear();

    for (const auto& excl : excludes) {
        if (is_regex_pattern(excl)) {
            try {
                regex_excludes_.emplace_back(excl, std::regex(excl, std::regex::icase));
            } catch (const std::regex_error&) {
                plain_excludes_.push_back(excl);
            }
        } else {
            plain_excludes_.push_back(excl);
        }
    }
}

bool LogScanner::scan(std::string_view line) const {
    for (const auto& excl : plain_excludes_) {
        if (line.find(excl) != std::string_view::npos)
            return false;
    }

    std::string line_str(line);
    for (const auto& [pattern, regex] : regex_excludes_) {
        if (std::regex_search(line_str, regex))
            return false;
    }

    for (const auto& trigger : plain_triggers_) {
        if (line.find(trigger) != std::string_view::npos) {
            last_match_ = trigger;
            return true;
        }
    }

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
