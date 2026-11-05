#pragma once

// The detail and section view styles, and the section settings.

#include "core.hpp"

namespace hdxf {

// The base of ACDBDETAILVIEWSTYLE and ACDBSECTIONVIEWSTYLE (subclass AcDbModelDocViewStyle, plan phase 6,
// B77): DetailViewStyleData::base and SectionViewStyleData::base (Object::as_view_style() gives it for either).
// Up to 2013 the display name and its flags are not in the record but in the XRECORD of its extension dictionary
// (ACAD_XREC_ROUNDTRIP: 102 DISPLAYNAME, 1, 102 FLAGS, 90 — ACadSharp's sample_AC1027); 2018 has them as 300 and
// 90 after 290.
struct ModelDocViewStyleData {
    int version = 0;                    // 70
    std::string name;                   // 3
    bool flag = false;                  // 290
    std::string display_name;           // 300 (2018); the XRECORD's DISPLAYNAME before
    int display_flags = 0;              // 90 (2018); the XRECORD's FLAGS before
};

// --- 9.12 the ModelDoc view styles (step 9.12, modeldoc_style.cpp): AutoCAD 2025's own records (every drawing has the
// template's Imperial24 and Metric50 of both: doc/facit/acad_<ver>_dxf_basic.dxf) in the order of their groups. After
// AcDbDetailViewStyle's / AcDbSectionViewStyle's 70 the record is five parts, each begun by a 71 of its own number
// (0 ... 4). A color is group 62 as AutoCAD writes it (256 ByLayer, 257 ByBlock, 1 ... 255); DXF 2000's records have none
// (the written form for 2000 leaves them out), a record read from 2000 has them empty. A handle (340) is a hard pointer
// to a table record, 0 for none: a text style (STYLE) for the identifier and the label, a linetype (LTYPE) for the
// lines, and for an arrow a block (AutoCAD's template has 0 for the built-in arrow). What AutoCAD's files do not explain
// keeps its group's place as its name (flags, flags_a, the numbered measures).
using ViewStyleColor = std::optional<int16_t>;

struct ViewStyleLineFormat {
    std::string linetype_handle;        // 340 (LTYPE; Continuous in the template)
    int lineweight = 25;                // 90 (1/100 mm; 25 in the template)
    ViewStyleColor color;               // 62
};

struct ViewLabelFormat {
    std::string style_handle;           // 340 (STYLE)
    ViewStyleColor color;               // 62
    double height = 0.0;                // 40
    int position_flag = 0;              // 90
    double offset = 0.0;                // 40
    int alignment_flag = 0;             // 90
    std::string pattern;                // 300: the label's text with its AcVar fields
};

struct DetailIdentifierFormat {
    std::string style_handle;           // 340 (STYLE)
    ViewStyleColor color;               // 62
    double height = 0.0;                // 40
    std::string arrow_handle;           // 340 (BLOCK_RECORD of the arrow symbol; 0 for the built-in arrow)
    ViewStyleColor arrow_color;         // 62
    double arrow_size = 0.0;            // 40
    std::string text;                   // 300
    double offset = 0.0;                // 40
    int flags = 0;                      // 280
};

struct SectionIdentifierFormat {
    std::string style_handle;           // 340 (STYLE)
    ViewStyleColor color;               // 62
    double height = 0.0;                // 40
    std::string start_arrow_handle;     // 340 (0 for the built-in arrow)
    std::string end_arrow_handle;       // 340
    ViewStyleColor arrow_color;         // 62
    double arrow_size = 0.0;            // 40
    std::string excluded_characters;    // 300 ("I, O, Q, S, X, Z" in the template)
    double offset = 0.0;                // 40
    int flags_a = 0;                    // 90
    double distance = 0.0;              // 40
    int flags_b = 0;                    // 90
};

// The hatch of the section's cut: its angles are a list of as many as 90 counts (AutoCAD's template has six)
struct SectionHatchFormat {
    ViewStyleColor background_color;    // 62 (256 in the template)
    ViewStyleColor foreground_color;    // 62 (257 in the template)
    std::string pattern;                // 300 ("ANSI31")
    double scale = 1.0;                 // 40
    int flags = 0;                      // 90
    bool flag_a = false;                // 290
    bool flag_b = false;                // 290
    std::vector<double> angles;         // 90 counts them, 40 each (radians)
};

struct DetailViewStyleData {
    ModelDocViewStyleData base;
    int version = 0;                    // 70 (AcDbDetailViewStyle)
    int flags = 0;                      // 90 (part 0)
    DetailIdentifierFormat identifier;  // part 1
    ViewStyleLineFormat boundary_line;  // part 2
    ViewLabelFormat label;              // part 3
    ViewStyleLineFormat lines[2];       // part 4, two lines then 280
    int end_flags = 0;                  // 280
};

struct SectionViewStyleData {
    ModelDocViewStyleData base;
    int version = 0;                    // 70 (AcDbSectionViewStyle)
    int flags = 0;                      // 90 (part 0)
    SectionIdentifierFormat identifier; // part 1
    ViewStyleLineFormat lines[2];       // part 2, two lines then three measures
    double measures[3] = {0.0, 0.0, 0.0};   // 40 40 40
    ViewLabelFormat label;              // part 3
    SectionHatchFormat hatch;           // part 4
};

// --- 9.12 sections (section.cpp): SECTION_SETTINGS' nested settings, from Autodesk's reference (Section Settings,
// Section Type Settings, Section Geometry Settings) in the order of the AutoCAD file the old plan's B.67 observed; AutoCAD
// 2025's own SECTION_SETTINGS (every section it makes) has only the first two groups: no settings of a type.
struct SectionGeometrySettings {
    int section_type = 0;               // 90
    int geometry_count = 0;             // 91 (the reference's "geometry count")
    int flags = 0;                      // 92 (bit flags)
    int16_t color = 256;                // 62 (the reference's tables say 63; AutoCAD writes 62)
    std::string layer;                  // 8
    std::string linetype;               // 6
    double linetype_scale = 1.0;        // 40
    std::string plot_style;             // 1
    int16_t lineweight = -3;            // 370
    int16_t face_transparency = 0;      // 70
    int16_t edge_transparency = 0;      // 71
    int16_t hatch_type = 0;             // 72
    std::string hatch_name;             // 2 (the second 2: the first is the marker "SectionGeometrySettings")
    double hatch_angle = 0.0;           // 41
    double hatch_scale = 1.0;           // 42
    double hatch_spacing = 1.0;         // 43
};

struct SectionTypeSettings {
    int section_type = 0;               // 90
    int generation_flags = 0;           // 91
    std::vector<std::string> sources;   // 92 counts them, 330 each: soft pointers to the source objects
    std::string destination_block;      // 331: soft pointer to the destination block (0 for none)
    std::string destination_file;       // 1 (the second 1: the first is the marker "SectionTypeSettings")
    std::vector<SectionGeometrySettings> geometries;    // 93 counts them
};

struct SectionSettingsData {
    int section_type = 2;               // 90 (2 in every section AutoCAD makes)
    std::vector<SectionTypeSettings> types;     // 91 counts them
};

} // namespace hdxf
