module;
#include <unistd.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

import logger;

export module event;

export class Event;

export class Runnable : virtual public std::enable_shared_from_this<Runnable> {

public:
    Runnable() = delete;
    Runnable(int const newFd) : fd(newFd) {}
    virtual ~Runnable();

    public:
    virtual void handleError() = 0;
    virtual void handleRead() = 0;
    virtual void handleWrite() = 0;
    virtual bool waitingOutEvent() = 0;
    virtual int getLastError() const = 0;

public:
    std::weak_ptr<Runnable> self;
    std::uint64_t evId;
    int const fd;
};

export class Event final {
public:
    Event();
    explicit Event(std::weak_ptr<Runnable> const &owner, std::function<void()> const &func);
    void operator()() const;
    bool operator==(Event const &rhs) = delete;

public:
    std::weak_ptr<Runnable> obj;
    std::function<void()> func;
};



Runnable::~Runnable() {
    logDebug("Runnable::~Runnable() " + std::to_string(fd));
    if (fd >= 0) {
        ::close(fd);
    }
}

Event::Event() {}

Event::Event(std::weak_ptr<Runnable> const &owner, std::function<void()> const &newFunc) : obj(owner), func(newFunc) {}

void Event::operator()() const {
    auto const ref = obj.lock();
    if (ref) {
        func();
    }
}
