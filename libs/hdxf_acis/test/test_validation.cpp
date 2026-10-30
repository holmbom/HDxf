#include "test_support.hpp"

#include <limits>

using namespace acis_test;
using acis::ErrorCode;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

// ---------------------------------------------------------------------------
// What the builder refuses: broken topology, orientation, geometry. Each refusal is a diagnostic
// with a code, the object concerned and where it is; nothing partial is returned.
// ---------------------------------------------------------------------------

namespace {

const double kInf = std::numeric_limits<double>::infinity();

bool message_has(const std::vector<acis::Diagnostic>& errors, ErrorCode code, const std::string& text) {
    for (const acis::Diagnostic& e : errors) {
        if (e.code == code && e.message.find(text) != std::string::npos) return true;
    }
    return false;
}

// A rectangle (0,0)-(10,10) as an open body with a plane face, to break
struct Rectangle {
    acis::BodyBuilder builder;
    acis::BodyId body;
    acis::ShellId shell;
    acis::FaceId face;
    acis::LoopId loop;
    acis::VertexId vertex[4];
    acis::EdgeId edge[4];
};

Rectangle make_rectangle(double plane_z = 0.0, int edge_count = 4, bool duplicate_vertex = false) {
    Rectangle r;
    acis::BodyBuilder& b = r.builder;
    r.body = b.create_body(acis::BodyKind::OpenBody);
    r.shell = b.add_shell(b.add_lump(r.body));
    acis::PlaneGeometry plane;
    plane.origin = {0, 0, plane_z};
    acis::FaceDefinition face_definition;
    face_definition.surface = b.add_surface(plane);
    r.face = b.add_face(r.shell, face_definition);
    r.loop = b.add_loop(r.face, acis::LoopRole::Outer);
    const hdxf::Point3 corner[4] = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    for (int i = 0; i < 4; ++i) r.vertex[i] = b.add_vertex(corner[i]);
    acis::VertexId second = b.add_vertex(corner[1]);        // the same position as vertex 1
    for (int i = 0; i < edge_count; ++i) {
        const hdxf::Point3& from = corner[i];
        const hdxf::Point3& to = corner[(i + 1) % 4];
        acis::LineGeometry line;
        line.origin = from;
        line.direction = {(to.x - from.x) / 10.0, (to.y - from.y) / 10.0, 0};
        acis::EdgeDefinition definition;
        definition.start = (duplicate_vertex && i == 1) ? second : r.vertex[i];
        definition.end = r.vertex[(i + 1) % 4];
        definition.curve = b.add_curve(line);
        definition.start_parameter = 0.0;
        definition.end_parameter = 10.0;
        r.edge[i] = b.add_edge(definition);
        acis::CoedgeDefinition coedge;
        coedge.edge = r.edge[i];
        b.add_coedge(r.loop, coedge);
    }
    return r;
}

// An open body with one face on the surface and no loop
acis::Result<acis::SatDocument> face_on(const acis::SurfaceGeometry& geometry) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(geometry);
    b.add_face(shell, definition);
    return b.build_sat(body);
}

// An open body whose one face is a plane with a closed edge on the curve
acis::Result<acis::SatDocument> edge_on(const acis::CurveGeometry& geometry, double start, double end) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition face_definition;
    face_definition.surface = b.add_surface(acis::PlaneGeometry{});
    acis::LoopId loop = b.add_loop(b.add_face(shell, face_definition), acis::LoopRole::Outer);
    acis::VertexId vertex = b.add_vertex({1, 0, 0});
    acis::EdgeDefinition definition;
    definition.start = vertex;
    definition.end = vertex;
    definition.curve = b.add_curve(geometry);
    definition.start_parameter = start;
    definition.end_parameter = end;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(definition);
    b.add_coedge(loop, coedge);
    return b.build_sat(body);
}

acis::BSplineSurfaceGeometry bilinear() {
    acis::BSplineSurfaceGeometry s;
    s.degree_u = 1;
    s.degree_v = 1;
    s.count_u = 2;
    s.count_v = 2;
    s.knots_u = {0, 0, 1, 1};
    s.knots_v = {0, 0, 1, 1};
    s.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
    return s;
}

acis::EllipseGeometry circle_at_unit() {
    acis::EllipseGeometry c;
    c.center = {0, 0, 0};
    c.major_axis = {1, 0, 0};
    return c;
}

} // namespace

TEST(acis_validation_valid_bodies_pass) {
    acis::BodyBuilder b;
    BoxModel m = build_box(b, 10, 20, 30);
    acis::Result<acis::SatDocument> box = b.build_sat(m.body);
    require(box, "box");
    Rectangle r = make_rectangle();
    acis::Result<acis::SatDocument> sheet = r.builder.build_sat(r.body);
    require(sheet, "rectangle");
    acis::Result<acis::SatDocument> hole = build_hole_block();
    require(hole, "hole block");
    acis::Result<acis::SatDocument> lumps = build_two_lumps();
    require(lumps, "two lumps");
    acis::Result<acis::SatDocument> cavity = build_cavity_cube();
    require(cavity, "cube with a cavity");
}

