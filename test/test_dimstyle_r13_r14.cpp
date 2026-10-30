#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dimstyle_legacy.hpp"
#include "dxf_writer.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DIMSTYLE in DXF R13 and R14 (phase 7, steps 7.7 and 7.8, plan B91): DIMUNIT (270) and DIMFIT
// (287), the arrow blocks as names (5, 6, 7), the handle in 105, the text style in 340. The facit is
// the DIMSTYLE records of AutoCAD's own R13 and R14 files (ezdxf's small_r13.dxf, bin_dxf_r13.dxf and
// bin_dxf_r14.dxf: saved by AutoCAD, kept in build/test-data) and Autodesk's R13 reference.
//
// The tests at the end write a whole R13/R14 drawing through Drawing::save_to_memory (steps 7.2-7.5).
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::internal::LegacyGroup;
using Group = std::pair<int, std::string>;
using Record = std::vector<Group>;

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    size_t first = s.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : s.substr(first);
}

// The DIMSTYLE records of a DXF file (ASCII or binary), each as its groups after the 0
std::vector<Record> dimstyle_records(const std::vector<uint8_t>& data) {
    std::istringstream in(std::string(data.begin(), data.end()), std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Record> out;
    hdxf::internal::GroupCode gc;
    bool in_record = false;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0) {
            in_record = trim(gc.value) == "DIMSTYLE";
            if (in_record) out.emplace_back();
            continue;
        }
        if (in_record) out.back().push_back({gc.code, trim(gc.value)});
    }
    return out;
}

const Record* record_named(const std::vector<Record>& records, const std::string& name) {
    for (const auto& r : records) {
        for (const auto& [code, value] : r) {
            if (code == 2 && value == name) return &r;
        }
    }
    return nullptr;
}

// The groups of the record from its name (2) on, with the head (105, 330, 100, 100) left out
Record body_of(const Record& r) {
    Record out;
    bool body = false;
    for (const auto& g : r) {
        if (g.first == 2) body = true;
        if (body) out.push_back(g);
    }
    return out;
}

std::string text_of(const Record& r, int code) {
    for (const auto& [c, v] : r) if (c == code) return v;
    return "<none>";
}

std::string dump(const Record& r) {
    std::string s;
    for (const auto& [c, v] : r) s += std::to_string(c) + "=" + v + " ";
    return s;
}

bool has_code(const Record& r, int code) {
    for (const auto& g : r) if (g.first == code) return true;
    return false;
}

hdxf::Drawing load_file(const std::string& name) {
    hdxf::Drawing dwg;
    std::vector<uint8_t> bytes = read_bytes(external(name));
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), name + ": " + dwg.last_error());
    return dwg;
}

const hdxf::DimStyle* find_style(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& s : dwg.dim_styles()) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

bool file_exists(const std::string& name) {
    return std::filesystem::exists(external(name));
}

// A DIMSTYLE of AutoCAD's older form as ASCII, for a file of the given version, in a minimal
// R13/R14 file with the STYLE records it points to (340)
struct OlderStyle {
    std::string name = "Older";
    std::string handle = "42";
    int unit = 2;
    int fit = 3;
    std::string blk;
    std::string blk1;
    std::string blk2;
    std::string text_style_handle = "11";
};

std::string older_file(const std::string& acadver, const std::vector<OlderStyle>& styles) {
    std::ostringstream o;
    auto g = [&](int code, const std::string& value) { o << code << "\n" << value << "\n"; };
    g(0, "SECTION"); g(2, "HEADER"); g(9, "$ACADVER"); g(1, acadver); g(0, "ENDSEC");
    g(0, "SECTION"); g(2, "TABLES");
    g(0, "TABLE"); g(2, "STYLE"); g(5, "3"); g(330, "0"); g(100, "AcDbSymbolTable"); g(70, "2");
    for (auto [name, handle] : {std::pair<std::string, std::string>{"STANDARD", "11"}, {"Notes", "12"}}) {
        g(0, "STYLE"); g(5, handle); g(330, "3"); g(100, "AcDbSymbolTableRecord");
        g(100, "AcDbTextStyleTableRecord"); g(2, name); g(70, "0"); g(40, "0.0"); g(41, "1.0");
        g(50, "0.0"); g(71, "0"); g(42, "2.5"); g(3, "txt"); g(4, "");
    }
    g(0, "ENDTAB");
    g(0, "TABLE"); g(2, "DIMSTYLE"); g(5, "A"); g(330, "0"); g(100, "AcDbSymbolTable");
    g(70, std::to_string(styles.size()));
    for (const auto& s : styles) {
        g(0, "DIMSTYLE"); g(105, s.handle); g(330, "A"); g(100, "AcDbSymbolTableRecord");
        g(100, "AcDbDimStyleTableRecord"); g(2, s.name); g(70, "0");
        g(3, ""); g(4, ""); g(5, s.blk); g(6, s.blk1); g(7, s.blk2);
        g(40, "1.0"); g(41, "2.5"); g(42, "0.625"); g(43, "3.75"); g(44, "1.25");
        g(45, "0.0"); g(46, "0.0"); g(47, "0.0"); g(48, "0.0");
        g(140, "2.5"); g(141, "2.5"); g(142, "0.0"); g(143, "0.03937007874"); g(144, "1.0");
        g(145, "0.0"); g(146, "1.0"); g(147, "0.625");
        for (int c = 71; c <= 78; ++c) g(c, c == 77 ? "1" : "0");
        for (int c = 170; c <= 178; ++c) g(c, c == 171 ? "3" : "0");
        g(270, std::to_string(s.unit)); g(271, "2"); g(272, "2"); g(273, "2"); g(274, "3");
        g(340, s.text_style_handle);
        g(275, "0");
        for (int c = 280; c <= 286; ++c) g(c, c == 283 ? "1" : "0");
        g(287, std::to_string(s.fit)); g(288, "0");
    }
    g(0, "ENDTAB");
    g(0, "ENDSEC"); g(0, "EOF");
    return o.str();
}

