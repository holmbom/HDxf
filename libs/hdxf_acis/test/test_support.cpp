#include "test_support.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

namespace acis_test {

const double kPi = 3.14159265358979323846;

namespace {

const double kTwoPi = 2.0 * 3.14159265358979323846;

hdxf::Point3 point(double x, double y, double z) { return hdxf::Point3{x, y, z}; }

} // namespace

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

std::string errors_text(const std::vector<acis::Diagnostic>& errors) {
    std::string out;
    for (const acis::Diagnostic& e : errors) {
        if (!out.empty()) out += "\n";
        out += e.text();
    }
    return out.empty() ? "(no diagnostics)" : out;
}

bool has_code(const std::vector<acis::Diagnostic>& errors, acis::ErrorCode code) {
    return std::any_of(errors.begin(), errors.end(), [&](const acis::Diagnostic& e) { return e.code == code; });
}

void require_failure(const acis::Result<acis::SatDocument>& result, acis::ErrorCode code, const std::string& what) {
    hdxf_test::assert_false(result.ok(), what + ": the body was written");
    hdxf_test::assert_true(has_code(result.errors(), code),
                           what + ": no " + acis::error_code_name(code) + " in\n" + errors_text(result.errors()));
}

// ---------------------------------------------------------------------------
// The SAT as HDxf reads it
// ---------------------------------------------------------------------------

hdxf::AcisData acis_data(const acis::SatDocument& document) {
    acis::Result<hdxf::AcisData> data = acis::to_acis_data(document);
    return require(data, "to_acis_data");
}

ParsedSat parse_document(const acis::SatDocument& document) {
    ParsedSat parsed;
    hdxf::AcisData data = acis_data(document);
    std::string text = hdxf::internal::acis_deciphered(data);
    hdxf_test::assert_true(hdxf::internal::read_sat(text, parsed.file), "HDxf cannot read the SAT");
    parsed.bodies = hdxf::internal::acis_bodies(parsed.file);
    return parsed;
}

int count_records(const hdxf::internal::AcisFile& file, const std::string& type) {
    int n = 0;
    for (const auto& r : file.records) {
        if (r.type == type) ++n;
    }
    return n;
}

double pcurve_deviation(const hdxf::internal::AcisFile& file, size_t coedge_record) {
    using hdxf::internal::AcisTokenType;
    const auto& coedge = file.records[coedge_record];
    // coedge: attribute, tag, pattern, next, previous, partner, edge, sense, owner, pcurve
    if (coedge.type != "coedge" || coedge.tokens.size() < 10) return -1.0;
    bool coedge_reversed = coedge.tokens[7].type == hdxf::internal::AcisTokenType::Word && coedge.tokens[7].text == "reversed";
    int pcurve = static_cast<int>(coedge.tokens[9].number);
    int edge = static_cast<int>(coedge.tokens[6].number);
    if (pcurve < 0 || edge < 0) return -1.0;
    const auto& edge_record = file.records[static_cast<size_t>(edge)];
    // edge: attribute, tag, pattern, start vertex, start parameter, end vertex, end parameter, coedge, curve
    if (edge_record.type != "edge" || edge_record.tokens.size() < 9) return -1.0;
    double t0 = edge_record.tokens[4].number;
    double t1 = edge_record.tokens[6].number;
    int curve_record = static_cast<int>(edge_record.tokens[8].number);
    if (curve_record < 0) return -1.0;
    hdxf::internal::AcisCurve curve;
    if (!hdxf::internal::acis_record_curve(file, static_cast<size_t>(curve_record), curve)) return -1.0;
    hdxf::internal::AcisSplineCurve uv;
    hdxf::internal::AcisSurface surface;
    bool reversed = false;
    double offset[2] = {0.0, 0.0};
    if (!hdxf::internal::acis_pcurve(file, static_cast<size_t>(pcurve), uv, surface, reversed, offset)) return -1.0;
    double worst = 0.0;
    const int samples = 16;
    for (int i = 0; i <= samples; ++i) {
        double t = t0 + (t1 - t0) * i / samples;
        hdxf::Point3 on_curve;
        hdxf::internal::acis_curve_eval(curve, t, on_curve);
        hdxf::Point3 q;
        // A pcurve runs in the direction of its coedge: against the edge's parameter on a reversed coedge,
        // its parameter the edge's negated (AutoCAD's loft: the reversed coedges' pcurves are on -t)
        hdxf::internal::acis_spline_point(uv, (reversed != coedge_reversed) ? -t : t, q);
        hdxf::Point3 on_surface;
        if (!hdxf::internal::acis_surface_point(surface, q.x + offset[0], q.y + offset[1], on_surface)) return -1.0;
        double dx = on_curve.x - on_surface.x;
        double dy = on_curve.y - on_surface.y;
        double dz = on_curve.z - on_surface.z;
        worst = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return worst;
}

std::vector<std::string> record_types(const hdxf::internal::AcisFile& file) {
    std::vector<std::string> out;
    for (const auto& r : file.records) out.push_back(r.type);
    return out;
}

// ---------------------------------------------------------------------------
// Drawings and extents
// ---------------------------------------------------------------------------

hdxf::EntityType entity_type_of(acis::BodyKind kind) {
    switch (kind) {
        case acis::BodyKind::Solid: return hdxf::EntityType::Solid3D;
        case acis::BodyKind::Region: return hdxf::EntityType::Region;
        case acis::BodyKind::ClosedBody:
        case acis::BodyKind::OpenBody: return hdxf::EntityType::Body;
    }
    return hdxf::EntityType::Solid3D;
}

hdxf::Drawing drawing_of(const acis::SatDocument& document, hdxf::DxfVersion version) {
    hdxf::Drawing drawing(version);
    drawing.add_acis(entity_type_of(document.kind), acis_data(document));
    return drawing;
}

hdxf::Drawing reloaded(hdxf::Drawing& drawing, hdxf::DxfFormat format) {
    std::vector<uint8_t> bytes = drawing.save_to_memory(format);
    hdxf_test::assert_true(!bytes.empty(), "save_to_memory: " + drawing.last_error());
    hdxf::Drawing out;
    hdxf_test::assert_true(out.load_from_memory(bytes.data(), bytes.size()), "load_from_memory: " + out.last_error());
    return out;
}

std::vector<const hdxf::Entity*> bodies_of(const hdxf::Drawing& drawing) {
    std::vector<const hdxf::Entity*> out;
    for (const hdxf::Entity& e : drawing.entities()) {
        if (e.type == hdxf::EntityType::Solid3D || e.type == hdxf::EntityType::Region || e.type == hdxf::EntityType::Body) {
            out.push_back(&e);
        }
    }
    return out;
}

const hdxf::Entity& first_body(const hdxf::Drawing& drawing) {
    std::vector<const hdxf::Entity*> all = bodies_of(drawing);
    hdxf_test::assert_true(!all.empty(), "the drawing has no 3DSOLID, REGION or BODY");
    return *all.front();
}

Box extents_of(const hdxf::Drawing& drawing, const hdxf::Entity& entity) {
    Box box;
    hdxf_test::assert_true(drawing.entity_extents(entity, box.min, box.max), "the entity has no extents");
    return box;
}

void require_near(double actual, double expected, double tolerance, const std::string& what) {
    char text[200];
    std::snprintf(text, sizeof text, "%s: %.17g, expected %.17g (±%g)", what.c_str(), actual, expected, tolerance);
    hdxf_test::assert_true(std::fabs(actual - expected) <= tolerance, text);
}

void require_box(const Box& actual, const Box& expected, double tolerance, const std::string& what) {
    require_near(actual.min.x, expected.min.x, tolerance, what + " min.x");
    require_near(actual.min.y, expected.min.y, tolerance, what + " min.y");
    require_near(actual.min.z, expected.min.z, tolerance, what + " min.z");
    require_near(actual.max.x, expected.max.x, tolerance, what + " max.x");
    require_near(actual.max.y, expected.max.y, tolerance, what + " max.y");
    require_near(actual.max.z, expected.max.z, tolerance, what + " max.z");
}

// ---------------------------------------------------------------------------
// Boxes
// ---------------------------------------------------------------------------

BoxModel build_box(acis::BodyBuilder& b, double sx, double sy, double sz, const BoxFlags& flags,
                   const BoxPlacement& placement) {
    BoxModel m;
    m.body = placement.body.valid() ? placement.body : b.create_body(placement.kind);
    m.lump = placement.lump.valid() ? placement.lump : b.add_lump(m.body);
    acis::ShellOptions shell_options;
    shell_options.role = placement.role;
    m.shell = b.add_shell(m.lump, shell_options);

    const double size[3] = {sx, sy, sz};
    auto position = [&](int i) {
        return point(placement.origin.x + ((i & 1) ? sx : 0.0), placement.origin.y + ((i & 2) ? sy : 0.0),
                     placement.origin.z + ((i & 4) ? sz : 0.0));
    };
    for (int i = 0; i < 8; ++i) {
        hdxf::Point3 p = position(i);
        if (flags.skew_vertex && i == 7) p.x += 0.5;
        m.corners.push_back(b.add_vertex(p));
    }
    const hdxf::Point3 axes[3] = {point(1, 0, 0), point(0, 1, 0), point(0, 0, 1)};
    std::map<std::pair<int, int>, acis::EdgeId> between;
    for (int axis = 0; axis < 3; ++axis) {
        for (int i = 0; i < 8; ++i) {
            if ((i >> axis) & 1) continue;
            int j = i | (1 << axis);
            acis::EdgeDefinition edge;
            edge.start = m.corners[static_cast<size_t>(i)];
            edge.end = m.corners[static_cast<size_t>(j)];
            if (flags.bspline_edge && m.edges.empty()) {
                acis::BSplineCurveGeometry curve;
                curve.degree = 1;
                curve.knots = {0.0, 0.0, size[axis], size[axis]};
                curve.points = {position(i), position(j)};
                edge.curve = b.add_curve(curve);
            } else {
                acis::LineGeometry line;
                line.origin = position(i);
                line.direction = axes[axis];
                edge.curve = b.add_curve(line);
            }
            edge.start_parameter = 0.0;
            edge.end_parameter = size[axis];
            acis::EdgeId id = b.add_edge(edge);
            between[{i, j}] = id;
            m.edges.push_back(id);
        }
    }

    auto make_face = [&](int axis, int sign, bool flip, bool save) {
        int a1 = (axis + 1) % 3;
        int a2 = (axis + 2) % 3;
        int pa = sign > 0 ? a1 : a2;
        int qa = sign > 0 ? a2 : a1;
        acis::PlaneGeometry plane;
        double origin[3] = {placement.origin.x, placement.origin.y, placement.origin.z};
        if (sign > 0) origin[axis] += size[axis];
        plane.origin = point(origin[0], origin[1], origin[2]);
        plane.normal = point(axes[axis].x * sign, axes[axis].y * sign, axes[axis].z * sign);
        plane.u_direction = axes[pa];
        acis::FaceDefinition definition;
        definition.surface = b.add_surface(plane);
        definition.reversed = flags.invert;
        acis::FaceId face = b.add_face(m.shell, definition);
        acis::LoopId loop = b.add_loop(face, acis::LoopRole::Outer);
        const int corner_steps[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        int ring[4];
        for (int k = 0; k < 4; ++k) {
            ring[k] = ((sign > 0 ? 1 : 0) << axis) | (corner_steps[k][0] << pa) | (corner_steps[k][1] << qa);
        }
        std::vector<std::pair<int, int>> steps;
        for (int k = 0; k < 4; ++k) steps.push_back({ring[k], ring[(k + 1) % 4]});
        if (flip != flags.invert) {
            std::reverse(steps.begin(), steps.end());
            for (auto& s : steps) std::swap(s.first, s.second);
        }
        std::vector<acis::CoedgeId> coedges;
        for (const auto& [from, to] : steps) {
            acis::CoedgeDefinition coedge;
            coedge.edge = between[{std::min(from, to), std::max(from, to)}];
            coedge.reversed = from > to;
            coedges.push_back(b.add_coedge(loop, coedge));
        }
        if (save) {
            m.faces.push_back(face);
            m.loops.push_back(loop);
            m.coedges.push_back(coedges);
        }
    };

    int index = 0;
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign = -1; sign <= 1; sign += 2, ++index) {
            if (index == flags.omit_face) {
                m.faces.push_back({});
                m.loops.push_back({});
                m.coedges.emplace_back();
                continue;
            }
            make_face(axis, sign, index == flags.flip_face, true);
        }
    }
    if (flags.extra_face) make_face(2, -1, false, false);
    if (flags.wrong_pair_face >= 0) {
        size_t f = static_cast<size_t>(flags.wrong_pair_face);
        size_t g = (f + 2) % 6;
        b.pair_coedges(m.coedges[f][0], m.coedges[g][0]);
    }
    return m;
}

acis::Result<acis::SatDocument> build_hole_block(bool correct, bool autocad_frame) {
    acis::BodyBuilder b;
    // As AutoCAD saves it: the geometry around the body's center, the body transform to where it stands
    const hdxf::Point3 shift = autocad_frame ? point(-15.0, -15.0, -12.0) : point(0.0, 0.0, 0.0);
    BoxPlacement place;
    place.origin = shift;
    BoxModel m = build_box(b, 30.0, 30.0, 24.0, BoxFlags{}, place);
    if (autocad_frame) {
        acis::Frame frame;
        frame.origin = point(15.0, 15.0, 12.0);
        require_ok(b.set_transform(m.body, acis::transform_from_frame(frame)), "the hole block's transform");
    }

    acis::VertexId v_top = b.add_vertex(point(21.0 + shift.x, 15.0 + shift.y, 24.0 + shift.z));
    acis::VertexId v_bottom = b.add_vertex(point(21.0 + shift.x, 15.0 + shift.y, 0.0 + shift.z));
    auto circle_edge = [&](double z, acis::VertexId vertex) {
        acis::EllipseGeometry circle;
        circle.center = point(15.0 + shift.x, 15.0 + shift.y, z + shift.z);
        circle.normal = point(0.0, 0.0, 1.0);
        circle.major_axis = point(6.0, 0.0, 0.0);
        acis::EdgeDefinition edge;
        edge.start = vertex;
        edge.end = vertex;
        edge.curve = b.add_curve(circle);
        edge.start_parameter = 0.0;
        edge.end_parameter = kTwoPi;
        return b.add_edge(edge);
    };
    acis::EdgeId e_top = circle_edge(24.0, v_top);
    acis::EdgeId e_bottom = circle_edge(0.0, v_bottom);

    // The top face's normal is +z: its hole runs clockwise, against the circle. The bottom face's
    // normal is -z: its hole runs with the circle.
    acis::CoedgeDefinition top_hole;
    top_hole.edge = e_top;
    top_hole.reversed = correct;
    b.add_coedge(b.add_loop(m.faces[5], acis::LoopRole::Inner), top_hole);
    acis::CoedgeDefinition bottom_hole;
    bottom_hole.edge = e_bottom;
    bottom_hole.reversed = !correct;
    b.add_coedge(b.add_loop(m.faces[4], acis::LoopRole::Inner), bottom_hole);

    // The hole's wall: a cylinder whose normal points away from the axis, the face turned round so
    // that it points into the hole, out of the material
    acis::ConeGeometry wall;
    wall.center = point(15.0 + shift.x, 15.0 + shift.y, 12.0 + shift.z);
    wall.axis = point(0.0, 0.0, 1.0);
    wall.major_axis = point(6.0, 0.0, 0.0);
    acis::FaceDefinition wall_face;
    wall_face.surface = b.add_surface(wall);
    wall_face.reversed = true;
    acis::FaceId face = b.add_face(m.shell, wall_face);
    acis::CoedgeDefinition wall_top;
    wall_top.edge = e_top;
    wall_top.reversed = !correct;
    b.add_coedge(b.add_loop(face, acis::LoopRole::Boundary), wall_top);
    acis::CoedgeDefinition wall_bottom;
    wall_bottom.edge = e_bottom;
    wall_bottom.reversed = correct;
    b.add_coedge(b.add_loop(face, acis::LoopRole::Boundary), wall_bottom);
    return b.build_sat(m.body);
}

acis::Result<acis::SatDocument> build_two_lumps(hdxf::Point3 origin) {
    acis::BodyBuilder b;
    BoxPlacement first_place;
    first_place.origin = origin;
    BoxModel first = build_box(b, 20.0, 20.0, 20.0, BoxFlags{}, first_place);
    BoxPlacement second;
    second.origin = point(origin.x + 35.0, origin.y, origin.z);
    second.body = first.body;
    build_box(b, 20.0, 20.0, 20.0, BoxFlags{}, second);
    return b.build_sat(first.body);
}

acis::Result<acis::SatDocument> build_cavity_cube(bool autocad_frame, hdxf::Point3 center) {
    acis::BodyBuilder b;
    BoxPlacement outer_place;
    if (autocad_frame) outer_place.origin = point(-20.0, -20.0, -20.0);
    BoxModel outer = build_box(b, 40.0, 40.0, 40.0, BoxFlags{}, outer_place);
    if (autocad_frame) {
        acis::Frame frame;
        frame.origin = center;
        require_ok(b.set_transform(outer.body, acis::transform_from_frame(frame)), "the cavity cube's transform");
    }
    BoxPlacement cavity;
    cavity.origin = autocad_frame ? point(-5.0, -5.0, -5.0) : point(15.0, 15.0, 15.0);
    cavity.body = outer.body;
    cavity.lump = outer.lump;
    cavity.role = acis::ShellRole::Void;
    BoxFlags flags;
    flags.invert = true;
    build_box(b, 10.0, 10.0, 10.0, flags, cavity);
    return b.build_sat(outer.body);
}

// A sphere cut through its center, the upper half kept, as AutoCAD's SLICE saves it
acis::Result<acis::SatDocument> build_hemisphere(double radius, hdxf::Point3 center) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::Solid);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::Frame frame;
    frame.origin = center;
    require_ok(b.set_transform(body, acis::transform_from_frame(frame)), "the hemisphere's transform");

    acis::SphereGeometry sphere;
    sphere.radius = radius;
    acis::FaceId dome = b.add_face(shell, acis::FaceDefinition{b.add_surface(sphere), false});
    // The cut is a plane facing down: the plane's normal is up, the face turned round
    acis::PlaneGeometry plane;
    acis::FaceId cut = b.add_face(shell, acis::FaceDefinition{b.add_surface(plane), true});

    // The rim: one closed edge from -pi to pi, its curve's normal down and its major axis along -y, as AutoCAD's
    acis::EllipseGeometry circle;
    circle.normal = point(0.0, 0.0, -1.0);
    circle.major_axis = point(0.0, -radius, 0.0);
    acis::VertexId vertex = b.add_vertex(point(0.0, radius, 0.0));
    acis::EdgeDefinition rim;
    rim.start = vertex;
    rim.end = vertex;
    rim.curve = b.add_curve(circle);
    rim.start_parameter = -kPi;
    rim.end_parameter = kPi;
    acis::EdgeId edge = b.add_edge(rim);

    acis::CoedgeDefinition on_dome;
    on_dome.edge = edge;
    on_dome.reversed = true;
    b.add_coedge(b.add_loop(dome, acis::LoopRole::Boundary), on_dome);
    acis::CoedgeDefinition on_cut;
    on_cut.edge = edge;
    b.add_coedge(b.add_loop(cut, acis::LoopRole::Outer), on_cut);
    return b.build_sat(body);
}

