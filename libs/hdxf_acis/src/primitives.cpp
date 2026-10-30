#include "hdxf_acis.hpp"

#include "format.hpp"
#include "geometry.hpp"
#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace hdxf::acis {

// The primitives are built through the public builder, so they pass the same checks as any body.
// Each is made around the origin, as AutoCAD's primitives are (the geometry centered, a body
// transform to where it stands): the box, cylinder and cone centered on the origin, the cone's
// apex on +z, the torus around the z axis.

namespace {

using detail::kTwoPi;

Diagnostic bad_argument(const std::string& function, const std::string& message) {
    return make_diagnostic(ErrorCode::InvalidArgument, {}, {}, function + ": " + message);
}

bool positive(double value) { return std::isfinite(value) && value > 0.0; }

// Why a frame cannot be a body transform: the axes are not perpendicular, or have no length
std::string frame_problem(const Frame& frame, const Tolerances& tolerance) {
    if (!detail::finite(frame.origin) || !detail::finite(frame.z_axis) || !detail::finite(frame.x_axis)) {
        return "the frame is not finite";
    }
    if (!(detail::length(frame.z_axis) > 0.0) || !(detail::length(frame.x_axis) > 0.0)) {
        return "the frame's axes need a direction";
    }
    if (!detail::perpendicular(detail::normalized(frame.z_axis), detail::normalized(frame.x_axis), tolerance.direction)) {
        return "the frame's z axis and x axis are not perpendicular";
    }
    return {};
}

bool is_identity_frame(const Frame& frame) {
    const double tiny = 1e-15;
    return std::fabs(frame.origin.x) < tiny && std::fabs(frame.origin.y) < tiny && std::fabs(frame.origin.z) < tiny &&
           std::fabs(frame.z_axis.x) < tiny && std::fabs(frame.z_axis.y) < tiny && std::fabs(frame.z_axis.z - 1.0) < tiny &&
           std::fabs(frame.x_axis.x - 1.0) < tiny && std::fabs(frame.x_axis.y) < tiny && std::fabs(frame.x_axis.z) < tiny;
}

// Puts the body where the frame says and writes it
Result<SatDocument> finish(BodyBuilder& builder, BodyId body, const Frame& frame) {
    if (!is_identity_frame(frame)) {
        Result<void> placed = builder.set_transform(body, transform_from_frame(frame));
        if (!placed) return placed.errors();
    }
    return builder.build_sat(body);
}

// The start of every primitive: the checks of the frame, and a builder with a solid body
struct Start {
    BodyBuilder builder;
    BodyId body;
    ShellId shell;

    explicit Start(const BuildOptions& options) : builder(options) {
        body = builder.create_body(BodyKind::Solid);
        shell = builder.add_shell(builder.add_lump(body));
    }
};

const Vector3 kAxes[3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};

Point3 point_from(const double v[3]) { return {v[0], v[1], v[2]}; }

CurveId add_circle(BodyBuilder& b, double z, double radius) {
    EllipseGeometry circle;
    circle.center = {0.0, 0.0, z};
    circle.normal = {0.0, 0.0, 1.0};
    circle.major_axis = {radius, 0.0, 0.0};
    circle.ratio = 1.0;
    return b.add_curve(circle);
}

PlaneGeometry plane_at(double z, double normal_z) {
    PlaneGeometry plane;
    plane.origin = {0.0, 0.0, z};
    plane.normal = {0.0, 0.0, normal_z};
    plane.u_direction = {1.0, 0.0, 0.0};
    return plane;
}

// A closed edge round a circle of the given radius at z, from the vertex at angle 0
EdgeId add_circle_edge(BodyBuilder& b, double z, double radius, VertexId vertex) {
    EdgeDefinition edge;
    edge.start = vertex;
    edge.end = vertex;
    edge.curve = add_circle(b, z, radius);
    edge.start_parameter = 0.0;
    edge.end_parameter = kTwoPi;
    return b.add_edge(edge);
}

// A planar cap with one circular boundary: the normal points along +z (up) or -z
void add_cap(BodyBuilder& b, ShellId shell, double z, bool up, EdgeId circle) {
    FaceId face = b.add_face(shell, FaceDefinition{b.add_surface(plane_at(z, up ? 1.0 : -1.0)), false});
    LoopId loop = b.add_loop(face, LoopRole::Outer);
    // Counter-clockwise about the normal: with the circle's own direction when the normal is +z
    CoedgeDefinition coedge;
    coedge.edge = circle;
    coedge.reversed = !up;
    b.add_coedge(loop, coedge);
}

} // namespace

