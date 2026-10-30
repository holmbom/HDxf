#pragma once

// Vector arithmetic, evaluation and checks of the builder's curves and surfaces (internal)

#include "hdxf_acis.hpp"

#include <string>
#include <vector>

namespace hdxf::acis::detail {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

Vector3 add(const Vector3& a, const Vector3& b);
Vector3 sub(const Vector3& a, const Vector3& b);
Vector3 scaled(const Vector3& a, double s);
double dot(const Vector3& a, const Vector3& b);
Vector3 cross(const Vector3& a, const Vector3& b);
double length(const Vector3& a);
double distance(const Point3& a, const Point3& b);
Vector3 normalized(const Vector3& a);       // the zero vector when a has no length
bool finite(const Vector3& a);
bool is_unit(const Vector3& a, double tolerance);
bool perpendicular(const Vector3& a, const Vector3& b, double tolerance);   // of unit vectors

// ---------------------------------------------------------------------------
// B-splines
// ---------------------------------------------------------------------------

// Whether the degree, knots and control point count of one direction fit together; the empty
// string when they do, else why not. `what` names it ("curve", "surface u direction").
std::string check_spline_direction(int degree, const std::vector<double>& knots, int count,
                                   const std::string& what);

// Whether the weights fit the control points: none (polynomial), or one each and positive
std::string check_spline_weights(const std::vector<double>& weights, size_t point_count,
                                 const std::string& what);

// The parameter interval the spline is defined on: knots[degree] … knots[count]
void spline_domain(int degree, const std::vector<double>& knots, int count, double& low, double& high);

Point3 bspline_curve_point(const BSplineCurveGeometry& curve, double t);
hdxf::Point2 bspline_curve2d_point(const BSplineCurve2D& curve, double t);
Point3 bspline_surface_point(const BSplineSurfaceGeometry& surface, double u, double v);

// The knots with their multiplicities, the first and the last of the whole vector left out
// as ACIS holds them: (value, multiplicity) in order
struct KnotRun {
    double value = 0.0;
    int multiplicity = 0;
};
std::vector<KnotRun> knot_runs(const std::vector<double>& knots);

// The knots where a derivative of order 1, 2 or 3 may jump: an inner knot of multiplicity m in a
// spline of degree p in order p - m + 1 (at least 1); in a periodic direction the knot where the
// period starts too, in order p. lists[order - 1] holds them.
void discontinuity_lists(int degree, const std::vector<double>& knots, int count, bool periodic,
                         std::vector<double> lists[3]);

// ---------------------------------------------------------------------------
// Curves and surfaces
// ---------------------------------------------------------------------------

// The parameter interval a curve can be asked for: a B-spline its domain, the others all reals
void curve_domain(const CurveGeometry& curve, double& low, double& high);

Point3 curve_point(const CurveGeometry& curve, double t);

// A point on a surface in its parameters (see the geometry types of hdxf_acis.hpp)
Point3 surface_point(const SurfaceGeometry& surface, double u, double v);

// The distance from p to an analytic surface (plane, cone, sphere, torus); false for a
// B-spline surface, which has no closed form
bool analytic_distance(const SurfaceGeometry& surface, const Point3& p, double& out);

// The normal of an analytic plane (the surface's own); the zero vector for the others
Vector3 plane_normal(const SurfaceGeometry& surface);

// Why a curve, surface or pcurve is not valid; the empty string when it is
std::string check_curve(const CurveGeometry& curve, const Tolerances& tolerance);
std::string check_surface(const SurfaceGeometry& surface, const Tolerances& tolerance);
std::string check_pcurve(const PcurveGeometry& pcurve, const Tolerances& tolerance);

} // namespace hdxf::acis::detail
