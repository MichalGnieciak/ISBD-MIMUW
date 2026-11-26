#pragma once

#include "api/controllers/execution_controller.h"
#include "api/controllers/metadata_controller.h"
#include "api/controllers/schema_controller.h"
#include "api/services/execution_service.h"
#include "api/services/metadata_service.h"
#include "api/services/schema_service.h"
#include "metastore/metastore.h"
#include "query/executor/executor.h"
#include "query/registry/query_registry.h"

#include <httplib.h>
#include <string>

namespace isbd::api {

class DbmsServer {
public:
    DbmsServer(Metastore& metastore, QueryRegistry& registry, Executor& executor);

    bool listen(const std::string& host, int port);
    void stop();

private:
    httplib::Server server_;

    SchemaService schema_service_;
    ExecutionService execution_service_;
    MetadataService metadata_service_;

    SchemaController schema_controller_;
    ExecutionController execution_controller_;
    MetadataController metadata_controller_;
};

} // namespace isbd::api