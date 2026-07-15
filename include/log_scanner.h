#pragma once

#include <string>
#include <string_view>
#include <vector>

// LogScanner: Fast pattern matching on log lines
// Uses std::string_view to avoid copying strings during search
// Checks each line against configured trigger keywords
class LogScanner
{
public:
    void set_triggers(const std::vector<std::string> &triggers);

    bool scan(std::string_view line) const;

    const std::string &matched_keyword() const;

private:
    std::vector<std::string> triggers_;
    mutable std::string last_match_;
};
