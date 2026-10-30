#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "text_encoding.hpp"
#include "dxf_structure.hpp"
#include "dxf_writer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DXF 2010 (phase 4, step 4.2 — plan B42–B46): a drawing in memory written as AutoCAD writes
// 2010, AutoCAD's own 2010 files written back record for record (ACadSharp's byte for byte),
// the fields 2010 adds read from AutoCAD's files, and what only 2010 has refused as older.
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

// A header variable's first group (code and value)
std::pair<int, std::string> header_group(const std::vector<Record>& records, const std::string& name) {
    const Record& h = records.at(0);
    for (size_t i = 0; i + 1 < h.size(); ++i) {
        if (h[i].first == 9 && h[i].second == name) return h[i + 1];
    }
    return {0, "?"};
}

std::string header_value(const std::vector<Record>& records, const std::string& name) {
    return header_group(records, name).second;
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

// The records of a type with their content after the head (handle, reactors, owner)
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

// Each visual style's ACAD_XREC_ROUNDTRIP data by the style's name
std::map<std::string, Record> roundtrips(const std::vector<Record>& file) {
    std::map<std::string, Record> out;
    for (const auto& r : file) {
        if (r[0].second != "VISUALSTYLE") continue;
        std::string xdict = value(r, 360);
        std::string xrec = value(record(file, xdict), 360);
        out[value(r, 2)] = groups_in(record(file, xrec), "AcDbXrecord");
    }
    return out;
}

// The visual styles of `ours` record for record as in AutoCAD's new drawing `file`
void same_visual_styles(const std::vector<Record>& ours, const char* file) {
    std::vector<Record> autocad = records_of(read_bytes(external(file)));
    for (const auto& [type, code] : {std::pair<std::string, int>{"MATERIAL", 1}, {"VISUALSTYLE", 2}}) {
        std::vector<Content> a = contents(ours, type, code);
        std::vector<Content> b = contents(autocad, type, code);
        hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), type);
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            hdxf_test::assert_eq(a[i].name, b[i].name, type + " order");
            hdxf_test::assert_true(a[i].body == b[i].body, std::string(file) + ": " + type + " " + a[i].name);
        }
    }
    hdxf_test::assert_true(roundtrips(ours) == roundtrips(autocad), std::string(file) + ": round-trip records");
}

const char* const kAscii = "acadsharp/sample_AC1024_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1024_binary.dxf";
const char* const kNew = "jscad/empty2010.dxf";
const char* const kNew2007 = "jscad/empty2007.dxf";
const char* const kLeader = "libre/Leader_2010.dxf";
const char* const kGeo = "libre/gh209_1_2010.dxf";

} // namespace

// A new 2010 drawing: AutoCAD's 252 header variables in AutoCAD's order, AC1024 and 225,
// $XCLIPFRAME as 280, the visual styles in 2010's form (B45), no APPID ACAD_MLEADERVER
TEST(write_2010_new_drawing) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    dwg.add_line({0, 0, 0}, {10, 5, 0});
    dwg.add_viewport({100, 100, 0}, 80, 60).props.paper_space = 1;
    dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "new 2010 drawing");
    std::vector<Record> recs = records_of(out);

    std::vector<std::string> names = header_names(recs);
    hdxf_test::assert_eq(static_cast<int>(names.size()), 252, "header variables");
    if (has_files({kAscii})) {
        hdxf_test::assert_true(names == header_names(records_of(read_bytes(external(kAscii)))),
                               "AutoCAD's variables in AutoCAD's order");
    }
    if (has_files({kNew})) {
        hdxf_test::assert_true(names == header_names(records_of(read_bytes(external(kNew)))),
                               "the variables of AutoCAD's new 2010 drawing");
    }
    hdxf_test::assert_eq(header_value(recs, "$ACADVER"), "AC1024");
    hdxf_test::assert_eq(header_value(recs, "$ACADMAINTVER"), "225");
    hdxf_test::assert_true(header_group(recs, "$XCLIPFRAME") == std::pair<int, std::string>(280, "0"), "$XCLIPFRAME 280");
    hdxf_test::assert_true(header_group(recs, "$DIMTXTDIRECTION") == std::pair<int, std::string>(70, "0"),
                           "$DIMTXTDIRECTION");

    for (const auto& r : recs) {
        if (r[0].second == "APPID") hdxf_test::assert_true(value(r, 2) != "ACAD_MLEADERVER", "no ACAD_MLEADERVER");
        if (r[0].second == "MULTILEADER" || r[0].second == "MLEADERSTYLE") {
            for (const auto& [code, v] : r) hdxf_test::assert_true(code < 1000 || v != "ACAD_MLEADERVER", "no XDATA version");
        }
        if (r[0].second == "MULTILEADER") {
            hdxf_test::assert_eq(value(r, 160) != "" ? 1 : 0, 1, "proxy graphics size in 160");
            Record body = groups_in(r, "AcDbMLeader");
            hdxf_test::assert_true(!body.empty() && body[0] == Record::value_type{270, "2"}, "270 2 first");
        }
        if (r[0].second == "MLEADERSTYLE") {
            Record body = groups_in(r, "AcDbMLeaderStyle");
            hdxf_test::assert_true(!body.empty() && body[0] == Record::value_type{179, "2"}, "179 2 first");
            Record tail(body.end() - 3, body.end());
            hdxf_test::assert_true(tail == Record({{271, "0"}, {272, "9"}, {273, "9"}}), "271-273 last");
        }
    }

    // MATERIAL and VISUALSTYLE record for record as in AutoCAD's new 2010 drawing
    if (has_files({kNew})) same_visual_styles(recs, kNew);

    hdxf::Drawing back = load(out);
    hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R2010, "version read back");
    std::vector<uint8_t> bin = save(dwg, hdxf::DxfFormat::Binary);
    hdxf_test::assert_eq(static_cast<int>(records_of(bin).size()), static_cast<int>(recs.size()), "binary");
    hdxf_test::assert_eq(static_cast<int>(records_of(save(dwg)).size()), static_cast<int>(recs.size()), "second save");
}

