# Examples CMakeLists.txt

# Full server demo
add_executable(full_server_demo
    full_server_demo.cpp
)
target_include_directories(full_server_demo PRIVATE ../include)
target_link_libraries(full_server_demo PRIVATE webserver pthread)

# Logger demo
add_executable(logger_demo
    logger_demo.cpp
    ../src/utils/logger.cpp
)
target_include_directories(logger_demo PRIVATE ../include)
target_link_libraries(logger_demo PRIVATE pthread)

# Storage demo
add_executable(storage_demo
    storage_demo.cpp
    ../src/storage/storage_engine.cpp
    ../src/utils/logger.cpp
)
target_include_directories(storage_demo PRIVATE ../include)
target_link_libraries(storage_demo PRIVATE pthread)
