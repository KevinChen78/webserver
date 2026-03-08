/**
 * Logger Demo
 * Demonstrates async logging functionality
 */

#include "webserver/utils/logger.hpp"
#include <iostream>
#include <thread>
#include <vector>

using namespace webserver::utils;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Async Logger Demo" << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize logger
    AsyncLogger::instance().init("./logs", "demo", LogLevel::DEBUG);

    // Basic logging
    LOG_INFO("Application started");
    LOG_DEBUG("Debug message: %d", 42);
    LOG_WARN("Warning: This is a warning");

    // Multi-threaded logging
    std::cout << "\nTesting multi-threaded logging..." << std::endl;

    std::vector<std::thread> threads;
    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < 100; ++i) {
                LOG_INFO("Thread %d, iteration %d", t, i);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    LOG_INFO("All threads completed");

    // Shutdown logger
    AsyncLogger::instance().shutdown();

    std::cout << "\nDemo completed. Check ./logs directory for output." << std::endl;

    return 0;
}