// ---------------------------------------------------------------------------
// Regions
// ---------------------------------------------------------------------------

acis::Result<acis::SatDocument> build_rectangle_region(double x0, double y0, double x1, double y1, acis::BodyKind kind) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(kind);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::PlaneGeometry plane;
    plane.origin = point(x0, y0, 0.0);
    plane.normal = point(0.0, 0.0, 1.0);
    plane.u_direction = point(1.0, 0.0, 0.0);
    acis::FaceDefinition face_definition;
    face_definition.surface = b.add_surface(plane);
    acis::FaceId face = b.add_face(shell, face_definition);
    acis::LoopId loop = b.add_loop(face, acis::LoopRole::Outer);

    const hdxf::Point3 corners[4] = {point(x0, y0, 0.0), point(x1, y0, 0.0), point(x1, y1, 0.0), point(x0, y1, 0.0)};
    acis::VertexId vertices[4];
    for (int i = 0; i < 4; ++i) vertices[i] = b.add_vertex(corners[i]);
    for (int i = 0; i < 4; ++i) {
        const hdxf::Point3& from = corners[i];
        const hdxf::Point3& to = corners[(i + 1) % 4];
        double length = std::hypot(to.x - from.x, to.y - from.y);
        acis::LineGeometry line;
        line.origin = from;
        line.direction = point((to.x - from.x) / length, (to.y - from.y) / length, 0.0);
        acis::EdgeDefinition edge;
        edge.start = vertices[i];
        edge.end = vertices[(i + 1) % 4];
        edge.curve = b.add_curve(line);
        edge.start_parameter = 0.0;
        edge.end_parameter = length;
        acis::CoedgeDefinition coedge;
        coedge.edge = b.add_edge(edge);
        b.add_coedge(loop, coedge);
    }
    return b.build_sat(body);
}

