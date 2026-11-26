#include "deserializer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include "test_utils.h"
#include <vector>
#include "../src/csv/csv_parser.h"
#include "../src/metastore/metastore.h"

namespace fs = std::filesystem;
using namespace isbd;

void test_mapping_validation(const fs::path& dir) {
    std::cout << "[1/4] Testing destinationColumns mapping validation... " << std::flush;
    Metastore ms(dir);

    std::vector<ValidationProblem> problems;
    auto table_id = ms.create_table("users", {
        {"id", DataType::INT64},
        {"username", DataType::VARCHAR},
        {"is_admin", DataType::BOOL}
    }, problems);
    CHECK(table_id.has_value());

    fs::path dummy_csv = dir / "dummy.csv";
    {
        std::ofstream out(dummy_csv);
        out << "1;alice;true\n";
    }

    auto meta = ms.get_table_by_id(*table_id);
    CHECK(meta.has_value());

    // Missing column in destinationColumns should fail
    problems.clear();
    CopyOptions opt_missing{dummy_csv, false, {"id", "username"}};
    CsvParser::validate_and_resolve_mapping(*meta, opt_missing, problems);
    CHECK(!problems.empty());

    // Unknown column name should fail
    problems.clear();
    CopyOptions opt_unknown{dummy_csv, false, {"id", "username", "nonexistent"}};
    CsvParser::validate_and_resolve_mapping(*meta, opt_unknown, problems);
    CHECK(!problems.empty());

    // Duplicate column name should fail
    problems.clear();
    CopyOptions opt_dup{dummy_csv, false, {"id", "id", "username"}};
    CsvParser::validate_and_resolve_mapping(*meta, opt_dup, problems);
    CHECK(!problems.empty());

    // Valid reordered mapping should succeed
    problems.clear();
    CopyOptions opt_valid{dummy_csv, false, {"is_admin", "id", "username"}};
    auto mapping = CsvParser::validate_and_resolve_mapping(*meta, opt_valid, problems);
    CHECK(problems.empty());
    CHECK(mapping.size() == 3);
    CHECK(mapping[0] == 2); // CSV col 0 ("is_admin") -> table col 2
    CHECK(mapping[1] == 0); // CSV col 1 ("id")       -> table col 0
    CHECK(mapping[2] == 1); // CSV col 2 ("username") -> table col 1

    std::cout << "PASSED\n";
}

void test_multibatch_ingest_and_deserialize_roundtrip(const fs::path& dir) {
    // Derive test size from Batch::BATCH_SIZE so multi-batch behavior is always exercised
    constexpr size_t FULL_BATCHES = (Batch::BATCH_SIZE == 1) ? 2 : 1;
    constexpr size_t REMAINDER_ROWS = (Batch::BATCH_SIZE > 1) ? (Batch::BATCH_SIZE / 4 + 1) : 0;
    constexpr size_t TOTAL_ROWS = FULL_BATCHES * Batch::BATCH_SIZE + REMAINDER_ROWS;
    constexpr size_t EXPECTED_BATCHES = FULL_BATCHES + (REMAINDER_ROWS > 0 ? 1 : 0);

    std::cout << "[2/4] Testing " << TOTAL_ROWS << "-row multi-batch CSV ingestion & Deserializer round-trip... "
              << std::flush;

    Metastore ms(dir);
    auto meta = ms.get_table_by_name("users");
    CHECK(meta.has_value());

    // Columns in CSV are ordered: is_admin;id;username (with header + Windows \r\n endings)
    fs::path csv_path = dir / "users_multibatch.csv";
    {
        std::ofstream out(csv_path, std::ios::binary);
        out << "is_admin;id;username\r\n";
        for (size_t i = 0; i < TOTAL_ROWS; ++i) {
            std::string admin_str = (i % 3 == 0) ? "true" : "0";
            std::string user_str = (i % 10 == 0) ? "" : ("user_" + std::to_string(i)); // Test empty VARCHAR too
            out << admin_str << ";" << (100000 + static_cast<int64_t>(i)) << ";" << user_str << "\r\n";
        }
    }

    fs::path output_isbd = ms.table_data_dir(meta->table_id) / "ingested_multibatch.isbd";
    CopyOptions options{
        csv_path,
        /*does_csv_contain_header=*/true,
        /*destination_columns=*/{"is_admin", "id", "username"}
    };

    CopyResult res = CsvParser::parse_to_isbd(*meta, options, output_isbd);
    CHECK(res.rows_ingested == TOTAL_ROWS);
    CHECK(res.batches_written == EXPECTED_BATCHES);
    CHECK(fs::exists(output_isbd));

    // Read the .isbd file back using Lab 2's Deserializer and verify every single row
    Deserializer deserializer(output_isbd.string());
    CHECK(deserializer.get_total_rows() == TOTAL_ROWS);

    const auto& schema = deserializer.get_schema();
    CHECK(schema.size() == 3);
    CHECK(schema[0] == DataType::INT64);   // id
    CHECK(schema[1] == DataType::VARCHAR); // username
    CHECK(schema[2] == DataType::BOOL);    // is_admin

    size_t verified_rows = 0;
    size_t batches_read = 0;
    while (deserializer.has_next_batch()) {
        Batch b = deserializer.read_next_batch();
        batches_read++;

        if (batches_read <= FULL_BATCHES) {
            CHECK(b.num_rows == Batch::BATCH_SIZE);
        } else {
            CHECK(b.num_rows == REMAINDER_ROWS);
        }

        const auto& col_id    = std::get<std::vector<int64_t>>(b.columns[0]);
        const auto& col_user  = std::get<std::vector<std::string>>(b.columns[1]);
        const auto& col_admin = std::get<std::vector<uint8_t>>(b.columns[2]);

        for (size_t r = 0; r < b.num_rows; ++r) {
            size_t global_idx = verified_rows + r;
            int64_t expected_id = 100000 + static_cast<int64_t>(global_idx);
            std::string expected_user = (global_idx % 10 == 0) ? "" : ("user_" + std::to_string(global_idx));
            uint8_t expected_admin = (global_idx % 3 == 0) ? 1 : 0;

            CHECK(col_id[r] == expected_id);
            CHECK(col_user[r] == expected_user);
            CHECK(col_admin[r] == expected_admin);
        }

        verified_rows += b.num_rows;
    }

    CHECK(batches_read == EXPECTED_BATCHES);
    CHECK(verified_rows == TOTAL_ROWS);

    std::cout << "PASSED\n";
}

