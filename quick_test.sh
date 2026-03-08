#!/bin/bash
# Quick test - run a single test for quick verification

set -e

cd build

echo "Running quick tests..."
echo ""

# Test 1: Logger
echo "[1/3] Testing Logger..."
./test_logger && echo "  Logger: PASS" || echo "  Logger: FAIL"

# Test 2: LRU Cache
echo "[2/3] Testing LRU Cache..."
./test_lru_cache && echo "  LRU Cache: PASS" || echo "  LRU Cache: FAIL"

# Test 3: Storage Engine (quick version - 1000 ops)
echo "[3/3] Testing Storage Engine..."
./test_storage_engine && echo "  Storage Engine: PASS" || echo "  Storage Engine: FAIL"

echo ""
echo "Quick tests completed!"
