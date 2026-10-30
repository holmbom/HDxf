#include "test_framework.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// The files AutoCAD 2025 saved (doc/acad_*_dxf.dxf: SAVEAS DXF, precision 16, ASCII, a
// drawing with one LINE and one CIRCLE) are the facit: read with HDxf and written back
// unchanged, they must come out byte for byte (line endings included).
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path facit_file(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / hdxf::path_from_utf8(name);
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string to_text(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

// The line number of the first difference, for the failure message
std::string first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t pos = 0;
    while (pos < a.size() && pos < b.size() && a[pos] == b[pos]) ++pos;
    size_t line = 1;
    for (size_t i = 0; i < pos; ++i) if (a[i] == '\n') ++line;
    return "first difference at byte " + std::to_string(pos) + " (line " + std::to_string(line) +
           "), " + std::to_string(a.size()) + " bytes in, " + std::to_string(b.size()) + " out";
}

void check_round_trip(const std::string& name) {
    std::vector<uint8_t> in = read_bytes(facit_file(name));
    hdxf_test::assert_true(!in.empty(), "cannot read " + name);
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(in.data(), in.size()), dwg.last_error());
    std::vector<uint8_t> out = dwg.save_to_memory(hdxf::DxfFormat::Ascii);
    hdxf_test::assert_true(!out.empty(), dwg.last_error());
    hdxf_test::assert_true(out == in, name + ": " + first_difference(in, out));
}

// A file in memory with its line endings changed to the other kind
std::vector<uint8_t> with_line_ending(const std::vector<uint8_t>& in, bool crlf) {
    std::vector<uint8_t> out;
    out.reserve(in.size() + in.size() / 8);
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\r' && i + 1 < in.size() && in[i + 1] == '\n') continue;
        if (in[i] == '\n' && crlf) out.push_back('\r');
        out.push_back(in[i]);
    }
    return out;
}

size_t count_of(const std::string& text, const std::string& what) {
    size_t n = 0;
    for (size_t pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + 1)) ++n;
    return n;
}

// The records of the ENTITIES section as lists of (code, value), in file order
std::vector<std::vector<std::pair<int, std::string>>> entity_records(const std::string& text) {
    std::vector<std::vector<std::pair<int, std::string>>> records;
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    bool in_entities = false;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        int code = std::stoi(lines[i]);
        const std::string& value = lines[i + 1];
        if (code == 0 && value == "SECTION" && i + 3 < lines.size() && lines[i + 3] == "ENTITIES") {
            in_entities = true;
            continue;
        }
        if (!in_entities) continue;
        if (code == 0 && value == "ENDSEC") break;
        if (code == 0) records.emplace_back();
        if (!records.empty()) records.back().emplace_back(code, value);
    }
    return records;
}

hdxf::Entity entity_of(hdxf::EntityType type, hdxf::EntityData data) {
    hdxf::Entity e;
    e.type = type;
    e.data = std::move(data);
    return e;
}

} // namespace

TEST(autocad_facit_r12_round_trip)  { check_round_trip("acad_r12_dxf.dxf"); }
TEST(autocad_facit_2000_round_trip) { check_round_trip("acad_2000_dxf.dxf"); }
TEST(autocad_facit_2004_round_trip) { check_round_trip("acad_2004_dxf.dxf"); }
TEST(autocad_facit_2007_round_trip) { check_round_trip("acad_2007_dxf.dxf"); }
TEST(autocad_facit_2010_round_trip) { check_round_trip("acad_2010_dxf.dxf"); }
TEST(autocad_facit_2013_round_trip) { check_round_trip("acad_2013_dxf.dxf"); }
TEST(autocad_facit_2018_round_trip) { check_round_trip("acad_2018_dxf.dxf"); }

// A file with LF only is written back with LF only — in every version
TEST(autocad_facit_lf_file_stays_lf) {
    for (const char* name : {"acad_r12_dxf.dxf", "acad_2000_dxf.dxf", "acad_2018_dxf.dxf"}) {
        std::vector<uint8_t> crlf = read_bytes(facit_file(name));
        std::vector<uint8_t> lf = with_line_ending(crlf, false);
        hdxf_test::assert_true(lf.size() < crlf.size(), "the file has CRLF");
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load_from_memory(lf.data(), lf.size()), dwg.last_error());
        std::vector<uint8_t> out = dwg.save_to_memory(hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(out == lf, std::string(name) + ": " + first_difference(lf, out));
    }
}

// A drawing built in memory gets CRLF, as AutoCAD writes it, in every version; binary DXF
// has no line endings to change
TEST(autocad_crlf_for_drawings_built_in_memory) {
    for (auto version : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2018}) {
        hdxf::Drawing dwg(version);
        dwg.add_line({0, 0, 0}, {10, 10, 0});
        std::string text = to_text(dwg.save_to_memory(hdxf::DxfFormat::Ascii));
        hdxf_test::assert_true(!text.empty(), dwg.last_error());
        size_t lf = count_of(text, "\n");
        hdxf_test::assert_true(lf > 0, "no lines");
        hdxf_test::assert_eq(static_cast<int>(count_of(text, "\r\n")), static_cast<int>(lf),
                             "every line ends with CRLF");
        hdxf_test::assert_eq(static_cast<int>(count_of(text, "\r")), static_cast<int>(lf), "no other CR");
    }
}

