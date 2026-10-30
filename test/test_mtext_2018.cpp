#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include "mtext_data.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MTEXT in DXF 2018 (phase 6, steps 6.3 and 6.4 — plan B72-B74): the columns of the embedded
// object (101 Embedded Object) typed, read from 2018's embedded object and from the XDATA of the
// versions before it, written as the version has them, and the rest of the record left alone.
// ---------------------------------------------------------------------------

namespace {

using Record = std::vector<std::pair<int, std::string>>;

std::vector<Record> records_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Record> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0 || out.empty()) out.emplace_back();
        out.back().push_back({gc.code, dxf_structure::number_text(gc.code, gc.value)});
    }
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

Record record(const std::vector<Record>& records, const std::string& handle) {
    for (const auto& r : records) {
        for (const auto& [code, v] : r) {
            if ((code == 5 || code == 105) && v == handle) return r;
            if (code == 100) break;
        }
    }
    return {};
}

std::string value(const Record& r, int code, int occurrence = 0) {
    for (const auto& [c, v] : r) {
        if (c == code && occurrence-- == 0) return v;
    }
    return {};
}

// The groups of a record from its first embedded object (101) to its XDATA
Record embedded_of(const Record& r) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first >= 1000) break;
        if (g.first == 101) in = true;
        if (in) out.push_back(g);
    }
    return out;
}

// The groups before the first embedded object (what the entity itself has)
Record parent_of(const Record& r) {
    Record out;
    for (const auto& g : r) {
        if (g.first == 101 || g.first >= 1000) break;
        out.push_back(g);
    }
    return out;
}

bool has_files(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (!std::filesystem::exists(external(n))) {
            hdxf_test::skip("external test file missing: " + n);
            return false;
        }
    }
    return true;
}

hdxf::Entity* find_entity(hdxf::Drawing& dwg, const std::string& handle) {
    for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    for (auto& b : dwg.blocks()) for (auto& e : b.entities) if (e.props.handle == handle) return &e;
    return nullptr;
}

double number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

const char* const kAscii = "acadsharp/sample_AC1032_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1032_binary.dxf";
const char* const kAscii2013 = "acadsharp/sample_AC1027_ascii.dxf";

// A group as an ASCII DXF file has it
std::string group_text(int code, const std::string& v) {
    std::string c = std::to_string(code);
    return std::string(c.size() < 3 ? 3 - c.size() : 0, ' ') + c + "\n" + v + "\n";
}

// AutoCAD's 2018 file with `extra` (groups as text) added at the end of the record `handle`
std::vector<uint8_t> with_groups(const std::string& file, const std::string& handle, const std::string& extra) {
    std::vector<uint8_t> bytes = read_bytes(external(file));
    std::string text(bytes.begin(), bytes.end());
    size_t at = text.find("\n  5\n" + handle + "\n");
    hdxf_test::assert_true(at != std::string::npos, "record " + handle);
    size_t end = text.find("\n  0\n", at);
    text.insert(end + 1, extra);
    return std::vector<uint8_t>(text.begin(), text.end());
}

} // namespace

// The columns of AutoCAD's four MTEXT in ENTITIES (3EC, 3F5, 3F6, 779 — ACadSharp's 2018 sample):
// each embedded group typed (B73), the nulls too, and the entity's own values untouched — the
// embedded object's 10, 11, 40, 41 are not the entity's (B72)
TEST(mtext_2018_embedded_object_is_typed) {
    for (const char* file : {kAscii, kBinary}) {
        if (!has_files({file})) return;
        std::vector<uint8_t> in = read_bytes(external(file));
        std::vector<Record> recs = records_of(in);
        hdxf::Drawing dwg = load(in);
        int checked = 0;
        for (const char* h : {"3EC", "3F5", "3F6", "779"}) {
            hdxf::Entity* e = find_entity(dwg, h);
            hdxf_test::assert_true(e != nullptr && e->type == hdxf::EntityType::MText, std::string(h) + ": an MTEXT");
            if (!e) continue;
            const hdxf::MTextData& d = e->as_mtext();
            Record r = record(recs, h);
            Record emb = embedded_of(r);
            hdxf_test::assert_true(!emb.empty() && emb[0].second == "Embedded Object", "an embedded object");
            hdxf_test::assert_true(d.columns.has_value() && d.embedded.has_value(), std::string(h) + ": columns");
            if (!d.columns || !d.embedded) continue;
            ++checked;
            std::string what = std::string(file) + " " + h;
            const hdxf::MTextColumns& c = *d.columns;
            const hdxf::MTextEmbedded& m = *d.embedded;
            // AutoCAD takes the embedded object's attachment, direction, insertion point and defined
            // height over the entity's own: they are the entity's fields (B74)
            hdxf_test::assert_eq(d.attachment, std::atoi(value(emb, 70).c_str()), what + ": 70");
            hdxf_test::assert_eq(d.x_axis_direction.x, number(value(emb, 10)), what + ": direction x");
            hdxf_test::assert_eq(d.x_axis_direction.y, number(value(emb, 20)), what + ": direction y");
            hdxf_test::assert_eq(d.x_axis_direction.z, number(value(emb, 30)), what + ": direction z");
            hdxf_test::assert_eq(d.insertion_point.x, number(value(emb, 11)), what + ": insertion x");
            hdxf_test::assert_eq(d.insertion_point.y, number(value(emb, 21)), what + ": insertion y");
            hdxf_test::assert_eq(d.insertion_point.z, number(value(emb, 31)), what + ": insertion z");
            hdxf_test::assert_eq(d.reference_width, number(value(emb, 40)), what + ": 40");
            hdxf_test::assert_eq(d.defined_height, number(value(emb, 41)), what + ": 41");
            hdxf_test::assert_eq(m.measured_width, number(value(emb, 42)), what + ": 42");
            hdxf_test::assert_eq(m.measured_height, number(value(emb, 43)), what + ": 43");
            hdxf_test::assert_eq(c.type, std::atoi(value(emb, 71).c_str()), what + ": type");
            hdxf_test::assert_eq(c.count, std::atoi(value(emb, 72).c_str()), what + ": count");
            hdxf_test::assert_eq(c.width, number(value(emb, 44)), what + ": width");
            hdxf_test::assert_eq(c.gutter, number(value(emb, 45)), what + ": gutter");
            hdxf_test::assert_eq(c.auto_height, std::atoi(value(emb, 73).c_str()) != 0, what + ": auto height");
            hdxf_test::assert_eq(c.reversed, std::atoi(value(emb, 74).c_str()) != 0, what + ": reversed");
            hdxf_test::assert_eq(static_cast<int>(c.heights.size()), 1, what + ": one height");
            hdxf_test::assert_eq(c.heights.at(0), 0.0, what + ": the height 0");
            // The entity's own groups are the entity's: 10, 40, 41, 46, 71, 72, 73, 44 of the parent
            hdxf_test::assert_eq(d.insertion_point.x, number(value(r, 10)), what + ": the entity's insertion x");
            hdxf_test::assert_eq(d.insertion_point.y, number(value(r, 20)), what + ": the entity's insertion y");
            hdxf_test::assert_eq(d.height, number(value(r, 40)), what + ": the entity's height");
            hdxf_test::assert_eq(d.reference_width, number(value(r, 41)), what + ": the entity's width");
            hdxf_test::assert_eq(d.defined_height, number(value(r, 46)), what + ": the entity's 46");
            hdxf_test::assert_eq(d.attachment, std::atoi(value(r, 71).c_str()), what + ": the entity's attachment");
            hdxf_test::assert_eq(d.line_spacing_style, std::atoi(value(r, 73).c_str()), what + ": 73");
            hdxf_test::assert_eq(d.line_spacing_factor, number(value(r, 44)), what + ": 44");
        }
        hdxf_test::assert_eq(checked, 4, "four MTEXT");
        // An MTEXT without column data has none (513 and the MTEXT of the blocks)
        hdxf::Entity* plain = find_entity(dwg, "513");
        if (plain && plain->type == hdxf::EntityType::MText) {
            hdxf_test::assert_false(plain->as_mtext().columns.has_value(), "513: no columns");
            hdxf_test::assert_false(plain->as_mtext().embedded.has_value(), "513: no embedded object");
        }
    }
}

