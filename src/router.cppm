module;

#include "exception.h"
#include "types.h"
#include <sys/timerfd.h>

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

import evinputdev;
import evdev;
import event;
import engine;
import logger;
import timer;
import kbdinputdev;
import mouseinputdev;
import mxsrv;
import socket;
export module router;

using namespace std::chrono_literals;

export class Router;

class ShutdownHandler : virtual public Runnable {
    friend class Engine;

public:
    static std::shared_ptr<ShutdownHandler> create(Router &rt);

public:
    ShutdownHandler() = delete;
    explicit ShutdownHandler(int const newFd, Router &rt);
    virtual ~ShutdownHandler() {}

protected:
    void handleError() override { throw std::runtime_error("Not supported for ShutdownHandler"); }
    void handleRead() override { throw std::runtime_error("handleRead should never run for ShutdownHandler"); }
    void handleWrite() override { throw std::runtime_error("Not supported for ShutdownHandler"); }
    bool hasPolledOut() const override { return false; }
    bool hasRead() const override { return true; }
    void preShutdown() override;
    int getLastError() const override { throw std::runtime_error("Not supported for ShutdownHandler"); }

private:
    Router &router;
};

export class Router : virtual public RouterIf {
public:
    Router() = delete;
    Router(char const *const inKbName, char const *const inMouseName);
    virtual ~Router();
    void init(std::weak_ptr<MxSrvIf> const &mx);
    // From RouterIf
    void handleInputEvent(EvDev const *src, linux_input_event const ev[], std::size_t const count) override;
    void handleError(EvDev const *dev) override;
    void fromNetwork(DeviceType const type, std::uint16_t const dest, linux_input_event const ev[],
                     std::size_t const count) override;

    void preShutdown();

protected:
    void timeout() override; // from timerif

private:
    void processKbdInput(linux_input_event const ev[], std::size_t const count);
    void processMouseInput(linux_input_event const ev[], std::size_t const count);
    void handleKeyDown(linux_input_event const &ev);
    void handleKeyUp(linux_input_event const &ev);
    bool handleLedChanges(linux_input_event const &ev);

private:
    static constexpr std::size_t NumLocalEvDevices = 3u;
    static constexpr std::size_t NumRemoteDevices = 6u;
    static constexpr std::size_t TotalDevices = NumLocalEvDevices + NumRemoteDevices;
    static constexpr std::uint16_t UnsetChangeIndex = 0xffff;

private:
    char const *const inKbdDevName;
    char const *const inMouseDevName;

    std::shared_ptr<EvDev> inMouse;
    std::shared_ptr<EvDev> inBoard;
    std::array<std::shared_ptr<EvDev>, NumLocalEvDevices> outMice;
    std::array<std::shared_ptr<EvDev>, NumLocalEvDevices> outBoards;
    std::array<unsigned char, TotalDevices> outLeds;
    std::uint16_t activeOutIndex = NumLocalEvDevices + 1u;
    std::uint16_t changeToIndex = UnsetChangeIndex;

    std::vector<linux_input_event> keyStoredEvents;
    bool hotKeySequenceActive = false;
    std::shared_ptr<PeriodicTimer> connectTimer;
    std::mutex routerLock;
    linux_input_event evSynRep;
    std::weak_ptr<MxSrvIf> mxif;
    std::set<decltype(std::declval<linux_input_event>().code)> keyDownEvents;
    std::shared_ptr<ShutdownHandler> shutdownHandler;
};

ShutdownHandler::ShutdownHandler(int const newFd, Router &rt) : Runnable(newFd), router(rt) {}

void ShutdownHandler::preShutdown() {
    logError("ShutdownHandler::preShutdown");
    router.preShutdown();
}

std::shared_ptr<ShutdownHandler> ShutdownHandler::create(Router &rt) {
    auto const timerFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    itimerspec newTimer{};

    newTimer.it_value.tv_sec = 0;
    newTimer.it_value.tv_nsec = 0;

    throwIf(::timerfd_settime(timerFd, 0, &newTimer, nullptr), StringException("ShutdownHandler timer settime"));

    auto timer = std::make_shared<ShutdownHandler>(timerFd, rt);
    Engine::add(timer);
    return timer;
}

