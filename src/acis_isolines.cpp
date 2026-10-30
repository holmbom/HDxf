#include "acis_isolines.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <cstdio>
#include <cstdlib>

// The isolines of a surface body as AutoCAD draws them in its proxy graphics (acis_isolines.hpp). The geometry is
// the body's own (src/acis.cpp: the faces' surfaces and the edges' curves, exactly); the topology — which face,
// loop, coedge and edge comes first — is read from the SAT records here.

namespace hdxf {
namespace internal {

namespace {

const double kPi = 3.14159265358979323846;
const double kTwoPi = 2.0 * kPi;

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

Point3 add(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point3 mul(const Point3& a, double k) { return {a.x * k, a.y * k, a.z * k}; }
double dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double norm(const Point3& a) { return std::sqrt(dot(a, a)); }
Point3 unit(const Point3& a) {
    double n = norm(a);
    return n > 0.0 ? mul(a, 1.0 / n) : a;
}
Point3 mid(const Point3& a, const Point3& b) { return mul(add(a, b), 0.5); }

// The AutoCAD arbitrary axis algorithm: the OCS x axis of a normal
Point3 arbitrary_x(const Point3& n) {
    Point3 z = unit(n);
    if (std::fabs(z.x) < 1.0 / 64.0 && std::fabs(z.y) < 1.0 / 64.0) return unit(cross({0.0, 1.0, 0.0}, z));
    return unit(cross({0.0, 0.0, 1.0}, z));
}

// ---------------------------------------------------------------------------
// The SAT records: pointers and numbers in the order of a record's data
// ---------------------------------------------------------------------------

std::vector<std::string> identifier_parts(const std::string& type) {
    std::vector<std::string> parts;
    size_t a = 0;
    while (true) {
        size_t b = type.find('-', a);
        parts.push_back(type.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return parts;
}

std::string base_class(const AcisRecord& r) { return identifier_parts(r.type).back(); }

// The index of the first value after the ENTITY data (as src/acis.cpp: the attribute pointer, from 7.0 the
// entity id and, for topology and geometry, the pattern pointer)
size_t data_start(const AcisFile& file, const AcisRecord& r) {
    size_t i = 1;
    if (file.version >= 700) {
        ++i;
        static const char* const patterned[] = {"body", "lump", "shell", "subshell", "face", "loop", "coedge",
                                                "edge", "vertex", "wire", "point", "curve", "surface", "pcurve"};
        std::string base = base_class(r);
        for (const char* p : patterned) {
            if (base == p) {
                ++i;
                break;
            }
        }
    }
    return i;
}

class Record {
public:
    Record(const AcisFile& file, size_t index) : r_(file.records[index]), i_(data_start(file, r_)) {}
    // The next pointer (-1 for none)
    int pointer() {
        while (i_ < r_.tokens.size()) {
            const AcisToken& t = r_.tokens[i_++];
            if (t.type == AcisTokenType::Pointer) return static_cast<int>(t.number);
        }
        return -1;
    }
    bool number(double& v) {
        while (i_ < r_.tokens.size()) {
            const AcisToken& t = r_.tokens[i_];
            if (t.type == AcisTokenType::Number) {
                v = t.number;
                ++i_;
                return true;
            }
            if (t.type == AcisTokenType::Pointer) return false;
            ++i_;
        }
        return false;
    }
    // The next sense: reversed (a word, or true)
    bool reversed() {
        while (i_ < r_.tokens.size()) {
            const AcisToken& t = r_.tokens[i_++];
            if (t.type == AcisTokenType::Word) return t.text == "reversed";
            if (t.type == AcisTokenType::Logical) return t.number != 0.0;
        }
        return false;
    }

private:
    const AcisRecord& r_;
    size_t i_;
};

// ---------------------------------------------------------------------------
// Topology
// ---------------------------------------------------------------------------

struct CoedgeInfo {
    int record = -1;
    int edge = -1;                  // index into Topology::edges
    bool reversed = false;
};

struct LoopInfo {
    std::vector<CoedgeInfo> coedges;
};

struct FaceInfo {
    int record = -1;
    int surface_record = -1;
    bool reversed = false;
    std::vector<LoopInfo> loops;
    AcisSurface surface;
    bool surface_ok = false;
};

struct EdgeInfo {
    int record = -1;
    int start_vertex = -1;
    int end_vertex = -1;
    double t0 = 0.0, t1 = 0.0;
    int curve_record = -1;
    bool reversed = false;
    int coedge = -1;                // the edge's own coedge: the edge is met (and numbered) at that one
    int index = 0;                  // 1 …: the order the faces' loops meet the edges' own coedges in
    AcisCurve curve;
    bool curve_ok = false;
    Point3 start, end;
};

struct Topology {
    std::vector<FaceInfo> faces;
    std::vector<EdgeInfo> edges;
    std::vector<int> vertices;      // the vertex records, in the order the loops meet them
    std::map<int, Point3> vertex_point;
};

bool is_class(const AcisFile& file, int record, const char* cls) {
    if (record < 0 || static_cast<size_t>(record) >= file.records.size()) return false;
    return base_class(file.records[static_cast<size_t>(record)]) == cls;
}

bool build_topology(const AcisFile& file, Topology& t, std::string& why) {
    int body = -1;
    for (size_t i = 0; i < file.records.size(); ++i) {
        if (is_class(file, static_cast<int>(i), "body")) {
            body = static_cast<int>(i);
            break;
        }
    }
    if (body < 0) {
        why = "the SAT has no body";
        return false;
    }
    std::map<int, int> edge_of;                 // edge record → index
    auto edge_index = [&](int rec) -> int {
        auto it = edge_of.find(rec);
        if (it != edge_of.end()) return it->second;
        if (!is_class(file, rec, "edge")) return -1;
        Record r(file, static_cast<size_t>(rec));
        EdgeInfo e;
        e.record = rec;
        e.start_vertex = r.pointer();
        r.number(e.t0);
        e.end_vertex = r.pointer();
        r.number(e.t1);
        e.coedge = r.pointer();
        e.curve_record = r.pointer();
        e.reversed = r.reversed();
        int index = static_cast<int>(t.edges.size());
        t.edges.push_back(e);
        edge_of[rec] = index;
        return index;
    };
    int numbered = 0;
    std::set<int> vertex_seen;
    auto note_vertex = [&](int rec) {
        if (rec < 0 || !vertex_seen.insert(rec).second) return;
        t.vertices.push_back(rec);
    };

    Record body_r(file, static_cast<size_t>(body));
    int lump = body_r.pointer();
    std::set<int> lumps;
    while (lump >= 0 && is_class(file, lump, "lump") && lumps.insert(lump).second) {
        Record lump_r(file, static_cast<size_t>(lump));
        int next_lump = lump_r.pointer();
        int shell = lump_r.pointer();
        std::set<int> shells;
        while (shell >= 0 && is_class(file, shell, "shell") && shells.insert(shell).second) {
            Record shell_r(file, static_cast<size_t>(shell));
            int next_shell = shell_r.pointer();
            shell_r.pointer();                  // the subshell
            int face = shell_r.pointer();
            std::set<int> faces;
            while (face >= 0 && is_class(file, face, "face") && faces.insert(face).second) {
                Record face_r(file, static_cast<size_t>(face));
                int next_face = face_r.pointer();
                int loop = face_r.pointer();
                face_r.pointer();               // the shell
                face_r.pointer();               // the subshell
                FaceInfo f;
                f.record = face;
                f.surface_record = face_r.pointer();
                f.reversed = face_r.reversed();
                std::set<int> loops;
                while (loop >= 0 && is_class(file, loop, "loop") && loops.insert(loop).second) {
                    Record loop_r(file, static_cast<size_t>(loop));
                    int next_loop = loop_r.pointer();
                    int first = loop_r.pointer();
                    LoopInfo l;
                    std::set<int> coedges;
                    int c = first;
                    while (c >= 0 && is_class(file, c, "coedge") && coedges.insert(c).second) {
                        Record c_r(file, static_cast<size_t>(c));
                        int next = c_r.pointer();
                        c_r.pointer();          // the previous coedge
                        c_r.pointer();          // the partner
                        int edge = c_r.pointer();
                        CoedgeInfo ci;
                        ci.record = c;
                        ci.reversed = c_r.reversed();
                        ci.edge = edge_index(edge);
                        if (ci.edge >= 0) {
                            EdgeInfo& e = t.edges[static_cast<size_t>(ci.edge)];
                            if (e.coedge == c && e.index == 0) e.index = ++numbered;
                            note_vertex(ci.reversed ? e.end_vertex : e.start_vertex);
                            note_vertex(ci.reversed ? e.start_vertex : e.end_vertex);
                            l.coedges.push_back(ci);
                        }
                        c = next;
                    }
                    f.loops.push_back(l);
                    loop = next_loop;
                }
                t.faces.push_back(f);
                face = next_face;
            }
            shell = next_shell;
        }
        lump = next_lump;
    }
    if (t.faces.empty()) {
        why = "the body has no face";
        return false;
    }
    for (EdgeInfo& e : t.edges) {
        if (e.index == 0) e.index = ++numbered;
    }
    for (FaceInfo& f : t.faces) {
        f.surface_ok = acis_record_surface(file, static_cast<size_t>(f.surface_record), f.surface);
    }
    for (EdgeInfo& e : t.edges) {
        if (e.curve_record >= 0) e.curve_ok = acis_record_curve(file, static_cast<size_t>(e.curve_record), e.curve);
        Point3 p;
        if (acis_vertex_point(file, static_cast<size_t>(e.start_vertex), p)) e.start = p;
        if (acis_vertex_point(file, static_cast<size_t>(e.end_vertex), p)) e.end = p;
        // Before ACIS 5 (AutoCAD 2000's SAT 400) an edge has no parameters: they are those of its vertices on its curve; a
        // circle that starts and ends at one vertex goes all the way round, starting at -pi where the vertex is on the seam
        if (file.version < 500 && e.curve_ok && e.t0 == 0.0 && e.t1 == 0.0) {
            const AcisCurve& c = e.curve;
            double a = acis_curve_parameter(c, e.start), b = acis_curve_parameter(c, e.end);
            if (c.type == AcisCurveType::Ellipse) {
                if (std::fabs(a - kPi) < 1e-9) a = -kPi;
                if (e.start_vertex == e.end_vertex || std::fabs(b - a) < 1e-9) {
                    b = a + kTwoPi;
                } else {
                    while (b <= a) b += kTwoPi;
                }
            } else if (c.type == AcisCurveType::Spline && e.start_vertex == e.end_vertex && c.spline.knots.size() > 2 * static_cast<size_t>(c.spline.degree)) {
                // a closed spline from one vertex round to it: all of it (a circle of a cone converted to NURBS,
                // acad_2000_dxf_surfaces_nurbs_converted 177F)
                double lo = c.spline.knots[static_cast<size_t>(c.spline.degree)];
                double hi = c.spline.knots[c.spline.knots.size() - 1 - static_cast<size_t>(c.spline.degree)];
                a = c.reversed ? -hi : lo;
                b = c.reversed ? -lo : hi;
            }
            e.t0 = a;
            e.t1 = b;
        }
    }
    for (int v : t.vertices) {
        Point3 p;
        if (acis_vertex_point(file, static_cast<size_t>(v), p)) t.vertex_point[v] = p;
    }
    return true;
}

// ---------------------------------------------------------------------------
// What is drawn: curves in the body's own coordinates, mapped when they are written
// ---------------------------------------------------------------------------

struct Mapper {
    double m[3][4];
    Point3 point(const Point3& p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
    }
    Point3 vector(const Point3& p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z, m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z,
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z};
    }
};

class Drawer {
public:
    Drawer(ProxyGraphics& g, const Mapper& map, int32_t color) : g_(g), map_(map), color_(color) {}

    void begin(int marker) {
        g_.marker(marker);
        g_.true_color(color_);
    }
    void line(const Point3& a, const Point3& b) { g_.polyline({map_.point(a), map_.point(b)}); }
    void polyline(const std::vector<Point3>& points) {
        std::vector<Point3> mapped;
        mapped.reserve(points.size());
        for (const Point3& p : points) mapped.push_back(map_.point(p));
        g_.polyline(mapped);
    }
    // A whole circle: three points of it, between fill 2 and fill 1
    void circle(const Point3& a, const Point3& b, const Point3& c) {
        g_.fill(false);
        g_.circle3(map_.point(a), map_.point(b), map_.point(c));
        g_.fill(true);
    }
    // A part of a circle: its start, a point between and its end
    void arc(const Point3& a, const Point3& b, const Point3& c) {
        g_.arc3(map_.point(a), map_.point(b), map_.point(c));
    }
    // An ellipse c + major cos t + minor sin t from t0 to t1
    void ellipse(const Point3& c, const Point3& major, const Point3& minor, double t0, double t1) {
        Point3 center = map_.point(c);
        Point3 a = map_.vector(major);
        Point3 b = map_.vector(minor);
        Point3 normal = unit(cross(a, b));
        Point3 x = arbitrary_x(normal);
        Point3 y = cross(unit(normal), x);
        double angle = std::atan2(dot(a, y), dot(a, x));
        if (angle < 0.0) angle += kTwoPi;
        g_.fill(false);
        g_.ellipse_arc(center, normal, norm(a), norm(b), t0, t1, angle, 0);
        g_.fill(true);
    }
    void vertex(const Point3& p) {
        Point3 q = map_.point(p);
        g_.polyline({q, q});
    }
    void color_only() { g_.true_color(color_); }

private:
    ProxyGraphics& g_;
    Mapper map_;
    int32_t color_;
};

// ---------------------------------------------------------------------------
// Spans halved until the curve's middle is within the tolerance of the chord
// ---------------------------------------------------------------------------

// Only a stop: AutoCAD's files never reach it (at VIEWRES 10000 AutoCAD cuts a span in more than 64, F9.18). A curve that
// does not come within the tolerance in 4096 pieces of a span is not drawn: the stream would not be AutoCAD's
const int kMaxDepth = 12;
const char* const kToleranceNotReached = "a curve of the body does not come within the tolerance in 4096 pieces of a span";

// T of an isoline over the view's height at VIEWRES 1000 (an edge's is two thirds of it). AutoCAD's nine view files
// (doc/facit/acad_2004_dxf_surfaces_view*.dxf, two extruded splines of 4 and 8 spans, F9.18 (g)) allow 1.39534e-4 …
// 1.40853e-4 for the isolines and 9.3650e-5 … 9.3741e-5 for the edges; two thirds of 1.4055e-4 (9.37e-5) is in both
const double kViewTolerance = 1.4055e-4;

// The parameters of a polyline for the curve f of the given degree, a span at a time (the knots between the first
// and the last). A span of degree 1 is straight: its ends. Any other span is a piece when its middle and its quarter
// points are each within the tolerance of the chord's point at the same fraction, else it is halved; each half is then
// halved while its middle is not: |f(m) - (f(a) + f(b)) / 2| > tolerance (F9.18 (g): the nine view files, a span
// whose middle is on its chord — an S between two inflections — cut in two; a span straight in a NURBS, a ruled loft
// of different splines whose short span between two knots stays whole, acad_*_surfaces_iso 26D)
// False when a span reached kMaxDepth without coming within the tolerance
bool tessellate(const std::function<Point3(double)>& f, const std::vector<double>& knots, int degree, double tolerance,
                std::vector<double>& params) {
    params.clear();
    if (knots.size() < 2) return true;
    bool reached = true;
    params.push_back(knots.front());
    for (size_t k = 0; k + 1 < knots.size(); ++k) {
        if (degree == 1) {
            params.push_back(knots[k + 1]);
            continue;
        }
        std::function<void(double, Point3, double, Point3, int)> halve = [&](double a, Point3 pa, double b, Point3 pb,
                                                                           int depth) {
            double m = 0.5 * (a + b);
            Point3 pm = f(m);
            bool piece = norm(sub(pm, mid(pa, pb))) <= tolerance;
            if (depth == 0) {
                for (double fraction : {0.25, 0.75}) {
                    Point3 on_chord = add(pa, mul(sub(pb, pa), fraction));
                    piece = piece && norm(sub(f(a + fraction * (b - a)), on_chord)) <= tolerance;
                }
            }
            if (piece || depth >= kMaxDepth) {
                if (!piece) reached = false;
                params.push_back(b);
                return;
            }
            halve(a, pa, m, pm, depth + 1);
            halve(m, pm, b, pb, depth + 1);
        };
        double a = knots[k], b = knots[k + 1];
        halve(a, f(a), b, f(b), 0);
    }
    return reached;
}

// The distinct knots of a spline's full knot vector within [lo, hi], with lo and hi themselves
std::vector<double> span_knots(const std::vector<double>& knots, double lo, double hi) {
    std::vector<double> out{lo};
    for (double k : knots) {
        if (k > lo + 1e-12 && k < hi - 1e-12 && k > out.back() + 1e-12) out.push_back(k);
    }
    out.push_back(hi);
    return out;
}

// ---------------------------------------------------------------------------
// The edges
// ---------------------------------------------------------------------------

bool circular(const AcisCurve& c) {
    double a = norm(c.major);
    return std::fabs(c.ratio - 1.0) < 1e-9 && a > 0.0;
}

Point3 curve_at(const AcisCurve& c, double t) {
    Point3 p;
    acis_curve_eval(c, t, p);
    return p;
}

bool draw_edge(Drawer& d, const EdgeInfo& e, double edge_tolerance, std::string& why) {
    if (!e.curve_ok) {
        why = "an edge's curve is of a kind that is not drawn";
        return false;
    }
    const AcisCurve& c = e.curve;
    double t0 = e.t0, t1 = e.t1;
    switch (c.type) {
        case AcisCurveType::Straight:
            d.line(e.start, e.end);
            return true;
        case AcisCurveType::Ellipse: {
            Point3 minor = mul(cross(c.axis, c.major), c.ratio);
            bool whole = std::fabs((t1 - t0) - kTwoPi) < 1e-9;
            if (!circular(c)) {
                d.ellipse(c.origin, c.major, minor, t0, t1);
            } else if (whole) {
                d.circle(curve_at(c, t0), curve_at(c, kTwoPi / 3.0), curve_at(c, -kTwoPi / 3.0));
            } else {
                d.arc(curve_at(c, t0), curve_at(c, 0.5 * (t0 + t1)), curve_at(c, t1));
            }
            return true;
        }
        case AcisCurveType::Spline: {
            auto at = [&](double t) { return curve_at(c, t); };
            std::vector<double> all = c.spline.knots;
            if (c.reversed) {
                for (double& k : all) k = -k;
                std::sort(all.begin(), all.end());
            }
            std::vector<double> knots = span_knots(all, std::min(t0, t1), std::max(t0, t1));
            std::vector<double> params;
            if (!tessellate(at, knots, c.spline.degree, edge_tolerance, params)) {
                why = kToleranceNotReached;
                return false;
            }
            std::vector<Point3> pts;
            for (double t : params) pts.push_back(at(t));
            d.polyline(pts);
            return true;
        }
        default:
            why = "an edge's curve is of a kind that is not drawn";
            return false;
    }
}

// ---------------------------------------------------------------------------
// A face's surface in its parameters
// ---------------------------------------------------------------------------

// Where the loops of a face go in the surface's parameters, and what the surface does there
// x taken into [0, period), a value within 1e-12 of the period kept at it (the end of a periodic range)
double periodic_offset(double x, double period) {
    double r = std::fmod(x, period);
    if (r < 0.0) r += period;
    if (period - r < 1e-12 * (1.0 + period) && x > 0.0) r = period;
    return r;
}

class FaceSurface {
public:
    explicit FaceSurface(const AcisSurface& s) : s_(s) {
        switch (s.type) {
            case AcisSurfaceType::Cone:
            case AcisSurfaceType::Sphere:
                period_v_ = kTwoPi;
                break;
            case AcisSurfaceType::Torus:
                period_u_ = period_v_ = kTwoPi;
                break;
            case AcisSurfaceType::Revolution:
                period_v_ = kTwoPi;
                break;
            case AcisSurfaceType::Spline: {
                const AcisSplineSurface& sp = s.spline;
                if (!sp.points.empty()) {
                    if (sp.periodic_u) period_u_ = knot_hi_u() - knot_lo_u();
                    if (sp.periodic_v) period_v_ = knot_hi_v() - knot_lo_v();
                }
                break;
            }
            default:
                break;
        }
    }

    const AcisSurface& surface() const { return s_; }
    double period_u() const { return period_u_; }
    double period_v() const { return period_v_; }
    double knot_lo_u() const { return s_.spline.knots_u[static_cast<size_t>(s_.spline.degree_u)]; }
    double knot_hi_u() const { return s_.spline.knots_u[static_cast<size_t>(s_.spline.count_u)]; }
    double knot_lo_v() const { return s_.spline.knots_v[static_cast<size_t>(s_.spline.degree_v)]; }
    double knot_hi_v() const { return s_.spline.knots_v[static_cast<size_t>(s_.spline.count_v)]; }

    // A cone's u as AutoCAD counts it: ACIS's own u (along = sign u scale, sign -1 for a reversed cone) times eps, the
    // sign of the line's cosine (a mirrored cylinder, reversed: AutoCAD's u is ACIS's, acad_*_surfaces_transform_mirrored)
    double cone_eps() const { return s_.cosine < 0.0 ? -1.0 : 1.0; }
    double cone_sign() const { return s_.reverse_u ? -1.0 : 1.0; }
    double cone_u(double u) const { return cone_eps() * u; }
    double cone_along(double u) const {
        double scale = s_.u_scale != 0.0 ? s_.u_scale : norm(s_.major);
        return cone_sign() * cone_u(u) * scale;
    }

    Point3 at(double u, double v) const {
        Point3 p;
        if (s_.type == AcisSurfaceType::Spline && !s_.spline.points.empty()) {
            // a periodic spline: its parameter taken into its own range (a face's turn may start elsewhere)
            if (period_u_ > 0.0) u = knot_lo_u() + periodic_offset(u - knot_lo_u(), period_u_);
            if (period_v_ > 0.0) v = knot_lo_v() + periodic_offset(v - knot_lo_v(), period_v_);
        }
        acis_surface_point(s_, s_.type == AcisSurfaceType::Cone ? cone_u(u) : u, v, p);
        return p;
    }

    // A spline's knots in one direction, repeated a period on each side where it is periodic
    std::vector<double> knots(bool along_u) const {
        std::vector<double> k = along_u ? s_.spline.knots_u : s_.spline.knots_v;
        double period = along_u ? period_u_ : period_v_;
        if (period > 0.0) {
            std::vector<double> more;
            for (double x : k) {
                more.push_back(x - period);
                more.push_back(x + period);
            }
            k.insert(k.end(), more.begin(), more.end());
            std::sort(k.begin(), k.end());
        }
        return k;
    }

    // The parameters of a point of the surface, near (hu, hv) where the surface has no closed form
    void inverse(const Point3& p, double hu, double hv, double& u, double& v) const {
        switch (s_.type) {
            case AcisSurfaceType::Plane: {
                double sign = s_.reverse_v ? -1.0 : 1.0;
                Point3 across = mul(cross(s_.axis, s_.u_direction), sign);
                Point3 q = sub(p, s_.origin);
                u = dot(q, s_.u_direction);
                v = dot(q, across);
                return;
            }
            case AcisSurfaceType::Cone: {
                double major = norm(s_.major);
                double scale = s_.u_scale != 0.0 ? s_.u_scale : major;
                Point3 q = sub(p, s_.origin);
                double h = dot(q, s_.axis);
                double along = std::fabs(s_.cosine) > 1e-12 ? h / s_.cosine : 0.0;
                u = cone_eps() * along / (cone_sign() * scale);
                Point3 radial = sub(q, mul(s_.axis, h));
                Point3 minor = mul(cross(s_.axis, s_.major), s_.ratio);
                double a = dot(radial, s_.major) / (major * major);
                double b = dot(radial, minor) / dot(minor, minor);
                if (std::fabs(a) < 1e-300 && std::fabs(b) < 1e-300) {
                    v = std::numeric_limits<double>::quiet_NaN();
                } else {
                    v = std::atan2(b, a);
                }
                return;
            }
            case AcisSurfaceType::Sphere: {
                double sign = s_.reverse_v ? -1.0 : 1.0;
                Point3 x = unit(s_.major);
                Point3 y = cross(s_.axis, x);
                Point3 q = mul(sub(p, s_.origin), 1.0 / s_.radius);
                double sinu = std::clamp(dot(q, s_.axis), -1.0, 1.0);
                u = std::asin(sinu);
                Point3 radial = sub(q, mul(s_.axis, dot(q, s_.axis)));
                if (norm(radial) < 1e-12) {
                    v = std::numeric_limits<double>::quiet_NaN();
                } else {
                    v = sign * std::atan2(dot(radial, y), dot(radial, x));
                }
                return;
            }
            case AcisSurfaceType::Torus: {
                double sign = s_.reverse_v ? -1.0 : 1.0;
                Point3 x = unit(s_.major);
                Point3 y = cross(s_.axis, x);
                Point3 q = sub(p, s_.origin);
                double h = dot(q, s_.axis);
                Point3 radial = sub(q, mul(s_.axis, h));
                double rho = norm(radial);
                v = rho < 1e-12 ? std::numeric_limits<double>::quiet_NaN() : sign * std::atan2(dot(radial, y), dot(radial, x));
                u = std::atan2(h / s_.minor, (rho - s_.radius) / s_.minor);
                return;
            }
            default:
                break;
        }
        newton(p, hu, hv, u, v);
    }

    // Nearest point of a spline, revolution or sum surface: Newton from the best of a grid (or from the hint)
    void newton(const Point3& p, double hu, double hv, double& u, double& v) const;

    // The parameter box of the surface itself (where it has one)
    bool box(double& u0, double& u1, double& v0, double& v1) const {
        if (s_.type == AcisSurfaceType::Spline && !s_.spline.points.empty()) {
            u0 = knot_lo_u();
            u1 = knot_hi_u();
            v0 = knot_lo_v();
            v1 = knot_hi_v();
            return true;
        }
        if ((s_.type == AcisSurfaceType::Revolution || s_.type == AcisSurfaceType::Sum) && s_.has_range) {
            u0 = s_.u0;
            u1 = s_.u1;
            v0 = s_.v0;
            v1 = s_.v1;
            return true;
        }
        return false;
    }

private:
    const AcisSurface& s_;
    double period_u_ = 0.0;
    double period_v_ = 0.0;
};

void FaceSurface::newton(const Point3& p, double hu, double hv, double& u, double& v) const {
    double u0 = 0.0, u1 = 1.0, v0 = 0.0, v1 = 1.0;
    bool ranged = box(u0, u1, v0, v1);
    double bu = hu, bv = hv;
    if (!std::isfinite(hu) || !std::isfinite(hv) || !ranged) {
        bu = u0;
        bv = v0;
    }
    double best = std::numeric_limits<double>::infinity();
    if (ranged) {
        for (int i = 0; i <= 24; ++i) {
            for (int j = 0; j <= 24; ++j) {
                double uu = u0 + (u1 - u0) * i / 24.0;
                double vv = v0 + (v1 - v0) * j / 24.0;
                Point3 q;
                if (!acis_surface_point(s_, uu, vv, q)) continue;
                double d = norm(sub(q, p));
                if (d < best) {
                    best = d;
                    bu = uu;
                    bv = vv;
                }
            }
        }
    }
    u = bu;
    v = bv;
    for (int it = 0; it < 60; ++it) {
        Point3 q, su, sv;
        if (!acis_surface_point(s_, u, v, q, &su, &sv)) break;
        Point3 r = sub(q, p);
        double a = dot(su, su), b = dot(su, sv), c = dot(sv, sv);
        double fu = dot(r, su), fv = dot(r, sv);
        double det = a * c - b * b;
        if (std::fabs(det) < 1e-300) break;
        double du = (c * fu - b * fv) / det;
        double dv = (a * fv - b * fu) / det;
        double nu = u - du, nv = v - dv;
        if (ranged) {
            nu = std::clamp(nu, u0, u1);
            nv = std::clamp(nv, v0, v1);
        }
        bool done = std::fabs(nu - u) < 1e-14 * (1.0 + std::fabs(u)) && std::fabs(nv - v) < 1e-14 * (1.0 + std::fabs(v));
        u = nu;
        v = nv;
        if (done) break;
    }
}

double wrap_near(double angle, double reference, double period) {
    if (period <= 0.0 || std::isnan(angle) || std::isnan(reference)) return angle;
    while (angle - reference > 0.5 * period) angle -= period;
    while (angle - reference < -0.5 * period) angle += period;
    return angle;
}

// ---------------------------------------------------------------------------
// A face: the loops in the parameters, the box they make, and what is inside
// ---------------------------------------------------------------------------

struct UV {
    double u = 0.0, v = 0.0;
};

// One coedge sampled along its edge's curve: the curve parameters and (u, v) there, continuous with the sample before
struct CoedgeSamples {
    const EdgeInfo* edge = nullptr;
    const CoedgeInfo* coedge = nullptr;
    std::vector<double> t;
    std::vector<UV> uv;
};

Point3 edge_point(const EdgeInfo& e, double t) {
    Point3 p;
    if (e.curve.type == AcisCurveType::Straight) {
        p = add(e.curve.origin, mul(e.curve.direction, t));
    } else {
        acis_curve_eval(e.curve, t, p);
    }
    return p;
}

// ---------------------------------------------------------------------------
// A loft straight from section to section
// ---------------------------------------------------------------------------

// A spline surface of one span in v whose every column of control points is evenly spaced on a line: straight in v.
// AutoCAD draws a loft of such a surface (simple surfaces: its own surface is ruled from section to section) along v
// as a polyline through its sections (doc/facit/acad_*_surfaces_spline.dxf: 3 sections 3 points, 4 sections 4;
// acad_*_surfaces_iso.dxf 25D, 26A: at the sections where they are, also unevenly spaced) — which the body does not
// give: surface_graphics.cpp names it
bool straight_in_v(const AcisSurface& s) {
    if (s.type != AcisSurfaceType::Spline) return false;
    const AcisSplineSurface& sp = s.spline;
    if (sp.points.empty() || sp.count_v < 2 || sp.periodic_v) return false;
    for (double w : sp.weights) {
        if (w != sp.weights.front()) return false;
    }
    double lo = sp.knots_v[static_cast<size_t>(sp.degree_v)], hi = sp.knots_v[static_cast<size_t>(sp.count_v)];
    for (double k : sp.knots_v) {
        if (k > lo && k < hi) return false;
    }
    size_t cu = static_cast<size_t>(sp.count_u), cv = static_cast<size_t>(sp.count_v);
    for (size_t c = 0; c < cu; ++c) {
        const Point3& first = sp.points[c];
        const Point3& last = sp.points[(cv - 1) * cu + c];
        double scale = 1.0 + norm(first) + norm(last);
        for (size_t r = 1; r + 1 < cv; ++r) {
            Point3 even = add(first, mul(sub(last, first), static_cast<double>(r) / static_cast<double>(cv - 1)));
            if (norm(sub(sp.points[r * cu + c], even)) > 1e-12 * scale) return false;
        }
    }
    return true;
}

// What the loops of a face make of its parameter box:
//   Enclosed: they bound it (the even-odd rule says what is inside)
//   Box: they do not (a whole sphere's one edge, used both ways, or none; a hemisphere's rim): the face is the box of
//        the surface's own range, cut where the loops cross a line
enum class Region { Enclosed, Box };

class Face {
public:
    Face(const FaceInfo& info, const Topology& topo) : info_(info), topo_(topo), surface_(info.surface) {}

    bool prepare(std::string& why);

    const FaceSurface& surface() const { return surface_; }
    double u0() const { return u0_; }
    double u1() const { return u1_; }
    double v0() const { return v0_; }
    double v1() const { return v1_; }
    bool full_u() const { return full_u_; }
    bool full_v() const { return full_v_; }
    // An edge of the face runs along the line at the start of the full period (a seam): the isolines of that
    // direction are then n between it and its other side, else n round the turn
    bool seam_u() const { return seam_u_; }
    bool seam_v() const { return seam_v_; }

    // The intervals of u inside the face on the line v = c (fixed_v) or of v on u = c, ordered
    std::vector<std::pair<double, double>> inside(bool fixed_v, double c) const;

private:
    // The point of a coedge at curve parameter t, as (u, v) near `near`
    UV uv_at(const EdgeInfo& e, double t, const UV& near) const;
    void crossings(bool fixed_v, double c, bool cutting, std::vector<double>& out) const;
    bool sphere_side(const CoedgeSamples& cs) const;

    const FaceInfo& info_;
    const Topology& topo_;
    FaceSurface surface_;
    std::vector<std::vector<CoedgeSamples>> loops_;
    Region region_ = Region::Enclosed;
    // per loop: bounds the face (a closed loop with an area), cuts it (closed, no area: an edge run both ways) or is open
    std::vector<bool> loop_cuts_;
    double u0_ = 0.0, u1_ = 0.0, v0_ = 0.0, v1_ = 0.0;
    bool full_u_ = false, full_v_ = false;
    bool seam_u_ = false, seam_v_ = false;
};

UV Face::uv_at(const EdgeInfo& e, double t, const UV& near) const {
    UV r;
    surface_.inverse(edge_point(e, t), near.u, near.v, r.u, r.v);
    r.u = wrap_near(r.u, near.u, surface_.period_u());
    r.v = wrap_near(r.v, near.v, surface_.period_v());
    return r;
}

// Whether a sphere face lies on the side of increasing latitude of a coedge of its rim: the face is on the left of
// its loops seen from outside (its normal: the surface's, turned when the face is reversed)
bool Face::sphere_side(const CoedgeSamples& cs) const {
    const AcisSurface& s = info_.surface;
    size_t i = cs.t.size() / 2;
    double t = cs.t[i];
    Point3 p = edge_point(*cs.edge, t);
    double h = 1e-6 * (std::fabs(cs.t.back() - cs.t.front()) + 1.0);
    double dir = cs.t.back() >= cs.t.front() ? 1.0 : -1.0;
    Point3 tangent = mul(sub(edge_point(*cs.edge, t + h), edge_point(*cs.edge, t - h)), dir);
    Point3 normal = mul(unit(sub(p, s.origin)), (s.radius < 0.0 ? -1.0 : 1.0) * (info_.reversed ? -1.0 : 1.0));
    Point3 left = cross(normal, tangent);
    Point3 su;
    acis_surface_point(s, cs.uv[i].u, cs.uv[i].v, p, &su);
    return dot(left, su) > 0.0;
}

bool Face::prepare(std::string& why) {
    if (!info_.surface_ok) {
        why = "a face's surface is of a kind that is not drawn";
        return false;
    }
    bool have = false;
    double umin = 0.0, umax = 0.0, vmin = 0.0, vmax = 0.0;
    UV last;
    bool started = false;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const LoopInfo& loop : info_.loops) {
        std::vector<CoedgeSamples> coedges;
        for (const CoedgeInfo& ci : loop.coedges) {
            const EdgeInfo& e = topo_.edges[static_cast<size_t>(ci.edge)];
            CoedgeSamples cs;
            cs.edge = &e;
            cs.coedge = &ci;
            if (!e.curve_ok) {
                coedges.push_back(cs);
                continue;
            }
            double ta = ci.reversed ? e.t1 : e.t0;
            double tb = ci.reversed ? e.t0 : e.t1;
            int n = e.curve.type == AcisCurveType::Straight && info_.surface.type == AcisSurfaceType::Plane ? 1 : 64;
            for (int i = 0; i <= n; ++i) {
                double t = i == n ? tb : ta + (tb - ta) * i / n;
                UV r;
                if (!started) {
                    // The first point of the first loop: the surface's own branch of an angle
                    surface_.inverse(edge_point(e, t), 0.0, 0.0, r.u, r.v);
                    started = true;
                } else {
                    r = uv_at(e, t, last);
                }
                cs.t.push_back(t);
                cs.uv.push_back(r);
                // an angle undefined (at a pole) leaves the one before it as the reference
                if (!std::isnan(r.u)) last.u = r.u;
                if (!std::isnan(r.v)) last.v = r.v;
            }
            coedges.push_back(cs);
        }
        loops_.push_back(coedges);
    }
    // The angle at a pole is that of the nearest point of its own edge
    for (auto& loop : loops_) {
        for (CoedgeSamples& cs : loop) {
            for (double UV::*member : {&UV::u, &UV::v}) {
                for (size_t i = 0; i < cs.uv.size(); ++i) {
                    if (!std::isnan(cs.uv[i].*member)) continue;
                    double fill = nan;
                    for (size_t k = i; k > 0 && std::isnan(fill); --k) fill = cs.uv[k - 1].*member;
                    for (size_t k = i + 1; k < cs.uv.size() && std::isnan(fill); ++k) fill = cs.uv[k].*member;
                    cs.uv[i].*member = fill;
                }
            }
        }
    }
    // (a whole edge at a pole: that of the point after it, or before it)
    for (auto& loop : loops_) {
        std::vector<UV*> all;
        for (CoedgeSamples& cs : loop) {
            for (UV& r : cs.uv) all.push_back(&r);
        }
        for (size_t i = 0; i < all.size(); ++i) {
            for (double UV::*member : {&UV::u, &UV::v}) {
                if (!std::isnan(all[i]->*member)) continue;
                double fill = nan;
                // the nearer of the neighbours along the edge it belongs to: the one before, else the one after
                for (size_t k = i; k > 0 && std::isnan(fill); --k) fill = all[k - 1]->*member;
                for (size_t k = i + 1; k < all.size() && std::isnan(fill); ++k) fill = all[k]->*member;
                if (std::isnan(fill)) fill = 0.0;
                all[i]->*member = fill;
            }
        }
    }
    // A lune of a sphere (two meridians): the angle between them is the way round that has the face on the left of the
    // first, seen from outside — not always the short one
    if (info_.surface.type == AcisSurfaceType::Sphere && loops_.size() == 1 && loops_[0].size() == 2 &&
        loops_[0][0].uv.size() > 2 && loops_[0][1].uv.size() > 2) {
        CoedgeSamples& c0 = loops_[0][0];
        CoedgeSamples& c1 = loops_[0][1];
        double v1 = c0.uv[c0.uv.size() / 2].v, v2 = c1.uv[c1.uv.size() / 2].v;
        if (std::fabs(c0.uv.front().v - v1) < 1e-9 && std::fabs(c1.uv.front().v - v2) < 1e-9) {
            const AcisSurface& sp = info_.surface;
            size_t i = c0.t.size() / 2;
            Point3 p = edge_point(*c0.edge, c0.t[i]);
            double h = 1e-6 * (std::fabs(c0.t.back() - c0.t.front()) + 1.0);
            double dir = c0.t.back() >= c0.t.front() ? 1.0 : -1.0;
            Point3 tangent = mul(sub(edge_point(*c0.edge, c0.t[i] + h), edge_point(*c0.edge, c0.t[i] - h)), dir);
            Point3 normal = mul(unit(sub(p, sp.origin)), (sp.radius < 0.0 ? -1.0 : 1.0) * (info_.reversed ? -1.0 : 1.0));
            Point3 left = cross(normal, tangent);
            Point3 su, sv;
            acis_surface_point(sp, c0.uv[i].u, v1, p, &su, &sv);
            double turn = std::fmod(v2 - v1, kTwoPi);
            if (turn < 0.0) turn += kTwoPi;
            double target = dot(left, sv) > 0.0 ? v1 + turn : v1 - (kTwoPi - turn);
            double shift = target - v2;
            for (UV& r : c1.uv) r.v += shift;
        }
    }
    for (const auto& loop : loops_) {
        for (const CoedgeSamples& cs : loop) {
            for (const UV& r : cs.uv) {
                if (!have) {
                    umin = umax = r.u;
                    vmin = vmax = r.v;
                    have = true;
                } else {
                    umin = std::min(umin, r.u);
                    umax = std::max(umax, r.u);
                    vmin = std::min(vmin, r.v);
                    vmax = std::max(vmax, r.v);
                }
            }
        }
    }

    // Do the loops bound the face? Each closed (its end where it starts) with an area between them
    bool all_closed = !loops_.empty();
    double area = 0.0;
    double extent0 = (umax - umin) * (vmax - vmin);
    loop_cuts_.assign(loops_.size(), false);
    size_t loop_index = 0;
    for (const auto& loop : loops_) {
        UV first, prev;
        bool any = false;
        double a = 0.0;
        for (const CoedgeSamples& cs : loop) {
            for (const UV& r : cs.uv) {
                if (!any) {
                    first = r;
                    any = true;
                } else {
                    a += prev.u * r.v - r.u * prev.v;
                }
                prev = r;
            }
        }
        if (!any) {
            all_closed = false;
            loop_cuts_[loop_index++] = true;
            continue;
        }
        a += prev.u * first.v - first.u * prev.v;
        // (at a pole of a sphere every angle is the same point)
        bool pole = info_.surface.type == AcisSurfaceType::Sphere && std::fabs(std::fabs(prev.u) - 0.5 * kPi) < 1e-7;
        bool closed = std::fabs(prev.u - first.u) <= 1e-7 && (pole || std::fabs(prev.v - first.v) <= 1e-7);
        if (!closed) all_closed = false;
        // a loop that does not bound (open, or closed with no area) cuts
        loop_cuts_[loop_index++] = !closed || std::fabs(0.5 * a) <= 1e-9 * (1.0 + extent0);
        area += 0.5 * a;
    }
    double extent = (umax - umin) * (vmax - vmin);
    bool any_cut = false;
    for (bool c : loop_cuts_) any_cut = any_cut || c;
    region_ = all_closed && !any_cut && std::fabs(area) > 1e-9 * (1.0 + extent) ? Region::Enclosed : Region::Box;
    if (std::getenv("ISODEBUG")) {
        std::fprintf(stderr, "region %s closed %d area %g box u %g..%g v %g..%g loops %zu\n", region_ == Region::Enclosed ? "enclosed" : "box",
                     all_closed, area, umin, umax, vmin, vmax, loops_.size());
        for (const auto& loop : loops_) {
            for (const CoedgeSamples& cs : loop) {
                if (cs.uv.empty()) continue;
                std::fprintf(stderr, "  coedge edge %d: (%g,%g) -> (%g,%g)\n", cs.edge->record, cs.uv.front().u, cs.uv.front().v, cs.uv.back().u,
                             cs.uv.back().v);
            }
        }
    }

    if (info_.surface.type == AcisSurfaceType::Spline) {
        double bu0, bu1, bv0, bv1;
        if (surface_.box(bu0, bu1, bv0, bv1)) {
            // A spline's parameter range: a face with its four edges is all of it. A periodic direction the loops go
            // all the way round in is theirs (its seams where the face's edges are: a torus converted to NURBS,
            // acad_*_surfaces_nurbs_converted 177D); in another the loops are moved by whole periods into the
            // spline's range (a cone converted to NURBS whose loops the inverse put a turn off, 177F)
            double pu = surface_.period_u(), pv = surface_.period_v();
            bool loops_u = pu > 0.0 && std::fabs((umax - umin) - pu) < 1e-7;
            bool loops_v = pv > 0.0 && std::fabs((vmax - vmin) - pv) < 1e-7;
            double moves[2] = {pu > 0.0 && !loops_u ? pu * std::ceil((bu0 - umin) / pu - 1e-9) : 0.0,
                               pv > 0.0 && !loops_v ? pv * std::ceil((bv0 - vmin) / pv - 1e-9) : 0.0};
            for (auto& loop : loops_) {
                for (CoedgeSamples& cs : loop) {
                    for (UV& r : cs.uv) {
                        r.u += moves[0];
                        r.v += moves[1];
                    }
                }
            }
            if (!loops_u) {
                umin = bu0;
                umax = bu1;
            }
            if (!loops_v) {
                vmin = bv0;
                vmax = bv1;
            }
            have = true;
        }
    } else if (region_ == Region::Box) {
        // The surface's own range, where the loops do not set it
        const AcisSurface& s = info_.surface;
        if (s.type == AcisSurfaceType::Sphere) {
            // The sphere's own range, from each rim (a circle of latitude) to the side the face lies on (two rims:
            // the band between them, acad_*_surfaces_trim_trimmed BCF)
            umin = -0.5 * kPi;
            umax = 0.5 * kPi;
            vmin = -kPi;
            vmax = kPi;
            for (const auto& loop : loops_) {
                for (const CoedgeSamples& cs : loop) {
                    if (cs.uv.size() < 2) continue;
                    double lo = cs.uv.front().u, hi = lo;
                    for (const UV& r : cs.uv) {
                        lo = std::min(lo, r.u);
                        hi = std::max(hi, r.u);
                    }
                    if (hi - lo < 1e-9 && std::fabs(std::fabs(cs.uv.back().v - cs.uv.front().v) - kTwoPi) < 1e-6) {
                        if (sphere_side(cs)) {
                            umin = std::max(umin, lo);
                        } else {
                            umax = std::min(umax, lo);
                        }
                    }
                }
            }
            have = true;
        } else if (!have) {
            why = "a face has no boundary that can be drawn in its parameters";
            return false;
        }
    }
    if (!have) {
        why = "a face has no boundary that can be drawn in its parameters";
        return false;
    }
    u0_ = umin;
    u1_ = umax;
    v0_ = vmin;
    v1_ = vmax;
    full_u_ = surface_.period_u() > 0.0 && std::fabs((u1_ - u0_) - surface_.period_u()) < 1e-7;
    full_v_ = surface_.period_v() > 0.0 && std::fabs((v1_ - v0_) - surface_.period_v()) < 1e-7;
    // A whole period starts where AutoCAD's own range does: in -pi … pi
    if (full_u_) {
        double shift = -surface_.period_u() * std::floor((u0_ + 0.5 * surface_.period_u() + 1e-9) / surface_.period_u());
        u0_ += shift;
        u1_ = u0_ + surface_.period_u();
        for (auto& loop : loops_) {
            for (CoedgeSamples& cs : loop) {
                for (UV& r : cs.uv) r.u += shift;
            }
        }
    }
    if (full_v_) {
        double shift = -surface_.period_v() * std::floor((v0_ + 0.5 * surface_.period_v() + 1e-9) / surface_.period_v());
        v0_ += shift;
        v1_ = v0_ + surface_.period_v();
        for (auto& loop : loops_) {
            for (CoedgeSamples& cs : loop) {
                for (UV& r : cs.uv) r.v += shift;
            }
        }
    }
    // A seam: an edge along the first line of a whole period, from one end of the other direction to the other
    auto seam = [&](bool along_v) {
        std::vector<std::pair<double, double>> pieces;
        for (const auto& loop : loops_) {
            for (const CoedgeSamples& cs : loop) {
                if (cs.uv.size() < 2) continue;
                double fixed_lo = along_v ? cs.uv.front().u : cs.uv.front().v, fixed_hi = fixed_lo;
                double run_lo = along_v ? cs.uv.front().v : cs.uv.front().u, run_hi = run_lo;
                for (const UV& r : cs.uv) {
                    double f = along_v ? r.u : r.v;
                    double q = along_v ? r.v : r.u;
                    fixed_lo = std::min(fixed_lo, f);
                    fixed_hi = std::max(fixed_hi, f);
                    run_lo = std::min(run_lo, q);
                    run_hi = std::max(run_hi, q);
                }
                if (fixed_hi - fixed_lo > 1e-9) continue;
                double period = along_v ? surface_.period_u() : surface_.period_v();
                double start = along_v ? u0_ : v0_;
                double d = std::fmod(std::fabs(fixed_lo - start), period);
                if (std::min(d, period - d) > 1e-6) continue;
                pieces.push_back({run_lo, run_hi});
            }
        }
        // the edges on the line cover it from one end to the other (one edge, or two slits from the equator to the
        // poles: a sphere converted to NURBS, acad_*_surfaces_nurbs_converted 177E)
        double lo = along_v ? v0_ : u0_, hi = along_v ? v1_ : u1_;
        std::sort(pieces.begin(), pieces.end());
        double reach = lo;
        for (const auto& piece : pieces) {
            if (piece.first > reach + (hi - lo) * 1e-6) break;
            reach = std::max(reach, piece.second);
        }
        return !pieces.empty() && reach > hi - (hi - lo) * 1e-6;
    };
    // The seam of v (a line v = const) runs along u
    if (full_v_) seam_v_ = seam(false);
    if (full_u_) seam_u_ = seam(true);
    return true;
}

// Where the loops cross the line (fixed_v: v = c, else u = c), found on the edges' curves themselves
void Face::crossings(bool fixed_v, double c, bool cutting, std::vector<double>& out) const {
    size_t loop_number = 0;
    for (const auto& loop : loops_) {
        bool is_cut = region_ == Region::Box && loop_cuts_[loop_number];
        ++loop_number;
        if (is_cut != cutting) continue;
        for (const CoedgeSamples& cs : loop) {
            if (!cs.edge || cs.uv.size() < 2) continue;
            for (size_t i = 0; i + 1 < cs.uv.size(); ++i) {
                double a = fixed_v ? cs.uv[i].v : cs.uv[i].u;
                double b = fixed_v ? cs.uv[i + 1].v : cs.uv[i + 1].u;
                bool crosses = (a <= c && c < b) || (b <= c && c < a);
                // a cut ends where it ends: the line through its end is cut too
                // (to the rounding of its end: a half equator from 0 to pi cuts the meridian at pi, 13E1 of
                // acad_*_surfaces_nurbs)
                if (cutting && !crosses) crosses = a != b && std::min(a, b) - 1e-12 <= c && c <= std::max(a, b) + 1e-12;
                if (!crosses) continue;
                double ta = cs.t[i], tb = cs.t[i + 1];
                UV ua = cs.uv[i];
                double fa = a - c;
                for (int it = 0; it < 80; ++it) {
                    double tm = 0.5 * (ta + tb);
                    UV um = uv_at(*cs.edge, tm, ua);
                    double fm = (fixed_v ? um.v : um.u) - c;
                    if ((fm <= 0.0) == (fa <= 0.0)) {
                        ta = tm;
                        ua = um;
                        fa = fm;
                    } else {
                        tb = tm;
                    }
                    if (std::fabs(tb - ta) < 1e-15 * (1.0 + std::fabs(ta))) break;
                }
                UV r = uv_at(*cs.edge, 0.5 * (ta + tb), ua);
                out.push_back(fixed_v ? r.u : r.v);
            }
        }
        // Where one coedge ends and the next starts elsewhere in the parameters (at a pole): a straight piece
        for (size_t i = 0; i < loop.size(); ++i) {
            const CoedgeSamples& from = loop[i];
            const CoedgeSamples& to = loop[(i + 1) % loop.size()];
            if (from.uv.empty() || to.uv.empty()) continue;
            UV pa = from.uv.back(), pb = to.uv.front();
            if (std::fabs(pa.u - pb.u) < 1e-9 && std::fabs(pa.v - pb.v) < 1e-9) continue;
            if (!(info_.surface.type == AcisSurfaceType::Sphere && std::fabs(std::fabs(pa.u) - 0.5 * kPi) < 1e-7 &&
                  std::fabs(pa.u - pb.u) < 1e-9)) {
                continue;
            }
            double a = fixed_v ? pa.v : pa.u;
            double b = fixed_v ? pb.v : pb.u;
            if (!((a <= c && c < b) || (b <= c && c < a))) continue;
            double f = (c - a) / (b - a);
            out.push_back(fixed_v ? pa.u + f * (pb.u - pa.u) : pa.v + f * (pb.v - pa.v));
        }
    }
}

std::vector<std::pair<double, double>> Face::inside(bool fixed_v, double c) const {
    std::vector<double> x;
    crossings(fixed_v, c, false, x);
    std::sort(x.begin(), x.end());
    double lo = fixed_v ? u0_ : v0_, hi = fixed_v ? u1_ : v1_;
    std::vector<std::pair<double, double>> out;
    if (region_ == Region::Enclosed) {
        for (size_t i = 0; i + 1 < x.size(); i += 2) {
            if (x[i + 1] - x[i] > 1e-9) out.push_back({x[i], x[i + 1]});
        }
        return out;
    }
    // The whole box: a bounding loop (a hole) takes pieces out of it, a loop that cuts (an edge run both ways, a rim)
    // splits it where it meets the line (once where an edge is run both ways)
    std::vector<double> cuts;
    crossings(fixed_v, c, true, cuts);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double p, double q) { return std::fabs(p - q) < 1e-9; }), cuts.end());
    struct Event {
        double at;
        bool cut;
    };
    std::vector<Event> events;
    for (double v : x) events.push_back({v, false});
    for (double v : cuts) events.push_back({v, true});
    std::stable_sort(events.begin(), events.end(), [](const Event& p, const Event& q) { return p.at < q.at; });
    bool in = true;
    double from = lo;
    for (const Event& ev : events) {
        if (ev.at <= lo + 1e-9 || ev.at >= hi - 1e-9) continue;
        if (ev.cut) {
            if (in && ev.at - from > 1e-9) {
                out.push_back({from, ev.at});
                from = ev.at;
            }
        } else if (in) {
            if (ev.at - from > 1e-9) out.push_back({from, ev.at});
            in = false;
        } else {
            from = ev.at;
            in = true;
        }
    }
    if (in && hi - from > 1e-9) out.push_back({from, hi});
    return out;
}

// ---------------------------------------------------------------------------
// The isolines of a face
// ---------------------------------------------------------------------------

class FaceDrawer {
public:
    FaceDrawer(Drawer& d, const Face& face, const IsolineOptions& o, double tolerance)
        : d_(d), face_(face), s_(face.surface().surface()), fs_(face.surface()), o_(o), tolerance_(tolerance) {}

