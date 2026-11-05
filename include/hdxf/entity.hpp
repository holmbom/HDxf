#pragma once

// Entity and the data of its types. The types EntityData boxes are only declared here; their definitions are in
// entity_*.hpp and table.hpp.

#include "boxed.hpp"
#include "core.hpp"
#include "fwd.hpp"
#include "proxy.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// Entity data structs
// ---------------------------------------------------------------------------

struct LineData {
    Point3 start;
    Point3 end;
};

struct CircleData {
    Point3 center;
    double radius = 0.0;
};

struct ArcData {
    Point3 center;
    double radius = 0.0;
    double start_angle = 0.0;
    double end_angle = 360.0;
};

struct PointData {
    Point3 location;
    double x_axis_angle = 0.0;  // group 50: angle for UCS
};

struct TextData {
    Point3 position;
    double height = 2.5;
    std::string content;
    double rotation = 0.0;
    double width_factor = 1.0;
    double oblique_angle = 0.0;
    std::string style = "Standard";
    int generation_flags = 0;   // group 71: bit 2=backward, bit 4=upside-down
    int h_justify = 0;          // group 72
    int v_justify = 0;          // group 73
    Point3 alignment_point;     // groups 11, 21, 31
};

// ---------------------------------------------------------------------------
// Vertex (used inside POLYLINE)
// ---------------------------------------------------------------------------

struct Vertex {
    Point3 position;
    double start_width = 0.0;
    double end_width = 0.0;
    double bulge = 0.0;
    int flags = 0;
    double tangent = 0.0;       // group 50: curve-fit tangent direction
    int face[4] = {0, 0, 0, 0}; // groups 71-74: polyface mesh vertex indices
    // Group 91 (2010+): the vertex identifier; absent where AutoCAD writes none (a vertex that no
    // constraint or associative dimension refers to has none; the ones that do, 1, 2, 3 …). Kept per
    // vertex, so it follows the vertex when the list changes (plan 9.2, F9.21)
    std::optional<int32_t> id;
    std::string handle;
    std::vector<std::string> reactors;  // 330 in 102 {ACAD_REACTORS (2000+), as EntityProperties
    // The vertex's own layer, linetype and color (8, 6, 62; plan F15): an empty layer is the
    // polyline's, an empty linetype is none written. 62 = 0 (BYBLOCK) is kept as 0.
    // The file follows the polyline as AutoCAD's do, and these fields stay as they are: a vertex read from a file is written with
    // the layer, linetype or color the polyline was given when it had the polyline's value as read and was not changed itself; a
    // vertex the program made is written with the polyline's linetype when its own is empty and the polyline's color when its own
    // is ByLayer (a vertex set to ByLayer cannot be told from one never set). The same goes for the SEQEND's seqend_* fields.
    std::string layer;
    std::string linetype;
    Color color;
    std::vector<XdataList> xdata;       // the vertex's own XDATA (plan F16)
    RawRecord raw;              // the VERTEX record (preserved records of the file read)
};

struct PolylineData {
    int flags = 0;
    double header_x = 0.0;              // groups 10 and 20 of the POLYLINE record (plan F14): 0 in
    double header_y = 0.0;              //     AutoCAD's own polylines, a point of the polyline in the
                                        //     3D polylines it exports from other types
    double elevation = 0.0;             // group 30: in the OCS when 2D, the WCS when 3D
    double default_start_width = 0.0;   // group 40
    double default_end_width = 0.0;     // group 41
    int m_count = 0;                    // group 71 (AutoCAD writes a polyface's count above 32767 cut to 16 bits, so a read one can be negative)
    int n_count = 0;                    // group 72
    int smooth_m = 0;                   // group 73
    int smooth_n = 0;                   // group 74
    int smooth_type = 0;                // group 75
    std::vector<Vertex> vertices;
    std::string seqend_handle;
    // The SEQEND's own layer, linetype and color (plan F15), as Vertex's
    std::string seqend_layer;
    std::string seqend_linetype;
    Color seqend_color;
    RawRecord seqend_raw;               // the SEQEND record (preserved records of the file read)

    bool is_closed() const        { return (flags & 1) != 0; }
    bool is_3d_polyline() const   { return (flags & 8) != 0; }
    bool is_polygon_mesh() const  { return (flags & 16) != 0; }
    bool is_mesh_closed_n() const { return (flags & 32) != 0; }
    bool is_polyface_mesh() const { return (flags & 64) != 0; }
};

// ---------------------------------------------------------------------------
// TRACE and SOLID (4 corner points)
// ---------------------------------------------------------------------------

struct TraceData {
    Point3 p0;
    Point3 p1;
    Point3 p2;
    Point3 p3;
};

struct SolidData {
    Point3 p0;
    Point3 p1;
    Point3 p2;
    Point3 p3;
};

// ---------------------------------------------------------------------------
// 3DFACE
// ---------------------------------------------------------------------------

struct Face3DData {
    Point3 p0;
    Point3 p1;
    Point3 p2;
    Point3 p3;
    int invisible_edges = 0;    // group 70: bit flags for invisible edges
};

// ---------------------------------------------------------------------------
// SHAPE
// ---------------------------------------------------------------------------

struct ShapeData {
    Point3 insertion_point;
    double size = 0.0;
    std::string name;
    double rotation = 0.0;
    double x_scale = 1.0;
    double oblique_angle = 0.0;
};

// ---------------------------------------------------------------------------
// MTEXT (2000+) and the text of a multiline attribute
// ---------------------------------------------------------------------------

