#include "test_framework.hpp"
#include "hdxf.hpp"
#include "dxf_structure.hpp"
#include <cmath>
#include <filesystem>
#include <string>

// ---------------------------------------------------------------------------
// The DXF 2000 entity types of phase 1, step 1.5 (doc/plans/2026-10-23-fas-1-dxf-2000.md):
// LWPOLYLINE, ELLIPSE, XLINE and RAY — read, built in memory, written as 2000, refused
// as R12, and their part of the extents.
//
// Expected values are read from ACadSharp's AutoCAD sample (checked with ezdxf), not
// from HDxf.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

const hdxf::Entity& entity(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return e;
    }
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) {
            if (e.props.handle == handle) return e;
        }
    }
    hdxf_test::assert_true(false, "entity " + handle + " not found");
    return dwg.entities().front();
}

hdxf::Drawing save_and_load(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    return back;
}

std::string text_of(hdxf::Drawing& dwg) {
    std::vector<uint8_t> out = dwg.save_to_memory(hdxf::DxfFormat::Ascii);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return hdxf_test::lf_text(out);
}

// The groups of the record with this handle, from its "0" on
std::vector<std::pair<int, std::string>> record(const std::string& dxf, const std::string& handle) {
    std::vector<std::pair<int, std::string>> groups;
    size_t pos = 0;
    auto line = [&]() {
        size_t end = dxf.find('\n', pos);
        std::string s = dxf.substr(pos, end - pos);
        pos = end == std::string::npos ? dxf.size() : end + 1;
        while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        return s;
    };
    std::vector<std::pair<int, std::string>> current;
    bool found = false;
    while (pos < dxf.size()) {
        int code = std::atoi(line().c_str());
        std::string value = line();
        if (code == 0) {
            if (found) break;
            current.clear();
        }
        current.push_back({code, dxf_structure::number_text(code, value)});
        if (code == 5 && value == handle && current.size() == 2) found = true;
    }
    return found ? current : groups;
}

hdxf::Point3 header_point(const hdxf::Drawing& dwg, const std::string& name) {
    const hdxf::HeaderVar* h = dwg.find_header(name);
    hdxf_test::assert_true(h != nullptr, name);
    hdxf::Point3 p;
    for (const auto& [code, value] : h->values) {
        if (code == 10) p.x = std::stod(value);
        if (code == 20) p.y = std::stod(value);
        if (code == 30) p.z = std::stod(value);
    }
    return p;
}

void near(double a, double b, double tolerance) {
    hdxf_test::assert_true(std::fabs(a - b) < tolerance, std::to_string(a) + " != " + std::to_string(b));
}

void check_sample(const std::string& file) {
    std::filesystem::path path = external(file);
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("external test file missing");
        return;
    }
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(path), dwg.last_error());

    const hdxf::Entity& ellipse = entity(dwg, "321");
    hdxf_test::assert_true(ellipse.type == hdxf::EntityType::Ellipse, "ELLIPSE");
    const auto& el = ellipse.as_ellipse();
    near(el.center.x, 68.00051689353697, 1e-12);
    near(el.center.y, 4.185210497355278, 1e-12);
    near(el.major_axis.x, -5.101255306291786, 1e-12);
    near(el.major_axis.y, 0.0, 1e-12);
    near(el.ratio, 0.6403727385155383, 1e-12);
    near(el.start_param, 0.0, 1e-12);
    near(el.end_param, 6.283185307179586, 1e-12);
    near(ellipse.props.extrusion.z, 1.0, 1e-12);

    const hdxf::Entity& xline = entity(dwg, "389");
    hdxf_test::assert_true(xline.type == hdxf::EntityType::Xline, "XLINE");
    near(xline.as_xline().base_point.x, -6.805688358374368, 1e-12);
    near(xline.as_xline().base_point.y, -5.110573038768279, 1e-12);
    near(xline.as_xline().direction.y, 1.0, 1e-12);

    const hdxf::Entity& ray = entity(dwg, "38A");
    hdxf_test::assert_true(ray.type == hdxf::EntityType::Ray, "RAY");
    near(ray.as_ray().base_point.x, -15.4176098639582, 1e-12);
    near(ray.as_ray().base_point.y, -4.203542186472134, 1e-12);
    near(ray.as_ray().direction.x, 0.0, 1e-12);
    near(ray.as_ray().direction.y, 1.0, 1e-12);

    // Widths per vertex (40/41), bulges
    const hdxf::Entity& widths = entity(dwg, "2E5");
    hdxf_test::assert_true(widths.type == hdxf::EntityType::LwPolyline, "LWPOLYLINE");
    const auto& w = widths.as_lwpolyline();
    hdxf_test::assert_false(w.is_closed(), "open");
    near(static_cast<int>(w.vertices.size()), 5, 1e-12);
    near(w.vertices[0].position.x, 35.30083172412064, 1e-12);
    near(w.vertices[0].bulge, 2.127776220378056, 1e-12);
    near(w.vertices[1].bulge, 0.3079358491434644, 1e-12);
    near(w.vertices[2].bulge, 0.0, 1e-12);
    near(w.vertices[4].position.y, 8.330067202309187, 1e-12);
    near(w.vertices[4].start_width, 3.669609517619411, 1e-12);
    near(w.vertices[4].end_width, 5.080563928030537, 1e-12);

    // Closed, with bulges
    const auto& closed = entity(dwg, "782").as_lwpolyline();
    hdxf_test::assert_true(closed.is_closed(), "closed");
    near(static_cast<int>(closed.vertices.size()), 7, 1e-12);
    near(closed.vertices[6].bulge, -0.0476105397037214, 1e-12);

    // One width for all vertices (43 0.15), in a block
    const hdxf::Entity& wide = entity(dwg, "4A6");
    hdxf_test::assert_true(wide.type == hdxf::EntityType::LwPolyline, "LWPOLYLINE in a block");
    for (const auto& v : wide.as_lwpolyline().vertices) {
        near(v.start_width, 0.15, 1e-12);
        near(v.end_width, 0.15, 1e-12);
    }
    hdxf_test::assert_eq(wide.props.linetype, "ByBlock");
}

} // namespace

