#include "validation.hpp"

#include "format.hpp"
#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace hdxf::acis::detail {

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

std::string check_transform(const Transform& transform, const Tolerances& tolerance) {
    if (!finite(transform.translation)) return "the transform's translation is not finite";
    if (!(transform.scale > 0.0) || !std::isfinite(transform.scale)) {
        return "the transform's scale must be positive (a uniform scale; no reflection)";
    }
    Vector3 columns[3];
    for (int k = 0; k < 3; ++k) {
        columns[k] = {transform.rotation[0][k], transform.rotation[1][k], transform.rotation[2][k]};
        if (!finite(columns[k])) return "the transform's rotation is not finite";
    }
    for (int k = 0; k < 3; ++k) {
        if (std::fabs(length(columns[k]) - 1.0) > tolerance.direction) {
            return "the transform's rotation is not orthonormal (a shear or a scale in it)";
        }
        if (std::fabs(dot(columns[k], columns[(k + 1) % 3])) > tolerance.direction) {
            return "the transform's rotation is not orthonormal (a shear in it)";
        }
    }
    if (dot(columns[0], cross(columns[1], columns[2])) < 0.0) {
        return "the transform's rotation reflects; the SAT body transform here is a rotation";
    }
    return {};
}

bool coedge_increasing(const CoedgeRecord& coedge, const EdgeRecord& edge) {
    return coedge.reversed == edge.against_curve;
}

namespace {

constexpr int kEllipseSamples = 32;

bool closed_kind(BodyKind kind) { return kind == BodyKind::Solid || kind == BodyKind::ClosedBody; }

// One use of an edge in a loop, with where it is
struct CoedgeContext {
    std::uint32_t coedge = 0;
    std::uint32_t face = 0;
    std::string path;                   // up to and including the coedge
};

// All uses of one edge
struct EdgeUse {
    std::vector<std::uint32_t> coedges;
    std::set<std::uint32_t> shells;
    std::string shell_path;             // the first use's shell
    std::uint32_t first_face = 0;
};

class Validator {
public:
    Validator(const BuilderData& data, BodyId body) : d_(data), body_id_(body), body_(data.bodies[body.index]) {}

    std::vector<Diagnostic> run();

private:
    void report(ErrorCode code, const std::string& entity, const std::string& path, const std::string& message) {
        errors_.push_back(make_diagnostic(code, entity, path, message));
    }

    std::string tolerance_text() const { return real_text(d_.options.tolerance.distance); }

    void check_lumps();
    void check_shell(ShellId id, const std::string& lump_path, const detail::LumpRecord& lump);
    void check_face(FaceId id, std::uint32_t shell_index, const std::string& shell_path);
    void check_loop(LoopId id, std::uint32_t face_index, const std::string& face_path, std::uint32_t shell_index,
                    const std::string& shell_path);
    bool surface_good(SurfaceId id, const std::string& path);
    bool curve_good(CurveId id, const std::string& path);

    void check_edges();
    void check_edge(std::uint32_t edge_index, const EdgeUse& use);
    void check_uses();
    void check_partners();
    void check_coedge_geometry();
    void check_loop_orientation(LoopId id, const std::string& path);
    void check_coedge_pcurve(const CoedgeContext& context);

    std::vector<Point3> coedge_points(const CoedgeRecord& coedge, int samples) const;

    const BuilderData& d_;
    BodyId body_id_;
    const BodyRecord& body_;
    std::vector<Diagnostic> errors_;

    std::set<std::uint32_t> surfaces_seen_;
    std::set<std::uint32_t> surfaces_bad_;
    std::set<std::uint32_t> curves_seen_;
    std::set<std::uint32_t> curves_bad_;
    std::set<std::uint32_t> pcurves_used_;
    std::set<std::uint32_t> edges_good_;

