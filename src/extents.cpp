#include "extents.hpp"
#include "mesh.hpp"
#include "mleader_graphics.hpp"
#include "acis.hpp"
#include "dxf_writer.hpp"
#include "handles.hpp"
#include "text_layout.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hdxf {
namespace internal {

void Extents::add(const Point3& p) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return;
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);
}

namespace {

// An affine map: WCS = m · (x, y, z, 1)
struct Xform {
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
    // this ∘ o
    Xform then(const Xform& o) const {
        Xform r;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) {
                double s = (j == 3) ? m[i][3] : 0.0;
                for (int k = 0; k < 3; ++k) s += m[i][k] * o.m[k][j];
                r.m[i][j] = s;
            }
        }
        return r;
    }
};

Xform translate(const Point3& d) {
    Xform t;
    t.m[0][3] = d.x;
    t.m[1][3] = d.y;
    t.m[2][3] = d.z;
    return t;
}

Xform scale(double sx, double sy, double sz) {
    Xform t;
    t.m[0][0] = sx;
    t.m[1][1] = sy;
    t.m[2][2] = sz;
    return t;
}

Xform rotate_z(double degrees) {
    double a = degrees * M_PI / 180.0;
    Xform t;
    t.m[0][0] = std::cos(a);
    t.m[0][1] = -std::sin(a);
    t.m[1][0] = std::sin(a);
    t.m[1][1] = std::cos(a);
    return t;
}

// OCS → WCS for an extrusion direction (the arbitrary axis algorithm)
Xform ocs(const Point3& extrusion) {
    if (extrusion.x == 0.0 && extrusion.y == 0.0 && extrusion.z == 1.0) return Xform{};
    Point3 ax, ay;
    ecs::arbitrary_axis(extrusion, ax, ay);
    double len = std::sqrt(extrusion.x * extrusion.x + extrusion.y * extrusion.y +
                           extrusion.z * extrusion.z);
    Point3 az = len > 0.0 ? Point3{extrusion.x / len, extrusion.y / len, extrusion.z / len}
                          : Point3{0.0, 0.0, 1.0};
    Xform t;
    t.m[0][0] = ax.x; t.m[0][1] = ay.x; t.m[0][2] = az.x;
    t.m[1][0] = ax.y; t.m[1][1] = ay.y; t.m[1][2] = az.y;
    t.m[2][0] = ax.z; t.m[2][1] = ay.z; t.m[2][2] = az.z;
    return t;
}

double coord(const Point3& p, int i) { return i == 0 ? p.x : (i == 1 ? p.y : p.z); }

// The curve c + u·cos a + v·sin a (WCS) from a0 to a1 radians: its end points and every
// angle where a coordinate has an extreme. An arc, or an ellipse by its parameter.
void add_curve(Extents& out, const Point3& c, const Point3& u, const Point3& v, double a0,
               double a1, bool full) {
    auto at = [&](double a) {
        return Point3{c.x + u.x * std::cos(a) + v.x * std::sin(a),
                      c.y + u.y * std::cos(a) + v.y * std::sin(a),
                      c.z + u.z * std::cos(a) + v.z * std::sin(a)};
    };
    double two_pi = 2.0 * M_PI;
    double sweep = full ? two_pi : std::fmod(std::fmod(a1 - a0, two_pi) + two_pi, two_pi);
    if (!full && sweep == 0.0) sweep = two_pi;
    out.add(at(a0));
    out.add(at(a0 + sweep));
    for (int i = 0; i < 3; ++i) {
        double base = std::atan2(coord(v, i), coord(u, i));
        for (double extreme : {base, base + M_PI}) {
            double rel = std::fmod(std::fmod(extreme - a0, two_pi) + two_pi, two_pi);
            if (rel <= sweep) out.add(at(a0 + rel));
        }
    }
}

// ---------------------------------------------------------------------------
// NURBS curves: the exact extents (rättningsfas R.6)
// ---------------------------------------------------------------------------

// A polynomial a0 + a1 t + a2 t^2 …
using Poly = std::vector<double>;

double poly_eval(const Poly& a, double t) {
    double v = 0.0;
    for (size_t i = a.size(); i-- > 0;) v = v * t + a[i];
    return v;
}

Poly poly_derivative(const Poly& a) {
    Poly d;
    for (size_t i = 1; i < a.size(); ++i) d.push_back(a[i] * static_cast<double>(i));
    return d;
}

Poly poly_multiply(const Poly& a, const Poly& b) {
    if (a.empty() || b.empty()) return {};
    Poly c(a.size() + b.size() - 1, 0.0);
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b.size(); ++j) c[i + j] += a[i] * b[j];
    }
    return c;
}

// Without the high coefficients that are rounding noise against the largest one
void poly_trim(Poly& a) {
    double big = 0.0;
    for (double c : a) big = std::max(big, std::fabs(c));
    while (!a.empty() && std::fabs(a.back()) <= big * 1e-13) a.pop_back();
}

// The real roots of a in (lo, hi): between the roots of its derivative it is monotone, so
// each sign change there is one root, found by bisection to the last bit
void poly_roots(Poly a, double lo, double hi, std::vector<double>& out) {
    poly_trim(a);
    if (a.size() < 2) return;
    if (a.size() == 2) {
        double r = -a[0] / a[1];
        if (r > lo && r < hi) out.push_back(r);
        return;
    }
    std::vector<double> splits;
    poly_roots(poly_derivative(a), lo, hi, splits);
    std::sort(splits.begin(), splits.end());
    std::vector<double> at = {lo};
    at.insert(at.end(), splits.begin(), splits.end());
    at.push_back(hi);
    for (size_t i = 0; i + 1 < at.size(); ++i) {
        double x0 = at[i], x1 = at[i + 1];
        double f0 = poly_eval(a, x0), f1 = poly_eval(a, x1);
        if (f0 == 0.0) {
            if (x0 > lo) out.push_back(x0);
            continue;
        }
        if ((f0 < 0.0) == (f1 < 0.0)) continue;
        for (int k = 0; k < 200; ++k) {
            double m = 0.5 * (x0 + x1);
            if (m <= x0 || m >= x1) break;
            double fm = poly_eval(a, m);
            if ((fm < 0.0) == (f0 < 0.0)) { x0 = m; f0 = fm; } else { x1 = m; }
        }
        out.push_back(0.5 * (x0 + x1));
    }
}

// Bernstein coefficients q0 … qp → the power basis on [0, 1]
Poly bernstein_to_power(const std::vector<double>& q) {
    size_t p = q.size() - 1;
    auto binomial = [](size_t n, size_t k) {
        double r = 1.0;
        for (size_t i = 1; i <= k; ++i) r = r * static_cast<double>(n - k + i) / static_cast<double>(i);
        return r;
    };
    Poly a(p + 1, 0.0);
    for (size_t k = 0; k <= p; ++k) {
        double sum = 0.0;
        for (size_t i = 0; i <= k; ++i) sum += ((k - i) % 2 ? -1.0 : 1.0) * binomial(k, i) * q[i];
        a[k] = binomial(p, k) * sum;
    }
    return a;
}

