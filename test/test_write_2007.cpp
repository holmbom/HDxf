#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "text_encoding.hpp"
#include "dxf_structure.hpp"
#include "dxf_writer.hpp"
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
// DXF 2007 (phase 3, step 3.2 — plan B30–B35): a drawing in memory written as AutoCAD
// writes 2007, AutoCAD's own 2007 file written back record for record, the fields 2007 adds
// read from AutoCAD's files, and text in UTF-8.
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

std::filesystem::path synthetic(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / hdxf::path_from_utf8(name);
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
        for (const auto& [code, value] : r) {
            if ((code == 5 || code == 105) && value == handle) return r;
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

// The groups of a record from its subclass `subclass` to the next one
Record groups_in(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 100) { in = g.second == subclass; continue; }
        if (in) out.push_back(g);
    }
    return out;
}

std::vector<int> codes_of(const Record& r) {
    std::vector<int> out;
    for (const auto& g : r) out.push_back(g.first);
    return out;
}

// The variable names of a file's HEADER, in order
std::vector<std::string> header_names(const std::vector<Record>& records) {
    std::vector<std::string> out;
    for (const auto& [code, v] : records.at(0)) if (code == 9) out.push_back(v);
    return out;
}

std::string header_value(const std::vector<Record>& records, const std::string& name) {
    const Record& h = records.at(0);
    for (size_t i = 0; i + 1 < h.size(); ++i) {
        if (h[i].first == 9 && h[i].second == name) return h[i + 1].second;
    }
    return "?";
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

// The records of a type with their content after the head (handle, reactors, owner), and
// what an owner's name is — to compare two files whose handles differ
struct Content {
    std::string name;
    Record body;
};

std::vector<Content> contents(const std::vector<Record>& records, const std::string& type, int name_code) {
    std::vector<Content> out;
    for (const auto& r : records) {
        if (r[0].second != type) continue;
        Content c;
        c.name = value(r, name_code);
        bool body = false;
        for (const auto& g : r) {
            if (g.first == 100) body = true;
            if (body) c.body.push_back(g);
        }
        out.push_back(c);
    }
    return out;
}

const char* const kAscii = "acadsharp/sample_AC1021_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1021_binary.dxf";
const char* const kNew = "jscad/empty2007.dxf";

} // namespace