// ---- edge uses --------------------------------------------------------------

TEST(acis_validation_open_solid_shell_is_refused) {
    acis::BodyBuilder b;
    BoxFlags flags;
    flags.omit_face = 5;
    BoxModel m = build_box(b, 10, 10, 10, flags);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    require_failure(result, ErrorCode::EdgeUseCount, "five faces of a box");
    assert_true(message_has(result.errors(), ErrorCode::EdgeUseCount, "exactly two coedges"), errors_text(result.errors()));
    assert_true(message_has(result.errors(), ErrorCode::EdgeUseCount, "found 1"), errors_text(result.errors()));
    // The same five faces are a fine open body
    acis::BodyBuilder open;
    BoxPlacement place;
    place.kind = acis::BodyKind::OpenBody;
    BoxModel sheet = build_box(open, 10, 10, 10, flags, place);
    require(open.build_sat(sheet.body), "open box");
}

TEST(acis_validation_three_uses_of_an_edge_are_refused) {
    acis::BodyBuilder b;
    BoxFlags flags;
    flags.extra_face = true;
    BoxModel m = build_box(b, 10, 10, 10, flags);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    require_failure(result, ErrorCode::EdgeUseCount, "a seventh face on the edges of the sixth");
    assert_true(message_has(result.errors(), ErrorCode::EdgeUseCount, "found 3"), errors_text(result.errors()));
}

TEST(acis_validation_open_body_edges_have_at_most_two_uses) {
    acis::BodyBuilder b;
    BoxFlags flags;
    flags.extra_face = true;
    BoxPlacement place;
    place.kind = acis::BodyKind::OpenBody;
    BoxModel m = build_box(b, 10, 10, 10, flags, place);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    require_failure(result, ErrorCode::EdgeUseCount, "three uses in an open body");
}

TEST(acis_validation_degenerate_edge_has_one_use) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::ConeGeometry cone;
    cone.major_axis = {1, 0, 0};
    cone.radius_gradient = -1.0;            // the apex is at z = 1
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(cone);
    acis::FaceId face = b.add_face(shell, definition);
    acis::VertexId apex = b.add_vertex({0, 0, 1});
    acis::EdgeDefinition degenerate;
    degenerate.start = apex;
    degenerate.end = apex;
    acis::EdgeId edge = b.add_edge(degenerate);
    acis::CoedgeDefinition coedge;
    coedge.edge = edge;
    b.add_coedge(b.add_loop(face, acis::LoopRole::Boundary), coedge);
    require(b.build_sat(body), "one use of the apex edge");
    // A second use is refused
    acis::FaceId second = b.add_face(shell, definition);
    b.add_coedge(b.add_loop(second, acis::LoopRole::Boundary), coedge);
    require_failure(b.build_sat(body), ErrorCode::EdgeUseCount, "two uses of the apex edge");
}

TEST(acis_validation_degenerate_edge_must_be_at_a_point_of_the_surface) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::ConeGeometry cone;
    cone.major_axis = {1, 0, 0};
    cone.radius_gradient = -1.0;
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(cone);
    acis::FaceId face = b.add_face(shell, definition);
    acis::VertexId not_the_apex = b.add_vertex({0, 0, 3});
    acis::EdgeDefinition degenerate;
    degenerate.start = not_the_apex;
    degenerate.end = not_the_apex;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(degenerate);
    b.add_coedge(b.add_loop(face, acis::LoopRole::Boundary), coedge);
    require_failure(b.build_sat(body), ErrorCode::SurfaceMismatch, "an edge with no length away from the apex");
}

TEST(acis_validation_degenerate_edge_is_a_cones_apex_only) {
    // AutoCAD's core measures a sphere whose seam and poles are edges with area 0: a pole is not written so
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(acis::SphereGeometry{});
    acis::FaceId face = b.add_face(shell, definition);
    acis::VertexId pole = b.add_vertex({0, 0, 1});
    acis::EdgeDefinition degenerate;
    degenerate.start = pole;
    degenerate.end = pole;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(degenerate);
    b.add_coedge(b.add_loop(face, acis::LoopRole::Boundary), coedge);
    acis::Result<acis::SatDocument> result = b.build_sat(body);
    require_failure(result, ErrorCode::UnsupportedGeometry, "a degenerate edge at a sphere's pole");
    assert_true(message_has(result.errors(), ErrorCode::UnsupportedGeometry, "cone's apex"), errors_text(result.errors()));
}

