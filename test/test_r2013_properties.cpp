#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// 2013's properties in 2000 … 2010 (phase 5, step 5.3 — plan B60): a linetype's upright elements
// (74 bit 8), a MULTILEADER's 295 and a MLEADERSTYLE's 298 as AutoCAD's round-trip XRECORDs in
// the record's extension dictionary, a body's change GUID as XDATA ACAD_OBJECT_CHANGE_GUID — read
// from AutoCAD's 2000 … 2010 files into the values its 2013 files have, written as AutoCAD writes
// them, made and removed after the version; and what 2013 has that cannot be written before it.
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

bool has_files(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (!std::filesystem::exists(external(n))) {
            hdxf_test::skip("external test file missing: " + n);
            return false;
        }
    }
    return true;
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
        if (r[0].second == "SECTION") continue;         // HEADER: $HANDSEED
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

// The groups after the last of `code` (a MULTILEADER's 295 after its 273: 2013's)
Record after_last(const Record& r, int code) {
    Record out;
    for (const auto& g : r) {
        if (g.first == code) out.clear();
        else out.push_back(g);
    }
    return out;
}

int count_of(const Record& r, int code) {
    int n = 0;
    for (const auto& g : r) n += g.first == code;
    return n;
}

// The groups from the first subclass on, handles as "H" — for comparing with AutoCAD's records
Record body_of(const Record& r) {
    static const std::set<int> handle_codes = {330, 340, 350, 360};
    Record out;
    bool body = false;
    for (const auto& [code, v] : r) {
        if (code == 100) body = true;
        if (body) out.push_back({code, handle_codes.count(code) ? "H" : v});
    }
    return out;
}

// The head groups of a record: 102 blocks and the owner, up to the first subclass
Record head_of(const Record& r) {
    Record out;
    for (size_t i = 2; i < r.size() && r[i].first != 100; ++i) out.push_back(r[i]);
    return out;
}

// The XDATA of an application in a record (from its 1001 to the next)
Record xdata_of(const Record& r, const std::string& app) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 1001) in = g.second == app;
        else if (in) out.push_back(g);
    }
    return out;
}

const hdxf::Entity* entity(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) if (e.props.handle == handle) return &e;
    }
    return nullptr;
}

const hdxf::Linetype* linetype(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& lt : dwg.linetypes()) if (lt.name == name) return &lt;
    return nullptr;
}

std::vector<int> element_types(const hdxf::Linetype& lt) {
    std::vector<int> types;
    for (const auto& el : lt.elements) types.push_back(el.type);
    types.resize(lt.pattern.size());
    return types;
}

const hdxf::AcisData* acis(const hdxf::Entity& e) {
    switch (e.type) {
        case hdxf::EntityType::Solid3D:
        case hdxf::EntityType::Region:
        case hdxf::EntityType::Body:
            return &e.as_acis();
        case hdxf::EntityType::RevolvedSurface:
        case hdxf::EntityType::ExtrudedSurface:
        case hdxf::EntityType::LoftedSurface:
        case hdxf::EntityType::SweptSurface:
        case hdxf::EntityType::PlaneSurface:
        case hdxf::EntityType::Surface:
            return &e.as_surface().acis;
        default:
            return nullptr;
    }
}

// Each body's change GUID by handle
std::map<std::string, std::string> guids_of(const hdxf::Drawing& dwg) {
    std::map<std::string, std::string> out;
    auto add = [&](const std::vector<hdxf::Entity>& list) {
        for (const auto& e : list) {
            if (const hdxf::AcisData* a = acis(e)) {
                out[e.props.handle] = a->guid;
                for (const auto& x : e.props.xdata) {
                    hdxf_test::assert_true(x.app_name != "ACAD_OBJECT_CHANGE_GUID", e.props.handle + ": GUID not in xdata");
                }
            }
        }
    };
    add(dwg.entities());
    for (const auto& b : dwg.blocks()) add(b.entities);
    return out;
}

std::vector<int> mleader_styles_extended(const hdxf::Drawing& dwg) {
    std::vector<int> out;
    for (const auto& o : dwg.objects()) {
        if (o.type == hdxf::ObjectType::MLeaderStyle) out.push_back(o.as_mleader_style().text_extended ? 1 : 0);
    }
    return out;
}

// The records of a type
std::vector<Record> of_type(const std::vector<Record>& file, const std::string& type) {
    std::vector<Record> out;
    for (const auto& r : file) if (r[0].second == type) out.push_back(r);
    return out;
}

// The round-trip XRECORD of a record (through its extension dictionary), empty for none
Record roundtrip_of(const std::vector<Record>& file, const Record& r) {
    std::string xdict;
    for (size_t i = 0; i + 1 < r.size() && r[i].first != 100; ++i) {
        if (r[i].first == 102 && r[i].second == "{ACAD_XDICTIONARY") xdict = r[i + 1].second;
    }
    if (xdict.empty()) return {};
    Record dict = record(file, xdict);
    for (size_t i = 0; i + 1 < dict.size(); ++i) {
        if (dict[i].first == 3 && dict[i].second == "ACAD_XREC_ROUNDTRIP") return record(file, dict[i + 1].second);
    }
    return {};
}

