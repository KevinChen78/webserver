#!/bin/bash
# WSL环境设置脚本
# 在WSL Ubuntu中运行此脚本安装所有依赖

set -e

echo "========================================"
echo "WebServer WSL 环境配置"
echo "========================================"
echo ""

# 检测Ubuntu版本
UBUNTU_VERSION=$(lsb_release -rs 2>/dev/null || echo "unknown")
echo "检测到Ubuntu版本: $UBUNTU_VERSION"

# 更新包列表
echo "[1/6] 更新包列表..."
sudo apt-get update -qq

# 安装基础工具
echo "[2/6] 安装基础工具..."
sudo apt-get install -y -qq \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    ninja-build \
    pkg-config

# 安装GCC 10+ (支持C++20协程)
echo "[3/6] 安装GCC 10+..."
sudo apt-get install -y -qq \
    gcc-10 \
    g++-10 \
    gcc-11 \
    g++-11 \
    gcc-12 \
    g++-12

# 设置默认GCC版本
if command -v g++-12 &> /dev/null; then
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100
    echo "  -> 设置GCC 12为默认编译器"
elif command -v g++-11 &> /dev/null; then
    sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 90
    sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 90
    echo "  -> 设置GCC 11为默认编译器"
fi

# 安装调试工具
echo "[4/6] 安装调试工具..."
sudo apt-get install -y -qq \
    gdb \
    valgrind \
    linux-tools-generic \
    linux-tools-common \
    perf-tools-unstable

# 安装测试工具
echo "[5/6] 安装测试工具..."
sudo apt-get install -y -qq \
    apache2-utils \
    ftp

# 验证安装
echo "[6/6] 验证安装..."
echo ""
echo "编译器版本:"
gcc --version | head -1
g++ --version | head -1
echo ""
echo "CMake版本:"
cmake --version | head -1
echo ""

# 检查C++20支持
echo "检查C++20协程支持..."
cat > /tmp/check_coro.cpp << 'EOF'
#include <coroutine>
#include <iostream>

struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() {}
        void unhandled_exception() {}
    };
};

int main() {
    std::cout << "C++20协程支持正常!" << std::endl;
    return 0;
}
EOF

g++-12 -std=c++20 -fcoroutines /tmp/check_coro.cpp -o /tmp/check_coro 2>/dev/null || \
g++-11 -std=c++20 -fcoroutines /tmp/check_coro.cpp -o /tmp/check_coro 2>/dev/null || \
g++-10 -std=c++20 -fcoroutines /tmp/check_coro.cpp -o /tmp/check_coro 2>/dev/null

if [ -f /tmp/check_coro ]; then
    /tmp/check_coro
    rm /tmp/check_coro /tmp/check_coro.cpp
else
    echo "警告: C++20协程支持检测失败"
    rm /tmp/check_coro.cpp
fi

echo ""
echo "========================================"
echo "WSL环境配置完成!"
echo "========================================"
echo ""
echo "下一步:"
echo "  1. cd到项目目录: cd /mnt/d/Claude\\ Code/webserver"
echo "  2. 运行编译脚本: ./wsl_build.sh"
echo ""
