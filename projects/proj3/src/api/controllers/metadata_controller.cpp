#include "metadata_controller.h"
#include "../http_utils.h"

namespace isbd::api {

MetadataController::MetadataController(MetadataService& metadata_service)
    : metadata_service_(metadata_service) {}

void MetadataController::register_routes(httplib::Server& server) {
    server.Get("/system/info", [this](const httplib::Request& req, httplib::Response& res) {
        handle_get_system_info(req, res);
    });
}

void MetadataController::handle_get_system_info(const httplib::Request&, httplib::Response& res) {
    send_json(res, 200, metadata_service_.get_system_info());
}

} // namespace isbd::api