int count_type(const std::vector<Record>& file, const std::string& type) {
    return static_cast<int>(of_type(file, type).size());
}

bool has_appid(const std::vector<Record>& file, const std::string& name) {
    for (const auto& r : of_type(file, "APPID")) if (value(r, 2) == name) return true;
    return false;
}

const char* const kAcadSharp[] = {
    "acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1015_binary.dxf",
    "acadsharp/sample_AC1018_ascii.dxf", "acadsharp/sample_AC1018_binary.dxf",
    "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1021_binary.dxf",
    "acadsharp/sample_AC1024_ascii.dxf", "acadsharp/sample_AC1024_binary.dxf"};
const char* const kAcadSharp2013 = "acadsharp/sample_AC1027_ascii.dxf";
const char* const kLibre[] = {"libre/example_2007.dxf", "libre/example_2010.dxf"};
const char* const kLibre2013 = "libre/example_2013.dxf";

// GAS_LINE as AutoCAD's: a dash, the text "GAS" upright, a gap
hdxf::Linetype& gas_line(hdxf::Drawing& dwg, const std::string& name) {
    hdxf::Linetype& lt = dwg.add_linetype(name, "Gas line ----GAS----GAS----GAS----GAS----GAS---", {0.5, -0.2, -0.25});
    lt.elements.resize(3);
    lt.elements[1].type = 2 | 8;
    lt.elements[1].style = "Standard";
    lt.elements[1].scale = 0.1;
    lt.elements[1].offset = {-0.1, -0.05};
    lt.elements[1].text = "GAS";
    return lt;
}

// A drawing with every 2013 property: GAS_LINE, a linetype upright on several elements, a
// multileader of the Standard style (text_extended, as AutoCAD's), a body with a change GUID
// (ACadSharp's D65's) — with SAT lines when `sat`, for the versions before 2013
hdxf::Drawing with_properties(hdxf::DxfVersion version, bool sat) {
    hdxf::Drawing dwg(version);
    gas_line(dwg, "GAS_LINE");
    hdxf::Linetype& many = dwg.add_linetype("MANY", "Upright texts and a shape",
                                            {0.5, -0.2, 0.3, -0.25, -0.1});
    many.elements.resize(5);
    for (size_t i : {1, 3, 4}) {
        many.elements[i].type = 2 | 8;
        many.elements[i].style = "Standard";
        many.elements[i].scale = 0.1;
        many.elements[i].text = "U" + std::to_string(i);
    }
    many.elements[4].type = 2;                      // text, not upright
    dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
    hdxf::AcisData body;
    if (sat) {
        body.lines = {"700 0 1 0 ", "@8 acadrom 1 0 1 0 ", "@7 unknown 1e-06 9.9999999999999995e-07 ",
                      "body $-1 -1 $-1 $-1 $-1 $-1 #", "End-of-ACIS-data "};
    }
    body.guid = "{1a113328-eb6d-d44d-824d-78b33668f9e7}";
    dwg.add_acis(hdxf::EntityType::Solid3D, body);
    return dwg;
}

} // namespace