    bool draw(int marker, std::string& why);
    // False when a curve did not come within the tolerance (tessellate())
    bool tolerance_reached() const { return tolerance_reached_; }

private:
    // How many curves of a family: n, or its share of the turn when the face goes only part of the way round
    int count(int n, bool full, double lo, double hi, double period) const {
        // (a surface of revolution of a curve has n curves of its turn also when it goes part of the way round:
        // acad_*_surfaces_analytic 291 and 293, a line and a spline revolved 90 degrees)
        if (period > 0.0 && !full && s_.type != AcisSurfaceType::Revolution) {
            double share = (hi - lo) / period;
            // rounded, and never none of n > 0 (AutoCAD's counts files: 1 + 1 isolines on a 30 degree arc give one of each)
            return std::max(n > 0 ? 1 : 0, static_cast<int>(std::floor(n * share + 0.5)));
        }
        return n;
    }
    bool curve_u(double v, double a, double b, std::string& why);    // the curve along u at v, from a to b
    bool curve_v(double u, double a, double b, std::string& why);    // the curve along v at u
    bool along_curve(const AcisCurve& c, const std::function<Point3(double)>& S, double a, double b, std::string& why);
    void circle_or_arc(const std::function<Point3(double)>& at, double a, double b, bool full);
    void tess(const std::function<Point3(double)>& at, const std::vector<double>& knots, int degree, double a, double b);

