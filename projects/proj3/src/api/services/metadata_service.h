#pragma once
#include "../dto/system_dto.h"
#include <chrono>

namespace isbd::api {

class MetadataService {
public:
    MetadataService();

    SystemInfoDto get_system_info() const;

private:
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace isbd::api