// AutoCAD's 2000 … 2010 files read with the values its 2013 files have: GAS_LINE's text upright
// (ACadSharp: 74 2 and the XRECORD in 2000 … 2010, 74 10 in 2013), the bodies' GUIDs from their
// XDATA (ACadSharp's three, LibreDWG's 2E1 and 37D), LibreDWG's multileader 640 and its style
// with text_extended, jscad's new drawings' Standard; each record's extension dictionary read
TEST(r2013_properties_read_before_2013) {
    if (has_files({kAcadSharp2013})) {
        hdxf::Drawing d13 = load(read_bytes(external(kAcadSharp2013)));
        std::map<std::string, std::string> want = guids_of(d13);
        hdxf_test::assert_eq(want["D65"], "{1a113328-eb6d-d44d-824d-78b33668f9e7}", "2013: D65");
        for (const char* name : kAcadSharp) {
            if (!has_files({name})) continue;
            hdxf::Drawing dwg = load(read_bytes(external(name)));
            const hdxf::Linetype* gas = linetype(dwg, "GAS_LINE");
            hdxf_test::assert_true(gas != nullptr, name);
            hdxf_test::assert_true(element_types(*gas) == std::vector<int>({0, 10, 0}), std::string(name) + ": GAS_LINE 0, 10, 0");
            hdxf_test::assert_false(gas->xdictionary.empty(), std::string(name) + ": GAS_LINE's extension dictionary");
            std::map<std::string, std::string> guids = guids_of(dwg);
            int compared = 0;
            for (const auto& [h, g] : want) {
                if (!guids.count(h)) continue;
                hdxf_test::assert_eq(guids[h], g, std::string(name) + ": " + h + "'s GUID");
                ++compared;
            }
            hdxf_test::assert_eq(compared, 3, std::string(name) + ": three bodies");
        }
    }
    if (has_files({kLibre2013})) {
        hdxf::Drawing d13 = load(read_bytes(external(kLibre2013)));
        std::map<std::string, std::string> want = guids_of(d13);
        hdxf_test::assert_eq(static_cast<int>(want["2E1"].size()), 38, "2013: 2E1's GUID");
        hdxf_test::assert_eq(static_cast<int>(want["37D"].size()), 38, "2013: 37D's GUID");
        hdxf_test::assert_true(entity(d13, "640")->as_mleader().text_extended, "2013: 640's 295 1");
        for (const char* name : kLibre) {
            if (!has_files({name})) continue;
            hdxf::Drawing dwg = load(read_bytes(external(name)));
            hdxf_test::assert_true(guids_of(dwg) == want, std::string(name) + ": the GUIDs as 2013's");
            const hdxf::Entity* ml = entity(dwg, "640");
            hdxf_test::assert_true(ml && ml->as_mleader().text_extended, std::string(name) + ": 640's text_extended");
            hdxf_test::assert_false(ml->props.xdictionary.empty(), std::string(name) + ": 640's extension dictionary");
            hdxf_test::assert_true(mleader_styles_extended(dwg) == mleader_styles_extended(d13),
                                   std::string(name) + ": the styles' 298 as 2013's");
            // 37D has ACAD_STEPID besides: it stays
            const hdxf::Entity* region = entity(dwg, "37D");
            hdxf_test::assert_eq(static_cast<int>(region->props.xdata.size()), 1, std::string(name) + ": 37D's other XDATA");
        }
    }
    for (const char* name : {"jscad/empty2007.dxf", "jscad/empty2010.dxf"}) {
        if (!has_files({name})) continue;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        hdxf_test::assert_true(mleader_styles_extended(dwg) == std::vector<int>({1}), std::string(name) + ": Standard's 298 1");
    }
}

// Read and written back: AutoCAD's records as they were (the XRECORDs, the XDATA in its order)
TEST(r2013_properties_roundtrip_before_2013) {
    std::vector<std::string> names(std::begin(kAcadSharp), std::end(kAcadSharp));
    names.insert(names.end(), std::begin(kLibre), std::end(kLibre));
    names.push_back("jscad/empty2007.dxf");
    names.push_back("jscad/empty2010.dxf");
    int files = 0;
    for (const auto& name : names) {
        if (!std::filesystem::exists(external(name))) continue;
        ++files;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        bool binary = name.find("binary") != std::string::npos;
        std::vector<Record> before = records_of(in);
        std::vector<Record> after = records_of(save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii));
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()), name + ": records");
        for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
            hdxf_test::assert_true(before[i] == after[i], name + ": record " + std::to_string(i) + " (" +
                                   before[i][0].second + ") differs");
        }
    }
    if (files == 0) hdxf_test::skip("no 2000 … 2010 files");
}

