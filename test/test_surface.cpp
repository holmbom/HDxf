#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include "extents.hpp"
#include "surface_graphics.hpp"
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
// Surfaces and HELIX (phase 3, step 3.5 — plan B39): read from AutoCAD's files (LibreDWG's
// Surface drawing as 2004 with all five types, its Helix as 2007, jscad's 3Dlines as 2013),
// written back as they were, written anew in 2007 with AutoCAD's groups and proxy graphics,
// their extents as AutoCAD's.
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

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

const hdxf::Entity* find(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    return nullptr;
}

Record record_of_type(const std::vector<Record>& records, const std::string& type) {
    for (const auto& r : records) if (!r.empty() && r[0].second == type) return r;
    return {};
}

// A record's groups from AcDbModelerGeometry (or AcDbSpline) to the XDATA: the type's own
bool same_body(const Record& a, const Record& b, const std::string& from, std::string& why) {
    auto body = [&](const Record& r) {
        Record out;
        bool in = false;
        for (const auto& g : r) {
            if (g.first == 100 && g.second == from) in = true;
            if (g.first >= 1000) break;
            if (in) out.push_back(g);
        }
        return out;
    };
    Record x = body(a), y = body(b);
    if (x.size() != y.size()) {
        why = std::to_string(x.size()) + " / " + std::to_string(y.size()) + " groups";
        return false;
    }
    for (size_t i = 0; i < x.size(); ++i) {
        bool real = hdxf::internal::group_value_type(x[i].first) == hdxf::internal::GroupValueType::Double;
        bool same = x[i].first == y[i].first &&
                    (real ? std::fabs(std::strtod(x[i].second.c_str(), nullptr) - std::strtod(y[i].second.c_str(), nullptr)) <=
                                1e-15 * std::max(1.0, std::fabs(std::strtod(x[i].second.c_str(), nullptr)))
                          : x[i].second == y[i].second);
        if (!same) {
            why = "group " + std::to_string(i) + ": " + std::to_string(x[i].first) + " " + x[i].second;
            return false;
        }
    }
    return true;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// The proxy graphics of a record (92/160 and 310 in AcDbEntity)
std::vector<uint8_t> proxy_of(const Record& r) {
    std::vector<uint8_t> out;
    bool in = false;
    for (const auto& [code, v] : r) {
        if (code == 100 && in) break;
        if (!in && (code == 92 || code == 160)) { in = true; continue; }
        if (in && code == 310) {
            for (size_t i = 0; i + 1 < v.size(); i += 2) {
                out.push_back(static_cast<uint8_t>(hex_digit(v[i]) * 16 + hex_digit(v[i + 1])));
            }
        }
    }
    return out;
}

// The points of a proxy polyline (command 6, the first)
std::vector<hdxf::Point3> polyline_of(const std::vector<uint8_t>& b) {
    std::vector<hdxf::Point3> out;
    if (b.size() < 20) return out;
    auto i32 = [&](size_t o) {
        uint32_t u = 0;
        for (int k = 0; k < 4; ++k) u |= static_cast<uint32_t>(b[o + k]) << (8 * k);
        return static_cast<int32_t>(u);
    };
    auto f64 = [&](size_t o) {
        uint64_t u = 0;
        for (int k = 0; k < 8; ++k) u |= static_cast<uint64_t>(b[o + k]) << (8 * k);
        double d;
        std::memcpy(&d, &u, 8);
        return d;
    };
    if (i32(12) != 6) return out;
    int n = i32(16);
    for (int i = 0; i < n && 20 + 24 * static_cast<size_t>(i + 1) <= b.size(); ++i) {
        size_t o = 20 + 24 * static_cast<size_t>(i);
        out.push_back({f64(o), f64(o + 8), f64(o + 16)});
    }
    return out;
}

hdxf::Point3 header_point(const hdxf::Drawing& dwg, const std::string& name) {
    hdxf::Point3 p;
    if (const hdxf::HeaderVar* hv = dwg.find_header(name)) {
        for (const auto& [code, value] : hv->values) {
            if (code == 10) p.x = std::stod(value);
            if (code == 20) p.y = std::stod(value);
            if (code == 30) p.z = std::stod(value);
        }
    }
    return p;
}

bool near(const hdxf::Point3& a, const hdxf::Point3& b, double tol) {
    return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol && std::fabs(a.z - b.z) <= tol;
}

} // namespace

