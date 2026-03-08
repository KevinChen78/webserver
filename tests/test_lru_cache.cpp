/**
 * LRU Cache Unit Tests
 * Tests for thread-safe LRU cache implementation
 */

#include "webserver/utils/lru_cache.hpp"

#include <cassert>
#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace webserver::utils;

// Test basic put/get operations
void test_basic_operations() {
    std::cout << "Testing basic put/get operations..." << std::endl;

    LRUCache<std::string, int> cache(3);

    // Test put and get
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);

    auto val_a = cache.get("a");
    assert(val_a.has_value() && "Key 'a' should exist");
    assert(*val_a == 1 && "Value should be 1");

    auto val_b = cache.get("b");
    assert(val_b.has_value() && *val_b == 2);

    auto val_c = cache.get("c");
    assert(val_c.has_value() && *val_c == 3);

    // Test non-existent key
    auto val_d = cache.get("d");
    assert(!val_d.has_value() && "Non-existent key should return nullopt");

    std::cout << "  ✓ Basic operations test passed" << std::endl;
}

// Test LRU eviction
void test_lru_eviction() {
    std::cout << "Testing LRU eviction..." << std::endl;

    LRUCache<std::string, int> cache(3);

    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);

    // Access 'a' to make it most recently used
    cache.get("a");

    // Add 'd', should evict 'b' (least recently used)
    cache.put("d", 4);

    assert(cache.get("a").has_value() && "'a' should still exist");
    assert(!cache.get("b").has_value() && "'b' should be evicted");
    assert(cache.get("c").has_value() && "'c' should still exist");
    assert(cache.get("d").has_value() && "'d' should exist");

    // Add 'e', should evict 'c'
    cache.put("e", 5);
    assert(!cache.get("c").has_value() && "'c' should be evicted");

    std::cout << "  ✓ LRU eviction test passed" << std::endl;
}

// Test update existing key
void test_update() {
    std::cout << "Testing update existing key..." << std::endl;

    LRUCache<std::string, std::string> cache(2);

    cache.put("key", "old_value");
    auto val1 = cache.get("key");
    assert(val1.has_value() && *val1 == "old_value");

    // Update with new value
    cache.put("key", "new_value");
    auto val2 = cache.get("key");
    assert(val2.has_value() && *val2 == "new_value");

    // Size should still be 1
    assert(cache.size() == 1 && "Size should be 1 after update");

    std::cout << "  ✓ Update test passed" << std::endl;
}

// Test remove operation
void test_remove() {
    std::cout << "Testing remove operation..." << std::endl;

    LRUCache<int, std::string> cache(5);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    assert(cache.remove(2) && "Remove should return true for existing key");
    assert(!cache.get(2).has_value() && "Removed key should not exist");
    assert(cache.size() == 2 && "Size should be 2 after remove");

    assert(!cache.remove(999) && "Remove should return false for non-existent key");

    // Other keys should still exist
    assert(cache.get(1).has_value() && cache.get(3).has_value());

    std::cout << "  ✓ Remove test passed" << std::endl;
}

// Test contains method
void test_contains() {
    std::cout << "Testing contains method..." << std::endl;

    LRUCache<std::string, int> cache(3);

    cache.put("a", 1);

    assert(cache.contains("a") && "Should contain 'a'");
    assert(!cache.contains("b") && "Should not contain 'b'");

    std::cout << "  ✓ Contains test passed" << std::endl;
}

// Test clear operation
void test_clear() {
    std::cout << "Testing clear operation..." << std::endl;

    LRUCache<std::string, int> cache(10);

    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);

    assert(cache.size() == 3);

    cache.clear();

    assert(cache.size() == 0 && "Size should be 0 after clear");
    assert(!cache.get("a").has_value() && "Should not contain any keys after clear");

    std::cout << "  ✓ Clear test passed" << std::endl;
}

// Test capacity boundary
void test_capacity_boundary() {
    std::cout << "Testing capacity boundary..." << std::endl;

    LRUCache<int, int> cache(1);

    cache.put(1, 100);
    assert(cache.size() == 1);

    cache.put(2, 200);
    assert(cache.size() == 1 && "Size should not exceed capacity");
    assert(!cache.get(1).has_value() && "First item should be evicted");
    assert(cache.get(2).has_value() && *cache.get(2) == 200);

    std::cout << "  ✓ Capacity boundary test passed" << std::endl;
}

