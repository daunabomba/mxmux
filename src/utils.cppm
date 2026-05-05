module;
#include "types.h"
#include "exception.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <cxxabi.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/epoll.h>

import logger;

export module utils;

export void throwErrnoException(std::string const& text) {
    auto errorText = text + " " + std::string(::strerror(errno));
    throw(StringException(errorText));
}

export void pErrorThrow(std::string const& loc, int const error, int const fd = -1) {
    if (error < 0) {
        auto errorText = loc + " " + std::string(::strerror(errno));
        if (fd > 0) {
            errorText += " " + std::to_string(fd);
        }
        logError(errorText);
        assert(false);
    }
}

export void pErrorLog(std::string const loc, int const error, int const fd) {
    if (error < 0) {
        auto errorText = std::string("pErrorLog: ") + loc + " " + std::string(::strerror(errno));
        if (fd > 0) {
            errorText += " " + std::to_string(fd);
        }
        logError(errorText);
    }
}

export void throwIf(bool const error, std::string const errorText) {
    if (error) {
        logError(errorText);
        assert(false);
    }
}

export std::string demangle(char const *const mangled_name) {
    int status = -1;

    // Use unique_ptr with custom deleter to automatically free memory allocated by __cxa_demangle
    std::unique_ptr<char, void (*)(void *)> demangled(abi::__cxa_demangle(mangled_name, nullptr, nullptr, &status),
                                                      std::free);

    return (status == 0) ? demangled.get() : mangled_name;
}
