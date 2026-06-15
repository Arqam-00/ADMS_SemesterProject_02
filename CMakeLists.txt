cmake_minimum_required(VERSION 3.10)
project(RaftKv)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Server executable
add_executable(raftkv
    src/server.cpp
)

# Client executable
add_executable(raftkv-cli
    src/client.cpp
)

# Link pthread for threading support
target_link_libraries(raftkv pthread)
target_link_libraries(raftkv-cli pthread)