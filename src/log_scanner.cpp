#include "log_scanner.h"

void LogScanner::set_triggers(const std::vector<std::string> &triggers)
{
    triggers_ = triggers;
}

bool LogScanner::scan(std::string_view line) const
{
    for (const auto &trigger : triggers_)
    {
        if (line.find(trigger) != std::string_view::npos)
        {
            last_match_ = trigger;
            return true;
        }
    }
    return false;
}

const std::string &LogScanner::matched_keyword() const
{
    return last_match_;
}