TEST(autocad_binary_has_no_line_ending_change) {
    std::vector<uint8_t> in = read_bytes(facit_file("acad_2000_dxf.dxf"));
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(in.data(), in.size()), dwg.last_error());
    std::vector<uint8_t> binary = dwg.save_to_memory(hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(binary.size() > 22, dwg.last_error());
    hdxf_test::assert_eq(to_text(std::vector<uint8_t>(binary.begin(), binary.begin() + 18)),
                         "AutoCAD Binary DXF");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(binary.data(), binary.size()), back.last_error());
    std::vector<uint8_t> again = back.save_to_memory(hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(again == binary, "binary round trip: " + first_difference(binary, again));
}

// R12 as AutoCAD 2025 writes it: LTYPE has CONTINUOUS only (count 1), the $HANDSEED read stays
// when it is above every handle, and an empty paper space has 0,0,0 extents
TEST(autocad_r12_tables_and_header) {
    hdxf::Drawing fresh;
    std::string text = to_text(fresh.save_to_memory(hdxf::DxfFormat::Ascii));
    hdxf_test::assert_true(!text.empty(), fresh.last_error());
    const std::string eol = "\r\n";
    hdxf_test::assert_eq(static_cast<int>(count_of(text, "LTYPE" + eol + "  2" + eol + "BYBLOCK")), 0);
    hdxf_test::assert_eq(static_cast<int>(count_of(text, "LTYPE" + eol + "  2" + eol + "BYLAYER")), 0);
    hdxf_test::assert_eq(static_cast<int>(count_of(text, "LTYPE" + eol + "  2" + eol + "CONTINUOUS")), 1);
    // An empty paper space is 0,0,0
    const std::string zero_point = eol + " 10" + eol + "0.0" + eol + " 20" + eol + "0.0" + eol +
                                   " 30" + eol + "0.0" + eol;
    hdxf_test::assert_eq(static_cast<int>(count_of(text, "$PEXTMIN" + zero_point)), 1, "$PEXTMIN");
    hdxf_test::assert_eq(static_cast<int>(count_of(text, "$PEXTMAX" + zero_point)), 1, "$PEXTMAX");
}

// Every entity AutoCAD's R12 has keeps `67 1` (paper space), also VERTEX, ATTRIB and SEQEND,
// and BLOCK/ENDBLK of $PAPER_SPACE
TEST(autocad_r12_keeps_paper_space_on_every_entity) {
    hdxf::Drawing dwg;
    dwg.add_block("PS_BLOCK");
    dwg.add_line({0, 0, 0}, {10, 0, 0});
    dwg.add_circle({0, 0, 0}, 5);
    dwg.add_arc({0, 0, 0}, 5, 0, 90);
    dwg.add_point({1, 1, 0});
    dwg.add_text({0, 0, 0}, "text");
    dwg.add_polyline({{0, 0}, {1, 0}, {1, 1}});
    dwg.add_trace({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
    dwg.add_solid({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
    dwg.add_face3d({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
    dwg.add_shape({0, 0, 0}, "SHAPE", 1.0);
    dwg.add_dimension(hdxf::DimensionType::Rotated, {0, 0, 0}, {5, 5, 0});
    dwg.add_viewport({100, 100, 0}, 80, 60);
    dwg.entities().push_back(entity_of(hdxf::EntityType::Attdef, hdxf::AttdefData{}));
    hdxf::Entity& insert = dwg.add_insert("PS_BLOCK", {0, 0, 0});
    hdxf::InsertAttribute attribute;
    insert.as_insert().attributes.push_back(attribute);
    for (auto& e : dwg.entities()) {
        e.props.paper_space = 1;
        if (e.type == hdxf::EntityType::Insert) {
            for (auto& a : e.as_insert().attributes) a.props.paper_space = 1;
        }
    }

    std::vector<uint8_t> bytes = dwg.save_to_memory(hdxf::DxfFormat::Ascii);
    hdxf_test::assert_true(!bytes.empty(), dwg.last_error());
    auto records = entity_records(to_text(bytes));
    hdxf_test::assert_true(records.size() > 14, "records written");
    for (const auto& record : records) {
        bool has_67 = false;
        for (const auto& [code, value] : record) {
            if (code == 67) has_67 = value.find('1') != std::string::npos;
        }
        hdxf_test::assert_true(has_67, "no 67 1 on " + record[0].second);
    }

    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(bytes.data(), bytes.size()), back.last_error());
    for (const auto& e : back.entities()) {
        hdxf_test::assert_eq(e.props.paper_space, 1, "paper space after reading");
    }
}

namespace {

// The group lines of a file as (code, value) pairs
std::vector<std::pair<std::string, std::string>> group_lines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    std::vector<std::pair<std::string, std::string>> groups;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) groups.emplace_back(lines[i], lines[i + 1]);
    return groups;
}

// The groups of one section, handle values (group 5) replaced by "H"
std::vector<std::pair<std::string, std::string>> section_groups(const std::string& text, const std::string& name) {
    std::vector<std::pair<std::string, std::string>> out;
    bool inside = false;
    auto groups = group_lines(text);
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].first == "  0" && groups[i].second == "SECTION" && i + 1 < groups.size() &&
            groups[i + 1].second == name) {
            inside = true;
            ++i;
            continue;
        }
        if (!inside) continue;
        if (groups[i].first == "  0" && groups[i].second == "ENDSEC") break;
        if (groups[i].first == "  5") out.emplace_back(groups[i].first, "H");
        else out.push_back(groups[i]);
    }
    return out;
}

} // namespace

