#include "query_registry.h"
#include "utils/uid.h"
#include <mutex>

namespace isbd {

std::string query_type_to_string(QueryType type) {
    switch (type) {
        case QueryType::COPY:   return "COPY";
        case QueryType::SELECT: return "SELECT";
    }
    return "UNKNOWN";
}

std::string query_status_to_string(QueryStatus status) {
    switch (status) {
        case QueryStatus::CREATED:   return "CREATED";
        case QueryStatus::PLANNING:  return "PLANNING";
        case QueryStatus::RUNNING:   return "RUNNING";
        case QueryStatus::COMPLETED: return "COMPLETED";
        case QueryStatus::FAILED:    return "FAILED";
    }
    return "UNKNOWN";
}

std::string QueryRegistry::create_query(QueryType type) {
    std::string id = generate_uid();

    std::unique_lock lock(mutex_);
    QueryRecord record;
    record.query_id = id;
    record.type = type;
    record.status = QueryStatus::CREATED;
    record.is_result_available = false;

    insertion_order_.push_back(id);
    queries_.emplace(id, std::move(record));
    return id;
}

void QueryRegistry::set_status(const std::string& query_id, QueryStatus status) {
    std::unique_lock lock(mutex_);
    auto it = queries_.find(query_id);
    if (it != queries_.end()) {
        it->second.status = status;
    }
}

void QueryRegistry::complete_copy(const std::string& query_id, uint64_t rows_ingested) {
    std::unique_lock lock(mutex_);
    auto it = queries_.find(query_id);
    if (it != queries_.end()) {
        it->second.status = QueryStatus::COMPLETED;
        it->second.is_result_available = false;
        it->second.rows_processed = rows_ingested;
    }
}

void QueryRegistry::complete_select(const std::string& query_id, SelectResultData&& result) {
    std::unique_lock lock(mutex_);
    auto it = queries_.find(query_id);
    if (it != queries_.end()) {
        it->second.status = QueryStatus::COMPLETED;
        it->second.is_result_available = true;
        it->second.rows_processed = result.total_rows;
        it->second.select_result = std::move(result);
    }
}

void QueryRegistry::fail_query(
    const std::string& query_id,
    const std::string& error_message,
    std::vector<ValidationProblem> problems) {

    std::unique_lock lock(mutex_);
    auto it = queries_.find(query_id);
    if (it != queries_.end()) {
        it->second.status = QueryStatus::FAILED;
        it->second.is_result_available = false;
        it->second.error_message = error_message;
        it->second.problems = std::move(problems);
    }
}

std::optional<QueryRecord> QueryRegistry::get_query(const std::string& query_id) const {
    std::shared_lock lock(mutex_);
    auto it = queries_.find(query_id);
    if (it == queries_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<QuerySummary> QueryRegistry::list_queries() const {
    std::shared_lock lock(mutex_);
    std::vector<QuerySummary> summaries;
    summaries.reserve(insertion_order_.size());

    for (const auto& id : insertion_order_) {
        const auto& rec = queries_.at(id);
        summaries.push_back({rec.query_id, rec.type, rec.status, rec.is_result_available});
    }
    return summaries;
}

} // namespace isbd