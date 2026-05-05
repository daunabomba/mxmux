module;

#include <cstdint>

#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <syslog.h>
#include <thread>
#include <vector>
#include <memory>

export module logger;

export class Logger final {

public:
    enum class LogType { NOTHING = 0, CRITICAL, ERROR, WARNING, DEBUG, EVERYTHING };
    static std::string to_string(LogType type) {
        switch (type) {
        case LogType::NOTHING:
            return "NOTHING";
        case LogType::CRITICAL:
            return "CRITICAL";
        case LogType::ERROR:
            return "ERROR";
        case LogType::WARNING:
            return "WARNING";
        case LogType::DEBUG:
            return "DEBUG";
        case LogType::EVERYTHING:
            return "EVERYTHING";
        }
        return "UNKNOWN";
    }
    static void logCritical(std::string const &what);
    static void logError(std::string const &what);
    static void logWarn(std::string const &what);
    static void logDebug(std::string const &what);
    static void log(LogType const type, std::string const &what);
    static void setMask(LogType const mask);

    static void start();
    static void stop();

private:
    Logger();
    void doLog(LogType type, std::string const &what);
    void doSetMask(LogType const level);
    static int toSyslogPriority(LogType type) {
        switch (type) {
            case LogType::CRITICAL: return LOG_LOCAL0 | LOG_CRIT;
            case LogType::ERROR:    return LOG_LOCAL0 | LOG_ERR;
            case LogType::WARNING:  return LOG_LOCAL0 | LOG_WARNING;
            case LogType::DEBUG:    return LOG_LOCAL0 | LOG_DEBUG;
            default:                return LOG_LOCAL0 | LOG_INFO;
        }
    }

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
        return;
    }
    Logger::theLogger = new Logger;
    openlog("mx", LOG_PID | LOG_CONS, LOG_USER);
}

void Logger::setMask(LogType const mask) { Logger::theLogger->doSetMask(mask); }

void Logger::doSetMask(LogType const newLevel) { this->level = newLevel; }

void Logger::stop() {
    if (Logger::theLogger == nullptr) {
        throw std::runtime_error("stop called before start");
        return;
    }
    std::unique_ptr<Logger> tmp(Logger::theLogger);
    closelog();
    Logger::theLogger = nullptr;
}

void Logger::logCritical(std::string const &what) { Logger::log(LogType::CRITICAL, what); }

void Logger::logError(std::string const &what) { Logger::log(LogType::ERROR, what); }

void Logger::logWarn(std::string const &what) { Logger::log(LogType::WARNING, what); }

void Logger::logDebug(std::string const &what) { Logger::log(LogType::DEBUG, what); }

void Logger::log(LogType const type, std::string const &what) {
    if (Logger::theLogger == nullptr) {
        std::cerr << Logger::to_string(type) << " " << what << std::endl;
        return;
    }
    Logger::theLogger->doLog(type, what);
}

void Logger::doLog(LogType type, std::string const &what) {
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
    auto start = nsecs == 0 ? static_cast<std::int64_t>(1) : nsecs;
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
    lock.lock();
    syslog(toSyslogPriority(type), "%s", line.str().c_str());
    lock.unlock();
    std::cerr << line.str();
    lock.unlock();
}
