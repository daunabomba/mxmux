module;
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <cassert>
#include <csignal>

import logger;
import event;
import semaphore;
import utils;

export module engine;

export class Engine final {
public:
    static void start(unsigned int const minWorkersPerCpu = 4);
    static void stop();
    static void init();
    static void add(std::shared_ptr<Runnable> const &what);
    static void remove(std::weak_ptr<Runnable> const &what);
    static void triggerWrites(Runnable *const what);
    static void runAsync(Event const &event);
    ~Engine();
    void startWorkers(unsigned int const minWorkersPerCpu);
    void stopWorkers();

private:
    class Worker;

    Engine();
    friend void signalHandler(int);
    void doEpoll(Worker *me) noexcept;
    static void doWork(Worker *me) noexcept;
    void doStop();
    void doSignalHandler();
    void doInit(unsigned int const minWorkersPerCpu);
    void newEvent(uint64_t const evId, uint32_t const events);
    void run(Runnable *const sock, const uint32_t events);
    void doEpoll();
    void worker(Worker &me);
    void doAdd(std::shared_ptr<Runnable> const &what);
    void doRemove(std::weak_ptr<Runnable> const &what);
    void doTriggerWrites(Runnable *const what);
    void doRunAsync(Event const &event);

private:
    static Engine *theEngine;
    Semaphore sem;
    std::mutex eqLock;
    std::deque<Event> eventQueue;
    bool stopping;
    std::atomic_int activeCount;
    std::thread::id epollTid;
    std::thread::native_handle_type epollThreadHandle;
    int epollFd = -1;
    std::vector<Worker *> slaves;
    std::mutex evHashLock;
    std::map<uint64_t, std::shared_ptr<Runnable>> eventHash;

private:
    uint64_t evCounter = 0;
    std::size_t const NUM_ENGINE_EVENTS = 0;
    std::int32_t const EPOLL_EVENTS_PER_RUN = 128;
};

class Engine::Worker {
public:
    Worker() = delete;
    Worker(void (*func)(Worker *)) : thread(func, this) {}
    ~Worker();
    std::thread thread;
    std::atomic_bool exited;
};

Engine *Engine::theEngine = nullptr;

Engine::Worker::~Worker() { thread.detach(); }

Engine::Engine()
    : stopping(false), activeCount(0), epollTid(std::this_thread::get_id()), epollThreadHandle(::pthread_self()),
      epollFd(::epoll_create1(EPOLL_CLOEXEC)) {
    Logger::start();
    Logger::setMask(Logger::LogType::EVERYTHING);
    assert(epollFd >= 0 && "Failed to create epollFd");
}

Engine::~Engine() {
    std::signal(SIGUSR1, SIG_DFL);
    ::close(epollFd);
    logDebug("Engine::~Engine()");
    Logger::stop();
}

void Engine::start(unsigned int const minWorkersPerCpu) {
    std::unique_ptr<Engine> tmp(Engine::theEngine);
    theEngine->doInit(minWorkersPerCpu);
    Engine::theEngine = nullptr;
}

void Engine::stop() {
    if (Engine::theEngine != nullptr) {
        Engine::theEngine->doStop();
    }
}

void signalHandler(int) { Engine::theEngine->doSignalHandler(); }

void Engine::doSignalHandler() {
    if(epollTid != std::this_thread::get_id()) {
        return;
    }
    logDebug("Shutting down");

    std::lock_guard<std::mutex> sync(evHashLock);

    for (auto const &[key, runnablePtr] : eventHash) {
        if (runnablePtr) {
            runnablePtr->preShutdown();
        }
    }

    if (!stopping) {
        stopping = true;
    }
}

void Engine::startWorkers(unsigned int const minWorkersPerCpu) {
    std::lock_guard<std::mutex> sync(evHashLock);
    auto const initialNumThreadsToSpawn = std::thread::hardware_concurrency() + minWorkersPerCpu;
    for (auto i = 0u; i < initialNumThreadsToSpawn; ++i) {
        slaves.push_back(new Worker(Engine::doWork));
    }
}

