#include "execution_service.h"
#include "query/planner/planner.h"
#include "utils/uid.h"

namespace isbd::api {

ExecutionService::ExecutionService(Metastore& metastore, QueryRegistry& registry, Executor& executor)
    : metastore_(metastore),
      registry_(registry),
      executor_(executor) {}

std::vector<ShallowQueryDto> ExecutionService::list_queries() const {
    auto summaries = registry_.list_queries();
    std::vector<ShallowQueryDto> dtos;
    dtos.reserve(summaries.size());
    for (const auto& q : summaries) {
        dtos.push_back({q.query_id, q.status});
    }
    return dtos;
}

std::variant<std::string, MultipleProblemsDto> ExecutionService::submit_query(
    const ExecuteQueryRequestDto& request_dto) {
    std::string query_id = generate_uid();

    PlanResult plan_result = (request_dto.type == QueryType::COPY)
        ? Planner::plan_copy(
              query_id,
              std::get<CopyQueryDto>(request_dto.query_definition).to_domain(),
              metastore_)
        : Planner::plan_select(
              query_id,
              std::get<SelectQueryDto>(request_dto.query_definition).to_domain(),
              metastore_);

    if (std::holds_alternative<std::vector<ValidationProblem>>(plan_result)) {
        const auto& problems = std::get<std::vector<ValidationProblem>>(plan_result);
        return MultipleProblemsDto::from_validation_problems(problems);
    }

    registry_.register_query(query_id, request_dto.type, request_dto.raw_query_definition.dump());
    executor_.submit(std::get<ExecutionPlan>(std::move(plan_result)));

    return query_id;
}

std::optional<QueryDetailDto> ExecutionService::get_query_by_id(const std::string& query_id) const {
    auto record = registry_.get_query(query_id);
    if (!record.has_value()) {
        return std::nullopt;
    }
    return QueryDetailDto::from_record(*record);
}

std::variant<std::vector<BatchResultDto>, ResultFetchStatus> ExecutionService::get_query_result(
    const std::string& query_id,
    const GetResultOptionsDto& options) {
    SelectResultData result_data;
    ResultFetchStatus status = registry_.fetch_select_result(
        query_id,
        options.row_limit,
        options.flush_result,
        result_data);

    if (status != ResultFetchStatus::SUCCESS) {
        return status;
    }
    return BatchResultDto::from_select_result(result_data);
}

std::variant<MultipleProblemsDto, ErrorFetchStatus> ExecutionService::get_query_error(
    const std::string& query_id) const {
    auto record = registry_.get_query(query_id);
    if (!record.has_value()) {
        return ErrorFetchStatus::NOT_FOUND;
    }
    if (record->status != QueryStatus::FAILED) {
        return ErrorFetchStatus::NOT_FAILED;
    }
    return MultipleProblemsDto::from_validation_problems(record->problems);
}

} // namespace isbd::api