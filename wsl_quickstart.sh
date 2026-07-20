#!/bin/bash
# WSL 快速开始脚本
# 一键完成环境设置、编译和测试

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================"
echo "WebServer WSL 快速开始"
echo "========================================"
echo ""

# 检查是否在WSL中
if ! grep -q Microsoft /proc/version 2>/dev/null && ! grep -q WSL /proc/version 2>/dev/null; then
    echo -e "${YELLOW}警告: 似乎不在WSL环境中${NC}"
    read -p "是否继续? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# 步骤1: 环境检查
echo -e "${BLUE}[步骤 1/4] 检查环境...${NC}"

if ! command -v g++ &> /dev/null; then
    echo -e "${YELLOW}未找到GCC，需要安装依赖${NC}"
    if [ -f "wsl_setup.sh" ]; then
        echo "运行 wsl_setup.sh..."
        chmod +x wsl_setup.sh
        ./wsl_setup.sh
    else
        echo -e "${RED}错误: wsl_setup.sh 不存在${NC}"
        exit 1
    fi
else
    GCC_VER=$(g++ --version | head -1 | grep -oP '\d+' | head -1)
    echo -e "  ${GREEN}✓${NC} 找到GCC版本: $GCC_VER"
fi

# 步骤2: 编译
echo ""
echo -e "${BLUE}[步骤 2/4] 编译项目...${NC}"
chmod +x wsl_build.sh
./wsl_build.sh

# 步骤3: 运行测试
echo ""
echo -e "${BLUE}[步骤 3/4] 运行测试...${NC}"
chmod +x wsl_test.sh
./wsl_test.sh --quick

# 步骤4: 验证
echo ""
echo -e "${BLUE}[步骤 4/4] 验证安装...${NC}"

cd build

# 检查关键可执行文件
KEY_EXECUTABLES=("test_logger" "test_storage_engine" "storage_demo" "logger_demo")
ALL_FOUND=true

for exe in "${KEY_EXECUTABLES[@]}"; do
    if [ -f "$exe" ]; then
        echo -e "  ${GREEN}✓${NC} $exe"
    else
        echo -e "  ${RED}✗${NC} $exe"
        ALL_FOUND=false
    fi
done

echo ""
echo "========================================"
if [ "$ALL_FOUND" = true ]; then
    echo -e "${GREEN}✓ 快速开始完成!${NC}"
    echo "========================================"
    echo ""
    echo "现在你可以:"
    echo ""
    echo "1. 运行演示:"
    echo -e "   ${YELLOW}cd build && ./logger_demo${NC}"
    echo -e "   ${YELLOW}cd build && ./storage_demo${NC}"
    echo ""
    echo "2. 启动完整服务器:"
    echo -e "   ${YELLOW}cd build && ./full_server_demo${NC}"
    echo ""
    echo "3. 运行单个测试:"
    echo -e "   ${YELLOW}cd build && ./test_storage_engine${NC}"
    echo ""
    echo "4. 查看详细指南:"
    echo -e "   ${YELLOW}cat WSL_GUIDE.md${NC}"
    echo ""
else
    echo -e "${YELLOW}! 部分文件未找到，请检查编译输出${NC}"
    echo "========================================"
fi
