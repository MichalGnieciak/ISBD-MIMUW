#pragma once
#include <string>
#include <random>
#include <cstdint>

// INT64
// Generates a random 64-bit integer within the specified range
int64_t random_int64(std::mt19937& rng, int64_t min_val, int64_t max_val);

// BOOL
// Generates a random boolean (0 or 1) as a uint8_t
uint8_t random_bool(std::mt19937& rng);

// VARCHAR
// Generates a random string of printable ASCII characters up to max_length
std::string random_string(std::mt19937& rng, size_t max_length);