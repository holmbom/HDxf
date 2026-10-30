#include "test_support.hpp"

#include <algorithm>

using namespace acis_test;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

// ---------------------------------------------------------------------------
// B-splines: exact surfaces, curves and pcurves, in the forms AutoCAD writes them
// ---------------------------------------------------------------------------

namespace {

// The document's text on one line: line breaks and runs of spaces are one space
std::string flat(const acis::SatDocument& document) {
    std::string out;
    bool space = false;
    for (char c : document.text()) {
        if (c == '\n' || c == ' ') {
            space = true;
            continue;
        }
        if (space && !out.empty()) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

struct PatchOptions {
    bool rational = false;
    acis::Singularity singular_u = acis::Singularity::None;
};

// A sheet of one NURBS patch: three control points across (the middle one raised) and two up,
// four edges with a pcurve each. Bottom and top are B-spline curves, the sides lines.
acis::Result<acis::SatDocument> build_patch_sheet(const PatchOptions& options = PatchOptions{}) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));

    acis::BSplineSurfaceGeometry patch;
    patch.degree_u = 1;
    patch.degree_v = 1;
    patch.count_u = 3;
    patch.count_v = 2;
    patch.knots_u = {0, 0, 0.5, 1, 1};
    patch.knots_v = {0, 0, 1, 1};
    patch.points = {{0, 0, 0}, {10, 0, 2}, {20, 0, 0}, {0, 10, 0}, {10, 10, 2}, {20, 10, 0}};
    if (options.rational) patch.weights.assign(6, 1.0);
    patch.singular_u = options.singular_u;
    acis::FaceDefinition face_definition;
    face_definition.surface = b.add_surface(patch);
    acis::LoopId loop = b.add_loop(b.add_face(shell, face_definition), acis::LoopRole::Boundary);

    acis::VertexId a = b.add_vertex({0, 0, 0});
    acis::VertexId bb = b.add_vertex({20, 0, 0});
    acis::VertexId c = b.add_vertex({20, 10, 0});
    acis::VertexId d = b.add_vertex({0, 10, 0});

    auto polyline = [&](double y) {
        acis::BSplineCurveGeometry curve;
        curve.degree = 1;
        curve.knots = {0, 0, 0.5, 1, 1};
        curve.points = {{0, y, 0}, {10, y, 2}, {20, y, 0}};
        if (options.rational) curve.weights.assign(3, 1.0);
        return curve;
    };
    auto straight = [&](double x, double u) {
        acis::LineGeometry line;
        line.origin = {x, 0, 0};
        line.direction = {0, 1, 0};
        (void)u;
        return line;
    };
    auto pcurve = [&](double u0, double v0, double u1, double v1, double t_end) {
        acis::BSplineCurve2D p;
        p.degree = 1;
        p.knots = {0.0, 0.0, t_end, t_end};
        p.points = {hdxf::Point2{u0, v0}, hdxf::Point2{u1, v1}};
        return b.add_pcurve(p);
    };
    auto edge = [&](acis::VertexId from, acis::VertexId to, acis::CurveId curve, double t_end) {
        acis::EdgeDefinition definition;
        definition.start = from;
        definition.end = to;
        definition.curve = curve;
        definition.start_parameter = 0.0;
        definition.end_parameter = t_end;
        return b.add_edge(definition);
    };
    acis::EdgeId bottom = edge(a, bb, b.add_curve(polyline(0)), 1.0);
    acis::EdgeId right = edge(bb, c, b.add_curve(straight(20, 1)), 10.0);
    acis::EdgeId top = edge(d, c, b.add_curve(polyline(10)), 1.0);
    acis::EdgeId left = edge(a, d, b.add_curve(straight(0, 0)), 10.0);

