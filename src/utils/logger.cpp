#include "webserver/utils/logger.hpp"

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace webserver {
namespace utils {

AsyncLogger& AsyncLogger::instance() {
    static AsyncLogger logger;
    return logger;
}

void AsyncLogger::init(const std::string& log_dir,
                       const std::string& base_name,
                       LogLevel level,
                       size_t max_file_size) {
    level_.store(level, std::memory_order_relaxed);
    log_dir_ = log_dir;
    base_name_ = base_name;
    max_file_size_ = max_file_size;

    // Create log directory
    std::filesystem::create_directories(log_dir_);

    // Initialize buffers
    current_buffer_ = std::make_unique<LogBuffer>();
    next_buffer_ = std::make_unique<LogBuffer>();

    // Start background thread
    running_ = true;
    thread_ = std::thread(&AsyncLogger::background_thread, this);

    LOG_INFO("Logger initialized, log_dir=%s, level=%d", log_dir_.c_str(), static_cast<int>(level));
}

void AsyncLogger::shutdown() {
    if (!running_.exchange(false)) {
        return;
    }

    cond_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }

    // Flush remaining logs
    if (current_buffer_ && !current_buffer_->empty()) {
        buffers_.push_back(std::move(current_buffer_));
    }
    write_to_file(buffers_);

    if (file_.is_open()) {
        file_.close();
    }
}

void AsyncLogger::log(LogLevel level, const char* file, int line,
                      const char* func, const char* format, ...) {
    if (!is_enabled(level)) {
        return;
    }

    // Get current time
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    // Format time
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif

    // Format log message
    char buffer[4096];
    int pos = snprintf(buffer, sizeof(buffer),
        "[%04d-%02d-%02d %02d:%02d:%02d.%03d] [%s] [%s:%d] [%s] ",
        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec,
        static_cast<int>(ms.count()),
        level_to_string(level), file, line, func);

    // Append user message
    va_list args;
    va_start(args, format);
    pos += vsnprintf(buffer + pos, sizeof(buffer) - pos - 2, format, args);
    va_end(args);

    // Add newline
    buffer[pos++] = '\n';
    buffer[pos] = '\0';

    // Append to buffer
    std::lock_guard<std::mutex> lock(mutex_);
    if (!current_buffer_->append(buffer, pos)) {
        // Current buffer full, push to queue and use next buffer
        buffers_.push_back(std::move(current_buffer_));
        current_buffer_ = std::move(next_buffer_);
        if (!current_buffer_) {
            current_buffer_ = std::make_unique<LogBuffer>();
        }
        current_buffer_->append(buffer, pos);
        cond_.notify_one();
    }
}

void AsyncLogger::background_thread() {
    std::vector<std::unique_ptr<LogBuffer>> buffers_to_write;
    buffers_to_write.reserve(16);

    while (running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait_for(lock, std::chrono::seconds(3),
                [this] { return !buffers_.empty() || !running_; });

            // Move buffers to write
            buffers_to_write.swap(buffers_);
            if (current_buffer_ && !current_buffer_->empty()) {
                buffers_.push_back(std::move(current_buffer_));
                current_buffer_ = std::make_unique<LogBuffer>();
            }
            if (!next_buffer_) {
                next_buffer_ = std::make_unique<LogBuffer>();
            }
        }

        if (!buffers_to_write.empty()) {
            write_to_file(buffers_to_write);
            buffers_to_write.clear();
        }

        // Pre-allocate buffers
        if (buffers_to_write.capacity() > 16) {
            std::vector<std::unique_ptr<LogBuffer>> tmp;
            tmp.reserve(16);
            buffers_to_write.swap(tmp);
        }
    }
}

void AsyncLogger::write_to_file(const std::vector<std::unique_ptr<LogBuffer>>& buffers) {
    if (!file_.is_open()) {
        std::string filename = generate_filename();
        file_.open(filename, std::ios::app);
        if (!file_) {
            std::cerr << "Failed to open log file: " << filename << std::endl;
            return;
        }
    }

    for (const auto& buffer : buffers) {
        if (current_file_size_ + buffer->length() > max_file_size_) {
            file_.close();
            file_index_++;
            std::string filename = generate_filename();
            file_.open(filename, std::ios::app);
            current_file_size_ = 0;
        }

        file_.write(buffer->data(), buffer->length());
        current_file_size_ += buffer->length();
    }

    file_.flush();
}

std::string AsyncLogger::generate_filename() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif

    std::ostringstream oss;
    oss << log_dir_ << "/" << base_name_ << "_"
        << std::put_time(&tm, "%Y%m%d")
        << "_" << std::setw(4) << std::setfill('0') << file_index_
        << ".log";
    return oss.str();
}

} // namespace utils
} // namespace webserver
