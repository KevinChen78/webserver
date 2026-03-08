#include "webserver/storage/storage_engine.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

namespace webserver {
namespace storage {

// ============================================================================
// Record serialization
// ============================================================================

std::vector<uint8_t> Record::serialize() const {
    std::vector<uint8_t> result;
    result.reserve(1 + 4 + 4 + key_len + value_len + 8);

    result.push_back(static_cast<uint8_t>(type));

    // Key length (little endian)
    result.push_back(key_len & 0xFF);
    result.push_back((key_len >> 8) & 0xFF);
    result.push_back((key_len >> 16) & 0xFF);
    result.push_back((key_len >> 24) & 0xFF);

    // Value length (little endian)
    result.push_back(value_len & 0xFF);
    result.push_back((value_len >> 8) & 0xFF);
    result.push_back((value_len >> 16) & 0xFF);
    result.push_back((value_len >> 24) & 0xFF);

    // Timestamp (8 bytes)
    for (int i = 0; i < 8; ++i) {
        result.push_back((timestamp >> (i * 8)) & 0xFF);
    }

    // Key
    result.insert(result.end(), key.begin(), key.end());

    // Value
    result.insert(result.end(), value.begin(), value.end());

    return result;
}

std::optional<Record> Record::deserialize(const uint8_t* data, size_t len) {
    if (len < 17) return std::nullopt; // Minimum header size

    Record record;
    record.type = static_cast<Type>(data[0]);

    // Parse key length
    record.key_len = data[1] | (data[2] << 8) | (data[3] << 16) | (data[4] << 24);

    // Parse value length
    record.value_len = data[5] | (data[6] << 8) | (data[7] << 16) | (data[8] << 24);

    // Parse timestamp
    record.timestamp = 0;
    for (int i = 0; i < 8; ++i) {
        record.timestamp |= static_cast<uint64_t>(data[9 + i]) << (i * 8);
    }

    size_t header_size = 17;
    if (len < header_size + record.key_len + record.value_len) {
        return std::nullopt;
    }

    // Parse key
    record.key.assign(reinterpret_cast<const char*>(data + header_size), record.key_len);

    // Parse value
    record.value.assign(reinterpret_cast<const char*>(data + header_size + record.key_len), record.value_len);

    return record;
}

// ============================================================================
// SkipList implementation
// ============================================================================

SkipListNode::SkipListNode(const std::string& k, const std::string& v,
                           uint64_t ts, int level)
    : key(k), value(v), timestamp(ts), forward(level) {}

SkipList::SkipList() {
    head_ = std::make_shared<SkipListNode>("", "", 0, MAX_LEVEL);
}

int SkipList::random_level() const {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<float> dist(0.0, 1.0);

    int level = 1;
    while (level < MAX_LEVEL && dist(gen) < PROBABILITY) {
        ++level;
    }
    return level;
}

std::shared_ptr<SkipListNode> SkipList::find_node(const std::string& key) const {
    auto current = head_;
    for (int i = max_level_.load(std::memory_order_relaxed) - 1; i >= 0; --i) {
        while (current->forward[i] && current->forward[i]->key < key) {
            current = current->forward[i];
        }
    }
    return current->forward[0];
}

void SkipList::put(const std::string& key, const std::string& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    std::vector<std::shared_ptr<SkipListNode>> update(MAX_LEVEL);
    auto current = head_;

    // Find update positions
    int current_max = max_level_.load(std::memory_order_relaxed);
    for (int i = current_max - 1; i >= 0; --i) {
        while (current->forward[i] && current->forward[i]->key < key) {
            current = current->forward[i];
        }
        update[i] = current;
    }

    current = current->forward[0];

    auto now = std::chrono::steady_clock::now().time_since_epoch().count();

    if (current && current->key == key) {
        // Update existing
        current->value = value;
        current->timestamp = now;
    } else {
        // Insert new
        int new_level = random_level();
        if (new_level > current_max) {
            for (int i = current_max; i < new_level; ++i) {
                update[i] = head_;
            }
            max_level_.store(new_level, std::memory_order_relaxed);
        }

        auto new_node = std::make_shared<SkipListNode>(key, value, now, new_level);
        for (int i = 0; i < new_level; ++i) {
            new_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = new_node;
        }
        size_.fetch_add(1, std::memory_order_relaxed);
    }
}

std::optional<std::string> SkipList::get(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    auto node = find_node(key);
    if (node && node->key == key) {
        return node->value;
    }
    return std::nullopt;
}

bool SkipList::remove(const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    std::vector<std::shared_ptr<SkipListNode>> update(MAX_LEVEL);
    auto current = head_;

    int current_max = max_level_.load(std::memory_order_relaxed);
    for (int i = current_max - 1; i >= 0; --i) {
        while (current->forward[i] && current->forward[i]->key < key) {
            current = current->forward[i];
        }
        update[i] = current;
    }

    current = current->forward[0];

    if (!current || current->key != key) {
        return false;
    }

    for (int i = 0; i < current_max; ++i) {
        if (update[i]->forward[i] != current) break;
        update[i]->forward[i] = current->forward[i];
    }

    size_.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

bool SkipList::contains(const std::string& key) const {
    return get(key).has_value();
}

std::vector<std::pair<std::string, std::string>> SkipList::get_all() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(size_.load(std::memory_order_relaxed));

    auto current = head_->forward[0];
    while (current) {
        result.emplace_back(current->key, current->value);
        current = current->forward[0];
    }

    return result;
}

// ============================================================================
// WAL implementation
// ============================================================================

WAL::WAL(const std::string& log_file) : log_file_(log_file) {
    writer_.open(log_file, std::ios::app | std::ios::binary);
}

WAL::~WAL() {
    flush();
}

bool WAL::append(const Record& record) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!writer_.is_open()) {
        return false;
    }