// A NURBS curve of degree p (knots, control points, weights — empty = 1), mapped by an
// affine map: exactly. The map goes on the control points (a NURBS curve is affinely
// invariant); each knot span is made a rational Bézier segment (its control points are
// blossom values: de Boor's algorithm with a parameter per level), and per axis the extremes
// are where the derivative's numerator A'W - AW' is 0, besides the ends. Without a valid
// knot vector, the control points (the curve lies in their convex hull). Only the part
// from lo to hi (an ACIS edge, R.9); the parameters of the points taken go in `params`.
void add_nurbs(Extents& out, const std::function<Point3(const Point3&)>& map, int p,
               const std::vector<double>& knots, const std::vector<Point3>& points,
               const std::vector<double>& weights,
               double lo = -std::numeric_limits<double>::infinity(),
               double hi = std::numeric_limits<double>::infinity(),
               std::vector<double>* params = nullptr) {
    size_t n = points.size();
    std::vector<Point3> mapped;
    for (const auto& c : points) mapped.push_back(map(c));
    if (p < 1 || n < static_cast<size_t>(p) + 1 || knots.size() != n + p + 1) {
        for (const auto& c : mapped) out.add(c);
        return;
    }
    auto weight = [&](size_t i) { return i < weights.size() ? weights[i] : 1.0; };
    using H = std::array<double, 4>;                    // w x, w y, w z, w
    for (size_t k = p; k < n; ++k) {
        double a = std::max(knots[k], lo);
        double b = std::min(knots[k + 1], hi);
        if (b <= a) continue;
        // The segment's Bézier points: the blossom at (a … a, b … b)
        std::vector<H> bez(p + 1);
        for (int j = 0; j <= p; ++j) {
            std::vector<H> d(p + 1);
            for (int i = 0; i <= p; ++i) {
                const Point3& c = mapped[k - p + i];
                double w = weight(k - p + i);
                d[i] = {c.x * w, c.y * w, c.z * w, w};
            }
            for (int r = 1; r <= p; ++r) {
                double t = r <= p - j ? a : b;
                for (int i = p; i >= r; --i) {
                    size_t g = k - p + i;
                    double denom = knots[g + p - r + 1] - knots[g];
                    double alpha = denom == 0.0 ? 0.0 : (t - knots[g]) / denom;
                    for (int c = 0; c < 4; ++c) d[i][c] = (1.0 - alpha) * d[i - 1][c] + alpha * d[i][c];
                }
            }
            bez[j] = d[p];
        }
        std::vector<double> qw(p + 1);
        for (int j = 0; j <= p; ++j) qw[j] = bez[j][3];
        Poly W = bernstein_to_power(qw);
        Poly A[3];
        for (int c = 0; c < 3; ++c) {
            std::vector<double> q(p + 1);
            for (int j = 0; j <= p; ++j) q[j] = bez[j][c];
            A[c] = bernstein_to_power(q);
        }
        // A point by de Casteljau's algorithm on the homogeneous Bézier points — stable for
        // any weights (the power basis above is only for finding the roots)
        auto at = [&](double t) {
            std::vector<H> q = bez;
            for (int r = 1; r <= p; ++r) {
                for (int j = 0; j <= p - r; ++j) {
                    for (int c = 0; c < 4; ++c) q[j][c] = (1.0 - t) * q[j][c] + t * q[j + 1][c];
                }
            }
            double w = q[0][3] != 0.0 ? q[0][3] : 1.0;
            return Point3{q[0][0] / w, q[0][1] / w, q[0][2] / w};
        };
        auto take = [&](double t) {
            out.add(at(t));
            if (params) params->push_back(a + t * (b - a));
        };
        take(0.0);
        take(1.0);
        Poly dW = poly_derivative(W);
        for (int c = 0; c < 3; ++c) {
            Poly g = poly_multiply(poly_derivative(A[c]), W);
            Poly h = poly_multiply(A[c], dW);
            g.resize(std::max(g.size(), h.size()), 0.0);
            for (size_t i = 0; i < h.size(); ++i) g[i] -= h[i];
            std::vector<double> roots;
            poly_roots(g, 0.0, 1.0, roots);
            for (double t : roots) take(t);
        }
    }
}

// A cubic spline through fit points, as CAD programs make one (SPLINE fit with the default
// knot parameterization "Chord"): parameters by chord length, the knots at the parameters, a
// given end tangent as the derivative at that end (its length the chord length, as the
// parameter runs 0 … 1), and an end without one natural (second derivative 0). The control
// points solve the banded system C(u_k) = Q_k with the two end conditions. (AutoCAD's own end
// conditions are not in the reference: checked when AutoCAD is installed.)
void interpolate_fit(const std::vector<Point3>& fit, const Point3& start_tangent,
                     const Point3& end_tangent, std::vector<double>& knots,
                     std::vector<Point3>& control) {
    knots.clear();
    control.clear();
    size_t m = fit.size();
    auto length = [](const Point3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); };
    auto sub = [](const Point3& a, const Point3& b) { return Point3{a.x - b.x, a.y - b.y, a.z - b.z}; };
    double total = 0.0;
    for (size_t i = 1; i < m; ++i) total += length(sub(fit[i], fit[i - 1]));
    const int p = 3;
    if (m < 2 || total == 0.0) {
        control = fit;
        return;
    }
    std::vector<double> u(m, 0.0);
    for (size_t i = 1; i < m; ++i) u[i] = u[i - 1] + length(sub(fit[i], fit[i - 1])) / total;
    u[m - 1] = 1.0;
    // Knots 0 ×4, every interior parameter, 1 ×4: m + 2 control points
    for (int i = 0; i <= p; ++i) knots.push_back(0.0);
    knots.insert(knots.end(), u.begin() + 1, u.end() - 1);
    for (int i = 0; i <= p; ++i) knots.push_back(1.0);
    size_t n = m + 2;

    // A sparse row: its first column and its values
    struct Row {
        size_t start = 0;
        std::vector<double> v;
        double at(size_t c) const { return c >= start && c < start + v.size() ? v[c - start] : 0.0; }
    };
    std::vector<Row> rows;
    std::vector<std::array<double, 3>> rhs;
    auto add_row = [&](size_t start, std::vector<double> values, const Point3& r) {
        rows.push_back({start, std::move(values)});
        rhs.push_back({r.x, r.y, r.z});
    };
    // The cubic basis functions at t: span s, N[s-3 … s]
    auto basis = [&](double t, size_t& first) {
        size_t span = p;
        while (span + 1 < n && knots[span + 1] <= t) ++span;
        std::vector<double> left(p + 1), right(p + 1), b(p + 1, 0.0);
        b[0] = 1.0;
        for (int j = 1; j <= p; ++j) {
            left[j] = t - knots[span + 1 - j];
            right[j] = knots[span + j] - t;
            double saved = 0.0;
            for (int r = 0; r < j; ++r) {
                double tmp = b[r] / (right[r + 1] + left[j - r]);
                b[r] = saved + right[r + 1] * tmp;
                saved = left[j - r] * tmp;
            }
            b[j] = saved;
        }
        first = span - p;
        return b;
    };
    // The end conditions on the first three / last three control points
    auto start_row = [&]() {
        if (length(start_tangent) > 0.0) {
            // C'(0) = p / u[1] (P1 - P0)  (knots[4] = u[1])
            double f = p / knots[p + 1];
            double s = total / length(start_tangent);
            add_row(0, {-f, f}, {start_tangent.x * s, start_tangent.y * s, start_tangent.z * s});
        } else {
            // C''(0) = p (p-1) / u[1] · ((P2 - P1) / knots[5] - (P1 - P0) / knots[4]) = 0
            double a = 1.0 / knots[p + 1], b = 1.0 / knots[p + 2];
            add_row(0, {a, -a - b, b}, {0.0, 0.0, 0.0});
        }
    };
    auto end_row = [&]() {
        double k1 = 1.0 - knots[n - 1];                 // 1 - u[m-2]
        double k2 = 1.0 - knots[n - 2];
        if (length(end_tangent) > 0.0) {
            double f = p / k1;
            double s = total / length(end_tangent);
            add_row(n - 2, {-f, f}, {end_tangent.x * s, end_tangent.y * s, end_tangent.z * s});
        } else {
            double a = 1.0 / k1, b = 1.0 / k2;
            add_row(n - 3, {b, -a - b, a}, {0.0, 0.0, 0.0});
        }
    };
    // In column order, so the matrix is banded: C(0), the start condition, the interior
    // points, the end condition, C(1)
    add_row(0, {1.0}, fit[0]);
    start_row();
    for (size_t i = 1; i + 1 < m; ++i) {
        size_t first = 0;
        std::vector<double> b = basis(u[i], first);
        add_row(first, b, fit[i]);
    }
    end_row();
    add_row(n - 1, {1.0}, fit[m - 1]);

    // Gaussian elimination with partial pivoting among the few rows that reach the column
    // (the band), so time and memory stay linear in the number of fit points
    for (size_t col = 0; col < n; ++col) {
        size_t piv = col;
        for (size_t r = col + 1; r < n && rows[r].start <= col; ++r) {
            if (std::fabs(rows[r].at(col)) > std::fabs(rows[piv].at(col))) piv = r;
        }
        std::swap(rows[col], rows[piv]);
        std::swap(rhs[col], rhs[piv]);
        double d = rows[col].at(col);
        if (d == 0.0) {
            knots.clear();
            control = fit;
            return;
        }
        for (size_t r = col + 1; r < n && rows[r].start <= col; ++r) {
            double f = rows[r].at(col) / d;
            if (f == 0.0) continue;
            // rows[r] -= f · rows[col], from col on
            size_t end = std::max(rows[r].start + rows[r].v.size(), rows[col].start + rows[col].v.size());
            std::vector<double> v(end - col, 0.0);
            for (size_t c = col; c < end; ++c) v[c - col] = rows[r].at(c) - f * rows[col].at(c);
            rows[r].start = col + 1;
            rows[r].v.assign(v.begin() + 1, v.end());
            for (int c = 0; c < 3; ++c) rhs[r][c] -= f * rhs[col][c];
        }
        // Keep the band sorted: rows reaching the next column first
        for (size_t r = col + 1; r + 1 < n && r <= col + 4; ++r) {
            for (size_t q = r + 1; q < n && q <= col + 4; ++q) {
                if (rows[q].start < rows[r].start) {
                    std::swap(rows[q], rows[r]);
                    std::swap(rhs[q], rhs[r]);
                }
            }
        }
    }
    control.assign(n, Point3{});
    for (size_t r = n; r-- > 0;) {
        std::array<double, 3> v = rhs[r];
        size_t end = rows[r].start + rows[r].v.size();
        for (size_t c = r + 1; c < end && c < n; ++c) {
            double a = rows[r].at(c);
            v[0] -= a * control[c].x;
            v[1] -= a * control[c].y;
            v[2] -= a * control[c].z;
        }
        double d = rows[r].at(r);
        control[r] = {v[0] / d, v[1] / d, v[2] / d};
    }
}