acis::Result<acis::SatDocument> build_circle_region(hdxf::Point3 center, double radius) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::Region);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    acis::PlaneGeometry plane;
    plane.origin = center;
    acis::FaceDefinition face_definition;
    face_definition.surface = b.add_surface(plane);
    acis::LoopId loop = b.add_loop(b.add_face(shell, face_definition), acis::LoopRole::Outer);

    acis::VertexId vertex = b.add_vertex(point(center.x + radius, center.y, center.z));
    acis::EllipseGeometry circle;
    circle.center = center;
    circle.major_axis = point(radius, 0.0, 0.0);
    acis::EdgeDefinition edge;
    edge.start = vertex;
    edge.end = vertex;
    edge.curve = b.add_curve(circle);
    edge.start_parameter = 0.0;
    edge.end_parameter = kTwoPi;
    acis::CoedgeDefinition coedge;
    coedge.edge = b.add_edge(edge);
    b.add_coedge(loop, coedge);
    return b.build_sat(body);
}

// ---------------------------------------------------------------------------
// A cylinder with a seam
// ---------------------------------------------------------------------------

acis::Result<acis::SatDocument> build_seam_cylinder(double radius, double height, bool pcurves) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::Solid);
    acis::ShellId shell = b.add_shell(b.add_lump(body));
    const double top = height / 2.0;
    acis::VertexId v_bottom = b.add_vertex(point(radius, 0.0, -top));
    acis::VertexId v_top = b.add_vertex(point(radius, 0.0, top));
    auto circle = [&](double z, acis::VertexId vertex) {
        acis::EllipseGeometry c;
        c.center = point(0.0, 0.0, z);
        c.major_axis = point(radius, 0.0, 0.0);
        acis::EdgeDefinition edge;
        edge.start = vertex;
        edge.end = vertex;
        edge.curve = b.add_curve(c);
        edge.end_parameter = kTwoPi;
        return b.add_edge(edge);
    };
    acis::EdgeId e_bottom = circle(-top, v_bottom);
    acis::EdgeId e_top = circle(top, v_top);
    acis::LineGeometry line;
    line.origin = point(radius, 0.0, -top);
    line.direction = point(0.0, 0.0, 1.0);
    acis::EdgeDefinition seam_definition;
    seam_definition.start = v_bottom;
    seam_definition.end = v_top;
    seam_definition.curve = b.add_curve(line);
    seam_definition.end_parameter = height;
    acis::EdgeId seam = b.add_edge(seam_definition);

    auto cap = [&](double z, double normal_z, acis::EdgeId circle_edge, bool reversed) {
        acis::PlaneGeometry plane;
        plane.origin = point(0.0, 0.0, z);
        plane.normal = point(0.0, 0.0, normal_z);
        acis::FaceDefinition definition;
        definition.surface = b.add_surface(plane);
        acis::CoedgeDefinition coedge;
        coedge.edge = circle_edge;
        coedge.reversed = reversed;
        b.add_coedge(b.add_loop(b.add_face(shell, definition), acis::LoopRole::Outer), coedge);
    };
    cap(-top, -1.0, e_bottom, true);
    cap(top, 1.0, e_top, false);

    acis::ConeGeometry wall;
    wall.major_axis = point(radius, 0.0, 0.0);
    acis::FaceDefinition wall_face;
    wall_face.surface = b.add_surface(wall);
    acis::LoopId loop = b.add_loop(b.add_face(shell, wall_face), acis::LoopRole::Boundary);
    // The cone-surface's u is the distance along the axis over |major axis|, v the angle
    auto pcurve = [&](double u0, double v0, double u1, double v1, double t_end) {
        acis::BSplineCurve2D p;
        p.degree = 1;
        p.knots = {0.0, 0.0, t_end, t_end};
        p.points = {hdxf::Point2{u0, v0}, hdxf::Point2{u1, v1}};
        return b.add_pcurve(p);
    };
    const double u0 = -top / radius;
    const double u1 = top / radius;
    acis::CoedgeDefinition c;
    c.edge = e_bottom;
    if (pcurves) c.pcurve = pcurve(u0, 0.0, u0, kTwoPi, kTwoPi);
    b.add_coedge(loop, c);
    c.edge = seam;
    if (pcurves) c.pcurve = pcurve(u0, kTwoPi, u1, kTwoPi, height);
    b.add_coedge(loop, c);
    c.edge = e_top;
    c.reversed = true;
    if (pcurves) c.pcurve = pcurve(u1, 0.0, u1, kTwoPi, kTwoPi);
    b.add_coedge(loop, c);
    c.edge = seam;
    c.reversed = true;
    if (pcurves) c.pcurve = pcurve(u0, 0.0, u1, 0.0, height);
    b.add_coedge(loop, c);
    return b.build_sat(body);
}