// A new 2007 drawing: AutoCAD's 251 header variables in AutoCAD's order, AC1021 and 50, the
// materials and visual styles AutoCAD's new drawing has (B34) and the pointers to them
TEST(write_2007_new_drawing) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    dwg.add_line({0, 0, 0}, {10, 5, 0});
    dwg.add_viewport({100, 100, 0}, 80, 60).props.paper_space = 1;
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "new 2007 drawing");
    std::vector<Record> recs = records_of(out);

    std::vector<std::string> names = header_names(recs);
    hdxf_test::assert_eq(static_cast<int>(names.size()), 251, "header variables");
    if (has_files({kAscii})) {
        hdxf_test::assert_true(names == header_names(records_of(read_bytes(external(kAscii)))),
                               "AutoCAD's variables in AutoCAD's order");
    }
    hdxf_test::assert_eq(header_value(recs, "$ACADVER"), "AC1021");
    hdxf_test::assert_eq(header_value(recs, "$ACADMAINTVER"), "50");
    hdxf_test::assert_eq(header_value(recs, "$LOFTPARAM"), "7");
    hdxf_test::assert_eq(header_value(recs, "$TIMEZONE"), "-8000");
    hdxf_test::assert_eq(header_value(recs, "$LOFTANG1"), "1.570796326794896");

    // The header points to ByLayer, Realistic and Wireframe; the layers to Global, the
    // viewports to 2dWireframe
    auto named = [&](const std::string& handle, int code) { return value(record(recs, handle), code); };
    hdxf_test::assert_eq(named(header_value(recs, "$CMATERIAL"), 1), "ByLayer", "$CMATERIAL");
    hdxf_test::assert_eq(named(header_value(recs, "$INTERFEREOBJVS"), 2), "Realistic", "$INTERFEREOBJVS");
    hdxf_test::assert_eq(named(header_value(recs, "$INTERFEREVPVS"), 2), "Wireframe", "$INTERFEREVPVS");
    for (const auto& r : recs) {
        if (r[0].second == "LAYER") hdxf_test::assert_eq(named(value(r, 347), 1), "Global", "LAYER 347");
        if (r[0].second == "VPORT") {
            hdxf_test::assert_eq(named(value(r, 348), 2), "2dWireframe", "VPORT 348");
            Record body = groups_in(r, "AcDbViewportTableRecord");
            std::vector<int> codes = codes_of(body);
            auto at = std::find(codes.begin(), codes.end(), 146);
            hdxf_test::assert_true(std::vector<int>(at, codes.end()) ==
                                       std::vector<int>({146, 348, 60, 61, 292, 282, 141, 142, 63, 421}),
                                   "VPORT's 2007 groups after 146, in AutoCAD's order");
            hdxf_test::assert_eq(value(r, 421), "3355443", "ambient color");
        }
        if (r[0].second == "VIEWPORT") {
            std::vector<int> codes = codes_of(groups_in(r, "AcDbViewport"));
            auto at = std::find(codes.begin(), codes.end(), 170);
            hdxf_test::assert_true(std::vector<int>(at, codes.end()) ==
                                       std::vector<int>({170, 61, 348, 292, 282, 141, 142, 63, 421}),
                                   "VIEWPORT's 2007 groups after 170");
        }
        if (r[0].second == "BLOCK_RECORD") {
            std::vector<int> codes = codes_of(groups_in(r, "AcDbBlockTableRecord"));
            hdxf_test::assert_true(codes.size() >= 3 && std::vector<int>(codes.end() - 3, codes.end()) ==
                                                            std::vector<int>({70, 280, 281}),
                                   "BLOCK_RECORD 70, 280, 281 last");
        }
    }

    // MATERIAL and VISUALSTYLE record for record as in AutoCAD's new 2007 drawing, with
    // their classes
    if (has_files({kNew})) {
        std::vector<Record> autocad = records_of(read_bytes(external(kNew)));
        for (const auto& [type, code] : {std::pair<std::string, int>{"MATERIAL", 1}, {"VISUALSTYLE", 2}}) {
            std::vector<Content> ours = contents(recs, type, code);
            std::vector<Content> theirs = contents(autocad, type, code);
            hdxf_test::assert_eq(static_cast<int>(ours.size()), static_cast<int>(theirs.size()), type);
            for (size_t i = 0; i < ours.size() && i < theirs.size(); ++i) {
                hdxf_test::assert_eq(ours[i].name, theirs[i].name, type + " order");
                hdxf_test::assert_true(ours[i].body == theirs[i].body, type + " " + ours[i].name);
            }
        }
        // Each visual style's ACAD_XREC_ROUNDTRIP as AutoCAD's
        std::map<std::string, Record> ours_rt, theirs_rt;
        auto roundtrips = [&](const std::vector<Record>& file, std::map<std::string, Record>& out) {
            for (const auto& r : file) {
                if (r[0].second != "VISUALSTYLE") continue;
                std::string xdict = value(r, 360);
                std::string xrec = value(record(file, xdict), 360);
                out[value(r, 2)] = groups_in(record(file, xrec), "AcDbXrecord");
            }
        };
        roundtrips(recs, ours_rt);
        roundtrips(autocad, theirs_rt);
        hdxf_test::assert_true(ours_rt == theirs_rt, "the visual styles' round-trip records");
        std::map<std::string, Record> ours_c, theirs_c;
        for (const auto& r : recs) if (r[0].second == "CLASS") ours_c[value(r, 1)] = r;
        for (const auto& r : autocad) if (r[0].second == "CLASS") theirs_c[value(r, 1)] = r;
        for (const char* name : {"MATERIAL", "VISUALSTYLE"}) {
            hdxf_test::assert_true(ours_c[name] == theirs_c[name], std::string("class ") + name);
        }
    }

    hdxf::Drawing back = load(out);
    hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R2007, "version read back");
    std::vector<uint8_t> bin = save(dwg, hdxf::DxfFormat::Binary);
    hdxf_test::assert_eq(static_cast<int>(records_of(bin).size()), static_cast<int>(recs.size()), "binary");
    // A second save adds nothing
    hdxf_test::assert_eq(static_cast<int>(records_of(save(dwg)).size()), static_cast<int>(recs.size()), "second save");
}

