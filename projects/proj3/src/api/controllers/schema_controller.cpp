#include "schema_controller.h"
#include <nlohmann/json.hpp>
#include <variant>
#include "../dto/common_dto.h"
#include "../dto/schema_dto.h"
#include "../http_utils.h"

namespace isbd::api {

SchemaController::SchemaController(SchemaService& schema_service)
    : schema_service_(schema_service) {}

void SchemaController::register_routes(httplib::Server& server) {
    server.Get("/tables", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_tables(req, res);
    });
    server.Put("/table", [this](const httplib::Request& req, httplib::Response& res) {
        handle_put_table(req, res);
    });
    server.Get(R"(/table/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_table_by_id(req, res);
    });
    server.Delete(R"(/table/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_delete_table(req, res);
    });
}

void SchemaController::handle_get_tables(const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, schema_service_.list_tables());
}

void SchemaController::handle_put_table(const httplib::Request& req, httplib::Response& res) {
    TableSchemaDto schema_dto;
    if (!try_parse_body(req, schema_dto, res)) {
        return;
    }

    auto outcome = schema_service_.create_table(schema_dto);
    if (std::holds_alternative<MultipleProblemsDto>(outcome)) {
        send_json(res, 400, std::get<MultipleProblemsDto>(outcome));
        return;
    }

    send_json(res, 200, nlohmann::json(std::get<std::string>(outcome)));
}

void SchemaController::handle_get_table_by_id(const httplib::Request& req, httplib::Response& res) {
    std::string table_id = req.matches[1];
    auto table = schema_service_.get_table_by_id(table_id);
    if (!table.has_value()) {
        send_json(res, 404, ErrorDto{"Table with ID '" + table_id + "' not found"});
        return;
    }

    send_json(res, 200, *table);
}

void SchemaController::handle_delete_table(const httplib::Request& req, httplib::Response& res) {
    std::string table_id = req.matches[1];
    if (!schema_service_.delete_table(table_id)) {
        send_json(res, 404, ErrorDto{"Table with ID '" + table_id + "' not found"});
        return;
    }

    res.status = 200;
}

} // namespace isbd::api