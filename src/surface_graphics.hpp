#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <string>
#include <vector>

// The isoline proxy graphics of a surface entity in DXF 2000 and 2004 (src/surface_graphics.cpp)

namespace hdxf {
namespace internal {

// Empty when the surface's isolines can be made as AutoCAD draws them, else why not (naming what is not drawn)
std::string surface_isoline_error(const Entity& e);

// The stream (size, count, commands) for a surface entity, drawn for a view of `view_height` (the active VPORT's)
bool surface_isoline_graphics(const Entity& e, double view_height, std::vector<uint8_t>& out, std::string& why, int circle_zoom = 1000);

// The stream a surface was read with (its record's 92/160 and 310), each true color (22) the entity's color `c`: AutoCAD's
// stream for the same body in another color (F9.18 (a): the 22 of every curve is the entity's color, nothing else of the
// entity's is in it). Empty when the record has no stream
std::vector<uint8_t> surface_recolored_graphics(const Entity& was, const Color& c);

} // namespace internal
} // namespace hdxf