// ---------------------------------------------------------------------------
// A cylinder with an exact NURBS wall
// ---------------------------------------------------------------------------

acis::Result<acis::SatDocument> build_nurbs_cylinder(double radius, double height, const NurbsFlags& flags) {
    acis::BodyBuilder b;
    acis::BodyId body = b.create_body(acis::BodyKind::Solid);
    acis::ShellId shell = b.add_shell(b.add_lump(body));

    // The circle as four rational quadratic arcs: corners and mid points of the square round it
    const double w = std::sqrt(0.5);
    const double ring[9][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {1, 0}};
    const double weights[9] = {1, w, 1, w, 1, w, 1, w, 1};
    const std::vector<double> circle_knots = {0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1};

    auto circle_curve = [&](double z) {
        acis::BSplineCurveGeometry curve;
        curve.degree = 2;
        curve.knots = circle_knots;
        for (int i = 0; i < 9; ++i) {
            curve.points.push_back(point(ring[i][0] * radius, ring[i][1] * radius, z));
            curve.weights.push_back(weights[i]);
        }
        curve.closure = acis::Closure::Closed;
        return curve;
    };

    acis::BSplineSurfaceGeometry wall;
    wall.degree_u = 2;
    wall.degree_v = 1;
    wall.count_u = 9;
    wall.count_v = 2;
    wall.knots_u = circle_knots;
    wall.knots_v = {0.0, 0.0, 1.0, 1.0};
    for (int v = 0; v < 2; ++v) {
        for (int i = 0; i < 9; ++i) {
            wall.points.push_back(point(ring[i][0] * radius, ring[i][1] * radius, v * height));
            wall.weights.push_back(weights[i]);
        }
    }
    wall.closure_u = acis::Closure::Closed;

    acis::VertexId v_bottom = b.add_vertex(point(radius, 0.0, 0.0));
    acis::VertexId v_top = b.add_vertex(point(radius, 0.0, height));
    auto circle_edge = [&](double z, acis::VertexId vertex) {
        acis::EdgeDefinition edge;
        edge.start = vertex;
        edge.end = vertex;
        edge.curve = b.add_curve(circle_curve(z));
        edge.start_parameter = 0.0;
        edge.end_parameter = 1.0;
        return b.add_edge(edge);
    };
    acis::EdgeId e_bottom = circle_edge(0.0, v_bottom);
    acis::EdgeId e_top = circle_edge(height, v_top);
    acis::LineGeometry seam_line;
    seam_line.origin = point(radius, 0.0, 0.0);
    seam_line.direction = point(0.0, 0.0, 1.0);
    acis::EdgeDefinition seam_definition;
    seam_definition.start = v_bottom;
    seam_definition.end = v_top;
    seam_definition.curve = b.add_curve(seam_line);
    seam_definition.start_parameter = 0.0;
    seam_definition.end_parameter = height;
    acis::EdgeId seam = b.add_edge(seam_definition);

    auto cap = [&](double z, double normal_z, acis::EdgeId circle, bool reversed) {
        acis::PlaneGeometry plane;
        plane.origin = point(0.0, 0.0, z);
        plane.normal = point(0.0, 0.0, normal_z);
        acis::FaceDefinition face_definition;
        face_definition.surface = b.add_surface(plane);
        acis::LoopId loop = b.add_loop(b.add_face(shell, face_definition), acis::LoopRole::Outer);
        acis::CoedgeDefinition coedge;
        coedge.edge = circle;
        coedge.reversed = reversed;
        b.add_coedge(loop, coedge);
    };
    cap(0.0, -1.0, e_bottom, true);
    cap(height, 1.0, e_top, false);

    // The wall: the rectangle of its parameters counter-clockwise: the bottom circle, the seam up, the
    // top circle back, the seam down. Each coedge has its pcurve, in the edge's own parameter.
    auto straight = [&](double u0, double v0, double u1, double v1, double t_end) {
        acis::BSplineCurve2D pcurve;
        pcurve.degree = 1;
        pcurve.knots = {0.0, 0.0, t_end, t_end};
        pcurve.points = {hdxf::Point2{u0, v0}, hdxf::Point2{u1, v1}};
        return b.add_pcurve(pcurve);
    };
    acis::FaceDefinition wall_face;
    wall_face.surface = b.add_surface(wall);
    acis::LoopId loop = b.add_loop(b.add_face(shell, wall_face), acis::LoopRole::Boundary);
    acis::CoedgeDefinition c;
    c.edge = e_bottom;
    c.reversed = false;
    acis::PcurveId bottom_pcurve = straight(0.0, flags.bottom_pcurve_v, 1.0, flags.bottom_pcurve_v, 1.0);
    if (flags.pcurves) c.pcurve = bottom_pcurve;
    b.add_coedge(loop, c);
    c.edge = seam;
    c.reversed = false;
    if (flags.pcurves) c.pcurve = straight(1.0, 0.0, 1.0, 1.0, height);
    b.add_coedge(loop, c);
    c.edge = e_top;
    c.reversed = true;
    if (flags.pcurves) c.pcurve = flags.share_pcurve ? bottom_pcurve : straight(0.0, 1.0, 1.0, 1.0, 1.0);
    b.add_coedge(loop, c);
    c.edge = seam;
    c.reversed = true;
    if (flags.pcurves) c.pcurve = straight(0.0, 0.0, 0.0, 1.0, height);
    b.add_coedge(loop, c);
    return b.build_sat(body);
}

