#include "acis.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// The exact box of ACIS bodies (rättningsfas R.9)
//
// A body's box is its edges' box together with the points inside its faces where a
// surface reaches further than the face's edges. Edges: lines by their ends, ellipses by
// their extremes, splines exactly (nurbs_extents, R.6); an intersection curve, whose
// spline only approximates it within its fit tolerance, at its exact points (Newton on
// its two surfaces). Faces: a coordinate on a plane or along a cone's straight lines has
// no extreme inside; a sphere's are c ± r·ŵ, a torus' where its normal is ±ŵ (circles
// when ŵ is its axis), a cone's its apex, a spline surface's by branch and bound on its
// Bézier patches (the convex hull bounds a patch) and Newton, and on its knot lines; a
// surface of revolution's from its profile (a polynomial's roots per Bézier span), a sum
// of curves' from both curves' extremes. Each such point counts when it lies in the face:
// the face's loops in the surface's parameters (the coedges sampled and mapped), the face
// on the left of its coedges seen from its outside, and a point inside when a path to it
// from just inside a loop crosses the loops an even number of times; a face bounded by
// seams only, or without loops, is all of its surface. Everything in the body's
// coordinates; a world coordinate is a linear function there (w · p + t), so any affine
// map is exact.
// ---------------------------------------------------------------------------

namespace hdxf {
namespace internal {

namespace {

const double kPi = M_PI;
const double kTwoPi = 2.0 * M_PI;

Point3 add(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point3 mul(const Point3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double norm(const Point3& a) { return std::sqrt(dot(a, a)); }
Point3 normalized(const Point3& a) {
    double l = norm(a);
    return l > 0.0 ? mul(a, 1.0 / l) : a;
}
// A unit vector perpendicular to a unit vector
Point3 perpendicular(const Point3& n) {
    Point3 t = std::fabs(n.x) < 0.6 ? Point3{1.0, 0.0, 0.0} : Point3{0.0, 1.0, 0.0};
    return normalized(sub(t, mul(n, dot(t, n))));
}

// The affine map body → WCS
struct Affine {
    double m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    Point3 apply(const Point3& p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
    }
    Point3 linear(const Point3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    // The vector w with WCS coordinate i = w · p + m[i][3]
    Point3 row(int i) const { return {m[i][0], m[i][1], m[i][2]}; }
};

// a ∘ b
Affine compose(const double a[3][4], const double b[3][4]) {
    Affine r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            double s = j == 3 ? a[i][3] : 0.0;
            for (int k = 0; k < 3; ++k) s += a[i][k] * b[k][j];
            r.m[i][j] = s;
        }
    }
    return r;
}

double wrap_angle(double a) {
    a = std::fmod(a, kTwoPi);
    return a < 0.0 ? a + kTwoPi : a;
}

// ---------------------------------------------------------------------------
// B-splines: basis functions and their derivatives
// ---------------------------------------------------------------------------

// The span k (knots[k] <= t < knots[k+1], within [knots[p], knots[n]]) of a knot vector
// for n control points
size_t find_span(const std::vector<double>& knots, int p, size_t n, double t) {
    if (t >= knots[n]) {
        size_t k = n - 1;
        while (k > static_cast<size_t>(p) && knots[k] >= knots[k + 1]) --k;
        return k;
    }
    if (t <= knots[static_cast<size_t>(p)]) {
        size_t k = static_cast<size_t>(p);
        while (k + 1 < n && knots[k + 1] <= t) ++k;
        return k;
    }
    size_t lo = static_cast<size_t>(p);
    size_t hi = n;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (knots[mid] <= t) lo = mid;
        else hi = mid;
    }
    return lo;
}

// N[d][j]: the d-th derivative (d ≤ 2) of the basis function span - p + j at t. Built from
// the Cox–de Boor recursion: the functions of every degree up to p, then the derivatives
// as differences of the degree p - d functions.
// The degrees are at most 32 (the reader takes no more): fixed arrays, no allocation.
const int kMaxDegree = 32;

void basis(const std::vector<double>& knots, int p, size_t span, double t, double out[kMaxDegree + 1][3]) {
    // table[r][j]: the degree-r basis functions that are not 0 on the span
    double table[kMaxDegree + 1][kMaxDegree + 1];
    table[0][0] = 1.0;
    for (int r = 1; r <= p; ++r) {
        for (int j = 0; j <= r; ++j) {
            // N(i, r) = (t - u_i)/(u_{i+r} - u_i) N(i, r-1) + (u_{i+r+1} - t)/(u_{i+r+1} - u_{i+1}) N(i+1, r-1)
            size_t i = span - r + j;
            double v = 0.0;
            if (j >= 1) {
                double d = knots[i + r] - knots[i];
                if (d > 0.0) v += (t - knots[i]) / d * table[r - 1][j - 1];
            }
            if (j <= r - 1) {
                double d = knots[i + r + 1] - knots[i + 1];
                if (d > 0.0) v += (knots[i + r + 1] - t) / d * table[r - 1][j];
            }
            table[r][j] = v;
        }
    }
    for (int j = 0; j <= p; ++j) {
        out[j][0] = table[p][j];
        out[j][1] = 0.0;
        out[j][2] = 0.0;
    }
    // N'(i, p) = p/(u_{i+p} - u_i) N(i, p-1) - p/(u_{i+p+1} - u_{i+1}) N(i+1, p-1), and the
    // second derivative the same rule applied twice
    auto lower = [&](int r, int j) {   // degree r function i = span - p + j, 0 if not on the span
        int k = j - (p - r);
        if (r < 0 || k < 0 || k > r) return 0.0;
        return table[r][k];
    };
    for (int j = 0; j <= p && p >= 1; ++j) {
        size_t i = span - p + j;
        double a = knots[i + p] - knots[i];
        double b = knots[i + p + 1] - knots[i + 1];
        double d1 = 0.0;
        if (a > 0.0) d1 += p / a * lower(p - 1, j);
        if (b > 0.0) d1 -= p / b * lower(p - 1, j + 1);
        out[j][1] = d1;
        if (p >= 2) {
            // N''(i,p) = p/a N'(i,p-1) - p/b N'(i+1,p-1), N'(i,p-1) by the same rule
            auto first = [&](size_t ii, int jj) {
                double aa = knots[ii + p - 1] - knots[ii];
                double bb = knots[ii + p] - knots[ii + 1];
                double v = 0.0;
                if (aa > 0.0) v += (p - 1) / aa * lower(p - 2, jj);
                if (bb > 0.0) v -= (p - 1) / bb * lower(p - 2, jj + 1);
                return v;
            };
            double d2 = 0.0;
            if (a > 0.0) d2 += p / a * first(i, j);
            if (b > 0.0) d2 -= p / b * first(i + 1, j + 1);
            out[j][2] = d2;
        }
    }
}

// A spline curve's point and first two derivatives at t
void curve_eval(const AcisSplineCurve& c, double t, Point3& p, Point3* d1 = nullptr, Point3* d2 = nullptr) {
    size_t n = c.points.size();
    size_t span = find_span(c.knots, c.degree, n, t);
    double N[kMaxDegree + 1][3];
    basis(c.knots, c.degree, span, t, N);
    Point3 A[3] = {};
    double W[3] = {0.0, 0.0, 0.0};
    for (int j = 0; j <= c.degree; ++j) {
        size_t i = span - c.degree + j;
        double w = i < c.weights.size() ? c.weights[i] : 1.0;
        for (int d = 0; d < 3; ++d) {
            A[d] = add(A[d], mul(c.points[i], N[j][d] * w));
            W[d] += N[j][d] * w;
        }
    }
    double w0 = W[0] != 0.0 ? W[0] : 1.0;
    p = mul(A[0], 1.0 / w0);
    Point3 q1 = mul(sub(A[1], mul(p, W[1])), 1.0 / w0);
    if (d1) *d1 = q1;
    if (d2) *d2 = mul(sub(sub(A[2], mul(q1, 2.0 * W[1])), mul(p, W[2])), 1.0 / w0);
}

double curve_lo(const AcisSplineCurve& c) { return c.knots[static_cast<size_t>(c.degree)]; }
double curve_hi(const AcisSplineCurve& c) { return c.knots[c.points.size()]; }

// The parameter of the point of a spline curve nearest to p: the best of samples on each
// span, then Newton on (C - p) · C' = 0
double curve_param(const AcisSplineCurve& c, const Point3& p) {
    double lo = curve_lo(c);
    double hi = curve_hi(c);
    double best_t = lo;
    double best_d = std::numeric_limits<double>::infinity();
    size_t n = c.points.size();
    for (size_t k = static_cast<size_t>(c.degree); k < n; ++k) {
        double a = c.knots[k];
        double b = c.knots[k + 1];
        if (b <= a) continue;
        for (int i = 0; i <= 16; ++i) {
            double t = a + (b - a) * i / 16.0;
            Point3 q;
            curve_eval(c, t, q);
            double d = dot(sub(q, p), sub(q, p));
            if (d < best_d) {
                best_d = d;
                best_t = t;
            }
        }
    }
    double t = best_t;
    for (int it = 0; it < 50; ++it) {
        Point3 q, d1, d2;
        curve_eval(c, t, q, &d1, &d2);
        Point3 r = sub(q, p);
        double f = dot(r, d1);
        double g = dot(d1, d1) + dot(r, d2);
        if (!(g > 0.0)) break;
        double step = f / g;
        double nt = t - step;
        if (c.periodic) {
            double len = hi - lo;
            while (nt < lo) nt += len;
            while (nt > hi) nt -= len;
        } else {
            nt = std::clamp(nt, lo, hi);
        }
        bool done = std::fabs(nt - t) <= 1e-15 * (std::fabs(hi) + std::fabs(lo) + 1.0);
        t = nt;
        if (done) break;
    }
    return t;
}

// A spline surface's point and derivatives up to the second at (u, v):
// d[0] S, d[1] Su, d[2] Sv, d[3] Suu, d[4] Suv, d[5] Svv
void surface_eval(const AcisSplineSurface& s, double u, double v, Point3 d[6]) {
    size_t nu = static_cast<size_t>(s.count_u);
    size_t nv = static_cast<size_t>(s.count_v);
    size_t su = find_span(s.knots_u, s.degree_u, nu, u);
    size_t sv = find_span(s.knots_v, s.degree_v, nv, v);
    double Nu[kMaxDegree + 1][3];
    double Nv[kMaxDegree + 1][3];
    basis(s.knots_u, s.degree_u, su, u, Nu);
    basis(s.knots_v, s.degree_v, sv, v, Nv);
    // A and W with their derivatives: index (du, dv) → 0 (0,0) 1 (1,0) 2 (0,1) 3 (2,0) 4 (1,1) 5 (0,2)
    static const int du_of[6] = {0, 1, 0, 2, 1, 0};
    static const int dv_of[6] = {0, 0, 1, 0, 1, 2};
    Point3 A[6] = {};
    double W[6] = {0, 0, 0, 0, 0, 0};
    for (int b = 0; b <= s.degree_v; ++b) {
        size_t j = sv - s.degree_v + b;
        for (int a = 0; a <= s.degree_u; ++a) {
            size_t i = su - s.degree_u + a;
            size_t idx = j * nu + i;
            double w = idx < s.weights.size() ? s.weights[idx] : 1.0;
            for (int k = 0; k < 6; ++k) {
                double f = Nu[a][du_of[k]] * Nv[b][dv_of[k]] * w;
                A[k] = add(A[k], mul(s.points[idx], f));
                W[k] += f;
            }
        }
    }
    double w0 = W[0] != 0.0 ? W[0] : 1.0;
    Point3 S = mul(A[0], 1.0 / w0);
    Point3 Su = mul(sub(A[1], mul(S, W[1])), 1.0 / w0);
    Point3 Sv = mul(sub(A[2], mul(S, W[2])), 1.0 / w0);
    d[0] = S;
    d[1] = Su;
    d[2] = Sv;
    d[3] = mul(sub(sub(A[3], mul(Su, 2.0 * W[1])), mul(S, W[3])), 1.0 / w0);
    d[4] = mul(sub(sub(sub(A[4], mul(Su, W[2])), mul(Sv, W[1])), mul(S, W[4])), 1.0 / w0);
    d[5] = mul(sub(sub(A[5], mul(Sv, 2.0 * W[2])), mul(S, W[5])), 1.0 / w0);
}

// ---------------------------------------------------------------------------
// The curves of a procedural surface: a straight, an ellipse or an intcurve
// ---------------------------------------------------------------------------

// A curve's point and first two derivatives at its own parameter t: a straight's root +
// t · direction, an ellipse's center + M cos t + N sin t (N the minor axis), an intcurve's
// spline at t (at -t when it is reversed; a closed spline's parameter taken round)
void acis_curve_point(const AcisCurve& c, double t, Point3& p, Point3* d1 = nullptr, Point3* d2 = nullptr) {
    switch (c.type) {
        case AcisCurveType::Straight:
            p = add(c.origin, mul(c.direction, t));
            if (d1) *d1 = c.direction;
            if (d2) *d2 = {0.0, 0.0, 0.0};
            return;
        case AcisCurveType::Ellipse: {
            Point3 minor = mul(cross(c.axis, c.major), c.ratio);
            double co = std::cos(t), si = std::sin(t);
            p = add(add(c.origin, mul(c.major, co)), mul(minor, si));
            if (d1) *d1 = add(mul(c.major, -si), mul(minor, co));
            if (d2) *d2 = add(mul(c.major, -co), mul(minor, -si));
            return;
        }
        case AcisCurveType::Spline: {
            const AcisSplineCurve& s = c.spline;
            double lo = curve_lo(s), hi = curve_hi(s);
            double x = c.reversed ? -t : t;
            if (s.periodic && hi > lo) {
                x = lo + std::fmod(x - lo, hi - lo);
                if (x < lo) x += hi - lo;
            }
            curve_eval(s, std::clamp(x, lo, hi), p, d1, d2);
            if (c.reversed && d1) *d1 = mul(*d1, -1.0);
            return;
        }
        default:
            p = c.origin;
            if (d1) *d1 = {0.0, 0.0, 0.0};
            if (d2) *d2 = {0.0, 0.0, 0.0};
            return;
    }
}

// A curve's parameter range: its saved range, else an intcurve's spline (negated when it
// is reversed) or an ellipse's whole turn; a straight without a range has none. period: the
// length of a closed curve's turn (0 open).
bool acis_curve_range(const AcisCurve& c, double& lo, double& hi, double& period) {
    period = 0.0;
    if (c.type == AcisCurveType::Ellipse) period = kTwoPi;
    if (c.type == AcisCurveType::Spline && c.spline.periodic) period = curve_hi(c.spline) - curve_lo(c.spline);
    if (c.bounded && c.t1 > c.t0) {
        lo = c.t0;
        hi = c.t1;
        return true;
    }
    switch (c.type) {
        case AcisCurveType::Ellipse:
            lo = 0.0;
            hi = kTwoPi;
            return true;
        case AcisCurveType::Spline:
            lo = c.reversed ? -curve_hi(c.spline) : curve_lo(c.spline);
            hi = c.reversed ? -curve_lo(c.spline) : curve_hi(c.spline);
            return hi > lo;
        default:
            return false;
    }
}

// The rotation by angle a about the unit axis d (the right hand), of a vector
Point3 rotated(const Point3& d, double a, const Point3& x) {
    Point3 along = mul(d, dot(d, x));
    Point3 across = sub(x, along);
    return add(add(along, mul(across, std::cos(a))), mul(cross(d, across), std::sin(a)));
}

// ---------------------------------------------------------------------------
// A surface in parameters of its own: what the face's loops are drawn in
// ---------------------------------------------------------------------------

// Sphere: u the longitude around a pole chosen away from the coordinate directions (so
// no extreme is at a pole), v the latitude. Cone: u the angle of the base ellipse, v the
// height along the axis. Torus: u around the axis, v around the tube. Spline: its own
// parameters. Revolution: u the angle, v the profile's parameter (ACIS's v and u: u is
// the angle here as on a sphere, undefined where the profile meets the axis). Sum: the
// curves' parameters. Each knows its natural normal (ACIS's): outward for a positive
// sphere radius and torus minor radius, away from the axis for a cone with positive
// cosine, Su × Sv in ACIS's parameters for a spline, a revolution and a sum unless
// reversed.
class SurfaceParam {
public:
    SurfaceParam(const AcisSurface& s, const Point3 directions[3]) : s_(s) {
        switch (s.type) {
            case AcisSurfaceType::Sphere: {
                Point3 z = add(add(normalized(directions[0]), mul(normalized(directions[1]), 2.0)),
                               mul(normalized(directions[2]), 3.0));
                z_ = norm(z) > 0.0 ? normalized(z) : s.axis;
                x_ = perpendicular(z_);
                y_ = cross(z_, x_);
                radius_ = std::fabs(s.radius);
                period_u_ = kTwoPi;
                valid_ = radius_ > 0.0;
                break;
            }
            case AcisSurfaceType::Torus: {
                z_ = s.axis;
                Point3 x = sub(s.major, mul(z_, dot(s.major, z_)));
                x_ = norm(x) > 0.0 ? normalized(x) : perpendicular(z_);
                y_ = cross(z_, x_);
                radius_ = std::fabs(s.minor);
                period_u_ = kTwoPi;
                period_v_ = kTwoPi;
                valid_ = radius_ > 0.0;
                break;
            }
            case AcisSurfaceType::Cone: {
                z_ = s.axis;
                x_ = s.major;                               // the major axis
                y_ = mul(cross(z_, s.major), s.ratio);      // the minor axis
                double a = norm(x_);
                k_ = a > 0.0 && s.cosine != 0.0 ? s.sine / s.cosine / a : 0.0;
                period_u_ = kTwoPi;
                valid_ = a > 0.0 && norm(y_) > 0.0 && std::fabs(s.cosine) > 1e-12;
                break;
            }
            case AcisSurfaceType::Spline: {
                const AcisSplineSurface& sp = s.spline;
                if (sp.points.empty()) {
                    valid_ = false;
                    break;
                }
                u0_ = sp.knots_u[static_cast<size_t>(sp.degree_u)];
                u1_ = sp.knots_u[static_cast<size_t>(sp.count_u)];
                v0_ = sp.knots_v[static_cast<size_t>(sp.degree_v)];
                v1_ = sp.knots_v[static_cast<size_t>(sp.count_v)];
                if (sp.periodic_u) period_u_ = u1_ - u0_;
                if (sp.periodic_v) period_v_ = v1_ - v0_;
                valid_ = u1_ > u0_ && v1_ > v0_;
                break;
            }
            case AcisSurfaceType::Revolution: {
                if (s.curves.empty()) {
                    valid_ = false;
                    break;
                }
                z_ = s.axis;
                x_ = perpendicular(z_);
                y_ = cross(z_, x_);
                u0_ = 0.0;
                u1_ = kTwoPi;
                period_u_ = kTwoPi;
                bool ranged = acis_curve_range(s.curves[0], v0_, v1_, period_v_);
                if (s.has_range) {
                    v0_ = s.u0;
                    v1_ = s.u1;
                    ranged = true;
                }
                valid_ = ranged && v1_ > v0_;
                break;
            }
            case AcisSurfaceType::Sum: {
                if (s.curves.size() < 2) {
                    valid_ = false;
                    break;
                }
                bool ranged = acis_curve_range(s.curves[0], u0_, u1_, period_u_) &&
                              acis_curve_range(s.curves[1], v0_, v1_, period_v_);
                if (s.has_range) {
                    u0_ = s.u0;
                    u1_ = s.u1;
                    v0_ = s.v0;
                    v1_ = s.v1;
                    ranged = true;
                }
                valid_ = ranged && u1_ > u0_ && v1_ > v0_;
                break;
            }
            default:
                valid_ = false;
                break;
        }
    }