TEST(acis_validation_seam_edge_is_used_twice_by_one_face) {
    // The wall's loop of a cylinder with a seam: the seam is one edge, two coedges of one face, in opposite directions
    for (bool pcurves : {false, true}) {
        acis::Result<acis::SatDocument> sat = build_seam_cylinder(10, 25, pcurves);
        ParsedSat parsed = parse_document(require(sat, "seam cylinder"));
        assert_eq(count_records(parsed.file, "coedge"), 6);
        assert_eq(count_records(parsed.file, "edge"), 3);
        assert_eq(count_records(parsed.file, "loop"), 3);
        assert_eq(count_records(parsed.file, "pcurve"), pcurves ? 4 : 0);
        for (size_t i = 0; i < parsed.file.records.size(); ++i) {
            if (parsed.file.records[i].type != "coedge") continue;
            double deviation = pcurve_deviation(parsed.file, i);
            bool has_pcurve = parsed.file.records[i].tokens[9].number >= 0;
            if (has_pcurve) assert_true(deviation >= 0.0 && deviation < 1e-9, "the seam cylinder's pcurve");
        }
    }
}

TEST(acis_validation_degenerate_edge_has_one_vertex) {
    Rectangle r = make_rectangle();
    acis::EdgeDefinition degenerate;
    degenerate.start = r.vertex[0];
    degenerate.end = r.vertex[1];
    acis::CoedgeDefinition coedge;
    coedge.edge = r.builder.add_edge(degenerate);
    r.builder.add_coedge(r.loop, coedge);
    acis::Result<acis::SatDocument> result = r.builder.build_sat(r.body);
    assert_true(has_code(result.errors(), ErrorCode::VertexMismatch), errors_text(result.errors()));
}

// ---- partners and orientation -----------------------------------------------

TEST(acis_validation_broken_partner_relation_is_refused) {
    acis::BodyBuilder b;
    BoxFlags flags;
    flags.wrong_pair_face = 0;
    BoxModel m = build_box(b, 10, 10, 10, flags);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    require_failure(result, ErrorCode::PartnerMismatch, "partners on two different edges");
    assert_true(message_has(result.errors(), ErrorCode::PartnerMismatch, "uses edge["), errors_text(result.errors()));
}

TEST(acis_validation_partner_that_does_not_point_back_is_refused) {
    acis::BodyBuilder b;
    BoxModel m = build_box(b, 10, 10, 10);
    // coedge a of face 0 and its real partner are paired by themselves; pairing a with a coedge of
    // another edge leaves the real partner pointing at a
    acis::CoedgeId a = m.coedges[0][0];
    require_ok(b.pair_coedges(a, m.coedges[3][1]));
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    require_failure(result, ErrorCode::PartnerMismatch, "a partner on another edge");
}

TEST(acis_validation_two_uses_in_the_same_direction_are_refused) {
    acis::BodyBuilder b;
    BoxFlags flags;
    flags.flip_face = 0;
    BoxModel m = build_box(b, 10, 10, 10, flags);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    require_failure(result, ErrorCode::OrientationConflict, "a face that runs the other way round its loop");
    assert_true(message_has(result.errors(), ErrorCode::OrientationConflict, "same direction"), errors_text(result.errors()));
}

TEST(acis_validation_loop_must_run_counter_clockwise_about_the_normal) {
    // A rectangle whose face is turned round while its loop is not
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(acis::PlaneGeometry{});
    definition.reversed = true;
    acis::LoopId loop = b.add_loop(b.add_face(shell, definition), acis::LoopRole::Outer);
    const hdxf::Point3 corner[4] = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    acis::VertexId v[4];
    for (int i = 0; i < 4; ++i) v[i] = b.add_vertex(corner[i]);
    for (int i = 0; i < 4; ++i) {
        const hdxf::Point3& from = corner[i];
        const hdxf::Point3& to = corner[(i + 1) % 4];
        acis::LineGeometry line;
        line.origin = from;
        line.direction = {(to.x - from.x) / 10.0, (to.y - from.y) / 10.0, 0};
        acis::EdgeDefinition edge;
        edge.start = v[i];
        edge.end = v[(i + 1) % 4];
        edge.curve = b.add_curve(line);
        edge.end_parameter = 10.0;
        acis::CoedgeDefinition coedge;
        coedge.edge = b.add_edge(edge);
        b.add_coedge(loop, coedge);
    }
    acis::Result<acis::SatDocument> result = b.build_sat(body);
    require_failure(result, ErrorCode::OrientationConflict, "an outer loop against the face's normal");
    assert_true(message_has(result.errors(), ErrorCode::OrientationConflict, "clockwise"), errors_text(result.errors()));
}

