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
// DXF 2013 (phase 5, step 5.2 — plan B56-B59): a drawing in memory written as AutoCAD writes
// 2013, AutoCAD's own 2013 files written back record for record (ACadSharp's byte for byte),
// the fields 2013 adds read from AutoCAD's files and written, and a drawing of another version
// refused as 2013.
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

// Each visual style's ACAD_XREC_ROUNDTRIP data by the style's name (none in 2013)
std::map<std::string, Record> roundtrips(const std::vector<Record>& file) {
    std::map<std::string, Record> out;
    for (const auto& r : file) {
        if (r[0].second != "VISUALSTYLE") continue;
        std::string xdict = value(r, 360);
        std::string xrec = xdict.empty() ? "" : value(record(file, xdict), 360);
        out[value(r, 2)] = xrec.empty() ? Record() : groups_in(record(file, xrec), "AcDbXrecord");
    }
    return out;
}

// The visual styles (and materials) of `ours` record for record as in AutoCAD's new drawing
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

// A record with its handles (and pointers) as "H", for comparing drawings with other handles
Record without_handles(const Record& r) {
    static const std::set<int> handle_codes = {5, 105, 320, 330, 331, 332, 333, 334, 340, 341, 342, 343, 344, 345, 346, 347, 348, 350, 360, 361, 390, 1005};
    Record out;
    for (const auto& [code, v] : r) out.push_back({code, handle_codes.count(code) ? (v == "0" ? "0" : "H") : v});
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

// The records of a new drawing that have a name (or are one of their type), by type and name
std::map<std::string, Record> named_records(const std::vector<Record>& records) {
    static const std::map<std::string, int> name_codes = {
        {"VPORT", 2}, {"LTYPE", 2}, {"LAYER", 2}, {"STYLE", 2}, {"VIEW", 2}, {"UCS", 2}, {"APPID", 2},
        {"DIMSTYLE", 2}, {"BLOCK_RECORD", 2}, {"BLOCK", 2}, {"TABLE", 2}, {"LAYOUT", 1},
        {"MATERIAL", 1}, {"VISUALSTYLE", 2}, {"MLINESTYLE", 2}, {"MLEADERSTYLE", 0}};
    std::map<std::string, Record> out;
    for (const auto& r : records) {
        auto it = name_codes.find(r[0].second);
        if (it == name_codes.end()) continue;
        std::string key = r[0].second + "/" + (it->second ? value(r, it->second) : std::string());
        if (!out.count(key)) out[key] = without_handles(r);
    }
    return out;
}

const char* const kAscii = "acadsharp/sample_AC1027_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1027_binary.dxf";
const char* const kNew = "jscad/empty2013.dxf";
const char* const kNew2010 = "jscad/empty2010.dxf";
const char* const kNew2007 = "jscad/empty2007.dxf";
const char* const kLibre = "libre/example_2013.dxf";
const char* const kAutoCad2013[] = {kAscii, kNew, kLibre, "jscad/3Ddraw.dxf", "jscad/3Dlines.dxf",
                                    "jscad/3Dmesh01.dxf", "oxcart/cryo_clamp_upper.dxf",
                                    "oxcart/CY7_diode_clamp.dxf", "oxcart/cryo_interface_v3.dxf",
                                    "oxcart/cryo_interface_v2_weld.dxf"};

// A new drawing with a multileader (so it has AutoCAD's MLEADERSTYLE Standard)
std::vector<Record> new_drawing(hdxf::DxfVersion version, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    hdxf::Drawing dwg(version);
    dwg.add_line({0, 0, 0}, {10, 5, 0});
    dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
    return records_of(save(dwg, format));
}

// The record (by type) of a section `name` from its SECTION record to its ENDSEC
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

} // namespace

