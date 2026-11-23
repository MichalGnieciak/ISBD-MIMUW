#include "system_dto.h"

namespace isbd::api {

void to_json(nlohmann::json& j, const SystemInfoDto& dto) {
    j = nlohmann::json{
        {"interfaceVersion", dto.interface_version},
        {"version", dto.version},
        {"author", dto.author},
        {"uptime", dto.uptime}
    };
}

} // namespace isbd::api