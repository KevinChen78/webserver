/**
 * Webbench-like HTTP server benchmark tool
 * Simple HTTP benchmark for testing server performance
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace webbench {

struct Config {
    std::string host = "localhost";
    int port = 8080;
    std::string path = "/";
    int clients = 100;
    int duration = 30;  // seconds
    bool keep_alive = false;
    bool quiet = false;
};

struct Result {
    std::atomic<uint64_t> success{0};
    std::atomic<uint64_t> failed{0};
    std::atomic<uint64_t> bytes{0};
};

class HttpClient {
public:
    HttpClient(const Config& config) : config_(config) {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    }

    ~HttpClient() {
#ifdef _WIN32
        WSACleanup();
#endif
    }

    bool request() {
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock < 0) return false;

        // Set timeout
        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config_.port);
        inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr);

        if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return false;
        }

        // Build HTTP request
        std::ostringstream request;
        request << "GET " << config_.path << " HTTP/1.1\r\n"
                << "Host: " << config_.host << ":" << config_.port << "\r\n"
                << "User-Agent: Webbench/1.0\r\n";

        if (config_.keep_alive) {
            request << "Connection: keep-alive\r\n";
        } else {
            request << "Connection: close\r\n";
        }

        request << "\r\n";

        std::string req_str = request.str();
        if (send(sock, req_str.c_str(), (int)req_str.length(), 0) < 0) {
#ifdef _WIN32
            closesocket(sock);
#else
            close(sock);
#endif
            return false;
        }

        // Read response
        char buffer[4096];
        int total_read = 0;
        int header_end = -1;

        while (true) {
            int n = recv(sock, buffer + total_read, sizeof(buffer) - total_read - 1, 0);
            if (n <= 0) break;
            total_read += n;
            buffer[total_read] = '\0';

            // Check for end of headers
            if (header_end < 0) {
                char* end = strstr(buffer, "\r\n\r\n");
                if (end) {
                    header_end = (int)(end - buffer) + 4;
                }
            }

            if (total_read >= sizeof(buffer) - 1) break;
        }

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif

        // Parse status code
        if (total_read < 12) return false;
        if (strncmp(buffer, "HTTP/1.", 7) != 0) return false;

        int status_code = (buffer[9] - '0') * 100 + (buffer[10] - '0') * 10 + (buffer[11] - '0');
        return status_code == 200;
    }

private:
    Config config_;
};

void worker_thread(const Config& config, Result& result, std::atomic<bool>& running) {
    HttpClient client(config);

    while (running.load()) {
        if (client.request()) {
            result.success.fetch_add(1);
            result.bytes.fetch_add(1000); // Estimate
        } else {
            result.failed.fetch_add(1);
        }
    }
}

int run_benchmark(const Config& config) {
    if (!config.quiet) {
        std::cout << "Webbench - Simple Web Benchmark " << config.clients
                  << " clients, running " << config.duration << " sec.\n";
        std::cout << "\nBenchmarking: http://" << config.host << ":" << config.port
                  << config.path << "\n";
        std::cout << config.clients << " clients, running " << config.duration
                  << " sec.\n\n";
    }

    Result result;
    std::atomic<bool> running{true};

    auto start_time = std::chrono::steady_clock::now();

    // Start worker threads
    std::vector<std::thread> threads;
    for (int i = 0; i < config.clients; ++i) {
        threads.emplace_back(worker_thread, std::cref(config),
                             std::ref(result), std::ref(running));
    }

    // Wait for duration
    std::this_thread::sleep_for(std::chrono::seconds(config.duration));
    running = false;

    // Wait for threads
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time).count();

    // Calculate stats
    uint64_t total_success = result.success.load();
    uint64_t total_failed = result.failed.load();
    uint64_t total_bytes = result.bytes.load();

    double qps = duration > 0 ? static_cast<double>(total_success) / duration : 0;
    double speed_kb = duration > 0 ? static_cast<double>(total_bytes) / duration / 1024 : 0;

    if (!config.quiet) {
        std::cout << "Speed=" << std::fixed << std::setprecision(2)
                  << speed_kb * 60 << " pages/min, "
                  << speed_kb << " KB/sec.\n";
        std::cout << "Requests: " << total_success << " succeed, "
                  << total_failed << " failed.\n";
        std::cout << "QPS: " << std::setprecision(2) << qps
                  << " requests/sec\n";
    } else {
        std::cout << std::setprecision(2) << qps << "\n";
    }

    return total_failed == 0 ? 0 : 1;
}

void print_usage(const char* program) {
    std::cout << "Usage: " << program << " [options] URL\n"
              << "Options:\n"
              << "  -c, --clients NUM    Number of concurrent clients (default: 100)\n"
              << "  -t, --time SEC       Test duration in seconds (default: 30)\n"
              << "  -k, --keepalive      Use HTTP keepalive\n"
              << "  -q, --quiet          Quiet mode\n"
              << "  -h, --help           Show this help\n"
              << "\nExample:\n"
              << "  " << program << " -c 1000 -t 60 http://localhost:8080/\n";
}

Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            exit(0);
        } else if ((arg == "-c" || arg == "--clients") && i + 1 < argc) {
            config.clients = std::stoi(argv[++i]);
        } else if ((arg == "-t" || arg == "--time") && i + 1 < argc) {
            config.duration = std::stoi(argv[++i]);
        } else if (arg == "-k" || arg == "--keepalive") {
            config.keep_alive = true;
        } else if (arg == "-q" || arg == "--quiet") {
            config.quiet = true;
        } else if (arg[0] != '-') {
            // Parse URL
            std::string url = arg;
            if (url.find("http://") == 0) {
                url = url.substr(7);
            }

            size_t colon_pos = url.find(':');
            size_t slash_pos = url.find('/');

            if (colon_pos != std::string::npos) {
                config.host = url.substr(0, colon_pos);
                if (slash_pos != std::string::npos) {
                    config.port = std::stoi(url.substr(colon_pos + 1, slash_pos - colon_pos - 1));
                    config.path = url.substr(slash_pos);
                } else {
                    config.port = std::stoi(url.substr(colon_pos + 1));
                }
            } else {
                config.host = url.substr(0, slash_pos);
                if (slash_pos != std::string::npos) {
                    config.path = url.substr(slash_pos);
                }
            }
        }
    }

    return config;
}

} // namespace webbench

int main(int argc, char* argv[]) {
    if (argc < 2) {
        webbench::print_usage(argv[0]);
        return 1;
    }

    auto config = webbench::parse_args(argc, argv);
    return webbench::run_benchmark(config);
}