TEST(acis_validation_inner_loops_run_clockwise) {
    require(build_hole_block(true), "hole block");
    acis::Result<acis::SatDocument> wrong = build_hole_block(false);
    require_failure(wrong, ErrorCode::OrientationConflict, "a hole that runs counter-clockwise");
}

// ---- loops and vertices -----------------------------------------------------

TEST(acis_validation_loop_that_does_not_close_is_refused) {
    Rectangle r = make_rectangle(0.0, 3);
    acis::Result<acis::SatDocument> result = r.builder.build_sat(r.body);
    require_failure(result, ErrorCode::LoopNotClosed, "a rectangle of three sides");
    assert_true(message_has(result.errors(), ErrorCode::LoopNotClosed, "mm away"), errors_text(result.errors()));
}

TEST(acis_validation_loop_without_coedges_is_refused) {
    Rectangle r = make_rectangle();
    r.builder.add_loop(r.face, acis::LoopRole::Inner);
    require_failure(r.builder.build_sat(r.body), ErrorCode::LoopNotClosed, "an empty loop");
}

TEST(acis_validation_two_vertices_at_one_position_are_not_merged) {
    Rectangle r = make_rectangle(0.0, 4, true);
    acis::Result<acis::SatDocument> result = r.builder.build_sat(r.body);
    require_failure(result, ErrorCode::VertexMismatch, "two vertices at the same position");
    assert_true(message_has(result.errors(), ErrorCode::VertexMismatch, "share one vertex"), errors_text(result.errors()));
}

TEST(acis_validation_edge_ends_must_be_on_the_curve) {
    acis::BodyBuilder b;
    BoxFlags flags;
    flags.skew_vertex = true;
    BoxModel m = build_box(b, 10, 10, 10, flags);
    acis::Result<acis::SatDocument> result = b.build_sat(m.body);
    require_failure(result, ErrorCode::CurveEndpointMismatch, "a corner moved off its edges");
    assert_true(message_has(result.errors(), ErrorCode::CurveEndpointMismatch, "0.5 mm"), errors_text(result.errors()));
    assert_true(message_has(result.errors(), ErrorCode::CurveEndpointMismatch, "tolerance is 1e-06 mm"),
                errors_text(result.errors()));
}

TEST(acis_validation_edge_curve_must_be_on_the_face_surface) {
    Rectangle r = make_rectangle(1.0);
    require_failure(r.builder.build_sat(r.body), ErrorCode::SurfaceMismatch, "a plane one mm above its edges");
    // A larger tolerance accepts a small offset; the tolerance is the caller's
    acis::BuildOptions options;
    options.tolerance.distance = 2.0;
    acis::BodyBuilder b(options);
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::PlaneGeometry plane;
    plane.origin = {0, 0, 1.0};
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(plane);
    acis::LoopId loop = b.add_loop(b.add_face(shell, definition), acis::LoopRole::Outer);
    acis::VertexId v = b.add_vertex({1, 0, 0});
    acis::EdgeDefinition edge;
    edge.start = v;
    edge.end = v;
    edge.curve = b.add_curve(circle_at_unit());
    edge.end_parameter = 2.0 * 3.14159265358979323846;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(edge);
    b.add_coedge(loop, coedge);
    require(b.build_sat(body), "a circle one mm below its plane, tolerance 2 mm");
}

TEST(acis_validation_face_without_loops_is_a_whole_sphere_or_torus) {
    require_failure(face_on(acis::PlaneGeometry{}), ErrorCode::LooplessFace, "a plane without a boundary");
    require_failure(face_on(acis::ConeGeometry{}), ErrorCode::LooplessFace, "a cylinder without a boundary");
    require(face_on(acis::SphereGeometry{}), "a whole sphere");
    require(face_on(acis::TorusGeometry{}), "a whole torus");
    // ... and alone in its shell
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::Solid);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(acis::SphereGeometry{});
    b.add_face(shell, definition);
    b.add_face(shell, definition);
    require_failure(b.build_sat(body), ErrorCode::LooplessFace, "two whole spheres in a shell");
}

TEST(acis_validation_planar_faces_have_one_outer_loop) {
    Rectangle r = make_rectangle();
    // A second outer loop: another triangle's worth of coedges is not needed, a loop of the same edges
    acis::LoopId second = r.builder.add_loop(r.face, acis::LoopRole::Outer);
    for (int i = 0; i < 4; ++i) {
        acis::CoedgeDefinition coedge;
        coedge.edge = r.edge[i];
        r.builder.add_coedge(second, coedge);
    }
    acis::Result<acis::SatDocument> result = r.builder.build_sat(r.body);
    assert_true(has_code(result.errors(), ErrorCode::LoopRole), errors_text(result.errors()));
}