// The same four in the XDATA of AutoCAD's 2013 file (ACAD_MTEXT_COLUMN_INFO) give the same columns;
// the XDATA leaves the entity (B73), and the 2013 file has no embedded object
TEST(mtext_2018_columns_from_xdata_before_2018) {
    if (!has_files({kAscii2013, kAscii})) return;
    hdxf::Drawing d13 = load(read_bytes(external(kAscii2013)));
    hdxf::Drawing d18 = load(read_bytes(external(kAscii)));
    for (const char* h : {"3EC", "3F5", "3F6", "779"}) {
        hdxf::Entity* a = find_entity(d13, h);
        hdxf::Entity* b = find_entity(d18, h);
        hdxf_test::assert_true(a && b, h);
        if (!a || !b) continue;
        const hdxf::MTextData& x = a->as_mtext();
        const hdxf::MTextData& y = b->as_mtext();
        hdxf_test::assert_true(x.columns.has_value(), std::string(h) + ": columns from the XDATA");
        hdxf_test::assert_false(x.embedded.has_value(), std::string(h) + ": no embedded object in 2013");
        if (!x.columns || !y.columns) continue;
        hdxf_test::assert_eq(x.columns->type, y.columns->type, "type");
        hdxf_test::assert_eq(x.columns->count, y.columns->count, "count");
        hdxf_test::assert_eq(x.columns->width, y.columns->width, "width");
        hdxf_test::assert_eq(x.columns->gutter, y.columns->gutter, "gutter");
        hdxf_test::assert_eq(x.columns->auto_height, y.columns->auto_height, "auto height");
        hdxf_test::assert_eq(x.columns->reversed, y.columns->reversed, "reversed");
        hdxf_test::assert_true(x.columns->heights == y.columns->heights, "heights");
        hdxf_test::assert_true(a->props.xdata.empty(), std::string(h) + ": the column XDATA left the entity");
    }
}

// Unrelated XDATA stays: another application's, and another ACAD block beside the columns
TEST(mtext_2018_other_xdata_stays) {
    if (!has_files({kAscii2013})) return;
    std::vector<uint8_t> bytes = read_bytes(external(kAscii2013));
    std::string text(bytes.begin(), bytes.end());
    size_t at = text.find("\n  5\n3EC\n");
    size_t end = text.find("\n  0\n", at);
    // Another application's list and another ACAD block, after the entity's own
    text.insert(end + 1, group_text(1001, "MY_APP") + group_text(1070, "5") + group_text(1000, "kept") +
                         group_text(1001, "ACAD") + group_text(1000, "ACAD_OTHER") + group_text(1070, "7"));
    std::vector<uint8_t> in(text.begin(), text.end());
    hdxf::Drawing dwg = load(in);
    hdxf::Entity* e = find_entity(dwg, "3EC");
    hdxf_test::assert_true(e && e->as_mtext().columns.has_value(), "columns");
    if (!e) return;
    size_t acad = 0, test = 0;
    for (const auto& x : e->props.xdata) {
        if (x.app_name == "ACAD") ++acad;
        if (x.app_name == "MY_APP") ++test;
        for (const auto& item : x.items) {
            hdxf_test::assert_true(item.value != "ACAD_MTEXT_COLUMN_INFO_BEGIN", "the columns left the XDATA");
        }
    }
    hdxf_test::assert_eq(static_cast<int>(test), 1, "the other application's list");
    hdxf_test::assert_true(acad >= 1, "the other ACAD list");
    // Unchanged: byte for byte; the text changed: the other XDATA still there and the columns too
    hdxf_test::assert_true(save(dwg) == [&] {
        std::vector<uint8_t> lf;
        for (uint8_t b : in) if (b != '\r') lf.push_back(b);
        return lf;
    }(), "unchanged: bytes");
    e->as_mtext().content = "changed";
    Record r = record(records_of(save(dwg)), "3EC");
    bool kept = false, columns = false, other = false;
    for (const auto& [code, v] : r) {
        kept = kept || v == "kept";
        columns = columns || v == "ACAD_MTEXT_COLUMN_INFO_BEGIN";
        other = other || v == "ACAD_OTHER";
    }
    hdxf_test::assert_true(kept && columns && other, "after a change the XDATA is all there");
    hdxf_test::assert_eq(value(r, 1), "changed", "the new text");
}