    Drawer& d_;
    const Face& face_;
    const AcisSurface& s_;
    const FaceSurface& fs_;
    const IsolineOptions& o_;
    double tolerance_;
    bool tolerance_reached_ = true;
};

// A part of a circle by its parameter (the angle): the whole circle by three points, else start, middle and end
void FaceDrawer::circle_or_arc(const std::function<Point3(double)>& at, double a, double b, bool full) {
    if (full) {
        d_.circle(at(a), at(kTwoPi / 3.0), at(-kTwoPi / 3.0));
    } else {
        d_.arc(at(a), at(0.5 * (a + b)), at(b));
    }
}

void FaceDrawer::tess(const std::function<Point3(double)>& at, const std::vector<double>& knots, int degree, double a, double b) {
    std::vector<double> ks = span_knots(knots, a, b);
    std::vector<double> params;
    if (!tessellate(at, ks, degree, tolerance_, params)) tolerance_reached_ = false;
    std::vector<Point3> pts;
    pts.reserve(params.size());
    for (double t : params) pts.push_back(at(t));
    d_.polyline(pts);
}

// A piece of an isoline that is one of the surface's own defining curves moved or turned (a revolution's profile, a
// sum's curves): drawn as that kind of curve
bool FaceDrawer::along_curve(const AcisCurve& c, const std::function<Point3(double)>& S, double a, double b, std::string& why) {
    if (c.type == AcisCurveType::Straight) {
        d_.line(S(a), S(b));
    } else if (c.type == AcisCurveType::Ellipse && circular(c)) {
        circle_or_arc(S, a, b, std::fabs((b - a) - kTwoPi) < 1e-7);
    } else if (c.type == AcisCurveType::Spline) {
        std::vector<double> all = c.spline.knots;
        if (c.reversed) {
            for (double& k : all) k = -k;
            std::sort(all.begin(), all.end());
        }
        tess(S, all, c.spline.degree, a, b);
    } else {
        why = "a surface of revolution or sum of this curve is not drawn";
        return false;
    }
    return true;
}

bool FaceDrawer::curve_u(double v, double a, double b, std::string& why) {
    auto S = [&](double u) { return fs_.at(u, v); };
    switch (s_.type) {
        case AcisSurfaceType::Plane:
        case AcisSurfaceType::Cone:
            d_.line(S(a), S(b));
            return true;
        case AcisSurfaceType::Sphere:
            d_.arc(S(a), S(0.5 * (a + b)), S(b));         // a meridian: part of a circle about the center
            return true;
        case AcisSurfaceType::Torus:
            circle_or_arc(S, a, b, std::fabs((b - a) - kTwoPi) < 1e-7);
            return true;
        case AcisSurfaceType::Spline:
            tess(S, fs_.knots(true), s_.spline.degree_u, a, b);
            return true;
        case AcisSurfaceType::Revolution:
            // the profile turned to v: its own kind of curve
            return along_curve(s_.curves[0], S, a, b, why);
        case AcisSurfaceType::Sum:
            // the first curve moved by the second's point at v
            return along_curve(s_.curves[0], S, a, b, why);
        default:
            why = "a surface of this kind is not drawn";
            return false;
    }
}

bool FaceDrawer::curve_v(double u, double a, double b, std::string& why) {
    auto S = [&](double v) { return fs_.at(u, v); };
    bool whole = std::fabs((b - a) - kTwoPi) < 1e-7;
    switch (s_.type) {
        case AcisSurfaceType::Plane:
            d_.line(S(a), S(b));
            return true;
        case AcisSurfaceType::Cone: {
            double major = norm(s_.major);
            double along = fs_.cone_along(u);
            double f = 1.0 + s_.sine * along / major;
            if (std::fabs(s_.ratio - 1.0) < 1e-9) {
                circle_or_arc(S, a, b, whole);
            } else {
                Point3 center = add(s_.origin, mul(s_.axis, s_.cosine * along));
                Point3 minor = mul(cross(s_.axis, s_.major), s_.ratio);
                d_.ellipse(center, mul(s_.major, f), mul(minor, f), a, b);
            }
            return true;
        }
        case AcisSurfaceType::Sphere:
        case AcisSurfaceType::Torus:
        case AcisSurfaceType::Revolution:
            circle_or_arc(S, a, b, whole);
            return true;
        case AcisSurfaceType::Sum:
            return along_curve(s_.curves[1], S, a, b, why);
        case AcisSurfaceType::Spline: {
            if (s_.ruled) {
                // straight along v (an extrusion, a ruled surface): ACIS's v curve is a line
                d_.line(S(a), S(b));
                return true;
            }
            std::vector<double> knots = fs_.knots(false);
            tess(S, knots, s_.spline.degree_v, a, b);
            return true;
        }
        default:
            why = "a surface of this kind is not drawn";
            return false;
    }
}

bool FaceDrawer::draw(int marker, std::string& why) {
    int n1 = count(o_.u_isolines, face_.full_v(), face_.v0(), face_.v1(), fs_.period_v());
    int n2 = count(o_.v_isolines, face_.full_u(), face_.u0(), face_.u1(), fs_.period_u());
    // Round a whole turn: n lines n-th of the way, the last on the line the turn starts from (when no edge is there);
    // else n lines of n + 1 parts
    if (std::getenv("ISODEBUG")) {
        std::fprintf(stderr, "face u %g..%g v %g..%g full %d %d seam %d %d n %d %d\n", face_.u0(), face_.u1(), face_.v0(), face_.v1(),
                     face_.full_u(), face_.full_v(), face_.seam_u(), face_.seam_v(), n1, n2);
    }
    int parts1 = face_.full_v() && !face_.seam_v() ? std::max(n1, 1) : n1 + 1;
    int parts2 = face_.full_u() && !face_.seam_u() ? std::max(n2, 1) : n2 + 1;
    for (int k = 1; k <= n1; ++k) {
        double v = face_.v0() + (face_.v1() - face_.v0()) * k / parts1;
        for (const auto& iv : face_.inside(true, v)) {
            d_.begin(marker);
            if (!curve_u(v, iv.first, iv.second, why)) return false;
        }
    }
    for (int k = 1; k <= n2; ++k) {
        double u = face_.u0() + (face_.u1() - face_.u0()) * k / parts2;
        for (const auto& iv : face_.inside(false, u)) {
            d_.begin(marker);
            if (!curve_v(u, iv.first, iv.second, why)) return false;
        }
    }
    return true;
}

} // namespace