hdxf::Drawing load_text(const std::string& text) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(
        dwg.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), dwg.last_error());
    return dwg;
}

// The record of a style written by the writer of the given version and format: the DIMSTYLE
// records of the file, and whether it was written (the error otherwise)
struct Written {
    bool ok = false;
    std::string error;
    std::vector<Record> records;
};

// A new drawing of the version, written by DxfWriter directly: only the DIMSTYLE records of the
// result are looked at
hdxf::Drawing drawing_for_writer(DxfVersion version) {
    return hdxf::Drawing(version);
}

Written write_styles(DxfVersion version, DxfFormat format, const std::vector<hdxf::DimStyle>& styles) {
    hdxf::Drawing dwg = drawing_for_writer(version);
    for (const auto& s : styles) {
        hdxf::DimStyle& added = dwg.add_dim_style(s.name);
        added = s;
    }
    uint64_t handle = 0x40;
    for (auto& s : dwg.dim_styles()) {
        if (s.handle.empty()) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(handle++));
            s.handle = buf;
        }
    }
    std::ostringstream oss(std::ios::out | std::ios::binary);
    hdxf::internal::DxfWriter writer(oss, version, format);
    writer.set_next_handle(0x100);
    Written w;
    w.ok = writer.write(dwg);
    w.error = writer.error();
    std::string s = oss.str();
    w.records = dimstyle_records(std::vector<uint8_t>(s.begin(), s.end()));
    return w;
}

// The older form of a DIMSTYLE of the 2000 groups that 2000 has instead: 276, 277, 279, 289, 341-344, 371, 372
bool has_2000_groups(const Record& r) {
    for (int code : {276, 277, 278, 279, 289, 341, 342, 343, 344, 371, 372, 79, 179, 148}) {
        if (has_code(r, code)) return true;
    }
    return false;
}

std::vector<uint8_t> save_ok(hdxf::Drawing& dwg, DxfFormat format) {
    std::vector<uint8_t> bytes = dwg.save_to_memory(format);
    hdxf_test::assert_false(bytes.empty(), "save: " + dwg.last_error());
    return bytes;
}

} // namespace

// ---------------------------------------------------------------------------
// The mapping (Autodesk: DIMFIT's documentation, ObjectARX dimunit())
// ---------------------------------------------------------------------------

TEST(dimstyle_legacy_dimunit_mapping) {
    using namespace hdxf::internal;
    struct Row { int unit, lunit, frac; };
    const Row rows[] = {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 4, 0}, {5, 5, 0}, {6, 4, 2}, {7, 5, 2}, {8, 6, 0}};
    for (const auto& r : rows) {
        auto m = modern_from_dimunit(r.unit);
        hdxf_test::assert_true(m.has_value(), "DIMUNIT " + std::to_string(r.unit));
        hdxf_test::assert_eq(m->dimlunit, r.lunit, "DIMLUNIT of DIMUNIT " + std::to_string(r.unit));
        hdxf_test::assert_eq(m->dimfrac, r.frac, "DIMFRAC of DIMUNIT " + std::to_string(r.unit));
        auto back = legacy_dimunit(r.lunit, r.frac);
        hdxf_test::assert_true(back.has_value() && *back == r.unit, "back to DIMUNIT " + std::to_string(r.unit));
    }
    hdxf_test::assert_false(modern_from_dimunit(0).has_value());
    hdxf_test::assert_false(modern_from_dimunit(9).has_value());
    // DIMFRAC means nothing to DIMLUNIT 1, 2, 3 and 6
    hdxf_test::assert_eq(*legacy_dimunit(2, 1), 2);
    hdxf_test::assert_eq(*legacy_dimunit(6, 2), 8);
    // Diagonal stacking has no DIMUNIT; nor has a DIMLUNIT outside 1 … 6
    hdxf_test::assert_false(legacy_dimunit(4, 1).has_value());
    hdxf_test::assert_false(legacy_dimunit(5, 1).has_value());
    hdxf_test::assert_false(legacy_dimunit(0, 0).has_value());
    hdxf_test::assert_false(legacy_dimunit(7, 0).has_value());
}

