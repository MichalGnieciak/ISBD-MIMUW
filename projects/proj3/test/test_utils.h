#pragma once

#include <stdexcept>
#include <string>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("Check failed: ") + #cond + \
                                     " at " + __FILE__ + ":" + std::to_string(__LINE__)); \
        } \
    } while (0)