// A new R12 drawing with one LINE and one CIRCLE has the facit's structure: BLOCKS exactly (the
// handle values aside) and the header variables in AutoCAD 2025's order
TEST(autocad_r12_new_drawing_structure) {
    std::string facit = to_text(read_bytes(facit_file("acad_r12_dxf.dxf")));
    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {100, 50, 0});
    dwg.add_circle({50, 50, 0}, 20);
    std::string mine = to_text(dwg.save_to_memory(hdxf::DxfFormat::Ascii));
    hdxf_test::assert_true(!mine.empty(), dwg.last_error());

    auto blocks = section_groups(facit, "BLOCKS");
    hdxf_test::assert_true(blocks.size() > 20, "the facit has $MODEL_SPACE and $PAPER_SPACE");
    hdxf_test::assert_true(section_groups(mine, "BLOCKS") == blocks, "BLOCKS as AutoCAD's");
    hdxf_test::assert_true(section_groups(mine, "ENTITIES") == section_groups(facit, "ENTITIES"), "ENTITIES");

    auto names = [](const std::string& text) {
        std::vector<std::string> list;
        for (const auto& g : section_groups(text, "HEADER")) if (g.first == "  9") list.push_back(g.second);
        return list;
    };
    hdxf_test::assert_true(names(mine) == names(facit), "the header variables in AutoCAD's order");

    // Written again, the same: the handles of the blocks do not move
    std::string again = to_text(dwg.save_to_memory(hdxf::DxfFormat::Ascii));
    hdxf_test::assert_true(again == mine, "a second save is the same");
}

// ---------------------------------------------------------------------------
// Every file AutoCAD 2025 saved that is in doc/facit (git-ignored, made in AutoCAD from
// doc/facit/*.scr): read and written in the same format, unchanged, it comes back byte for byte.
// ---------------------------------------------------------------------------

namespace {

// Whether the bytes start with the 22-byte binary DXF sentinel
bool is_binary_dxf(const std::vector<uint8_t>& bytes) {
    static const char sentinel[] = "AutoCAD Binary DXF\r\n\x1a";
    return bytes.size() > 22 && std::equal(sentinel, sentinel + 21, bytes.begin()) && bytes[21] == 0;
}

// The version a file says it is ($ACADVER, "AC1009" … "AC1032"), "?" when it does not
std::string version_of(const std::vector<uint8_t>& bytes) {
    std::string text(bytes.begin(), bytes.end());
    size_t at = text.find("$ACADVER");
    if (at == std::string::npos) return "?";
    size_t ac = text.find("AC10", at);
    return ac == std::string::npos ? "?" : text.substr(ac, 6);
}

// The first differing byte as an offset; of an ASCII file also its line
std::string difference_at(const std::vector<uint8_t>& in, const std::vector<uint8_t>& out, bool binary) {
    size_t pos = 0;
    while (pos < in.size() && pos < out.size() && in[pos] == out[pos]) ++pos;
    std::string text = "first difference at byte " + std::to_string(pos);
    if (!binary) {
        size_t line = 1;
        for (size_t i = 0; i < pos; ++i) if (in[i] == '\n') ++line;
        text += " (line " + std::to_string(line) + ")";
    }
    return text + ", " + std::to_string(in.size()) + " bytes in, " + std::to_string(out.size()) + " out";
}

// R12 is written from the records of the file too (F9 to F22 of doc/plans/2026-10-25-facit-rattningar.md),
// so its files must round trip like the others
constexpr bool r12_must_round_trip = true;

} // namespace

