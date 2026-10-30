#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// ---------------------------------------------------------------------------
// SPLINE and HATCH in DXF 2000, phase 1 step 1.7 (doc/plans/2026-10-23-fas-1-dxf-2000.md):
// read, built in memory, written as 2000, a drawing read from 2000 changed and written
// back (B12: a changed hatch body is written whole; B19: boundary objects ↔ hatch), and
// their part of the extents.
//
// Expected values are read from ACadSharp's AutoCAD sample (checked with ezdxf), not
// from HDxf.
// ---------------------------------------------------------------------------

namespace {

using Groups = std::vector<std::pair<int, std::string>>;

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

const hdxf::Entity& entity(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return e;
    }
    hdxf_test::assert_true(false, "entity " + handle + " not found");
    return dwg.entities().front();
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The groups of the record with this handle, from its "0" on (ASCII)
Groups record(const std::vector<uint8_t>& data, const std::string& handle) {
    std::string dxf(data.begin(), data.end());
    size_t pos = 0;
    auto line = [&]() {
        size_t end = dxf.find('\n', pos);
        std::string s = dxf.substr(pos, end - pos);
        pos = end == std::string::npos ? dxf.size() : end + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        return s;
    };
    Groups current;
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
    return found ? current : Groups{};
}

std::vector<std::string> values(const Groups& g, int code) {
    std::vector<std::string> out;
    for (const auto& [c, v] : g) if (c == code) out.push_back(v);
    return out;
}

// From the first group with this code on
Groups from(const Groups& g, int code) {
    Groups out;
    bool in = false;
    for (const auto& x : g) {
        in = in || x.first == code;
        if (in) out.push_back(x);
    }
    return out;
}

void near(double a, double b, const std::string& what, double tolerance = 1e-12) {
    hdxf_test::assert_true(std::fabs(a - b) < tolerance, what + ": " + std::to_string(a) + " != " +
                                                         std::to_string(b));
}

bool has_sample() {
    if (std::filesystem::exists(external("acadsharp/sample_AC1015_ascii.dxf"))) return true;
    hdxf_test::skip("external test file missing");
    return false;
}

void check_sample(const std::string& file) {
    std::filesystem::path path = external(file);
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("external test file missing");
        return;
    }
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(path), dwg.last_error());

    const hdxf::Entity& spline = entity(dwg, "433");
    hdxf_test::assert_true(spline.type == hdxf::EntityType::Spline, "SPLINE");
    const auto& s = spline.as_spline();
    hdxf_test::assert_eq(s.flags, 8, "planar");
    hdxf_test::assert_eq(s.degree, 3);
    hdxf_test::assert_eq(static_cast<int>(s.knots.size()), 8);
    hdxf_test::assert_eq(s.knots[3], 0.0);
    hdxf_test::assert_eq(s.knots[4], 1.0);
    hdxf_test::assert_true(s.weights.empty(), "not rational");
    hdxf_test::assert_eq(static_cast<int>(s.control_points.size()), 4);
    near(s.control_points[1].y, 11.98610420237003, "control point");
    near(s.control_points[3].x, 259.2245511100041, "control point");
    hdxf_test::assert_true(s.fit_points.empty(), "no fit points");
    near(s.knot_tolerance, 1e-9, "42", 1e-20);
    near(s.control_tolerance, 1e-10, "43", 1e-20);
    hdxf_test::assert_eq(entity(dwg, "434").as_spline().flags, 1160);

    // Pattern ANSI31 in an edge path of four lines, associative with LWPOLYLINE 353
    const hdxf::Entity& ansi = entity(dwg, "35A");
    hdxf_test::assert_true(ansi.type == hdxf::EntityType::Hatch, "HATCH");
    const auto& h = ansi.as_hatch();
    hdxf_test::assert_eq(h.pattern_name, "ANSI31");
    hdxf_test::assert_false(h.solid, "pattern");
    hdxf_test::assert_true(h.associative, "associative");
    hdxf_test::assert_eq(h.style, 1);
    hdxf_test::assert_eq(h.pattern_type, 1);
    hdxf_test::assert_eq(h.pattern_scale, 1.0);
    hdxf_test::assert_eq(static_cast<int>(h.paths.size()), 1);
    const auto& edge_path = h.paths[0];
    hdxf_test::assert_eq(edge_path.flags, 1);
    hdxf_test::assert_false(edge_path.is_polyline(), "edges");
    hdxf_test::assert_eq(static_cast<int>(edge_path.edges.size()), 4);
    hdxf_test::assert_true(edge_path.edges[1].type == hdxf::HatchEdgeType::Line, "line");
    near(edge_path.edges[1].end.y, 13.71006874482629, "edge end");
    hdxf_test::assert_eq(static_cast<int>(edge_path.source_handles.size()), 1);
    hdxf_test::assert_eq(edge_path.source_handles[0], "353");
    hdxf_test::assert_eq(static_cast<int>(h.pattern_lines.size()), 1);
    hdxf_test::assert_eq(h.pattern_lines[0].angle, 45.0);
    near(h.pattern_lines[0].base_point.x, 2.750381723718078, "base point");
    near(h.pattern_lines[0].offset.x, -0.0883883476483185, "offset");
    hdxf_test::assert_true(h.pattern_lines[0].dashes.empty(), "solid lines");
    hdxf_test::assert_eq(static_cast<int>(h.seed_points.size()), 1);
    near(h.seed_points[0].x, 2.750381723718078, "seed");

    // Solid fill, associative
    const auto& solid = entity(dwg, "371").as_hatch();
    hdxf_test::assert_true(solid.solid, "solid");
    hdxf_test::assert_eq(solid.pattern_name, "SOLID,_O");
    hdxf_test::assert_true(solid.pattern_lines.empty(), "no lines");
    hdxf_test::assert_eq(solid.paths[0].source_handles[0], "36F");

    // A polyline path (92 = 7), 78 pattern lines with dashes, not associative
    const auto& poly = entity(dwg, "BBB").as_hatch();
    hdxf_test::assert_false(poly.associative, "not associative");
    hdxf_test::assert_eq(poly.style, 1);
    hdxf_test::assert_eq(poly.pattern_type, 2, "custom");
    hdxf_test::assert_eq(poly.pattern_scale, 0.1);
    hdxf_test::assert_eq(poly.paths[0].flags, 7);
    hdxf_test::assert_true(poly.paths[0].is_polyline(), "polyline");
    hdxf_test::assert_true(poly.paths[0].closed, "closed");
    hdxf_test::assert_eq(static_cast<int>(poly.paths[0].vertices.size()), 4);
    near(poly.paths[0].vertices[1].position.x, 14.84707159894242, "vertex");
    hdxf_test::assert_eq(static_cast<int>(poly.pattern_lines.size()), 78);
    near(poly.pattern_lines[0].angle, 71.5650482, "line angle");
    hdxf_test::assert_eq(static_cast<int>(poly.pattern_lines[0].dashes.size()), 2);
    near(poly.pattern_lines[0].dashes[1], -7.27323837, "dash");
    near(poly.seed_points[0].y, -192.9216984220589, "seed");
    hdxf_test::assert_eq(static_cast<int>(entity(dwg, "BC0").as_hatch().pattern_lines.size()), 69);
}

} // namespace

