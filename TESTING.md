# WebServer 测试指南

## 快速开始

### 1. 编译项目

```bash
cd webserver
./build.sh
```

或手动编译：
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 2. 运行测试

#### 快速测试（推荐）
```bash
./quick_test.sh
```
运行3个核心测试，约30秒完成。

#### 完整测试
```bash
./run_tests.sh
```
运行所有5个测试模块。

#### 单独测试
```bash
cd build

./test_logger                    # 测试日志系统
./test_storage_engine            # 测试存储引擎
./test_lru_cache                 # 测试LRU缓存
./test_ftp_server                # 测试FTP服务器
./test_static_file_handler       # 测试静态文件服务
```

### 3. 运行演示

#### 日志演示
```bash
cd build
./logger_demo
# 查看输出：ls logs/
```

#### 存储引擎演示
```bash
cd build
./storage_demo
```

#### 完整服务器演示
```bash
cd build
./full_server_demo

# 另开终端测试HTTP:
curl http://localhost:8080/api/health
curl http://localhost:8080/api/stats

# 测试FTP:
ftp localhost 2121
# 用户名: anonymous
# 密码: (任意)
```

### 4. 压力测试

```bash
cd build

# 存储引擎压测
./storage_bench

# HTTP压测
./webbench -c 1000 -t 60 http://localhost:8080/
```

## 测试说明

### 预期输出

#### test_logger
```
========================================
Logger Unit Tests
========================================
  ✓ Singleton test passed
  ✓ Basic logging test passed
  ✓ Log level filtering test passed
  ✓ Concurrent logging test passed
  ✓ File rotation test passed

========================================
All tests passed!
========================================
```

#### test_storage_engine
```
========================================
Storage Engine Unit Tests
========================================
  ✓ Basic operations test passed
  ✓ Delete operations test passed
  ✓ Bulk operations test passed
  ✓ Concurrent access test passed
  ✓ WAL recovery test passed
  ✓ Large values test passed
  ✓ Stats test passed

=== Performance ===
Write: 302.45K ops/sec (3.31 us/op)
Read:  362.18K ops/sec (2.76 us/op)
```

#### test_lru_cache
```
========================================
LRU Cache Unit Tests
========================================
  ✓ Basic operations test passed
  ✓ LRU eviction test passed
  ...
  ✓ Performance test completed

========================================
All tests passed!
========================================
```

## 常见问题

### Q: 编译错误 "undefined reference to pthread"
**A**: 确保CMakeLists.txt中链接了pthread库。

### Q: 测试失败 "Cannot create directory"
**A**: 确保有写权限，或手动创建logs/和data/目录。

### Q: FTP测试失败
**A**: 确保端口未被占用，或修改测试中的端口号。

## 性能基准

| 测试项 | 预期值 | 说明 |
|-------|-------|-----|
| 存储写QPS | >300K | 单线程写入 |
| 存储读QPS | >360K | 单线程读取 |
| 缓存操作 | >1M | 单线程读写 |
| 日志写入 | >100K | 多线程并发 |

## 下一步

1. 运行测试确保功能正常
2. 修改代码尝试不同参数
3. 查看测试源码学习用法
4. 集成到自己的项目中