Router::Router(char const *const inKbName, char const *const inMouseName)
    : inKbdDevName(inKbName), inMouseDevName(inMouseName) {
    evSynRep.type = EV_SYN;
    evSynRep.code = SYN_REPORT;
    evSynRep.value = 0;
}

void Router::init(std::weak_ptr<MxSrvIf> const &mx) {
    shutdownHandler = ShutdownHandler::create(*this);
    mxif = mx;
    for (auto i = 0u; i < NumLocalEvDevices; ++i) {
        outLeds[i] = 0u;
        try {
            outBoards[i] = std::make_shared<KbdInputDev>(i, ("mxev-kbd-event-" + std::to_string(i)).c_str(), *this);
            Engine::add(outBoards[i]);
        } catch (StringException const &ex) {
            logError("Disabling outBoards[" + std::to_string(i) + "] because " + std::string(ex.why()));
        }
    }

    for (auto i = 0u; i < NumLocalEvDevices; ++i) {
        try {
            outMice[i] = std::make_shared<MouseInputDev>(i, ("mxev-mouse-event-" + std::to_string(i)).c_str(), *this);
            Engine::add(outMice[i]);
        } catch (StringException const &ex) {
            logError("Disabling outMice[" + std::to_string(i) + "] because " + std::string(ex.why()));
        }
    }
    for (auto i = NumLocalEvDevices; i < NumRemoteDevices; ++i) {
        outLeds[i] = 0u;
    }
    connectTimer = PeriodicTimer::create(40000ns, 1s + 10ns, shared_from_this());
    auto ref = mxif.lock();
    if (ref) {
        // all 1 based. ie key 1 maps to local 0 etc. and remote 1 maps to remote 0 etc
        ref->addRemote(NumLocalEvDevices,     1, Socket::destFromString("192.168.1.2", MXEV_PORT));
        ref->addRemote(NumLocalEvDevices + 1, 2, Socket::destFromString("192.168.1.2", MXEV_PORT));
        ref->addRemote(NumLocalEvDevices + 2, 3, Socket::destFromString("192.168.1.2", MXEV_PORT));
        ref->addRemote(NumLocalEvDevices + 3, 1, Socket::destFromString("192.168.1.3", MXEV_PORT));
        ref->addRemote(NumLocalEvDevices + 4, 2, Socket::destFromString("192.168.1.3", MXEV_PORT));
        ref->addRemote(NumLocalEvDevices + 5, 3, Socket::destFromString("192.168.1.3", MXEV_PORT));
    }
}

Router::~Router() { logDebug("Router::~Router()"); }

void Router::preShutdown() {
    logDebug("Num key down events " + std::to_string(keyDownEvents.size()));
    if (activeOutIndex <= NumLocalEvDevices) {
        auto &ref = outBoards[activeOutIndex - 1u];
        for (auto const &c : keyDownEvents) {
            linux_input_event e{};
            e.type = EV_KEY;
            e.code = c;
            e.value = EvDev::KeyStateValues::Up;
            if (ref) {
                ref->write(&e, 1);
            } else {
                logError("Outboard not created - check uinput");
            }
        }
        if (ref) {
            ref->write(&evSynRep, 1);
        } else {
            logError("No local (uinput perms?) outboard " + std::to_string(activeOutIndex));
        }
    } else if (activeOutIndex <= TotalDevices) {
        decltype(activeOutIndex) const remoteIndex = activeOutIndex - static_cast<decltype(activeOutIndex)>(1u);
        auto ref = mxif.lock();
        for (auto const &c : keyDownEvents) {
            linux_input_event e{};
            e.type = EV_KEY;
            e.code = c;
            e.value = EvDev::KeyStateValues::Up;
            if (ref) {
                ref->sendToRemote(DeviceType::keyboard, remoteIndex, &e, 1);
            } else {
                logError("No remote outboard " + std::to_string(remoteIndex));
            }
        }
    }

    keyDownEvents.clear();
}