TEST(dimstyle_legacy_dimfit_mapping) {
    using namespace hdxf::internal;
    for (int fit = 0; fit <= 3; ++fit) {
        auto m = modern_from_dimfit(fit);
        hdxf_test::assert_true(m.has_value());
        hdxf_test::assert_eq(m->dimatfit, fit);
        hdxf_test::assert_eq(m->dimtmove, 0);
        hdxf_test::assert_eq(*legacy_dimfit(fit, 0), fit);
    }
    hdxf_test::assert_eq(modern_from_dimfit(4)->dimatfit, 3);
    hdxf_test::assert_eq(modern_from_dimfit(4)->dimtmove, 1);
    hdxf_test::assert_eq(modern_from_dimfit(5)->dimatfit, 3);
    hdxf_test::assert_eq(modern_from_dimfit(5)->dimtmove, 2);
    hdxf_test::assert_eq(*legacy_dimfit(3, 1), 4);
    hdxf_test::assert_eq(*legacy_dimfit(3, 2), 5);
    hdxf_test::assert_false(modern_from_dimfit(6).has_value());
    hdxf_test::assert_false(modern_from_dimfit(-1).has_value());
    // A text move without DIMATFIT 3 has no DIMFIT
    hdxf_test::assert_false(legacy_dimfit(1, 1).has_value());
    hdxf_test::assert_false(legacy_dimfit(0, 2).has_value());
    hdxf_test::assert_false(legacy_dimfit(4, 0).has_value());
}

TEST(dimstyle_legacy_value_as_read_follows_the_newer_variables) {
    using namespace hdxf::internal;
    hdxf::DimStyle s;
    set_dimunit(s, 6);
    set_dimfit(s, 5);
    hdxf_test::assert_eq(s.dimlunit, 4);
    hdxf_test::assert_eq(s.dimfrac, 2);
    hdxf_test::assert_eq(s.dimatfit, 3);
    hdxf_test::assert_eq(s.dimtmove, 2);
    hdxf_test::assert_eq(*dimunit_of(s), 6);
    hdxf_test::assert_eq(*dimfit_of(s), 5);
    // Changed through the newer variables: they decide
    s.dimlunit = 5;
    s.dimfrac = 0;
    s.dimtmove = 1;
    hdxf_test::assert_eq(*dimunit_of(s), 5);
    hdxf_test::assert_eq(*dimfit_of(s), 4);
    // A value outside the ranges stays while the newer variables are untouched, and goes when they change
    hdxf::DimStyle odd;
    set_dimunit(odd, 0);
    set_dimfit(odd, 9);
    hdxf_test::assert_eq(*dimunit_of(odd), 0);
    hdxf_test::assert_eq(*dimfit_of(odd), 9);
    odd.dimlunit = 3;
    odd.dimatfit = 1;
    hdxf_test::assert_eq(*dimunit_of(odd), 3);
    hdxf_test::assert_eq(*dimfit_of(odd), 1);
    // A style made in memory has neither field: the newer variables give them
    hdxf::DimStyle fresh;
    hdxf_test::assert_false(fresh.dimunit.has_value());
    hdxf_test::assert_eq(*dimunit_of(fresh), 2);
    hdxf_test::assert_eq(*dimfit_of(fresh), 3);
}

// ---------------------------------------------------------------------------
// Reading AutoCAD's own files (7.7)
// ---------------------------------------------------------------------------

TEST(dimstyle_r13_read_small_r13_standard) {
    if (!file_exists("ezdxf/small_r13.dxf")) hdxf_test::skip("ezdxf/small_r13.dxf is not there");
    hdxf::Drawing dwg = load_file("ezdxf/small_r13.dxf");
    hdxf_test::assert_true(dwg.version() == DxfVersion::R13);
    const hdxf::DimStyle* s = find_style(dwg, "STANDARD");
    hdxf_test::assert_true(s != nullptr, "STANDARD");
    hdxf_test::assert_eq(s->handle, "6F", "105 is the handle");
    hdxf_test::assert_true(s->dimunit.has_value() && *s->dimunit == 2, "270");
    hdxf_test::assert_true(s->dimfit.has_value() && *s->dimfit == 3, "287");
    hdxf_test::assert_eq(s->dimlunit, 2);
    hdxf_test::assert_eq(s->dimfrac, 0);
    hdxf_test::assert_eq(s->dimatfit, 3);
    hdxf_test::assert_eq(s->dimtmove, 0);
    hdxf_test::assert_eq(s->dimblk, "", "5 is DIMBLK, not the handle");
    hdxf_test::assert_eq(s->dimblk1, "");
    hdxf_test::assert_eq(s->dimblk2, "");
    hdxf_test::assert_eq(s->dimtxsty, "STANDARD", "340 is the text style's handle, 11");
    hdxf_test::assert_eq(s->dimasz, 0.18);
    hdxf_test::assert_eq(s->dimtih, 1);
    hdxf_test::assert_eq(s->dimtad, 0);
    hdxf_test::assert_eq(s->dimdec, 4);
    hdxf_test::assert_eq(s->dimupt, 0);
    hdxf_test::assert_false(s->raw.empty(), "the record as read");
}

