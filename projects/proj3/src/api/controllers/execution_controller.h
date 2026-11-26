#pragma once
#include "api/services/execution_service.h"
#include <httplib.h>

namespace isbd::api {

class ExecutionController {
public:
    explicit ExecutionController(ExecutionService& execution_service);

    void register_routes(httplib::Server& server);

private:
    void handle_get_queries(const httplib::Request& req, httplib::Response& res);
    void handle_post_query(const httplib::Request& req, httplib::Response& res);
    void handle_get_query_by_id(const httplib::Request& req, httplib::Response& res);
    void handle_get_query_result(const httplib::Request& req, httplib::Response& res);
    void handle_get_query_error(const httplib::Request& req, httplib::Response& res);

    ExecutionService& execution_service_;
};

} // namespace isbd::api