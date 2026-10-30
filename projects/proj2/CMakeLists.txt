cmake_minimum_required(VERSION 3.15)
project(DbFormat)
set(CMAKE_CXX_STANDARD 17)

# ZSTD
find_package(zstd REQUIRED)

# Database engine library
add_library(DbCore
    src/compression/encoding.cpp
    src/compression/decoding.cpp
    src/utils/random_utils.cpp
    src/io/serializer.cpp
)
target_link_libraries(DbCore PUBLIC zstd::libzstd_shared)
target_include_directories(DbCore PUBLIC src/compression src/io src/utils)

# Executables
# Tools
add_executable(generate_sample tools/generate_sample.cpp)
target_link_libraries(generate_sample PRIVATE DbCore)
# Isolated Tests
add_executable(test_isolated test/test_isolated.cpp)
target_link_libraries(test_isolated PRIVATE DbCore)