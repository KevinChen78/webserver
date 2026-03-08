#!/bin/bash
# Test runner script for WebServer project

set -e

echo "========================================"
echo "WebServer Test Suite"
echo "========================================"

cd build

# Run individual tests
tests=(
    "test_logger:Logger Tests"
    "test_storage_engine:Storage Engine Tests"
    "test_lru_cache:LRU Cache Tests"
    "test_ftp_server:FTP Server Tests"
    "test_static_file_handler:Static File Handler Tests"
)

passed=0
failed=0

for test_info in "${tests[@]}"; do
    IFS=':' read -r test_name test_desc <<< "$test_info"
    
    echo ""
    echo "----------------------------------------"
    echo "Running: $test_desc"
    echo "----------------------------------------"
    
    if [ -f "./$test_name" ]; then
        if "./$test_name"; then
            echo "[PASS] $test_desc"
            ((passed++))
        else
            echo "[FAIL] $test_desc"
            ((failed++))
        fi
    else
        echo "[SKIP] $test_desc - executable not found"
        ((failed++))
    fi
done

echo ""
echo "========================================"
echo "Test Summary"
echo "========================================"
echo "Passed: $passed"
echo "Failed: $failed"
echo "Total:  $((passed + failed))"
echo "========================================"

if [ $failed -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
