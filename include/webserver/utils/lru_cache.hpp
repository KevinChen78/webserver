#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace webserver {
namespace utils {

// Thread-safe LRU Cache template
template<typename Key, typename Value>
class LRUCache {
public:
    using KeyValuePair = std::pair<Key, Value>;
    using ListIterator = typename std::list<KeyValuePair>::iterator;

    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    // Get value from cache
    std::optional<Value> get(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return std::nullopt;
        }

        // Move to front (most recently used)
        cache_list_.splice(cache_list_.begin(), cache_list_, it->second);

        return it->second->second;
    }

    // Put value into cache
    void put(const Key& key, const Value& value) {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_map_.find(key);
        if (it != cache_map_.end()) {
            // Update existing value and move to front
            it->second->second = value;
            cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
            return;
        }

        // Add new item
        cache_list_.emplace_front(key, value);
        cache_map_[key] = cache_list_.begin();

        // Evict if over capacity
        if (cache_list_.size() > capacity_) {
            auto last = cache_list_.end();
            --last;
            cache_map_.erase(last->first);
            cache_list_.pop_back();
        }
    }

    // Check if key exists
    bool contains(const Key& key) const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_map_.find(key) != cache_map_.end();
    }

    // Remove a key
    bool remove(const Key& key) {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        auto it = cache_map_.find(key);
        if (it == cache_map_.end()) {
            return false;
        }

        cache_list_.erase(it->second);
        cache_map_.erase(it);
        return true;
    }

    // Get current size
    size_t size() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return cache_list_.size();
    }

    // Get capacity
    size_t capacity() const {
        return capacity_;
    }

    // Clear cache
    void clear() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        cache_list_.clear();
        cache_map_.clear();
    }

    // Get cache hit/miss stats
    struct Stats {
        size_t hits;
        size_t misses;
        double hit_rate;
    };

    Stats get_stats() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        size_t total = hits_ + misses_;
        return {
            hits_,
            misses_,
            total > 0 ? static_cast<double>(hits_) / total : 0.0
        };
    }

    // Record cache access for stats
    void record_hit() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        ++hits_;
    }

    void record_miss() {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        ++misses_;
    }

private:
    size_t capacity_;
    std::list<KeyValuePair> cache_list_;
    std::unordered_map<Key, ListIterator> cache_map_;

    mutable std::shared_mutex mutex_;
    mutable size_t hits_ = 0;
    mutable size_t misses_ = 0;
};

// Cache with TTL (Time To Live) support
template<typename Key, typename Value>
class TTLCache {
public:
    struct Entry {
        Value value;
        std::chrono::steady_clock::time_point expiry;
    };

    TTLCache(size_t capacity, std::chrono::seconds default_ttl)
        : capacity_(capacity), default_ttl_(default_ttl) {}

    // Get value if not expired
    std::optional<Value> get(const Key& key) {
        std::unique_lock<std::mutex> lock(mutex_);

        auto it = cache_.find(key);
        if (it == cache_.end()) {
            return std::nullopt;
        }

        // Check if expired
        if (std::chrono::steady_clock::now() > it->second.expiry) {
            cache_.erase(it);
            return std::nullopt;
        }

        return it->second.value;
    }

    // Put with default TTL
    void put(const Key& key, const Value& value) {
        put(key, value, default_ttl_);
    }

    // Put with custom TTL
    void put(const Key& key, const Value& value, std::chrono::seconds ttl) {
        std::unique_lock<std::mutex> lock(mutex_);

        auto expiry = std::chrono::steady_clock::now() + ttl;

        // Evict expired entries if at capacity
        if (cache_.size() >= capacity_) {
            evict_expired();
        }

        // If still at capacity, evict oldest
        if (cache_.size() >= capacity_) {
            cache_.erase(cache_.begin());
        }

        cache_[key] = {value, expiry};
    }

    // Remove expired entries
    void evict_expired() {
        auto now = std::chrono::steady_clock::now();
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (now > it->second.expiry) {
                it = cache_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Clear all entries
    void clear() {
        std::unique_lock<std::mutex> lock(mutex_);
        cache_.clear();
    }

private:
    size_t capacity_;
    std::chrono::seconds default_ttl_;
    std::unordered_map<Key, Entry> cache_;
    std::mutex mutex_;
};

} // namespace utils
} // namespace webserver
