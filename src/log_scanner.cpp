#include "log_scanner.h"
#include <cstring>
#include <string>

LogScanner::~LogScanner() = default;

// Convert PCRE shorthands (\s, \d, \w) to POSIX equivalents before regcomp
static std::string pcre_to_posix(const std::string& pattern) {
    std::string result;
    result.reserve(pattern.size() * 4);
    for (size_t i = 0; i < pattern.size(); i++) {
        if (pattern[i] == '\\' && i + 1 < pattern.size()) {
            char next = pattern[i + 1];
            switch (next) {
                case 's': result += "[[:space:]]"; i++; break;
                case 'd': result += "[0-9]"; i++; break;
                case 'w': result += "[A-Za-z0-9_]"; i++; break;
                case 'S': result += "[^[:space:]]"; i++; break;
                case 'D': result += "[^0-9]"; i++; break;
                case 'W': result += "[^A-Za-z0-9_]"; i++; break;
                default: result += pattern[i]; break;
            }
        } else {
            result += pattern[i];
        }
    }
    return result;
}

void LogScanner::PosixRegex::compile() {
    free();
    std::string posix_pattern = pcre_to_posix(pattern);
    int rc = regcomp(&compiled, posix_pattern.c_str(), REG_EXTENDED | REG_ICASE | REG_NOSUB);
    valid = (rc == 0);
}

void LogScanner::PosixRegex::free() {
    if (valid) {
        regfree(&compiled);
        valid = false;
    }
}

bool LogScanner::PosixRegex::match(const std::string& s) const {
    if (!valid) return false;
    return regexec(&compiled, s.c_str(), 0, nullptr, 0) == 0;
}

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
            PosixRegex r;
            r.pattern = trigger;
            r.compile();
            if (r.valid) {
                regex_triggers_.push_back(std::move(r));
            } else {
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
            PosixRegex r;
            r.pattern = excl;
            r.compile();
            if (r.valid) {
                regex_excludes_.push_back(std::move(r));
            } else {
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
    for (const auto& regex : regex_excludes_) {
        if (regex.match(line_str))
            return false;
    }

    for (const auto& trigger : plain_triggers_) {
        if (line.find(trigger) != std::string_view::npos) {
            last_match_ = trigger;
            return true;
        }
    }

    for (const auto& regex : regex_triggers_) {
        if (regex.match(line_str)) {
            last_match_ = regex.pattern;
            return true;
        }
    }

    return false;
}

const std::string& LogScanner::matched_keyword() const {
    return last_match_;
}
