#include "geometry.hpp"

#include "format.hpp"

#include <algorithm>
#include <cmath>

namespace hdxf::acis::detail {

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

Vector3 add(const Vector3& a, const Vector3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 sub(const Vector3& a, const Vector3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vector3 scaled(const Vector3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vector3 cross(const Vector3& a, const Vector3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(const Vector3& a) { return std::sqrt(dot(a, a)); }
double distance(const Point3& a, const Point3& b) { return length(sub(a, b)); }

Vector3 normalized(const Vector3& a) {
    double l = length(a);
    if (!(l > 0.0)) return {0.0, 0.0, 0.0};
    return scaled(a, 1.0 / l);
}

bool finite(const Vector3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

bool is_unit(const Vector3& a, double tolerance) {
    return finite(a) && std::fabs(length(a) - 1.0) <= tolerance;
}

bool perpendicular(const Vector3& a, const Vector3& b, double tolerance) {
    return std::fabs(dot(a, b)) <= tolerance;
}

// ---------------------------------------------------------------------------
// B-spline data
// ---------------------------------------------------------------------------

std::string check_spline_direction(int degree, const std::vector<double>& knots, int count,
                                   const std::string& what) {
    if (degree < 1 || degree > 25) return what + ": the degree " + integer_text(degree) + " is not in 1 … 25";
    if (count < degree + 1) {
        return what + ": " + integer_text(count) + " control points are too few for degree " + integer_text(degree);
    }
    size_t expected = static_cast<size_t>(count) + static_cast<size_t>(degree) + 1;
    if (knots.size() != expected) {
        return what + ": degree " + integer_text(degree) + " and " + integer_text(count) + " control points need " +
               integer_text(static_cast<long long>(expected)) + " knots, found " +
               integer_text(static_cast<long long>(knots.size()));
    }
    for (size_t i = 0; i < knots.size(); ++i) {
        if (!std::isfinite(knots[i])) return what + ": knot " + integer_text(static_cast<long long>(i)) + " is not a number";
        if (i > 0 && knots[i] < knots[i - 1]) {
            return what + ": the knots decrease at knot " + integer_text(static_cast<long long>(i));
        }
    }
    // Multiplicities: an inner knot at most the degree, an end knot at most degree + 1
    size_t run = 1;
    for (size_t i = 1; i <= knots.size(); ++i) {
        if (i < knots.size() && knots[i] == knots[i - 1]) {
            ++run;
            continue;
        }
        bool at_end = i == run || i == knots.size();
        size_t limit = at_end ? static_cast<size_t>(degree) + 1 : static_cast<size_t>(degree);
        if (run > limit) {
            return what + ": the knot " + real_text(knots[i - 1]) + " has multiplicity " +
                   integer_text(static_cast<long long>(run)) + ", more than " +
                   integer_text(static_cast<long long>(limit));
        }
        run = 1;
    }
    if (!(knots[static_cast<size_t>(degree)] < knots[static_cast<size_t>(count)])) {
        return what + ": the parameter interval is empty";
    }
    return {};
}

std::string check_spline_weights(const std::vector<double>& weights, size_t point_count, const std::string& what) {
    if (weights.empty()) return {};
    if (weights.size() != point_count) {
        return what + ": " + integer_text(static_cast<long long>(weights.size())) + " weights for " +
               integer_text(static_cast<long long>(point_count)) + " control points";
    }
    for (double w : weights) {
        if (!(w > 0.0) || !std::isfinite(w)) return what + ": a weight is not positive (" + real_text(w) + ")";
    }
    return {};
}

void spline_domain(int degree, const std::vector<double>& knots, int count, double& low, double& high) {
    low = knots[static_cast<size_t>(degree)];
    high = knots[static_cast<size_t>(count)];
}

namespace {

// The knot span [knots[i], knots[i+1]) of positive length that holds t (the last one at the end)
int find_span(int count, int degree, const std::vector<double>& knots, double t) {
    if (t >= knots[static_cast<size_t>(count)]) return count - 1;
    if (t <= knots[static_cast<size_t>(degree)]) return degree;
    int low = degree;
    int high = count;
    while (high - low > 1) {
        int mid = (low + high) / 2;
        if (t < knots[static_cast<size_t>(mid)]) high = mid;
        else low = mid;
    }
    return low;
}

// The degree + 1 basis functions that are not zero in the span
std::vector<double> basis_functions(int span, double t, int degree, const std::vector<double>& knots) {
    std::vector<double> basis(static_cast<size_t>(degree) + 1, 0.0);
    std::vector<double> left(static_cast<size_t>(degree) + 1, 0.0);
    std::vector<double> right(static_cast<size_t>(degree) + 1, 0.0);
    basis[0] = 1.0;
    for (int j = 1; j <= degree; ++j) {
        left[static_cast<size_t>(j)] = t - knots[static_cast<size_t>(span + 1 - j)];
        right[static_cast<size_t>(j)] = knots[static_cast<size_t>(span + j)] - t;
        double saved = 0.0;
        for (int r = 0; r < j; ++r) {
            double denominator = right[static_cast<size_t>(r + 1)] + left[static_cast<size_t>(j - r)];
            double term = denominator != 0.0 ? basis[static_cast<size_t>(r)] / denominator : 0.0;
            basis[static_cast<size_t>(r)] = saved + right[static_cast<size_t>(r + 1)] * term;
            saved = left[static_cast<size_t>(j - r)] * term;
        }
        basis[static_cast<size_t>(j)] = saved;
    }
    return basis;
}

double weight_at(const std::vector<double>& weights, size_t i) { return weights.empty() ? 1.0 : weights[i]; }

} // namespace

Point3 bspline_curve_point(const BSplineCurveGeometry& curve, double t) {
    int count = static_cast<int>(curve.points.size());
    int span = find_span(count, curve.degree, curve.knots, t);
    std::vector<double> basis = basis_functions(span, t, curve.degree, curve.knots);
    Vector3 sum{0.0, 0.0, 0.0};
    double w_sum = 0.0;
    for (int j = 0; j <= curve.degree; ++j) {
        size_t i = static_cast<size_t>(span - curve.degree + j);
        double w = basis[static_cast<size_t>(j)] * weight_at(curve.weights, i);
        sum = add(sum, scaled(curve.points[i], w));
        w_sum += w;
    }
    return w_sum != 0.0 ? scaled(sum, 1.0 / w_sum) : sum;
}

hdxf::Point2 bspline_curve2d_point(const BSplineCurve2D& curve, double t) {
    int count = static_cast<int>(curve.points.size());
    int span = find_span(count, curve.degree, curve.knots, t);
    std::vector<double> basis = basis_functions(span, t, curve.degree, curve.knots);
    double u = 0.0;
    double v = 0.0;
    double w_sum = 0.0;
    for (int j = 0; j <= curve.degree; ++j) {
        size_t i = static_cast<size_t>(span - curve.degree + j);
        double w = basis[static_cast<size_t>(j)] * weight_at(curve.weights, i);
        u += curve.points[i].x * w;
        v += curve.points[i].y * w;
        w_sum += w;
    }
    if (w_sum != 0.0) {
        u /= w_sum;
        v /= w_sum;
    }
    return {u, v};
}

Point3 bspline_surface_point(const BSplineSurfaceGeometry& surface, double u, double v) {
    int span_u = find_span(surface.count_u, surface.degree_u, surface.knots_u, u);
    int span_v = find_span(surface.count_v, surface.degree_v, surface.knots_v, v);
    std::vector<double> basis_u = basis_functions(span_u, u, surface.degree_u, surface.knots_u);
    std::vector<double> basis_v = basis_functions(span_v, v, surface.degree_v, surface.knots_v);
    Vector3 sum{0.0, 0.0, 0.0};
    double w_sum = 0.0;
    for (int l = 0; l <= surface.degree_v; ++l) {
        for (int k = 0; k <= surface.degree_u; ++k) {
            size_t i = static_cast<size_t>(span_u - surface.degree_u + k) +
                       static_cast<size_t>(span_v - surface.degree_v + l) * static_cast<size_t>(surface.count_u);
            double w = basis_u[static_cast<size_t>(k)] * basis_v[static_cast<size_t>(l)] *
                       weight_at(surface.weights, i);
            sum = add(sum, scaled(surface.points[i], w));
            w_sum += w;
        }
    }
    return w_sum != 0.0 ? scaled(sum, 1.0 / w_sum) : sum;
}

std::vector<KnotRun> knot_runs(const std::vector<double>& knots) {
    std::vector<KnotRun> runs;
    for (size_t i = 1; i + 1 < knots.size(); ++i) {
        if (!runs.empty() && knots[i] == runs.back().value) ++runs.back().multiplicity;
        else runs.push_back({knots[i], 1});
    }
    return runs;
}

void discontinuity_lists(int degree, const std::vector<double>& knots, int count, bool periodic,
                         std::vector<double> lists[3]) {
    double low = knots[static_cast<size_t>(degree)];
    double high = knots[static_cast<size_t>(count)];
    // The inner knots, each by its multiplicity in the whole vector
    size_t i = 0;
    while (i < knots.size()) {
        size_t j = i;
        while (j < knots.size() && knots[j] == knots[i]) ++j;
        double value = knots[i];
        int multiplicity = static_cast<int>(j - i);
        i = j;
        if (!(value > low) || !(value < high)) continue;
        int order = std::max(1, degree - multiplicity + 1);
        if (order <= 3) lists[order - 1].push_back(value);
    }
    if (periodic && degree <= 3) lists[degree - 1].insert(lists[degree - 1].begin(), low);
}

// ---------------------------------------------------------------------------
// Curves and surfaces
// ---------------------------------------------------------------------------

void curve_domain(const CurveGeometry& curve, double& low, double& high) {
    if (const auto* spline = std::get_if<BSplineCurveGeometry>(&curve)) {
        spline_domain(spline->degree, spline->knots, static_cast<int>(spline->points.size()), low, high);
        return;
    }
    low = -1e300;
    high = 1e300;
}

Point3 curve_point(const CurveGeometry& curve, double t) {
    if (const auto* line = std::get_if<LineGeometry>(&curve)) {
        return add(line->origin, scaled(line->direction, t));
    }
    if (const auto* ellipse = std::get_if<EllipseGeometry>(&curve)) {
        Vector3 minor = scaled(cross(ellipse->normal, ellipse->major_axis), ellipse->ratio);
        return add(ellipse->center,
                   add(scaled(ellipse->major_axis, std::cos(t)), scaled(minor, std::sin(t))));
    }
    return bspline_curve_point(std::get<BSplineCurveGeometry>(curve), t);
}

Point3 surface_point(const SurfaceGeometry& surface, double u, double v) {
    if (const auto* plane = std::get_if<PlaneGeometry>(&surface)) {
        Vector3 across = cross(plane->normal, plane->u_direction);
        return add(plane->origin, add(scaled(plane->u_direction, u), scaled(across, v)));
    }
    if (const auto* cone = std::get_if<ConeGeometry>(&surface)) {
        double major = length(cone->major_axis);
        Vector3 minor = cross(cone->axis, cone->major_axis);
        Vector3 radius = add(scaled(cone->major_axis, std::cos(v)), scaled(minor, std::sin(v)));
        double cosine = 1.0 / std::sqrt(1.0 + cone->radius_gradient * cone->radius_gradient);
        double sine = cone->radius_gradient * cosine;
        Vector3 line = add(scaled(cone->axis, cosine), scaled(radius, sine / major));
        return add(add(cone->center, radius), scaled(line, u * major));
    }
    if (const auto* sphere = std::get_if<SphereGeometry>(&surface)) {
        Vector3 x = normalized(sphere->u_direction);
        Vector3 y = cross(sphere->pole, x);
        Vector3 around = add(scaled(x, std::cos(v)), scaled(y, std::sin(v)));
        Vector3 on = add(scaled(around, std::cos(u)), scaled(sphere->pole, std::sin(u)));
        return add(sphere->center, scaled(on, sphere->radius));
    }
    if (const auto* torus = std::get_if<TorusGeometry>(&surface)) {
        Vector3 x = normalized(torus->u_direction);
        Vector3 y = cross(torus->axis, x);
        Vector3 around = add(scaled(x, std::cos(v)), scaled(y, std::sin(v)));
        Vector3 ring = scaled(around, torus->major_radius + torus->minor_radius * std::cos(u));
        return add(add(torus->center, ring), scaled(torus->axis, torus->minor_radius * std::sin(u)));
    }
    return bspline_surface_point(std::get<BSplineSurfaceGeometry>(surface), u, v);
}

bool analytic_distance(const SurfaceGeometry& surface, const Point3& p, double& out) {
    if (const auto* plane = std::get_if<PlaneGeometry>(&surface)) {
        out = std::fabs(dot(sub(p, plane->origin), plane->normal));
        return true;
    }
    if (const auto* cone = std::get_if<ConeGeometry>(&surface)) {
        Vector3 d = sub(p, cone->center);
        double along = dot(d, cone->axis);
        double radial = length(sub(d, scaled(cone->axis, along)));
        double g = cone->radius_gradient;
        out = std::fabs(radial - g * along - length(cone->major_axis)) / std::sqrt(1.0 + g * g);
        return true;
    }
    if (const auto* sphere = std::get_if<SphereGeometry>(&surface)) {
        out = std::fabs(distance(p, sphere->center) - sphere->radius);
        return true;
    }
    if (const auto* torus = std::get_if<TorusGeometry>(&surface)) {
        Vector3 d = sub(p, torus->center);
        double along = dot(d, torus->axis);
        double radial = length(sub(d, scaled(torus->axis, along)));
        double offset = radial - torus->major_radius;
        out = std::fabs(std::sqrt(offset * offset + along * along) - torus->minor_radius);
        return true;
    }
    return false;
}

Vector3 plane_normal(const SurfaceGeometry& surface) {
    if (const auto* plane = std::get_if<PlaneGeometry>(&surface)) return plane->normal;
    return {0.0, 0.0, 0.0};
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

namespace {

std::string check_axes(const Vector3& a, const char* a_name, const Vector3& b, const char* b_name, const Tolerances& tol) {
    if (!is_unit(a, tol.direction)) return std::string("the ") + a_name + " is not a unit vector";
    if (!is_unit(b, tol.direction)) return std::string("the ") + b_name + " is not a unit vector";
    if (!perpendicular(a, b, tol.direction)) {
        return std::string("the ") + a_name + " and the " + b_name + " are not perpendicular";
    }
    return {};
}

} // namespace

std::string check_curve(const CurveGeometry& curve, const Tolerances& tol) {
    if (const auto* line = std::get_if<LineGeometry>(&curve)) {
        if (!finite(line->origin)) return "the line's origin is not finite";
        if (!is_unit(line->direction, tol.direction)) return "the line's direction is not a unit vector";
        return {};
    }
    if (const auto* ellipse = std::get_if<EllipseGeometry>(&curve)) {
        if (!finite(ellipse->center)) return "the ellipse's center is not finite";
        if (!is_unit(ellipse->normal, tol.direction)) return "the ellipse's normal is not a unit vector";
        if (!finite(ellipse->major_axis) || !(length(ellipse->major_axis) > 0.0)) {
            return "the ellipse's major axis has no length";
        }
        if (!perpendicular(ellipse->normal, normalized(ellipse->major_axis), tol.direction)) {
            return "the ellipse's normal and major axis are not perpendicular";
        }
        if (!(ellipse->ratio > 0.0) || !(ellipse->ratio <= 1.0)) return "the ellipse's ratio is not in (0, 1]";
        return {};
    }
    const auto& spline = std::get<BSplineCurveGeometry>(curve);
    std::string why = check_spline_direction(spline.degree, spline.knots, static_cast<int>(spline.points.size()), "B-spline curve");
    if (!why.empty()) return why;
    why = check_spline_weights(spline.weights, spline.points.size(), "B-spline curve");
    if (!why.empty()) return why;
    for (const Point3& p : spline.points) {
        if (!finite(p)) return "B-spline curve: a control point is not finite";
    }
    if (spline.closure == Closure::Closed && distance(spline.points.front(), spline.points.back()) > tol.distance) {
        return "B-spline curve: closed, but its first and last control points differ";
    }
    return {};
}

std::string check_surface(const SurfaceGeometry& surface, const Tolerances& tol) {
    if (const auto* plane = std::get_if<PlaneGeometry>(&surface)) {
        if (!finite(plane->origin)) return "the plane's origin is not finite";
        return check_axes(plane->normal, "plane's normal", plane->u_direction, "plane's u direction", tol);
    }
    if (const auto* cone = std::get_if<ConeGeometry>(&surface)) {
        if (!finite(cone->center)) return "the cone's center is not finite";
        if (!is_unit(cone->axis, tol.direction)) return "the cone's axis is not a unit vector";
        if (!finite(cone->major_axis) || !(length(cone->major_axis) > 0.0)) return "the cone's major axis has no length";
        if (!perpendicular(cone->axis, normalized(cone->major_axis), tol.direction)) {
            return "the cone's axis and major axis are not perpendicular";
        }
        if (!std::isfinite(cone->radius_gradient)) return "the cone's radius gradient is not finite";
        return {};
    }
    if (const auto* sphere = std::get_if<SphereGeometry>(&surface)) {
        if (!finite(sphere->center)) return "the sphere's center is not finite";
        if (!(sphere->radius > 0.0) || !std::isfinite(sphere->radius)) return "the sphere's radius is not positive";
        return check_axes(sphere->pole, "sphere's pole", sphere->u_direction, "sphere's u direction", tol);
    }
    if (const auto* torus = std::get_if<TorusGeometry>(&surface)) {
        if (!finite(torus->center)) return "the torus' center is not finite";
        if (!(torus->minor_radius > 0.0) || !std::isfinite(torus->major_radius) ||
            !(torus->major_radius > torus->minor_radius)) {
            return "the torus' radii must satisfy major > minor > 0";
        }
        return check_axes(torus->axis, "torus' axis", torus->u_direction, "torus' u direction", tol);
    }
    const auto& spline = std::get<BSplineSurfaceGeometry>(surface);
    std::string why = check_spline_direction(spline.degree_u, spline.knots_u, spline.count_u, "B-spline surface u direction");
    if (!why.empty()) return why;
    why = check_spline_direction(spline.degree_v, spline.knots_v, spline.count_v, "B-spline surface v direction");
    if (!why.empty()) return why;
    size_t expected = static_cast<size_t>(spline.count_u) * static_cast<size_t>(spline.count_v);
    if (spline.points.size() != expected) {
        return "B-spline surface: a control net of " + integer_text(spline.count_u) + " × " + integer_text(spline.count_v) +
               " needs " + integer_text(static_cast<long long>(expected)) + " points, found " +
               integer_text(static_cast<long long>(spline.points.size()));
    }
    why = check_spline_weights(spline.weights, spline.points.size(), "B-spline surface");
    if (!why.empty()) return why;
    for (const Point3& p : spline.points) {
        if (!finite(p)) return "B-spline surface: a control point is not finite";
    }
    return {};
}

std::string check_pcurve(const PcurveGeometry& pcurve, const Tolerances&) {
    std::string why = check_spline_direction(pcurve.degree, pcurve.knots, static_cast<int>(pcurve.points.size()), "pcurve");
    if (!why.empty()) return why;
    why = check_spline_weights(pcurve.weights, pcurve.points.size(), "pcurve");
    if (!why.empty()) return why;
    for (const hdxf::Point2& p : pcurve.points) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) return "pcurve: a control point is not finite";
    }
    return {};
}

} // namespace hdxf::acis::detail