// A drawing made as 2007 and set to 2010 gets the visual styles in 2010's form, and back in
// 2007's (B45); one changed is left as it is
TEST(write_2010_visual_styles_follow_the_version) {
    if (!has_files({kNew, kNew2007})) return;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    dwg.add_circle({0, 0, 0}, 1.0);
    same_visual_styles(records_of(save(dwg)), kNew2007);
    dwg.set_version(hdxf::DxfVersion::R2010);
    same_visual_styles(records_of(save(dwg)), kNew);
    dwg.set_version(hdxf::DxfVersion::R2007);
    same_visual_styles(records_of(save(dwg)), kNew2007);

    // A style changed by the program keeps its groups in either version
    hdxf::Object* changed = nullptr;
    for (auto& o : dwg.objects()) {
        if (o.type == hdxf::ObjectType::VisualStyle && !changed) changed = &o;
    }
    hdxf_test::assert_true(changed != nullptr, "a visual style");
    changed->as_visual_style().style_type = 9;
    const hdxf::VisualStyleData kept = changed->as_visual_style();
    const std::string changed_handle = changed->handle;
    dwg.set_version(hdxf::DxfVersion::R2010);
    save(dwg);
    bool found = false;
    for (const auto& o : dwg.objects()) {
        if (o.handle != changed_handle) continue;
        found = true;
        hdxf_test::assert_true(o.as_visual_style().form == kept.form && o.as_visual_style().style_type == 9 &&
                                   o.as_visual_style().properties.size() == kept.properties.size(),
                               "kept");
    }
    hdxf_test::assert_true(found, "the changed style is still there");
}

// What 2010 introduced cannot be written as an older version (B46): an error before
// anything is changed
TEST(write_2010_properties_refused_as_older) {
    struct Case {
        const char* message;
        void (*set)(hdxf::Drawing&);
    };
    const Case cases[] = {
        {"An image's clip mode inside requires DXF 2010 or later",
         [](hdxf::Drawing& d) { d.add_image("a.png", {0, 0, 0}, {10, 10}, 10.0).as_image().clip_inside = true; }},
        {"DIMTXTDIRECTION requires DXF 2010 or later",
         [](hdxf::Drawing& d) { d.add_dim_style("R").dimtxtdirection = 1; }},
        {"DIMTXTDIRECTION requires DXF 2010 or later",
         [](hdxf::Drawing& d) { d.set_header_int("$DIMTXTDIRECTION", 70, 1); }},
        {"A MULTILEADER's vertical attachment requires DXF 2010 or later",
         [](hdxf::Drawing& d) {
             hdxf::MLeaderData& m = d.add_mleader({{0, 0, 0}, {2, 3, 0}}, "x").as_mleader();
             m.text_attachment_direction = 1;
             m.context.leaders.at(0).attachment_direction = 1;
         }},
        {"A multileader style's vertical attachment requires DXF 2010 or later",
         [](hdxf::Drawing& d) { d.add_mleader_style("V").as_mleader_style().text_attachment_direction = 1; }},
    };
    for (const auto& c : cases) {
        for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2000}) {
            hdxf::Drawing dwg(v);
            c.set(dwg);
            hdxf_test::assert_true(dwg.save_to_memory().empty(), c.message);
            hdxf_test::assert_eq(dwg.last_error(), c.message);
            dwg.set_version(hdxf::DxfVersion::R2010);
            hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
        }
    }
}

