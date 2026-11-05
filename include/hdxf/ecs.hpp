#pragma once

// The entity coordinate system.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// ECS (Entity Coordinate System) utilities
// ---------------------------------------------------------------------------

namespace ecs {

void arbitrary_axis(const Point3& extrusion, Point3& x_axis, Point3& y_axis);
Point3 ecs_to_wcs(const Point3& ecs_point, const Point3& extrusion);
Point3 wcs_to_ecs(const Point3& wcs_point, const Point3& extrusion);

} // namespace ecs

} // namespace hdxf
