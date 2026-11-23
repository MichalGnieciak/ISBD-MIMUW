#include "schema_service.h"

namespace isbd::api {

SchemaService::SchemaService(Metastore& metastore)
    : metastore_(metastore) {}

std::vector<ShallowTableDto> SchemaService::list_tables() const {
    auto tables = metastore_.list_tables();
    std::vector<ShallowTableDto> dtos;
    dtos.reserve(tables.size());
    for (const auto& t : tables) {
        dtos.push_back({t.table_id, t.name});
    }
    return dtos;
}

std::variant<std::string, MultipleProblemsDto> SchemaService::create_table(
    const TableSchemaDto& schema_dto) {
    std::vector<ValidationProblem> catalog_problems;
    auto table_id = metastore_.create_table(
        schema_dto.name,
        schema_dto.to_domain_columns(),
        catalog_problems);

    if (!table_id.has_value()) {
        return MultipleProblemsDto::from_validation_problems(catalog_problems);
    }
    return *table_id;
}

std::optional<TableSchemaDto> SchemaService::get_table_by_id(const std::string& table_id) const {
    auto table = metastore_.get_table_by_id(table_id);
    if (!table.has_value()) {
        return std::nullopt;
    }
    return TableSchemaDto::from_metadata(*table);
}

bool SchemaService::delete_table(const std::string& table_id) {
    return metastore_.delete_table(table_id);
}

} // namespace isbd::api