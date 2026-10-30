#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// GEODATA (phase 4, step 4.6 — plan B50): its three forms read from AutoCAD's files (LibreDWG's
// gh209_1 in 2010, ACadSharp's geoloc in 2007 and 2018), written anew with AutoCAD's groups,
// set_geodata() as AutoCAD keeps it.
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

bool has_file(const std::string& name) {
    if (std::filesystem::exists(external(name))) return true;
    hdxf_test::skip("external test file missing: " + name);
    return false;
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

Record body_of(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first >= 1000) break;
        if (g.first == 100 && g.second == subclass) in = true;
        if (in) out.push_back(g);
    }
    return out;
}

// The same groups, a real number to its 16th digit
bool same_groups(const Record& a, const Record& b, std::string& why) {
    if (a.size() != b.size()) {
        why = std::to_string(a.size()) + " / " + std::to_string(b.size()) + " groups";
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        bool ok = a[i].first == b[i].first;
        if (ok && hdxf::internal::group_value_type(a[i].first) == hdxf::internal::GroupValueType::Double) {
            double x = std::strtod(a[i].second.c_str(), nullptr), y = std::strtod(b[i].second.c_str(), nullptr);
            ok = std::fabs(x - y) <= 1e-15 * std::max(1.0, std::fabs(x));
        } else if (ok) {
            ok = a[i].second == b[i].second;
        }
        if (!ok) {
            why = "group " + std::to_string(i) + ": " + std::to_string(a[i].first) + " " + a[i].second + " / " + b[i].second;
            return false;
        }
    }
    return true;
}

const hdxf::Object* geodata(const hdxf::Drawing& dwg) {
    for (const auto& o : dwg.objects()) if (o.type == hdxf::ObjectType::GeoData) return &o;
    return nullptr;
}

const char* const kGh209 = "libre/gh209_1_2010.dxf";
const char* const kV1 = "acadsharp/geoloc_AC1021.dxf";
const char* const kV3 = "acadsharp/geoloc.dxf";

} // namespace

// The three forms read: AutoCAD's 2010 default (gh209_1, version 2) and ACadSharp's drawing as
// 2007 (version 1) and 2018 (version 3) — the same place in both
TEST(geodata_read_values) {
    if (has_file(kGh209)) {
        hdxf::Drawing dwg = load(read_bytes(external(kGh209)));
        const hdxf::Object* o = geodata(dwg);
        hdxf_test::assert_true(o != nullptr, "GEODATA");
        const hdxf::GeoDataData& d = o->as_geodata();
        hdxf_test::assert_eq(d.version, 2, "version");
        hdxf_test::assert_eq(d.coordinate_type, 1, "local grid");
        hdxf_test::assert_true(d.reference_point.x == -122.394 && d.reference_point.y == 37.795, "reference point");
        hdxf_test::assert_true(d.horizontal_units == 6 && d.vertical_units == 6 && d.scale_estimation == 3, "91, 92, 95");
        hdxf_test::assert_eq(d.geo_rss, "<georss:point>37.7950 -122.3940</georss:point>", "302");
        hdxf_test::assert_true(d.coordinate_system.empty() && d.mesh_points.empty(), "301, 93");
        // Its defaults are this GEODATA's
        hdxf::GeoDataData fresh;
        fresh.host_block_handle = d.host_block_handle;
        hdxf_test::assert_true(fresh.reference_point.x == d.reference_point.x && fresh.scale_estimation == d.scale_estimation &&
                                   fresh.geo_rss == d.geo_rss && fresh.north_direction.y == d.north_direction.y,
                               "the defaults are AutoCAD's");
        hdxf_test::assert_eq(o->owner, "3", "owned by the model space's extension dictionary");
        hdxf_test::assert_eq(dwg.find_block("*Model_Space")->record.xdictionary, "3", "BLOCK_RECORD 360");
    }
    hdxf::GeoDataData v1, v3;
    if (has_file(kV1)) {
        v1 = geodata(load(read_bytes(external(kV1))))->as_geodata();
        hdxf_test::assert_eq(v1.version, 1, "version 1");
        hdxf_test::assert_true(v1.coordinate_system.rfind("PROJCS[\"WORLD-LM-CONIC\"", 0) == 0, "its WKT");
        hdxf_test::assert_eq(static_cast<int>(v1.mesh_points.size()), 12, "12 mesh points");
        hdxf_test::assert_eq(v1.civil3d.front().value, "CIVIL3D_DATA_BEGIN", "CIVIL3D");
        hdxf_test::assert_eq(v1.projection_radius, 6362228.650184014, "143 from the CIVIL3D data");
    }
    if (has_file(kV3)) {
        v3 = geodata(load(read_bytes(external(kV3))))->as_geodata();
        hdxf_test::assert_eq(v3.version, 3, "version 3");
        hdxf_test::assert_true(v3.coordinate_system.rfind("<?xml version=\"1.0\" encoding=\"UTF-16\"", 0) == 0, "its XML");
        hdxf_test::assert_eq(static_cast<int>(v3.coordinate_system.size()), 12 * 250 + 195, "the parts joined");
    }
    if (has_file(kV1) && has_file(kV3)) {
        // The same drawing: version 1 has the reference point's y, x, z in 40, 41, 42
        hdxf_test::assert_true(v1.reference_point.x == v3.reference_point.x && v1.reference_point.y == v3.reference_point.y &&
                                   v1.reference_point.z == v3.reference_point.z,
                               "the reference point");
        hdxf_test::assert_eq(v1.horizontal_unit_scale, v3.horizontal_unit_scale, "46 as 40");
        for (size_t i = 0; i < v1.mesh_points.size() && i < v3.mesh_points.size(); ++i) {
            hdxf_test::assert_true(v1.mesh_points[i].source.x == v3.mesh_points[i].source.x &&
                                       v1.mesh_points[i].destination.y == v3.mesh_points[i].destination.y,
                                   "the mesh points");
        }
        hdxf_test::assert_eq(v1.projection_radius, v3.projection_radius, "143");
    }
}