// Read and written in the same format: every record as in the file, group for group
TEST(roundtrip_2010_files_record_for_record) {
    struct File { std::filesystem::path path; hdxf::DxfFormat format; };
    std::vector<File> files;
    for (const char* name : {kAscii, kNew, kLeader, kGeo}) {
        if (std::filesystem::exists(external(name))) files.push_back({external(name), hdxf::DxfFormat::Ascii});
    }
    if (std::filesystem::exists(external(kBinary))) files.push_back({external(kBinary), hdxf::DxfFormat::Binary});
    if (files.empty()) hdxf_test::skip("no 2010 files");
    for (const auto& f : files) {
        std::string what = hdxf::utf8_from_path(f.path.filename());
        std::vector<uint8_t> in = read_bytes(f.path);
        hdxf::Drawing dwg = load(in);
        hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R2010, what + ": 2010");
        std::vector<Record> before = records_of(in);
        std::vector<Record> after = records_of(save(dwg, f.format));
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()), what + ": records");
        for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
            hdxf_test::assert_true(before[i] == after[i], what + ": record " + std::to_string(i) +
                                   " (" + before[i][0].second + ") differs");
        }
    }
}

// AutoCAD's 2010 files come back byte for byte
TEST(roundtrip_2010_bytes) {
    for (const char* name : {kBinary, kAscii}) {
        if (!has_files({name})) return;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        bool binary = std::string(name) == kBinary;
        std::vector<uint8_t> out = save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        if (!binary) {
            std::vector<uint8_t> lf;
            for (uint8_t b : in) if (b != '\r') lf.push_back(b);
            in = lf;
        }
        hdxf_test::assert_true(out == in, std::string(name) + ": bytes");
    }
}

// Into the other format and back: the same values (binary exactly, ASCII with AutoCAD's 16
// significant digits)
TEST(roundtrip_2010_ascii_binary_ascii) {
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

// Changes to AutoCAD's 2010 file: written anew where they are, everything else as it was
TEST(roundtrip_2010_changes) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);

    auto find = [&](const std::string& handle) -> hdxf::Entity* {
        for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
        return nullptr;
    };
    hdxf_test::assert_true(find("2C7") != nullptr && find("69D") != nullptr && find("3BA") != nullptr &&
                           find("E00") != nullptr && find("514") != nullptr && find("8C6") != nullptr,
                           "the entities of the 2007 test are in the 2010 file too");
    find("2C7")->as_line().end.x = 50.0;
    find("E00")->props.material_handle.clear();
    find("514")->as_dimension().text_override = "<> mm";           // a dimension written anew
    find("8C6")->as_image().fade = 20;                              // an image written anew
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) {
        return e.props.handle == "69D" || e.props.handle == "3BA";
    }), ents.end());
    dwg.add_line({0, 0, 0}, {1, 1, 0}, "Layer1");
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "changed AutoCAD 2010 file");
    std::vector<Record> after = records_of(out);

    Record line = record(after, "2C7");
    hdxf_test::assert_eq(std::strtod(value(line, 11).c_str(), nullptr), 50.0, "moved");
    hdxf_test::assert_eq(value(record(after, "E00"), 347), "", "the circle's material removed");
    // The dimension and the image keep 2010's groups where AutoCAD has them
    Record dim = groups_in(record(after, "514"), "AcDbDimension");
    hdxf_test::assert_true(!dim.empty() && dim[0] == Record::value_type{280, "0"}, "DIMENSION 280 first");
    hdxf_test::assert_eq(value(record(after, "514"), 1), "<> mm", "the dimension's text");
    std::vector<int> image = codes_of(groups_in(record(after, "8C6"), "AcDbRasterImage"));
    auto at = std::find(image.begin(), image.end(), 283);
    hdxf_test::assert_true(at != image.end() && at + 2 < image.end() && *(at + 1) == 290 && *(at + 2) == 360,
                           "IMAGE 290 after 283");
    hdxf_test::assert_eq(value(record(after, "8C6"), 283), "20", "fade");
    hdxf_test::assert_eq(value(record(after, "8C6"), 160), value(record(before, "8C6"), 160), "its proxy graphics");
    for (const char* h : {"69D", "3BA"}) hdxf_test::assert_true(record(after, h).empty(), std::string(h) + " still there");

    int same = 0;
    for (const auto& r : before) {
        std::string h;
        for (const auto& [code, v] : r) { if (code == 5) { h = v; break; } if (code == 100) break; }
        if (h.empty() || r[0].second == "SECTION" || h == "2C7" || h == "E00" || h == "514" || h == "8C6" || h == "69E") continue;
        Record w = record(after, h);
        if (w.empty()) continue;
        if (w == r) ++same;
        else if (r[0].second != "XRECORD" && r[0].second != "DICTIONARY" && r[0].second != "BLOCK_RECORD" &&
                 r[0].second != "LINE" && r[0].second != "MTEXT" && r[0].second != "SOLID" && r[0].second != "INSERT") {
            hdxf_test::assert_true(false, "record " + h + " (" + r[0].second + ") changed");
        }
    }
    hdxf_test::assert_true(same > 850, "unchanged records: " + std::to_string(same));
}

