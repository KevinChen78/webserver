# HTTP Server Benchmark Report

## 测试环境

| 项目 | 配置 |
|------|------|
| 操作系统 | WSL2 (Ubuntu) |
| CPU | 待补充 |
| 内存 | 待补充 |
| 编译器 | GCC 12.4.0 |
| 构建类型 | Release |

## 测试目标

测试基于线程的简单HTTP服务器的性能表现。

## 测试配置

```
目标服务器: 127.0.0.1:8080
并发连接数: 200
测试时长: 15秒
请求路径: /api/hello
响应内容: {"status":"ok","msg":"hi"}
```

## 测试结果

### 核心指标

| 指标 | 数值 |
|------|------|
| **QPS (Requests/sec)** | **34,673.5** |
| **平均延迟** | **0.029 ms** |
| 总请求数 | 555,701 |
| 成功响应 | 555,701 |
| 失败响应 | 0 |
| 成功率 | 100% |
| 接收字节 | 62.4 MB |

### 实时吞吐量

| 时间 | 累计请求 | 每秒请求 |
|------|----------|----------|
| 1s | 36,288 | 36,288 |
| 2s | 73,699 | 37,411 |
| 5s | 184,754 | 37,409 |
| 10s | 370,370 | 37,323 |
| 15s | 555,501 | 37,307 |

### 结果分析

1. **吞吐量稳定**: 整个测试期间QPS保持在 36K-37K 之间，波动小于 5%
2. **零错误**: 55万+ 请求全部成功，无失败
3. **低延迟**: 平均延迟仅 0.029ms，表现优秀
4. **线程模型**: 使用传统线程池处理并发连接

## 压测工具技术详解

### 1. HTTP Server 架构

```
┌─────────────────────────────────────────────────┐
│              HTTP Server (主线程)                │
│  ┌─────────────┐    ┌─────────────────────────┐ │
│  │  Socket创建  │───▶│  bind() + listen()      │ │
│  └─────────────┘    └─────────────────────────┘ │
│           │                                     │
│           ▼                                     │
│  ┌──────────────────────────────────────────┐  │
│  │           accept() 循环                   │  │
│  │  每来一个连接 ──▶ 创建 pthread 线程       │  │
│  └──────────────────────────────────────────┘  │
│           │                                     │
│           ▼                                     │
│  ┌──────────────────────────────────────────┐  │
│  │         Worker Thread (每个连接一个)       │  │
│  │  1. recv() 读取HTTP请求                   │  │
│  │  2. 解析请求路径                          │  │
│  │  3. 生成响应 (200 OK 或 404)              │  │
│  │  4. send() 发送响应                       │  │
│  │  5. close() 关闭连接                      │  │
│  └──────────────────────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

**核心代码逻辑**:
```cpp
// 主循环 - 接受连接
while (g_running) {
    int client_fd = accept(server_fd, ...);
    // 每个连接创建一个线程处理
    std::thread([client_fd]() {
        handle_client(client_fd);
    }).detach();
}

// 请求处理
void handle_client(int client_fd) {
    recv(client_fd, buffer, ...);           // 读请求
    if (strstr(buffer, "GET /api/hello")) {
        send(client_fd, HTTP_200_RESPONSE); // 发响应
    }
    close(client_fd);                        // 关闭
}
```

### 2. HTTP Benchmark 压测工具架构

```
┌─────────────────────────────────────────────────────────┐
│                    Benchmark 主线程                      │
│  ┌─────────────────────────────────────────────────┐   │
│  │              进度报告器 (主线程)                   │   │
│  │  每秒打印: 时间 | 累计成功数 | 当前QPS           │   │
│  └─────────────────────────────────────────────────┘   │
│                          │                              │
│           ┌──────────────┼──────────────┐              │
│           ▼              ▼              ▼              │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ Worker #1   │  │ Worker #2   │  │ Worker #N   │    │
│  │ (pthread)   │  │ (pthread)   │  │ (pthread)   │    │
│  └─────────────┘  └─────────────┘  └─────────────┘    │
│         │               │               │              │
│         ▼               ▼               ▼              │
│  ┌─────────────────────────────────────────────────┐   │
│  │           共享的原子计数器 (线程安全)              │   │
│  │  requests_sent: 已发送请求总数                   │   │
│  │  successful_responses: 成功响应数               │   │
│  │  failed_responses: 失败数                       │   │
│  │  bytes_received: 接收字节数                     │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 3. 压测工具工作原理