// The arc of the circle (center, r) in the OCS plane z = center.z, from a0 to a1 radians
// counterclockwise, mapped by t
void add_arc(Extents& out, const Xform& t, const Point3& center, double r, double a0,
             double a1, bool full) {
    add_curve(out, t.apply(center), t.linear({r, 0.0, 0.0}), t.linear({0.0, r, 0.0}), a0, a1,
              full);
}

// A box in the plane of MTEXT or TOLERANCE (WCS): x along its direction, y the normal × x,
// from its insertion point
void add_plane_box(Extents& out, const Xform& t, const Point3& q, Point3 u, Point3 n,
                   const TextBox& box) {
    double ln = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    n = ln > 0.0 ? Point3{n.x / ln, n.y / ln, n.z / ln} : Point3{0.0, 0.0, 1.0};
    double along = u.x * n.x + u.y * n.y + u.z * n.z;         // into the plane
    u = {u.x - n.x * along, u.y - n.y * along, u.z - n.z * along};
    double lu = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
    if (lu == 0.0) {
        Point3 ay;
        ecs::arbitrary_axis(n, u, ay);
        lu = 1.0;
    }
    u = {u.x / lu, u.y / lu, u.z / lu};
    Point3 v = {n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};
    for (int c = 0; c < 4; ++c) {
        double x = (c & 1) ? box.max_x : box.min_x;
        double y = (c & 2) ? box.max_y : box.min_y;
        out.add(t.apply({q.x + u.x * x + v.x * y, q.y + u.y * x + v.y * y, q.z + u.z * x + v.z * y}));
    }
}

// ---------------------------------------------------------------------------
// The representation at the current annotation scale (9.S, Codex 17; plan 9.17)
// ---------------------------------------------------------------------------

// An entry of a dictionary by its name; null when there is none
const Object* dictionary_entry(const Drawing& dwg, const std::string& dictionary, const char* name) {
    const Object* d = dictionary.empty() ? nullptr : dwg.find_object(dictionary);
    if (!d || d->type != ObjectType::Dictionary) return nullptr;
    for (const auto& entry : d->as_dictionary().entries) {
        if (entry.name == name) return dwg.find_object(entry.handle);
    }
    return nullptr;
}

// The current annotation scale of model space (CANNOSCALE): the SCALE whose name the DICTIONARYVAR CANNOSCALE of the root
// dictionary's AcDbVariableDictionary holds (AutoCAD 2025's files: "1:1", "HDXF_1_2", "HDXF_1_10"); null when there is none
const Object* current_annotation_scale(const Drawing& dwg) {
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::Dictionary || o.owner != "0") continue;
        const Object* vars = dictionary_entry(dwg, o.handle, "AcDbVariableDictionary");
        const Object* var = vars ? dictionary_entry(dwg, vars->handle, "CANNOSCALE") : nullptr;
        if (!var || var->type != ObjectType::DictionaryVar) return nullptr;
        const Object* scale = dwg.find_scale(var->as_dictionary_var().value);
        return scale && scale->type == ObjectType::Scale ? scale : nullptr;
    }
    return nullptr;
}

// The contexts of an entity by its extension dictionary: AcDbContextDataManager → ACDB_ANNOTATIONSCALES (→ ACDB_ANNOTATIONSCALES)
// → the *A<n> entries (object_context.cpp reads the same chain; an ATTRIB is not in Drawing::find_entity, so the chain is
// followed from the dictionary here)
std::vector<const Object*> contexts_of(const Drawing& dwg, const std::string& xdictionary) {
    std::vector<const Object*> out;
    const Object* manager = dictionary_entry(dwg, xdictionary, "AcDbContextDataManager");
    const Object* scales = manager ? dictionary_entry(dwg, manager->handle, "ACDB_ANNOTATIONSCALES") : nullptr;
    const Object* list = scales ? dictionary_entry(dwg, scales->handle, "ACDB_ANNOTATIONSCALES") : nullptr;
    const Object* holder = list ? list : scales;
    if (!holder || holder->type != ObjectType::Dictionary) return out;
    for (const auto& entry : holder->as_dictionary().entries) {
        const Object* o = dwg.find_object(entry.handle);
        if (o && o->context_base()) out.push_back(o);
    }
    return out;
}

bool handles_equal(const std::string& a, const std::string& b) {
    uint64_t x = 0, y = 0;
    return parse_handle(a, x) == HandleParse::Ok && parse_handle(b, y) == HandleParse::Ok && x == y;
}

// Drawing units per paper unit of a SCALE (141 / 140): what an annotative object's paper size is multiplied by
double scale_factor(const Drawing& dwg, const std::string& scale) {
    const Object* s = dwg.find_scale(scale);
    if (!s || s->type != ObjectType::Scale) return 0.0;
    const ScaleData& d = s->as_scale();
    return d.paper_units > 0.0 ? d.drawing_units / d.paper_units : 0.0;
}

// The context of an entity (by its extension dictionary) at the scale `current`, and the factor from its own representation (the
// default context's: an annotative entity's fields are the default context's in AutoCAD's files) to that one. Null when the
// entity has none at that scale, or when that is its default (unless `with_default`: then the default too, factor 1)
const Object* context_at(const Drawing& dwg, const std::string& xdictionary, const Object& current, double& factor,
                         bool with_default = false) {
    if (xdictionary.empty()) return nullptr;
    const Object* at = nullptr;
    const Object* own = nullptr;
    for (const Object* o : contexts_of(dwg, xdictionary)) {
        const ObjectContextBase* b = o->context_base();
        if (b->is_default) own = o;
        if (handles_equal(b->scale, current.handle)) at = o;
    }
    if (at && at == own && with_default) {
        factor = 1.0;
        return at;
    }
    if (!at || !own || at == own) return nullptr;
    const double from = scale_factor(dwg, own->context_base()->scale);
    const double to = scale_factor(dwg, at->context_base()->scale);
    if (from <= 0.0 || to <= 0.0) return nullptr;
    factor = to / from;
    return at;
}

