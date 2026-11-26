#include <cctype>
#include <cstdint>
#include "query_dto.h"
#include "../http_utils.h"

namespace isbd::api {

bool CopyQueryDto::parse(const nlohmann::json& qdef,
                         CopyQueryDto& out,
                         std::vector<ProblemItemDto>& problems) {
    if (!qdef.contains("sourceFilepath") || !qdef["sourceFilepath"].is_string() ||
        qdef["sourceFilepath"].get<std::string>().empty()) {
        problems.push_back({
            "'sourceFilepath' is required and must be a non-empty string",
            "queryDefinition.sourceFilepath"
        });
    } else {
        out.source_filepath = qdef["sourceFilepath"].get<std::string>();
    }

    if (!qdef.contains("destinationTableName") || !qdef["destinationTableName"].is_string() ||
        qdef["destinationTableName"].get<std::string>().empty()) {
        problems.push_back({
            "'destinationTableName' is required and must be a non-empty string",
            "queryDefinition.destinationTableName"
        });
    } else {
        out.destination_table_name = qdef["destinationTableName"].get<std::string>();
    }

    if (qdef.contains("doesCsvContainHeader") && !qdef["doesCsvContainHeader"].is_null()) {
        if (!qdef["doesCsvContainHeader"].is_boolean()) {
            problems.push_back({
                "'doesCsvContainHeader' must be a boolean",
                "queryDefinition.doesCsvContainHeader"
            });
        } else {
            out.does_csv_contain_header = qdef["doesCsvContainHeader"].get<bool>();
        }
    }

    if (qdef.contains("destinationColumns") && !qdef["destinationColumns"].is_null()) {
        if (!qdef["destinationColumns"].is_array()) {
            problems.push_back({
                "'destinationColumns' must be an array of strings",
                "queryDefinition.destinationColumns"
            });
        } else {
            for (size_t i = 0; i < qdef["destinationColumns"].size(); ++i) {
                const auto& item = qdef["destinationColumns"][i];
                if (!item.is_string() || item.get<std::string>().empty()) {
                    problems.push_back({
                        "Each element of 'destinationColumns' must be a non-empty string",
                        "queryDefinition.destinationColumns[" + std::to_string(i) + "]"
                    });
                } else {
                    out.destination_columns.push_back(item.get<std::string>());
                }
            }
        }
    }

    return problems.empty();
}

CopyRequest CopyQueryDto::to_domain() const {
    CopyRequest req;
    req.table_name = destination_table_name;
    req.source_file = source_filepath;
    req.does_csv_contain_header = does_csv_contain_header;
    req.destination_columns = destination_columns;
    return req;
}

bool SelectQueryDto::parse(const nlohmann::json& qdef,
                           SelectQueryDto& out,
                           std::vector<ProblemItemDto>& problems) {
    if (!qdef.contains("columnClauses") || !qdef["columnClauses"].is_array() ||
        qdef["columnClauses"].empty()) {
        problems.push_back({
            "'columnClauses' must be a non-empty array",
            "queryDefinition.columnClauses"
        });
        return false;
    }

    if (qdef.contains("whereClause") && !qdef["whereClause"].is_null()) {
        problems.push_back({"'whereClause' is not supported in this version", "queryDefinition.whereClause"});
    }
    if (qdef.contains("orderByClause") && !qdef["orderByClause"].is_null()) {
        problems.push_back({"'orderByClause' is not supported in this version", "queryDefinition.orderByClause"});
    }
    if (qdef.contains("limitClause") && !qdef["limitClause"].is_null()) {
        problems.push_back({"'limitClause' is not supported in this version", "queryDefinition.limitClause"});
    }

    std::string expected_table;
    out.column_clauses.reserve(qdef["columnClauses"].size());

    for (size_t i = 0; i < qdef["columnClauses"].size(); ++i) {
        const auto& clause = qdef["columnClauses"][i];
        std::string ctx = "queryDefinition.columnClauses[" + std::to_string(i) + "]";

        if (!clause.is_object()) {
            problems.push_back({"Column clause must be an object", ctx});
            continue;
        }

        if (!clause.contains("tableName") || !clause["tableName"].is_string() ||
            clause["tableName"].get<std::string>().empty()) {
            problems.push_back({"'tableName' is required and must be a non-empty string", ctx + ".tableName"});
            continue;
        }

        if (!clause.contains("columnName") || !clause["columnName"].is_string() ||
            clause["columnName"].get<std::string>().empty()) {
            problems.push_back({"'columnName' is required and must be a non-empty string", ctx + ".columnName"});
            continue;
        }

        std::string tbl = clause["tableName"].get<std::string>();
        std::string col = clause["columnName"].get<std::string>();

        if (expected_table.empty()) {
            expected_table = tbl;
        } else if (expected_table != tbl) {
            problems.push_back({
                "All columnClauses must reference the same table (expected '" +
                    expected_table + "', got '" + tbl + "')",
                ctx + ".tableName"
            });
        }

        out.column_clauses.push_back({std::move(tbl), std::move(col)});
    }

    return problems.empty();
}

SelectRequest SelectQueryDto::to_domain() const {
    SelectRequest req;
    if (!column_clauses.empty()) {
        req.table_name = column_clauses.front().table_name;
    }
    req.column_names.reserve(column_clauses.size());
    for (const auto& clause : column_clauses) {
        req.column_names.push_back(clause.column_name);
    }
    return req;
}

bool ExecuteQueryRequestDto::parse(const nlohmann::json& j,
                                   ExecuteQueryRequestDto& out,
                                   std::vector<ProblemItemDto>& problems) {
    if (!j.is_object()) {
        problems.push_back({"Request body must be a JSON object", ""});
        return false;
    }

    if (!j.contains("queryDefinition") || !j["queryDefinition"].is_object()) {
        problems.push_back({"'queryDefinition' is required and must be a JSON object", "queryDefinition"});
        return false;
    }

    const auto& qdef = j["queryDefinition"];
    out.raw_query_definition = qdef;

    bool has_copy_fields = qdef.contains("sourceFilepath") || qdef.contains("destinationTableName");
    bool has_select_fields = qdef.contains("columnClauses");

    if (has_copy_fields && !has_select_fields) {
        out.type = QueryType::COPY;
        CopyQueryDto copy_dto;
        if (!CopyQueryDto::parse(qdef, copy_dto, problems)) {
            return false;
        }
        out.query_definition = std::move(copy_dto);
        return true;
    }

    if (has_select_fields && !has_copy_fields) {
        out.type = QueryType::SELECT;
        SelectQueryDto select_dto;
        if (!SelectQueryDto::parse(qdef, select_dto, problems)) {
            return false;
        }
        out.query_definition = std::move(select_dto);
        return true;
    }

    problems.push_back({
        "queryDefinition must represent either a CopyQuery (sourceFilepath, destinationTableName) "
        "or a SelectQuery (columnClauses)",
        "queryDefinition"
    });
    return false;
}

QueryDetailDto QueryDetailDto::from_record(const QueryRecord& record) {
    nlohmann::json qdef = nlohmann::json::object();
    if (!record.query_definition_json.empty()) {
        qdef = nlohmann::json::parse(record.query_definition_json, nullptr, false);
        if (qdef.is_discarded()) {
            qdef = nlohmann::json::object();
        }
    }

    return QueryDetailDto{
        record.query_id,
        record.status,
        record.is_result_available,
        std::move(qdef)
    };
}

std::vector<BatchResultDto> BatchResultDto::from_select_result(const SelectResultData& result) {
    std::vector<BatchResultDto> batches;
    batches.reserve(result.batches.size());
    for (const Batch& b : result.batches) {
        batches.push_back({b.num_rows, b.columns});
    }
    return batches;
}

bool GetResultOptionsDto::parse(const std::string& raw_body,
                                GetResultOptionsDto& out,
                                std::string& out_error) {
    out.row_limit = std::nullopt;
    out.flush_result = false;

    if (is_whitespace_only(raw_body)) {
        return true;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(raw_body);
    } catch (const nlohmann::json::parse_error& ex) {
        out_error = std::string("Invalid JSON request body: ") + ex.what();
        return false;
    }

    if (!body.is_object()) {
        out_error = "Request body must be a JSON object";
        return false;
    }

    if (body.contains("rowLimit") && !body["rowLimit"].is_null()) {
        if (!body["rowLimit"].is_number_integer()) {
            out_error = "'rowLimit' must be an integer";
            return false;
        }
        int64_t val = body["rowLimit"].get<int64_t>();
        if (val < 0) {
            out_error = "'rowLimit' must be non-negative";
            return false;
        }
        out.row_limit = static_cast<size_t>(val);
    }

    if (body.contains("flushResult") && !body["flushResult"].is_null()) {
        if (!body["flushResult"].is_boolean()) {
            out_error = "'flushResult' must be a boolean";
            return false;
        }
        out.flush_result = body["flushResult"].get<bool>();
    }

    return true;
}

void to_json(nlohmann::json& j, const ShallowQueryDto& dto) {
    j = nlohmann::json{
        {"queryId", dto.query_id},
        {"status", query_status_to_string(dto.status)}
    };
}

void to_json(nlohmann::json& j, const QueryDetailDto& dto) {
    j = nlohmann::json{
        {"queryId", dto.query_id},
        {"status", query_status_to_string(dto.status)},
        {"isResultAvailable", dto.is_result_available},
        {"queryDefinition", dto.query_definition}
    };
}

void to_json(nlohmann::json& j, const BatchResultDto& dto) {
    nlohmann::json cols_json = nlohmann::json::array();
    for (const ColumnData& col : dto.columns) {
        std::visit([&](const auto& vec) {
            cols_json.push_back(vec);
        }, col);
    }

    j = nlohmann::json{
        {"rowCount", dto.row_count},
        {"columns", std::move(cols_json)}
    };
}

} // namespace isbd::api