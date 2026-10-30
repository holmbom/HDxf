#include "test_framework.hpp"
#include "test_drawing_builder.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "text_encoding.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DXF 2018 (phase 6, step 6.2 — plan B69-B71): a drawing in memory written as AutoCAD writes
// 2018, AutoCAD's own 2018 files written back byte for byte, ASCII and binary, and a drawing of
// another version refused as 2018.
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

std::string value(const Record& r, int code, int occurrence = 0) {
    for (const auto& [c, v] : r) {
        if (c == code && occurrence-- == 0) return v;
    }
    return {};
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

Record groups_in(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 100) { in = g.second == subclass; continue; }
        if (in) out.push_back(g);
    }
    return out;
}

std::vector<std::string> header_names(const std::vector<Record>& records) {
    std::vector<std::string> out;
    for (const auto& [code, v] : records.at(0)) if (code == 9) out.push_back(v);
    return out;
}

std::pair<int, std::string> header_group(const std::vector<Record>& records, const std::string& name) {
    const Record& h = records.at(0);
    for (size_t i = 0; i + 1 < h.size(); ++i) {
        if (h[i].first == 9 && h[i].second == name) return h[i + 1];
    }
    return {0, "?"};
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

// The records of a section from its SECTION record to its ENDSEC
std::vector<Record> section(const std::vector<Record>& records, const std::string& name) {
    std::vector<Record> out;
    bool in = false;
    for (const auto& r : records) {
        if (r[0].second == "SECTION") in = value(r, 2) == name;
        if (in) out.push_back(r);
        if (r[0].second == "ENDSEC") in = false;
    }
    return out;
}

// The groups the one record has and the other has not (as multisets), in order
std::pair<Record, Record> difference(const Record& a, const Record& b) {
    std::multiset<std::pair<int, std::string>> in_a(a.begin(), a.end()), in_b(b.begin(), b.end());
    std::pair<Record, Record> out;
    std::multiset<std::pair<int, std::string>> left = in_b;
    for (const auto& g : a) {
        auto it = left.find(g);
        if (it == left.end()) out.first.push_back(g);
        else left.erase(it);
    }
    left = in_a;
    for (const auto& g : b) {
        auto it = left.find(g);
        if (it == left.end()) out.second.push_back(g);
        else left.erase(it);
    }
    return out;
}

const char* const kAscii = "acadsharp/sample_AC1032_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1032_binary.dxf";
const char* const kAscii2013 = "acadsharp/sample_AC1027_ascii.dxf";
const char* const kNew2013 = "jscad/empty2013.dxf";
const char* const kLibre = "libre/example_2018.dxf";

// A new drawing with a line and a multileader (so it has AutoCAD's MLEADERSTYLE Standard)
std::vector<uint8_t> new_drawing(hdxf::DxfVersion version, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    hdxf::Drawing dwg(version);
    dwg.set_header_string("$FINGERPRINTGUID", 2, "{11111111-2222-4333-8444-555555555555}");
    dwg.set_header_string("$VERSIONGUID", 2, "{66666666-7777-4888-9999-AAAAAAAAAAAA}");
    dwg.add_line({0, 0, 0}, {10, 5, 0});
    dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
    return save(dwg, format);
}

} // namespace