// Written as the version has it (B74): a text with columns is XDATA in 2000 … 2013 (the block
// first in its ACAD list) and the embedded object in 2018 — ASCII and binary, read back the same
TEST(mtext_2018_columns_written_as_the_version) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007,
                               hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            hdxf::Drawing dwg(v);
            hdxf::Entity& e = dwg.add_mtext({10, 20, 0}, "Kolumn ett\\PKolumn två", 2.5);
            hdxf::MTextColumns c;
            c.type = 1;
            c.count = 3;
            c.width = 40.0;
            c.gutter = 5.0;
            c.auto_height = true;
            c.reversed = true;
            c.heights = {12.5, 13.5, 14.5};
            e.as_mtext().reference_width = 135.0;
            e.as_mtext().columns = c;
            std::vector<uint8_t> out = save(dwg, format);
            std::string handle = dwg.entities()[0].props.handle;     // (saving may add the other columns)
            Record r = record(records_of(out), handle);
            if (v == hdxf::DxfVersion::R2018) {
                Record emb = embedded_of(r);
                hdxf_test::assert_true(emb.size() > 10 && emb[0] == Record::value_type(101, "Embedded Object"), "101");
                hdxf_test::assert_eq(value(emb, 71), "1", "type");
                hdxf_test::assert_eq(value(emb, 72), "3", "count");
                hdxf_test::assert_eq(number(value(emb, 44)), 40.0, "width");
                hdxf_test::assert_eq(number(value(emb, 45)), 5.0, "gutter");
                hdxf_test::assert_eq(value(emb, 73), "1", "auto height");
                hdxf_test::assert_eq(value(emb, 74), "1", "reversed");
                std::vector<double> heights;
                for (const auto& [code, val] : emb) if (code == 46) heights.push_back(number(val));
                hdxf_test::assert_true(heights == std::vector<double>({12.5, 13.5, 14.5}), "three heights");
                hdxf_test::assert_eq(emb.back().first, 46, "the heights last");
                for (const auto& [code, val] : r) hdxf_test::assert_true(code < 1000, "no XDATA in 2018");
            } else {
                hdxf_test::assert_true(embedded_of(r).empty(), "no embedded object before 2018");
                bool begin = false;
                std::vector<std::string> keys;
                for (size_t i = 0; i + 1 < r.size(); ++i) {
                    if (r[i].first == 1000 && r[i].second == "ACAD_MTEXT_COLUMN_INFO_BEGIN") { begin = true; continue; }
                    if (begin && r[i].first == 1070) {
                        keys.push_back(r[i].second + "=" + r[i + 1].second);
                        ++i;
                    }
                    if (r[i].first == 1000 && r[i].second == "ACAD_MTEXT_COLUMN_INFO_END") break;
                }
                hdxf_test::assert_true(begin, "the XDATA block");
                hdxf_test::assert_true(keys.size() >= 6 && keys[0] == "75=1" && keys[1] == "79=1" && keys[2] == "76=3" &&
                                       keys[3] == "78=1" && keys[4].rfind("48=", 0) == 0 && number(keys[4].substr(3)) == 40.0 &&
                                       keys[5].rfind("49=", 0) == 0 && number(keys[5].substr(3)) == 5.0, "the keys in AutoCAD's order");
            }
            hdxf::Drawing back = load(out);
            hdxf::Entity* b = find_entity(back, handle);
            hdxf_test::assert_true(b && b->type == hdxf::EntityType::MText && b->as_mtext().columns.has_value(), "columns read back");
            if (!b || !b->as_mtext().columns) continue;
            const hdxf::MTextColumns& got = *b->as_mtext().columns;
            hdxf_test::assert_eq(got.type, 1, "type back");
            hdxf_test::assert_eq(got.count, 3, "count back");
            hdxf_test::assert_eq(got.width, 40.0, "width back");
            hdxf_test::assert_eq(got.gutter, 5.0, "gutter back");
            hdxf_test::assert_true(got.auto_height && got.reversed, "flags back");
            hdxf_test::assert_true(got.heights == c.heights, "heights back");
            hdxf_test::assert_eq(b->as_mtext().reference_width, 135.0, "the entity's width back");
            // Written back unchanged: the same records
            {
                std::string info;
                for (auto& en : back.entities()) if (en.type == hdxf::EntityType::MText) info += en.props.handle + "(" + en.as_mtext().column_parent + ") ";
                std::vector<Record> x = records_of(save(back, format)), y = records_of(out);
                (void)info;
                for (size_t k = 0; k < x.size() && k < y.size(); ++k) {
                    if (x[k] == y[k]) continue;
                    std::string detail;
                    for (size_t g = 0; g < x[k].size() && g < y[k].size(); ++g) {
                        if (x[k][g] != y[k][g]) { detail = std::to_string(x[k][g].first) + "=" + x[k][g].second + " vs " + std::to_string(y[k][g].first) + "=" + y[k][g].second; break; }
                    }
                    if (x[k][0].second == "SECTION") continue;
                    hdxf_test::assert_true(false, std::string(hdxf::acadver_string(v)) + (format == hdxf::DxfFormat::Binary ? " binary" : " ASCII") + " record " + std::to_string(k) + " " + x[k][0].second + ": " + detail + " [" + info + "]");
                }
            }
            hdxf_test::assert_true(records_of(save(back, format)) == records_of(out), "written back unchanged: version " + std::string(hdxf::acadver_string(v)) + (format == hdxf::DxfFormat::Binary ? " binary" : " ASCII"));
        }
    }
}

// An MTEXT without column data gets no embedded object (B73) and no XDATA
TEST(mtext_2018_plain_mtext_has_no_embedded_object) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    hdxf::Entity& e = dwg.add_mtext({0, 0, 0}, "plain");
    std::vector<uint8_t> out = save(dwg);
    Record r = record(records_of(out), e.props.handle);
    hdxf_test::assert_true(embedded_of(r).empty(), "no 101");
    for (const auto& g : r) hdxf_test::assert_true(g.first < 1000 && g.first != 101, "no XDATA");
    hdxf::Drawing back = load(out);
    hdxf::Entity* b = find_entity(back, e.props.handle);
    hdxf_test::assert_true(b && !b->as_mtext().columns.has_value() && !b->as_mtext().embedded.has_value(), "none read back");
}

// A changed column setting is written anew: the embedded object from the typed values, the rest
// of the record and the other records as they were (B72)
TEST(mtext_2018_changing_the_columns_changes_the_embedded_object_only) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    hdxf::Entity* e = find_entity(dwg, "3EC");
    hdxf_test::assert_true(e && e->as_mtext().columns.has_value(), "3EC");
    if (!e) return;
    e->as_mtext().columns->count = 2;
    e->as_mtext().columns->gutter = 7.5;
    e->as_mtext().columns->heights = {1.0, 2.0};
    std::vector<uint8_t> out = save(dwg);
    std::vector<Record> after = records_of(out);
    Record a = record(after, "3EC"), b = record(before, "3EC");
    hdxf_test::assert_true(parent_of(a) == parent_of(b), "the entity's own groups as they were");
    Record emb = embedded_of(a);
    hdxf_test::assert_eq(value(emb, 72), "2", "count");
    hdxf_test::assert_eq(number(value(emb, 45)), 7.5, "gutter");
    std::vector<double> heights;
    for (const auto& [code, val] : emb) if (code == 46) heights.push_back(number(val));
    hdxf_test::assert_true(heights == std::vector<double>({1.0, 2.0}), "heights");
    // What was not changed in the embedded object is as it was
    Record old_emb = embedded_of(b);
    for (int code : {70, 10, 11, 40, 41, 71, 44, 73, 74}) {
        hdxf_test::assert_eq(value(emb, code), value(old_emb, code), "embedded " + std::to_string(code));
    }
    // Every other record as it was
    int same = 0, total = 0;
    for (const auto& r : before) {
        std::string h;
        for (const auto& [code, v] : r) { if (code == 5) { h = v; break; } if (code == 100) break; }
        if (h.empty() || r[0].second == "SECTION" || h == "3EC") continue;
        ++total;
        if (record(after, h) == r) ++same;
    }
    hdxf_test::assert_eq(same, total, "the other records");
}

