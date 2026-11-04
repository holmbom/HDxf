#include "test_framework.hpp"
#include "hdxf.hpp"
#include "acis.hpp"
#include "extents.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The limits of the exact box of ACIS bodies: a piece of an edge that is one point in 3D is
// not refined, what the refinement of an edge found is kept for every later query, and a face
// is skipped only where its surface lies in the box known already. Everything is compared as
// values; the work is read from ExtentsCounters, never from a clock.
// ---------------------------------------------------------------------------

namespace {

using hdxf::Point3;
using hdxf::internal::AcisBody;
using hdxf::internal::AcisCoedge;
using hdxf::internal::AcisCurve;
using hdxf::internal::AcisCurveType;
using hdxf::internal::AcisEdge;
using hdxf::internal::AcisExtentsPass;
using hdxf::internal::AcisFace;
using hdxf::internal::AcisFile;
using hdxf::internal::AcisLoop;
using hdxf::internal::AcisMap;
using hdxf::internal::AcisSurface;
using hdxf::internal::AcisSurfaceType;
using hdxf::internal::Extents;
using hdxf::internal::ExtentsCounters;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

Point3 vec(double x, double y, double z) { return {x, y, z}; }
Point3 plus(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 times(const Point3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
Point3 cross_of(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Extents box(double x0, double y0, double z0, double x1, double y1, double z1) {
    Extents e;
    e.min = {x0, y0, z0};
    e.max = {x1, y1, z1};
    return e;
}

Extents united(const Extents& a, const Extents& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    Extents e = a;
    e.add(b.min);
    e.add(b.max);
    return e;
}

void near(double a, double b, double tol, const std::string& what) {
    char buf[240];
    std::snprintf(buf, sizeof buf, "%s: %.17g != %.17g", what.c_str(), a, b);
    hdxf_test::assert_true(std::fabs(a - b) <= tol, buf);
}

void same_box(const Extents& a, const Extents& b, double tol, const std::string& what) {
    near(a.min.x, b.min.x, tol, what + " min.x");
    near(a.min.y, b.min.y, tol, what + " min.y");
    near(a.min.z, b.min.z, tol, what + " min.z");
    near(a.max.x, b.max.x, tol, what + " max.x");
    near(a.max.y, b.max.y, tol, what + " max.y");
    near(a.max.z, b.max.z, tol, what + " max.z");
}

// What a call of acis_extents left: the box and the work it did
struct Outcome {
    Extents box;
    ExtentsCounters counters;
};

Outcome run(const std::vector<AcisBody>& bodies, const AcisMap& map, AcisExtentsPass pass, const Extents& start = Extents{}) {
    Outcome o;
    o.box = start;
    hdxf::internal::acis_extents(bodies, map, o.box, pass, &o.counters);
    return o;
}

// The faces measured into a box that holds some already, compared with the faces measured
// into an empty one: skipping may only drop what the box holds, so the result must be the
// box united with what the faces give alone
Outcome run_faces_checked(const std::vector<AcisBody>& bodies, const AcisMap& map, const Extents& known,
                          const std::string& what) {
    Outcome alone = run(bodies, map, AcisExtentsPass::Faces);
    Outcome got = run(bodies, map, AcisExtentsPass::Faces, known);
    same_box(got.box, united(known, alone.box), 1e-9, what);
    return got;
}

int count_of(size_t n) { return static_cast<int>(n); }

// ---------------------------------------------------------------------------
// Bodies from SAT text (the shapes the other ACIS tests read)
// ---------------------------------------------------------------------------

const char* kHeader700 = "700 0 1 0\n@4 Test @8 ACIS 7.0 @24 Thu Oct 24 12:00:00 2026\n1 9.9999999999999995e-007 1e-010\n";

std::vector<AcisBody> bodies_of(const std::string& sat) {
    AcisFile file;
    hdxf_test::assert_true(hdxf::internal::read_sat(sat, file), "SAT not read");
    std::vector<AcisBody> bodies = hdxf::internal::acis_bodies(file);
    hdxf_test::assert_eq(static_cast<int>(bodies.size()), 1, "one body");
    return bodies;
}

// A sphere (center 0, radius 1) cut at z = 0.5: the cap above, the north pole in it
std::string sphere_cap() {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n"
        "sphere-surface $-1 -1 $-1 0 0 0 1 1 0 0 0 0 1 forward_v I I I I #\n"
        "coedge $-1 -1 $-1 $6 $6 $-1 $7 forward $4 $-1 #\n"
        "edge $-1 -1 $-1 $8 0 $8 6.2831853071795862 $6 $9 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $7 $10 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0.5 0 0 1 0.8660254037844386 0 0 1 I I #\n"
        "point $-1 -1 $-1 0.8660254037844386 0 0.5 #\n"
        "End-of-ACIS-data\n";
}

// The torus R 3, r 1 about z, cut at z = 0 by its outer and inner equator: the upper half
std::string torus_half() {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $6 forward single #\n"
        "loop $-1 -1 $-1 $5 $7 $3 #\n"
        "loop $-1 -1 $-1 $-1 $8 $3 #\n"
        "torus-surface $-1 -1 $-1 0 0 0 0 0 1 3 1 1 0 0 forward_v I I I I #\n"
        "coedge $-1 -1 $-1 $7 $7 $-1 $9 forward $4 $-1 #\n"
        "coedge $-1 -1 $-1 $8 $8 $-1 $10 reversed $5 $-1 #\n"
        "edge $-1 -1 $-1 $11 0 $11 6.2831853071795862 $7 $13 forward @7 unknown #\n"
        "edge $-1 -1 $-1 $12 0 $12 6.2831853071795862 $8 $14 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $9 $15 #\n"
        "vertex $-1 -1 $-1 $10 $16 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0 0 0 1 4 0 0 1 I I #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0 0 0 1 2 0 0 1 I I #\n"
        "point $-1 -1 $-1 4 0 0 #\n"
        "point $-1 -1 $-1 2 0 0 #\n"
        "End-of-ACIS-data\n";
}

// The dome z = 1 - x² - y² over [-1, 1]² as a biquadratic Bézier patch (exactsur), the face
// the cap inside the circle x² + y² = 0.25 (where z = 0.75); its control net reaches z = 3
std::string spline_dome() {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n"
        "spline-surface $-1 -1 $-1 forward { exactsur full nubs 2 2 open open none none 2 2\n"
        " 0 2 1 2\n"
        " 0 2 1 2\n"
        " -1 -1 -1\n 0 -1 1\n 1 -1 -1\n"
        " -1 0 1\n 0 0 3\n 1 0 1\n"
        " -1 1 -1\n 0 1 1\n 1 1 -1\n"
        " 0\n"
        " } I I I I #\n"
        "coedge $-1 -1 $-1 $6 $6 $-1 $7 forward $4 $-1 #\n"
        "edge $-1 -1 $-1 $8 0 $8 6.2831853071795862 $6 $9 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $7 $10 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0.75 0 0 1 0.5 0 0 1 I I #\n"
        "point $-1 -1 $-1 0.5 0 0.75 #\n"
        "End-of-ACIS-data\n";
}

// A face on the surface (its text) bounded by the polygon through the points, in the order
// given (counterclockwise seen from the surface's normal), each side a straight edge
std::string polygon_face(const std::string& surface, const std::vector<std::array<double, 3>>& points) {
    const int n = static_cast<int>(points.size());
    auto ref = [](int i) { return "$" + std::to_string(i); };
    const int coedge0 = 6, edge0 = 6 + n, vertex0 = 6 + 2 * n, curve0 = 6 + 3 * n, point0 = 6 + 4 * n;
    std::string s = std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n" +
        surface + "\n";
    for (int i = 0; i < n; ++i) {
        s += "coedge $-1 -1 $-1 " + ref(coedge0 + (i + 1) % n) + " " + ref(coedge0 + (i + n - 1) % n) + " $-1 " +
             ref(edge0 + i) + " forward $4 $-1 #\n";
    }
    for (int i = 0; i < n; ++i) {
        s += "edge $-1 -1 $-1 " + ref(vertex0 + i) + " 0 " + ref(vertex0 + (i + 1) % n) + " 2 " + ref(coedge0 + i) +
             " " + ref(curve0 + i) + " forward @7 unknown #\n";
    }
    for (int i = 0; i < n; ++i) s += "vertex $-1 -1 $-1 " + ref(edge0 + i) + " " + ref(point0 + i) + " #\n";
    for (int i = 0; i < n; ++i) {
        const auto& a = points[static_cast<size_t>(i)];
        const auto& b = points[static_cast<size_t>((i + 1) % n)];
        char buf[300];
        std::snprintf(buf, sizeof buf, "straight-curve $-1 -1 $-1 %.17g %.17g %.17g %.17g %.17g %.17g I I #\n", a[0], a[1],
                      a[2], (b[0] - a[0]) / 2.0, (b[1] - a[1]) / 2.0, (b[2] - a[2]) / 2.0);
        s += buf;
    }
    for (int i = 0; i < n; ++i) {
        const auto& a = points[static_cast<size_t>(i)];
        char buf[200];
        std::snprintf(buf, sizeof buf, "point $-1 -1 $-1 %.17g %.17g %.17g #\n", a[0], a[1], a[2]);
        s += buf;
    }
    return s + "End-of-ACIS-data\n";
}

// A tent of degree 1 × 1 (knots 0, 0.5, 1 both ways): x = 2u - 1, y = 2v - 1, z = 0 except
// z = 2 at the middle, where the two knot lines cross. The patches have no critical point.
std::string tent_surface() {
    const char* grid = " -1 -1 0\n 0 -1 0\n 1 -1 0\n -1 0 0\n 0 0 2\n 1 0 0\n -1 1 0\n 0 1 0\n 1 1 0\n";
    return std::string("spline-surface $-1 -1 $-1 forward { exactsur full nubs 1 1 open open none none 3 3\n"
                       " 0 1 0.5 1 1 1\n 0 1 0.5 1 1 1\n") + grid + " 0\n } I I I I #";
}

// ---------------------------------------------------------------------------
// Bodies made as data: a unit sphere with an edge in its face
// ---------------------------------------------------------------------------

AcisSurface unit_sphere() {
    AcisSurface s;
    s.type = AcisSurfaceType::Sphere;
    s.radius = 1.0;
    return s;
}

int add_curve(AcisBody& b, const AcisCurve& c) {
    b.curves.push_back(c);
    return static_cast<int>(b.curves.size()) - 1;
}

int add_edge(AcisBody& b, const AcisEdge& e) {
    b.edges.push_back(e);
    return static_cast<int>(b.edges.size()) - 1;
}

// A straight edge in no face from (-h, -h, -h) to (h, h, h): the body's vertices then span a
// box of a known size, which is what two points are compared with
void add_wire(AcisBody& b, double h) {
    AcisCurve c;
    c.type = AcisCurveType::Straight;
    AcisEdge e;
    e.has_start = e.has_end = true;
    e.start = vec(-h, -h, -h);
    e.end = vec(h, h, h);
    e.curve = add_curve(b, c);
    add_edge(b, e);
}

// The face of the sphere bounded by the one edge, run with or against its direction
void add_sphere_face(AcisBody& b, int edge, bool coedge_reversed) {
    b.surfaces.push_back(unit_sphere());
    AcisFace f;
    f.surface = static_cast<int>(b.surfaces.size()) - 1;
    AcisLoop loop;
    loop.coedges.push_back({edge, coedge_reversed});
    f.loops.push_back(loop);
    b.faces.push_back(f);
}

// A closed edge on an ellipse: center, unit axis, major (its length the radius)
int add_circle_edge(AcisBody& b, const Point3& center, const Point3& axis, const Point3& major) {
    AcisCurve c;
    c.type = AcisCurveType::Ellipse;
    c.origin = center;
    c.axis = axis;
    c.major = major;
    c.ratio = 1.0;
    AcisEdge e;
    e.has_start = e.has_end = e.closed = true;
    e.start = e.end = plus(center, major);
    e.curve = add_curve(b, c);
    return add_edge(b, e);
}

// The point of the unit sphere the surface is parameterized from when the map is the identity:
// the longitude has no meaning there, so every sample within 1e-12 of it maps to (0, pi/2)
Point3 sphere_pole() {
    const double n = std::sqrt(14.0);
    return vec(1.0 / n, 2.0 / n, 3.0 / n);
}

// A closed edge at the sphere's pole, a polyline of degree 1 through the pole, the pole plus
// `excursion`, the pole plus a step of 1e-14, and the pole again, with the knots 0 0 .01 .02 1 1.
// The first of the 16 samples of its parameter range and the second are one point in 3D
// (the curve is back at the pole by the second knot), yet the curve between them reaches the
// excursion. A face of the sphere on it, with a wire that gives the body its size.
AcisBody pole_polyline_body(double excursion, Point3& far_point) {
    AcisBody b;
    add_wire(b, 0.5);
    const Point3 pole = sphere_pole();
    const Point3 along = times(vec(2.0, -1.0, 0.0), 1.0 / std::sqrt(5.0));      // perpendicular to the pole
    const Point3 sideways = cross_of(pole, along);
    far_point = plus(pole, times(sideways, excursion));
    AcisCurve c;
    c.type = AcisCurveType::Spline;
    c.spline.degree = 1;
    c.spline.knots = {0.0, 0.0, 0.01, 0.02, 1.0, 1.0};
    c.spline.points = {pole, far_point, plus(pole, times(along, 1e-14)), pole};
    AcisEdge e;
    e.has_start = e.has_end = e.closed = true;
    e.start = e.end = pole;
    e.curve = add_curve(b, c);
    add_sphere_face(b, add_edge(b, e), false);
    return b;
}

// The sphere minus a circular hole of the given radius around a point that is no pole and
// no extreme of the sphere's axes: the face is the rest, so its box is the sphere's
AcisBody sphere_with_hole(double radius) {
    AcisBody b;
    add_wire(b, 0.5);
    const Point3 h = vec(0.6, -0.8, 0.0);
    const Point3 m = vec(0.8, 0.6, 0.0);
    add_sphere_face(b, add_circle_edge(b, h, h, times(m, radius)), true);
    return b;
}

// The sphere minus a circle of angle eps around the pole of a map that sends all three axes
// to the pole's direction, or to three directions of which the first is the pole's
AcisBody sphere_with_cap_hole(double eps) {
    AcisBody b;
    add_wire(b, 0.25);
    add_sphere_face(b, add_circle_edge(b, vec(0.0, 0.0, std::cos(eps)), vec(0.0, 0.0, 1.0), vec(std::sin(eps), 0.0, 0.0)), true);
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
// The 3D stop
// ---------------------------------------------------------------------------

// An edge collapsed to a point at the sphere's pole (a polyline within 1e-14 of it) is not
// refined: all 16 of its pieces are points in 3D. Their (u, v) are no use there (the pole has
// no longitude), and every one of them left for refinement costs a deviation.
TEST(collapsed_spline_edge_stops_in_3d) {
    Point3 far_point;
    std::vector<AcisBody> bodies = {pole_polyline_body(1e-14, far_point)};
    Outcome o = run(bodies, AcisMap{}, AcisExtentsPass::All);
    hdxf_test::assert_eq(count_of(o.counters.faces_measured), 1, "the sphere's face is measured");
    hdxf_test::assert_eq(count_of(o.counters.deviations), 0, "a piece that is one point in 3D costs no deviation");
    // The edges are in the box whatever the face does
    hdxf_test::assert_true(o.box.min.x <= -0.5 + 1e-9, "the wire's min.x is in the box");
    hdxf_test::assert_true(o.box.max.x >= 0.5 - 1e-9, "the wire's max.x is in the box");
    hdxf_test::assert_true(o.box.max.z >= sphere_pole().z - 1e-9, "the pole is in the box");
}

// Equal ends are not enough: the same polyline with an excursion of 0.3 between its first
// two samples has equal ends there (1e-14 apart) but its hull is 0.3 wide, so that piece is
// refined as it always was (it costs a deviation: both its samples are at the pole, where the
// piece cannot be split). The rest of the curve is still within 1e-14 of the pole.
TEST(closed_edge_is_not_a_point) {
    Point3 far_point;
    std::vector<AcisBody> bodies = {pole_polyline_body(0.3, far_point)};
    Outcome o = run(bodies, AcisMap{}, AcisExtentsPass::All);
    hdxf_test::assert_eq(count_of(o.counters.faces_measured), 1, "the sphere's face is measured");
    hdxf_test::assert_true(o.counters.deviations >= 1, "the piece with the excursion is not taken for a point");
    // The edge's own box reaches the excursion
    hdxf_test::assert_true(o.box.min.x <= far_point.x + 1e-9 && o.box.max.x >= far_point.x - 1e-9, "excursion x");
    hdxf_test::assert_true(o.box.min.y <= far_point.y + 1e-9 && o.box.max.y >= far_point.y - 1e-9, "excursion y");
    hdxf_test::assert_true(o.box.min.z <= far_point.z + 1e-9 && o.box.max.z >= far_point.z - 1e-9, "excursion z");
}

// A hole of 1e-12 in the sphere is a tiny closed loop in (u, v): its pieces are points in 3D
// and are kept as they are. A query next to them, within the slack of a piece, must not take
// that for a point on the loop (a piece that is a point proves nothing about the edge): the
// reference point inside the face is found, and the six extremes of the sphere, all in the
// face, are all in the box.
TEST(pole_piece_does_not_claim_on_loop) {
    std::vector<AcisBody> bodies = {sphere_with_hole(1e-12)};
    Outcome o = run(bodies, AcisMap{}, AcisExtentsPass::All);
    hdxf_test::assert_eq(count_of(o.counters.faces_measured), 1, "the sphere's face is measured");
    same_box(o.box, box(-1, -1, -1, 1, 1, 1), 1e-9, "sphere minus a hole");
    hdxf_test::assert_eq(count_of(o.counters.deviations), 0, "the pieces of the hole are points in 3D");
}

// ---------------------------------------------------------------------------
// The refinement tree
// ---------------------------------------------------------------------------

// A hole of angle 1e-6 around the sphere's pole: the pole itself is 1e-6 from the loop in
// (u, v), near enough that the loop is followed on its edge, but off it. With a map that sends
// all three axes to the pole the pole is asked about three times (once for each axis); with
// a map of which only the first axis is the pole's direction, once. What the first question
// found of the edge is kept: the other two cost nothing more.
TEST(refine_tree_reuses_deviation) {
    std::vector<AcisBody> bodies = {sphere_with_cap_hole(1e-6)};
    AcisMap triple;
    for (int i = 0; i < 3; ++i) {
        triple.m[i][0] = 0.0;
        triple.m[i][1] = 0.0;
        triple.m[i][2] = 1.0;
    }
    // Rows a = (0, 0, 1), s = (1, 0, 0) and (a·sqrt(5) - 2 s) / 3, for which the pole of the
    // sphere's parameters (the sum of the unit rows weighted 1, 2, 3) is a again
    AcisMap single;
    single.m[0][2] = 1.0;
    single.m[1][0] = 1.0;
    single.m[2][0] = -2.0 / 3.0;
    single.m[2][2] = std::sqrt(5.0) / 3.0;
    Outcome one = run(bodies, single, AcisExtentsPass::All);
    Outcome three = run(bodies, triple, AcisExtentsPass::All);
    hdxf_test::assert_eq(count_of(one.counters.faces_measured), 1, "single: measured");
    hdxf_test::assert_eq(count_of(three.counters.faces_measured), 1, "triple: measured");
    hdxf_test::assert_true(one.counters.deviations > 0, "the loop is followed for the question at the pole");
    hdxf_test::assert_true(three.counters.deviations <= one.counters.deviations + 1,
                           "three questions at the pole cost " + std::to_string(three.counters.deviations) +
                               " deviations, one costs " + std::to_string(one.counters.deviations));
}

// ---------------------------------------------------------------------------
// The surface's hull in the box
// ---------------------------------------------------------------------------

// A face is skipped when its whole surface lies in the box known, and only then. A sphere is
// in the cube of its radius and a torus in the cube of its two radii, taken through the map; a
// spline surface is in the hull of its control points when all its weights are positive.
// Whatever is skipped, the box is the known box united with what the faces give alone.
TEST(surface_hulls_skip_only_contained_faces) {
    // Sphere: radius 1
    std::vector<AcisBody> cap = bodies_of(sphere_cap());
    Outcome held = run_faces_checked(cap, AcisMap{}, box(-2, -2, -2, 2, 2, 2), "sphere in a larger cube");
    hdxf_test::assert_eq(count_of(held.counters.faces_in_box), 1, "sphere held: skipped");
    hdxf_test::assert_eq(count_of(held.counters.faces_measured), 0, "sphere held: not measured");
    same_box(held.box, box(-2, -2, -2, 2, 2, 2), 1e-12, "sphere held: unchanged");

    // A cube of radius 1 is not in a box of 0.99; the cap's top then adds z = 1
    Outcome tight = run_faces_checked(cap, AcisMap{}, box(-0.99, -0.99, -0.99, 0.99, 0.99, 0.99), "sphere past the cube");
    hdxf_test::assert_eq(count_of(tight.counters.faces_in_box), 0, "sphere not held: not skipped");
    hdxf_test::assert_eq(count_of(tight.counters.faces_measured), 1, "sphere not held: measured");
    near(tight.box.max.z, 1.0, 1e-12, "the cap's top");

    // No box known: nothing to compare with
    Outcome none = run(cap, AcisMap{}, AcisExtentsPass::Faces);
    hdxf_test::assert_eq(count_of(none.counters.faces_in_box), 0, "empty box: not skipped");
    hdxf_test::assert_eq(count_of(none.counters.faces_measured), 1, "empty box: measured");

    // The map is part of the test: scaled by 2 the cube is 2, and the box is in the map's coordinates
    AcisMap twice;
    twice.m[0][0] = twice.m[1][1] = twice.m[2][2] = 2.0;
    Outcome inside_cube = run_faces_checked(cap, twice, box(-1.5, -1.5, -1.5, 1.5, 1.5, 1.5), "scaled sphere past the cube");
    hdxf_test::assert_eq(count_of(inside_cube.counters.faces_in_box), 0, "scaled sphere: 1.5 does not hold the cube of 2");
    hdxf_test::assert_eq(count_of(inside_cube.counters.faces_measured), 1, "scaled sphere: measured");
    near(inside_cube.box.max.z, 2.0, 1e-12, "the scaled cap's top");
    Outcome scaled_held = run_faces_checked(cap, twice, box(-2.5, -2.5, -2.5, 2.5, 2.5, 2.5), "scaled sphere in 2.5");
    hdxf_test::assert_eq(count_of(scaled_held.counters.faces_in_box), 1, "scaled sphere: 2.5 holds the cube of 2");

    // Torus: R 3, r 1, the cube of 4
    std::vector<AcisBody> torus = bodies_of(torus_half());
    Outcome torus_held = run_faces_checked(torus, AcisMap{}, box(-5, -5, -5, 5, 5, 5), "torus in a larger cube");
    hdxf_test::assert_eq(count_of(torus_held.counters.faces_in_box), 1, "torus held: skipped");
    Outcome torus_tight = run_faces_checked(torus, AcisMap{}, box(-3.9, -3.9, -3.9, 3.9, 3.9, 3.9), "torus past the cube");
    hdxf_test::assert_eq(count_of(torus_tight.counters.faces_in_box), 0, "torus not held: not skipped");
    hdxf_test::assert_eq(count_of(torus_tight.counters.faces_measured), 1, "torus not held: measured");

    // Spline surface: control points x, y in [-1, 1], z in [-1, 3]
    std::vector<AcisBody> dome = bodies_of(spline_dome());
    Outcome dome_held = run_faces_checked(dome, AcisMap{}, box(-2, -2, -2, 2, 2, 4), "dome net in the box");
    hdxf_test::assert_eq(count_of(dome_held.counters.faces_in_box), 1, "dome net held: skipped");
    hdxf_test::assert_eq(count_of(dome_held.counters.faces_measured), 0, "dome net held: not measured");
    Outcome dome_short = run_faces_checked(dome, AcisMap{}, box(-2, -2, -2, 2, 2, 2.9), "dome net past the box");
    hdxf_test::assert_eq(count_of(dome_short.counters.faces_in_box), 0, "dome net not held: not skipped");
    hdxf_test::assert_eq(count_of(dome_short.counters.faces_measured), 1, "dome net not held: measured");

    // The hull needs positive weights: with them all 1 the net still holds, with a zero or a
    // negative weight no shortcut is taken although every point is in the box
    size_t spline_at = dome[0].surfaces.size();
    for (size_t i = 0; i < dome[0].surfaces.size(); ++i) {
        if (dome[0].surfaces[i].type == AcisSurfaceType::Spline) spline_at = i;
    }
    hdxf_test::assert_true(spline_at < dome[0].surfaces.size(), "the dome has a spline surface");
    {
        std::vector<AcisBody> weighted = dome;
        weighted[0].surfaces[spline_at].spline.weights.assign(9, 1.0);
        Outcome o = run(weighted, AcisMap{}, AcisExtentsPass::Faces, box(-2, -2, -2, 2, 2, 4));
        hdxf_test::assert_eq(count_of(o.counters.faces_in_box), 1, "weights 1: skipped");
    }
    for (double weight : {0.0, -0.5}) {
        std::vector<AcisBody> weighted = dome;
        weighted[0].surfaces[spline_at].spline.weights.assign(9, 1.0);
        weighted[0].surfaces[spline_at].spline.weights[4] = weight;
        Outcome o = run(weighted, AcisMap{}, AcisExtentsPass::Faces, box(-2, -2, -2, 2, 2, 4));
        hdxf_test::assert_eq(count_of(o.counters.faces_in_box), 0, "weight " + std::to_string(weight) + ": not skipped");
        hdxf_test::assert_eq(count_of(o.counters.faces_measured), 1, "weight " + std::to_string(weight) + ": measured");
    }
}

// ---------------------------------------------------------------------------
// The best value of a spline face starts from the box known
// ---------------------------------------------------------------------------

// The dome's top (z = 1, inside its cap) is the only thing its face adds to a box. Known
// boxes that lie just below it in a coordinate of the map, in each sense, with a translation
// and a flip: the top is added exactly when it lies beyond the box. A box known beyond it
// leaves the box as it is.
TEST(spline_best_uses_known_axis_bound) {
    std::vector<AcisBody> dome = bodies_of(spline_dome());

    // World z = z: the box reaches 0.9 of 1
    Outcome plain = run_faces_checked(dome, AcisMap{}, box(-0.5, -0.5, -5, 0.5, 0.5, 0.9), "plain");
    hdxf_test::assert_eq(count_of(plain.counters.faces_measured), 1, "plain: measured");
    near(plain.box.max.z, 1.0, 1e-12, "plain: the top is added");

    // World z = z + 10: the top is at 11, the box reaches 10.9. Without the map's translation
    // the box would be taken as 10.9 of the body's own z, which no point of the face reaches
    AcisMap moved;
    moved.m[2][3] = 10.0;
    Outcome shifted = run_faces_checked(dome, moved, box(-0.5, -0.5, 5, 0.5, 0.5, 10.9), "translated");
    hdxf_test::assert_eq(count_of(shifted.counters.faces_measured), 1, "translated: measured");
    near(shifted.box.max.z, 11.0, 1e-12, "translated: the top is added");

    // World z = -z: the top is at -1, the box reaches -0.9 downwards, the other sense
    AcisMap flipped;
    flipped.m[2][2] = -1.0;
    Outcome turned = run_faces_checked(dome, flipped, box(-0.5, -0.5, -0.9, 0.5, 0.5, 5), "flipped");
    hdxf_test::assert_eq(count_of(turned.counters.faces_measured), 1, "flipped: measured");
    near(turned.box.min.z, -1.0, 1e-12, "flipped: the top is added");

    // World z = 3 - z: the top is at 2, the box reaches 2.1 downwards
    AcisMap both;
    both.m[2][2] = -1.0;
    both.m[2][3] = 3.0;
    Outcome flipped_moved = run_faces_checked(dome, both, box(-0.5, -0.5, 2.1, 0.5, 0.5, 9), "flipped and translated");
    hdxf_test::assert_eq(count_of(flipped_moved.counters.faces_measured), 1, "flipped and translated: measured");
    near(flipped_moved.box.min.z, 2.0, 1e-12, "flipped and translated: the top is added");

    // A box known beyond the top in the sense asked leaves the box as it is
    Outcome beyond = run_faces_checked(dome, AcisMap{}, box(-0.5, -0.5, -5, 0.5, 0.5, 5), "beyond");
    hdxf_test::assert_eq(count_of(beyond.counters.faces_measured), 1, "beyond: measured");
    same_box(beyond.box, box(-0.5, -0.5, -5, 0.5, 0.5, 5), 1e-12, "beyond: unchanged");
}

// ---------------------------------------------------------------------------
// Knot candidates in the box
// ---------------------------------------------------------------------------

// The half of the tent with y <= 0, bounded by straight edges on the surface, one of its
// corners the tent's top (0, 0, 2) where the knot lines cross. The top is a candidate of the
// knot lines. When the box known holds it the candidate is not asked about (asking costs the
// loops being followed, a deviation each); when it does not, it is asked about, and is in the
// face on its loop.
TEST(knot_candidate_in_box_skips_inside) {
    std::vector<AcisBody> half = bodies_of(polygon_face(tent_surface(),
        {{-1, -1, 0}, {1, -1, 0}, {1, 0, 0}, {0, 0, 2}, {-1, 0, 0}}));
    // Neither box holds the net (it reaches y = 1); the second holds the top, the first not
    Outcome below = run(half, AcisMap{}, AcisExtentsPass::Faces, box(-1, -1, 0, 1, 0, 1.5));
    Outcome holds = run(half, AcisMap{}, AcisExtentsPass::Faces, box(-1, -1, 0, 1, 0, 2.5));
    hdxf_test::assert_eq(count_of(below.counters.faces_measured), 1, "below: measured");
    hdxf_test::assert_eq(count_of(holds.counters.faces_measured), 1, "holds: measured");
    hdxf_test::assert_eq(count_of(holds.counters.faces_in_box), 0, "holds: the net is not in the box");
    hdxf_test::assert_true(holds.counters.deviations < below.counters.deviations,
                           "a candidate in the box is not asked about (" + std::to_string(holds.counters.deviations) +
                               " deviations against " + std::to_string(below.counters.deviations) + ")");
    same_box(holds.box, box(-1, -1, 0, 1, 0, 2.5), 1e-12, "holds: unchanged");
    hdxf_test::assert_true(below.box.max.z >= 1.5 && below.box.max.z <= 2.0 + 1e-9, "below: at most the top is added");
}

// ---------------------------------------------------------------------------
// AutoCAD's files: the extents computed are the extents AutoCAD saved
// ---------------------------------------------------------------------------

// A block with a sphere, a torus, a loft to a point and the same loft sliced, inserted at 0,
// 30 and 90 degrees: the box of the drawing is the $EXTMIN/$EXTMAX AutoCAD saved (its
// values after ZOOM E carry noise up to about 4e-7)
TEST(acis_hulls_facit_extents_equal_saved_header) {
    std::filesystem::path directory = hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit";
    int checked = 0;
    std::string failures;
    // R12 has no bodies: AutoCAD writes them there as lines and meshes, and the header it saved is still the bodies'
    for (const char* version : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (const char* suffix : {"", "_bin"}) {
            std::string name = std::string("acad_") + version + "_dxf_acis_hulls" + suffix + ".dxf";
            std::filesystem::path path = directory / hdxf::path_from_utf8(name);
            std::error_code ec;
            if (!std::filesystem::exists(path, ec)) continue;
            hdxf::Drawing dwg;
            if (!dwg.load(path)) {
                failures += name + ": cannot be read: " + dwg.last_error() + "\n";
                continue;
            }
            const hdxf::HeaderVar* lo = dwg.find_header("$EXTMIN");
            const hdxf::HeaderVar* hi = dwg.find_header("$EXTMAX");
            if (!lo || !hi || lo->values.size() < 3 || hi->values.size() < 3) {
                failures += name + ": no $EXTMIN/$EXTMAX\n";
                continue;
            }
            Extents model, paper;
            std::vector<std::string> warnings;
            hdxf::internal::drawing_extents(dwg, {}, {}, model, paper, warnings);
            const double saved[6] = {std::stod(lo->values[0].second), std::stod(lo->values[1].second),
                                     std::stod(lo->values[2].second), std::stod(hi->values[0].second),
                                     std::stod(hi->values[1].second), std::stod(hi->values[2].second)};
            const double got[6] = {model.min.x, model.min.y, model.min.z, model.max.x, model.max.y, model.max.z};
            const char* axes[6] = {"min.x", "min.y", "min.z", "max.x", "max.y", "max.z"};
            ++checked;
            for (int i = 0; i < 6; ++i) {
                if (!(std::fabs(got[i] - saved[i]) <= 1e-5)) {
                    char buf[240];
                    std::snprintf(buf, sizeof buf, "%s %s: %.9g, AutoCAD saved %.9g\n", name.c_str(), axes[i], got[i], saved[i]);
                    failures += buf;
                }
            }
        }
    }
    if (checked == 0) hdxf_test::skip("doc/facit has no acad_*_dxf_acis_hulls files");
    hdxf_test::assert_true(failures.empty(), failures);
}