TEST(autocad_facit_all_local_files_round_trip) {
    std::filesystem::path directory = hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit";
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) hdxf_test::skip("doc/facit is not there");
    std::vector<std::filesystem::path> files;
    for (std::filesystem::directory_iterator it(directory, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().extension() == ".dxf") files.push_back(it->path());
    }
    hdxf_test::assert_true(!ec, "cannot list doc/facit: " + ec.message());
    if (files.empty()) hdxf_test::skip("doc/facit has no DXF files");
    std::sort(files.begin(), files.end());

    // Per version and format: identical, different, cannot be read, cannot be written
    std::map<std::string, std::vector<int>> counts;
    std::vector<std::string> failures;
    std::vector<std::string> r12_failures;
    for (const auto& path : files) {
        std::string name = hdxf::utf8_from_path(path.filename());
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            failures.push_back(name + ": cannot be opened");
            continue;
        }
        std::vector<uint8_t> in((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        bool binary = is_binary_dxf(in);
        std::string version = version_of(in);
        std::vector<int>& count = counts[version + (binary ? " binary" : " ASCII")];
        count.resize(4, 0);

        std::string problem;
        hdxf::Drawing dwg;
        if (!dwg.load_from_memory(in.data(), in.size())) {
            ++count[2];
            problem = "cannot be read: " + dwg.last_error();
        } else {
            std::vector<uint8_t> out = dwg.save_to_memory(binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
            if (out.empty() || !dwg.last_error().empty()) {
                ++count[3];
                problem = "cannot be written: " + dwg.last_error();
            } else if (out != in) {
                ++count[1];
                problem = difference_at(in, out, binary);
            } else {
                ++count[0];
            }
        }
        if (problem.empty()) continue;
        std::string line = name + " (" + version + (binary ? ", binary" : ", ASCII") + "): " + problem;
        if (version == "AC1009" && !r12_must_round_trip) r12_failures.push_back(line);
        else failures.push_back(line);
    }

    std::cout << "\n  facit round trip, " << files.size() << " files (identical / different / unreadable / unwritable):\n";
    for (const auto& [kind, count] : counts) {
        std::cout << "    " << kind << ": " << count[0] << " / " << count[1] << " / " << count[2] << " / " << count[3] << "\n";
    }
    if (!r12_failures.empty()) {
        std::cout << "    R12 (not required yet): " << r12_failures.size() << " files do not round trip\n";
    }
    std::string message = std::to_string(failures.size()) + " of " + std::to_string(files.size()) + " facit files do not round trip:";
    for (size_t i = 0; i < failures.size() && i < 40; ++i) message += "\n  " + failures[i];
    if (failures.size() > 40) message += "\n  ... and " + std::to_string(failures.size() - 40) + " more";
    hdxf_test::assert_true(failures.empty(), message);
}

// ---------------------------------------------------------------------------
// F10: a table record is added or removed (the TABLE's 70). doc/facit/14_annotative.scr made the
// files in one AutoCAD session, each from the one before: scale_removed, then a VIEW added and
// removed, a STYLE added and removed, an LTYPE added and removed, saved as ASCII and binary after
// each step, in R12 and in 2000 ... 2018. HDxf reads one of them, makes the same change through the API and
// writes it in the same format; what comes out is compared group by group with the file AutoCAD saved after
// the change. Not compared, because they are AutoCAD's alone (R12 and 2000+):
//  - the handles (group 5, $HANDSEED), renumbered in every session (2000+: compared as names, see
//    facit_differences), and the clock: $TDUPDATE, $TDUUPDATE, $TDINDWG, $TDUSRTIMER;
//  - the VIEW table's count, 10 higher than the file's plus the records added, in every step;
//  - 2000+ only: $FINGERPRINTGUID and $VERSIONGUID with the XRECORD that holds the GUID and the saved path;
//    the times of the DWGPROPS COOKIE XRECORD; the instance counts of CLASS (91); the stale reactors of an
//    MLEADERSTYLE; the order of {BLKREFS, of the five schema XRECORDs and of the dictionary named by memory
//    addresses; up to 8 padding bytes in the proxy graphics of MULTILEADER and LARGE_RADIAL_DIMENSION
// ---------------------------------------------------------------------------

namespace {

struct FacitGroup {
    int code = 0;
    std::string value;      // ASCII: the line; binary: the value as text, a double as the hex of its bytes
    bool literal = false;   // a handle already named as AutoCAD's (see order_lists): compared as it stands
};

std::string hex_of(const uint8_t* data, size_t size) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < size; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 15];
    }
    return out;
}

// The groups of a file, ASCII or binary; `modern` is 2000 and later, whose binary group codes are two bytes
std::vector<FacitGroup> facit_groups(const std::vector<uint8_t>& bytes, bool modern = false) {
    std::vector<FacitGroup> groups;
    if (!is_binary_dxf(bytes)) {
        std::vector<std::string> lines;
        std::string line;
        for (uint8_t c : bytes) {
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(line);
                line.clear();
            } else {
                line += static_cast<char>(c);
            }
        }
        for (size_t i = 0; i + 1 < lines.size(); i += 2) {
            groups.push_back({std::stoi(lines[i]), lines[i + 1]});
        }
        return groups;
    }
    size_t pos = 22;
    auto number = [&](size_t size) {
        int64_t value = 0;
        for (size_t i = 0; i < size && pos + i < bytes.size(); ++i) value |= static_cast<int64_t>(bytes[pos + i]) << (8 * i);
        return value;
    };
    while (pos < bytes.size()) {
        int code = bytes[pos++];
        if (modern) {
            --pos;
            code = static_cast<int>(number(2));
            pos += 2;
        } else if (code == 255) {
            code = static_cast<int>(number(2));
            pos += 2;
        }
        FacitGroup group;
        group.code = code;
        if (code <= 9 || (code >= 100 && code <= 102) || code == 105 || (code >= 300 && code <= 309) ||
            (code >= 320 && code <= 369) || (code >= 390 && code <= 399) || (code >= 410 && code <= 439) ||
            (code >= 470 && code <= 481) || (code >= 999 && code <= 1003) || (code >= 1005 && code <= 1009)) {
            while (pos < bytes.size() && bytes[pos] != 0) group.value += static_cast<char>(bytes[pos++]);
            ++pos;
        } else if ((code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239) ||
                   (code >= 460 && code <= 469) || (code >= 1010 && code <= 1059)) {
            group.value = hex_of(&bytes[pos], 8);
            pos += 8;
        } else if (code >= 160 && code <= 169) {
            group.value = std::to_string(number(8));
            pos += 8;
        } else if ((code >= 90 && code <= 99) || (code >= 420 && code <= 429) || (code >= 440 && code <= 459) || code == 1071) {
            group.value = std::to_string(static_cast<int32_t>(number(4)));
            pos += 4;
        } else if (code >= 290 && code <= 299) {
            group.value = std::to_string(number(1));
            pos += 1;
        } else if ((code >= 310 && code <= 319) || code == 1004) {
            size_t length = bytes[pos++];
            group.value = hex_of(&bytes[pos], length);
            pos += length;
        } else {
            group.value = std::to_string(static_cast<int16_t>(number(2)));
            pos += 2;
        }
        groups.push_back(group);
    }
    return groups;
}