void Router::handleInputEvent(EvDev const *src, linux_input_event const ev[], std::size_t const count) {
    std::lock_guard<std::mutex> sync(routerLock);
    if (activeOutIndex == 0u) {
        logDebug("Router::handleInputEvent() with index 0");
    } else {
        // no need to lock these as these are cleared under the same routerLock in handleError()
        if (inBoard.get() == src) {
            processKbdInput(ev, count);
        } else if (inMouse.get() == src) {
            processMouseInput(ev, count);
        } else {
            throw StringException("handleInputEvent");
        }
    }
}

void Router::fromNetwork(DeviceType const type, std::uint16_t const dest, linux_input_event const ev[],
                         std::size_t const count) {
    if (type == DeviceType::keyboard) {
        // logDebug("FromNet dest keyb " + std::to_string(dest) + " count " + std::to_string(count));
        auto &ref = outBoards[dest - 1u];
        if (ref) {
            ref->write(ev, count);
            ref->write(&evSynRep, 1);
        } else {
            logError("No local outboard " + std::to_string(dest));
        }
    } else if (type == DeviceType::mouse) {
        // logDebug("FromNet dest mouse " + std::to_string(dest) + " count " + std::to_string(count));
        auto &ref = outMice[dest - 1u];
        if (ref) {
            ref->write(ev, count);
            ref->write(&evSynRep, 1);
        } else {
            logError("No local outMouse " + std::to_string(dest));
        }
    } else {
        throw("Unhandled device type in fromNetwork");
    }
}

// has routerLock
void Router::processMouseInput(linux_input_event const ev[], std::size_t const count) {
    if (activeOutIndex <= NumLocalEvDevices) {
        auto &ref = outMice[activeOutIndex - 1u];
        if (ref) {
            ref->write(ev, count);
            ref->write(&evSynRep, 1);
        } else {
            logError("No local outMice check uinput " + std::to_string(activeOutIndex));
        }
    } else if (activeOutIndex <= TotalDevices) {
        decltype(activeOutIndex) const remoteIndex = activeOutIndex - static_cast<decltype(activeOutIndex)>(1u);
        auto ref = mxif.lock();
        if (ref) {
            ref->sendToRemote(DeviceType::mouse, remoteIndex, ev, count);
        } else {
            logError("No remote outMice " + std::to_string(remoteIndex));
        }
    }
}

// void dumpEv(std::vector<linux_input_event> const &evs) {
//     logDebug("EVS size " + std::to_string(evs.size()));
//     for (auto &&e : evs) {
//         logDebug("  : " + std::to_string(e.code) + " " + std::to_string(e.value));
//     }
// }

void Router::handleKeyDown(linux_input_event const &ev) {
    // dumpEv(keyStoredEvents);
    auto it = std::find_if(keyStoredEvents.begin(), keyStoredEvents.end(), [ev](linux_input_event const &item) {
        return item.code == ev.code && item.value == ev.value;
    });
    // Don't use any LEDs for hotkeys
    if (it != keyStoredEvents.end()) {
        logDebug("Found dup keycode down in size " + std::to_string(keyStoredEvents.size()));
    } else if (!hotKeySequenceActive && keyStoredEvents.size() == 0u &&
               (ev.code == KEY_LEFTMETA || ev.code == KEY_GRAVE)) {
        keyStoredEvents.push_back(ev);
        changeToIndex = UnsetChangeIndex;
        hotKeySequenceActive = true;
    } else if (hotKeySequenceActive) {
        keyStoredEvents.push_back(ev);
        bool graveKeyDown =
            std::find_if(keyStoredEvents.begin(), keyStoredEvents.end(), [](linux_input_event const &item) {
                return item.code == KEY_GRAVE && item.value == EvDev::KeyStateValues::Down;
            }) != keyStoredEvents.end();
        bool leftMetaKeyDown =
            std::find_if(keyStoredEvents.begin(), keyStoredEvents.end(), [](linux_input_event const &item) {
                return item.code == KEY_LEFTMETA && item.value == EvDev::KeyStateValues::Down;
            }) != keyStoredEvents.end();

        if (leftMetaKeyDown && graveKeyDown) {
            if (ev.code == KEY_LEFTMETA || ev.code == KEY_GRAVE) {
                logDebug("Got LEFTMETA+GRAVE" + std::to_string(keyStoredEvents.size()) + " events");
                return;
            }
            if (KEY_1 <= ev.code && KEY_9 >= ev.code &&
                (ev.code - KEY_1) < static_cast<decltype(ev.code)>(TotalDevices)) {
                changeToIndex = static_cast<decltype(activeOutIndex)>(ev.code - KEY_1) + 1u;
                logDebug("setting changeToIndex set to " + std::to_string(changeToIndex));
                return;
            }
        }
        // fall through
        logDebug("Aborting key events with " + std::to_string(keyStoredEvents.size()) + " events");
        hotKeySequenceActive = false;
        changeToIndex = UnsetChangeIndex;
    }
}