// MTEXT's columns (plan phase 6, B73). Up to 2013 AutoCAD keeps them in the entity's XDATA
// ACAD (ACAD_MTEXT_COLUMN_INFO_BEGIN … END: 75 type, 79 auto height, 76 count, 78 reversed,
// 48 width, 49 gutter, 50 and one 1040 per height); 2018 has them in the entity's embedded
// object (101 Embedded Object: 71, 72, 44, 45, 73, 74 and the heights in 46). A single column
// is column data too. Reading gives the same values from both.
//
// Up to 2013 the other columns of a text with several are entities of their own — MTEXT with
// the rest of the text (the static columns' pieces end with the column break \N; the dynamic
// ones' are empty), linked from the first by ACAD_MTEXT_COLUMNS (47 and a 1005 for each). They
// are in the drawing's entity lists like any entity (`linked` has their handles; their
// MTextData::column_parent says whose they are); the library makes, moves and removes them with
// the text when the drawing is saved. In 2018 the text is one entity.
struct MTextColumns {
    int type = 2;                       // 75 / 71: 0 none, 1 static, 2 dynamic
    int count = 1;                      // 76 / 72
    double width = 0.0;                 // 48 / 44
    double gutter = 0.0;                // 49 / 45
    bool auto_height = false;           // 79 / 73
    bool reversed = false;              // 78 / 74
    std::vector<double> heights;        // 50 (their count) and 1040 / 46
    std::vector<std::string> linked;    // up to 2013: 1005 of ACAD_MTEXT_COLUMNS, the other columns
};

// What the 2018 embedded object of an MTEXT has besides the columns (plan B73, B74): AutoCAD's
// own values for the text's size as drawn (42, 43), kept as read; HDxf writes what its layout
// measures when it has the font, these otherwise. The embedded object's other groups — 70
// (the attachment point), 10 (the direction), 11 (the insertion point), 40 (the width), 41 (the
// defined height) — are the entity's own attachment, x_axis_direction, insertion_point,
// reference_width and defined_height: AutoCAD takes the embedded object's values over the entity's
// (checked by AutoCAD's core reading a file where they differ), so reading sets those from it
// and writing writes those into it.
struct MTextEmbedded {
    double measured_width = 0.0;        // 42
    double measured_height = 0.0;       // 43
};

// The text is one string with its formatting codes as written (\P new paragraph, \f font,
// {…} …); the file splits it into groups of 250 characters (3 …, then 1). A rotation (50)
// in the file is read as the direction it gives.
// MTEXT's background fill (2004+), after 44 as AutoCAD writes it when it is on
struct MTextBackground {
    int flags = 0;                      // 90, bits: 1 on (the color below), 2 the drawing
                                        //     window's color instead — 3, with 1; 2 alone
                                        //     makes a file TrueView does not open; 16 the text
                                        //     frame (with or without 1)
    Color color;                        // 63 (ACI), 421 (RGB), 431 (color book name)
    double scale = 1.5;                 // 45: the box around the text, times the height
    Transparency transparency;          // 441 (the reference: not implemented)
    // Up to 2013 the frame (bit 16) is XDATA ACAD_MTEXT_TEXT_BORDERS (80 flags, 46 the scale, 81
    // and a 5 with a 1005 for each) and LWPOLYLINE entities of its own, one for each column
    // (AutoCAD's mtext_text_frame and mtext_framed_columns, ezdxf's test data). Like the other
    // columns they are in the entity lists and made, moved and removed with the text.
    std::vector<std::string> border_handles;
};

struct MTextData {
    Point3 insertion_point;             // 10, 20, 30
    double height = 2.5;                // 40: nominal text height
    double reference_width = 0.0;       // 41: 0 = no wrapping
    int attachment = 1;                 // 71: 1 top left, 2 top center … 9 bottom right
    int drawing_direction = 1;          // 72: 1 left to right, 3 top to bottom, 5 by style
    std::string content;                // 1, 3
    std::string style = "Standard";     // 7
    Point3 x_axis_direction = {1.0, 0.0, 0.0}; // 11, 21, 31 (WCS)
    int line_spacing_style = 1;         // 73: 1 at least, 2 exactly
    double line_spacing_factor = 1.0;   // 44
    MTextBackground background;         // 90 … 441 (2004+)
    double defined_height = 0.0;        // 46 (2007+): the height it was given (columns); in no
                                        //     reference, AutoCAD writes it
    OptionalBoxed<MTextColumns> columns;    // the columns, when it has column data (B73)
    OptionalBoxed<MTextEmbedded> embedded;  // 2018's embedded object, beyond the columns
    // Up to 2013, of the MTEXT entity that is another column of a text (MTextColumns::linked):
    // the handle of the first. Not written; made by reading and saving.
    std::string column_parent;
};


// A multiline attribute or attribute definition (plan phase 6, B75): in 2018 the ATTRIB/ATTDEF has
// 71 (2 attribute, 4 definition), 72 (the number of secondary attributes), 11 and an embedded
// object (101) with its MTEXT's AcDbMText groups, no entity of its own; up to 2013 the same data
// is an XRECORD ACAD_MLATT in the attribute's extension dictionary (70, 70, 70, 40, 2, 1 "Embedded
// Object", the MTEXT) with ACAD_MATT_HEIGHT beside it. The outer text of the ATTRIB (1) is empty
// then; the text is `text`.
struct MultilineAttribute {
    int type = 2;                       // 71: 1 single line (no text of its own: ODA writes it for
                                        //     every attribute), 2 multiline attribute, 4 multiline
                                        //     definition
    std::vector<std::string> secondary; // 72 (their number) and 340: secondary attributes — before
                                        //     2018 the ATTRIBs of the text's other lines
    bool has_alignment_point = true;    // 11 is there (not in every single line one)
    Point3 alignment_point;             // 11 (after the lock flag): the attribute's alignment point
    bool has_text = true;               // the embedded object (101) is there: types 2 and 4
    MTextData text;                     // the MTEXT
    // Up to 2013 only (the XRECORD ACAD_MLATT; 2018 has none of these)
    bool really_locked = false;         // its second 70
    double annotation_scale = 1.0;      // its 40: the current annotation scale
    std::string tag;                    // its 2: the attribute's tag (the ATTRIB's own 2 can differ)
};

