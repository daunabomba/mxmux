module;
#include "exception.h"

#include <unistd.h>

#include <chrono>
#include <memory>
#include <string>
#include <sys/timerfd.h>

import engine;
import logger;
import event;


export module timer;

using namespace std::chrono_literals;

export class Timer;

export class TimerIf : public std::enable_shared_from_this<TimerIf> {
public:
    virtual ~TimerIf() {}
    friend class Timer;

private:
    virtual void timeout() = 0;

protected:
    std::weak_ptr<Timer> timer;
};

export class Timer : public Runnable {
    friend class Engine;

public:
    static std::shared_ptr<Timer> create(std::chrono::nanoseconds initial, std::chrono::nanoseconds period,
                                         std::shared_ptr<TimerIf> const &newClient);

public:
    Timer() = delete;
    explicit Timer(int const newFd, std::shared_ptr<TimerIf> const &newClient);
    virtual ~Timer();
    void destroy();
protected:
    void handleError() override { throw std::runtime_error("Not supported for Timer"); }
    void handleRead() override;
    void handleWrite() override { throw std::runtime_error("Not supported for Timer"); }
    bool waitingOutEvent() override { return false; }
    int getLastError() const override { throw std::runtime_error("Not supported for Timer"); }

private:
    std::weak_ptr<TimerIf> client;
};

using namespace std::chrono_literals;

Timer::Timer(int const newFd, std::shared_ptr<TimerIf> const &newClient) : Runnable(newFd), client(newClient) {}

std::shared_ptr<Timer> Timer::create(std::chrono::nanoseconds initial, std::chrono::nanoseconds period,
                                     std::shared_ptr<TimerIf> const &newClient) {
    auto const timerFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    itimerspec newTimer{};

    if (period != 0ns) {
        newTimer.it_interval.tv_sec = period / 1s;
        newTimer.it_interval.tv_nsec = (period % 1s).count();
    }
    newTimer.it_value.tv_sec = initial / 1s;
    newTimer.it_value.tv_nsec = (initial % 1s).count();

    throwIf(::timerfd_settime(timerFd, 0, &newTimer, nullptr), StringException("Periodic timer settime"));

    auto timer = std::make_shared<Timer>(timerFd, newClient);
    Engine::add(timer);
    return timer;
}

Timer::~Timer() { }

void Timer::destroy() {
    Engine::remove(self);
}

void Timer::handleRead() {
    uint64_t expirations;
    ssize_t s = read(fd, &expirations, sizeof(expirations));
    if (s != sizeof(expirations)) {
        throw std::runtime_error("read from timerfd failed");
    }
    auto ref = client.lock();

    if (ref) {
        ref->timeout();
    }
}