// A new drawing with the 2013 properties as 2000 … 2010: AutoCAD's groups and objects — GAS_LINE's
// XRECORD byte for byte as ACadSharp's, the multileader's as LibreDWG's 640, D65's GUID XDATA as
// ACadSharp's (last) and its APPID; read back with the same values. As 2013: in the records, no
// round-trip objects, no APPID.
TEST(r2013_properties_written_before_2013) {
    std::vector<Record> acadsharp, libre;
    if (std::filesystem::exists(external("acadsharp/sample_AC1024_ascii.dxf"))) {
        acadsharp = records_of(read_bytes(external("acadsharp/sample_AC1024_ascii.dxf")));
    }
    if (std::filesystem::exists(external("libre/example_2010.dxf"))) {
        libre = records_of(read_bytes(external("libre/example_2010.dxf")));
    }
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007,
                               hdxf::DxfVersion::R2010}) {
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            std::string what = "version " + std::to_string(static_cast<int>(v)) +
                               (format == hdxf::DxfFormat::Binary ? " binary" : " ascii");
            hdxf::Drawing dwg = with_properties(v, true);
            std::vector<uint8_t> out = save(dwg, format);
            if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, what);
            std::vector<Record> file = records_of(out);

            // The linetypes: 74 without bit 8, the extension dictionary before the owner
            for (const auto& lt : of_type(file, "LTYPE")) {
                std::string name = value(lt, 2);
                if (name != "GAS_LINE" && name != "MANY") continue;
                Record head = head_of(lt);
                hdxf_test::assert_true(head.size() == 4 && head[0] == Record::value_type{102, "{ACAD_XDICTIONARY"} &&
                                       head[3].first == 330, what + ": " + name + "'s head");
                for (int i = 0; i < count_of(lt, 74); ++i) {
                    hdxf_test::assert_true(value(lt, 74, i) == "0" || value(lt, 74, i) == "2", what + ": 74 without 8");
                }
                Record dict = record(file, value(lt, 360));
                hdxf_test::assert_eq(value(dict, 330), value(lt, 5), what + ": the dictionary's owner");
                hdxf_test::assert_eq(count_of(dict, 102), 0, what + ": the dictionary has no reactors");
                hdxf_test::assert_eq(value(dict, 280), "1", what + ": hard owner");
                hdxf_test::assert_eq(value(dict, 281), "1", what + ": cloning");
                Record x = roundtrip_of(file, lt);
                hdxf_test::assert_eq(value(x, 330, 1), value(dict, 5), what + ": the XRECORD's owner");
                hdxf_test::assert_eq(value(x, 330, 0), value(dict, 5), what + ": the XRECORD's reactor");
                hdxf_test::assert_eq(value(x, 102, 2), "ACAD_ROUNDTRIP_2011_LINETYPE", what + ": the XRECORD");
                std::string data = name == "GAS_LINE" ? "04000000000000000A00000000000000"
                                                      : "06000000000000000A000000000000000A00000002000000";
                hdxf_test::assert_eq(value(x, 310), data, what + ": " + name + "'s values");
                if (name == "GAS_LINE" && !acadsharp.empty()) {
                    for (const auto& theirs : of_type(acadsharp, "LTYPE")) {
                        if (value(theirs, 2) != "GAS_LINE") continue;
                        hdxf_test::assert_true(body_of(roundtrip_of(file, lt)) == body_of(roundtrip_of(acadsharp, theirs)),
                                               what + ": GAS_LINE's XRECORD as AutoCAD's");
                        hdxf_test::assert_true(body_of(record(file, value(lt, 360))) ==
                                               body_of(record(acadsharp, value(theirs, 360))),
                                               what + ": GAS_LINE's dictionary as AutoCAD's");
                        // (its numbers as AutoCAD's ASCII; binary has the doubles)
                        if (format == hdxf::DxfFormat::Ascii) {
                            hdxf_test::assert_true(body_of(lt) == body_of(theirs), what + ": GAS_LINE as AutoCAD's");
                        }
                    }
                }
            }
            // The multileader: its extension dictionary with ACAD_ROUNDTRIP_2012_MLEADER (295 1)
            std::vector<Record> mls = of_type(file, "MULTILEADER");
            hdxf_test::assert_eq(static_cast<int>(mls.size()), 1, what + ": one multileader");
            Record mx = roundtrip_of(file, mls[0]);
            hdxf_test::assert_eq(value(mx, 102, 2), "ACAD_ROUNDTRIP_2012_MLEADER", what + ": the multileader's XRECORD");
            hdxf_test::assert_eq(value(mx, 295), "1", what + ": 295 1");
            hdxf_test::assert_eq(count_of(after_last(mls[0], 301), 295), 0, what + ": no 295 before 2013");
            if (!libre.empty()) {
                Record theirs = record(libre, "640");
                hdxf_test::assert_true(body_of(mx) == body_of(roundtrip_of(libre, theirs)), what + ": as LibreDWG's 640");
                hdxf_test::assert_true(body_of(record(file, value(mls[0], 360))) == body_of(record(libre, "EA1")),
                                       what + ": the dictionary as LibreDWG's EA1");
            }
            // The style: ACAD_ROUNDTRIP_2012_MLEADER_STYLE (298 1), 295 0
            std::vector<Record> styles = of_type(file, "MLEADERSTYLE");
            hdxf_test::assert_eq(value(roundtrip_of(file, styles[0]), 298), "1", what + ": the style's 298 1");
            hdxf_test::assert_eq(value(styles[0], 295), "0", what + ": the style's 295 0 before 2013");
            // The body: the GUID last in its XDATA, as ACadSharp's D65
            std::vector<Record> solids = of_type(file, "3DSOLID");
            Record guid = xdata_of(solids[0], "ACAD_OBJECT_CHANGE_GUID");
            hdxf_test::assert_eq(static_cast<int>(guid.size()), 12, what + ": the GUID's XDATA");
            hdxf_test::assert_eq(solids[0][solids[0].size() - 13].second, "ACAD_OBJECT_CHANGE_GUID", what + ": last");
            if (!acadsharp.empty()) {
                hdxf_test::assert_true(guid == xdata_of(record(acadsharp, "D65"), "ACAD_OBJECT_CHANGE_GUID"),
                                       what + ": D65's XDATA");
            }
            hdxf_test::assert_true(has_appid(file, "ACAD_OBJECT_CHANGE_GUID"), what + ": the APPID");

            // Read back: the same values, no GUID in the XDATA
            hdxf::Drawing back = load(out);
            hdxf_test::assert_true(element_types(*linetype(back, "GAS_LINE")) == std::vector<int>({0, 10, 0}), what + ": GAS_LINE");
            hdxf_test::assert_true(element_types(*linetype(back, "MANY")) == std::vector<int>({0, 10, 0, 10, 2}), what + ": MANY");
            for (const auto& e : back.entities()) {
                if (e.type == hdxf::EntityType::MLeader) hdxf_test::assert_true(e.as_mleader().text_extended, what + ": 295");
            }
            std::map<std::string, std::string> guids = guids_of(back);
            hdxf_test::assert_eq(static_cast<int>(guids.size()), 1, what + ": one body");
            hdxf_test::assert_eq(guids.begin()->second, "{1a113328-eb6d-d44d-824d-78b33668f9e7}", what + ": the GUID");
            hdxf_test::assert_true(mleader_styles_extended(back) == std::vector<int>({1}), what + ": 298");
            // … and again: the same records
            std::vector<Record> again = records_of(save(back, format));
            hdxf_test::assert_true(again == file, what + ": written back as read");
        }
    }

    // 2013: the values in the records
    hdxf::Drawing dwg = with_properties(hdxf::DxfVersion::R2013, false);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "2013");
    std::vector<Record> file = records_of(out);
    hdxf_test::assert_eq(count_type(file, "XRECORD"), 0, "2013: no round-trip XRECORD");
    for (const auto& lt : of_type(file, "LTYPE")) {
        if (value(lt, 2) == "GAS_LINE") hdxf_test::assert_eq(value(lt, 74, 1), "10", "2013: 74 10");
        hdxf_test::assert_eq(count_of(lt, 360), 0, "2013: no extension dictionary");
    }
    hdxf_test::assert_eq(value(after_last(of_type(file, "MULTILEADER")[0], 273), 295), "1", "2013: 295 1");
    Record solid = of_type(file, "3DSOLID")[0];
    hdxf_test::assert_eq(value(solid, 290), "1", "2013: 290 1");
    hdxf_test::assert_eq(value(solid, 2), "{1a113328-eb6d-d44d-824d-78b33668f9e7}", "2013: the GUID");
    hdxf_test::assert_eq(count_of(solid, 1001), 0, "2013: no XDATA");
    hdxf_test::assert_false(has_appid(file, "ACAD_OBJECT_CHANGE_GUID"), "2013: no APPID");
}