// ---------------------------------------------------------------------------
// ATTDEF (attribute definition, in blocks)
// ---------------------------------------------------------------------------

struct AttdefData {
    Point3 start_point;
    double height = 2.5;
    std::string default_value;      // group 1
    std::string prompt;             // group 3
    std::string tag;                // group 2
    int flags = 0;                  // group 70: 1=invisible,2=constant,4=verify,8=preset
    int field_length = 0;           // group 73
    double rotation = 0.0;
    double width_factor = 1.0;
    double oblique_angle = 0.0;
    std::string style = "Standard";
    int generation_flags = 0;
    int h_justify = 0;              // group 72
    int v_justify = 0;              // group 74
    Point3 alignment_point;
    // Its position locked in the block: 280 in 2007; up to 2004 the XDATA AcDbAttr
    // 1070 0 1070 1, as AutoCAD writes it (in R12 too)
    bool lock_position = false;
    int version = 0;                // 280 (2010+), the first after the subclass: 0 = 2010
    OptionalBoxed<MultilineAttribute> multiline;    // a multiline attribute definition (2018, B75)
};

// ---------------------------------------------------------------------------
// ATTRIB (attribute value, follows INSERT)
// ---------------------------------------------------------------------------

struct AttribData {
    Point3 start_point;
    double height = 2.5;
    std::string value;              // group 1
    std::string tag;                // group 2
    int flags = 0;
    int field_length = 0;
    double rotation = 0.0;
    double width_factor = 1.0;
    double oblique_angle = 0.0;
    std::string style = "Standard";
    int generation_flags = 0;
    int h_justify = 0;
    int v_justify = 0;
    Point3 alignment_point;
    bool lock_position = false;     // as AttdefData's
    int version = 0;                // as AttdefData's
    OptionalBoxed<MultilineAttribute> multiline;    // a multiline attribute (2018, B75)
    // Before 2018, of the ATTRIB that is another line of a multiline attribute's text
    // (MultilineAttribute::secondary): the handle of the first. Not written; made by reading and saving.
    std::string secondary_of;
};

// ---------------------------------------------------------------------------
// InsertAttribute (ATTRIB instance stored in INSERT)
// ---------------------------------------------------------------------------

struct InsertAttribute {
    AttribData data;
    EntityProperties props;
    RawRecord raw;                      // the ATTRIB record (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// INSERT (block reference)
// ---------------------------------------------------------------------------

struct InsertData {
    std::string block_name;
    Point3 insertion_point;
    double x_scale = 1.0;
    double y_scale = 1.0;
    double z_scale = 1.0;
    double rotation = 0.0;
    int column_count = 1;
    int row_count = 1;
    double column_spacing = 0.0;
    double row_spacing = 0.0;
    std::vector<InsertAttribute> attributes;
    std::string seqend_handle;          // SEQEND after the attributes
    RawRecord seqend_raw;               // SEQEND after the attributes (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// DIMENSION
// ---------------------------------------------------------------------------

struct DimensionData {
    DimensionType dim_type = DimensionType::Rotated;
    int16_t attachment_point = 5;       // 71 (9.S, Codex 10): the text's attachment (5 middle center)
    int16_t value_73 = 0;               // 73 (2007+, not in the reference; 9.S)
    std::string block_name;             // group 2: pseudo-block name (*D1, *D2 ...)
    std::string dim_style = "Standard"; // group 3
    Point3 definition_point;            // groups 10, 20, 30
    Point3 text_midpoint;               // groups 11, 21, 31
    Point3 block_translation;           // groups 12, 22, 32
    int flags = 0;                      // group 70 (includes dim type bits 0-3)
    std::string text_override;          // group 1

    // Definition points (used by different subtypes)
    Point3 def_point2;                  // groups 13, 23, 33
    Point3 def_point3;                  // groups 14, 24, 34
    Point3 def_point4;                  // groups 15, 25, 35
    Point3 arc_point;                   // groups 16, 26, 36

    double leader_length = 0.0;         // group 40
    double rotation = 0.0;              // group 50
    double horizontal_dir = 0.0;        // group 51
    double oblique_angle = 0.0;         // group 52
    double text_rotation = 0.0;         // group 53
    int version = 0;                    // 280 (2010+): 0 = 2010
    // --- 9.S (Codex 10): AcDbDimension's text groups as read and written (2000+; R12, R13 and R14 have none). AutoCAD's files
    // have 71 5 in every dimension and 73 74 75 0 from 2007. In the padding of the fields above (sizeof(Entity) stays, B84); the
    // reference's line spacing (72, 41), which no AutoCAD file has, is the special dimensions' own (ArcDimensionData …).
    // attachment_point (71) and value_73 stand after dim_type
    int16_t value_74 = 0;               // 74
    int16_t value_75 = 0;               // 75
};

// ---------------------------------------------------------------------------
// LWPOLYLINE (2000+): a 2D polyline in its OCS (props.extrusion), at one elevation
// ---------------------------------------------------------------------------

struct LwVertex {
    Point2 position;                    // 10, 20
    double start_width = 0.0;           // 40
    double end_width = 0.0;             // 41
    double bulge = 0.0;                 // 42
    // 91 (2010+) after the vertex's point: its identifier, absent where AutoCAD writes none. AutoCAD
    // writes it on the vertices a constraint or an associative dimension refers to and leaves a
    // vertex it added (a chamfer's, a break's) without one, with the polyline's flags (70) as
    // they are: flag 1024 is no condition of it (AutoCAD's files, plan 9.2, F9.21)
    std::optional<int32_t> id;
};

// The file has one width for all vertices (43) when every vertex has the same start and
// end width, as AutoCAD writes it; otherwise each vertex's 40/41. Read either way, the
// widths are in the vertices.
struct LwPolylineData {
    int flags = 0;                      // 70: 1 = closed, 128 = linetype through vertices
    double elevation = 0.0;             // 38
    std::vector<LwVertex> vertices;
    // Up to 2013, of the polyline that is the frame of an MTEXT (MTextBackground::border_handles):
    // the handle of the MTEXT. Not written; made by reading and saving.
    std::string frame_of;