// The values of AutoCAD's surfaces (LibreDWG's Surface drawing, 2004) and helix (2007)
TEST(surface_read_values) {
    if (!has_files({"libre/Surface_2004.dxf", "libre/Helix_2007.dxf"})) return;
    hdxf::Drawing dwg = load(read_bytes(external("libre/Surface_2004.dxf")));
    const hdxf::Entity* x = find(dwg, "2D8");
    hdxf_test::assert_true(x && x->type == hdxf::EntityType::ExtrudedSurface, "EXTRUDEDSURFACE");
    const hdxf::SurfaceData& e = x->as_surface();
    hdxf_test::assert_eq(e.u_isolines, 6, "71");
    hdxf_test::assert_eq(e.source.class_id, 35, "the profile's class");
    hdxf_test::assert_eq(e.source.bits, 472, "its size in bits");
    hdxf_test::assert_eq(static_cast<int>(e.source.data.size()), 60, "its bytes");
    hdxf_test::assert_true(e.sweep_vector.z == 200.0, "the sweep vector");
    hdxf_test::assert_eq(static_cast<int>(e.matrix.size()), 16, "the matrix");
    hdxf_test::assert_true(e.sweep.sweep_matrix.size() == 16 && e.sweep.sweep_matrix[3] == 1791.25022532106, "46");
    hdxf_test::assert_eq(e.sweep.option_71, 2, "71 of the sweep");
    hdxf_test::assert_false(e.acis.lines.empty(), "its ACIS data");

    const hdxf::Entity* l = find(dwg, "34D");
    hdxf_test::assert_true(l && l->type == hdxf::EntityType::LoftedSurface, "LOFTEDSURFACE");
    hdxf_test::assert_eq(static_cast<int>(l->as_surface().curves.size()), 2, "its two curves");
    hdxf_test::assert_eq(l->as_surface().curves[1].bits, 608, "the second's size");
    hdxf_test::assert_true(l->as_surface().start_draft_angle == 1.570796326794896, "41");
    hdxf_test::assert_true(l->as_surface().virtual_guide, "297");

    const hdxf::Entity* r = find(dwg, "366");
    hdxf_test::assert_true(r && r->type == hdxf::EntityType::RevolvedSurface, "REVOLVEDSURFACE");
    hdxf_test::assert_true(r->as_surface().axis_vector.y == 1.0 && r->as_surface().revolve_angle == 0.1929873338236447,
                           "the axis and the angle");

    const hdxf::Entity* s = find(dwg, "411");
    hdxf_test::assert_true(s && s->type == hdxf::EntityType::SweptSurface, "SWEPTSURFACE");
    hdxf_test::assert_eq(s->as_surface().path.class_id, 19, "the path's class (91)");
    hdxf_test::assert_eq(static_cast<int>(s->as_surface().source.data.size()), 135, "the profile in two 310");
    hdxf_test::assert_eq(s->as_surface().sweep.alignment, 1, "70");
    hdxf_test::assert_true(s->as_surface().sweep.path_matrix[11] == 175.2425754417727, "47");

    const hdxf::Entity* p = find(dwg, "50A");
    hdxf_test::assert_true(p && p->type == hdxf::EntityType::PlaneSurface, "PLANESURFACE");
    hdxf_test::assert_eq(static_cast<int>(p->as_surface().acis.lines.size()), 17, "its ACIS lines");

    hdxf::Drawing h = load(read_bytes(external("libre/Helix_2007.dxf")));
    const hdxf::Entity* hx = find(h, "1BF");
    hdxf_test::assert_true(hx && hx->type == hdxf::EntityType::Helix, "HELIX");
    const hdxf::HelixData& d = hx->as_helix();
    hdxf_test::assert_eq(static_cast<int>(d.spline.control_points.size()), 44, "control points");
    hdxf_test::assert_eq(static_cast<int>(d.spline.knots.size()), 48, "knots");
    hdxf_test::assert_true(d.radius == 6.827831659934211 && d.turns == 3.0 && d.right_handed && d.constrain == 1,
                           "radius, turns, handedness, constraint");
    hdxf_test::assert_true(d.axis_base.x == 2.254053240352249 && d.axis_vector.z == 1.0, "the axis");
}