// The fields 2010 adds, read from AutoCAD's files (ACadSharp's sample_AC1024, B44)
TEST(read_2010_fields) {
    if (!has_files({kAscii, kBinary})) return;
    for (const char* name : {kAscii, kBinary}) {
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        int dimensions = 0, attdefs = 0, attribs = 0, images = 0, locked = 0;
        auto entities = [&](const std::vector<hdxf::Entity>& list) {
            for (const auto& e : list) {
                if (e.type == hdxf::EntityType::Dimension) {
                    ++dimensions;
                    hdxf_test::assert_eq(e.as_dimension().version, 0, "DIMENSION 280");
                }
                if (e.type == hdxf::EntityType::Attdef) {
                    ++attdefs;
                    hdxf_test::assert_eq(e.as_attdef().version, 0, "ATTDEF 280");
                    locked += e.as_attdef().lock_position;
                }
                if (e.type == hdxf::EntityType::Insert) {
                    for (const auto& a : e.as_insert().attributes) {
                        ++attribs;
                        locked += a.data.lock_position;
                    }
                }
                if (e.type == hdxf::EntityType::Image) {
                    ++images;
                    hdxf_test::assert_false(e.as_image().clip_inside, "IMAGE 290");
                }
                if (e.type == hdxf::EntityType::MLeader) {
                    const hdxf::MLeaderData& m = e.as_mleader();
                    hdxf_test::assert_eq(m.version, 2, "270");
                    hdxf_test::assert_eq(m.context.bottom_attachment_direction, 9, "272");
                }
            }
        };
        entities(dwg.entities());
        for (const auto& b : dwg.blocks()) entities(b.entities);
        hdxf_test::assert_eq(dimensions, 11 + 0, std::string(name) + ": dimensions");
        hdxf_test::assert_true(attdefs >= 5 && attribs >= 4, "attributes");
        hdxf_test::assert_eq(locked, 9, "the locks as in 2007 (the last 280)");
        hdxf_test::assert_eq(images, 1, "image");
        for (const auto& o : dwg.objects()) {
            if (o.type == hdxf::ObjectType::MLeaderStyle) hdxf_test::assert_eq(o.as_mleader_style().version, 2, "179");
        }
    }
}