std::string trimmed(const std::string& value) {
    size_t first = value.find_first_not_of(' ');
    return first == std::string::npos ? "" : value.substr(first);
}

// A double that is uninitialised memory in an AutoCAD file: the y and z (1021, 1031) of a hatch's seed point in
// XDATA are 0.0 in one file and 1.3596500564211868E+241 (ASCII) or leftover bytes (binary, tiny or huge as a
// double) in the next one saved. Binary doubles are the hex of their bytes
bool uninitialised(const std::string& value) {
    double d = 0.0;
    if (value.size() == 16 && value.find_first_not_of("0123456789ABCDEF") == std::string::npos) {
        uint64_t bits = std::stoull(value.substr(0, 16), nullptr, 16);
        uint64_t swapped = 0;
        for (int k = 0; k < 8; ++k) swapped |= ((bits >> (8 * k)) & 0xFF) << (8 * (7 - k));
        std::memcpy(&d, &swapped, sizeof d);
    } else {
        d = std::stod(value);
    }
    return std::fabs(d) > 1e100 || (d != 0.0 && std::fabs(d) < 1e-100);
}

// The reactors of an MLEADERSTYLE in an AutoCAD session file include, besides the objects that use the style,
// handles of objects that are not in the file (the multileaders and blocks the session regenerated and
// dropped again: 8 more in every step of 14_annotative.scr, whatever the step does). Those are AutoCAD's
// alone; they are taken out of the reactor list before the comparison. The objects in the file are kept
std::vector<FacitGroup> without_stale_reactors(const std::vector<FacitGroup>& groups) {
    std::set<std::string> defined;
    for (size_t i = 1; i < groups.size(); ++i) {
        if (groups[i - 1].code == 0 && (groups[i].code == 5 || groups[i].code == 105)) defined.insert(trimmed(groups[i].value));
    }
    std::vector<FacitGroup> out;
    std::string record;
    bool reactors = false;
    for (const auto& g : groups) {
        if (g.code == 0) record = g.value;
        if (g.code == 102) reactors = trimmed(g.value) == "{ACAD_REACTORS";
        if (reactors && record == "MLEADERSTYLE" && g.code == 330 && !defined.count(trimmed(g.value))) continue;
        out.push_back(g);
    }
    return out;
}

// The five XRECORDs of the data-storage schemas (group 1 "AcDb_Thumbnail_Schema", "AcDbDs::...") that follow each
// other in OBJECTS are written by AutoCAD in a different cyclic order in some saves, not in others (2004 and
// 2013 binary: the run starts at the third or the second record). The order is AutoCAD's alone; both files
// are put in the order of the names
std::vector<FacitGroup> order_schema_records(const std::vector<FacitGroup>& groups) {
    auto record_end = [&](size_t start) {
        size_t end = start + 1;
        while (end < groups.size() && groups[end].code != 0) ++end;
        return end;
    };
    auto schema_name = [&](size_t start, size_t end) -> std::string {
        if (groups[start].value != "XRECORD") return "";
        for (size_t k = start; k < end; ++k) {
            if (groups[k].code == 1 && (groups[k].value.rfind("AcDb_Thumbnail_Schema", 0) == 0 || groups[k].value.rfind("AcDbDs::", 0) == 0)) {
                return groups[k].value;
            }
        }
        return "";
    };
    std::vector<FacitGroup> out;
    size_t i = 0;
    while (i < groups.size()) {
        if (groups[i].code != 0 || schema_name(i, record_end(i)).empty()) {
            out.push_back(groups[i++]);
            continue;
        }
        std::vector<std::pair<std::string, std::pair<size_t, size_t>>> run;
        size_t j = i;
        while (j < groups.size() && groups[j].code == 0) {
            size_t end = record_end(j);
            std::string name = schema_name(j, end);
            if (name.empty()) break;
            run.push_back({name, {j, end}});
            j = end;
        }
        std::stable_sort(run.begin(), run.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        for (const auto& r : run) out.insert(out.end(), groups.begin() + r.second.first, groups.begin() + r.second.second);
        i = j;
    }
    return out;
}

// Two chunks of binary data (hex) of the same length that differ in at most `most` bytes
bool few_bytes_differ(const std::string& x, const std::string& y, size_t most) {
    if (x.size() != y.size()) return false;
    size_t different = 0;
    for (size_t i = 0; i + 1 < x.size(); i += 2) {
        if (x.compare(i, 2, y, i, 2) != 0) ++different;
    }
    return different <= most;
}

// The groups that hold a handle in a 2000+ file: the entity's and the table entry's own (5, 105) and every
// pointer (330-369, 390-399, 480, 481, the XDATA handle 1005)
bool is_handle_code(int code) {
    return code == 5 || code == 105 || (code >= 330 && code <= 369) || (code >= 390 && code <= 399) ||
           code == 480 || code == 481 || code == 1005;
}

// Lists of handles that AutoCAD writes in an order of its own in each session. In both files the list is put in
// one order: the handles of the written file are first named as AutoCAD's (`names`, the entities' own handles
// matched record for record), then the list is sorted, and its handles are compared as they stand.
//  - the objects that refer to a block ({BLKREFS, 331), in an order that is not the file's;
//  - the entries of a dictionary named by memory addresses (the ACDSDATA schemas, code 3 a number), which go
//    in the order of the five schema XRECORDs (see order_schema_records); the names are not compared
void order_lists(std::vector<FacitGroup>& groups, const std::map<std::string, std::string>* names) {
    auto named = [&](FacitGroup& g) {
        if (names) {
            auto it = names->find(trimmed(g.value));
            if (it != names->end()) g.value = it->second;
        }
        g.literal = true;
    };
    bool list = false;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].code == 102) list = trimmed(groups[i].value) == "{BLKREFS";
        if (list && groups[i].code == 331) {
            size_t end = i;
            while (end < groups.size() && groups[end].code == 331) named(groups[end++]);
            std::sort(groups.begin() + i, groups.begin() + end,
                      [](const FacitGroup& x, const FacitGroup& y) { return x.value < y.value; });
            i = end - 1;
            continue;
        }
        auto address = [&](size_t k) {
            std::string v = trimmed(groups[k].value);
            return k + 1 < groups.size() && groups[k].code == 3 && groups[k + 1].code == 350 && v.size() >= 10 &&
                   v.find_first_not_of("0123456789") == std::string::npos;
        };
        if (address(i)) {
            size_t end = i;
            std::vector<std::pair<std::string, std::pair<FacitGroup, FacitGroup>>> entries;
            while (address(end)) {
                named(groups[end + 1]);
                groups[end].value = "0";
                entries.push_back({groups[end + 1].value, {groups[end], groups[end + 1]}});
                end += 2;
            }
            std::stable_sort(entries.begin(), entries.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
            for (size_t k = 0; k < entries.size(); ++k) {
                groups[i + 2 * k] = entries[k].second.first;
                groups[i + 2 * k + 1] = entries[k].second.second;
            }
            i = end - 1;
        }
    }
}

