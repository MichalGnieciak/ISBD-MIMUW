#include "schema_dto.h"

namespace isbd::api {

bool TableSchemaDto::parse(const nlohmann::json& j,
                           TableSchemaDto& out,
                           std::vector<ProblemItemDto>& problems) {
    if (!j.is_object()) {
        problems.push_back({"Request body must be a JSON object", ""});
        return false;
    }

    if (!j.contains("name") || !j["name"].is_string() || j["name"].get<std::string>().empty()) {
        problems.push_back({"Table 'name' is required and must be a non-empty string", "name"});
    } else {
        out.name = j["name"].get<std::string>();
    }

    if (!j.contains("columns") || !j["columns"].is_array() || j["columns"].empty()) {
        problems.push_back({"Table 'columns' is required and must be a non-empty array", "columns"});
        return false;
    }

    out.columns.clear();
    out.columns.reserve(j["columns"].size());

    for (size_t i = 0; i < j["columns"].size(); ++i) {
        const auto& col_json = j["columns"][i];
        std::string ctx = "columns[" + std::to_string(i) + "]";

        if (!col_json.is_object()) {
            problems.push_back({"Column definition must be an object", ctx});
            continue;
        }

        if (!col_json.contains("name") || !col_json["name"].is_string() ||
            col_json["name"].get<std::string>().empty()) {
            problems.push_back({"Column 'name' must be a non-empty string", ctx + ".name"});
            continue;
        }

        if (!col_json.contains("type") || !col_json["type"].is_string()) {
            problems.push_back({"Column 'type' is required and must be 'INT64' or 'VARCHAR'", ctx + ".type"});
            continue;
        }

        std::string type_str = col_json["type"].get<std::string>();
        auto parsed_type = string_to_data_type(type_str);
        if (!parsed_type.has_value()) {
            problems.push_back({
                "Unsupported column type '" + type_str + "' (expected INT64 or VARCHAR)",
                ctx + ".type"
            });
            continue;
        }

        out.columns.push_back({col_json["name"].get<std::string>(), *parsed_type});
    }

    return problems.empty();
}

TableSchemaDto TableSchemaDto::from_metadata(const TableMetadata& meta) {
    TableSchemaDto dto;
    dto.name = meta.name;
    dto.columns.reserve(meta.columns.size());
    for (const auto& col : meta.columns) {
        dto.columns.push_back({col.name, col.type});
    }
    return dto;
}

std::vector<ColumnSchema> TableSchemaDto::to_domain_columns() const {
    std::vector<ColumnSchema> result;
    result.reserve(columns.size());
    for (const auto& col : columns) {
        result.push_back({col.name, col.type});
    }
    return result;
}

void to_json(nlohmann::json& j, const ColumnDto& dto) {
    j = nlohmann::json{
        {"name", dto.name},
        {"type", data_type_to_string(dto.type)}
    };
}

void to_json(nlohmann::json& j, const TableSchemaDto& dto) {
    j = nlohmann::json{
        {"name", dto.name},
        {"columns", dto.columns}
    };
}

void to_json(nlohmann::json& j, const ShallowTableDto& dto) {
    j = nlohmann::json{
        {"tableId", dto.table_id},
        {"name", dto.name}
    };
}

} // namespace isbd::api