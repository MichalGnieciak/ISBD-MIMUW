#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace isbd::api {

struct SystemInfoDto {
    std::string interface_version = "2.1.0";
    std::string version = "1.0.0";
    std::string author = "MichalGnieciak";
    int64_t uptime = 0;
};

void to_json(nlohmann::json& j, const SystemInfoDto& dto);

} // namespace isbd::api