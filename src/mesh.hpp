#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <vector>

// MESH as AutoCAD draws and measures it (plan phase 4, B48/B49)

namespace hdxf {
namespace internal {

// Each edge of the cage, in the edge list's order, as AutoCAD draws it: a polyline from its
// first vertex to its second — at the mesh's smoothing level through the points of the
// smoothed edge (B49)
std::vector<std::vector<Point3>> mesh_wire(const MeshData& d);

// Its proxy graphics as AutoCAD writes them (ACadSharp's samples 2000 … 2018): per edge the
// marker 8i + 2 and the edge's polyline
std::vector<uint8_t> mesh_proxy(const MeshData& d);

} // namespace internal
} // namespace hdxf