// Written anew: AutoCAD's groups — version 2 in its 2010 file, version 1 in its 2007 file; and
// both files come back byte for byte
TEST(geodata_written_anew_as_autocad) {
    for (const char* name : {kGh209, kV1}) {
        if (!has_file(name)) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        std::vector<uint8_t> same = save(dwg);
        std::vector<uint8_t> lf;
        for (uint8_t b : in) if (b != '\r') lf.push_back(b);
        hdxf_test::assert_true(same == lf, std::string(name) + ": bytes");
        std::string handle;
        for (auto& o : dwg.objects()) {
            if (o.type != hdxf::ObjectType::GeoData) continue;
            o.raw = hdxf::RawRecord();
            handle = o.handle;
        }
        for (auto& b : dwg.blocks()) {
            if (b.name == "*Model_Space") b.record.raw = hdxf::RawRecord();
        }
        std::vector<Record> after = records_of(save(dwg));
        std::string why;
        hdxf_test::assert_true(same_groups(body_of(record(before, handle), "AcDbGeoData"),
                                           body_of(record(after, handle), "AcDbGeoData"), why),
                               std::string(name) + ": " + why);
        hdxf_test::assert_true(record(before, handle) == record(after, handle) ||
                                   same_groups(record(before, handle), record(after, handle), why),
                               std::string(name) + ": the record " + why);
        std::string model = dwg.find_block("*Model_Space")->record.handle;
        hdxf_test::assert_true(record(before, model) == record(after, model), std::string(name) + ": BLOCK_RECORD with its 360");
    }
}

