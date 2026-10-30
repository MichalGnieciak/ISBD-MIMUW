#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Compression
// INT64
// Delta Encoding
std::vector<int64_t> delta_encode(const std::vector<int64_t>& data);
// ZigZag Encoding
uint64_t zigzag_encode(int64_t n);
// Varint Encoding
void varint_encode(uint64_t value, std::vector<uint8_t>& out_bytes);

// BOOL
// Bit Packing
std::vector<uint8_t> pack_booleans(const std::vector<uint8_t>& bools);

// VARCHAR
// ZSTD
std::vector<uint8_t> compress_zstd(const std::vector<char>& raw_data);


// Column Encoders
// INT64
void encode_int64_column(const std::vector<int64_t>& data, std::vector<uint8_t>& out_bytes);

// BOOL
void encode_bool_column(const std::vector<uint8_t>& bools, std::vector<uint8_t>& out_bytes);

// VARCHAR
void encode_varchar_column(const std::vector<std::string>& strings, std::vector<uint8_t>& out_index_bytes, std::vector<uint8_t>& out_data_bytes);