// A text's place and turn at a scale (a TEXT context, the text part of an MTEXT attribute's): the point keeps its Z (the context
// has none); the height by the factor. The context's 50 is in the unit of the entity's (AutoCAD's files: 30.00000000000001 for
// a TEXT turned 30 degrees)
void text_at(Point3& position, Point3& alignment, double& height, double& rotation, const Point2& c_position,
             const Point2& c_alignment, double c_rotation, double factor) {
    height *= factor;
    rotation = c_rotation;
    position = {c_position.x, c_position.y, position.z};
    alignment = {c_alignment.x, c_alignment.y, alignment.z};
}

// An MTEXT's place at a scale (an MTEXT context): its point, direction, attachment (70), widths; the height by the factor
void mtext_at(MTextData& m, const MTextContextData& c, double factor) {
    m.height *= factor;
    m.insertion_point = c.location;
    m.x_axis_direction = c.direction;
    m.attachment = c.flags;
    m.reference_width = c.reference_width;
    m.defined_height = c.defined_height;
}

// An ATTRIB of an annotative INSERT at the scale: by its own context, a TEXT context or an MTEXT attribute's — also its default
// one: before 2018 a multiline attribute's MTEXT is in the XRECORD ACAD_MLATT, which AutoCAD leaves where another scale had it
// in the copies of a decomposed annotative block (Erik's annotative 2000 … 2013: 10 at y 260, the context at 227, AutoCAD's
// extents by the context). True when it has one
bool attribute_at(const Drawing& dwg, InsertAttribute& a, const Object& current) {
    double factor = 1.0;
    const Object* c = context_at(dwg, a.props.xdictionary, current, factor, true);
    if (!c) return false;
    AttribData& d = a.data;
    if (const auto* t = c->data.get_if<Boxed<TextContextData>>()) {
        text_at(d.start_point, d.alignment_point, d.height, d.rotation, (**t).insertion, (**t).alignment, (**t).rotation, factor);
    } else if (const auto* m = c->data.get_if<Boxed<MTextAttributeContextData>>()) {
        text_at(d.start_point, d.alignment_point, d.height, d.rotation, (**m).insertion, (**m).alignment, (**m).rotation, factor);
        if (d.multiline && d.multiline->has_text && (**m).embedded) mtext_at(d.multiline->text, *(**m).embedded, factor);
    } else {
        return false;
    }
    return true;
}

// 2000, 2004 and 2007 have no annotative objects: AutoCAD writes each as an INSERT of a *U block (XDATA
// AcadAnnotativeDecomposition, or AcadAnnotativeMTextDecomposition for an MTEXT) holding one copy per scale, each on a layer
// "<layer> @ <n>" whose extension dictionary's XRECORD ASDK_XREC_ANNO_SCALE_INFO names the scale (its first 340). AutoCAD 2025's
// $EXTMIN/$EXTMAX of such a file count the copy of the current scale only (fas9_cannoscale: 1:1, 1:2, 1:5 in 2000 … 2007)
bool is_annotative_decomposition(const Entity& e) {
    if (e.type != EntityType::Insert) return false;
    for (const auto& x : e.props.xdata) {
        if (x.app_name == "AcadAnnotativeDecomposition" || x.app_name == "AcadAnnotativeMTextDecomposition") return true;
    }
    return false;
}

// The scale a decomposition layer stands for (its SCALE's handle); empty for any other layer
std::string decomposition_layer_scale(const Drawing& dwg, const std::string& layer) {
    for (const auto& l : dwg.layers()) {
        if (!names_equal(l.name, layer)) continue;
        const Object* info = dictionary_entry(dwg, l.xdictionary, "ASDK_XREC_ANNO_SCALE_INFO");
        if (!info || info->type != ObjectType::XRecord) return {};
        for (const auto& g : info->as_xrecord().data) {
            if (g.code == 340) return g.value;
        }
        return {};
    }
    return {};
}

// The entity as it is drawn at the scale `current`, when it is annotative and has a context there other than its default: TEXT,
// MTEXT, LEADER, the dimensions (their block at that scale), TOLERANCE (its box by the factor: `box_factor`), INSERT (and its
// attributes), MULTILEADER. A hatch's context changes its pattern only, not its boundary: its extents stay. False otherwise
bool at_annotation_scale(const Drawing& dwg, const Entity& e, const Object& current, Entity& rep, double& box_factor) {
    if (e.props.paper_space) return false;      // paper space is drawn at 1:1 (its viewports have their own scales)
    double factor = 1.0;
    const Object* c = context_at(dwg, e.props.xdictionary, current, factor);
    if (!c && e.type == EntityType::Insert && !e.as_insert().attributes.empty()) {
        // The INSERT at its own scale: its attributes still by theirs
        rep = e;
        box_factor = 1.0;
        bool any = false;
        for (auto& a : rep.as_insert().attributes) any = attribute_at(dwg, a, current) || any;
        return any;
    }
    if (!c) return false;
    rep = e;
    box_factor = 1.0;
    const DimContextCommon* dim = nullptr;
    switch (c->type) {
        case ObjectType::TextContext: {
            if (e.type != EntityType::Text) return false;
            const TextContextData& t = **c->data.get_if<Boxed<TextContextData>>();
            TextData& d = rep.as_text();
            text_at(d.position, d.alignment_point, d.height, d.rotation, t.insertion, t.alignment, t.rotation, factor);
            return true;
        }
        case ObjectType::MTextContext:
            if (e.type != EntityType::MText) return false;
            mtext_at(rep.as_mtext(), **c->data.get_if<Boxed<MTextContextData>>(), factor);
            return true;
        case ObjectType::LeaderContext: {
            if (e.type != EntityType::Leader) return false;
            const LeaderContextData& l = **c->data.get_if<Boxed<LeaderContextData>>();
            if (!l.points.empty()) rep.as_leader().vertices = l.points;
            return true;
        }
        case ObjectType::FcfContext: {
            if (e.type != EntityType::Tolerance) return false;
            const FcfContextData& f = **c->data.get_if<Boxed<FcfContextData>>();
            rep.as_tolerance().insertion_point = f.location;
            rep.as_tolerance().x_axis_direction = f.direction;
            box_factor = factor;
            return true;
        }
        case ObjectType::BlkRefContext: {
            if (e.type != EntityType::Insert) return false;
            const BlkRefContextData& b = **c->data.get_if<Boxed<BlkRefContextData>>();
            InsertData& d = rep.as_insert();
            d.insertion_point = b.insertion;
            d.rotation = b.rotation;
            d.x_scale = b.scale.x;
            d.y_scale = b.scale.y;
            d.z_scale = b.scale.z;
            for (auto& a : d.attributes) attribute_at(dwg, a, current);
            return true;
        }
        case ObjectType::MLeaderContext: {
            if (e.type != EntityType::MLeader) return false;
            MLeaderContext content = (**c->data.get_if<Boxed<MLeaderContextData>>()).content;
            // The context names its text style and block by handle, the entity by name
            for (const auto& s : dwg.text_styles()) {
                if (!content.text_style.empty() && handles_equal(s.handle, content.text_style)) content.text_style = s.name;
            }
            for (const auto& b : dwg.blocks()) {
                if (!content.block.empty() && handles_equal(b.record.handle, content.block)) content.block = b.name;
            }
            rep.as_mleader().context = std::move(content);
            return true;
        }
        case ObjectType::AlDimContext: dim = &(**c->data.get_if<Boxed<AlDimContextData>>()).dimension; break;
        case ObjectType::AngDimContext: dim = &(**c->data.get_if<Boxed<AngDimContextData>>()).dimension; break;
        case ObjectType::DmDimContext: dim = &(**c->data.get_if<Boxed<DmDimContextData>>()).dimension; break;
        case ObjectType::OrdDimContext: dim = &(**c->data.get_if<Boxed<OrdDimContextData>>()).dimension; break;
        case ObjectType::RadimContext: dim = &(**c->data.get_if<Boxed<RadimContextData>>()).dimension; break;
        case ObjectType::RadimLgContext: dim = &(**c->data.get_if<Boxed<RadimLgContextData>>()).dimension; break;
        default:
            return false;
    }
    // A dimension at the scale is its block there (AutoCAD builds one *D block per scale)
    DimensionData* d = rep.dimension_part();
    if (!d || !dim || dim->block_name.empty()) return false;
    d->block_name = dim->block_name;
    return true;
}

