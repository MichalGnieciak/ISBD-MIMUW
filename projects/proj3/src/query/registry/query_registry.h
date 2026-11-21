#pragma once
#include "batch.h"
#include "metastore/metastore.h"
#include "query/query_types.h"
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
    std::string create_query(QueryType type);

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

private:
    mutable std::shared_mutex mutex_;
    std::vector<std::string> insertion_order_;
    std::unordered_map<std::string, QueryRecord> queries_;
};

} // namespace isbd