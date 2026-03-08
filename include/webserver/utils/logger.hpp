#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace webserver {
namespace utils {

// Log levels
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

// Convert log level to string
inline const char* level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO ";
        case LogLevel::WARN:  return "WARN ";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        default: return "UNKNOWN";
    }
}

// Log buffer - fixed size for performance
class LogBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 4 * 1024 * 1024; // 4MB per buffer

    LogBuffer() : data_(BUFFER_SIZE), write_pos_(0) {}

    // Try to append data to buffer
    bool append(const char* data, size_t len) {
        if (write_pos_ + len > BUFFER_SIZE) {
            return false; // Buffer full
        }
        std::copy(data, data + len, data_.begin() + write_pos_);
        write_pos_ += len;
        return true;
    }

    const char* data() const { return data_.data(); }
    size_t length() const { return write_pos_; }
    void reset() { write_pos_ = 0; }
    bool empty() const { return write_pos_ == 0; }

private:
    std::vector<char> data_;
    size_t write_pos_;
};

// Async logger with double buffering
class AsyncLogger {
public:
    static AsyncLogger& instance();

    // Initialize logger
    void init(const std::string& log_dir = "./logs",
              const std::string& base_name = "webserver",
              LogLevel level = LogLevel::INFO,
              size_t max_file_size = 100 * 1024 * 1024); // 100MB

    // Shutdown logger
    void shutdown();

    // Set log level
    void set_level(LogLevel level) { level_.store(level, std::memory_order_relaxed); }
    LogLevel level() const { return level_.load(std::memory_order_relaxed); }

    // Check if level is enabled
    bool is_enabled(LogLevel level) const {
        return level >= level_.load(std::memory_order_relaxed);
    }

    // Log a message (thread-safe)
    void log(LogLevel level, const char* file, int line,
             const char* func, const char* format, ...);

private:
    AsyncLogger() : running_(false), level_(LogLevel::INFO) {}
    ~AsyncLogger() { shutdown(); }

    // Background thread function
    void background_thread();

    // Flush current buffer and swap
    void flush_buffer();

    // Write to file
    void write_to_file(const std::vector<std::unique_ptr<LogBuffer>>& buffers);

    // Generate log file name
    std::string generate_filename();

private:
    std::atomic<bool> running_;
    std::atomic<LogLevel> level_;

    // Double buffering
    std::mutex mutex_;
    std::condition_variable cond_;
    std::unique_ptr<LogBuffer> current_buffer_;
    std::unique_ptr<LogBuffer> next_buffer_;
    std::vector<std::unique_ptr<LogBuffer>> buffers_;

    // Background thread
    std::thread thread_;

    // File management
    std::string log_dir_;
    std::string base_name_;
    size_t max_file_size_;
    std::ofstream file_;
    size_t current_file_size_ = 0;
    int file_index_ = 0;
};

// Convenience macros
#define LOG_DEBUG(fmt, ...) \
    do { \
        if (webserver::utils::AsyncLogger::instance().is_enabled(webserver::utils::LogLevel::DEBUG)) { \
            webserver::utils::AsyncLogger::instance().log( \
                webserver::utils::LogLevel::DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (webserver::utils::AsyncLogger::instance().is_enabled(webserver::utils::LogLevel::INFO)) { \
            webserver::utils::AsyncLogger::instance().log( \
                webserver::utils::LogLevel::INFO, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (webserver::utils::AsyncLogger::instance().is_enabled(webserver::utils::LogLevel::WARN)) { \
            webserver::utils::AsyncLogger::instance().log( \
                webserver::utils::LogLevel::WARN, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_ERROR(fmt, ...) \
    do { \
        if (webserver::utils::AsyncLogger::instance().is_enabled(webserver::utils::LogLevel::ERROR)) { \
            webserver::utils::AsyncLogger::instance().log( \
                webserver::utils::LogLevel::ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_FATAL(fmt, ...) \
    do { \
        webserver::utils::AsyncLogger::instance().log( \
            webserver::utils::LogLevel::FATAL, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__); \
        abort(); \
    } while (0)

} // namespace utils
} // namespace webserver