class ExtentsBuilder {
public:
    ExtentsBuilder(const Drawing& dwg, const std::vector<Block>& generated,
                   const std::unordered_map<const Entity*, std::string>& dim_block_names,
                   const std::vector<Block>* replaced = nullptr)
        : dwg_(dwg), generated_(generated), dim_block_names_(dim_block_names), replaced_(replaced), text_(dwg),
          acis_(dwg), annotation_scale_(current_annotation_scale(dwg)) {}

    void entity(const Entity& e, const Xform& t, bool in_block, Extents& out);
    const std::vector<std::string>& warnings() const { return text_.warnings(); }

private:
    const Block* find(const std::string& name) const;
    const Extents& block_extents(const Block& b);
    void add_box(const Extents& box, const Xform& t, Extents& out);

    const Drawing& dwg_;
    const std::vector<Block>& generated_;
    const std::unordered_map<const Entity*, std::string>& dim_block_names_;
    const std::vector<Block>* replaced_;
    std::unordered_map<const Block*, Extents> cache_;
    std::unordered_map<const Block*, bool> busy_;     // a block that inserts itself
    TextLayout text_;
    AcisSource acis_;
    // 9.S (Codex 17): the current annotation scale (CANNOSCALE), and while an annotative entity's representation at it is
    // counted, the factor its TOLERANCE box is drawn by
    const Object* annotation_scale_ = nullptr;
    bool representing_ = false;
    double box_factor_ = 1.0;
};

const Block* ExtentsBuilder::find(const std::string& name) const {
    if (replaced_) {
        for (const auto& b : *replaced_) {
            if (names_equal(b.name, name)) return &b;
        }
    }
    if (const Block* b = dwg_.find_block(name)) return b;
    for (const auto& b : generated_) {
        if (names_equal(b.name, name)) return &b;
    }
    return nullptr;
}

// A block's extents in its own coordinates, relative to its base point. ATTDEF is not
// part of an insertion (its ATTRIB is).
const Extents& ExtentsBuilder::block_extents(const Block& b) {
    auto it = cache_.find(&b);
    if (it != cache_.end()) return it->second;
    Extents box;
    if (!busy_[&b]) {
        busy_[&b] = true;
        Xform base = translate({-b.base_point.x, -b.base_point.y, -b.base_point.z});
        for (const auto& e : b.entities) entity(e, base, true, box);
        busy_[&b] = false;
    }
    return cache_[&b] = box;
}

void ExtentsBuilder::add_box(const Extents& box, const Xform& t, Extents& out) {
    if (box.empty()) return;
    for (int i = 0; i < 8; ++i) {
        out.add(t.apply({(i & 1) ? box.max.x : box.min.x, (i & 2) ? box.max.y : box.min.y,
                         (i & 4) ? box.max.z : box.min.z}));
    }
}