// Test concurrent access
void test_concurrent_access() {
    std::cout << "Testing concurrent access..." << std::endl;

    LRUCache<int, int> cache(1000);

    const int num_threads = 10;
    const int ops_per_thread = 10000;

    std::vector<std::thread> threads;

    auto start = std::chrono::steady_clock::now();

    // Spawn writer threads
    for (int t = 0; t < num_threads / 2; ++t) {
        threads.emplace_back([&cache, t, ops_per_thread]() {
            for (int i = 0; i < ops_per_thread; ++i) {
                int key = t * ops_per_thread + i;
                cache.put(key, key * 2);
            }
        });
    }

    // Spawn reader threads
    for (int t = num_threads / 2; t < num_threads; ++t) {
        threads.emplace_back([&cache, t, ops_per_thread]() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, (t - num_threads / 2) * ops_per_thread - 1);

            for (int i = 0; i < ops_per_thread; ++i) {
                cache.get(dis(gen));
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    size_t total_ops = num_threads * ops_per_thread;
    double ops_per_sec = (total_ops * 1000.0) / duration;

    std::cout << "  Completed " << total_ops << " operations in " << duration << "ms"
              << " (" << ops_per_sec / 1000 << "K ops/sec)" << std::endl;

    // Verify cache size doesn't exceed capacity
    assert(cache.size() <= 1000 && "Cache size should not exceed capacity");

    std::cout << "  ✓ Concurrent access test passed" << std::endl;
}

// Test hit/miss stats
void test_stats() {
    std::cout << "Testing hit/miss stats..." << std::endl;

    LRUCache<std::string, int> cache(3);

    cache.put("a", 1);
    cache.put("b", 2);

    // Generate hits
    cache.get("a");
    cache.get("b");
    cache.get("a");

    // Record hits
    cache.record_hit();
    cache.record_hit();
    cache.record_hit();

    // Record misses
    cache.record_miss();
    cache.record_miss();

    auto stats = cache.get_stats();
    assert(stats.hits == 3 && "Should have 3 hits");
    assert(stats.misses == 2 && "Should have 2 misses");
    assert(stats.hit_rate == 0.6 && "Hit rate should be 60%");

    std::cout << "  ✓ Stats test passed" << std::endl;
}

// Test with complex value types
void test_complex_types() {
    std::cout << "Testing complex value types..." << std::endl;

    struct User {
        std::string name;
        int age;
        std::vector<std::string> tags;

        bool operator==(const User& other) const {
            return name == other.name && age == other.age && tags == other.tags;
        }
    };

    LRUCache<int, User> cache(10);

    User user1{"Alice", 30, {"developer", "cpp"}};
    User user2{"Bob", 25, {"designer", "ui"}};

    cache.put(1, user1);
    cache.put(2, user2);

    auto retrieved = cache.get(1);
    assert(retrieved.has_value() && "Should retrieve user");
    assert(retrieved->name == "Alice" && retrieved->age == 30);
    assert(retrieved->tags.size() == 2);

    std::cout << "  ✓ Complex types test passed" << std::endl;
}

// Performance benchmark
void test_performance() {
    std::cout << "Testing performance..." << std::endl;

    const int capacity = 10000;
    const int num_ops = 1000000;

    LRUCache<int, int> cache(capacity);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, capacity * 2 - 1);

    // Mixed read/write benchmark
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_ops; ++i) {
        int key = dis(gen);
        if (i % 3 == 0) {
            // 33% writes
            cache.put(key, i);
        } else {
            // 67% reads
            cache.get(key);
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    double ops_per_sec = (num_ops * 1000000.0) / duration;
    double latency_ns = (duration * 1000.0) / num_ops;

    std::cout << "  Performed " << num_ops << " operations in " << duration / 1000 << "ms"
              << " (" << ops_per_sec / 1000000 << "M ops/sec, "
              << latency_ns << " ns/op)" << std::endl;

    std::cout << "  ✓ Performance test completed" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "LRU Cache Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        test_basic_operations();
        test_lru_eviction();
        test_update();
        test_remove();
        test_contains();
        test_clear();
        test_capacity_boundary();
        test_concurrent_access();
        test_stats();
        test_complex_types();
        test_performance();

        std::cout << "\n========================================" << std::endl;
        std::cout << "All tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
