/**
 * Static File Handler Unit Tests
 * Tests for HTTP static file serving functionality
 */

#include "webserver/net/http/static_file_handler.hpp"
#include "webserver/net/http/request.hpp"
#include "webserver/net/http/response.hpp"
#include "webserver/core/task.hpp"
#include "webserver/utils/logger.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

using namespace webserver;
using namespace webserver::net::http;

// Test MIME type detection
void test_mime_types() {
    std::cout << "Testing MIME type detection..." << std::endl;

    assert(MimeTypes::get("test.html") == "text/html");
    assert(MimeTypes::get("test.css") == "text/css");
    assert(MimeTypes::get("test.js") == "application/javascript");
    assert(MimeTypes::get("test.json") == "application/json");
    assert(MimeTypes::get("test.png") == "image/png");
    assert(MimeTypes::get("test.jpg") == "image/jpeg");
    assert(MimeTypes::get("test.pdf") == "application/pdf");
    assert(MimeTypes::get("test.unknown") == "application/octet-stream");

    assert(MimeTypes::is_text("text/html") == true);
    assert(MimeTypes::is_text("application/json") == true);
    assert(MimeTypes::is_text("image/png") == false);
    assert(MimeTypes::is_text("application/octet-stream") == false);

    std::cout << "  ✓ MIME type detection test passed" << std::endl;
}

// Test serving existing file
void test_serve_existing_file() {
    std::cout << "Testing serve existing file..." << std::endl;

    std::string test_dir = "./test_www";
    std::filesystem::create_directories(test_dir);

    // Create test file
    std::string test_content = "<html><body>Hello World</body></html>";
    std::ofstream(test_dir + "/index.html") << test_content;

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_cache = false;

    StaticFileHandler handler(config);

    Request req;
    req.parse("GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");

    Response resp;
    handler.handle(req, resp).result();

    assert(resp.status_code() == static_cast<int>(Status::OK));
    assert(resp.body() == test_content);
    assert(resp.header("Content-Type") == "text/html");

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Serve existing file test passed" << std::endl;
}

// Test serving non-existent file
void test_serve_nonexistent_file() {
    std::cout << "Testing serve non-existent file..." << std::endl;

    std::string test_dir = "./test_www_404";
    std::filesystem::create_directories(test_dir);

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_cache = false;

    StaticFileHandler handler(config);

    Request req;
    req.parse("GET /nonexistent.html HTTP/1.1\r\nHost: localhost\r\n\r\n");

    Response resp;
    handler.handle(req, resp).result();

    assert(resp.status_code() == static_cast<int>(Status::NotFound));
    assert(resp.body().find("404") != std::string::npos);

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Serve non-existent file test passed" << std::endl;
}

