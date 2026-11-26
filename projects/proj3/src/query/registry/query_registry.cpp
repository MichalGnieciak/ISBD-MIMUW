#include <algorithm>
#include <mutex>
#include "query_registry.h"
#include "utils/uid.h"

namespace isbd {

std::string QueryRegistry::create_query(QueryType type, std::string query_definition_json) {
    std::string id = generate_uid();
    register_query(id, type, std::move(query_definition_json));
    return id;
}

void QueryRegistry::register_query(
    const std::string& query_id,
    QueryType type,
    std::string query_definition_json) {

    std::unique_lock lock(mutex_);
    QueryRecord record;
    record.query_id = query_id;
    record.type = type;
    record.status = QueryStatus::CREATED;
    record.is_result_available = false;
    record.query_definition_json = std::move(query_definition_json);

    insertion_order_.push_back(query_id);
    queries_.emplace(query_id, std::move(record));
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

    if (problems.empty() && !error_message.empty()) {
        problems.push_back({error_message, ""});
    }

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

SelectResultData QueryRegistry::slice_result(
    const SelectResultData& source,
    std::optional<size_t> row_limit) {

    if (!row_limit.has_value() || *row_limit >= source.total_rows) {
        return source;
    }

    SelectResultData sliced;
    sliced.schema = source.schema;
    sliced.total_rows = 0;

    size_t remaining = *row_limit;
    for (const Batch& src_batch : source.batches) {
        if (remaining == 0) {
            break;
        }
        if (src_batch.num_rows == 0) {
            continue;
        }

        if (src_batch.num_rows <= remaining) {
            sliced.batches.push_back(src_batch);
            sliced.total_rows += src_batch.num_rows;
            remaining -= src_batch.num_rows;
        } else {
            // Partial batch truncation
            Batch partial;
            partial.num_rows = remaining;
            partial.columns.reserve(src_batch.columns.size());

            for (const ColumnData& col : src_batch.columns) {
                std::visit([&](const auto& vec) {
                    using VecType = std::decay_t<decltype(vec)>;
                    partial.columns.emplace_back(VecType(vec.begin(), vec.begin() + remaining));
                }, col);
            }

            sliced.total_rows += remaining;
            sliced.batches.push_back(std::move(partial));
            remaining = 0;
        }
    }

    return sliced;
}

ResultFetchStatus QueryRegistry::fetch_select_result(
    const std::string& query_id,
    std::optional<size_t> row_limit,
    bool flush_result,
    SelectResultData& out_result) {

    if (flush_result) {
        std::unique_lock lock(mutex_);
        auto it = queries_.find(query_id);
        if (it == queries_.end()) {
            return ResultFetchStatus::NOT_FOUND;
        }
        if (!it->second.is_result_available || !it->second.select_result.has_value()) {
            return ResultFetchStatus::NOT_AVAILABLE;
        }

        out_result = slice_result(*it->second.select_result, row_limit);
        it->second.select_result.reset();
        it->second.is_result_available = false;
        return ResultFetchStatus::SUCCESS;
    } else {
        std::shared_lock lock(mutex_);
        auto it = queries_.find(query_id);
        if (it == queries_.end()) {
            return ResultFetchStatus::NOT_FOUND;
        }
        if (!it->second.is_result_available || !it->second.select_result.has_value()) {
            return ResultFetchStatus::NOT_AVAILABLE;
        }

        out_result = slice_result(*it->second.select_result, row_limit);
        return ResultFetchStatus::SUCCESS;
    }
}

} // namespace isbd