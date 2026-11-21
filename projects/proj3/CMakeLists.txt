cmake_minimum_required(VERSION 3.15)
project(IsbdDbmsServer)
set(CMAKE_CXX_STANDARD 17)

# Flags for SIMD operations
# for portable Docker images -O3 -mavx2 is safer than -march=native if building on one CPU and running on another.
if(MSVC)
    add_compile_options(/O2 /arch:AVX2)
else()
    add_compile_options(-O3 -mavx2)
endif()

find_package(Threads REQUIRED)
find_package(zstd REQUIRED)
find_package(nlohmann_json REQUIRED)

# Libraries
# Storage Engine (DbCore)
set(PROJ2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../proj2")
add_library(DbCore STATIC
    "${PROJ2_DIR}/src/compression/encoding.cpp"
    "${PROJ2_DIR}/src/compression/decoding.cpp"
    "${PROJ2_DIR}/src/utils/random_utils.cpp"
    "${PROJ2_DIR}/src/io/batch.cpp"
    "${PROJ2_DIR}/src/io/serializer.cpp"
    "${PROJ2_DIR}/src/io/deserializer.cpp"
)
target_include_directories(DbCore PUBLIC
    "${PROJ2_DIR}/src/compression"
    "${PROJ2_DIR}/src/io"
    "${PROJ2_DIR}/src/utils"
)
target_link_libraries(DbCore PUBLIC zstd::libzstd_shared)
# Utils
add_library(Utils STATIC src/utils/uid.cpp)
target_include_directories(Utils PUBLIC src)
# Metastore
add_library(Metastore STATIC src/metastore/metastore.cpp)
target_include_directories(Metastore PUBLIC src)
target_link_libraries(Metastore PUBLIC
    Utils
    DbCore
    Threads::Threads
    nlohmann_json::nlohmann_json
)
# CSV parser
add_library(CsvParser STATIC src/csv/csv_parser.cpp)
target_include_directories(CsvParser PUBLIC src)
target_link_libraries(CsvParser PUBLIC
    Metastore
    DbCore
)
# Query Engine (Planner + Executor + Registry)
add_library(QueryEngine STATIC
    src/query/query_types.cpp
    src/query/planner/planner.cpp
    src/query/registry/query_registry.cpp
    src/query/executor/executor.cpp
)
target_include_directories(QueryEngine PUBLIC src)
target_link_libraries(QueryEngine PUBLIC
    Metastore
    CsvParser
    DbCore
    Utils
    Threads::Threads
)

# Executables
add_executable(isbd_server src/main.cpp)
target_include_directories(isbd_server PRIVATE src)
target_link_libraries(isbd_server PRIVATE
    QueryEngine
    Threads::Threads
    nlohmann_json::nlohmann_json
    cpp-httplib
)

# Tests
enable_testing()
# Metastore test
add_executable(test_metastore test/test_metastore.cpp)
target_link_libraries(test_metastore PRIVATE Metastore)
add_test(NAME MetastoreTests COMMAND test_metastore)
# CSV parser test
add_executable(test_csv_parser test/test_csv_parser.cpp)
target_link_libraries(test_csv_parser PRIVATE CsvParser)
add_test(NAME CsvParserTests COMMAND test_csv_parser)
# Query engine test
add_executable(test_query_engine test/test_query_engine.cpp)
target_link_libraries(test_query_engine PRIVATE QueryEngine)
add_test(NAME QueryEngineTests COMMAND test_query_engine)