TEST(entities_2000_read_ascii)  { check_sample("acadsharp/sample_AC1015_ascii.dxf"); }
TEST(entities_2000_read_binary) { check_sample("acadsharp/sample_AC1015_binary.dxf"); }
TEST(entities_2000_read_2018)   { check_sample("acadsharp/sample_AC1032_ascii.dxf"); }

// Built in memory, written as 2000 and read back, ASCII and binary
TEST(entities_2000_built_roundtrip) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        hdxf::Entity& lw = dwg.add_lwpolyline({{0, 0}, {10, 0}, {10, 5}}, true, "Walls");
        lw.as_lwpolyline().vertices[1].bulge = 0.5;
        lw.as_lwpolyline().vertices[2].start_width = 1.0;
        lw.as_lwpolyline().vertices[2].end_width = 2.0;
        lw.as_lwpolyline().elevation = 3.0;
        lw.as_lwpolyline().flags |= 128;
        lw.props.thickness = 0.5;
        lw.props.extrusion = {0, 0, -1};
        dwg.add_ellipse({5, 5, 1}, {4, 0, 0}, 0.5, 0.25, 3.0).props.color = hdxf::Color::red();
        dwg.add_xline({1, 2, 3}, {0, 3, 4});
        dwg.add_ray({-1, -2, 0}, {1, 0, 0});

        hdxf::Drawing back = save_and_load(dwg, format);
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 4);
        const hdxf::Entity& l = back.entities()[0];
        hdxf_test::assert_true(l.type == hdxf::EntityType::LwPolyline, "LWPOLYLINE");
        const auto& d = l.as_lwpolyline();
        hdxf_test::assert_eq(d.flags, 129);
        hdxf_test::assert_eq(d.elevation, 3.0);
        hdxf_test::assert_eq(static_cast<int>(d.vertices.size()), 3);
        hdxf_test::assert_eq(d.vertices[1].position.x, 10.0);
        hdxf_test::assert_eq(d.vertices[1].bulge, 0.5);
        hdxf_test::assert_eq(d.vertices[0].start_width, 0.0);
        hdxf_test::assert_eq(d.vertices[2].start_width, 1.0);
        hdxf_test::assert_eq(d.vertices[2].end_width, 2.0);
        hdxf_test::assert_eq(l.props.layer, "Walls");
        hdxf_test::assert_eq(l.props.thickness, 0.5);
        hdxf_test::assert_eq(l.props.extrusion.z, -1.0);

        const auto& e = back.entities()[1].as_ellipse();
        hdxf_test::assert_eq(e.center.z, 1.0);
        hdxf_test::assert_eq(e.major_axis.x, 4.0);
        hdxf_test::assert_eq(e.ratio, 0.5);
        hdxf_test::assert_eq(e.start_param, 0.25);
        hdxf_test::assert_eq(e.end_param, 3.0);
        hdxf_test::assert_eq(back.entities()[1].props.color.aci, 1);

        // add_xline() keeps the direction a unit vector, as AutoCAD does
        const auto& x = back.entities()[2].as_xline();
        hdxf_test::assert_eq(x.base_point.z, 3.0);
        hdxf_test::assert_eq(x.direction.y, 0.6);
        hdxf_test::assert_eq(x.direction.z, 0.8);
        hdxf_test::assert_true(back.entities()[3].type == hdxf::EntityType::Ray, "RAY");
        hdxf_test::assert_eq(back.entities()[3].as_ray().base_point.y, -2.0);
    }
}

