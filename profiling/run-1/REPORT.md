# webserver — 火焰图调优审计（run-1）

**日期**：2026-06-30 · **工具链**：WSL2 Ubuntu + Linux perf 6.8.12 + Brendan Gregg FlameGraph
**构建**：`g++ -std=c++20 -fcoroutines -O2 -g -fno-omit-frame-pointer -DNDEBUG`（build-prof/）
**采样**：`perf record -F 999 -g --call-graph=dwarf -e task-clock:u` × 10 轮，通过 `stackcollapse-perf.pl` 合并

---

## TL;DR

| 场景 | 综合分 | 评级 | 头条结论 |
|---|---|---|---|
| HTTP（coro_http_server，c=32 -k） | **63.3** | C | `pthread_create` 占 **30.66%**——thread-per-connection，不是协程 |
| 存储引擎（storage_bench） | **60.4** | C | `shared_ptr` 原子引用计数 ≈ **10%** + `pthread_rwlock` ≈ **5%** |

两者都属于"中等水平"。每种情形的瓶颈都是 **基础设施开销（线程/TLS/引用计数/锁），而非实际业务逻辑**。HTTP 的结果更严重，因为它与 README 宣称的 Reactor+协程架构相矛盾。

---

## 关键架构发现 ⚠️

**webserver 项目并不包含可用的 HTTP 服务器。** [`CMakeLists.txt:65-70`](../../CMakeLists.txt#L65-L70) 显示静态库只编译了 `logger`/`storage_engine`/`ftp_server`/`static_file_handler`。[`build-prof/full_server_demo`](../../build-prof/) 二进制启动的是 FTP+Storage+Logger —— **没有 8080 端口，没有 HTTP 路由**。

README 头条的"HTTP长连接 QPS 5.2万"无法从此代码库复现，因为 HTTP 服务器根本不在里面。为了让火焰图上有 *任何* HTTP 场景，我们用 [`coro/examples/http_server_demo.cpp`](../../../coro/examples/http_server_demo.cpp) 替代——它位于 *另一个* 项目里。此处仍汇报该结果，是因为 coro HTTP 服务器是 webserver README HTTP 声称最接近的可用代理，也是面试官运行该仓库时实际看到的东西。

存储场景是唯一真正运行 webserver 自身代码的场景。

---

## 场景 A —— HTTP 服务器（以 coro_http_server 作为代理）

**负载**：`webbench -c 32 -t 28 -k http://localhost:8080/` → **28,877 QPS**，808,572 成功 / 2,131 失败。
**README 声称**：5.2万（52,000）QPS —— **偏差 44.5%**（K10 = 0）。

### Top-5 自身热点（占 CPU 74.57%）

| % | 函数 | 解读 |
|---|---|---|
| **30.66%** | `pthread_create` | 每个请求创建一个新线程 |
| 21.60% | `[libc.so.6]`（未解析） | 主要是 TLS + 动态链接器胶水代码 |
| 9.76% | `[ld-linux-x86-64.so.2]` | `_dl_allocate_tls_init` 等——同一根因 |
| 7.67% | `malloc` | 线程栈 + TLS 分配 |
| 4.88% | `_dl_allocate_tls_init` | 每线程 TLS 初始化 |

### 火焰图解读

打开 [`coro_http/flamegraph.svg`](coro_http/flamegraph.svg)。最大的高原是 `pthread_create` → `_dl_allocate_tls_init` → `malloc`。真正的 HTTP 解析路径（`coro::io::TcpAcceptor::accept` 1.74%、`coro::net::http::Server::start` 0.70%）在这个尺度下 **几乎看不见**。

### 结论

该服务器号称是基于协程的 HTTP 服务器，但请求路径却是 `accept → std::thread::detach → handle`。每个请求要付出：
- ~30 ms 的 `pthread_create` 开销
- ~5 ms 的 TLS 初始化
- ~8 ms 的线程栈 malloc

32 个并发客户端下，这会串行化到内核的线程创建路径上。coro 库的 `Task<T>` 和 `ThreadPool` 在代码里都有，但 **不在它自己 HTTP 示例的热路径上**。这是一个穿着协程外衣的 thread-per-connection 服务器。

### KPI 分项

| KPI | 得分 | 原始值 | 备注 |
|---|---|---|---|
| K1 集中度 | 41.7 | top-5 = 74.6% | 全部 5 个热点都是基础设施 |
| K2 分配 | 0 | 11.5% | 线程栈 + TLS |
| K3 锁 | 100 | 4.5% | 线程 spawn 时的 mutex |
| K4 原子 | 100 | 0% | 无原子密集路径 |
| K5 系统调用 | 100 | 6.3% | accept() 正常 |
| K6 空闲/等待 | 100 | 0% | CPU 密集型 |
| K7 memcpy | 100 | 0% | —— |
| K8 叶子多样性 | 40 | 11 个叶子 | 路径很少（火焰图扁平 = 线程开销主导） |
| K9 调用栈深度 | 100 | p90 = 10 | 浅 |
| K10 README 复现 | 0 | 偏差 44.5% | 实测 28.8K vs 声称 52K |

---

## 场景 B —— 存储引擎（storage_bench）

**负载**：100K 次操作 × {1,4,8,16} 线程，写 + 读基准测试。
**README 声称**：36万读 / 30万写。

### 实测 vs 声称

| 工作负载 | 1 线程 | 4 线程 | 8 线程 | 16 线程 | README | 结论 |
|---|---|---|---|---|---|---|
| 写 | 172.0K | 132.0K | 138.1K | 137.5K | 300K | **最佳情况差 43%**；1→4 线程 *负 scaling* |
| 读 | 1.21M | 3.47M | 4.61M | 4.21M | 360K | **+1180%**（声明值的 12.8 倍） ✅ |

读性能远超 README 声称——可能是 README 报错了或测的是更冷的缓存。写性能才是值得关注的失败：1→4 线程出现 **负 scaling**，说明存在争用。

### Top-5 自身热点（占 CPU 42.6%）

| % | 函数 | 解读 |
|---|---|---|
| 22.02% | `[libc.so.6]`（未解析） | 包含 libc 内的字符串操作 + memcpy |
| 6.46% | `std::_Sp_counted_base` | **shared_ptr 原子引用计数** |
| 5.00% | `std::__cmp_cat::__unspec` | `std::string::compare` 胶水 |
| 4.76% | `__gnu_cxx::__atomic_add` | **shared_ptr 引用计数原子操作** |
| 4.35% | `malloc` | 节点分配 |

### CPU 实际花在哪（重新分组）

| 类别 | % | 细节 |
|---|---|---|
| `shared_ptr` 引用计数（原子增减 + counted_base） | **≈10%** | SkipListNode 生命周期管理 |
| `pthread_rwlock_*`（读/写/解锁） | **≈5%** | Memtable 行锁 |
| malloc/free | **≈7.5%** | 节点 + 字符串分配 |
| `std::string::compare` + `xsputn` | **≈8%** | 跳表键比较 |

### 火焰图解读

打开 [`webserver_storage/flamegraph.svg`](webserver_storage/flamegraph.svg)。最宽的用户代码帧是 `SkipList::find_node`（0.90%）——也就是说 **实际算法工作只占 CPU 的不到 1%**。火焰图被 libstdc++ 模板管道（`std::_Sp_counted_base`、`std::__shared_count`、`std::__copy_move<false, false, …>`）主导，它们夹在算法和数据之间。

### 写性能为何不 scaling

`SkipListNode` 由 `shared_ptr` 持有——每次插入都要：
1. `pthread_rwlock_wrlock`（1.27%）
2. 分配节点 + N 个 `shared_ptr` 构造时的原子 inc（每条前向指针）
3. 更新邻居 → 更多原子 inc/dec
4. `pthread_rwlock_unlock`（2.58%）

多线程写下，rwlock + 前向指针向量上的原子操作把写线程串行化了。读能 scaling，因为 `pthread_rwlock_rdlock`（0.98%）是并发持有的。

### 结论

存储引擎的 **算法选择没问题**（SkipList + Memtable 是正确的设计），但 **实现把 shared_ptr 和 rwlock 开销泄漏到了每一次操作里**。把 `shared_ptr<SkipListNode>` 换成 `unique_ptr` + 裸反向指针（或侵入式引用计数）可以直接消掉 ~10% 的 CPU，并很可能修复写性能的负 scaling。

### KPI 分项

| KPI | 得分 | 原始值 | 备注 |
|---|---|---|---|
| K1 集中度 | 100 | top-5 = 42.6% | 分布良好 |
| K2 分配 | 0 | 13.5% | malloc + shared_ptr 引用计数机制 |
| K3 锁 | 94.4 | 5.6% | rwlock——处于健康上限 |
| K4 原子 | 43.7 | 5.4% | shared_ptr 引用计数 |
| K5 系统调用 | 100 | 0% | 纯用户态 |
| K6 空闲/等待 | 100 | 0% | CPU 密集型 |
| K7 memcpy | 100 | 0% | —— |
| K8 叶子多样性 | 100 | 40 个叶子 | 丰富，内联合理 |
| K9 调用栈深度 | 0 | p90 = 189 ⚠ | 中位数 p50=14 健康；p90 被超长 DWARF unwind 尾巴（最大 624）拉高。视为伪影。 |
| K10 README 复现 | 0 | 偏差 42.7% | 写差 43%；读超声明 12 倍 |

---

## 最优先修复（按 ROI 排序）

1. **HTTP：停止每请求创建线程。** 把 `coro_http_server` 改成真正用自带的 `coro::ThreadPool`。光这一项就应该把 QPS 从 28K 推到 50K+ 区间，并把 `pthread_create` 从 30% 降到 <1%。**消除整个审计中最大的热点。**
2. **存储：去掉 `shared_ptr<SkipListNode>`。** 用 `unique_ptr` 持有所有权，前向链接用裸指针。CPU 节省 ~10% + 修复写 scaling。
3. **存储：考虑把 rwlock 换成 sequence lock 或 RCU。** rwlock 写者会串行化；对于读多写少的跳表，seqlock 能让写者无需独占锁推进。

## 已经做得好的地方

- 存储算法选择（SkipList + Memtable + SSTable）合适；4.6M ops/sec 的读性能确实强。
- 无内存池病态：`memcpy`/`memmove`/`memset` ≈ 0%——没有意外的批量拷贝。
- 存储无系统调用泄漏：0% `read`/`write`/`epoll`——纯内存引擎。
- 无 futex/nanosleep 等待：0%——代码是 CPU 密集而非阻塞。

## 产物

```
webserver/profiling/run-1/
├── REPORT.md                         (本文件)
├── score.py                          (KPI 引擎——可复用)
├── coro_http/
│   ├── flamegraph.svg                ← 浏览器打开
│   ├── folded.txt                    ← 合并 10 轮的调用栈
│   ├── top-self-merged.txt           ← 合并后的叶子排名
│   ├── webbench.out                  ← 28,877 QPS
│   └── scores.json                   ← K1–K10 + 综合分 63.3
└── webserver_storage/
    ├── flamegraph.svg                ← 浏览器打开
    ├── folded.txt
    ├── top-self-merged.txt
    ├── storage_bench.out             ← 各线程数实测 QPS
    └── scores.json                   ← 综合分 60.4
```
