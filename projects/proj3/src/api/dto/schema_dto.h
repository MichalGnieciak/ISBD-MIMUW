#pragma once
#include "api/dto/common_dto.h"
#include "metastore/metastore.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace isbd::api {

struct ColumnDto {
    std::string name;
    DataType type;
};

struct TableSchemaDto {
    std::string name;
    std::vector<ColumnDto> columns;

    static bool parse(const nlohmann::json& j,
                      TableSchemaDto& out,
                      std::vector<ProblemItemDto>& problems);

    static TableSchemaDto from_metadata(const TableMetadata& meta);
    std::vector<ColumnSchema> to_domain_columns() const;
};

struct ShallowTableDto {
    std::string table_id;
    std::string name;
};

void to_json(nlohmann::json& j, const ColumnDto& dto);
void to_json(nlohmann::json& j, const TableSchemaDto& dto);
void to_json(nlohmann::json& j, const ShallowTableDto& dto);

} // namespace isbd::api