// The 2010 groups where AutoCAD writes them in a drawing built in memory: DIMENSION's and the
// attributes' version first, IMAGE's clip mode after 283 and its outline as proxy graphics,
// DIMSTYLE's 295 among the others in ascending order
TEST(write_2010_codes) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    hdxf::Entity& dim = dwg.add_dimension(hdxf::DimensionType::Aligned, {0, 5, 0}, {5, 6, 0});
    dim.as_dimension().def_point2 = {0, 0, 0};
    dim.as_dimension().def_point3 = {10, 0, 0};
    hdxf::Block& b = dwg.add_block("T");
    hdxf::Entity def;
    def.type = hdxf::EntityType::Attdef;
    hdxf::AttdefData a;
    a.tag = "X";
    a.prompt = "x?";
    def.data = a;
    b.entities.push_back(def);
    hdxf::Entity& ins = dwg.add_insert("T", {0, 0, 0});
    hdxf::InsertAttribute at;
    at.data.tag = "X";
    at.data.value = "1";
    ins.as_insert().attributes.push_back(at);
    hdxf::Entity& image = dwg.add_image("bild.png", {0, 0, 0}, {100, 50}, 10.0);
    image.as_image().clip_inside = true;
    hdxf::DimStyle& ds = dwg.add_dim_style("RTL");
    ds.dimtxtdirection = 1;
    ds.dimfxlon = 1;
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "2010 codes");
    for (const auto& r : records_of(out)) {
        const std::string& t = r[0].second;
        if (t == "DIMENSION") {
            Record body = groups_in(r, "AcDbDimension");
            hdxf_test::assert_true(body.at(0) == Record::value_type{280, "0"} && body.at(1).first == 2, "DIMENSION 280, 2");
        }
        if (t == "ATTDEF") {
            Record body = groups_in(r, "AcDbAttributeDefinition");
            hdxf_test::assert_true(body.at(0) == Record::value_type{280, "0"} && body.at(1).first == 3, "ATTDEF 280, 3");
            hdxf_test::assert_eq(body.back().first, 280, "the lock last");
        }
        if (t == "ATTRIB") {
            Record body = groups_in(r, "AcDbAttribute");
            hdxf_test::assert_true(body.at(0) == Record::value_type{280, "0"} && body.at(1).first == 2, "ATTRIB 280, 2");
        }
        if (t == "IMAGE") {
            std::vector<int> codes = codes_of(groups_in(r, "AcDbRasterImage"));
            auto p = std::find(codes.begin(), codes.end(), 283);
            hdxf_test::assert_true(p != codes.end() && *(p + 1) == 290, "IMAGE 290 after 283");
            hdxf_test::assert_eq(value(r, 290), "1", "inside");
            hdxf_test::assert_eq(value(r, 160), "140", "the outline: a closed polyline of five points");
        }
        if (t == "DIMSTYLE" && value(r, 2) == "RTL") {
            std::vector<int> codes = codes_of(groups_in(r, "AcDbDimStyleTableRecord"));
            auto p290 = std::find(codes.begin(), codes.end(), 290);
            auto p295 = std::find(codes.begin(), codes.end(), 295);
            auto p340 = std::find(codes.begin(), codes.end(), 340);
            hdxf_test::assert_true(p290 < p295 && p295 < p340, "295 between 290 and 340");
        }
    }
    hdxf::Drawing back = load(out);
    for (const auto& ds2 : back.dim_styles()) {
        if (ds2.name == "RTL") hdxf_test::assert_eq(ds2.dimtxtdirection, 1, "295 read back");
    }
    for (const auto& e : back.entities()) {
        if (e.type == hdxf::EntityType::Image) hdxf_test::assert_true(e.as_image().clip_inside, "290 read back");
    }
}

// The bytes of a record's proxy graphics (310 in hex)
std::vector<uint8_t> proxy_bytes(const Record& r) {
    std::vector<uint8_t> out;
    auto digit = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (const auto& [code, v] : r) {
        if (code != 310) continue;
        for (size_t i = 0; i + 1 < v.size(); i += 2) out.push_back(static_cast<uint8_t>(digit(v[i]) * 16 + digit(v[i + 1])));
    }
    return out;
}

// The points of the polylines (command 6) in proxy graphics
std::vector<double> polyline_numbers(const std::vector<uint8_t>& b) {
    std::vector<double> out;
    auto i32 = [&](size_t o) { int32_t v; std::memcpy(&v, b.data() + o, 4); return v; };
    size_t o = 8;
    while (o + 8 <= b.size()) {
        int32_t size = i32(o), type = i32(o + 4);
        if (size <= 0) break;
        if (type == 6) {
            int32_t n = i32(o + 8);
            for (int32_t k = 0; k < 3 * n; ++k) {
                double d;
                std::memcpy(&d, b.data() + o + 12 + 8 * k, 8);
                out.push_back(d);
            }
        }
        o += static_cast<size_t>(size);
    }
    return out;
}

