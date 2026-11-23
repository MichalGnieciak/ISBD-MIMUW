#pragma once
#include "metastore/metastore.h"
#include "query/query_types.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace isbd {

struct QueryRecord {
    std::string query_id;
    QueryType type;
    QueryStatus status = QueryStatus::CREATED;
    bool is_result_available = false;
    std::string query_definition_json;
    uint64_t rows_processed = 0;
    std::string error_message;
    std::vector<ValidationProblem> problems;
    std::optional<SelectResultData> select_result;
};

struct QuerySummary {
    std::string query_id;
    QueryType type;
    QueryStatus status;
    bool is_result_available = false;
};

class QueryRegistry {
public:
    QueryRegistry() = default;

    // Generates a unique query ID and registers it in CREATED state
    std::string create_query(QueryType type, std::string query_definition_json = "{}");

    // Registers a pre-generated query ID in CREATED state
    // so requests that fail semantic validation with 400 Bad Request do not pollute GET /queries
    void register_query(const std::string& query_id,
                        QueryType type,
                        std::string query_definition_json = "{}");

    // State transition helpers (exclusive lock)
    void set_status(const std::string& query_id, QueryStatus status);
    void complete_copy(const std::string& query_id, uint64_t rows_ingested);
    void complete_select(const std::string& query_id, SelectResultData&& result);
    void fail_query(const std::string& query_id,
                    const std::string& error_message,
                    std::vector<ValidationProblem> problems = {});

    // Thread-safe readers (shared lock)
    std::optional<QueryRecord> get_query(const std::string& query_id) const;
    std::vector<QuerySummary> list_queries() const;

    // Thread-safe SELECT result retrieval with optional rowLimit slicing and flushResult cleanup
    ResultFetchStatus fetch_select_result(
        const std::string& query_id,
        std::optional<size_t> row_limit,
        bool flush_result,
        SelectResultData& out_result);

private:
    static SelectResultData slice_result(const SelectResultData& source, std::optional<size_t> row_limit);

    mutable std::shared_mutex mutex_;
    std::vector<std::string> insertion_order_;
    std::unordered_map<std::string, QueryRecord> queries_;
};

} // namespace isbd