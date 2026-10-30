#pragma once

#include "hdxf.hpp"
#include "mleader_graphics.hpp"
#include <cstdint>
#include <vector>

// What the section family shares between its files (phase 9, step 9.12): the indicator of a SECTIONOBJECT, which AutoCAD
// writes as the entity's proxy graphics (section_graphics.cpp), and its extents.

namespace hdxf {
namespace internal {

// The lines of a section's indicator, each a polyline of two points, in the order AutoCAD draws them. `primary` is drawn
// on the entity's layer (the section line), `secondary` on the layer Defpoints (the outline of a slice, a boundary or a
// volume). Plane: the segments of the vertices; slice and boundary (state 2): and the rest of the outline through the
// back vertices; volume (state 4): the same at the top and the bottom and the edges between them.
struct SectionLines {
    std::vector<std::vector<Point3>> primary;
    std::vector<std::vector<Point3>> secondary;
};

SectionLines section_lines(const SectionObjectData& d);

// The indicator as AutoCAD's proxy graphics (92/160 + 310): the indicator's color, the entity's layer, color and linetype,
// the lines, and the layer Defpoints. `defpoints_found` false: the drawing has no such layer (index 0 is used).
// Empty when the section has fewer than two vertices.
std::vector<uint8_t> section_indicator_graphics(const SectionObjectData& d, const EntityProperties& props,
                                                const ProxyTables& tables, bool* defpoints_found = nullptr);

// Whether a stream has the shape section_indicator_graphics() makes (the commands in AutoCAD's order, every line of two
// points): such an indicator follows the section when it changes; any other is kept as it was read
bool section_graphics_standard(const std::vector<uint8_t>& stream);

} // namespace internal
} // namespace hdxf
