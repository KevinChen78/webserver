/**
 * FTP Server Unit Tests
 * Tests for FTP server functionality
 */

#include "webserver/net/ftp/ftp_server.hpp"
#include "webserver/utils/logger.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace webserver::net::ftp;

// Simple FTP client for testing
class TestFtpClient {
public:
    TestFtpClient() : control_sock_(-1), data_sock_(-1) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~TestFtpClient() {
        disconnect();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool connect(const std::string& host, int port) {
        control_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (control_sock_ < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

        if (::connect(control_sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close_socket(control_sock_);
            control_sock_ = -1;
            return false;
        }

        // Read welcome message
        std::string response = read_response();
        return response.find("220") == 0;
    }

    bool login(const std::string& username, const std::string& password) {
        if (!send_command("USER " + username)) return false;
        std::string response = read_response();
        if (response.find("331") != 0 && response.find("230") != 0) return false;

        if (response.find("331") == 0) {
            if (!send_command("PASS " + password)) return false;
            response = read_response();
            return response.find("230") == 0;
        }

        return true;
    }

    bool enter_passive_mode(std::string& ip, int& port) {
        if (!send_command("PASV")) return false;
        std::string response = read_response();
        if (response.find("227") != 0) return false;

        // Parse PASV response: 227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)
        size_t start = response.find('(');
        size_t end = response.find(')');
        if (start == std::string::npos || end == std::string::npos) return false;

        std::string nums = response.substr(start + 1, end - start - 1);
        int h1, h2, h3, h4, p1, p2;
        if (sscanf(nums.c_str(), "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
            return false;
        }

        ip = std::to_string(h1) + "." + std::to_string(h2) + "." +
             std::to_string(h3) + "." + std::to_string(h4);
        port = p1 * 256 + p2;
        return true;
    }

    bool list_files(std::string& listing) {
        std::string ip;
        int port;
        if (!enter_passive_mode(ip, port)) return false;

        // Connect data channel
        data_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (data_sock_ < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (::connect(data_sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close_socket(data_sock_);
            data_sock_ = -1;
            return false;
        }

        if (!send_command("LIST")) return false;
        std::string response = read_response();
        if (response.find("150") != 0 && response.find("125") != 0) return false;

        // Read data
        char buffer[4096];
        listing.clear();
        while (true) {
            int n = recv(data_sock_, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) break;
            buffer[n] = '\0';
            listing += buffer;
        }

        close_socket(data_sock_);
        data_sock_ = -1;

        response = read_response();
        return response.find("226") == 0;
    }

    bool upload_file(const std::string& local_path, const std::string& remote_name) {
        std::ifstream file(local_path, std::ios::binary);
        if (!file.is_open()) return false;

        std::string ip;
        int port;
        if (!enter_passive_mode(ip, port)) return false;

        data_sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (data_sock_ < 0) return false;

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

        if (::connect(data_sock_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close_socket(data_sock_);
            data_sock_ = -1;
            return false;
        }

        if (!send_command("STOR " + remote_name)) return false;
        std::string response = read_response();
        if (response.find("150") != 0 && response.find("125") != 0) return false;

        // Send file data
        char buffer[4096];
        while (file.good()) {
            file.read(buffer, sizeof(buffer));
            int to_send = static_cast<int>(file.gcount());
            if (to_send > 0) {
                send(data_sock_, buffer, to_send, 0);
            }
        }

        close_socket(data_sock_);
        data_sock_ = -1;

        response = read_response();
        return response.find("226") == 0;
    }

    bool quit() {
        if (control_sock_ < 0) return false;
        send_command("QUIT");
        read_response();
        disconnect();
        return true;
    }

private:
    bool send_command(const std::string& cmd) {
        if (control_sock_ < 0) return false;
        std::string full_cmd = cmd + "\r\n";
        return send(control_sock_, full_cmd.c_str(), static_cast<int>(full_cmd.size()), 0) > 0;
    }

    std::string read_response() {
        if (control_sock_ < 0) return "";

        char buffer[1024];
        std::string response;

        while (true) {
            int n = recv(control_sock_, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) break;
            buffer[n] = '\0';
            response += buffer;
            if (response.find("\r\n") != std::string::npos) break;
        }

        // Extract first line
        size_t pos = response.find("\r\n");
        if (pos != std::string::npos) {
            return response.substr(0, pos);
        }
        return response;
    }

    void disconnect() {
        if (data_sock_ >= 0) {
            close_socket(data_sock_);
            data_sock_ = -1;
        }
        if (control_sock_ >= 0) {
            close_socket(control_sock_);
            control_sock_ = -1;
        }
    }

    void close_socket(int sock) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

    int control_sock_;
    int data_sock_;
};

// Test server start/stop
void test_server_start_stop() {
    std::cout << "Testing server start/stop..." << std::endl;

    std::string test_dir = "./test_ftp_root";
    std::filesystem::create_directories(test_dir);

    FtpServer::Config config;
    config.port = 2122; // Use different port to avoid conflicts
    config.root_dir = test_dir;

    FtpServer server(config);
    assert(!server.is_running() && "Server should not be running initially");

    assert(server.start() && "Server should start successfully");
    assert(server.is_running() && "Server should be running");

    server.stop();
    assert(!server.is_running() && "Server should be stopped");

    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Server start/stop test passed" << std::endl;
}

// Test FTP connection
void test_ftp_connection() {
    std::cout << "Testing FTP connection..." << std::endl;

    std::string test_dir = "./test_ftp_conn";
    std::filesystem::create_directories(test_dir);

    FtpServer::Config config;
    config.port = 2123;
    config.root_dir = test_dir;
    config.welcome_message = "Test FTP Server";

    FtpServer server(config);
    server.start();

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestFtpClient client;
    assert(client.connect("127.0.0.1", 2123) && "Should connect to server");
    assert(client.login("anonymous", "test@test.com") && "Should login successfully");

    client.quit();
    server.stop();
    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ FTP connection test passed" << std::endl;
}

// Test directory listing
void test_directory_listing() {
    std::cout << "Testing directory listing..." << std::endl;

    std::string test_dir = "./test_ftp_list";
    std::filesystem::create_directories(test_dir);

    // Create some test files
    std::ofstream(test_dir + "/file1.txt") << "content1";
    std::ofstream(test_dir + "/file2.txt") << "content2";
    std::filesystem::create_directory(test_dir + "/subdir");

    FtpServer::Config config;
    config.port = 2124;
    config.root_dir = test_dir;

    FtpServer server(config);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestFtpClient client;
    client.connect("127.0.0.1", 2124);
    client.login("anonymous", "test@test.com");

    std::string listing;
    assert(client.list_files(listing) && "Should list files");
    assert(listing.find("file1.txt") != std::string::npos && "Should contain file1.txt");
    assert(listing.find("file2.txt") != std::string::npos && "Should contain file2.txt");

    client.quit();
    server.stop();
    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Directory listing test passed" << std::endl;
}

// Test file upload
void test_file_upload() {
    std::cout << "Testing file upload..." << std::endl;

    std::string test_dir = "./test_ftp_upload";
    std::filesystem::create_directories(test_dir);

    std::string local_file = "./test_upload_file.txt";
    std::string test_content = "This is test content for FTP upload test.";
    std::ofstream(local_file) << test_content;

    FtpServer::Config config;
    config.port = 2125;
    config.root_dir = test_dir;

    FtpServer server(config);
    server.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestFtpClient client;
    client.connect("127.0.0.1", 2125);
    client.login("anonymous", "test@test.com");

    assert(client.upload_file(local_file, "uploaded.txt") && "Should upload file");

    // Verify file was uploaded
    std::ifstream uploaded(test_dir + "/uploaded.txt");
    assert(uploaded.is_open() && "Uploaded file should exist");
    std::string content((std::istreambuf_iterator<char>(uploaded)),
                        std::istreambuf_iterator<char>());
    assert(content == test_content && "Uploaded content should match");

    client.quit();
    server.stop();

    std::filesystem::remove(local_file);
    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ File upload test passed" << std::endl;
}

// Test server stats
void test_server_stats() {
    std::cout << "Testing server stats..." << std::endl;

    std::string test_dir = "./test_ftp_stats";
    std::filesystem::create_directories(test_dir);

    FtpServer::Config config;
    config.port = 2126;
    config.root_dir = test_dir;

    FtpServer server(config);
    server.start();

    auto stats1 = server.get_stats();
    assert(stats1.total_connections == 0 && "Initial connections should be 0");

    // Connect and disconnect
    {
        TestFtpClient client;
        client.connect("127.0.0.1", 2126);
        client.login("anonymous", "test@test.com");
        client.quit();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto stats2 = server.get_stats();
    assert(stats2.total_connections >= 1 && "Should have recorded connection");

    server.stop();
    std::filesystem::remove_all(test_dir);

    std::cout << "  ✓ Server stats test passed" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "FTP Server Unit Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    // Initialize logger
    webserver::utils::AsyncLogger::instance().init("./test_logs", "ftp_test",
                                                    webserver::utils::LogLevel::WARN);

    try {
        test_server_start_stop();
        test_ftp_connection();
        test_directory_listing();
        test_file_upload();
        test_server_stats();

        std::cout << "\n========================================" << std::endl;
        std::cout << "All tests passed!" << std::endl;
        std::cout << "========================================" << std::endl;

        webserver::utils::AsyncLogger::instance().shutdown();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        webserver::utils::AsyncLogger::instance().shutdown();
        return 1;
    }
}