// Read and written again, unchanged: byte for byte
TEST(surface_round_trip) {
    for (const char* name : {"libre/Surface_2004.dxf", "libre/Helix_2007.dxf"}) {
        if (!has_files({name})) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_true(out == in, std::string(name) + ": byte for byte");
    }
}

// AutoCAD's surfaces and helix in a new 2007 and 2010 drawing: AutoCAD's groups, the surfaces'
// empty proxy graphics (92 8, as AutoCAD's 2007 files; 160 8 in 2010, B51), the helix's polyline
// as AutoCAD's
TEST(surface_written_anew_2007_2010) {
    for (hdxf::DxfVersion version : {hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010}) {
        if (!has_files({"libre/Surface_2004.dxf", "libre/Helix_2007.dxf"})) return;
        std::vector<uint8_t> surfaces = read_bytes(external("libre/Surface_2004.dxf"));
        std::vector<uint8_t> helix = read_bytes(external("libre/Helix_2007.dxf"));
        hdxf::Drawing dwg(version);
        for (const auto* data : {&surfaces, &helix}) {
            hdxf::Drawing src = load(*data);
            for (const auto& e : src.entities()) {
                bool surface = e.type >= hdxf::EntityType::Surface && e.type <= hdxf::EntityType::PlaneSurface;
                if (!surface && e.type != hdxf::EntityType::Helix) continue;
                hdxf::Entity c = e;
                c.raw = hdxf::RawRecord();
                c.props.handle.clear();
                c.props.reactors.clear();       // handles of the source drawing
                dwg.add_entity(c);
            }
        }
        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        dxf_structure::check_structure(out, "surfaces and helix");
        std::vector<Record> after = records_of(out);
        std::vector<Record> theirs = records_of(surfaces);
        for (const char* type : {"EXTRUDEDSURFACE", "LOFTEDSURFACE", "REVOLVEDSURFACE", "SWEPTSURFACE", "PLANESURFACE"}) {
            Record ours = record_of_type(after, type);
            std::string why;
            hdxf_test::assert_true(same_body(record_of_type(theirs, type), ours, "AcDbModelerGeometry", why),
                                   std::string(type) + ": " + why);
            std::vector<uint8_t> proxy = proxy_of(ours);
            hdxf_test::assert_true(proxy == std::vector<uint8_t>({8, 0, 0, 0, 0, 0, 0, 0}), std::string(type) + ": proxy graphics");
        }
        Record ours = record_of_type(after, "HELIX");
        Record autocad = record_of_type(records_of(helix), "HELIX");
        std::string why;
        hdxf_test::assert_true(same_body(autocad, ours, "AcDbSpline", why), "HELIX: " + why);
        std::vector<hdxf::Point3> a = polyline_of(proxy_of(autocad)), b = polyline_of(proxy_of(ours));
        hdxf_test::assert_eq(static_cast<int>(b.size()), 192, "the helix as 192 points");
        hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), "as many as AutoCAD's");
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            hdxf_test::assert_true(near(a[i], b[i], 1e-12), "point " + std::to_string(i));
        }
        // The classes, as AutoCAD writes them
        bool helix_class = false, plane_class = false;
        for (const auto& r : after) {
            if (r.empty() || r[0].second != "CLASS") continue;
            for (size_t i = 0; i + 4 < r.size(); ++i) {
                if (r[i].first == 1 && r[i].second == "HELIX") helix_class = r[i + 3].second == "4095";
                if (r[i].first == 1 && r[i].second == "PLANESURFACE") plane_class = r[i + 3].second == "4095";
            }
        }
        hdxf_test::assert_true(helix_class && plane_class, "HELIX and PLANESURFACE classes (4095)");
        for (const auto& r : after) {
            if (r.empty() || r[0].second != "PLANESURFACE") continue;
            bool size160 = std::find_if(r.begin(), r.end(), [](const Record::value_type& g) { return g.first == 160; }) != r.end();
            hdxf_test::assert_true(size160 == (version == hdxf::DxfVersion::R2010), "92 / 160");
        }
    }
}

