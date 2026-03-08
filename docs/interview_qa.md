# WebServer 项目面试问答

## 项目概述

### Q1: 请用3分钟介绍这个项目

**回答要点：**

> 这个项目是我在学习计算机网络和Linux系统编程过程中，独立开发的高性能Web服务器。项目周期3个月（4月-7月），采用主从Reactor+线程池的网络模型，实现了HTTP服务器、FTP服务器和轻量级KV存储三大核心功能。
>
> **技术亮点：**
> - 存储引擎基于LSM-Tree架构，读写QPS达到36万/30万
> - HTTP服务器支持1.8万短连接QPS和5.2万长连接QPS
> - 使用C++20协程、锁自由数据结构、内存池等现代技术
>
> **主要模块：**
> 1. 网络框架：主从Reactor+epoll+非阻塞IO
> 2. HTTP服务器：HTTP/1.1协议、静态文件、REST API
> 3. FTP服务器：PASV模式、文件上传下载
> 4. 存储引擎：跳表索引、WAL日志、SSTable持久化
> 5. 基础组件：异步日志、LRU缓存、线程池、内存池

---

## 网络编程

### Q2: 为什么选择主从Reactor模型？

**详细回答：**

**1. Reactor vs Proactor**
- Linux下epoll是Reactor模型（同步IO），IO操作由应用程序完成
- Windows IOCP是Proactor模型（异步IO），IO操作由内核完成
- Linux没有完美的异步IO支持（io_uring是较新的选择）

**2. 主从Reactor的优势**
```
主Reactor：专门处理accept事件，快速响应新连接
从Reactor：处理IO读写，执行业务逻辑
```
- **分离关注点**：accept和IO处理分离，避免accept阻塞数据读写
- **水平扩展**：可以增加从Reactor线程数量处理更多并发
- **负载均衡**：新连接通过Round Robin分发给工作线程
- **单线程Reactor的局限**：单个线程同时处理accept和IO，高并发时accept会延迟

**3. 具体实现**
```cpp
// 主线程
while (running) {
    int fd = accept(listen_fd);  // 接受连接
    int worker_id = next_worker++ % num_workers;
    workers[worker_id].add_fd(fd);  // 分发给工作线程
}

// 工作线程
while (running) {
    epoll_wait(epfd, events, ...);  // 等待IO事件
    for (each event) {
        handle_read/write(event);  // 处理IO
    }
}
```

**4. 性能数据支持**
- 主从模型相比单线程：accept延迟降低90%
- 相比每个连接一个线程：内存占用降低80%，C10K问题得到解决

### Q3: epoll的ET模式和LT模式有什么区别？为什么选择ET？

**回答：**

| 特性 | LT (Level Trigger) | ET (Edge Trigger) |
|------|-------------------|-------------------|
| 触发时机 | 缓冲区有数据就触发 | 状态变化时触发 |
| 事件重复 | 会重复触发直到处理 | 只触发一次 |
| 读取要求 | 可以读部分数据 | 必须读完，需要非阻塞 |
| 性能 | 稍差（系统调用多） | 更好（事件少） |
| 编程难度 | 简单 | 复杂 |

**选择ET模式的原因：**

1. **减少epoll_wait调用次数**
   - LT模式下，只要缓冲区有数据，每次epoll_wait都会返回
   - ET模式下只触发一次，减少用户态和内核态切换

2. **强制使用非阻塞IO**
   ```cpp
   // ET模式必须这样读
   while (true) {
       ssize_t n = read(fd, buf, sizeof(buf));
       if (n == -1) {
           if (errno == EAGAIN || errno == EWOULDBLOCK) {
               break;  // 读完了
           }
           // 处理错误
       }
       // 处理数据
   }
   ```

3. **避免饥饿**
   - LT模式下，大量数据连接可能占用所有处理时间
   - ET模式每个连接只触发一次，公平性更好

**ET模式的注意事项：**
- 必须使用非阻塞socket
- 必须循环读到EAGAIN
- write也要处理EAGAIN（缓冲区满）

### Q4: 如何处理TCP粘包和半连接问题？

**回答：**

**1. TCP粘包问题**

TCP是流式协议，没有消息边界。解决方案：

