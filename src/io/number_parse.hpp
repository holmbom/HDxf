#pragma once

#include <charconv>
#include <clocale>
#include <cstdlib>
#include <string>
#include <system_error>

namespace hdxf {
namespace internal {

// atof with a decimal point, whatever the C locale says: the point is given to it as the locale writes it
inline double atof_with_point(const std::string& text) {
    const char* const mark = std::localeconv()->decimal_point;
    if (mark[0] == '.' && mark[1] == '\0') return std::atof(text.c_str());
    const size_t point = text.find('.');
    if (point == std::string::npos) return std::atof(text.c_str());
    std::string local = text;
    local.replace(point, 1, mark);
    return std::atof(local.c_str());
}

// A group's number, read as atof reads it: white space and one sign first, what follows the number ignored, 0 when
// there is no number. Always with a decimal point, whatever the C locale says. What from_chars does not read the same
// way goes to atof: hexadecimal numbers, inf and nan with their payload, a value out of range and a second sign.
inline double parse_double_compat(const std::string& text) {
    const char* p = text.data();
    const char* const end = p + text.size();
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '\v' || *p == '\f')) ++p;
    const char* number = p;
    if (number < end && *number == '+') ++number;
    const char* digits = number;
    if (digits < end && *digits == '-') ++digits;
    const bool plain = digits < end && ((*digits >= '0' && *digits <= '9') || *digits == '.') &&
                       !(number != p && number < end && *number == '-');
    const bool hexadecimal = digits + 1 < end && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X');
    if (!plain || hexadecimal) return atof_with_point(text);
    double value = 0.0;
    const std::from_chars_result result = std::from_chars(number, end, value);
    if (result.ec != std::errc()) return atof_with_point(text);
    return value;
}

} // namespace internal
} // namespace hdxf