// A new 2013 drawing (B57, B59): AutoCAD's 253 header variables in AutoCAD's order with
// $REQUIREDVERSIONS after $LASTSAVEDBY; MATERIAL and VISUALSTYLE record for record as AutoCAD
// 2017's new drawing saved as 2013 (jscad's empty2013); and every other record HDxf's new
// drawing has differs from AutoCAD's new 2013 drawing exactly as the same drawing as 2010
// differs from AutoCAD's new 2010 drawing (empty2010: the same drawing of AutoCAD's) — what
// differs between the versions is AutoCAD's. Not compared: the records HDxf's new drawing has
// not in 2010 either (AutoCAD's title block, scales, table and view styles, ACDB_RECOMPOSE_DATA
// and the dictionaries of the layers and of AcDsDecomposeData — 2010's only) and the
// Thumbnail_Data record of ACDSDATA (HDxf draws no preview).
TEST(write_2013_new_drawing) {
    std::vector<Record> recs = new_drawing(hdxf::DxfVersion::R2013);
    std::vector<uint8_t> raw;
    {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
        raw = save(dwg);
    }
    dxf_structure::check_structure(raw, "new 2013 drawing");

    std::vector<std::string> names = header_names(recs);
    hdxf_test::assert_eq(static_cast<int>(names.size()), 253, "header variables");
    for (const char* file : {kAscii, kNew}) {
        if (!has_files({file})) continue;
        hdxf_test::assert_true(names == header_names(records_of(read_bytes(external(file)))),
                               std::string(file) + ": AutoCAD's variables in AutoCAD's order");
    }
    auto at = std::find(names.begin(), names.end(), "$REQUIREDVERSIONS");
    hdxf_test::assert_true(at != names.begin() && at != names.end() && *(at - 1) == "$LASTSAVEDBY" &&
                           *(at + 1) == "$INSBASE", "$REQUIREDVERSIONS after $LASTSAVEDBY");
    hdxf_test::assert_true(header_group(recs, "$REQUIREDVERSIONS") == std::pair<int, std::string>(160, "0"),
                           "$REQUIREDVERSIONS 160 0");
    hdxf_test::assert_eq(header_group(recs, "$ACADVER").second, "AC1027");
    hdxf_test::assert_eq(header_group(recs, "$ACADMAINTVER").second, "125");

    for (const auto& r : recs) {
        const std::string& t = r[0].second;
        if (t == "VISUALSTYLE" || t == "MLEADERSTYLE") {
            hdxf_test::assert_eq(value(r, 360), "", t + ": no extension dictionary in 2013");
        }
        if (t == "XRECORD") {
            for (const auto& [code, v] : r) {
                hdxf_test::assert_true(code != 102 || (v.find("ACAD_ROUNDTRIP") == std::string::npos &&
                                                       v.find("RTVS") == std::string::npos), "no round-trip XRECORD");
            }
        }
        if (t == "LAYER") {
            Record body = groups_in(r, "AcDbLayerTableRecord");
            hdxf_test::assert_true(body.size() >= 2 && body.back() == Record::value_type{348, "0"} &&
                                   body[body.size() - 2].first == 347, "LAYER 348 0 after 347");
        }
        if (t == "MLEADERSTYLE") {
            Record body = groups_in(r, "AcDbMLeaderStyle");
            hdxf_test::assert_true(body.back() == Record::value_type{298, "1"}, "298 1 last");
            hdxf_test::assert_eq(value(body, 295), "1", "295 1");
        }
        if (t == "DICTIONARY") {
            for (const auto& g : r) hdxf_test::assert_true(g.second != "AcDsDecomposeData", "no AcDsDecomposeData");
        }
    }

    if (!has_files({kNew, kNew2010})) return;
    std::vector<Record> autocad = records_of(read_bytes(external(kNew)));
    same_visual_styles(recs, kNew);

    // The VPORT table's XDATA as AutoCAD's
    auto table = [](const std::vector<Record>& file, const std::string& name) {
        for (const auto& r : file) if (r[0].second == "TABLE" && value(r, 2) == name) return without_handles(r);
        return Record();
    };
    hdxf_test::assert_true(table(recs, "VPORT") == table(autocad, "VPORT"), "VPORT table");
    Record vport_table = table(recs, "VPORT");
    hdxf_test::assert_true(Record(vport_table.end() - 3, vport_table.end()) ==
                           Record({{1001, "ACAD"}, {1000, "DbSaveVer"}, {1071, "26"}}), "DbSaveVer 26");

    // MLEADERSTYLE Standard: AutoCAD's groups (its values are AutoCAD's imperial drawing's)
    Record ours_style = groups_in(named_records(recs)["MLEADERSTYLE/"], "AcDbMLeaderStyle");
    Record autocad_style = groups_in(named_records(autocad)["MLEADERSTYLE/"], "AcDbMLeaderStyle");
    hdxf_test::assert_true(codes_of(ours_style) == codes_of(autocad_style), "MLEADERSTYLE: AutoCAD's codes");
    for (int code : {179, 170, 171, 172, 90, 173, 290, 291, 174, 178, 175, 176, 292, 297, 293, 294, 177, 295,
                     296, 271, 272, 273, 298}) {
        hdxf_test::assert_eq(value(ours_style, code), value(autocad_style, code), "MLEADERSTYLE " + std::to_string(code));
    }

    // ACDSDATA: AutoCAD's schemas, group for group, and no record
    std::vector<Record> ours_acds = section(recs, "ACDSDATA");
    std::vector<Record> autocad_acds = section(autocad, "ACDSDATA");
    std::vector<Record> autocad_schemas;
    for (const auto& r : autocad_acds) if (r[0].second != "ACDSRECORD") autocad_schemas.push_back(r);
    hdxf_test::assert_eq(static_cast<int>(autocad_schemas.size()), 7, "AutoCAD's: SECTION, five schemas, ENDSEC");
    hdxf_test::assert_true(ours_acds == autocad_schemas, "ACDSDATA's schemas as AutoCAD's");
    hdxf_test::assert_eq(recs.back()[0].second, "EOF", "EOF last");
    hdxf_test::assert_true(recs[recs.size() - 2][0].second == "ENDSEC" && ours_acds.size() == 7, "ACDSDATA last");

    // Every named record differs from AutoCAD's as in 2010
    std::map<std::string, Record> ours13 = named_records(recs), autocad13 = named_records(autocad);
    std::map<std::string, Record> ours10 = named_records(new_drawing(hdxf::DxfVersion::R2010));
    std::map<std::string, Record> autocad10 = named_records(records_of(read_bytes(external(kNew2010))));
    int compared = 0;
    for (const auto& [key, ours] : ours13) {
        if (!autocad13.count(key)) continue;
        hdxf_test::assert_true(ours10.count(key) && autocad10.count(key), key + ": in both 2010 drawings");
        if (!ours10.count(key) || !autocad10.count(key)) continue;
        ++compared;
        // Our groups exactly; AutoCAD's by their codes (its view of the drawing differs a little
        // between its two saves: *Active's center and aspect)
        auto d13 = difference(ours, autocad13[key]);
        auto d10 = difference(ours10[key], autocad10[key]);
        hdxf_test::assert_true(d13.first == d10.first && codes_of(d13.second) == codes_of(d10.second),
                               key + ": differs from AutoCAD's 2013 as from its 2010");
    }
    hdxf_test::assert_true(compared >= 50, "records compared: " + std::to_string(compared));

    // The same types missing or extra as in 2010, but the objects only 2010 has (round-trip
    // XRECORDs with their dictionaries, AcDsDecomposeData) and ACDSDATA's
    auto counts = [](const std::vector<Record>& file) {
        std::map<std::string, int> out;
        for (const auto& r : file) ++out[r[0].second];
        return out;
    };
    std::map<std::string, int> c13 = counts(recs), a13 = counts(autocad);
    std::map<std::string, int> c10 = counts(new_drawing(hdxf::DxfVersion::R2010));
    std::map<std::string, int> a10 = counts(records_of(read_bytes(external(kNew2010))));
    std::set<std::string> types;
    for (const auto* m : {&c13, &a13, &c10, &a10}) for (const auto& [t, n] : *m) types.insert(t);
    for (const auto& t : types) {
        if (t == "DICTIONARY" || t == "XRECORD" || t == "ACDSSCHEMA" || t == "ACDSRECORD") continue;
        hdxf_test::assert_eq(a13[t] - c13[t], a10[t] - c10[t], t + ": the records AutoCAD's has and HDxf's not");
    }

    // Read back as 2013; binary the same records; a second save the same
    hdxf::Drawing back = load(raw);
    hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R2013, "version read back");
    hdxf_test::assert_true(records_of(save(back)) == records_of(raw), "read back and written: unchanged");
    std::vector<Record> bin = new_drawing(hdxf::DxfVersion::R2013, hdxf::DxfFormat::Binary);
    hdxf_test::assert_eq(static_cast<int>(bin.size()), static_cast<int>(recs.size()), "binary");
    hdxf_test::assert_true(section(bin, "ACDSDATA") == ours_acds, "binary ACDSDATA");
}

