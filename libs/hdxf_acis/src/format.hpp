#pragma once

// Numbers as text, independent of the process's locale (internal)

#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>

namespace hdxf::acis::detail {

// The shortest text that reads back as the same double ("0.1", "1e-06", "6.283185307179586"),
// a negative zero as "0"
inline std::string real_text(double value) {
    if (value == 0.0) return "0";
    char buffer[40];
    auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return std::string(buffer, result.ptr);
}

inline std::string integer_text(long long value) {
    char buffer[24];
    auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    return std::string(buffer, result.ptr);
}

} // namespace hdxf::acis::detail