// A new 2018 drawing (B70, B71): AutoCAD's 253 header variables in AutoCAD's order, $ACADVER
// AC1032, $ACADMAINTVER 90 329 (ASCII and binary), $REQUIREDVERSIONS an int64; and everything
// else the new 2013 drawing — AutoCAD's own (empty2013) in its structure — but the VPORT table's
// XDATA, which AutoCAD's 2018 files do not have.
TEST(write_2018_new_drawing) {
    std::vector<uint8_t> raw = new_drawing(hdxf::DxfVersion::R2018);
    dxf_structure::check_structure(raw, "new 2018 drawing");
    std::vector<Record> recs = records_of(raw);
    std::vector<Record> r13 = records_of(new_drawing(hdxf::DxfVersion::R2013));

    std::vector<std::string> names = header_names(recs);
    hdxf_test::assert_eq(static_cast<int>(names.size()), 253, "header variables");
    for (const char* file : {kAscii, kBinary, kLibre}) {
        if (!has_files({file})) continue;
        hdxf_test::assert_true(names == header_names(records_of(read_bytes(external(file)))),
                               std::string(file) + ": AutoCAD's variables in AutoCAD's order");
    }
    hdxf_test::assert_true(names == header_names(r13), "the same variables as 2013");
    auto at = std::find(names.begin(), names.end(), "$REQUIREDVERSIONS");
    hdxf_test::assert_true(at != names.begin() && at != names.end() && *(at - 1) == "$LASTSAVEDBY" &&
                           *(at + 1) == "$INSBASE", "$REQUIREDVERSIONS after $LASTSAVEDBY");
    hdxf_test::assert_true(header_group(recs, "$REQUIREDVERSIONS") == std::pair<int, std::string>(160, "0"),
                           "$REQUIREDVERSIONS 160 0");
    hdxf_test::assert_eq(header_group(recs, "$ACADVER").second, "AC1032");
    hdxf_test::assert_true(header_group(recs, "$ACADMAINTVER") == std::pair<int, std::string>(90, "329"),
                           "$ACADMAINTVER 90 329");
    hdxf_test::assert_true(header_group(r13, "$ACADMAINTVER") == std::pair<int, std::string>(70, "125"),
                           "2013's stays 70");

    // The rest of the header is 2013's, group for group (the variables whose value is the version's)
    for (const auto& name : names) {
        if (name == "$ACADVER" || name == "$ACADMAINTVER") continue;
        hdxf_test::assert_true(header_group(recs, name) == header_group(r13, name), name);
    }

    // Every section but the header as in the new 2013 drawing, but the VPORT table's XDATA
    for (const char* name : {"CLASSES", "TABLES", "BLOCKS", "ENTITIES", "OBJECTS", "ACDSDATA"}) {
        std::vector<Record> a = section(recs, name), b = section(r13, name);
        hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), std::string(name) + ": records");
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            if (a[i] == b[i]) continue;
            hdxf_test::assert_true(std::string(name) == "TABLES" && a[i][0].second == "TABLE" && value(a[i], 2) == "VPORT",
                                   std::string(name) + ": record " + std::to_string(i) + " (" + a[i][0].second + ")");
            Record vport13 = b[i];
            hdxf_test::assert_true(vport13.size() == a[i].size() + 3 &&
                                   Record(vport13.end() - 3, vport13.end()) ==
                                       Record({{1001, "ACAD"}, {1000, "DbSaveVer"}, {1071, "26"}}),
                                   "2013's VPORT table has DbSaveVer");
        }
    }
    for (const auto& r : recs) {
        if (r[0].second == "TABLE" && value(r, 2) == "VPORT") {
            for (const auto& g : r) hdxf_test::assert_true(g.first < 1000, "no XDATA on the VPORT table");
        }
        for (const auto& [code, v] : r) hdxf_test::assert_true(v != "AcDbSavedByObjectVersion" && v != "DbSaveVer", v);
        if (r[0].second == "VISUALSTYLE" || r[0].second == "MLEADERSTYLE") {
            hdxf_test::assert_eq(value(r, 360), "", r[0].second + ": no extension dictionary");
        }
        if (r[0].second == "LAYER") {
            Record body = groups_in(r, "AcDbLayerTableRecord");
            hdxf_test::assert_true(body.size() >= 2 && body.back() == Record::value_type{348, "0"} &&
                                   body[body.size() - 2].first == 347, "LAYER 348 0 after 347");
        }
        if (r[0].second == "MLEADERSTYLE") {
            Record body = groups_in(r, "AcDbMLeaderStyle");
            hdxf_test::assert_true(body.back() == Record::value_type{298, "1"}, "298 1 last");
            hdxf_test::assert_eq(value(body, 295), "1", "295 1");
        }
    }

    // ACDSDATA: AutoCAD's schemas group for group and no record, as in its new drawing (empty2013);
    // AutoCAD's 2018 file has the schemas of its 2013 file (same names, attributes, order)
    if (has_files({kNew2013, kAscii, kAscii2013})) {
        std::vector<Record> autocad_schemas;
        for (const auto& r : section(records_of(read_bytes(external(kNew2013))), "ACDSDATA")) {
            if (r[0].second != "ACDSRECORD") autocad_schemas.push_back(r);
        }
        hdxf_test::assert_eq(static_cast<int>(autocad_schemas.size()), 7, "AutoCAD's: SECTION, five schemas, ENDSEC");
        hdxf_test::assert_true(section(recs, "ACDSDATA") == autocad_schemas, "ACDSDATA's schemas as AutoCAD's");
        std::vector<Record> s18, s13;
        for (const auto& r : section(records_of(read_bytes(external(kAscii))), "ACDSDATA")) {
            if (r[0].second != "ACDSRECORD") s18.push_back(r);
        }
        for (const auto& r : section(records_of(read_bytes(external(kAscii2013))), "ACDSDATA")) {
            if (r[0].second != "ACDSRECORD") s13.push_back(r);
        }
        hdxf_test::assert_true(s18 == s13, "AutoCAD's 2018 schemas are its 2013's");
    }
    hdxf_test::assert_eq(recs.back()[0].second, "EOF", "EOF last");

    // MATERIAL and VISUALSTYLE as in AutoCAD's new drawing saved as 2013 (2018's are the same
    // but for XDATA that no new drawing has)
    if (has_files({kNew2013})) {
        std::vector<Record> autocad = records_of(read_bytes(external(kNew2013)));
        for (const char* type : {"MATERIAL", "VISUALSTYLE"}) {
            std::vector<Record> a, b;
            for (const auto& r : recs) if (r[0].second == type) a.push_back(groups_in(r, std::string("AcDb") + (type[0] == 'M' ? "Material" : "VisualStyle")));
            for (const auto& r : autocad) if (r[0].second == type) b.push_back(groups_in(r, std::string("AcDb") + (type[0] == 'M' ? "Material" : "VisualStyle")));
            hdxf_test::assert_true(a == b, std::string(type) + " as AutoCAD's");
        }
    }

    // Read back as 2018; a second save the same; binary the same records, maintenance 90 int32
    hdxf::Drawing back = load(raw);
    hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R2018, "version read back");
    hdxf_test::assert_true(records_of(save(back)) == recs, "read back and written: unchanged");
    std::vector<uint8_t> bin_raw = new_drawing(hdxf::DxfVersion::R2018, hdxf::DxfFormat::Binary);
    std::vector<Record> bin = records_of(bin_raw);
    hdxf_test::assert_eq(static_cast<int>(bin.size()), static_cast<int>(recs.size()), "binary");
    hdxf_test::assert_true(header_group(bin, "$ACADMAINTVER") == std::pair<int, std::string>(90, "329"), "binary 90");
    hdxf_test::assert_true(header_group(bin, "$REQUIREDVERSIONS") == std::pair<int, std::string>(160, "0"), "binary 160");
    hdxf_test::assert_true(section(bin, "ACDSDATA") == section(recs, "ACDSDATA"), "binary ACDSDATA");
    hdxf::Drawing back_binary = load(bin_raw);
    hdxf_test::assert_true(back_binary.version() == hdxf::DxfVersion::R2018, "binary version");
    hdxf_test::assert_true(records_of(save(back_binary, hdxf::DxfFormat::Binary)) == bin, "binary read back and written");
}

