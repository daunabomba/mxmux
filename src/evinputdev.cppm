module;

#include "exception.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <string>
#include <unistd.h>

import logger;
import evdev;
import utils;

export module evinputdev;

export class EvInputDev final : public EvDev {
public:
    EvInputDev() = delete;
    EvInputDev(char const *const name, RouterIf &newRouter);
    static int tryConnect(char const *const name);

    virtual ~EvInputDev();
    void setLeds(unsigned char ledsState) const;
    void handleError() override;
    void handleRead() override;
    bool hasRead() const override { return true;};

};

EvInputDev::EvInputDev(char const *const name, RouterIf &newRouter)
    : Runnable(EvInputDev::tryConnect(name)), EvDev(newRouter) {}

int EvInputDev::tryConnect(char const *const devName) {
    auto newFd = ::open(devName, O_RDWR | O_NONBLOCK);
    if (newFd < 0) {
        throwErrnoException(std::string("Unable to open " + std::string(devName)));
    }

    throwIf(0 > ::ioctl(newFd, EVIOCGRAB, true), StringException(std::string("Could not grab ")));

    Logger::logCritical("EvInputDev::tryConnect " + std::string(devName) + " connected on " + std::to_string(newFd));
    return newFd;
}

EvInputDev::~EvInputDev() {}

void EvInputDev::setLeds(unsigned char ledsState) const {
    struct input_event ev;
    ev.type = EV_LED;
    ev.code = LED_CAPSL;
    if (EvDev::CapsLock == (EvDev::CapsLock & ledsState)) {
        ev.value = 1u;
    } else {
        ev.value = 0u;
    }
    throwIf(0 > ::write(fd, &ev, sizeof(ev)), StringException("Cannot set CAPSLOCK"));

    ev.code = LED_NUML;
    if (EvDev::NumLock == (EvDev::NumLock & ledsState)) {
        ev.value = 1u;
    } else {
        ev.value = 0u;
    }
    throwIf(0 > ::write(fd, &ev, sizeof(ev)), StringException("Cannot set MUMLOCK"));

    ev.code = LED_SCROLLL;
    if (EvDev::ScrollLock == (EvDev::ScrollLock & ledsState)) {
        ev.value = 1u;
    } else {
        ev.value = 0u;
    }
    throwIf(0 > ::write(fd, &ev, sizeof(ev)), StringException("Cannot set SCROLLLOCK"));
}

void EvInputDev::handleError() {
    logDebug("EvInputDev::error");
    router.handleError(this);
}

void EvInputDev::handleRead() {
    struct input_event ev[MaxEventsPerRead];
    for (;;) {
        auto const nr = ::read(fd, &ev[0], sizeof(ev));
        if (nr < 0) {
            if (nr == -1 && (errno == EINTR || errno == EAGAIN)) {
                return;
            } else {
                throw StringException("fatal: EvInputDev::handleRead()");
                return;
            }
        }

        auto numEvents = static_cast<std::size_t>(nr) / sizeof(ev[0]);

        throwIf(numEvents == 0 || (static_cast<std::size_t>(nr) % sizeof(ev[0]) != 0), StringException("Read len wrong"));
        router.handleInputEvent(this, ev, numEvents);
        if (sizeof(ev) / sizeof(ev[0]) > numEvents) {
            return;
        }
    }
}
