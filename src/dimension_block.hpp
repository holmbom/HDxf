#pragma once

#include "hdxf.hpp"
#include "text_layout.hpp"
#include <string>
#include <vector>

// A DIMENSION's block as AutoCAD builds it (rättningsfas R.7): extension lines, dimension
// line or arc, arrowheads, the text as MTEXT (TEXT in R12), the definition points — from
// its dimension style with the entity's own overrides. Measured against AutoCAD's blocks in
// ACadSharp's samples and ODA File Converter's (whose dimension blocks match AutoCAD's).

namespace hdxf {
namespace internal {

// What building a dimension gives
struct DimensionBuild {
    std::vector<Entity> entities;           // the block's
    Point3 definition_point;                // 10 as AutoCAD keeps it
    Point3 text_position;                   // 11: the text's middle
    Point3 arc_point;                       // 16 (OCS) of a 2-line angular dimension
    double measurement = 0.0;               // 42
    std::vector<std::string> arrows;        // built-in arrow blocks the entities insert
};

// The style a dimension is drawn with: its dimension style (Standard, as the writers
// write it, when the drawing has none of that name) with the entity's overrides (XDATA
// ACAD DSTYLE, and ACAD_DSTYLE_… as in a style)
DimStyle dimension_style(const Drawing& dwg, const Entity& dimension);

// AutoCAD's Standard when the drawing has none (what the writers write)
DimStyle default_standard_dim_style();

// The dimension's text as AutoCAD writes it in the block's MTEXT (format codes, UTF-8):
// the measurement in the style's units, prefix, suffix, alternate units, tolerances — or
// the text override (<> for the measurement)
std::string dimension_text(const DimStyle& style, const DimensionData& d, double measurement,
                           bool paper_space, DxfVersion version);

// Builds the block; false (error says why) when the dimension cannot be drawn
bool build_dimension(const Drawing& dwg, TextLayout& text, const Entity& dimension,
                     DxfVersion version, DimensionBuild& out, std::string& error);

// The measurement (42) of a dimension as it is, as the builder measures it: a length in the
// dimension's plane, or an angle in radians
double dimension_measurement(const DimensionData& d, const Point3& extrusion);
// The same for an entity of any of the three dimension types (step 9.17): an ARC_DIMENSION's is its arc length (radius times
// the sweep), a LARGE_RADIAL_DIMENSION's its radius
double dimension_entity_measurement(const Entity& e, const DimensionData& d);

// AutoCAD's built-in arrowheads (_ClosedBlank, _Dot, _ArchTick …; _ClosedFilled is drawn
// as a SOLID, not a block): whether a name is one, and its block
bool is_builtin_arrow(const std::string& name);
Block builtin_arrow_block(const std::string& name);
// The built-in arrowhead a DIMBLK name of R13 or R14 stands for ("ARCHTICK" is `_ArchTick`); the name
// itself for any other version or name
std::string builtin_arrow_name(const Drawing& dwg, const std::string& name, DxfVersion version);
// Whether a line stops one arrow size short of the arrowhead's tip (its body): the built-in
// ones as ODA File Converter's dimension blocks have them, any other block too
bool arrow_trims(const std::string& name);

} // namespace internal
} // namespace hdxf