    acis::CoedgeDefinition coedge;
    coedge.edge = bottom;
    coedge.pcurve = pcurve(0, 0, 1, 0, 1.0);
    b.add_coedge(loop, coedge);
    coedge.edge = right;
    coedge.pcurve = pcurve(1, 0, 1, 1, 10.0);
    b.add_coedge(loop, coedge);
    coedge.edge = top;
    coedge.reversed = true;
    coedge.pcurve = pcurve(0, 1, 1, 1, 1.0);
    b.add_coedge(loop, coedge);
    coedge.edge = left;
    coedge.pcurve = pcurve(0, 0, 0, 1, 10.0);
    b.add_coedge(loop, coedge);
    return b.build_sat(body);
}

} // namespace

TEST(acis_spline_nurbs_cylinder_is_built_and_read_back) {
    acis::Result<acis::SatDocument> sat = build_nurbs_cylinder(10, 25);
    acis::SatDocument& document = require(sat, "NURBS cylinder");
    ParsedSat parsed = parse_document(document);
    assert_eq(count_records(parsed.file, "face"), 3);
    assert_eq(count_records(parsed.file, "loop"), 3);
    assert_eq(count_records(parsed.file, "coedge"), 6, "the caps' one each, the wall's four");
    assert_eq(count_records(parsed.file, "edge"), 3, "two circles and the seam");
    assert_eq(count_records(parsed.file, "spline-surface"), 1);
    assert_eq(count_records(parsed.file, "intcurve-curve"), 2);
    assert_eq(count_records(parsed.file, "straight-curve"), 1);
    assert_eq(count_records(parsed.file, "pcurve"), 4);
    assert_eq(count_records(parsed.file, "plane-surface"), 2);

    // HDxf reads the surface as the exact NURBS it was given
    const hdxf::internal::AcisSurface* wall = nullptr;
    for (const auto& s : parsed.bodies[0].surfaces) {
        if (s.type == hdxf::internal::AcisSurfaceType::Spline) wall = &s;
    }
    assert_true(wall != nullptr, "a spline surface");
    assert_eq(wall->spline.degree_u, 2);
    assert_eq(wall->spline.degree_v, 1);
    assert_eq(wall->spline.count_u, 9);
    assert_eq(wall->spline.count_v, 2);
    assert_eq(static_cast<int>(wall->spline.weights.size()), 18);
    assert_eq(static_cast<int>(wall->spline.knots_u.size()), 12);
    require_near(wall->fit_tolerance, 0.0, 0.0, "an exact surface has no fit tolerance");
    require_near(wall->spline.points[1].x, 10.0, 0.0, "second control point of the bottom row");
    require_near(wall->spline.points[1].y, 10.0, 0.0, "second control point of the bottom row");
    require_near(wall->spline.points[9 + 1].z, 25.0, 0.0, "second control point of the top row");
    require_near(wall->spline.weights[1], std::sqrt(0.5), 1e-15, "weight");

    hdxf::Drawing drawing = drawing_of(document, hdxf::DxfVersion::R2010);
    require_box(extents_of(drawing, first_body(drawing)), Box{{-10, -10, 0}, {10, 10, 25}}, 1e-6, "NURBS cylinder extents");
}

