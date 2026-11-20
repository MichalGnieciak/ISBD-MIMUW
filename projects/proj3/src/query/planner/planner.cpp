#include "planner.h"

#include <unordered_map>

namespace isbd {

PlanResult Planner::plan_copy(
    const std::string& query_id,
    const CopyRequest& request,
    const Metastore& metastore) {

    std::vector<ValidationProblem> problems;

    if (request.table_name.empty()) {
        problems.push_back({"Target table name cannot be empty", "destinationTableName"});
        return problems;
    }

    auto table_opt = metastore.get_table_by_name(request.table_name);
    if (!table_opt.has_value()) {
        problems.push_back({
            "Table '" + request.table_name + "' does not exist",
            "destinationTableName"
        });
        return problems;
    }

    const TableMetadata& table = *table_opt;

    CopyOptions options{
        request.source_file,
        request.does_csv_contain_header,
        request.destination_columns
    };

    std::vector<size_t> csv_to_table_idx =
        CsvParser::validate_and_resolve_mapping(table, options, problems);

    if (!problems.empty()) {
        return problems;
    }

    // Explicit bounds check on all resolved column indices
    const size_t num_table_cols = table.columns.size();
    for (size_t idx : csv_to_table_idx) {
        if (idx >= num_table_cols) {
            problems.push_back({
                "Mapped column index " + std::to_string(idx) + " is out of bounds",
                "destinationColumns"
            });
            return problems;
        }
    }

    CopyPlan plan;
    plan.query_id = query_id;
    plan.table_id = table.table_id;
    plan.table_name = table.name;
    plan.table_schema = table.columns;
    plan.source_csv_path = request.source_file;
    plan.does_csv_contain_header = request.does_csv_contain_header;
    plan.csv_to_table_idx = std::move(csv_to_table_idx);
    plan.target_isbd_path = metastore.table_data_dir(table.table_id) / ("part_" + query_id + ".isbd");

    return plan;
}

PlanResult Planner::plan_select(
    const std::string& query_id,
    const SelectRequest& request,
    const Metastore& metastore) {

    std::vector<ValidationProblem> problems;

    if (request.table_name.empty()) {
        problems.push_back({"Table name cannot be empty", "tableName"});
        return problems;
    }

    auto table_opt = metastore.get_table_by_name(request.table_name);
    if (!table_opt.has_value()) {
        problems.push_back({
            "Table '" + request.table_name + "' does not exist",
            "tableName"
        });
        return problems;
    }

    if (request.column_names.empty()) {
        problems.push_back({"SELECT requires at least one column in columnNames", "columnNames"});
        return problems;
    }

    const TableMetadata& table = *table_opt;
    const size_t num_table_cols = table.columns.size();

    std::unordered_map<std::string, size_t> col_lookup;
    col_lookup.reserve(num_table_cols);
    for (size_t i = 0; i < num_table_cols; ++i) {
        col_lookup[table.columns[i].name] = i;
    }

    SelectPlan plan;
    plan.query_id = query_id;
    plan.table_id = table.table_id;
    plan.table_name = table.name;
    plan.files = table.files; // Snapshots shared_ptr<ManagedFile> list under Metastore shared_lock
    plan.projected_col_indices.reserve(request.column_names.size());
    plan.output_schema.reserve(request.column_names.size());

    for (const auto& col_name : request.column_names) {
        auto it = col_lookup.find(col_name);
        if (it == col_lookup.end()) {
            problems.push_back({
                "Column '" + col_name + "' does not exist in table '" + table.name + "'",
                "columnNames"
            });
            continue;
        }

        size_t col_idx = it->second;
        if (col_idx >= num_table_cols) {
            problems.push_back({
                "Resolved column index " + std::to_string(col_idx) + " is out of bounds",
                "columnNames"
            });
            continue;
        }

        plan.projected_col_indices.push_back(col_idx);
        plan.output_schema.push_back(table.columns[col_idx]);
    }

    if (!problems.empty()) {
        return problems;
    }

    return plan;
}

} // namespace isbd