#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <regex.h>

class LogScanner {
public:
    void set_triggers(const std::vector<std::string>& triggers);
    void set_excludes(const std::vector<std::string>& excludes);
    bool scan(std::string_view line) const;
    const std::string& matched_keyword() const;

    ~LogScanner();

private:
    static bool is_regex_pattern(const std::string& trigger);

    struct PosixRegex {
        std::string pattern;
        regex_t compiled{};
        bool valid = false;
        PosixRegex() = default;
        PosixRegex(const PosixRegex& o) : pattern(o.pattern) { compile(); }
        PosixRegex& operator=(const PosixRegex& o) {
            free();
            pattern = o.pattern;
            compile();
            return *this;
        }
        ~PosixRegex() { free(); }
        void compile();
        void free();
        bool match(const std::string& s) const;
    };

    std::vector<std::string> plain_triggers_;
    std::vector<PosixRegex> regex_triggers_;
    std::vector<std::string> plain_excludes_;
    std::vector<PosixRegex> regex_excludes_;
    mutable std::string last_match_;
};