void test_malformed_row_cleanup(const fs::path& dir) {
    std::cout << "[3/4] Testing malformed CSV error reporting & partial .isbd cleanup... " << std::flush;
    Metastore ms(dir);
    auto meta = ms.get_table_by_name("users");
    CHECK(meta.has_value());

    fs::path bad_csv = dir / "bad_rows.csv";
    {
        std::ofstream out(bad_csv);
        out << "1;alice;true\n";
        out << "not_an_int;bob;false\n"; // Line 2 has invalid INT64
    }

    fs::path failed_isbd = ms.table_data_dir(meta->table_id) / "should_not_exist.isbd";
    CopyOptions options{bad_csv, false, {}};

    bool threw_expected = false;
    try {
        CsvParser::parse_to_isbd(*meta, options, failed_isbd);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        if (msg.find("Line 2") != std::string::npos && msg.find("INT64") != std::string::npos) {
            threw_expected = true;
        }
    }

    CHECK(threw_expected);
    CHECK(!fs::exists(failed_isbd));
    CHECK(!fs::exists(failed_isbd.string() + ".tmp"));

    std::cout << "PASSED\n";
}

void test_empty_csv_with_header_only(const fs::path& dir) {
    std::cout << "[4/4] Testing header-only 0-row CSV edge case... " << std::flush;
    Metastore ms(dir);
    auto meta = ms.get_table_by_name("users");
    CHECK(meta.has_value());

    fs::path header_only_csv = dir / "header_only.csv";
    {
        std::ofstream out(header_only_csv);
        out << "id;username;is_admin\n";
    }

    fs::path out_isbd = ms.table_data_dir(meta->table_id) / "empty_rows.isbd";
    CopyOptions options{header_only_csv, true, {}};

    CopyResult res = CsvParser::parse_to_isbd(*meta, options, out_isbd);
    CHECK(res.rows_ingested == 0);
    CHECK(res.batches_written == 0);
    CHECK(fs::exists(out_isbd));

    Deserializer deserializer(out_isbd.string());
    CHECK(deserializer.get_total_rows() == 0);
    CHECK(!deserializer.has_next_batch());

    std::cout << "PASSED\n";
}

int main() {
    fs::path test_dir = fs::temp_directory_path() / "isbd_csv_parser_unit_test";
    fs::remove_all(test_dir);

    try {
        test_mapping_validation(test_dir);
        test_multibatch_ingest_and_deserialize_roundtrip(test_dir);
        test_malformed_row_cleanup(test_dir);
        test_empty_csv_with_header_only(test_dir);
    } catch (const std::exception& ex) {
        std::cerr << "\nTEST FAILED WITH EXCEPTION: " << ex.what() << "\n";
        fs::remove_all(test_dir);
        return 1;
    }

    fs::remove_all(test_dir);
    std::cout << "\nAll CsvParser + Deserializer round-trip tests passed successfully!\n";
    return 0;
}