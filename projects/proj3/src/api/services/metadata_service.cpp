#include "metadata_service.h"

namespace isbd::api {

MetadataService::MetadataService()
    : start_time_(std::chrono::steady_clock::now()) {}

SystemInfoDto MetadataService::get_system_info() const {
    auto now = std::chrono::steady_clock::now();
    int64_t uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();

    SystemInfoDto info;
    info.uptime = uptime;
    return info;
}

} // namespace isbd::api