TEST(curves_2000_read_ascii)  { check_sample("acadsharp/sample_AC1015_ascii.dxf"); }
TEST(curves_2000_read_binary) { check_sample("acadsharp/sample_AC1015_binary.dxf"); }
TEST(curves_2000_read_2018)   { check_sample("acadsharp/sample_AC1032_ascii.dxf"); }

// Built in memory and read back; the groups as AutoCAD writes them
TEST(curves_2000_built) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_spline({{0, 0, 0}, {5, 10, 0}, {10, 0, 0}, {15, 10, 0}, {20, 0, 0}});
        hdxf::Entity& rational = dwg.add_spline({{0, 0, 0}, {10, 10, 0}, {20, 0, 0}}, 2);
        rational.as_spline().flags |= 4;
        rational.as_spline().weights = {1.0, 0.7071067811865476, 1.0};
        rational.as_spline().fit_points = {{0, 0, 0}, {20, 0, 0}};
        rational.as_spline().start_tangent = {1, 1, 0};

        dwg.add_lwpolyline({{0, 20}, {20, 20}, {20, 40}, {0, 40}}, true);
        dwg.add_hatch({{30, 20}, {50, 20}, {50, 40}}).as_hatch().paths[0].vertices[1].bulge = 0.5;
        hdxf::Entity& pattern = dwg.add_hatch({});
        auto& ph = pattern.as_hatch();
        ph.solid = false;
        ph.pattern_name = "ANSI31";
        ph.pattern_scale = 2.0;
        ph.pattern_lines.push_back({45.0, {0, 0}, {-0.1767766952966369, 0.1767766952966369}, {}});
        ph.associative = true;
        hdxf::HatchBoundaryPath edges;
        edges.flags = 1;
        hdxf::HatchEdge line;
        line.start = {0, 20};
        line.end = {20, 20};
        hdxf::HatchEdge arc;
        arc.type = hdxf::HatchEdgeType::Arc;
        arc.center = {20, 30};
        arc.radius = 10;
        arc.start_angle = 270;
        arc.end_angle = 90;
        hdxf::HatchEdge ellipse;
        ellipse.type = hdxf::HatchEdgeType::Ellipse;
        ellipse.center = {10, 40};
        ellipse.major_axis = {10, 0};
        ellipse.ratio = 0.5;
        ellipse.start_angle = 0;
        ellipse.end_angle = 180;
        hdxf::HatchEdge curve;
        curve.type = hdxf::HatchEdgeType::Spline;
        curve.degree = 2;
        curve.knots = {0, 0, 0, 1, 1, 1};
        curve.control_points = {{0, 40}, {-5, 30}, {0, 20}};
        edges.edges = {line, arc, ellipse, curve};
        ph.paths = {edges};
        ph.seed_points = {{10, 30}};

        // The boundary object gets its handle on the first save; then it is the source
        save(dwg, format);
        std::string boundary_handle = dwg.entities()[2].props.handle;
        dwg.entities()[4].as_hatch().paths[0].source_handles = {boundary_handle};

        std::vector<uint8_t> out = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "curves");
        hdxf::Drawing back = load(out);
        const auto& s = back.entities()[0].as_spline();
        hdxf_test::assert_eq(static_cast<int>(s.knots.size()), 9);
        hdxf_test::assert_eq(s.knots[4], 0.5);
        hdxf_test::assert_eq(s.control_points[3].x, 15.0);
        const auto& r = back.entities()[1].as_spline();
        hdxf_test::assert_true(r.is_rational(), "rational");
        hdxf_test::assert_eq(r.weights[1], 0.7071067811865476);
        hdxf_test::assert_eq(static_cast<int>(r.fit_points.size()), 2);
        hdxf_test::assert_eq(r.start_tangent.y, 1.0);

        const auto& f = back.entities()[3].as_hatch();
        hdxf_test::assert_true(f.solid, "solid");
        hdxf_test::assert_eq(f.paths[0].vertices[1].bulge, 0.5);
        hdxf_test::assert_eq(f.paths[0].vertices[2].position.y, 40.0);
        const auto& p = back.entities()[4].as_hatch();
        hdxf_test::assert_false(p.solid, "pattern");
        hdxf_test::assert_eq(p.pattern_scale, 2.0);
        hdxf_test::assert_eq(static_cast<int>(p.paths[0].edges.size()), 4);
        hdxf_test::assert_eq(p.paths[0].edges[1].radius, 10.0);
        hdxf_test::assert_eq(p.paths[0].edges[1].start_angle, 270.0);
        hdxf_test::assert_eq(p.paths[0].edges[2].ratio, 0.5);
        hdxf_test::assert_true(p.paths[0].edges[3].type == hdxf::HatchEdgeType::Spline, "spline edge");
        hdxf_test::assert_eq(p.paths[0].edges[3].control_points[1].x, -5.0);
        hdxf_test::assert_eq(p.paths[0].source_handles.at(0), boundary_handle);
        hdxf_test::assert_eq(p.seed_points[0].y, 30.0);
        if (format == hdxf::DxfFormat::Binary) continue;

        // The boundary has the hatch as reactor
        Groups b = record(out, boundary_handle);
        hdxf_test::assert_eq(b[2].second, "{ACAD_REACTORS");
        hdxf_test::assert_eq(b[3].second, back.entities()[4].props.handle);

        // Solid fill: no pattern groups between 76 and 98
        Groups solid = from(record(out, back.entities()[3].props.handle), 75);
        std::vector<int> codes;
        for (const auto& g : solid) codes.push_back(g.first);
        hdxf_test::assert_true(codes == std::vector<int>({75, 76, 98}), "solid: 75, 76, 98");
        Groups solid_head = record(out, back.entities()[3].props.handle);
        std::vector<int> head;
        for (const auto& g : from(solid_head, 10)) head.push_back(g.first);
        std::vector<int> expected = {10, 20, 30, 210, 220, 230, 2, 70, 71, 91, 92, 72, 73, 93, 10, 20,
                                     42, 10, 20, 42, 10, 20, 42, 97, 75, 76, 98};
        hdxf_test::assert_true(head == expected, "solid hatch groups");

        Groups spline = from(record(out, back.entities()[1].props.handle), 210);
        codes.clear();
        for (const auto& g : spline) codes.push_back(g.first);
        std::vector<int> expected_spline = {210, 220, 230, 70, 71, 72, 73, 74, 42, 43, 44, 12, 22, 32,
                                            40, 40, 40, 40, 40, 40, 10, 20, 30, 41, 10, 20, 30, 41,
                                            10, 20, 30, 41, 11, 21, 31, 11, 21, 31};   // weights as AutoCAD 2018 (plan 5.9)
        hdxf_test::assert_true(codes == expected_spline, "SPLINE groups");
    }
}