bool acis_straight_in_v(const AcisSurface& s) { return straight_in_v(s); }

bool acis_isoline_graphics(const AcisFile& file, const IsolineOptions& options, ProxyGraphics& out, std::string& why) {
    Topology topo;
    if (!build_topology(file, topo, why)) return false;
    std::vector<AcisBody> bodies = acis_bodies(file);
    if (bodies.empty()) {
        why = "the SAT has no body";
        return false;
    }
    Mapper map;
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 4; ++k) map.m[i][k] = bodies[0].transform[i][k];
    }
    double T = options.tolerance > 0.0 ? options.tolerance : options.view_height * kViewTolerance * 1000.0 / (options.circle_zoom > 0 ? options.circle_zoom : 1000);
    double Te = T * options.edge_ratio;
    Drawer d(out, map, options.color);
    if (std::getenv("ISODEBUG")) {
        for (const EdgeInfo& e : topo.edges) {
            std::fprintf(stderr, "edge rec %d index %d coedge %d curve %d start(%g,%g,%g) end(%g,%g,%g) t %g..%g\n", e.record, e.index, e.coedge,
                         e.curve_record, e.start.x, e.start.y, e.start.z, e.end.x, e.end.y, e.end.z, e.t0, e.t1);
        }
    }
    std::vector<const EdgeInfo*> by_index(topo.edges.size(), nullptr);
    for (const EdgeInfo& e : topo.edges) by_index[static_cast<size_t>(e.index - 1)] = &e;
    for (const EdgeInfo* e : by_index) {
        if (!e) continue;
        d.begin(4 * e->index + 2);
        if (!draw_edge(d, *e, Te, why)) return false;
    }
    int number = 0;
    for (const FaceInfo& info : topo.faces) {
        ++number;
        Face face(info, topo);
        if (!face.prepare(why)) return false;
        FaceDrawer fd(d, face, options, T);
        if (!fd.draw(4 * number + 1, why)) return false;
        if (!fd.tolerance_reached()) {
            why = kToleranceNotReached;
            return false;
        }
    }
    d.color_only();
    for (const int v : topo.vertices) {
        auto it = topo.vertex_point.find(v);
        if (it != topo.vertex_point.end()) d.vertex(it->second);
    }
    return true;
}

} // namespace internal
} // namespace hdxf
