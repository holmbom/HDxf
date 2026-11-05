#pragma once

// MULTILEADER.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// MULTILEADER (plan phase 3, B36): AutoCAD 2008's multileader. AutoCAD writes it in 2000, 2004
// and 2007 files with the same groups (and XDATA ACAD_MLEADERVER for the version), and its
// proxy graphics, which HDxf makes when it writes one. The content — MTEXT or a block — and
// the leaders are in its context data (300 CONTEXT_DATA{ … 301 }); the properties after it
// say which come from the style and which are the entity's own (90, override flags).
// Table records and blocks are named here as in DIMSTYLE (handles in the file); the style
// by its name in ACAD_MLEADERSTYLE, or its handle.
// ---------------------------------------------------------------------------

// A gap in a leader line or a leader (from start to end)
struct MLeaderGap {
    Point3 start;                       // line: 11; leader: 12
    Point3 end;                         // line: 12; leader: 13
};

// The gaps in one segment of a leader line
struct MLeaderSegmentGaps {
    int segment = 0;                    // 90: the segment, from the arrowhead
    std::vector<MLeaderGap> gaps;       // 11, 12
};

// A leader line (304 LEADER_LINE{ … 305 }): from the arrowhead to the leader's last point
struct MLeaderLine {
    std::vector<Point3> vertices;       // 10: the arrowhead first; the last point is the leader's
    std::vector<MLeaderSegmentGaps> breaks;     // 90, 11, 12
    int index = 0;                      // 91
    // 2010+: the line's own properties, after 91 (ODA's specification; not in Autodesk's
    // reference), each written when it has one — AutoCAD's files have all seven, only the
    // color, or none. They count where override_flags says so (plan phase 4, B47).
    std::optional<int> leader_type;     // 170: 0 invisible, 1 straight, 2 spline
    std::optional<CmColor> color;       // 92
    std::optional<std::string> linetype;        // 340: LTYPE name; "" = none (0)
    std::optional<int> lineweight;      // 171
    std::optional<double> arrow_size;   // 40
    std::optional<std::string> arrow_block;     // 341: block name; "" = none (0)
    std::optional<int> override_flags;  // 93: 1 type, 2 color, 4 linetype, 8 lineweight,
                                        //     16 arrow size, 32 arrowhead
};

// A leader (302 LEADER{ … 303 }): its lines meet at its last point, which the landing
// (dogleg) joins to the content
struct MLeaderLeader {
    bool has_last_point = true;         // 290
    bool has_dogleg = true;             // 291
    Point3 last_point;                  // 10
    Point3 dogleg_direction = {1.0, 0.0, 0.0};  // 11: from the last point toward the content
    std::vector<MLeaderGap> breaks;     // 12, 13
    int branch_index = 0;               // 90
    double dogleg_length = 0.0;         // 40
    std::vector<MLeaderLine> lines;     // 304 …
    int attachment_direction = 0;       // 271 (2010+): 0 horizontal, 1 vertical
};