void Router::handleKeyUp(linux_input_event const &ev) {
    // logDebug("handleKeyUp " + std::to_string(ev.code) + " " + std::to_string(ev.value));
    // dumpEv(keyStoredEvents);

    if (!hotKeySequenceActive) {
        return;
    }

    auto it = std::find_if(keyStoredEvents.begin(), keyStoredEvents.end(), [ev](linux_input_event const &item) {
        return item.code == ev.code && item.type == ev.value;
    });

    if (it != keyStoredEvents.end()) {
        logDebug("Found dup key up in " + std::to_string(keyStoredEvents.size()) + " to " +
                 std::to_string(changeToIndex));
    }
    if (changeToIndex == UnsetChangeIndex) {
        // These are output in the main loop
        hotKeySequenceActive = false;
    } else {
        auto erased = std::erase_if(keyStoredEvents, [ev](const linux_input_event &item) {
                          return item.code == ev.code && item.value == EvDev::KeyStateValues::Down;
                      }) == 1u;
        if (!erased) {
            logError("Unable to erase corresponding key " + std::to_string(ev.code));
        }
    }
}

bool Router::handleLedChanges(linux_input_event const &ev) {
    if (ev.code == KEY_CAPSLOCK) {
        outLeds[activeOutIndex - 1u] ^= EvDev::Leds::CapsLock;
        return true;
    } else if (ev.code == KEY_NUMLOCK) {
        outLeds[activeOutIndex - 1u] ^= EvDev::Leds::NumLock;
        return true;
    } else if (ev.code == KEY_SCROLLLOCK) {
        outLeds[activeOutIndex - 1u] ^= EvDev::Leds::ScrollLock;
        return true;
    }
    return false;
}