// A record's other embedded objects and the XDATA after them stay when the typed one changes or
// the entity does (B72): an unknown embedded object with its own 100, and an application's XDATA
TEST(mtext_2018_unknown_embedded_object_and_xdata_stay) {
    if (!has_files({kAscii})) return;
    std::string extra = group_text(101, "Embedded Object") + group_text(100, "AcDbHdxfTest") + group_text(10, "1.5") +
                        group_text(1, "unknown") + group_text(71, "9") +
                        group_text(1001, "MY_APP") + group_text(1000, "after") + group_text(1070, "3");
    std::vector<uint8_t> in = with_groups(kAscii, "3EC", extra);
    std::vector<uint8_t> lf;
    for (uint8_t b : in) if (b != '\r') lf.push_back(b);
    hdxf::Drawing dwg = load(in);
    hdxf::Entity* e = find_entity(dwg, "3EC");
    hdxf_test::assert_true(e && e->as_mtext().columns.has_value(), "columns");
    if (!e) return;
    // The entity's own values are not overwritten by the unknown part's 10, 1, 71
    hdxf_test::assert_eq(e->as_mtext().content, "this is a Mtext\nwith multiple lines in it", "the text");
    hdxf_test::assert_eq(e->as_mtext().attachment, 1, "attachment");
    hdxf_test::assert_eq(e->as_mtext().insertion_point.x, 183.5889280790413, "insertion");
    size_t lists = 0;
    for (const auto& x : e->props.xdata) {
        ++lists;
        hdxf_test::assert_eq(x.app_name, "MY_APP", "the XDATA after the embedded objects");
    }
    hdxf_test::assert_eq(static_cast<int>(lists), 1, "one XDATA list");
    hdxf_test::assert_true(save(dwg) == lf, "unchanged: bytes");

    // The text changed: the embedded objects and the XDATA stay
    e->as_mtext().content = "new text";
    Record r = record(records_of(save(dwg)), "3EC");
    hdxf_test::assert_eq(value(r, 1), "new text", "the new text");
    Record emb = embedded_of(r);
    int parts = 0;
    for (const auto& g : emb) if (g.first == 101) ++parts;
    hdxf_test::assert_eq(parts, 2, "both embedded objects");
    bool unknown = false;
    for (size_t i = 0; i + 1 < emb.size(); ++i) {
        if (emb[i].first == 100 && emb[i].second == "AcDbHdxfTest" && emb[i + 1] == Record::value_type(10, "1.5")) unknown = true;
    }
    hdxf_test::assert_true(unknown, "the unknown embedded object with its 100");
    hdxf_test::assert_eq(r.back().second, "3", "the XDATA last");

    // The columns changed: the typed object is written anew, the unknown one stays
    e->as_mtext().columns->gutter = 3.0;
    r = record(records_of(save(dwg)), "3EC");
    emb = embedded_of(r);
    parts = 0;
    for (const auto& g : emb) if (g.first == 101) ++parts;
    hdxf_test::assert_eq(parts, 2, "both embedded objects after the columns changed");
    hdxf_test::assert_eq(number(value(emb, 45)), 3.0, "the new gutter");
    unknown = false;
    for (size_t i = 0; i + 1 < emb.size(); ++i) {
        if (emb[i].first == 100 && emb[i].second == "AcDbHdxfTest" && emb[i + 1] == Record::value_type(10, "1.5")) unknown = true;
    }
    hdxf_test::assert_true(unknown, "the unknown embedded object stays");
    hdxf_test::assert_eq(r.back().second, "3", "the XDATA still last");
}

// A long text (parts of 250), UTF-8 and supplementary characters in an MTEXT with an embedded
// object: the text whole and its columns, as written and read back
TEST(mtext_2018_long_and_unicode_text) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    std::string text;
    for (int i = 0; i < 40; ++i) text += "Åäö 日本語 😀 ";
    hdxf::Entity& e = dwg.add_mtext({1, 2, 0}, text, 3.0);
    e.as_mtext().columns = hdxf::MTextColumns{};
    std::vector<uint8_t> out = save(dwg, hdxf::DxfFormat::Binary);
    hdxf::Drawing back = load(out);
    hdxf::Entity* b = find_entity(back, e.props.handle);
    hdxf_test::assert_true(b != nullptr, "the MTEXT");
    if (!b) return;
    hdxf_test::assert_eq(b->as_mtext().content, text, "the text");
    hdxf_test::assert_true(b->as_mtext().columns.has_value(), "the columns");
    std::vector<uint8_t> ascii = save(dwg);
    hdxf::Drawing back_ascii = load(ascii);
    hdxf::Entity* a = find_entity(back_ascii, e.props.handle);
    hdxf_test::assert_true(a && a->as_mtext().content == text, "the text from ASCII");
}

// Copies are independent (B84): the columns and the embedded values are not shared
TEST(mtext_2018_copies_are_independent) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    hdxf::Entity& e = dwg.add_mtext({0, 0, 0}, "x");
    e.as_mtext().columns = hdxf::MTextColumns{};
    e.as_mtext().embedded = hdxf::MTextEmbedded{};
    hdxf::Entity copy = e;
    copy.as_mtext().columns->count = 5;
    copy.as_mtext().embedded->measured_width = 4;
    hdxf_test::assert_eq(e.as_mtext().columns->count, 1, "the original's count");
    hdxf_test::assert_eq(e.as_mtext().embedded->measured_width, 0.0, "the original's 42");
    hdxf::Entity moved = std::move(copy);
    hdxf_test::assert_eq(moved.as_mtext().columns->count, 5, "moved");
    hdxf::Drawing other = dwg;
    other.entities()[0].as_mtext().columns->type = 0;
    hdxf_test::assert_eq(dwg.entities()[0].as_mtext().columns->type, 2, "a copied drawing");
    hdxf_test::assert_true(sizeof(hdxf::Entity) <= 736, "sizeof(Entity) = " + std::to_string(sizeof(hdxf::Entity)));
}

// ---------------------------------------------------------------------------
// Columns and frames before 2018 (plan B74): AutoCAD 2013's own files (ezdxf's integration data)
// ---------------------------------------------------------------------------

namespace {

const char* const kFramedColumns = "ezdxf/mtext_framed_columns.dxf";
const char* const kTextFrame = "ezdxf/mtext_text_frame.dxf";

const hdxf::Entity* find_const(const hdxf::Drawing& dwg, const std::string& handle) {
    return dwg.find_entity(handle);
}

int count_type(hdxf::Drawing& dwg, hdxf::EntityType type) {
    int n = 0;
    for (const auto& e : dwg.entities()) n += e.type == type;
    return n;
}

} // namespace

