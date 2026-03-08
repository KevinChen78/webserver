#include "webserver/net/ftp/ftp_server.hpp"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace webserver {
namespace net {
namespace ftp {

// Socket helpers
namespace {
    void close_socket(int sock) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
    }

    std::string trim(const std::string& str) {
        size_t start = str.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = str.find_last_not_of(" \t\r\n");
        return str.substr(start, end - start + 1);
    }
}

FtpServer::FtpServer(const Config& config) : config_(config) {
    std::filesystem::create_directories(config_.root_dir);
}

FtpServer::~FtpServer() {
    stop();
}

bool FtpServer::start() {
    if (running_.exchange(true)) {
        return true;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("Failed to initialize Winsock");
        return false;
    }
#endif

    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ < 0) {
        LOG_ERROR("Failed to create socket");
        return false;
    }

    int reuse = 1;
#ifdef _WIN32
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    inet_pton(AF_INET, config_.bind_address.c_str(), &addr.sin_addr);

    if (bind(listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind to port %d", config_.port);
        close_socket(listen_socket_);
        return false;
    }

    if (listen(listen_socket_, 10) < 0) {
        LOG_ERROR("Failed to listen on socket");
        close_socket(listen_socket_);
        return false;
    }

    accept_thread_ = std::thread(&FtpServer::accept_loop, this);

    LOG_INFO("FTP Server started on %s:%d, root=%s",
             config_.bind_address.c_str(), config_.port, config_.root_dir.c_str());
    return true;
}

void FtpServer::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    if (listen_socket_ >= 0) {
        close_socket(listen_socket_);
        listen_socket_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    LOG_INFO("FTP Server stopped");
}

void FtpServer::accept_loop() {
    while (running_) {
        sockaddr_in client_addr{};
        socklen_t addr_len = sizeof(client_addr);

        int client_socket = accept(listen_socket_,
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &addr_len);
        if (client_socket < 0) {
            if (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++total_connections_;
        }

        std::thread(&FtpServer::handle_client, this, client_socket).detach();
    }
}

void FtpServer::handle_client(int client_socket) {
    FtpSession session;
    session.control_socket = client_socket;
    session.root_dir = std::filesystem::absolute(config_.root_dir);
    session.current_dir = "/";

    send_reply(session, 220, config_.welcome_message);

    char buffer[4096];
    while (session.running && running_) {
        int received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (received <= 0) {
            break;
        }

        buffer[received] = '\0';
        std::string line(buffer);
        line = trim(line);

        if (line.empty()) continue;

        LOG_DEBUG("FTP command: %s", line.c_str());

        auto [cmd, arg] = parse_command(line);

        switch (cmd) {
            case FtpCommand::USER:
                cmd_user(session, arg);
                break;
            case FtpCommand::PASS:
                cmd_pass(session, arg);
                break;
            case FtpCommand::SYST:
                cmd_syst(session);
                break;
            case FtpCommand::FEAT:
                cmd_feat(session);
                break;
            case FtpCommand::PWD:
                cmd_pwd(session);
                break;
            case FtpCommand::CWD:
                cmd_cwd(session, arg);
                break;
            case FtpCommand::TYPE:
                cmd_type(session, arg);
                break;
            case FtpCommand::PASV:
                cmd_pasv(session);
                break;
            case FtpCommand::PORT:
                cmd_port(session, arg);
                break;
            case FtpCommand::LIST:
                cmd_list(session, arg);
                break;
            case FtpCommand::RETR:
                cmd_retr(session, arg);
                break;
            case FtpCommand::STOR:
                cmd_stor(session, arg);
                break;
            case FtpCommand::DELE:
                cmd_dele(session, arg);
                break;
            case FtpCommand::MKD:
                cmd_mkd(session, arg);
                break;
            case FtpCommand::RMD:
                cmd_rmd(session, arg);
                break;
            case FtpCommand::SIZE:
                cmd_size(session, arg);
                break;
            case FtpCommand::QUIT:
                cmd_quit(session);
                break;
            default:
                send_reply(session, 500, "Unknown command");
                break;
        }
    }

    close_data_connection(session);
    close_socket(session.control_socket);

    LOG_DEBUG("FTP client disconnected");
}

std::pair<FtpCommand, std::string> FtpServer::parse_command(const std::string& line) {
    size_t space_pos = line.find(' ');
    std::string cmd_str, arg;

    if (space_pos != std::string::npos) {
        cmd_str = line.substr(0, space_pos);
        arg = trim(line.substr(space_pos + 1));
    } else {
        cmd_str = line;
    }

    // Convert to uppercase
    for (auto& c : cmd_str) c = static_cast<char>(toupper(c));

    static const std::map<std::string, FtpCommand> cmd_map = {
        {"USER", FtpCommand::USER}, {"PASS", FtpCommand::PASS},
        {"SYST", FtpCommand::SYST}, {"FEAT", FtpCommand::FEAT},
        {"PWD", FtpCommand::PWD}, {"CWD", FtpCommand::CWD},
        {"TYPE", FtpCommand::TYPE}, {"PASV", FtpCommand::PASV},
        {"PORT", FtpCommand::PORT}, {"LIST", FtpCommand::LIST},
        {"RETR", FtpCommand::RETR}, {"STOR", FtpCommand::STOR},
        {"DELE", FtpCommand::DELE}, {"MKD", FtpCommand::MKD},
        {"RMD", FtpCommand::RMD}, {"SIZE", FtpCommand::SIZE},
        {"QUIT", FtpCommand::QUIT}
    };

    auto it = cmd_map.find(cmd_str);
    if (it != cmd_map.end()) {
        return {it->second, arg};
    }
    return {FtpCommand::UNKNOWN, arg};
}

void FtpServer::send_reply(FtpSession& session, int code, const std::string& message) {
    std::ostringstream oss;
    oss << code << " " << message << "\r\n";
    std::string reply = oss.str();
    send(session.control_socket, reply.c_str(), static_cast<int>(reply.size()), 0);
}

void FtpServer::cmd_user(FtpSession& session, const std::string& arg) {
    session.username = arg;
    if (config_.allow_anonymous && arg == "anonymous") {
        send_reply(session, 331, "Anonymous access allowed, send any password");
    } else {
        send_reply(session, 331, "Password required for " + arg);
    }
}

void FtpServer::cmd_pass(FtpSession& session, const std::string& arg) {
    if (config_.allow_anonymous && session.username == "anonymous") {
        session.authenticated = true;
        send_reply(session, 230, "Anonymous user logged in");
    } else {
        // Simple auth: any password is accepted
        session.authenticated = true;
        send_reply(session, 230, "User logged in successfully");
    }
}

void FtpServer::cmd_syst(FtpSession& session) {
    send_reply(session, 215, "UNIX Type: L8");
}

void FtpServer::cmd_feat(FtpSession& session) {
    send_reply(session, 211, "Features:\r\n PASV\r\n SIZE\r\n211 End");
}

void FtpServer::cmd_pwd(FtpSession& session) {
    send_reply(session, 257, "\"" + session.current_dir.string() + "\" is the current directory");
}

void FtpServer::cmd_cwd(FtpSession& session, const std::string& arg) {
    auto new_path = resolve_path(session, arg);
    if (!is_path_allowed(session, new_path)) {
        send_reply(session, 550, "Access denied");
        return;
    }

    if (std::filesystem::exists(new_path) && std::filesystem::is_directory(new_path)) {
        session.current_dir = std::filesystem::relative(new_path, session.root_dir);
        send_reply(session, 250, "Directory changed to " + session.current_dir.string());
    } else {
        send_reply(session, 550, "Failed to change directory");
    }
}

void FtpServer::cmd_type(FtpSession& session, const std::string& arg) {
    if (arg == "I" || arg == "A") {
        session.transfer_type = arg;
        send_reply(session, 200, "Type set to " + arg);
    } else {
        send_reply(session, 504, "Command not implemented for that parameter");
    }
}

void FtpServer::cmd_pasv(FtpSession& session) {
    session.passive_mode = true;

    if (session.data_listen_socket >= 0) {
        close_socket(session.data_listen_socket);
    }

    session.data_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (session.data_listen_socket < 0) {
        send_reply(session, 425, "Can't open data connection");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; // Let system choose port

    if (bind(session.data_listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close_socket(session.data_listen_socket);
        session.data_listen_socket = -1;
        send_reply(session, 425, "Can't open data connection");
        return;
    }

    if (listen(session.data_listen_socket, 1) < 0) {
        close_socket(session.data_listen_socket);
        session.data_listen_socket = -1;
        send_reply(session, 425, "Can't open data connection");
        return;
    }

    socklen_t addr_len = sizeof(addr);
    getsockname(session.data_listen_socket, reinterpret_cast<sockaddr*>(&addr), &addr_len);

    int port = ntohs(addr.sin_port);
    int p1 = port / 256;
    int p2 = port % 256;

    std::ostringstream oss;
    oss << "127,0,0,1," << p1 << "," << p2;
    send_reply(session, 227, "Entering Passive Mode (" + oss.str() + ")");
}

void FtpServer::cmd_port(FtpSession& session, const std::string& arg) {
    // Parse PORT command (h1,h2,h3,h4,p1,p2)
    // Simplified: not fully implemented
    send_reply(session, 502, "Command not implemented");
}

void FtpServer::cmd_list(FtpSession& session, const std::string& arg) {
    if (!setup_data_connection(session)) {
        send_reply(session, 425, "Can't open data connection");
        return;
    }

    send_reply(session, 150, "Opening data connection");

    auto path = resolve_path(session, arg.empty() ? "." : arg);
    std::ostringstream listing;

    try {
        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            std::string name = entry.path().filename().string();
            bool is_dir = entry.is_directory();

            auto perms = std::filesystem::status(entry).permissions();
            std::string mode = is_dir ? "drwxr-xr-x" : "-rw-r--r--";

            auto time = entry.last_write_time();
            auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(time);
            auto time_t = std::chrono::system_clock::to_time_t(sys_time);

            std::tm tm{};
#ifdef _WIN32
            localtime_s(&tm, &time_t);
#else
            localtime_r(&time_t, &tm);
#endif

            listing << mode << " 1 owner group ";
            if (is_dir) {
                listing << std::setw(10) << std::right << 4096;
            } else {
                listing << std::setw(10) << std::right << entry.file_size();
            }
            listing << " " << std::put_time(&tm, "%b %d %H:%M") << " " << name << "\r\n";
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to list directory: %s", e.what());
    }

    std::string data = listing.str();
    send(session.data_socket, data.c_str(), static_cast<int>(data.size()), 0);

    close_data_connection(session);
    send_reply(session, 226, "Transfer complete");
}

void FtpServer::cmd_retr(FtpSession& session, const std::string& arg) {
    auto path = resolve_path(session, arg);

    if (!is_path_allowed(session, path) || !std::filesystem::exists(path)) {
        send_reply(session, 550, "File not found or access denied");
        return;
    }

    if (!setup_data_connection(session)) {
        send_reply(session, 425, "Can't open data connection");
        return;
    }

    send_reply(session, 150, "Opening data connection");

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        close_data_connection(session);
        send_reply(session, 550, "Failed to open file");
        return;
    }

    char buffer[8192];
    uint64_t total_sent = 0;
    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        int to_send = static_cast<int>(file.gcount());
        if (to_send > 0) {
            send(session.data_socket, buffer, to_send, 0);
            total_sent += to_send;
        }
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++files_downloaded_;
        bytes_transferred_ += total_sent;
    }

    close_data_connection(session);
    send_reply(session, 226, "Transfer complete");

    LOG_INFO("File downloaded: %s (%zu bytes)", arg.c_str(), total_sent);
}

void FtpServer::cmd_stor(FtpSession& session, const std::string& arg) {
    auto path = resolve_path(session, arg);

    if (!is_path_allowed(session, path)) {
        send_reply(session, 550, "Access denied");
        return;
    }

    if (!setup_data_connection(session)) {
        send_reply(session, 425, "Can't open data connection");
        return;
    }

    send_reply(session, 150, "Opening data connection");

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        close_data_connection(session);
        send_reply(session, 553, "Failed to open file");
        return;
    }

    char buffer[8192];
    uint64_t total_received = 0;
    while (true) {
        int received = recv(session.data_socket, buffer, sizeof(buffer), 0);
        if (received <= 0) break;
        file.write(buffer, received);
        total_received += received;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        ++files_uploaded_;
        bytes_transferred_ += total_received;
    }

    close_data_connection(session);
    send_reply(session, 226, "Transfer complete");

    LOG_INFO("File uploaded: %s (%zu bytes)", arg.c_str(), total_received);
}

void FtpServer::cmd_dele(FtpSession& session, const std::string& arg) {
    auto path = resolve_path(session, arg);

    if (!is_path_allowed(session, path) || !std::filesystem::exists(path)) {
        send_reply(session, 550, "File not found or access denied");
        return;
    }

    try {
        std::filesystem::remove(path);
        send_reply(session, 250, "File deleted");
    } catch (...) {
        send_reply(session, 450, "Failed to delete file");
    }
}

void FtpServer::cmd_mkd(FtpSession& session, const std::string& arg) {
    auto path = resolve_path(session, arg);

    if (!is_path_allowed(session, path)) {
        send_reply(session, 550, "Access denied");
        return;
    }

    try {
        std::filesystem::create_directory(path);
        send_reply(session, 257, "\"" + arg + "\" directory created");
    } catch (...) {
        send_reply(session, 550, "Failed to create directory");
    }
}

void FtpServer::cmd_rmd(FtpSession& session, const std::string& arg) {
    auto path = resolve_path(session, arg);

    if (!is_path_allowed(session, path) || !std::filesystem::exists(path)) {
        send_reply(session, 550, "Directory not found or access denied");
        return;
    }

    try {
        std::filesystem::remove(path);
        send_reply(session, 250, "Directory removed");
    } catch (...) {
        send_reply(session, 450, "Failed to remove directory");
    }
}

void FtpServer::cmd_size(FtpSession& session, const std::string& arg) {
    auto path = resolve_path(session, arg);

    if (!is_path_allowed(session, path) || !std::filesystem::exists(path)) {
        send_reply(session, 550, "File not found");
        return;
    }

    auto size = std::filesystem::file_size(path);
    send_reply(session, 213, std::to_string(size));
}

void FtpServer::cmd_quit(FtpSession& session) {
    send_reply(session, 221, "Goodbye");
    session.running = false;
}

bool FtpServer::setup_data_connection(FtpSession& session) {
    if (session.passive_mode) {
        if (session.data_listen_socket < 0) {
            return false;
        }

        sockaddr_in addr{};
        socklen_t addr_len = sizeof(addr);
        session.data_socket = accept(session.data_listen_socket,
                                      reinterpret_cast<sockaddr*>(&addr),
                                      &addr_len);

        close_socket(session.data_listen_socket);
        session.data_listen_socket = -1;

        return session.data_socket >= 0;
    }
    // Active mode not implemented
    return false;
}

void FtpServer::close_data_connection(FtpSession& session) {
    if (session.data_socket >= 0) {
        close_socket(session.data_socket);
        session.data_socket = -1;
    }
    if (session.data_listen_socket >= 0) {
        close_socket(session.data_listen_socket);
        session.data_listen_socket = -1;
    }
}

std::filesystem::path FtpServer::resolve_path(const FtpSession& session, const std::string& path) {
    if (path.empty() || path == ".") {
        return session.root_dir / session.current_dir.relative_path();
    }

    if (path[0] == '/') {
        return session.root_dir / path.substr(1);
    }

    return session.root_dir / session.current_dir.relative_path() / path;
}

bool FtpServer::is_path_allowed(const FtpSession& session, const std::filesystem::path& path) {
    try {
        auto canonical_path = std::filesystem::weakly_canonical(path);
        auto canonical_root = std::filesystem::weakly_canonical(session.root_dir);

        auto root_str = canonical_root.string();
        auto path_str = canonical_path.string();

        return path_str.find(root_str) == 0;
    } catch (...) {
        return false;
    }
}

FtpServer::Stats FtpServer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return {
        0, // active_connections not tracked yet
        total_connections_,
        files_downloaded_,
        files_uploaded_,
        bytes_transferred_
    };
}

} // namespace ftp
} // namespace net
} // namespace webserver
