cmake_minimum_required(VERSION 3.10)
project(HighPerfSQLEngine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(kv_server 
    src/disk_manager.cpp 
    src/buffer_pool_manager.cpp 
    src/server.cpp
)

target_link_libraries(kv_server PRIVATE pthread)
