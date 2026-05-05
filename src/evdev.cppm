module;
#include "exception.h"
#include "types.h"
#include <errno.h>
#include <linux/input.h>
#include <string>
#include <unistd.h>

import event;
import logger;
import timer;

export module evdev;

export class EvDev;

export class RouterIf : virtual public TimerIf {
public:
    enum DeviceType : std::uint16_t { keyboard = 0x1u, mouse = 0x2u };

    virtual ~RouterIf() {};
    virtual void handleInputEvent(EvDev const *src, linux_input_event const ev[], std::size_t const count) = 0;
    virtual void handleError(EvDev const *dev) = 0;
    virtual void fromNetwork(DeviceType const type, std::uint16_t const dest, linux_input_event const ev[],
                             std::size_t const count) = 0;

private:
    virtual void timeout() = 0;
};

export class EvDev : virtual public Runnable {

public:
    static constexpr char const *const devUinput = "/dev/uinput";
    static constexpr std::size_t MaxEventsPerRead = 16u;

public:
    EvDev() = delete;
    EvDev(RouterIf &newRouter);
    virtual ~EvDev() = 0; // pure virtual class

public:
    void readAndRoute();

public:
    void write(linux_input_event const ev[], std::size_t const count) const;

protected:
    virtual void handleError() override;
    virtual void handleRead() override;
    virtual void handleWrite() override;
    virtual bool hasPolledOut() const override;
    virtual bool hasRead() const override = 0;
    virtual int getLastError() const override;
    virtual void error() { throw std::runtime_error("Not supported for EvDev"); }
    virtual void preShutdown() override {}

protected:
    RouterIf &router;

public:
    enum KeyStateValues : std::int32_t { Up = 0, Down = 1, Repeat = 2 };
    enum Leds : std::uint8_t { CapsLock = 0x1u, ScrollLock = 0x2u, NumLock = 0x4u };
};

EvDev::EvDev(RouterIf &newRouter) : router(newRouter) {}

EvDev::~EvDev() {}

// All non-overidden errors are fatal.
void EvDev::handleError() { throw new StringException("EvDev::handleError()"); }
void EvDev::handleWrite() { throw new StringException("EvDev::handleWrite()"); }
bool EvDev::hasPolledOut() const { return false; }
int EvDev::getLastError() const { return -1; }

void EvDev::handleRead() {
    struct input_event ev[MaxEventsPerRead];
    for (;;) {
        auto const nr = ::read(fd, &ev[0], sizeof ev);
        if (nr < 0) {
            if (nr == -1 && (errno == EINTR || errno == EAGAIN)) {
                return;
            } else {
                throw StringException("fatal: EvDev::handleRead() " + std::to_string(errno));
                return;
            }
        }

        auto numEvents = static_cast<std::size_t>(nr) / sizeof ev[0];

        throwIf(numEvents == 0 || (static_cast<std::size_t>(nr) % sizeof ev[0] != 0),
                StringException("Read len wrong"));
        if (sizeof(ev) / sizeof(ev[0]) > numEvents) {
            logDebug("Virtual device got input");
            return;
        }
    }
}

void EvDev::write(linux_input_event const ev[], std::size_t const count) const {
    auto const writeSize = count * sizeof ev[0];
    auto const nw = ::write(fd, &ev[0], writeSize);
    if (nw != static_cast<std::remove_cv_t<decltype(nw)>>(writeSize)) {
        if (nw == -1 && (errno == EINTR || errno == EWOULDBLOCK)) {
            throw StringException("Blocking writes not supported.");
        } else {
            throw StringException("Device disconnected errno " + std::to_string(errno));
        }
    }
}
