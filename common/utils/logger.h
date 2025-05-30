#pragma once

#include <string>
#include <memory>
#include <fstream>
#include <mutex>
#include <sstream>

// 避免与系统DEBUG宏冲突
#ifdef DEBUG
#undef DEBUG
#endif

namespace usb_redirector {
namespace utils {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARNING = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
public:
    static Logger& Instance();

    void SetLogLevel(LogLevel level) { log_level_ = level; }
    void SetLogFile(const std::string& filename);
    void SetConsoleOutput(bool enable) { console_output_ = enable; }

    void Log(LogLevel level, const std::string& message);
    void Log(LogLevel level, const std::string& file, int line, const std::string& message);

    // 便捷方法
    void Debug(const std::string& message) { Log(LogLevel::DEBUG, message); }
    void Info(const std::string& message) { Log(LogLevel::INFO, message); }
    void Warning(const std::string& message) { Log(LogLevel::WARNING, message); }
    void Error(const std::string& message) { Log(LogLevel::ERROR, message); }
    void Fatal(const std::string& message) { Log(LogLevel::FATAL, message); }

private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string GetTimestamp();
    std::string GetLogLevelString(LogLevel level);

    LogLevel log_level_ = LogLevel::INFO;
    bool console_output_ = true;
    std::unique_ptr<std::ofstream> log_file_;
    std::mutex mutex_;
};

// 日志宏
#define LOG_DEBUG(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        usb_redirector::utils::Logger::Instance().Log(usb_redirector::utils::LogLevel::DEBUG, __FILE__, __LINE__, oss.str()); \
    } while(0)

#define LOG_INFO(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        usb_redirector::utils::Logger::Instance().Log(usb_redirector::utils::LogLevel::INFO, __FILE__, __LINE__, oss.str()); \
    } while(0)

#define LOG_WARNING(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        usb_redirector::utils::Logger::Instance().Log(usb_redirector::utils::LogLevel::WARNING, __FILE__, __LINE__, oss.str()); \
    } while(0)

#define LOG_ERROR(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        usb_redirector::utils::Logger::Instance().Log(usb_redirector::utils::LogLevel::ERROR, __FILE__, __LINE__, oss.str()); \
    } while(0)

#define LOG_FATAL(msg) \
    do { \
        std::ostringstream oss; \
        oss << msg; \
        usb_redirector::utils::Logger::Instance().Log(usb_redirector::utils::LogLevel::FATAL, __FILE__, __LINE__, oss.str()); \
    } while(0)

} // namespace utils
} // namespace usb_redirector
