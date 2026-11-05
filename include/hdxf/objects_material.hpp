#pragma once

// MATERIAL and VISUALSTYLE.

#include "core.hpp"

namespace hdxf {

// --- 9.7 materials and visual styles ---

// MATERIAL (AcDbMaterial; in ACAD_MATERIAL, 2000+). AutoCAD writes a material as a run of groups in a fixed order
// in which every group but the name is there only when the material has it: a material ByBlock, ByLayer or Global
// is the name and the channel flags, one with maps has the maps' file names and mapper transforms. Every optional
// value here is therefore "present in the record or not" (the default AutoCAD gives the one that is not is in the
// comment); a value present is written back, one absent is not (plan 9.7, B100, B103).

// A color channel (ambient 70/40/90, diffuse 71/41/91, specular 76/45/92)
struct MaterialColor {
    std::optional<int16_t> method;      // 70, 71, 76: 0 use the current color (default), 1 override it
    std::optional<double> factor;       // 40, 41, 45: 0 … 1 (default 1.0)
    std::optional<int32_t> color;       // 90, 91, 92: an AcCmEntityColor, as the signed 32 bits AutoCAD writes it
};

// A map channel, in the order its groups are written: the blend factor, the source, the file name and the mapper
// (projection, tiling, auto transform and the 4 x 4 transform row by row). The six channels and the normal map use
// the same seven values with their own codes:
//   diffuse    42 72  3 73 74 75  43      specular 46 77  4 78 79 170  47      reflection 48 171 6 172 173 174 49
//   opacity   141 175 7 176 177 178 142   bump    143 179 8 270 271 272 144     refraction 146 273 9 274 275 276 147
//   normal map 42 72  3 73 74 75  43 (2007+, after 465)
struct MaterialMap {
    std::optional<double> blend;        // default 1.0
    std::optional<int16_t> source;      // 0 the current scene, 1 an image file (default 1)
    std::optional<std::string> file_name;   // default none
    std::optional<int16_t> projection;  // 1 planar (default), 2 box, 3 cylinder, 4 sphere
    std::optional<int16_t> tiling;      // 1 tile (default), 2 crop, 3 clamp
    std::optional<int16_t> auto_transform;  // bit set: 1 none (default), 2 scale to the entity, 4 block transform
    std::optional<std::array<double, 16>> transform;    // 16 x the transform's code; default the identity matrix