// A drawing made as 2000 and set to 2007 gets 2007's objects, and the 2000 file it was
// still is a 2000 file
TEST(write_2007_set_version) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_circle({0, 0, 0}, 1.0);
    std::vector<uint8_t> r2000 = save(dwg);
    hdxf_test::assert_eq(header_value(records_of(r2000), "$ACADVER"), "AC1015");
    dwg.set_version(hdxf::DxfVersion::R2007);
    std::vector<Record> recs = records_of(save(dwg));
    hdxf_test::assert_true(contents(recs, "VISUALSTYLE", 2).size() > 20, "visual styles");
    hdxf_test::assert_eq(static_cast<int>(contents(recs, "MATERIAL", 1).size()), 3, "materials");
    // And back as 2004: the 40 variables of 2007 are not written there
    dwg.set_version(hdxf::DxfVersion::R2004);
    std::vector<Record> r2004 = records_of(save(dwg));
    hdxf_test::assert_eq(static_cast<int>(header_names(r2004).size()), 211, "2004 header");
    // Nor as 2000 or R12 — the three pointers ensure_r2007_objects() set ($CMATERIAL,
    // $INTERFEREOBJVS, $INTERFEREVPVS) included: R12 has the variables of a new R12 drawing
    hdxf::Drawing plain_r12;
    std::vector<std::string> r12_names = header_names(records_of(save(plain_r12)));
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R12}) {
        dwg.set_version(v);
        std::vector<std::string> names = header_names(records_of(save(dwg)));
        for (const char* only_2007 : {"$CMATERIAL", "$INTERFEREOBJVS", "$INTERFEREVPVS", "$LATITUDE", "$DIMFXL"}) {
            hdxf_test::assert_true(std::find(names.begin(), names.end(), only_2007) == names.end(),
                                   std::string(only_2007) + " not written as an older version");
        }
        if (v == hdxf::DxfVersion::R12) hdxf_test::assert_true(names == r12_names, "R12's variables");
    }
}

// What 2007 introduced cannot be written as an older version (B35): an error before
// anything is changed
TEST(write_2007_properties_refused_as_older) {
    struct Case {
        const char* message;
        void (*set)(hdxf::Drawing&);
    };
    const Case cases[] = {
        {"A material requires DXF 2007 or later",
         [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.material_handle = "7F"; }},
        {"A layer's material requires DXF 2007 or later",
         [](hdxf::Drawing& d) { d.add_layer("M").material_handle = "7F"; }},
        {"A viewport's lighting requires DXF 2007 or later",
         [](hdxf::Drawing& d) { d.add_viewport({0, 0, 0}, 10, 10).as_viewport().brightness = 20.0; }},
        {"A block's units, explodability and scaling requires DXF 2007 or later",
         [](hdxf::Drawing& d) { d.add_block("B").record.insert_units = 4; }},
        {"A solid's history requires DXF 2007 or later",
         [](hdxf::Drawing& d) { d.add_acis(hdxf::EntityType::Solid3D, {}).as_acis().history_handle = "A1"; }},
    };
    for (const auto& c : cases) {
        for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2000}) {
            hdxf::Drawing dwg(v);
            c.set(dwg);
            hdxf_test::assert_true(dwg.save_to_memory().empty(), c.message);
            hdxf_test::assert_eq(dwg.last_error(), c.message);
        }
    }
    hdxf::Drawing vport;
    vport.vports().push_back(hdxf::Vport{});
    vport.vports().back().grid_major = 10;
    hdxf_test::assert_true(vport.save_to_memory().empty(), "R12 VPORT");
    hdxf_test::assert_eq(vport.last_error(), "A VPORT's grid settings requires DXF 2007 or later");
    vport.set_version(hdxf::DxfVersion::R2007);
    hdxf_test::assert_false(vport.save_to_memory().empty(), vport.last_error());
}