// IMAGE's proxy graphics as AutoCAD writes them in every version from 2000: the outline of
// ACadSharp's image (not clipped: its frame from the corner at the top left) — written anew
// from the binary files, AutoCAD's bytes — and of LibreDWG's clipped ones (their polygons in
// order; the ASCII file's 16 digits give the points to 1e-12)
TEST(write_image_proxy_graphics) {
    int images = 0;
    auto check = [&](const std::string& name, hdxf::DxfFormat format, bool exact) {
        if (!has_files({name})) return;
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        for (auto& e : dwg.entities()) {
            if (e.type == hdxf::EntityType::Image) e.raw = hdxf::RawRecord();     // written anew
        }
        std::vector<Record> after = records_of(save(dwg, format));
        for (const auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::Image) continue;
            ++images;
            std::vector<uint8_t> a = proxy_bytes(record(before, e.props.handle));
            std::vector<uint8_t> b = proxy_bytes(record(after, e.props.handle));
            std::string what = name + ": IMAGE " + e.props.handle + " proxy graphics";
            if (exact) {
                hdxf_test::assert_true(a == b, what);
                continue;
            }
            hdxf_test::assert_eq(static_cast<int>(b.size()), static_cast<int>(a.size()), what);
            std::vector<double> x = polyline_numbers(a), y = polyline_numbers(b);
            hdxf_test::assert_eq(static_cast<int>(y.size()), static_cast<int>(x.size()), what + ": points");
            for (size_t k = 0; k < x.size() && k < y.size(); ++k) {
                hdxf_test::assert_true(std::fabs(x[k] - y[k]) <= 1e-12 * std::max(1.0, std::fabs(x[k])), what);
            }
        }
    };
    for (const char* v : {"1015", "1018", "1021", "1024"}) {
        check(std::string("acadsharp/sample_AC") + v + "_binary.dxf", hdxf::DxfFormat::Binary, true);
    }
    check(kLeader, hdxf::DxfFormat::Ascii, false);
    hdxf_test::assert_true(images > 0, "images");
}

// $XCLIPFRAME: 280 (0, 1, 2) in 2010; 290 in 2004/2007, where AutoCAD keeps a 2 in the
// DICTIONARYVAR XCLIPFRAME and writes 0 in the header (ACadSharp's sample_AC1021, B43)
TEST(write_2010_xclipframe) {
    for (int v : {0, 1, 2}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
        dwg.set_header_int("$XCLIPFRAME", 280, v);
        std::vector<Record> recs = records_of(save(dwg));
        hdxf_test::assert_true(header_group(recs, "$XCLIPFRAME") == std::pair<int, std::string>(280, std::to_string(v)),
                               "2010: 280");
        for (const auto& r : recs) {
            if (r[0].second == "DICTIONARY") hdxf_test::assert_true(std::find(r.begin(), r.end(), Record::value_type{3, "XCLIPFRAME"}) == r.end(), "no variable in 2010");
        }
        for (hdxf::DxfVersion old : {hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2004}) {
            hdxf::Drawing o(old);
            o.set_header_int("$XCLIPFRAME", 290, v);
            std::vector<Record> r2 = records_of(save(o));
            hdxf_test::assert_true(header_group(r2, "$XCLIPFRAME") == std::pair<int, std::string>(290, v == 1 ? "1" : "0"),
                                   "2004/2007: 290");
            std::string var;
            for (const auto& r : r2) {
                if (r[0].second != "DICTIONARY") continue;
                for (size_t i = 0; i + 1 < r.size(); ++i) {
                    if (r[i] == Record::value_type{3, "XCLIPFRAME"}) var = r[i + 1].second;
                }
            }
            hdxf_test::assert_eq(var.empty() ? 0 : 1, v == 2 ? 1 : 0, "DICTIONARYVAR XCLIPFRAME");
            if (!var.empty()) hdxf_test::assert_eq(value(record(r2, var), 1), "2", "its value");
        }
    }
}

// The values of the DICTIONARYVARs a dictionary names XCLIPFRAME in a written file
std::vector<std::string> xclipframe_variables(const std::vector<Record>& records) {
    std::vector<std::string> out;
    for (const auto& r : records) {
        if (r[0].second != "DICTIONARY") continue;
        for (size_t i = 0; i + 1 < r.size(); ++i) {
            if (r[i] == Record::value_type{3, "XCLIPFRAME"}) out.push_back(value(record(records, r[i + 1].second), 1));
        }
    }
    return out;
}