// AutoCAD's static and dynamic columns with a frame each: the model has one text with its columns
// (the other columns' pieces folded into the first's content, the entities and the frames' polylines
// kept and told whose they are), written back byte for byte
TEST(mtext_2018_autocad_2013_columns_and_frames) {
    if (!has_files({kFramedColumns})) return;
    std::vector<uint8_t> in = read_bytes(external(kFramedColumns));
    hdxf::Drawing dwg = load(in);
    std::vector<Record> recs = records_of(in);

    const hdxf::Entity* staticc = find_const(dwg, "2D");
    const hdxf::Entity* dynamic = find_const(dwg, "33");
    hdxf_test::assert_true(staticc && dynamic, "the two texts");
    if (!staticc || !dynamic) return;
    const hdxf::MTextData& s = staticc->as_mtext();
    hdxf_test::assert_true(s.columns.has_value(), "static: columns");
    hdxf_test::assert_eq(s.columns->type, 1, "static: type");
    hdxf_test::assert_eq(s.columns->count, 3, "static: count");
    hdxf_test::assert_eq(s.columns->width, 20.0, "static: width");
    hdxf_test::assert_eq(s.columns->gutter, 2.0, "static: gutter");
    hdxf_test::assert_true(s.columns->heights.empty(), "static: no heights (no 50)");
    hdxf_test::assert_true(s.columns->linked == std::vector<std::string>({"71", "72"}), "static: the other columns");
    hdxf_test::assert_eq(s.defined_height, 100.0, "static: defined height (46 and the XDATA)");
    hdxf_test::assert_eq(s.background.flags, 17, "static: fill and frame");
    hdxf_test::assert_true(s.background.border_handles == std::vector<std::string>({"6E", "6F", "70"}), "static: one frame each column");
    hdxf_test::assert_true(staticc->props.xdata.size() == 1 && staticc->props.xdata[0].app_name == "AcadAnnotative",
                           "static: only the annotative XDATA is left");
    // The three pieces are one text (the first's content and the other two's, cut after each column break)
    Record p = record(recs, "2D"), a = record(recs, "71"), b = record(recs, "72");
    auto text_of = [](const Record& r) {
        std::string out;
        for (const auto& [code, v] : r) if (code == 3 || code == 1) out += v;
        return out;
    };
    hdxf_test::assert_eq(s.content, text_of(p) + text_of(a) + text_of(b), "static: the text whole");
    const hdxf::Entity* child = find_const(dwg, "71");
    hdxf_test::assert_eq(child->as_mtext().column_parent, "2D", "the other column is told whose it is");
    hdxf_test::assert_eq(child->as_mtext().content, "", "its piece is in the first's content");
    hdxf_test::assert_eq(find_const(dwg, "6E")->as_lwpolyline().frame_of, "2D", "the frame is told whose it is");

    const hdxf::MTextData& d = dynamic->as_mtext();
    hdxf_test::assert_eq(d.columns->type, 2, "dynamic: type");
    hdxf_test::assert_true(d.columns->heights.size() == 3 && std::fabs(d.columns->heights[0] - 38.293333333333) < 1e-9, "dynamic: three heights");
    hdxf_test::assert_true(d.columns->linked == std::vector<std::string>({"76", "77"}), "dynamic: the other columns");
    hdxf_test::assert_eq(d.defined_height, 0.0, "dynamic: no defined height");

    // Written back as it was
    std::vector<uint8_t> lf;
    for (uint8_t c : in) if (c != '\r') lf.push_back(c);
    hdxf_test::assert_true(save(dwg) == lf, "bytes");
}

// The text frame alone: 90 16 without a fill's groups, one polyline
TEST(mtext_2018_autocad_2013_text_frame) {
    if (!has_files({kTextFrame})) return;
    std::vector<uint8_t> in = read_bytes(external(kTextFrame));
    hdxf::Drawing dwg = load(in);
    const hdxf::Entity* e = find_const(dwg, "87");
    hdxf_test::assert_true(e != nullptr, "the MTEXT");
    if (!e) return;
    const hdxf::MTextData& d = e->as_mtext();
    hdxf_test::assert_eq(d.background.flags, 16, "frame alone");
    hdxf_test::assert_true(d.background.border_handles == std::vector<std::string>({"9F"}), "its polyline");
    hdxf_test::assert_false(d.columns.has_value(), "no columns");
    hdxf_test::assert_eq(find_const(dwg, "9F")->as_lwpolyline().frame_of, "87", "the polyline is told whose it is");
    // Written anew: no 63, 45, 441 (AutoCAD's core refuses them with a frame alone), the XDATA block as AutoCAD's
    for (auto& en : dwg.entities()) if (en.props.handle == "87") en.raw = hdxf::RawRecord();
    Record r = record(records_of(save(dwg)), "87");
    Record before = record(records_of(in), "87");
    hdxf_test::assert_true(r == before, "written anew as AutoCAD's");
    for (const auto& g : r) hdxf_test::assert_true(g.first != 63 && g.first != 45 && g.first != 441, "no fill groups");
}

// AutoCAD's records from the same text made from scratch: the first column, the other two and the
// frames (the frame's y range from the defined height, so no font is needed)
TEST(mtext_2018_columns_made_as_autocad_makes_them) {
    if (!has_files({kFramedColumns})) return;
    std::vector<Record> recs = records_of(read_bytes(external(kFramedColumns)));
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::Entity& e = dwg.add_mtext({0, 0, 0}, "", 1.0, "STATIC");
    std::string full;
    {
        Record p = record(recs, "2D"), a = record(recs, "71"), b = record(recs, "72");
        for (const Record* r : {&p, &a, &b}) for (const auto& [code, v] : *r) if (code == 3 || code == 1) full += v;
    }
    hdxf::MTextData& d = e.as_mtext();
    d.content = full;
    d.reference_width = 20.0;
    d.defined_height = 100.0;
    d.background.flags = 17;
    d.background.color = hdxf::Color::from_rgb(0xC8, 0xC8, 0xC8);
    d.background.color.aci = 9;
    hdxf::MTextColumns c;
    c.type = 1;
    c.count = 3;
    c.width = 20.0;
    c.gutter = 2.0;
    d.columns = c;
    std::vector<uint8_t> out = save(dwg);
    std::vector<Record> mine = records_of(out);
    dxf_structure::check_structure(out, "columns");

    std::vector<Record> texts, polylines;
    for (const auto& r : mine) {
        if (r[0].second == "MTEXT") texts.push_back(r);
        if (r[0].second == "LWPOLYLINE") polylines.push_back(r);
    }
    hdxf_test::assert_eq(static_cast<int>(texts.size()), 3, "three MTEXT");
    hdxf_test::assert_eq(static_cast<int>(polylines.size()), 3, "three frames");
    // AutoCAD's three: the same groups (but its extension dictionary and annotative XDATA), the same values
    for (size_t i = 0; i < 3; ++i) {
        Record theirs = record(recs, i == 0 ? "2D" : i == 1 ? "71" : "72");
        Record ours = texts[i];
        std::vector<int> ca, co;
        for (const auto& g : theirs) if (g.first < 1000 && g.first != 102 && g.first != 360) ca.push_back(g.first);
        for (const auto& g : ours) if (g.first < 1000 && g.first != 102) co.push_back(g.first);
        hdxf_test::assert_true(ca == co, "MTEXT " + std::to_string(i) + ": AutoCAD's groups");
        for (int code : {10, 20, 40, 41, 46, 44, 45, 71, 72, 73, 90}) {
            hdxf_test::assert_eq(number(value(ours, code)), number(value(theirs, code)),
                                 "MTEXT " + std::to_string(i) + " group " + std::to_string(code));
        }
        // The XDATA of the ACAD list: the same blocks in the same order
        std::vector<std::string> xa, xo;
        for (const auto& g : theirs) if (g.first == 1000) xa.push_back(g.second);
        for (const auto& g : ours) if (g.first == 1000) xo.push_back(g.second);
        xa.erase(std::remove(xa.begin(), xa.end(), "AnnotativeData"), xa.end());
        hdxf_test::assert_true(xa == xo, "MTEXT " + std::to_string(i) + ": the XDATA blocks");
    }
    const char* frames[] = {"6E", "6F", "70"};
    for (size_t i = 0; i < 3; ++i) {
        Record theirs = record(recs, frames[i]);
        Record ours = polylines[i];
        std::vector<double> a, b;
        for (const auto& [code, v] : theirs) if (code == 10 || code == 20) a.push_back(number(v));
        for (const auto& [code, v] : ours) if (code == 10 || code == 20) b.push_back(number(v));
        hdxf_test::assert_true(a.size() == 8 && b.size() == 8, "four vertices");
        // The same rectangle (the corner the polyline starts at differs)
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        for (size_t k = 0; k < a.size() && k < b.size(); ++k) {
            hdxf_test::assert_true(std::fabs(a[k] - b[k]) < 1e-9, "frame " + std::to_string(i) + ": " + std::to_string(a[k]) + " / " + std::to_string(b[k]));
        }
    }
    // The links are the handles the polylines and the other columns have
    const hdxf::MTextData& first = dwg.entities()[0].as_mtext();
    hdxf_test::assert_eq(static_cast<int>(first.columns->linked.size()), 2, "linked");
    hdxf_test::assert_eq(static_cast<int>(first.background.border_handles.size()), 3, "frames");
    hdxf_test::assert_eq(count_type(dwg, hdxf::EntityType::MText), 3, "three MTEXT in the drawing");
    hdxf_test::assert_eq(count_type(dwg, hdxf::EntityType::LwPolyline), 3, "three polylines in the drawing");
    hdxf_test::assert_eq(first.content, full, "the first's content is still the whole text");
}