    bool empty() const {
        return !blend && !source && !file_name && !projection && !tiling && !auto_transform && !transform;
    }
};

// One value of a procedural material's table (2007+): its kind is its code (291 bool, 271 integer, 469 real,
// 301 text, 62 with 420 and 430 color)
struct MaterialProcedureValue {
    enum class Kind : uint8_t { Boolean, Integer, Real, Text, Color };
    Kind kind = Kind::Integer;
    bool boolean = false;               // 291
    int16_t integer = 0;                // 271
    double real = 0.0;                  // 469
    std::string text;                   // 301
    Color color;                        // 62, 420, 430
};

// A procedural material's table (2007+): its name (300), its values and the end of the table (292). No AutoCAD
// file has one: the grammar is the reference's group list (plan 9.7, ej testat, F9.6)
struct MaterialProcedure {
    std::string name;                   // 300
    std::vector<MaterialProcedureValue> values;
    std::optional<bool> table_end;      // 292
};

struct MaterialData {
    std::string name;                   // 1
    std::optional<std::string> description;     // 2
    MaterialColor ambient;
    MaterialColor diffuse;
    MaterialMap diffuse_map;
    std::optional<double> gloss;        // 44: the specular gloss factor (default 0.5)
    MaterialColor specular;
    MaterialMap specular_map;
    MaterialMap reflection_map;
    std::optional<double> opacity;      // 140: percent as 0 … 1 (default 1.0)
    MaterialMap opacity_map;
    MaterialMap bump_map;
    std::optional<double> refraction_index;     // 145 (default 1.0)
    MaterialMap refraction_map;
    // 2007+ (AutoCAD's reference and its files); the groups of the reference that no AutoCAD file has are ej testat
    std::optional<double> color_bleed_scale;        // 460
    std::optional<double> indirect_dump_scale;      // 461
    std::optional<double> reflectance_scale;        // 462
    std::optional<double> transmittance_scale;      // 463
    std::optional<bool> two_sided;                  // 290
    std::optional<double> luminance;                // 464
    std::optional<int16_t> luminance_mode;          // 270
    std::optional<int16_t> normal_map_method;       // 271
    std::optional<double> normal_map_strength;      // 465
    MaterialMap normal_map;
    std::optional<bool> anonymous;                  // 293
    std::optional<int16_t> global_illumination_mode;    // 272
    std::optional<int16_t> final_gather_mode;       // 273
    std::vector<MaterialProcedure> procedures;      // 300 …
    std::optional<int16_t> map_u_tile;              // 270 after the procedures
    std::optional<double> translucence;             // 148
    std::optional<int32_t> self_illumination;       // 90 after 148
    std::optional<double> reflectivity;             // 468
    std::optional<int32_t> illumination_model;      // 93
    std::optional<int32_t> channel_flags;           // 94
    // What follows the groups above in a record (a later version's, or another producer's): kept as it was read
    // and written back after them (B105)
    std::vector<DxfGroup> trailing;
};

// VISUALSTYLE (AcDbVisualStyle; in ACAD_VISUALSTYLE, 2000+) in AutoCAD's three forms. The properties of
// AcGiVisualStyle are fixed positions; the DXF names them by group code in the two older forms and by position
// only in the newest (the reference still shows the oldest form for every version, plan 9.7):
//   Form2007  2 70, then the 28 properties of the first table below by their codes, then 174, 93 .. and 291, 45
//             (2000 … 2007 and every older file AutoCAD saves; 2000 and 2004 without true color companions)
//   Form2010  2 70 177=2 291, then the same 28 properties each followed by its operation (176)
//   Form2013  2 70 177=3 291 70=n, then n values by position (28 of the table, then Post2010Prop28 … 57), each
//             followed by its operation (176); a color is 62 with 420 when it has a true color
// The properties 28 … 57 are called "Post2010PropNN" in AutoCAD's own round-trip XRECORD; before 2013 they and (in
// Form2007) the operations of the properties 0 … 27 are in that XRECORD of the style's extension dictionary, which
// the drawing reads into `properties` and writes back (sync_visual_style_records, drawing_roundtrip.cpp).
enum class VisualStyleForm : uint8_t { Form2007, Form2010, Form2013 };

enum class VisualStyleProperty : uint8_t {
    FaceLightingModel,          //  0  71   0 invisible, 1 visible, 2 Phong, 3 Gooch
    FaceLightingQuality,        //  1  72   0 none, 1 per face, 2 per vertex
    FaceColorMode,              //  2  73   0 none, 1 object, 2 background, 3 custom, 4 mono, 5 tinted, 6 desaturated
    FaceModifiers,              //  3  90   bit set: 1 opacity, 2 specular
    FaceOpacity,                //  4  40
    FaceSpecular,               //  5  41
    FaceMonoColor,              //  6  63 (62 before it in AutoCAD's older files) with 421
    EdgeModel,                  //  7  74   0 none, 1 isolines, 2 facet edges
    EdgeStyles,                 //  8  91
    EdgeIntersectionColor,      //  9  64
    EdgeObscuredColor,          // 10  65
    EdgeObscuredLinetype,       // 11  75
    EdgeIntersectionLinetype,   // 12  175
    EdgeCreaseAngle,            // 13  42
    EdgeModifiers,              // 14  92
    EdgeColor,                  // 15  66 with 424
    EdgeOpacity,                // 16  43
    EdgeWidth,                  // 17  76
    EdgeOverhang,               // 18  77
    EdgeJitter,                 // 19  78
    EdgeSilhouetteColor,        // 20  67 with 425
    EdgeSilhouetteWidth,        // 21  79
    EdgeHaloGap,                // 22  170
    EdgeIsolines,               // 23  171
    EdgeHidePrecision,          // 24  290
    DisplaySettings,            // 25  93
    DisplayBrightness,          // 26  44
    DisplayShadowType,          // 27  173
    Post2010Prop28, Post2010Prop29, Post2010Prop30, Post2010Prop31, Post2010Prop32, Post2010Prop33,
    Post2010Prop34, Post2010Prop35, Post2010Prop36, Post2010Prop37, Post2010Prop38, Post2010Prop39,
    Post2010Prop40, Post2010Prop41, Post2010Prop42, Post2010Prop43, Post2010Prop44, Post2010Prop45,
    Post2010Prop46, Post2010Prop47, Post2010Prop48, Post2010Prop49, Post2010Prop50, Post2010Prop51,
    Post2010Prop52, Post2010Prop53, Post2010Prop54, Post2010Prop55, Post2010Prop56, Post2010Prop57
};

constexpr size_t kVisualStylePropertyCount = 58;        // the properties of the newest form
constexpr size_t kVisualStyleOldPropertyCount = 28;     // those of the two older forms

enum class VisualStyleValueKind : uint8_t { Integer, Real, Boolean, Color, Text };

// A property's value; `kind` is the property's own (visual_style_property_kind())
struct VisualStyleValue {
    VisualStyleValueKind kind = VisualStyleValueKind::Integer;
    int32_t integer = 0;                // 90 (71 … 79, 91 … 93, 170 … 175 in the older forms)
    double real = 0.0;                  // 40 … 44
    bool boolean = false;               // 290
    // A color: the index (62 in the newest form, 63 in the older) and the true color (420; 421, 424, 425 in the older,
    // which only FaceMonoColor, EdgeColor and EdgeSilhouetteColor have); -1 = none. The 62 that AutoCAD's older
    // files write before 63 (always 5) is `color_tag`
    Color color;
    std::optional<int16_t> color_tag;
    std::string text;                   // 1
};

struct VisualStyleEntry {
    VisualStyleProperty property = VisualStyleProperty::FaceLightingModel;
    VisualStyleValue value;
    std::optional<int16_t> operation;   // 176 (or the XRECORD's RTVSPropertyOp / Post2010PropOp): 0 inherit, 1 set, 2 ...
};

struct VisualStyleData {
    VisualStyleForm form = VisualStyleForm::Form2013;
    std::string description;            // 2: the style's name
    int16_t style_type = 0;             // 70
    std::optional<int16_t> format_version;  // 177: 2 in Form2010, 3 in Form2013
    bool internal_use = false;          // 291 (at the end of Form2007, after 291's place in the others)
    std::vector<VisualStyleEntry> properties;   // in the order of their ids, as the record has them
    // Form2007 only
    std::optional<int16_t> edge_style_apply;    // 174, after EdgeHidePrecision
    std::optional<double> trailing_real;        // 45, last
    // The 2007 form's XRECORD also has the operation of properties that have no value in any form (RTVSPropertyOp58 …)
    std::vector<int16_t> extra_operations;
    // What follows the groups above (a later version's): kept as it was read and written back (B105)
    std::vector<DxfGroup> trailing;

    const VisualStyleEntry* find(VisualStyleProperty p) const {
        for (const VisualStyleEntry& e : properties) if (e.property == p) return &e;
        return nullptr;
    }
    VisualStyleEntry* find(VisualStyleProperty p) {
        for (VisualStyleEntry& e : properties) if (e.property == p) return &e;
        return nullptr;
    }
};

// The kind of value a property has, its name ("FaceLightingModel", "Post2010Prop41") and, in the older forms, the group
// code of its value (0 for the properties 28 … 57, which have none: they are the XRECORD's or positional)
VisualStyleValueKind visual_style_property_kind(VisualStyleProperty property);
const char* visual_style_property_name(VisualStyleProperty property);
int visual_style_property_code(VisualStyleProperty property);

} // namespace hdxf
