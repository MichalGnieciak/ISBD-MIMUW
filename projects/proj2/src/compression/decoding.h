#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Decompression
// INT64
// Delta Decoding
std::vector<int64_t> delta_decode(const std::vector<int64_t>& deltas);
// ZigZag Decoding
int64_t zigzag_decode(uint64_t n);
// Varint Decoding
uint64_t varint_decode(const std::vector<uint8_t>& in_bytes, size_t& offset);

// BOOL
// Bit Unpacking
std::vector<uint8_t> unpack_booleans(const std::vector<uint8_t>& packed_bytes, size_t num_booleans);

// VARCHAR
// ZSTD
std::vector<char> decompress_zstd(const std::vector<uint8_t>& compressed_data, size_t original_size);


// Column Decoders
// INT64
std::vector<int64_t> decode_int64_column(const std::vector<uint8_t>& bytes);

// BOOL
std::vector<uint8_t> decode_bool_column(const std::vector<uint8_t>& bytes, size_t num_booleans);

// VARCHAR
std::vector<std::string> decode_varchar_column(const std::vector<uint8_t>& index_bytes, const std::vector<uint8_t>& data_bytes);