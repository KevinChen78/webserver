/**
 * Full Server Demo - HTTP + FTP + Storage
 * Demonstrates all features of the WebServer project
 */

#include "webserver/core/task.hpp"
#include "webserver/net/http/server.hpp"
#include "webserver/net/http/static_file_handler.hpp"
#include "webserver/net/ftp/ftp_server.hpp"
#include "webserver/storage/storage_engine.hpp"
#include "webserver/utils/logger.hpp"
#include "webserver/utils/lru_cache.hpp"

#include <iostream>
#include <signal.h>
#include <thread>

using namespace webserver;

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    LOG_INFO("Received signal %d, shutting down...", sig);
    g_running = false;
}

int main() {
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Initialize logger
    utils::AsyncLogger::instance().init("./logs", "webserver", utils::LogLevel::INFO);

    LOG_INFO("========================================");
    LOG_INFO("WebServer - Full Server Demo");
    LOG_INFO("========================================");
    LOG_INFO("");

    // ========================================
    // Initialize Storage Engine
    // ========================================
    LOG_INFO("Initializing Storage Engine...");
    storage::StorageEngine::Config storage_config;
    storage_config.data_dir = "./data";
    storage_config.memtable_size = 64 * 1024 * 1024;  // 64MB
    storage_config.enable_wal = true;
    storage_config.sync_on_write = false;

    storage::StorageEngine storage(storage_config);
    if (!storage.init()) {
        LOG_FATAL("Failed to initialize storage engine");
        return 1;
    }
    LOG_INFO("Storage Engine initialized");

    // ========================================
    // Initialize Cache
    // ========================================
    LOG_INFO("Initializing LRU Cache...");
    utils::LRUCache<std::string, std::string> cache(10000);

    // ========================================
    // Initialize HTTP Server
    // ========================================
    LOG_INFO("Initializing HTTP Server...");

    // Create static file handler
    net::http::StaticFileHandler::Config static_config;
    static_config.root_dir = "./www";
    static_config.enable_cache = true;
    static_config.cache_size = 100 * 1024 * 1024;  // 100MB
    static_config.enable_directory_listing = true;

    auto static_handler = std::make_shared<net::http::StaticFileHandler>(static_config);

    // Create HTTP server
    net::http::Server http_server(8080);

    // Setup routes
    http_server
        // Health check
        .get("/api/health", [](const net::http::Request& req, net::http::Response& resp) -> Task<void> {
            resp.json(R"({"status":"ok","service":"webserver","version":"1.0"})");
            co_return;
        })

        // Server stats
        .get("/api/stats", [&storage](const net::http::Request& req, net::http::Response& resp) -> Task<void> {
            auto stats = storage.get_stats();
            std::ostringstream json;
            json << "{"
                 << "\"memtable_entries\":" << stats.memtable_entries << ","
                 << "\"sstable_count\":" << stats.sstable_count << ","
                 << "\"total_data_size\":" << stats.total_data_size << ","
                 << "\"total_writes\":" << stats.total_writes << ","
                 << "\"total_reads\":" << stats.total_reads
                 << "}";
            resp.json(json.str());
            co_return;
        })

        // Storage PUT
        .post("/api/storage/:key", [&storage](const net::http::Request& req, net::http::Response& resp) -> Task<void> {
            std::string path = req.path();
            size_t key_start = path.find_last_of('/') + 1;
            std::string key = path.substr(key_start);
            std::string value = req.body();

            if (storage.put(key, value)) {
                resp.json(R"({"status":"ok","message":"stored"})");
            } else {
                resp.status(net::http::Status::InternalServerError)
                   .json(R"({"status":"error","message":"failed to store"})");
            }
            co_return;
        })

        // Storage GET
        .get("/api/storage/:key", [&storage, &cache](const net::http::Request& req, net::http::Response& resp) -> Task<void> {
            std::string path = req.path();
            size_t key_start = path.find_last_of('/') + 1;
            std::string key = path.substr(key_start);

            // Try cache first
            auto cached = cache.get(key);
            if (cached) {
                resp.json("{\"status\":\"ok\",\"value\":\"" + *cached + "\",\"source\":\"cache\"}");
                co_return;
            }

            // Try storage
            auto value = storage.get(key);
            if (value) {
                cache.put(key, *value);
                resp.json("{\"status\":\"ok\",\"value\":\"" + *value + "\",\"source\":\"storage\"}");
            } else {
                resp.status(net::http::Status::NotFound)
                   .json(R"({"status":"error","message":"key not found"})");
            }
            co_return;
        })

        // Storage DELETE
        .del("/api/storage/:key", [&storage, &cache](const net::http::Request& req, net::http::Response& resp) -> Task<void> {
            std::string path = req.path();
            size_t key_start = path.find_last_of('/') + 1;
            std::string key = path.substr(key_start);

            cache.remove(key);
            storage.remove(key);
            resp.json(R"({"status":"ok","message":"deleted"})");
            co_return;
        })

        // Static file handler (catch-all)
        .route(net::http::Method::GET, "/*", [&static_handler](const net::http::Request& req, net::http::Response& resp) -> Task<void> {
            co_await static_handler->handle(req, resp);
        });

    // Start HTTP server in a thread
    std::thread http_thread([&http_server]() {
        LOG_INFO("HTTP Server starting on port 8080");
        http_server.start();
    });

    // ========================================
    // Initialize FTP Server
    // ========================================
    LOG_INFO("Initializing FTP Server...");

    net::ftp::FtpServer::Config ftp_config;
    ftp_config.bind_address = "0.0.0.0";
    ftp_config.port = 2121;
    ftp_config.root_dir = "./ftp_root";
    ftp_config.allow_anonymous = true;

    net::ftp::FtpServer ftp_server(ftp_config);
    if (!ftp_server.start()) {
        LOG_ERROR("Failed to start FTP server");
    } else {
        LOG_INFO("FTP Server started on port 2121");
    }

    // ========================================
    // Print usage info
    // ========================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "WebServer is running!" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "\nHTTP Server: http://localhost:8080" << std::endl;
    std::cout << "FTP Server: localhost:2121 (anonymous)" << std::endl;
    std::cout << "\nAPI Endpoints:" << std::endl;
    std::cout << "  GET  /api/health         - Health check" << std::endl;
    std::cout << "  GET  /api/stats          - Server statistics" << std::endl;
    std::cout << "  GET  /api/storage/:key   - Get value from storage" << std::endl;
    std::cout << "  POST /api/storage/:key   - Store value" << std::endl;
    std::cout << "  DEL  /api/storage/:key   - Delete value" << std::endl;
    std::cout << "\nStatic files are served from ./www" << std::endl;
    std::cout << "FTP files are stored in ./ftp_root" << std::endl;
    std::cout << "\nPress Ctrl+C to stop the server" << std::endl;
    std::cout << "========================================" << std::endl;

    // ========================================
    // Main loop
    // ========================================
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // ========================================
    // Shutdown
    // ========================================
    LOG_INFO("Shutting down...");

    http_server.stop();
    ftp_server.stop();
    storage.shutdown();

    if (http_thread.joinable()) {
        http_thread.join();
    }

    LOG_INFO("Shutdown complete");
    utils::AsyncLogger::instance().shutdown();

    return 0;
}
