#pragma once

// The procedural surfaces and HELIX.

#include "entity.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// Surfaces (plan phase 3, B39): AutoCAD 2007's procedural surfaces — EXTRUDEDSURFACE,
// LOFTEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE, PLANESURFACE and the plain SURFACE. Each is
// its ACIS body (AcDbModelerGeometry, as 3DSOLID), its isolines (AcDbSurface) and what it was
// made from (its type's subclass, as the DXF reference and AutoCAD's files have it). AutoCAD
// writes them in 2007 with empty proxy graphics; in 2004 with their isolines as proxy
// graphics, which HDxf keeps but does not make — a new surface needs 2007.
// ---------------------------------------------------------------------------

// An entity a surface was made from, as the file has it: its class (90; a swept surface's
// path 91), the size of its data in bits (90 — AutoCAD's files have a byte more than the
// bits fill) and the data (310, in groups of 127 bytes)
struct SurfaceSource {
    int class_id = 0;
    int bits = 0;
    std::vector<uint8_t> data;
};

// A sweep's options: EXTRUDEDSURFACE's and SWEPTSURFACE's
struct SurfaceSweep {
    double draft_angle = 0.0;           // 42 (radians)
    double draft_start = 0.0;           // 43
    double draft_end = 0.0;             // 44
    double twist = 0.0;                 // 45
    double scale = 1.0;                 // 48
    double align_angle = 0.0;           // 49 (radians)
    std::vector<double> sweep_matrix;   // 46 (16, row by row; empty = identity)
    std::vector<double> path_matrix;    // 47 (16)
    bool solid = false;                 // 290
    int alignment = 0;                  // 70: 0 none, 1 align the sweep entity to the path,
                                        //     2 move it to the path, 3 the path to it
    int option_71 = 2;                  // 71: not in Autodesk's reference; AutoCAD writes 1 or 2
    bool align_start = true;            // 292
    bool bank = false;                  // 293
    bool base_point_set = false;        // 294
    bool sweep_transform_computed = true;   // 295
    bool path_transform_computed = false;   // 296
    Point3 reference_vector;            // 11: for the twist
};

// AcDbNurbSurface (NURBSURFACE, 2013 and later), in the order AutoCAD writes it (plan 9.16, F9.17). AutoCAD's files
// have 0 in every group — also for a surface converted from a loft or made in NURBS mode: the surface's knots and
// control points are in its ACIS body, not here (the name NURBSURFACE says nothing of knot fields)
struct NurbSurface {
    int16_t flag = 0;                   // 170
    bool flag_290 = false;              // 290
    Point3 point_10;                    // 10
    Point3 point_11;                    // 11
    Point3 point_12;                    // 12
    Point3 point_13;                    // 13
};

struct SurfaceData {
    AcisData acis;                      // AcDbModelerGeometry
    int u_isolines = 6;                 // 71
    int v_isolines = 6;                 // 72
    NurbSurface nurbs;                  // NURBSURFACE: AcDbNurbSurface
    // EXTRUDEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE: the entity extruded, revolved, swept
    SurfaceSource source;               // 90, 90, 310
    // The source's transform (16, row by row; empty = identity): EXTRUDEDSURFACE,
    // LOFTEDSURFACE and SWEPTSURFACE 40, REVOLVEDSURFACE 42
    std::vector<double> matrix;
    // EXTRUDEDSURFACE
    Point3 sweep_vector;                // 10
    // EXTRUDEDSURFACE, SWEPTSURFACE
    SurfaceSweep sweep;
    // SWEPTSURFACE: the path (91, 90, 310) and its transform (41)
    SurfaceSource path;
    std::vector<double> path_entity_matrix;
    // REVOLVEDSURFACE
    Point3 axis_point;                  // 10
    Point3 axis_vector = {0.0, 0.0, 1.0};   // 11
    double revolve_angle = 0.0;         // 40 (radians)
    double start_angle = 0.0;           // 41
    double revolve_draft_angle = 0.0;   // 43
    double start_draft_distance = 0.0;  // 44
    double end_draft_distance = 0.0;    // 45
    double revolve_twist = 0.0;         // 46
    bool revolve_solid = false;         // 290
    bool close_to_axis = false;         // 291
    // LOFTEDSURFACE: the curves — cross sections, guides, path, in the file's order (90, 90,
    // 310 each; the file does not say which is which)
    std::vector<SurfaceSource> curves;
    int plane_normal_lofting = 0;       // 70
    double start_draft_angle = 0.0;     // 41
    double end_draft_angle = 0.0;       // 42
    double start_draft_magnitude = 0.0; // 43
    double end_draft_magnitude = 0.0;   // 44
    bool arc_length_parameters = false; // 290
    bool no_twist = true;               // 291
    bool align_direction = true;        // 292
    bool simple_surfaces = true;        // 293
    bool closed = false;                // 294
    bool loft_solid = false;            // 295
    bool ruled = false;                 // 296
    bool virtual_guide = true;          // 297
};

// ---------------------------------------------------------------------------
// HELIX (plan phase 3, B39): AutoCAD 2007's helix, a spline (AcDbSpline, as SPLINE) and its
// definition (AcDbHelix). Its proxy graphics are the spline drawn as a polyline.
// ---------------------------------------------------------------------------

struct HelixData {
    SplineData spline;                  // AcDbSpline
    int major_version = 33;             // 90
    int maintenance_version = 29;       // 91
    Point3 axis_base;                   // 10
    Point3 start;                       // 11: where it starts (its base radius from the axis)
    Point3 axis_vector = {0.0, 0.0, 1.0};   // 12
    double radius = 1.0;                // 40: the top radius
    double turns = 3.0;                 // 41
    double turn_height = 1.0;           // 42
    bool right_handed = true;           // 290: 0 left, 1 right
    int constrain = 1;                  // 280: 0 turn height, 1 turns, 2 height
};

} // namespace hdxf