int header_xclipframe(const hdxf::Drawing& dwg) {
    const hdxf::HeaderVar* h = dwg.find_header("$XCLIPFRAME");
    return h && !h->values.empty() ? std::atoi(h->values[0].second.c_str()) : -1;
}

// The DICTIONARYVAR XCLIPFRAME follows the header's value at every save (Codex review of phase
// 4): made for a 2 in 2004/2007 and for every value but 0 in 2000, removed when the value goes
// back, gone when a new drawing is written as 2010 — and a file's variable read into the header
TEST(write_2010_xclipframe_follows_header) {
    using V = std::vector<std::string>;
    for (hdxf::DxfVersion version : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007}) {
        bool r2000 = version == hdxf::DxfVersion::R2000;
        std::string name = "version " + std::to_string(static_cast<int>(version));
        hdxf::Drawing dwg(version);
        dwg.set_header_int("$XCLIPFRAME", 290, 2);
        hdxf_test::assert_true(xclipframe_variables(records_of(save(dwg))) == V{"2"}, name + ": 2");
        dwg.set_header_int("$XCLIPFRAME", 290, 0);
        std::vector<Record> recs = records_of(save(dwg));
        hdxf_test::assert_true(xclipframe_variables(recs).empty(), name + ": 0 removes the variable");
        if (!r2000) hdxf_test::assert_eq(header_value(recs, "$XCLIPFRAME"), "0", name + ": header 0");
        dwg.set_header_int("$XCLIPFRAME", 290, 1);
        recs = records_of(save(dwg));
        hdxf_test::assert_true(xclipframe_variables(recs) == (r2000 ? V{"1"} : V{}), name + ": 1");
        if (!r2000) hdxf_test::assert_eq(header_value(recs, "$XCLIPFRAME"), "1", name + ": header 1");
        dwg.set_header_int("$XCLIPFRAME", 290, 2);
        recs = records_of(save(dwg));
        hdxf_test::assert_true(xclipframe_variables(recs) == V{"2"}, name + ": 2 again, once");
        // Read back: the value in the header
        hdxf::Drawing back = load(save(dwg));
        hdxf_test::assert_eq(header_xclipframe(back), 2, name + ": read");
    }
    // Made as 2007, then written as 2010: no variable
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    dwg.set_header_int("$XCLIPFRAME", 290, 2);
    save(dwg);
    dwg.set_version(hdxf::DxfVersion::R2010);
    std::vector<Record> recs = records_of(save(dwg));
    hdxf_test::assert_true(xclipframe_variables(recs).empty(), "2010: no variable");
    hdxf_test::assert_true(header_group(recs, "$XCLIPFRAME") == std::pair<int, std::string>(280, "2"), "2010: 280 2");

    // AutoCAD's files: the 2 of their variable is the header's; written back unchanged, a change
    // of the value made, given or removed in the variable
    for (const char* file : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                             "acadsharp/sample_AC1021_ascii.dxf", "jscad/empty2007.dxf"}) {
        if (!has_files({file})) continue;
        std::vector<uint8_t> in = read_bytes(external(file));
        hdxf::Drawing d = load(in);
        bool r2000 = d.version() == hdxf::DxfVersion::R2000;
        bool empty = std::string(file) == "jscad/empty2007.dxf";
        hdxf_test::assert_eq(header_xclipframe(d), empty ? 0 : 2, std::string(file) + ": read");
        hdxf_test::assert_true(records_of(save(d)) == records_of(in), std::string(file) + ": unchanged");
        d.set_header_int("$XCLIPFRAME", 290, 0);
        recs = records_of(save(d));
        hdxf_test::assert_true(xclipframe_variables(recs).empty(), std::string(file) + ": 0");
        if (!r2000) hdxf_test::assert_eq(header_value(recs, "$XCLIPFRAME"), "0", std::string(file) + ": header 0");
        hdxf_test::assert_eq(header_xclipframe(load(save(d))), r2000 ? -1 : 0, std::string(file) + ": 0 read");
        d.set_header_int("$XCLIPFRAME", 290, 1);
        recs = records_of(save(d));
        hdxf_test::assert_true(xclipframe_variables(recs) == (r2000 ? V{"1"} : V{}), std::string(file) + ": 1");
        if (!r2000) hdxf_test::assert_eq(header_value(recs, "$XCLIPFRAME"), "1", std::string(file) + ": header 1");
        d.set_header_int("$XCLIPFRAME", 290, 2);
        recs = records_of(save(d));
        hdxf_test::assert_true(xclipframe_variables(recs) == V{"2"}, std::string(file) + ": 2");
        if (!r2000) hdxf_test::assert_eq(header_value(recs, "$XCLIPFRAME"), "0", std::string(file) + ": header 0 for 2");
        hdxf_test::assert_eq(header_xclipframe(load(save(d))), 2, std::string(file) + ": 2 read");
    }
}

