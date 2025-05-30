#include "logger.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>

// 避免与系统DEBUG宏冲突
#ifdef DEBUG
#undef DEBUG
#endif

namespace usb_redirector {
namespace utils {

Logger& Logger::Instance() {
    static Logger instance;
    return instance;
}

void Logger::SetLogFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(mutex_);
    log_file_ = std::make_unique<std::ofstream>(filename, std::ios::app);
}

void Logger::Log(LogLevel level, const std::string& message) {
    if (level < log_level_) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    std::string timestamp = GetTimestamp();
    std::string level_str = GetLogLevelString(level);
    std::string log_line = "[" + timestamp + "] [" + level_str + "] " + message;

    if (console_output_) {
        std::cout << log_line << std::endl;
    }

    if (log_file_ && log_file_->is_open()) {
        *log_file_ << log_line << std::endl;
        log_file_->flush();
    }
}

void Logger::Log(LogLevel level, const std::string& file, int line, const std::string& message) {
    if (level < log_level_) {
        return;
    }

    std::string filename = file.substr(file.find_last_of("/\\") + 1);
    std::string full_message = message + " (" + filename + ":" + std::to_string(line) + ")";
    Log(level, full_message);
}

std::string Logger::GetTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

std::string Logger::GetLogLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG:   return "DEBUG";
        case LogLevel::INFO:    return "INFO";
        case LogLevel::WARNING: return "WARN";
        case LogLevel::ERROR:   return "ERROR";
        case LogLevel::FATAL:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

} // namespace utils
} // namespace usb_redirector
