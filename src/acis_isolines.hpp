#pragma once

#include "acis.hpp"
#include "proxy_graphics.hpp"
#include <cstdint>
#include <string>
#include <vector>

// The isolines of a surface body as AutoCAD draws them (plan phase 9, step 9.16; plan 2026-10-27-fas-9-typer.md,
// "NURBSURFACE och 2004-ytornas isolinjer"): the proxy graphics of the surface types in DXF 2000 and 2004 — the
// edges of the body's faces, the isolines of each face and the vertices — from the body's own ACIS geometry
// (src/acis.cpp's model, nothing is approximated by a tessellation of HDxf's own: where AutoCAD draws a curve as a
// polyline it cuts each knot span by halves as below, and that is what is done here).
//
// What AutoCAD 2025 writes (doc/facit/acad_{2000,2004}_dxf_surfaces*.dxf and the corpus' Surface_2004; the notes in
// doc/facit/fas9_notes/F9.18.md):
//   - one command 19 (marker) per drawn curve: 4 n + 1 for the isolines of the n-th face, 4 n + 2 for an edge (the
//     n-th edge met going through the faces, their loops and the coedges of each loop), 22 (true color) after it,
//     then the curve: a line as a polyline of two points (6), a whole circle or ellipse between fill 2 and fill 1
//     (3 for a circle, 44 for an ellipse), a part of a circle as 5 (three points: start, middle, end) and a
//     spline as a polyline; the vertices last, as polylines of two equal points, under one 22
//   - the isolines of a face: the first family are the curves along u (a v each, SURFU = 71 of them), the second
//     the curves along v (SURFV = 72 of them); k / (n + 1) of the face's parameter range for k = 1 … n; in a
//     direction the face runs round (a cone's or a sphere's v, a torus's u and v) only part of the way round, the
//     number is the share of the turn the face has times n, rounded (not for a surface of revolution of a curve: n
//     there too); a whole turn has n parts, n + 1 when edges lie along its start line from end to end
//   - a spline: each knot span of degree 2 or more is one piece when its middle and quarter points are within T of the
//     chord's points at the same fractions, else halved, and each half halved while |C(middle) - (C(a) + C(b)) / 2| > T;
//     T = view height * 1.4055e-4 * 1000 / VIEWRES (the view AutoCAD saves with — the active VPORT's 40 and 72) for an
//     isoline, two thirds of it for an edge; a span of degree 1 is its two ends

namespace hdxf {
namespace internal {

struct IsolineOptions {
    int u_isolines = 6;                         // 71: the curves along u
    int v_isolines = 6;                         // 72: the curves along v
    double tolerance = 0.0;                     // T of an isoline; 0: from view_height
    double view_height = 100.0;                 // the active VPORT's height (group 40)
    int circle_zoom = 1000;                     // the active VPORT's circle zoom percent (VIEWRES, group 72): T is 1000 / it times as big
    int32_t color = static_cast<int32_t>(0xC0000000);   // command 22's value: by layer
    double edge_ratio = 2.0 / 3.0;              // an edge's tolerance over an isoline's
};

// The edges, isolines and vertices of the first body of `file` as proxy graphics commands (the whole stream: its
// size and count first, as ProxyGraphics::bytes() has it). False when a face's surface or an edge's curve is of
// a kind this does not draw: `why` then names it, and nothing is made.
bool acis_isoline_graphics(const AcisFile& file, const IsolineOptions& options, ProxyGraphics& out, std::string& why);

// A spline surface of one span in v whose columns of control points are evenly spaced on lines: AutoCAD draws a loft
// of it along v through its sections, which the body does not give
bool acis_straight_in_v(const AcisSurface& s);

} // namespace internal
} // namespace hdxf