// Read and written in the same format: every record as in the file, group for group
TEST(roundtrip_2007_files_record_for_record) {
    struct File { std::filesystem::path path; hdxf::DxfFormat format; };
    std::vector<File> files = {{synthetic("r2007_utf8.dxf"), hdxf::DxfFormat::Ascii}};
    if (std::filesystem::exists(external(kAscii))) files.push_back({external(kAscii), hdxf::DxfFormat::Ascii});
    if (std::filesystem::exists(external(kBinary))) files.push_back({external(kBinary), hdxf::DxfFormat::Binary});
    if (std::filesystem::exists(external(kNew))) files.push_back({external(kNew), hdxf::DxfFormat::Ascii});
    for (const auto& f : files) {
        std::string what = hdxf::utf8_from_path(f.path.filename());
        std::vector<uint8_t> in = read_bytes(f.path);
        hdxf::Drawing dwg = load(in);
        std::vector<Record> before = records_of(in);
        std::vector<Record> after = records_of(save(dwg, f.format));
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()),
                             what + ": records");
        for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
            hdxf_test::assert_true(before[i] == after[i], what + ": record " + std::to_string(i) +
                                   " (" + before[i][0].second + ") differs");
        }
        hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), what + ": warnings");
    }
}

// AutoCAD's 2007 files come back byte for byte
TEST(roundtrip_2007_bytes) {
    for (const char* name : {kBinary, kAscii}) {
        if (!has_files({name})) return;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        bool binary = std::string(name) == kBinary;
        std::vector<uint8_t> out = save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        if (!binary) {
            // ASCII is written with the platform's line endings when saved to a file; in
            // memory with "\n" — compare without the "\r"
            std::vector<uint8_t> lf;
            for (uint8_t b : in) if (b != '\r') lf.push_back(b);
            in = lf;
        }
        hdxf_test::assert_true(out == in, std::string(name) + ": bytes");
    }
}

// Into the other format and back: the same values (binary exactly, ASCII with AutoCAD's 16
// significant digits); the UTF-8 text the same in both
TEST(roundtrip_2007_ascii_binary_ascii) {
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
}

// Changes to AutoCAD's 2007 file: written anew where they are, everything else as it was
TEST(roundtrip_2007_changes) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);

    auto find = [&](const std::string& handle) -> hdxf::Entity* {
        for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
        return nullptr;
    };
    hdxf_test::assert_true(find("2C7") != nullptr && find("69D") != nullptr && find("3BA") != nullptr &&
                           find("E00") != nullptr, "the entities of the 2004 test are in the 2007 file too");
    find("2C7")->as_line().end.x = 50.0;
    find("E00")->props.material_handle.clear();          // the circle's material → ByLayer
    for (auto& l : dwg.layers()) {
        if (l.name == "Layer1") l.color = hdxf::Color::from_aci(3);
    }
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) {
        return e.props.handle == "69D" || e.props.handle == "3BA";
    }), ents.end());
    hdxf::Entity& added = dwg.add_line({0, 0, 0}, {1, 1, 0}, "Layer1");
    added.props.material_handle = "DC0";
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "changed AutoCAD 2007 file");
    std::vector<Record> after = records_of(out);

    Record line = record(after, "2C7");
    hdxf_test::assert_eq(std::strtod(value(line, 11).c_str(), nullptr), 50.0, "moved");
    hdxf_test::assert_eq(value(line, 10), value(record(before, "2C7"), 10), "start as it was");
    hdxf_test::assert_eq(value(record(after, "E00"), 347), "", "the circle's material removed");
    Record new_line = record(after, dwg.entities().back().props.handle);
    Record head = groups_in(new_line, "AcDbEntity");
    hdxf_test::assert_true(codes_of(head) == std::vector<int>({8, 347}), "8, 347 for the new line");
    for (const char* h : {"69D", "3BA"}) hdxf_test::assert_true(record(after, h).empty(), std::string(h) + " still there");

    int same = 0;
    for (const auto& r : before) {
        std::string h;
        for (const auto& [code, v] : r) { if (code == 5) { h = v; break; } if (code == 100) break; }
        if (h.empty() || r[0].second == "SECTION" || h == "2C7" || h == "E00" || h == "69E") continue;
        Record w = record(after, h);
        if (w.empty()) continue;
        if (w == r) ++same;
        else if (r[0].second != "LAYER" && r[0].second != "XRECORD" && r[0].second != "DICTIONARY") {
            hdxf_test::assert_true(false, "record " + h + " (" + r[0].second + ") changed");
        }
    }
    // 925 records with a handle, less the ones removed, changed and the group's
    hdxf_test::assert_true(same > 900, "unchanged records: " + std::to_string(same));
}

