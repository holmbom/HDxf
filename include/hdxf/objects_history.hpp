#pragma once

// The solid history records.

#include "core.hpp"
#include "entity_surface.hpp"
#include "eval_expr.hpp"
#include "objects_material.hpp"

namespace hdxf {

// --- 9.14 solid history (plan 2026-10-27-fas-9-typer.md, step 9.14; solid_history.cpp, dxf_writer_history.cpp) ----------
//
// A 3DSOLID made with SOLIDHIST 1 (2007 and later) has a history: ACSH_HISTORY_CLASS (owned by the solid, which points at
// it with its 350), the ACAD_EVALUATION_GRAPH the history owns (step 9.9) and one node object per operation, each owned
// by the graph and held by one of the graph's nodes. A node has AcDbEvalExpr (EvalExprData: its node id in the graph),
// AcDbShHistoryNode (what every node shares) and its own subclass(es); the primitives and the operations on a body that
// has no primitive of its own (the BREP) have AcDbShPrimitive between. The groups are AutoCAD's own: the facit of
// doc/facit/fas9_history.scr (AutoCAD 2025's core, 2007 … 2018) and AutoCAD's solids of the older facit
// (acad_<version>_dxf_solids.dxf), ACadSharp's sample_AC1021 … AC1032, jscad's 3Ddraw, LibreDWG's example_2007. Group
// meanings that no AutoCAD file establishes have a name of their place (values_42, flag_280): none is invented. The history
// is not the solid's geometry: changing a node never changes the solid's ACIS data, and the history adds no extents.

// A bit stream of an entity: AutoCAD's own serialization of a curve, a region or a face (90 … 310), kept as it is. AutoCAD's
// files have one byte more than the bits fill (672 bits, 85 bytes): the bytes are held as written and the bit count is
// the 90 value, never recomputed from the bytes
struct ShBitStream {
    int32_t bits = 0;                   // 90 (profile, path), 94 (loft section), 97 (guide), 99 (loft path)
    std::vector<uint8_t> data;          // 310
};

// The same entity as ACIS text, the form AutoCAD 2025 uses for a region and a 3D polyline: 70 (1 in every file) and the
// SAT lines (1), enciphered as in a 3DSOLID's; a line of 255 characters or more is its 3s and the 1 that ends it
struct ShSatSource {
    int16_t version = 1;                // 70
    std::vector<std::string> lines;     // 1 (3)
};

// The source geometry of a node: a class id (92, 93, 96, 98) and its data, a bit stream or SAT lines; class id 0 and no
// data = none (an extrusion has no path; a loft has no guide curves)
struct ShSourceGeometry {
    int32_t class_id = 0;
    std::variant<std::monostate, ShBitStream, ShSatSource> data;

    bool has_data() const { return data.index() != 0; }
    const ShBitStream* bit_stream() const { return std::get_if<ShBitStream>(&data); }
    const ShSatSource* sat() const { return std::get_if<ShSatSource>(&data); }
};

// ACSH_HISTORY_CLASS (AcDbShHistory)
struct ShHistoryData {
    int32_t version = 33;               // 90
    int32_t maintenance = 327;          // 91
    std::string graph;                  // 360: the history's ACAD_EVALUATION_GRAPH (a hard owner); empty = none (written 0)
    int32_t last_node_id = 0;           // 92: the highest node id of the graph (0 when the history has no node)
    int16_t flag_280 = 0;               // 280: 0 in every AutoCAD file
    int16_t flag_281 = 1;               // 281: 1 or 0 in AutoCAD's files, not established what it says
};

// AcDbShHistoryNode, the second subclass of every node (after AcDbEvalExpr)
struct ShHistoryNodeData {
    int32_t version = 33;               // 90
    int32_t maintenance = 327;          // 91
    // 40 … 55, sixteen values in the file's order (row by row, the translation in 43, 47 and 51): the node's placement of
    // its primitive. AutoCAD's files write 50, 51 and 55 (the matrix's (2,2), (2,3) and (3,3)) multiplied by 180/pi
    // (57.29577951308232): an identity is 1 0 0 0, 0 1 0 0, 0 0 K 0, 0 0 0 K with K = 180/pi, and a box moved to (10, 7.5, 10)
    // has 43 = 10, 47 = 7.5 and 51 = 572.9577951308232. The values are held as the file has them (transform() gives the matrix
    // with the factor taken away: AutoCAD's ROTATE3D, MOVE and MIRROR3D of the facit show it; the 16 are never fed back
    // through it, so what was read is written as it was)
    std::array<double, 16> matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 57.29577951308232, 0, 0, 0, 0, 57.29577951308232};
    int16_t color = 256;                // 62: 256 (by layer) in every AutoCAD file
    int32_t history_id = 0;             // 92: 1, 2, 3 … in the order the primitives were made; 0 for an operation
    std::string material;               // 347: a hard pointer to a MATERIAL (the "Global" one in AutoCAD's files); empty = 0

