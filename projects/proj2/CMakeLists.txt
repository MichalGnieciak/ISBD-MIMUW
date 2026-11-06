cmake_minimum_required(VERSION 3.15)
project(DbFormat)
set(CMAKE_CXX_STANDARD 17)

# Flags for SIMD optimisiations
if(MSVC)
    add_compile_options(/O2 /arch:AVX2 /Qvec-report:2)
else()
    add_compile_options(-O3 -march=native -fopt-info-vec)
endif()

# ZSTD
find_package(zstd REQUIRED)

# Database engine library
add_library(DbCore
    src/compression/encoding.cpp
    src/compression/decoding.cpp
    src/utils/random_utils.cpp
    src/io/serializer.cpp
    src/io/deserializer.cpp
)
target_link_libraries(DbCore PUBLIC zstd::libzstd_shared)
target_include_directories(DbCore PUBLIC src/compression src/io src/utils)

# Executables

# Tools
# Sample generation
add_executable(generate_sample tools/generate_sample.cpp)
target_link_libraries(generate_sample PRIVATE DbCore)
# Metrics calculation
add_executable(analyze_db tools/analyze_db.cpp)
target_link_libraries(analyze_db PRIVATE DbCore)

# Tests
# Isolated Tests
add_executable(test_isolated test/test_isolated.cpp)
target_link_libraries(test_isolated PRIVATE DbCore)
# End-to-end Test
add_executable(test_e2e test/test_e2e.cpp)
target_link_libraries(test_e2e PRIVATE DbCore)