// The fields 2007 adds, read from AutoCAD's file (ACadSharp's sample_AC1021, B33)
TEST(read_2007_fields) {
    if (!has_files({kAscii, kBinary})) return;
    for (const char* name : {kAscii, kBinary}) {
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        const hdxf::Vport& vp = dwg.vports().at(0);
        hdxf_test::assert_eq(vp.visual_style_handle, "9F", "VPORT 348");
        hdxf_test::assert_eq(vp.grid_flags, 3, "VPORT 60");
        hdxf_test::assert_eq(vp.grid_major, 5, "VPORT 61");
        hdxf_test::assert_true(vp.default_lighting && vp.default_lighting_type == 1, "VPORT 292/282");
        hdxf_test::assert_eq(vp.ambient_color.aci, 250, "VPORT 63");
        hdxf_test::assert_eq(vp.ambient_color.rgb, 3355443, "VPORT 421");
        for (const auto& l : dwg.layers()) hdxf_test::assert_eq(l.material_handle, "98", "LAYER 347 " + l.name);
        const hdxf::View& view = dwg.views().at(0);
        hdxf_test::assert_eq(view.visual_style_handle, "9F", "VIEW 348");
        hdxf_test::assert_false(view.camera_plottable, "VIEW 73");
        const hdxf::Block* block = dwg.find_block("MyBlock");
        hdxf_test::assert_true(block && block->record.insert_units == 4 && block->record.explodable,
                               "BLOCK_RECORD 70/280");
        int texts = 0, locked = 0;
        bool circle = false, point = false, solid = false;
        auto entities = [&](const std::vector<hdxf::Entity>& list) {
            for (const auto& e : list) {
                if (e.type == hdxf::EntityType::MText) {
                    ++texts;
                    hdxf_test::assert_eq(e.as_mtext().defined_height, 0.0, "MTEXT 46");
                }
                if (e.type == hdxf::EntityType::Attdef && e.as_attdef().lock_position) ++locked;
                if (e.type == hdxf::EntityType::Insert) {
                    for (const auto& a : e.as_insert().attributes) locked += a.data.lock_position ? 1 : 0;
                }
                if (e.props.handle == "E00") circle = e.props.material_handle == "DC0";
                if (e.props.handle == "298") point = e.props.material_handle == "97";
                if (e.props.handle == "D65") solid = e.as_acis().history_handle == "D66";
            }
        };
        entities(dwg.entities());
        for (const auto& b : dwg.blocks()) entities(b.entities);
        hdxf_test::assert_true(texts > 0, "MTEXT");
        hdxf_test::assert_eq(locked, 9, "five ATTDEF and four ATTRIB locked (280 1)");
        hdxf_test::assert_true(circle && point, "347 on CIRCLE E00 and POINT 298");
        hdxf_test::assert_true(solid, "3DSOLID D65's history D66");
        for (const auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::Viewport) continue;
            hdxf_test::assert_eq(e.as_viewport().visual_style_handle, "9F", "VIEWPORT 348");
            hdxf_test::assert_eq(e.as_viewport().grid_major, 5, "VIEWPORT 61");
        }
    }
}