TEST(acis_validation_planar_loops_are_outer_or_inner) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(acis::PlaneGeometry{});
    acis::LoopId loop = b.add_loop(b.add_face(shell, definition), acis::LoopRole::Boundary);
    acis::VertexId v = b.add_vertex({1, 0, 0});
    acis::EdgeDefinition edge;
    edge.start = v;
    edge.end = v;
    edge.curve = b.add_curve(circle_at_unit());
    edge.end_parameter = 2.0 * 3.14159265358979323846;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(edge);
    b.add_coedge(loop, coedge);
    acis::Result<acis::SatDocument> result = b.build_sat(body);
    assert_true(has_code(result.errors(), ErrorCode::LoopRole), errors_text(result.errors()));
}

// ---- bodies, lumps, shells --------------------------------------------------

TEST(acis_validation_empty_parts_are_refused) {
    acis::BodyBuilder b;
    acis::BodyId empty = b.create_body(acis::BodyKind::Solid);
    require_failure(b.build_sat(empty), ErrorCode::EmptyBody, "a body without a lump");
    acis::BodyId with_lump = b.create_body(acis::BodyKind::Solid);
    b.add_lump(with_lump);
    require_failure(b.build_sat(with_lump), ErrorCode::EmptyBody, "a lump without a shell");
    acis::BodyId with_shell = b.create_body(acis::BodyKind::Solid);
    b.add_shell(b.add_lump(with_shell));
    require_failure(b.build_sat(with_shell), ErrorCode::EmptyBody, "a shell without a face");
}

TEST(acis_validation_a_lump_has_one_outer_shell) {
    acis::BodyBuilder b;
    BoxModel first = build_box(b, 10, 10, 10);
    BoxPlacement second;
    second.origin = {50, 0, 0};
    second.body = first.body;
    second.lump = first.lump;           // a second Outer shell in the lump
    build_box(b, 10, 10, 10, BoxFlags{}, second);
    require_failure(b.build_sat(first.body), ErrorCode::ShellRole, "two outer shells in a lump");

    // A void shell with no outer shell
    acis::BodyBuilder v;
    BoxPlacement only_void;
    only_void.role = acis::ShellRole::Void;
    BoxFlags inverted;
    inverted.invert = true;
    BoxModel cavity = build_box(v, 10, 10, 10, inverted, only_void);
    require_failure(v.build_sat(cavity.body), ErrorCode::ShellRole, "a lump of a void shell only");
}

TEST(acis_validation_void_shells_belong_to_closed_bodies) {
    acis::BodyBuilder b;
    BoxPlacement outer;
    outer.kind = acis::BodyKind::OpenBody;
    BoxModel first = build_box(b, 40, 40, 40, BoxFlags{}, outer);
    BoxPlacement cavity;
    cavity.origin = {15, 15, 15};
    cavity.body = first.body;
    cavity.lump = first.lump;
    cavity.role = acis::ShellRole::Void;
    BoxFlags inverted;
    inverted.invert = true;
    build_box(b, 10, 10, 10, inverted, cavity);
    require_failure(b.build_sat(first.body), ErrorCode::ShellRole, "a void shell in an open body");
}

TEST(acis_validation_region_is_planar_and_has_one_lump_and_shell) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::Region);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(acis::SphereGeometry{});
    b.add_face(shell, definition);
    require_failure(b.build_sat(body), ErrorCode::InvalidGeometry, "a sphere as a region");

    acis::BodyBuilder two;
    acis::BodyId region = two.create_body(acis::BodyKind::Region);
    two.add_shell(two.add_lump(region));
    two.add_shell(two.add_lump(region));
    acis::Result<acis::SatDocument> result = two.build_sat(region);
    assert_true(has_code(result.errors(), ErrorCode::EmptyBody), errors_text(result.errors()));
}

TEST(acis_validation_edge_in_two_shells_is_refused) {
    acis::BodyBuilder b;
    BoxModel box = build_box(b, 10, 10, 10);
    // A void shell with a face on four edges of the box (its z = 0 side)
    acis::ShellOptions options;
    options.role = acis::ShellRole::Void;
    acis::ShellId second = b.add_shell(box.lump, options);
    acis::PlaneGeometry plane;
    plane.normal = {0, 0, -1};
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(plane);
    acis::LoopId loop = b.add_loop(b.add_face(second, definition), acis::LoopRole::Outer);
    // Edges 0 (corner 0 to 1), 5 (1 to 3), 1 (2 to 3, run backwards) and 4 (0 to 2, backwards)
    const std::pair<size_t, bool> ring[4] = {{0, false}, {5, false}, {1, true}, {4, true}};
    for (const auto& [index, reversed] : ring) {
        acis::CoedgeDefinition coedge;
        coedge.edge = box.edges[index];
        coedge.reversed = reversed;
        b.add_coedge(loop, coedge);
    }
    acis::Result<acis::SatDocument> result = b.build_sat(box.body);
    require_failure(result, ErrorCode::LoopNotClosed, "an edge in two shells");
    assert_true(message_has(result.errors(), ErrorCode::LoopNotClosed, "more than one shell"), errors_text(result.errors()));
}

