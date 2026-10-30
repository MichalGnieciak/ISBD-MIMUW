#include "encoding.h"

#include <stdexcept>
#include <string>
#include <zstd.h>

// Compression
// INT64
// Delta Encoding
std::vector<int64_t> delta_encode(const std::vector<int64_t>& data) {
    if (data.empty()) return {};
    
    std::vector<int64_t> encoded;
    encoded.reserve(data.size());
    
    encoded.push_back(data[0]);
    for (size_t i = 1; i < data.size(); ++i) {
        encoded.push_back(data[i] - data[i - 1]);
    }
    return encoded;
}

// ZigZag Encoding
uint64_t zigzag_encode(int64_t n) {
    // Shift left by 1. XOR with the sign bit smeared across all 64 bits.
    // source: https://en.wikipedia.org/wiki/Variable-length_integer#Variable-length_quantity
    return (static_cast<uint64_t>(n) << 1) ^ static_cast<uint64_t>(n >> 63);
}

// Varint Encoding
void varint_encode(uint64_t value, std::vector<uint8_t>& out_bytes) {
    // While value is >= 128, needs more than 7 bits
    while (value >= 0x80) { 
        // Take the lowest 7 bits, set the 8th bit to 1 (continuation flag)
        out_bytes.push_back(static_cast<uint8_t>(value | 0x80));
        value >>= 7;
    }
    // Push the final byte with the continuation flag set to 0
    out_bytes.push_back(static_cast<uint8_t>(value));
}

// BOOL
// Bit Packing
std::vector<uint8_t> pack_booleans(const std::vector<uint8_t>& bools) {
    std::vector<uint8_t> packed;
    // Calculate required capacity
    packed.reserve((bools.size() + 7) / 8); 
    
    uint8_t current_byte = 0;
    for (size_t i = 0; i < bools.size(); ++i) {
        if (bools[i] != 0) {
            // Shift a '1' into the correct bit slot (0 to 7) and merge it with OR
            current_byte |= (1 << (i % 8)); 
        }
        if ((i % 8 == 7) || (i == bools.size() - 1)) {
            packed.push_back(current_byte);
            current_byte = 0;
        }
    }
    return packed;
}

// VARCHAR
// ZSTD
std::vector<uint8_t> compress_zstd(const std::vector<char>& raw_data) {
    if (raw_data.empty()) return {};

    // Maximum possible compressed size
    size_t const max_dst_size = ZSTD_compressBound(raw_data.size());
    std::vector<uint8_t> compressed_data(max_dst_size);

    // Compression
    size_t const cSize = ZSTD_compress(
        compressed_data.data(), max_dst_size,
        raw_data.data(), raw_data.size(), 
        1 
    );
    if (ZSTD_isError(cSize)) {
        throw std::runtime_error("ZSTD Compression failed!");
    }

    // Resize
    compressed_data.resize(cSize);
    return compressed_data;
}

// Column Encoders
// INT64
void encode_int64_column(const std::vector<int64_t>& data, std::vector<uint8_t>& out_bytes) {
    // Pipeline: Delta -> ZigZag -> Varint
    std::vector<int64_t> deltas = delta_encode(data);
    for (int64_t d : deltas) {
        varint_encode(zigzag_encode(d), out_bytes);
    }
}

// BOOL
void encode_bool_column(const std::vector<uint8_t>& bools, std::vector<uint8_t>& out_bytes) {
    out_bytes = pack_booleans(bools);
}

// VARCHAR
void encode_varchar_column(const std::vector<std::string>& strings, std::vector<uint8_t>& out_index_bytes, std::vector<uint8_t>& out_data_bytes) {

    std::vector<int64_t> offsets;
    std::vector<char> raw_data_blob;
    int64_t current_offset = 0;

    // Splitting into blobs: index, text
    for (const std::string& str : strings) {
        current_offset += str.size();
        offsets.push_back(current_offset);
        raw_data_blob.insert(raw_data_blob.end(), str.begin(), str.end());
    }

    // Compressing index blob
    encode_int64_column(offsets, out_index_bytes);

    // Compressing text blob
    out_data_bytes = compress_zstd(raw_data_blob);
}