// The groups as AutoCAD writes them: one width (43) or each vertex's 40/41, a bulge only
// where there is one; ELLIPSE with its normal (210) in AcDbEllipse
TEST(entities_2000_groups_as_autocad) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::Entity& same = dwg.add_lwpolyline({{0, 0}, {1, 1}});
    for (auto& v : same.as_lwpolyline().vertices) v.start_width = v.end_width = 0.15;
    hdxf::Entity& varied = dwg.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}});
    varied.as_lwpolyline().vertices[1].start_width = 0.2;
    varied.as_lwpolyline().vertices[1].bulge = -1.0;
    dwg.add_ellipse({0, 0, 0}, {2, 0, 0}, 0.5);
    std::string dxf = text_of(dwg);
    std::vector<hdxf::Entity>& ents = dwg.entities();

    using G = std::vector<std::pair<int, std::string>>;
    G a = record(dxf, ents[0].props.handle);
    G expected_a = {{0, "LWPOLYLINE"}, {5, ents[0].props.handle}, {330, a[2].second},
                    {100, "AcDbEntity"}, {8, "0"}, {100, "AcDbPolyline"}, {90, "2"}, {70, "0"},
                    {43, "0.15"}, {10, "0.0"}, {20, "0.0"}, {10, "1.0"}, {20, "1.0"}};
    hdxf_test::assert_true(a == expected_a, "constant width");

    G b = record(dxf, ents[1].props.handle);
    std::vector<int> codes;
    for (size_t i = 6; i < b.size(); ++i) codes.push_back(b[i].first);
    std::vector<int> expected_b = {90, 70, 10, 20, 40, 41, 10, 20, 40, 41, 42, 10, 20, 40, 41};
    hdxf_test::assert_true(codes == expected_b, "widths per vertex");

    G c = record(dxf, ents[2].props.handle);
    codes.clear();
    for (size_t i = 6; i < c.size(); ++i) codes.push_back(c[i].first);
    std::vector<int> expected_c = {10, 20, 30, 11, 21, 31, 210, 220, 230, 40, 41, 42};
    hdxf_test::assert_eq(c[5].second, "AcDbEllipse");
    hdxf_test::assert_true(codes == expected_c, "ELLIPSE groups");
}

