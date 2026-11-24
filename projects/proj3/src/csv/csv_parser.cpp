#include <algorithm>
#include <cctype>
#include "csv_parser.h"
#include <fstream>
#include "serializer.h"
#include <stdexcept>
#include <unordered_map>

namespace isbd {

std::vector<size_t> CsvParser::validate_and_resolve_mapping(
    const TableMetadata& table,
    const CopyOptions& options,
    std::vector<ValidationProblem>& problems) {

    std::vector<size_t> csv_to_table_idx;
    const size_t num_table_cols = table.columns.size();

    // Checks if file exists
    if (options.source_file.empty()) {
        problems.push_back({"sourceFile cannot be empty", "sourceFile"});
    } else if (!std::filesystem::exists(options.source_file) ||
               !std::filesystem::is_regular_file(options.source_file)) {
        problems.push_back({
            "CSV source file does not exist or is not a regular file: " + options.source_file.string(),
            "sourceFile"
        });
    }

    // Default 1-1 positional mapping matching table creation order
    if (options.destination_columns.empty()) {
        csv_to_table_idx.resize(num_table_cols);
        for (size_t i = 0; i < num_table_cols; ++i) {
            csv_to_table_idx[i] = i;
        }
        return csv_to_table_idx;
    }

    // Checks if the mapping size matches the columnn count
    if (options.destination_columns.size() != num_table_cols) {
        problems.push_back({
            "destinationColumns count (" + std::to_string(options.destination_columns.size()) +
            ") must match table column count (" + std::to_string(num_table_cols) + ")",
            "destinationColumns"
        });
        return {};
    }

    std::unordered_map<std::string, size_t> table_col_lookup;
    for (size_t i = 0; i < num_table_cols; ++i) {
        table_col_lookup[table.columns[i].name] = i;
    }

    std::vector<bool> seen(num_table_cols, false);
    csv_to_table_idx.reserve(num_table_cols);

    // Checks if names of the columns match and dont repeat
    for (const auto& dest_name : options.destination_columns) {
        auto it = table_col_lookup.find(dest_name);
        if (it == table_col_lookup.end()) {
            problems.push_back({
                "Column '" + dest_name + "' does not exist in table '" + table.name + "'",
                "destinationColumns"
            });
            continue;
        }
        size_t target_idx = it->second;
        if (seen[target_idx]) {
            problems.push_back({
                "Duplicate column '" + dest_name + "' in destinationColumns",
                "destinationColumns"
            });
            continue;
        }
        seen[target_idx] = true;
        csv_to_table_idx.push_back(target_idx);
    }

    return problems.empty() ? csv_to_table_idx : std::vector<size_t>{};
}

std::vector<std::string> CsvParser::split_semicolon_line(const std::string& raw_line) {
    std::string line = raw_line;
    if (!line.empty() && line.back() == '\r') {
        // Strip Windows CRLF carriage return
        line.pop_back(); 
    }

    std::vector<std::string> tokens;
    size_t start = 0;
    while (true) {
        size_t pos = line.find(';', start);
        if (pos == std::string::npos) {
            tokens.emplace_back(line.substr(start));
            break;
        }
        tokens.emplace_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return tokens;
}

int64_t CsvParser::parse_int64(const std::string& token, size_t line_num, const std::string& col_name) {
    if (token.empty()) {
        throw std::runtime_error(
            "Line " + std::to_string(line_num) + ", column '" + col_name + "': empty value for INT64");
    }
    try {
        size_t processed = 0;
        long long val = std::stoll(token, &processed, 10);
        if (processed != token.size()) {
            throw std::invalid_argument("trailing characters");
        }
        return static_cast<int64_t>(val);
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Line " + std::to_string(line_num) + ", column '" + col_name +
            "': invalid INT64 value '" + token + "'");
    }
}

uint8_t CsvParser::parse_bool(const std::string& token, size_t line_num, const std::string& col_name) {
    std::string lower;
    lower.reserve(token.size());
    for (unsigned char c : token) {
        lower.push_back(static_cast<char>(std::tolower(c)));
    }

    if (lower == "true" || lower == "1") {
        return 1;
    }
    if (lower == "false" || lower == "0") {
        return 0;
    }
    throw std::runtime_error(
        "Line " + std::to_string(line_num) + ", column '" + col_name +
        "': invalid BOOL value '" + token + "' (expected true/false or 1/0)");
}

CopyResult CsvParser::parse_to_isbd(
    const TableMetadata& table,
    const CopyOptions& options,
    const std::filesystem::path& output_isbd_path) {

    std::vector<ValidationProblem> problems;
    std::vector<size_t> csv_to_table_idx = validate_and_resolve_mapping(table, options, problems);
    if (!problems.empty()) {
        throw std::runtime_error("Invalid COPY parameters: " + problems.front().error);
    }

    std::ifstream in(options.source_file, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Failed to open CSV file: " + options.source_file.string());
    }

    const size_t num_cols = table.columns.size();
    std::vector<DataType> storage_schema;
    storage_schema.reserve(num_cols);
    for (const auto& col : table.columns) {
        storage_schema.push_back(col.type);
    }

    CopyResult result;
    result.output_isbd_path = output_isbd_path;

    try {
        std::filesystem::create_directories(output_isbd_path.parent_path());
        Serializer serializer(output_isbd_path.string(), storage_schema);

        std::string line;
        size_t line_num = 0;

        if (options.does_csv_contain_header) {
            if (!std::getline(in, line)) {
                throw std::runtime_error("CSV file is empty but doesCsvContainHeader is true");
            }
            ++line_num;
            auto header_tokens = split_semicolon_line(line);
            if (header_tokens.size() != num_cols) {
                throw std::runtime_error(
                    "Header column count (" + std::to_string(header_tokens.size()) +
                    ") does not match table column count (" + std::to_string(num_cols) + ")");
            }
        }

        Batch current_batch = Batch::create_empty(storage_schema, Batch::BATCH_SIZE);

        while (std::getline(in, line)) {
            ++line_num;

            // Skip trailing empty newline at EOF
            if (line.empty() && in.eof()) {
                break;
            }

            std::vector<std::string> tokens = split_semicolon_line(line);
            if (tokens.size() != num_cols) {
                throw std::runtime_error(
                    "Line " + std::to_string(line_num) + ": expected " + std::to_string(num_cols) +
                    " columns, got " + std::to_string(tokens.size()));
            }

            for (size_t csv_idx = 0; csv_idx < num_cols; ++csv_idx) {
                size_t table_idx = csv_to_table_idx[csv_idx];
                const auto& col_schema = table.columns[table_idx];
                const std::string& token = tokens[csv_idx];

                switch (col_schema.type) {
                    case DataType::INT64: {
                        int64_t val = parse_int64(token, line_num, col_schema.name);
                        std::get<std::vector<int64_t>>(current_batch.columns[table_idx]).push_back(val);
                        break;
                    }
                    case DataType::VARCHAR: {
                        std::get<std::vector<std::string>>(current_batch.columns[table_idx]).push_back(token);
                        break;
                    }
                    case DataType::BOOL: {
                        uint8_t val = parse_bool(token, line_num, col_schema.name);
                        std::get<std::vector<uint8_t>>(current_batch.columns[table_idx]).push_back(val);
                        break;
                    }
                }
            }

            current_batch.num_rows++;
            result.rows_ingested++;

            if (current_batch.num_rows == Batch::BATCH_SIZE) {
                serializer.write_batch(current_batch);
                result.batches_written++;
                current_batch = Batch::create_empty(storage_schema, Batch::BATCH_SIZE);
            }
        }

        if (current_batch.num_rows > 0) {
            serializer.write_batch(current_batch);
            result.batches_written++;
        }

        serializer.close();
    } catch (...) {
        // Ensure no orphaned partial .isbd or .tmp file remains if ingestion fails mid-stream
        std::error_code ec;
        std::filesystem::remove(output_isbd_path, ec);
        std::filesystem::remove(output_isbd_path.string() + ".tmp", ec);
        throw;
    }

    return result;
}

} // namespace isbd