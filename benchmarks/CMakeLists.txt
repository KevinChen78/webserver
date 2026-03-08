# Benchmarks CMakeLists.txt

# Storage benchmark
add_executable(storage_bench
    storage_bench.cpp
    ../src/storage/storage_engine.cpp
    ../src/utils/logger.cpp
)
target_include_directories(storage_bench PRIVATE ../include)
target_link_libraries(storage_bench PRIVATE pthread)

# Webbench HTTP benchmark tool
add_executable(webbench
    webbench.cpp
)
target_include_directories(webbench PRIVATE ../include)

if(WIN32)
    target_link_libraries(webbench PRIVATE ws2_32)
else()
    target_link_libraries(webbench PRIVATE pthread)
endif()