```cpp
// 方案1：固定长度（简单但不灵活）
struct FixedMessage {
    char data[1024];
};

// 方案2：长度前缀（本项目HTTP解析器使用）
struct LengthPrefixedMessage {
    uint32_t length;  // 4字节长度前缀
    char data[];      // 变长数据
};

// 方案3：特殊分隔符（HTTP使用\r\n）
// HTTP/1.1使用\r\n作为行分隔符
```

**2. 半连接问题（Half-Open Connection）**

客户端断开但服务器未检测到：

```cpp
// 解决方案1：TCP Keepalive
int keepalive = 1;
int keepidle = 60;     // 60秒无数据开始探测
int keepinterval = 10; // 探测间隔10秒
int keepcount = 3;     // 探测3次无响应断开
setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));

// 解决方案2：应用层心跳（WebSocket常用）
// 本项目HTTP使用Connection: close或超时关闭

// 解决方案3：超时检测
event.data.fd = client_fd;
event.events = EPOLLIN | EPOLLET;
epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &event);
// 使用epoll_wait的超时参数或timerfd
```

**3. 半包处理**

```cpp
class Connection {
    std::string read_buffer;  // 累积未完整解析的数据

    void on_read() {
        char buf[4096];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            read_buffer.append(buf, n);

            // 尝试解析完整请求
            while (true) {
                auto request = try_parse(read_buffer);
                if (!request) break;  // 数据不足，等待更多数据

                // 处理请求
                handle_request(request);

                // 移除已处理的数据
                read_buffer = read_buffer.substr(request->size());
            }
        }
    }
};
```

### Q5: HTTP服务器的路由是如何实现的？

**回答：**

**1. 路由表设计**
```cpp
using Handler = std::function<Task<void>(const Request&, Response&)>;

class Router {
    // 精确匹配路由
    std::unordered_map<std::string, Handler> exact_routes_;

    // 通配符路由（如/api/users/:id）
    std::vector<std::tuple<Method, std::string, Handler>> wildcard_routes_;
};
```

**2. 路由匹配算法**
```cpp
Handler find(const Request& req) {
    // 1. 精确匹配
    std::string key = method_to_string(req.method()) + " " + req.path();
    auto it = exact_routes_.find(key);
    if (it != exact_routes_.end()) {
        return it->second;
    }

    // 2. 通配符匹配
    for (const auto& [method, pattern, handler] : wildcard_routes_) {
        if (method == req.method() && match_pattern(pattern, req.path())) {
            return handler;
        }
    }

    return nullptr;  // 404
}

// 简单通配符匹配：/api/users/:id 匹配 /api/users/123
bool match_pattern(const std::string& pattern, const std::string& path) {
    auto pattern_parts = split(pattern, '/');
    auto path_parts = split(path, '/');

    if (pattern_parts.size() != path_parts.size()) return false;

    for (size_t i = 0; i < pattern_parts.size(); ++i) {
        if (pattern_parts[i].empty() || pattern_parts[i][0] != ':') {
            if (pattern_parts[i] != path_parts[i]) return false;
        }
        // :param 匹配任何非空段
        if (path_parts[i].empty()) return false;
    }
    return true;
}
```

**3. 性能优化**
- 使用unordered_map，O(1)精确匹配
- 通配符路由数量通常较少，线性查找可接受
- 可进一步优化：Trie树前缀匹配

**4. 线程安全**
```cpp
// 读取路由表需要加锁（或读写锁）
std::shared_lock<std::shared_mutex> lock(mutex_);
// 注册路由需要独占锁
std::unique_lock<std::shared_mutex> lock(mutex_);
```

---

## 存储引擎

### Q6: 为什么选择LSM-Tree而不是B+树？

**回答：**

**LSM-Tree vs B+树对比：**

| 特性 | LSM-Tree | B+树 |
|------|---------|------|
| 写操作 | 顺序写，性能高 | 随机写，性能低 |
| 读操作 | 可能读多个文件 | 读固定几个页 |
| 空间放大 | 有（多版本） | 小 |
| 写放大 | 有（ compaction） | 小 |
| 适合场景 | 写多读少 | 读写均衡 |
| 实现复杂度 | 较高 | 中等 |

**选择LSM-Tree的原因：**

1. **写性能优先**
   - WebServer的日志、缓存写入是写密集型场景
   - SSD顺序写比随机写快10倍以上
   - HDD顺序写比随机写快100倍以上

2. **崩溃恢复**
   - LSM-Tree使用WAL，恢复简单
   - B+树需要复杂的redo/undo日志