// What differs between the file HDxf wrote and the one AutoCAD saved, each as "context: HDxf / AutoCAD",
// without the handles and the clock (above). A double of a binary file is compared by its bytes, so a
// difference there is shown as the hex. In a 2000+ file (`modern`) a handle is not compared by its value, which
// is AutoCAD's numbering of that session, but as a name: HDxf's handle and AutoCAD's must stand for each other
// wherever they occur (the same HDxf handle always faces the same AutoCAD handle and the other way round), so
// an owner, a reactor or a pointer to the wrong object is a difference. A handle 0 (no pointer) and the handles
// of the standard tables and dictionaries that every file has must be equal. $HANDSEED, $FINGERPRINTGUID and
// $VERSIONGUID, AutoCAD's alone, are not compared
std::vector<std::string> facit_differences(const std::vector<uint8_t>& written, const std::vector<uint8_t>& autocad,
                                           bool modern = false) {
    std::vector<FacitGroup> a = facit_groups(written, modern);
    std::vector<FacitGroup> b = facit_groups(autocad, modern);
    if (modern) {
        a = order_schema_records(without_stale_reactors(a));
        b = order_schema_records(without_stale_reactors(b));
        std::map<std::string, std::string> names;
        for (size_t i = 1; i < a.size() && i < b.size(); ++i) {
            if (a[i].code == b[i].code && (a[i].code == 5 || a[i].code == 105) && a[i - 1].code == 0 && b[i - 1].code == 0) {
                names.emplace(trimmed(a[i].value), trimmed(b[i].value));
            }
        }
        order_lists(a, &names);
        order_lists(b, nullptr);
    }
    std::vector<std::string> out;
    if (a.size() != b.size()) {
        out.push_back("the number of groups: " + std::to_string(a.size()) + " / " + std::to_string(b.size()));
    }
    std::map<std::string, std::string> hdxf_to_acad, acad_to_hdxf;
    std::string variable, table, section, record;
    int since_table = 99;
    bool table_head = false, guid_record = false, cookie_record = false;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        const FacitGroup& g = b[i];
        if (g.code == 9) variable = g.value;
        if (g.code == 0) {
            table_head = false;
            guid_record = cookie_record = false;
            record = g.value;
        }
        if (record == "XRECORD" && g.code == 300 && !g.value.empty() && g.value[0] == '{') guid_record = true;
        if (record == "XRECORD" && g.code == 1 && g.value == "DWGPROPS COOKIE") cookie_record = true;
        if (g.code == 0 && g.value == "SECTION") variable.clear();
        if (g.code == 2 && i > 0 && b[i - 1].code == 0 && b[i - 1].value == "SECTION") section = g.value;
        if (g.code == 0 && g.value == "TABLE") {
            since_table = 0;
            table_head = true;
        }
        if (g.code == 2 && since_table == 1) table = g.value;
        bool count = modern ? table_head && g.code == 70 : since_table == 2 && g.code == 70;
        std::string where = count ? table + " table count"
                                  : "group " + std::to_string(g.code) + " " + (variable.empty() ? section + " " + record : variable);
        ++since_table;
        if (a[i].code != g.code) {
            out.push_back("group " + std::to_string(i) + ": code " + std::to_string(a[i].code) + " / " + std::to_string(g.code));
            if (out.size() > 20) break;
            continue;
        }
        bool hatch_handle = i > 0 && b[i - 1].code == 1000 && b[i - 1].value == "R14_HATCH_DATA" && g.code == 1000;
        if (modern && is_handle_code(g.code) && !g.literal && variable != "$HANDSEED") {
            std::string mine = trimmed(a[i].value), theirs = trimmed(g.value);
            auto [x, x_new] = hdxf_to_acad.emplace(mine, theirs);
            auto [y, y_new] = acad_to_hdxf.emplace(theirs, mine);
            bool null_pointer = mine == "0" || theirs == "0";
            if ((null_pointer && mine != theirs) || x->second != theirs || y->second != mine) {
                out.push_back(where + ": handle " + mine + " / " + theirs);
            }
            if (out.size() > 20) break;
            continue;
        }
        if (a[i].value == g.value || (!modern && (g.code == 5 || g.code == 1005)) || hatch_handle) continue;
        if (modern && g.code == 5) continue;     // $HANDSEED
        if (modern && g.code == 2 && (variable == "$FINGERPRINTGUID" || variable == "$VERSIONGUID")) continue;
        if ((g.code == 1021 || g.code == 1031) && (uninitialised(a[i].value) || uninitialised(g.value))) continue;
        // The XRECORD with the file's version GUID (twice) and the path it was saved to; the DWGPROPS COOKIE
        // XRECORD's times (total editing time 40, created 41, updated 42), which AutoCAD sets at every save
        if (modern && guid_record && (g.code == 300 || g.code == 302 || g.code == 303)) continue;
        if (modern && cookie_record && g.code >= 40 && g.code <= 42) continue;
        // A class's instance count (91): AutoCAD's count of the objects of the class the session has made, which
        // every save of the script raises (the blocks and multileaders it regenerates), never lowers
        if (modern && record == "CLASS" && g.code == 91 && std::stoi(g.value) >= std::stoi(a[i].value)) continue;
        // The proxy graphics (310) of the MULTILEADER and LARGE_RADIAL_DIMENSION that a 2000 ... 2018 file
        // keeps as proxy entities: AutoCAD makes them anew at every save and leaves a few bytes of padding as
        // they were in memory (after the font name, in the doubles' unused bytes). The chunk must be as long
        // and differ in no more than 8 bytes
        if (modern && g.code == 310 && (record == "MULTILEADER" || record == "LARGE_RADIAL_DIMENSION") &&
            few_bytes_differ(a[i].value, g.value, 8)) continue;
        if (g.code == 40 && (variable == "$TDUPDATE" || variable == "$TDUUPDATE" || variable == "$TDINDWG" || variable == "$TDUSRTIMER")) continue;
        out.push_back(where + ": " + trimmed(a[i].value) + " / " + trimmed(g.value));
        if (out.size() > 20) break;
    }
    return out;
}