// $ACADMAINTVER is 90 in 2018 and 70 before, whichever group the program gave it (B70); a drawing
// read keeps its value and its group
TEST(write_2018_maintenance_version_group) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        bool r2018 = v == hdxf::DxfVersion::R2018;
        for (int given : {70, 90}) {
            hdxf::Drawing dwg(v);
            dwg.set_header_int("$ACADMAINTVER", given, 41);
            dwg.add_line({0, 0, 0}, {1, 1, 0});
            for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
                std::vector<Record> recs = records_of(save(dwg, format));
                hdxf_test::assert_true(header_group(recs, "$ACADMAINTVER") ==
                                       std::pair<int, std::string>(r2018 ? 90 : 70, "41"), "group of $ACADMAINTVER");
            }
            if (r2018) {
                // Read back, changed by the program: the group is the version's
                hdxf::Drawing back = load(save(dwg));
                back.set_header_int("$ACADMAINTVER", 70, 42);
                hdxf_test::assert_true(header_group(records_of(save(back)), "$ACADMAINTVER") ==
                                       std::pair<int, std::string>(90, "42"), "changed after reading");
            }
        }
    }
}

// AutoCAD's 2018 files written back, ASCII and binary: byte for byte
TEST(roundtrip_2018_bytes) {
    for (const char* name : {kBinary, kAscii}) {
        if (!has_files({name})) return;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R2018, std::string(name) + ": 2018");
        bool binary = std::string(name) == kBinary;
        std::vector<uint8_t> out = save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(out == in, std::string(name) + ": bytes");
    }
}