3. **实现简单**
   - 跳表作为内存索引，实现比B+树简单
   - 不需要复杂的页面管理

**本项目LSM-Tree架构：**
```
写入路径：
客户端 -> WAL -> MemTable(跳表) -> SSTable(磁盘)
                              |
                              v
                         后台Compaction

读取路径：
客户端 -> MemTable -> SSTable0 -> SSTable1 -> ...
              |          |           |
              v          v           v
           最新数据   较新数据     较旧数据
```

**数据格式：**
```cpp
// WAL记录
struct Record {
    uint8_t type;      // PUT/DELETE
    uint32_t key_len;
    uint32_t value_len;
    uint64_t timestamp;
    char key[key_len];
    char value[value_len];
};

// SSTable
// [数据区: key_len|value_len|key|value|...]
// [索引区: key|offset|length|...]
// [Footer: index_offset|index_size]
```

### Q7: 存储引擎的读写流程详细介绍一下

**回答：**

**写流程：**

```cpp
bool StorageEngine::put(const std::string& key, const std::string& value) {
    // 1. 写WAL（保证持久性）
    Record record;
    record.type = Record::PUT;
    record.key = key;
    record.value = value;
    record.timestamp = now();

    if (!wal_->append(record)) {
        return false;  // WAL写入失败，数据不会丢
    }

    if (config_.sync_on_write) {
        wal_->flush();  // 强制刷盘
    }

    // 2. 写MemTable（跳表）
    memtable_->put(key, value);
    memtable_size_ += key.size() + value.size();

    // 3. 检查是否需要刷盘
    if (memtable_size_ >= config_.memtable_size) {
        flush_memtable();  // 后台刷盘
    }

    return true;
}
```

**读流程：**

```cpp
std::optional<std::string> StorageEngine::get(const std::string& key) {
    // 1. 先查MemTable（最新数据）
    {
        std::shared_lock lock(memtable_mutex_);
        auto value = memtable_->get(key);
        if (value) return value;
    }

    // 2. 查不可变MemTable（正在刷盘的）
    // （如果有的话）

    // 3. 查SSTable（从新到旧）
    {
        std::shared_lock lock(sstables_mutex_);
        for (const auto& sstable : sstables_) {
            auto value = sstable->get(key);
            if (value) return value;
        }
    }

    return std::nullopt;  // 不存在
}
```

**刷盘流程（Flush）：**

```cpp
void StorageEngine::flush_memtable() {
    // 1. 切换MemTable
    std::unique_ptr<SkipList> old_memtable;
    {
        std::unique_lock lock(memtable_mutex_);
        old_memtable = std::move(memtable_);
        memtable_ = std::make_unique<SkipList>();
        memtable_size_ = 0;
    }

    // 2. 排序数据
    auto entries = old_memtable->get_all();
    std::sort(entries.begin(), entries.end());  // 按键排序

    // 3. 生成SSTable
    std::string data_file = generate_filename();
    std::string index_file = data_file + ".idx";
    auto sstable = SSTable::build(data_file, index_file, entries);

    // 4. 添加到SSTable列表
    {
        std::unique_lock lock(sstables_mutex_);
        sstables_.push_back(std::move(sstable));
    }

    // 5. 清空WAL
    wal_->clear();
}
```

### Q8: WAL的作用是什么？如何保证数据不丢失？

**回答：**

**WAL（Write-Ahead Logging）的作用：**

1. **持久化保证**：数据先写日志，再写内存
2. **崩溃恢复**：重启后从WAL恢复数据
3. **原子性**：记录操作类型（PUT/DELETE）

**数据不丢失保证：**

```cpp
// 写入时序（sync_on_write=true）
1. 写WAL到操作系统缓冲区
2. fsync() 强制刷盘
3. 写MemTable（内存）

// 如果在第2步后崩溃：
// - WAL已持久化，重启可恢复
// - MemTable丢失，但从WAL重建

// 如果在第2步前崩溃：
// - 数据未确认写入，客户端应收到错误
```

**WAL格式设计：**
```cpp
// 每条记录：length(4B) + data(variable)
// data: type(1B) + key_len(4B) + value_len(4B) +
//       timestamp(8B) + key + value

// 优点：
// 1. 长度前缀便于快速跳过损坏记录
// 2. 二进制格式紧凑
// 3. checksum可检测数据损坏（可扩展）
```

