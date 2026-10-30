#include "test_support.hpp"

#include <set>

using namespace acis_test;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

// ---------------------------------------------------------------------------
// The primitives: the numbers of AutoCAD's own (10_solids.scr), built through the generic model
// ---------------------------------------------------------------------------

namespace {

acis::Frame frame_at(double x, double y, double z) {
    acis::Frame frame;
    frame.origin = {x, y, z};
    return frame;
}

int total_records(const hdxf::internal::AcisFile& file) { return static_cast<int>(file.records.size()); }

// The extents of the body after it has been through a DXF 2010 drawing
Box extents_through_dxf(const acis::SatDocument& document) {
    hdxf::Drawing drawing = drawing_of(document, hdxf::DxfVersion::R2010);
    return extents_of(drawing, first_body(drawing));
}

struct Counts {
    int faces, loops, coedges, edges, vertices;
};

void require_counts(const ParsedSat& parsed, const Counts& expected, const std::string& what) {
    assert_eq(count_records(parsed.file, "face"), expected.faces, what + " faces");
    assert_eq(count_records(parsed.file, "loop"), expected.loops, what + " loops");
    assert_eq(count_records(parsed.file, "coedge"), expected.coedges, what + " coedges");
    assert_eq(count_records(parsed.file, "edge"), expected.edges, what + " edges");
    assert_eq(count_records(parsed.file, "vertex"), expected.vertices, what + " vertices");
    assert_eq(count_records(parsed.file, "point"), expected.vertices, what + " points");
    assert_eq(count_records(parsed.file, "asmheader"), 1, what + " asmheader");
    assert_eq(count_records(parsed.file, "body"), 1, what + " body");
    assert_eq(count_records(parsed.file, "lump"), 1, what + " lump");
    assert_eq(count_records(parsed.file, "shell"), 1, what + " shell");
}

// The count of '#' in the lines: the records
int hashes(const acis::SatDocument& document) {
    int n = 0;
    for (size_t i = 3; i < document.lines.size(); ++i) {
        for (char c : document.lines[i]) {
            if (c == '#') ++n;
        }
    }
    return n;
}

hdxf::Point3 rotated(const hdxf::Point3& v, const hdxf::Point3& axis, double angle) {
    double c = std::cos(angle);
    double s = std::sin(angle);
    double d = axis.x * v.x + axis.y * v.y + axis.z * v.z;
    hdxf::Point3 k{axis.y * v.z - axis.z * v.y, axis.z * v.x - axis.x * v.z, axis.x * v.y - axis.y * v.x};
    return {v.x * c + k.x * s + axis.x * d * (1 - c), v.y * c + k.y * s + axis.y * d * (1 - c),
            v.z * c + k.z * s + axis.z * d * (1 - c)};
}

} // namespace

TEST(acis_primitives_box_as_autocad_makes_it) {
    acis::BoxParameters p;
    p.frame = frame_at(10, 10, 10);
    p.size_x = 20;
    p.size_y = 15;
    p.size_z = 20;
    acis::Result<acis::SatDocument> sat = acis::make_box(p);
    acis::SatDocument& document = require(sat, "box");
    ParsedSat parsed = parse_document(document);
    require_counts(parsed, {6, 6, 24, 12, 8}, "box");
    assert_eq(count_records(parsed.file, "plane-surface"), 6);
    assert_eq(count_records(parsed.file, "straight-curve"), 12);
    assert_eq(count_records(parsed.file, "transform"), 1);
    // 5 + 6 + 6 + 24 + 12 + 8 + 8 + 6 + 12; no attributes, as AutoCAD's history is not made up
    assert_eq(total_records(parsed.file), 87);
    assert_eq(document.record_count, 87);
    assert_eq(hashes(document), 87);
    // The facit's box is 113 records: the same 87 and 26 history attributes
    require_box(extents_through_dxf(document), Box{{0, 2.5, 0}, {20, 17.5, 20}}, 1e-9, "box extents");
    // Each face is a plane with 4 coedges, and every edge has two
    assert_eq(static_cast<int>(parsed.bodies.size()), 1);
    assert_eq(static_cast<int>(parsed.bodies[0].faces.size()), 6);
    assert_eq(static_cast<int>(parsed.bodies[0].edges.size()), 12);
    for (const auto& face : parsed.bodies[0].faces) {
        assert_eq(static_cast<int>(face.loops.size()), 1);
        assert_eq(static_cast<int>(face.loops[0].coedges.size()), 4);
    }
}