// ---- geometry ---------------------------------------------------------------

TEST(acis_validation_geometry_must_be_finite_and_have_unit_axes) {
    acis::PlaneGeometry plane;
    plane.normal = {0, 0, 2};
    require_failure(face_on(plane), ErrorCode::InvalidGeometry, "a plane whose normal is not a unit vector");
    plane = acis::PlaneGeometry{};
    plane.u_direction = {0, 0, 1};
    require_failure(face_on(plane), ErrorCode::InvalidGeometry, "a plane whose u direction is its normal");
    plane = acis::PlaneGeometry{};
    plane.origin = {kInf, 0, 0};
    require_failure(face_on(plane), ErrorCode::InvalidGeometry, "a plane at infinity");

    acis::SphereGeometry sphere;
    sphere.radius = 0.0;
    require_failure(face_on(sphere), ErrorCode::InvalidGeometry, "a sphere of radius 0");
    sphere.radius = std::nan("");
    require_failure(face_on(sphere), ErrorCode::InvalidGeometry, "a sphere of radius NaN");

    acis::TorusGeometry torus;
    torus.major_radius = 1.0;
    torus.minor_radius = 1.0;
    require_failure(face_on(torus), ErrorCode::InvalidGeometry, "a torus whose radii are equal");
    torus.minor_radius = 2.0;
    require_failure(face_on(torus), ErrorCode::InvalidGeometry, "a torus whose minor radius is the greater");

    acis::ConeGeometry cone;
    cone.major_axis = {0, 0, 1};
    require_failure(face_on(cone), ErrorCode::InvalidGeometry, "a cone whose major axis is its axis");
    cone = acis::ConeGeometry{};
    cone.axis = {0, 0, 0};
    require_failure(face_on(cone), ErrorCode::InvalidGeometry, "a cone without an axis");
}

TEST(acis_validation_curves_must_be_valid) {
    acis::LineGeometry line;
    line.direction = {2, 0, 0};
    require_failure(edge_on(line, 0, 1), ErrorCode::InvalidGeometry, "a line whose direction is not a unit vector");
    acis::EllipseGeometry ellipse = circle_at_unit();
    ellipse.ratio = 1.5;
    require_failure(edge_on(ellipse, 0, 1), ErrorCode::InvalidGeometry, "an ellipse whose minor axis is longer");
    ellipse = circle_at_unit();
    ellipse.major_axis = {0, 0, 0};
    require_failure(edge_on(ellipse, 0, 1), ErrorCode::InvalidGeometry, "an ellipse without a major axis");
    ellipse = circle_at_unit();
    ellipse.center = {std::nan(""), 0, 0};
    require_failure(edge_on(ellipse, 0, 1), ErrorCode::InvalidGeometry, "an ellipse centered at NaN");
}

TEST(acis_validation_edge_parameters_are_checked) {
    acis::EllipseGeometry circle = circle_at_unit();
    const double two_pi = 2.0 * 3.14159265358979323846;
    require(edge_on(circle, 0.0, two_pi), "a whole circle");
    require_failure(edge_on(circle, 1.0, 1.0), ErrorCode::InvalidGeometry, "an edge of no length");
    require_failure(edge_on(circle, 1.0, 0.0), ErrorCode::InvalidGeometry, "parameters that decrease");
    require_failure(edge_on(circle, 0.0, 1.5 * two_pi), ErrorCode::InvalidGeometry, "more than one turn");
    require_failure(edge_on(circle, 0.0, std::nan("")), ErrorCode::InvalidGeometry, "a parameter that is NaN");
}