void Router::processKbdInput(linux_input_event const ev[], std::size_t const count) {
    auto prevActiveOut = activeOutIndex;
    bool ledsChanged = false;

    for (auto i = 0u; i < count; ++i) {
        if (ev[i].type == EV_KEY) {
            if (ev[i].value == EvDev::KeyStateValues::Up) {
                auto kdEv = keyDownEvents.find(ev[i].code);
                if (kdEv != keyDownEvents.end()) {
                    keyDownEvents.erase(kdEv);
                } else {
                    logError("Key " + std::to_string(ev[i].code) + " not found in keyDownEvents");
                }
                handleKeyUp(ev[i]);
            } else if (ev[i].value == EvDev::KeyStateValues::Down) {
                auto kdEv = keyDownEvents.find(ev[i].code);
                if (kdEv == keyDownEvents.end()) {
                    keyDownEvents.insert(ev[i].code);
                } else {
                    logError("Key " + std::to_string(ev[i].code) + " aleady exists in keyDownEvents");
                }
                handleKeyDown(ev[i]);
                ledsChanged = handleLedChanges(ev[i]); // LEDs cannot be hotkeys
            }
            // don't store repeat events
        }
    }
    
    bool wakeupRemote = false;
    if (changeToIndex != UnsetChangeIndex && hotKeySequenceActive && keyStoredEvents.size() == 0u) {
        logDebug("Effecting Changing from " + std::to_string(prevActiveOut) + " to " + std::to_string(changeToIndex));
        activeOutIndex = changeToIndex;
        hotKeySequenceActive = false;
        changeToIndex = UnsetChangeIndex;
        wakeupRemote = true;
    }

    if (ledsChanged || prevActiveOut != activeOutIndex) {
        auto const *keyBoard = dynamic_cast<EvInputDev *>(inBoard.get());
        auto const ledState = outLeds[activeOutIndex - 1u];
        keyBoard->setLeds(ledState);
    }

    if (wakeupRemote) {
        auto ref = mxif.lock();
        if (ref && activeOutIndex > NumLocalEvDevices && activeOutIndex <= TotalDevices) {
            decltype(activeOutIndex) const remoteIndex = activeOutIndex - static_cast<decltype(activeOutIndex)>(1u);
            ref->wakeUpRemote(remoteIndex);
        }
        return;
    }

    if (!hotKeySequenceActive) {
        if (activeOutIndex <= NumLocalEvDevices) {
            auto &ref = outBoards[activeOutIndex - 1u];
            for (auto &&e : keyStoredEvents) {
                if (ref) {
                    ref->write(&e, 1);
                } else {
                    logError("Outboard not created - check uinput");
                }
            }
            keyStoredEvents.clear();
            if (ref) {
                ref->write(ev, count);
                ref->write(&evSynRep, 1);
            } else {
                logError("No local (uinput perms?) outboard " + std::to_string(activeOutIndex));
            }
        } else if (activeOutIndex <= TotalDevices) {
            decltype(activeOutIndex) const remoteIndex = activeOutIndex - static_cast<decltype(activeOutIndex)>(1u);
            auto ref = mxif.lock();
            for (auto &&e : keyStoredEvents) {
                if (ref) {
                    ref->sendToRemote(DeviceType::keyboard, remoteIndex, &e, 1);
                } else {
                    logError("No remote outboard " + std::to_string(remoteIndex));
                }
            }
            keyStoredEvents.clear();
            if (ref) {
                // write stored first
                ref->sendToRemote(DeviceType::keyboard, remoteIndex, ev, count);
            }
        }
    }
}

void Router::timeout() {
    std::lock_guard<std::mutex> sync(routerLock);
    bool connecting = false;
    if (inKbdDevName && inKbdDevName[0] != '\0' && !inBoard) {
        connecting = true;
        logDebug("Connecting inboard " + std::string(inKbdDevName));
        try {
            inBoard = std::make_shared<EvInputDev>(inKbdDevName, *this);
            Engine::add(inBoard);
        } catch (StringException const &e) {
            logDebug(std::string("Connecting inboard ") + e.why());
        }
    }

    if (inMouseDevName && inMouseDevName[0] != '\0' && !inMouse) {
        connecting = true;
        logDebug("Connecting inboard " + std::string(inMouseDevName));

        try {
            inMouse = std::make_shared<EvInputDev>(inMouseDevName, *this);
            Engine::add(inMouse);
        } catch (StringException const &e) {
            logDebug(std::string("Connecting inboard ") + e.why());
        }
    }
    if (!connecting) {
        connectTimer->destroy();
        connectTimer.reset();
    }
}

void Router::handleError(EvDev const *dev) {
    std::lock_guard<std::mutex> sync(routerLock);

    if (dev == inBoard.get()) {
        logDebug("Router::inBoard");
        Engine::remove(inBoard);
        inBoard.reset();
    } else if (dev == inMouse.get()) {
        logDebug("Router::inMouse");
        Engine::remove(inMouse);
        inMouse.reset();
    } else {
        logDebug("Router::handleError incorrect");
        throw StringException("handleError");
    }

    if (!connectTimer) {
        connectTimer = PeriodicTimer::create(40000ns, 1s + 10ns, shared_from_this());
    }
}