// A drawing built in memory that changes version: the round-trip objects made and removed (and
// their dictionaries), the values kept
TEST(r2013_properties_follow_the_version) {
    hdxf::Drawing dwg = with_properties(hdxf::DxfVersion::R2010, false);
    auto roundtrips = [](const std::vector<Record>& file) {
        int n = 0;
        for (const auto& r : of_type(file, "XRECORD")) {
            std::string name = value(r, 102, 2);
            n += name == "ACAD_ROUNDTRIP_2011_LINETYPE" || name == "ACAD_ROUNDTRIP_2012_MLEADER" ||
                 name == "ACAD_ROUNDTRIP_2012_MLEADER_STYLE";
        }
        return n;
    };
    std::vector<Record> r10 = records_of(save(dwg));
    hdxf_test::assert_eq(roundtrips(r10), 4, "2010: two linetypes, the multileader, the style");
    int dictionaries10 = count_type(r10, "DICTIONARY");
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2013,
                               hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2000}) {
        dwg.set_version(v);
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "follow the version");
        std::vector<Record> file = records_of(out);
        bool r2013 = v == hdxf::DxfVersion::R2013;
        hdxf_test::assert_eq(roundtrips(file), r2013 ? 0 : 4, "round-trip XRECORDs");
        // 2013 has no visual style dictionaries either (B59): those and the four of B60
        if (r2013) hdxf_test::assert_eq(count_type(file, "DICTIONARY"), dictionaries10 - 26 - 4, "dictionaries");
        if (v == hdxf::DxfVersion::R2010) hdxf_test::assert_true(file.size() == r10.size(), "2010 again: as before");
        hdxf_test::assert_true(element_types(*linetype(dwg, "MANY")) == std::vector<int>({0, 10, 0, 10, 2}), "MANY kept");
    }
    // Back as 2010 and a value changed: the XRECORD follows; one no longer needed goes with its
    // dictionary
    dwg.set_version(hdxf::DxfVersion::R2010);
    hdxf::Linetype* many = nullptr;
    for (auto& lt : dwg.linetypes()) if (lt.name == "MANY") many = &lt;
    many->elements[1].type = 2;
    many->elements[3].type = 2;
    for (auto& e : dwg.entities()) if (e.type == hdxf::EntityType::MLeader) e.as_mleader().text_extended = false;
    many = nullptr;
    for (auto& lt : dwg.linetypes()) if (lt.name == "GAS_LINE") lt.elements[0].type = 8;
    std::vector<Record> file = records_of(save(dwg));
    hdxf_test::assert_eq(roundtrips(file), 2, "GAS_LINE's and the style's");
    hdxf_test::assert_eq(count_type(file, "DICTIONARY"), dictionaries10 - 2, "MANY's and the multileader's gone");
    for (const auto& lt : of_type(file, "LTYPE")) {
        if (value(lt, 2) == "MANY") hdxf_test::assert_eq(count_of(lt, 360), 0, "MANY: no extension dictionary");
        if (value(lt, 2) == "GAS_LINE") {
            hdxf_test::assert_eq(value(roundtrip_of(file, lt), 310), "04000000080000000A00000000000000", "GAS_LINE changed");
        }
    }
    hdxf_test::assert_eq(count_of(of_type(file, "MULTILEADER")[0], 360), 0, "the multileader: none");

    // A multileader removed: its dictionary and XRECORD go with it (B19)
    for (auto& e : dwg.entities()) if (e.type == hdxf::EntityType::MLeader) e.as_mleader().text_extended = true;
    file = records_of(save(dwg));
    hdxf_test::assert_eq(roundtrips(file), 3, "the multileader's again");
    auto& list = dwg.entities();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const hdxf::Entity& e) { return e.type == hdxf::EntityType::MLeader; }),
               list.end());
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "multileader removed");
    file = records_of(out);
    hdxf_test::assert_eq(roundtrips(file), 2, "the multileader's gone with it");
    hdxf_test::assert_eq(count_type(file, "DICTIONARY"), dictionaries10 - 2, "and its dictionary");
}

