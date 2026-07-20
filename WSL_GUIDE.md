# WSL 编译和测试指南

本指南帮助你在 Windows Subsystem for Linux (WSL) 中编译和测试 WebServer 项目。

## 环境要求

- Windows 10/11 安装 WSL2
- Ubuntu 20.04+ (推荐 22.04 LTS)
- 至少 4GB 内存
- 20GB 可用磁盘空间

## 快速开始

### 1. 安装 WSL (如果尚未安装)

```powershell
# 以管理员身份打开 PowerShell，运行:
wsl --install -d Ubuntu-22.04
```

安装完成后重启电脑，然后打开 Ubuntu 终端完成初始设置。

### 2. 克隆或进入项目目录

如果你在 Windows D 盘有项目：

```bash
# 进入 Windows D 盘的项目目录
cd /mnt/d/Claude\ Code/webserver

# 或者将项目复制到 WSL 内部（性能更好）
cp -r /mnt/d/Claude\ Code/webserver ~/webserver
cd ~/webserver
```

**提示**: 在 WSL 内部编译比访问 Windows 文件系统（/mnt/d）性能更好。

### 3. 一键设置环境

```bash
# 给脚本添加执行权限
chmod +x wsl_setup.sh wsl_build.sh wsl_test.sh

# 运行环境设置脚本
./wsl_setup.sh
```

这会安装：
- GCC 10/11/12 (C++20 支持)
- CMake 3.15+
- Ninja 构建工具
- 调试工具 (gdb, valgrind)
- 测试工具 (ab, ftp)

### 4. 编译项目

```bash
./wsl_build.sh
```

编译完成后，可执行文件位于 `build/` 目录。

### 5. 运行测试

```bash
./wsl_test.sh
```

这会运行所有单元测试并显示结果。

## 手动编译步骤

如果你想手动控制编译过程：

```bash
# 创建构建目录
mkdir -p build && cd build

# 配置 (使用 GCC 12)
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-12 \
    -DCMAKE_CXX_COMPILER=g++-12 \
    -DCMAKE_CXX_FLAGS="-fcoroutines"

# 编译
make -j$(nproc)
```

## 调试编译问题

### 问题 1: GCC 版本过低

```bash
# 检查 GCC 版本
g++ --version

# 如果版本低于 10，安装新版本
sudo apt-get install g++-12

# 设置默认版本
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100
```

### 问题 2: CMake 版本过低

```bash
# 检查 CMake 版本
cmake --version

# 如果低于 3.15，从官方安装
pip3 install cmake>=3.25

# 或者使用 snap
sudo snap install cmake --classic
```

### 问题 3: C++20 协程不支持

确保编译时添加了 `-fcoroutines` 标志：

```bash
cmake .. -DCMAKE_CXX_FLAGS="-fcoroutines"
```

### 问题 4: 找不到 pthread

```bash
# 安装 pthread 开发库
sudo apt-get install libpthread-stubs0-dev
```

## 运行演示

### 日志演示

```bash
cd build
./logger_demo
ls logs/  # 查看生成的日志文件
```

### 存储引擎演示

```bash
cd build
./storage_demo
```

### 完整服务器演示

```bash
cd build
./full_server_demo

# 在另一个 WSL 终端测试 HTTP:
curl http://localhost:8080/api/health
curl http://localhost:8080/api/stats

# 测试 FTP:
ftp localhost 2121
# 用户名: anonymous
# 密码: (任意输入)
```

## 性能测试

### 存储引擎基准测试

```bash
cd build
./storage_bench
```

预期结果：
- 写操作: >300K ops/sec
- 读操作: >360K ops/sec

### HTTP 压力测试

首先需要安装 webbench：

```bash
# 在 benchmarks/ 目录有 webbench 源码
cd benchmarks/webbench-1.5
make
sudo make install
```

然后运行测试：

```bash
# 启动服务器
cd build
./full_server_demo &

# 短连接测试
webbench -c 1000 -t 60 http://localhost:8080/

# 长连接测试
webbench -c 1000 -t 60 -k http://localhost:8080/

# 停止服务器
kill %1
```

## 使用 VSCode 远程开发

推荐在 Windows 上使用 VSCode + WSL 扩展进行开发：

1. 安装 [VSCode](https://code.visualstudio.com/)
2. 安装 "WSL" 扩展
3. 按 `Ctrl+Shift+P`，输入 "WSL: Connect to WSL"
4. 打开项目文件夹
5. 安装 C/C++ 扩展获得智能提示和调试支持

### VSCode 配置

创建 `.vscode/settings.json`：

```json
{
    "cmake.configureSettings": {
        "CMAKE_C_COMPILER": "/usr/bin/gcc-12",
        "CMAKE_CXX_COMPILER": "/usr/bin/g++-12",
        "CMAKE_CXX_FLAGS": "-fcoroutines"
    },
    "cmake.buildDirectory": "${workspaceFolder}/build",
    "C_Cpp.default.cppStandard": "c++20"
}
```

## 常见问题

### Q: WSL2 访问 Windows 文件慢怎么办？

**A**: 将项目复制到 WSL 内部文件系统：

```bash
cp -r /mnt/d/Claude\ Code/webserver ~/webserver
cd ~/webserver
# 后续在 ~/webserver 中工作
```

### Q: 编译时内存不足？

**A**: 减少并行编译任务数：

```bash
make -j2  # 使用 2 个线程而不是全部
```

### Q: 端口被占用？

**A**: 查找并结束占用端口的进程：

```bash
# 查找占用 8080 端口的进程
sudo lsof -i :8080

# 结束进程
kill -9 <PID>

# 或者修改演示程序使用其他端口
```

### Q: 如何完全清理重新编译？

```bash
rm -rf build
./wsl_build.sh
```

### Q: 测试超时怎么办？

**A**: 可能是系统负载过高，可以单独运行测试：

```bash
cd build
./test_logger
./test_storage_engine
# ... 逐个运行
```

## 性能优化建议

1. **使用 WSL2**: WSL2 比 WSL1 有更好的文件系统性能
2. **项目在 WSL 内部**: 避免在 `/mnt/` 下编译
3. **使用 Ninja**: 比 Make 更快
4. **Release 模式**: 确保 `CMAKE_BUILD_TYPE=Release`

```bash
# 使用 Ninja 构建
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja
```

## 获取帮助

如果遇到问题：

1. 检查各组件版本：
   ```bash
   gcc --version
   cmake --version
   lsb_release -a
   ```

2. 查看详细编译输出：
   ```bash
   cmake .. --debug-output
   make VERBOSE=1
   ```

3. 在 build 目录查看 CMake 错误日志

---

**提示**: 首次编译可能需要 5-10 分钟（取决于硬件），后续增量编译会很快。