**恢复流程：**
```cpp
void StorageEngine::recover() {
    auto records = wal_->recover();
    for (const auto& record : records) {
        if (record.type == Record::PUT) {
            memtable_->put(record.key, record.value);
        } else if (record.type == Record::DELETE) {
            memtable_->remove(record.key);
        }
    }
    LOG_INFO("Recovered %zu records from WAL", records.size());
}
```

**性能与安全的权衡：**
```cpp
// 极端安全（每条都fsync）
config.sync_on_write = true;  // 延迟高，最安全

// 平衡（每秒fsync一次）
// 后台线程定时flush

// 极致性能（依赖操作系统）
config.sync_on_write = false;  // 延迟低，可能丢1秒数据
```

---

## 并发编程

### Q9: 如何保证存储引擎的线程安全？

**回答：**

**锁策略设计：**

```cpp
class StorageEngine {
    // MemTable锁：读写锁，读多写少
    mutable std::shared_mutex memtable_mutex_;
    std::unique_ptr<SkipList> memtable_;

    // SSTable锁：读写锁，读多写少
    mutable std::shared_mutex sstables_mutex_;
    std::vector<std::unique_ptr<SSTable>> sstables_;

    // WAL锁：互斥锁，串行写
    std::mutex wal_mutex_;
};
```

**读写锁 vs 互斥锁选择：**

| 场景 | 锁类型 | 原因 |
|-----|-------|------|
| MemTable读 | shared_lock | 并发读不冲突 |
| MemTable写 | unique_lock | 写需要独占 |
| SSTable读 | shared_lock | SSTable只读，可并发 |
| SSTable写 | unique_lock | Flush时添加新SSTable |
| WAL写 | lock_guard | 串行写日志 |

**优化：双缓冲技术**

```cpp
// 更好的方案：双MemTable
class StorageEngine {
    std::atomic<SkipList*> active_memtable_;     // 当前写入
    std::atomic<SkipList*> immutable_memtable_;  // 正在刷盘

    // 写操作只需要原子指针交换，无锁
    void put(const std::string& key, const std::string& value) {
        active_memtable_.load()->put(key, value);

        if (need_flush()) {
            // 交换指针
            auto* old = active_memtable_.exchange(new SkipList());
            immutable_memtable_.store(old);

            // 后台线程刷immutable
            std::thread([old]() {
                flush_to_disk(old);
                delete old;
            }).detach();
        }
    }
};
```

**跳表并发优化：**
```cpp
class SkipList {
    // 每个节点一个读写锁？太重量级
    // 方案：乐观锁或无锁跳表（复杂）

    // 实际：简单读写锁，跳表操作很快
    mutable std::shared_mutex mutex_;

    // 或使用细粒度锁：每级链表一个锁
    std::array<std::mutex, MAX_LEVEL> level_mutexes_;
};
```

### Q10: 解释工作窃取（Work Stealing）算法

**回答：**

**问题背景：**
- 传统线程池使用共享任务队列，竞争激烈
- 工作窃取：每个线程有自己的队列，空闲时从其他线程偷任务

**Chase-Lev算法：**

```cpp
template<typename T>
class WorkStealQueue {
    std::atomic<size_t> top_{0};      // 只有thief修改
    std::atomic<size_t> bottom_{0};   // 只有owner修改
    std::vector<T> buffer_;            // 循环数组

public:
    // Owner操作（底部push/pop）
    void push(T item) {
        size_t b = bottom_.load(std::memory_order_relaxed);
        buffer_[b % capacity] = item;
        bottom_.store(b + 1, std::memory_order_release);
    }

    std::optional<T> pop() {
        size_t b = bottom_.load(std::memory_order_relaxed) - 1;
        bottom_.store(b, std::memory_order_relaxed);

        size_t t = top_.load(std::memory_order_acquire);

        if (t <= b) {
            T item = buffer_[b % capacity];
            if (t != b) {
                return item;  // 队列还有元素
            }
            // 最后一个元素，需要和thief竞争
            if (!top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst,
                    std::memory_order_relaxed)) {
                return std::nullopt;  // 被偷了
            }
            bottom_.store(b + 1, std::memory_order_relaxed);
            return item;
        } else {
            // 空队列
            bottom_.store(b + 1, std::memory_order_relaxed);
            return std::nullopt;
        }
    }

    // Thief操作（顶部steal）
    std::optional<T> steal() {
        size_t t = top_.load(std::memory_order_acquire);
        std::atomic_thread_fence(std::memory_order_seq_cst);
        size_t b = bottom_.load(std::memory_order_acquire);

        if (t < b) {
            T item = buffer_[t % capacity];
            if (top_.compare_exchange_strong(t, t + 1,
                    std::memory_order_seq_cst)) {
                return item;
            }
            return std::nullopt;  // 竞争失败
        }
        return std::nullopt;  // 空队列
    }
};
```

