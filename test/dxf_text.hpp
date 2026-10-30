#pragma once

#include "test_framework.hpp"
#include <algorithm>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ASCII DXF as lines, for tests that give a file AutoCAD's form group by group: take a group or a
// 102 block out of a record, find a record by its handle. Every line keeps its line ending, so a
// file that is joined again is the file with only that change.
// ---------------------------------------------------------------------------

namespace dxf_text {

// A file as its lines, each with its line ending
inline std::vector<std::string> text_lines(const std::string& text) {
    std::vector<std::string> lines;
    size_t from = 0;
    while (from < text.size()) {
        size_t end = text.find('\n', from);
        end = end == std::string::npos ? text.size() : end + 1;
        lines.push_back(text.substr(from, end - from));
        from = end;
    }
    return lines;
}

inline std::string joined(const std::vector<std::string>& lines) {
    std::string out;
    for (const auto& l : lines) out += l;
    return out;
}

inline std::string bare(const std::string& line) {
    size_t a = line.find_first_not_of(' ');
    size_t b = line.find_last_not_of(" \r\n");
    return a == std::string::npos ? std::string() : line.substr(a, b - a + 1);
}

// The line of the 0 that starts the record with handle `handle` (its 5 or 105)
inline size_t record_at(const std::vector<std::string>& lines, const std::string& handle) {
    size_t start = 0;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        if (bare(lines[i]) == "0") start = i;
        if ((bare(lines[i]) == "5" || bare(lines[i]) == "105") && bare(lines[i + 1]) == handle) return start;
    }
    return lines.size();
}

// The line of the first record of this type
inline size_t first_record_of(const std::vector<std::string>& lines, const std::string& type) {
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        if (bare(lines[i]) == "0" && bare(lines[i + 1]) == type) return i;
    }
    return lines.size();
}

// The line of the first group `code` in the record that starts at `start`
inline size_t group_at(const std::vector<std::string>& lines, size_t start, const std::string& code) {
    for (size_t i = start + 2; i + 1 < lines.size() && bare(lines[i]) != "0"; i += 2) {
        if (bare(lines[i]) == code) return i;
    }
    return lines.size();
}

// Takes the group `code` out of the record
inline void erase_group(std::vector<std::string>& lines, size_t start, const std::string& code) {
    size_t at = group_at(lines, start, code);
    hdxf_test::assert_true(at < lines.size(), "group " + code + " in the record");
    lines.erase(lines.begin() + static_cast<long>(at), lines.begin() + static_cast<long>(at) + 2);
}

// Takes out the 102 block `name` ("{BLKREFS") of the record, or only `pointer` (a handle) from it
inline void erase_102_block(std::vector<std::string>& lines, size_t start, const std::string& name, const std::string& pointer = {}) {
    size_t open = lines.size();
    for (size_t i = start + 2; i + 1 < lines.size() && bare(lines[i]) != "0"; i += 2) {
        if (bare(lines[i]) == "102" && bare(lines[i + 1]) == name) { open = i; break; }
    }
    hdxf_test::assert_true(open < lines.size(), name + " in the record");
    size_t close = open + 2;
    while (bare(lines[close]) != "102") close += 2;
    if (pointer.empty()) {
        lines.erase(lines.begin() + static_cast<long>(open), lines.begin() + static_cast<long>(close) + 2);
        return;
    }
    for (size_t i = open + 2; i < close; i += 2) {
        if (bare(lines[i + 1]) == pointer) {
            lines.erase(lines.begin() + static_cast<long>(i), lines.begin() + static_cast<long>(i) + 2);
            // A block with nothing in it is not written at all
            if (close == open + 2 + 2) lines.erase(lines.begin() + static_cast<long>(open), lines.begin() + static_cast<long>(open) + 4);
            return;
        }
    }
    hdxf_test::assert_true(false, pointer + " in " + name);
}

inline bool has_102_block(const std::vector<std::string>& lines, size_t start, const std::string& name) {
    for (size_t i = start + 2; i + 1 < lines.size() && bare(lines[i]) != "0"; i += 2) {
        if (bare(lines[i]) == "102" && bare(lines[i + 1]) == name) return true;
    }
    return false;
}

// The 330 pointers of the record's 102 {ACAD_REACTORS
inline std::vector<std::string> reactors_in(const std::vector<std::string>& lines, const std::string& handle) {
    std::vector<std::string> out;
    size_t start = record_at(lines, handle);
    bool inside = false;
    for (size_t i = start + 2; i + 1 < lines.size() && bare(lines[i]) != "0"; i += 2) {
        if (bare(lines[i]) == "102") inside = bare(lines[i + 1]) == "{ACAD_REACTORS";
        else if (inside && bare(lines[i]) == "330") out.push_back(bare(lines[i + 1]));
    }
    return out;
}

inline bool contains(const std::vector<std::string>& list, const std::string& what) {
    return std::find(list.begin(), list.end(), what) != list.end();
}

inline std::string text_of(const std::vector<uint8_t>& data) { return std::string(data.begin(), data.end()); }

inline std::vector<uint8_t> bytes_of(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

inline size_t count_records(const std::vector<std::string>& lines, const std::string& type) {
    size_t n = 0;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) n += bare(lines[i]) == "0" && bare(lines[i + 1]) == type;
    return n;
}

// Where two files first differ: the line number and the two lines, for a failure message
inline std::string first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::vector<std::string> x = text_lines(text_of(a)), y = text_lines(text_of(b));
    for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
        if (x[i] != y[i]) {
            return "line " + std::to_string(i + 1) + ": \"" + bare(x[i]) + "\" / \"" + bare(y[i]) + "\" (after \"" +
                   (i > 0 ? bare(x[i - 1]) : std::string()) + "\")";
        }
    }
    return x.size() == y.size() ? "no difference" : "lengths " + std::to_string(x.size()) + " / " + std::to_string(y.size()) + " lines";
}

} // namespace dxf_text