// set_geodata() in a new 2010 drawing: the model space's extension dictionary as AutoCAD makes it,
// the GEODATA in it as version 2 with its class; a second call replaces it; refused as 2007
TEST(geodata_set) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    hdxf::GeoDataData d;
    d.coordinate_type = 2;
    d.design_point = {100, 200, 0};
    d.reference_point = {674000, 6580000, 0};
    d.coordinate_system = std::string(600, 'x');
    d.mesh_points = {{{1, 2}, {3, 4}}};
    hdxf::Object& o = dwg.set_geodata(d);
    std::string handle = o.handle;
    dwg.set_geodata(d);
    int count = 0;
    for (const auto& x : dwg.objects()) count += x.type == hdxf::ObjectType::GeoData;
    hdxf_test::assert_eq(count, 1, "replaced, not added");
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "GEODATA");
    std::vector<Record> recs = records_of(out);
    std::string model = dwg.find_block("*Model_Space")->record.handle;
    Record br = record(recs, model);
    hdxf_test::assert_true(br.size() > 4 && br[2] == Record::value_type{102, "{ACAD_XDICTIONARY"} && br[4] == Record::value_type{102, "}"},
                           "BLOCK_RECORD's 102 {ACAD_XDICTIONARY");
    Record dict = record(recs, br[3].second);
    hdxf_test::assert_true(std::find(dict.begin(), dict.end(), Record::value_type{3, "ACAD_GEOGRAPHICDATA"}) != dict.end() &&
                               std::find(dict.begin(), dict.end(), Record::value_type{360, handle}) != dict.end() &&
                               std::find(dict.begin(), dict.end(), Record::value_type{280, "1"}) != dict.end(),
                           "the hard-owning dictionary");
    Record geo = record(recs, handle);
    Record body = body_of(geo, "AcDbGeoData");
    std::vector<int> codes;
    for (const auto& g : body) codes.push_back(g.first);
    hdxf_test::assert_true(codes == std::vector<int>({100, 90, 330, 70, 10, 20, 30, 11, 21, 31, 40, 91, 41, 92, 210, 220, 230,
                                                      12, 22, 95, 141, 294, 142, 143, 301, 302, 305, 306, 307, 93, 13, 23,
                                                      14, 24, 96}),
                           "the 2010 form");
    hdxf_test::assert_eq(body[1].second, "2", "version 2");
    hdxf_test::assert_eq(body[2].second, model, "the host block");
    hdxf_test::assert_eq(static_cast<int>(body[24].second.size()), 600, "the coordinate system in one 301");
    bool cls = false;
    for (const auto& r : recs) cls = cls || (r[0].second == "CLASS" && r[1].second == "GEODATA");
    hdxf_test::assert_true(cls, "class GEODATA");
    hdxf::Drawing back = load(out);
    hdxf_test::assert_eq(geodata(back)->as_geodata().coordinate_system, std::string(600, 'x'), "read back");

    dwg.set_version(hdxf::DxfVersion::R2007);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "2007");
    hdxf_test::assert_eq(dwg.last_error(), "A GEODATA of version 2 or 3 requires DXF 2010 or later");
    dwg.set_version(hdxf::DxfVersion::R2010);
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    hdxf::GeoDataData v3 = d;
    v3.version = 3;
    dwg.set_geodata(v3);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "version 3");
    hdxf_test::assert_eq(dwg.last_error(), "A GEODATA of version 3 requires DXF 2013 or later");
}

// GEODATA in 2013 (phase 5, step 5.8 — plan B64): ACadSharp's GEODATA of version 3 (AutoCAD
// 2018's geoloc.dxf; no AutoCAD file of 2013 has one) set in a new 2013 drawing, ASCII and binary:
// AutoCAD's record group for group — its coordinate system in AutoCAD's twelve 303 parts of 250
// characters and the last in 301 — but the host block's handle; read back the same
TEST(geodata_2013_as_autocad) {
    if (!has_file(kV3)) return;
    std::vector<uint8_t> in = read_bytes(external(kV3));
    std::vector<Record> theirs = records_of(in);
    hdxf::GeoDataData d = geodata(load(in))->as_geodata();
    std::string their_handle = geodata(load(in))->handle;
    d.host_block_handle.clear();
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        dwg.add_line({0, 0, 0}, {100, 50, 0});
        hdxf::Object& o = dwg.set_geodata(d);
        std::string handle = o.handle;
        std::vector<uint8_t> out = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "GEODATA 3");
        std::vector<Record> ours = records_of(out);
        Record a = body_of(record(theirs, their_handle), "AcDbGeoData");
        Record b = body_of(record(ours, handle), "AcDbGeoData");
        std::string model = dwg.find_block("*Model_Space")->record.handle;
        hdxf_test::assert_true(b.size() > 3 && b[2] == Record::value_type{330, model}, "the host block");
        if (b.size() > 3 && a.size() > 3) b[2].second = a[2].second;
        std::string why;
        // A binary file has the control characters themselves, an ASCII file their carets (^J)
        for (auto& [code, v] : b) {
            std::string carets;
            for (char c : v) {
                if (static_cast<unsigned char>(c) < 32) carets += std::string("^") + static_cast<char>(c + 64);
                else carets += c;
            }
            v = carets;
        }
        bool same = same_groups(a, b, why);
        hdxf_test::assert_true(same, "AutoCAD's groups: " + why);
        int parts = 0;
        for (const auto& g : b) parts += g.first == 303;
        hdxf_test::assert_eq(parts, 12, "twelve 303");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(geodata(back)->as_geodata().coordinate_system, d.coordinate_system, "read back");
        hdxf_test::assert_eq(geodata(back)->as_geodata().version, 3, "version 3 read back");
    }
}