void Engine::stopWorkers() {
    bool waiting = true;
    for (int i = 1024; i > 0 && waiting; i--) {
        waiting = false;
        for (auto &slave : slaves) {
            if (!slave->exited) {
                sem.signal();
                waiting = true;
            }
        }
    }
    assert(!waiting && "Engine::stopWorkers failed to stop");
    for (auto &slave : slaves) {
        delete slave;
    }
}

void Engine::doInit(unsigned int const minWorkersPerCpu) {
    assert((eventHash.size() > NUM_ENGINE_EVENTS) && "Engine::doInit Need to Add() something before Go().");
    std::signal(SIGPIPE, signalHandler);
    for (int i = SIGHUP; i < _NSIG; ++i) {
        std::signal(i, SIG_IGN);
    }
    std::signal(SIGQUIT, signalHandler);
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGUSR1, signalHandler);
    startWorkers(minWorkersPerCpu);
    doEpoll();
    stopWorkers();
    for (int i = SIGHUP; i < _NSIG; ++i) {
        std::signal(i, SIG_DFL);
    }
    eventHash.clear();
    eventQueue.clear();
}

void Engine::triggerWrites(Runnable *const what) {
    if (Engine::theEngine == nullptr) {
        throw std::runtime_error("Engine::triggerWrites Please call Engine::Init() first");
    }
    Engine::theEngine->doTriggerWrites(what);
}

void Engine::newEvent(uint64_t const evId, uint32_t const events) {
    {
        std::lock_guard<std::mutex> syncEventHash(evHashLock);
        auto it = eventHash.find(evId);
        if (it == eventHash.end()) {
            return;
        }
        {
            std::lock_guard<std::mutex> syncEventQueue(eqLock);
            eventQueue.push_back(Event(it->second, std::bind(&Engine::run, this, it->second.get(), events)));
        }
    }
    sem.signal();
}

void Engine::doTriggerWrites(Runnable *const what) { newEvent(what->evId, EPOLLOUT); }

void Engine::runAsync(Event const &event) {
    if (Engine::theEngine == nullptr) {
        throw std::runtime_error("Engine::runAsync Please call Engine::Init() first");
    }
    Engine::theEngine->doRunAsync(event);
}

void Engine::doRunAsync(Event const &event) {
    {
        std::lock_guard<std::mutex> sync(eqLock);
        eventQueue.push_back(event);
    }
    sem.signal();
}

void Engine::doAdd(std::shared_ptr<Runnable> const &what) {
    if (!stopping) {
        auto const needOut = what->hasPolledOut();
        auto const needIn = what->hasRead();

        std::lock_guard<std::mutex> sync(evHashLock);
        what->self = what;
        what->evId = ++evCounter;
        assert(eventHash.find(what->evId) == eventHash.end() && "Already cound id");
        bool const added = eventHash.emplace(what->evId, what).second;
        (void)added;
        assert(added && "Already exists in hash");
        epoll_event event = {(needOut ? EPOLLOUT : static_cast<decltype(EPOLLOUT)>(0)) |
                                 (needIn ? EPOLLIN : static_cast<decltype(EPOLLIN)>(0)) | EPOLLONESHOT | EPOLLERR |
                                 EPOLLHUP | EPOLLRDHUP | EPOLLET,
                             {.u64 = what->evId}};
        pErrorThrow("epoll_ctl add", ::epoll_ctl(epollFd, EPOLL_CTL_ADD, what->fd, &event), epollFd);
    }
}

void Engine::add(std::shared_ptr<Runnable> const &what) {
    if (Engine::theEngine == nullptr) {
        throw std::runtime_error("Engine::add Please call Engine::Init() first");
    }
    theEngine->doAdd(what);
}

void Engine::doRemove(std::weak_ptr<Runnable> const &what) {
    if (!stopping) {
        auto const ref = what.lock();
        if (ref) {
            std::lock_guard<std::mutex> sync(evHashLock);
            auto it = eventHash.find(ref->evId);
            assert(it != eventHash.end());
            eventHash.erase(it);
        }
    }
}

