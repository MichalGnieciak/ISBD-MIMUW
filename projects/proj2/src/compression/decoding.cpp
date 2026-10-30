#include "decoding.h"

#include <stdexcept>
#include <string>
#include <zstd.h>

// Decpmpression
// INT64
// Delta Decoding
std::vector<int64_t> delta_decode(const std::vector<int64_t>& deltas) {
    if (deltas.empty()) return {};
    
    std::vector<int64_t> decoded;
    decoded.reserve(deltas.size());
    
    decoded.push_back(deltas[0]);
    for (size_t i = 1; i < deltas.size(); ++i) {
        decoded.push_back(decoded[i - 1] + deltas[i]);
    }
    return decoded;
}

// ZigZag Decoding
int64_t zigzag_decode(uint64_t n) {
    // Shift right by 1. XOR with the lowest bit negated (which acts as the sign).
    // source: https://en.wikipedia.org/wiki/Variable-length_integer#Variable-length_quantity
    return static_cast<int64_t>((n >> 1) ^ (~(n & 1) + 1));
}

// Varint Decoding
uint64_t varint_decode(const std::vector<uint8_t>& in_bytes, size_t& offset) {
    uint64_t result = 0;
    int shift = 0;
    
    while (offset < in_bytes.size()) {
        uint8_t byte = in_bytes[offset++];
        // Extract the lowest 7 bits and shift them into the correct position
        result |= (static_cast<uint64_t>(byte & 0x7F) << shift);
        
        // If the highest bit is 0, we have reached the end of this integer
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
    }
    return result;
}

// BOOL
// Bit Unpacking
std::vector<uint8_t> unpack_booleans(const std::vector<uint8_t>& packed_bytes, size_t num_booleans) {
    std::vector<uint8_t> bools;
    bools.reserve(num_booleans);
    
    for (size_t i = 0; i < num_booleans; ++i) {
        size_t byte_index = i / 8;
        size_t bit_index = i % 8;
        
        // Isolate the specific bit using a mask AND
        bool bit = (packed_bytes[byte_index] & (1 << bit_index)) != 0;
        bools.push_back(bit ? 1 : 0);
    }
    return bools;
}

// VARCHAR
// ZSTD
std::vector<char> decompress_zstd(const std::vector<uint8_t>& compressed_data, size_t original_size) {
    if (compressed_data.empty() || original_size == 0) return {};

    std::vector<char> decompressed_data(original_size);

    size_t const dSize = ZSTD_decompress(
        decompressed_data.data(), original_size,
        compressed_data.data(), compressed_data.size()
    );

    if (ZSTD_isError(dSize) || dSize != original_size) {
        throw std::runtime_error("ZSTD Decompression failed!");
    }

    return decompressed_data;
}

// Column Decoders
// INT64
std::vector<int64_t> decode_int64_column(const std::vector<uint8_t>& bytes) {
    std::vector<int64_t> decoded_deltas;
    size_t offset = 0;
    
    // Varint -> ZigZag -> Delta
    while (offset < bytes.size()) {
        uint64_t v = varint_decode(bytes, offset);
        decoded_deltas.push_back(zigzag_decode(v));
    }
    return delta_decode(decoded_deltas);
}

// BOOL
std::vector<uint8_t> decode_bool_column(const std::vector<uint8_t>& bytes, size_t num_booleans) {
    return unpack_booleans(bytes, num_booleans); 
}

// VARCHAR
std::vector<std::string> decode_varchar_column(const std::vector<uint8_t>& index_bytes, const std::vector<uint8_t>& data_bytes) {
    if (index_bytes.empty()) return {};

    // Decoding index blob
    std::vector<int64_t> decoded_offsets = decode_int64_column(index_bytes);
    
    // Decoding text
    size_t total_raw_length = decoded_offsets.back(); 
    std::vector<char> raw_text = decompress_zstd(data_bytes, total_raw_length);
    
    // Reconstruction
    std::vector<std::string> reconstructed;
    reconstructed.reserve(decoded_offsets.size());
    
    size_t prev_offset = 0;
    for (int64_t offset : decoded_offsets) {
        size_t len = offset - prev_offset;
        reconstructed.push_back(std::string(raw_text.begin() + prev_offset, raw_text.begin() + prev_offset + len));
        prev_offset = offset;
    }
    
    return reconstructed;
}