    bool is_closed() const { return (flags & 1) != 0; }
};

// ---------------------------------------------------------------------------
// ELLIPSE (2000+), in the WCS; props.extrusion is its normal
// ---------------------------------------------------------------------------

struct EllipseData {
    Point3 center;                      // 10, 20, 30
    Point3 major_axis = {1.0, 0.0, 0.0}; // 11, 21, 31: end of the major axis, from the center
    double ratio = 1.0;                 // 40: minor axis / major axis
    double start_param = 0.0;           // 41: 0 = the major axis end
    double end_param = 6.283185307179586; // 42: 2π = a full ellipse
};

// ---------------------------------------------------------------------------
// XLINE and RAY (2000+): unbounded lines in the WCS
// ---------------------------------------------------------------------------

struct XlineData {
    Point3 base_point;                  // 10, 20, 30
    Point3 direction = {1.0, 0.0, 0.0}; // 11, 21, 31: unit vector
};

struct RayData {
    Point3 base_point;                  // 10, 20, 30: where the ray starts
    Point3 direction = {1.0, 0.0, 0.0}; // 11, 21, 31: unit vector
};

// ---------------------------------------------------------------------------
// TOLERANCE (2000+): a feature control frame
// ---------------------------------------------------------------------------

struct ToleranceData {
    Point3 insertion_point;             // 10, 20, 30
    std::string content;                // 1: frames and symbols with formatting codes;
                                        //    lines separated by "\n" (^J in the file)
    std::string dim_style = "Standard"; // 3
    Point3 x_axis_direction = {1.0, 0.0, 0.0}; // 11, 21, 31 (WCS)
};

// ---------------------------------------------------------------------------
// LEADER (2000+); props.extrusion is its normal
// ---------------------------------------------------------------------------

struct LeaderData {
    std::string dim_style = "Standard"; // 3
    bool arrowhead = true;              // 71
    int path_type = 0;                  // 72: 0 straight segments, 1 spline
    int annotation_type = 3;            // 73: 0 MTEXT, 1 TOLERANCE, 2 INSERT, 3 none
    int hookline_direction = 0;         // 74: 0 against the horizontal direction, 1 with it
    bool hookline = false;              // 75
    double text_height = 0.0;           // 40: the annotation's height
    double text_width = 0.0;            // 41: the annotation's width
    std::vector<Point3> vertices;       // 76 (the count), 10, 20, 30
    int byblock_color = 0;              // 77: the colour when DIMCLRD is BYBLOCK; 0 = none
    std::string annotation_handle;      // 340: the MTEXT, TOLERANCE or INSERT; it has the
                                        //      leader as reactor
    Point3 horizontal_direction = {1.0, 0.0, 0.0}; // 211
    Point3 block_offset;                // 212: of the INSERT annotation's last vertex
    Point3 annotation_offset;           // 213: of the annotation from the last vertex
};

// ---------------------------------------------------------------------------
// SPLINE (2000+), in the WCS; props.extrusion is its normal when it is planar
// ---------------------------------------------------------------------------

struct SplineData {
    int flags = 8;                      // 70: 1 closed, 2 periodic, 4 rational, 8 planar,
                                        //     16 linear
    int degree = 3;                     // 71
    std::vector<double> knots;          // 72 (the count), 40
    std::vector<double> weights;        // 41: one per control point when rational; empty = 1
    std::vector<Point3> control_points; // 73 (the count), 10, 20, 30
    std::vector<Point3> fit_points;     // 74 (the count), 11, 21, 31
    Point3 start_tangent;               // 12, 22, 32: zero = none
    Point3 end_tangent;                 // 13, 23, 33: zero = none
    double knot_tolerance = 0.000000001;     // 42 (as AutoCAD writes a new spline)
    double control_tolerance = 0.0000000001; // 43
    double fit_tolerance = 0.0000000001;     // 44: written with fit points

    bool is_closed() const   { return (flags & 1) != 0; }
    bool is_rational() const { return (flags & 4) != 0; }
};

// ---------------------------------------------------------------------------
// HATCH (2000+): boundary paths in the OCS (props.extrusion) at an elevation
// ---------------------------------------------------------------------------

enum class HatchEdgeType {
    Line    = 1,
    Arc     = 2,
    Ellipse = 3,
    Spline  = 4
};

// An edge of a boundary path (72). Which fields count depends on the type. Angles in
// degrees; a clockwise arc or ellipse (73 = 0) has them as AutoCAD stores them.
struct HatchEdge {
    HatchEdgeType type = HatchEdgeType::Line;
    Point2 start;                       // line: 10, 20
    Point2 end;                         // line: 11, 21
    Point2 center;                      // arc, ellipse: 10, 20
    double radius = 0.0;                // arc: 40
    Point2 major_axis;                  // ellipse: 11, 21, from the center
    double ratio = 1.0;                 // ellipse: 40, minor / major
    double start_angle = 0.0;           // arc, ellipse: 50
    double end_angle = 360.0;           // arc, ellipse: 51
    bool counterclockwise = true;       // arc, ellipse: 73
    int degree = 3;                     // spline: 94
    bool rational = false;              // spline: 73
    bool periodic = false;              // spline: 74
    std::vector<double> knots;          // spline: 95 (the count), 40
    std::vector<Point2> control_points; // spline: 96 (the count), 10, 20
    std::vector<double> weights;        // spline: 42, one per control point when rational
    std::vector<Point2> fit_points;     // spline, 2010+: 97, 11, 21 (not in DXF 2000)
    Point2 start_tangent;               // spline, 2010+: 12, 22
    Point2 end_tangent;                 // spline, 2010+: 13, 23
};

struct HatchVertex {
    Point2 position;                    // 10, 20
    double bulge = 0.0;                 // 42
};

struct HatchBoundaryPath {
    int flags = 1;                      // 92: 1 external, 2 polyline, 4 derived, 8 text box,
                                        //     16 outermost
    bool closed = true;                 // polyline: 73
    std::vector<HatchVertex> vertices;  // polyline: 93 (the count), 10, 20, 42
    std::vector<HatchEdge> edges;       // otherwise: 93 (the count), 72 …
    std::vector<std::string> source_handles; // 97 (the count), 330: the boundary objects of
                                             // an associative hatch; they have it as reactor