// 2013 -> 2018 -> 2013: the other columns and the frames go and come back, the text stays whole
TEST(mtext_2018_columns_follow_the_version) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    dwg.add_mtext({0, 0, 0}, "One\\NTwo\\NThree", 2.5);
    {
        hdxf::MTextColumns c;
        c.type = 1;
        c.count = 3;
        c.width = 10.0;
        c.gutter = 1.0;
        dwg.entities()[0].as_mtext().columns = c;
        dwg.entities()[0].as_mtext().defined_height = 20.0;
        dwg.entities()[0].as_mtext().background.flags = 17;
    }
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2013,
                               hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2007}) {
        dwg.set_version(v);
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "columns follow the version");
        bool r2018 = v == hdxf::DxfVersion::R2018;
        int texts = 0, polylines = 0;
        for (const auto& r : records_of(out)) {
            if (r[0].second == "MTEXT") ++texts;
            if (r[0].second == "LWPOLYLINE") ++polylines;
        }
        hdxf_test::assert_eq(texts, r2018 ? 1 : 3, "MTEXT");
        hdxf_test::assert_eq(polylines, r2018 ? 0 : 3, "frames");
        hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), r2018 ? 1 : 6, "entities of the drawing");
        hdxf_test::assert_eq(dwg.entities()[0].as_mtext().content, "One\\NTwo\\NThree", "the text whole");
        hdxf_test::assert_eq(static_cast<int>(dwg.entities()[0].as_mtext().columns->linked.size()), r2018 ? 0 : 2, "links");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(back.entities()[0].as_mtext().content, "One\\NTwo\\NThree", "the text whole, read back");
        hdxf_test::assert_eq(back.entities()[0].as_mtext().columns->count, 3, "columns read back");
    }
}

// What the text owns goes with it (B19): a text taken out of the drawing takes its other columns and
// its frames; a text whose columns are taken away loses the other columns; the text of the others stays
TEST(mtext_2018_linked_entities_follow_the_text) {
    if (!has_files({kFramedColumns})) return;
    std::vector<uint8_t> in = read_bytes(external(kFramedColumns));
    {
        hdxf::Drawing dwg = load(in);
        auto& list = dwg.entities();
        list.erase(std::remove_if(list.begin(), list.end(), [](const hdxf::Entity& e) { return e.props.handle == "2D"; }), list.end());
        std::vector<Record> after = records_of(save(dwg));
        for (const char* h : {"2D", "71", "72", "6E", "6F", "70"}) hdxf_test::assert_true(record(after, h).empty(), std::string(h) + " is gone");
        for (const char* h : {"33", "76", "77", "73", "74", "75"}) hdxf_test::assert_false(record(after, h).empty(), std::string(h) + " stays");
    }
    {
        hdxf::Drawing dwg = load(in);
        for (auto& e : dwg.entities()) if (e.props.handle == "2D") e.as_mtext().columns.reset();
        std::vector<Record> after = records_of(save(dwg));
        for (const char* h : {"71", "72"}) hdxf_test::assert_true(record(after, h).empty(), std::string(h) + " is gone");
        hdxf_test::assert_false(record(after, "2D").empty(), "2D stays");
        for (const auto& [code, v] : record(after, "2D")) hdxf_test::assert_true(v != "ACAD_MTEXT_COLUMN_INFO_BEGIN" && v != "ACAD_MTEXT_COLUMNS_BEGIN", "no column XDATA");
        // The frame of a text without columns: one
        int frames = 0;
        for (const auto& r : after) frames += r[0].second == "LWPOLYLINE";
        hdxf_test::assert_eq(frames, 3 + 1, "the frames: the other text's three and this one's");
    }
    {
        // The text changed: cut again over the same entities, XDATA and the other texts as they were
        hdxf::Drawing dwg = load(in);
        for (auto& e : dwg.entities()) if (e.props.handle == "2D") e.as_mtext().content = "A\\NB\\NC";
        std::vector<Record> after = records_of(save(dwg));
        hdxf_test::assert_eq(value(record(after, "2D"), 1), "A\\N", "first piece");
        hdxf_test::assert_eq(value(record(after, "71"), 1), "B\\N", "second piece");
        hdxf_test::assert_eq(value(record(after, "72"), 1), "C", "third piece");
        std::vector<Record> before = records_of(in);
        for (const char* h : {"33", "76", "77", "73", "74", "75"}) hdxf_test::assert_true(record(after, h) == record(before, h), std::string(h) + " as it was");
    }
}