Result<SatDocument> make_box(BoxParameters p) {
    const double sizes[3] = {p.size_x, p.size_y, p.size_z};
    for (double s : sizes) {
        if (!positive(s)) return bad_argument("make_box", "the sizes must be positive");
    }
    std::string problem = frame_problem(p.frame, p.options.tolerance);
    if (!problem.empty()) return bad_argument("make_box", problem);

    Start start(p.options);
    BodyBuilder& b = start.builder;
    const double half[3] = {p.size_x / 2.0, p.size_y / 2.0, p.size_z / 2.0};

    // The corners: bit k of the index says + (1) or - (0) on axis k
    VertexId corner[8];
    for (int i = 0; i < 8; ++i) {
        double v[3];
        for (int k = 0; k < 3; ++k) v[k] = ((i >> k) & 1) ? half[k] : -half[k];
        corner[i] = b.add_vertex(point_from(v));
    }
    // The twelve edges, each from the corner with - on its axis to the one with +
    std::map<std::pair<int, int>, EdgeId> edge_between;
    for (int axis = 0; axis < 3; ++axis) {
        for (int i = 0; i < 8; ++i) {
            if ((i >> axis) & 1) continue;
            int j = i | (1 << axis);
            double v[3];
            for (int k = 0; k < 3; ++k) v[k] = ((i >> k) & 1) ? half[k] : -half[k];
            LineGeometry line;
            line.origin = point_from(v);
            line.direction = kAxes[axis];
            EdgeDefinition edge;
            edge.start = corner[i];
            edge.end = corner[j];
            edge.curve = b.add_curve(line);
            edge.start_parameter = 0.0;
            edge.end_parameter = sizes[axis];
            edge_between[{i, j}] = b.add_edge(edge);
        }
    }
    // The six faces. Seen from outside (along -normal) the corners run counter-clockwise: p × q is the
    // normal for the in-plane axes (p, q).
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign = -1; sign <= 1; sign += 2) {
            int a1 = (axis + 1) % 3;
            int a2 = (axis + 2) % 3;
            int pa = sign > 0 ? a1 : a2;
            int qa = sign > 0 ? a2 : a1;
            PlaneGeometry plane;
            double origin[3] = {0.0, 0.0, 0.0};
            origin[axis] = sign * half[axis];
            plane.origin = point_from(origin);
            plane.normal = detail::scaled(kAxes[axis], sign);
            plane.u_direction = kAxes[pa];
            FaceId face = b.add_face(start.shell, FaceDefinition{b.add_surface(plane), false});
            LoopId loop = b.add_loop(face, LoopRole::Outer);
            const int steps[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            int ring[4];
            for (int k = 0; k < 4; ++k) {
                ring[k] = ((sign > 0 ? 1 : 0) << axis) | (steps[k][0] << pa) | (steps[k][1] << qa);
            }
            for (int k = 0; k < 4; ++k) {
                int from = ring[k];
                int to = ring[(k + 1) % 4];
                CoedgeDefinition coedge;
                coedge.edge = edge_between[{std::min(from, to), std::max(from, to)}];
                coedge.reversed = from > to;
                b.add_coedge(loop, coedge);
            }
        }
    }
    return finish(b, start.body, p.frame);
}

Result<SatDocument> make_cylinder(CylinderParameters p) {
    if (!positive(p.radius) || !positive(p.height)) {
        return bad_argument("make_cylinder", "the radius and the height must be positive");
    }
    std::string problem = frame_problem(p.frame, p.options.tolerance);
    if (!problem.empty()) return bad_argument("make_cylinder", problem);

    Start start(p.options);
    BodyBuilder& b = start.builder;
    double top = p.height / 2.0;
    VertexId v_top = b.add_vertex({p.radius, 0.0, top});
    VertexId v_bottom = b.add_vertex({p.radius, 0.0, -top});
    EdgeId e_top = add_circle_edge(b, top, p.radius, v_top);
    EdgeId e_bottom = add_circle_edge(b, -top, p.radius, v_bottom);

    add_cap(b, start.shell, top, true, e_top);
    add_cap(b, start.shell, -top, false, e_bottom);

    ConeGeometry wall;
    wall.center = {0.0, 0.0, 0.0};
    wall.axis = {0.0, 0.0, 1.0};
    wall.major_axis = {p.radius, 0.0, 0.0};
    wall.radius_gradient = 0.0;
    FaceId face = b.add_face(start.shell, FaceDefinition{b.add_surface(wall), false});
    // The wall's boundary runs against the top cap's circle and with the bottom cap's
    b.add_coedge(b.add_loop(face, LoopRole::Boundary), CoedgeDefinition{e_top, true, {}});
    b.add_coedge(b.add_loop(face, LoopRole::Boundary), CoedgeDefinition{e_bottom, false, {}});
    return finish(b, start.body, p.frame);
}