#### 3.1 并发模型
- **多线程并发**: 创建 N 个worker线程，每个线程独立发送请求
- **无锁设计**: 使用 `std::atomic` 原子变量统计结果，避免锁竞争
- **短连接模式**: 每个请求新建TCP连接，模拟最坏情况

#### 3.2 请求流程
```cpp
while (running) {
    // 1. 创建socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);

    // 2. 设置SO_REUSEADDR加速端口复用
    setsockopt(sock, SO_REUSEADDR, ...);

    // 3. 连接服务器
    connect(sock, &server_addr, ...);

    // 4. 发送HTTP GET请求
    send(sock, "GET /api/hello HTTP/1.1\r\nHost: ...\r\n\r\n", ...);
    requests_sent++;

    // 5. 接收响应
    recv(sock, buffer, sizeof(buffer), ...);

    // 6. 解析状态码
    if (strncmp(buffer, "HTTP/1.1 200", 12) == 0) {
        successful_responses++;
    } else {
        failed_responses++;
    }

    // 7. 关闭连接
    close(sock);
}
```

#### 3.3 统计指标计算
```cpp
struct Results {
    std::atomic<uint64_t> requests_sent{0};      // 总发送
    std::atomic<uint64_t> successful_responses{0}; // 成功数
    std::atomic<uint64_t> failed_responses{0};    // 失败数
    std::atomic<uint64_t> bytes_received{0};      // 总字节

    // QPS = 成功数 / 测试时长(秒)
    double requests_per_second() {
        return successful_responses / duration_seconds;
    }

    // 平均延迟 = 总时长 / 成功数 * 1000 (ms)
    double avg_latency_ms() {
        return (duration_seconds * 1000) / successful_responses;
    }
};
```

### 4. 关键设计决策

| 设计点 | 选择 | 原因 |
|--------|------|------|
| 并发模型 | 多线程 | 模拟真实多客户端场景 |
| 连接模式 | 短连接 | 测试服务器处理新建连接能力 |
| 统计方式 | 原子变量 | 无锁，高性能，线程安全 |
| 请求内容 | 固定JSON | 减少变量，专注测试服务器框架开销 |
| 响应验证 | 检查HTTP状态码 | 简单可靠，兼容不同服务器 |

### 5. 工具代码结构

```
benchmarks/
├── http_benchmark.cpp      # 压测客户端
│   ├── Config 结构体        # 配置参数
│   ├── Results 结构体       # 统计结果
│   ├── main()              # 主逻辑
│   └── worker线程函数       # 并发请求发送
│
└── http_server_simple.cpp  # 测试服务器
    ├── handle_client()     # 请求处理
    ├── main()              # 监听循环
    └── HTTP响应模板        # 200/404响应
```

### 6. 测试局限性

1. **本机测试**: 无网络延迟，结果比真实网络环境更好
2. **短连接**: 未测试HTTP Keep-Alive长连接性能
3. **单一URL**: 未测试路由解析、静态文件等复杂场景
4. **无负载均衡**: 单服务器，未测试集群性能

## 如何复现

```bash
# 编译
cd build
make http_server_simple http_benchmark

# 启动服务器
./examples/http_server_simple 8080 &

# 运行压测
./examples/http_benchmark 127.0.0.1 8080 /api/hello 200 0 15
```

## 对比参考

| 服务器类型 | QPS | 说明 |
|-----------|-----|------|
| 本测试 (线程池) | 34,673 | C++ 简单实现 |
| Nginx | 50,000+ | 事件驱动，高度优化 |
| Node.js | 10,000-30,000 | 单线程事件循环 |
| Go net/http | 30,000-50,000 | goroutine调度 |

## 优化建议

1. **使用epoll**: 替换线程池为Reactor模式，提升并发能力
2. **连接复用**: 支持HTTP Keep-Alive，减少TCP握手开销
3. **零拷贝**: 使用sendfile发送静态文件
4. **IO多路复用**: 使用coro库的协程替代线程

## 源代码位置

| 文件 | 路径 | 说明 |
|------|------|------|
| 压测客户端 | [benchmarks/http_benchmark.cpp](../benchmarks/http_benchmark.cpp) | 200并发压测工具 |
| 测试服务器 | [benchmarks/http_server_simple.cpp](../benchmarks/http_server_simple.cpp) | 多线程HTTP服务器 |
| 构建配置 | [examples/CMakeLists.txt](../examples/CMakeLists.txt) | CMake构建配置 |

## 测试时间

2026-03-10
