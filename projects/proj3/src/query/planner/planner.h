#pragma once
#include "csv/csv_parser.h"
#include "metastore/metastore.h"
#include "query/query_types.h"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace isbd {

// Immutable execution plan for COPY
struct CopyPlan {
    std::string query_id;
    std::string table_id;
    std::string table_name;
    std::vector<ColumnSchema> table_schema;
    std::filesystem::path source_csv_path;
    bool does_csv_contain_header = false;
    std::vector<size_t> csv_to_table_idx;
    std::filesystem::path target_isbd_path;
};

// Immutable execution plan for SELECT
struct SelectPlan {
    std::string query_id;
    std::string table_id;
    std::string table_name;
    std::vector<std::shared_ptr<ManagedFile>> files; // RAII snapshot preventing premature deletion
    std::vector<size_t> projected_col_indices;       // Output col i -> table col index
    std::vector<ColumnSchema> output_schema;         // Output col i -> name + DataType
};

using ExecutionPlan = std::variant<CopyPlan, SelectPlan>;
using PlanResult = std::variant<ExecutionPlan, std::vector<ValidationProblem>>;

class Planner {
public:
    // Synchronously validates a COPY request against Metastore & filesystem and builds an immutable CopyPlan.
    static PlanResult plan_copy(
        const std::string& query_id,
        const CopyRequest& request,
        const Metastore& metastore);

    // Synchronously validates a SELECT request against Metastore and builds an immutable SelectPlan.
    static PlanResult plan_select(
        const std::string& query_id,
        const SelectRequest& request,
        const Metastore& metastore);
};

} // namespace isbd