// A drawing built in memory set to 2013 and back gets its visual styles and its multileader
// style in the version's form (B59, B60): no extension dictionaries and round-trip XRECORDs in
// 2013, AutoCAD's in 2007/2010
TEST(write_2013_visual_styles_follow_the_version) {
    if (!has_files({kNew, kNew2010, kNew2007})) return;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "x");
    auto mleader_style_xrecord = [](const std::vector<Record>& file) {
        for (const auto& r : file) {
            if (r[0].second == "XRECORD" &&
                std::find(r.begin(), r.end(), Record::value_type{102, "ACAD_ROUNDTRIP_2012_MLEADER_STYLE"}) != r.end()) {
                return value(r, 298);
            }
        }
        return std::string();
    };
    auto dictionaries = [](const std::vector<Record>& file) {
        int n = 0;
        for (const auto& r : file) n += r[0].second == "DICTIONARY";
        return n;
    };
    std::vector<Record> r10 = records_of(save(dwg));
    same_visual_styles(r10, kNew2010);
    hdxf_test::assert_eq(mleader_style_xrecord(r10), "1", "2010: the style's XRECORD");
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2013,
                               hdxf::DxfVersion::R2010}) {
        dwg.set_version(v);
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "visual styles");
        std::vector<Record> recs = records_of(out);
        bool r2013 = v == hdxf::DxfVersion::R2013;
        same_visual_styles(recs, r2013 ? kNew : v == hdxf::DxfVersion::R2010 ? kNew2010 : kNew2007);
        hdxf_test::assert_eq(mleader_style_xrecord(recs), r2013 ? "" : "1", "the style's XRECORD");
        // The visual styles' 26, the style's and the multileader's (text_extended, B60)
        if (r2013) hdxf_test::assert_eq(dictionaries(recs), dictionaries(r10) - 28, "26 + 2 dictionaries gone");
        else hdxf_test::assert_eq(dictionaries(recs), dictionaries(r10), "the dictionaries back");
        for (const auto& r : recs) {
            if (r[0].second != "MLEADERSTYLE") continue;
            Record body = groups_in(r, "AcDbMLeaderStyle");
            hdxf_test::assert_eq(value(body, 295), r2013 ? "1" : "0", "295: 0 before 2013 (B60)");
            hdxf_test::assert_eq(value(body, 298), r2013 ? "1" : "", "298 only in 2013");
        }
    }

    // Made as 2013 first, then 2010: the round-trip records are made
    hdxf::Drawing d13(hdxf::DxfVersion::R2013);
    d13.add_circle({0, 0, 0}, 1.0);
    same_visual_styles(records_of(save(d13)), kNew);
    d13.set_version(hdxf::DxfVersion::R2010);
    same_visual_styles(records_of(save(d13)), kNew2010);
    d13.set_version(hdxf::DxfVersion::R2007);
    same_visual_styles(records_of(save(d13)), kNew2007);

    // A style changed by the program keeps its groups and its round-trip record
    hdxf::Object* changed = nullptr;
    for (auto& o : d13.objects()) {
        if (o.type == hdxf::ObjectType::VisualStyle && !changed) changed = &o;
    }
    hdxf_test::assert_true(changed != nullptr, "a visual style");
    changed->as_visual_style().style_type = 9;
    const hdxf::VisualStyleData kept = changed->as_visual_style();
    std::string handle = changed->handle;
    d13.set_version(hdxf::DxfVersion::R2013);
    save(d13);
    for (const auto& o : d13.objects()) {
        if (o.handle == handle) {
            hdxf_test::assert_true(o.as_visual_style().form == kept.form && o.as_visual_style().style_type == 9 &&
                                       o.as_visual_style().properties.size() == kept.properties.size(),
                                   "kept");
            hdxf_test::assert_false(o.xdictionary.empty(), "its round-trip record kept");
        }
    }
}

