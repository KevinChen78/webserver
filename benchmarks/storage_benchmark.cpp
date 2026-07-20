/**
 * Storage Engine Benchmark
 * Production environment performance test
 */

#include "webserver/storage/storage_engine.hpp"
#include "webserver/utils/logger.hpp"

#include <iostream>
#include <chrono>
#include <random>
#include <filesystem>

using namespace webserver;
using namespace std::chrono;

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Storage Engine Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;

    std::string test_dir = "./benchmark_data";
    std::filesystem::remove_all(test_dir);

    // Production config
    storage::StorageEngine::Config config;
    config.data_dir = test_dir;
    config.memtable_size = 4 * 1024 * 1024;   // 4MB
    config.sstable_size = 16 * 1024 * 1024;   // 16MB
    config.enable_wal = true;                  // Enable WAL
    config.sync_on_write = false;              // Async WAL

    storage::StorageEngine engine(config);
    if (!engine.init()) {
        std::cerr << "Failed to initialize storage engine" << std::endl;
        return 1;
    }

    const int num_ops = 500000;
    std::cout << "\nConfiguration:" << std::endl;
    std::cout << "  Operations: " << num_ops << std::endl;
    std::cout << "  Memtable: 4MB" << std::endl;
    std::cout << "  WAL: Enabled (async)" << std::endl;
    std::cout << "  Value size: ~120 bytes" << std::endl;
    std::cout << std::endl;

    // Write benchmark
    std::cout << "Phase 1: Write Benchmark" << std::endl;
    std::cout << "------------------------" << std::endl;

    auto start = steady_clock::now();
    for (int i = 0; i < num_ops; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string value = "value_" + std::to_string(i) + std::string(100, 'x');
        engine.put(key, value);

        if ((i + 1) % 100000 == 0) {
            std::cout << "  Progress: " << (i + 1) / 1000 << "K writes" << std::endl;
        }
    }
    auto write_end = steady_clock::now();

    auto stats_mid = engine.get_stats();
    std::cout << "  Memtable entries: " << stats_mid.memtable_entries << std::endl;

    // Read benchmark (random)
    std::cout << "\nPhase 2: Read Benchmark (random)" << std::endl;
    std::cout << "----------------------------------" << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, num_ops - 1);

    int found_count = 0;
    auto read_start = steady_clock::now();
    for (int i = 0; i < num_ops; ++i) {
        int idx = dis(gen);
        auto value = engine.get("key_" + std::to_string(idx));
        if (value) found_count++;
    }
    auto read_end = steady_clock::now();

    // Results
    auto stats_final = engine.get_stats();

    auto write_time = duration_cast<microseconds>(write_end - start).count();
    auto read_time = duration_cast<microseconds>(read_end - read_start).count();

    double write_qps = (num_ops * 1000000.0) / write_time;
    double read_qps = (num_ops * 1000000.0) / read_time;

    std::cout << "\n========================================" << std::endl;
    std::cout << "Results" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Write:" << std::endl;
    std::cout << "  Time:    " << write_time / 1000 << " ms" << std::endl;
    std::cout << "  QPS:     " << write_qps / 1000 << "K ops/sec" << std::endl;
    std::cout << "  Latency: " << write_time / num_ops << " us/op" << std::endl;
    std::cout << std::endl;
    std::cout << "Read:" << std::endl;
    std::cout << "  Time:    " << read_time / 1000 << " ms" << std::endl;
    std::cout << "  QPS:     " << read_qps / 1000 << "K ops/sec" << std::endl;
    std::cout << "  Latency: " << read_time / num_ops << " us/op" << std::endl;
    std::cout << std::endl;
    std::cout << "Statistics:" << std::endl;
    std::cout << "  Cache hit rate: " << (found_count * 100.0 / num_ops) << "%" << std::endl;
    std::cout << "  SSTable count:  " << stats_final.sstable_count << std::endl;
    std::cout << "  Total writes:   " << stats_final.total_writes << std::endl;
    std::cout << "  Total reads:    " << stats_final.total_reads << std::endl;
    std::cout << "========================================" << std::endl;

    engine.shutdown();
    std::filesystem::remove_all(test_dir);

    return 0;
}