// AutoCAD's files changed and written back: the XRECORD changed, made or removed after the value,
// everything else as read
TEST(r2013_properties_changed_in_a_file) {
    const char* name = "libre/example_2010.dxf";
    if (!has_files({name, "acadsharp/sample_AC1024_ascii.dxf"})) return;
    {
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        for (auto& e : dwg.entities()) if (e.props.handle == "640") e.as_mleader().text_extended = false;
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "640 without text_extended");
        std::vector<Record> after = records_of(out);
        hdxf_test::assert_true(record(after, "EA1").empty(), "640's dictionary gone");
        hdxf_test::assert_true(record(after, "EA2").empty(), "640's XRECORD gone");
        hdxf_test::assert_eq(count_of(record(after, "640"), 360), 0, "640: no extension dictionary");
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()) - 2, "two records fewer");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_false(entity(back, "640")->as_mleader().text_extended, "read back: false");

        // Removed: the multileader takes its dictionary and XRECORD (B19)
        hdxf::Drawing removed = load(in);
        auto& list = removed.entities();
        list.erase(std::remove_if(list.begin(), list.end(), [](const hdxf::Entity& e) { return e.props.handle == "640"; }),
                   list.end());
        std::vector<Record> without = records_of(save(removed));
        hdxf_test::assert_true(record(without, "EA1").empty() && record(without, "EA2").empty(), "removed with 640");

        // A body's GUID changed and removed
        hdxf::Drawing guid = load(in);
        for (auto& e : guid.entities()) {
            if (e.props.handle == "2E1") e.as_acis().guid = "{00112233-4455-6677-8899-aabbccddeeff}";
            if (e.props.handle == "37D") e.as_acis().guid.clear();
        }
        std::vector<Record> changed = records_of(save(guid));
        Record x = xdata_of(record(changed, "2E1"), "ACAD_OBJECT_CHANGE_GUID");
        Record want = {{1070, "1"}, {1071, "1122867"}, {1070, "17493"}, {1070, "26231"}, {1070, "136"}, {1070, "153"},
                       {1070, "170"}, {1070, "187"}, {1070, "204"}, {1070, "221"}, {1070, "238"}, {1070, "255"}};
        hdxf_test::assert_true(x == want, "2E1: the new GUID's XDATA");
        hdxf_test::assert_true(xdata_of(record(changed, "37D"), "ACAD_OBJECT_CHANGE_GUID").empty(), "37D: none");
        hdxf_test::assert_false(xdata_of(record(changed, "37D"), "ACAD_STEPID").empty(), "37D: ACAD_STEPID kept");
        hdxf::Drawing reread = load(save(guid));
        hdxf_test::assert_eq(entity(reread, "2E1")->as_acis().guid, "{00112233-4455-6677-8899-aabbccddeeff}", "read back");
    }
    {
        // ACadSharp's 2010 file: GAS_LINE's upright removed (its XRECORD and dictionary go), a
        // multileader given text_extended (an XRECORD made, in a new dictionary)
        std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1024_ascii.dxf"));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        std::string xdict;
        for (auto& lt : dwg.linetypes()) {
            if (lt.name != "GAS_LINE") continue;
            xdict = lt.xdictionary;
            lt.elements[1].type = 2;
        }
        std::string ml;
        for (auto& e : dwg.entities()) {
            if (e.type == hdxf::EntityType::MLeader && ml.empty()) {
                ml = e.props.handle;
                e.as_mleader().text_extended = true;
            }
        }
        hdxf_test::assert_false(ml.empty(), "a multileader");
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "ACadSharp changed");
        std::vector<Record> after = records_of(out);
        hdxf_test::assert_true(record(after, xdict).empty(), "GAS_LINE's dictionary gone");
        Record mx = roundtrip_of(after, record(after, ml));
        hdxf_test::assert_eq(value(mx, 295), "1", "the multileader's XRECORD");
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()), "two gone, two made");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_true(element_types(*linetype(back, "GAS_LINE")) == std::vector<int>({0, 2, 0}), "GAS_LINE read back");
        hdxf_test::assert_true(entity(back, ml)->as_mleader().text_extended, "the multileader read back");
    }
}