    auto data = record.serialize();
    uint32_t len = static_cast<uint32_t>(data.size());

    writer_.write(reinterpret_cast<const char*>(&len), sizeof(len));
    writer_.write(reinterpret_cast<const char*>(data.data()), data.size());

    return writer_.good();
}

void WAL::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (writer_.is_open()) {
        writer_.flush();
    }
}

std::vector<Record> WAL::recover() {
    std::vector<Record> records;

    std::ifstream file(log_file_, std::ios::binary);
    if (!file.is_open()) {
        return records;
    }

    while (file.good()) {
        uint32_t len = 0;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));

        if (!file.good() || len == 0 || len > 10 * 1024 * 1024) {
            break;
        }

        std::vector<uint8_t> data(len);
        file.read(reinterpret_cast<char*>(data.data()), len);

        auto record = Record::deserialize(data.data(), data.size());
        if (record) {
            records.push_back(*record);
        }
    }

    return records;
}

void WAL::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    writer_.close();
    writer_.open(log_file_, std::ios::trunc | std::ios::binary);
}

// ============================================================================
// StorageEngine implementation
// ============================================================================

StorageEngine::StorageEngine(const Config& config) : config_(config) {
    memtable_ = std::make_unique<SkipList>();
}

StorageEngine::~StorageEngine() {
    shutdown();
}

bool StorageEngine::init() {
    std::filesystem::create_directories(config_.data_dir);

    // Initialize WAL
    if (config_.enable_wal) {
        wal_ = std::make_unique<WAL>(config_.data_dir + "/wal.log");

        // Recover from WAL
        auto records = wal_->recover();
        for (const auto& record : records) {
            if (record.type == Record::PUT) {
                memtable_->put(record.key, record.value);
                memtable_size_ += record.key.size() + record.value.size();
            } else if (record.type == Record::DELETE) {
                memtable_->remove(record.key);
            }
        }

        LOG_INFO("Recovered %zu records from WAL", records.size());
    }

    // Load existing SSTables
    load_sstables();

    // Start background thread
    running_ = true;
    compaction_thread_ = std::thread(&StorageEngine::compaction_thread, this);

    LOG_INFO("Storage engine initialized, data_dir=%s", config_.data_dir.c_str());
    return true;
}

void StorageEngine::shutdown() {
    running_ = false;

    if (compaction_thread_.joinable()) {
        compaction_thread_.join();
    }

    // Final flush
    flush_memtable();
}

