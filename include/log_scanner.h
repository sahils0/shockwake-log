#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <regex>

class LogScanner {
public:
    void set_triggers(const std::vector<std::string>& triggers);
    bool scan(std::string_view line) const;
    const std::string& matched_keyword() const;

private:
    static bool is_regex_pattern(const std::string& trigger);

    std::vector<std::string> plain_triggers_;
    std::vector<std::pair<std::string, std::regex>> regex_triggers_;
    mutable std::string last_match_;
};
