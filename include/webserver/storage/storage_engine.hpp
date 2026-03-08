#pragma once

#include "webserver/utils/logger.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace webserver {
namespace storage {

// Record structure for storage
struct Record {
    enum Type : uint8_t {
        PUT = 0,
        DELETE = 1
    };

    Type type;
    uint32_t key_len;
    uint32_t value_len;
    std::string key;
    std::string value;
    uint64_t timestamp;

    // Serialize to binary format
    std::vector<uint8_t> serialize() const;

    // Deserialize from binary
    static std::optional<Record> deserialize(const uint8_t* data, size_t len);
};

// Skip list node template
struct SkipListNode {
    std::string key;
    std::string value;
    uint64_t timestamp;
    std::vector<std::shared_ptr<SkipListNode>> forward;

    SkipListNode(const std::string& k, const std::string& v,
                 uint64_t ts, int level);
};

// Concurrent skip list for in-memory index
class SkipList {
public:
    static constexpr int MAX_LEVEL = 16;
    static constexpr float PROBABILITY = 0.5f;

    SkipList();
    ~SkipList() = default;

    // Insert or update a key-value pair
    void put(const std::string& key, const std::string& value);

    // Get value by key
    std::optional<std::string> get(const std::string& key) const;

    // Delete a key
    bool remove(const std::string& key);

    // Check if key exists
    bool contains(const std::string& key) const;

    // Get approximate size
    size_t size() const { return size_.load(std::memory_order_relaxed); }

    // Get all entries (for compaction)
    std::vector<std::pair<std::string, std::string>> get_all() const;

private:
    int random_level() const;
    std::shared_ptr<SkipListNode> find_node(const std::string& key) const;

private:
    mutable std::shared_mutex mutex_;
    std::shared_ptr<SkipListNode> head_;
    std::atomic<int> max_level_{1};
    std::atomic<size_t> size_{0};
};

// Write-Ahead Log for durability
class WAL {
public:
    explicit WAL(const std::string& log_file);
    ~WAL();

    // Append record to WAL
    bool append(const Record& record);

    // Flush to disk
    void flush();

    // Recover from WAL
    std::vector<Record> recover();

    // Clear WAL
    void clear();

private:
    std::string log_file_;
    std::ofstream writer_;
    std::mutex mutex_;
};

// SSTable for persistent storage
class SSTable {
public:
    struct IndexEntry {
        std::string key;
        uint64_t offset;
        uint32_t length;
    };

    SSTable(const std::string& data_file, const std::string& index_file);

    // Build SSTable from memtable
    static std::unique_ptr<SSTable> build(
        const std::string& data_file,
        const std::string& index_file,
        const std::vector<std::pair<std::string, std::string>>& entries);

    // Get value by key
    std::optional<std::string> get(const std::string& key) const;

    // Get file size
    size_t file_size() const { return file_size_; }

private:
    std::string data_file_;
    std::string index_file_;
    std::vector<IndexEntry> index_;
    size_t file_size_ = 0;

    void load_index();
};

// Main storage engine
class StorageEngine {
public:
    struct Config {
        std::string data_dir = "./data";
        size_t memtable_size = 4 * 1024 * 1024; // 4MB flush threshold
        size_t sstable_size = 16 * 1024 * 1024; // 16MB SSTable size
        bool enable_wal = true;
        bool sync_on_write = false;
    };

    explicit StorageEngine(const Config& config = Config{});
    ~StorageEngine();

    // Initialize engine
    bool init();

    // Shutdown engine
    void shutdown();

    // Put key-value
    bool put(const std::string& key, const std::string& value);

    // Get value by key
    std::optional<std::string> get(const std::string& key) const;

    // Delete key
    bool remove(const std::string& key);

    // Batch operations
    bool batch_put(const std::vector<std::pair<std::string, std::string>>& kvs);

    // Get engine stats
    struct Stats {
        size_t memtable_entries;
        size_t sstable_count;
        size_t total_data_size;
        uint64_t total_writes;
        uint64_t total_reads;
    };
    Stats get_stats() const;

private:
    // Flush memtable to SSTable
    void flush_memtable();

    // Load existing SSTables
    void load_sstables();

    // Background compaction
    void compaction_thread();

private:
    Config config_;

    // Memtable (in-memory)
    mutable std::shared_mutex memtable_mutex_;
    std::unique_ptr<SkipList> memtable_;
    std::atomic<size_t> memtable_size_{0};

    // WAL
    std::unique_ptr<WAL> wal_;

    // SSTables (on-disk)
    mutable std::shared_mutex sstables_mutex_;
    std::vector<std::unique_ptr<SSTable>> sstables_;

    // Stats
    mutable std::atomic<uint64_t> total_writes_{0};
    mutable std::atomic<uint64_t> total_reads_{0};

    // Background thread
    std::atomic<bool> running_{false};
    std::thread compaction_thread_;

    int next_sstable_id_ = 0;
};

} // namespace storage
} // namespace webserver
