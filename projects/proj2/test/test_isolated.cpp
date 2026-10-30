#include "encoding.h"
#include "decoding.h"
#include "random_utils.h"
#include <iostream>
#include <cassert>
#include <string>
#include <random>
#include <chrono>

void test_int64(std::mt19937& rng, int passes) {
    std::uniform_int_distribution<size_t> size_dist(1000, 10000);

    for (int p = 0; p < passes; ++p) {
        std::vector<int64_t> original = (p == 0) ? 
            std::vector<int64_t>{1000, 1002, 995, -10, 0, 5000000000LL} : 
            std::vector<int64_t>(size_dist(rng));
        
        if (p > 0) {
            for (size_t i = 0; i < original.size(); ++i) {
                original[i] = random_int64(rng, -1000000000LL, 1000000000LL);
            }
        }

        std::vector<uint8_t> compressed_bytes;
        encode_int64_column(original, compressed_bytes);
        std::vector<int64_t> decoded = decode_int64_column(compressed_bytes);
        
        assert(original.size() == decoded.size());
        for (size_t i = 0; i < original.size(); ++i) assert(original[i] == decoded[i]);
    }
    std::cout << "INT64 encoding/decoding passed (" << passes << " passes)!\n";
}

void test_bool(std::mt19937& rng, int passes) {
    std::uniform_int_distribution<size_t> size_dist(1000, 10000);

    for (int p = 0; p < passes; ++p) {
        std::vector<uint8_t> original = (p == 0) ? 
            std::vector<uint8_t>{1, 0, 0, 1, 1, 1, 0, 0, 1} : 
            std::vector<uint8_t>(size_dist(rng));
        
        if (p > 0) {
            for (size_t i = 0; i < original.size(); ++i) {
                original[i] = random_bool(rng);
            }
        }

        std::vector<uint8_t> packed_bytes;
        encode_bool_column(original, packed_bytes);
        std::vector<uint8_t> unpacked = decode_bool_column(packed_bytes, original.size());
        
        assert(original.size() == unpacked.size());
        for (size_t i = 0; i < original.size(); ++i) assert(original[i] == unpacked[i]);
    }
    std::cout << "BOOL encoding/decoding passed (" << passes << " passes)!\n";
}

void test_varchar(std::mt19937& rng, int passes) {
    std::uniform_int_distribution<size_t> size_dist(500, 5000);

    for (int p = 0; p < passes; ++p) {
        std::vector<std::string> original = (p == 0) ? 
            std::vector<std::string>{"foo", "bar", "baz", "", "database"} : 
            std::vector<std::string>(size_dist(rng));
        
        if (p > 0) {
            for (size_t i = 0; i < original.size(); ++i) original[i] = random_string(rng, 100); 
        }

        std::vector<uint8_t> index_bytes;
        std::vector<uint8_t> data_bytes;
        
        encode_varchar_column(original, index_bytes, data_bytes);
        std::vector<std::string> reconstructed = decode_varchar_column(index_bytes, data_bytes);
        
        assert(original.size() == reconstructed.size());
        for (size_t i = 0; i < original.size(); ++i) assert(original[i] == reconstructed[i]);
    }
    std::cout << "VARCHAR encoding/decoding passed (" << passes << " passes)!\n";
}

int main() {
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::cout << "Running randomized isolated tests... [SEED: " << seed << "]\n";
    std::mt19937 rng(seed);
    
    int passes = 10;
    test_int64(rng, passes);
    test_bool(rng, passes);
    test_varchar(rng, passes);
    
    std::cout << "All tests passed successfully!\n";
    return 0;
}