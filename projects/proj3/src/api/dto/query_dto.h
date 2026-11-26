#pragma once
#include "common_dto.h"
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include "query/registry/query_registry.h"
#include <string>
#include <variant>
#include <vector>
#include "../../query/query_types.h"

namespace isbd::api {

struct CopyQueryDto {
    std::string source_filepath;
    std::string destination_table_name;
    std::vector<std::string> destination_columns;
    bool does_csv_contain_header = false;

    static bool parse(const nlohmann::json& qdef,
                      CopyQueryDto& out,
                      std::vector<ProblemItemDto>& problems);

    CopyRequest to_domain() const;
};

struct ColumnReferenceDto {
    std::string table_name;
    std::string column_name;
};

struct SelectQueryDto {
    std::vector<ColumnReferenceDto> column_clauses;

    static bool parse(const nlohmann::json& qdef,
                      SelectQueryDto& out,
                      std::vector<ProblemItemDto>& problems);

    SelectRequest to_domain() const;
};

struct ExecuteQueryRequestDto {
    QueryType type;
    std::variant<CopyQueryDto, SelectQueryDto> query_definition;
    nlohmann::json raw_query_definition;

    static bool parse(const nlohmann::json& j,
                      ExecuteQueryRequestDto& out,
                      std::vector<ProblemItemDto>& problems);
};

struct ShallowQueryDto {
    std::string query_id;
    QueryStatus status;
};

struct QueryDetailDto {
    std::string query_id;
    QueryStatus status;
    bool is_result_available = false;
    nlohmann::json query_definition;

    static QueryDetailDto from_record(const QueryRecord& record);
};

struct BatchResultDto {
    size_t row_count = 0;
    std::vector<ColumnData> columns;

    static std::vector<BatchResultDto> from_select_result(const SelectResultData& result);
};

struct GetResultOptionsDto {
    std::optional<size_t> row_limit;
    bool flush_result = false;

    static bool parse(const std::string& raw_body,
                      GetResultOptionsDto& out,
                      std::string& out_error);
};

void to_json(nlohmann::json& j, const ShallowQueryDto& dto);
void to_json(nlohmann::json& j, const QueryDetailDto& dto);
void to_json(nlohmann::json& j, const BatchResultDto& dto);

} // namespace isbd::api