/**
 * Full Server Demo - Storage + Logger + FTP
 * Demonstrates the main features of the WebServer project
 */

#include "webserver/storage/storage_engine.hpp"
#include "webserver/utils/logger.hpp"
#include "webserver/net/ftp/ftp_server.hpp"
#include "webserver/utils/lru_cache.hpp"

#include <iostream>
#include <signal.h>
#include <thread>
#include <chrono>

using namespace webserver;

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    std::cout << "Received signal " << sig << ", shutting down..." << std::endl;
    g_running = false;
}

int main() {
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize logger
    utils::AsyncLogger::instance().init("./logs", "webserver", utils::LogLevel::INFO);

    std::cout << "========================================" << std::endl;
    std::cout << "WebServer - Full Server Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "" << std::endl;

    // ========================================
    // Initialize Storage Engine
    // ========================================
    std::cout << "Initializing Storage Engine..." << std::endl;
    storage::StorageEngine::Config storage_config;
    storage_config.data_dir = "./data";
    storage_config.memtable_size = 64 * 1024 * 1024;  // 64MB
    storage_config.enable_wal = true;
    storage_config.sync_on_write = false;

    storage::StorageEngine storage(storage_config);
    if (!storage.init()) {
        std::cerr << "Failed to initialize storage engine" << std::endl;
        return 1;
    }
    std::cout << "Storage Engine initialized" << std::endl;

    // ========================================
    // Initialize Cache
    // ========================================
    std::cout << "Initializing LRU Cache..." << std::endl;
    utils::LRUCache<std::string, std::string> cache(10000);

    // ========================================
    // Initialize FTP Server
    // ========================================
    std::cout << "Initializing FTP Server..." << std::endl;

    net::ftp::FtpServer::Config ftp_config;
    ftp_config.bind_address = "0.0.0.0";
    ftp_config.port = 2121;
    ftp_config.root_dir = "./ftp_root";
    ftp_config.allow_anonymous = true;

    net::ftp::FtpServer ftp_server(ftp_config);
    if (!ftp_server.start()) {
        std::cerr << "Failed to start FTP server" << std::endl;
    } else {
        std::cout << "FTP Server started on port 2121" << std::endl;
    }

    // ========================================
    // Demo: Storage operations
    // ========================================
    std::cout << "" << std::endl;
    std::cout << "Testing Storage Operations..." << std::endl;

    // Put some test data
    for (int i = 0; i < 10; i++) {
        std::string key = "key_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i) + "_" + std::to_string(std::time(nullptr));
        if (storage.put(key, value)) {
            std::cout << "  Stored: " << key << " -> " << value << std::endl;
        }
    }

    // Read back
    std::cout << "  Reading back..." << std::endl;
    for (int i = 0; i < 10; i++) {
        std::string key = "key_" + std::to_string(i);
        auto value = storage.get(key);
        if (value) {
            std::cout << "  Retrieved: " << key << " -> " << *value << std::endl;
        }
    }

    // Get stats
    auto stats = storage.get_stats();
    std::cout << "  Storage stats:" << std::endl;
    std::cout << "    Total writes: " << stats.total_writes << std::endl;
    std::cout << "    Total reads: " << stats.total_reads << std::endl;

    // ========================================
    // Print usage info
    // ========================================
    std::cout << "" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "WebServer is running!" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "FTP Server: localhost:2121 (anonymous)" << std::endl;
    std::cout << "  Files stored in ./ftp_root" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Logs: ./logs/" << std::endl;
    std::cout << "Data: ./data/" << std::endl;
    std::cout << "" << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    std::cout << "" << std::endl;

    // ========================================
    // Main loop
    // ========================================
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Cleanup
    std::cout << "Shutting down..." << std::endl;
    ftp_server.stop();
    storage.shutdown();
    utils::AsyncLogger::instance().shutdown();

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