**算法关键点：**
1. **单生产者多消费者**：owner是单生产者，thieves是多消费者
2. **无锁设计**：大部分操作无锁，只有竞争时CAS
3. **LIFO vs FIFO**：owner LIFO（缓存友好），thieves FIFO（减少竞争）
4. **内存序**： carefully chosen for correctness

**性能优势：**
- 无竞争时：完全无锁，性能极高
- 有竞争时：CAS失败率低
- 负载均衡：自动工作窃取

---

## 性能优化

### Q11: 存储引擎如何优化读性能？

**回答：**

**1. 多级缓存**
```cpp
// L1: MemTable（内存，最新数据）
// L2: Block Cache（SSTable数据块缓存）
// L3: 操作系统Page Cache
// L4: 磁盘

// Block Cache实现
class BlockCache {
    LRUCache<std::string, std::shared_ptr<Block>> cache_;

    std::shared_ptr<Block> get(const std::string& block_id) {
        auto block = cache_.get(block_id);
        if (block) return *block;

        // 从磁盘加载
        block = load_from_disk(block_id);
        cache_.put(block_id, block);
        return block;
    }
};
```

**2. Bloom Filter**
```cpp
// 每个SSTable带一个Bloom Filter
// 查询时先查BF，如果不存在，直接跳过该SSTable

class SSTable {
    BloomFilter bloom_filter_;

    std::optional<std::string> get(const std::string& key) {
        if (!bloom_filter_.may_contain(key)) {
            return std::nullopt;  // 一定不存在，快速返回
        }
        // 可能存在，继续查找
        // ...
    }
};
```

**3. 索引优化**
```cpp
// SSTable索引：稀疏索引，每1KB数据一个索引点
// 内存中只加载索引，不加载数据

struct IndexEntry {
    std::string key;      // 该数据块的最大key
    uint64_t offset;      // 在文件中的偏移
    uint32_t length;      // 数据块长度
};

// 二分查找定位数据块
auto it = std::lower_bound(index_.begin(), index_.end(), key);
if (it != index_.begin()) --it;  // 找到包含key的数据块

// 只读取该数据块，不是整个文件
```

**4. 压缩**
```cpp
// SSTable数据压缩，减少IO
// 可选：Snappy（速度快）、LZ4、Zstd

// 读取时解压
std::string compressed = read_from_disk(offset, length);
std::string data = decompress(compressed);
```

### Q12: 日志系统为什么需要异步？如何实现的？

**回答：**

**同步日志的问题：**
```cpp
// 同步日志
void log_sync(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_ << msg << std::endl;  // 直接写磁盘
    file_.flush();              // 强制刷盘
}
// 问题：每次日志都磁盘IO，延迟高（~10ms）
// 问题：锁竞争，多线程串行
```

**异步日志实现：**

```cpp
class AsyncLogger {
    // 双缓冲
    std::unique_ptr<LogBuffer> current_buffer_;  // 当前写入
    std::unique_ptr<LogBuffer> next_buffer_;     // 备用
    std::vector<std::unique_ptr<LogBuffer>> buffers_to_write_;  // 待写入

    std::mutex mutex_;
    std::condition_variable cond_;
    std::thread backend_thread_;

public:
    void log(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!current_buffer_->append(msg)) {
            // 当前缓冲区满，切换到备用
            buffers_to_write_.push_back(std::move(current_buffer_));

            if (next_buffer_) {
                current_buffer_ = std::move(next_buffer_);
            } else {
                current_buffer_ = std::make_unique<LogBuffer>();
            }

            current_buffer_->append(msg);
            cond_.notify_one();
        }
    }

    void backend_thread() {
        while (running_) {
            std::vector<std::unique_ptr<LogBuffer>> buffers_to_write;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cond_.wait_for(lock, std::chrono::seconds(3),
                    [this] { return !buffers_to_write_.empty(); });

                buffers_to_write.swap(buffers_to_write_);

                if (!current_buffer_->empty()) {
                    buffers_to_write_.push_back(std::move(current_buffer_));
                    current_buffer_ = std::move(next_buffer_) ?: std::make_unique<LogBuffer>();
                }
            }

            // 批量写入磁盘（无锁，不阻塞前端）
            for (auto& buffer : buffers_to_write) {
                file_.write(buffer->data(), buffer->length());
            }
            file_.flush();
        }
    }
};
```