    std::map<std::uint32_t, EdgeUse> uses_;
    std::vector<std::uint32_t> edge_order_;
    std::vector<CoedgeContext> coedges_;
    std::vector<std::pair<LoopId, std::string>> planar_loops_;
    std::map<std::uint32_t, std::string> shell_paths_;
};

std::string body_text(BodyId id) { return handle_text(id); }

// ---------------------------------------------------------------------------
// Structure: lumps, shells, faces, loops
// ---------------------------------------------------------------------------

void Validator::check_lumps() {
    std::string body_path = body_text(body_id_);
    if (body_.has_transform) {
        std::string why = check_transform(body_.transform, d_.options.tolerance);
        if (!why.empty()) report(ErrorCode::InvalidGeometry, body_path, body_path, why);
    }
    if (body_.lumps.empty()) {
        report(ErrorCode::EmptyBody, body_path, body_path, "the body has no lump");
        return;
    }
    if (body_.kind == BodyKind::Region && (body_.lumps.size() != 1)) {
        report(ErrorCode::EmptyBody, body_path, body_path, "a REGION has one lump");
    }
    for (const LumpId& lump_id : body_.lumps) {
        const LumpRecord& lump = d_.lumps[lump_id.index];
        std::string lump_path = body_path + "/" + handle_text(lump_id);
        if (lump.shells.empty()) {
            report(ErrorCode::EmptyBody, handle_text(lump_id), lump_path, "the lump has no shell");
            continue;
        }
        int outer = 0;
        for (const ShellId& s : lump.shells) {
            if (d_.shells[s.index].role == ShellRole::Outer) ++outer;
        }
        if (outer != 1) {
            report(ErrorCode::ShellRole, handle_text(lump_id), lump_path,
                   "a lump has exactly one outer shell; found " + integer_text(outer));
        }
        if (body_.kind == BodyKind::Region && lump.shells.size() != 1) {
            report(ErrorCode::ShellRole, handle_text(lump_id), lump_path, "a REGION has one shell");
        }
        for (const ShellId& s : lump.shells) {
            if (d_.shells[s.index].role == ShellRole::Void && !closed_kind(body_.kind)) {
                report(ErrorCode::ShellRole, handle_text(s), lump_path + "/" + handle_text(s),
                       "only a body with closed shells has void shells");
            }
            check_shell(s, lump_path, lump);
        }
    }
}

void Validator::check_shell(ShellId id, const std::string& lump_path, const LumpRecord&) {
    const ShellRecord& shell = d_.shells[id.index];
    std::string path = lump_path + "/" + handle_text(id);
    shell_paths_[id.index] = path;
    if (shell.faces.empty()) {
        report(ErrorCode::EmptyBody, handle_text(id), path, "the shell has no face");
        return;
    }
    for (const FaceId& f : shell.faces) check_face(f, id.index, path);
}

bool Validator::surface_good(SurfaceId id, const std::string& path) {
    if (surfaces_seen_.insert(id.index).second) {
        const SurfaceGeometry& g = d_.surfaces[id.index].geometry;
        std::string why = check_surface(g, d_.options.tolerance);
        if (!why.empty()) {
            surfaces_bad_.insert(id.index);
            bool spline = std::holds_alternative<BSplineSurfaceGeometry>(g);
            report(spline ? ErrorCode::InvalidBSpline : ErrorCode::InvalidGeometry, handle_text(id), path, why);
        }
    }
    return surfaces_bad_.count(id.index) == 0;
}

bool Validator::curve_good(CurveId id, const std::string& path) {
    if (curves_seen_.insert(id.index).second) {
        const CurveGeometry& g = d_.curves[id.index].geometry;
        std::string why = check_curve(g, d_.options.tolerance);
        if (!why.empty()) {
            curves_bad_.insert(id.index);
            bool spline = std::holds_alternative<BSplineCurveGeometry>(g);
            report(spline ? ErrorCode::InvalidBSpline : ErrorCode::InvalidGeometry, handle_text(id), path, why);
        }
    }
    return curves_bad_.count(id.index) == 0;
}

void Validator::check_face(FaceId id, std::uint32_t shell_index, const std::string& shell_path) {
    const FaceRecord& face = d_.faces[id.index];
    const ShellRecord& shell = d_.shells[shell_index];
    std::string path = shell_path + "/" + handle_text(id);
    bool good = surface_good(face.surface, path);
    const SurfaceGeometry& surface = d_.surfaces[face.surface.index].geometry;
    bool planar = std::holds_alternative<PlaneGeometry>(surface);
    bool closed_surface = std::holds_alternative<SphereGeometry>(surface) || std::holds_alternative<TorusGeometry>(surface);

    if (body_.kind == BodyKind::Region && !planar) {
        report(ErrorCode::InvalidGeometry, handle_text(id), path, "a REGION is planar: its faces lie on planes");
    }
    if (face.loops.empty()) {
        if (!closed_surface) {
            report(ErrorCode::LooplessFace, handle_text(id), path,
                   "a face without loops is a whole sphere or torus; this surface needs a boundary");
        } else if (shell.faces.size() != 1) {
            report(ErrorCode::LooplessFace, handle_text(id), path,
                   "a face without loops is a whole sphere or torus and is alone in its shell");
        }
        return;
    }
    if (planar && good) {
        int outer = 0;
        for (const LoopId& l : face.loops) {
            LoopRole role = d_.loops[l.index].role;
            if (role == LoopRole::Outer) ++outer;
            if (role == LoopRole::Boundary) {
                report(ErrorCode::LoopRole, handle_text(l), path + "/" + handle_text(l),
                       "a loop on a planar face is outer or inner");
            }
        }
        if (outer != 1) {
            report(ErrorCode::LoopRole, handle_text(id), path,
                   "a planar face has exactly one outer loop; found " + integer_text(outer));
        }
    }
    for (const LoopId& l : face.loops) check_loop(l, id.index, path, shell_index, shell_path);
}

void Validator::check_loop(LoopId id, std::uint32_t face_index, const std::string& face_path,
                           std::uint32_t shell_index, const std::string& shell_path) {
    const LoopRecord& loop = d_.loops[id.index];
    std::string path = face_path + "/" + handle_text(id);
    if (loop.coedges.empty()) {
        report(ErrorCode::LoopNotClosed, handle_text(id), path, "the loop has no coedge");
        return;
    }
    size_t n = loop.coedges.size();
    auto start_vertex = [&](const CoedgeRecord& c) {
        const EdgeRecord& e = d_.edges[c.edge.index];
        return c.reversed ? e.end : e.start;
    };
    auto end_vertex = [&](const CoedgeRecord& c) {
        const EdgeRecord& e = d_.edges[c.edge.index];
        return c.reversed ? e.start : e.end;
    };
    for (size_t i = 0; i < n; ++i) {
        const CoedgeId& cid = loop.coedges[i];
        const CoedgeRecord& c = d_.coedges[cid.index];
        const CoedgeRecord& next = d_.coedges[loop.coedges[(i + 1) % n].index];
        std::string cpath = path + "/" + handle_text(cid);

        VertexId a = end_vertex(c);
        VertexId b = start_vertex(next);
        if (!(a == b)) {
            double gap = distance(d_.vertices[a.index].position, d_.vertices[b.index].position);
            std::string next_text = handle_text(loop.coedges[(i + 1) % n]);
            if (gap <= d_.options.tolerance.distance) {
                report(ErrorCode::VertexMismatch, handle_text(cid), cpath,
                       handle_text(cid) + " ends at " + handle_text(a) + " and " + next_text + " starts at " +
                           handle_text(b) + ", the same position: share one vertex");
            } else {
                report(ErrorCode::LoopNotClosed, handle_text(cid), cpath,
                       handle_text(cid) + " ends at " + handle_text(a) + " but " + next_text + " starts at " +
                           handle_text(b) + ", " + real_text(gap) + " mm away");
            }
        }

        auto inserted = uses_.try_emplace(c.edge.index);
        EdgeUse& use = inserted.first->second;
        if (inserted.second) {
            edge_order_.push_back(c.edge.index);
            use.shell_path = shell_path;
            use.first_face = face_index;
        }
        use.coedges.push_back(cid.index);
        use.shells.insert(shell_index);

        CoedgeContext context;
        context.coedge = cid.index;
        context.face = face_index;
        context.path = cpath;
        coedges_.push_back(context);
    }
    if (std::holds_alternative<PlaneGeometry>(d_.surfaces[d_.faces[face_index].surface.index].geometry)) {
        planar_loops_.push_back({id, path});
    }
}

// ---------------------------------------------------------------------------
// Edges
// ---------------------------------------------------------------------------

void Validator::check_edges() {
    for (std::uint32_t index : edge_order_) check_edge(index, uses_[index]);
}

void Validator::check_edge(std::uint32_t edge_index, const EdgeUse& use) {
    const EdgeRecord& edge = d_.edges[edge_index];
    EdgeId id = d_.make_handle<HandleKind::Edge>(edge_index);
    std::string path = use.shell_path + "/" + handle_text(id);
    const Point3& p_start = d_.vertices[edge.start.index].position;
    const Point3& p_end = d_.vertices[edge.end.index].position;
    if (!finite(p_start) || !finite(p_end)) {
        report(ErrorCode::InvalidGeometry, handle_text(id), path, "a vertex of the edge has a position that is not finite");
        return;
    }

    if (!edge.curve.valid()) {
        // Degenerate: one vertex, used once (a cone's apex)
        if (!(edge.start == edge.end)) {
            report(ErrorCode::VertexMismatch, handle_text(id), path, "an edge without a curve has the same vertex at both ends");
            return;
        }
        const FaceRecord& face = d_.faces[use.first_face];
        // AutoCAD's cone has it at its apex. A sphere with its seam and its poles as degenerate edges is read,
        // but its surface area is measured as 0: only the cone's apex is written this way.
        if (!std::holds_alternative<ConeGeometry>(d_.surfaces[face.surface.index].geometry)) {
            report(ErrorCode::UnsupportedGeometry, handle_text(id), path,
                   "a degenerate edge is written at a cone's apex only; a whole sphere or torus is a face without loops");
            return;
        }
        double off = 0.0;
        if (analytic_distance(d_.surfaces[face.surface.index].geometry, p_start, off) &&
            off > d_.options.tolerance.distance) {
            report(ErrorCode::SurfaceMismatch, handle_text(id), path,
                   "the degenerate edge's vertex is " + real_text(off) + " mm from the surface of " +
                       handle_text(FaceId{d_.id, use.first_face}) + "; the tolerance is " + tolerance_text() + " mm");
            return;
        }
        edges_good_.insert(edge_index);
        return;
    }

    if (!curve_good(edge.curve, path)) return;
    const CurveGeometry& curve = d_.curves[edge.curve.index].geometry;
    double t0 = edge.start_parameter;
    double t1 = edge.end_parameter;
    if (!std::isfinite(t0) || !std::isfinite(t1) || !(t1 - t0 > d_.options.tolerance.parameter)) {
        report(ErrorCode::InvalidGeometry, handle_text(id), path,
               "the edge's parameters must increase: start " + real_text(t0) + ", end " + real_text(t1));
        return;
    }
    double low = 0.0;
    double high = 0.0;
    curve_domain(curve, low, high);
    double slack = d_.options.tolerance.parameter;
    if (t0 < low - slack || t1 > high + slack) {
        report(ErrorCode::InvalidGeometry, handle_text(id), path,
               "the edge's parameters [" + real_text(t0) + ", " + real_text(t1) + "] are outside the curve's [" +
                   real_text(low) + ", " + real_text(high) + "]");
        return;
    }
    if (std::holds_alternative<EllipseGeometry>(curve) && t1 - t0 > kTwoPi + 1e-9) {
        report(ErrorCode::InvalidGeometry, handle_text(id), path, "an edge on an ellipse covers at most one turn");
        return;
    }

    // Where the curve ends are: the vertex at the lower parameter and at the higher
    const VertexId& at_low = edge.against_curve ? edge.end : edge.start;
    const VertexId& at_high = edge.against_curve ? edge.start : edge.end;
    bool good = true;
    struct EndCheck {
        double t;
        const VertexId* vertex;
        const char* name;
    };
    const EndCheck ends[2] = {{t0, &at_low, "start"}, {t1, &at_high, "end"}};
    for (const EndCheck& e : ends) {
        double gap = distance(curve_point(curve, e.t), d_.vertices[e.vertex->index].position);
        if (gap > d_.options.tolerance.distance) {
            report(ErrorCode::CurveEndpointMismatch, handle_text(id), path + "/" + e.name,
                   "the distance between the curve at parameter " + real_text(e.t) + " and " + handle_text(*e.vertex) +
                       " is " + real_text(gap) + " mm; the tolerance is " + tolerance_text() + " mm");
            good = false;
        }
    }
    if (good) edges_good_.insert(edge_index);
}

void Validator::check_uses() {
    for (std::uint32_t index : edge_order_) {
        const EdgeUse& use = uses_[index];
        const EdgeRecord& edge = d_.edges[index];
        EdgeId id = d_.make_handle<HandleKind::Edge>(index);
        std::string path = use.shell_path + "/" + handle_text(id);
        size_t n = use.coedges.size();
        if (use.shells.size() > 1) {
            report(ErrorCode::LoopNotClosed, handle_text(id), path, "the edge is used in more than one shell");
            continue;
        }
        if (!edge.curve.valid()) {
            if (n != 1) {
                report(ErrorCode::EdgeUseCount, handle_text(id), path,
                       "a degenerate edge has exactly one coedge; found " + integer_text(static_cast<long long>(n)));
            }
            continue;
        }
        if (closed_kind(body_.kind)) {
            if (n != 2) {
                report(ErrorCode::EdgeUseCount, handle_text(id), path,
                       "a closed shell needs exactly two coedges on each edge; found " +
                           integer_text(static_cast<long long>(n)));
                continue;
            }
        } else if (n > 2) {
            report(ErrorCode::EdgeUseCount, handle_text(id), path,
                   "an edge has at most two coedges; found " + integer_text(static_cast<long long>(n)));
            continue;
        }
        if (n == 2) {
            const CoedgeRecord& a = d_.coedges[use.coedges[0]];
            const CoedgeRecord& b = d_.coedges[use.coedges[1]];
            if (a.reversed == b.reversed) {
                report(ErrorCode::OrientationConflict, handle_text(id), path,
                       "the two uses run in the same direction, " + handle_text(CoedgeId{d_.id, use.coedges[0]}) +
                           " and " + handle_text(CoedgeId{d_.id, use.coedges[1]}) +
                           ": neighbouring faces induce opposite directions");
            }
        }
    }
}

void Validator::check_partners() {
    for (std::uint32_t index : edge_order_) {
        const EdgeUse& use = uses_[index];
        if (use.shells.size() > 1) continue;
        for (std::uint32_t ci : use.coedges) {
            const CoedgeRecord& c = d_.coedges[ci];
            if (!c.partner.valid()) continue;
            CoedgeId cid = d_.make_handle<HandleKind::Coedge>(ci);
            std::string path = use.shell_path + "/" + handle_text(cid);
            const CoedgeRecord& p = d_.coedges[c.partner.index];
            if (!(p.edge == c.edge)) {
                report(ErrorCode::PartnerMismatch, handle_text(cid), path,
                       "the partner " + handle_text(c.partner) + " uses " + handle_text(p.edge) + ", not " + handle_text(c.edge));
            } else if (!(p.partner == cid)) {
                report(ErrorCode::PartnerMismatch, handle_text(cid), path,
                       "the partner " + handle_text(c.partner) + " points back at " +
                           (p.partner.valid() ? handle_text(p.partner) : std::string("no coedge")));
            } else if (use.coedges.size() != 2) {
                report(ErrorCode::PartnerMismatch, handle_text(cid), path, "an edge with one use has no partner");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Geometry of the uses
// ---------------------------------------------------------------------------

// The points along a coedge in its direction, both ends included
std::vector<Point3> Validator::coedge_points(const CoedgeRecord& coedge, int samples) const {
    const EdgeRecord& edge = d_.edges[coedge.edge.index];
    const CurveGeometry& curve = d_.curves[edge.curve.index].geometry;
    bool increasing = coedge_increasing(coedge, edge);
    std::vector<Point3> points;
    int n = std::holds_alternative<LineGeometry>(curve) ? 1 : samples;
    for (int i = 0; i <= n; ++i) {
        double s = static_cast<double>(i) / static_cast<double>(n);
        double t = increasing ? edge.start_parameter + (edge.end_parameter - edge.start_parameter) * s
                              : edge.end_parameter - (edge.end_parameter - edge.start_parameter) * s;
        points.push_back(curve_point(curve, t));
    }
    return points;
}

void Validator::check_coedge_geometry() {
    for (const CoedgeContext& context : coedges_) {
        const CoedgeRecord& c = d_.coedges[context.coedge];
        const EdgeRecord& edge = d_.edges[c.edge.index];
        if (!edge.curve.valid() || edges_good_.count(c.edge.index) == 0) continue;
        const FaceRecord& face = d_.faces[context.face];
        if (surfaces_bad_.count(face.surface.index)) continue;
        const SurfaceGeometry& surface = d_.surfaces[face.surface.index].geometry;
        CoedgeId cid = d_.make_handle<HandleKind::Coedge>(context.coedge);
        double off = 0.0;
        if (analytic_distance(surface, d_.vertices[edge.start.index].position, off)) {
            std::vector<Point3> points = coedge_points(c, 8);
            double worst = 0.0;
            for (const Point3& p : points) {
                analytic_distance(surface, p, off);
                worst = std::max(worst, off);
            }
            if (worst > d_.options.tolerance.distance) {
                report(ErrorCode::SurfaceMismatch, handle_text(cid), context.path,
                       "the edge's curve is up to " + real_text(worst) + " mm from the surface of " +
                           handle_text(FaceId{d_.id, context.face}) + "; the tolerance is " + tolerance_text() + " mm");
            }
        }
        check_coedge_pcurve(context);
    }
}

void Validator::check_coedge_pcurve(const CoedgeContext& context) {
    const CoedgeRecord& c = d_.coedges[context.coedge];
    const EdgeRecord& edge = d_.edges[c.edge.index];
    const FaceRecord& face = d_.faces[context.face];
    const SurfaceGeometry& surface = d_.surfaces[face.surface.index].geometry;
    CoedgeId cid = d_.make_handle<HandleKind::Coedge>(context.coedge);
    bool spline = std::holds_alternative<BSplineSurfaceGeometry>(surface);

    if (!c.pcurve.valid()) {
        if (spline) {
            report(ErrorCode::PcurveRequired, handle_text(cid), context.path,
                   "the trimming cannot be determined on a B-spline surface without the coedge's pcurve");
        }
        return;
    }
    if (!pcurves_used_.insert(c.pcurve.index).second) {
        report(ErrorCode::PcurveMismatch, handle_text(c.pcurve), context.path,
               handle_text(c.pcurve) + " is used by more than one coedge; each coedge has its own");
        return;
    }
    const PcurveGeometry& pcurve = d_.pcurves[c.pcurve.index].geometry;
    std::string why = check_pcurve(pcurve, d_.options.tolerance);
    if (!why.empty()) {
        report(ErrorCode::InvalidBSpline, handle_text(c.pcurve), context.path, why);
        return;
    }
    double low = 0.0;
    double high = 0.0;
    spline_domain(pcurve.degree, pcurve.knots, static_cast<int>(pcurve.points.size()), low, high);
    double slack = d_.options.tolerance.parameter;
    if (edge.start_parameter < low - slack || edge.end_parameter > high + slack) {
        report(ErrorCode::PcurveMismatch, handle_text(c.pcurve), context.path,
               "the pcurve's parameters [" + real_text(low) + ", " + real_text(high) + "] do not cover the edge's [" +
                   real_text(edge.start_parameter) + ", " + real_text(edge.end_parameter) + "]");
        return;
    }
    const CurveGeometry& curve = d_.curves[edge.curve.index].geometry;
    double worst = 0.0;
    const int samples = 16;
    for (int i = 0; i <= samples; ++i) {
        double t = edge.start_parameter + (edge.end_parameter - edge.start_parameter) * i / samples;
        hdxf::Point2 uv = bspline_curve2d_point(pcurve, t);
        Point3 on_surface = surface_point(surface, uv.x, uv.y);
        worst = std::max(worst, distance(on_surface, curve_point(curve, t)));
    }
    if (worst > d_.options.tolerance.distance) {
        report(ErrorCode::PcurveMismatch, handle_text(c.pcurve), context.path,
               "the pcurve is up to " + real_text(worst) + " mm from the edge's curve on the surface; the tolerance is " +
                   tolerance_text() + " mm");
    }
}

void Validator::check_loop_orientation(LoopId id, const std::string& path) {
    const LoopRecord& loop = d_.loops[id.index];
    const FaceRecord& face = d_.faces[loop.face.index];
    const PlaneGeometry& plane = std::get<PlaneGeometry>(d_.surfaces[face.surface.index].geometry);
    if (surfaces_bad_.count(face.surface.index)) return;
    std::vector<Point3> polygon;
    for (const CoedgeId& cid : loop.coedges) {
        const CoedgeRecord& c = d_.coedges[cid.index];
        const EdgeRecord& e = d_.edges[c.edge.index];
        if (!e.curve.valid()) continue;
        if (edges_good_.count(c.edge.index) == 0) return;
        std::vector<Point3> points = coedge_points(c, kEllipseSamples);
        polygon.insert(polygon.end(), points.begin(), points.end() - 1);
    }
    if (polygon.size() < 3) return;
    // The area vector about the first point
    Vector3 area{0.0, 0.0, 0.0};
    for (size_t i = 1; i + 1 < polygon.size(); ++i) {
        area = add(area, cross(sub(polygon[i], polygon[0]), sub(polygon[i + 1], polygon[0])));
    }
    Vector3 normal = face.reversed ? scaled(plane.normal, -1.0) : plane.normal;
    double along = 0.5 * dot(area, normal);
    LoopRole role = loop.role;
    if (std::fabs(along) < 1e-12) {
        report(ErrorCode::LoopRole, handle_text(id), path, "the loop encloses no area");
        return;
    }
    if (role == LoopRole::Outer && along < 0.0) {
        report(ErrorCode::OrientationConflict, handle_text(id), path,
               "an outer loop runs counter-clockwise about the face's normal; this runs clockwise");
    } else if (role == LoopRole::Inner && along > 0.0) {
        report(ErrorCode::OrientationConflict, handle_text(id), path,
               "an inner loop runs clockwise about the face's normal; this runs counter-clockwise");
    }
}

std::vector<Diagnostic> Validator::run() {
    check_lumps();
    check_edges();
    check_uses();
    check_partners();
    check_coedge_geometry();
    for (const auto& [loop, path] : planar_loops_) check_loop_orientation(loop, path);
    return errors_;
}

} // namespace

std::vector<Diagnostic> validate_body(const BuilderData& data, BodyId body) {
    Validator validator(data, body);
    return validator.run();
}

} // namespace hdxf::acis::detail