bool StorageEngine::put(const std::string& key, const std::string& value) {
    // Write to WAL first
    if (config_.enable_wal) {
        Record record;
        record.type = Record::PUT;
        record.key = key;
        record.value = value;
        record.key_len = static_cast<uint32_t>(key.size());
        record.value_len = static_cast<uint32_t>(value.size());
        record.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();

        if (!wal_->append(record)) {
            LOG_ERROR("Failed to write to WAL");
            return false;
        }

        if (config_.sync_on_write) {
            wal_->flush();
        }
    }

    // Write to memtable
    {
        std::unique_lock<std::shared_mutex> lock(memtable_mutex_);
        memtable_->put(key, value);
        memtable_size_ += key.size() + value.size();
    }

    total_writes_.fetch_add(1, std::memory_order_relaxed);

    // Check if need to flush
    if (memtable_size_.load(std::memory_order_relaxed) >= config_.memtable_size) {
        flush_memtable();
    }

    return true;
}

std::optional<std::string> StorageEngine::get(const std::string& key) const {
    // Check memtable first
    {
        std::shared_lock<std::shared_mutex> lock(memtable_mutex_);
        auto value = memtable_->get(key);
        if (value) {
            total_reads_.fetch_add(1, std::memory_order_relaxed);
            return value;
        }
    }

    // Check SSTables
    {
        std::shared_lock<std::shared_mutex> lock(sstables_mutex_);
        for (const auto& sstable : sstables_) {
            auto value = sstable->get(key);
            if (value) {
                total_reads_.fetch_add(1, std::memory_order_relaxed);
                return value;
            }
        }
    }

    return std::nullopt;
}

bool StorageEngine::remove(const std::string& key) {
    if (config_.enable_wal) {
        Record record;
        record.type = Record::DELETE;
        record.key = key;
        record.value = "";
        record.key_len = static_cast<uint32_t>(key.size());
        record.value_len = 0;
        record.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        wal_->append(record);
    }

    {
        std::unique_lock<std::shared_mutex> lock(memtable_mutex_);
        memtable_->remove(key);
    }

    return true;
}

bool StorageEngine::batch_put(const std::vector<std::pair<std::string, std::string>>& kvs) {
    for (const auto& [key, value] : kvs) {
        if (!put(key, value)) {
            return false;
        }
    }
    return true;
}

void StorageEngine::flush_memtable() {
    std::unique_ptr<SkipList> old_memtable;

    {
        std::unique_lock<std::shared_mutex> lock(memtable_mutex_);
        if (memtable_->size() == 0) {
            return;
        }
        old_memtable = std::move(memtable_);
        memtable_ = std::make_unique<SkipList>();
        memtable_size_.store(0, std::memory_order_relaxed);
    }

    auto entries = old_memtable->get_all();

    std::string data_file = config_.data_dir + "/sstable_" + std::to_string(next_sstable_id_++) + ".db";
    std::string index_file = data_file + ".idx";

    auto sstable = SSTable::build(data_file, index_file, entries);
    if (sstable) {
        std::unique_lock<std::shared_mutex> lock(sstables_mutex_);
        sstables_.push_back(std::move(sstable));
    }

    // Clear WAL after successful flush
    if (config_.enable_wal && wal_) {
        wal_->clear();
    }

    LOG_INFO("Flushed memtable to %s, entries=%zu", data_file.c_str(), entries.size());
}

void StorageEngine::load_sstables() {
    for (const auto& entry : std::filesystem::directory_iterator(config_.data_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".db") {
            std::string data_file = entry.path().string();
            std::string index_file = data_file + ".idx";

            if (std::filesystem::exists(index_file)) {
                try {
                    auto sstable = std::make_unique<SSTable>(data_file, index_file);
                    sstables_.push_back(std::move(sstable));
                } catch (...) {
                    LOG_WARN("Failed to load SSTable: %s", data_file.c_str());
                }
            }
        }
    }

    LOG_INFO("Loaded %zu SSTables", sstables_.size());
}

void StorageEngine::compaction_thread() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(60));

        // Simple compaction: merge old SSTables
        std::unique_lock<std::shared_mutex> lock(sstables_mutex_);
        if (sstables_.size() > 4) {
            LOG_INFO("Compaction needed, SSTables=%zu", sstables_.size());
            // TODO: Implement full compaction
        }
    }
}