    bool is_polyline() const { return (flags & 2) != 0; }
};

struct HatchPatternLine {
    double angle = 0.0;                 // 53 (degrees)
    Point2 base_point;                  // 43, 44
    Point2 offset;                      // 45, 46
    std::vector<double> dashes;         // 79 (the count), 49
};

// A gradient fill (2004+). A gradient has two colors, also when it is a single-color one
// (452 1: the second is the tinted first), as the reference says.
struct HatchGradientColor {
    double value = 0.0;                 // 463: where the color is, 0 or 1
    Color color;                        // 63 (ACI) and 421 (RGB), both written as AutoCAD
};

struct HatchGradient {
    int type = 1;                       // 450: 0 solid, 1 gradient
    int reserved = 0;                   // 451
    double angle = 0.0;                 // 460: radians
    double shift = 0.0;                 // 461: 0 centered, 1 shifted
    bool single_color = false;          // 452
    double tint = 0.0;                  // 462: of a single-color gradient, 0-1
    std::vector<HatchGradientColor> colors;     // 453 (the count)
    std::string name = "LINEAR";        // 470: LINEAR, CYLINDER, INVCYLINDER, SPHERICAL,
                                        //      INVSPHERICAL, HEMISPHERICAL, INVHEMISPHERICAL,
                                        //      CURVED, INVCURVED
};

struct HatchData {
    double elevation = 0.0;             // 30 (10 and 20 are always 0)
    std::string pattern_name = "SOLID"; // 2
    bool solid = true;                  // 70
    bool associative = false;           // 71
    std::vector<HatchBoundaryPath> paths;        // 91 (the count)
    int style = 0;                      // 75: 0 odd parity, 1 outermost, 2 entire area
    int pattern_type = 1;               // 76: 0 user-defined, 1 predefined, 2 custom
    double pattern_angle = 0.0;         // 52 (not for solid fill)
    double pattern_scale = 1.0;         // 41
    bool pattern_double = false;        // 77
    std::vector<HatchPatternLine> pattern_lines; // 78 (the count)
    double pixel_size = 0.0;            // 47: 0 = not written
    std::vector<Point2> seed_points;    // 98 (the count), 10, 20
    std::optional<HatchGradient> gradient;   // 450 … 470 (2004+), after the seed points
};

// ---------------------------------------------------------------------------
// MLINE (2000+), in the WCS; props.extrusion is its normal
// ---------------------------------------------------------------------------

// One element of the style at one vertex, as AutoCAD stores it
struct MlineElementParams {
    std::vector<double> params;         // 74 (the count), 41: along the miter, then breaks
    std::vector<double> fill_params;    // 75 (the count), 42
};

struct MlineVertex {
    Point3 position;                    // 11, 21, 31
    Point3 direction;                   // 12, 22, 32: of the segment from here
    Point3 miter;                       // 13, 23, 33
    std::vector<MlineElementParams> elements;   // one per element of the style
};

// The style is the MLINESTYLE of that name in ACAD_MLINESTYLE (2); its handle (340) is
// found from the name when written, unless style_handle is set. add_mline() computes the
// vertices' geometry from the style as AutoCAD does.
struct MlineData {
    std::string style = "STANDARD";     // 2
    std::string style_handle;           // 340
    double scale = 1.0;                 // 40
    int justification = 0;              // 70: 0 top, 1 zero, 2 bottom
    int flags = 1;                      // 71: 1 has vertices, 2 closed, 4 no start caps,
                                        //     8 no end caps
    Point3 start_point;                 // 10, 20, 30
    std::vector<MlineVertex> vertices;  // 72 (the count)
    int style_elements = 0;             // 73: the number of elements in the style