void Engine::remove(std::weak_ptr<Runnable> const &what) {
    if (Engine::theEngine == nullptr) {
        throw std::runtime_error("Engine::remove Please call Engine::Init() first");
    }
    theEngine->doRemove(what);
}

void Engine::init() {
    if (Engine::theEngine == nullptr) {
        Engine::theEngine = new Engine();
    }
}

void Engine::doStop() {
    if (!stopping && epollTid != std::this_thread::get_id()) {
        ::pthread_kill(epollThreadHandle, SIGUSR1);
    }
    logDebug("Engine stopped");
}

void Engine::run(Runnable *const sock, const uint32_t events) {
    {
        std::lock_guard<std::mutex> sync(evHashLock);
        auto const it = eventHash.find(sock->evId);
        if (it == eventHash.end()) {
            return;
        }
    }

    if ((events & EPOLLHUP) != 0 || (events & EPOLLRDHUP) != 0 || (events & EPOLLERR) != 0) {
        sock->handleError();
        pErrorLog("Engine::run", sock->getLastError(), sock->fd);
    } else {
        if ((events & EPOLLOUT) != 0) {
            sock->handleWrite();
        } else if ((events & (EPOLLIN)) != 0) {
            sock->handleRead();
        }
        bool const needOut = sock->hasPolledOut();
        bool const needIn = sock->hasRead();

        std::lock_guard<std::mutex> sync(evHashLock);
        auto const it = eventHash.find(sock->evId);
        if (it != eventHash.end()) {
            epoll_event event = {(needOut ? EPOLLOUT : static_cast<decltype(EPOLLOUT)>(0)) |
                                     (needIn ? EPOLLIN : static_cast<decltype(EPOLLIN)>(0)) | EPOLLONESHOT | EPOLLERR |
                                     EPOLLHUP | EPOLLRDHUP | EPOLLET,
                                 {.u64 = sock->evId}};
            pErrorThrow("epoll_ctl mod", ::epoll_ctl(epollFd, EPOLL_CTL_MOD, sock->fd, &event), epollFd);
        }
    }
}

void Engine::worker(Worker &me) {
    try {
        while (!stopping) {
            sem.wait();
            Event event({}, []() {});
            {
                std::lock_guard<std::mutex> sync(eqLock);
                if (eventQueue.size() != 0) {
                    event = eventQueue.front();
                    eventQueue.pop_front();
                }
            }
            activeCount++;
            event();
            activeCount--;
            if (eventHash.size() == NUM_ENGINE_EVENTS && activeCount == 0) {
                doStop();
                break;
            }
        }
    } catch (std::exception &e) {
        logError(std::string("Engine::worker threw a ") + e.what());
        doStop();
    } catch (...) {
        logError("Unknown exception in Engine::worker");
        doStop();
    }
    me.exited = true;
}

void Engine::doWork(Worker *me) noexcept { theEngine->worker(*me); }

void Engine::doEpoll() {
    try {
        while (!stopping) {
            std::vector<epoll_event> epEvents(static_cast<size_t>(EPOLL_EVENTS_PER_RUN));
            auto num = epoll_wait(epollFd, epEvents.data(), EPOLL_EVENTS_PER_RUN, -1);
            if (stopping) {
                break;
            }
            if (num >= 0) {
                size_t const numEv = static_cast<size_t>(num);
                for (std::size_t i = 0; i < numEv; ++i) {
                    newEvent(epEvents[i].data.u64, epEvents[i].events);
                }
            } else if (num == -1 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            } else {
                pErrorThrow("Engine::doEpoll", num, epollFd);
            }
        }
    } catch (std::exception &e) {
        logError(std::string("Engine::epollThread threw a ") + e.what());
        stop();
    } catch (...) {
        logError("Unknown exception in Engine::epollThread");
        stop();
    }
}