TEST(dimstyle_r13_r14_read_iso25_of_the_binary_files) {
    for (const char* file : {"ezdxf/bin_dxf_r13.dxf", "ezdxf/bin_dxf_r14.dxf"}) {
        if (!file_exists(file)) hdxf_test::skip(std::string(file) + " is not there");
        hdxf::Drawing dwg = load_file(file);
        const hdxf::DimStyle* s = find_style(dwg, "ISO-25");
        hdxf_test::assert_true(s != nullptr, std::string(file) + ": ISO-25");
        hdxf_test::assert_eq(s->handle, "42", std::string(file));
        hdxf_test::assert_true(s->dimunit && *s->dimunit == 2 && s->dimfit && *s->dimfit == 3, file);
        hdxf_test::assert_eq(s->dimdec, 2);
        hdxf_test::assert_eq(s->dimtdec, 2);
        hdxf_test::assert_eq(s->dimaltu, 2);
        hdxf_test::assert_eq(s->dimalttd, 3);
        hdxf_test::assert_eq(s->dimzin, 8);
        hdxf_test::assert_eq(s->dimtzin, 8);
        hdxf_test::assert_eq(s->dimtad, 1);
        hdxf_test::assert_eq(s->dimexe, 1.25);
        hdxf_test::assert_eq(s->dimaltf, 0.03937007874);
        hdxf_test::assert_eq(s->dimtxsty, "STANDARD");
        hdxf_test::assert_eq(s->dimblk, "");
    }
}