    bool valid() const { return valid_; }
    double period_u() const { return period_u_; }
    double period_v() const { return period_v_; }
    // The cone's apex height (v), if it has one
    bool apex(double& v) const {
        if (s_.type != AcisSurfaceType::Cone || k_ == 0.0) return false;
        v = -1.0 / k_;
        return true;
    }

    Point3 point(double u, double v) const {
        switch (s_.type) {
            case AcisSurfaceType::Sphere:
                return add(s_.origin, mul(add(mul(add(mul(x_, std::cos(u)), mul(y_, std::sin(u))), std::cos(v)),
                                              mul(z_, std::sin(v))), radius_));
            case AcisSurfaceType::Torus: {
                double rho = s_.radius + radius_ * std::cos(v);
                return add(add(s_.origin, mul(add(mul(x_, std::cos(u)), mul(y_, std::sin(u))), rho)),
                           mul(z_, radius_ * std::sin(v)));
            }
            case AcisSurfaceType::Cone: {
                double f = 1.0 + v * k_;
                return add(add(s_.origin, mul(z_, v)), mul(add(mul(x_, std::cos(u)), mul(y_, std::sin(u))), f));
            }
            case AcisSurfaceType::Spline:
            case AcisSurfaceType::Revolution:
            case AcisSurfaceType::Sum: {
                Point3 d[6];
                derivatives(u, v, d);
                return d[0];
            }
            default:
                return s_.origin;
        }
    }

    // A spline's, a revolution's or a sum's point and derivatives up to the second, in
    // these parameters: d[0] S, d[1] Su, d[2] Sv, d[3] Suu, d[4] Suv, d[5] Svv
    void derivatives(double u, double v, Point3 d[6]) const {
        switch (s_.type) {
            case AcisSurfaceType::Spline:
                surface_eval(s_.spline, clamp_u(u), clamp_v(v), d);
                return;
            case AcisSurfaceType::Revolution: {
                // S = A + R(u)(C(v) - A): Su = z × R r, Suu = z × (z × R r), Sv = R C',
                // Suv = z × R C', Svv = R C''
                Point3 c, c1, c2;
                acis_curve_point(s_.curves[0], v, c, &c1, &c2);
                Point3 r = rotated(z_, u, sub(c, s_.origin));
                Point3 r1 = rotated(z_, u, c1);
                d[0] = add(s_.origin, r);
                d[1] = cross(z_, r);
                d[2] = r1;
                d[3] = cross(z_, d[1]);
                d[4] = cross(z_, r1);
                d[5] = rotated(z_, u, c2);
                return;
            }
            case AcisSurfaceType::Sum: {
                Point3 a, a1, a2, b, b1, b2;
                acis_curve_point(s_.curves[0], u, a, &a1, &a2);
                acis_curve_point(s_.curves[1], v, b, &b1, &b2);
                d[0] = sub(add(a, b), s_.origin);
                d[1] = a1;
                d[2] = b1;
                d[3] = a2;
                d[4] = {0.0, 0.0, 0.0};
                d[5] = b2;
                return;
            }
            default:
                for (int i = 0; i < 6; ++i) d[i] = {0.0, 0.0, 0.0};
                d[0] = point(u, v);
                return;
        }
    }

    void partials(double u, double v, Point3& su, Point3& sv) const {
        switch (s_.type) {
            case AcisSurfaceType::Sphere: {
                Point3 e = add(mul(x_, std::cos(u)), mul(y_, std::sin(u)));
                Point3 de = add(mul(x_, -std::sin(u)), mul(y_, std::cos(u)));
                su = mul(de, radius_ * std::cos(v));
                sv = mul(sub(mul(z_, std::cos(v)), mul(e, std::sin(v))), radius_);
                return;
            }
            case AcisSurfaceType::Torus: {
                Point3 e = add(mul(x_, std::cos(u)), mul(y_, std::sin(u)));
                Point3 de = add(mul(x_, -std::sin(u)), mul(y_, std::cos(u)));
                su = mul(de, s_.radius + radius_ * std::cos(v));
                sv = add(mul(e, -radius_ * std::sin(v)), mul(z_, radius_ * std::cos(v)));
                return;
            }
            case AcisSurfaceType::Cone: {
                Point3 e = add(mul(x_, std::cos(u)), mul(y_, std::sin(u)));
                Point3 de = add(mul(x_, -std::sin(u)), mul(y_, std::cos(u)));
                su = mul(de, 1.0 + v * k_);
                sv = add(z_, mul(e, k_));
                return;
            }
            case AcisSurfaceType::Spline:
            case AcisSurfaceType::Revolution:
            case AcisSurfaceType::Sum: {
                Point3 d[6];
                derivatives(u, v, d);
                su = d[1];
                sv = d[2];
                return;
            }
            default:
                su = sv = {0.0, 0.0, 0.0};
                return;
        }
    }

    // ACIS's normal at (u, v), not of unit length
    Point3 normal(double u, double v) const {
        Point3 su, sv;
        partials(u, v, su, sv);
        Point3 x = cross(su, sv);
        switch (s_.type) {
            case AcisSurfaceType::Sphere: {
                Point3 out = sub(point(u, v), s_.origin);
                return s_.radius < 0.0 ? mul(out, -1.0) : out;
            }
            case AcisSurfaceType::Torus: {
                Point3 e = add(mul(x_, std::cos(u)), mul(y_, std::sin(u)));
                Point3 out = sub(point(u, v), add(s_.origin, mul(e, s_.radius)));
                return s_.minor < 0.0 ? mul(out, -1.0) : out;
            }
            case AcisSurfaceType::Cone: {
                // Away from the axis (on the base's side of the apex), times the
                // cosine's sign
                Point3 radial = sub(point(u, v), add(s_.origin, mul(z_, v)));
                if (dot(x, radial) < 0.0) x = mul(x, -1.0);
                double f = 1.0 + v * k_;
                double sign = (s_.cosine < 0.0 ? -1.0 : 1.0) * (f < 0.0 ? -1.0 : 1.0);
                return mul(x, sign);
            }
            case AcisSurfaceType::Spline:
            case AcisSurfaceType::Sum:
                return s_.reversed ? mul(x, -1.0) : x;
            case AcisSurfaceType::Revolution:
                // ACIS's u and v are this v and u
                return s_.reversed ? x : mul(x, -1.0);
            default:
                return x;
        }
    }

    // The parameters of a point on (or near) the surface. singular: at a pole or apex,
    // where u is undefined. With hint, u and v hold a guess (a spline's Newton starts
    // there).
    bool param(const Point3& p, double& u, double& v, bool& singular, bool hint) const {
        singular = false;
        Point3 d = sub(p, s_.origin);
        switch (s_.type) {
            case AcisSurfaceType::Sphere: {
                double x = dot(d, x_);
                double y = dot(d, y_);
                double h = std::sqrt(x * x + y * y);
                v = std::atan2(dot(d, z_), h);
                singular = h <= 1e-12 * radius_;
                u = singular ? 0.0 : std::atan2(y, x);
                return true;
            }
            case AcisSurfaceType::Torus: {
                double x = dot(d, x_);
                double y = dot(d, y_);
                double rho = std::sqrt(x * x + y * y);
                singular = rho <= 1e-12 * (std::fabs(s_.radius) + radius_);
                u = singular ? 0.0 : std::atan2(y, x);
                v = std::atan2(dot(d, z_), rho - s_.radius);
                return true;
            }
            case AcisSurfaceType::Cone: {
                v = dot(d, z_);
                Point3 q = sub(d, mul(z_, v));
                double f = 1.0 + v * k_;
                double scale = norm(x_);
                singular = std::fabs(f) * scale <= 1e-12 * (scale + std::fabs(v));
                if (singular) {
                    u = 0.0;
                    return true;
                }
                q = mul(q, 1.0 / f);
                u = std::atan2(dot(q, y_) / dot(y_, y_), dot(q, x_) / dot(x_, x_));
                return true;
            }
            case AcisSurfaceType::Spline:
                if (!hint) spline_guess(p, u, v);
                return newton_param(p, u, v);
            case AcisSurfaceType::Revolution: {
                // The angle from the point's direction off the axis; the profile's
                // parameter where its distance from the axis and its height are nearest
                double h = dot(d, z_);
                Point3 q = sub(d, mul(z_, h));
                double rho = norm(q);
                double size = norm(d) + std::fabs(v1_) + std::fabs(v0_);
                singular = rho <= 1e-12 * size;
                if (singular) {
                    // On the axis: the profile's nearest point, any angle
                    double angle = hint ? u : 0.0;
                    revolution_guess(h, rho, q, u, v);
                    u = angle;
                    const AcisCurve& c = s_.curves[0];
                    for (int it = 0; it < 50; ++it) {
                        Point3 cp, c1, c2;
                        acis_curve_point(c, v, cp, &c1, &c2);
                        Point3 r = sub(cp, p);
                        double g = dot(c1, c1) + dot(r, c2);
                        if (!(g > 0.0)) break;
                        double nv = v - dot(r, c1) / g;
                        if (period_v_ <= 0.0) nv = std::clamp(nv, v0_, v1_);
                        bool done = std::fabs(nv - v) <= 1e-15 * (std::fabs(v1_) + std::fabs(v0_) + 1.0);
                        v = nv;
                        if (done) break;
                    }
                    return true;
                }
                if (!hint) revolution_guess(h, rho, q, u, v);
                return newton_param(p, u, v);
            }
            case AcisSurfaceType::Sum:
                if (!hint) grid_guess(p, u, v);
                return newton_param(p, u, v);
            default:
                return false;
        }
    }

    double u_min() const { return u0_; }
    double u_max() const { return u1_; }
    double v_min() const { return v0_; }
    double v_max() const { return v1_; }

private:
    double clamp_u(double u) const {
        if (period_u_ > 0.0) {
            u = u0_ + std::fmod(u - u0_, period_u_);
            if (u < u0_) u += period_u_;
        }
        return std::clamp(u, u0_, u1_);
    }
    double clamp_v(double v) const {
        if (period_v_ > 0.0) {
            v = v0_ + std::fmod(v - v0_, period_v_);
            if (v < v0_) v += period_v_;
        }
        return std::clamp(v, v0_, v1_);
    }

    // A spline's guess: the nearest of samples over every patch
    void spline_guess(const Point3& p, double& u, double& v) const {
        const AcisSplineSurface& sp = s_.spline;
        double best = std::numeric_limits<double>::infinity();
        for (int i = static_cast<int>(sp.degree_u); i < sp.count_u; ++i) {
            double a = sp.knots_u[static_cast<size_t>(i)];
            double b = sp.knots_u[static_cast<size_t>(i) + 1];
            if (b <= a) continue;
            for (int j = static_cast<int>(sp.degree_v); j < sp.count_v; ++j) {
                double c = sp.knots_v[static_cast<size_t>(j)];
                double e = sp.knots_v[static_cast<size_t>(j) + 1];
                if (e <= c) continue;
                for (int x = 0; x <= 6; ++x) {
                    for (int y = 0; y <= 6; ++y) {
                        double uu = a + (b - a) * x / 6.0;
                        double vv = c + (e - c) * y / 6.0;
                        Point3 q = point(uu, vv);
                        double dd = dot(sub(q, p), sub(q, p));
                        if (dd < best) {
                            best = dd;
                            u = uu;
                            v = vv;
                        }
                    }
                }
            }
        }
    }

