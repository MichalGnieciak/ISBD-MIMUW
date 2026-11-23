#pragma once
#include "api/services/schema_service.h"
#include <httplib.h>

namespace isbd::api {

class SchemaController {
public:
    explicit SchemaController(SchemaService& schema_service);

    void register_routes(httplib::Server& server);

private:
    void handle_get_tables(const httplib::Request& req, httplib::Response& res);
    void handle_put_table(const httplib::Request& req, httplib::Response& res);
    void handle_get_table_by_id(const httplib::Request& req, httplib::Response& res);
    void handle_delete_table(const httplib::Request& req, httplib::Response& res);

    SchemaService& schema_service_;
};

} // namespace isbd::api