// The groups of the DIMSTYLE as the style writes them (7.8) against AutoCAD's own record: the
// codes, their order, their values, in the three files
TEST(dimstyle_r13_r14_legacy_groups_equal_autocads_records) {
    struct File { const char* name; const char* style; };
    for (const File& f : {File{"ezdxf/small_r13.dxf", "STANDARD"}, File{"ezdxf/bin_dxf_r13.dxf", "ISO-25"},
                          File{"ezdxf/bin_dxf_r14.dxf", "ISO-25"}}) {
        if (!file_exists(f.name)) hdxf_test::skip(std::string(f.name) + " is not there");
        std::vector<Record> records = dimstyle_records(read_bytes(external(f.name)));
        const Record* file_record = record_named(records, f.style);
        hdxf_test::assert_true(file_record != nullptr, f.name);
        Record expected = body_of(*file_record);
        hdxf::Drawing dwg = load_file(f.name);
        const hdxf::DimStyle* s = find_style(dwg, f.style);
        hdxf_test::assert_true(s != nullptr, f.name);

        std::string error;
        std::vector<LegacyGroup> groups =
            hdxf::internal::legacy_dimstyle_groups(*s, text_of(*file_record, 340), error);
        hdxf_test::assert_eq(error, "", f.name);
        hdxf_test::assert_eq(static_cast<int>(groups.size()), static_cast<int>(expected.size()),
                             std::string(f.name) + ": number of groups");
        for (size_t i = 0; i < groups.size(); ++i) {
            const std::string at = std::string(f.name) + ", group " + std::to_string(i) + " (" +
                                   std::to_string(expected[i].first) + ")";
            hdxf_test::assert_eq(groups[i].code, expected[i].first, at + ": code");
            switch (groups[i].kind) {
                case LegacyGroup::Kind::String:
                    hdxf_test::assert_eq(groups[i].text, expected[i].second, at);
                    break;
                case LegacyGroup::Kind::Int:
                    hdxf_test::assert_eq(groups[i].integer, std::atoi(expected[i].second.c_str()), at);
                    break;
                case LegacyGroup::Kind::Double:
                    hdxf_test::assert_eq(groups[i].real, std::atof(expected[i].second.c_str()), at);
                    break;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Reading the older form: other DIMUNIT, DIMFIT and arrow block names (7.7)
// ---------------------------------------------------------------------------

TEST(dimstyle_r13_r14_read_every_dimunit_and_dimfit) {
    struct Unit { int unit, lunit, frac; };
    for (const char* version : {"AC1012", "AC1014"}) {
        std::vector<OlderStyle> styles;
        const Unit units[] = {{1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 4, 0}, {5, 5, 0}, {6, 4, 2}, {7, 5, 2}, {8, 6, 0}};
        for (const auto& u : units) {
            OlderStyle s;
            s.name = "U" + std::to_string(u.unit);
            s.handle = "5" + std::to_string(u.unit);
            s.unit = u.unit;
            styles.push_back(s);
        }
        struct Fit { int fit, atfit, tmove; };
        const Fit fits[] = {{0, 0, 0}, {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 3, 1}, {5, 3, 2}};
        for (const auto& f : fits) {
            OlderStyle s;
            s.name = "F" + std::to_string(f.fit);
            s.handle = "6" + std::to_string(f.fit);
            s.fit = f.fit;
            styles.push_back(s);
        }
        OlderStyle odd;       // outside what AutoCAD defines: kept as it was
        odd.name = "Odd";
        odd.handle = "70";
        odd.unit = 0;
        odd.fit = 6;
        styles.push_back(odd);

        hdxf::Drawing dwg = load_text(older_file(version, styles));
        for (const auto& u : units) {
            const hdxf::DimStyle* s = find_style(dwg, "U" + std::to_string(u.unit));
            hdxf_test::assert_true(s != nullptr, "U" + std::to_string(u.unit));
            hdxf_test::assert_true(s->dimunit && *s->dimunit == u.unit, "DIMUNIT as read");
            hdxf_test::assert_eq(s->dimlunit, u.lunit, std::string(version) + " DIMUNIT " + std::to_string(u.unit));
            hdxf_test::assert_eq(s->dimfrac, u.frac, std::string(version) + " DIMUNIT " + std::to_string(u.unit));
        }
        for (const auto& f : fits) {
            const hdxf::DimStyle* s = find_style(dwg, "F" + std::to_string(f.fit));
            hdxf_test::assert_true(s != nullptr, "F" + std::to_string(f.fit));
            hdxf_test::assert_true(s->dimfit && *s->dimfit == f.fit, "DIMFIT as read");
            hdxf_test::assert_eq(s->dimatfit, f.atfit, std::string(version) + " DIMFIT " + std::to_string(f.fit));
            hdxf_test::assert_eq(s->dimtmove, f.tmove, std::string(version) + " DIMFIT " + std::to_string(f.fit));
        }
        const hdxf::DimStyle* s = find_style(dwg, "Odd");
        hdxf_test::assert_true(s != nullptr);
        hdxf_test::assert_true(s->dimunit && *s->dimunit == 0, "DIMUNIT 0 kept");
        hdxf_test::assert_true(s->dimfit && *s->dimfit == 6, "DIMFIT 6 kept");
        hdxf_test::assert_eq(s->dimlunit, 2, "the newer variables are left alone");
        hdxf_test::assert_eq(s->dimatfit, 3);
    }
}

TEST(dimstyle_r13_r14_read_code_5_is_a_name_and_105_the_handle) {
    for (const char* version : {"AC1012", "AC1014"}) {
        OlderStyle s;
        s.handle = "4B";
        s.blk = "DOT";
        s.blk1 = "ARCHTICK";
        s.blk2 = "OPEN";
        s.text_style_handle = "12";
        hdxf::Drawing dwg = load_text(older_file(version, {s}));
        const hdxf::DimStyle* r = find_style(dwg, "Older");
        hdxf_test::assert_true(r != nullptr, version);
        hdxf_test::assert_eq(r->handle, "4B", "105");
        hdxf_test::assert_eq(r->dimblk, "DOT");
        hdxf_test::assert_eq(r->dimblk1, "ARCHTICK");
        hdxf_test::assert_eq(r->dimblk2, "OPEN");
        hdxf_test::assert_eq(r->dimtxsty, "Notes", "340 names the text style by its handle");
        // The groups it writes have the names where AutoCAD has them
        std::string error;
        std::vector<LegacyGroup> groups = hdxf::internal::legacy_dimstyle_groups(*r, "12", error);
        hdxf_test::assert_eq(error, "");
        std::vector<std::string> names;
        for (const auto& g : groups) {
            if (g.code >= 5 && g.code <= 7) names.push_back(g.text);
        }
        hdxf_test::assert_eq(static_cast<int>(names.size()), 3);
        hdxf_test::assert_eq(names[0], "DOT");
        hdxf_test::assert_eq(names[1], "ARCHTICK");
        hdxf_test::assert_eq(names[2], "OPEN");
    }
}

// A dimension's DSTYLE override with 270 and 287 (R13/R14's XDATA form) gives the same newer variables
TEST(dimstyle_r13_r14_read_does_not_apply_270_287_in_2000_files) {
    // A 2000 file has DIMLUNIT/DIMATFIT: the older groups there (not written by AutoCAD) are not read
    // as DIMUNIT/DIMFIT, the record is preserved as it is
    std::string text = older_file("AC1015", {OlderStyle{}});
    hdxf::Drawing dwg = load_text(text);
    const hdxf::DimStyle* s = find_style(dwg, "Older");
    if (s) {      // a minimal 2000 file may not be taken at all; when it is, 270/287 stay unread
        hdxf_test::assert_false(s->dimunit.has_value());
        hdxf_test::assert_false(s->dimfit.has_value());
    }
}

// ---------------------------------------------------------------------------
// What the old form cannot say (7.8)
// ---------------------------------------------------------------------------

TEST(dimstyle_r13_r14_error_for_what_has_no_older_form) {
    using hdxf::internal::dimstyle_older_error;
    hdxf_test::assert_eq(dimstyle_older_error(hdxf::DimStyle{}), "", "a default style can be written");

    struct Case { const char* variable; void (*apply)(hdxf::DimStyle&); };
    const Case cases[] = {
        {"DIMALTRND", [](hdxf::DimStyle& s) { s.dimaltrnd = 0.5; }},
        {"DIMAZIN", [](hdxf::DimStyle& s) { s.dimazin = 1; }},
        {"DIMADEC", [](hdxf::DimStyle& s) { s.dimadec = 2; }},
        {"DIMDSEP", [](hdxf::DimStyle& s) { s.dimdsep = ','; }},
        {"DIMLDRBLK", [](hdxf::DimStyle& s) { s.dimldrblk = "_DOT"; }},
        {"DIMLWD", [](hdxf::DimStyle& s) { s.dimlwd = 25; }},
        {"DIMLWE", [](hdxf::DimStyle& s) { s.dimlwe = 25; }},
        {"DIMTFILL", [](hdxf::DimStyle& s) { s.dimtfill = 1; }},
        {"DIMTFILLCLR", [](hdxf::DimStyle& s) { s.dimtfillclr = hdxf::Color::from_aci(3); }},
        {"DIMLTYPE", [](hdxf::DimStyle& s) { s.dimltype = "DASHED"; }},
        {"DIMLTEX1", [](hdxf::DimStyle& s) { s.dimltex1 = "DASHED"; }},
        {"DIMLTEX2", [](hdxf::DimStyle& s) { s.dimltex2 = "DASHED"; }},
        {"DIMFXLON", [](hdxf::DimStyle& s) { s.dimfxlon = 1; }},
        {"DIMFXL", [](hdxf::DimStyle& s) { s.dimfxl = 2.0; }},
        {"DIMJOGANG", [](hdxf::DimStyle& s) { s.dimjogang = 30.0; }},
        {"DIMTXTDIRECTION", [](hdxf::DimStyle& s) { s.dimtxtdirection = 1; }},
        {"DIMARCSYM", [](hdxf::DimStyle& s) { s.dimarcsym = 1; }},
        {"DIMFRAC (diagonal stacking)", [](hdxf::DimStyle& s) { s.dimlunit = 4; s.dimfrac = 1; }},
        {"DIMTMOVE (without DIMATFIT 3)", [](hdxf::DimStyle& s) { s.dimatfit = 1; s.dimtmove = 1; }},
    };
    for (const auto& c : cases) {
        hdxf::DimStyle s;
        s.name = "Newer";
        c.apply(s);
        hdxf_test::assert_eq(dimstyle_older_error(s),
                             std::string("Dimension style \"Newer\": ") + c.variable + " requires DXF 2000 or later",
                             c.variable);
    }
    // A value that is no DIMLUNIT at all is not a question of version
    hdxf::DimStyle bad;
    bad.dimlunit = 9;
    hdxf_test::assert_true(dimstyle_older_error(bad).find("has no DIMUNIT") != std::string::npos);
    // The ones with an older form are no error: they are DIMUNIT and DIMFIT
    hdxf::DimStyle ok;
    ok.dimlunit = 5;
    ok.dimfrac = 2;
    ok.dimatfit = 3;
    ok.dimtmove = 2;
    hdxf_test::assert_eq(dimstyle_older_error(ok), "");
}

TEST(dimstyle_r13_r14_writer_raises_the_version_error) {
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        hdxf::DimStyle s;
        s.name = "Newer";
        s.dimdsep = ',';
        Written w = write_styles(version, DxfFormat::Ascii, {s});
        hdxf_test::assert_false(w.ok, "written");
        hdxf_test::assert_true(w.error.find("DIMDSEP requires DXF 2000 or later") != std::string::npos, w.error);
    }
}

// ---------------------------------------------------------------------------
// Writing a new style, ASCII and binary (7.8)
// ---------------------------------------------------------------------------

TEST(dimstyle_r13_r14_written_form_ascii_and_binary) {
    hdxf::DimStyle s;
    s.name = "Plan";
    s.handle = "4C";
    s.dimblk = "DOT";
    s.dimblk1 = "ARCHTICK";
    s.dimblk2 = "OPEN";
    s.dimlunit = 4;
    s.dimfrac = 2;      // DIMUNIT 6
    s.dimatfit = 3;
    s.dimtmove = 1;     // DIMFIT 4
    s.dimdec = 3;
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string what = std::string(version == DxfVersion::R13 ? "R13" : "R14") +
                                     (format == DxfFormat::Ascii ? " ASCII" : " binary");
            Written w = write_styles(version, format, {s});
            hdxf_test::assert_true(w.ok, what + ": " + w.error);
            const Record* r = record_named(w.records, "Plan");
            hdxf_test::assert_true(r != nullptr, what);
            // The head: 105 first (not 5), the two subclass markers, no owner 330 in a new drawing (family N, D1)
            hdxf_test::assert_eq((*r)[0].first, 105, what);
            hdxf_test::assert_eq((*r)[0].second, "4C", what);
            hdxf_test::assert_false(has_code(*r, 5) && (*r)[0].first == 5, what);
            hdxf_test::assert_false(has_code(*r, 330), what + ": no owner");
            hdxf_test::assert_eq(text_of(*r, 100), "AcDbSymbolTableRecord", what);
            hdxf_test::assert_eq(text_of(*r, 270), "6", what + ": DIMUNIT");
            hdxf_test::assert_eq(text_of(*r, 287), "4", what + ": DIMFIT");
            hdxf_test::assert_eq(text_of(*r, 271), "3", what);
            hdxf_test::assert_false(has_2000_groups(*r), what + ": no 2000 groups");
            // 5, 6, 7 are the names
            Record body = body_of(*r);
            std::vector<std::string> names;
            for (const auto& g : body) if (g.first >= 5 && g.first <= 7) names.push_back(g.second);
            hdxf_test::assert_eq(static_cast<int>(names.size()), 3, what);
            hdxf_test::assert_eq(names[0], "DOT", what);
            hdxf_test::assert_eq(names[1], "ARCHTICK", what);
            hdxf_test::assert_eq(names[2], "OPEN", what);
            // Every variable, in AutoCAD's order: 70, 3 … 7, 40 … 48, 140 … 147, 71 … 78, 170 … 178, 270 … 274, 340, 275, 280 … 288
            std::vector<int> codes;
            for (const auto& g : body) codes.push_back(g.first);
            std::vector<int> expected = {2, 70, 3, 4, 5, 6, 7, 40, 41, 42, 43, 44, 45, 46, 47, 48, 140, 141, 142, 143, 144,
                                         145, 146, 147, 71, 72, 73, 74, 75, 76, 77, 78, 170, 171, 172, 173, 174, 175, 176,
                                         177, 178, 270, 271, 272, 273, 274, 340, 275, 280, 281, 282, 283, 284, 285, 286,
                                         287, 288};
            hdxf_test::assert_true(codes == expected, what + ": codes and order");
        }
    }
}

// What the writer wrote reads back as the same typed style (the reader of 7.7 against the writer of 7.8)
TEST(dimstyle_r13_r14_written_style_reads_back) {
    hdxf::DimStyle s;
    s.name = "Back";
    s.dimblk = "DOT";
    s.dimlunit = 5;
    s.dimfrac = 0;
    s.dimatfit = 2;
    s.dimdec = 1;
    s.dimtad = 1;
    s.dimtxt = 3.5;
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string what = std::string(version == DxfVersion::R13 ? "R13 " : "R14 ") +
                                     (format == DxfFormat::Ascii ? "ASCII" : "binary");
            hdxf::Drawing dwg = drawing_for_writer(version);
            hdxf::DimStyle& added = dwg.add_dim_style("Back");
            added = s;
            added.handle = "4D";
            std::ostringstream oss(std::ios::out | std::ios::binary);
            hdxf::internal::DxfWriter writer(oss, version, format);
            writer.set_next_handle(0x100);
            hdxf_test::assert_true(writer.write(dwg), what + ": " + writer.error());
            std::string bytes = oss.str();
            hdxf::Drawing back;
            hdxf_test::assert_true(
                back.load_from_memory(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()),
                what + ": " + back.last_error());
            const hdxf::DimStyle* r = find_style(back, "Back");
            hdxf_test::assert_true(r != nullptr, what);
            hdxf_test::assert_eq(r->handle, "4D", what);
            hdxf_test::assert_eq(r->dimblk, "DOT", what);
            hdxf_test::assert_eq(r->dimlunit, 5, what);
            hdxf_test::assert_eq(r->dimfrac, 0, what);
            hdxf_test::assert_eq(r->dimatfit, 2, what);
            hdxf_test::assert_eq(r->dimdec, 1, what);
            hdxf_test::assert_eq(r->dimtxt, 3.5, what);
            hdxf_test::assert_true(r->dimunit && *r->dimunit == 5, what);
            hdxf_test::assert_true(r->dimfit && *r->dimfit == 2, what);
        }
    }
}