    bool is_closed() const { return (flags & 2) != 0; }
};

// ---------------------------------------------------------------------------
// 3DSOLID, REGION, BODY (2000+): ACIS data
// ---------------------------------------------------------------------------

// The lines as in the file (the ACIS data, encoded as AutoCAD writes it); a line longer
// than 255 characters is written in 1 and continued in 3. Kept as read and written back
// unchanged; HDxf reads the B-rep in it (the SAT lines, or the SAB from 2013) only for the
// extents, where the body counts exactly (rättningsfas R.9). The body's data is the SAT lines
// when there are any, else the SAB.
struct AcisData {
    int version = 1;                    // 70 (2000 … 2010; 2013 has 290 and 2 instead)
    std::vector<std::string> lines;     // 1, 3 (2000 … 2010): the SAT lines, enciphered as in the
                                        //     file — a line is its 3s and the 1 that ends it, as
                                        //     AutoCAD writes a line of 255 characters or more
                                        //     (3s of 255, the rest in 1). In 2013 a body with SAT lines is
                                        //     written as the SAB made from them (plan phase 5,
                                        //     B62); SAT 21200/21500/21800 of AutoCAD's record
                                        //     types, other versions and types an error
    // 3DSOLID 2007+ (AcDb3dSolid, 350): its history (ACSH_HISTORY_CLASS, owned by the
    // solid); empty = none, written as 350 0
    std::string history_handle;
    // 2013+ (AcDbModelerGeometry 290 and 2, in no reference): the body's change GUID as read,
    // "{…}"; empty = none (290 0 and the GUID of zeros, as AutoCAD writes a body without one).
    // In 2013 the ACIS data itself is in the ACDSDATA section, not in 1/3 (plan phase 5, B58).
    // Before 2013 XDATA ACAD_OBJECT_CHANGE_GUID (1070 1, 1071 Data1, 1070 Data2, Data3 and
    // Data4's 8 bytes), not in props.xdata — read as "{…}" in lowercase (B60)
    std::string guid;
    // 2013+: the body's SAB, the bytes of its ASM_Data record in the ACDSDATA section (in no
    // reference; plan phase 5, B61), found by the body's handle when the file is read. Written
    // in 2013 as it is; a body with only SAB data cannot be written before 2013 (SAB → SAT is
    // a conversion, phase 8). Empty = none (a body without data has no record).
    std::vector<uint8_t> sab;
};

// ---------------------------------------------------------------------------
// OLE frames and proxy classes (phase 9, step 9.3)
// ---------------------------------------------------------------------------
//
// OLEFRAME and OLE2FRAME hold an OLE object as the bytes of its compound file (310, the byte count in 90) and
// the string "OLE" (1) that ends it; HDxf keeps the bytes as they are and starts no OLE server. What
// OLE2FRAME's other groups say (the application's name, the frame's corners, the kind of object) AutoCAD
// derives from the OLE content and ignores when it opens a file (DXF reference, OLE2FRAME): they are kept as
// they are written and written back as given.
//
// ACAD_PROXY_ENTITY and ACAD_PROXY_OBJECT stand for a custom entity or object whose application is not loaded:
// the class data of the application as an opaque stream (a bit length in 93/161 and its bytes in 310), the
// graphics the application drew (96/162 with 311), the ids of the objects the data refers to (330/340/350/360,
// ended by 94) and the class's place in CLASSES (91 = 500 + the position). The bytes belong to the
// application: HDxf never changes or invents them.

// OLE2FRAME's 71: the kind of OLE object (any other value as read is kept)
enum class OleObjectType : int32_t { Link = 1, Embedded = 2, Static = 3 };

// OLEFRAME (AcDbOleFrame, R13): the version (70), the OLE bytes (90, 310) and, when the file has them, the
// entity's proxy graphics (92/160 and 310 of AcDbEntity: kept as bytes)
struct OleFrameData {
    int16_t version = 2;                        // 70
    std::vector<uint8_t> data;                  // 310 (90 is its length)
    std::vector<uint8_t> proxy_graphics;        // the entity's own proxy graphics (AcDbEntity 92/160 + 310)
};

// OLE2FRAME (AcDbOle2Frame, R14 and later): OLEFRAME's data and what AutoCAD derives from the object
struct Ole2FrameData {
    int16_t version = 2;                        // 70
    std::string application_name;               // 3: the application of the object ("Paintbrush Picture")
    Point3 upper_left;                          // 10: the frame's upper left corner (WCS)
    Point3 lower_right;                         // 11: the lower right corner (WCS)
    OleObjectType object_type = OleObjectType::Embedded;    // 71
    int16_t tile_mode = 0;                      // 72: 0 the object is in model space, 1 in paper space
    std::vector<uint8_t> data;                  // 310 (90 is its length)
    std::vector<uint8_t> proxy_graphics;        // the entity's own proxy graphics (AcDbEntity 92/160 + 310)
};

// ACAD_PROXY_ENTITY (R14 and later)
struct ProxyEntityData {
    ProxyEntityData() { proxy.class_id = 498; }
    ProxyClassData proxy;
    std::vector<uint8_t> entity_graphics;       // the entity's own proxy graphics (AcDbEntity 92/160 + 310), apart
                                                //     from the class's `proxy.graphics`
    OptionalBoxed<ProxyEntityClass> class_data; // 70 = 1: the class's subclasses as its type (TCPOINTENTITY: TcPointData)
};

// ---------------------------------------------------------------------------
// Unknown entity (preserved for round-trip)
// ---------------------------------------------------------------------------

struct UnknownEntityData {
    std::string entity_type;
    std::vector<std::pair<int, std::string>> group_codes;
};

// ---------------------------------------------------------------------------
// Entity variant and Entity struct
// ---------------------------------------------------------------------------

using EntityDataVariant = std::variant<
    LineData,
    CircleData,
    ArcData,
    PointData,
    TextData,
    PolylineData,
    TraceData,
    SolidData,
    Face3DData,
    ShapeData,
    InsertData,
    AttdefData,
    AttribData,
    DimensionData,
    Boxed<ViewportData>,
    LwPolylineData,
    EllipseData,
    XlineData,
    RayData,
    MTextData,
    ToleranceData,
    LeaderData,
    SplineData,
    HatchData,
    MlineData,
    Boxed<ImageData>,
    AcisData,
    Boxed<MLeaderData>,
    Boxed<SurfaceData>,
    Boxed<HelixData>,
    Boxed<MeshData>,
    Boxed<LightData>,
    Boxed<CoordinationModelData>,
    Boxed<UnderlayData>,            // 9.6: PDFUNDERLAY, DWFUNDERLAY, DGNUNDERLAY
    // --- 9.3 OLE and proxy classes ---
    OleFrameData,
    Ole2FrameData,
    ProxyEntityData,
    Boxed<TableData>,
    // --- 9.12 sections ---
    Boxed<SectionObjectData>,
    // --- 9.17 special dimensions ---
    Boxed<ArcDimensionData>,
    Boxed<LargeRadialDimensionData>,
    // --- 9.18 Map, TC and ZWM ---
    Boxed<TcPointData>,
    UnknownEntityData
>;

// The index of the alternative T in EntityData
template <typename T>
inline constexpr std::size_t entity_data_index = data_index_in<T>(static_cast<const EntityDataVariant*>(nullptr));

// The data of an entity: a std::variant whose copying, moving, destruction, construction and access go through
// entity_data.cpp, so that libstdc++'s nested helpers for its alternatives are written out once and not in every file.
// std::get, std::get_if, std::holds_alternative and std::visit take it as the std::variant it is; get<T>() and
// get_if<T>() are the same without those helpers. It is assigned as a whole (`data = SomeData{...}` converts first).
struct EntityData : EntityDataVariant {
    EntityData();

