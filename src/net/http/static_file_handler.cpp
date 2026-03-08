#include "webserver/net/http/static_file_handler.hpp"
#include "webserver/utils/logger.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace webserver {
namespace net {
namespace http {

// ============================================================================
// MIME Types
// ============================================================================

const std::unordered_map<std::string, std::string> MimeTypes::mime_map_ = {
    // Text
    {".html", "text/html"},
    {".htm", "text/html"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".json", "application/json"},
    {".xml", "application/xml"},
    {".txt", "text/plain"},
    // Images
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".svg", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".webp", "image/webp"},
    // Fonts
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {".otf", "font/otf"},
    // Documents
    {".pdf", "application/pdf"},
    {".zip", "application/zip"},
    {".gz", "application/gzip"},
    {".tar", "application/x-tar"},
    {".mp4", "video/mp4"},
    {".mp3", "audio/mpeg"},
    // Default
    {"", "application/octet-stream"}
};

std::string MimeTypes::get(const std::string& path) {
    size_t pos = path.find_last_of('.');
    if (pos != std::string::npos) {
        std::string ext = path.substr(pos);
        auto it = mime_map_.find(ext);
        if (it != mime_map_.end()) {
            return it->second;
        }
    }
    return "application/octet-stream";
}

bool MimeTypes::is_text(const std::string& mime_type) {
    return mime_type.find("text/") == 0 ||
           mime_type.find("application/json") == 0 ||
           mime_type.find("application/javascript") == 0 ||
           mime_type.find("application/xml") == 0 ||
           mime_type.find("image/svg") == 0;
}

// ============================================================================
// StaticFileHandler
// ============================================================================

StaticFileHandler::StaticFileHandler(const Config& config)
    : config_(config) {
    root_path_ = std::filesystem::absolute(config.root_dir);
    std::filesystem::create_directories(root_path_);

    if (config.enable_cache) {
        cache_ = std::make_unique<utils::LRUCache<std::string, FileCacheEntry>>(
            config.cache_size / sizeof(FileCacheEntry));
    }

    LOG_INFO("StaticFileHandler initialized, root=%s, cache_size=%zu MB",
             config.root_dir.c_str(), config.cache_size / (1024 * 1024));
}

Task<void> StaticFileHandler::handle(const Request& req, Response& resp) {
    std::string uri = req.path();

    // Remove query string
    size_t query_pos = uri.find('?');
    if (query_pos != std::string::npos) {
        uri = uri.substr(0, query_pos);
    }

    // URL decode
    // Simplified: just handle spaces
    size_t space_pos = 0;
    while ((space_pos = uri.find("%20", space_pos)) != std::string::npos) {
        uri.replace(space_pos, 3, " ");
        ++space_pos;
    }

    // Get full path
    auto full_path = get_full_path(uri);

    // Security check
    if (!is_safe_path(full_path)) {
        resp.status(Status::Forbidden)
           .content_type("text/html")
           .body("<h1>403 Forbidden</h1><p>Access denied</p>");
        co_return;
    }

    // Check if exists
    if (!std::filesystem::exists(full_path)) {
        resp.status(Status::NotFound)
           .content_type("text/html")
           .body("<h1>404 Not Found</h1><p>File not found: " + uri + "</p>");
        co_return;
    }

    // Handle directory
    if (std::filesystem::is_directory(full_path)) {
        // Try index file
        auto index_path = full_path / config_.index_file;
        if (std::filesystem::exists(index_path) && std::filesystem::is_regular_file(index_path)) {
            full_path = index_path;
        } else if (config_.enable_directory_listing) {
            // Generate directory listing
            std::string html = generate_directory_listing(full_path, uri);
            resp.html(html);
            co_return;
        } else {
            resp.status(Status::Forbidden)
               .content_type("text/html")
               .body("<h1>403 Forbidden</h1><p>Directory listing disabled</p>");
            co_return;
        }
    }

    // Handle file
    if (!std::filesystem::is_regular_file(full_path)) {
        resp.status(Status::Forbidden)
           .content_type("text/html")
           .body("<h1>403 Forbidden</h1><p>Not a regular file</p>");
        co_return;
    }

    // Get file info
    auto last_modified = std::filesystem::last_write_time(full_path);
    auto file_size = std::filesystem::file_size(full_path);
    std::string mime_type = MimeTypes::get(full_path.string());

    // Check If-Modified-Since
    if (req.has_header("If-Modified-Since")) {
        // Simplified: always return 200 for now
        // In production, parse the date and compare
    }

    // Try cache first (for small files)
    std::string cache_key = full_path.string();
    FileCacheEntry entry;
    bool from_cache = false;

    if (config_.enable_cache && cache_ && file_size <= config_.max_file_size) {
        auto cached = cache_->get(cache_key);
        if (cached) {
            if (cached->last_modified == last_modified) {
                entry = *cached;
                from_cache = true;
                cache_->record_hit();
            } else {
                cache_->record_miss();
            }
        } else {
            cache_->record_miss();
        }
    }

    // Read from disk if not in cache
    if (!from_cache) {
        auto file_entry = read_file(full_path);
        if (!file_entry) {
            resp.status(Status::InternalServerError)
               .content_type("text/html")
               .body("<h1>500 Internal Server Error</h1><p>Failed to read file</p>");
            co_return;
        }
        entry = *file_entry;

        // Cache the entry
        if (config_.enable_cache && cache_ && file_size <= config_.max_file_size) {
            cache_->put(cache_key, entry);
        }
    }

    // Build response
    resp.status(Status::OK)
       .content_type(mime_type)
       .header("Content-Length", std::to_string(entry.file_size))
       .header("Cache-Control", "public, max-age=3600")
       .header("Last-Modified", "Wed, 21 Oct 2025 07:28:00 GMT"); // Simplified

    // Set body
    std::string body(reinterpret_cast<const char*>(entry.data.data()), entry.data.size());
    resp.body(body);

    LOG_DEBUG("Served file: %s (size=%zu, cache=%s)",
              full_path.string().c_str(), file_size, from_cache ? "hit" : "miss");

    co_return;
}

std::filesystem::path StaticFileHandler::get_full_path(const std::string& uri) const {
    std::string decoded = uri;
    // Basic URL decode
    size_t pos = 0;
    while ((pos = decoded.find("%20", pos)) != std::string::npos) {
        decoded.replace(pos, 3, " ");
    }

    // Remove leading slashes
    while (!decoded.empty() && decoded[0] == '/') {
        decoded = decoded.substr(1);
    }

    // Handle special paths
    if (decoded.empty() || decoded == "/") {
        return root_path_ / config_.index_file;
    }

    return root_path_ / decoded;
}

bool StaticFileHandler::is_safe_path(const std::filesystem::path& path) const {
    try {
        auto canonical_path = std::filesystem::weakly_canonical(path);
        auto canonical_root = std::filesystem::weakly_canonical(root_path_);

        // Check if path starts with root
        auto root_str = canonical_root.string();
        auto path_str = canonical_path.string();

        return path_str.find(root_str) == 0;
    } catch (...) {
        return false;
    }
}

std::string StaticFileHandler::generate_directory_listing(const std::filesystem::path& path, const std::string& uri) {
    std::ostringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n<head>\n"
         << "<meta charset=\"UTF-8\">\n"
         << "<title>Index of " << uri << "</title>\n"
         << "<style>\n"
         << "body { font-family: Arial, sans-serif; margin: 40px; }\n"
         << "h1 { border-bottom: 1px solid #ccc; padding-bottom: 10px; }\n"
         << "table { border-collapse: collapse; width: 100%; max-width: 800px; }\n"
         << "th, td { text-align: left; padding: 8px; border-bottom: 1px solid #ddd; }\n"
         << "th { background-color: #f2f2f2; }\n"
         << "tr:hover { background-color: #f5f5f5; }\n"
         << "a { color: #0066cc; text-decoration: none; }\n"
         << "a:hover { text-decoration: underline; }\n"
         << ".dir { font-weight: bold; }\n"
         << "</style>\n"
         << "</head>\n<body>\n"
         << "<h1>Index of " << uri << "</h1>\n"
         << "<table>\n"
         << "<tr><th>Name</th><th>Size</th><th>Modified</th></tr>\n";

    // Parent directory link
    if (uri != "/") {
        html << "<tr><td><a href=\"../\">../</a></td><td>-</td><td>-</td></tr>\n";
    }

    // List entries
    std::vector<std::filesystem::directory_entry> entries;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        entries.push_back(entry);
    }