// The text cut over the columns (B74): static after each column break, not after an escaped one;
// dynamic whole in the first
TEST(mtext_2018_column_pieces) {
    hdxf::MTextColumns c;
    c.type = 1;
    c.count = 3;
    auto pieces = hdxf::internal::column_pieces("a\\Nb\\Nc\\Nd", c);
    hdxf_test::assert_true(pieces == std::vector<std::string>({"a\\N", "b\\N", "c\\Nd"}), "the rest in the last");
    pieces = hdxf::internal::column_pieces("a\\\\Nb", c);
    hdxf_test::assert_true(pieces == std::vector<std::string>({"a\\\\Nb", "", ""}), "an escaped backslash is no break");
    pieces = hdxf::internal::column_pieces("only", c);
    hdxf_test::assert_true(pieces == std::vector<std::string>({"only", "", ""}), "fewer breaks, empty columns");
    c.type = 2;
    pieces = hdxf::internal::column_pieces("a\\Nb", c);
    hdxf_test::assert_true(pieces == std::vector<std::string>({"a\\Nb", "", ""}), "dynamic: the first has it all");
    c.count = 1;
    pieces = hdxf::internal::column_pieces("a\\Nb", c);
    hdxf_test::assert_eq(static_cast<int>(pieces.size()), 1, "one column");
}

// ---------------------------------------------------------------------------
// What AutoCAD's core showed about the embedded object, the frame and the defined height (6.4)
// ---------------------------------------------------------------------------

// AutoCAD takes the embedded object's attachment (70), direction (10), insertion point (11) and
// defined height (41) over the entity's own (AutoCAD's core reading a file where they differ,
// then ODA writing the DWG as DXF: b_embedded_custom of phase 6's oracle run): so reading sets the
// entity's fields from the embedded object
TEST(mtext_2018_embedded_values_take_over_the_entitys) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> bytes = read_bytes(external(kAscii));
    std::string text(bytes.begin(), bytes.end());
    size_t at = text.find("\n  5\n3EC\n");
    size_t emb = text.find("101\nEmbedded Object\n", at);
    size_t end = text.find("\n  0\n", at);
    hdxf_test::assert_true(at != std::string::npos && emb < end, "3EC's embedded object");
    // 70 7, direction (0, 1, 0), insertion (99, 98, 0), defined height 88
    std::string changed = "101\nEmbedded Object\n" + group_text(70, "7") + group_text(10, "0.0") + group_text(20, "1.0") +
                          group_text(30, "0.0") + group_text(11, "99.0") + group_text(21, "98.0") + group_text(31, "0.0") +
                          group_text(40, "15.74141424498748") + group_text(41, "88.0") + group_text(42, "13.5") +
                          group_text(43, "2.9") + group_text(71, "2") + group_text(72, "1") + group_text(44, "15.74141424498748") +
                          group_text(45, "110.2231670381195") + group_text(73, "0") + group_text(74, "0") + group_text(46, "0.0");
    text.replace(emb, end + 1 - emb, changed);
    std::vector<uint8_t> in(text.begin(), text.end());
    hdxf::Drawing dwg = load(in);
    hdxf::Entity* e = find_entity(dwg, "3EC");
    hdxf_test::assert_true(e != nullptr, "3EC");
    if (!e) return;
    const hdxf::MTextData& d = e->as_mtext();
    hdxf_test::assert_eq(d.attachment, 7, "70 is the attachment");
    hdxf_test::assert_eq(d.x_axis_direction.x, 0.0, "direction x");
    hdxf_test::assert_eq(d.x_axis_direction.y, 1.0, "direction y");
    hdxf_test::assert_eq(d.insertion_point.x, 99.0, "insertion x");
    hdxf_test::assert_eq(d.insertion_point.y, 98.0, "insertion y");
    hdxf_test::assert_eq(d.defined_height, 88.0, "41 is the defined height");
    hdxf_test::assert_eq(d.height, 1.0, "the height is still the entity's");
    // Written anew: the embedded object and the entity say the same
    e->raw = hdxf::RawRecord();
    Record r = record(records_of(save(dwg)), "3EC");
    Record own = parent_of(r), emb_groups = embedded_of(r);
    hdxf_test::assert_eq(value(own, 71), "7", "the entity's attachment");
    hdxf_test::assert_eq(value(emb_groups, 70), "7", "the embedded attachment");
    hdxf_test::assert_eq(number(value(own, 10)), 99.0, "the entity's insertion");
    hdxf_test::assert_eq(number(value(emb_groups, 11)), 99.0, "the embedded insertion");
    hdxf_test::assert_eq(number(value(own, 46)), 88.0, "the entity's defined height");
    hdxf_test::assert_eq(number(value(emb_groups, 41)), 88.0, "the embedded defined height");
    hdxf_test::assert_eq(number(value(own, 21)), 1.0, "the entity's direction");
    hdxf_test::assert_eq(number(value(emb_groups, 20)), 1.0, "the embedded direction");
}

// The fill's groups (63, 45, 441) with a frame: before 2018 only with a fill (a frame alone has
// none — AutoCAD's core refuses it with them), in 2018 always (the core refuses it without)
TEST(mtext_2018_frame_alone_and_the_fill_groups) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        for (int flags : {16, 17, 1}) {
            hdxf::Drawing dwg(v);
            hdxf::Entity& e = dwg.add_mtext({0, 0, 0}, "Frame", 2.5);
            e.as_mtext().background.flags = flags;
            std::vector<Record> saved = records_of(save(dwg));
            Record r = record(saved, dwg.entities()[0].props.handle);
            bool fill = !value(r, 63).empty() || !value(r, 45).empty() || !value(r, 441).empty();
            bool expected = (flags & 1) || v == hdxf::DxfVersion::R2018;
            hdxf_test::assert_eq(fill, expected, std::string(hdxf::acadver_string(v)) + " 90 " + std::to_string(flags));
            hdxf_test::assert_eq(value(r, 90), std::to_string(flags), "90 in " + std::string(hdxf::acadver_string(v)) + " for " + std::to_string(flags) + ": [" + value(r, 90) + "]");
        }
    }
    // 2000 has no background at all
    hdxf::Drawing r2000(hdxf::DxfVersion::R2000);
    r2000.add_mtext({0, 0, 0}, "x").as_mtext().background.flags = 16;
    hdxf_test::assert_true(r2000.save_to_memory().empty(), "2000 refuses a background");
}

// The defined height: XDATA alone in 2000 and 2004, group 46 and XDATA from 2007 to 2013 (AutoCAD's
// Universal_Pen_Holder, SmartCitizen's case and ezdxf's mtext_framed_columns), the embedded
// object's 41 in 2018 — read back as the same value
TEST(mtext_2018_defined_height_in_every_version) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007,
                               hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        hdxf::Drawing dwg(v);
        hdxf::Entity& e = dwg.add_mtext({0, 0, 0}, "Height", 2.5);
        e.as_mtext().defined_height = 12.5;
        e.as_mtext().columns = hdxf::MTextColumns{};
        std::vector<uint8_t> out = save(dwg);
        Record r = record(records_of(out), dwg.entities()[0].props.handle);     // (out is saved)
        bool group = !value(parent_of(r), 46).empty();
        bool xdata = false;
        for (const auto& g : r) xdata = xdata || g.second == "ACAD_MTEXT_DEFINED_HEIGHT_BEGIN";
        bool r2018 = v == hdxf::DxfVersion::R2018;
        hdxf_test::assert_eq(group, v >= hdxf::DxfVersion::R2007, "46 in " + std::string(hdxf::acadver_string(v)));
        hdxf_test::assert_eq(xdata, !r2018, "XDATA in " + std::string(hdxf::acadver_string(v)));
        if (r2018) hdxf_test::assert_eq(number(value(embedded_of(r), 41)), 12.5, "41 in 2018");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(back.entities()[0].as_mtext().defined_height, 12.5, "read back in " + std::string(hdxf::acadver_string(v)));
        hdxf_test::assert_true(back.entities()[0].props.xdata.empty(), "the XDATA block is the field");
        hdxf_test::assert_true(records_of(save(back)) == records_of(out), "written back in " + std::string(hdxf::acadver_string(v)));
    }
}