// ---------------------------------------------------------------------------
// The facit
// ---------------------------------------------------------------------------

std::filesystem::path facit_path(const std::string& name) {
    return std::filesystem::path(HDXF_ACIS_FACIT_DIR) / name;
}

hdxf::Drawing load_facit(const std::string& name) {
    std::filesystem::path path = facit_path(name);
    if (!std::filesystem::exists(path)) hdxf_test::skip("AutoCAD facit " + name + " is not there (doc/facit)");
    hdxf::Drawing drawing;
    hdxf_test::assert_true(drawing.load(path), "load " + name + ": " + drawing.last_error());
    return drawing;
}

std::vector<const hdxf::Entity*> facit_bodies(const hdxf::Drawing& drawing) { return bodies_of(drawing); }

FacitPrimitives find_facit_primitives(const hdxf::Drawing& drawing) {
    FacitPrimitives found;
    auto matches = [](const Box& box, const Box& expected) {
        const double tolerance = 1e-6;
        return std::fabs(box.min.x - expected.min.x) < tolerance && std::fabs(box.min.y - expected.min.y) < tolerance &&
               std::fabs(box.min.z - expected.min.z) < tolerance && std::fabs(box.max.x - expected.max.x) < tolerance &&
               std::fabs(box.max.y - expected.max.y) < tolerance && std::fabs(box.max.z - expected.max.z) < tolerance;
    };
    // 10_solids.scr: BOX (0,2.5,0)-(20,17.5) h 20, SPHERE (50,10,10) r 12, CYLINDER (90,10,0) r 10 h 25,
    // CONE (130,10,0) r 12 h 30, TORUS (170,10,12) R 15 r 4, the rectangle (0,60)-(30,80) and the
    // circle (170,145) r 15 as REGIONs
    const std::pair<const hdxf::Entity**, Box> wanted[] = {
        {&found.box, Box{point(0, 2.5, 0), point(20, 17.5, 20)}},
        {&found.sphere, Box{point(38, -2, -2), point(62, 22, 22)}},
        {&found.cylinder, Box{point(80, 0, 0), point(100, 20, 25)}},
        {&found.cone, Box{point(118, -2, 0), point(142, 22, 30)}},
        {&found.torus, Box{point(151, -9, 8), point(189, 29, 16)}},
        {&found.region_rectangle, Box{point(0, 60, 0), point(30, 80, 0)}},
        {&found.region_circle, Box{point(155, 130, 0), point(185, 160, 0)}},
    };
    for (const hdxf::Entity* e : bodies_of(drawing)) {
        hdxf::Point3 min, max;
        if (!drawing.entity_extents(*e, min, max)) continue;
        Box box{min, max};
        for (const auto& [slot, expected] : wanted) {
            if (!*slot && matches(box, expected)) *slot = e;
        }
    }
    return found;
}

} // namespace acis_test
