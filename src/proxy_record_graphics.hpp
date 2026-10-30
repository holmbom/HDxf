#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <string>
#include <vector>

// Reading proxy graphics (phase 9, step 9.3): the stream AcGi's worldDraw() leaves for a program that does not
// know an entity (92/160 + 310 of AcDbEntity, and the graphics of a proxy class, 96/162 + 311): the total size
// and the number of commands, then each command as its size, its number and its data, little-endian. The
// writer's side is proxy_graphics.hpp (ProxyGraphics); this is what a proxy entity's and an OLE frame's extents,
// and the later families that draw proxy graphics, read of it.
//
// A command is `size` bytes: the size itself, the type and the data. The types HDxf reads are the ones the
// writer makes and the ones that draw geometry: 1 extents, 2 circle, 3 circle through 3 points, 4 arc, 5 arc
// through 3 points, 6 polyline, 7 polygon, 8 mesh, 9 shell, 11 and 38 text, 12 xline, 13 ray, 29 and 30 push a
// transform, 31 pop it, 32 polyline with a normal, 33 LWPOLYLINE (DWG's bit stream) and 44 elliptical arc. The
// rest (colors, layers, linetypes, markers, fill, lineweights …) is state that draws nothing.

namespace hdxf {
namespace internal {

// One command of a stream: its type and its data (the bytes after size and type), pointing into the stream
struct ProxyCommandRef {
    uint32_t type = 0;
    const uint8_t* data = nullptr;
    size_t size = 0;
};

// A stream cut into its commands. `complete`: the total size and the count the stream gives are what the bytes
// hold; otherwise `problem` says what is not, and `commands` are the whole commands that were found
struct ProxyStreamView {
    std::vector<ProxyCommandRef> commands;
    bool complete = false;
    std::string problem;
};

ProxyStreamView decode_proxy_stream(const std::vector<uint8_t>& bytes);

// What a stream draws, in the coordinates it is drawn in (the commands' transforms applied)
struct ProxyDrawn {
    std::vector<Point3> points;         // points the geometry has: its vertices, the extremes of its curves and the
                                        // curves sampled every 2 degrees (a later affine map of a curve stays inside)
    bool has_declared = false;          // the stream has an extents command (1)
    Point3 declared_min;
    Point3 declared_max;
    int commands_drawn = 0;             // the commands that gave geometry
    int commands_skipped = 0;           // commands of a type that draws geometry and is not read (an xline and a ray
                                        // are infinite and give none: not counted)
};

// `code_page` is the drawing's, what narrow text is in
ProxyDrawn proxy_graphics_drawn(const std::vector<uint8_t>& bytes, int code_page);

// The points AutoCAD's extents take from a stream: the extents command's two corners when the stream has one
// (the extents the application recorded), else the geometry
std::vector<Point3> proxy_graphics_extent_points(const std::vector<uint8_t>& bytes, int code_page);

} // namespace internal
} // namespace hdxf