// An attribute's locked position: 280 in 2007, XDATA AcDbAttr up to 2004 (R12 too) — read
// from AutoCAD's 2004 file as the field (the XDATA no longer in xdata), written back as
// AutoCAD writes it, with the APPID when it is new
TEST(attribute_lock_by_version) {
    if (has_files({"acadsharp/sample_AC1018_ascii.dxf"})) {
        hdxf::Drawing dwg = load(read_bytes(external("acadsharp/sample_AC1018_ascii.dxf")));
        int locked = 0;
        for (const auto& b : dwg.blocks()) {
            for (const auto& e : b.entities) {
                if (e.type != hdxf::EntityType::Attdef) continue;
                locked += e.as_attdef().lock_position ? 1 : 0;
                for (const auto& x : e.props.xdata) hdxf_test::assert_true(x.app_name != "AcDbAttr", "AcDbAttr taken");
            }
        }
        hdxf_test::assert_eq(locked, 5, "2004: five ATTDEF locked by XDATA");
    }
    auto build = [](hdxf::DxfVersion v) {
        hdxf::Drawing dwg(v);
        hdxf::Block& b = dwg.add_block("T");
        hdxf::Entity def;
        def.type = hdxf::EntityType::Attdef;
        hdxf::AttdefData a;
        a.tag = "X";
        a.lock_position = true;
        def.data = a;
        b.entities.push_back(def);
        hdxf::Entity& ins = dwg.add_insert("T", {0, 0, 0});
        hdxf::InsertAttribute at;
        at.data.tag = "X";
        at.data.value = "1";
        at.data.lock_position = true;
        ins.as_insert().attributes.push_back(at);
        return dwg;
    };
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004,
                               hdxf::DxfVersion::R2007}) {
        hdxf::Drawing dwg = build(v);
        std::vector<uint8_t> out = save(dwg);
        std::vector<Record> recs = records_of(out);
        bool r2007 = v == hdxf::DxfVersion::R2007;
        bool appid = false;
        for (const auto& r : recs) {
            if (r[0].second == "APPID" && value(r, 2) == "AcDbAttr") appid = true;
            if (r[0].second != "ATTDEF" && r[0].second != "ATTRIB") continue;
            if (r2007) {
                hdxf_test::assert_eq(r.back().first, 280, "280 last");
                hdxf_test::assert_eq(r.back().second, "1", "locked");
            } else {
                Record tail(r.end() - 3, r.end());
                hdxf_test::assert_true(tail == Record({{1001, "AcDbAttr"}, {1070, "0"}, {1070, "1"}}),
                                       r[0].second + ": XDATA AcDbAttr");
            }
        }
        hdxf_test::assert_true(appid == !r2007, "APPID AcDbAttr");
        hdxf::Drawing back = load(out);
        int locked = 0;
        for (const auto& b : back.blocks()) {
            for (const auto& e : b.entities) locked += (e.type == hdxf::EntityType::Attdef && e.as_attdef().lock_position);
        }
        for (const auto& e : back.entities()) {
            if (e.type == hdxf::EntityType::Insert) locked += e.as_insert().attributes.at(0).data.lock_position;
        }
        hdxf_test::assert_eq(locked, 2, "read back");
    }
}

// DIMSTYLE in 2007: the variables 2000/2004 keep in XDATA as group codes, in ascending
// order; AutoCAD's styles written anew give AutoCAD's groups (ACadSharp's sample_AC1021)
TEST(write_2007_dimstyle_codes) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    for (auto& ds : dwg.dim_styles()) ds.raw = hdxf::RawRecord();     // written anew
    std::vector<Record> after = records_of(save(dwg));
    for (const char* h : {"994", "C88", "39A", "27"}) {
        hdxf_test::assert_true(groups_in(record(after, h), "AcDbDimStyleTableRecord") ==
                               groups_in(record(before, h), "AcDbDimStyleTableRecord"),
                               std::string("DIMSTYLE ") + h);
    }
    hdxf::Drawing made(hdxf::DxfVersion::R2007);
    hdxf::DimStyle& ds = made.add_dim_style("Fast");
    ds.dimfxlon = 1;
    ds.dimfxl = 2.0;
    ds.dimjogang = 30.0;
    ds.dimtfill = 2;
    ds.dimtfillclr = hdxf::Color::from_aci(5);
    std::vector<Record> recs = records_of(save(made));
    for (const auto& r : recs) {
        if (r[0].second != "DIMSTYLE" || value(r, 2) != "Fast") continue;
        std::vector<int> codes = codes_of(groups_in(r, "AcDbDimStyleTableRecord"));
        auto pos = [&](int code, int occurrence = 0) {
            for (size_t i = 0; i < codes.size(); ++i) if (codes[i] == code && occurrence-- == 0) return static_cast<int>(i);
            return -1;
        };
        hdxf_test::assert_true(pos(49) >= 0 && pos(49) < pos(50) && pos(50) < pos(69) && pos(69) < pos(70, 1) &&
                               pos(70, 1) < pos(73) && pos(289) < pos(290) && pos(290) < pos(340), "ascending");
        hdxf_test::assert_eq(value(r, 70, 1), "5", "DIMTFILLCLR");
        for (const auto& [code, v] : r) hdxf_test::assert_true(code < 1000, "no XDATA");
    }
}

