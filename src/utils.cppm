module;
#include "types.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <errno.h>
#include <string.h>
#include <cassert>
#include <sys/epoll.h>

import logger;

export module utils;

export void pErrorThrow(std::string const loc, int const error, int const fd = -1);
export void pErrorLog(std::string const loc, int const error, int const fd);

void pErrorThrow(std::string const loc, int const error, int const fd) {
    if (error < 0) {
        auto errorText = loc + " " + std::string(::strerror(errno));
        if (fd > 0) {
            errorText += " " + std::to_string(fd);
        }
        logError(errorText);
        assert(false);
    }
}

void pErrorLog(std::string const loc, int const error, int const fd) {
    if (error < 0) {
        auto errorText = std::string("pErrorLog: ") + loc + " " + std::string(::strerror(errno));
        if (fd > 0) {
            errorText += " " + std::to_string(fd);
        }
        logError(errorText);
    }
}