    // From an alternative or a value that converts to one, as std::variant converts it. The alternative is made here
    // and moved in by entity_data.cpp, so that nothing of the variant itself is made in the caller's file
    template <typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, EntityData>) && (!DataInPlace<std::remove_cvref_t<T>>::value) &&
                 std::is_constructible_v<EntityDataVariant, T>
    EntityData(T&& value)
        : EntityData(entity_data_index<DataPicked<EntityDataVariant, T>>,
                     address_of(DataPicked<EntityDataVariant, T>(std::forward<T>(value)))) {}

    // In place, as std::variant constructs (these are made in the caller's file)
    template <typename T, typename... Args>
    explicit EntityData(std::in_place_type_t<T> tag, Args&&... args) : EntityDataVariant(tag, std::forward<Args>(args)...) {}
    template <std::size_t I, typename... Args>
    explicit EntityData(std::in_place_index_t<I> tag, Args&&... args) : EntityDataVariant(tag, std::forward<Args>(args)...) {}

    EntityData(const EntityData& other);
    EntityData(EntityData&& other) noexcept;
    EntityData& operator=(const EntityData& other);
    EntityData& operator=(EntityData&& other) noexcept;
    ~EntityData();

    std::size_t index() const noexcept;
    bool valueless_by_exception() const noexcept;

    template <typename T>
    T& get() {
        if (T* value = get_if<T>()) return *value;
        throw std::bad_variant_access();
    }
    template <typename T>
    const T& get() const {
        if (const T* value = get_if<T>()) return *value;
        throw std::bad_variant_access();
    }
    template <typename T>
    T* get_if() noexcept {
        return const_cast<T*>(static_cast<const EntityData*>(this)->get_if<T>());
    }
    template <typename T>
    const T* get_if() const noexcept {
        static_assert(entity_data_index<T> < std::variant_size_v<EntityDataVariant>, "not an alternative of EntityData");
        return index() == entity_data_index<T> ? static_cast<const T*>(held()) : nullptr;
    }

    // The address of the alternative held (the Boxed<T> itself for a boxed one); null when an exception left it valueless
    const void* held() const noexcept;

private:
    // Holds the alternative at index, moved from *value
    EntityData(std::size_t index, void* value);

    template <typename A>
    static void* address_of(A&& made) { return &made; }
};


// A proxy entity's class data (ProxyEntityData::class_data): the class's type and its data as that type has it — the
// subclasses after AcDbEntity of the class's own record (TCPOINTENTITY: `type` EntityType::TcPoint, `data` Boxed<TcPointData>)
struct ProxyEntityClass {
    EntityType type = EntityType::Unknown;
    EntityData data;
};

struct Entity {
    EntityType type;
    EntityProperties props;
    EntityData data;
    RawRecord raw;                      // the record as read (DXF 2000+ files)

    LineData& as_line()                          { return data.get<LineData>(); }
    const LineData& as_line() const              { return data.get<LineData>(); }

    CircleData& as_circle()                      { return data.get<CircleData>(); }
    const CircleData& as_circle() const          { return data.get<CircleData>(); }

    ArcData& as_arc()                            { return data.get<ArcData>(); }
    const ArcData& as_arc() const                { return data.get<ArcData>(); }

    PointData& as_point()                        { return data.get<PointData>(); }
    const PointData& as_point() const            { return data.get<PointData>(); }

    TextData& as_text()                          { return data.get<TextData>(); }
    const TextData& as_text() const              { return data.get<TextData>(); }

    PolylineData& as_polyline()                  { return data.get<PolylineData>(); }
    const PolylineData& as_polyline() const      { return data.get<PolylineData>(); }

    TraceData& as_trace()                        { return data.get<TraceData>(); }
    const TraceData& as_trace() const            { return data.get<TraceData>(); }

    SolidData& as_solid()                        { return data.get<SolidData>(); }
    const SolidData& as_solid() const            { return data.get<SolidData>(); }

    Face3DData& as_face3d()                      { return data.get<Face3DData>(); }
    const Face3DData& as_face3d() const          { return data.get<Face3DData>(); }

    ShapeData& as_shape()                        { return data.get<ShapeData>(); }
    const ShapeData& as_shape() const            { return data.get<ShapeData>(); }

    InsertData& as_insert()                      { return data.get<InsertData>(); }
    const InsertData& as_insert() const          { return data.get<InsertData>(); }

    AttdefData& as_attdef()                      { return data.get<AttdefData>(); }
    const AttdefData& as_attdef() const          { return data.get<AttdefData>(); }

    AttribData& as_attrib()                      { return data.get<AttribData>(); }
    const AttribData& as_attrib() const          { return data.get<AttribData>(); }

    DimensionData& as_dimension()                { return data.get<DimensionData>(); }
    const DimensionData& as_dimension() const    { return data.get<DimensionData>(); }

    ViewportData& as_viewport()                  { return *data.get<Boxed<ViewportData>>(); }
    const ViewportData& as_viewport() const      { return *data.get<Boxed<ViewportData>>(); }

    LwPolylineData& as_lwpolyline()              { return data.get<LwPolylineData>(); }
    const LwPolylineData& as_lwpolyline() const  { return data.get<LwPolylineData>(); }

    EllipseData& as_ellipse()                    { return data.get<EllipseData>(); }
    const EllipseData& as_ellipse() const        { return data.get<EllipseData>(); }

    XlineData& as_xline()                        { return data.get<XlineData>(); }
    const XlineData& as_xline() const            { return data.get<XlineData>(); }