// Text in 2007 is UTF-8: no \U+, except for a character above U+FFFF — its surrogates in two
// escapes, the form AutoCAD reads (B31, B55); the same drawing as 2004 writes \U+XXXX
TEST(write_2007_text_utf8) {
    const std::string text = "Grön äng \xF0\x9F\x98\x80 \xE2\x88\x85";
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
        dwg.add_text({0, 0, 0}, text);
        dwg.add_layer("Blå");
        std::vector<uint8_t> out = save(dwg, format);
        hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), "no warnings");
        std::string s(out.begin(), out.end());
        hdxf_test::assert_true(s.find("Grön äng \\U+D83D\\U+DE00 \xE2\x88\x85") != std::string::npos,
                               "UTF-8, the surrogates escaped");
        hdxf_test::assert_true(s.find("\\U+00") == std::string::npos, "no \\U+ for the rest");
        hdxf::Drawing back = load(out);
        bool found = false;
        for (const auto& e : back.entities()) found = found || (e.type == hdxf::EntityType::Text && e.as_text().content == text);
        hdxf_test::assert_true(found, "read back");
    }
    hdxf::Drawing old(hdxf::DxfVersion::R2004);
    old.add_text({0, 0, 0}, text);
    std::vector<uint8_t> out = save(old);
    std::string s(out.begin(), out.end());
    hdxf_test::assert_true(s.find("Gr\\U+00F6n \\U+00E4ng \\U+D83D\\U+DE00 \\U+2205") != std::string::npos, "2004: \\U+");
}

// The dimension text in 2007: the degree sign as AutoCAD's dimension engine writes it
// (\U+00B0 in the text), the diameter sign as the character (ACadSharp's sample_AC1021)
TEST(write_2007_dimension_text) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::Entity& angle = dwg.add_dimension(hdxf::DimensionType::Angular3Pt, {90, 10, 0}, {95, 8, 0});
    angle.as_dimension().def_point2 = {100, 0, 0};
    angle.as_dimension().def_point3 = {80, 20, 0};
    angle.as_dimension().def_point4 = {80, 0, 0};
    hdxf::Entity& diameter = dwg.add_dimension(hdxf::DimensionType::Diameter, {65, 0, 0}, {70, 5, 0});
    diameter.as_dimension().def_point4 = {55, 0, 0};
    std::vector<uint8_t> out = save(dwg);
    std::string s(out.begin(), out.end());
    hdxf_test::assert_true(s.find("90\\U+00B0") != std::string::npos, "\\U+00B0");
    hdxf_test::assert_true(s.find("\xE2\x88\x85" "10") != std::string::npos, "the diameter sign in UTF-8");
    std::vector<Record> recs = records_of(out);
    for (const auto& r : recs) {
        if (r[0].second != "DIMENSION") continue;
        std::vector<int> codes = codes_of(groups_in(r, "AcDbDimension"));
        auto at = std::find(codes.begin(), codes.end(), 42);
        hdxf_test::assert_true(at != codes.end() && codes.end() - at > 3 &&
                                   std::vector<int>(at, at + 4) == std::vector<int>({42, 73, 74, 75}),
                               "73, 74, 75 after 42");
    }
}

// A typed 2007 pointer to a record that is gone is left out (B19, as 333 of a layout): the
// entity's material is then ByLayer
TEST(roundtrip_2007_pointer_removed) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    std::string material;
    for (const auto& o : dwg.objects()) {
        if (o.type == hdxf::ObjectType::Material) material = o.handle;
    }
    hdxf::Entity& line = dwg.add_line({0, 0, 0}, {1, 0, 0});
    line.props.material_handle = material;
    std::vector<uint8_t> first = save(dwg);
    hdxf::Drawing read = load(first);
    auto& objects = read.objects();
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [&](const hdxf::Object& o) { return o.handle == material; }), objects.end());
    std::vector<uint8_t> out = save(read);
    dxf_structure::check_structure(out, "347 to a removed MATERIAL");
    for (const auto& r : records_of(out)) {
        if (r[0].second == "LINE") hdxf_test::assert_eq(value(r, 347), "", "no 347");
    }
}
