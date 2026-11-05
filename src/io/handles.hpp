#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace hdxf {
namespace internal {

// A handle is hex text of 1 to 16 digits (64 bits). Surrounding spaces are ignored.
enum class HandleParse {
    Ok,
    Empty,          // no handle
    NotHex,         // not a handle — ignored, as before
    TooLong         // more than 16 hex digits: does not fit in 64 bits
};

inline HandleParse parse_handle(const std::string& text, uint64_t& value) {
    size_t begin = text.find_first_not_of(' ');
    if (begin == std::string::npos) return HandleParse::Empty;
    size_t end = text.find_last_not_of(' ') + 1;

    // Leading zeros do not count towards the 16 digits
    while (begin + 1 < end && text[begin] == '0') ++begin;
    if (end - begin > 16) {
        for (size_t i = begin; i < end; ++i) {
            if (!std::isxdigit(static_cast<unsigned char>(text[i]))) return HandleParse::NotHex;
        }
        return HandleParse::TooLong;
    }

    uint64_t v = 0;
    for (size_t i = begin; i < end; ++i) {
        char c = text[i];
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else return HandleParse::NotHex;
        v = (v << 4) | static_cast<uint64_t>(digit);
    }
    value = v;
    return HandleParse::Ok;
}

inline std::string format_handle(uint64_t value) {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(value));
    return buf;
}

} // namespace internal
} // namespace hdxf
