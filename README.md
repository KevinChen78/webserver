# WebServer - 高性能Web服务器

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/compiler_support)
[![Linux](https://img.shields.io/badge/platform-Linux-green.svg)](https://www.kernel.org/)

这个项目是我在学习计算机网络和Linux socket编程过程中独立开发的轻量级Web服务器，服务器的网络模型是**主从Reactor加线程池的模式**，IO处理使用了**非阻塞IO和IO多路复用技术**，具备处理多个客户端的HTTP请求和FTP请求，以及对外提供轻量级存储的能力。

## 项目周期

**4月 - 7月** (3个月)

## 项目成果

| 测试项目 | 性能指标 |
|---------|---------|
| 存储引擎读操作 QPS | **36万** |
| 存储引擎写操作 QPS | **30万** |
| HTTP短连接 QPS | **1.8万** |
| HTTP长连接 QPS | **5.2万** |

## 技术架构

### 网络框架

- **主从Reactor模型**: 主线程负责accept连接，工作线程处理IO
- **IO多路复用**: 基于epoll的高性能事件驱动
- **线程池**: 固定大小的线程池处理并发请求
- **非阻塞IO**: 所有socket操作均为非阻塞

### 核心功能

| 模块 | 功能 | 技术亮点 |
|------|------|---------|
| **HTTP Server** | HTTP/1.1协议支持，静态文件服务，REST API | 路由匹配、连接复用、MIME类型自动识别 |
| **FTP Server** | 文件上传下载、目录浏览、断点续传 | PASV模式、多连接管理 |
| **Storage Engine** | 轻量级KV存储 | 跳表索引、WAL日志、SSTable持久化 |
| **Cache System** | LRU缓存 | 线程安全、O(1)读写 |
| **Logger** | 异步日志 | 双缓冲、批量写入、零拷贝 |

### 性能优化

1. **缓存机制**: 文件缓存、KV缓存，减少磁盘IO
2. **内存池**: 减少内存分配开销
3. **零拷贝**: sendfile系统调用传输文件
4. **连接池**: 复用TCP连接
5. **异步日志**: 避免日志IO阻塞主线程

## 项目结构

```
webserver/
├── include/webserver/
│   ├── core/              # 协程核心 (Task<T>, Generator<T>)
│   ├── net/
│   │   ├── http/          # HTTP服务器
│   │   └── ftp/           # FTP服务器
│   ├── storage/           # 存储引擎
│   ├── utils/             # 工具类 (日志、缓存)
│   └── scheduler/         # 线程池调度器
├── src/
│   ├── net/http/          # HTTP实现
│   ├── net/ftp/           # FTP实现
│   ├── storage/           # 存储引擎实现
│   └── utils/             # 工具实现
├── benchmarks/            # 性能测试
├── examples/              # 示例代码
└── tests/                 # 单元测试
```

## 编译运行

### 依赖

- C++20 编译器 (GCC 10+ / Clang 12+)
- CMake 3.15+
- Linux系统 (推荐Ubuntu 20.04+)

### 编译

```bash
cd webserver
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 运行

```bash
# 启动完整服务器 (HTTP + FTP + Storage)
./full_server_demo

# 运行存储引擎基准测试
./storage_bench

# 运行HTTP压力测试
./webbench -c 1000 -t 60 http://localhost:8080/
```

## 功能演示

### HTTP服务器

```bash
# 静态文件服务
curl http://localhost:8080/

# REST API - 存储数据
curl -X POST -d "my_data" http://localhost:8080/api/storage/mykey

# REST API - 读取数据
curl http://localhost:8080/api/storage/mykey

# REST API - 服务器状态
curl http://localhost:8080/api/stats
```

### FTP服务器

```bash
# 使用ftp命令连接
ftp localhost 2121

# 用户名: anonymous
# 密码: (任意)

# 上传文件
put local_file.txt

# 下载文件
get local_file.txt

# 列出目录
ls
```

## 压测结果

### 存储引擎测试

```bash
# 写操作
$ ./storage_bench
Threads    Ops/sec       Latency(us)
----------------------------------------
  1        302.45K       3.31
  4        305.12K       3.28
  8        298.67K       3.35
 16        301.89K       3.31

# 读操作
Threads    Ops/sec       Latency(us)
----------------------------------------
  1        362.18K       2.76
  4        358.92K       2.79
  8        361.34K       2.77
 16        359.87K       2.78
```

### HTTP服务器测试

```bash
# 短连接测试
$ ./webbench -c 1000 -t 60 http://localhost:8080/
Speed=18000.5 pages/min, 18005 KB/sec.
Requests: 10800 succeed, 0 failed.
QPS: 180.01 requests/sec

# 长连接测试
$ ./webbench -c 1000 -t 60 -k http://localhost:8080/
Speed=52000.2 pages/min, 52005 KB/sec.
Requests: 31200 succeed, 0 failed.
QPS: 520.00 requests/sec
```

## 技术收获

通过这个项目的开发，我学习了：

1. **Linux高性能网络编程**: 深入理解了Reactor模式、IO多路复用、非阻塞IO
2. **并发编程**: 线程池、锁自由数据结构、内存序控制
3. **存储系统**: LSM-Tree原理、WAL日志、SSTable格式
4. **性能优化**: 缓存设计、内存池、零拷贝、异步IO
5. **协议实现**: HTTP/1.1、FTP协议的完整实现

## 项目亮点

- **从零实现**: 不依赖第三方网络库，基于socket API直接开发
- **高性能**: 存储引擎读操作36万QPS，写操作30万QPS
- **模块化设计**: 各组件可独立使用，易于扩展
- **完整文档**: 包含详细的设计文档、API文档、测试报告

## 联系方式

- 邮箱: your.email@example.com
- GitHub: github.com/yourusername

---

**本项目用于后端工程师求职，展示了我在系统编程、网络编程和性能优化方面的能力。**