// Read and written in the same format: every record as in the file, group for group
TEST(roundtrip_2013_files_record_for_record) {
    struct File { std::filesystem::path path; hdxf::DxfFormat format; };
    std::vector<File> files;
    for (const char* name : kAutoCad2013) {
        if (std::filesystem::exists(external(name))) files.push_back({external(name), hdxf::DxfFormat::Ascii});
    }
    if (std::filesystem::exists(external(kBinary))) files.push_back({external(kBinary), hdxf::DxfFormat::Binary});
    if (files.empty()) hdxf_test::skip("no 2013 files");
    for (const auto& f : files) {
        std::string what = hdxf::utf8_from_path(f.path.filename());
        std::vector<uint8_t> in = read_bytes(f.path);
        hdxf::Drawing dwg = load(in);
        hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R2013, what + ": 2013");
        std::vector<Record> before = records_of(in);
        std::vector<uint8_t> out = save(dwg, f.format);
        std::vector<Record> after = records_of(out);
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()), what + ": records");
        for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
            hdxf_test::assert_true(before[i] == after[i], what + ": record " + std::to_string(i) +
                                   " (" + before[i][0].second + ") differs");
        }
        // ASCII files byte for byte, line endings included
        if (f.format == hdxf::DxfFormat::Ascii) hdxf_test::assert_true(out == in, what + ": bytes");
    }
}

// AutoCAD's 2013 files of ACadSharp come back byte for byte
TEST(roundtrip_2013_bytes) {
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
// significant digits) — ACDSDATA's groups too
TEST(roundtrip_2013_ascii_binary_ascii) {
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
}

// Changes to AutoCAD's 2013 file: written anew where they are, everything else as it was —
// the ACDSDATA section with the bodies' SAB too (B61 types it in step 5.4)
TEST(roundtrip_2013_changes) {
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
                           "the entities of the 2010 test are in the 2013 file too");
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
    dxf_structure::check_structure(out, "changed AutoCAD 2013 file");
    std::vector<Record> after = records_of(out);

    Record line = record(after, "2C7");
    hdxf_test::assert_eq(std::strtod(value(line, 11).c_str(), nullptr), 50.0, "moved");
    hdxf_test::assert_eq(value(record(after, "E00"), 347), "", "the circle's material removed");
    Record dim = groups_in(record(after, "514"), "AcDbDimension");
    hdxf_test::assert_true(!dim.empty() && dim[0] == Record::value_type{280, "0"}, "DIMENSION 280 first");
    hdxf_test::assert_eq(value(record(after, "514"), 1), "<> mm", "the dimension's text");
    hdxf_test::assert_eq(value(record(after, "8C6"), 283), "20", "fade");
    hdxf_test::assert_eq(value(record(after, "8C6"), 160), value(record(before, "8C6"), 160), "its proxy graphics");
    for (const char* h : {"69D", "3BA"}) hdxf_test::assert_true(record(after, h).empty(), std::string(h) + " still there");
    hdxf_test::assert_true(section(after, "ACDSDATA") == section(before, "ACDSDATA"), "ACDSDATA as read");
    hdxf_test::assert_eq(header_group(after, "$REQUIREDVERSIONS").second, "0", "$REQUIREDVERSIONS");

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
    hdxf_test::assert_true(same >= 840, "unchanged records: " + std::to_string(same));   // of 869
}

