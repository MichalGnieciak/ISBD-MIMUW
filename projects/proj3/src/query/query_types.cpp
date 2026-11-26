#include "query_types.h"

namespace isbd {

std::string query_type_to_string(QueryType type) {
    switch (type) {
        case QueryType::COPY:   return "COPY";
        case QueryType::SELECT: return "SELECT";
    }
    return "UNKNOWN";
}

std::string query_status_to_string(QueryStatus status) {
    switch (status) {
        case QueryStatus::CREATED:   return "CREATED";
        case QueryStatus::PLANNING:  return "PLANNING";
        case QueryStatus::RUNNING:   return "RUNNING";
        case QueryStatus::COMPLETED: return "COMPLETED";
        case QueryStatus::FAILED:    return "FAILED";
    }
    return "UNKNOWN";
}

} // namespace isbd