// The snapshot compares the style as written now with the one as read: a style changed in its older
// typed data differs in 270 or 287, an unchanged one does not
TEST(dimstyle_r13_r14_changed_older_data_is_seen) {
    if (!file_exists("ezdxf/bin_dxf_r14.dxf")) hdxf_test::skip("ezdxf/bin_dxf_r14.dxf is not there");
    hdxf::Drawing dwg = load_file("ezdxf/bin_dxf_r14.dxf");
    hdxf::Drawing original = load_file("ezdxf/bin_dxf_r14.dxf");
    const hdxf::DimStyle* o = find_style(original, "ISO-25");
    hdxf::DimStyle* s = nullptr;
    for (auto& d : dwg.dim_styles()) if (d.name == "ISO-25") s = &d;
    hdxf_test::assert_true(o != nullptr && s != nullptr);

    auto groups = [](const hdxf::DimStyle& d) {
        std::string error;
        return hdxf::internal::legacy_dimstyle_groups(d, "11", error);
    };
    auto same = [](const std::vector<LegacyGroup>& a, const std::vector<LegacyGroup>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i].code != b[i].code || a[i].text != b[i].text || a[i].integer != b[i].integer ||
                a[i].real != b[i].real) return false;
        }
        return true;
    };
    hdxf_test::assert_true(same(groups(*s), groups(*o)), "unchanged: the same groups");

    s->dimlunit = 3;                                 // changed through the newer variable
    hdxf_test::assert_false(same(groups(*s), groups(*o)), "DIMLUNIT changed");
    for (const auto& g : groups(*s)) if (g.code == 270) hdxf_test::assert_eq(g.integer, 3);
    s->dimlunit = 2;
    hdxf_test::assert_true(same(groups(*s), groups(*o)), "changed back");
    s->dimtmove = 2;                                 // DIMFIT 5
    for (const auto& g : groups(*s)) if (g.code == 287) hdxf_test::assert_eq(g.integer, 5);
    hdxf_test::assert_false(same(groups(*s), groups(*o)), "DIMTMOVE changed");
    s->dimtmove = 0;
    s->dimblk = "DOT";                               // the arrow block's name
    hdxf_test::assert_false(same(groups(*s), groups(*o)), "DIMBLK changed");
}