// The fields 2013 adds, read from AutoCAD's files (B58): LAYER 348 (0 on every layer),
// MLEADERSTYLE 298 (0 in ACadSharp's, 1 in AutoCAD 2017's new drawing and LibreDWG's) and 295,
// and the bodies' change GUIDs (290/2: ACadSharp's three, LibreDWG's REGION without one)
TEST(read_2013_fields) {
    for (const char* name : {kAscii, kBinary}) {
        if (!has_files({name})) return;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        hdxf_test::assert_true(dwg.layers().size() > 10, "layers");
        for (const auto& l : dwg.layers()) hdxf_test::assert_eq(l.pointer_348, "0", std::string(name) + ": 348 of " + l.name);
        int styles = 0;
        for (const auto& o : dwg.objects()) {
            if (o.type != hdxf::ObjectType::MLeaderStyle) continue;
            ++styles;
            hdxf_test::assert_false(o.as_mleader_style().text_extended, "298 0");
            hdxf_test::assert_false(o.as_mleader_style().overwrite_property, "295 0");
        }
        hdxf_test::assert_true(styles > 0, "multileader styles");
        std::map<std::string, std::string> guids;
        for (const auto& e : dwg.entities()) {
            if (e.type == hdxf::EntityType::Solid3D || e.type == hdxf::EntityType::Region) {
                guids[e.props.handle] = e.as_acis().guid;
                hdxf_test::assert_true(e.as_acis().lines.empty(), "no SAT in 2013");
            }
        }
        // GAS_LINE's text upright: 74 10 (text 2 + upright 8)
        bool gas = false;
        for (const auto& lt : dwg.linetypes()) {
            if (lt.name != "GAS_LINE") continue;
            gas = true;
            std::vector<int> types;
            for (const auto& el : lt.elements) types.push_back(el.type);
            types.resize(lt.pattern.size());
            hdxf_test::assert_true(types == std::vector<int>({0, 10, 0}), "GAS_LINE 74: 0, 10, 0");
        }
        hdxf_test::assert_true(gas, "GAS_LINE");
        hdxf_test::assert_eq(guids["D65"], "{1a113328-eb6d-d44d-824d-78b33668f9e7}", "D65's GUID");
        hdxf_test::assert_eq(static_cast<int>(guids.size()), 3, "three bodies");
        for (const auto& [h, g] : guids) hdxf_test::assert_eq(static_cast<int>(g.size()), 38, h + ": a GUID");
    }
    if (has_files({kNew})) {
        hdxf::Drawing dwg = load(read_bytes(external(kNew)));
        int styles = 0;
        for (const auto& o : dwg.objects()) {
            if (o.type != hdxf::ObjectType::MLeaderStyle) continue;
            ++styles;
            hdxf_test::assert_true(o.as_mleader_style().text_extended, "empty2013: 298 1");
            hdxf_test::assert_true(o.as_mleader_style().overwrite_property, "empty2013: 295 1");
        }
        hdxf_test::assert_eq(styles, 1, "Standard");
    }
    if (has_files({kLibre})) {
        hdxf::Drawing dwg = load(read_bytes(external(kLibre)));
        int bodies = 0, with_guid = 0;
        auto entities = [&](const std::vector<hdxf::Entity>& list) {
            for (const auto& e : list) {
                if (e.type != hdxf::EntityType::Solid3D && e.type != hdxf::EntityType::Region &&
                    e.type != hdxf::EntityType::Body) continue;
                ++bodies;
                if (!e.as_acis().guid.empty()) ++with_guid;
                if (e.props.handle == "176") hdxf_test::assert_eq(e.as_acis().guid, "", "176: 290 0 and the GUID of zeros");
            }
        };
        entities(dwg.entities());
        for (const auto& b : dwg.blocks()) entities(b.entities);
        hdxf_test::assert_eq(bodies, 3, "LibreDWG: three bodies");
        hdxf_test::assert_eq(with_guid, 2, "two with a GUID");
        for (const auto& o : dwg.objects()) {
            if (o.type == hdxf::ObjectType::MLeaderStyle) hdxf_test::assert_true(o.as_mleader_style().text_extended, "LibreDWG: 298 1");
        }
    }
}

