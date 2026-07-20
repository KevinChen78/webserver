/**
 * Simple HTTP Server for Benchmarking
 */

#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <vector>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

// Simple HTTP response
const char* HTTP_200 = "HTTP/1.1 200 OK\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: 26\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "{\"status\":\"ok\",\"msg\":\"hi\"}";

const char* HTTP_404 = "HTTP/1.1 404 Not Found\r\n"
                       "Content-Type: text/plain\r\n"
                       "Content-Length: 9\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "Not Found";

void handle_client(int client_fd, int client_num) {
    char buffer[4096];
    ssize_t received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (received > 0) {
        buffer[received] = '\0';

        // Simple routing
        if (std::strstr(buffer, "GET /api/hello") != nullptr ||
            std::strstr(buffer, "GET /") != nullptr) {
            send(client_fd, HTTP_200, std::strlen(HTTP_200), 0);
        } else {
            send(client_fd, HTTP_404, std::strlen(HTTP_404), 0);
        }
    }

    close(client_fd);
}

int main(int argc, char* argv[]) {
    int port = (argc > 1) ? std::stoi(argv[1]) : 8080;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    // Allow reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind to port " << port << std::endl;
        close(server_fd);
        return 1;
    }

    // Listen
    if (listen(server_fd, 128) < 0) {
        std::cerr << "Failed to listen" << std::endl;
        close(server_fd);
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "Simple HTTP Server" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Listening on http://0.0.0.0:" << port << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  GET /api/hello - JSON response" << std::endl;
    std::cout << "  GET /          - Same as /api/hello" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;

    int client_count = 0;

    // Accept loop
    while (g_running) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Handle in thread
        std::thread([client_fd, client_num = ++client_count]() {
            handle_client(client_fd, client_num);
        }).detach();
    }

    close(server_fd);
    std::cout << "\nServer stopped" << std::endl;

    return 0;
}