TEST(acis_spline_nurbs_cylinder_text_has_autocads_forms) {
    acis::Result<acis::SatDocument> sat = build_nurbs_cylinder(10, 25);
    std::string text = flat(require(sat, "NURBS cylinder"));
    // exact_spl_sur: AutoCAD 2025's version, "full", the NURBS surface (the knots without the first and
    // the last of the whole vector, as (value, multiplicity)), the fit tolerance, the discontinuities of
    // u and of v, the 21500 logical, the four intervals, and the extension
    assert_true(contains(text, "spline-surface $-1 -1 $-1 forward { exact_spl_sur 22800 full nurbs 2 1 both closed open "
                               "none none 5 2 0 2 0.25 2 0.5 2 0.75 2 1 2 0 1 1 1 10 0 0 1"),
                text);
    assert_true(contains(text, " 1 0 3 0.25 0.5 0.75 0 0 0 0 0 F F 1 F 0 F 1 F 0 UNEXTENDED } I I I I #"), text);
    // exact_int_cur for the circle edges
    assert_true(contains(text, "intcurve-curve $-1 -1 $-1 forward { exact_int_cur 22800 full nurbs 2 closed 5 "
                               "0 2 0.25 2 0.5 2 0.75 2 1 2 10 0 0 1"),
                text);
    assert_true(contains(text, "0 null_surface null_surface nullbs nullbs I I 3 0.25 0.5 0.75 0 0 0 F 1 F 0 UNEXTENDED "
                               "UNEXTENDED } I I #"),
                text);
    // A pcurve refers to the spline surface by its number among the subtype definitions
    assert_true(contains(text, "pcurve $-1 -1 $-1 0 forward { exp_par_cur nubs 1 open 2 0 1 1 1 0 0 1 0 0 spline forward "
                               "{ ref 0 } I I I I } 0 0 #"),
                text);
    // The top circle's coedge runs against its edge: its pcurve is on the negated parameter, as the reversed
    // coedges' pcurves of AutoCAD's loft are (knots from -1 to 0, the points the other way)
    assert_true(contains(text, "exp_par_cur nubs 1 open 2 -1 1 0 1 1 1 0 1 0 spline forward { ref 0 }"), text);
    // The planes have no pcurves, the wall's coedges do
    int refs = 0;
    for (size_t at = text.find("{ ref 0 }"); at != std::string::npos; at = text.find("{ ref 0 }", at + 1)) ++refs;
    assert_eq(refs, 4);
}

TEST(acis_spline_pcurves_lie_on_the_edges_curves) {
    acis::Result<acis::SatDocument> sat = build_nurbs_cylinder(10, 25);
    ParsedSat parsed = parse_document(require(sat, "NURBS cylinder"));
    int checked = 0;
    for (size_t i = 0; i < parsed.file.records.size(); ++i) {
        if (parsed.file.records[i].type != "coedge") continue;
        double deviation = pcurve_deviation(parsed.file, i);
        if (deviation < 0.0) continue;
        ++checked;
        assert_true(deviation < 1e-9, "pcurve of record " + std::to_string(i) + " deviates by " + std::to_string(deviation));
    }
    assert_eq(checked, 4);
}

TEST(acis_spline_control_net_is_u_fastest) {
    acis::Result<acis::SatDocument> sat = build_patch_sheet();
    acis::SatDocument& document = require(sat, "patch");
    ParsedSat parsed = parse_document(document);
    const hdxf::internal::AcisSurface* patch = nullptr;
    for (const auto& s : parsed.bodies[0].surfaces) {
        if (s.type == hdxf::internal::AcisSurfaceType::Spline) patch = &s;
    }
    assert_true(patch != nullptr);
    assert_eq(patch->spline.count_u, 3);
    assert_eq(patch->spline.count_v, 2);
    const hdxf::Point3 expected[6] = {{0, 0, 0}, {10, 0, 2}, {20, 0, 0}, {0, 10, 0}, {10, 10, 2}, {20, 10, 0}};
    for (int v = 0; v < 2; ++v) {
        for (int u = 0; u < 3; ++u) {
            const hdxf::Point3& p = patch->spline.points[static_cast<size_t>(v * 3 + u)];
            const hdxf::Point3& e = expected[v * 3 + u];
            require_near(p.x, e.x, 0.0, "x of the net point");
            require_near(p.y, e.y, 0.0, "y of the net point");
            require_near(p.z, e.z, 0.0, "z of the net point");
        }
    }
    hdxf::Drawing drawing = drawing_of(document, hdxf::DxfVersion::R2010);
    require_box(extents_of(drawing, first_body(drawing)), Box{{0, 0, 0}, {20, 10, 2}}, 1e-6, "patch extents");
    for (size_t i = 0; i < parsed.file.records.size(); ++i) {
        if (parsed.file.records[i].type != "coedge") continue;
        double deviation = pcurve_deviation(parsed.file, i);
        assert_true(deviation >= 0.0 && deviation < 1e-9, "the patch's pcurve");
    }
}