    // The matrix with 50, 51 and 55 divided by 180/pi: a row-by-row homogeneous matrix (the translation in 3, 7 and 11)
    std::array<double, 16> transform() const {
        std::array<double, 16> m = matrix;
        constexpr double k = 57.29577951308232;
        m[10] /= k;
        m[11] /= k;
        m[15] /= k;
        return m;
    }
    // Sets `matrix` from such a matrix (the inverse of transform())
    void set_transform(const std::array<double, 16>& m) {
        constexpr double k = 57.29577951308232;
        matrix = m;
        matrix[10] *= k;
        matrix[11] *= k;
        matrix[15] *= k;
    }
};

// 90 and 91 at the head of the node's own subclass (33 and 327 in AutoCAD 2025's files, 31 and 55/125/29 in older ones)
struct ShPartVersion {
    int32_t version = 33;
    int32_t maintenance = 327;
};

// ACSH_BOX_CLASS (AcDbShBox)
struct ShBoxData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    double length = 1.0;                // 40
    double width = 1.0;                 // 41
    double height = 1.0;                // 42
};

// ACSH_WEDGE_CLASS (AcDbShWedge)
struct ShWedgeData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    double length = 1.0;                // 40
    double width = 1.0;                 // 41
    double height = 1.0;                // 42
};

// ACSH_SPHERE_CLASS (AcDbShSphere)
struct ShSphereData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    double radius = 1.0;                // 40
};

// AcDbShCylinder: the part a cylinder and a cone share. A cone is this subclass followed by an AcDbShCone with no group
struct ShCylinderBody {
    ShPartVersion part;
    double height = 1.0;                // 40
    double base_radius_1 = 1.0;         // 41: along the node's first axis (an elliptical cylinder: 4 and 10)
    double base_radius_2 = 1.0;         // 42
    double top_radius = 1.0;            // 43: a cylinder's equals the first base radius, a cone's is 0 (a frustum's 5)
};

// ACSH_CYLINDER_CLASS (AcDbShCylinder)
struct ShCylinderData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShCylinderBody body;
};

// ACSH_CONE_CLASS (AcDbShCylinder, then the empty AcDbShCone)
struct ShConeData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShCylinderBody body;
};

// ACSH_TORUS_CLASS (AcDbShTorus)
struct ShTorusData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    double major_radius = 1.0;          // 40
    double minor_radius = 0.25;         // 41
};

// ACSH_PYRAMID_CLASS (AcDbShPyramid)
struct ShPyramidData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    double height = 1.0;                // 40
    int32_t sides = 4;                  // 92
    double radius = 1.0;                // 41: the circumscribed radius (PYRAMID 15 with 5 sides is 18.54101966249684)
    double top_radius = 0.0;            // 42: likewise (the frustum's top, 0 for the apex)
};

// AcDbShSweepBase, the subclass of an extrusion and a sweep: the vector, the profile and the path, the options of a
// sweep (SurfaceSweep, as EXTRUDEDSURFACE's and SWEPTSURFACE's: 42 … 49, the two matrices, 290 … 296, 11)
struct ShSweepBaseData {
    ShSweepBaseData() { options.solid = true; }
    ShPartVersion part;
    Point3 vector;                      // 10: the extrusion vector (0, 0, 25 for a height of 25)
    ShSourceGeometry profile;           // 92 + 90, 310 or 70, 1…
    ShSourceGeometry path;              // 93 + 90, 310 or 70, 1…; class id 0 and no data: no path (an extrusion)
    SurfaceSweep options;               // 42 … 49, 46 and 47 (16 values each; empty = identity), 290 … 296, 11
};

// ACSH_EXTRUSION_CLASS (AcDbShSweepBase, then the empty AcDbShExtrusion)
struct ShExtrusionData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShSweepBaseData base;
};

