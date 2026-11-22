#pragma once
#include "../dto/common_dto.h"
#include "../dto/schema_dto.h"
#include "metastore/metastore.h"
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace isbd::api {

class SchemaService {
public:
    explicit SchemaService(Metastore& metastore);

    std::vector<ShallowTableDto> list_tables() const;

    // Returns created table_id on success, or MultipleProblemsDto on validation/catalog error
    std::variant<std::string, MultipleProblemsDto> create_table(const TableSchemaDto& schema_dto);

    std::optional<TableSchemaDto> get_table_by_id(const std::string& table_id) const;

    bool delete_table(const std::string& table_id);

private:
    Metastore& metastore_;
};

} // namespace isbd::api