#pragma once

#include "webserver/core/task.hpp"
#include "webserver/utils/logger.hpp"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace webserver {
namespace net {
namespace ftp {

// FTP command codes
enum class FtpCommand {
    USER,    // Username
    PASS,    // Password
    SYST,    // System type
    FEAT,    // Features
    PWD,     // Print working directory
    CWD,     // Change working directory
    TYPE,    // Transfer type (I=binary, A=ascii)
    PASV,    // Passive mode
    PORT,    // Active mode
    LIST,    // List files
    RETR,    // Retrieve file
    STOR,    // Store file
    DELE,    // Delete file
    MKD,     // Make directory
    RMD,     // Remove directory
    SIZE,    // Get file size
    QUIT,    // Disconnect
    UNKNOWN
};

// FTP session state
struct FtpSession {
    int control_socket = -1;
    int data_socket = -1;
    int data_listen_socket = -1;

    std::string username;
    bool authenticated = false;
    std::filesystem::path current_dir;
    std::filesystem::path root_dir;

    std::string transfer_type = "I"; // I=binary, A=ascii
    bool passive_mode = true;
    std::string data_ip;
    int data_port = 0;

    bool running = true;
};

// Simple FTP Server
class FtpServer {
public:
    struct Config {
        std::string bind_address = "0.0.0.0";
        int port = 2121;              // FTP control port (2121 to avoid needing root)
        std::string root_dir = "./ftp_root";
        std::string welcome_message = "Welcome to WebServer FTP";
        bool allow_anonymous = false;
        size_t max_connections = 100;
    };

    explicit FtpServer(const Config& config = Config{});
    ~FtpServer();

    // Start FTP server
    bool start();

    // Stop FTP server
    void stop();

    // Check if running
    bool is_running() const { return running_.load(); }

    // Get stats
    struct Stats {
        size_t active_connections;
        size_t total_connections;
        size_t files_downloaded;
        size_t files_uploaded;
        uint64_t bytes_transferred;
    };
    Stats get_stats() const;

private:
    // Accept connections
    void accept_loop();

    // Handle client session
    void handle_client(int client_socket);

    // Parse FTP command
    std::pair<FtpCommand, std::string> parse_command(const std::string& line);

    // Command handlers
    void cmd_user(FtpSession& session, const std::string& arg);
    void cmd_pass(FtpSession& session, const std::string& arg);
    void cmd_syst(FtpSession& session);
    void cmd_feat(FtpSession& session);
    void cmd_pwd(FtpSession& session);
    void cmd_cwd(FtpSession& session, const std::string& arg);
    void cmd_type(FtpSession& session, const std::string& arg);
    void cmd_pasv(FtpSession& session);
    void cmd_port(FtpSession& session, const std::string& arg);
    void cmd_list(FtpSession& session, const std::string& arg);
    void cmd_retr(FtpSession& session, const std::string& arg);
    void cmd_stor(FtpSession& session, const std::string& arg);
    void cmd_dele(FtpSession& session, const std::string& arg);
    void cmd_mkd(FtpSession& session, const std::string& arg);
    void cmd_rmd(FtpSession& session, const std::string& arg);
    void cmd_size(FtpSession& session, const std::string& arg);
    void cmd_quit(FtpSession& session);

    // Helper functions
    void send_reply(FtpSession& session, int code, const std::string& message);
    bool setup_data_connection(FtpSession& session);
    void close_data_connection(FtpSession& session);
    std::filesystem::path resolve_path(const FtpSession& session, const std::string& path);
    bool is_path_allowed(const FtpSession& session, const std::filesystem::path& path);

private:
    Config config_;
    std::atomic<bool> running_{false};

    int listen_socket_ = -1;
    std::thread accept_thread_;

    // Stats
    mutable std::mutex stats_mutex_;
    size_t total_connections_ = 0;
    size_t files_downloaded_ = 0;
    size_t files_uploaded_ = 0;
    uint64_t bytes_transferred_ = 0;
};

} // namespace ftp
} // namespace net
} // namespace webserver