    // The samples of a curve's range: more for a spline with more control points
    static int curve_samples(const AcisCurve& c) {
        if (c.type == AcisCurveType::Spline) return std::max(16, 4 * static_cast<int>(c.spline.points.size()));
        if (c.type == AcisCurveType::Ellipse) return 64;
        return 16;
    }

    // A revolution's guess: the profile's sample nearest in height and distance from the
    // axis, and the angle that turns it to the point
    void revolution_guess(double h, double rho, const Point3& q, double& u, double& v) const {
        const AcisCurve& c = s_.curves[0];
        int n = curve_samples(c);
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i <= n; ++i) {
            double t = v0_ + (v1_ - v0_) * i / n;
            Point3 cp;
            acis_curve_point(c, t, cp);
            Point3 r = sub(cp, s_.origin);
            double hr = dot(r, z_);
            double rr = norm(sub(r, mul(z_, hr)));
            double dd = (hr - h) * (hr - h) + (rr - rho) * (rr - rho);
            if (dd < best) {
                best = dd;
                v = t;
            }
        }
        Point3 cp;
        acis_curve_point(c, v, cp);
        Point3 r = sub(cp, s_.origin);
        Point3 rp = sub(r, mul(z_, dot(r, z_)));
        u = std::atan2(dot(q, y_), dot(q, x_)) - std::atan2(dot(rp, y_), dot(rp, x_));
    }

    // A sum's guess: the nearest of samples over both curves' ranges
    void grid_guess(const Point3& p, double& u, double& v) const {
        int nu = curve_samples(s_.curves[0]);
        int nv = curve_samples(s_.curves[1]);
        std::vector<Point3> a(static_cast<size_t>(nu) + 1), b(static_cast<size_t>(nv) + 1);
        for (int i = 0; i <= nu; ++i) acis_curve_point(s_.curves[0], u0_ + (u1_ - u0_) * i / nu, a[static_cast<size_t>(i)]);
        for (int j = 0; j <= nv; ++j) acis_curve_point(s_.curves[1], v0_ + (v1_ - v0_) * j / nv, b[static_cast<size_t>(j)]);
        double best = std::numeric_limits<double>::infinity();
        for (int i = 0; i <= nu; ++i) {
            for (int j = 0; j <= nv; ++j) {
                Point3 q = sub(add(a[static_cast<size_t>(i)], b[static_cast<size_t>(j)]), s_.origin);
                double dd = dot(sub(q, p), sub(q, p));
                if (dd < best) {
                    best = dd;
                    u = u0_ + (u1_ - u0_) * i / nu;
                    v = v0_ + (v1_ - v0_) * j / nv;
                }
            }
        }
    }

    // Newton on (S - p) · Su = (S - p) · Sv = 0 from the guess
    bool newton_param(const Point3& p, double& u, double& v) const {
        double previous = std::numeric_limits<double>::infinity();
        for (int it = 0; it < 40; ++it) {
            Point3 d[6];
            derivatives(u, v, d);
            Point3 r = sub(d[0], p);
            double fu = dot(r, d[1]);
            double fv = dot(r, d[2]);
            double a = dot(d[1], d[1]) + dot(r, d[3]);
            double b = dot(d[1], d[2]) + dot(r, d[4]);
            double c = dot(d[2], d[2]) + dot(r, d[5]);
            double det = a * c - b * b;
            double du, dv;
            if (std::fabs(det) > 1e-300 && a > 0.0 && det > 0.0) {
                du = (c * fu - b * fv) / det;
                dv = (a * fv - b * fu) / det;
            } else {
                // Gauss–Newton without the second derivatives
                a = dot(d[1], d[1]);
                b = dot(d[1], d[2]);
                c = dot(d[2], d[2]);
                det = a * c - b * b;
                if (!(std::fabs(det) > 1e-300)) break;
                du = (c * fu - b * fv) / det;
                dv = (a * fv - b * fu) / det;
            }
            double nu = u - du;
            double nv = v - dv;
            if (period_u_ <= 0.0) nu = std::clamp(nu, u0_, u1_);
            if (period_v_ <= 0.0) nv = std::clamp(nv, v0_, v1_);
            double step = std::fabs(nu - u) / (u1_ - u0_) + std::fabs(nv - v) / (v1_ - v0_);
            u = nu;
            v = nv;
            // Converged to the last bits, or no longer getting closer
            if (step <= 1e-14 || (it >= 6 && step >= previous)) break;
            previous = step;
        }
        return true;
    }

    const AcisSurface& s_;
    Point3 x_, y_, z_;
    double radius_ = 0.0;
    double k_ = 0.0;
    double period_u_ = 0.0;
    double period_v_ = 0.0;
    double u0_ = 0.0, u1_ = 0.0, v0_ = 0.0, v1_ = 0.0;
    bool valid_ = false;
};

// ---------------------------------------------------------------------------
// Edges: their parameter ranges and their points
// ---------------------------------------------------------------------------

// A piece of an edge on its curve: from parameter `from` to `to` (in the edge's
// direction; a straight edge from 0 at its start to 1 at its end)
struct EdgePiece {
    double from = 0.0;
    double to = 0.0;
};

double ellipse_angle(const AcisCurve& c, const Point3& p) {
    Point3 minor = mul(cross(c.axis, c.major), c.ratio);
    Point3 d = sub(p, c.origin);
    return std::atan2(dot(d, minor) / dot(minor, minor), dot(d, c.major) / dot(c.major, c.major));
}

Point3 ellipse_point(const AcisCurve& c, double t) {
    Point3 minor = mul(cross(c.axis, c.major), c.ratio);
    return add(add(c.origin, mul(c.major, std::cos(t))), mul(minor, std::sin(t)));
}

// The pieces of an edge, from its start vertex to its end vertex. Ellipse: the angles,
// counterclockwise about the normal when the edge runs with the curve; the whole ellipse
// for a closed edge. Spline: the parameters of its vertices, the curve's way unless one of
// the edge and the intcurve is reversed; the whole spline for a closed edge.
std::vector<EdgePiece> edge_pieces(const AcisEdge& e, const AcisCurve& c) {
    std::vector<EdgePiece> out;
    switch (c.type) {
        case AcisCurveType::Straight:
            if (e.has_start && e.has_end) out.push_back({0.0, 1.0});
            break;
        case AcisCurveType::Ellipse: {
            if (!e.has_start || !e.has_end || e.closed) {
                double a = e.has_start ? ellipse_angle(c, e.start) : 0.0;
                out.push_back({a, e.reversed ? a - kTwoPi : a + kTwoPi});
                break;
            }
            double a = ellipse_angle(c, e.start);
            double b = ellipse_angle(c, e.end);
            double sweep = e.reversed ? wrap_angle(a - b) : wrap_angle(b - a);
            if (sweep == 0.0) break;
            out.push_back({a, e.reversed ? a - sweep : a + sweep});
            break;
        }
        case AcisCurveType::Spline: {
            const AcisSplineCurve& s = c.spline;
            double lo = curve_lo(s);
            double hi = curve_hi(s);
            bool backward = e.reversed != c.reversed;
            if (!e.has_start || !e.has_end || e.closed) {
                if (backward) out.push_back({hi, lo});
                else out.push_back({lo, hi});
                break;
            }
            double a = curve_param(s, e.start);
            double b = curve_param(s, e.end);
            if (!backward) {
                if (b >= a) out.push_back({a, b});
                else if (s.periodic) {
                    out.push_back({a, hi});
                    out.push_back({lo, b});
                } else {
                    out.push_back({a, b});
                }
            } else {
                if (b <= a) out.push_back({a, b});
                else if (s.periodic) {
                    out.push_back({a, lo});
                    out.push_back({hi, b});
                } else {
                    out.push_back({a, b});
                }
            }
            break;
        }
        default:
            break;
    }
    return out;
}

Point3 edge_point(const AcisEdge& e, const AcisCurve& c, double t) {
    switch (c.type) {
        case AcisCurveType::Straight:
            return add(e.start, mul(sub(e.end, e.start), t));
        case AcisCurveType::Ellipse:
            return ellipse_point(c, t);
        case AcisCurveType::Spline: {
            Point3 p;
            curve_eval(c.spline, std::clamp(t, curve_lo(c.spline), curve_hi(c.spline)), p);
            return p;
        }
        default:
            return e.start;
    }
}

// ---------------------------------------------------------------------------
// Intersection curves on their surfaces: the exact points near the spline's
// ---------------------------------------------------------------------------

// A surface as an equation f(p) = 0, with its gradient: the signed distance for a plane,
// a sphere and a torus, a distance-like function for a cone, the distance along the
// normal from the nearest point for a spline, a revolution and a sum
class SurfaceEquation {
public:
    explicit SurfaceEquation(const AcisSurface& s) : s_(s) {
        if (s.type == AcisSurfaceType::Cone) {
            a_ = norm(s.major);
            x_ = normalized(s.major);
            Point3 minor = mul(cross(s.axis, s.major), s.ratio);
            b_ = norm(minor);
            y_ = normalized(minor);
            k_ = a_ > 0.0 && s.cosine != 0.0 ? s.sine / s.cosine / a_ : 0.0;
        } else if (s.type == AcisSurfaceType::Torus) {
            Point3 x = sub(s.major, mul(s.axis, dot(s.major, s.axis)));
            x_ = norm(x) > 0.0 ? normalized(x) : perpendicular(s.axis);
            y_ = cross(s.axis, x_);
        }
    }

    bool usable() const {
        switch (s_.type) {
            case AcisSurfaceType::Plane:
            case AcisSurfaceType::Sphere:
            case AcisSurfaceType::Torus:
                return true;
            case AcisSurfaceType::Spline:
                return !s_.spline.points.empty();
            case AcisSurfaceType::Revolution:
            case AcisSurfaceType::Sum: {
                const Point3 dirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                return SurfaceParam(s_, dirs).valid();
            }
            case AcisSurfaceType::Cone:
                return a_ > 0.0 && b_ > 0.0;
            default:
                return false;
        }
    }

    double value(const Point3& p, Point3& grad) {
        Point3 d = sub(p, s_.origin);
        switch (s_.type) {
            case AcisSurfaceType::Plane:
                grad = s_.axis;
                return dot(d, s_.axis);
            case AcisSurfaceType::Sphere: {
                double l = norm(d);
                grad = l > 0.0 ? mul(d, 1.0 / l) : Point3{0.0, 0.0, 1.0};
                return l - std::fabs(s_.radius);
            }
            case AcisSurfaceType::Torus: {
                double x = dot(d, x_);
                double y = dot(d, y_);
                double h = dot(d, s_.axis);
                double rho = std::sqrt(x * x + y * y);
                Point3 radial = rho > 0.0 ? mul(add(mul(x_, x), mul(y_, y)), 1.0 / rho) : x_;
                double e = rho - s_.radius;
                double D = std::sqrt(e * e + h * h);
                grad = D > 0.0 ? add(mul(radial, e / D), mul(s_.axis, h / D)) : s_.axis;
                return D - std::fabs(s_.minor);
            }
            case AcisSurfaceType::Cone: {
                double h = dot(d, s_.axis);
                double x = dot(d, x_);
                double y = dot(d, y_);
                double f = 1.0 + h * k_;
                double q = std::sqrt(x * x / (a_ * a_) + y * y / (b_ * b_));
                double sign = f < 0.0 ? -1.0 : 1.0;
                grad = mul(add(add(mul(x_, q > 0.0 ? x / (a_ * a_ * q) : 0.0),
                                   mul(y_, q > 0.0 ? y / (b_ * b_ * q) : 0.0)),
                               mul(s_.axis, -sign * k_)),
                           a_);
                return a_ * (q - std::fabs(f));
            }
            case AcisSurfaceType::Spline:
            case AcisSurfaceType::Revolution:
            case AcisSurfaceType::Sum: {
                const Point3 dirs[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                SurfaceParam sp(s_, dirs);
                bool singular = false;
                sp.param(p, u_, v_, singular, have_uv_);
                have_uv_ = true;
                Point3 q = sp.point(u_, v_);
                Point3 n = normalized(sp.normal(u_, v_));
                grad = n;
                return dot(sub(p, q), n);
            }
            default:
                grad = {0.0, 0.0, 1.0};
                return 0.0;
        }
    }

private:
    const AcisSurface& s_;
    Point3 x_, y_;
    double a_ = 0.0, b_ = 0.0, k_ = 0.0;
    double u_ = 0.0, v_ = 0.0;
    bool have_uv_ = false;
};

// Solves a 3 × 3 system J x = f; false when J is singular
bool solve3(const double J[3][3], const double f[3], double x[3]) {
    double det = J[0][0] * (J[1][1] * J[2][2] - J[1][2] * J[2][1]) -
                 J[0][1] * (J[1][0] * J[2][2] - J[1][2] * J[2][0]) +
                 J[0][2] * (J[1][0] * J[2][1] - J[1][1] * J[2][0]);
    if (!(std::fabs(det) > 1e-300)) return false;
    for (int c = 0; c < 3; ++c) {
        double M[3][3];
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) M[i][j] = j == c ? f[i] : J[i][j];
        }
        x[c] = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
                M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) / det;
    }
    return true;
}

// The point of the intersection curve near p0 where w · p is extreme along it (its
// tangent, the normals' cross product, ⊥ w): Newton on f1 = f2 = 0, w · (∇f1 × ∇f2) = 0.
// Without w (zero): the nearest point of the curve (the third equation keeps the step in
// the plane through p0 across the curve).
bool relax(SurfaceEquation& s1, SurfaceEquation& s2, const Point3& p0, const Point3& w, double scale,
           Point3& out) {
    Point3 p = p0;
    bool extreme = norm(w) > 0.0;
    Point3 g1, g2;
    s1.value(p0, g1);
    s2.value(p0, g2);
    Point3 tangent0 = normalized(cross(g1, g2));
    // w · (∇f1 × ∇f2) at q: its derivative is taken numerically, the surfaces' gradients are
    // their own
    auto turn = [&](const Point3& q) {
        Point3 a, b;
        s1.value(q, a);
        s2.value(q, b);
        return dot(w, cross(a, b));
    };
    double h = 1e-7 * scale;
    double previous = std::numeric_limits<double>::infinity();
    bool converged = false;
    for (int it = 0; it < 30; ++it) {
        Point3 a, b;
        double F[3];
        F[0] = s1.value(p, a);
        F[1] = s2.value(p, b);
        double J[3][3] = {{a.x, a.y, a.z}, {b.x, b.y, b.z}, {0.0, 0.0, 0.0}};
        if (extreme) {
            F[2] = dot(w, cross(a, b));
            for (int c = 0; c < 3; ++c) {
                Point3 dp = {c == 0 ? h : 0.0, c == 1 ? h : 0.0, c == 2 ? h : 0.0};
                J[2][c] = (turn(add(p, dp)) - turn(sub(p, dp))) / (2.0 * h);
            }
        } else {
            F[2] = dot(tangent0, sub(p, p0));
            J[2][0] = tangent0.x;
            J[2][1] = tangent0.y;
            J[2][2] = tangent0.z;
        }
        double step[3];
        if (!solve3(J, F, step)) return false;
        Point3 d = {step[0], step[1], step[2]};
        p = sub(p, d);
        double size = norm(d);
        if (size <= 1e-14 * scale) {
            converged = true;
            break;
        }
        if (it >= 8 && size >= previous) break;     // no longer getting closer
        previous = size;
    }
    double F[2];
    Point3 a, b;
    F[0] = s1.value(p, a);
    F[1] = s2.value(p, b);
    if (!(std::fabs(F[0]) <= 1e-9 * scale && std::fabs(F[1]) <= 1e-9 * scale)) return false;
    if (!converged && extreme && std::fabs(dot(w, cross(a, b))) > 1e-9 * norm(w) * norm(a) * norm(b)) return false;
    out = p;
    return true;
}

