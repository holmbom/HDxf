#pragma once

// The types everything else in HDxf is made of: versions, points, color, XDATA, preserved records and an entity's
// common properties.

#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <type_traits>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <utility>
#include <filesystem>
#include <optional>
#include <map>
#include <unordered_map>
#include <memory>
#include <array>

namespace hdxf {

// ---------------------------------------------------------------------------
// DXF version
// ---------------------------------------------------------------------------

enum class DxfVersion {
    R12,        // AC1009 — read and written
    R13,        // AC1012 — read and written (phase 7)
    R14,        // AC1014 — read and written (phase 7)
    R2000,      // AC1015
    R2004,      // AC1018
    R2007,      // AC1021
    R2010,      // AC1024
    R2013,      // AC1027
    R2018       // AC1032
};

// The $ACADVER value of a version ("AC1009" …) and back. version_from_acadver() gives
// nothing for a value HDxf does not know.
inline std::string_view acadver_string(DxfVersion version) {
    switch (version) {
        case DxfVersion::R12:   return "AC1009";
        case DxfVersion::R13:   return "AC1012";
        case DxfVersion::R14:   return "AC1014";
        case DxfVersion::R2000: return "AC1015";
        case DxfVersion::R2004: return "AC1018";
        case DxfVersion::R2007: return "AC1021";
        case DxfVersion::R2010: return "AC1024";
        case DxfVersion::R2013: return "AC1027";
        case DxfVersion::R2018: return "AC1032";
    }
    return "";
}

inline std::optional<DxfVersion> version_from_acadver(std::string_view acadver) {
    const DxfVersion all[] = {DxfVersion::R12, DxfVersion::R13, DxfVersion::R14,
                              DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007,
                              DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018};
    for (DxfVersion v : all) {
        if (acadver_string(v) == acadver) return v;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// DXF format (ASCII or Binary)
// ---------------------------------------------------------------------------

enum class DxfFormat { Ascii, Binary };

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------
//
// File paths are taken as std::filesystem::path so the stream constructors pick
// the wide overload on Windows — a narrow std::ifstream(std::string) goes to
// fopen and is interpreted in the ANSI code page, which loses non-ASCII paths.
//
// Use these helpers when the caller holds a path as UTF-8 in std::string: they
// convert via char8_t and are therefore UTF-8 on every toolchain, unlike
// std::filesystem::path(std::string) which is UTF-8 only on libstdc++.

inline std::filesystem::path path_from_utf8(std::string_view utf8) {
    // memcpy, not a reinterpret_cast: reading char storage through char8_t
    // would violate the aliasing rules. A byte copy is well defined both ways
    // and, unlike char8_t <-> char conversion, keeps every byte unchanged
    // regardless of whether char is signed.
    std::u8string s(utf8.size(), u8'\0');
    if (!utf8.empty()) std::memcpy(s.data(), utf8.data(), utf8.size());
    return std::filesystem::path(std::move(s));
}

inline std::string utf8_from_path(const std::filesystem::path& p) {
    const std::u8string s = p.u8string();
    std::string out(s.size(), '\0');
    if (!s.empty()) std::memcpy(out.data(), s.data(), s.size());
    return out;
}

// ---------------------------------------------------------------------------
// Point types
// ---------------------------------------------------------------------------

struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Point2 {
    double x = 0.0;
    double y = 0.0;

    operator Point3() const { return Point3{x, y, 0.0}; }
};

// ---------------------------------------------------------------------------
// Color: an ACI index (62), and from DXF 2004 a true color (420) and the name of a color
// from a color book (430)
// ---------------------------------------------------------------------------

// Where the fonts text styles name are found (.ttf, .ttc, .otf, .shx), as AutoCAD's support
// file search path: for the size of text (extents, MTEXT, dimensions). Default: the Windows
// Fonts folder. A font that is not found: simplex.shx instead (AutoCAD's FONTALT), without
// it the text counts by its insertion point; the save reports it in warnings().
void set_font_paths(std::vector<std::filesystem::path> paths);
std::vector<std::filesystem::path> font_paths();

// AutoCAD's ACI palette: the RGB (0xRRGGBB) of index 1-255, and the index nearest to an RGB
// color, as AutoCAD writes it in 62 next to a true color (plan phase 2, B21)
int aci_rgb(int aci);
int nearest_aci(int rgb);

// aci, rgb and name are one color together: aci is always the color for whoever reads only
// ACI. Change a color with from_aci()/from_rgb()/from_book(), which set all three.
struct Color {
    int aci = 256;          // 62: 0 ByBlock, 1-255, 256 ByLayer (default)
    int rgb = -1;           // 420 (2004+): 0xRRGGBB; -1 = no true color
    std::string name;       // 430 (2004+): "BOOK$COLOR" from a color book; empty = none

    static Color from_aci(int index) { return Color{index, -1, {}}; }
    static Color from_rgb(int r, int g, int b) {
        int value = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
        return Color{nearest_aci(value), value, {}};
    }
    // A color from a color book: its name in the file's form ("RAL CLASSIC$RAL 1006") and
    // its RGB
    static Color from_book(const std::string& book_and_color, int r, int g, int b) {
        Color c = from_rgb(r, g, b);
        c.name = book_and_color;
        return c;
    }

    static Color by_block() { return Color{0, -1, {}}; }
    static Color by_layer() { return Color{256, -1, {}}; }

    bool is_by_block() const { return aci == 0 && rgb < 0; }
    bool is_by_layer() const { return aci == 256 && rgb < 0; }
    bool has_rgb() const { return rgb >= 0; }
    int rgb_red() const { return (rgb >> 16) & 0xFF; }
    int rgb_green() const { return (rgb >> 8) & 0xFF; }
    int rgb_blue() const { return rgb & 0xFF; }

    static Color red()     { return from_aci(1); }
    static Color yellow()  { return from_aci(2); }
    static Color green()   { return from_aci(3); }
    static Color cyan()    { return from_aci(4); }
    static Color blue()    { return from_aci(5); }
    static Color magenta() { return from_aci(6); }
    static Color white()   { return from_aci(7); }
};

// ---------------------------------------------------------------------------
// Dimension type
// ---------------------------------------------------------------------------

enum class DimensionType {
    Rotated    = 0,
    Aligned    = 1,
    Angular    = 2,
    Diameter   = 3,
    Radius     = 4,
    Angular3Pt = 5,
    Ordinate   = 6
};

// ---------------------------------------------------------------------------
// Entity types
// ---------------------------------------------------------------------------

// LwPolyline and the types after it are DXF 2000 types: a drawing that has one can be
// written as 2000, not as R12.
enum class EntityType {
    Line,
    Circle,
    Arc,
    Point,
    Text,
    Polyline,
    Trace,
    Solid,
    Face3D,
    Shape,
    Insert,
    Attdef,
    Attrib,
    Dimension,
    Viewport,
    LwPolyline,
    Ellipse,
    Xline,
    Ray,
    MText,
    Tolerance,
    Leader,
    Spline,
    Hatch,
    Mline,
    Image,
    Solid3D,
    Region,
    Body,
    MLeader,        // MULTILEADER: AutoCAD 2008's, in 2000, 2004 and 2007 files too
    // AutoCAD 2007's surfaces (SurfaceData) and its helix (HelixData)
    Surface,        // SURFACE
    ExtrudedSurface,
    LoftedSurface,
    RevolvedSurface,
    SweptSurface,
    PlaneSurface,
    Helix,
    Mesh,           // MESH: AutoCAD 2010's subdivision mesh (MeshData), in 2000 … 2010 files
    Wipeout,        // WIPEOUT (2000+): IMAGE's data (ImageData) under AcDbWipeout
    Light,          // LIGHT (2007+): LightData
    CoordinationModel,  // COORDINATION_MODEL (2013+): CoordinationModelData
    // --- 9.6 underlays ---
    PdfUnderlay,        // PDFUNDERLAY (2000+): UnderlayData
    DwfUnderlay,        // DWFUNDERLAY (2000+): UnderlayData
    DgnUnderlay,        // DGNUNDERLAY (2000+): UnderlayData
    // --- 9.3 OLE and proxy classes ---
    OleFrame,          // OLEFRAME (R13; in the references of 2000 … 2018): OleFrameData
    Ole2Frame,          // OLE2FRAME (R14 and later): Ole2FrameData
    ProxyEntity,        // ACAD_PROXY_ENTITY (R14 and later): ProxyEntityData
    Table,              // ACAD_TABLE (2000+): TableData (phase 9, step 9.1)
    // --- 9.12 sections ---
    SectionObject,      // SECTIONOBJECT (2000+): SectionObjectData
    NurbsSurface,   // NURBSURFACE (2013+; before it AutoCAD writes SURFACE): SurfaceData with NurbSurface
    // --- 9.17 special dimensions ---
    ArcDimension,       // ARC_DIMENSION (2000+): ArcDimensionData
    LargeRadialDimension,   // LARGE_RADIAL_DIMENSION (2000+): LargeRadialDimensionData
    // --- 9.18 Map, TC and ZWM ---
    TcPoint,            // TCPOINTENTITY (TurboCAD): TcPointData
    Unknown
};

// ---------------------------------------------------------------------------
// XDATA types (extended entity data, groups 1000-1071)
// ---------------------------------------------------------------------------

struct XdataItem {
    int code = 0;
    std::string value;
};

struct XdataList {
    std::string app_name;
    std::vector<XdataItem> items;
};

// ---------------------------------------------------------------------------
// Preserved records (DXF 2000+ files)
// ---------------------------------------------------------------------------
//
// A record read from a DXF R13–2018 file is kept as it stood in the file: every group,
// in file order, the value exactly as written (only the line ending removed; binary
// numbers as round-trip decimal text, binary chunks as hex). Comments (999) are not
// kept. The interpreted fields beside a record describe the same thing; after a change,
// the interpreted field is what counts.
//
// R12 files and drawings built in memory have no preserved records — raw is empty.

class Drawing;
namespace internal {
class DxfReader;
class DxfWriter;
struct AsciiForm;
std::string template_text(const Drawing& dwg, bool source_binary);
}

struct DxfGroup {
    int code = 0;
    std::string value;
    // ASCII: the width of the code line as the file had it where it is not AutoCAD's three columns (0):
    // four for some R12 records AutoCAD exports other types as, one (no padding) for R14 files of other
    // writers
    int code_columns = 0;
};

class RawRecord {
public:
    bool empty() const { return data_.empty(); }
    std::vector<DxfGroup> groups() const;

private:
    friend class internal::DxfReader;
    void append(int code, const std::string& value, int code_columns = 0);
    // The group code of a stored 16 bit word (see data_)
    static int decode_code(uint16_t word, int& code_columns);

    // per group: a 16 bit word (little-endian), the value, '\0'. The word is the code, with bit 15 clear
    // and the width of an ASCII code line that is not three columns in bits 12-14 (code up to 4095); a
    // negative code (the binary sentinels) is the int16 as it is
    std::string data_;
};

// A table from a DXF 2000+ file: its TABLE record, and — for tables HDxf does not
// interpret (unknown tables), BLOCK_RECORD entries that belong to no block (the others
// are in Block::record) or records it does not recognise inside a table — the entries,
// in file order.
struct RawTable {
    std::string name;
    RawRecord header;
    std::vector<RawRecord> entries;
};

// A section HDxf does not interpret (CLASSES, unknown; ACDSDATA of a file before 2013), or
// HEADER: head is the record that starts with 0/SECTION (for HEADER, the
// whole section), records the records that follow, in file order. Records inside an
// interpreted section that no parser took are kept here too, under that section's name.
struct RawSection {
    std::string name;
    RawRecord head;
    std::vector<RawRecord> records;
};

// ---------------------------------------------------------------------------
// Entity properties (common to all entities)
// ---------------------------------------------------------------------------

// Transparency (2004+) as AutoCAD stores it: the method in the high byte — 0 ByLayer,
// 1 ByBlock, 2 by alpha — and the alpha in the low byte (0 clear … 255 opaque). An
// entity's is 440; a layer's is XDATA "AcCmTransparency" (plan phase 2, B22).
struct Transparency {
    int raw = 0;

    static Transparency by_layer() { return Transparency{0}; }
    static Transparency by_block() { return Transparency{0x01000000}; }
    static Transparency from_alpha(int alpha) { return Transparency{0x02000000 | (alpha & 0xFF)}; }

    bool is_by_layer() const { return raw == 0; }
    bool is_by_block() const { return raw == 0x01000000; }
    // 0-255, or -1 for ByLayer and ByBlock
    int alpha() const { return (raw >> 24) == 2 ? raw & 0xFF : -1; }
};

// A color as AutoCAD stores it in one 32-bit value (AcCmColor) — MULTILEADER's and
// MLEADERSTYLE's: the method in the high byte — 0xC0 ByLayer, 0xC1 ByBlock, 0xC2 an RGB color
// (0xC2RRGGBB), 0xC3 an ACI color (0xC30000nn), 0xC8 none — and the color in the rest
struct CmColor {
    int32_t raw = static_cast<int32_t>(0xC1000000u);    // ByBlock

    static CmColor by_layer() { return CmColor{static_cast<int32_t>(0xC0000000u)}; }
    static CmColor by_block() { return CmColor{static_cast<int32_t>(0xC1000000u)}; }
    static CmColor none()     { return CmColor{static_cast<int32_t>(0xC8000000u)}; }
    static CmColor from_color(const Color& c) {
        uint32_t v = c.has_rgb() ? 0xC2000000u | static_cast<uint32_t>(c.rgb & 0xFFFFFF)
                   : c.aci == 0   ? 0xC1000000u
                   : c.aci == 256 ? 0xC0000000u
                                  : 0xC3000000u | static_cast<uint32_t>(c.aci & 0xFF);
        return CmColor{static_cast<int32_t>(v)};
    }

    int method() const { return static_cast<int>((static_cast<uint32_t>(raw) >> 24) & 0xFF); }
    bool is_none() const { return method() == 0xC8; }
    // As a Color: ByLayer, ByBlock, the RGB or the ACI color. Another method (none …) has no
    // Color: ByLayer then — see method()
    Color color() const {
        uint32_t v = static_cast<uint32_t>(raw);
        switch (method()) {
            case 0xC1: return Color::by_block();
            case 0xC2: return Color::from_rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
            case 0xC3: return Color::from_aci(static_cast<int>(v & 0xFF));
            default:   return Color::by_layer();
        }
    }
};

struct EntityProperties {
    std::string layer = "0";
    Color color;
    std::string linetype;
    bool visible = true;
    double linetype_scale = 1.0;
    double thickness = 0.0;
    Point3 extrusion = {0.0, 0.0, 1.0};
    int paper_space = 0;
    std::string handle;
    // 370 (2000+): in 1/100 mm; -1 ByLayer, -2 ByBlock, -3 the default lineweight
    int lineweight = -1;
    // 380 (2000+): the plot style's type (AcDb::PlotStyleNameType): 0 ByLayer, 1 ByBlock,
    // 2 the dictionary's default, 3 by its handle (390) — AutoCAD writes it when not 0
    int plot_style_type = 0;
    std::string plot_style_handle;      // 390 (2000+): named plot style, empty = ByLayer
    Transparency transparency;          // 440 (2004+): ByLayer is not written
    std::string material_handle;        // 347 (2007+): the MATERIAL, empty = ByLayer
    int shadow_mode = 0;                // 284 (2007+): 0 casts and receives, 1 casts, 2 receives,
                                        //     3 ignores shadows
    // 360 in 102 {ACAD_XDICTIONARY (2000+): the entity's extension dictionary (a DICTIONARY in
    // objects(), owned by the entity); empty = none. A MULTILEADER's text_extended is a
    // round-trip XRECORD in it before 2013 (plan phase 5, B60)
    std::string xdictionary;
    // 330 in 102 {ACAD_REACTORS (2000+): the records the entity has as reactors, as read (an
    // associative dimension's DIMASSOC, an ACDBASSOC* network …). Written with the reactors
    // HDxf's relations give (a GROUP's members, a hatch's boundary, a leader's annotation);
    // one to a record that is gone is left out (B19). Handles of this drawing: an entity copied
    // to another drawing has them cleared, as its xdictionary
    std::vector<std::string> reactors;
    std::vector<XdataList> xdata;
};

} // namespace hdxf