// add_mleader() gives the multileader its style's 298 (LibreDWG's 1 and 1, ACadSharp's 0 and 0)
TEST(r2013_properties_mleader_takes_the_style) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::Entity& standard = dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "a");
    hdxf_test::assert_true(standard.as_mleader().text_extended, "Standard: 298 1");
    hdxf::Object& plain = dwg.add_mleader_style("Plain");
    plain.as_mleader_style().text_extended = false;
    hdxf::Entity& other = dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "b", "Plain");
    hdxf_test::assert_false(other.as_mleader().text_extended, "Plain: 298 0");
    std::vector<Record> file = records_of(save(dwg));
    std::vector<Record> mls = of_type(file, "MULTILEADER");
    hdxf_test::assert_eq(value(after_last(mls[0], 273), 295), "1", "295 1");
    hdxf_test::assert_eq(value(after_last(mls[1], 273), 295), "0", "295 0");
}

// What 2013 has that 2000 … 2010 cannot: a layer's 348 that is not 0, a GEODATA of version 3 —
// refused before anything changes; a change GUID that is not one; R12 as before
TEST(r2013_properties_refused_before_2013) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2010}) {
        hdxf::Drawing dwg(v);
        dwg.add_layer("A").pointer_348 = "1F";
        hdxf_test::assert_true(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "348 refused");
        hdxf_test::assert_eq(dwg.last_error(), "Layer \"A\": 348 requires DXF 2013 or later");
        dwg.layers().back().pointer_348 = "0";
        hdxf_test::assert_false(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "348 0: " + dwg.last_error());
    }
    {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
        hdxf::GeoDataData g;
        g.version = 3;
        dwg.set_geodata(g);
        hdxf_test::assert_true(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "GEODATA 3 refused");
        hdxf_test::assert_eq(dwg.last_error(), "A GEODATA of version 3 requires DXF 2013 or later");
    }
    {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
        hdxf::AcisData body;
        body.guid = "{not a guid}";
        dwg.add_acis(hdxf::EntityType::Region, body);
        size_t objects = dwg.objects().size();
        hdxf_test::assert_true(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "bad GUID refused");
        hdxf_test::assert_eq(dwg.last_error(), "A body's change GUID \"{not a guid}\" is not a GUID");
        hdxf_test::assert_eq(static_cast<int>(dwg.objects().size()), static_cast<int>(objects), "nothing changed");
        dwg.set_version(hdxf::DxfVersion::R2013);
        hdxf_test::assert_false(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "2013 writes it as it is");
    }
    {
        // R12: a linetype's upright is not written (R12 has no complex linetypes), as before
        hdxf::Drawing dwg;
        gas_line(dwg, "GAS_LINE");
        std::vector<Record> file = records_of(save(dwg));
        hdxf_test::assert_eq(count_type(file, "XRECORD"), 0, "R12: no objects");
        for (const auto& lt : of_type(file, "LTYPE")) hdxf_test::assert_eq(count_of(lt, 74), 0, "R12: no 74");
    }
}

// ---------------------------------------------------------------------------
// The phase's review (Codex, against review-base-fas5)
// ---------------------------------------------------------------------------

// A MLEADERSTYLE added through objects() without a handle: saved (it crashed — the round-trip
// XRECORDs are made before handles are given out); one with text_extended gets a handle and its
// XRECORD, one without is left to the writer's handles
TEST(r2013_review_new_mleader_style_without_handle) {
    for (bool extended : {false, true}) {
        for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013}) {
            hdxf::Drawing dwg(v);
            hdxf::Object style;
            style.type = hdxf::ObjectType::MLeaderStyle;
            hdxf::MLeaderStyleData data;
            data.text_extended = extended;
            style.data = data;
            dwg.objects().push_back(style);
            std::vector<Record> file = records_of(save(dwg));
            std::vector<Record> styles = of_type(file, "MLEADERSTYLE");
            hdxf_test::assert_eq(static_cast<int>(styles.size()), 1, "the style written");
            bool roundtrip = !roundtrip_of(file, styles[0]).empty();
            hdxf_test::assert_eq(roundtrip, extended && v < hdxf::DxfVersion::R2013, "its round-trip XRECORD");
            hdxf_test::assert_false(value(styles[0], 5).empty(), "a handle");
        }
    }
}