// ---------------------------------------------------------------------------
// A face in its surface's parameters
// ---------------------------------------------------------------------------

struct UvPoint {
    double u = 0.0;
    double v = 0.0;
};

struct UvSegment {
    UvPoint a;
    UvPoint b;
};

// A point of an edge sampled for its face: its parameter on the curve, the point, its
// (u, v), and whether it is at a pole or an apex (where u is undefined)
struct EdgeSample {
    double t = 0.0;
    Point3 p;
    UvPoint uv;
    bool singular = false;
};

// Where a segment of a loop comes from: its edge and the edge's curve, the curve's
// parameters at the segment's ends, and how far the edge (mapped to (u, v)) may be from
// the segment
struct SegmentSource {
    const AcisEdge* edge = nullptr;
    const AcisCurve* curve = nullptr;
    double ta = 0.0;
    double tb = 0.0;
    double band = 0.0;
};

// A piece of a loop as a decision takes it: a chord, how far the edge may be from it, and
// whether it is the edge itself to the last bits (it cannot be split further)
struct UvPiece {
    UvSegment chord;
    double slack = 0.0;
    bool last = false;
};

// Whether what is decided may lie within a margin of a chord
using UvNear = std::function<bool(const UvPoint&, const UvPoint&, double)>;

// The distance from p to the segment from a to b
double segment_distance(const UvPoint& p, const UvPoint& a, const UvPoint& b) {
    double du = b.u - a.u, dv = b.v - a.v;
    double ll = du * du + dv * dv;
    double t = ll > 0.0 ? std::clamp(((p.u - a.u) * du + (p.v - a.v) * dv) / ll, 0.0, 1.0) : 0.0;
    double x = a.u + t * du - p.u, y = a.v + t * dv - p.v;
    return std::sqrt(x * x + y * y);
}

// Whether the segment from r to q crosses the segment from a to b
bool segments_cross(const UvPoint& r, const UvPoint& q, const UvPoint& a, const UvPoint& b) {
    auto orient = [](const UvPoint& o, const UvPoint& x, const UvPoint& y) {
        return (x.u - o.u) * (y.v - o.v) - (x.v - o.v) * (y.u - o.u);
    };
    if ((orient(r, q, a) > 0.0) == (orient(r, q, b) > 0.0)) return false;
    return (orient(a, b, r) > 0.0) != (orient(a, b, q) > 0.0);
}

// The face's loops are sampled (the polylines, for speed) and every decision is exact: where
// a point it decides lies within the sampling's distance of a segment (its band), the edge
// is followed there on its own curve — halved, mapped point by point — until the point is
// farther from each piece than the edge can be (twice the distance of the edge's points at a
// quarter, a half and three quarters from the piece), or the piece is the edge to the last
// bits. A point on a loop to the last bits is on the face.
class FaceRegion {
public:
    FaceRegion(const AcisBody& body, const AcisFace& face, const SurfaceParam& sp);

    bool everywhere() const { return everywhere_; }
    bool usable() const { return usable_; }
    bool inside(double u, double v) const;
    // 0 outside, 1 inside, 2 a loop may run through the rectangle
    int rectangle(double u0, double u1, double v0, double v1) const;
    // The first point inside the face on the line v = v0 for u in [u0, u0 + length]
    bool line(double v0, double u0, double length, UvPoint& found) const;
    // Whether the face comes as near as one likes to the line v = v0 from the side
    // (+1 above, -1 below): an apex or a pole there is in it
    bool reaches(double v0, double side) const;
    // The 3D points the loops were drawn from
    const std::vector<Point3>& boundary_points() const { return points_; }

private:
    void add_coedge(const AcisBody& body, const AcisCoedge& ce);
    bool pick_reference();
    int crossings(const UvPoint& r, const UvPoint& q, bool& on_loop) const;
    // The edge's (u, v) at t, continued from near
    UvPoint map_at(const SegmentSource& s, double t, const UvPoint& near) const;
    // How far the edge between ta and tb is from the chord a b, by its points at a quarter,
    // a half and three quarters
    double deviation(const SegmentSource& s, double ta, double tb, const UvPoint& a, const UvPoint& b,
                     UvPoint* middle = nullptr) const;
    // The edge between ta and tb as pieces, split where near holds within the edge's slack
    void refine(const SegmentSource& s, double ta, double tb, const UvPoint& a, const UvPoint& b, const UvNear& near,
                std::vector<UvPiece>& out, int depth, double above) const;
    // Segment i moved by (du, dv) (a periodic image) as the pieces near needs
    void pieces(size_t i, double du, double dv, const UvNear& near, std::vector<UvPiece>& out) const;
    // The periodic images k of a segment that may reach [lo, hi] in a periodic coordinate
    static void image_range(double seg_lo, double seg_hi, double lo, double hi, double period,
                            int& k0, int& k1);

    const SurfaceParam& sp_;
    double pu_ = 0.0;
    double pv_ = 0.0;
    std::vector<UvSegment> segments_;
    std::vector<SegmentSource> sources_;   // per segment
    std::vector<bool> seam_;            // per segment: from a seam's coedge
    std::vector<Point3> points_;
    UvPoint ref_;
    double band_ = 0.0;                 // the largest band
    double eps_ = 0.0;                  // the last bits of (u, v)
    double size_ = 1.0;                 // the loops' extent in (u, v)
    bool everywhere_ = false;
    bool usable_ = true;
    double face_sign_ = 1.0;
};

// A seam is an edge the face's loops run along both ways (a closed surface's seam, or a
// slit): the face lies on both its sides, and a path crosses it twice. A face bounded by
// seams only, or without loops, is the whole surface (ACIS).
FaceRegion::FaceRegion(const AcisBody& body, const AcisFace& face, const SurfaceParam& sp)
    : sp_(sp), pu_(sp.period_u()), pv_(sp.period_v()) {
    face_sign_ = face.reversed ? -1.0 : 1.0;
    std::unordered_map<int, int> forward, backward;
    for (const auto& loop : face.loops) {
        for (const auto& ce : loop.coedges) ++(ce.reversed ? backward : forward)[ce.edge];
    }
    bool bounded = false;
    for (const auto& loop : face.loops) {
        for (const auto& ce : loop.coedges) {
            bool seam = forward[ce.edge] == 1 && backward[ce.edge] == 1;
            if (!seam) bounded = true;
            add_coedge(body, ce);
            seam_.resize(segments_.size(), seam);
        }
    }
    if (segments_.empty() || !bounded) {
        everywhere_ = true;
        return;
    }
    double size = std::max({1.0, pu_, pv_});
    for (size_t i = 0; i < segments_.size(); ++i) {
        const UvSegment& s = segments_[i];
        size = std::max({size, std::fabs(s.a.u), std::fabs(s.a.v), std::fabs(s.b.u), std::fabs(s.b.v)});
        band_ = std::max(band_, sources_[i].band);
    }
    size_ = size;
    eps_ = 1e-14 * size;
    usable_ = pick_reference();
}

// The coedge's edge sampled in the coedge's direction, mapped to (u, v): more samples
// where the mapped polyline bends away from its chords; u (and v) continued across a
// period; a sample at a pole or an apex takes its neighbour's u. Each segment keeps its
// source and its band: 8 times the tolerance its middle was found within, or where it was
// not split to that (at a pole, or at the sampling's limits) twice its edge's distance.
void FaceRegion::add_coedge(const AcisBody& body, const AcisCoedge& ce) {
    if (ce.edge < 0) return;
    const AcisEdge& e = body.edges[static_cast<size_t>(ce.edge)];
    // An edge whose curve cannot be read: its chord, so that the loop stays closed
    static const AcisCurve chord = [] {
        AcisCurve c;
        c.type = AcisCurveType::Straight;
        return c;
    }();
    bool readable = e.curve >= 0 && body.curves[static_cast<size_t>(e.curve)].type != AcisCurveType::None;
    const AcisCurve& c = readable ? body.curves[static_cast<size_t>(e.curve)] : chord;
    if (!readable && (!e.has_start || !e.has_end)) return;
    std::vector<EdgePiece> pieces = edge_pieces(e, c);
    if (ce.reversed) {
        std::reverse(pieces.begin(), pieces.end());
        for (auto& pc : pieces) std::swap(pc.from, pc.to);
    }
    using Sample = EdgeSample;
    bool hint = false;
    UvPoint last;
    auto map = [&](double t, Sample& s) {
        s.t = t;
        s.p = edge_point(e, c, t);
        double u = last.u, v = last.v;
        sp_.param(s.p, u, v, s.singular, hint);
        hint = true;
        s.uv = {u, v};
        last = s.uv;
    };
    for (const auto& piece : pieces) {
        int count = 16;
        if (c.type == AcisCurveType::Ellipse) {
            count = std::max(16, static_cast<int>(std::ceil(std::fabs(piece.to - piece.from) / kTwoPi * 64.0)));
        } else if (c.type == AcisCurveType::Spline) {
            count = std::max(16, 4 * static_cast<int>(c.spline.points.size()));
        }
        std::vector<Sample> samples(static_cast<size_t>(count) + 1);
        for (int i = 0; i <= count; ++i) map(piece.from + (piece.to - piece.from) * i / count, samples[static_cast<size_t>(i)]);
        // Refine where the midpoint is off the chord (in the mapped parameters)
        double span_u = 0.0, span_v = 0.0;
        for (const auto& s : samples) {
            span_u = std::max(span_u, std::fabs(s.uv.u - samples[0].uv.u));
            span_v = std::max(span_v, std::fabs(s.uv.v - samples[0].uv.v));
        }
        double tol = 1e-6 * (std::max({span_u, span_v, 1.0}));
        std::vector<Sample> refined;
        std::vector<bool> settled;          // per segment (each leaf): its middle within tol
        refined.push_back(samples[0]);
        std::function<void(const Sample&, const Sample&, int)> refine = [&](const Sample& a, const Sample& b, int depth) {
            if (depth < 16 && !a.singular && !b.singular) {
                Sample m;
                last = a.uv;
                hint = true;
                map(0.5 * (a.t + b.t), m);
                double du = m.uv.u - 0.5 * (a.uv.u + b.uv.u);
                double dv = m.uv.v - 0.5 * (a.uv.v + b.uv.v);
                if (pu_ > 0.0) du -= pu_ * std::round(du / pu_);
                if (pv_ > 0.0) dv -= pv_ * std::round(dv / pv_);
                bool within = std::sqrt(du * du + dv * dv) <= tol;
                if (!within && refined.size() < 20000) {
                    refine(a, m, depth + 1);
                    refined.push_back(m);
                    refine(m, b, depth + 1);
                    return;
                }
                settled.push_back(within);
                return;
            }
            settled.push_back(false);
        };
        for (size_t i = 0; i + 1 < samples.size(); ++i) {
            refine(samples[i], samples[i + 1], 0);
            refined.push_back(samples[i + 1]);
        }
        // A singular sample takes the u of a neighbour
        for (size_t i = 0; i < refined.size(); ++i) {
            if (!refined[i].singular) continue;
            bool found = false;
            for (size_t j = i; j-- > 0;) {
                if (!refined[j].singular) { refined[i].uv.u = refined[j].uv.u; found = true; break; }
            }
            if (!found) {
                for (size_t j = i + 1; j < refined.size(); ++j) {
                    if (!refined[j].singular) { refined[i].uv.u = refined[j].uv.u; break; }
                }
            }
        }
        // Continuous across the periods
        for (size_t i = 1; i < refined.size(); ++i) {
            UvPoint& q = refined[i].uv;
            const UvPoint& prev = refined[i - 1].uv;
            if (pu_ > 0.0) q.u -= pu_ * std::round((q.u - prev.u) / pu_);
            if (pv_ > 0.0) q.v -= pv_ * std::round((q.v - prev.v) / pv_);
        }
        for (size_t i = 0; i < refined.size(); ++i) {
            points_.push_back(refined[i].p);
            if (i + 1 < refined.size()) {
                segments_.push_back({refined[i].uv, refined[i + 1].uv});
                SegmentSource src;
                src.edge = &e;
                src.curve = &c;
                src.ta = refined[i].t;
                src.tb = refined[i + 1].t;
                src.band = 8.0 * tol;
                if (!settled[i]) {
                    src.band += 2.0 * deviation(src, src.ta, src.tb, segments_.back().a, segments_.back().b);
                }
                sources_.push_back(src);
            }
        }
    }
}

void FaceRegion::image_range(double seg_lo, double seg_hi, double lo, double hi, double period,
                             int& k0, int& k1) {
    if (period <= 0.0) {
        k0 = k1 = 0;
        return;
    }
    k0 = static_cast<int>(std::ceil((lo - seg_hi) / period - 1e-9));
    k1 = static_cast<int>(std::floor((hi - seg_lo) / period + 1e-9));
}

UvPoint FaceRegion::map_at(const SegmentSource& s, double t, const UvPoint& near) const {
    Point3 p = edge_point(*s.edge, *s.curve, t);
    double u = near.u, v = near.v;
    bool singular = false;
    sp_.param(p, u, v, singular, true);
    if (singular) u = near.u;
    if (pu_ > 0.0) u -= pu_ * std::round((u - near.u) / pu_);
    if (pv_ > 0.0) v -= pv_ * std::round((v - near.v) / pv_);
    return {u, v};
}

double FaceRegion::deviation(const SegmentSource& s, double ta, double tb, const UvPoint& a, const UvPoint& b,
                             UvPoint* middle) const {
    double tm = 0.5 * (ta + tb);
    UvPoint m = map_at(s, tm, a);
    UvPoint q1 = map_at(s, 0.5 * (ta + tm), a);
    UvPoint q3 = map_at(s, 0.5 * (tm + tb), m);
    if (middle) *middle = m;
    return std::max({segment_distance(q1, a, b), segment_distance(m, a, b), segment_distance(q3, a, b)});
}

// A piece is the edge to the last bits (last) when the edge is that near its chord, or when
// halving it no longer brings the edge nearer (the mapping's own last bits); then its slack
// is how near. Pieces near the target are halved, the others kept.
void FaceRegion::refine(const SegmentSource& s, double ta, double tb, const UvPoint& a, const UvPoint& b,
                        const UvNear& near, std::vector<UvPiece>& out, int depth, double above) const {
    double tm = 0.5 * (ta + tb);
    if (!(tm > std::min(ta, tb) && tm < std::max(ta, tb))) {
        out.push_back({{a, b}, eps_, true});
        return;
    }
    UvPoint m;
    double dev = deviation(s, ta, tb, a, b, &m);
    double slack = 2.0 * dev + eps_;
    bool settled = slack <= 4.0 * eps_ || (dev <= 1e-12 * size_ && dev > 0.5 * above) || depth >= 80;
    if (settled || out.size() >= 65536) {
        out.push_back({{a, b}, slack, settled});
        return;
    }
    if (!near(a, b, slack)) {
        out.push_back({{a, b}, slack, false});
        return;
    }
    refine(s, ta, tm, a, m, near, out, depth + 1, dev);
    refine(s, tm, tb, m, b, near, out, depth + 1, dev);
}

