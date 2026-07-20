#!/bin/bash
# WSL编译脚本
# 在项目根目录运行: ./wsl_build.sh

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "========================================"
echo "WebServer WSL 编译脚本"
echo "========================================"
echo ""

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "工作目录: $(pwd)"
echo ""

# 检测可用的GCC版本
detect_gcc_version() {
    if command -v g++-12 &> /dev/null; then
        echo "12"
    elif command -v g++-11 &> /dev/null; then
        echo "11"
    elif command -v g++-10 &> /dev/null; then
        echo "10"
    else
        echo ""
    fi
}

GCC_VERSION=$(detect_gcc_version)
if [ -z "$GCC_VERSION" ]; then
    echo -e "${RED}错误: 未找到GCC 10+，请先运行 ./wsl_setup.sh${NC}"
    exit 1
fi

echo "使用GCC版本: $GCC_VERSION"

# 创建build目录
echo ""
echo "[1/4] 创建build目录..."
if [ -d "build" ]; then
    echo "  -> build目录已存在，清理旧的构建文件..."
    rm -rf build/*
else
    mkdir build
fi

cd build

# 配置CMake
echo ""
echo "[2/4] 配置CMake..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-$GCC_VERSION \
    -DCMAKE_CXX_COMPILER=g++-$GCC_VERSION \
    -G "Unix Makefiles" \
    -DCMAKE_CXX_FLAGS="-fcoroutines"

# 编译
echo ""
echo "[3/4] 开始编译..."
echo "  -> 使用 $(nproc) 个并行任务"
echo ""
make -j$(nproc)

# 检查编译结果
echo ""
echo "[4/4] 检查编译结果..."

EXECUTABLES=(
    "test_logger"
    "test_storage_engine"
    "test_lru_cache"
    "test_ftp_server"
    "test_static_file_handler"
    "logger_demo"
    "storage_demo"
    "full_server_demo"
    "storage_bench"
)

COMPILED_COUNT=0
for exe in "${EXECUTABLES[@]}"; do
    if [ -f "$exe" ]; then
        echo -e "  ${GREEN}✓${NC} $exe"
        ((COMPILED_COUNT++))
    else
        echo -e "  ${RED}✗${NC} $exe (未找到)"
    fi
done

echo ""
echo "========================================"
if [ $COMPILED_COUNT -eq ${#EXECUTABLES[@]} ]; then
    echo -e "${GREEN}编译成功! ($COMPILED_COUNT/${#EXECUTABLES[@]})${NC}"
else
    echo -e "${YELLOW}部分目标编译完成 ($COMPILED_COUNT/${#EXECUTABLES[@]})${NC}"
fi
echo "========================================"
echo ""
echo "下一步:"
echo "  1. 运行测试: ./wsl_test.sh"
echo "  2. 运行演示: cd build && ./logger_demo"
echo ""
