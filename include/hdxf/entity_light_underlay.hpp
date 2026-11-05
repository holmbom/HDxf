#pragma once

// LIGHT, the coordination model and the underlays.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// LIGHT (2007+): a point, spot or distant light, as AutoCAD writes it (AcDbLight; LibreDWG's
// example_2007/2010); AutoCAD puts its lights on *ADSK_SYSTEM_LIGHTS
// ---------------------------------------------------------------------------

struct LightData {
    int version = 1;                    // 90
    std::string name;                   // 1
    int light_type = 2;                 // 70: 1 distant, 2 point, 3 spot
    bool on = true;                     // 290
    Color color = Color::from_aci(7);   // 63 (and 421 when it has a true color)
    bool plot_glyph = false;            // 291
    double intensity = 1.0;             // 40
    Point3 position;                    // 10
    Point3 target = {0.0, 0.0, -1.0};   // 11
    int attenuation_type = 0;           // 72: 0 none, 1 inverse linear, 2 inverse square
    bool use_limits = false;            // 292
    double start_limit = 1.0;           // 41
    double end_limit = 10.0;            // 42
    double hotspot_angle = 45.0;        // 50: degrees
    double falloff_angle = 50.0;        // 51: degrees
    bool cast_shadows = true;           // 293
    int shadow_type = 0;                // 73: 0 ray traced, 1 shadow maps
    int shadow_map_size = 256;          // 91
    int shadow_softness = 1;            // 280
};


// ---------------------------------------------------------------------------
// COORDINATION MODEL (2013+; plan phase 6, B78): an attached Navisworks model and its definition
// (NAVISWORKSMODELDEF in the dictionary ACAD_BIM_DEFINITIONS), as Autodesk's DXF reference
// has them. The 4x4 transform and the insertion unit factor are two separate fields.
// ---------------------------------------------------------------------------

struct CoordinationModelData {
    int version = 1;                    // 70 (AutoCAD's core requires it between the subclass marker and 340)
    std::string definition_handle;      // 340: the NAVISWORKSMODELDEF; empty = none (written as 0)
    // 40 x 16: the model's transform in WCS, row by row (the translation in the fourth column)
    std::array<double, 16> matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    double unit_factor = 1.0;           // the 17th 40: insertion unit factor (not part of the matrix)
    // The entity's own proxy graphics (AcDbEntity 92/160 + 310) as a file has them: kept as bytes, written
    // from 2000 on when the drawing keeps proxy graphics; HDxf makes none (the model's drawing is Navisworks')
    std::vector<uint8_t> proxy_graphics;
};

// ---------------------------------------------------------------------------
// --- 9.6 underlays: PDFUNDERLAY, DWFUNDERLAY, DGNUNDERLAY and their definitions (phase 9, step 9.6) ---
// One data type for the three entities (AcDbUnderlayReference) and one for the three definitions
// (AcDbUnderlayDefinition); the entity's/object's type tells which file kind it is. AutoCAD's files
// have only PDFUNDERLAY and PDFDEFINITION; DWF and DGN have the same groups (Autodesk's reference).
// ---------------------------------------------------------------------------

enum class UnderlayKind { Pdf, Dwf, Dgn };

// UnderlayData::flags (280)
namespace UnderlayFlag {
constexpr int ClippingOn = 1;
constexpr int UnderlayOn = 2;
constexpr int Monochrome = 4;
constexpr int AdjustForBackground = 8;
constexpr int ClipInside = 16;
}

struct UnderlayData {
    std::string definition_handle;      // 340: the definition (hard pointer); empty = none (written as 0)
    Point3 insertion;                   // 10, 20, 30 (OCS)
    // 41, 42, 43: each is written only when it is not 1 (AutoCAD omits the default)
    Point3 scale = {1.0, 1.0, 1.0};
    double rotation = 0.0;              // 50 (degrees; omitted when 0)
    Point3 normal = {0.0, 0.0, 1.0};    // 210, 220, 230 (WCS; the three are omitted when it is the Z axis)
    int flags = UnderlayFlag::UnderlayOn;   // 280: see UnderlayFlag; bits HDxf does not name are kept
    int contrast = 100;                 // 281 (20 … 100)
    int fade = 0;                       // 282 (0 … 80)
    // 11, 21 repeated (OCS): two points are the lower left and upper right corner of a clip rectangle, more
    // are a clip polygon's vertices (Autodesk's reference)
    std::vector<Point2> clip;
    // The entity's own proxy graphics (AcDbEntity 92/160 + 310) as the file has them: AutoCAD's stream of an
    // underlay whose file it cannot read is the 8 bytes of an empty one. HDxf does not draw the underlay's
    // source (no PDF/DWF/DGN reader): it keeps these bytes and writes the empty stream for a new entity
    std::vector<uint8_t> proxy_graphics;
};

} // namespace hdxf