// `from` is read, `change` is made, the drawing is written in the format of `from` and compared with `to`
// (both names without ".dxf" or "_bin.dxf"); the differences are returned. The test is skipped without the files
std::vector<std::string> facit_change(const std::string& from, const std::string& to, bool binary,
                                      const std::function<void(hdxf::Drawing&)>& change) {
    std::string suffix = binary ? "_bin.dxf" : ".dxf";
    std::filesystem::path from_path = facit_file("facit/" + from + suffix);
    std::filesystem::path to_path = facit_file("facit/" + to + suffix);
    std::error_code ec;
    if (!std::filesystem::exists(from_path, ec) || !std::filesystem::exists(to_path, ec)) {
        hdxf_test::skip(from + suffix + " or " + to + suffix + " is not in doc/facit");
    }
    std::vector<uint8_t> in = read_bytes(from_path);
    std::vector<uint8_t> acad = read_bytes(to_path);
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(in.data(), in.size()), dwg.last_error());
    change(dwg);
    std::vector<uint8_t> out = dwg.save_to_memory(binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
    hdxf_test::assert_true(!out.empty(), dwg.last_error());
    return facit_differences(out, acad, from.rfind("acad_r12", 0) != 0);
}

std::string joined(const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& l : lines) text += "\n  " + l;
    return text;
}

hdxf::View f10_view() {
    hdxf::View v;
    v.name = "HDXF_F10_VIEW";
    v.height = 100.0;
    v.width = 200.0;
    v.direction = {0.0, 0.0, 1.0};
    v.lens_length = 50.0;
    return v;
}

void f10_add_style(hdxf::Drawing& dwg) {
    dwg.add_text_style("HDXF_F10_STYLE", "txt").last_height = 0.2;
}

void f10_add_linetype(hdxf::Drawing& dwg) {
    dwg.add_linetype("HDXF_F10_LTYPE", "HDXF F10 dashed", {10.0, -5.0});
}

template <typename T>
void f10_remove(std::vector<T>& records, const std::string& name) {
    auto it = std::find_if(records.begin(), records.end(), [&](const T& r) { return r.name == name; });
    hdxf_test::assert_true(it != records.end(), name + " is not in the drawing");
    records.erase(it);
}

// "VIEW table count: 11 / 21" as the numbers; false for anything else
bool view_count_difference(const std::string& line, int& mine, int& theirs) {
    return std::sscanf(line.c_str(), "VIEW table count: %d / %d", &mine, &theirs) == 2;
}