// A new surface cannot be written as 2004 or older (AutoCAD's 2004 file has its isolines as proxy
// graphics, B39); one read from a 2004 file is written back with them, changed too. HELIX not as R12.
TEST(surface_refused_before_2007) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004}) {
        hdxf::Drawing dwg(v);
        dwg.add_surface(hdxf::EntityType::ExtrudedSurface, hdxf::SurfaceData{});
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved as an older version");
        // the error says what is missing: the isolines HDxf does not make (9.S, Codex 13), not a version the surface needs
        const std::string error = dwg.last_error();
        hdxf_test::assert_true(error.rfind("EXTRUDEDSURFACE ", 0) == 0 && error.find("HDxf does not make its isolines") != std::string::npos &&
                                   error.find("DXF 2000 and 2004 keep") != std::string::npos,
                               error);
    }
    hdxf::Drawing r12;
    hdxf::Entity helix;
    helix.type = hdxf::EntityType::Helix;
    helix.data = hdxf::HelixData{};
    r12.add_entity(helix);
    hdxf_test::assert_true(r12.save_to_memory().empty(), "HELIX as R12");
    hdxf_test::assert_eq(r12.last_error(), std::string("HELIX requires DXF 2000 or later"));

    if (!has_files({"libre/Surface_2004.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("libre/Surface_2004.dxf"));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    for (auto& e : dwg.entities()) {
        if (e.props.handle == "50A") e.props.color = hdxf::Color::from_aci(3);
    }
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    Record changed;
    for (const auto& r : records_of(out)) {
        if (r.size() > 1 && r[0].second == "PLANESURFACE") changed = r;
    }
    bool green = false;
    for (const auto& [code, v] : changed) green = green || (code == 62 && v == "3");
    hdxf_test::assert_true(green, "the new color");
    // The color is in the isolines (22 of each curve: ACI 3, 0xC3000003): they are made anew since 9.16 (surface_graphics.cpp),
    // as AutoCAD's own draws them for the changed plane
    {
        std::vector<uint8_t> graphics = proxy_of(changed);
        const uint8_t green[4] = {3, 0, 0, 0xC3};
        bool found = std::search(graphics.begin(), graphics.end(), green, green + 4) != graphics.end();
        hdxf_test::assert_true(found && graphics.size() > 8, "its isolines with the new color");
    }

    // Only in that round trip: the file's surface (its record with it) copied into a new R12,
    // 2000 or 2004 drawing is refused like a new one — R12 has no surfaces, and the copy's
    // isolines would be the other drawing's
    const hdxf::Entity* surface = nullptr;
    for (const auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::ExtrudedSurface) surface = &e;
    }
    hdxf_test::assert_true(surface && !surface->raw.empty(), "the file's surface, with its record");
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004}) {
        hdxf::Drawing copy(v);
        copy.add_entity(*surface);
        // 2000 and 2004 make the isolines of the bodies surface_graphics.cpp draws (9.16); R12 has no surfaces
        bool drawn = v != hdxf::DxfVersion::R12 && hdxf::internal::surface_isoline_error(*surface).empty();
        hdxf_test::assert_true(copy.save_to_memory().empty() != drawn, "the copy saved or refused as the body allows");
        if (v == hdxf::DxfVersion::R12) hdxf_test::assert_eq(copy.last_error(), std::string("EXTRUDEDSURFACE requires DXF 2007 or later"));
        else if (!drawn) hdxf_test::assert_true(copy.last_error().find("HDxf does not make its isolines") != std::string::npos, copy.last_error());
    }
}