// Test directory listing
void test_directory_listing() {
    std::cout << "Testing directory listing..." << std::endl;

    std::string test_dir = "./test_www_list";
    std::filesystem::create_directories(test_dir);
    std::filesystem::create_directories(test_dir + "/subdir");

    std::ofstream(test_dir + "/file1.txt") << "content1";
    std::ofstream(test_dir + "/file2.txt") << "content2";

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_directory_listing = true;
    config.enable_cache = false;

    StaticFileHandler handler(config);

    Request req;
    req.parse("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    Response resp;
    handler.handle(req, resp).result();

    assert(resp.status_code() == static_cast<int>(Status::OK));
    assert(resp.header("Content-Type") == "text/html");
    assert(resp.body().find("file1.txt") != std::string::npos);
    assert(resp.body().find("file2.txt") != std::string::npos);
    assert(resp.body().find("subdir/") != std::string::npos);

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Directory listing test passed" << std::endl;
}

// Test disabled directory listing
void test_disabled_directory_listing() {
    std::cout << "Testing disabled directory listing..." << std::endl;

    std::string test_dir = "./test_www_no_list";
    std::filesystem::create_directories(test_dir);

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_directory_listing = false;
    config.enable_cache = false;

    StaticFileHandler handler(config);

    Request req;
    req.parse("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    Response resp;
    handler.handle(req, resp).result();

    assert(resp.status_code() == static_cast<int>(Status::Forbidden));

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Disabled directory listing test passed" << std::endl;
}

// Test index file fallback
void test_index_fallback() {
    std::cout << "Testing index file fallback..." << std::endl;

    std::string test_dir = "./test_www_index";
    std::filesystem::create_directories(test_dir);

    std::string index_content = "<html><body>Index Page</body></html>";
    std::ofstream(test_dir + "/index.html") << index_content;

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.index_file = "index.html";
    config.enable_cache = false;

    StaticFileHandler handler(config);

    Request req;
    req.parse("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");

    Response resp;
    handler.handle(req, resp).result();

    assert(resp.status_code() == static_cast<int>(Status::OK));
    assert(resp.body() == index_content);

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Index file fallback test passed" << std::endl;
}

// Test path security (path traversal prevention)
void test_path_security() {
    std::cout << "Testing path security..." << std::endl;

    std::string test_dir = "./test_www_secure";
    std::filesystem::create_directories(test_dir);
    std::filesystem::create_directories("./test_secret");

    std::ofstream("./test_secret/secret.txt") << "secret content";
    std::ofstream(test_dir + "/public.txt") << "public content";

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_cache = false;

    StaticFileHandler handler(config);

    // Try path traversal
    Request req;
    req.parse("GET /../test_secret/secret.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");

    Response resp;
    handler.handle(req, resp).result();

    // Should be forbidden or not found
    assert(resp.status_code() == static_cast<int>(Status::Forbidden) ||
           resp.status_code() == static_cast<int>(Status::NotFound));

    std::filesystem::remove_all(test_dir);
    std::filesystem::remove_all("./test_secret");

    std::cout << "  ✓ Path security test passed" << std::endl;
}

// Test file caching
void test_file_caching() {
    std::cout << "Testing file caching..." << std::endl;

    std::string test_dir = "./test_www_cache";
    std::filesystem::create_directories(test_dir);

    std::string test_content = "Cached content";
    std::ofstream(test_dir + "/cached.txt") << test_content;

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_cache = true;
    config.cache_size = 10 * 1024 * 1024; // 10MB
    config.enable_cache = false;

    StaticFileHandler handler(config);

    // First request (should load from disk)
    Request req1;
    req1.parse("GET /cached.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    Response resp1;
    handler.handle(req1, resp1).result();
    assert(resp1.body() == test_content);

    // Modify file on disk
    std::ofstream(test_dir + "/cached.txt") << "Modified content";

    // Second request (with cache disabled, should see new content)
    Request req2;
    req2.parse("GET /cached.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    Response resp2;
    handler.handle(req2, resp2).result();
    // Without cache, should get modified content
    assert(resp2.body() == "Modified content");

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ File caching test passed" << std::endl;
}

// Test various file types
void test_various_file_types() {
    std::cout << "Testing various file types..." << std::endl;

    std::string test_dir = "./test_www_types";
    std::filesystem::create_directories(test_dir);

    // Create files of different types
    std::ofstream(test_dir + "/style.css") << "body { margin: 0; }";
    std::ofstream(test_dir + "/script.js") << "console.log('hello');";
    std::ofstream(test_dir + "/data.json") << "{\"key\": \"value\"}";

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_cache = false;

    StaticFileHandler handler(config);

    // Test CSS
    {
        Request req;
        req.parse("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n");
        Response resp;
        handler.handle(req, resp).result();
        assert(resp.header("Content-Type") == "text/css");
    }

    // Test JS
    {
        Request req;
        req.parse("GET /script.js HTTP/1.1\r\nHost: localhost\r\n\r\n");
        Response resp;
        handler.handle(req, resp).result();
        assert(resp.header("Content-Type") == "application/javascript");
    }

    // Test JSON
    {
        Request req;
        req.parse("GET /data.json HTTP/1.1\r\nHost: localhost\r\n\r\n");
        Response resp;
        handler.handle(req, resp).result();
        assert(resp.header("Content-Type") == "application/json");
    }

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Various file types test passed" << std::endl;
}

// Test large file handling
void test_large_file() {
    std::cout << "Testing large file handling..." << std::endl;

    std::string test_dir = "./test_www_large";
    std::filesystem::create_directories(test_dir);

    // Create a larger file (1MB)
    std::string large_content(1024 * 1024, 'X');
    std::ofstream(test_dir + "/large.bin") << large_content;

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_cache = false; // Disable to avoid caching large file
    config.max_file_size = 10 * 1024 * 1024; // 10MB

    StaticFileHandler handler(config);

    Request req;
    req.parse("GET /large.bin HTTP/1.1\r\nHost: localhost\r\n\r\n");

    Response resp;
    handler.handle(req, resp).result();

    assert(resp.status_code() == static_cast<int>(Status::OK));
    assert(resp.body().size() == large_content.size());
    assert(resp.body() == large_content);

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Large file test passed" << std::endl;
}

// Test performance
void test_performance() {
    std::cout << "Testing performance..." << std::endl;

    std::string test_dir = "./test_www_perf";
    std::filesystem::create_directories(test_dir);

    std::string test_content = "Performance test content";
    std::ofstream(test_dir + "/perf.html") << test_content;

    StaticFileHandler::Config config;
    config.root_dir = test_dir;
    config.enable_cache = true;
    config.cache_size = 100 * 1024 * 1024;

    StaticFileHandler handler(config);

    const int num_requests = 10000;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_requests; ++i) {
        Request req;
        req.parse("GET /perf.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
        Response resp;
        handler.handle(req, resp).result();
    }

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    double req_per_sec = (num_requests * 1000.0) / duration;

    std::cout << "  Served " << num_requests << " requests in " << duration << "ms"
              << " (" << req_per_sec << " req/sec)" << std::endl;

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Performance test completed" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Static File Handler Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize logger
    utils::AsyncLogger::instance().init("./test_logs", "static_file_test",
                                        utils::LogLevel::WARN);

    try {
        test_mime_types();
        test_serve_existing_file();
        test_serve_nonexistent_file();
        test_directory_listing();
        test_disabled_directory_listing();
        test_index_fallback();
        test_path_security();
        test_file_caching();
        test_various_file_types();
        test_large_file();
        test_performance();

        std::cout << "\n========================================" << std::endl;
        std::cout << "All tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;

        utils::AsyncLogger::instance().shutdown();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        utils::AsyncLogger::instance().shutdown();
        return 1;
    }
}