TEST(acis_validation_bspline_surfaces_are_checked) {
    auto broken = [](auto change) {
        acis::BSplineSurfaceGeometry s = bilinear();
        change(s);
        return face_on(s);
    };
    // The bilinear patch itself is a valid surface (a face without loops on it is not)
    acis::Result<acis::SatDocument> plain = face_on(bilinear());
    assert_false(has_code(plain.errors(), ErrorCode::InvalidBSpline), errors_text(plain.errors()));
    const ErrorCode code = ErrorCode::InvalidBSpline;
    require_failure(broken([](auto& s) { s.degree_u = 0; }), code, "degree 0");
    require_failure(broken([](auto& s) { s.degree_v = 30; }), code, "degree 30");
    require_failure(broken([](auto& s) { s.knots_u = {0, 0, 1}; }), code, "too few knots");
    require_failure(broken([](auto& s) { s.knots_v = {0, 1, 0.5, 1}; }), code, "decreasing knots");
    require_failure(broken([](auto& s) { s.knots_u = {0, 0, 0.5, 0.5, 1, 1}; s.count_u = 4; s.points.resize(8); }), code,
                    "an inner knot of multiplicity 2 in degree 1");
    require_failure(broken([](auto& s) { s.points.pop_back(); }), code, "a net that is too small");
    require_failure(broken([](auto& s) { s.weights = {1, 1, 1}; }), code, "three weights for four points");
    require_failure(broken([](auto& s) { s.weights = {1, 1, 0, 1}; }), code, "a weight of 0");
    require_failure(broken([](auto& s) { s.weights = {1, 1, -1, 1}; }), code, "a negative weight");
    require_failure(broken([](auto& s) { s.points[2].z = std::nan(""); }), code, "a control point that is NaN");
    require_failure(broken([](auto& s) { s.knots_v = {0, 0, 0, 1, 1, 1}; s.count_v = 3; }), code, "a net that does not fit");
    require_failure(broken([](auto& s) { s.knots_u = {1, 1, 1, 1}; }), code, "an empty parameter interval");
}

TEST(acis_validation_bspline_curves_are_checked) {
    acis::BSplineCurveGeometry line;
    line.degree = 1;
    line.knots = {0, 0, 5, 5};
    line.points = {{0, 0, 0}, {5, 0, 0}};
    auto with = [](acis::BSplineCurveGeometry c) { return edge_on(c, 0.0, 5.0); };
    // The curve ends at (5, 0, 0) and (0, 0, 0), the vertex is at (1, 0, 0): the endpoints, not the curve, fail
    acis::Result<acis::SatDocument> fine = with(line);
    assert_false(has_code(fine.errors(), ErrorCode::InvalidBSpline), errors_text(fine.errors()));
    assert_true(has_code(fine.errors(), ErrorCode::CurveEndpointMismatch));

    const ErrorCode code = ErrorCode::InvalidBSpline;
    acis::BSplineCurveGeometry c = line;
    c.degree = 2;
    require_failure(with(c), code, "degree 2 with two points");
    c = line;
    c.knots = {0, 0, 5};
    require_failure(with(c), code, "three knots");
    c = line;
    c.weights = {1.0};
    require_failure(with(c), code, "one weight for two points");
    c = line;
    c.points[1].x = kInf;
    require_failure(with(c), code, "an infinite control point");
    c = line;
    c.closure = acis::Closure::Closed;
    require_failure(with(c), code, "a closed curve whose ends differ");
    c = line;
    c.knots = {0, 0, 0, 5};
    require_failure(with(c), code, "a start knot of multiplicity 3 in degree 1");
}

TEST(acis_validation_edge_parameters_must_be_inside_a_bspline_domain) {
    acis::BSplineCurveGeometry line;
    line.degree = 1;
    line.knots = {0, 0, 1, 1};
    line.points = {{1, 0, 0}, {1, 0, 0}};
    acis::Result<acis::SatDocument> result = edge_on(line, 0.0, 2.0);
    assert_true(has_code(result.errors(), ErrorCode::InvalidGeometry), errors_text(result.errors()));
}

// ---- pcurves ----------------------------------------------------------------

TEST(acis_validation_coedge_on_a_bspline_surface_needs_a_pcurve) {
    require(build_nurbs_cylinder(10, 25), "NURBS cylinder with its pcurves");
    NurbsFlags without;
    without.pcurves = false;
    acis::Result<acis::SatDocument> result = build_nurbs_cylinder(10, 25, without);
    require_failure(result, ErrorCode::PcurveRequired, "a NURBS wall without pcurves");
    assert_true(message_has(result.errors(), ErrorCode::PcurveRequired, "trimming cannot be determined"),
                errors_text(result.errors()));
    int missing = 0;
    for (const acis::Diagnostic& d : result.errors()) {
        if (d.code == ErrorCode::PcurveRequired) {
            ++missing;
            assert_true(d.path.find("/face[") != std::string::npos && d.path.find("/loop[") != std::string::npos &&
                            d.path.find("/coedge[") != std::string::npos,
                        d.path);
        }
    }
    assert_eq(missing, 4);
}

TEST(acis_validation_pcurve_must_be_the_edges_curve_on_the_surface) {
    NurbsFlags off;
    off.bottom_pcurve_v = 0.1;
    acis::Result<acis::SatDocument> result = build_nurbs_cylinder(10, 25, off);
    require_failure(result, ErrorCode::PcurveMismatch, "a pcurve one tenth up the wall");
    assert_true(message_has(result.errors(), ErrorCode::PcurveMismatch, "from the edge's curve"), errors_text(result.errors()));
}

