#pragma once

// SECTIONOBJECT, ARC_DIMENSION, LARGE_RADIAL_DIMENSION and TurboCAD's point.

#include "entity.hpp"

namespace hdxf {

// --- 9.12 sections (plan phase 9, step 9.12; section.cpp) ---------------------------------------------------------------
// SECTIONOBJECT (2000+, AcDbSection): a section plane, as AutoCAD 2025 writes it (doc/facit/acad_<ver>_dxf_objects*.dxf and
// the sections of fas9_sections.scr). The entity's own proxy graphics (AcDbEntity 92/160 + 310) are the indicator: the
// section line as one polyline per segment (section_graphics.cpp); AutoCAD's XDATA IsSlice and ThicknessDepth are the
// entity's XDATA. Groups of the 2018 reference's tables that AutoCAD's files do not show (63/411 for the color) are not
// invented: the indicator color is 62, and 2000's records have none.
struct SectionObjectData {
    int state = 1;                      // 90 (1 plane; the AutoCAD API's SectionState: 2 boundary, 4 volume)
    int flags = 1;                      // 91 (1 from -SECTIONPLANE; 4 in the sections the API made with a jog)
    std::string name;                   // 1
    Point3 vertical_direction = {0.0, 0.0, 1.0};    // 10, 20, 30
    double top_height = 5.0;            // 40
    double bottom_height = 5.0;         // 41
    int indicator_transparency = 70;    // 70 (percent)
    std::optional<int16_t> indicator_color;         // 62 (2004 and later; AutoCAD's default is 9); none in a 2000 record
    std::vector<Point3> vertices;       // 92 counts them, 11 21 31 each: the section line, in the WCS
    std::vector<Point3> back_vertices;  // 93 counts them, 12 22 32 each
    std::string settings_handle;        // 360: the SECTION_SETTINGS it owns
    std::vector<uint8_t> proxy_graphics;    // the entity's 92/160 + 310 as bytes: the indicator
};

// --- 9.17 the special dimensions (plan 2026-10-27-fas-9-typer.md, step 9.17; dimension_special.cpp) ------------------------
// ARC_DIMENSION (AcDbArcDimension) and LARGE_RADIAL_DIMENSION (AcDbRadialDimensionLarge, DIMJOGGED): both are an
// AcDbDimension (DimensionData: the block, 10, 11, 70, the style, the text; its dim_type is the low nibble of 70, 5 for the
// arc length and 9 for the large radius, which DimensionType has no name for) and the groups of their own. AutoCAD's files
// (doc/facit/fas9_special_dimensions.scr and Erik's acad_<version>_dxf_dimensions*.dxf, 2000 … 2018) have both in 2000 and
// later. Their block (*D<n>) is built by HDxf as for any dimension (dimension_block.cpp); the proxy graphics (AcDbEntity
// 92/160 + 310) are kept as read, and none is made for a new one.
struct ArcDimensionData {
    DimensionData dimension;            // AcDbDimension; 13/14/15 are def_point2/3/4 (the arc's ends and its center), 16 is
                                        // arc_point: the first point of the leader (when has_leader)
    bool is_partial = false;            // 70 of AcDbArcDimension (a part of the arc is dimensioned)
    double start_parameter = 0.0;       // 40 (AutoCAD's files: 0)
    double end_parameter = 0.0;         // 41 (radians; 0 in the files of a whole arc made with DIMARC)
    bool has_leader = false;            // 71
    Point3 leader_end;                  // 17
    std::vector<uint8_t> proxy_graphics;    // the entity's own 92/160 + 310
    // AcDbDimension's 72 and 41 (the reference's line spacing style and factor), as read: no AutoCAD file has them (9.S, Codex 10)
    std::optional<int16_t> line_spacing_style;
    std::optional<double> line_spacing_factor;
};

struct LargeRadialDimensionData {
    DimensionData dimension;            // AcDbDimension; 10 is the circle's center, 15 (def_point4) the point on the circle
    Point3 center_override;             // 13: where the dimension line starts (the "center location override")
    Point3 jog_point;                   // 14: where the jog crosses (AutoCAD: half way between 13 and 15)
    double leader_length = 0.0;         // 40
    std::vector<uint8_t> proxy_graphics;    // the entity's own 92/160 + 310
    // AcDbDimension's 72 and 41, as read: no AutoCAD file has them (9.S, Codex 10)
    std::optional<int16_t> line_spacing_style;
    std::optional<double> line_spacing_factor;
};

// --- 9.18 TurboCAD's point (tc_records.cpp) ---
// TCPOINTENTITY (entity, AcDbEntity + AcDbPoint + TcPointEntity): a TurboCAD point; its symbol is the entity's proxy graphics
// (AcDbEntity 92/160 and 310), which is what the drawing's extents count
struct TcPointData {
    PointData point;                    // AcDbPoint: 10 20 30, 50 (the thickness and the normal are in the entity's properties)
    int16_t version = 1;                // TcPointEntity 70
    int16_t flags = 4;                  // 71
    double size = 2.0;                  // 40
    std::vector<uint8_t> proxy_graphics;    // the entity's 92/160 + 310 as bytes: the symbol
};

} // namespace hdxf
