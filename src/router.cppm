module;

#include "exception.h"
#include "types.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>

import evdev;
import evinputdev;
import event;
import engine;
import logger;
import timer;
import kbdinputdev;
import mouseinputdev;

export module router;

using namespace std::chrono_literals;


export class Router : virtual public RouterIf {
public:
    Router() = delete;
    Router(char const *const inKbName, char const *const inMouseName);
    virtual ~Router();
    void init();
    void handleInputEvent(EvDev const *src, linux_input_event const ev[], size_t const count) override;
    void handleError(EvDev const* dev) override; 
protected:
    void timeout() override; // from timerif

private:
    void processKbdInput(linux_input_event const ev[], size_t const count);
    void processMouseInput(linux_input_event const ev[], size_t const count);

private:
    static constexpr size_t NumDisplays = 5u;

private:
    char const *const inKbdDevName ;
    char const *const inMouseDevName;
    std::shared_ptr<EvDev> inMouse;
    std::shared_ptr<EvDev> inBoard;
    std::array<std::shared_ptr<EvDev>, NumDisplays> outMice;
    std::array<std::shared_ptr<EvDev>, NumDisplays> outBoards;
    std::array<unsigned char, NumDisplays> outLeds;
    size_t activeOutIndex = 1;
    bool leftCtrlDown = false;
    bool leftAltDown = false;
    std::shared_ptr<Timer> connectTimer;
    std::mutex routerLock;
    linux_input_event evSynRep;
    
};



Router::Router(char const *const inKbName, char const *const inMouseName)
    : inKbdDevName(inKbName), inMouseDevName(inMouseName) {
        evSynRep.type = EV_SYN;
        evSynRep.code = SYN_REPORT;
        evSynRep.value = 0;
    }

void Router::init() {
    constexpr char NoneString[] = "none";
    if (std::string(NoneString).compare(std::string(inKbdDevName)) != 0) {
        for (auto i = 0u; i < NumDisplays; ++i) {
            outLeds[i] = 0u;
            outBoards[i] = std::make_shared<KbdInputDev>(i, ("etmux-kbd-event-" + std::to_string(i)).c_str(), *this);
            Engine::add(outBoards[i]);
        }
    }

    if (std::string(NoneString).compare(std::string(inMouseDevName)) != 0) {

        for (auto i = 0u; i < NumDisplays; ++i) {
            outLeds[i] = 0u;
            outMice[i] = std::make_shared<MouseInputDev>(i, ("etmux-mouse-event-" + std::to_string(i)).c_str(), *this);
            Engine::add(outMice[i]);
        }
    }
    connectTimer = Timer::create(40000ns, 1s + 10ns, shared_from_this());
}

Router::~Router() { logDebug("Router::~Router()"); }

void Router::handleInputEvent(EvDev const *src, linux_input_event const ev[], size_t const count) {
    std::lock_guard<std::mutex> sync(routerLock);

    if (activeOutIndex == 0u) {
        logDebug("Router::handleInputEvent() with index 0");
    } else {
        // no need to lock these as these are cleared under the same lock in handleError()
        if (inBoard.get() == src) {
            processKbdInput(ev, count);
        } else if (inMouse.get() == src) {
            processMouseInput(ev, count);
        } else {
            throw StringException("handleInputEvent");
        }
    }
}

void Router::processMouseInput(linux_input_event const ev[], size_t const count) {
    auto &ref = outMice[activeOutIndex - 1u];
    ref->write(ev, count);
    ref->write(&evSynRep, 1);
}

void Router::processKbdInput(linux_input_event const ev[], size_t const count) {
    auto &ref = outBoards[activeOutIndex - 1u];
    auto prevActiveOut = activeOutIndex;
    bool ledsChanged = false;
    for (auto i = 0u; i < count; ++i) {
        auto lastKeyUp = KEY_CNT;
        if (ev[i].type == EV_KEY) {
            if (EvDev::KeyStateValues::Up == ev[i].value) {
                lastKeyUp = ev[i].code;
                if (KEY_LEFTCTRL == ev[i].code) {
                    leftCtrlDown = false;
                } else if (KEY_LEFTALT == ev[i].code) {
                    leftAltDown = false;
                }
            } else if (EvDev::KeyStateValues::Down == ev[i].value) {
                if (KEY_LEFTCTRL == ev[i].code) {
                    leftCtrlDown = true;
                } else if (KEY_LEFTALT == ev[i].code) {
                    leftAltDown = true;
                } else if (KEY_CAPSLOCK == ev[i].code) {
                    ledsChanged = true;
                    outLeds[activeOutIndex - 1u] ^= EvDev::Leds::CapsLock;
                } else if (KEY_NUMLOCK == ev[i].code) {
                    ledsChanged = true;
                    outLeds[activeOutIndex - 1u] ^= EvDev::Leds::NumLock;
                } else if (KEY_SCROLLLOCK == ev[i].code) {
                    ledsChanged = true;
                    outLeds[activeOutIndex - 1u] ^= EvDev::Leds::ScrollLock;
                }
            }
            if (leftCtrlDown && leftAltDown && KEY_1 <= lastKeyUp && KEY_9 >= lastKeyUp &&
                (lastKeyUp - KEY_1) < static_cast<decltype(lastKeyUp)>(NumDisplays)) {
                auto prevIndex = activeOutIndex;
                activeOutIndex = static_cast<decltype(activeOutIndex)>(lastKeyUp - KEY_1) + 1u;
                logDebug("Outindex changed from " + std::to_string(prevIndex) + " to " +
                         std::to_string(activeOutIndex));
            }
        }
    }
    if (ledsChanged || prevActiveOut != activeOutIndex) {
        auto const *keyBoard = dynamic_cast<EvInputDev *>(inBoard.get());
        auto const ledState = outLeds[activeOutIndex - 1u];
        keyBoard->setLeds(ledState);
    }
    ref->write(ev, count);
    ref->write(&evSynRep, 1);
}

void Router::timeout() {
    std::lock_guard<std::mutex> sync(routerLock);
    if (!inBoard) {
        logDebug("Connecting inboard " + std::string(inKbdDevName));
        try {
            inBoard = std::make_shared<EvInputDev>(inKbdDevName, *this);
            Engine::add(inBoard);
        } catch (EvInputDev::DevNotPresentException const &e) {
            logDebug("Connecting inboard " + std::string(inKbdDevName) + " not found");
        }
    }

    if (!inMouse) {
        logDebug("Connecting inboard " + std::string(inKbdDevName));

        try {
            inMouse = std::make_shared<EvInputDev>(inMouseDevName, *this);
            Engine::add(inMouse);
        } catch (EvInputDev::DevNotPresentException const &e) {
            logDebug("Connecting inboard " + std::string(inKbdDevName) + " not found");
        }
    }
    if (inMouse && inBoard) {
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
        connectTimer = Timer::create(40000ns, 1s + 10ns, shared_from_this());
    }
}