// ---------------------------------------------------------------------------
// 2000 and later are as they were
// ---------------------------------------------------------------------------

TEST(dimstyle_r13_r14_2000_form_is_unchanged) {
    hdxf::DimStyle s;
    s.name = "Modern";
    s.dimlunit = 4;
    s.dimfrac = 2;
    s.dimatfit = 2;
    s.dimtmove = 1;
    s.dimdsep = ',';
    hdxf::Drawing dwg(DxfVersion::R2000);
    hdxf::DimStyle& added = dwg.add_dim_style("Modern");
    added = s;
    std::vector<uint8_t> bytes = dwg.save_to_memory(DxfFormat::Ascii);
    hdxf_test::assert_false(bytes.empty(), dwg.last_error());
    const std::vector<Record> r_records = dimstyle_records(bytes);
    const Record* r = record_named(r_records, "Modern");
    hdxf_test::assert_true(r != nullptr);
    hdxf_test::assert_eq(text_of(*r, 277), "4", "DIMLUNIT " + dump(*r));
    hdxf_test::assert_eq(text_of(*r, 276), "2", "DIMFRAC");
    hdxf_test::assert_eq(text_of(*r, 289), "2", "DIMATFIT");
    hdxf_test::assert_eq(text_of(*r, 279), "1", "DIMTMOVE");
    hdxf_test::assert_eq(text_of(*r, 278), std::to_string(','), "DIMDSEP");
    hdxf_test::assert_false(has_code(*r, 270), "no DIMUNIT in 2000");
    hdxf_test::assert_false(has_code(*r, 287), "no DIMFIT in 2000");
    hdxf_test::assert_true(has_code(*r, 340), "340");

    // The same as 2007 and 2018 write it: no 270/287 (the older groups are for R13 and R14 only)
    for (DxfVersion version : {DxfVersion::R2007, DxfVersion::R2018}) {
        hdxf::Drawing newer(version);
        hdxf::DimStyle& n = newer.add_dim_style("Modern");
        n = s;
        std::vector<uint8_t> out = newer.save_to_memory(DxfFormat::Ascii);
        hdxf_test::assert_false(out.empty(), newer.last_error());
        const std::vector<Record> nr_records = dimstyle_records(out);
        const Record* nr = record_named(nr_records, "Modern");
        hdxf_test::assert_true(nr != nullptr);
        hdxf_test::assert_eq(text_of(*nr, 277), "4");
        hdxf_test::assert_false(has_code(*nr, 270));
        hdxf_test::assert_false(has_code(*nr, 287));
    }
}