StorageEngine::Stats StorageEngine::get_stats() const {
    Stats stats;
    stats.memtable_entries = memtable_->size();
    stats.sstable_count = sstables_.size();
    stats.total_writes = total_writes_.load(std::memory_order_relaxed);
    stats.total_reads = total_reads_.load(std::memory_order_relaxed);

    std::shared_lock<std::shared_mutex> lock(sstables_mutex_);
    for (const auto& sstable : sstables_) {
        stats.total_data_size += sstable->file_size();
    }

    return stats;
}

// ============================================================================
// SSTable implementation
// ============================================================================

SSTable::SSTable(const std::string& data_file, const std::string& index_file)
    : data_file_(data_file), index_file_(index_file) {
    load_index();
}

std::unique_ptr<SSTable> SSTable::build(
    const std::string& data_file,
    const std::string& index_file,
    const std::vector<std::pair<std::string, std::string>>& entries) {

    std::ofstream data_out(data_file, std::ios::binary);
    std::ofstream index_out(index_file, std::ios::binary);

    if (!data_out || !index_out) {
        return nullptr;
    }

    std::vector<IndexEntry> index;
    uint64_t offset = 0;

    // Sort entries by key
    auto sorted_entries = entries;
    std::sort(sorted_entries.begin(), sorted_entries.end());

    for (const auto& [key, value] : sorted_entries) {
        IndexEntry entry;
        entry.key = key;
        entry.offset = offset;
        entry.length = sizeof(uint32_t) * 2 + key.size() + value.size();

        // Write to data file
        uint32_t key_len = static_cast<uint32_t>(key.size());
        uint32_t value_len = static_cast<uint32_t>(value.size());

        data_out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        data_out.write(reinterpret_cast<const char*>(&value_len), sizeof(value_len));
        data_out.write(key.data(), key.size());
        data_out.write(value.data(), value.size());

        offset += entry.length;
        index.push_back(entry);
    }

    // Write index
    uint32_t index_size = static_cast<uint32_t>(index.size());
    index_out.write(reinterpret_cast<const char*>(&index_size), sizeof(index_size));

    for (const auto& entry : index) {
        uint32_t key_len = static_cast<uint32_t>(entry.key.size());
        index_out.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        index_out.write(entry.key.data(), entry.key.size());
        index_out.write(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
        index_out.write(reinterpret_cast<const char*>(&entry.length), sizeof(entry.length));
    }

    auto sstable = std::unique_ptr<SSTable>(new SSTable(data_file, index_file));
    sstable->index_ = std::move(index);
    sstable->file_size_ = offset;
    return sstable;
}

void SSTable::load_index() {
    std::ifstream file(index_file_, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open index file: " + index_file_);
    }

    uint32_t index_size = 0;
    file.read(reinterpret_cast<char*>(&index_size), sizeof(index_size));

    for (uint32_t i = 0; i < index_size; ++i) {
        uint32_t key_len = 0;
        file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));

        IndexEntry entry;
        entry.key.resize(key_len);
        file.read(entry.key.data(), key_len);
        file.read(reinterpret_cast<char*>(&entry.offset), sizeof(entry.offset));
        file.read(reinterpret_cast<char*>(&entry.length), sizeof(entry.length));

        index_.push_back(entry);
    }
}

std::optional<std::string> SSTable::get(const std::string& key) const {
    // Binary search in index
    auto it = std::lower_bound(index_.begin(), index_.end(), key,
        [](const IndexEntry& entry, const std::string& k) {
            return entry.key < k;
        });

    if (it == index_.end() || it->key != key) {
        return std::nullopt;
    }

    // Read from data file
    std::ifstream file(data_file_, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    file.seekg(it->offset);

    uint32_t key_len, value_len;
    file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    file.read(reinterpret_cast<char*>(&value_len), sizeof(value_len));

    file.seekg(key_len, std::ios::cur);

    std::string value;
    value.resize(value_len);
    file.read(value.data(), value_len);

    return value;
}

} // namespace storage
} // namespace webserver