TEST(acis_primitives_box_at_the_origin_has_no_transform) {
    acis::BoxParameters p;
    p.size_x = 2;
    p.size_y = 3;
    p.size_z = 4;
    acis::Result<acis::SatDocument> sat = acis::make_box(p);
    ParsedSat parsed = parse_document(require(sat, "box"));
    assert_eq(count_records(parsed.file, "transform"), 0);
    assert_eq(total_records(parsed.file), 86);
    require_box(extents_through_dxf(sat.value()), Box{{-1, -1.5, -2}, {1, 1.5, 2}}, 1e-9, "box extents");
}

TEST(acis_primitives_cylinder_as_autocad_makes_it) {
    acis::CylinderParameters p;
    p.frame = frame_at(90, 10, 12.5);
    p.radius = 10;
    p.height = 25;
    acis::Result<acis::SatDocument> sat = acis::make_cylinder(p);
    acis::SatDocument& document = require(sat, "cylinder");
    ParsedSat parsed = parse_document(document);
    // Three faces, the wall's two loops, and no seam: AutoCAD's cylinder has none
    require_counts(parsed, {3, 4, 4, 2, 2}, "cylinder");
    assert_eq(count_records(parsed.file, "cone-surface"), 1);
    assert_eq(count_records(parsed.file, "plane-surface"), 2);
    assert_eq(count_records(parsed.file, "ellipse-curve"), 2);
    assert_eq(total_records(parsed.file), 27);
    require_box(extents_through_dxf(document), Box{{80, 0, 0}, {100, 20, 25}}, 1e-9, "cylinder extents");
}

TEST(acis_primitives_cone_has_its_apex_as_a_degenerate_edge) {
    acis::ConeParameters p;
    p.frame = frame_at(130, 10, 15);
    p.radius = 12;
    p.height = 30;
    acis::Result<acis::SatDocument> sat = acis::make_cone(p);
    acis::SatDocument& document = require(sat, "cone");
    ParsedSat parsed = parse_document(document);
    require_counts(parsed, {2, 3, 3, 2, 2}, "cone");
    assert_eq(count_records(parsed.file, "cone-surface"), 1);
    assert_eq(count_records(parsed.file, "plane-surface"), 1);
    assert_eq(count_records(parsed.file, "ellipse-curve"), 1);
    assert_eq(total_records(parsed.file), 22);
    // One edge has no curve: the apex, a single coedge with no partner
    int degenerate = 0;
    for (const auto& r : parsed.file.records) {
        if (r.type != "edge") continue;
        if (r.tokens.size() > 8 && r.tokens[8].type == hdxf::internal::AcisTokenType::Pointer && r.tokens[8].number < 0) {
            ++degenerate;
            assert_eq(static_cast<int>(r.tokens[4].number), 1, "AutoCAD's apex edge starts at parameter 1");
            assert_eq(static_cast<int>(r.tokens[6].number), 0, "and ends at 0");
        }
    }
    assert_eq(degenerate, 1);
    require_box(extents_through_dxf(document), Box{{118, -2, 0}, {142, 22, 30}}, 1e-9, "cone extents");
}

TEST(acis_primitives_truncated_cone_has_two_circles) {
    acis::ConeParameters p;
    p.radius = 12;
    p.top_radius = 6;
    p.height = 20;
    acis::Result<acis::SatDocument> sat = acis::make_cone(p);
    acis::SatDocument& document = require(sat, "frustum");
    ParsedSat parsed = parse_document(document);
    require_counts(parsed, {3, 4, 4, 2, 2}, "frustum");
    assert_eq(count_records(parsed.file, "cone-surface"), 1);
    assert_eq(count_records(parsed.file, "plane-surface"), 2);
    require_box(extents_through_dxf(document), Box{{-12, -12, -10}, {12, 12, 10}}, 1e-9, "frustum extents");
}

TEST(acis_primitives_sphere_is_one_face_without_loops) {
    acis::SphereParameters p;
    p.frame = frame_at(50, 10, 10);
    p.radius = 12;
    acis::Result<acis::SatDocument> sat = acis::make_sphere(p);
    acis::SatDocument& document = require(sat, "sphere");
    ParsedSat parsed = parse_document(document);
    require_counts(parsed, {1, 0, 0, 0, 0}, "sphere");
    assert_eq(count_records(parsed.file, "sphere-surface"), 1);
    assert_eq(total_records(parsed.file), 7);
    // The face has "$-1" for its loop: no loop record, no vertex, no seam
    for (const auto& r : parsed.file.records) {
        if (r.type != "face") continue;
        assert_true(r.tokens[4].type == hdxf::internal::AcisTokenType::Pointer && r.tokens[4].number < 0, "loop pointer");
    }
    require_box(extents_through_dxf(document), Box{{38, -2, -2}, {62, 22, 22}}, 1e-9, "sphere extents");
}