// The text frame is in the box the layout gives (and so in the extents): the text's box grown by
// (scale - 1) times the height on each side — Arial, as ezdxf's frames from AutoCAD (0.5 for 1.5)
TEST(mtext_2018_frame_in_the_layout_box) {
    const char* windir = std::getenv("WINDIR");
    std::filesystem::path arial = windir ? hdxf::path_from_utf8(windir) / "Fonts" / "arial.ttf" : std::filesystem::path("arial.ttf");
    if (!std::filesystem::exists(arial)) hdxf_test::skip("arial.ttf missing");
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    hdxf::set_font_paths({arial.parent_path()});
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    hdxf::TextStyle& s = dwg.add_text_style("Standard", "arial.ttf");
    s.font_family = "Arial";
    s.font_flags = 34;
    dwg.add_mtext({0, 0, 0}, "Some text", 2.0);
    dwg.add_mtext({0, 0, 0}, "Some text", 2.0);
    dwg.entities()[1].as_mtext().background.flags = 16;
    hdxf::Point3 a0, a1, b0, b1;
    bool ok_a = dwg.entity_extents(dwg.entities()[0], a0, a1), ok_b = dwg.entity_extents(dwg.entities()[1], b0, b1);
    hdxf::set_font_paths(saved);
    hdxf_test::assert_true(ok_a && ok_b, "extents");
    double offset = 0.5 * 2.0;
    hdxf_test::assert_true(std::fabs((a0.x - b0.x) - offset) < 1e-9 && std::fabs((a0.y - b0.y) - offset) < 1e-9, "the frame grows the box down-left");
    hdxf_test::assert_true(std::fabs((b1.x - a1.x) - offset) < 1e-9 && std::fabs((b1.y - a1.y) - offset) < 1e-9, "and up-right");
}

// ---------------------------------------------------------------------------
// The frame's XDATA (80) is the frame bit alone while 90 has the fill's bits with it (faciträttning
// F8, AutoCAD 2013's text file: 90 19, 80 16): one frame, read and written again as it was
// ---------------------------------------------------------------------------

namespace {

int frames_in(const hdxf::Drawing& dwg) {
    int n = 0;
    for (const auto& e : dwg.entities()) n += e.type == hdxf::EntityType::LwPolyline;
    return n;
}

// The 1070 value after the key 80 in the text's frame block
std::string frame_flags_in_xdata(const hdxf::Entity& e) {
    for (const auto& list : e.props.xdata) {
        for (size_t i = 0; i + 2 < list.items.size(); ++i) {
            if (list.items[i].value == "ACAD_MTEXT_TEXT_BORDERS_BEGIN") return std::to_string(std::atoi(list.items[i + 2].value.c_str()));
        }
    }
    return {};
}

} // namespace

TEST(mtext_2013_frame_with_fill_has_80_16_and_90_19) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::Entity& e = dwg.add_mtext({0, 0, 0}, "Frame and fill", 2.5);
    e.as_mtext().background.flags = 19;
    e.as_mtext().background.scale = 1.5;
    std::vector<uint8_t> file = save(dwg);
    Record r = record(records_of(file), dwg.entities()[0].props.handle);
    hdxf_test::assert_eq(value(r, 90), "19", "90 has the frame and the fill");
    bool found = false;
    for (size_t i = 0; i + 3 < r.size(); ++i) {
        if (r[i].first == 1000 && r[i].second == "ACAD_MTEXT_TEXT_BORDERS_BEGIN") {
            hdxf_test::assert_true(r[i + 1] == std::make_pair(1070, std::string("80")), "key 80");
            hdxf_test::assert_true(r[i + 2] == std::make_pair(1070, std::string("16")), "80 is the frame bit alone");
            found = true;
        }
    }
    hdxf_test::assert_true(found, "the frame block");

    hdxf::Drawing back = load(file);
    hdxf_test::assert_eq(frames_in(back), 1, "one frame");
    const hdxf::MTextData& d = back.entities()[0].as_mtext();
    hdxf_test::assert_eq(d.background.flags, 19, "flags");
    hdxf_test::assert_eq(static_cast<int>(d.background.border_handles.size()), 1, "linked to its frame");
    hdxf_test::assert_true(frame_flags_in_xdata(back.entities()[0]).empty(), "the block is typed, not left raw");
    hdxf_test::assert_true(save(back) == file, "written again: the same bytes");

    // The fill changes, the frame stays one
    back.entities()[0].as_mtext().background.scale = 2.0;
    hdxf::Drawing changed = load(save(back));
    hdxf_test::assert_eq(frames_in(changed), 1, "still one frame");
    hdxf_test::assert_eq(static_cast<int>(changed.entities()[0].as_mtext().background.border_handles.size()), 1, "one handle");
    // The frame taken away: its polyline goes
    changed.entities()[0].as_mtext().background.flags = 3;
    hdxf::Drawing none = load(save(changed));
    hdxf_test::assert_eq(frames_in(none), 0, "no frame");
}

// AutoCAD 2010's file has the frame block (80 16) with 90 3, no frame bit: the block stays as it is
TEST(mtext_2010_frame_xdata_without_frame_bit_stays_raw) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    hdxf::Entity& e = dwg.add_mtext({0, 0, 0}, "Text", 2.5);
    e.as_mtext().background.flags = 3;
    e.props.xdata.push_back({"ACAD", {{1000, "ACAD_MTEXT_TEXT_BORDERS_BEGIN"}, {1070, "80"}, {1070, "16"}, {1070, "46"},
                                      {1040, "1.5"}, {1070, "81"}, {1070, "1"}, {1070, "5"}, {1005, "FFFF"},
                                      {1000, "ACAD_MTEXT_TEXT_BORDERS_END"}}});
    std::vector<uint8_t> file = save(dwg);
    hdxf::Drawing back = load(file);
    hdxf_test::assert_eq(frames_in(back), 0, "no polyline was made");
    hdxf_test::assert_eq(frame_flags_in_xdata(back.entities()[0]), "16", "the block as it was");
    hdxf_test::assert_eq(back.entities()[0].as_mtext().background.flags, 3, "90 as it was");
    hdxf_test::assert_true(save(back) == file, "written again: the same bytes");
}