    // Sort: directories first, then alphabetically
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        bool a_is_dir = a.is_directory();
        bool b_is_dir = b.is_directory();
        if (a_is_dir != b_is_dir) return a_is_dir > b_is_dir;
        return a.path().filename().string() < b.path().filename().string();
    });

    for (const auto& entry : entries) {
        std::string name = entry.path().filename().string();
        bool is_dir = entry.is_directory();

        // Format size
        std::string size_str = "-";
        if (!is_dir) {
            auto size = entry.file_size();
            if (size < 1024) {
                size_str = std::to_string(size) + " B";
            } else if (size < 1024 * 1024) {
                size_str = std::to_string(size / 1024) + " KB";
            } else {
                size_str = std::to_string(size / (1024 * 1024)) + " MB";
            }
        }

        // Format time
        auto time = entry.last_write_time();
        auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(time);
        auto time_t = std::chrono::system_clock::to_time_t(sys_time);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &time_t);
#else
        localtime_r(&time_t, &tm);
#endif

        std::ostringstream time_str;
        time_str << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

        std::string link = name;
        if (is_dir) link += "/";

        html << "<tr><td class=\"" << (is_dir ? "dir" : "file") << "\">"
             << "<a href=\"" << link << "\">" << link << "</a></td>"
             << "<td>" << size_str << "</td>"
             << "<td>" << time_str.str() << "</td></tr>\n";
    }

    html << "</table>\n"
         << "<hr>\n"
         << "<p><em>WebServer/1.0</em></p>\n"
         << "</body>\n</html>";

    return html.str();
}

std::optional<FileCacheEntry> StaticFileHandler::read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::nullopt;
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    FileCacheEntry entry;
    entry.file_size = size;
    entry.last_modified = std::filesystem::last_write_time(path);
    entry.mime_type = MimeTypes::get(path.string());

    entry.data.resize(size);
    if (!file.read(reinterpret_cast<char*>(entry.data.data()), size)) {
        return std::nullopt;
    }

    return entry;
}

std::string StaticFileHandler::get_extension(const std::filesystem::path& path) {
    return path.extension().string();
}

void StaticFileHandler::set_cache_entry(const std::string& path, const FileCacheEntry& entry) {
    if (cache_) {
        cache_->put(path, entry);
    }
}

void StaticFileHandler::clear_cache() {
    if (cache_) {
        cache_->clear();
    }
}

} // namespace http
} // namespace net
} // namespace webserver
