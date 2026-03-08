#include "webserver/storage/storage_engine.hpp"
#include "webserver/utils/logger.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace webserver;
using namespace std::chrono;

// Storage engine benchmark
class StorageBenchmark {
public:
    struct Result {
        double operations_per_second;
        double latency_us;
        size_t total_operations;
    };

    static Result benchmark_write(storage::StorageEngine& engine,
                                   size_t num_operations,
                                   size_t num_threads) {
        std::vector<std::thread> threads;
        std::atomic<size_t> counter{0};
        auto start = steady_clock::now();

        for (size_t t = 0; t < num_threads; ++t) {
            threads.emplace_back([&engine, num_operations, num_threads, &counter, t]() {
                size_t per_thread = num_operations / num_threads;
                for (size_t i = 0; i < per_thread; ++i) {
                    std::string key = "key_" + std::to_string(t) + "_" + std::to_string(i);
                    std::string value = generate_value(100);
                    engine.put(key, value);
                    ++counter;
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end = steady_clock::now();
        auto duration = duration_cast<microseconds>(end - start).count();

        Result result;
        result.total_operations = counter.load();
        result.operations_per_second = static_cast<double>(result.total_operations) * 1e6 / duration;
        result.latency_us = static_cast<double>(duration) / result.total_operations;
        return result;
    }

    static Result benchmark_read(storage::StorageEngine& engine,
                                  size_t num_operations,
                                  size_t num_threads,
                                  size_t key_range) {
        std::vector<std::thread> threads;
        std::atomic<size_t> counter{0};
        std::atomic<size_t> hits{0};

        // Pre-populate data
        LOG_INFO("Pre-populating data for read benchmark...");
        for (size_t i = 0; i < key_range; ++i) {
            engine.put("key_0_" + std::to_string(i), generate_value(100));
        }

        auto start = steady_clock::now();

        for (size_t t = 0; t < num_threads; ++t) {
            threads.emplace_back([&engine, num_operations, num_threads, key_range, &counter, &hits]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(0, static_cast<int>(key_range - 1));

                size_t per_thread = num_operations / num_threads;
                for (size_t i = 0; i < per_thread; ++i) {
                    std::string key = "key_0_" + std::to_string(dis(gen));
                    auto value = engine.get(key);
                    if (value) ++hits;
                    ++counter;
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end = steady_clock::now();
        auto duration = duration_cast<microseconds>(end - start).count();

        Result result;
        result.total_operations = counter.load();
        result.operations_per_second = static_cast<double>(result.total_operations) * 1e6 / duration;
        result.latency_us = static_cast<double>(duration) / result.total_operations;
        return result;
    }

private:
    static std::string generate_value(size_t size) {
        std::string value;
        value.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            value += 'a' + (i % 26);
        }
        return value;
    }
};

int main() {
    // Initialize logger
    utils::AsyncLogger::instance().init("./logs", "benchmark", utils::LogLevel::INFO);

    std::cout << "========================================" << std::endl;
    std::cout << "Storage Engine Benchmark" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    storage::StorageEngine::Config config;
    config.data_dir = "./benchmark_data";
    config.memtable_size = 64 * 1024 * 1024; // 64MB
    config.enable_wal = true;
    config.sync_on_write = false;

    storage::StorageEngine engine(config);
    if (!engine.init()) {
        LOG_ERROR("Failed to initialize storage engine");
        return 1;
    }

    const size_t num_operations = 100000;
    const std::vector<size_t> thread_counts = {1, 4, 8, 16};

    std::cout << std::fixed << std::setprecision(2);

    // Write benchmark
    std::cout << "\n=== Write Benchmark ===" << std::endl;
    std::cout << std::setw(10) << "Threads" << std::setw(15) << "Ops/sec" << std::setw(15) << "Latency(us)" << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    for (size_t threads : thread_counts) {
        storage::StorageEngine test_engine(config);
        test_engine.init();

        auto result = StorageBenchmark::benchmark_write(test_engine, num_operations, threads);
        std::cout << std::setw(10) << threads
                  << std::setw(15) << result.operations_per_second / 1000 << "K"
                  << std::setw(15) << result.latency_us << std::endl;
    }

    // Read benchmark
    std::cout << "\n=== Read Benchmark ===" << std::endl;
    std::cout << std::setw(10) << "Threads" << std::setw(15) << "Ops/sec" << std::setw(15) << "Latency(us)" << std::endl;
    std::cout << std::string(40, '-') << std::endl;

    for (size_t threads : thread_counts) {
        storage::StorageEngine test_engine(config);
        test_engine.init();

        auto result = StorageBenchmark::benchmark_read(test_engine, num_operations, threads, num_operations);
        std::cout << std::setw(10) << threads
                  << std::setw(15) << result.operations_per_second / 1000 << "K"
                  << std::setw(15) << result.latency_us << std::endl;
    }

    // Final stats
    auto stats = engine.get_stats();
    std::cout << "\n=== Storage Engine Stats ===" << std::endl;
    std::cout << "Memtable entries: " << stats.memtable_entries << std::endl;
    std::cout << "SSTable count: " << stats.sstable_count << std::endl;
    std::cout << "Total data size: " << stats.total_data_size / (1024 * 1024) << " MB" << std::endl;

    std::cout << "\nBenchmark complete!" << std::endl;

    engine.shutdown();
    utils::AsyncLogger::instance().shutdown();

    return 0;
}