TEST(acis_validation_each_coedge_has_its_own_pcurve) {
    NurbsFlags shared;
    shared.share_pcurve = true;
    require_failure(build_nurbs_cylinder(10, 25, shared), ErrorCode::PcurveMismatch, "one pcurve for two coedges");
}

TEST(acis_validation_pcurve_must_cover_the_edge_parameters) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::BSplineSurfaceGeometry patch = bilinear();
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(patch);
    acis::LoopId loop = b.add_loop(b.add_face(shell, definition), acis::LoopRole::Boundary);
    const hdxf::Point3 corner[4] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    acis::VertexId v[4];
    for (int i = 0; i < 4; ++i) v[i] = b.add_vertex(corner[i]);
    const hdxf::Point2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int i = 0; i < 4; ++i) {
        const hdxf::Point3& from = corner[i];
        const hdxf::Point3& to = corner[(i + 1) % 4];
        acis::LineGeometry line;
        line.origin = from;
        line.direction = {to.x - from.x, to.y - from.y, 0};
        acis::EdgeDefinition edge;
        edge.start = v[i];
        edge.end = v[(i + 1) % 4];
        edge.curve = b.add_curve(line);
        edge.end_parameter = 1.0;
        acis::BSplineCurve2D pcurve;
        pcurve.degree = 1;
        // The pcurve's parameters end at 0.5: it does not cover the edge
        pcurve.knots = {0.0, 0.0, 0.5, 0.5};
        pcurve.points = {uv[i], uv[(i + 1) % 4]};
        acis::CoedgeDefinition coedge;
        coedge.edge = b.add_edge(edge);
        coedge.pcurve = b.add_pcurve(pcurve);
        b.add_coedge(loop, coedge);
    }
    acis::Result<acis::SatDocument> result = b.build_sat(body);
    require_failure(result, ErrorCode::PcurveMismatch, "pcurves that end before the edges do");
    assert_true(message_has(result.errors(), ErrorCode::PcurveMismatch, "do not cover"), errors_text(result.errors()));
}

TEST(acis_validation_pcurve_geometry_is_checked) {
    acis::BodyBuilder b;
    // A pcurve with knots that do not fit its points
    acis::BodyId body = b.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = b.add_surface(bilinear());
    acis::LoopId loop = b.add_loop(b.add_face(shell, definition), acis::LoopRole::Boundary);
    acis::LineGeometry line;
    line.direction = {1, 0, 0};
    acis::EdgeDefinition edge;
    edge.start = b.add_vertex({0, 0, 0});
    edge.end = b.add_vertex({1, 0, 0});
    edge.curve = b.add_curve(line);
    edge.end_parameter = 1.0;
    acis::BSplineCurve2D pcurve;
    pcurve.degree = 1;
    pcurve.knots = {0.0, 1.0};
    pcurve.points = {hdxf::Point2{0, 0}, hdxf::Point2{1, 0}};
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(edge);
    coedge.pcurve = b.add_pcurve(pcurve);
    b.add_coedge(loop, coedge);
    acis::Result<acis::SatDocument> result = b.build_sat(body);
    assert_true(has_code(result.errors(), ErrorCode::InvalidBSpline), errors_text(result.errors()));
}

// ---- values the SAT cannot hold -----------------------------------------------

TEST(acis_validation_vertex_positions_must_be_finite) {
    acis::BodyBuilder b;
    BoxModel m = build_box(b, 10, 10, 10);
    b.add_vertex({std::nan(""), 0, 0});     // unused: not an error
    require(b.build_sat(m.body), "an unused NaN vertex");

    Rectangle r = make_rectangle();
    acis::Result<acis::SatDocument> fine = r.builder.build_sat(r.body);
    require(fine, "a rectangle");
    acis::BodyBuilder n;
    acis::BodyId body = n.create_body(acis::BodyKind::OpenBody);
    acis::ShellId shell = n.add_shell(n.add_lump(body));
    acis::FaceDefinition definition;
    definition.surface = n.add_surface(acis::PlaneGeometry{});
    acis::LoopId loop = n.add_loop(n.add_face(shell, definition), acis::LoopRole::Outer);
    acis::VertexId v = n.add_vertex({kInf, 0, 0});
    acis::EdgeDefinition edge;
    edge.start = v;
    edge.end = v;
    edge.curve = n.add_curve(circle_at_unit());
    edge.end_parameter = 6.28;
    acis::CoedgeDefinition coedge;
    coedge.edge = n.add_edge(edge);
    n.add_coedge(loop, coedge);
    acis::Result<acis::SatDocument> result = n.build_sat(body);
    assert_true(has_code(result.errors(), ErrorCode::InvalidGeometry), errors_text(result.errors()));
}