// A drawing read from 2007 is not written as 2010 (that is conversion, phase 8), nor one read
// from 2010 as 2007
TEST(write_2010_not_from_another_version) {
    for (const char* name : {"acadsharp/sample_AC1021_ascii.dxf", kAscii}) {
        if (!has_files({name})) return;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        bool from_2010 = dwg.version() == hdxf::DxfVersion::R2010;
        dwg.set_version(from_2010 ? hdxf::DxfVersion::R2007 : hdxf::DxfVersion::R2010);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), name);
        hdxf_test::assert_true(dwg.last_error().find(" as ") != std::string::npos &&
                               dwg.last_error().find("is not supported yet") != std::string::npos, dwg.last_error());
    }
}

// A hatch's spline edge in 2010: its fit points (97, 11) and with them its tangents (12, 13)
// after the control points — 97 0 without them — as AutoCAD 2027's DXFIN reads a 2010 file
// (it refuses the edge without 97, and tangents without fit points); as 2007 the fit points
// are refused (B46)
TEST(write_2010_hatch_spline_fit_points) {
    auto build = [](hdxf::DxfVersion v, bool fit) {
        hdxf::Drawing dwg(v);
        hdxf::Entity& h = dwg.add_hatch({{0, 0}, {10, 0}, {10, 10}});
        hdxf::HatchBoundaryPath path;
        path.flags = 1;
        hdxf::HatchEdge line;
        line.type = hdxf::HatchEdgeType::Line;
        line.start = {0, 0};
        line.end = {10, 0};
        hdxf::HatchEdge spline;
        spline.type = hdxf::HatchEdgeType::Spline;
        spline.degree = 2;
        spline.knots = {0, 0, 0, 1, 1, 1};
        spline.control_points = {{10, 0}, {12, 6}, {0, 0}};
        if (fit) {
            spline.fit_points = {{10, 0}, {8, 3}, {0, 0}};
            spline.start_tangent = {0.3, 0.9};
            spline.end_tangent = {-0.9, -0.3};
        }
        path.edges = {line, spline};
        h.as_hatch().paths = {path};
        return dwg;
    };
    for (bool fit : {false, true}) {
        hdxf::Drawing dwg = build(hdxf::DxfVersion::R2010, fit);
        std::vector<uint8_t> out = save(dwg);
        for (const auto& r : records_of(out)) {
            if (r[0].second != "HATCH") continue;
            std::vector<int> codes = codes_of(groups_in(r, "AcDbHatch"));
            auto at = std::find(codes.begin(), codes.end(), 94);
            std::vector<int> tail(at, codes.end());
            std::vector<int> expected = {94, 73, 74, 95, 96, 40, 40, 40, 40, 40, 40, 10, 20, 10, 20, 10, 20, 97};
            if (fit) {
                for (int k = 0; k < 3; ++k) { expected.push_back(11); expected.push_back(21); }
                for (int c : {12, 22, 13, 23}) expected.push_back(c);
            }
            expected.push_back(97);         // the path's source objects
            hdxf_test::assert_true(std::vector<int>(tail.begin(), tail.begin() + static_cast<long>(expected.size())) == expected,
                                   fit ? "fit points and tangents" : "97 0");
        }
        hdxf::Drawing back = load(out);
        const hdxf::HatchEdge& e = back.entities().at(0).as_hatch().paths.at(0).edges.at(1);
        hdxf_test::assert_eq(static_cast<int>(e.fit_points.size()), fit ? 3 : 0, "read back");
        if (fit) hdxf_test::assert_eq(e.end_tangent.x, -0.9, "tangent read back");
    }
    hdxf::Drawing old = build(hdxf::DxfVersion::R2007, true);
    hdxf_test::assert_true(old.save_to_memory().empty(), "2007");
    hdxf_test::assert_eq(old.last_error(), "A hatch spline edge's fit points requires DXF 2010 or later");
    hdxf::Drawing plain = build(hdxf::DxfVersion::R2007, false);
    hdxf_test::assert_false(plain.save_to_memory().empty(), plain.last_error());
}