// The extents: HELIX as AutoCAD draws it (its 192 points: AutoCAD's $EXTMIN/$EXTMAX), surfaces
// by their ACIS body (jscad's 3Dlines, 2013: four extruded surfaces among the edges)
TEST(surface_extents_as_autocad) {
    for (const char* name : {"libre/Helix_2007.dxf", "jscad/3Dlines.dxf"}) {
        if (!has_files({name})) continue;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        hdxf::internal::Extents model, paper;
        std::vector<std::string> warnings;
        hdxf::internal::drawing_extents(dwg, {}, {}, model, paper, warnings);
        hdxf::Point3 lo = header_point(dwg, "$EXTMIN"), hi = header_point(dwg, "$EXTMAX");
        double tol = 1e-12 * std::max({1.0, std::fabs(lo.x), std::fabs(lo.y), std::fabs(hi.x), std::fabs(hi.y)});
        hdxf_test::assert_true(near(model.min, lo, tol), std::string(name) + ": $EXTMIN");
        hdxf_test::assert_true(near(model.max, hi, tol), std::string(name) + ": $EXTMAX");
    }
}

// ---------------------------------------------------------------------------
// Surfaces and HELIX in 2013 (phase 5, step 5.8 — plan B65): the surfaces' AcDbModelerGeometry
// in 2013's form (290/2) with their ACIS as SAB in ACDSDATA, everything else as in 2010
// ---------------------------------------------------------------------------

namespace {

std::string value_of(const Record& r, int code) {
    for (const auto& [c, v] : r) if (c == code) return v;
    return {};
}

Record record_by_handle(const std::vector<Record>& records, const std::string& handle) {
    for (const auto& r : records) {
        if (r.size() > 1 && r[1].first == 5 && r[1].second == handle) return r;
    }
    return {};
}

// The groups of a record's subclass, to the next subclass
Record subclass_groups(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 100) { in = g.second == subclass; continue; }
        if (in) out.push_back(g);
    }
    return out;
}

// ACDSDATA's ASM_Data records: the bytes by the owner's handle
std::vector<std::pair<std::string, std::vector<uint8_t>>> asm_data(const std::vector<Record>& records) {
    std::vector<std::pair<std::string, std::vector<uint8_t>>> out;
    for (const auto& r : records) {
        if (r[0].second != "ACDSRECORD") continue;
        bool sab = false;
        for (const auto& g : r) sab = sab || (g.first == 2 && g.second == "ASM_Data");
        if (!sab) continue;
        std::vector<uint8_t> bytes;
        for (const auto& [code, v] : r) {
            if (code != 310) continue;
            for (size_t i = 0; i + 1 < v.size(); i += 2) {
                bytes.push_back(static_cast<uint8_t>(hex_digit(v[i]) * 16 + hex_digit(v[i + 1])));
            }
        }
        out.push_back({value_of(r, 320), bytes});
    }
    return out;
}

bool is_surface(hdxf::EntityType t) {
    return t >= hdxf::EntityType::Surface && t <= hdxf::EntityType::PlaneSurface;
}