// The context data: the content as it is placed, and the leaders
struct MLeaderContext {
    double scale = 1.0;                 // 40: content scale
    Point3 base_point;                  // 10: the content's base position
    double text_height = 0.18;          // 41
    double arrow_size = 0.18;           // 140
    double landing_gap = 0.09;          // 145
    int left_attachment = 1;            // 174
    int right_attachment = 1;           // 175
    int text_align = 0;                 // 176: 0 left, 1 center, 2 right
    int block_connection = 0;           // 177: 0 extents, 1 base point
    bool has_text = true;               // 290: MTEXT content
    // The MTEXT content (has_text)
    std::string text;                   // 304: with MTEXT's formatting codes
    Point3 text_normal = {0.0, 0.0, 1.0};       // 11
    std::string text_style;             // 340: STYLE name, empty = Standard
    Point3 text_location;               // 12: the MTEXT's insertion point
    Point3 text_direction = {1.0, 0.0, 0.0};    // 13
    double text_rotation = 0.0;         // 42
    double text_width = 0.0;            // 43: 0 = no wrapping
    double text_defined_height = 0.0;   // 44
    double line_spacing_factor = 1.0;   // 45
    int line_spacing_style = 1;         // 170
    CmColor text_color;                 // 90
    int text_attachment = 1;            // 171: MTEXT's 71
    int text_flow = 1;                  // 172: MTEXT's 72
    CmColor background_color;           // 91
    double background_scale = 1.5;      // 141
    int background_transparency = 0;    // 92
    bool background_color_on = false;   // 291
    bool background_fill_on = false;    // 292
    int column_type = 0;                // 173
    bool auto_height = false;           // 293
    double column_width = 0.0;          // 142
    double column_gutter = 0.0;         // 143
    bool column_flow_reversed = false;  // 294
    std::vector<double> column_heights; // 144
    bool word_break = false;            // 295
    // The block content (has_block)
    bool has_block = false;             // 296
    std::string block;                  // 341: block name
    Point3 block_normal = {0.0, 0.0, 1.0};      // 14
    Point3 block_position;              // 15
    Point3 block_scale = {1.0, 1.0, 1.0};       // 16
    double block_rotation = 0.0;        // 46
    CmColor block_color;                // 93
    std::vector<double> block_matrix;   // 47 (16, row by row): the block's transform
    Point3 plane_origin;                // 110: the MLEADER's plane
    Point3 plane_x_axis = {1.0, 0.0, 0.0};      // 111
    Point3 plane_y_axis = {0.0, 1.0, 0.0};      // 112
    bool plane_normal_reversed = false; // 297
    std::vector<MLeaderLeader> leaders; // 302 …
    int bottom_attachment_direction = 9;    // 272 (2010+)
    int top_attachment_direction = 9;       // 273 (2010+)
};

// An arrowhead of its own on one leader
struct MLeaderArrowhead {
    int index = 0;                      // 94: the leader
    std::string block;                  // 345: block name
};

// A value for an attribute of the block content
struct MLeaderAttribute {
    std::string attdef_handle;          // 330: the ATTDEF in the block
    int index = 0;                      // 177
    double width = 0.0;                 // 44
    std::string text;                   // 302
};

struct MLeaderData {
    MLeaderContext context;
    std::string style = "Standard";     // its name in ACAD_MLEADERSTYLE
    std::string style_handle;           // 340: found from the name when written, unless set
    int override_flags = 0;             // 90: the properties that are the entity's own
    int leader_type = 1;                // 170: 0 invisible, 1 straight, 2 spline
    CmColor leader_color;               // 91
    std::string leader_linetype = "ByBlock";    // 341: LTYPE name
    int leader_lineweight = -2;         // 171
    bool landing = true;                // 290
    bool dogleg = true;                 // 291
    double dogleg_length = 0.36;        // 41
    std::string arrow_block;            // 342: empty = closed filled
    double arrow_size = 0.18;           // 42
    int content_type = 2;               // 172: 0 none, 1 block, 2 MTEXT, 3 tolerance
    std::string text_style;             // 343: STYLE name, empty = Standard
    int text_left_attachment = 1;       // 173
    int text_right_attachment = 1;      // 95
    int text_angle_type = 1;            // 174
    int text_alignment = 0;             // 175
    CmColor text_color;                 // 92
    bool text_frame = false;            // 292
    std::string block;                  // 344: the block content's name
    CmColor block_color;                // 93
    Point3 block_scale = {1.0, 1.0, 1.0};       // 10
    double block_rotation = 0.0;        // 43
    int block_connection = 0;           // 176
    bool annotation_scale = false;      // 293
    std::vector<MLeaderArrowhead> arrowheads;   // 94, 345
    std::vector<MLeaderAttribute> attributes;   // 330, 177, 44, 302
    bool text_direction_negative = false;       // 294
    int text_align_in_ipe = 0;          // 178
    int text_attachment_point = 1;      // 179
    double scale = 1.0;                 // 45: the overall scale (not in the reference; with
                                        //     the context's 40 in AutoCAD's files)
    int version = 2;                    // 2000-2007 XDATA ACAD_MLEADERVER, 2010+ 270
    // 2010+
    int text_attachment_direction = 0;  // 271
    int bottom_attachment_direction = 9;    // 272
    int top_attachment_direction = 9;       // 273
    // 295 (2013+; not in the reference): add_mleader() gives it the style's text_extended (298).
    // Before 2013 an XRECORD ACAD_ROUNDTRIP_2012_MLEADER (295 1) in the entity's extension
    // dictionary, as AutoCAD writes it (B60)
    bool text_extended = false;
};

} // namespace hdxf