    RayData& as_ray()                            { return data.get<RayData>(); }
    const RayData& as_ray() const                { return data.get<RayData>(); }

    MTextData& as_mtext()                        { return data.get<MTextData>(); }
    const MTextData& as_mtext() const            { return data.get<MTextData>(); }

    ToleranceData& as_tolerance()                { return data.get<ToleranceData>(); }
    const ToleranceData& as_tolerance() const    { return data.get<ToleranceData>(); }

    LeaderData& as_leader()                      { return data.get<LeaderData>(); }
    const LeaderData& as_leader() const          { return data.get<LeaderData>(); }

    SplineData& as_spline()                      { return data.get<SplineData>(); }
    const SplineData& as_spline() const          { return data.get<SplineData>(); }

    HatchData& as_hatch()                        { return data.get<HatchData>(); }
    const HatchData& as_hatch() const            { return data.get<HatchData>(); }

    MlineData& as_mline()                        { return data.get<MlineData>(); }
    const MlineData& as_mline() const            { return data.get<MlineData>(); }

    ImageData& as_image()                        { return *data.get<Boxed<ImageData>>(); }
    const ImageData& as_image() const            { return *data.get<Boxed<ImageData>>(); }
    // WIPEOUT: an image without a picture (no IMAGEDEF), its boundary in -0.5 … 0.5
    ImageData& as_wipeout()                      { return *data.get<Boxed<ImageData>>(); }
    const ImageData& as_wipeout() const          { return *data.get<Boxed<ImageData>>(); }

    // 3DSOLID, REGION and BODY
    AcisData& as_acis()                          { return data.get<AcisData>(); }
    const AcisData& as_acis() const              { return data.get<AcisData>(); }

    MLeaderData& as_mleader()                    { return *data.get<Boxed<MLeaderData>>(); }
    const MLeaderData& as_mleader() const        { return *data.get<Boxed<MLeaderData>>(); }

    // SURFACE, EXTRUDEDSURFACE, LOFTEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE, PLANESURFACE
    SurfaceData& as_surface()                    { return *data.get<Boxed<SurfaceData>>(); }
    const SurfaceData& as_surface() const        { return *data.get<Boxed<SurfaceData>>(); }
    HelixData& as_helix()                        { return *data.get<Boxed<HelixData>>(); }
    const HelixData& as_helix() const            { return *data.get<Boxed<HelixData>>(); }
    MeshData& as_mesh()                          { return *data.get<Boxed<MeshData>>(); }
    const MeshData& as_mesh() const              { return *data.get<Boxed<MeshData>>(); }
    LightData& as_light()                        { return *data.get<Boxed<LightData>>(); }
    const LightData& as_light() const            { return *data.get<Boxed<LightData>>(); }

    CoordinationModelData& as_coordination_model()
        { return *data.get<Boxed<CoordinationModelData>>(); }
    const CoordinationModelData& as_coordination_model() const
        { return *data.get<Boxed<CoordinationModelData>>(); }
    // PDFUNDERLAY, DWFUNDERLAY, DGNUNDERLAY (9.6)
    // --- 9.17 ---
    ArcDimensionData& as_arc_dimension()         { return *data.get<Boxed<ArcDimensionData>>(); }
    const ArcDimensionData& as_arc_dimension() const { return *data.get<Boxed<ArcDimensionData>>(); }
    LargeRadialDimensionData& as_large_radial_dimension() { return *data.get<Boxed<LargeRadialDimensionData>>(); }
    const LargeRadialDimensionData& as_large_radial_dimension() const { return *data.get<Boxed<LargeRadialDimensionData>>(); }
    // The AcDbDimension part of a DIMENSION, an ARC_DIMENSION or a LARGE_RADIAL_DIMENSION; null for any other entity
    const DimensionData* dimension_part() const;
    DimensionData* dimension_part() { return const_cast<DimensionData*>(static_cast<const Entity*>(this)->dimension_part()); }
    TcPointData& as_tc_point()                   { return *data.get<Boxed<TcPointData>>(); }
    const TcPointData& as_tc_point() const       { return *data.get<Boxed<TcPointData>>(); }
    SectionObjectData& as_section_object()       { return *data.get<Boxed<SectionObjectData>>(); }
    const SectionObjectData& as_section_object() const { return *data.get<Boxed<SectionObjectData>>(); }

    UnderlayData& as_underlay()                  { return *data.get<Boxed<UnderlayData>>(); }
    const UnderlayData& as_underlay() const      { return *data.get<Boxed<UnderlayData>>(); }

    // OLEFRAME, OLE2FRAME and ACAD_PROXY_ENTITY (9.3)
    OleFrameData& as_ole_frame()                 { return data.get<OleFrameData>(); }
    const OleFrameData& as_ole_frame() const     { return data.get<OleFrameData>(); }
    Ole2FrameData& as_ole2_frame()               { return data.get<Ole2FrameData>(); }
    const Ole2FrameData& as_ole2_frame() const   { return data.get<Ole2FrameData>(); }
    ProxyEntityData& as_proxy_entity()           { return data.get<ProxyEntityData>(); }
    const ProxyEntityData& as_proxy_entity() const { return data.get<ProxyEntityData>(); }
    // ACAD_TABLE
    TableData& as_table()                        { return *data.get<Boxed<TableData>>(); }
    const TableData& as_table() const            { return *data.get<Boxed<TableData>>(); }

    UnknownEntityData& as_unknown()              { return data.get<UnknownEntityData>(); }
    const UnknownEntityData& as_unknown() const  { return data.get<UnknownEntityData>(); }
};

} // namespace hdxf

// EntityData is the std::variant it derives from for std::variant_size and std::variant_alternative
template <>
struct std::variant_size<hdxf::EntityData> : std::variant_size<hdxf::EntityDataVariant> {};
template <std::size_t I>
struct std::variant_alternative<I, hdxf::EntityData> : std::variant_alternative<I, hdxf::EntityDataVariant> {};