std::string surface_type_name(hdxf::EntityType t) {
    switch (t) {
        case hdxf::EntityType::ExtrudedSurface: return "EXTRUDEDSURFACE";
        case hdxf::EntityType::LoftedSurface:   return "LOFTEDSURFACE";
        case hdxf::EntityType::RevolvedSurface: return "REVOLVEDSURFACE";
        case hdxf::EntityType::SweptSurface:    return "SWEPTSURFACE";
        case hdxf::EntityType::PlaneSurface:    return "PLANESURFACE";
        default:                                return "SURFACE";
    }
}

} // namespace

// AutoCAD's surfaces (LibreDWG's Surface drawing, SAT 20800) and helix (2007) in a new 2013
// drawing, ASCII and binary: AutoCAD's 2010 groups but AcDbModelerGeometry's 290 0 and the zero
// GUID (a body without a change GUID, B58), proxy graphics 160 8 (3Dlines), one ASM_Data record of
// SAB 21800 per surface, the helix's groups and polyline as AutoCAD's; read back with the same
// extents
TEST(surface_written_anew_2013) {
    if (!has_files({"libre/Surface_2004.dxf", "libre/Helix_2007.dxf"})) return;
    std::vector<uint8_t> surfaces = read_bytes(external("libre/Surface_2004.dxf"));
    std::vector<uint8_t> helix = read_bytes(external("libre/Helix_2007.dxf"));
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        std::vector<std::pair<hdxf::Point3, hdxf::Point3>> boxes;
        std::map<std::string, std::string> guids;
        for (const auto* data : {&surfaces, &helix}) {
            hdxf::Drawing src = load(*data);
            for (const auto& e : src.entities()) {
                if (!is_surface(e.type) && e.type != hdxf::EntityType::Helix) continue;
                if (is_surface(e.type)) guids[surface_type_name(e.type)] = e.as_surface().acis.guid;
                hdxf::Entity c = e;
                c.raw = hdxf::RawRecord();
                c.props.handle.clear();
                c.props.reactors.clear();       // handles of the source drawing
                hdxf::Point3 lo, hi;
                hdxf_test::assert_true(src.entity_extents(e, lo, hi), "the source's extents");
                boxes.push_back({lo, hi});
                dwg.add_entity(c);
            }
        }
        std::vector<uint8_t> out = dwg.save_to_memory(format);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "2013 surfaces and helix");
        std::vector<Record> after = records_of(out);
        std::vector<Record> theirs = records_of(surfaces);
        auto sab = asm_data(after);
        for (const char* type : {"EXTRUDEDSURFACE", "LOFTEDSURFACE", "REVOLVEDSURFACE", "SWEPTSURFACE", "PLANESURFACE"}) {
            Record ours = record_of_type(after, type);
            std::string why;
            hdxf_test::assert_true(same_body(record_of_type(theirs, type), ours, "AcDbSurface", why),
                                   std::string(type) + ": " + why);
            // The surfaces' change GUIDs, XDATA ACAD_OBJECT_CHANGE_GUID in the 2004 file (B60)
            std::string guid = guids.at(type);
            hdxf_test::assert_false(guid.empty(), std::string(type) + ": a change GUID");
            hdxf_test::assert_true(subclass_groups(ours, "AcDbModelerGeometry") == Record({{290, "1"}, {2, guid}}),
                                   std::string(type) + ": 290 and 2");
            hdxf_test::assert_false(value_of(ours, 1001) == "ACAD_OBJECT_CHANGE_GUID", std::string(type) + ": no GUID XDATA");
            hdxf_test::assert_true(proxy_of(ours) == std::vector<uint8_t>({8, 0, 0, 0, 0, 0, 0, 0}), std::string(type) + ": proxy graphics");
            hdxf_test::assert_eq(value_of(ours, 160), std::string("8"), std::string(type) + ": 160");
            int records = 0;
            for (const auto& [handle, bytes] : sab) {
                if (handle != value_of(ours, 5)) continue;
                ++records;
                hdxf_test::assert_true(bytes.size() > 31 && std::string(bytes.begin(), bytes.begin() + 15) == "ACIS BinaryFile" &&
                                           bytes[15] == 0x28 && bytes[16] == 0x55, std::string(type) + ": SAB 21800");
            }
            hdxf_test::assert_eq(records, 1, std::string(type) + ": its ASM_Data record");
        }
        hdxf_test::assert_eq(static_cast<int>(sab.size()), 5, "five ASM_Data records");
        for (const auto& r : after) {
            if (r[0].second != "CLASS" || (r[1].second != "HELIX" && r[1].second.find("SURFACE") == std::string::npos)) continue;
            // As AutoCAD's 2013 files (3Dlines, 3Dhelix) and its 2004 file: 0 but for these two
            bool all = r[1].second == "PLANESURFACE" || r[1].second == "HELIX";
            hdxf_test::assert_eq(value_of(r, 90), std::string(all ? "4095" : "0"), r[1].second + ": class flags");
        }
        Record ours = record_of_type(after, "HELIX");
        Record autocad = record_of_type(records_of(helix), "HELIX");
        std::string why;
        hdxf_test::assert_true(same_body(autocad, ours, "AcDbSpline", why), "HELIX: " + why);
        std::vector<hdxf::Point3> a = polyline_of(proxy_of(autocad)), b = polyline_of(proxy_of(ours));
        hdxf_test::assert_eq(static_cast<int>(b.size()), static_cast<int>(a.size()), "the helix's points");
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            hdxf_test::assert_true(near(a[i], b[i], 1e-12), "point " + std::to_string(i));
        }

        hdxf::Drawing back = load(out);
        size_t i = 0;
        for (const auto& e : back.entities()) {
            if (!is_surface(e.type) && e.type != hdxf::EntityType::Helix) continue;
            if (is_surface(e.type)) {
                hdxf_test::assert_true(e.as_surface().acis.lines.empty() && !e.as_surface().acis.sab.empty(), "read back as SAB");
            }
            hdxf::Point3 lo, hi;
            hdxf_test::assert_true(back.entity_extents(e, lo, hi) && i < boxes.size(), "extents read back");
            if (i < boxes.size()) {
                hdxf_test::assert_true(near(lo, boxes[i].first, 1e-9) && near(hi, boxes[i].second, 1e-9),
                                       "extents as the source's: " + std::to_string(i));
            }
            ++i;
        }
        hdxf_test::assert_eq(static_cast<int>(i), static_cast<int>(boxes.size()), "all read back");
    }
}

