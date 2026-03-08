# Tests CMakeLists.txt

# Test executable for logger
add_executable(test_logger
    test_logger.cpp
    ../src/utils/logger.cpp
)
target_include_directories(test_logger PRIVATE ../include)
target_link_libraries(test_logger PRIVATE pthread)

# Test executable for storage engine
add_executable(test_storage_engine
    test_storage_engine.cpp
    ../src/storage/storage_engine.cpp
    ../src/utils/logger.cpp
)
target_include_directories(test_storage_engine PRIVATE ../include)
target_link_libraries(test_storage_engine PRIVATE pthread)

# Test executable for LRU cache
add_executable(test_lru_cache
    test_lru_cache.cpp
)
target_include_directories(test_lru_cache PRIVATE ../include)
target_link_libraries(test_lru_cache PRIVATE pthread)

# Test executable for FTP server
add_executable(test_ftp_server
    test_ftp_server.cpp
    ../src/net/ftp/ftp_server.cpp
    ../src/utils/logger.cpp
)
target_include_directories(test_ftp_server PRIVATE ../include)
target_link_libraries(test_ftp_server PRIVATE pthread)

# Test executable for static file handler
add_executable(test_static_file_handler
    test_static_file_handler.cpp
    ../src/net/http/static_file_handler.cpp
    ../src/utils/logger.cpp
)
target_include_directories(test_static_file_handler PRIVATE ../include)
target_link_libraries(test_static_file_handler PRIVATE pthread)

# Test runner
add_executable(run_tests run_tests.cpp)
target_include_directories(run_tests PRIVATE ../include)
