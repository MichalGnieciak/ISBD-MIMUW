#include "execution_controller.h"
#include "api/dto/common_dto.h"
#include "api/dto/query_dto.h"
#include "api/http_utils.h"
#include <nlohmann/json.hpp>
#include <variant>

namespace isbd::api {

ExecutionController::ExecutionController(ExecutionService& execution_service)
    : execution_service_(execution_service) {}

void ExecutionController::register_routes(httplib::Server& server) {
    server.Get("/queries", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_queries(req, res);
    });
    server.Post("/query", [this](const httplib::Request& req, httplib::Response& res) {
        handle_post_query(req, res);
    });
    server.Get(R"(/query/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_query_by_id(req, res);
    });
    server.Get(R"(/result/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_query_result(req, res);
    });
    server.Get(R"(/error/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_query_error(req, res);
    });
}

void ExecutionController::handle_get_queries(const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, execution_service_.list_queries());
}

void ExecutionController::handle_post_query(const httplib::Request& req, httplib::Response& res) {
    ExecuteQueryRequestDto exec_dto;
    if (!try_parse_body(req, exec_dto, res)) {
        return;
    }

    auto outcome = execution_service_.submit_query(exec_dto);
    if (std::holds_alternative<MultipleProblemsDto>(outcome)) {
        send_json(res, 400, std::get<MultipleProblemsDto>(outcome));
        return;
    }

    send_json(res, 200, nlohmann::json(std::get<std::string>(outcome)));
}

void ExecutionController::handle_get_query_by_id(const httplib::Request& req, httplib::Response& res) {
    std::string query_id = req.matches[1];
    auto detail = execution_service_.get_query_by_id(query_id);
    if (!detail.has_value()) {
        send_json(res, 404, ErrorDto{"Query with ID '" + query_id + "' not found"});
        return;
    }

    send_json(res, 200, *detail);
}

void ExecutionController::handle_get_query_result(const httplib::Request& req, httplib::Response& res) {
    std::string query_id = req.matches[1];

    GetResultOptionsDto options;
    std::string parse_err;
    if (!GetResultOptionsDto::parse(req.body, options, parse_err)) {
        send_json(res, 400, ErrorDto{parse_err});
        return;
    }

    auto outcome = execution_service_.get_query_result(query_id, options);
    if (std::holds_alternative<ResultFetchStatus>(outcome)) {
        if (std::get<ResultFetchStatus>(outcome) == ResultFetchStatus::NOT_FOUND) {
            send_json(res, 404, ErrorDto{"Query with ID '" + query_id + "' not found"});
        } else {
            send_json(res, 400, ErrorDto{"Result for query '" + query_id + "' is not available"});
        }
        return;
    }

    send_json(res, 200, std::get<std::vector<BatchResultDto>>(outcome));
}

void ExecutionController::handle_get_query_error(const httplib::Request& req, httplib::Response& res) {
    std::string query_id = req.matches[1];
    auto outcome = execution_service_.get_query_error(query_id);
    if (std::holds_alternative<ErrorFetchStatus>(outcome)) {
        if (std::get<ErrorFetchStatus>(outcome) == ErrorFetchStatus::NOT_FOUND) {
            send_json(res, 404, ErrorDto{"Query with ID '" + query_id + "' not found"});
        } else {
            send_json(res, 400, ErrorDto{"Error details are not available for query '" + query_id + "'"});
        }
        return;
    }

    send_json(res, 200, std::get<MultipleProblemsDto>(outcome));
}

} // namespace isbd::api