void ExtentsBuilder::entity(const Entity& e, const Xform& t, bool in_block, Extents& out) {
    // An annotative entity of model space is drawn at the current annotation scale (CANNOSCALE) by its context for it, when that
    // is not its default (9.S, Codex 17; AutoCAD 2025's $EXTMIN/$EXTMAX of fas9_contexts autoscale and Erik's annotative). One in
    // a block is drawn by the INSERT's scale (not established: ej testat)
    if (annotation_scale_ && !in_block && !representing_) {
        Entity rep;
        double factor = 1.0;
        if (at_annotation_scale(dwg_, e, *annotation_scale_, rep, factor)) {
            representing_ = true;
            box_factor_ = factor;
            entity(rep, t, in_block, out);
            representing_ = false;
            box_factor_ = 1.0;
            return;
        }
    }
    // 2000 … 2007: an annotative object's decomposition counts with its copy at the current scale (a copy on a layer of another
    // scale does not; when no copy has the current scale, all count)
    if (annotation_scale_ && !in_block && is_annotative_decomposition(e)) {
        const InsertData& d = e.as_insert();
        if (const Block* b = find(d.block_name)) {
            std::vector<const Entity*> shown;
            for (const auto& inner : b->entities) {
                const std::string copy_scale = decomposition_layer_scale(dwg_, inner.props.layer);
                if (!copy_scale.empty() && handles_equal(copy_scale, annotation_scale_->handle)) shown.push_back(&inner);
            }
            if (!shown.empty()) {
                for (const auto& inner : b->entities) {
                    if (decomposition_layer_scale(dwg_, inner.props.layer).empty()) shown.push_back(&inner);
                }
                Xform m = t.then(ocs(e.props.extrusion))
                              .then(translate(d.insertion_point))
                              .then(rotate_z(d.rotation))
                              .then(scale(d.x_scale, d.y_scale, d.z_scale))
                              .then(translate({-b->base_point.x, -b->base_point.y, -b->base_point.z}));
                // A copy keeps its contexts: drawn by the one of the current scale (an annotative block's copy has its attributes
                // where AutoCAD left them for every copy, its contexts where the scale puts them: Erik's annotative 2000 … 2013)
                for (const Entity* inner : shown) {
                    Entity rep;
                    double factor = 1.0;
                    if (!representing_ && at_annotation_scale(dwg_, *inner, *annotation_scale_, rep, factor)) {
                        representing_ = true;
                        box_factor_ = factor;
                        entity(rep, m, true, out);
                        representing_ = false;
                        box_factor_ = 1.0;
                    } else {
                        entity(*inner, m, true, out);
                    }
                }
                return;
            }
        }
    }
    const EntityProperties& p = e.props;
    Xform o = t.then(ocs(p.extrusion));         // for the types that are in their OCS
    // Thickness extrudes along the extrusion direction (OCS z)
    auto add_ocs = [&](const Point3& q) {
        out.add(o.apply(q));
        if (p.thickness != 0.0) out.add(o.apply({q.x, q.y, q.z + p.thickness}));
    };
    auto add_wcs = [&](const Point3& q) {
        out.add(t.apply(q));
        if (p.thickness != 0.0) {
            Point3 n = p.extrusion;
            out.add(t.apply({q.x + n.x * p.thickness, q.y + n.y * p.thickness,
                             q.z + n.z * p.thickness}));
        }
    };
    auto arc = [&](const Point3& center, double r, double a0, double a1, bool full) {
        add_arc(out, o, center, r, a0, a1, full);
        if (p.thickness != 0.0) {
            add_arc(out, o, {center.x, center.y, center.z + p.thickness}, r, a0, a1, full);
        }
    };
    // A line of text: its box in the font; without a font its insertion and alignment
    // points
    auto text_line = [&](const TextLine& line) {
        TextCorners box = text_.text(line);
        if (!box.font) {
            add_ocs(line.position);
            if (line.h_justify != 0 || line.v_justify != 0) add_ocs(line.alignment_point);
        }
        if (box.ink) for (const auto& q : box.corners) add_ocs(q);
    };
    // A 2D polyline in the OCS: its vertices, and its bulges as arcs. vertex(i) gives the
    // position and the bulge of the segment that starts there.
    auto path = [&](size_t n, bool closed, const auto& vertex) {
        for (size_t i = 0; i < n; ++i) {
            auto [pa, bulge] = vertex(i);
            add_ocs(pa);
            bool last = i + 1 == n;
            if (bulge == 0.0 || (last && !closed)) continue;
            Point3 pb = vertex(last ? 0 : i + 1).first;
            double dx = pb.x - pa.x;
            double dy = pb.y - pa.y;
            double chord = std::sqrt(dx * dx + dy * dy);
            if (chord == 0.0) continue;
            double theta = 4.0 * std::atan(bulge);              // included angle
            double r = chord / (2.0 * std::sin(theta / 2.0));
            double h = r * std::cos(theta / 2.0);               // center from the chord
            Point3 mid = {(pa.x + pb.x) / 2.0, (pa.y + pb.y) / 2.0, pa.z};
            Point3 center = {mid.x - dy / chord * h, mid.y + dx / chord * h, pa.z};
            double a0 = std::atan2(pa.y - center.y, pa.x - center.x);
            double a1 = std::atan2(pb.y - center.y, pb.x - center.x);
            if (bulge < 0.0) std::swap(a0, a1);                 // clockwise
            arc(center, std::fabs(r), a0, a1, false);
        }
    };

    // A multiline attribute's text is the MTEXT embedded in it (2018, or XRECORD ACAD_MLATT before);
    // the outer TEXT fields are its mirror and can be empty
    auto embedded_text = [&](const OptionalBoxed<MultilineAttribute>& ml) {
        if (!ml || !ml->has_text) return;
        MTextSize size = text_.mtext(ml->text);
        if (!size.font) {
            out.add(t.apply(ml->text.insertion_point));
        } else if (size.ink) {
            add_plane_box(out, t, ml->text.insertion_point, ml->text.x_axis_direction, p.extrusion, size.box);
        }
    };

    // The types phase 9 typed give their own extents (TypedRecordTypes.hpp, B107): what they add is mapped
    // like the entity's own points
    if (const EntityCodec* codec = TypedRegistry::instance().find_entity(e.type); codec && codec->extents) {
        TypedExtentsSink sink;
        sink.add_ocs = add_ocs;
        sink.add_wcs = add_wcs;
        sink.drawing = &dwg_;
        sink.in_block = in_block;
        codec->extents(e, sink);
        return;
    }

    switch (e.type) {
        case EntityType::Line:
            add_wcs(e.as_line().start);
            add_wcs(e.as_line().end);
            break;
        case EntityType::Point:
            out.add(t.apply(e.as_point().location));
            break;
        case EntityType::Circle:
            arc(e.as_circle().center, e.as_circle().radius, 0.0, 0.0, true);
            break;
        case EntityType::Arc: {
            const auto& d = e.as_arc();
            arc(d.center, d.radius, d.start_angle * M_PI / 180.0, d.end_angle * M_PI / 180.0,
                false);
            break;
        }
        case EntityType::Text: {
            const auto& d = e.as_text();
            text_line({d.style, d.content, d.height, d.width_factor, d.oblique_angle, d.rotation,
                       d.generation_flags, d.h_justify, d.v_justify, d.position, d.alignment_point});
            break;
        }
        case EntityType::Attdef: {
            // In a block it is shown as its ATTRIB when inserted — a constant one has none
            // and is shown with its value; elsewhere as its tag
            const auto& d = e.as_attdef();
            bool constant = d.flags & 2;
            if (in_block && !constant) break;
            embedded_text(d.multiline);
            text_line({d.style, in_block ? d.default_value : d.tag, d.height, d.width_factor,
                       d.oblique_angle, d.rotation, d.generation_flags, d.h_justify, d.v_justify,
                       d.start_point, d.alignment_point});
            break;
        }
        case EntityType::Attrib: {
            const auto& d = e.as_attrib();
            embedded_text(d.multiline);
            text_line({d.style, d.value, d.height, d.width_factor, d.oblique_angle, d.rotation,
                       d.generation_flags, d.h_justify, d.v_justify, d.start_point, d.alignment_point});
            break;
        }
        case EntityType::Shape: {
            TextCorners box = text_.shape(e.as_shape());
            if (!box.font) add_ocs(e.as_shape().insertion_point);
            if (box.ink) for (const auto& q : box.corners) add_ocs(q);
            break;
        }
        case EntityType::Solid:
            for (const Point3* q : {&e.as_solid().p0, &e.as_solid().p1, &e.as_solid().p2,
                                    &e.as_solid().p3}) add_ocs(*q);
            break;
        case EntityType::Trace:
            for (const Point3* q : {&e.as_trace().p0, &e.as_trace().p1, &e.as_trace().p2,
                                    &e.as_trace().p3}) add_ocs(*q);
            break;
        case EntityType::Face3D:
            for (const Point3* q : {&e.as_face3d().p0, &e.as_face3d().p1, &e.as_face3d().p2,
                                    &e.as_face3d().p3}) out.add(t.apply(*q));
            break;
        case EntityType::Polyline: {
            const auto& d = e.as_polyline();
            if (d.is_3d_polyline() || d.is_polygon_mesh() || d.is_polyface_mesh()) {
                for (const auto& v : d.vertices) {
                    bool face_record = (v.flags & 128) && !(v.flags & 64);
                    if (!face_record) out.add(t.apply(v.position));
                }
                break;
            }
            // A 2D polyline lies at its elevation (POLYLINE's 30) in its OCS
            path(d.vertices.size(), d.is_closed(), [&](size_t i) {
                const Point3& v = d.vertices[i].position;
                return std::pair<Point3, double>{{v.x, v.y, d.elevation}, d.vertices[i].bulge};
            });
            break;
        }
        case EntityType::LwPolyline: {
            const auto& d = e.as_lwpolyline();
            path(d.vertices.size(), d.is_closed(), [&](size_t i) {
                const LwVertex& v = d.vertices[i];
                return std::pair<Point3, double>{{v.position.x, v.position.y, d.elevation}, v.bulge};
            });
            break;
        }
        case EntityType::Ellipse: {
            // In the WCS: the minor axis is the normal × the major axis, times the ratio
            const auto& d = e.as_ellipse();
            Point3 n = p.extrusion;
            double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (len > 0.0) n = {n.x / len, n.y / len, n.z / len};
            const Point3& u = d.major_axis;
            Point3 v = {d.ratio * (n.y * u.z - n.z * u.y), d.ratio * (n.z * u.x - n.x * u.z),
                        d.ratio * (n.x * u.y - n.y * u.x)};
            add_curve(out, t.apply(d.center), t.linear(u), t.linear(v), d.start_param,
                      d.end_param, false);
            break;
        }
        case EntityType::Xline:
        case EntityType::Ray:
            break;                      // unbounded: no part of the extents
        case EntityType::MText: {
            const auto& d = e.as_mtext();
            MTextSize size = text_.mtext(d);
            if (!size.font) {
                out.add(t.apply(d.insertion_point));
            } else if (size.ink) {
                TextBox box = size.box;       // (with its fill and frame)
                add_plane_box(out, t, d.insertion_point, d.x_axis_direction, p.extrusion, box);
            }
            break;
        }
        case EntityType::Tolerance: {
            const auto& d = e.as_tolerance();
            MTextSize size = text_.tolerance(d, p.xdata);
            if (box_factor_ != 1.0) {
                // At an annotation scale its frames and text are drawn larger by the factor from its insertion point (9.S)
                size.box.min_x *= box_factor_;
                size.box.max_x *= box_factor_;
                size.box.min_y *= box_factor_;
                size.box.max_y *= box_factor_;
            }
            if (!size.font) out.add(t.apply(d.insertion_point));
            else if (size.ink) add_plane_box(out, t, d.insertion_point, d.x_axis_direction, p.extrusion, size.box);
            break;
        }
        case EntityType::Leader:
            for (const auto& v : e.as_leader().vertices) out.add(t.apply(v));
            break;
        case EntityType::Mline:
            // Each element's line passes through the vertex moved along its miter
            for (const auto& v : e.as_mline().vertices) {
                out.add(t.apply(v.position));
                for (const auto& el : v.elements) {
                    if (el.params.empty()) continue;
                    double a = el.params[0];
                    out.add(t.apply({v.position.x + v.miter.x * a, v.position.y + v.miter.y * a,
                                     v.position.z + v.miter.z * a}));
                }
            }
            break;
        case EntityType::Image:
        case EntityType::Wipeout: {
            // Its four corners: the insertion point and the pixel vectors times the size
            const auto& d = e.as_image();
            const Point3& origin = d.insertion_point;
            Point3 u = {d.u_vector.x * d.size.x, d.u_vector.y * d.size.x, d.u_vector.z * d.size.x};
            Point3 v = {d.v_vector.x * d.size.y, d.v_vector.y * d.size.y, d.v_vector.z * d.size.y};
            for (int c = 0; c < 4; ++c) {
                double a = (c & 1) ? 1.0 : 0.0;
                double b = (c & 2) ? 1.0 : 0.0;
                out.add(t.apply({origin.x + u.x * a + v.x * b, origin.y + u.y * a + v.y * b, origin.z + u.z * a + v.z * b}));
            }
            break;
        }
        case EntityType::Solid3D:
        case EntityType::Region:
        case EntityType::Body:
        case EntityType::Surface:
        case EntityType::ExtrudedSurface:
        case EntityType::LoftedSurface:
        case EntityType::RevolvedSurface:
        case EntityType::SweptSurface:
        case EntityType::NurbsSurface:
        case EntityType::PlaneSurface: {
            // The B-rep in the ACIS data, exactly (src/acis.cpp, R.9); a surface's too (B39)
            AcisMap map;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 4; ++j) map.m[i][j] = t.m[i][j];
            }
            acis_extents(acis_.bodies(e), map, out);
            break;
        }
        case EntityType::Helix:
            // As AutoCAD draws it (helix_points, B39)
            for (const auto& q : helix_points(e.as_helix())) out.add(t.apply(q));
            break;
        case EntityType::Mesh:
            // Its edges as AutoCAD draws them, smoothed to its level (B49)
            for (const auto& edge : mesh_wire(e.as_mesh())) {
                for (const auto& q : edge) out.add(t.apply(q));
            }
            break;
        case EntityType::Spline: {
            // In the WCS: the curve; with fit points only, the curve AutoCAD makes through
            // them
            const auto& d = e.as_spline();
            auto wcs = [&](const Point3& q) { return t.apply(q); };
            if (d.control_points.empty()) {
                std::vector<double> knots;
                std::vector<Point3> control;
                interpolate_fit(d.fit_points, d.start_tangent, d.end_tangent, knots, control);
                add_nurbs(out, wcs, 3, knots, control, {});
            } else {
                add_nurbs(out, wcs, d.degree, d.knots, d.control_points, d.weights);
            }
            break;
        }
        case EntityType::Hatch: {
            // The boundary paths, in the OCS at the elevation. A clockwise arc or ellipse
            // (73 = 0) has its angles measured clockwise: the minor direction is turned.
            const auto& d = e.as_hatch();
            double z = d.elevation;
            auto ocs3 = [&](const Point2& q) { return Point3{q.x, q.y, z}; };
            auto mapped = [&](const Point3& q) { return o.apply(q); };
            for (const auto& hp : d.paths) {
                if (hp.is_polyline()) {
                    path(hp.vertices.size(), hp.closed, [&](size_t i) {
                        return std::pair<Point3, double>{ocs3(hp.vertices[i].position),
                                                         hp.vertices[i].bulge};
                    });
                    continue;
                }
                for (const auto& edge : hp.edges) {
                    switch (edge.type) {
                        case HatchEdgeType::Line:
                            out.add(o.apply(ocs3(edge.start)));
                            out.add(o.apply(ocs3(edge.end)));
                            break;
                        case HatchEdgeType::Arc:
                        case HatchEdgeType::Ellipse: {
                            bool arc_edge = edge.type == HatchEdgeType::Arc;
                            Point3 u = arc_edge ? Point3{edge.radius, 0.0, 0.0}
                                                : Point3{edge.major_axis.x, edge.major_axis.y, 0.0};
                            double ratio = arc_edge ? 1.0 : edge.ratio;
                            double turn = edge.counterclockwise ? 1.0 : -1.0;
                            Point3 v = {-u.y * ratio * turn, u.x * ratio * turn, 0.0};
                            add_curve(out, o.apply(ocs3(edge.center)), o.linear(u), o.linear(v),
                                      edge.start_angle * M_PI / 180.0, edge.end_angle * M_PI / 180.0,
                                      edge.end_angle - edge.start_angle >= 360.0);
                            break;
                        }
                        case HatchEdgeType::Spline: {
                            std::vector<Point3> points;
                            for (const auto& c : edge.control_points) points.push_back(ocs3(c));
                            if (points.empty() && !edge.fit_points.empty()) {
                                // Fit points only (2010+): the curve through them
                                std::vector<Point3> fit;
                                for (const auto& f : edge.fit_points) fit.push_back(ocs3(f));
                                Point3 t0 = {edge.start_tangent.x, edge.start_tangent.y, 0.0};
                                Point3 t1 = {edge.end_tangent.x, edge.end_tangent.y, 0.0};
                                std::vector<double> knots;
                                std::vector<Point3> control;
                                interpolate_fit(fit, t0, t1, knots, control);
                                add_nurbs(out, mapped, 3, knots, control, {});
                                break;
                            }
                            add_nurbs(out, mapped, edge.degree, edge.knots, points,
                                      edge.rational ? edge.weights : std::vector<double>{});
                            break;
                        }
                    }
                }
            }
            break;
        }
        case EntityType::Insert: {
            const auto& d = e.as_insert();
            for (const auto& attr : d.attributes) {
                Entity a;
                a.type = EntityType::Attrib;
                a.props = attr.props;
                a.data = attr.data;
                entity(a, t, false, out);
            }
            const Block* b = find(d.block_name);
            if (!b) break;
            const Extents& box = block_extents(*b);
            int cols = std::max(1, d.column_count);
            int rows = std::max(1, d.row_count);
            // Turned a multiple of 90 degrees the box of the block has the extents of what is in it (a box is
            // convex: the corner copies of an array bound all of them). Turned another angle AutoCAD's extents
            // are those of the entities where they are, not the turned box's corners (AutoCAD 2025,
            // fas9_existing_fields.scr: an inserted wipeout and lines, ZOOM E and not), so the entities are mapped
            const double quarter = d.rotation / 90.0;
            const bool aligned = std::fabs(quarter - std::round(quarter)) < 1e-9;
            Xform base = translate({-b->base_point.x, -b->base_point.y, -b->base_point.z});
            for (int c : {0, cols - 1}) {
                for (int r : {0, rows - 1}) {
                    Xform m = o.then(translate(d.insertion_point))
                               .then(rotate_z(d.rotation))
                               .then(translate({c * d.column_spacing, r * d.row_spacing, 0.0}))
                               .then(scale(d.x_scale, d.y_scale, d.z_scale));
                    if (aligned) {
                        add_box(box, m, out);
                    } else if (!busy_[b]) {
                        busy_[b] = true;
                        for (const auto& inner : b->entities) entity(inner, m.then(base), true, out);
                        busy_[b] = false;
                    }
                }
            }
            break;
        }
        case EntityType::Dimension:
        case EntityType::ArcDimension:
        case EntityType::LargeRadialDimension: {
            const auto& d = *e.dimension_part();
            auto it = dim_block_names_.find(&e);
            const Block* b = find(it != dim_block_names_.end() ? it->second : d.block_name);
            if (b) {
                // R13 and R14: the block of a clone is where the dimension would be without 12 (step 7.16)
                bool older = dwg_.version() == DxfVersion::R13 || dwg_.version() == DxfVersion::R14;
                Xform place = t.then(translate(b->base_point));
                // 12 is in the dimension's OCS: turned into the WCS by its extrusion (Codex review)
                if (older) {
                    Point3 ax, ay;
                    ecs::arbitrary_axis(e.props.extrusion, ax, ay);
                    const Point3& az = e.props.extrusion;
                    double len = std::sqrt(az.x * az.x + az.y * az.y + az.z * az.z);
                    if (len == 0.0) len = 1.0;
                    const Point3& shift = d.block_translation;
                    Point3 w{ax.x * shift.x + ay.x * shift.y + az.x / len * shift.z,
                             ax.y * shift.x + ay.y * shift.y + az.y / len * shift.z,
                             ax.z * shift.x + ay.z * shift.y + az.z / len * shift.z};
                    place = place.then(translate(w));
                }
                add_box(block_extents(*b), place, out);
            }
            out.add(t.apply(d.definition_point));
            break;
        }
        case EntityType::Viewport: {
            const auto& d = e.as_viewport();
            if (d.id == 1) break;       // the paper space view itself
            add_wcs({d.center.x - d.width / 2.0, d.center.y - d.height / 2.0, d.center.z});
            add_wcs({d.center.x + d.width / 2.0, d.center.y + d.height / 2.0, d.center.z});
            break;
        }
        case EntityType::MLeader: {
            // What its graphics draw (mleader_graphics.cpp, B38) mapped by t: the leaders,
            // the text and the attributes; the entities of its blocks each as an entity of a
            // block, through the block's transform
            std::array<double, 16> map = {t.m[0][0], t.m[0][1], t.m[0][2], t.m[0][3],
                                          t.m[1][0], t.m[1][1], t.m[1][2], t.m[1][3],
                                          t.m[2][0], t.m[2][1], t.m[2][2], t.m[2][3],
                                          0.0, 0.0, 0.0, 1.0};
            mleader_extents(dwg_, e, text_, map,
                            [&](const Entity& be, const std::array<double, 16>& m) {
                                Xform x;
                                for (int i = 0; i < 3; ++i) {
                                    for (int j = 0; j < 4; ++j) x.m[i][j] = m[4 * i + j];
                                }
                                entity(be, x, true, out);
                            },
                            out);
            break;
        }
        case EntityType::Table:             // the types phase 9 typed: their codec's extents (above), none without one
        case EntityType::SectionObject:
        case EntityType::Light:
        case EntityType::CoordinationModel:
        case EntityType::PdfUnderlay:
        case EntityType::DwfUnderlay:
        case EntityType::DgnUnderlay:
        case EntityType::OleFrame:
        case EntityType::Ole2Frame:
        case EntityType::ProxyEntity:
        case EntityType::TcPoint:
        case EntityType::Unknown:
            break;
    }
}

} // namespace

