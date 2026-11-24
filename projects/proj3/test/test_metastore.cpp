#include <filesystem>
#include <fstream>
#include <iostream>
#include "metastore/metastore.h"
#include <stdexcept>
#include <string>
#include "test_utils.h"
#include <vector>

namespace fs = std::filesystem;
using namespace isbd;

void test_create_and_validation(const fs::path& dir) {
    std::cout << "[1/5] Testing table creation & validation... " << std::flush;
    Metastore ms(dir);

    std::vector<ValidationProblem> problems;
    auto id1 = ms.create_table("users", {
        {"id", DataType::INT64},
        {"username", DataType::VARCHAR},
        {"active", DataType::BOOL}
    }, problems);

    CHECK(id1.has_value());
    CHECK(problems.empty());

    problems.clear();
    auto id_dup = ms.create_table("users", {{"id", DataType::INT64}}, problems);
    CHECK(!id_dup.has_value());
    CHECK(!problems.empty());

    problems.clear();
    auto id_dup_col = ms.create_table("orders", {
        {"id", DataType::INT64},
        {"id", DataType::VARCHAR}
    }, problems);
    CHECK(!id_dup_col.has_value());
    CHECK(!problems.empty());

    std::cout << "PASSED\n";
}

void test_persistence_across_restart(const fs::path& dir) {
    std::cout << "[2/5] Testing persistence across simulated server restart... " << std::flush;
    std::string saved_id;
    fs::path dummy_isbd;

    {
        Metastore ms(dir);
        std::vector<ValidationProblem> problems;
        auto id = ms.create_table("events", {
            {"ts", DataType::INT64},
            {"payload", DataType::VARCHAR}
        }, problems);
        CHECK(id.has_value());
        saved_id = *id;

        dummy_isbd = ms.table_data_dir(saved_id) / "batch_0.isbd";
        {
            std::ofstream out(dummy_isbd, std::ios::binary);
            out << "DUMMY_ISBD_BYTES";
        }

        bool committed = ms.commit_copy_files(saved_id, {dummy_isbd});
        CHECK(committed);
    }

    {
        Metastore ms_restarted(dir);
        auto tables = ms_restarted.list_tables();
        CHECK(tables.size() == 2);

        auto meta = ms_restarted.get_table_by_name("events");
        CHECK(meta.has_value());
        CHECK(meta->table_id == saved_id);
        CHECK(meta->columns.size() == 2);
        CHECK(meta->columns[0].name == "ts");
        CHECK(meta->columns[0].type == DataType::INT64);
        CHECK(meta->files.size() == 1);
        CHECK(fs::exists(meta->files[0]->path()));
    }

    std::cout << "PASSED\n";
}

void test_deferred_deletion_during_active_select(const fs::path& dir) {
    std::cout << "[3/5] Testing RAII deferred file deletion while SELECT is active... " << std::flush;
    Metastore ms(dir);

    auto meta = ms.get_table_by_name("events");
    CHECK(meta.has_value());
    CHECK(meta->files.size() == 1);

    std::string table_id = meta->table_id;
    fs::path file_path = meta->files[0]->path();
    CHECK(fs::exists(file_path));

    // Simulate an active SELECT execution plan snapshotting the ManagedFile shared_ptr
    std::vector<std::shared_ptr<ManagedFile>> active_select_snapshot = meta->files;
    meta.reset();

    // Delete the table via DELETE /table/{id} while SELECT is still running
    CHECK(ms.delete_table(table_id));

    // 1. Table must immediately disappear from Metastore catalog
    CHECK(!ms.get_table_by_name("events").has_value());

    // 2. Physical .isbd file MUST still exist on disk because SELECT is still reading it!
    CHECK(fs::exists(file_path));

    // 3. Finish the SELECT query (drop the last shared_ptr) -> destructor deletes the file!
    active_select_snapshot.clear();
    CHECK(!fs::exists(file_path));

    std::cout << "PASSED\n";
}

void test_leftover_tmp_file_cleanup(const fs::path& dir) {
    std::cout << "[4/5] Testing leftover .tmp file cleanup on startup... " << std::flush;
    fs::path tmp_path = dir / "metastore.json.tmp";
    {
        std::ofstream out(tmp_path);
        out << "{ corrupted half-written crash garbage";
    }
    CHECK(fs::exists(tmp_path));

    Metastore ms(dir);
    CHECK(!fs::exists(tmp_path));
    CHECK(ms.get_table_by_name("users").has_value());

    std::cout << "PASSED\n";
}

void test_crc32_corruption_detection(const fs::path& dir) {
    std::cout << "[5/5] Testing CRC32 corruption detection... " << std::flush;
    fs::path meta_path = dir / "metastore.json";

    std::string content;
    {
        std::ifstream in(meta_path);
        content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    auto pos = content.find("users");
    CHECK(pos != std::string::npos);
    content[pos] = 'X';
    {
        std::ofstream out(meta_path, std::ios::trunc);
        out << content;
    }

    bool caught_corruption = false;
    try {
        Metastore ms(dir);
    } catch (const std::runtime_error& e) {
        std::string msg = e.what();
        if (msg.find("CRC32") != std::string::npos) {
            caught_corruption = true;
        }
    }
    CHECK(caught_corruption);

    std::cout << "PASSED\n";
}

int main() {
    fs::path test_dir = fs::temp_directory_path() / "isbd_metastore_unit_test";
    fs::remove_all(test_dir);

    try {
        test_create_and_validation(test_dir);
        test_persistence_across_restart(test_dir);
        test_deferred_deletion_during_active_select(test_dir);
        test_leftover_tmp_file_cleanup(test_dir);
        test_crc32_corruption_detection(test_dir);
    } catch (const std::exception& ex) {
        std::cerr << "\nTEST FAILED WITH EXCEPTION: " << ex.what() << "\n";
        fs::remove_all(test_dir);
        return 1;
    }

    fs::remove_all(test_dir);
    std::cout << "\nAll Metastore tests passed successfully!\n";
    return 0;
}