// ACSH_SWEEP_CLASS (AcDbShSweepBase, then the empty AcDbShSweep): an extrusion along a path is recorded as a sweep too
struct ShSweepData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShSweepBaseData base;
};

// ACSH_REVOLVE_CLASS (AcDbShRevolve; the names of REVOLVEDSURFACE's)
struct ShRevolveData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    Point3 axis_point;                  // 10 (the middle of the axis the user gave)
    Point3 axis_vector = {0.0, 0.0, 1.0};   // 11
    double angle = 6.283185307179586;   // 40 (radians: 270 degrees is 4.71238898038469)
    double start_angle = 0.0;           // 41
    ShSourceGeometry profile;           // 92 + 90, 310 or 70, 1…
    double draft_angle = 0.0;           // 43
    double start_draft_distance = 0.0;  // 44
    double end_draft_distance = 0.0;    // 45
    double twist = 0.0;                 // 46
    bool solid = true;                  // 290
    bool close_to_axis = false;         // 291
};

// ACSH_LOFT_CLASS (AcDbShLoft; the names of LOFTEDSURFACE's)
struct ShLoftData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    std::vector<ShSourceGeometry> sections;     // 92 (the count), then 93 + 94, 310 each
    std::vector<ShSourceGeometry> guides;       // 95, then 96 + 97, 310 each
    ShSourceGeometry path;              // 98 (class id; 0 = no path), 99 + 310
    int16_t plane_normal_lofting = 0;   // 70
    double start_draft_angle = 1.5707963267948966;  // 41
    double end_draft_angle = 1.5707963267948966;    // 42
    double start_draft_magnitude = 0.0; // 43
    double end_draft_magnitude = 0.0;   // 44
    bool arc_length_parameters = false; // 290
    bool no_twist = true;               // 291
    bool align_direction = true;        // 292
    bool simple_surfaces = true;        // 293
    bool closed = false;                // 294
    bool solid = true;                  // 295
    bool ruled = false;                 // 296
    bool virtual_guide = true;          // 297
};

// What a Boolean node does to its two operands (AcDb::BoolOperType: 280 is 0 for UNION, 1 for INTERSECT, 2 for SUBTRACT in
// AutoCAD 2025's own files)
enum class ShBooleanOperation : int16_t { Union = 0, Intersect = 1, Subtract = 2 };

// ACSH_BOOLEAN_CLASS (AcDbShBoolean, no AcDbShPrimitive)
struct ShBooleanData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    ShBooleanOperation operation = ShBooleanOperation::Union;   // 280
    int32_t first_node_id = 0;          // 92: the EvalExpr node id (not a handle) of the operand
    int32_t second_node_id = 0;         // 93: the one the first is united with, intersected with or reduced by
};

// ACSH_BREP_CLASS (AcDbShPrimitive, AcDbShBrep): a body AutoCAD keeps as it is (what BREP leaves) — its ACIS as SAT lines
struct ShBrepData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    ShSatSource sat;                    // 70, 1…
};

// ACSH_CHAMFER_CLASS (AcDbShChamfer, no AcDbShPrimitive)
struct ShChamferData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    int32_t base_node_id = 0;           // 92: the node (EvalExpr node id) whose body is chamfered
    double distance_1 = 1.0;            // 41
    double distance_2 = 1.0;            // 42
    std::vector<int32_t> edge_ids;      // 93 (the count), then 94 each: the persistent ids of the edges (step 9.10's managers)
    int32_t face_id = 0;                // 95: the base face's id (the id after the last edge's in AutoCAD's files)
};

// One edge of a fillet (ACSH_FILLET_CLASS): its persistent id (94) and its three values, one in each of the lists 41, 42
// and 43 (41 is the radius; 42 and 43 are 0.0 for a fillet of one radius, their role is not established)
struct ShFilletEdge {
    int32_t edge_id = 0;                // 94
    double radius = 1.0;                // 41
    double value_42 = 0.0;              // 42
    double value_43 = 0.0;              // 43
};

// ACSH_FILLET_CLASS (AcDbShFillet, no AcDbShPrimitive). AutoCAD writes four counted lists after the base node (93 + 94,
// 95 + 41, 96 + 42, 97 + 43) with the same count in every file
struct ShFilletData {
    EvalExprData eval;
    ShHistoryNodeData node;
    ShPartVersion part;
    int32_t base_node_id = 0;           // 92
    std::vector<ShFilletEdge> edges;
};

// --- end 9.14 ---

} // namespace hdxf
