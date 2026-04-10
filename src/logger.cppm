module;
#include <syslog.h>

#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <sstream>

export module logger;

export class Logger final {

  public:
    enum class LogType { NOTHING = 0, CRITICAL, ERROR, WARNING, DEBUG, EVERYTHING };
    static void logCritical(const std::string &what);
    static void logError(const std::string &what);
    static void logWarn(const std::string &what);
    static void logDebug(const std::string &what);
    static void log(const LogType type, const std::string what);
    static void setMask(const LogType mask);

    static void start();
    static void stop();
  private:
    Logger();
    void doLog(LogType type, const std::string what);
    void doSetMask(const LogType level);


  private:
    std::vector<std::thread::id> threadIds;
    std::mutex lock;
    LogType level = LogType::NOTHING;
    static Logger *theLogger;
};

export const auto logCritical = Logger::logCritical;
export const auto logError = Logger::logError;
export const auto logWarn = Logger::logWarn;
export const auto logDebug = Logger::logDebug;


using namespace std::chrono_literals;

Logger *Logger::theLogger = nullptr;

Logger::Logger() {}

void Logger::start() {
    if (Logger::theLogger != nullptr) {
        throw std::runtime_error("start called when already started");
    }
    Logger::theLogger = new Logger;
    openlog("gatecont", LOG_PID | LOG_CONS, LOG_USER);
}

void Logger::setMask(const LogType mask) { Logger::theLogger->doSetMask(mask); }

void Logger::doSetMask(const LogType newLevel) { this->level = newLevel; }

void Logger::stop() {
    if (Logger::theLogger == nullptr) {
        throw std::runtime_error("stop called before start");
    }
    std::unique_ptr<Logger> tmp(Logger::theLogger);
    closelog();
    Logger::theLogger = nullptr;
}

void Logger::logCritical(const std::string &what) { Logger::log(LogType::CRITICAL, what); }

void Logger::logError(const std::string &what) { Logger::log(LogType::ERROR, what); }

void Logger::logWarn(const std::string &what) { Logger::log(LogType::WARNING, what); }

void Logger::logDebug(const std::string &what) { Logger::log(LogType::DEBUG, what); }

void Logger::log(const LogType type, const std::string what) {
    if (Logger::theLogger == nullptr) {
        throw std::runtime_error("need to call start first or start failed");
    }
    Logger::theLogger->doLog(type, what);
}

void Logger::doLog(LogType type, const std::string what) {
    if (type > level) {
        return;
    }
    auto nowNs =
        std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now()).time_since_epoch();
    std::time_t secs = nowNs / 1s;
    auto nsecs = (nowNs % 1s).count();
    lock.lock();
    std::tm *local_time_now = std::localtime(&secs);
    lock.unlock();
    std::locale loc;
    const std::time_put<char> &tmput{std::use_facet<std::time_put<char>>(loc)};
    std::stringbuf line;
    std::ostream os(&line);
    std::string fmt("%Y-%Om-%Od %OH:%OM:%OS.");
    tmput.put(os, os, '.', local_time_now, fmt.data(), fmt.data() + fmt.length());
    auto start = nsecs == 0 ? static_cast<int64_t>(1) : nsecs;
    for (auto div = start; div < 100000000; div = div * 10) {
        os << "0";
    }
    os << nsecs;
    lock.lock();
    std::size_t tid = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 0; i < Logger::threadIds.size(); ++i) {
        if (threadIds.at(i) == std::this_thread::get_id()) {
            tid = i;
        }
    }
    if (tid == std::numeric_limits<std::size_t>::max()) {
        Logger::threadIds.push_back(std::this_thread::get_id());
        tid = Logger::threadIds.size() - 1;
    }
    os << "\t" << what << "\n";
    // lock.lock();
    // syslog(LOG_LOCAL0, line.str().c_str());
    // lock.unlock();
    std::cerr << line.str();
    lock.unlock();
}
