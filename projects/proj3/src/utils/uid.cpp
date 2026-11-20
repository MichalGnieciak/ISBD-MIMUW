#include "uid.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <random>

namespace isbd {

std::string generate_uid() {
    // thread_local avoids mutex contention while keeping each thread's RNG independent
    thread_local std::mt19937_64 rng([] {
        std::random_device rd;
        uint64_t seed1 = (static_cast<uint64_t>(rd()) << 32) ^ rd();
        uint64_t seed2 = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::seed_seq seq{seed1, seed2};
        return std::mt19937_64(seq);
    }());

    std::uniform_int_distribution<uint64_t> dist;
    uint64_t high = dist(rng);
    uint64_t low = dist(rng);

    // Set RFC 4122 version 4 (0100) and variant (10xx) bits
    high = (high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    low  = (low  & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    char buf[37];
    std::snprintf(
        buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        static_cast<uint32_t>(high >> 32),
        static_cast<uint16_t>((high >> 16) & 0xFFFF),
        static_cast<uint16_t>(high & 0xFFFF),
        static_cast<uint16_t>(low >> 48),
        static_cast<unsigned long long>(low & 0x0000FFFFFFFFFFFFULL));

    return std::string(buf, 36);
}

} // namespace isbd