TEST(acis_spline_polynomial_and_rational_words) {
    std::string polynomial = flat(require(build_patch_sheet(), "polynomial patch"));
    assert_true(contains(polynomial, "{ exact_spl_sur 22800 full nubs 1 1 open open none none 3 2 "), polynomial);
    assert_true(contains(polynomial, "exact_int_cur 22800 full nubs 1 open 3 "), polynomial);
    PatchOptions rational;
    rational.rational = true;
    std::string weighted = flat(require(build_patch_sheet(rational), "rational patch"));
    assert_true(contains(weighted, "{ exact_spl_sur 22800 full nurbs 1 1 both open open none none 3 2 "), weighted);
    assert_true(contains(weighted, "exact_int_cur 22800 full nurbs 1 open 3 "), weighted);
    // A control point is followed by its weight
    assert_true(contains(weighted, " 0 0 0 1 10 0 2 1 20 0 0 1 0 10 0 1 "), weighted);
}

TEST(acis_spline_singularity_words) {
    PatchOptions options;
    options.singular_u = acis::Singularity::Low;
    std::string low = flat(require(build_patch_sheet(options), "low"));
    assert_true(contains(low, "nubs 1 1 open open low none 3 2 "), low);
    options.singular_u = acis::Singularity::High;
    assert_true(contains(flat(require(build_patch_sheet(options), "high")), "open open high none 3 2 "));
    options.singular_u = acis::Singularity::Both;
    assert_true(contains(flat(require(build_patch_sheet(options), "both")), "open open both none 3 2 "));
}

TEST(acis_spline_periodic_closure_word) {
    // A closed polygon as a degree 1 curve, periodic: the edge round a planar face
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition face_definition;
    face_definition.surface = b.add_surface(acis::PlaneGeometry{});
    acis::LoopId loop = b.add_loop(b.add_face(shell, face_definition), acis::LoopRole::Outer);
    acis::BSplineCurveGeometry square;
    square.degree = 1;
    square.knots = {0, 0, 1, 2, 3, 4, 4};
    square.points = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}};
    square.closure = acis::Closure::Periodic;
    acis::VertexId vertex = b.add_vertex({0, 0, 0});
    acis::EdgeDefinition edge;
    edge.start = vertex;
    edge.end = vertex;
    edge.curve = b.add_curve(square);
    edge.start_parameter = 0.0;
    edge.end_parameter = 4.0;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(edge);
    b.add_coedge(loop, coedge);
    acis::Result<acis::SatDocument> sat = b.build_sat(body);
    std::string text = flat(require(sat, "periodic square"));
    assert_true(contains(text, "exact_int_cur 22800 full nubs 1 periodic 5 0 1 1 1 2 1 3 1 4 1 0 0 0 1 0 0 1 1 0 0 1 0 0 0 0 "),
                text);
}

TEST(acis_spline_discontinuity_lists_follow_the_knots) {
    // A cubic curve with a double knot: the second derivative may jump there (order 2), and the inner
    // simple knot has a third derivative that may jump (order 3)
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition face_definition;
    face_definition.surface = b.add_surface(acis::PlaneGeometry{});
    acis::LoopId loop = b.add_loop(b.add_face(shell, face_definition), acis::LoopRole::Outer);
    acis::BSplineCurveGeometry curve;
    curve.degree = 3;
    curve.knots = {0, 0, 0, 0, 0.25, 0.5, 0.5, 1, 1, 1, 1};
    curve.points = {{0, 0, 0}, {1, 0, 0}, {2, 1, 0}, {2, 2, 0}, {1, 2, 0}, {0, 1, 0}, {0, 0, 0}};
    curve.closure = acis::Closure::Closed;
    acis::VertexId vertex = b.add_vertex({0, 0, 0});
    acis::EdgeDefinition edge;
    edge.start = vertex;
    edge.end = vertex;
    edge.curve = b.add_curve(curve);
    edge.end_parameter = 1.0;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(edge);
    b.add_coedge(loop, coedge);
    acis::Result<acis::SatDocument> sat = b.build_sat(body);
    std::string text = flat(require(sat, "cubic with a double knot"));
    // Order 1: none; order 2: 0.5; order 3: 0.25
    assert_true(contains(text, "I I 0 1 0.5 1 0.25 0 F 1 F 0 UNEXTENDED UNEXTENDED } I I #"), text);
}
