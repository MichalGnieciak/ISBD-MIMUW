#pragma once
#include "metastore/metastore.h"
#include "query/executor/executor.h"
#include "query/query_types.h"
#include "query/registry/query_registry.h"
#include <optional>
#include <string>
#include <variant>
#include <vector>
#include "../dto/common_dto.h"
#include "../dto/query_dto.h"

namespace isbd::api {

class ExecutionService {
public:
    ExecutionService(Metastore& metastore, QueryRegistry& registry, Executor& executor);

    std::vector<ShallowQueryDto> list_queries() const;

    std::variant<std::string, MultipleProblemsDto> submit_query(
        const ExecuteQueryRequestDto& request_dto);

    std::optional<QueryDetailDto> get_query_by_id(const std::string& query_id) const;

    std::variant<std::vector<BatchResultDto>, ResultFetchStatus> get_query_result(
        const std::string& query_id,
        const GetResultOptionsDto& options);

    std::variant<MultipleProblemsDto, ErrorFetchStatus> get_query_error(
    const std::string& query_id) const;

private:
    Metastore& metastore_;
    QueryRegistry& registry_;
    Executor& executor_;
};

} // namespace isbd::api