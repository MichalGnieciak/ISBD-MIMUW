#include "api/dto/common_dto.h"
#include "api/http_utils.h"
#include <exception>
#include "server.h"

namespace isbd::api {

DbmsServer::DbmsServer(Metastore& metastore, QueryRegistry& registry, Executor& executor)
    : schema_service_(metastore),
      execution_service_(metastore, registry, executor),
      schema_controller_(schema_service_),
      execution_controller_(execution_service_),
      metadata_controller_(metadata_service_) {
    server_.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
        try {
            if (ep) {
                std::rethrow_exception(ep);
            }
        } catch (const std::exception& ex) {
            send_json(res, 500, ErrorDto{std::string("Internal server error: ") + ex.what()});
            return;
        }
        send_json(res, 500, ErrorDto{"Unknown internal server error"});
    });

    schema_controller_.register_routes(server_);
    execution_controller_.register_routes(server_);
    metadata_controller_.register_routes(server_);
}

bool DbmsServer::listen(const std::string& host, int port) {
    return server_.listen(host, port);
}

void DbmsServer::stop() {
    server_.stop();
}

} // namespace isbd::api