Result<SatDocument> make_cone(ConeParameters p) {
    if (!positive(p.radius) || !positive(p.height) || !std::isfinite(p.top_radius) || p.top_radius < 0.0) {
        return bad_argument("make_cone", "the radius and the height must be positive, the top radius not negative");
    }
    std::string problem = frame_problem(p.frame, p.options.tolerance);
    if (!problem.empty()) return bad_argument("make_cone", problem);

    Start start(p.options);
    BodyBuilder& b = start.builder;
    double top = p.height / 2.0;
    bool apex = p.top_radius == 0.0;

    VertexId v_bottom = b.add_vertex({p.radius, 0.0, -top});
    EdgeId e_bottom = add_circle_edge(b, -top, p.radius, v_bottom);
    add_cap(b, start.shell, -top, false, e_bottom);

    // The cone's reference circle is at the middle of the axis
    ConeGeometry wall;
    wall.center = {0.0, 0.0, 0.0};
    wall.axis = {0.0, 0.0, 1.0};
    wall.major_axis = {(p.radius + p.top_radius) / 2.0, 0.0, 0.0};
    wall.radius_gradient = (p.top_radius - p.radius) / p.height;
    FaceId face = b.add_face(start.shell, FaceDefinition{b.add_surface(wall), false});
    b.add_coedge(b.add_loop(face, LoopRole::Boundary), CoedgeDefinition{e_bottom, false, {}});

    if (apex) {
        // The apex is a vertex and an edge without a curve that the wall's second loop runs through
        VertexId v_apex = b.add_vertex({0.0, 0.0, top});
        EdgeDefinition degenerate;
        degenerate.start = v_apex;
        degenerate.end = v_apex;
        EdgeId e_apex = b.add_edge(degenerate);
        b.add_coedge(b.add_loop(face, LoopRole::Boundary), CoedgeDefinition{e_apex, false, {}});
    } else {
        VertexId v_top = b.add_vertex({p.top_radius, 0.0, top});
        EdgeId e_top = add_circle_edge(b, top, p.top_radius, v_top);
        add_cap(b, start.shell, top, true, e_top);
        b.add_coedge(b.add_loop(face, LoopRole::Boundary), CoedgeDefinition{e_top, true, {}});
    }
    return finish(b, start.body, p.frame);
}

Result<SatDocument> make_sphere(SphereParameters p) {
    if (!positive(p.radius)) return bad_argument("make_sphere", "the radius must be positive");
    std::string problem = frame_problem(p.frame, p.options.tolerance);
    if (!problem.empty()) return bad_argument("make_sphere", problem);

    Start start(p.options);
    SphereGeometry sphere;
    sphere.center = {0.0, 0.0, 0.0};
    sphere.radius = p.radius;
    // One face without loops: the whole sphere, no seam and no poles
    start.builder.add_face(start.shell, FaceDefinition{start.builder.add_surface(sphere), false});
    return finish(start.builder, start.body, p.frame);
}

Result<SatDocument> make_torus(TorusParameters p) {
    if (!positive(p.minor_radius) || !positive(p.major_radius) || !(p.major_radius > p.minor_radius)) {
        return bad_argument("make_torus", "a ring torus needs major radius > minor radius > 0");
    }
    std::string problem = frame_problem(p.frame, p.options.tolerance);
    if (!problem.empty()) return bad_argument("make_torus", problem);

    Start start(p.options);
    TorusGeometry torus;
    torus.center = {0.0, 0.0, 0.0};
    torus.major_radius = p.major_radius;
    torus.minor_radius = p.minor_radius;
    start.builder.add_face(start.shell, FaceDefinition{start.builder.add_surface(torus), false});
    return finish(start.builder, start.body, p.frame);
}

} // namespace hdxf::acis
