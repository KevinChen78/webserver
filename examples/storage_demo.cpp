/**
 * Storage Engine Demo
 * Demonstrates KV storage functionality
 */

#include "webserver/storage/storage_engine.hpp"
#include "webserver/utils/logger.hpp"

#include <iostream>
#include <chrono>

using namespace webserver;
using namespace std::chrono;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Storage Engine Demo" << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize logger
    utils::AsyncLogger::instance().init("./logs", "storage_demo", utils::LogLevel::INFO);

    // Configuration
    storage::StorageEngine::Config config;
    config.data_dir = "./demo_data";
    config.memtable_size = 16 * 1024 * 1024; // 16MB
    config.enable_wal = true;

    storage::StorageEngine engine(config);
    if (!engine.init()) {
        LOG_ERROR("Failed to initialize storage engine");
        return 1;
    }

    std::cout << "\n1. Basic Operations" << std::endl;
    std::cout << "-------------------" << std::endl;

    // Put some data
    engine.put("name", "WebServer");
    engine.put("version", "1.0.0");
    engine.put("author", "Developer");

    // Get data
    auto name = engine.get("name");
    auto version = engine.get("version");

    std::cout << "name: " << (name ? *name : "not found") << std::endl;
    std::cout << "version: " << (version ? *version : "not found") << std::endl;

    std::cout << "\n2. Performance Test" << std::endl;
    std::cout << "-------------------" << std::endl;

    const int num_ops = 10000;

    // Write test
    auto start = steady_clock::now();
    for (int i = 0; i < num_ops; ++i) {
        engine.put("key_" + std::to_string(i), "value_" + std::to_string(i));
    }
    auto write_end = steady_clock::now();

    // Read test
    for (int i = 0; i < num_ops; ++i) {
        engine.get("key_" + std::to_string(i));
    }
    auto read_end = steady_clock::now();

    auto write_time = duration_cast<microseconds>(write_end - start).count();
    auto read_time = duration_cast<microseconds>(read_end - write_end).count();

    double write_qps = (num_ops * 1000000.0) / write_time;
    double read_qps = (num_ops * 1000000.0) / read_time;

    std::cout << "Wrote " << num_ops << " keys in " << write_time / 1000 << " ms"
              << " (" << write_qps / 1000 << "K ops/sec)" << std::endl;
    std::cout << "Read " << num_ops << " keys in " << read_time / 1000 << " ms"
              << " (" << read_qps / 1000 << "K ops/sec)" << std::endl;

    // Stats
    std::cout << "\n3. Statistics" << std::endl;
    std::cout << "-------------" << std::endl;

    auto stats = engine.get_stats();
    std::cout << "Memtable entries: " << stats.memtable_entries << std::endl;
    std::cout << "SSTable count: " << stats.sstable_count << std::endl;
    std::cout << "Total writes: " << stats.total_writes << std::endl;
    std::cout << "Total reads: " << stats.total_reads << std::endl;

    engine.shutdown();
    utils::AsyncLogger::instance().shutdown();

    std::cout << "\nDemo completed!" << std::endl;

    return 0;
}
