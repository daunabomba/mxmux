module;
#include "exception.h"

#include <chrono>
#include <memory>
#include <string>
#include <sys/timerfd.h>
#include <unistd.h>
#include <cstdint>

import engine;
import logger;
import event;

export module timer;

using namespace std::chrono_literals;

export class PeriodicTimer;
export class OneShotTimer;

export class TimerIf : public std::enable_shared_from_this<TimerIf> {
public:
    virtual ~TimerIf() {}
    friend class PeriodicTimer;
    friend class OneShotTimer;

private:
    virtual void timeout() = 0;

protected:
    std::weak_ptr<PeriodicTimer> timer;
};

export class PeriodicTimer : public Runnable {
    friend class Engine;

public:
    static std::shared_ptr<PeriodicTimer> create(std::chrono::nanoseconds initial, std::chrono::nanoseconds period,
                                                 std::shared_ptr<TimerIf> const &newClient);

public:
    PeriodicTimer() = delete;
    explicit PeriodicTimer(int const newFd, std::shared_ptr<TimerIf> const &newClient);
    virtual ~PeriodicTimer();
    void destroy();

protected:
    void handleError() override { throw std::runtime_error("Not supported for PeriodicTimer"); }
    void handleRead() override;
    void handleWrite() override { throw std::runtime_error("Not supported for PeriodicTimer"); }
    bool hasPolledOut() const override { return false; }
    bool hasRead() const override { return true; }
    void preShutdown() override {}
    int getLastError() const override { throw std::runtime_error("Not supported for PeriodicTimer"); }

private:
    std::weak_ptr<TimerIf> client;
};

export class OneShotTimer : public Runnable {
    friend class Engine;

public:
    static std::shared_ptr<OneShotTimer> create(std::chrono::nanoseconds timeout,
                                                std::shared_ptr<TimerIf> const &newClient);

public:
    OneShotTimer() = delete;
    explicit OneShotTimer(int const newFd, std::shared_ptr<TimerIf> const &newClient);
    virtual ~OneShotTimer();
    void reset(std::chrono::nanoseconds newTimeout);
    void destroy();

protected:
    void handleError() override { throw std::runtime_error("Not supported for OneShotTimer"); }
    void handleRead() override;
    void handleWrite() override { throw std::runtime_error("Not supported for OneShotTimer"); }
    bool hasPolledOut() const override { return false; }
    bool hasRead() const override { return true; }
    void preShutdown() override {}
    int getLastError() const override { throw std::runtime_error("Not supported for OneShotTimer"); }

private:
    std::weak_ptr<TimerIf> client;
};

using namespace std::chrono_literals;

PeriodicTimer::PeriodicTimer(int const newFd, std::shared_ptr<TimerIf> const &newClient)
    : Runnable(newFd), client(newClient) {}

std::shared_ptr<PeriodicTimer> PeriodicTimer::create(std::chrono::nanoseconds initial, std::chrono::nanoseconds period,
                                                     std::shared_ptr<TimerIf> const &newClient) {
    auto const timerFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    itimerspec newTimer{};

    if (period != 0ns) {
        newTimer.it_interval.tv_sec = static_cast<decltype(newTimer.it_interval.tv_sec)>(std::chrono::duration_cast<std::chrono::seconds>(period).count());
        newTimer.it_interval.tv_nsec = static_cast<decltype(newTimer.it_interval.tv_nsec)>((period % 1s).count());
    }
    newTimer.it_value.tv_sec = static_cast<decltype(newTimer.it_value.tv_sec)>(std::chrono::duration_cast<std::chrono::seconds>(initial).count());
    newTimer.it_value.tv_nsec = static_cast<decltype(newTimer.it_value.tv_nsec)>((initial % 1s).count());

    throwIf(::timerfd_settime(timerFd, 0, &newTimer, nullptr), StringException("Periodic timer settime"));

    auto timer = std::make_shared<PeriodicTimer>(timerFd, newClient);
    Engine::add(timer);
    return timer;
}

PeriodicTimer::~PeriodicTimer() {}

void PeriodicTimer::destroy() { Engine::remove(self); }

void PeriodicTimer::handleRead() {
    std::uint64_t expirations;
    ssize_t s = ::read(fd, &expirations, sizeof(expirations));
    if (s != sizeof(expirations)) {
        throw std::runtime_error("read from timerfd failed");
    }
    auto ref = client.lock();

    if (ref) {
        ref->timeout();
    }
}

OneShotTimer::OneShotTimer(int const newFd, std::shared_ptr<TimerIf> const &newClient)
    : Runnable(newFd), client(newClient) {}

std::shared_ptr<OneShotTimer> OneShotTimer::create(std::chrono::nanoseconds initial,
                                                   std::shared_ptr<TimerIf> const &newClient) {
    auto const timerFd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    itimerspec newTimer{};

    newTimer.it_value.tv_sec = static_cast<decltype(newTimer.it_value.tv_sec)>(std::chrono::duration_cast<std::chrono::seconds>(initial).count());
    newTimer.it_value.tv_nsec = static_cast<decltype(newTimer.it_value.tv_nsec)>((initial % 1s).count());

    throwIf(::timerfd_settime(timerFd, 0, &newTimer, nullptr), StringException("OneShot timer settime"));

    auto timer = std::make_shared<OneShotTimer>(timerFd, newClient);
    Engine::add(timer);
    return timer;
}

OneShotTimer::~OneShotTimer() {}

void OneShotTimer::destroy() { Engine::remove(self); }

void OneShotTimer::handleRead() {
    std::uint64_t expirations;
    ssize_t s = ::read(fd, &expirations, sizeof(expirations));
    if (s != sizeof(expirations)) {
        throw std::runtime_error("read from timerfd failed");
    }
    auto ref = client.lock();

    if (ref) {
        ref->timeout();
    }
}

void OneShotTimer::reset(std::chrono::nanoseconds timeout) {
    auto ref = client.lock();
    if (ref) {
        itimerspec newTimer{};

        newTimer.it_value.tv_sec = static_cast<decltype(newTimer.it_value.tv_sec)>(std::chrono::duration_cast<std::chrono::seconds>(timeout).count());
        newTimer.it_value.tv_nsec = static_cast<decltype(newTimer.it_value.tv_nsec)>((timeout % 1s).count());

        throwIf(::timerfd_settime(fd, 0, &newTimer, nullptr), StringException("OneShot timer reset settime"));
    } else {
        logError("Timer is probably dead, how can this happen?");
    }
}
