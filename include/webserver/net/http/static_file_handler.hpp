#pragma once

#include "webserver/net/http/request.hpp"
#include "webserver/net/http/response.hpp"
#include "webserver/utils/lru_cache.hpp"
#include "webserver/core/task.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <shared_mutex>

namespace webserver {
namespace net {
namespace http {

// MIME type utility
class MimeTypes {
public:
    static std::string get(const std::string& path);
    static bool is_text(const std::string& mime_type);

private:
    static const std::unordered_map<std::string, std::string> mime_map_;
};

// File cache entry
struct FileCacheEntry {
    std::vector<uint8_t> data;
    std::filesystem::file_time_type last_modified;
    std::string mime_type;
    size_t file_size;
};

// Static file handler
class StaticFileHandler {
public:
    struct Config {
        std::string root_dir = "./www";
        std::string index_file = "index.html";
        bool enable_cache = true;
        size_t cache_size = 100 * 1024 * 1024; // 100MB cache
        size_t max_file_size = 10 * 1024 * 1024; // 10MB max cached file
        bool enable_directory_listing = true;
        bool enable_gzip = false;
    };

    explicit StaticFileHandler(const Config& config = Config{});

    // Handle request (Task coroutine)
    Task<void> handle(const Request& req, Response& resp);

    // Set cache entry
    void set_cache_entry(const std::string& path, const FileCacheEntry& entry);

    // Clear cache
    void clear_cache();

private:
    // Get full path for request
    std::filesystem::path get_full_path(const std::string& uri) const;

    // Check if path is safe (within root directory)
    bool is_safe_path(const std::filesystem::path& path) const;

    // Generate directory listing HTML
    std::string generate_directory_listing(const std::filesystem::path& path, const std::string& uri);

    // Read file from disk
    std::optional<FileCacheEntry> read_file(const std::filesystem::path& path);

    // Get file extension
    static std::string get_extension(const std::filesystem::path& path);

private:
    Config config_;
    std::filesystem::path root_path_;

    // File cache (path -> entry)
    std::unique_ptr<utils::LRUCache<std::string, FileCacheEntry>> cache_;
};

} // namespace http
} // namespace net
} // namespace webserver