// The 2013 fields written (B58): a layer's 348 as given, a multileader style's 298 and 295,
// a body's GUID (290 1 and the GUID; 290 0 and zeros without one) — and read back
TEST(write_2013_fields) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    dwg.add_layer("A").pointer_348 = "0";
    hdxf::Linetype& gas = dwg.add_linetype("GAS_LINE", "Gas line", {0.5, -0.2, -0.25});
    hdxf::LinetypeElement text;
    text.type = 10;                     // text, upright
    text.style = "Standard";
    text.scale = 0.1;
    text.offset = {-0.1, -0.05};
    text.text = "GAS";
    gas.elements = {hdxf::LinetypeElement{}, text, hdxf::LinetypeElement{}};
    hdxf::Object& style = dwg.add_mleader_style("Plain");
    style.as_mleader_style().text_extended = false;
    style.as_mleader_style().overwrite_property = false;
    hdxf::AcisData with;
    with.guid = "{1A113328-EB6D-D44D-824D-78B33668F9E7}";
    std::string h_with = dwg.add_acis(hdxf::EntityType::Solid3D, with).props.handle;
    dwg.add_acis(hdxf::EntityType::Region, hdxf::AcisData{});
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "2013 fields");
    int bodies = 0;
    for (const auto& r : records_of(out)) {
        const std::string& t = r[0].second;
        if (t == "3DSOLID" || t == "REGION") {
            ++bodies;
            Record body = groups_in(r, "AcDbModelerGeometry");
            bool guid = t == "3DSOLID";
            hdxf_test::assert_true(body == Record({{290, guid ? "1" : "0"},
                                                   {2, guid ? "{1A113328-EB6D-D44D-824D-78B33668F9E7}"
                                                            : "{00000000-0000-0000-0000-000000000000}"}}),
                                   t + ": 290 and 2 only");
            if (guid) hdxf_test::assert_true(groups_in(r, "AcDb3dSolid") == Record({{350, "0"}}), "AcDb3dSolid 350 0");
        }
        if (t == "LTYPE" && value(r, 2) == "GAS_LINE") {
            Record body = groups_in(r, "AcDbLinetypeTableRecord");
            std::vector<std::string> types;
            for (const auto& [code, v] : body) if (code == 74) types.push_back(v);
            hdxf_test::assert_true(types == std::vector<std::string>({"0", "10", "0"}), "74 10: upright as it is");
        }
        if (t == "MLEADERSTYLE") {
            Record body = groups_in(r, "AcDbMLeaderStyle");
            bool plain = value(r, 5) == style.handle;
            hdxf_test::assert_true(body.back() == Record::value_type{298, plain ? "0" : "1"}, "298");
            hdxf_test::assert_eq(value(body, 295), plain ? "0" : "1", "295");
        }
    }
    hdxf_test::assert_eq(bodies, 2, "bodies");
    hdxf::Drawing back = load(out);
    for (const auto& e : back.entities()) {
        if (e.type == hdxf::EntityType::Solid3D) hdxf_test::assert_eq(e.as_acis().guid, with.guid, "GUID read back");
        if (e.type == hdxf::EntityType::Region) hdxf_test::assert_eq(e.as_acis().guid, "", "no GUID read back");
    }
    for (const auto& o : back.objects()) {
        if (o.type == hdxf::ObjectType::MLeaderStyle) {
            hdxf_test::assert_eq(o.as_mleader_style().text_extended, o.handle != style.handle, "298 read back");
        }
    }
    (void)h_with;
    for (const auto& lt : back.linetypes()) {
        if (lt.name == "GAS_LINE") hdxf_test::assert_eq(lt.elements.at(1).type, 10, "upright read back");
    }

    // A body of AutoCAD's 2013 file written anew keeps its 290/2 and its SAB in ACDSDATA
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    hdxf::Drawing file = load(in);
    for (auto& e : file.entities()) {
        if (e.props.handle == "D65") e.raw = hdxf::RawRecord();
    }
    std::vector<Record> before = records_of(in), after = records_of(save(file));
    hdxf_test::assert_true(record(after, "D65") == record(before, "D65"), "D65 written anew as AutoCAD's");
    hdxf_test::assert_true(section(after, "ACDSDATA") == section(before, "ACDSDATA"), "its SAB");
}