// A long coordinate system in 2013: parts of 250 characters (code points, not bytes) in 303, the
// rest in 301; one of 250 or fewer in 301 alone. Version 1 or 2 as 2013 is an error before
// anything changes (AutoCAD makes it version 3: a conversion, B64); version 3 as 2010 as before
TEST(geodata_2013_coordinate_system_and_versions) {
    for (size_t length : {std::size_t(250), std::size_t(251), std::size_t(600)}) {
        std::string cs;
        for (size_t i = 0; i < length; ++i) cs += i % 7 == 0 ? "\xC3\xA5" : "x";  // å, 2 bytes
        hdxf::GeoDataData d;
        d.version = 3;
        d.coordinate_system = cs;
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        dwg.set_geodata(d);
        std::vector<Record> recs = records_of(save(dwg));
        Record body = body_of(record(recs, geodata(dwg)->handle), "AcDbGeoData");
        std::vector<size_t> sizes;
        std::string joined;
        for (const auto& [code, v] : body) {
            if (code != 303 && code != 301) continue;
            size_t points = 0;
            for (unsigned char c : v) points += (c & 0xC0) != 0x80;
            sizes.push_back(points);
            joined += v;
            hdxf_test::assert_eq(code, sizes.size() * 250 < length ? 303 : 301, "303 parts, the last in 301");
        }
        std::vector<size_t> expected;
        for (size_t left = length; left > 250; left -= 250) expected.push_back(250);
        expected.push_back(length % 250 == 0 ? 250 : length % 250);
        hdxf_test::assert_true(sizes == expected, "parts of 250 code points: " + std::to_string(length));
        hdxf_test::assert_eq(joined, cs, "the coordinate system whole");
        hdxf_test::assert_eq(geodata(load(save(dwg, hdxf::DxfFormat::Binary)))->as_geodata().coordinate_system, cs,
                             "read back from binary");
    }

    {
        hdxf::GeoDataData d;
        d.version = 1;
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        dwg.add_line({0, 0, 0}, {1, 1, 0});
        dwg.set_geodata(d);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "version 1 as 2013");
        hdxf_test::assert_eq(dwg.last_error(), std::string("A GEODATA of version 1 is written as version 3 in DXF 2013"));
        hdxf_test::assert_true(dwg.entities()[0].props.handle.empty(), "nothing changed");
    }
    // Version 2 is a record of 2013 and 2018 as well: AutoCAD 2025 saves it there (ext_map_drawing3, proxy)
    for (hdxf::DxfVersion target : {hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        hdxf::GeoDataData d;
        d.version = 2;
        hdxf::Drawing dwg(target);
        dwg.add_line({0, 0, 0}, {1, 1, 0});
        dwg.set_geodata(d);
        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_true(!out.empty(), "version 2 in 2013 and 2018: " + dwg.last_error());
        hdxf_test::assert_eq(2, geodata(load(out))->as_geodata().version, "version 2 read back");
    }
    hdxf::GeoDataData v3;
    v3.version = 3;
    hdxf::Drawing r2010(hdxf::DxfVersion::R2010);
    r2010.set_geodata(v3);
    hdxf_test::assert_true(r2010.save_to_memory().empty(), "version 3 as 2010");
    hdxf_test::assert_eq(r2010.last_error(), "A GEODATA of version 3 requires DXF 2013 or later");
}