**优势：**
- 前端只是内存拷贝，~100ns
- 批量写入磁盘，减少IO次数
- 不阻塞业务线程

---

## 场景设计题

### Q13: 如果QPS突然增加10倍，系统会有什么表现？如何优化？

**回答：**

**可能出现的问题：**
1. **CPU打满**：线程数固定，任务队列堆积
2. **内存暴涨**：请求堆积，OOM
3. **磁盘IO饱和**：WAL写入跟不上
4. **连接数耗尽**：文件描述符或内存限制

**优化方案：**

```cpp
// 1. 动态线程池
class DynamicThreadPool {
    size_t min_threads_ = 4;
    size_t max_threads_ = 64;
    std::atomic<size_t> current_threads_{4};

    void monitor() {
        while (running_) {
            auto queue_size = task_queue_.size();
            auto active = active_tasks_.load();

            if (queue_size > 100 && current_threads_ < max_threads_) {
                add_thread();  // 扩容
            } else if (queue_size == 0 && active < current_threads_ / 2) {
                remove_thread();  // 缩容
            }

            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
};

// 2. 限流
class RateLimiter {
    TokenBucket bucket_{1000, 10000};  // 每秒1000，突发10000

    bool allow() {
        return bucket_.consume(1);
    }
};

// 3. 快速拒绝
if (current_connections_ >= max_connections_) {
    send_error(503, "Service Unavailable");
    close(fd);
    return;
}

// 4. 多级队列
// - 高优先级：控制命令
// - 中优先级：普通请求
// - 低优先级：后台任务
```

### Q14: 如何实现存储引擎的分布式扩展？

**回答：**

**分布式架构：**

```
                 Client
                   |
             Load Balancer
                   |
    +--------------+--------------+
    |              |              |
  Node1         Node2         Node3
 (A-C)          (D-F)          (G-I)
```

**关键设计：**

```cpp
// 1. 一致性哈希分片
class ConsistentHash {
    std::map<uint32_t, std::string> ring_;  // hash -> node

    std::string get_node(const std::string& key) {
        uint32_t hash = hash_key(key);
        auto it = ring_.lower_bound(hash);
        if (it == ring_.end()) it = ring_.begin();
        return it->second;
    }
};

// 2. Raft共识（复制）
class RaftNode {
    enum Role { FOLLOWER, CANDIDATE, LEADER };

    // Leader处理写，复制到Follower
    bool put(const std::string& key, const std::string& value) {
        if (role_ != LEADER) {
            return forward_to_leader(key, value);
        }

        // 写入本地WAL
        wal_->append(key, value);

        // 复制到多数节点
        int acks = 1;  // 自己
        for (auto& peer : peers_) {
            if (peer.append_entries(key, value)) {
                acks++;
            }
        }

        if (acks > peers_.size() / 2) {
            apply_to_state_machine(key, value);
            return true;
        }
        return false;
    }
};

// 3. 分片迁移
void migrate_shard(int shard_id, const std::string& from, const std::string& to) {
    // 1. 在to节点创建副本
    auto data = from_node.get_shard_data(shard_id);
    to_node.import_shard(shard_id, data);

    // 2. 双写
    config_.set_dual_write(shard_id, from, to);

    // 3. 等待同步完成
    wait_until_synced(shard_id);

    // 4. 切换路由
    config_.update_route(shard_id, to);

    // 5. 删除from数据
    from_node.delete_shard(shard_id);
}
```

---

## 总结

### 面试核心要点

1. **项目背景清晰**：为什么做、怎么做的、遇到什么困难、怎么解决的
2. **技术细节扎实**：能讲清楚每个设计决策的原因
3. **性能数据准确**：知道每个数字的来源和测试方法
4. **有扩展思考**：不仅实现了，还思考了如何改进

### 常见问题陷阱

- 不要说"用了某个技术因为别人都用"
- 不要夸大性能数字
- 不要回避缺点，要说明如何改进
- 不要只讲做了什么，要讲为什么这么做