void FaceRegion::pieces(size_t i, double du, double dv, const UvNear& near, std::vector<UvPiece>& out) const {
    out.clear();
    const UvSegment& s = segments_[i];
    UvPoint a = {s.a.u + du, s.a.v + dv};
    UvPoint b = {s.b.u + du, s.b.v + dv};
    if (!near(a, b, sources_[i].band)) {
        out.push_back({{a, b}, sources_[i].band, false});
        return;
    }
    // Followed on the edge in the segment's own place, then moved
    UvNear moved = [&](const UvPoint& x, const UvPoint& y, double margin) {
        return near({x.u + du, x.v + dv}, {y.u + du, y.v + dv}, margin);
    };
    refine(sources_[i], sources_[i].ta, sources_[i].tb, s.a, s.b, moved, out, 0,
           std::numeric_limits<double>::infinity());
    for (auto& p : out) {
        p.chord.a.u += du;
        p.chord.a.v += dv;
        p.chord.b.u += du;
        p.chord.b.v += dv;
    }
}

// A point just inside the face: off the edge at the middle of a long segment, a quarter of
// the way to the nearest other segment, to the face's side. The face's side of a coedge is
// its left seen from the face's outside: in (u, v) the left when Su × Sv points to the
// outside, else the right.
bool FaceRegion::pick_reference() {
    std::vector<size_t> order(segments_.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    auto length = [&](size_t i) {
        double du = segments_[i].b.u - segments_[i].a.u;
        double dv = segments_[i].b.v - segments_[i].a.v;
        return std::sqrt(du * du + dv * dv);
    };
    // The longest first, a seam's last (its other side is the same line)
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (seam_[a] != seam_[b]) return !seam_[a];
        return length(a) > length(b);
    });
    size_t tries = std::min<size_t>(order.size(), 64);
    for (size_t n = 0; n < tries; ++n) {
        const UvSegment& s = segments_[order[n]];
        const SegmentSource& src = sources_[order[n]];
        double len = length(order[n]);
        if (!(len > 0.0)) break;
        UvPoint m = map_at(src, 0.5 * (src.ta + src.tb), s.a);
        Point3 su, sv;
        sp_.partials(m.u, m.v, su, sv);
        Point3 x = cross(su, sv);
        Point3 nrm = sp_.normal(m.u, m.v);
        double orient = dot(x, nrm) * face_sign_;
        if (!(norm(x) > 0.0) || orient == 0.0) continue;
        double side = orient > 0.0 ? 1.0 : -1.0;
        // How near another segment comes to the middle
        double nearest = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (i == order[n]) continue;
            const UvSegment& o = segments_[i];
            int k0, k1, l0, l1;
            image_range(std::min(o.a.u, o.b.u), std::max(o.a.u, o.b.u), m.u - len, m.u + len, pu_, k0, k1);
            image_range(std::min(o.a.v, o.b.v), std::max(o.a.v, o.b.v), m.v - len, m.v + len, pv_, l0, l1);
            for (int k = k0; k <= k1; ++k) {
                for (int l = l0; l <= l1; ++l) {
                    UvPoint a = {o.a.u + k * pu_, o.a.v + l * pv_};
                    UvPoint b = {o.b.u + k * pu_, o.b.v + l * pv_};
                    nearest = std::min(nearest, segment_distance(m, a, b));
                }
            }
        }
        if (!(nearest > 1e-9 * len)) continue;       // a seam: the same line twice
        double eps = 0.25 * std::min(len, nearest);
        double du = (s.b.u - s.a.u) / len;
        double dv = (s.b.v - s.a.v) / len;
        ref_ = {m.u - dv * eps * side, m.v + du * eps * side};
        bool on_loop = false;
        crossings(ref_, ref_, on_loop);
        if (on_loop) continue;
        return true;
    }
    return false;
}

// How many times the segment from r to q crosses the loops (all their periodic images),
// exactly; on_loop when q is on a loop
int FaceRegion::crossings(const UvPoint& r, const UvPoint& q, bool& on_loop) const {
    on_loop = false;
    double ulo = std::min(r.u, q.u) - band_, uhi = std::max(r.u, q.u) + band_;
    double vlo = std::min(r.v, q.v) - band_, vhi = std::max(r.v, q.v) + band_;
    UvNear near = [&](const UvPoint& a, const UvPoint& b, double margin) {
        return segment_distance(r, a, b) <= margin || segment_distance(q, a, b) <= margin;
    };
    std::vector<UvPiece> parts;
    int count = 0;
    for (size_t i = 0; i < segments_.size(); ++i) {
        const UvSegment& s = segments_[i];
        int k0, k1, l0, l1;
        image_range(std::min(s.a.u, s.b.u), std::max(s.a.u, s.b.u), ulo, uhi, pu_, k0, k1);
        image_range(std::min(s.a.v, s.b.v), std::max(s.a.v, s.b.v), vlo, vhi, pv_, l0, l1);
        for (int k = k0; k <= k1; ++k) {
            for (int l = l0; l <= l1; ++l) {
                double du = k * pu_, dv = l * pv_;
                if (std::max(s.a.u, s.b.u) + du < ulo || std::min(s.a.u, s.b.u) + du > uhi) continue;
                if (std::max(s.a.v, s.b.v) + dv < vlo || std::min(s.a.v, s.b.v) + dv > vhi) continue;
                pieces(i, du, dv, near, parts);
                for (const auto& p : parts) {
                    if (p.last && segment_distance(q, p.chord.a, p.chord.b) <= p.slack) {
                        on_loop = true;
                        return 0;
                    }
                    if (segments_cross(r, q, p.chord.a, p.chord.b)) ++count;
                }
            }
        }
    }
    return count;
}

bool FaceRegion::inside(double u, double v) const {
    if (everywhere_) return true;
    if (!usable_) return false;
    UvPoint q = {u, v};
    if (pu_ > 0.0) q.u -= pu_ * std::round((q.u - ref_.u) / pu_);
    if (pv_ > 0.0) q.v -= pv_ * std::round((q.v - ref_.v) / pv_);
    bool on_loop = false;
    int count = crossings(ref_, q, on_loop);
    return on_loop || count % 2 == 0;
}

// Each segment against the rectangle widened by its band (Liang–Barsky): a loop that may
// run through it gives 2; else its middle decides
int FaceRegion::rectangle(double u0, double u1, double v0, double v1) const {
    if (everywhere_) return 1;
    if (!usable_) return 0;
    auto hits = [&](double au, double av, double bu, double bv, double w) {
        double t0 = 0.0, t1 = 1.0;
        double du = bu - au, dv = bv - av;
        double p[4] = {-du, du, -dv, dv};
        double q[4] = {au - (u0 - w), (u1 + w) - au, av - (v0 - w), (v1 + w) - av};
        for (int i = 0; i < 4; ++i) {
            if (p[i] == 0.0) {
                if (q[i] < 0.0) return false;
            } else {
                double t = q[i] / p[i];
                if (p[i] < 0.0) t0 = std::max(t0, t);
                else t1 = std::min(t1, t);
                if (t0 > t1) return false;
            }
        }
        return true;
    };
    for (size_t i = 0; i < segments_.size(); ++i) {
        const UvSegment& s = segments_[i];
        double w = sources_[i].band;
        int k0, k1, l0, l1;
        image_range(std::min(s.a.u, s.b.u), std::max(s.a.u, s.b.u), u0 - w, u1 + w, pu_, k0, k1);
        image_range(std::min(s.a.v, s.b.v), std::max(s.a.v, s.b.v), v0 - w, v1 + w, pv_, l0, l1);
        for (int k = k0; k <= k1; ++k) {
            for (int l = l0; l <= l1; ++l) {
                if (hits(s.a.u + k * pu_, s.a.v + l * pv_, s.b.u + k * pu_, s.b.v + l * pv_, w)) return 2;
            }
        }
    }
    return inside(0.5 * (u0 + u1), 0.5 * (v0 + v1)) ? 1 : 0;
}

// The loops' exact crossings of the line (each followed on its edge to the last bits)
// cut it; the middle of each part decides
bool FaceRegion::line(double v0, double u0, double length, UvPoint& found) const {
    if (everywhere_) {
        found = {u0, v0};
        return true;
    }
    if (!usable_) return false;
    std::vector<double> cuts = {u0, u0 + length};
    UvNear near = [&](const UvPoint& a, const UvPoint& b, double margin) {
        return v0 >= std::min(a.v, b.v) - margin && v0 <= std::max(a.v, b.v) + margin;
    };
    std::vector<UvPiece> parts;
    for (size_t i = 0; i < segments_.size(); ++i) {
        const UvSegment& s = segments_[i];
        double w = sources_[i].band;
        int l0, l1, k0, k1;
        image_range(std::min(s.a.v, s.b.v) - w, std::max(s.a.v, s.b.v) + w, v0, v0, pv_, l0, l1);
        image_range(std::min(s.a.u, s.b.u) - w, std::max(s.a.u, s.b.u) + w, u0, u0 + length, pu_, k0, k1);
        for (int l = l0; l <= l1; ++l) {
            pieces(i, 0.0, l * pv_, near, parts);
            for (const auto& p : parts) {
                double av = p.chord.a.v, bv = p.chord.b.v;
                if ((av - v0) * (bv - v0) > 0.0 || av == bv) continue;
                double t = (v0 - av) / (bv - av);
                for (int k = k0; k <= k1; ++k) {
                    double u = p.chord.a.u + k * pu_ + t * (p.chord.b.u - p.chord.a.u);
                    if (u > u0 && u < u0 + length) cuts.push_back(u);
                }
            }
        }
    }
    std::sort(cuts.begin(), cuts.end());
    for (size_t i = 0; i + 1 < cuts.size(); ++i) {
        if (cuts[i + 1] <= cuts[i]) continue;
        double u = 0.5 * (cuts[i] + cuts[i + 1]);
        if (inside(u, v0)) {
            found = {u, v0};
            return true;
        }
    }
    return false;
}

// The loops' least distance from the line on the side (exact, each followed on its edge
// where it comes within its band): the strip between holds no loop, so a point in it
// decides for all of it; a loop that reaches the line itself puts the line's point on the
// face's boundary
bool FaceRegion::reaches(double v0, double side) const {
    if (everywhere_) return true;
    if (!usable_) return false;
    // A chord's distance from the line on the side (0 when it crosses or touches it, or may
    // reach it from the other side within margin); false when it cannot come to the side
    auto distance = [&](const UvPoint& a, const UvPoint& b, double margin, double& d) {
        double wa = side * (a.v - v0), wb = side * (b.v - v0);
        if (std::max(wa, wb) < -margin) return false;
        d = std::min(wa, wb) > 0.0 ? std::min(wa, wb) : 0.0;
        return true;
    };
    UvNear near = [&](const UvPoint& a, const UvPoint& b, double margin) {
        double d = 0.0;
        return distance(a, b, margin, d) && d <= margin;
    };
    double gap = std::numeric_limits<double>::infinity();
    std::vector<UvPiece> parts;
    for (size_t i = 0; i < segments_.size(); ++i) {
        const UvSegment& s = segments_[i];
        int l0, l1;
        image_range(std::min(s.a.v, s.b.v), std::max(s.a.v, s.b.v), v0 - pv_, v0 + pv_, pv_, l0, l1);
        for (int l = l0; l <= l1; ++l) {
            pieces(i, 0.0, l * pv_, near, parts);
            for (const auto& p : parts) {
                double d = 0.0;
                if (!distance(p.chord.a, p.chord.b, p.slack, d)) continue;
                if (p.last && d <= p.slack) return true;
                gap = std::min(gap, d - p.slack);
            }
        }
    }
    if (gap <= eps_) return true;
    double step = std::isinf(gap) ? 1.0 : 0.5 * gap;
    return inside(ref_.u, v0 + side * step);
}

// ---------------------------------------------------------------------------
// The body's box
// ---------------------------------------------------------------------------

class BodyExtents {
public:
    BodyExtents(const AcisBody& body, const Affine& world, Extents& out)
        : body_(body), world_(world), out_(out) {
        for (int i = 0; i < 3; ++i) directions_[i] = world.row(i);
        scale_ = 1.0;
        for (const auto& e : body.edges) {
            for (const Point3* p : {&e.start, &e.end}) {
                scale_ = std::max({scale_, std::fabs(p->x), std::fabs(p->y), std::fabs(p->z)});
            }
        }
    }

    void run() {
        for (const auto& e : body_.edges) edge(e);
        for (const auto& f : body_.faces) face(f);
    }

private:
    void add_local(const Point3& p) { out_.add(world_.apply(p)); }
    void edge(const AcisEdge& e);
    void intersection_edge(const AcisCurve& c, const std::vector<EdgePiece>& pieces);
    void face(const AcisFace& f);
    void spline_face(const AcisSurface& s, const SurfaceParam& sp, const FaceRegion& region);
    void knot_lines(const AcisSplineSurface& s, const SurfaceParam& sp, const FaceRegion& region);
    void revolution_face(const AcisSurface& s, const SurfaceParam& sp, const FaceRegion& region);
    void sum_face(const AcisSurface& s, const SurfaceParam& sp, const FaceRegion& region);

    const AcisBody& body_;
    const Affine& world_;
    Extents& out_;
    Point3 directions_[3];
    double scale_ = 1.0;
};

// An edge's points: line ends; an ellipse's ends and extremes; a spline's exactly; an
// intersection curve's exact points
void BodyExtents::edge(const AcisEdge& e) {
    if (e.has_start) add_local(e.start);
    if (e.has_end) add_local(e.end);
    if (e.curve < 0) return;
    const AcisCurve& c = body_.curves[static_cast<size_t>(e.curve)];
    std::vector<EdgePiece> pieces = edge_pieces(e, c);
    switch (c.type) {
        case AcisCurveType::Straight:
            break;
        case AcisCurveType::Ellipse: {
            Point3 minor = mul(cross(c.axis, c.major), c.ratio);
            for (const auto& pc : pieces) {
                double a0 = std::min(pc.from, pc.to);
                double a1 = std::max(pc.from, pc.to);
                bool full = a1 - a0 >= kTwoPi;
                ellipse_extents(out_, world_.apply(c.origin), world_.linear(c.major), world_.linear(minor),
                                a0, a1, full);
            }
            break;
        }
        case AcisCurveType::Spline: {
            if (c.fit_tolerance > 0.0) {
                intersection_edge(c, pieces);
                break;
            }
            auto map = [&](const Point3& p) { return world_.apply(p); };
            for (const auto& pc : pieces) {
                nurbs_extents(out_, map, c.spline.degree, c.spline.knots, c.spline.points, c.spline.weights,
                              std::min(pc.from, pc.to), std::max(pc.from, pc.to));
            }
            break;
        }
        default:
            break;
    }
}

