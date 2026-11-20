#include "metastore/metastore.h"
#include "query/executor/executor.h"
#include "query/planner/planner.h"
#include "query/registry/query_registry.h"
#include "test_utils.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace isbd;

namespace {

QueryRecord wait_for_completion(const QueryRegistry& registry, const std::string& query_id) {
    using namespace std::chrono_literals;
    for (int i = 0; i < 500; ++i) {
        auto rec = registry.get_query(query_id);
        CHECK(rec.has_value());
        if (rec->status == QueryStatus::COMPLETED || rec->status == QueryStatus::FAILED) {
            return *rec;
        }
        std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error("Timed out waiting for query " + query_id);
}

} // namespace

void test_planner_validation_errors(const fs::path& dir) {
    std::cout << "[1/5] Testing synchronous Planner validation... " << std::flush;
    Metastore ms(dir);

    std::vector<ValidationProblem> problems;
    auto tid = ms.create_table("items", {
        {"id", DataType::INT64},
        {"label", DataType::VARCHAR}
    }, problems);
    CHECK(tid.has_value());

    // SELECT on missing table
    auto res1 = Planner::plan_select("q1", SelectRequest{"missing_table", {"id"}}, ms);
    CHECK(std::holds_alternative<std::vector<ValidationProblem>>(res1));

    // SELECT on missing column
    auto res2 = Planner::plan_select("q2", SelectRequest{"items", {"id", "no_such_col"}}, ms);
    CHECK(std::holds_alternative<std::vector<ValidationProblem>>(res2));

    // COPY from missing CSV file
    auto res3 = Planner::plan_copy("q3", CopyRequest{"items", dir / "ghost.csv", false, {}}, ms);
    CHECK(std::holds_alternative<std::vector<ValidationProblem>>(res3));

    std::cout << "PASSED\n";
}

void test_empty_table_select(const fs::path& dir) {
    std::cout << "[2/5] Testing async SELECT on empty table (0 .isbd files)... " << std::flush;
    Metastore ms(dir);
    QueryRegistry registry;
    Executor executor(ms, registry, 2);

    std::string qid = registry.create_query(QueryType::SELECT);
    auto plan_res = Planner::plan_select(qid, SelectRequest{"items", {"label", "id"}}, ms);
    CHECK(std::holds_alternative<ExecutionPlan>(plan_res));

    executor.submit(std::get<ExecutionPlan>(std::move(plan_res)));
    QueryRecord done = wait_for_completion(registry, qid);

    CHECK(done.status == QueryStatus::COMPLETED);
    CHECK(done.is_result_available);
    CHECK(done.rows_processed == 0);
    CHECK(done.select_result.has_value());
    CHECK(done.select_result->total_rows == 0);
    CHECK(done.select_result->batches.empty());

    std::cout << "PASSED\n";
}

void test_async_copy_and_projected_select(const fs::path& dir) {
    std::cout << "[3/5] Testing async COPY -> projected/reordered SELECT pipeline... " << std::flush;
    Metastore ms(dir);
    QueryRegistry registry;
    Executor executor(ms, registry, 2);

    fs::path csv_path = dir / "items.csv";
    {
        std::ofstream out(csv_path);
        out << "id;label\n";
        out << "10;alpha\n";
        out << "20;beta\n";
        out << "30;gamma\n";
    }

    // Submit async COPY
    std::string copy_qid = registry.create_query(QueryType::COPY);
    auto copy_plan_res = Planner::plan_copy(
        copy_qid, CopyRequest{"items", csv_path, true, {}}, ms);
    CHECK(std::holds_alternative<ExecutionPlan>(copy_plan_res));

    executor.submit(std::get<ExecutionPlan>(std::move(copy_plan_res)));
    QueryRecord copy_done = wait_for_completion(registry, copy_qid);
    CHECK(copy_done.status == QueryStatus::COMPLETED);
    CHECK(!copy_done.is_result_available);
    CHECK(copy_done.rows_processed == 3);

    // Submit async SELECT with reordered & duplicate columns: ["label", "id", "label"]
    std::string sel_qid = registry.create_query(QueryType::SELECT);
    auto sel_plan_res = Planner::plan_select(
        sel_qid, SelectRequest{"items", {"label", "id", "label"}}, ms);
    CHECK(std::holds_alternative<ExecutionPlan>(sel_plan_res));

    executor.submit(std::get<ExecutionPlan>(std::move(sel_plan_res)));
    QueryRecord sel_done = wait_for_completion(registry, sel_qid);
    CHECK(sel_done.status == QueryStatus::COMPLETED);
    CHECK(sel_done.is_result_available);
    CHECK(sel_done.rows_processed == 3);
    CHECK(sel_done.select_result.has_value());
    CHECK(sel_done.select_result->batches.size() == 1);

    const Batch& b = sel_done.select_result->batches[0];
    CHECK(b.num_rows == 3);
    const auto& col0_label = std::get<std::vector<std::string>>(b.columns[0]);
    const auto& col1_id    = std::get<std::vector<int64_t>>(b.columns[1]);
    const auto& col2_label = std::get<std::vector<std::string>>(b.columns[2]);

    CHECK(col0_label == (std::vector<std::string>{"alpha", "beta", "gamma"}));
    CHECK(col1_id == (std::vector<int64_t>{10, 20, 30}));
    CHECK(col2_label == (std::vector<std::string>{"alpha", "beta", "gamma"}));

    std::cout << "PASSED\n";
}

void test_select_snapshot_isolation_on_concurrent_delete(const fs::path& dir) {
    std::cout << "[4/5] Testing SELECT snapshot isolation when table is deleted mid-flight... " << std::flush;
    Metastore ms(dir);
    QueryRegistry registry;
    Executor executor(ms, registry, 2);

    auto meta = ms.get_table_by_name("items");
    CHECK(meta.has_value());
    std::string table_id = meta->table_id;

    // Plan SELECT while table still exists (captures shared_ptr<ManagedFile>)
    std::string sel_qid = registry.create_query(QueryType::SELECT);
    auto sel_plan_res = Planner::plan_select(sel_qid, SelectRequest{"items", {"id"}}, ms);
    CHECK(std::holds_alternative<ExecutionPlan>(sel_plan_res));

    // Delete the table BEFORE Executor runs the SELECT
    CHECK(ms.delete_table(table_id));
    CHECK(!ms.get_table_by_name("items").has_value());

    // Executor must still read the snapshotted .isbd file cleanly
    executor.submit(std::get<ExecutionPlan>(std::move(sel_plan_res)));
    QueryRecord sel_done = wait_for_completion(registry, sel_qid);
    CHECK(sel_done.status == QueryStatus::COMPLETED);
    CHECK(sel_done.rows_processed == 3);

    std::cout << "PASSED\n";
}

void test_copy_toctou_cleanup_on_concurrent_delete(const fs::path& dir) {
    std::cout << "[5/5] Testing COPY fail-fast & orphan cleanup when table is deleted mid-flight... " << std::flush;
    Metastore ms(dir);
    QueryRegistry registry;
    Executor executor(ms, registry, 2);

    std::vector<ValidationProblem> problems;
    auto tid = ms.create_table("ephemeral", {{"x", DataType::INT64}}, problems);
    CHECK(tid.has_value());

    fs::path csv_path = dir / "ephemeral.csv";
    {
        std::ofstream out(csv_path);
        out << "1\n2\n3\n";
    }

    std::string copy_qid = registry.create_query(QueryType::COPY);
    auto copy_plan_res = Planner::plan_copy(
        copy_qid, CopyRequest{"ephemeral", csv_path, false, {}}, ms);
    CHECK(std::holds_alternative<ExecutionPlan>(copy_plan_res));

    fs::path planned_target = std::get<CopyPlan>(std::get<ExecutionPlan>(copy_plan_res)).target_isbd_path;

    // Delete table after planning, before Executor commits
    CHECK(ms.delete_table(*tid));

    executor.submit(std::get<ExecutionPlan>(std::move(copy_plan_res)));
    QueryRecord copy_done = wait_for_completion(registry, copy_qid);

    CHECK(copy_done.status == QueryStatus::FAILED);
    CHECK(!copy_done.error_message.empty());
    CHECK(!fs::exists(planned_target));

    std::cout << "PASSED\n";
}

int main() {
    fs::path test_dir = fs::temp_directory_path() / "isbd_query_engine_unit_test";
    fs::remove_all(test_dir);

    try {
        test_planner_validation_errors(test_dir);
        test_empty_table_select(test_dir);
        test_async_copy_and_projected_select(test_dir);
        test_select_snapshot_isolation_on_concurrent_delete(test_dir);
        test_copy_toctou_cleanup_on_concurrent_delete(test_dir);
    } catch (const std::exception& ex) {
        std::cerr << "\nTEST FAILED WITH EXCEPTION: " << ex.what() << "\n";
        fs::remove_all(test_dir);
        return 1;
    }

    fs::remove_all(test_dir);
    std::cout << "\nAll QueryEngine tests passed successfully!\n";
    return 0;
}