SplineData spline_curve(const SplineData& d) {
    if (!d.control_points.empty()) return d;
    SplineData out = d;
    out.degree = 3;
    out.weights.clear();
    interpolate_fit(d.fit_points, d.start_tangent, d.end_tangent, out.knots, out.control_points);
    return out;
}

Point3 nurbs_point(const SplineData& s, double u) {
    int p = s.degree;
    const auto& k = s.knots;
    const auto& c = s.control_points;
    size_t n = c.size();
    if (p < 1 || n < static_cast<size_t>(p) + 1 || k.size() != n + p + 1) return c.empty() ? Point3{} : c.front();
    size_t span = p;
    while (span + 1 < n && u >= k[span + 1]) ++span;
    auto weight = [&](size_t i) { return s.is_rational() && i < s.weights.size() ? s.weights[i] : 1.0; };
    std::vector<std::array<double, 4>> d(p + 1);
    for (int j = 0; j <= p; ++j) {
        const Point3& q = c[span - p + j];
        double w = weight(span - p + j);
        d[j] = {q.x * w, q.y * w, q.z * w, w};
    }
    for (int r = 1; r <= p; ++r) {
        for (int j = p; j >= r; --j) {
            size_t i = span - p + j;
            double denom = k[i + p - r + 1] - k[i];
            double alpha = denom == 0.0 ? 0.0 : (u - k[i]) / denom;
            for (int m = 0; m < 4; ++m) d[j][m] = (1.0 - alpha) * d[j - 1][m] + alpha * d[j][m];
        }
    }
    double w = d[p][3] != 0.0 ? d[p][3] : 1.0;
    return {d[p][0] / w, d[p][1] / w, d[p][2] / w};
}

