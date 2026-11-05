#pragma once

// The annotation contexts and DIMASSOC.

#include "core.hpp"
#include "entity.hpp"
#include "entity_mleader.hpp"

namespace hdxf {

// --- 9.17 annotation contexts and DIMASSOC (plan 2026-10-27-fas-9-typer.md, step 9.17; object_context.cpp, dimassoc.cpp) ----
// An annotative entity has, in its extension dictionary, AcDbContextDataManager → ACDB_ANNOTATIONSCALES → a dictionary with
// one entry per scale ("*A1", "*A2" …) → one context object per scale, owned by that last dictionary. The context holds what
// the entity looks like at that scale. Every context is AcDbObjectContextData (70, 290) + AcDbAnnotScaleObjectContextData (340)
// + the groups of its class (AutoCAD 2025's own files: doc/facit/fas9_contexts.scr, Erik's acad_<version>_dxf_annotative*.dxf,
// 2000 … 2018). The 70 of the base (its version) is not the 70 of the class's own groups, and a TEXT's point is two-dimensional,
// a LEADER's three-dimensional: the structs keep what the files have.
struct ObjectContextBase {
    int16_t version = 4;                // 70 of AcDbObjectContextData (AutoCAD: 4)
    bool is_default = false;            // 290: the entity's default context (the scale it was made at)
    std::string scale;                  // 340: the SCALE object (ACAD_SCALELIST) of this context
};

struct TextContextData {                // ACDB_TEXTOBJECTCONTEXTDATA_CLASS
    ObjectContextBase base;
    int16_t flags = 0;                  // 70 (0 in AutoCAD's files)
    double rotation = 0.0;              // 50 (radians)
    Point2 insertion;                   // 10, 20 (no Z)
    Point2 alignment;                   // 11, 21 (no Z)
};

struct LeaderContextData {              // ACDB_LEADEROBJECTCONTEXTDATA_CLASS
    ObjectContextBase base;
    std::vector<Point3> points;         // 70 (the count), 10: the leader's vertices
    Point3 direction;                   // 11
    bool flag_290 = true;               // 290
    Point3 offset;                      // 12
    Point3 annotation_offset;           // 13
};

struct MTextContextData {               // ACDB_MTEXTOBJECTCONTEXTDATA_CLASS (also inside an MTEXT attribute's)
    ObjectContextBase base;
    int16_t flags = 1;                  // 70 (1 in AutoCAD's files)
    Point3 direction = {1.0, 0.0, 0.0}; // 10: the text's direction
    Point3 location;                    // 11: its insertion point
    double reference_width = 0.0;       // 40
    double defined_height = 0.0;        // 41
    double text_width = 0.0;            // 42: the text's own extents at this scale
    double text_height = 0.0;           // 43
    int16_t value_71 = 0;               // 71
};

struct MTextAttributeContextData {      // ACDB_MTEXTATTRIBUTEOBJECTCONTEXTDATA_CLASS: a text context and the MTEXT's (101)
    ObjectContextBase base;
    int16_t flags = 0;                  // 70
    double rotation = 0.0;              // 50
    Point2 insertion;                   // 10, 20
    Point2 alignment;                   // 11, 21
    bool flag_290 = true;               // 290
    std::optional<MTextContextData> embedded;   // 101 Embedded Object: the MTEXT context (its own 70 4, 290, 340 …)
};

struct MLeaderContextData {             // ACDB_MLEADEROBJECTCONTEXTDATA_CLASS
    ObjectContextBase base;
    // The groups of an MLEADER's context data (40 … 273, MLeaderContext) as AutoCAD writes them in the object; the style
    // (340) and the block (341) are handles here, not names
    MLeaderContext content;
};

struct BlkRefContextData {              // ACDB_BLKREFOBJECTCONTEXTDATA_CLASS
    ObjectContextBase base;
    double rotation = 0.0;              // 50 (radians)
    Point3 insertion;                   // 10
    Point3 scale = {1.0, 1.0, 1.0};     // 41, 42, 43
};

// AcDbDimensionObjectContextData: the dimension at the scale. The groups have no role in the references: they keep their codes
struct DimContextCommon {
    std::string block_name;             // 2: the *D block the dimension has at this scale
    bool flag_293 = false;              // 293
    Point2 text_location;               // 10, 20: the text's middle
    bool flag_294 = true;               // 294
    double value_140 = 0.0;             // 140
    bool flag_298 = false;              // 298
    bool flag_291 = false;              // 291
    int16_t value_70 = 0;               // 70
    bool flag_292 = false;              // 292
    int16_t value_71 = 0;               // 71
    int8_t value_280 = 0;               // 280
    bool flag_295 = false;              // 295
    bool flag_296 = false;              // 296
    bool flag_297 = false;              // 297
};

struct AlDimContextData {               // ACDB_ALDIMOBJECTCONTEXTDATA_CLASS (rotated and aligned dimensions)
    ObjectContextBase base;
    DimContextCommon dimension;
    Point3 dimension_line_point;        // 11 of AcDbAlignedDimensionObjectContextData
};

struct AngDimContextData {              // ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS
    ObjectContextBase base;
    DimContextCommon dimension;
    Point3 arc_point;                   // 11 of AcDbAngularDimensionObjectContextData
};

struct DmDimContextData {               // ACDB_DMDIMOBJECTCONTEXTDATA_CLASS (diameter)
    ObjectContextBase base;
    DimContextCommon dimension;
    Point3 point_11;                    // 11 of AcDbDiametricDimensionObjectContextData
    Point3 point_12;                    // 12
};

struct OrdDimContextData {              // ACDB_ORDDIMOBJECTCONTEXTDATA_CLASS
    ObjectContextBase base;
    DimContextCommon dimension;
    Point3 point_11;                    // 11 of AcDbOrdinateDimensionObjectContextData
    Point3 point_12;                    // 12
};

struct RadimContextData {               // ACDB_RADIMOBJECTCONTEXTDATA_CLASS
    ObjectContextBase base;
    DimContextCommon dimension;
    Point3 point_11;                    // 11 of AcDbRadialDimensionObjectContextData
};

struct RadimLgContextData {             // ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS (AutoCAD's name; the plan's ..._RADIMLG... is the same)
    ObjectContextBase base;
    DimContextCommon dimension;
    Point3 point_11;                    // 11 of AcDbRadialDimensionObjectContextData
    Point3 point_12;                    // 12 of AcDbRadialDimensionLargeObjectContextData
    Point3 point_13;                    // 13
};

struct FcfContextData {                 // ACDB_FCFOBJECTCONTEXTDATA_CLASS (a TOLERANCE)
    ObjectContextBase base;
    Point3 location;                    // 10
    Point3 direction = {1.0, 0.0, 0.0}; // 11
};

struct AnnotScaleContextData {          // ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS: the base alone (no file has it as a record)
    ObjectContextBase base;
};

// What follows the second 90 in the contexts AutoCAD wrote for a hatch with a dashed pattern (every file's own record: 90 536, a 290,
// and in a scale context 93 with the pattern's boundary as line edges). A hatch with a plain pattern has none of it
struct HatchContextExtension {
    int32_t value_90 = 0;               // 90 (536 in every file)
    bool flag_290 = false;              // 290 (1 in a view context, 0 in a scale context)
    std::optional<std::vector<HatchEdge>> edges;    // 93 (the count), 72 1, 10, 20, 11, 21 for each: line edges (the only kind in a file)
};

// AcDbHatchObjectContextData: the hatch pattern at the scale
struct HatchContextCommon {
    std::vector<HatchPatternLine> pattern_lines;    // 78 (the count), 53 43 44 45 46 79 49…
    double pattern_scale = 1.0;         // 40
    Point3 base_point;                  // 10, 20, 30
    int32_t value_90_a = 1;             // 90
    int32_t value_90_b = 1;             // 90
    bool flag_290 = true;               // 290
    std::optional<HatchContextExtension> extension; // the groups after them: a dashed pattern's
};

struct HatchScaleContextData {          // ACDB_HATCHSCALECONTEXTDATA_CLASS
    ObjectContextBase base;
    HatchContextCommon hatch;
};

struct HatchViewContextData {           // ACDB_HATCHVIEWCONTEXTDATA_CLASS
    ObjectContextBase base;
    HatchContextCommon hatch;
    std::string pointer_330 = "0";      // 330 of AcDbHatchViewContextData (a pointer: 0 in every file)
    Point3 normal = {0.0, 0.0, 1.0};    // 10
    double value_51 = 0.0;              // 51
    bool flag_290 = false;              // 290
};

// DIMASSOC (AcDbDimAssoc): the association of a dimension with the geometry it measures. 90 is a mask: bit 1, 2, 4 and 8 say
// which of the four points (the first extension line's origin, the second's, the third, the fourth) have a reference, which
// follow in the record in that order. A point's reference is an AcDbOsnapPointRef: the object snap, the main object (and, for
// an object inside a block or an xref, the chain of them), and for an intersection the second object.
struct DimAssocPointRef {
    std::string class_name = "AcDbOsnapPointRef";   // 1
    int16_t osnap_type = 0;             // 72 (1 end point, 3 center …; 13 and 14 as AutoCAD writes them)
    std::vector<std::string> main_handles;          // 331: the object (more than one: the path into a nested object)
    int16_t main_subentity_type = 0;    // 73 (2 an edge, AutoCAD's files; 1 and 3 for a solid's vertex and face)
    int32_t main_marker = 0;            // 91 (the subentity's marker; 0 for none)
    std::string xref_handle_string;     // 301 (a handle in an xref's drawing, kept as the text it is)
    double near_parameter = 0.0;        // 40 (where on the object a point snapped near it is)
    Point3 point;                       // 10, 20, 30 (the point; AutoCAD writes 2.0E+50 for z when it is not set)
    std::vector<std::string> intersection_handles;  // 332: the second object of an intersection
    std::optional<int16_t> intersection_subentity_type; // 74
    std::optional<int32_t> intersection_marker;     // 92
    std::string intersection_xref;      // 302
    int16_t last_point_ref = 0;         // 75
};

struct DimAssocData {
    std::string dimension;              // 330: the DIMENSION this associates (a soft pointer)
    int32_t flags = 0;                  // 90: bits 1, 2, 4, 8 = the four points that have a reference (kept in step with `points`); other bits as read
    int16_t trans_space = 0;            // 70 (1: the dimension is in paper space, the geometry in model space)
    int16_t rotated_type = 0;           // 71
    // The four points by their mask bit: an empty one has no reference (the bit is clear in `flags` when written)
    std::array<std::optional<DimAssocPointRef>, 4> points;
};

// --- end 9.17 ---

} // namespace hdxf