// An intersection curve: its spline's extremes moved onto the curve — to where the
// coordinate is extreme along the curve itself, or (no such point near) to the nearest
// point of the curve. Without usable surfaces, the spline (within its fit tolerance).
void BodyExtents::intersection_edge(const AcisCurve& c, const std::vector<EdgePiece>& pieces) {
    SurfaceEquation s1(c.surface1);
    SurfaceEquation s2(c.surface2);
    bool exact = s1.usable() && s2.usable();
    auto map = [&](const Point3& p) { return world_.apply(p); };
    for (const auto& pc : pieces) {
        double lo = std::min(pc.from, pc.to);
        double hi = std::max(pc.from, pc.to);
        if (!exact) {
            nurbs_extents(out_, map, c.spline.degree, c.spline.knots, c.spline.points, c.spline.weights, lo, hi);
            continue;
        }
        Extents scratch;
        std::vector<double> params;
        nurbs_extents(scratch, map, c.spline.degree, c.spline.knots, c.spline.points, c.spline.weights, lo, hi,
                      &params);
        double reach = 10.0 * c.fit_tolerance + 1e-9 * scale_;
        double step = 1e-9 * (hi - lo);
        for (double t : params) {
            if (!(t > lo && t < hi)) continue;          // the ends are the vertices
            Point3 p0, d1, before, after;
            curve_eval(c.spline, t, p0, &d1);
            curve_eval(c.spline, std::max(lo, t - step), before);
            curve_eval(c.spline, std::min(hi, t + step), after);
            for (int i = 0; i < 3; ++i) {
                // Only where the spline has an extreme in this coordinate: its derivative
                // 0, or (at a knot where it only is continuous) turning there
                const Point3& w = directions_[i];
                bool flat = std::fabs(dot(w, d1)) <= 1e-6 * norm(w) * norm(d1);
                bool turning = dot(w, sub(p0, before)) * dot(w, sub(after, p0)) < 0.0;
                if (!flat && !turning) continue;
                Point3 p;
                if (relax(s1, s2, p0, w, scale_, p) && norm(sub(p, p0)) <= reach) {
                    double tp = curve_param(c.spline, p);
                    if (tp > lo && tp < hi) {
                        add_local(p);
                        continue;
                    }
                }
                // No extreme of the curve itself here: its nearest point
                if (relax(s1, s2, p0, {0.0, 0.0, 0.0}, scale_, p) && norm(sub(p, p0)) <= reach) add_local(p);
            }
        }
    }
}

