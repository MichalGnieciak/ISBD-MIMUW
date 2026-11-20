#pragma once

#include "metastore/metastore.h"
#include "format.h"
#include "batch.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace isbd {

struct CopyOptions {
    std::filesystem::path source_file;
    bool does_csv_contain_header = false;
    // Optional explicit mapping from CSV column positions to target table column names.
    // If empty, CSV columns are assumed to match the table schema order 1-to-1.
    std::vector<std::string> destination_columns;
};

struct CopyResult {
    uint64_t rows_ingested = 0;
    uint32_t batches_written = 0;
    std::filesystem::path output_isbd_path;
};

class CsvParser {
public:
    // Synchronous validation step (used by Planner before launching async execution).
    // Returns a vector mapping csv_col_index -> table_col_index if valid, or fills `problems`.
    static std::vector<size_t> validate_and_resolve_mapping(
        const TableMetadata& table,
        const CopyOptions& options,
        std::vector<ValidationProblem>& problems);

    // Streams the CSV file in bounded BATCH_SIZE-row Batches directly into an .isbd file via Serializer.
    // Throws std::runtime_error with exact line/column diagnostics on malformed CSV data
    // and cleans up any partial .isbd file on failure.
    static CopyResult parse_to_isbd(
        const TableMetadata& table,
        const CopyOptions& options,
        const std::filesystem::path& output_isbd_path);

private:
    static std::vector<std::string> split_semicolon_line(const std::string& line);
    static int64_t parse_int64(const std::string& token, size_t line_num, const std::string& col_name);
    static uint8_t parse_bool(const std::string& token, size_t line_num, const std::string& col_name);
    static Batch make_empty_batch(const std::vector<DataType>& schema);
};

} // namespace isbd