// A drawing read from 2000, changed (B12, B19)
TEST(curves_2000_changes) {
    if (!has_sample()) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_ascii.dxf"));
    {
        // A hatch's boundary moved: its body is written whole from 91 on; a spline's
        // control point moved: its list written whole, the counts as they were
        hdxf::Drawing dwg = load(in);
        for (auto& e : dwg.entities()) {
            if (e.props.handle == "35A") e.as_hatch().paths[0].edges[0].start.x = 100.0;
            if (e.props.handle == "433") e.as_spline().control_points[1].y = 12.0;
        }
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "curves changed");
        Groups hatch = record(out, "35A");
        Groups hatch_before = record(in, "35A");
        hdxf_test::assert_eq(static_cast<int>(hatch.size()), static_cast<int>(hatch_before.size()));
        for (size_t i = 0; i < hatch.size(); ++i) {
            hdxf_test::assert_eq(hatch[i].first, hatch_before[i].first, "the same groups");
        }
        hdxf_test::assert_eq(std::strtod(values(from(hatch, 91), 10).at(0).c_str(), nullptr), 100.0, "moved");
        hdxf_test::assert_eq(values(hatch, 1001).at(0), "GradientColor1ACI", "XDATA as it was");
        Groups spline = record(out, "433");
        hdxf_test::assert_eq(std::strtod(values(spline, 20).at(1).c_str(), nullptr), 12.0, "control point");
        hdxf_test::assert_eq(values(spline, 20).at(0), values(record(in, "433"), 20).at(0));
        hdxf_test::assert_eq(static_cast<int>(spline.size()), static_cast<int>(record(in, "433").size()));
    }
    {
        // The boundary LWPOLYLINE 353 removed: the hatch no longer points to it and is no
        // longer associative, as AutoCAD does when the boundary is erased
        hdxf::Drawing dwg = load(in);
        auto& ents = dwg.entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [](const hdxf::Entity& e) { return e.props.handle == "353"; }),
                   ents.end());
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "boundary removed");
        Groups hatch = record(out, "35A");
        hdxf_test::assert_eq(values(hatch, 71).at(0), "0", "not associative");
        hdxf_test::assert_eq(values(hatch, 97).at(0), "0", "no sources");
        for (const auto& [code, value] : from(hatch, 91)) hdxf_test::assert_true(code != 330, "no 330");
        // The draw order (SORTENTSTABLE 355) loses the pair 331 353 / 5 35A, and nothing
        // is left pointing nowhere
        Groups order = record(out, "355");
        hdxf_test::assert_eq(static_cast<int>(order.size()), static_cast<int>(record(in, "355").size()) - 2);
        for (const auto& [code, value] : order) hdxf_test::assert_true(value != "353", "353 not in the draw order");
        hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), "no pointer set to none");
    }
    {
        // The hatch removed: its boundary no longer has it as reactor
        hdxf::Drawing dwg = load(in);
        auto& ents = dwg.entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [](const hdxf::Entity& e) { return e.props.handle == "35A"; }),
                   ents.end());
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "hatch removed");
        for (const auto& [code, value] : record(out, "353")) hdxf_test::assert_true(value != "35A", "no reactor");
        hdxf_test::assert_eq(values(record(out, "36C"), 330).at(0), "36E", "other reactors kept");
    }
}

