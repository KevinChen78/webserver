#!/bin/bash
# WSL测试脚本
# 在项目根目录运行: ./wsl_test.sh

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo "========================================"
echo "WebServer WSL 测试脚本"
echo "========================================"
echo ""

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 检查build目录是否存在
if [ ! -d "build" ]; then
    echo -e "${RED}错误: build目录不存在，请先运行 ./wsl_build.sh${NC}"
    exit 1
fi

cd build

# 创建必要的目录
mkdir -p logs data demo_data www ftp_root

# 测试结果统计
PASSED=0
FAILED=0
TOTAL=0

# 运行单个测试
run_test() {
    local test_name=$1
    local test_exe=$2
    local max_time=${3:-30}  # 默认30秒超时

    ((TOTAL++))

    echo ""
    echo -e "${BLUE}[测试 $TOTAL] $test_name${NC}"
    echo "----------------------------------------"

    if [ ! -f "$test_exe" ]; then
        echo -e "${RED}  ✗ 测试程序不存在: $test_exe${NC}"
        ((FAILED++))
        return
    fi

    # 运行测试并捕获输出
    if timeout $max_time ./$test_exe 2>&1; then
        echo -e "${GREEN}  ✓ $test_name 通过${NC}"
        ((PASSED++))
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            echo -e "${RED}  ✗ $test_name 超时 (> ${max_time}s)${NC}"
        else
            echo -e "${RED}  ✗ $test_name 失败 (退出码: $exit_code)${NC}"
        fi
        ((FAILED++))
    fi
}

echo "开始运行单元测试..."
echo ""

# 运行测试
run_test "日志系统测试" "test_logger" 30
run_test "存储引擎测试" "test_storage_engine" 60
run_test "LRU缓存测试" "test_lru_cache" 30
run_test "FTP服务器测试" "test_ftp_server" 30
run_test "静态文件处理器测试" "test_static_file_handler" 30

# 测试摘要
echo ""
echo "========================================"
echo "测试结果摘要"
echo "========================================"
echo "总测试数: $TOTAL"
echo -e "通过: ${GREEN}$PASSED${NC}"
echo -e "失败: ${RED}$FAILED${NC}"

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}所有测试通过!${NC}"
else
    echo -e "${YELLOW}部分测试失败，请检查上方输出${NC}"
fi

echo ""
echo "========================================"
echo "性能测试"
echo "========================================"
echo ""

# 询问是否运行性能测试
read -p "是否运行存储引擎性能测试? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo ""
    echo "运行存储引擎基准测试..."
    if [ -f "storage_bench" ]; then
        ./storage_bench
    else
        echo -e "${RED}未找到 storage_bench${NC}"
    fi
fi

echo ""
echo "========================================"
echo "演示程序"
echo "========================================"
echo ""
echo "可用演示程序:"
echo "  1. ./logger_demo      - 日志系统演示"
echo "  2. ./storage_demo     - 存储引擎演示"
echo "  3. ./full_server_demo - 完整服务器演示 (HTTP + FTP)"
echo ""
echo "使用示例:"
echo "  cd build"
echo "  ./logger_demo"
echo ""