// B11 and B98: a type R12 does not have cannot be written as R12 — in ENTITIES or in a block (the types
// of step 1.6 too). The error names the first version that has it (format profile 12): R13 for the
// types it introduced, R14 for LWPOLYLINE, HATCH and IMAGE
TEST(entities_2000_refused_as_r12) {
    struct Case { const char* name; const char* first; void (*add)(hdxf::Drawing&); };
    const Case cases[] = {
        {"LWPOLYLINE", "R14", [](hdxf::Drawing& d) { d.add_lwpolyline({{0, 0}, {1, 1}}); }},
        {"ELLIPSE", "R13", [](hdxf::Drawing& d) { d.add_ellipse({0, 0, 0}, {1, 0, 0}, 0.5); }},
        {"XLINE", "R13", [](hdxf::Drawing& d) { d.add_xline({0, 0, 0}, {1, 0, 0}); }},
        {"RAY", "R13", [](hdxf::Drawing& d) { d.add_ray({0, 0, 0}, {1, 0, 0}); }},
        {"MTEXT", "R13", [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "M"); }},
        {"TOLERANCE", "R13", [](hdxf::Drawing& d) { d.add_tolerance({0, 0, 0}, "{\\Fgdt;j}"); }},
        {"LEADER", "R13", [](hdxf::Drawing& d) { d.add_leader({{0, 0, 0}, {1, 1, 0}}); }},
        {"SPLINE", "R13", [](hdxf::Drawing& d) { d.add_spline({{0, 0, 0}, {1, 1, 0}, {2, 0, 0}, {3, 1, 0}}); }},
        {"HATCH", "R14", [](hdxf::Drawing& d) { d.add_hatch({{0, 0}, {1, 0}, {1, 1}}); }},
        {"MLINE", "R13", [](hdxf::Drawing& d) { d.add_mline({{0, 0, 0}, {1, 0, 0}}); }},
        {"IMAGE", "R14", [](hdxf::Drawing& d) { d.add_image("a.png", {0, 0, 0}, {10, 10}, 1.0); }},
        {"3DSOLID", "R13", [](hdxf::Drawing& d) { d.add_acis(hdxf::EntityType::Solid3D, {}); }},
    };
    for (const auto& c : cases) {
        hdxf::Drawing dwg;
        c.add(dwg);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), c.name);
        hdxf_test::assert_eq(dwg.last_error(), std::string(c.name) + " requires DXF " + c.first + " or later");
        dwg.set_version(hdxf::DxfVersion::R2000);
        hdxf_test::assert_false(dwg.save_to_memory().empty(), std::string(c.name) + " as 2000");
    }
    hdxf::Drawing dwg;
    hdxf::Drawing source(hdxf::DxfVersion::R2000);
    source.add_ray({0, 0, 0}, {1, 0, 0});
    dwg.add_block("Arrow").entities.push_back(source.entities().front());
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "in a block");
    hdxf_test::assert_eq(dwg.last_error(), "RAY requires DXF R13 or later");
}

// Extents: an ellipse exactly, a light polyline with its bulges at its elevation in its
// OCS; XLINE and RAY are unbounded and add nothing. Widths are not part of the extents —
// AutoCAD's bounding box of a wide polyline is its centre line.
TEST(entities_2000_extents) {
    auto extents = [](hdxf::Drawing& dwg, hdxf::Point3& min, hdxf::Point3& max) {
        hdxf::Drawing back = save_and_load(dwg);
        min = header_point(back, "$EXTMIN");
        max = header_point(back, "$EXTMAX");
    };
    hdxf::Point3 min, max;
    {
        // The sample's ellipse (ezdxf: 62.899261587245185, 0.9185056669982861 …)
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_ellipse({68.00051689353697, 4.185210497355278, 0}, {-5.101255306291786, 0, 0},
                        0.6403727385155383);
        extents(dwg, min, max);
        near(min.x, 62.899261587245185, 1e-12);
        near(min.y, 0.9185056669982861, 1e-12);
        near(max.x, 73.10177219982876, 1e-12);
        near(max.y, 7.451915327712271, 1e-12);
    }
    {
        // Half an ellipse whose normal is -Z: the minor axis points to -Y
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_ellipse({0, 0, 0}, {10, 0, 0}, 0.5, 0.0, 3.141592653589793).props.extrusion = {0, 0, -1};
        extents(dwg, min, max);
        near(min.x, -10.0, 1e-12);
        near(min.y, -5.0, 1e-12);
        near(max.x, 10.0, 1e-12);
        near(max.y, 0.0, 1e-12);
    }
    {
        // A closed polyline: a semicircle under (0,0)–(10,0), back straight, at z = 3;
        // its widths are not counted
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        hdxf::Entity& lw = dwg.add_lwpolyline({{0, 0}, {10, 0}}, true);
        lw.as_lwpolyline().vertices[0].bulge = 1.0;
        lw.as_lwpolyline().elevation = 3.0;
        for (auto& v : lw.as_lwpolyline().vertices) v.start_width = v.end_width = 2.0;
        dwg.add_xline({-100, -100, 0}, {1, 1, 0});
        dwg.add_ray({100, 100, 0}, {1, 0, 0});
        extents(dwg, min, max);
        near(min.x, 0.0, 1e-12);
        near(min.y, -5.0, 1e-12);
        near(min.z, 3.0, 1e-12);
        near(max.x, 10.0, 1e-12);
        near(max.y, 0.0, 1e-12);
        near(max.z, 3.0, 1e-12);
    }
    {
        // Only unbounded lines: an empty drawing's extents
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_xline({0, 0, 0}, {1, 0, 0});
        extents(dwg, min, max);
        hdxf_test::assert_eq(min.x, 1e20);
        hdxf_test::assert_eq(max.x, -1e20);
    }
}