std::vector<Point3> image_outline(const ImageData& d) {
    std::vector<Point2> pixels;
    Point2 a = {-0.5, -0.5}, b = {d.size.x - 0.5, d.size.y - 0.5};
    bool polygon = false;
    if (d.clipping && d.clip_boundary.size() >= 2) {
        if (d.clip_boundary_type == 1 || d.clip_boundary.size() == 2) {
            a = d.clip_boundary[0];
            b = d.clip_boundary[1];
        } else {
            pixels = d.clip_boundary;
            const Point2& f = pixels.front();
            const Point2& l = pixels.back();
            if (f.x != l.x || f.y != l.y) pixels.push_back(f);
            polygon = true;
        }
    }
    if (!polygon) pixels = {{a.x, b.y}, a, {b.x, a.y}, b, {a.x, b.y}};
    std::vector<Point3> points;
    for (const auto& q : pixels) {
        double u = q.x + 0.5, v = d.size.y - (q.y + 0.5);
        points.push_back({d.insertion_point.x + d.u_vector.x * u + d.v_vector.x * v,
                          d.insertion_point.y + d.u_vector.y * u + d.v_vector.y * v,
                          d.insertion_point.z + d.u_vector.z * u + d.v_vector.z * v});
    }
    return points;
}

std::vector<Point3> helix_points(const HelixData& d) {
    const SplineData& s = d.spline;
    size_t p = static_cast<size_t>(std::max(0, s.degree));
    if (s.knots.size() < 2 * p + 2 || s.control_points.empty()) return {};
    int n = std::max(2, static_cast<int>(std::lround(64.0 * d.turns)));
    double lo = s.knots[p], hi = s.knots[s.knots.size() - 1 - p];
    std::vector<Point3> points;
    for (int i = 0; i < n; ++i) points.push_back(nurbs_point(s, lo + (hi - lo) * i / (n - 1)));
    return points;
}

void ellipse_extents(Extents& out, const Point3& c, const Point3& u, const Point3& v, double a0,
                     double a1, bool full) {
    add_curve(out, c, u, v, a0, a1, full);
}

void nurbs_extents(Extents& out, const std::function<Point3(const Point3&)>& map, int degree,
                   const std::vector<double>& knots, const std::vector<Point3>& points,
                   const std::vector<double>& weights, double lo, double hi,
                   std::vector<double>* params) {
    add_nurbs(out, map, degree, knots, points, weights, lo, hi, params);
}

void polynomial_roots(const std::vector<double>& a, double lo, double hi, std::vector<double>& out) {
    poly_roots(a, lo, hi, out);
}

std::vector<double> bernstein_power(const std::vector<double>& q) { return bernstein_to_power(q); }

void drawing_extents(const Drawing& dwg, const std::vector<Block>& generated,
                     const std::unordered_map<const Entity*, std::string>& dim_block_names,
                     Extents& model, Extents& paper, std::vector<std::string>& warnings,
                     const std::vector<Block>* replaced) {
    ExtentsBuilder builder(dwg, generated, dim_block_names, replaced);
    for (const auto& e : dwg.entities()) {
        builder.entity(e, Xform{}, false, e.props.paper_space ? paper : model);
    }
    warnings = builder.warnings();
}

void entity_extents(const Drawing& dwg, const Entity& e, Extents& out) {
    std::vector<Block> none;
    std::unordered_map<const Entity*, std::string> names;
    ExtentsBuilder builder(dwg, none, names);
    builder.entity(e, Xform{}, false, out);
}

} // namespace internal
} // namespace hdxf
