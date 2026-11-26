#pragma once
#include "api/services/metadata_service.h"
#include <httplib.h>

namespace isbd::api {

class MetadataController {
public:
    explicit MetadataController(MetadataService& metadata_service);

    void register_routes(httplib::Server& server);

private:
    void handle_get_system_info(const httplib::Request& req, httplib::Response& res);

    MetadataService& metadata_service_;
};

} // namespace isbd::api