// Read and written in the same format, record by record: ACadSharp's two, LibreDWG's example
TEST(roundtrip_2018_files_record_for_record) {
    struct File { std::filesystem::path path; hdxf::DxfFormat format; };
    std::vector<File> files;
    for (const char* name : {kAscii, kLibre, "acadsharp/geoloc.dxf"}) {
        if (std::filesystem::exists(external(name))) files.push_back({external(name), hdxf::DxfFormat::Ascii});
    }
    if (std::filesystem::exists(external(kBinary))) files.push_back({external(kBinary), hdxf::DxfFormat::Binary});
    if (files.empty()) hdxf_test::skip("no 2018 files");
    for (const auto& f : files) {
        std::string what = hdxf::utf8_from_path(f.path.filename());
        std::vector<uint8_t> in = read_bytes(f.path);
        hdxf::Drawing dwg = load(in);
        hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R2018, what + ": 2018");
        std::vector<Record> before = records_of(in);
        std::vector<uint8_t> out = save(dwg, f.format);
        std::vector<Record> after = records_of(out);
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()), what + ": records");
        for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
            hdxf_test::assert_true(before[i] == after[i], what + ": record " + std::to_string(i) +
                                   " (" + before[i][0].second + ") differs");
        }
        if (f.format == hdxf::DxfFormat::Ascii) {
            hdxf_test::assert_true(out == in, what + ": bytes");
        }
    }
}

// The binary file's values read back as the ASCII file's, and the other way: into the other
// format and back the same values (binary exactly, ASCII with AutoCAD's 16 significant digits) —
// ACDSDATA's groups too
TEST(roundtrip_2018_ascii_binary_ascii) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    hdxf::Drawing ascii = load(in);
    std::vector<uint8_t> bin = save(ascii, hdxf::DxfFormat::Binary);
    hdxf::Drawing binary = load(bin);
    std::vector<uint8_t> back = save(binary, hdxf::DxfFormat::Ascii);

    std::vector<Record> a = records_of(in), b = records_of(bin), c = records_of(back);
    hdxf_test::assert_eq(static_cast<int>(b.size()), static_cast<int>(a.size()), "records (binary)");
    hdxf_test::assert_eq(static_cast<int>(c.size()), static_cast<int>(a.size()), "records (ASCII)");
    auto same = [](const Record& x, const Record& y, double tolerance, const std::string& what, bool y_binary) {
        hdxf_test::assert_eq(static_cast<int>(y.size()), static_cast<int>(x.size()), what + ": groups");
        for (size_t g = 0; g < x.size() && g < y.size(); ++g) {
            hdxf_test::assert_eq(y[g].first, x[g].first, what + ": code");
            using hdxf::internal::GroupValueType;
            GroupValueType t = hdxf::internal::group_value_type(x[g].first);
            if (t == GroupValueType::Double) {
                double u = std::strtod(x[g].second.c_str(), nullptr);
                double v = std::strtod(y[g].second.c_str(), nullptr);
                hdxf_test::assert_true(std::fabs(u - v) <= tolerance * std::max(1.0, std::fabs(u)),
                                       what + ": " + x[g].second + " / " + y[g].second);
            } else if (t == GroupValueType::String || t == GroupValueType::BinaryChunk) {
                std::string expected = (y_binary && t == GroupValueType::String)
                                           ? hdxf::internal::decode_carets(x[g].second, hdxf::internal::utf8_code_page)
                                           : x[g].second;
                hdxf_test::assert_eq(y[g].second, expected, what);
            } else {
                hdxf_test::assert_true(std::atoll(y[g].second.c_str()) == std::atoll(x[g].second.c_str()),
                                       what + ": " + x[g].second);
            }
        }
    };
    for (size_t i = 0; i < a.size() && i < b.size() && i < c.size(); ++i) {
        same(a[i], b[i], 0.0, "binary record " + std::to_string(i), true);
        same(a[i], c[i], 1e-15, "ASCII record " + std::to_string(i), false);
    }
    hdxf_test::assert_true(section(a, "ACDSDATA") == section(c, "ACDSDATA"), "ACDSDATA");
    hdxf_test::assert_true(header_group(b, "$ACADMAINTVER") == std::pair<int, std::string>(90, "329"), "binary 90");
}

