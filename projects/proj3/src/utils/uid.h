#pragma once
#include <string>

namespace isbd {

// Generates a thread-safe RFC 4122 version 4 UUID string
std::string generate_uid();

} // namespace isbd