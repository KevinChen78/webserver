/**
 * Logger Unit Tests
 * Tests for async logging functionality
 */

#include "webserver/utils/logger.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace webserver::utils;

// Test basic logging functionality
void test_basic_logging() {
    std::cout << "Testing basic logging..." << std::endl;

    // Create temporary log directory
    std::string test_dir = "./test_logs";
    std::filesystem::create_directories(test_dir);

    // Initialize logger
    AsyncLogger::instance().init(test_dir, "test", LogLevel::DEBUG);

    // Log messages at different levels
    LOG_DEBUG("Debug message: %d", 42);
    LOG_INFO("Info message: %s", "hello");
    LOG_WARN("Warning message");
    LOG_ERROR("Error message");

    // Shutdown to flush buffers
    AsyncLogger::instance().shutdown();

    // Check log file was created
    bool found_log = false;
    for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
        if (entry.path().extension() == ".log") {
            found_log = true;

            // Read and verify content
            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

            assert(content.find("DEBUG") != std::string::npos);
            assert(content.find("Info message: hello") != std::string::npos);
            assert(content.find("WARN") != std::string::npos);
            assert(content.find("ERROR") != std::string::npos);
        }
    }

    assert(found_log && "Log file should be created");

    // Cleanup
    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Basic logging test passed" << std::endl;
}

// Test log level filtering
void test_log_level_filtering() {
    std::cout << "Testing log level filtering..." << std::endl;

    std::string test_dir = "./test_logs_filter";
    std::filesystem::create_directories(test_dir);

    AsyncLogger::instance().init(test_dir, "test", LogLevel::WARN);

    LOG_DEBUG("This should not appear");
    LOG_INFO("This should not appear");
    LOG_WARN("This should appear");
    LOG_ERROR("This should appear");

    AsyncLogger::instance().shutdown();

    // Verify content
    for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
        if (entry.path().extension() == ".log") {
            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

            assert(content.find("should not appear") == std::string::npos);
            assert(content.find("should appear") != std::string::npos);
        }
    }

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Log level filtering test passed" << std::endl;
}

// Test concurrent logging from multiple threads
void test_concurrent_logging() {
    std::cout << "Testing concurrent logging..." << std::endl;

    std::string test_dir = "./test_logs_concurrent";
    std::filesystem::create_directories(test_dir);

    AsyncLogger::instance().init(test_dir, "test", LogLevel::INFO);

    const int num_threads = 10;
    const int logs_per_thread = 100;
    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, logs_per_thread]() {
            for (int i = 0; i < logs_per_thread; ++i) {
                LOG_INFO("Thread %d, message %d", t, i);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    AsyncLogger::instance().shutdown();

    // Count total lines in log files
    size_t total_lines = 0;
    for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
        if (entry.path().extension() == ".log") {
            std::ifstream file(entry.path());
            std::string line;
            while (std::getline(file, line)) {
                ++total_lines;
            }
        }
    }

    size_t expected_lines = num_threads * logs_per_thread;
    assert(total_lines == expected_lines && "All log messages should be written");

    double logs_per_second = (expected_lines * 1000.0) / duration;
    std::cout << "  Wrote " << expected_lines << " logs in " << duration << "ms"
              << " (" << logs_per_second << " logs/sec)" << std::endl;

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Concurrent logging test passed" << std::endl;
}

// Test log file rotation (by size)
void test_file_rotation() {
    std::cout << "Testing file rotation..." << std::endl;

    std::string test_dir = "./test_logs_rotation";
    std::filesystem::create_directories(test_dir);

    // Small max file size to trigger rotation
    AsyncLogger::instance().init(test_dir, "test", LogLevel::INFO, 1024); // 1KB max

    // Write enough data to trigger rotation
    std::string long_message(200, 'X');
    for (int i = 0; i < 20; ++i) {
        LOG_INFO("Message %d: %s", i, long_message.c_str());
    }

    AsyncLogger::instance().shutdown();

    // Count log files
    int log_file_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(test_dir)) {
        if (entry.path().extension() == ".log") {
            ++log_file_count;
        }
    }

    assert(log_file_count >= 2 && "Multiple log files should be created after rotation");

    std::cout << "  Created " << log_file_count << " log files after rotation" << std::endl;

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ File rotation test passed" << std::endl;
}

// Test logger singleton behavior
void test_singleton() {
    std::cout << "Testing singleton behavior..." << std::endl;

    AsyncLogger& logger1 = AsyncLogger::instance();
    AsyncLogger& logger2 = AsyncLogger::instance();

    assert(&logger1 == &logger2 && "Logger should be singleton");

    std::cout << "  ✓ Singleton test passed" << std::endl;
}

// Main test runner
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Logger Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        test_singleton();
        test_basic_logging();
        test_log_level_filtering();
        test_concurrent_logging();
        test_file_rotation();

        std::cout << "\n========================================" << std::endl;
        std::cout << "All tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