// The same change in ASCII and binary. AutoCAD's VIEW count is 10 higher than the file's count plus the
// records added (every step of 14_annotative.scr raises it by 10 more, a view added or not; the files of the
// first run, before the script saved all versions, had it 10 higher once and then constant). It does not
// follow from any record, so HDxf cannot write it, and it is the only difference allowed
void f10_check(const std::string& from, const std::string& to, const std::function<void(hdxf::Drawing&)>& change) {
    for (bool binary : {false, true}) {
        auto differences = facit_change(from, to, binary, change);
        int mine = 0, theirs = 0;
        hdxf_test::assert_true(differences.size() == 1 && view_count_difference(differences[0], mine, theirs) &&
                                   theirs == mine + 10,
                               std::string(binary ? "binary:" : "ASCII:") + joined(differences));
    }
}

} // namespace

// A VIEW added to a drawing whose file counted 10 views with none exported: the count is the file's plus the
// record, 11 (STYLE and LTYPE below: 2 to 3, 1 to 2); AutoCAD's is 21 (see f10_check)
TEST(autocad_facit_f10_view_added) {
    f10_check("acad_r12_dxf_annotative_scale_removed", "acad_r12_dxf_annotative_scale_removed_view_added",
              [](hdxf::Drawing& dwg) { dwg.views().push_back(f10_view()); });
}

// The view removed again: the count of the file stays (21) while the table is empty
TEST(autocad_facit_f10_view_removed) {
    f10_check("acad_r12_dxf_annotative_scale_removed_view_added", "acad_r12_dxf_annotative_scale_removed_view_removed",
              [](hdxf::Drawing& dwg) { f10_remove(dwg.views(), "HDXF_F10_VIEW"); });
}

// A STYLE added: the count of 2 is 3, as AutoCAD's file
TEST(autocad_facit_f10_style_added) {
    f10_check("acad_r12_dxf_annotative_scale_removed_view_removed", "acad_r12_dxf_annotative_scale_removed_style_added",
              f10_add_style);
}

// The STYLE removed again: the count stays 3 while the table has 2 records
TEST(autocad_facit_f10_style_removed) {
    f10_check("acad_r12_dxf_annotative_scale_removed_style_added", "acad_r12_dxf_annotative_scale_removed_style_removed",
              [](hdxf::Drawing& dwg) { f10_remove(dwg.text_styles(), "HDXF_F10_STYLE"); });
}

// An LTYPE added: the count of 1 is 2
TEST(autocad_facit_f10_ltype_added) {
    f10_check("acad_r12_dxf_annotative_scale_removed_style_removed", "acad_r12_dxf_annotative_scale_removed_ltype_added",
              f10_add_linetype);
}

// The LTYPE removed again: the count stays 2 while the table has 1 record
TEST(autocad_facit_f10_ltype_removed) {
    f10_check("acad_r12_dxf_annotative_scale_removed_ltype_added", "acad_r12_dxf_annotative_scale_removed_ltype_removed",
              [](hdxf::Drawing& dwg) { f10_remove(dwg.linetypes(), "HDXF_F10_LTYPE"); });
}

// ---------------------------------------------------------------------------
// F10 for 2000-2018: the same six steps (doc/facit/14_annotative.scr saved them in every version, ASCII and
// binary, in the one session). The change is made through the API on the file of the step before, and
// compared with AutoCAD's file of the step. The handles are compared as names (see facit_differences), not
// by value, and the clock and the GUIDs are left out; the count of a VIEW table is the one difference below.
// ---------------------------------------------------------------------------

namespace {

struct F10Step {
    const char* from;           // the file of the step before, after "annotative_scale_removed"
    const char* to;
    std::function<void(hdxf::Drawing&)> change;
};

const std::vector<F10Step>& f10_steps() {
    static const std::vector<F10Step> steps = {
        {"", "_view_added", [](hdxf::Drawing& dwg) { dwg.views().push_back(f10_view()); }},
        {"_view_added", "_view_removed", [](hdxf::Drawing& dwg) { f10_remove(dwg.views(), "HDXF_F10_VIEW"); }},
        {"_view_removed", "_style_added", f10_add_style},
        {"_style_added", "_style_removed", [](hdxf::Drawing& dwg) { f10_remove(dwg.text_styles(), "HDXF_F10_STYLE"); }},
        {"_style_removed", "_ltype_added", f10_add_linetype},
        {"_ltype_added", "_ltype_removed", [](hdxf::Drawing& dwg) { f10_remove(dwg.linetypes(), "HDXF_F10_LTYPE"); }},
    };
    return steps;
}

void f10_modern_check(const std::string& version, size_t step) {
    const F10Step& s = f10_steps()[step];
    std::string base = "acad_" + version + "_dxf_annotative_scale_removed";
    f10_check(base + s.from, base + s.to, s.change);
}

} // namespace

#define F10_MODERN_TESTS(ver) \
    TEST(autocad_facit_f10_##ver##_view_added) { f10_modern_check(#ver, 0); } \
    TEST(autocad_facit_f10_##ver##_view_removed) { f10_modern_check(#ver, 1); } \
    TEST(autocad_facit_f10_##ver##_style_added) { f10_modern_check(#ver, 2); } \
    TEST(autocad_facit_f10_##ver##_style_removed) { f10_modern_check(#ver, 3); } \
    TEST(autocad_facit_f10_##ver##_ltype_added) { f10_modern_check(#ver, 4); } \
    TEST(autocad_facit_f10_##ver##_ltype_removed) { f10_modern_check(#ver, 5); }

F10_MODERN_TESTS(2000)
F10_MODERN_TESTS(2004)
F10_MODERN_TESTS(2007)
F10_MODERN_TESTS(2010)
F10_MODERN_TESTS(2013)
F10_MODERN_TESTS(2018)
