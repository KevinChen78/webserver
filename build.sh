#!/bin/bash
# Build script for WebServer project

set -e

echo "========================================"
echo "WebServer Build Script"
echo "========================================"

# Create build directory
mkdir -p build
cd build

# Configure
echo "Configuring with CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
echo "Building project..."
make -j$(nproc) 2>&1 | tee build.log

echo ""
echo "========================================"
echo "Build completed successfully!"
echo "========================================"
echo ""
echo "Executables created:"
ls -lh test_* full_server_demo storage_bench webbench 2>/dev/null || echo "  (check build directory)"