// ---------------------------------------------------------------------------
// Whole drawings (the R13/R14 writer of the drawing, 7.2-7.5)
// ---------------------------------------------------------------------------

// A new R13/R14 drawing with a style: written, read again, the style the same
TEST(dimstyle_r13_r14_new_drawing_roundtrip) {
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            hdxf::Drawing dwg(version);
            hdxf::DimStyle& s = dwg.add_dim_style("Plan");
            s.dimblk1 = "ARCHTICK";
            s.dimlunit = 4;
            s.dimfrac = 2;
            s.dimatfit = 3;
            s.dimtmove = 2;
            std::vector<uint8_t> bytes = save_ok(dwg, format);
            const std::vector<Record> r_records = dimstyle_records(bytes);
            const Record* r = record_named(r_records, "Plan");
            hdxf_test::assert_true(r != nullptr);
            hdxf_test::assert_eq(text_of(*r, 270), "6");
            hdxf_test::assert_eq(text_of(*r, 287), "5");
            hdxf_test::assert_false(has_2000_groups(*r));
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(bytes.data(), bytes.size()), back.last_error());
            const hdxf::DimStyle* b = find_style(back, "Plan");
            hdxf_test::assert_true(b != nullptr);
            hdxf_test::assert_eq(b->dimblk1, "ARCHTICK");
            hdxf_test::assert_eq(b->dimlunit, 4);
            hdxf_test::assert_eq(b->dimfrac, 2);
            hdxf_test::assert_eq(b->dimtmove, 2);
        }
    }
}

// A style of AutoCAD's own R13/R14 file: unchanged is the file's bytes, changed has only its changed
// groups anew
TEST(dimstyle_r13_r14_read_file_unchanged_and_changed) {
    for (const char* file : {"ezdxf/bin_dxf_r13.dxf", "ezdxf/bin_dxf_r14.dxf"}) {
        if (!file_exists(file)) hdxf_test::skip(std::string(file) + " is not there");
        std::vector<uint8_t> original = read_bytes(external(file));
        hdxf::Drawing dwg = load_file(file);
        std::vector<uint8_t> same = save_ok(dwg, DxfFormat::Binary);
        hdxf_test::assert_true(same == original, std::string(file) + ": an unchanged file is written as it was");

        hdxf::DimStyle* iso = nullptr;
        for (auto& d : dwg.dim_styles()) if (d.name == "ISO-25") iso = &d;
        hdxf_test::assert_true(iso != nullptr);
        iso->dimlunit = 3;
        iso->dimatfit = 1;
        std::vector<uint8_t> changed = save_ok(dwg, DxfFormat::Binary);
        const std::vector<Record> r_records = dimstyle_records(changed);
        const Record* r = record_named(r_records, "ISO-25");
        hdxf_test::assert_true(r != nullptr);
        hdxf_test::assert_eq(text_of(*r, 270), "3");
        hdxf_test::assert_eq(text_of(*r, 287), "1");
        hdxf_test::assert_eq(text_of(*r, 105), "42");
        hdxf_test::assert_eq(text_of(*r, 271), "2", "what did not change stays");
        hdxf_test::assert_false(has_2000_groups(*r));
        // A newer setting without an older form is an error and nothing is written
        iso->dimdsep = ',';
        std::vector<uint8_t> refused = dwg.save_to_memory(DxfFormat::Binary);
        hdxf_test::assert_true(refused.empty());
        hdxf_test::assert_true(dwg.last_error().find("DIMDSEP requires DXF 2000 or later") != std::string::npos,
                               dwg.last_error());
    }
}
