#include "hdxf.hpp"
#include <cmath>

namespace hdxf {
namespace ecs {

// Threshold used by the Arbitrary Axis Algorithm (1/64).
// If both |Nx| and |Ny| are less than this value, the extrusion is
// considered close to the World Z axis (0,0,1) or (0,0,-1).
static constexpr double kThreshold = 1.0 / 64.0;

static double length(const Point3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static Point3 normalize(const Point3& v) {
    double len = length(v);
    if (len < 1e-15) {
        return {0.0, 0.0, 0.0};
    }
    return {v.x / len, v.y / len, v.z / len};
}

static Point3 cross(const Point3& a, const Point3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static double dot(const Point3& a, const Point3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// ---------------------------------------------------------------------------
// Arbitrary Axis Algorithm (DXF R12 specification)
//
// Given an extrusion direction (the entity's normal vector), compute the
// X and Y axes of the Entity Coordinate System (ECS).
//
// If the extrusion direction is close to the World Z axis (i.e. both
// |Nx| < 1/64 and |Ny| < 1/64), use the World Y axis (0,1,0) as the
// reference vector.  Otherwise use the World Z axis (0,0,1).
//
// X_axis = normalize(cross(reference, extrusion))
// Y_axis = normalize(cross(extrusion, X_axis))
// ---------------------------------------------------------------------------

void arbitrary_axis(const Point3& extrusion, Point3& x_axis, Point3& y_axis) {
    Point3 ref;

    if (std::fabs(extrusion.x) < kThreshold && std::fabs(extrusion.y) < kThreshold) {
        // Close to World Z — use World Y as reference
        ref = {0.0, 1.0, 0.0};
    } else {
        // Otherwise use World Z as reference
        ref = {0.0, 0.0, 1.0};
    }

    x_axis = normalize(cross(ref, extrusion));
    y_axis = normalize(cross(extrusion, x_axis));
}

// ---------------------------------------------------------------------------
// Transform a point from ECS (Entity Coordinate System) to WCS (World
// Coordinate System).
//
// WCS_point = ecs.x * X_axis + ecs.y * Y_axis + ecs.z * extrusion
// ---------------------------------------------------------------------------

Point3 ecs_to_wcs(const Point3& ecs_point, const Point3& extrusion) {
    Point3 x_axis, y_axis;
    arbitrary_axis(extrusion, x_axis, y_axis);

    Point3 n = normalize(extrusion);

    return {
        ecs_point.x * x_axis.x + ecs_point.y * y_axis.x + ecs_point.z * n.x,
        ecs_point.x * x_axis.y + ecs_point.y * y_axis.y + ecs_point.z * n.y,
        ecs_point.x * x_axis.z + ecs_point.y * y_axis.z + ecs_point.z * n.z
    };
}

// ---------------------------------------------------------------------------
// Transform a point from WCS (World Coordinate System) to ECS (Entity
// Coordinate System).
//
// This is the inverse of ecs_to_wcs.  Since [X_axis, Y_axis, N] form an
// orthonormal basis, the inverse is simply dot products:
//
// ecs.x = dot(wcs_point, X_axis)
// ecs.y = dot(wcs_point, Y_axis)
// ecs.z = dot(wcs_point, N)
// ---------------------------------------------------------------------------

Point3 wcs_to_ecs(const Point3& wcs_point, const Point3& extrusion) {
    Point3 x_axis, y_axis;
    arbitrary_axis(extrusion, x_axis, y_axis);

    Point3 n = normalize(extrusion);

    return {
        dot(wcs_point, x_axis),
        dot(wcs_point, y_axis),
        dot(wcs_point, n)
    };
}

} // namespace ecs
} // namespace hdxf
