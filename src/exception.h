#pragma once

#include <stdexcept>
#include <string>

class StringException : public std::runtime_error {
public:
    explicit StringException(const std::string &message) : std::runtime_error(message), s(message) {}
    char const *why() const throw() { return s.c_str(); }

private:
    std::string const s;
};

inline void throwIf(bool const cond, StringException const &e) {
    if (cond) {
        throw e;
    }
}