// Extents: a spline by its curve, a hatch by its boundary (a clockwise arc edge measured
// clockwise)
TEST(curves_2000_extents) {
    auto extents = [](hdxf::Drawing& dwg, hdxf::Point3& min, hdxf::Point3& max) {
        hdxf::Drawing back = load(save(dwg));
        for (const char* name : {"$EXTMIN", "$EXTMAX"}) {
            hdxf::Point3& p = std::string(name) == "$EXTMIN" ? min : max;
            for (const auto& [c, v] : back.find_header(name)->values) {
                if (c == 10) p.x = std::stod(v);
                if (c == 20) p.y = std::stod(v);
                if (c == 30) p.z = std::stod(v);
            }
        }
    };
    hdxf::Point3 min, max;
    {
        // A quadratic Bézier: its top is at t = 1/2, y = 5 (the control point is at 10)
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_spline({{0, 0, 0}, {5, 10, 0}, {10, 0, 0}}, 2);
        extents(dwg, min, max);
        near(min.x, 0.0, "x");
        near(max.x, 10.0, "x");
        near(max.y, 5.0, "the curve, not the control point");
    }
    {
        // Edges: a line (0,0)–(10,0) and a clockwise arc around (10,5) r 5 from 0° to 90°
        // as stored — measured clockwise: from (15,5) down to (10,0), below the centre
        // (counterclockwise it would be above, up to y = 10)
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        hdxf::Entity& h = dwg.add_hatch({});
        hdxf::HatchBoundaryPath path;
        path.flags = 1;
        hdxf::HatchEdge line;
        line.start = {0, 0};
        line.end = {10, 0};
        hdxf::HatchEdge arc;
        arc.type = hdxf::HatchEdgeType::Arc;
        arc.center = {10, 5};
        arc.radius = 5;
        arc.start_angle = 0;
        arc.end_angle = 90;
        arc.counterclockwise = false;
        path.edges = {line, arc};
        h.as_hatch().paths = {path};
        h.as_hatch().elevation = 2.0;
        extents(dwg, min, max);
        near(min.x, 0.0, "x");
        near(max.x, 15.0, "x");
        near(min.y, 0.0, "y");
        near(max.y, 5.0, "clockwise: below the centre");
        near(min.z, 2.0, "elevation");
    }
}