// A record's extension dictionary changed or cleared after load: the new one alone, or none
// (the one read was kept beside it)
TEST(r2013_review_xdictionary_replaced_and_cleared) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
    hdxf::Entity& line = dwg.add_line({0, 0, 0}, {1, 1, 0});
    line.props.handle = "A00";
    for (const char* h : {"A01", "A02"}) {
        hdxf::Object dict;
        dict.type = hdxf::ObjectType::Dictionary;
        dict.handle = h;
        dict.owner = "A00";
        hdxf::DictionaryData d;
        d.hard_owner = true;
        dict.data = d;
        dwg.objects().push_back(dict);
    }
    line.props.xdictionary = "A01";
    std::vector<uint8_t> in = save(dwg);
    hdxf_test::assert_eq(value(record(records_of(in), "A00"), 360), "A01", "written with A01");

    hdxf::Drawing changed = load(in);
    for (auto& e : changed.entities()) if (e.props.handle == "A00") e.props.xdictionary = "A02";
    Record r = record(records_of(save(changed)), "A00");
    hdxf_test::assert_eq(count_of(r, 360), 1, "one 360");
    hdxf_test::assert_eq(value(r, 360), "A02", "the new dictionary");
    hdxf::Drawing back = load(save(changed));
    for (const auto& e : back.entities()) {
        if (e.props.handle == "A00") hdxf_test::assert_eq(e.props.xdictionary, "A02", "read back");
    }

    hdxf::Drawing cleared = load(in);
    for (auto& e : cleared.entities()) if (e.props.handle == "A00") e.props.xdictionary.clear();
    r = record(records_of(save(cleared)), "A00");
    hdxf_test::assert_eq(count_of(r, 360), 0, "cleared: no 360");
    hdxf_test::assert_eq(count_of(r, 102), 0, "cleared: no {ACAD_XDICTIONARY");

    // Unchanged: as read
    hdxf::Drawing same = load(in);
    hdxf_test::assert_true(same.save_to_memory(hdxf::DxfFormat::Ascii) == in, "unchanged: as read");
}

// XDATA on a VPORT, a linetype and a BLOCK: their applications in APPID
TEST(r2013_review_xdata_app_ids) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2013}) {
        hdxf::Drawing dwg(v);
        hdxf::Vport vp;
        vp.xdata = {{"HDXF_VPORT", {{1000, "v"}}}};
        dwg.vports().push_back(vp);
        dwg.add_linetype("DASHED", "Dashed", {0.5, -0.25}).xdata = {{"HDXF_LTYPE", {{1000, "l"}}}};
        hdxf::Block& b = dwg.add_block("B");
        b.xdata = {{"HDXF_BLOCK", {{1000, "b"}}}};
        dwg.add_line({0, 0, 0}, {1, 0, 0});
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "XDATA on tables and blocks");
        std::vector<Record> file = records_of(out);
        for (const char* app : {"HDXF_VPORT", "HDXF_LTYPE", "HDXF_BLOCK"}) {
            hdxf_test::assert_true(has_appid(file, app), std::string(app) + " registered");
        }
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(static_cast<int>(back.blocks().back().xdata.size()), 1, "the BLOCK's XDATA read back");
    }
}

// A block's color (BLOCK and ENDBLK): true color refused before 2004, a color book color a
// DBCOLOR from 2004, a color book color without RGB an error
TEST(r2013_review_block_color) {
    {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_block("B").block_color = hdxf::Color::from_rgb(10, 20, 30);
        hdxf_test::assert_true(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "R2000: refused");
        hdxf_test::assert_eq(dwg.last_error(), "True color requires DXF 2004 or later");
    }
    {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
        hdxf::Color c = hdxf::Color::from_rgb(10, 20, 30);
        c.name = "RAL CLASSIC$RAL 1006";
        dwg.add_block("B").block_color = c;
        std::vector<Record> file = records_of(save(dwg));
        std::vector<Record> colors = of_type(file, "DBCOLOR");
        hdxf_test::assert_eq(static_cast<int>(colors.size()), 1, "its DBCOLOR");
        for (const auto& r : of_type(file, "BLOCK")) {
            if (value(r, 2) == "B") hdxf_test::assert_eq(value(r, 430), "RAL CLASSIC$RAL 1006", "430 on BLOCK");
        }
    }
    {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
        hdxf::Color c;
        c.name = "RAL CLASSIC$RAL 1006";
        dwg.add_block("B").block_color = c;
        hdxf_test::assert_true(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "no RGB: refused");
        hdxf_test::assert_eq(dwg.last_error(), "Color \"RAL CLASSIC$RAL 1006\" from a color book has no RGB value");
    }
}

// DIMARCSYM (90, 2007+) not AutoCAD's 0: refused before 2007, written from 2007
TEST(r2013_review_dimarcsym_before_2007) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004}) {
        hdxf::Drawing dwg(v);
        dwg.add_dim_style("ARC").dimarcsym = 1;
        hdxf_test::assert_true(dwg.save_to_memory(hdxf::DxfFormat::Ascii).empty(), "refused");
        hdxf_test::assert_eq(dwg.last_error(), "A dimension style's arc length symbol (DIMARCSYM) requires DXF 2007 or later");
    }
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    dwg.add_dim_style("ARC").dimarcsym = 1;
    for (const auto& r : of_type(records_of(save(dwg)), "DIMSTYLE")) {
        if (value(r, 2) == "ARC") hdxf_test::assert_eq(value(r, 90), "1", "90 1");
    }
}