// A body whose SAT the encoder cannot make SAB (src/acis_sab.cpp, B62/B63): an error naming the
// body, before anything is changed — data that is not SAT, a version of steps 5.6/5.7 ("yet")
TEST(write_2013_sat_refused) {
    hdxf::AcisData sat;
    sat.version = 1;
    sat.lines = {"21200 12 2 0"};
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    dwg.add_acis(hdxf::EntityType::Region, sat);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "REGION");
    hdxf_test::assert_eq(dwg.last_error(), "REGION: the ACIS data cannot be read as SAT");
    hdxf_test::assert_true(dwg.entities()[0].props.handle.empty(), "no handles given");
    dwg.set_version(hdxf::DxfVersion::R2010);
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());

    // AutoCAD 2000's form (SAT 400; step 5.7): written as SAB; a record without its type's fields
    // is not
    auto enciphered = [](std::vector<std::string> lines) {
        for (auto& line : lines) {
            for (char& c : line) {
                unsigned char u = static_cast<unsigned char>(c);
                if (u > 32 && u < 159) c = static_cast<char>(159 - u);
            }
        }
        return lines;
    };
    hdxf::AcisData old;
    old.lines = enciphered({"400 0 1 0", "16 Autodesk AutoCAD 12 ACIS 4.00 NT 0", "1 9.9999999999999995e-07 1e-10",
                            "body $-1 $-1 $-1 $-1 #"});
    hdxf::AcisData broken = old;
    broken.lines.back() = enciphered({"body $-1 $-1 #"})[0];
    for (const hdxf::AcisData* data : {&old, &broken}) {
        bool good = data == &old;
        hdxf::Drawing surfaces(hdxf::DxfVersion::R2013);
        hdxf::SurfaceData s;
        s.acis = *data;
        surfaces.add_surface(hdxf::EntityType::PlaneSurface, s);
        hdxf_test::assert_eq(surfaces.save_to_memory().empty(), !good, "PLANESURFACE");
        hdxf_test::assert_eq(surfaces.last_error(),
                             good ? std::string()
                                  : std::string("PLANESURFACE: the ACIS record 0 (\"body\") does not have the fields of its type in "
                                                "version 400"));

        hdxf::Drawing block(hdxf::DxfVersion::R2013);
        hdxf::Entity solid;
        solid.type = hdxf::EntityType::Solid3D;
        solid.data = *data;
        block.add_block("B").entities.push_back(solid);
        hdxf_test::assert_eq(block.save_to_memory().empty(), !good, "3DSOLID in a block");
        hdxf_test::assert_eq(block.last_error(),
                             good ? std::string()
                                  : std::string("3DSOLID: the ACIS record 0 (\"body\") does not have the fields of its type in "
                                                "version 400"));
    }
}

// A drawing read from 2010 is not written as 2013 (that is conversion, phase 8), nor one read
// from 2013 as 2010; one read from R12 is generated as 2013 (B56)
TEST(write_2013_not_from_another_version) {
    hdxf::Drawing r12 = r12_drawing_read_back();
    r12.set_version(hdxf::DxfVersion::R2013);
    std::vector<uint8_t> generated = save(r12);
    dxf_structure::check_structure(generated, "R12 as 2013");
    hdxf_test::assert_eq(header_group(records_of(generated), "$ACADVER").second, "AC1027", "R12 as 2013");

    for (const char* name : {"acadsharp/sample_AC1024_ascii.dxf", kAscii}) {
        if (!has_files({name})) return;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        bool from_2013 = dwg.version() == hdxf::DxfVersion::R2013;
        dwg.set_version(from_2013 ? hdxf::DxfVersion::R2010 : hdxf::DxfVersion::R2013);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), name);
        hdxf_test::assert_eq(dwg.last_error(), std::string("Writing DXF ") + (from_2013 ? "2013 as 2010" : "2010 as 2013") +
                                                   " is not supported yet (the drawing was read from that version)");
    }
}

