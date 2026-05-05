module;
#include "exception.h"
#include <cstring>
#include <fcntl.h>
#include <linux/uinput.h>
#include <unistd.h>

import evdev;

export module kbdinputdev;

export class KbdInputDev final : public EvDev {
public:
    KbdInputDev() = delete;
    KbdInputDev(unsigned short const num, char const *const devName, RouterIf& newRouter);
    virtual ~KbdInputDev();
    bool hasRead() const override { return false; }
};

KbdInputDev::KbdInputDev(unsigned short const num, char const *const devName, RouterIf& newRouter) :
            Runnable(::open(devUinput, O_WRONLY | O_NONBLOCK)),
            EvDev(newRouter) {
    throwIf(fd < 0, StringException("check /dev/uinput for keyboard"));

    throwIf(0 > ::ioctl(fd, UI_SET_EVBIT, EV_SYN), StringException(std::string("EV_KEY")));
    throwIf(0 > ::ioctl(fd, UI_SET_EVBIT, EV_KEY), StringException(std::string("EV_KEY")));
    throwIf(0 > ::ioctl(fd, UI_SET_EVBIT, EV_REP), StringException(std::string("EV_REP")));
    throwIf(0 > ::ioctl(fd, UI_SET_EVBIT, EV_LED), StringException(std::string("EV_LED")));
    throwIf(0 > ::ioctl(fd, UI_SET_EVBIT, EV_MSC), StringException(std::string("EV_MSC")));
    throwIf(0 > ::ioctl(fd, UI_SET_LEDBIT, LED_NUML), StringException(std::string("LED_NUML")));
    throwIf(0 > ::ioctl(fd, UI_SET_LEDBIT, LED_CAPSL), StringException(std::string("LED_CAPSL")));
    throwIf(0 > ::ioctl(fd, UI_SET_LEDBIT, LED_SCROLLL), StringException(std::string("LED_SCROLLL")));
    for (auto i = KEY_ESC; i <= KEY_F24; ++i) {
        throwIf(0 > ::ioctl(fd, UI_SET_KEYBIT, i), StringException(std::string("UI_SET_KEYBIT0 " + std::to_string(i))));
    }
    for (auto i = KEY_OK; i <= KEY_NUMERIC_D; ++i) {
        throwIf(0 > ::ioctl(fd, UI_SET_KEYBIT, i), StringException(std::string("UI_SET_KEYBIT1 " + std::to_string(i))));
    }
    const auto devNameLen = std::strlen(devName);
    struct uinput_setup dev{};
    dev.id.bustype = BUS_USB;
    dev.id.vendor = 0xbeef;
    dev.id.product = num;
    dev.id.version = 0;
    throwIf(sizeof(dev.name) <= (devNameLen + 1), StringException("Retared long dev name"));
    throwIf(nullptr == ::strncpy(dev.name, devName, sizeof(dev.name) - 1), StringException("Seriously?"));
    throwIf(0 > ::ioctl(fd, UI_DEV_SETUP, &dev), StringException(std::string("UI_DEV_SETUP")));
    throwIf(0 > ::ioctl(fd, UI_DEV_CREATE), StringException(std::string("UI_DEV_CREATE")));
}

KbdInputDev::~KbdInputDev() {
    if (fd >= 0) {
        ::ioctl(fd, UI_DEV_DESTROY);
        // ::close(fd);
    }
}
