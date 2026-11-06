#include "random_utils.h"

// INT64
int64_t random_int64(std::mt19937& rng, int64_t min_val, int64_t max_val) {
    std::uniform_int_distribution<int64_t> dist(min_val, max_val);
    return dist(rng);
}

// BOOL
uint8_t random_bool(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, 1);
    return static_cast<uint8_t>(dist(rng));
}

// VARCHAR
std::string random_string(std::mt19937& rng, size_t max_length) {
    std::uniform_int_distribution<size_t> len_dist(0, max_length);
    std::uniform_int_distribution<int> char_dist(32, 126); // ASCII space to '~'
    
    size_t len = len_dist(rng);
    std::string s(len, ' ');
    for (size_t i = 0; i < len; ++i) {
        s[i] = static_cast<char>(char_dist(rng));
    }
    return s;
}