TEST(acis_primitives_torus_is_one_face_without_loops) {
    acis::TorusParameters p;
    p.frame = frame_at(170, 10, 12);
    p.major_radius = 15;
    p.minor_radius = 4;
    acis::Result<acis::SatDocument> sat = acis::make_torus(p);
    acis::SatDocument& document = require(sat, "torus");
    ParsedSat parsed = parse_document(document);
    require_counts(parsed, {1, 0, 0, 0, 0}, "torus");
    assert_eq(count_records(parsed.file, "torus-surface"), 1);
    assert_eq(total_records(parsed.file), 7);
    require_box(extents_through_dxf(document), Box{{151, -9, 8}, {189, 29, 16}}, 1e-9, "torus extents");
}

TEST(acis_primitives_dimensions_are_checked) {
    auto code_of = [](const acis::Result<acis::SatDocument>& r) {
        return r.ok() ? -1 : static_cast<int>(r.errors().front().code);
    };
    const int invalid = static_cast<int>(acis::ErrorCode::InvalidArgument);

    acis::BoxParameters box;
    box.size_x = 0;
    assert_eq(code_of(acis::make_box(box)), invalid, "box of width 0");
    box.size_x = -1;
    assert_eq(code_of(acis::make_box(box)), invalid, "box of negative width");
    box.size_x = std::nan("");
    assert_eq(code_of(acis::make_box(box)), invalid, "box of NaN width");

    acis::CylinderParameters cylinder;
    cylinder.height = 0;
    assert_eq(code_of(acis::make_cylinder(cylinder)), invalid, "cylinder of height 0");
    cylinder.height = 1;
    cylinder.radius = -2;
    assert_eq(code_of(acis::make_cylinder(cylinder)), invalid, "cylinder of negative radius");

    acis::ConeParameters cone;
    cone.top_radius = -1;
    assert_eq(code_of(acis::make_cone(cone)), invalid, "cone of negative top radius");
    cone.top_radius = 0;
    cone.radius = 0;
    assert_eq(code_of(acis::make_cone(cone)), invalid, "cone of radius 0");

    acis::SphereParameters sphere;
    sphere.radius = 0;
    assert_eq(code_of(acis::make_sphere(sphere)), invalid, "sphere of radius 0");

    acis::TorusParameters torus;
    torus.major_radius = 3;
    torus.minor_radius = 3;
    assert_eq(code_of(acis::make_torus(torus)), invalid, "torus with equal radii (a horn torus)");
    torus.minor_radius = 4;
    assert_eq(code_of(acis::make_torus(torus)), invalid, "torus with the minor radius the greater (a spindle torus)");
    torus.minor_radius = 1;
    require(acis::make_torus(torus), "a ring torus");
}

TEST(acis_primitives_frames_are_checked) {
    acis::SphereParameters p;
    p.frame.z_axis = {0, 0, 0};
    assert_false(acis::make_sphere(p).ok(), "a frame without a z axis");
    p.frame = acis::Frame{};
    p.frame.x_axis = {0, 0, 1};
    assert_false(acis::make_sphere(p).ok(), "a frame whose axes are parallel");
    p.frame = acis::Frame{};
    p.frame.origin = {std::nan(""), 0, 0};
    assert_false(acis::make_sphere(p).ok(), "a frame at NaN");
    // Axes that are not unit vectors are normalized (their directions are the frame)
    p.frame = acis::Frame{};
    p.frame.z_axis = {0, 0, 5};
    p.frame.x_axis = {3, 0, 0};
    require(acis::make_sphere(p), "axes of any length");
}

