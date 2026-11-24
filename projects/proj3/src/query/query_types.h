#pragma once
#include "batch.h"
#include <cstdint>
#include <filesystem>
#include "metastore/metastore.h"
#include <string>
#include <vector>

namespace isbd {

enum class QueryType {
    COPY,
    SELECT
};

enum class QueryStatus {
    CREATED,
    PLANNING,
    RUNNING,
    COMPLETED,
    FAILED
};

enum class ResultFetchStatus {
    SUCCESS,
    NOT_FOUND,
    NOT_AVAILABLE
};

enum class ErrorFetchStatus {
    NOT_FOUND,
    NOT_FAILED
};

std::string query_type_to_string(QueryType type);
std::string query_status_to_string(QueryStatus status);

struct CopyRequest {
    std::string table_name;
    std::filesystem::path source_file;
    bool does_csv_contain_header = false;
    // Empty = default 1:1 table column order
    std::vector<std::string> destination_columns; 
};

struct SelectRequest {
    std::string table_name;
    std::vector<std::string> column_names;
};

struct SelectResultData {
    std::vector<ColumnSchema> schema;
    std::vector<Batch> batches;
    uint64_t total_rows = 0;
};

} // namespace isbd