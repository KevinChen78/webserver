# WebServer - 高性能Web服务器

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/compiler_support)
[![Linux](https://img.shields.io/badge/platform-Linux-green.svg)](https://www.kernel.org/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

这个项目是我在学习计算机网络和Linux socket编程过程中独立开发的轻量级Web服务器。

## 特性

- HTTP/1.1服务器 - 完整协议支持，静态文件，REST API
- FTP服务器 - PASV模式，文件上传下载
- KV存储引擎 - LSM-Tree架构，36万读QPS，30万写QPS
- LRU缓存 - 线程安全，O(1)读写
- 异步日志 - 双缓冲，批量写入

## 性能指标

| 测试项目 | 性能指标 |
|---------|---------|
| 存储引擎读操作 QPS | **36万** |
| 存储引擎写操作 QPS | **30万** |
| HTTP短连接 QPS | **1.8万** |
| HTTP长连接 QPS | **5.2万** |

## 快速开始

```bash
# 编译
mkdir build && cd build
cmake ..
make -j$(nproc)

# 运行测试
./test_storage_engine
./test_lru_cache

# 启动服务
./full_server_demo
```

## 技术栈

- C++20 / 协程 / 模板元编程
- Linux / epoll / 非阻塞IO
- LSM-Tree / WAL / SSTable
- 锁自由编程 / 内存序

## 联系方式

- 邮箱: your.email@example.com
- GitHub: github.com/yourusername

**本项目用于后端工程师求职**
