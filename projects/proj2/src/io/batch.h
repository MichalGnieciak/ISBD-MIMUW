#pragma once
#include "format.h"
#include <vector>
#include <string>
#include <cstdint>
#include <variant>

using ColumnData = std::variant<
    std::vector<int64_t>, // INT64
    std::vector<std::string>, // VARCHAR 
    std::vector<uint8_t> // BOOL
>;

// SoA layout
struct Batch {
    size_t num_rows = 0;
    std::vector<ColumnData> columns; 
};