namespace {

// Every record HDxf types written anew (its record as read dropped): entities with their
// attributes, vertices and SEQENDs, table entries, blocks and their records, objects
void drop_records(hdxf::Drawing& dwg) {
    auto entities = [](std::vector<hdxf::Entity>& list) {
        for (auto& e : list) {
            if (e.type == hdxf::EntityType::Unknown) continue;
            e.raw = hdxf::RawRecord();
            if (e.type == hdxf::EntityType::Insert) {
                for (auto& a : e.as_insert().attributes) a.raw = hdxf::RawRecord();
                e.as_insert().seqend_raw = hdxf::RawRecord();
            }
            if (e.type == hdxf::EntityType::Polyline) {
                for (auto& v : e.as_polyline().vertices) v.raw = hdxf::RawRecord();
                e.as_polyline().seqend_raw = hdxf::RawRecord();
            }
        }
    };
    entities(dwg.entities());
    for (auto& b : dwg.blocks()) {
        entities(b.entities);
        b.raw = hdxf::RawRecord();
        b.endblk_raw = hdxf::RawRecord();
        b.record.raw = hdxf::RawRecord();
    }
    for (auto& x : dwg.layers()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.linetypes()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.text_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.dim_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.views()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.ucs_list()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.vports()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.app_ids()) x.raw = hdxf::RawRecord();
    for (auto& o : dwg.objects()) {
        if (o.type != hdxf::ObjectType::Unknown) o.raw = hdxf::RawRecord();
    }
}

// A record without its proxy graphics (92/160 and the 310 in AcDbEntity)
Record without_proxy(const Record& r) {
    Record out;
    std::string subclass;
    for (const auto& g : r) {
        if (g.first == 100) subclass = g.second;
        if (subclass == "AcDbEntity" && (g.first == 92 || g.first == 160 || g.first == 310)) continue;
        out.push_back(g);
    }
    return out;
}

// What written anew changes in each record of AutoCAD's file, by handle: the groups AutoCAD's
// record has and ours has not, and the other way
std::map<std::string, std::pair<Record, Record>> changes_written_anew(const std::string& name, hdxf::DxfFormat format) {
    std::vector<uint8_t> in = read_bytes(external(name));
    hdxf::Drawing dwg = load(in);
    drop_records(dwg);
    std::vector<Record> before = records_of(in), after = records_of(save(dwg, format));
    std::map<std::string, std::pair<Record, Record>> out;
    for (const auto& r : before) {
        std::string handle = value(r, r[0].second == "DIMSTYLE" ? 105 : 5);
        if (handle.empty()) continue;
        if (r[0].second == "SECTION") continue;  // the header: $LASTSAVEDBY, $FINGERPRINTGUID
        Record theirs = r, ours = record(after, handle);
        if (r[0].second == "MULTILEADER") {
            theirs = without_proxy(theirs);
            ours = without_proxy(ours);
        }
        if (ours != theirs) out[r[0].second + " " + handle] = difference(theirs, ours);
    }
    return out;
}

std::string show(const Record& r) {
    std::string out;
    for (const auto& [code, v] : r) {
        if (out.size() > 300) return out + " …";
        out += " " + std::to_string(code) + " " + v.substr(0, 40);
    }
    return out;
}

} // namespace

// Every type HDxf writes in 2010 is written in 2013 as AutoCAD writes it there (plan 5.8): the
// same drawings of AutoCAD's saved as 2010 and as 2013 (ACadSharp's ASCII and binary, LibreDWG's
// example, AutoCAD 2017's new drawing), every typed record written anew — what that changes in a
// record of the 2013 file is what it changes in the same record of the 2010 file (AutoCAD's 16
// digits, what HDxf does not type), and nothing more. The files have every entity and object type
// HDxf writes but the surfaces, HELIX and GEODATA (test_surface.cpp, test_geodata.cpp): 3DFACE …
// XLINE, ATTRIB/ATTDEF, DIMENSION, IMAGE, HATCH, MESH, MLINE, LIGHT, WIPEOUT, VIEWPORT, MULTILEADER,
// 3DSOLID/REGION, the tables, LAYOUT, MLEADERSTYLE, SUN, SCALE, DBCOLOR, IMAGEDEF … The multileaders'
// proxy graphics are left out: AutoCAD does not clear the padding after their strings, which
// differs between the two saves (plan 5.3; their commands are compared in test_mleader_graphics.cpp)
TEST(write_2013_every_type_anew) {
    const std::pair<const char*, const char*> pairs[] = {
        {"acadsharp/sample_AC1024_ascii.dxf", kAscii}, {"acadsharp/sample_AC1024_binary.dxf", kBinary},
        {"libre/example_2010.dxf", kLibre}, {kNew2010, kNew}};
    for (const auto& [old_name, new_name] : pairs) {
        if (!has_files({old_name, new_name})) continue;
        hdxf::DxfFormat format = std::string(new_name).find("binary") != std::string::npos ? hdxf::DxfFormat::Binary
                                                                                             : hdxf::DxfFormat::Ascii;
        auto old_changes = changes_written_anew(old_name, format);
        auto new_changes = changes_written_anew(new_name, format);
        hdxf_test::assert_eq(static_cast<int>(new_changes.size()), static_cast<int>(old_changes.size()),
                             std::string(new_name) + ": records changed");
        for (const auto& [key, diff] : new_changes) {
            auto it = old_changes.find(key);
            bool same = it != old_changes.end() && it->second == diff;
            // A record under another handle in the 2010 file (ACadSharp's anonymous blocks):
            // the same change to a record of its type
            std::string type = key.substr(0, key.find(' '));
            for (const auto& [old_key, old_diff] : old_changes) {
                same = same || (old_key.substr(0, old_key.find(' ')) == type && old_diff == diff);
            }
            hdxf_test::assert_true(same, std::string(new_name) + " " + key + ": AutoCAD's" + show(diff.first) +
                                             ", HDxf's" + show(diff.second));
        }
    }
}