// AutoCAD's own 2013 surfaces (jscad's 3Dlines: four EXTRUDEDSURFACE with change GUIDs, ACIS in
// ACDSDATA) written anew: AutoCAD's records group for group — the reactor of each to its
// ACDBASSOCDEPENDENCY too, kept as read (plan 5.9; the associative network itself is phase
// 9's, B68) — and its ASM_Data byte for byte; and
// copied into a new 2013 drawing (their SAB and GUID with them): their groups from
// AcDbModelerGeometry on and their proxy graphics as AutoCAD's, their SAB AutoCAD's bytes
TEST(surface_2013_written_anew_as_autocad) {
    if (!has_files({"jscad/3Dlines.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("jscad/3Dlines.dxf"));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    std::vector<std::string> handles;
    for (auto& e : dwg.entities()) {
        if (!is_surface(e.type)) continue;
        e.raw = hdxf::RawRecord();
        handles.push_back(e.props.handle);
    }
    hdxf_test::assert_eq(static_cast<int>(handles.size()), 4, "four surfaces");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    std::vector<Record> after = records_of(out);
    for (const auto& h : handles) {
        Record a = record_by_handle(before, h), b = record_by_handle(after, h);
        hdxf_test::assert_true(!a.empty() && a == b, h + ": AutoCAD's record");
    }
    hdxf_test::assert_true(asm_data(after) == asm_data(before), "ACDSDATA's ASM_Data as AutoCAD's");

    hdxf::Drawing copy(hdxf::DxfVersion::R2013);
    for (const auto& e : dwg.entities()) {
        if (!is_surface(e.type)) continue;
        hdxf::Entity c = e;
        c.props.handle.clear();
        c.props.reactors.clear();
        copy.add_entity(c);
    }
    std::vector<uint8_t> copied = copy.save_to_memory();
    hdxf_test::assert_false(copied.empty(), copy.last_error());
    dxf_structure::check_structure(copied, "3Dlines' surfaces in a new drawing");
    std::vector<Record> ours = records_of(copied);
    auto theirs_sab = asm_data(before), ours_sab = asm_data(ours);
    hdxf_test::assert_eq(static_cast<int>(ours_sab.size()), 4, "four ASM_Data records");
    size_t n = 0;
    for (const auto& e : copy.entities()) {
        if (!is_surface(e.type)) continue;
        const std::string& source = handles.at(n++);
        Record a = record_by_handle(before, source), b = record_by_handle(ours, e.props.handle);
        std::string why;
        hdxf_test::assert_true(same_body(a, b, "AcDbModelerGeometry", why), e.props.handle + ": " + why);
        hdxf_test::assert_true(proxy_of(a) == proxy_of(b), e.props.handle + ": proxy graphics");
        std::vector<uint8_t> theirs_bytes, ours_bytes;
        for (const auto& [h, bytes] : theirs_sab) if (h == source) theirs_bytes = bytes;
        for (const auto& [h, bytes] : ours_sab) if (h == e.props.handle) ours_bytes = bytes;
        hdxf_test::assert_true(!ours_bytes.empty() && ours_bytes == theirs_bytes, e.props.handle + ": AutoCAD's SAB");
    }
}

// AutoCAD 2017's helices in 2013 (jscad's 3Dhelix and 2Dhelix): read and written back byte for
// byte; written anew their groups and 160 as AutoCAD's, the proxy graphics' polyline to 1e-12
TEST(helix_2013_written_anew_as_autocad) {
    for (const char* name : {"jscad/3Dhelix.dxf", "jscad/2Dhelix.dxf"}) {
        if (!has_files({name})) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        std::vector<uint8_t> same = dwg.save_to_memory();
        std::vector<uint8_t> lf;
        for (uint8_t b : in) if (b != '\r') lf.push_back(b);
        hdxf_test::assert_true(same == in || same == lf, std::string(name) + ": byte for byte");
        std::vector<Record> before = records_of(in);
        std::vector<std::string> handles;
        for (auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::Helix) continue;
            e.raw = hdxf::RawRecord();
            handles.push_back(e.props.handle);
        }
        hdxf_test::assert_eq(static_cast<int>(handles.size()), 2, std::string(name) + ": two helices");
        std::vector<Record> after = records_of(dwg.save_to_memory());
        for (const auto& h : handles) {
            std::string what = std::string(name) + " " + h;
            Record a = record_by_handle(before, h), b = record_by_handle(after, h);
            std::string why;
            hdxf_test::assert_true(same_body(a, b, "AcDbSpline", why), what + ": " + why);
            hdxf_test::assert_eq(value_of(b, 160), value_of(a, 160), what + ": 160");
            std::vector<hdxf::Point3> pa = polyline_of(proxy_of(a)), pb = polyline_of(proxy_of(b));
            hdxf_test::assert_true(!pa.empty() && pa.size() == pb.size(), what + ": the points");
            for (size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
                double scale = std::max({1.0, std::fabs(pa[i].x), std::fabs(pa[i].y), std::fabs(pa[i].z)});
                hdxf_test::assert_true(near(pa[i], pb[i], 1e-12 * scale), what + ": point " + std::to_string(i));
            }
        }
    }
}