TEST(acis_primitives_frame_becomes_the_bodys_transform) {
    const hdxf::Point3 axis{1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0), 0.0};
    const double angle = 0.4;
    acis::BoxParameters p;
    p.size_x = 20;
    p.size_y = 15;
    p.size_z = 20;
    p.frame.origin = {12.75, 7.25, 9.25};
    p.frame.z_axis = rotated({0, 0, 1}, axis, angle);
    p.frame.x_axis = rotated({1, 0, 0}, axis, angle);
    acis::Result<acis::SatDocument> sat = acis::make_box(p);
    acis::SatDocument& document = require(sat, "rotated box");
    ParsedSat parsed = parse_document(document);
    // The transform maps the local axes to the frame's: HDxf reads it as WCS = m · (x, y, z, 1)
    const auto& t = parsed.bodies[0].transform;
    hdxf::Point3 y_axis = rotated({0, 1, 0}, axis, angle);
    require_near(t[0][0], p.frame.x_axis.x, 1e-15, "m00");
    require_near(t[1][0], p.frame.x_axis.y, 1e-15, "m10");
    require_near(t[2][0], p.frame.x_axis.z, 1e-15, "m20");
    require_near(t[0][1], y_axis.x, 1e-15, "m01");
    require_near(t[1][1], y_axis.y, 1e-15, "m11");
    require_near(t[2][2], p.frame.z_axis.z, 1e-15, "m22");
    require_near(t[0][3], 12.75, 0.0, "tx");
    // The text says "rotate"
    bool found = false;
    for (const std::string& line : document.lines) {
        if (line.rfind("transform ", 0) == 0) {
            found = true;
            assert_true(line.find(" rotate no_reflect no_shear #") != std::string::npos, line);
        }
    }
    assert_true(found);
    // The box's extents are those of its eight corners
    Box expected{{1e300, 1e300, 1e300}, {-1e300, -1e300, -1e300}};
    for (int i = 0; i < 8; ++i) {
        hdxf::Point3 local{(i & 1) ? 10.0 : -10.0, (i & 2) ? 7.5 : -7.5, (i & 4) ? 10.0 : -10.0};
        hdxf::Point3 w{p.frame.origin.x + local.x * p.frame.x_axis.x + local.y * y_axis.x + local.z * p.frame.z_axis.x,
                       p.frame.origin.y + local.x * p.frame.x_axis.y + local.y * y_axis.y + local.z * p.frame.z_axis.y,
                       p.frame.origin.z + local.x * p.frame.x_axis.z + local.y * y_axis.z + local.z * p.frame.z_axis.z};
        expected.min = {std::min(expected.min.x, w.x), std::min(expected.min.y, w.y), std::min(expected.min.z, w.z)};
        expected.max = {std::max(expected.max.x, w.x), std::max(expected.max.y, w.y), std::max(expected.max.z, w.z)};
    }
    require_box(extents_through_dxf(document), expected, 1e-9, "rotated box extents");
}

TEST(acis_primitives_have_no_attributes_and_only_the_records_of_the_facit) {
    const std::set<std::string> allowed = {"asmheader",     "body",         "lump",          "transform",      "shell",
                                           "face",          "loop",         "coedge",        "edge",           "vertex",
                                           "point",         "plane-surface", "cone-surface", "sphere-surface", "torus-surface",
                                           "straight-curve", "ellipse-curve"};
    std::vector<acis::Result<acis::SatDocument>> all;
    acis::BoxParameters box;
    all.push_back(acis::make_box(box));
    acis::CylinderParameters cylinder;
    all.push_back(acis::make_cylinder(cylinder));
    acis::ConeParameters cone;
    all.push_back(acis::make_cone(cone));
    acis::SphereParameters sphere;
    all.push_back(acis::make_sphere(sphere));
    acis::TorusParameters torus;
    all.push_back(acis::make_torus(torus));
    for (auto& r : all) {
        ParsedSat parsed = parse_document(require(r, "primitive"));
        for (const std::string& type : record_types(parsed.file)) {
            assert_true(allowed.count(type) == 1, "unexpected record " + type);
        }
    }
}

TEST(acis_primitives_sat_header_has_the_facit_form) {
    acis::SphereParameters p;
    acis::Result<acis::SatDocument> sat = acis::make_sphere(p);
    const acis::SatDocument& document = require(sat, "sphere");
    assert_eq(document.version, 21500);
    // 6 records without a transform: asmheader, body, lump, shell, face, sphere-surface
    assert_eq(document.record_count, 6);
    assert_eq(document.lines[0], std::string("21500 6 2 24") + std::string(11, ' '));
    assert_eq(document.lines[1], "16 Autodesk AutoCAD 20 ASM 230.1.0.65535 NT 0  ");
    assert_eq(document.lines[2], "1 1e-06 1e-10 ");
    assert_eq(document.lines[3], "asmheader $-1 -1 @13 230.1.0.65535 #");
    for (const std::string& line : document.lines) {
        assert_true(line.size() <= 254, "a line of " + std::to_string(line.size()) + " characters");
    }
    assert_eq(document.lines[4], "body $-1 -1 $-1 $2 $-1 $-1 #");
    assert_eq(document.lines[5], "lump $-1 -1 $-1 $-1 $3 $1 #");
    assert_eq(document.lines[6], "shell $-1 -1 $-1 $-1 $-1 $4 $-1 $2 #");
    assert_eq(document.lines[7], "face $-1 -1 $-1 $-1 $-1 $3 $-1 $5 forward single #");
    assert_eq(document.lines[8], "sphere-surface $-1 -1 $-1 0 0 0 1 1 0 0 0 0 1 forward_v I I I I #");
    assert_eq(static_cast<int>(document.lines.size()), 9);
    // AutoCAD's own end: the last record's "#", no End-of-ACIS-data
    assert_true(document.text().find("End-of") == std::string::npos);
}
