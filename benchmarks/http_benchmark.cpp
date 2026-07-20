/**
 * HTTP Benchmark Tool
 * High-performance HTTP load testing using threads
 */

#include <iostream>
#include <chrono>
#include <atomic>
#include <vector>
#include <thread>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std::chrono;

// Benchmark configuration
struct Config {
    std::string host = "127.0.0.1";
    int port = 8080;
    std::string path = "/api/hello";
    int concurrency = 100;        // Concurrent connections
    int total_requests = 10000;   // Total requests to send
    int duration_seconds = 10;    // Test duration (0 = unlimited)
};

// Benchmark results
struct Results {
    std::atomic<uint64_t> requests_sent{0};
    std::atomic<uint64_t> responses_received{0};
    std::atomic<uint64_t> successful_responses{0};
    std::atomic<uint64_t> failed_responses{0};
    std::atomic<uint64_t> bytes_received{0};

    steady_clock::time_point start_time;
    steady_clock::time_point end_time;

    double duration_seconds() const {
        return duration_cast<duration<double>>(end_time - start_time).count();
    }

    double requests_per_second() const {
        return successful_responses.load() / duration_seconds();
    }

    double avg_latency_ms() const {
        return (duration_seconds() * 1000.0) / successful_responses.load();
    }
};

void print_results(const Results& results, const Config& config) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "HTTP Benchmark Results" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Target:   " << config.host << ":" << config.port << config.path << std::endl;
    std::cout << "Clients:  " << config.concurrency << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Duration:        " << results.duration_seconds() << " seconds" << std::endl;
    std::cout << "Requests sent:   " << results.requests_sent.load() << std::endl;
    std::cout << "Responses:       " << results.responses_received.load() << std::endl;
    std::cout << "Successful:      " << results.successful_responses.load() << std::endl;
    std::cout << "Failed:          " << results.failed_responses.load() << std::endl;
    std::cout << "Bytes received:  " << results.bytes_received.load() << std::endl;
    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Requests/sec:    " << results.requests_per_second() << std::endl;
    std::cout << "Avg latency:     " << results.avg_latency_ms() << " ms" << std::endl;
    std::cout << "========================================" << std::endl;
}

int main(int argc, char* argv[]) {
    Config config;

    // Parse arguments
    if (argc > 1) config.host = argv[1];
    if (argc > 2) config.port = std::stoi(argv[2]);
    if (argc > 3) config.path = argv[3];
    if (argc > 4) config.concurrency = std::stoi(argv[4]);
    if (argc > 5) config.total_requests = std::stoi(argv[5]);
    if (argc > 6) config.duration_seconds = std::stoi(argv[6]);

    std::cout << "========================================" << std::endl;
    std::cout << "HTTP Benchmark Tool" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Target:   " << config.host << ":" << config.port << config.path << std::endl;
    std::cout << "Clients:  " << config.concurrency << std::endl;
    std::cout << "Total:    " << config.total_requests << " requests" << std::endl;
    std::cout << "Duration: " << (config.duration_seconds > 0 ? std::to_string(config.duration_seconds) + "s" : "unlimited") << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    Results results;
    std::atomic<bool> running{true};

    results.start_time = steady_clock::now();

    // Create worker threads for concurrent clients
    std::vector<std::thread> threads;

    for (int i = 0; i < config.concurrency; ++i) {
        threads.emplace_back([&config, &results, &running]() {
            while (running.load()) {
                if (config.total_requests > 0 &&
                    results.requests_sent.load() >= static_cast<uint64_t>(config.total_requests)) {
                    break;
                }

                // Create socket
                int sock = socket(AF_INET, SOCK_STREAM, 0);
                if (sock < 0) {
                    results.failed_responses++;
                    continue;
                }

                // Set socket options for faster reconnect
                int opt = 1;
                setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

                // Connect
                sockaddr_in addr{};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(config.port);
                inet_pton(AF_INET, config.host.c_str(), &addr.sin_addr);

                if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
                    std::string request = "GET " + config.path + " HTTP/1.1\r\n"
                                          "Host: " + config.host + "\r\n"
                                          "Connection: close\r\n"
                                          "\r\n";

                    ssize_t sent = send(sock, request.c_str(), request.length(), 0);
                    if (sent > 0) {
                        results.requests_sent++;

                        char buffer[4096];
                        ssize_t received = recv(sock, buffer, sizeof(buffer) - 1, 0);
                        if (received > 0) {
                            results.responses_received++;
                            results.bytes_received += received;
                            if (std::strncmp(buffer, "HTTP/1.1 200", 12) == 0 ||
                                std::strncmp(buffer, "HTTP/1.0 200", 12) == 0) {
                                results.successful_responses++;
                            } else {
                                results.failed_responses++;
                            }
                        } else {
                            results.failed_responses++;
                        }
                    } else {
                        results.failed_responses++;
                    }
                } else {
                    results.failed_responses++;
                }

                close(sock);
            }
        });
    }

    // Progress reporter in main thread
    int last_count = 0;
    auto start = steady_clock::now();

    while (running.load()) {
        std::this_thread::sleep_for(1s);

        int current = results.successful_responses.load();
        int delta = current - last_count;
        last_count = current;

        auto elapsed = duration_cast<seconds>(steady_clock::now() - start).count();

        std::cout << "\r[" << elapsed << "s] "
                  << "Success: " << current
                  << " | Rate: " << delta << " req/sec"
                  << std::flush;

        if (config.duration_seconds > 0 && elapsed >= config.duration_seconds) {
            break;
        }

        if (config.total_requests > 0 && current >= config.total_requests) {
            break;
        }
    }

    running = false;

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    results.end_time = steady_clock::now();

    print_results(results, config);

    return 0;
}