// Changes to AutoCAD's 2018 file: written anew where they are, everything else — the columns
// of the MTEXT, the SAB of the bodies — as it was
TEST(roundtrip_2018_changes) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);

    hdxf::Entity* line = nullptr;
    std::string line_handle;
    for (auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::Line) { line = &e; line_handle = e.props.handle; break; }
    }
    hdxf_test::assert_true(line != nullptr, "a LINE in the file");
    if (!line) return;
    line->as_line().end.x += 50.0;
    double moved = line->as_line().end.x;
    dwg.add_line({0, 0, 0}, {1, 1, 0}, "Layer1");
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "changed AutoCAD 2018 file");
    std::vector<Record> after = records_of(out);

    hdxf_test::assert_eq(std::strtod(value(record(after, line_handle), 11).c_str(), nullptr), moved, "moved");
    hdxf_test::assert_true(section(after, "ACDSDATA") == section(before, "ACDSDATA"), "ACDSDATA as read");
    hdxf_test::assert_true(header_group(after, "$ACADMAINTVER") == header_group(before, "$ACADMAINTVER"), "$ACADMAINTVER as read");
    hdxf_test::assert_eq(header_group(after, "$REQUIREDVERSIONS").second, "0", "$REQUIREDVERSIONS");

    // Every record but the line, the new line, the layer's ... is as it was, group for group
    int same = 0, total = 0;
    for (const auto& r : before) {
        std::string h;
        for (const auto& [code, v] : r) { if (code == 5) { h = v; break; } if (code == 100) break; }
        if (h.empty() || r[0].second == "SECTION") continue;
        ++total;
        Record w = record(after, h);
        if (w.empty()) continue;
        if (w == r) ++same;
        else if (h != line_handle && r[0].second != "BLOCK_RECORD" && r[0].second != "DICTIONARY" &&
                 r[0].second != "XRECORD") {
            hdxf_test::assert_true(false, "record " + h + " (" + r[0].second + ") changed");
        }
    }
    hdxf_test::assert_true(same >= total - 12, "unchanged records: " + std::to_string(same) + " of " + std::to_string(total));
}

// A drawing built in memory set to 2018 and back (B69): the visual styles, the multileader style
// and the ACDSDATA are made and taken away as the version has them, again and again
TEST(write_2018_set_version_back_and_forth) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "x");
    std::vector<Record> first = records_of(save(dwg));
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018,
                               hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2007,
                               hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        dwg.set_version(v);
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "set_version");
        std::vector<Record> recs = records_of(out);
        hdxf_test::assert_eq(header_group(recs, "$ACADVER").second, std::string(hdxf::acadver_string(v)), "version");
        hdxf_test::assert_true(header_group(recs, "$ACADMAINTVER").first == (v == hdxf::DxfVersion::R2018 ? 90 : 70), "maintenance group");
        if (v >= hdxf::DxfVersion::R2013) {
            // 2013 and 2018 differ in the header and the VPORT table only
            hdxf_test::assert_eq(static_cast<int>(recs.size()), static_cast<int>(first.size()), "records");
            hdxf_test::assert_true(section(recs, "OBJECTS").size() == section(first, "OBJECTS").size(), "OBJECTS");
            hdxf_test::assert_true(section(recs, "ACDSDATA") == section(first, "ACDSDATA"), "ACDSDATA");
        }
    }
}