// The interior extremes of a face
void BodyExtents::face(const AcisFace& f) {
    if (f.surface < 0) return;
    const AcisSurface& s = body_.surfaces[static_cast<size_t>(f.surface)];
    if (s.type != AcisSurfaceType::Sphere && s.type != AcisSurfaceType::Torus &&
        s.type != AcisSurfaceType::Cone && s.type != AcisSurfaceType::Spline &&
        s.type != AcisSurfaceType::Revolution && s.type != AcisSurfaceType::Sum) return;
    // Straight in one direction: its edges bound it (a sum without loops: its ends)
    if (s.ruled && !(s.type == AcisSurfaceType::Sum && f.loops.empty())) return;
    SurfaceParam sp(s, directions_);
    if (!sp.valid()) return;
    if (s.type == AcisSurfaceType::Cone) {
        double va = 0.0;
        if (!sp.apex(va) || f.loops.empty()) return;          // a cylinder, or unbounded
    }
    FaceRegion region(body_, f, sp);
    if (!region.usable()) return;

    auto candidate = [&](const Point3& p) {
        double u = 0.0, v = 0.0;
        bool singular = false;
        if (!sp.param(p, u, v, singular, false)) return;
        if (region.inside(u, v)) add_local(p);
    };

    switch (s.type) {
        case AcisSurfaceType::Sphere: {
            double r = std::fabs(s.radius);
            for (const auto& w : directions_) {
                if (!(norm(w) > 0.0)) continue;
                Point3 n = normalized(w);
                candidate(add(s.origin, mul(n, r)));
                candidate(sub(s.origin, mul(n, r)));
            }
            break;
        }
        case AcisSurfaceType::Torus: {
            // Where the normal is ±w: the tube's circle in the plane of w and the axis,
            // at its extremes in w; when w is the axis, the top and bottom circles. A lemon
            // or apple (|R| < r) also has its apexes on the axis.
            double R = s.radius;
            double r = std::fabs(s.minor);
            Point3 z = s.axis;
            for (const auto& w : directions_) {
                if (!(norm(w) > 0.0)) continue;
                Point3 wp = sub(w, mul(z, dot(w, z)));
                if (norm(wp) > 1e-12 * norm(w)) {
                    Point3 d0 = normalized(wp);
                    for (double sd : {1.0, -1.0}) {
                        Point3 d = mul(d0, sd);
                        double a = dot(w, d);
                        double b = dot(w, z);
                        double l = std::sqrt(a * a + b * b);
                        for (double s2 : {1.0, -1.0}) {
                            double cv = s2 * a / l;
                            double sv = s2 * b / l;
                            if (R + r * cv < -1e-12 * (std::fabs(R) + r)) continue;
                            candidate(add(add(s.origin, mul(d, R + r * cv)), mul(z, r * sv)));
                        }
                    }
                } else if (R >= 0.0) {
                    for (double vv : {kPi / 2.0, -kPi / 2.0}) {
                        UvPoint q;
                        if (region.line(vv, 0.0, kTwoPi, q)) add_local(sp.point(q.u, q.v));
                    }
                }
            }
            if (std::fabs(R) < r) {
                double h = std::sqrt(r * r - R * R);
                candidate(add(s.origin, mul(z, h)));
                candidate(sub(s.origin, mul(z, h)));
            }
            break;
        }
        case AcisSurfaceType::Cone: {
            // The apex: in the face when the face reaches it from either side
            double va = 0.0;
            sp.apex(va);
            if (region.reaches(va, 1.0) || region.reaches(va, -1.0)) add_local(sp.point(0.0, va));
            break;
        }
        case AcisSurfaceType::Spline:
            spline_face(s, sp, region);
            break;
        case AcisSurfaceType::Revolution:
            revolution_face(s, sp, region);
            break;
        case AcisSurfaceType::Sum:
            sum_face(s, sp, region);
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// A spline face: branch and bound on the surface's Bézier patches
// ---------------------------------------------------------------------------

using Homogeneous = std::array<double, 4>;     // w x, w y, w z, w

// A rational Bézier patch: (du + 1) × (dv + 1) homogeneous points, net[j * (du + 1) + i]
struct BezierPatch {
    int du = 0;
    int dv = 0;
    double u0 = 0.0, u1 = 0.0, v0 = 0.0, v1 = 0.0;
    std::vector<Homogeneous> net;
    double bound = 0.0;
};

// The Bézier points of the span [a, b] (in span k) of a B-spline with homogeneous points
// ctrl: the blossom at (a … a, b … b)
std::vector<Homogeneous> bezier_span(const std::vector<Homogeneous>& ctrl, const std::vector<double>& knots,
                                     int p, size_t k, double a, double b) {
    std::vector<Homogeneous> out(static_cast<size_t>(p) + 1);
    for (int j = 0; j <= p; ++j) {
        std::vector<Homogeneous> d(static_cast<size_t>(p) + 1);
        for (int i = 0; i <= p; ++i) d[static_cast<size_t>(i)] = ctrl[k - p + i];
        for (int r = 1; r <= p; ++r) {
            double t = r <= p - j ? a : b;
            for (int i = p; i >= r; --i) {
                size_t g = k - p + i;
                double den = knots[g + p - r + 1] - knots[g];
                double alpha = den == 0.0 ? 0.0 : (t - knots[g]) / den;
                for (int c = 0; c < 4; ++c) {
                    d[static_cast<size_t>(i)][c] = (1.0 - alpha) * d[static_cast<size_t>(i) - 1][c] + alpha * d[static_cast<size_t>(i)][c];
                }
            }
        }
        out[static_cast<size_t>(j)] = d[static_cast<size_t>(p)];
    }
    return out;
}

std::vector<BezierPatch> bezier_patches(const AcisSplineSurface& s) {
    std::vector<BezierPatch> out;
    size_t nu = static_cast<size_t>(s.count_u);
    size_t nv = static_cast<size_t>(s.count_v);
    std::vector<Homogeneous> h(nu * nv);
    for (size_t i = 0; i < h.size(); ++i) {
        double w = i < s.weights.size() ? s.weights[i] : 1.0;
        h[i] = {s.points[i].x * w, s.points[i].y * w, s.points[i].z * w, w};
    }
    int pu = s.degree_u;
    int pv = s.degree_v;
    for (size_t ku = static_cast<size_t>(pu); ku < nu; ++ku) {
        double a = s.knots_u[ku], b = s.knots_u[ku + 1];
        if (b <= a) continue;
        // Each row (fixed v index) to its u-Bézier points on [a, b]
        std::vector<std::vector<Homogeneous>> rows(nv);
        for (size_t j = 0; j < nv; ++j) {
            std::vector<Homogeneous> row(nu);
            for (size_t i = 0; i < nu; ++i) row[i] = h[j * nu + i];
            rows[j] = bezier_span(row, s.knots_u, pu, ku, a, b);
        }
        for (size_t kv = static_cast<size_t>(pv); kv < nv; ++kv) {
            double c = s.knots_v[kv], d = s.knots_v[kv + 1];
            if (d <= c) continue;
            BezierPatch patch;
            patch.du = pu;
            patch.dv = pv;
            patch.u0 = a;
            patch.u1 = b;
            patch.v0 = c;
            patch.v1 = d;
            patch.net.assign(static_cast<size_t>((pu + 1) * (pv + 1)), Homogeneous{});
            for (int i = 0; i <= pu; ++i) {
                std::vector<Homogeneous> col(nv);
                for (size_t j = 0; j < nv; ++j) col[j] = rows[j][static_cast<size_t>(i)];
                std::vector<Homogeneous> bz = bezier_span(col, s.knots_v, pv, kv, c, d);
                for (int j = 0; j <= pv; ++j) patch.net[static_cast<size_t>(j * (pu + 1) + i)] = bz[static_cast<size_t>(j)];
            }
            out.push_back(std::move(patch));
        }
    }
    return out;
}

// Halves of a patch along u (dir 0) or v (dir 1), by de Casteljau at the middle
void split_patch(const BezierPatch& p, int dir, BezierPatch& lo, BezierPatch& hi) {
    lo = p;
    hi = p;
    int nu = p.du + 1;
    int nv = p.dv + 1;
    int lines = dir == 0 ? nv : nu;
    int len = dir == 0 ? nu : nv;
    for (int line = 0; line < lines; ++line) {
        auto at = [&](int k) { return dir == 0 ? static_cast<size_t>(line * nu + k) : static_cast<size_t>(k * nu + line); };
        std::vector<Homogeneous> q(static_cast<size_t>(len));
        for (int k = 0; k < len; ++k) q[static_cast<size_t>(k)] = p.net[at(k)];
        // q after r levels: left gets q[0], right gets q[len - 1 - r]
        lo.net[at(0)] = q[0];
        hi.net[at(len - 1)] = q[static_cast<size_t>(len - 1)];
        for (int r = 1; r < len; ++r) {
            for (int k = 0; k < len - r; ++k) {
                for (int c = 0; c < 4; ++c) q[static_cast<size_t>(k)][c] = 0.5 * (q[static_cast<size_t>(k)][c] + q[static_cast<size_t>(k) + 1][c]);
            }
            lo.net[at(r)] = q[0];
            hi.net[at(len - 1 - r)] = q[static_cast<size_t>(len - 1 - r)];
        }
    }
    if (dir == 0) {
        double m = 0.5 * (p.u0 + p.u1);
        lo.u1 = m;
        hi.u0 = m;
    } else {
        double m = 0.5 * (p.v0 + p.v1);
        lo.v1 = m;
        hi.v0 = m;
    }
}

// A tensor-product Bernstein polynomial: c[j * (du + 1) + i]
struct Bernstein2 {
    int du = 0;
    int dv = 0;
    std::vector<double> c;
};

// The product of two tensor Bernstein polynomials, term by term:
// B(m,i) B(n,k) = C(m,i) C(n,k) / C(m+n,i+k) B(m+n,i+k) in each direction
Bernstein2 bernstein_multiply(const Bernstein2& a, const Bernstein2& b) {
    Bernstein2 r;
    r.du = a.du + b.du;
    r.dv = a.dv + b.dv;
    r.c.assign(static_cast<size_t>((r.du + 1) * (r.dv + 1)), 0.0);
    auto binomial = [](int N, int K) {
        double x = 1.0;
        for (int i = 1; i <= K; ++i) x = x * (N - K + i) / i;
        return x;
    };
    for (int ja = 0; ja <= a.dv; ++ja) {
        for (int ia = 0; ia <= a.du; ++ia) {
            double ca = a.c[static_cast<size_t>(ja * (a.du + 1) + ia)];
            if (ca == 0.0) continue;
            for (int jb = 0; jb <= b.dv; ++jb) {
                for (int ib = 0; ib <= b.du; ++ib) {
                    double f = binomial(a.du, ia) * binomial(b.du, ib) / binomial(r.du, ia + ib) *
                               binomial(a.dv, ja) * binomial(b.dv, jb) / binomial(r.dv, ja + jb);
                    r.c[static_cast<size_t>((ja + jb) * (r.du + 1) + ia + ib)] +=
                        f * ca * b.c[static_cast<size_t>(jb * (b.du + 1) + ib)];
                }
            }
        }
    }
    return r;
}

// The derivative of a tensor Bernstein polynomial along u (dir 0) or v (dir 1)
Bernstein2 bernstein_derivative(const Bernstein2& a, int dir) {
    Bernstein2 r;
    r.du = dir == 0 ? a.du - 1 : a.du;
    r.dv = dir == 1 ? a.dv - 1 : a.dv;
    if (r.du < 0 || r.dv < 0) {
        r.du = std::max(r.du, 0);
        r.dv = std::max(r.dv, 0);
        r.c.assign(static_cast<size_t>((r.du + 1) * (r.dv + 1)), 0.0);
        return r;
    }
    r.c.assign(static_cast<size_t>((r.du + 1) * (r.dv + 1)), 0.0);
    for (int j = 0; j <= r.dv; ++j) {
        for (int i = 0; i <= r.du; ++i) {
            double hi = dir == 0 ? a.c[static_cast<size_t>(j * (a.du + 1) + i + 1)]
                                 : a.c[static_cast<size_t>((j + 1) * (a.du + 1) + i)];
            double lo = a.c[static_cast<size_t>(j * (a.du + 1) + i)];
            r.c[static_cast<size_t>(j * (r.du + 1) + i)] = (dir == 0 ? a.du : a.dv) * (hi - lo);
        }
    }
    return r;
}

// Can w · S have a critical point in the patch? Not when a partial derivative keeps its
// sign: its numerator (w·A)' W - (w·A) W' (A, W the homogeneous patch) has Bernstein
// coefficients all of one sign.
bool may_be_critical(const BezierPatch& p, const Point3& w) {
    Bernstein2 a, b;
    a.du = b.du = p.du;
    a.dv = b.dv = p.dv;
    for (const auto& h : p.net) {
        a.c.push_back(w.x * h[0] + w.y * h[1] + w.z * h[2]);
        b.c.push_back(h[3]);
    }
    for (int dir = 0; dir < 2; ++dir) {
        Bernstein2 n1 = bernstein_multiply(bernstein_derivative(a, dir), b);
        Bernstein2 n2 = bernstein_multiply(a, bernstein_derivative(b, dir));
        bool positive = true, negative = true;
        for (size_t i = 0; i < n1.c.size(); ++i) {
            double v = n1.c[i] - n2.c[i];
            if (!(v > 0.0)) positive = false;
            if (!(v < 0.0)) negative = false;
        }
        if (positive || negative) return false;
    }
    return true;
}

// Newton on the gradient of w · S from (u, v): a critical point, within [u0, u1] × [v0, v1]
bool critical_point(const AcisSplineSurface& s, const Point3& w, double u0, double u1, double v0, double v1,
                    double& u, double& v) {
    for (int it = 0; it < 50; ++it) {
        Point3 d[6];
        surface_eval(s, u, v, d);
        double gu = dot(w, d[1]), gv = dot(w, d[2]);
        double huu = dot(w, d[3]), huv = dot(w, d[4]), hvv = dot(w, d[5]);
        double det = huu * hvv - huv * huv;
        if (!(std::fabs(det) > 0.0)) return false;
        double su = (hvv * gu - huv * gv) / det;
        double sv = (huu * gv - huv * gu) / det;
        u -= su;
        v -= sv;
        if (!(u >= u0 && u <= u1 && v >= v0 && v <= v1)) return false;
        if (std::fabs(su) <= 1e-15 * (u1 - u0 + std::fabs(u)) && std::fabs(sv) <= 1e-15 * (v1 - v0 + std::fabs(v))) {
            return true;
        }
    }
    return false;
}

// The spline face's extremes, for each coordinate and sense: branch and bound over the
// surface's Bézier patches, largest bound first (the largest value at a control point —
// the patch lies in their hull). A patch goes when it cannot beat the best point known
// (the loops' points first), when it lies outside the face, or when the coordinate has no
// critical point in it (then its largest value in the face is on the face's edges, counted
// exactly, or on the patch's sides: on the knot lines, counted by knot_lines, or inside a
// neighbouring patch); else Newton from its middle finds the critical point, which counts
// when it is in the face, and the patch is halved.
void BodyExtents::spline_face(const AcisSurface& s, const SurfaceParam& sp, const FaceRegion& region) {
    std::vector<BezierPatch> patches = bezier_patches(s.spline);
    if (patches.empty()) return;
    const AcisSplineSurface& sp_s = s.spline;
    knot_lines(sp_s, sp, region);
    double extent = 1.0;
    for (const auto& p : sp_s.points) extent = std::max({extent, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
    for (const auto& w0 : directions_) {
        if (!(norm(w0) > 0.0)) continue;
        for (double sense : {1.0, -1.0}) {
            Point3 w = mul(w0, sense);
            auto bound_of = [&](BezierPatch& p) {
                double b = -std::numeric_limits<double>::infinity();
                for (const auto& h : p.net) {
                    if (h[3] != 0.0) b = std::max(b, (w.x * h[0] + w.y * h[1] + w.z * h[2]) / h[3]);
                }
                p.bound = b;
            };
            double best = -std::numeric_limits<double>::infinity();
            if (region.everywhere()) best = dot(w, sp.point(sp.u_min(), sp.v_min()));
            for (const auto& q : region.boundary_points()) best = std::max(best, dot(w, q));
            bool have_point = false;
            Point3 best_point;
            double tol = 1e-12 * extent * norm(w0);
            auto cmp = [](const BezierPatch& a, const BezierPatch& b) { return a.bound < b.bound; };
            std::priority_queue<BezierPatch, std::vector<BezierPatch>, decltype(cmp)> queue(cmp);
            for (auto p : patches) {
                bound_of(p);
                queue.push(std::move(p));
            }
            double du_min = 1e-9 * (sp.u_max() - sp.u_min());
            double dv_min = 1e-9 * (sp.v_max() - sp.v_min());
            int pops = 0;
            while (!queue.empty() && pops < 100000) {
                BezierPatch p = queue.top();
                queue.pop();
                ++pops;
                if (p.bound <= best + tol) break;
                if (!may_be_critical(p, w)) continue;
                if (region.rectangle(p.u0, p.u1, p.v0, p.v1) == 0) continue;
                double u = 0.5 * (p.u0 + p.u1);
                double v = 0.5 * (p.v0 + p.v1);
                if (critical_point(sp_s, w, p.u0, p.u1, p.v0, p.v1, u, v) && region.inside(u, v)) {
                    Point3 q = sp.point(u, v);
                    double val = dot(w, q);
                    if (val > best) {
                        best = val;
                        best_point = q;
                        have_point = true;
                    }
                }
                bool small_u = p.u1 - p.u0 <= du_min;
                bool small_v = p.v1 - p.v0 <= dv_min;
                if (small_u && small_v) continue;
                int dir = small_u ? 1 : (small_v ? 0 : ((p.u1 - p.u0) / (sp.u_max() - sp.u_min()) >=
                                                        (p.v1 - p.v0) / (sp.v_max() - sp.v_min()) ? 0 : 1));
                BezierPatch a, b;
                split_patch(p, dir, a, b);
                bound_of(a);
                bound_of(b);
                if (a.bound > best + tol) queue.push(std::move(a));
                if (b.bound > best + tol) queue.push(std::move(b));
            }
            if (have_point) add_local(best_point);
        }
    }
}

// The knot lines of a spline face: at an inner knot the surface may be only continuous, so
// its largest value in the face can lie on the line, or where two lines cross. A line is a
// curve of the surface — its control points the rows blended by the basis functions at the
// knot — whose extremes (nurbs_extents's parameters, its knots among them) count where they
// are in the face; a line's ends only in a face without loops (else they are on its
// edges). The surface's ends are lines too in a face without loops, and a closed
// surface's seam is one always.
void BodyExtents::knot_lines(const AcisSplineSurface& s, const SurfaceParam& sp, const FaceRegion& region) {
    auto map = [&](const Point3& p) { return world_.apply(p); };
    bool whole = region.everywhere();
    size_t nu = static_cast<size_t>(s.count_u);
    for (int dir = 0; dir < 2; ++dir) {
        const std::vector<double>& knots = dir == 0 ? s.knots_u : s.knots_v;
        const std::vector<double>& other = dir == 0 ? s.knots_v : s.knots_u;
        int p = dir == 0 ? s.degree_u : s.degree_v;
        int q = dir == 0 ? s.degree_v : s.degree_u;
        size_t n = static_cast<size_t>(dir == 0 ? s.count_u : s.count_v);
        size_t m = static_cast<size_t>(dir == 0 ? s.count_v : s.count_u);
        bool periodic = dir == 0 ? s.periodic_u : s.periodic_v;
        bool periodic_other = dir == 0 ? s.periodic_v : s.periodic_u;
        double lo = knots[static_cast<size_t>(p)], hi = knots[n];
        double other_lo = other[static_cast<size_t>(q)], other_hi = other[m];
        double last = std::numeric_limits<double>::quiet_NaN();
        for (size_t i = static_cast<size_t>(p); i <= n; ++i) {
            double k = knots[i];
            if (k == last) continue;
            last = k;
            bool end = k == lo || k == hi;
            if (periodic && k == hi) continue;          // the line at lo
            if (end && !whole && !periodic) continue;
            // The line's control points: the rows (or columns) blended at k
            size_t span = find_span(knots, p, n, k);
            double N[kMaxDegree + 1][3];
            basis(knots, p, span, k, N);
            std::vector<Point3> points(m);
            std::vector<double> weights(m);
            for (size_t j = 0; j < m; ++j) {
                Homogeneous h = {0.0, 0.0, 0.0, 0.0};
                for (int a = 0; a <= p; ++a) {
                    size_t i2 = span - static_cast<size_t>(p) + static_cast<size_t>(a);
                    size_t idx = dir == 0 ? j * nu + i2 : i2 * nu + j;
                    double w = idx < s.weights.size() ? s.weights[idx] : 1.0;
                    double f = N[a][0] * w;
                    h[0] += f * s.points[idx].x;
                    h[1] += f * s.points[idx].y;
                    h[2] += f * s.points[idx].z;
                    h[3] += f;
                }
                double w = h[3] != 0.0 ? h[3] : 1.0;
                points[j] = {h[0] / w, h[1] / w, h[2] / w};
                weights[j] = h[3];
            }
            Extents scratch;
            std::vector<double> params;
            nurbs_extents(scratch, map, q, other, points, weights, -std::numeric_limits<double>::infinity(),
                          std::numeric_limits<double>::infinity(), &params);
            for (double t : params) {
                if (!whole && !periodic_other && (t <= other_lo || t >= other_hi)) continue;
                double u = dir == 0 ? k : t;
                double v = dir == 0 ? t : k;
                if (region.inside(u, v)) add_local(sp.point(u, v));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Surfaces defined by curves: a revolution and a sum
// ---------------------------------------------------------------------------

// A piece of a curve as a rational Bézier curve: its homogeneous points, and the curve's
// parameters at its ends (an ellipse's arc is not parameterized by its angle)
struct CurveSpan {
    std::vector<Homogeneous> net;
    double t0 = 0.0;
    double t1 = 0.0;
};

// The curve from parameter lo to hi as rational Bézier spans, exactly: a spline's knot
// spans (the blossom), an ellipse's arcs of at most a quarter turn (c + (M cos m + N sin m)/
// cos h between the ends, weight cos h, for the arc m ± h), a straight's segment
std::vector<CurveSpan> curve_spans(const AcisCurve& c, double lo, double hi) {
    std::vector<CurveSpan> out;
    switch (c.type) {
        case AcisCurveType::Straight: {
            Point3 a, b;
            acis_curve_point(c, lo, a);
            acis_curve_point(c, hi, b);
            out.push_back({{{a.x, a.y, a.z, 1.0}, {b.x, b.y, b.z, 1.0}}, lo, hi});
            break;
        }
        case AcisCurveType::Ellipse: {
            int pieces = std::max(1, static_cast<int>(std::ceil((hi - lo) / (kPi / 2.0) - 1e-12)));
            Point3 minor = mul(cross(c.axis, c.major), c.ratio);
            for (int i = 0; i < pieces; ++i) {
                double a = lo + (hi - lo) * i / pieces;
                double b = lo + (hi - lo) * (i + 1) / pieces;
                double h = 0.5 * (b - a), m = 0.5 * (a + b);
                Point3 pa, pb;
                acis_curve_point(c, a, pa);
                acis_curve_point(c, b, pb);
                double w = std::cos(h);
                Point3 mid = add(c.origin, mul(add(mul(c.major, std::cos(m)), mul(minor, std::sin(m))), 1.0 / w));
                out.push_back({{{pa.x, pa.y, pa.z, 1.0}, {mid.x * w, mid.y * w, mid.z * w, w}, {pb.x, pb.y, pb.z, 1.0}},
                               a, b});
            }
            break;
        }
        case AcisCurveType::Spline: {
            const AcisSplineCurve& s = c.spline;
            double a = c.reversed ? -hi : lo;
            double b = c.reversed ? -lo : hi;
            a = std::max(a, curve_lo(s));
            b = std::min(b, curve_hi(s));
            std::vector<Homogeneous> h(s.points.size());
            for (size_t i = 0; i < h.size(); ++i) {
                double w = i < s.weights.size() ? s.weights[i] : 1.0;
                h[i] = {s.points[i].x * w, s.points[i].y * w, s.points[i].z * w, w};
            }
            for (size_t k = static_cast<size_t>(s.degree); k < s.points.size(); ++k) {
                double ka = std::max(s.knots[k], a);
                double kb = std::min(s.knots[k + 1], b);
                if (kb <= ka) continue;
                CurveSpan span;
                span.net = bezier_span(h, s.knots, s.degree, k, ka, kb);
                span.t0 = c.reversed ? -ka : ka;
                span.t1 = c.reversed ? -kb : kb;
                out.push_back(std::move(span));
            }
            break;
        }
        default:
            break;
    }
    return out;
}

// A Bézier span's point at τ in [0, 1] (de Casteljau on the homogeneous points)
Point3 span_point(const CurveSpan& s, double tau) {
    std::vector<Homogeneous> q = s.net;
    for (size_t r = 1; r < q.size(); ++r) {
        for (size_t j = 0; j + r < q.size(); ++j) {
            for (int c = 0; c < 4; ++c) q[j][c] = (1.0 - tau) * q[j][c] + tau * q[j + 1][c];
        }
    }
    double w = q[0][3] != 0.0 ? q[0][3] : 1.0;
    return {q[0][0] / w, q[0][1] / w, q[0][2] / w};
}

// The curve's parameter at a span's τ: the spline's and the straight's run with τ; an
// ellipse's is the angle of the point, taken near the arc's middle
double span_param(const AcisCurve& c, const CurveSpan& s, double tau) {
    if (c.type != AcisCurveType::Ellipse) return s.t0 + tau * (s.t1 - s.t0);
    double m = 0.5 * (s.t0 + s.t1);
    double d = ellipse_angle(c, span_point(s, tau)) - m;
    return m + d - kTwoPi * std::round(d / kTwoPi);
}

// Polynomials in the power basis: a[0] + a[1] t + …
using Poly = std::vector<double>;

Poly poly_mul(const Poly& a, const Poly& b) {
    if (a.empty() || b.empty()) return {};
    Poly c(a.size() + b.size() - 1, 0.0);
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b.size(); ++j) c[i + j] += a[i] * b[j];
    }
    return c;
}

Poly poly_add(const Poly& a, const Poly& b, double sb = 1.0) {
    Poly c(std::max(a.size(), b.size()), 0.0);
    for (size_t i = 0; i < a.size(); ++i) c[i] += a[i];
    for (size_t i = 0; i < b.size(); ++i) c[i] += sb * b[i];
    return c;
}

Poly poly_scale(const Poly& a, double s) {
    Poly c = a;
    for (double& x : c) x *= s;
    return c;
}

Poly poly_diff(const Poly& a) {
    Poly d;
    for (size_t i = 1; i < a.size(); ++i) d.push_back(a[i] * static_cast<double>(i));
    if (d.empty()) d.push_back(0.0);
    return d;
}

// The power forms of a span: W and the homogeneous x, y, z
void span_polys(const CurveSpan& s, Poly& W, Poly P[3]) {
    std::vector<double> q(s.net.size());
    for (size_t j = 0; j < q.size(); ++j) q[j] = s.net[j][3];
    W = bernstein_power(q);
    for (int c = 0; c < 3; ++c) {
        for (size_t j = 0; j < q.size(); ++j) q[j] = s.net[j][static_cast<size_t>(c)];
        P[c] = bernstein_power(q);
    }
}

// v · P - k W: the numerator of v · C - k over the span
Poly span_linear(const Poly& W, const Poly P[3], const Point3& v, double k) {
    Poly r = poly_add(poly_add(poly_scale(P[0], v.x), poly_scale(P[1], v.y)), poly_scale(P[2], v.z));
    return poly_add(r, W, -k);
}

// A surface of revolution's extremes. Along a turn a world coordinate w · S is a(v) +
// b(v) cos u + c(v) sin u, with r = C(v) - A: a = w · A + (w · z)(z · r), b = w⊥ · r, c =
// (w × z) · r. It is extreme across the turns where tan u = c / b, at a ± √(b² + c²), and
// along the profile where that has its derivative 0: on a rational Bézier span (r = R / W,
// a = Â / W, b = B / W, c = C / W) (Â'W - ÂW') √(B² + C²) = ∓((BB' + CC')W - (B² + C²)W'),
// whose square is a polynomial — its roots, both senses. When w is the axis the coordinate
// is the profile's own (Â'W - ÂW' = 0: a circle, any point of it in the face). On the
// profile's knots (the surface may be only continuous along their circles) the same across
// the turns. Where the profile meets the axis, the pole, counts when the face reaches it.
// A face without loops: the points within the surface's range, and its end circles and
// end profiles.
void BodyExtents::revolution_face(const AcisSurface& s, const SurfaceParam& sp, const FaceRegion& region) {
    const AcisCurve& c = s.curves[0];
    const Point3& A = s.origin;
    const Point3& z = s.axis;
    double lo = sp.v_min(), hi = sp.v_max();
    bool whole = region.everywhere();
    // The angles the surface spans (ACIS's v range)
    bool all_turns = !s.has_range || s.v1 - s.v0 >= kTwoPi * (1.0 - 1e-12);
    double turn0 = s.has_range ? s.v0 : 0.0;
    double turn = all_turns ? kTwoPi : s.v1 - s.v0;
    double tiny = 1e-12 * (hi - lo);
    auto in_face = [&](double u, double v) {
        if (!whole) return region.inside(u, v);
        if (v < lo - tiny || v > hi + tiny) return false;
        return all_turns || wrap_angle(u - turn0) <= turn + 1e-12;
    };
    std::vector<CurveSpan> spans = curve_spans(c, lo, hi);
    if (spans.empty()) return;
    bool closed = sp.period_v() > 0.0 && hi - lo >= sp.period_v() * (1.0 - 1e-12);
    // A circle of the coordinate's extremes (w the axis): a point of it in the face
    auto circle = [&](double v) {
        if (whole) {
            if (v >= lo - tiny && v <= hi + tiny) add_local(sp.point(turn0, v));
            return;
        }
        UvPoint q;
        if (region.line(v, 0.0, kTwoPi, q)) add_local(sp.point(q.u, q.v));
    };
    // Across the turns at the profile's point r (relative to A), parameter v
    auto across = [&](const Point3& w, const Point3& r, double v) {
        Point3 g = cross(w, z);
        Point3 b = cross(z, g);
        double u = std::atan2(dot(g, r), dot(b, r));
        for (double uu : {u, u + kPi}) {
            if (in_face(uu, v)) add_local(add(A, rotated(z, uu, r)));
        }
    };
    double size = scale_ + norm(A);
    for (const auto& w : directions_) {
        if (!(norm(w) > 0.0)) continue;
        Point3 g = cross(w, z);
        Point3 b = cross(z, g);
        bool along_axis = norm(g) <= 1e-14 * norm(w);
        for (size_t k = 0; k < spans.size(); ++k) {
            const CurveSpan& span = spans[k];
            Poly W, P[3];
            span_polys(span, W, P);
            Poly dW = poly_diff(W);
            // Â = (w · A) W + (w · z)(z · R), R = P - A W
            Poly zr = span_linear(W, P, z, dot(z, A));
            Poly Ah = poly_add(poly_scale(W, dot(w, A)), poly_scale(zr, dot(w, z)));
            Poly L = poly_add(poly_mul(poly_diff(Ah), W), poly_mul(Ah, dW), -1.0);
            std::vector<double> roots;
            if (along_axis) {
                polynomial_roots(L, 0.0, 1.0, roots);
                for (double tau : roots) circle(span_param(c, span, tau));
            } else {
                Poly B = span_linear(W, P, b, dot(b, A));
                Poly C = span_linear(W, P, g, dot(g, A));
                Poly S2 = poly_add(poly_mul(B, B), poly_mul(C, C));
                Poly M = poly_add(poly_mul(poly_add(poly_mul(B, poly_diff(B)), poly_mul(C, poly_diff(C))), W),
                                  poly_mul(S2, dW), -1.0);
                polynomial_roots(poly_add(poly_mul(poly_mul(L, L), S2), poly_mul(M, M), -1.0), 0.0, 1.0, roots);
                for (double tau : roots) across(w, sub(span_point(span, tau), A), span_param(c, span, tau));
            }
            // An inner knot (a closed profile's seam too): its circle may be a crease
            if (k > 0 || closed) {
                Point3 r = sub(span_point(span, 0.0), A);
                if (along_axis) circle(span.t0);
                else across(w, r, span.t0);
            }
        }
    }
    // The poles: where the profile meets the axis — the ends, or an inner knot
    std::vector<double> ends = {lo, hi};
    for (size_t k = 1; k < spans.size(); ++k) ends.push_back(spans[k].t0);
    for (double v : ends) {
        Point3 cp;
        acis_curve_point(c, v, cp);
        Point3 r = sub(cp, A);
        Point3 off = sub(r, mul(z, dot(z, r)));
        if (norm(off) > 1e-12 * (norm(r) + size)) continue;
        Point3 pole = add(A, mul(z, dot(z, r)));
        if (whole) {
            add_local(pole);
            continue;
        }
        for (double side : {1.0, -1.0}) {
            if ((side > 0.0 && v >= hi) || (side < 0.0 && v <= lo)) continue;
            if (region.reaches(v, side)) {
                add_local(pole);
                break;
            }
        }
    }
    if (!whole) return;
    // The range's ends: the end circles (an open profile) and the end profiles (a part of
    // the turn), each exactly
    if (!closed) {
        for (double v : {lo, hi}) {
            Point3 cp;
            acis_curve_point(c, v, cp);
            Point3 r = sub(cp, A);
            Point3 h = mul(z, dot(z, r));
            Point3 off = sub(r, h);
            ellipse_extents(out_, world_.apply(add(A, h)), world_.linear(off), world_.linear(cross(z, off)), turn0,
                            turn0 + turn, all_turns);
        }
    }
    if (!all_turns) {
        for (double angle : {turn0, turn0 + turn}) {
            auto map = [&](const Point3& p) { return world_.apply(add(A, rotated(z, angle, sub(p, A)))); };
            for (const auto& span : spans) {
                int p = static_cast<int>(span.net.size()) - 1;
                std::vector<double> knots(static_cast<size_t>(2 * p + 2), 0.0);
                for (int i = p + 1; i < 2 * p + 2; ++i) knots[static_cast<size_t>(i)] = 1.0;
                std::vector<Point3> points;
                std::vector<double> weights;
                for (const auto& h : span.net) {
                    double ww = h[3] != 0.0 ? h[3] : 1.0;
                    points.push_back({h[0] / ww, h[1] / ww, h[2] / ww});
                    weights.push_back(h[3]);
                }
                nurbs_extents(out_, map, p, knots, points, weights, 0.0, 1.0);
            }
        }
    }
}

// A curve's parameters for a sum: where a world coordinate is extreme along it (a
// spline's by nurbs_extents, its knots among them; an ellipse's where tan t = (w · N) /
// (w · M)), and the range's ends with `ends` (a face without loops, or a closed curve's
// seam)
std::vector<double> sum_curve_params(const AcisCurve& c, double lo, double hi, bool ends, const Point3 directions[3],
                                     const Affine& world) {
    std::vector<double> out;
    switch (c.type) {
        case AcisCurveType::Spline: {
            const AcisSplineCurve& s = c.spline;
            auto map = [&](const Point3& p) { return world.apply(p); };
            Extents scratch;
            std::vector<double> params;
            nurbs_extents(scratch, map, s.degree, s.knots, s.points, s.weights, c.reversed ? -hi : lo,
                          c.reversed ? -lo : hi, &params);
            for (double x : params) out.push_back(c.reversed ? -x : x);
            break;
        }
        case AcisCurveType::Ellipse: {
            Point3 minor = mul(cross(c.axis, c.major), c.ratio);
            for (int i = 0; i < 3; ++i) {
                const Point3& w = directions[i];
                if (!(norm(w) > 0.0)) continue;
                double base = std::atan2(dot(w, minor), dot(w, c.major));
                for (int k = static_cast<int>(std::floor((lo - base) / kPi)); base + k * kPi <= hi; ++k) {
                    double t = base + k * kPi;
                    if (t >= lo) out.push_back(t);
                }
            }
            break;
        }
        default:
            break;
    }
    if (ends) {
        out.push_back(lo);
        out.push_back(hi);
    } else {
        out.erase(std::remove_if(out.begin(), out.end(), [&](double t) { return t <= lo || t >= hi; }), out.end());
    }
    return out;
}

// A sum's extremes: w · S = w · C1(u) + w · C2(v) - w · A separates, so it is extreme
// inside where both curves are extreme in it, and on a knot line of one where the other is
// extreme: every pair of the curves' parameters (their extremes in any coordinate, their
// knots) counts where it is in the face. A face without loops: the ranges' ends too; a
// closed curve's seam always.
void BodyExtents::sum_face(const AcisSurface& s, const SurfaceParam& sp, const FaceRegion& region) {
    bool whole = region.everywhere();
    std::vector<double> us = sum_curve_params(s.curves[0], sp.u_min(), sp.u_max(), whole || sp.period_u() > 0.0,
                                              directions_, world_);
    std::vector<double> vs = sum_curve_params(s.curves[1], sp.v_min(), sp.v_max(), whole || sp.period_v() > 0.0,
                                              directions_, world_);
    for (double u : us) {
        for (double v : vs) {
            if (region.inside(u, v)) add_local(sp.point(u, v));
        }
    }
}

} // namespace

std::vector<std::vector<Point3>> acis_edge_polylines(const std::vector<AcisBody>& bodies) {
    std::vector<std::vector<Point3>> out;
    for (const auto& body : bodies) {
        Affine world;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) world.m[i][j] = body.transform[i][j];
        }
        for (const auto& e : body.edges) {
            if (e.curve < 0 || static_cast<size_t>(e.curve) >= body.curves.size()) continue;
            const AcisCurve& c = body.curves[static_cast<size_t>(e.curve)];
            for (const auto& piece : edge_pieces(e, c)) {
                int segments = 1;
                if (c.type == AcisCurveType::Ellipse) {
                    segments = std::max(1, static_cast<int>(std::ceil(std::fabs(piece.to - piece.from) / (kTwoPi / 64.0) - 1e-9)));
                } else if (c.type == AcisCurveType::Spline) {
                    const auto& k = c.spline.knots;
                    double lo = std::min(piece.from, piece.to), hi = std::max(piece.from, piece.to);
                    int spans = 0;
                    for (size_t i = 0; i + 1 < k.size(); ++i) {
                        if (k[i + 1] > k[i] && k[i + 1] > lo && k[i] < hi) ++spans;
                    }
                    segments = 8 * std::max(1, spans);
                }
                std::vector<Point3> line;
                for (int i = 0; i <= segments; ++i) {
                    double t = piece.from + (piece.to - piece.from) * i / segments;
                    line.push_back(world.apply(edge_point(e, c, t)));
                }
                out.push_back(line);
            }
        }
    }
    return out;
}

void acis_extents(const std::vector<AcisBody>& bodies, const AcisMap& map, Extents& out) {
    for (const auto& body : bodies) {
        Affine world = compose(map.m, body.transform);
        BodyExtents(body, world, out).run();
    }
}

void acis_curve_eval(const AcisCurve& c, double t, Point3& p, Point3* d1, Point3* d2) {
    acis_curve_point(c, t, p, d1, d2);
}

void acis_spline_point(const AcisSplineCurve& c, double t, Point3& p, Point3* d1) {
    curve_eval(c, std::clamp(t, curve_lo(c), curve_hi(c)), p, d1);
}

bool acis_surface_point(const AcisSurface& s, double u, double v, Point3& p, Point3* du, Point3* dv) {
    Point3 su{0.0, 0.0, 0.0}, sv{0.0, 0.0, 0.0};
    switch (s.type) {
        case AcisSurfaceType::Plane: {
            double sign = s.reverse_v ? -1.0 : 1.0;
            Point3 across = mul(cross(s.axis, s.u_direction), sign);
            p = add(add(s.origin, mul(s.u_direction, u)), mul(across, v));
            su = s.u_direction;
            sv = across;
            break;
        }
        case AcisSurfaceType::Cone: {
            // The base ellipse at v, then along the cone's line through it: u · scale, the line
            // cos · axis + sin · (the radius over the major axis' length)
            double major = norm(s.major);
            if (!(major > 0.0)) return false;
            double sign = s.reverse_u ? -1.0 : 1.0;
            double scale = s.u_scale != 0.0 ? s.u_scale : major;
            Point3 minor = mul(cross(s.axis, s.major), s.ratio);
            Point3 radius = add(mul(s.major, std::cos(v)), mul(minor, std::sin(v)));
            Point3 radius_v = add(mul(s.major, -std::sin(v)), mul(minor, std::cos(v)));
            Point3 line = add(mul(s.axis, s.cosine), mul(radius, s.sine / major));
            double along = sign * u * scale;
            p = add(add(s.origin, radius), mul(line, along));
            su = mul(line, sign * scale);
            sv = add(radius_v, mul(radius_v, s.sine / major * along));
            break;
        }
        case AcisSurfaceType::Sphere: {
            double sign = s.reverse_v ? -1.0 : 1.0;
            double w = sign * v;
            Point3 x = normalized(s.major);
            Point3 y = cross(s.axis, x);
            Point3 around = add(mul(x, std::cos(w)), mul(y, std::sin(w)));
            Point3 around_v = mul(add(mul(x, -std::sin(w)), mul(y, std::cos(w))), sign);
            p = add(s.origin, mul(add(mul(around, std::cos(u)), mul(s.axis, std::sin(u))), s.radius));
            su = mul(add(mul(around, -std::sin(u)), mul(s.axis, std::cos(u))), s.radius);
            sv = mul(around_v, s.radius * std::cos(u));
            break;
        }
        case AcisSurfaceType::Torus: {
            double sign = s.reverse_v ? -1.0 : 1.0;
            double w = sign * v;
            Point3 x = normalized(s.major);
            Point3 y = cross(s.axis, x);
            Point3 around = add(mul(x, std::cos(w)), mul(y, std::sin(w)));
            Point3 around_v = mul(add(mul(x, -std::sin(w)), mul(y, std::cos(w))), sign);
            p = add(add(s.origin, mul(around, s.radius + s.minor * std::cos(u))), mul(s.axis, s.minor * std::sin(u)));
            su = add(mul(around, -s.minor * std::sin(u)), mul(s.axis, s.minor * std::cos(u)));
            sv = mul(around_v, s.radius + s.minor * std::cos(u));
            break;
        }
        case AcisSurfaceType::Spline: {
            if (s.spline.points.empty()) return false;
            Point3 d[6];
            const AcisSplineSurface& sp = s.spline;
            surface_eval(sp, std::clamp(u, sp.knots_u[static_cast<size_t>(sp.degree_u)], sp.knots_u[static_cast<size_t>(sp.count_u)]),
                         std::clamp(v, sp.knots_v[static_cast<size_t>(sp.degree_v)], sp.knots_v[static_cast<size_t>(sp.count_v)]), d);
            p = d[0];
            su = d[1];
            sv = d[2];
            break;
        }
        case AcisSurfaceType::Revolution: {
            if (s.curves.empty()) return false;
            Point3 c, c1;
            acis_curve_point(s.curves[0], u, c, &c1);
            p = add(s.origin, rotated(s.axis, v, sub(c, s.origin)));
            su = rotated(s.axis, v, c1);
            sv = cross(s.axis, sub(p, s.origin));
            break;
        }
        case AcisSurfaceType::Sum: {
            if (s.curves.size() < 2) return false;
            Point3 a, b;
            acis_curve_point(s.curves[0], u, a, &su);
            acis_curve_point(s.curves[1], v, b, &sv);
            p = sub(add(a, b), s.origin);
            break;
        }
        default:
            return false;
    }
    if (du) *du = su;
    if (dv) *dv = sv;
    return true;
}

double acis_curve_parameter(const AcisCurve& c, const Point3& p) {
    switch (c.type) {
        case AcisCurveType::Straight: {
            double l = dot(c.direction, c.direction);
            return l > 0.0 ? dot(sub(p, c.origin), c.direction) / l : 0.0;
        }
        case AcisCurveType::Ellipse:
            return ellipse_angle(c, p);
        case AcisCurveType::Spline: {
            double t = curve_param(c.spline, p);
            return c.reversed ? -t : t;
        }
        default:
            return 0.0;
    }
}

} // namespace internal
} // namespace hdxf