// A drawing read from another version is not written as 2018 (conversion, phase 8), nor a
// drawing read from 2018 as another; one read from R12 is generated as 2018 (B69). Nothing is
// changed by the refusal.
TEST(write_2018_not_from_another_version) {
    hdxf::Drawing r12 = r12_drawing_read_back();
    r12.set_version(hdxf::DxfVersion::R2018);
    std::vector<uint8_t> generated = save(r12);
    dxf_structure::check_structure(generated, "R12 as 2018");
    hdxf_test::assert_eq(header_group(records_of(generated), "$ACADVER").second, "AC1032", "R12 as 2018");

    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                             "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1024_ascii.dxf", kAscii2013}) {
        if (!has_files({name})) return;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        hdxf::DxfVersion from = dwg.version();
        size_t entities = dwg.entities().size();
        const hdxf::HeaderVar* seed = dwg.find_header("$HANDSEED");
        std::string handseed = seed ? seed->values[0].second : "";
        dwg.set_version(hdxf::DxfVersion::R2018);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), name);
        hdxf_test::assert_true(dwg.last_error().rfind("Writing DXF ", 0) == 0 &&
                               dwg.last_error().find(" as 2018 is not supported yet (the drawing was read from that version)") !=
                                   std::string::npos, dwg.last_error());
        hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), static_cast<int>(entities), "entities");
        seed = dwg.find_header("$HANDSEED");
        hdxf_test::assert_eq(seed ? seed->values[0].second : "", handseed, "$HANDSEED unchanged");
        dwg.set_version(from);
        hdxf_test::assert_false(dwg.save_to_memory().empty(), std::string(name) + ": still saved as its own version");
    }
    if (has_files({kAscii})) {
        hdxf::Drawing dwg = load(read_bytes(external(kAscii)));
        for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2010}) {
            dwg.set_version(v);
            hdxf_test::assert_true(dwg.save_to_memory().empty(), "2018 as an older version");
            hdxf_test::assert_true(dwg.last_error().find("Writing DXF 2018 as ") != std::string::npos, dwg.last_error());
        }
    }
}

// A body with SAT data in a 2018 drawing is written as SAB 22300 (step 6.8, B80); a SAT that cannot be
// encoded is refused, named, before anything is changed
TEST(write_2018_sat_body_that_cannot_be_encoded_is_refused) {
    hdxf::AcisData sat;
    sat.lines = {"21500 0 1 0"};
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    dwg.add_acis(hdxf::EntityType::Region, sat);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "REGION");
    hdxf_test::assert_true(dwg.last_error().rfind("REGION: ", 0) == 0, dwg.last_error());
    hdxf_test::assert_true(dwg.entities()[0].props.handle.empty(), "no handles given");
}

// The classes of a new 2018 drawing are AutoCAD's 2018 definitions: the same names, applications
// and flags as in ACadSharp's file (the instance count 91 is the file's own — 12 against 13
// TABLECONTENT … between 2013 and 2018, B70's table)
TEST(write_2018_classes_as_autocad) {
    if (!has_files({kAscii})) return;
    std::map<std::string, Record> autocad;
    for (const auto& r : section(records_of(read_bytes(external(kAscii))), "CLASSES")) {
        if (r[0].second == "CLASS") autocad[value(r, 1)] = r;
    }
    hdxf_test::assert_eq(static_cast<int>(autocad.size()), 49, "AutoCAD's 2018 classes");
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
    dwg.add_circle({0, 0, 0}, 1.0);
    int compared = 0;
    for (const auto& r : section(records_of(save(dwg)), "CLASSES")) {
        if (r[0].second != "CLASS") continue;
        auto it = autocad.find(value(r, 1));
        hdxf_test::assert_true(it != autocad.end(), "class " + value(r, 1) + " is AutoCAD's");
        if (it == autocad.end()) continue;
        ++compared;
        for (int code : {1, 2, 3, 90, 280, 281}) {
            hdxf_test::assert_eq(value(r, code), value(it->second, code), value(r, 1) + ": " + std::to_string(code));
        }
    }
    hdxf_test::assert_true(compared >= 5, "classes compared: " + std::to_string(compared));
}
