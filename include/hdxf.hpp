#pragma once

// HDXF - DXF File Library, version 1.9.1
// Reads DXF R12 … 2018 (ASCII and binary); writes R12, R13, R14, 2000, 2004, 2007, 2010, 2013 and
// 2018. Every record of a file is preserved in full (RawRecord): a drawing read from a file is
// written back from them in its own version, with only what changed written anew.
//
// Every string in this API is UTF-8 (text, names, XDATA strings). Files are decoded
// from $DWGCODEPAGE (up to 2004) or UTF-8 (2007+). R12 is written in the drawing's
// $DWGCODEPAGE with \U+XXXX for characters the code page lacks; 2000 and 2004 have \U+XXXX
// for everything outside ASCII, 2007, 2010 and 2013 are UTF-8 — as AutoCAD writes them.

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
// Version
// ---------------------------------------------------------------------------

struct Version {
    static constexpr int major = 1;
    static constexpr int minor = 9;
    static constexpr int patch = 1;
    static constexpr const char* suffix = "";

    static std::string string() {
        std::string v = std::to_string(major) + "." +
                        std::to_string(minor) + "." +
                        std::to_string(patch);
        if (suffix[0] != '\0') v += "-" + std::string(suffix);
        return v;
    }
};

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
    int m_count = 0;                    // group 71
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

// A value on the heap that may be absent, copied deeply — for the parts of an entity that few
// have (MTEXT's columns, a multiline attribute's text), so they do not make every Entity
// larger (plan phase 6, B84). Converts from T, so `data.columns = MTextColumns{…}` works.
template <typename T>
class OptionalBoxed {
public:
    OptionalBoxed() = default;
    OptionalBoxed(T value) : p_(std::make_unique<T>(std::move(value))) {}
    OptionalBoxed(const OptionalBoxed& other) : p_(other.p_ ? std::make_unique<T>(*other.p_) : nullptr) {}
    OptionalBoxed(OptionalBoxed&& other) noexcept = default;
    OptionalBoxed& operator=(const OptionalBoxed& other) {
        if (this != &other) p_ = other.p_ ? std::make_unique<T>(*other.p_) : nullptr;
        return *this;
    }
    OptionalBoxed& operator=(OptionalBoxed&& other) noexcept = default;
    OptionalBoxed& operator=(T value) {
        p_ = std::make_unique<T>(std::move(value));
        return *this;
    }
    bool has_value() const { return p_ != nullptr; }
    explicit operator bool() const { return p_ != nullptr; }
    T& operator*() { return *p_; }
    const T& operator*() const { return *p_; }
    T* operator->() { return p_.get(); }
    const T* operator->() const { return p_.get(); }
    T& emplace() {
        p_ = std::make_unique<T>();
        return *p_;
    }
    void reset() { p_.reset(); }

private:
    std::unique_ptr<T> p_;
};

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
// VIEWPORT (paper space)
// ---------------------------------------------------------------------------

// In R12 the view is ACAD/MVIEW XDATA on the entity; from 2000 it is these fields. A
// drawing written as 2000 that still has MVIEW XDATA (read from R12) takes the view from
// the XDATA.
struct ViewportData {
    Point3 center;
    double width = 0.0;
    double height = 0.0;
    int status = 0;     // group 68
    int id = 0;         // group 69
    // 2000+
    Point2 view_center;                     // 12, 22 (model space)
    Point2 snap_base;                       // 13, 23
    Point2 snap_spacing = {10.0, 10.0};     // 14, 24
    Point2 grid_spacing = {10.0, 10.0};     // 15, 25
    Point3 view_direction = {0.0, 0.0, 1.0}; // 16, 26, 36
    Point3 view_target;                     // 17, 27, 37
    double lens_length = 50.0;              // 42
    double front_clip = 0.0;                // 43
    double back_clip = 0.0;                 // 44
    double view_height = 0.0;               // 45 (model space); 0 = the viewport's height
    double snap_angle = 0.0;                // 50
    double twist_angle = 0.0;               // 51
    int circle_zoom = 1000;                 // 72
    int status_flags = 819232;              // 90, as AutoCAD writes a new viewport
    int render_mode = 0;                    // 281
    int ucs_per_viewport = 1;               // 71
    int ucs_icon = 0;                       // 74
    int ucs_ortho_type = 0;                 // 79
    Point3 ucs_origin;                      // 110, 120, 130
    Point3 ucs_x_axis = {1.0, 0.0, 0.0};    // 111, 121, 131
    Point3 ucs_y_axis = {0.0, 1.0, 0.0};    // 112, 122, 132
    double elevation = 0.0;                 // 146
    int shade_plot_mode = 0;                // 170 (2004+): 0 as displayed, 1 wireframe,
                                            //     2 hidden, 3 rendered
    // 2007+, as AutoCAD writes a new viewport
    int grid_major = 5;                     // 61: a major grid line every n lines
    std::string visual_style_handle;        // 348: the VISUALSTYLE, empty = 2dWireframe
    // 348 in AcDbEntity instead of AcDbViewport, as some AutoCAD files have it (KioskStation's;
    // in no reference)
    bool visual_style_in_entity = false;
    bool default_lighting = true;           // 292
    int default_lighting_type = 1;          // 282: 0 one distant light, 1 two
    double brightness = 0.0;                // 141
    double contrast = 0.0;                  // 142
    Color ambient_color = Color::from_rgb(51, 51, 51);  // 63, 421, 431 (ACI 250)
    std::string background_handle;          // 332
    std::string shade_plot_handle;          // 333
    std::string sun_handle;                 // 361
    std::string ucs;                        // 345: a named UCS, empty = the one above
    std::string base_ucs;                   // 346: UCS name, empty = WORLD
    std::string plot_style_sheet;           // 1
    std::string clip_boundary_handle;       // 340: the entity that clips the viewport
    std::vector<std::string> frozen_layers; // 331, as layer names
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
// IMAGE (2000+), in the WCS; its IMAGEDEF and IMAGEDEF_REACTOR are objects
// ---------------------------------------------------------------------------

struct ImageData {
    int class_version = 0;              // 90
    Point3 insertion_point;             // 10, 20, 30: the lower left corner
    Point3 u_vector = {1.0, 0.0, 0.0};  // 11, 21, 31: one pixel along the image's x
    Point3 v_vector = {0.0, 1.0, 0.0};  // 12, 22, 32: one pixel along its y
    Point2 size;                        // 13, 23: in pixels
    std::string image_def_handle;       // 340: the IMAGEDEF
    int display_flags = 7;              // 70: 1 show, 2 show when not aligned with the
                                        //     screen, 4 use the clip boundary, 8 transparency
    bool clipping = false;              // 280
    int brightness = 50;                // 281
    int contrast = 50;                  // 282
    int fade = 0;                       // 283
    std::string reactor_handle;         // 360: its IMAGEDEF_REACTOR (owned by the image)
    int clip_boundary_type = 1;         // 71: 1 rectangle, 2 polygon
    std::vector<Point2> clip_boundary;  // 91 (the count), 14, 24: in pixels
    bool clip_inside = false;           // 290 (2010+): the clip mode, false = outside
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
// Surfaces (plan phase 3, B39): AutoCAD 2007's procedural surfaces — EXTRUDEDSURFACE,
// LOFTEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE, PLANESURFACE and the plain SURFACE. Each is
// its ACIS body (AcDbModelerGeometry, as 3DSOLID), its isolines (AcDbSurface) and what it was
// made from (its type's subclass, as the DXF reference and AutoCAD's files have it). AutoCAD
// writes them in 2007 with empty proxy graphics; in 2004 with their isolines as proxy
// graphics, which HDxf keeps but does not make — a new surface needs 2007.
// ---------------------------------------------------------------------------

// An entity a surface was made from, as the file has it: its class (90; a swept surface's
// path 91), the size of its data in bits (90 — AutoCAD's files have a byte more than the
// bits fill) and the data (310, in groups of 127 bytes)
struct SurfaceSource {
    int class_id = 0;
    int bits = 0;
    std::vector<uint8_t> data;
};

// A sweep's options: EXTRUDEDSURFACE's and SWEPTSURFACE's
struct SurfaceSweep {
    double draft_angle = 0.0;           // 42 (radians)
    double draft_start = 0.0;           // 43
    double draft_end = 0.0;             // 44
    double twist = 0.0;                 // 45
    double scale = 1.0;                 // 48
    double align_angle = 0.0;           // 49 (radians)
    std::vector<double> sweep_matrix;   // 46 (16, row by row; empty = identity)
    std::vector<double> path_matrix;    // 47 (16)
    bool solid = false;                 // 290
    int alignment = 0;                  // 70: 0 none, 1 align the sweep entity to the path,
                                        //     2 move it to the path, 3 the path to it
    int option_71 = 2;                  // 71: not in Autodesk's reference; AutoCAD writes 1 or 2
    bool align_start = true;            // 292
    bool bank = false;                  // 293
    bool base_point_set = false;        // 294
    bool sweep_transform_computed = true;   // 295
    bool path_transform_computed = false;   // 296
    Point3 reference_vector;            // 11: for the twist
};

// AcDbNurbSurface (NURBSURFACE, 2013 and later), in the order AutoCAD writes it (plan 9.16, F9.17). AutoCAD's files
// have 0 in every group — also for a surface converted from a loft or made in NURBS mode: the surface's knots and
// control points are in its ACIS body, not here (the name NURBSURFACE says nothing of knot fields)
struct NurbSurface {
    int16_t flag = 0;                   // 170
    bool flag_290 = false;              // 290
    Point3 point_10;                    // 10
    Point3 point_11;                    // 11
    Point3 point_12;                    // 12
    Point3 point_13;                    // 13
};

struct SurfaceData {
    AcisData acis;                      // AcDbModelerGeometry
    int u_isolines = 6;                 // 71
    int v_isolines = 6;                 // 72
    NurbSurface nurbs;                  // NURBSURFACE: AcDbNurbSurface
    // EXTRUDEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE: the entity extruded, revolved, swept
    SurfaceSource source;               // 90, 90, 310
    // The source's transform (16, row by row; empty = identity): EXTRUDEDSURFACE,
    // LOFTEDSURFACE and SWEPTSURFACE 40, REVOLVEDSURFACE 42
    std::vector<double> matrix;
    // EXTRUDEDSURFACE
    Point3 sweep_vector;                // 10
    // EXTRUDEDSURFACE, SWEPTSURFACE
    SurfaceSweep sweep;
    // SWEPTSURFACE: the path (91, 90, 310) and its transform (41)
    SurfaceSource path;
    std::vector<double> path_entity_matrix;
    // REVOLVEDSURFACE
    Point3 axis_point;                  // 10
    Point3 axis_vector = {0.0, 0.0, 1.0};   // 11
    double revolve_angle = 0.0;         // 40 (radians)
    double start_angle = 0.0;           // 41
    double revolve_draft_angle = 0.0;   // 43
    double start_draft_distance = 0.0;  // 44
    double end_draft_distance = 0.0;    // 45
    double revolve_twist = 0.0;         // 46
    bool revolve_solid = false;         // 290
    bool close_to_axis = false;         // 291
    // LOFTEDSURFACE: the curves — cross sections, guides, path, in the file's order (90, 90,
    // 310 each; the file does not say which is which)
    std::vector<SurfaceSource> curves;
    int plane_normal_lofting = 0;       // 70
    double start_draft_angle = 0.0;     // 41
    double end_draft_angle = 0.0;       // 42
    double start_draft_magnitude = 0.0; // 43
    double end_draft_magnitude = 0.0;   // 44
    bool arc_length_parameters = false; // 290
    bool no_twist = true;               // 291
    bool align_direction = true;        // 292
    bool simple_surfaces = true;        // 293
    bool closed = false;                // 294
    bool loft_solid = false;            // 295
    bool ruled = false;                 // 296
    bool virtual_guide = true;          // 297
};

// ---------------------------------------------------------------------------
// HELIX (plan phase 3, B39): AutoCAD 2007's helix, a spline (AcDbSpline, as SPLINE) and its
// definition (AcDbHelix). Its proxy graphics are the spline drawn as a polyline.
// ---------------------------------------------------------------------------

struct HelixData {
    SplineData spline;                  // AcDbSpline
    int major_version = 33;             // 90
    int maintenance_version = 29;       // 91
    Point3 axis_base;                   // 10
    Point3 start;                       // 11: where it starts (its base radius from the axis)
    Point3 axis_vector = {0.0, 0.0, 1.0};   // 12
    double radius = 1.0;                // 40: the top radius
    double turns = 3.0;                 // 41
    double turn_height = 1.0;           // 42
    bool right_handed = true;           // 290: 0 left, 1 right
    int constrain = 1;                  // 280: 0 turn height, 1 turns, 2 height
};

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

// ---------------------------------------------------------------------------
// MESH (plan phase 4, B48/B49): AutoCAD 2010's subdivision mesh (AcDbSubDMesh) — its level-0
// cage of vertices, faces, edges and their creases, and the smoothing level AutoCAD subdivides
// it to (Catmull-Clark, B49). AutoCAD writes it in 2000 … 2010 with the same groups, its edges
// as proxy graphics.
// ---------------------------------------------------------------------------

// An edge of the cage: two vertex indices (90, 90), the smaller first as AutoCAD writes them
struct MeshEdge {
    int start = 0;
    int end = 0;
};

// A property a face, edge or vertex has of its own — as AutoCAD reads it and ODA writes it (no
// AutoCAD file has one): its color, material or transparency
struct MeshProperty {
    int type = 0;                       // 90: 0 color, 1 material, 2 transparency (3 a material
                                        //     mapper: see MeshData::overrides_unread)
    Color color;                        // type 0: 63, 421, 431
    std::string material_handle;        // type 1: 330, the MATERIAL
    Transparency transparency;          // type 2: 90
};

struct MeshOverride {
    int marker = 0;                     // 91: the sub-entity's marker
    std::vector<MeshProperty> properties;   // 92 (the count)
};

struct MeshData {
    int version = 2;                    // 71
    bool blend_crease = false;          // 72 ("blend crease"; changes nothing AutoCAD smooths)
    int level = 0;                      // 91: the smoothing level, 0 = the cage itself
    std::vector<Point3> vertices;       // 92 (the count), 10, 20, 30
    std::vector<std::vector<int>> faces;    // 93 (the list's size), 90: each face's vertex
                                            // count and its vertex indices
    std::vector<MeshEdge> edges;        // 94 (the count), 90, 90
    std::vector<double> creases;        // 95 (the count), 140: one per edge — 0 none, -1 always,
                                        //     n the levels it stays sharp (fractions blend)
    std::vector<MeshOverride> overrides;    // 90 (the count)
    // A MESH read with a material mapper (property type 3) among its overrides: the override
    // data from its count (90) on as read, `overrides` then empty — the mapper's DXF form is not
    // known (no AutoCAD file has one, AutoCAD's reader refuses every form tried) — written back
    // as it was
    std::vector<DxfGroup> overrides_unread;
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

// What a reference of a proxy is: 330, 340, 350 or 360
enum class ProxyPointerKind : int32_t {
    SoftPointer = 330,
    HardPointer = 340,
    SoftOwner = 350,
    HardOwner = 360
};

// One object id of the proxy's id list: the handle ("0" is a null id, as AutoCAD writes it)
struct ProxyReference {
    ProxyPointerKind kind = ProxyPointerKind::HardPointer;
    std::string handle;
};

// A CLASS record of the application's class, as the CLASSES section has it (1, 2, 3, 90, 281)
struct ProxyApplicationClass {
    std::string dxf_name;                       // 1: the DXF name of the class ("DMMAPMANAGER")
    std::string cpp_name;                       // 2: the C++ class name ("AcMapDMMapManager")
    std::string application;                    // 3: the application ("AcMapDisplayManagement")
    int32_t proxy_flags = 0;                    // 90
    bool entity = false;                        // 281: the class is an entity's
};

// 95 (or 71 with 97 in 2018): the drawing format the object had when it became a proxy; the low word is
// AcDbDwgVersion, the high word the maintenance release
struct ProxyDrawingFormat {
    uint16_t dwg_version = 0;
    uint16_t maintenance_version = 0;
    uint32_t combined() const { return (static_cast<uint32_t>(maintenance_version) << 16) | dwg_version; }
};

// The class data of a proxy entity or object (AcDbProxyEntity, AcDbProxyObject)
struct ProxyClassData {
    int32_t class_id = 499;                     // 90: 498 for an entity, 499 for an object
    int32_t application_class_id = 500;         // 91: 500 + the class's position in CLASSES, as the file has it;
                                                //     written from `application_class` when that names a class
    ProxyApplicationClass application_class;    // the class 91 names (read from CLASSES; empty when it names none)
    std::optional<ProxyDrawingFormat> drawing_format;   // 95 / 71 + 97; none in R14's form
    int16_t original_format = 0;                // 70: 0 the data is DWG's, 1 DXF's (then the class's own subclasses
                                                //     follow the proxy's, typed in `class_data`: no 93/161, no ids)
    // The graphics the application drew: AutoCAD 2007 and later keep them apart from the data (96 before 2010,
    // 162 from, the length in bits and the bytes in 311); R14 has them as 92 (bytes) + 310; 2000 and 2004
    // have them inside the data. A proxy of DXF data (70 = 1) has them in bytes: 92 before 2010, 160 from, and
    // 310 (AutoCAD's TurboCAD point). `graphics_bits` counts the bits of `graphics` (8 per byte in the byte forms)
    std::vector<uint8_t> graphics;
    uint32_t graphics_bits = 0;
    // The application's object data: 93 (161 from 2010) is its length in bits, 310 its bytes
    std::vector<uint8_t> data;
    uint32_t data_bits = 0;
    std::vector<ProxyReference> references;     // 330/340/350/360 up to 94, in order (the data refers to them by place)
    bool zombie_marker = false;                 // R13 files of other producers name the subclass AcDbZombieObject /
                                                //     AcDbZombieEntity: read so, written so
};

// The class's own data of a proxy of DXF data (70 = 1, step 9.S): AutoCAD writes the class's subclasses after the proxy's
// (TurboCAD's TCPOINTENTITY: AcDbPoint and TcPointEntity; Map's, ZWM's and TCOPTIONALDATA's objects), and in the version the
// object was made in, the class's own record. They are the type HDxf has for the class (defined after EntityData and
// ObjectData); a proxy whose class HDxf has no type for stays untyped, as it was read
struct ProxyEntityClass;
struct ProxyObjectClass;

// ACAD_PROXY_ENTITY (R14 and later)
struct ProxyEntityData {
    ProxyEntityData() { proxy.class_id = 498; }
    ProxyClassData proxy;
    std::vector<uint8_t> entity_graphics;       // the entity's own proxy graphics (AcDbEntity 92/160 + 310), apart
                                                //     from the class's `proxy.graphics`
    OptionalBoxed<ProxyEntityClass> class_data; // 70 = 1: the class's subclasses as its type (TCPOINTENTITY: TcPointData)
};

// ACAD_PROXY_OBJECT (2000 and later; R13 and R14 as read)
struct ProxyObjectData {
    ProxyClassData proxy;
    OptionalBoxed<ProxyObjectClass> class_data; // 70 = 1: the class's subclasses as its type (TCOPTIONALDATA, DM…, ZWM…)
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

// A value kept on the heap and copied deeply — for entity data that is large but rare
// (VIEWPORT), so it does not make every Entity larger. Converts from T, so
// `entity.data = ViewportData{…}` works; Entity::as_viewport() gives the T.
template <typename T>
class Boxed {
public:
    Boxed() : p_(std::make_unique<T>()) {}
    Boxed(T value) : p_(std::make_unique<T>(std::move(value))) {}
    Boxed(const Boxed& other) : p_(std::make_unique<T>(*other.p_)) {}
    Boxed(Boxed&& other) noexcept : p_(std::move(other.p_)) {}
    Boxed& operator=(const Boxed& other) {
        if (this != &other) p_ = std::make_unique<T>(*other.p_);
        return *this;
    }
    Boxed& operator=(Boxed&& other) noexcept {
        p_ = std::move(other.p_);
        return *this;
    }
    T& operator*() { return *p_; }
    const T& operator*() const { return *p_; }

private:
    std::unique_ptr<T> p_;
};

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

struct UnderlayDefinitionData {
    std::string file_name;              // 1: the file's path as AutoCAD stores it (UTF-8; relative or absolute)
    std::string item_name;              // 2: the page of a PDF (its number as text), a DWF sheet, a DGN model
};
// --- end 9.6 ---

// ---------------------------------------------------------------------------
// Value types the typed records of phase 9 share (plan 2026-10-27-fas-9-typer.md, step 9.0)
// ---------------------------------------------------------------------------

// AcDbEvalVariant: the value of an evaluation expression and of an associative value parameter,
// as AutoCAD writes it — a type tag (70) followed by the value in the group that tag names. AutoCAD's
// files have two tags: 40 (a double, in 140 in AcDbEvalExpr) and 70 (an int16, in a second 70). A record
// with another tag is not typed (no AutoCAD file has one): it stays as it was read, with a warning.
struct EvalVariant {
    int tag = 40;                       // 70: 40 real, 70 int16 (the tags AutoCAD's files have)
    double real = 0.0;                  // tag 40
    int32_t integer = 0;                // tag 70
};

// The value part of AcDbEvalExpr that is present only on some nodes: the expression text (1) and its
// current value
struct EvalExprValue {
    std::string expression;             // 1
    EvalVariant value;                  // 70 and its value group
};

// AcDbEvalExpr, the first subclass of every dynamic block element and of the solid history nodes: the
// graph node's id (an integer in the ACAD_EVALUATION_GRAPH, not a handle), the class version and,
// on some nodes, an expression with its value
struct EvalExprData {
    int32_t node_id = 0;                // 90
    int32_t version = 0;                // 98
    int32_t maintenance = 0;            // 99
    std::optional<EvalExprValue> expression;    // 1, 70, value: present on some nodes only
};

// ---------------------------------------------------------------------------
// Tables (phase 9, step 9.1): ACAD_TABLE (2000+) with TABLESTYLE, CELLSTYLEMAP, TABLECONTENT and
// TABLEGEOMETRY. The group codes are AutoCAD's: its own files (doc/facit/acad_*_dxf_tables.dxf and the
// corpus of build/test-data) decide, the DXF reference (TABLE, TABLESTYLE group codes) where they show nothing.
// ---------------------------------------------------------------------------

// AcValue's data type (90): which value groups follow
enum class TableValueType : int32_t {
    Unset = 0,          // no value
    Long = 1,           // 91
    Double = 2,         // 140
    String = 4,         // 1
    Date = 8,           // 92 + 310: a Windows SYSTEMTIME (16 bytes, TableDate); 2000 and 2004 write eight bytes
                        // (a 64-bit count of seconds since 1970, or -1), held as the bytes
    Point2D = 16,       // 11, 21
    Point3D = 32,       // 11, 21, 31
    ObjectId = 64,      // 330
    Buffer = 128,       // 92 + 310
    ResBuf = 256,       // (no AutoCAD file has a value: not typed)
    General = 512       // no value in any AutoCAD file
};

// A date of an AcValue as AutoCAD writes it: the 16 bytes of a Windows SYSTEMTIME, eight 16-bit values
struct TableDate {
    uint16_t year = 0;
    uint16_t month = 0;
    uint16_t day_of_week = 0;
    uint16_t day = 0;
    uint16_t hour = 0;
    uint16_t minute = 0;
    uint16_t second = 0;
    uint16_t millisecond = 0;

    bool operator==(const TableDate& o) const {
        return year == o.year && month == o.month && day_of_week == o.day_of_week && day == o.day &&
               hour == o.hour && minute == o.minute && second == o.second && millisecond == o.millisecond;
    }
};

// A handle held by an AcValue (type ObjectId, 330)
struct TableHandle {
    std::string handle;
    bool operator==(const TableHandle& o) const { return handle == o.handle; }
};

// AcValue: a typed value with its unit type, format string and formatted text. DXF 2007 and later write
// `93 flags, 90 type, <value>, 94 unit, 300 format, 302 text, 304 ACVALUE_END`; 2000 and 2004 only
// `90 type, <value>`. The type says which value groups AutoCAD wrote; a type with no value there (an empty
// cell: Unset, flags 7) has no value. The empty string and no value are two things.
struct TableValue {
    TableValueType type = TableValueType::Unset;
    uint32_t flags = 7;                 // 93: the bits AutoCAD sets (7 unset, 6 a value, 4 a value with a format)
    std::variant<std::monostate, int32_t, double, std::string, TableDate, Point2, Point3, TableHandle,
                 std::vector<uint8_t>> value;
    int32_t unit_type = 0;              // 94
    std::string format;                 // 300: the format string ("%lu6%ds[\,]")
    std::string text;                   // 302: the formatted text

    bool has_value() const { return !std::holds_alternative<std::monostate>(value); }

    // An empty cell
    static TableValue unset() { return TableValue{}; }
    // A text value (the text is the string and its formatted text)
    static TableValue from_text(const std::string& text);
    // A number with its format (the formatted text as AutoCAD would show it is given by the caller)
    static TableValue from_double(double v, const std::string& format = std::string(),
                                  const std::string& text = std::string());
    static TableValue from_long(int32_t v, const std::string& format = std::string(),
                                const std::string& text = std::string());

    // Equal in every member (a point is equal by its coordinates)
    bool operator==(const TableValue& o) const;
};

// The six border positions of a table style and of a cell format (AcDb::GridLineType order of the
// TABLESTYLE groups 274 + i, 284 + i, 64 + i)
enum class TableGridEdge : int {
    HorizontalTop = 0,
    HorizontalInside = 1,
    HorizontalBottom = 2,
    VerticalLeft = 3,
    VerticalInside = 4,
    VerticalRight = 5
};

// ---- ACAD_TABLE (the entity) ----

// One border of a cell the table overrides: lineweight (275, 276, 278, 279), color (65, 66, 68, 69) and
// visibility (285, 286, 288, 289), each only when the cell overrides it
struct TableEdgeOverride {
    std::optional<int16_t> lineweight;
    std::optional<int16_t> color;
    std::optional<bool> visible;

    bool any() const { return lineweight || color || visible; }
    bool operator==(const TableEdgeOverride& o) const {
        return lineweight == o.lineweight && color == o.color && visible == o.visible;
    }
};

// What a cell overrides of its table style (the groups after the cell's rotation in 2007 and later,
// after its content before). Each is present only when the cell overrides it.
struct TableCellOverrides {
    std::optional<std::string> text_style;      // 7
    std::optional<double> text_height;          // 140
    std::optional<int16_t> alignment;           // 170
    std::optional<Color> content_color;         // 64
    std::optional<Color> fill_color;            // 63 (and 421)
    std::optional<bool> fill_enabled;           // 283
    TableEdgeOverride top;                      // 279, 69, 289
    TableEdgeOverride right;                    // 275, 65, 285
    TableEdgeOverride bottom;                   // 276, 66, 286
    TableEdgeOverride left;                     // 278, 68, 288

    bool operator==(const TableCellOverrides& o) const {
        return text_style == o.text_style && text_height == o.text_height && alignment == o.alignment &&
               content_color.has_value() == o.content_color.has_value() &&
               (!content_color || (content_color->aci == o.content_color->aci && content_color->rgb == o.content_color->rgb)) &&
               fill_color.has_value() == o.fill_color.has_value() &&
               (!fill_color || (fill_color->aci == o.fill_color->aci && fill_color->rgb == o.fill_color->rgb)) &&
               fill_enabled == o.fill_enabled && top == o.top && right == o.right && bottom == o.bottom &&
               left == o.left;
    }
};

// A block attribute's value in a block cell: its ATTDEF in the block (331) and the text (300)
struct TableAttributeValue {
    std::string attdef_handle;
    std::string text;
    bool operator==(const TableAttributeValue& o) const { return attdef_handle == o.attdef_handle && text == o.text; }
};

enum class TableCellType : int16_t { Text = 1, Block = 2 };

// One cell of the ACAD_TABLE entity (the groups 171 … 178, 145, the content, the overrides)
struct TableCell {
    TableCellType type = TableCellType::Text;   // 171
    int16_t flags = 0;                  // 172
    int16_t merged = 0;                 // 173: 1 for a cell that is part of another's merged range
    bool auto_fit = false;              // 174
    int16_t merge_columns = 1;          // 175: the columns of the merged range (on its first cell)
    int16_t merge_rows = 1;             // 176: the rows of the merged range
    int32_t override_flags = 0;         // 177 (before 2007) or 91: AutoCAD's override bits; the bits of the
                                        // groups below that HDxf knows are kept in step when written
    int16_t virtual_edge = 0;           // 178
    double rotation = 0.0;              // 145
    // The text cell's content: an AcValue from 2007; before, the text only (its formatted text)
    TableValue value;
    std::string field_handle;           // 344: the FIELD the text is (a text cell with a field)
    // The block cell's content
    std::string block_handle;           // 340: the BLOCK_RECORD
    double block_scale = 1.0;           // 144
    std::vector<TableAttributeValue> attributes;    // 179, 331, 300
    int32_t extended_flags = 0;         // 92 (2007 and later)
    TableCellOverrides overrides;
};

// A row type's overrides at the table (one entry per cell type, in the order data, title, header)
struct TableRowTypeOverride {
    std::optional<double> text_height;          // 140
    bool operator==(const TableRowTypeOverride& o) const { return text_height == o.text_height; }
};

// What the table overrides of its style: AutoCAD's override flag (93) and the groups it says follow
struct TableOverrides {
    uint32_t flags = 0;                 // 93: the bits of the groups below are kept in step when written
    uint32_t border_color_flags = 0;    // 94
    uint32_t border_lineweight_flags = 0;   // 95
    uint32_t border_visibility_flags = 0;   // 96
    std::optional<int16_t> flow_direction;      // 70
    std::optional<double> horizontal_margin;    // 40
    std::optional<double> vertical_margin;      // 41
    std::optional<bool> title_suppressed;       // 280
    std::optional<bool> header_suppressed;      // 281
    TableRowTypeOverride rows[3];               // data, title, header: 140

    bool operator==(const TableOverrides& o) const {
        return flags == o.flags && border_color_flags == o.border_color_flags &&
               border_lineweight_flags == o.border_lineweight_flags &&
               border_visibility_flags == o.border_visibility_flags && flow_direction == o.flow_direction &&
               horizontal_margin == o.horizontal_margin && vertical_margin == o.vertical_margin &&
               title_suppressed == o.title_suppressed && header_suppressed == o.header_suppressed &&
               rows[0] == o.rows[0] && rows[1] == o.rows[1] && rows[2] == o.rows[2];
    }
};

// ACAD_TABLE (AcDbBlockReference + AcDbTable): the table as the entity holds it. The anonymous block *T
// (block_name, block_record_handle) is its drawing; the TABLECONTENT and TABLEGEOMETRY of its round-trip
// dictionary (extension dictionary ACAD_XREC_ROUNDTRIP, Drawing::table_content()) hold the rest of it. The
// number of rows (91) and columns (92) are row_heights.size() and column_widths.size(); `cells` runs row by
// row and has rows * columns of them.
struct TableData {
    std::string block_name;             // 2: "*T1"
    Point3 insertion;                   // 10
    std::optional<int16_t> data_version;    // 280 (2010 and later: 0)
    std::string style_handle;           // 342: the TABLESTYLE
    std::string block_record_handle;    // 343: the BLOCK_RECORD of the *T block
    Point3 horizontal_direction = {1.0, 0.0, 0.0};  // 11
    uint32_t flags = 0;                 // 90
    TableOverrides overrides;           // 93 … 96, 70, 40, 41, 280, 281, 140
    std::vector<double> row_heights;    // 141
    std::vector<double> column_widths;  // 142
    std::vector<TableCell> cells;
    // The proxy graphics AutoCAD drew the table with (92 before 2010, 160 from 2010, then 310): the *T
    // block's drawing in AcGi's form; opaque (bytes as written)
    std::vector<uint8_t> proxy_graphics;
    // --- 9.5 table layout --- HDxf's own bookkeeping (not in the file): a hash of what the table's layout reads (the
    // table, its style, the blocks and fields it draws), as of reading the file or the last layout, and of where the
    // table was then. A table whose hash is the same is not laid out again; 0 = never laid out (a table made in memory)
    uint64_t layout_stamp = 0;
    uint64_t placement_stamp = 0;

    size_t rows() const { return row_heights.size(); }
    size_t columns() const { return column_widths.size(); }
    TableCell* cell(size_t row, size_t column) {
        return row < rows() && column < columns() ? &cells[row * columns() + column] : nullptr;
    }
    const TableCell* cell(size_t row, size_t column) const {
        return row < rows() && column < columns() ? &cells[row * columns() + column] : nullptr;
    }
};

// ---- TABLESTYLE ----

struct TableStyleBorder {
    int16_t lineweight = -2;            // 274 + i (-2: ByBlock)
    bool visible = true;                // 284 + i
    int16_t color = 0;                  // 64 + i (0: ByBlock)
    bool operator==(const TableStyleBorder& o) const {
        return lineweight == o.lineweight && visible == o.visible && color == o.color;
    }
};

// One row type of a table style: the data rows, the title row, the header row (in the file's order)
struct TableStyleRow {
    std::optional<std::string> text_style;      // 7 (the text style's name)
    double text_height = 0.0;           // 140
    int16_t alignment = 0;              // 170
    int16_t text_color = 0;             // 62
    int16_t fill_color = 7;             // 63
    bool fill_enabled = false;          // 283
    std::optional<int32_t> data_type;   // 90 (2007 and later)
    std::optional<int32_t> unit_type;   // 91
    std::optional<std::string> format;  // 1
    TableStyleBorder borders[6];        // by TableGridEdge: 274 + i, 284 + i, 64 + i

    bool operator==(const TableStyleRow& o) const {
        if (text_style != o.text_style || text_height != o.text_height || alignment != o.alignment ||
            text_color != o.text_color || fill_color != o.fill_color || fill_enabled != o.fill_enabled ||
            data_type != o.data_type || unit_type != o.unit_type || format != o.format) {
            return false;
        }
        for (int i = 0; i < 6; ++i) if (!(borders[i] == o.borders[i])) return false;
        return true;
    }
};

// TABLESTYLE (AcDbTableStyle); its name is its entry in ACAD_TABLESTYLE. The CELLSTYLEMAP of its extension
// dictionary (ACAD_ROUNDTRIP_2008_TABLESTYLE_CELLSTYLEMAP) holds the cell styles.
struct TableStyleData {
    std::optional<int16_t> class_version;       // 280 first (2010 and later: 0)
    std::optional<std::string> description;     // 3
    int16_t flow_direction = 0;         // 70: 0 down, 1 up
    int16_t flags = 0;                  // 71
    double horizontal_margin = 1.5;     // 40
    double vertical_margin = 1.5;       // 41
    bool title_suppressed = false;      // 280
    bool header_suppressed = false;     // 281
    TableStyleRow rows[3];              // data, title, header
};

// ---- Format parts shared by CELLSTYLEMAP and TABLECONTENT ----

// CONTENTFORMAT: how a cell's content looks
struct TableContentFormat {
    int32_t property_flags = 0;         // 90: the properties overridden (AcDb::CellProperty bits)
    int32_t extended_flags = 0;         // 91
    int32_t data_type = 512;            // 92 (the AcValue type; 512 general)
    int32_t unit_type = 0;              // 93
    std::string format_string;          // 300
    double rotation = 0.0;              // 40
    double scale = 1.0;                 // 140
    int32_t alignment = 1;              // 94
    Color color = Color::from_aci(0);   // 62 (and 420)
    std::string text_style_handle = "0";    // 340
    double text_height = 0.18;          // 144
};

// CELLMARGIN: six values of AutoCAD's own files; in these top, left, bottom, right are the table style's
// vertical and horizontal margins (40, 41); what the last two are is not established
struct TableCellMargins {
    double top = 0.0;
    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double spacing_1 = 0.18;
    double spacing_2 = 0.18;
};

// GRIDFORMAT: one border
struct TableGridFormat {
    int32_t edge = 0;                   // 95: AcDb::GridProperty edge (1 top, 2 horizontal inside, 4 bottom, 8 left,
                                        //     16 vertical inside, 32 right)
    int32_t property_flags = 0;         // 90: the properties overridden (63: all)
    int32_t kind = 0;                   // 91
    Color color = Color::from_aci(0);   // 62
    int32_t lineweight = -2;            // 92
    std::string linetype_handle = "0";  // 340
    int32_t visibility = 0;             // 93
    double double_line_spacing = 0.0;   // 40
};

// TABLEFORMAT: the format of a cell, row, column, table or cell style
struct TableFormatData {
    int32_t kind = 0;                   // 90: 1 cell, 2 row, 3 column, 4 table, 5 cell style
    bool has_format = false;            // 170: the format below is present
    int32_t property_flags = 0;         // 91 (AcDb::CellProperty bits)
    int32_t merge_flags = 0;            // 92
    Color background = Color::from_aci(257);    // 62 (and 420, 430)
    int32_t flags = 1;                  // 93
    TableContentFormat content;         // 300 CONTENTFORMAT
    bool has_margins = false;           // 171
    TableCellMargins margins;           // 301 MARGIN
    std::vector<TableGridFormat> grids; // 94 counts them
};

// ---- CELLSTYLEMAP ----

struct TableCellStyle {
    TableFormatData format;             // 300 CELLSTYLE, TABLEFORMAT
    int32_t id = 0;                     // 90
    int32_t kind = 0;                   // 91
    std::string name;                   // 300: "_TITLE", "_HEADER", "_DATA" or a style of the user's
};

struct CellStyleMapData {
    std::vector<TableCellStyle> styles; // 90 counts them
};

// ---- TABLECONTENT ----

// A custom data value of a cell, row or column: its name and AcValue (AutoCAD keeps the cell's checksum as
// ACAD_ROUNDTRIP_2008_CELL_CHECKSUM in its custom data)
struct TableCustomData {
    std::string name;                   // 300
    TableValue value;                   // 301 DATAMAP_VALUE and the value
};

enum class TableContentKind : int32_t { Value = 1, Field = 2, Block = 4 };

struct TableCellContent {
    TableContentKind kind = TableContentKind::Value;   // 90
    TableValue value;                   // 300 VALUE and the AcValue (a Value content)
    std::string handle;                 // 340: the FIELD of a Field content (owned by the TABLECONTENT) or the
                                        //      BLOCK_RECORD of a Block content
    std::optional<TableContentFormat> format;       // 170, 300 CONTENTFORMAT: the content's own format
};

// TABLECELL's attachments: a cell that holds a block of its own (an anonymous BLOCK_RECORD): 91, 40, 41, 330, 92
struct TableCellAttachment {
    int32_t kind = 0;                   // 91
    double width = 0.0;                 // 40
    double height = 0.0;                // 41
    std::string block_handle;           // 330: the BLOCK_RECORD
    int32_t flags = 0;                  // 92
};

struct TableContentCell {
    int32_t state = 0;                  // 90
    std::string name;                   // 300
    int32_t flags = 0;                  // 91
    std::vector<TableCustomData> custom_data;       // 301 CUSTOMDATA, DATAMAP
    int32_t extra = 0;                  // 92
    std::vector<TableCellContent> contents;         // 95 counts them
    TableFormatData format;             // FORMATTEDTABLEDATACELL
    int32_t cell_flags = 0;             // TABLECELL 90
    std::vector<TableCellAttachment> attachments;   // TABLECELL 91 counts them
};

struct TableContentColumn {
    std::string name;                   // 300
    int32_t flags = 0;                  // 91
    std::vector<TableCustomData> custom_data;
    TableFormatData format;             // FORMATTEDTABLEDATACOLUMN
    int32_t column_flags = 0;           // TABLECOLUMN 90
    double width = 0.0;                 // 40
};

struct TableContentRow {
    int32_t flags = 0;                  // 91
    std::vector<TableCustomData> custom_data;
    TableFormatData format;             // FORMATTEDTABLEDATAROW
    int32_t row_type = 0;               // TABLEROW 90 (1 the title row, 2 the header, 3 data in AutoCAD's files)
    double height = 0.0;                // 40
    std::vector<TableContentCell> cells;            // 90 counts them
};

// A merged range of cells: top row, left column, bottom row, right column (91, 92, 93, 94)
struct TableMergeRange {
    int32_t top_row = 0;
    int32_t left_column = 0;
    int32_t bottom_row = 0;
    int32_t right_column = 0;
    bool operator==(const TableMergeRange& o) const {
        return top_row == o.top_row && left_column == o.left_column && bottom_row == o.bottom_row &&
               right_column == o.right_column;
    }
};

// TABLECONTENT (AcDbLinkedData, AcDbLinkedTableData, AcDbFormattedTableData, AcDbTableContent): the table's
// content as AutoCAD 2008 keeps it for round trip, owned by the ACAD_XREC_ROUNDTRIP XRECORD of the table's
// extension dictionary. The number of columns and rows is the lists' length.
struct TableContentData {
    std::string name;                   // 1
    std::string description;            // 300
    std::vector<TableContentColumn> columns;        // 90 counts them
    std::vector<TableContentRow> rows;              // 91 counts them
    std::vector<std::string> field_handles;         // 92 counts them, 360: the FIELDs it owns (the fields of
                                                    // the cells with a Field content)
    TableFormatData table_format;       // AcDbFormattedTableData 300 TABLEFORMAT
    std::vector<TableMergeRange> merges;            // 90 counts them
    std::string style_handle;           // AcDbTableContent 340: the TABLESTYLE
};

// ---- TABLEGEOMETRY ----

// The place and size of one content of a cell (the groups 10 … 46, 95)
struct TableContentGeometry {
    Point3 point;                       // 10
    Point3 anchor;                      // 11
    double width = 0.0;                 // 43
    double height = 0.0;                // 44
    double content_width = 0.0;         // 45
    double content_height = 0.0;        // 46
    int32_t flags = 0;                  // 95
};

struct TableCellGeometry {
    int32_t flags = 0;                  // 93 (7: the cell has been laid out; 4 a merged-away cell)
    double width = 0.0;                 // 40
    double height = 0.0;                // 41
    std::string handle;                 // 330: a soft pointer (0 in every AutoCAD file seen), not the owner
    std::vector<TableContentGeometry> contents;     // 94 counts them
};

// TABLEGEOMETRY: the laid out size of every cell (AcDbTableGeometry); `cells` has rows * columns, row by row,
// and its length is 92
struct TableGeometryData {
    int32_t rows = 0;                   // 90
    int32_t columns = 0;                // 91
    std::vector<TableCellGeometry> cells;
};

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

// The enumerations of RAPIDRTRENDERSETTINGS
enum class RenderDurationType { ByTime = 0, ByLevel = 1, UntilSatisfactory = 2 };
enum class RenderAccuracy { Low = 0, Draft = 1, High = 2 };
enum class SamplingFilter { Box = 0, Triangle = 1, Gauss = 2, Mitchell = 3, Lanczos = 4 };

using EntityData = std::variant<
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

    LineData& as_line()                          { return std::get<LineData>(data); }
    const LineData& as_line() const              { return std::get<LineData>(data); }

    CircleData& as_circle()                      { return std::get<CircleData>(data); }
    const CircleData& as_circle() const          { return std::get<CircleData>(data); }

    ArcData& as_arc()                            { return std::get<ArcData>(data); }
    const ArcData& as_arc() const                { return std::get<ArcData>(data); }

    PointData& as_point()                        { return std::get<PointData>(data); }
    const PointData& as_point() const            { return std::get<PointData>(data); }

    TextData& as_text()                          { return std::get<TextData>(data); }
    const TextData& as_text() const              { return std::get<TextData>(data); }

    PolylineData& as_polyline()                  { return std::get<PolylineData>(data); }
    const PolylineData& as_polyline() const      { return std::get<PolylineData>(data); }

    TraceData& as_trace()                        { return std::get<TraceData>(data); }
    const TraceData& as_trace() const            { return std::get<TraceData>(data); }

    SolidData& as_solid()                        { return std::get<SolidData>(data); }
    const SolidData& as_solid() const            { return std::get<SolidData>(data); }

    Face3DData& as_face3d()                      { return std::get<Face3DData>(data); }
    const Face3DData& as_face3d() const          { return std::get<Face3DData>(data); }

    ShapeData& as_shape()                        { return std::get<ShapeData>(data); }
    const ShapeData& as_shape() const            { return std::get<ShapeData>(data); }

    InsertData& as_insert()                      { return std::get<InsertData>(data); }
    const InsertData& as_insert() const          { return std::get<InsertData>(data); }

    AttdefData& as_attdef()                      { return std::get<AttdefData>(data); }
    const AttdefData& as_attdef() const          { return std::get<AttdefData>(data); }

    AttribData& as_attrib()                      { return std::get<AttribData>(data); }
    const AttribData& as_attrib() const          { return std::get<AttribData>(data); }

    DimensionData& as_dimension()                { return std::get<DimensionData>(data); }
    const DimensionData& as_dimension() const    { return std::get<DimensionData>(data); }

    ViewportData& as_viewport()                  { return *std::get<Boxed<ViewportData>>(data); }
    const ViewportData& as_viewport() const      { return *std::get<Boxed<ViewportData>>(data); }

    LwPolylineData& as_lwpolyline()              { return std::get<LwPolylineData>(data); }
    const LwPolylineData& as_lwpolyline() const  { return std::get<LwPolylineData>(data); }

    EllipseData& as_ellipse()                    { return std::get<EllipseData>(data); }
    const EllipseData& as_ellipse() const        { return std::get<EllipseData>(data); }

    XlineData& as_xline()                        { return std::get<XlineData>(data); }
    const XlineData& as_xline() const            { return std::get<XlineData>(data); }

    RayData& as_ray()                            { return std::get<RayData>(data); }
    const RayData& as_ray() const                { return std::get<RayData>(data); }

    MTextData& as_mtext()                        { return std::get<MTextData>(data); }
    const MTextData& as_mtext() const            { return std::get<MTextData>(data); }

    ToleranceData& as_tolerance()                { return std::get<ToleranceData>(data); }
    const ToleranceData& as_tolerance() const    { return std::get<ToleranceData>(data); }

    LeaderData& as_leader()                      { return std::get<LeaderData>(data); }
    const LeaderData& as_leader() const          { return std::get<LeaderData>(data); }

    SplineData& as_spline()                      { return std::get<SplineData>(data); }
    const SplineData& as_spline() const          { return std::get<SplineData>(data); }

    HatchData& as_hatch()                        { return std::get<HatchData>(data); }
    const HatchData& as_hatch() const            { return std::get<HatchData>(data); }

    MlineData& as_mline()                        { return std::get<MlineData>(data); }
    const MlineData& as_mline() const            { return std::get<MlineData>(data); }

    ImageData& as_image()                        { return *std::get<Boxed<ImageData>>(data); }
    const ImageData& as_image() const            { return *std::get<Boxed<ImageData>>(data); }
    // WIPEOUT: an image without a picture (no IMAGEDEF), its boundary in -0.5 … 0.5
    ImageData& as_wipeout()                      { return *std::get<Boxed<ImageData>>(data); }
    const ImageData& as_wipeout() const          { return *std::get<Boxed<ImageData>>(data); }

    // 3DSOLID, REGION and BODY
    AcisData& as_acis()                          { return std::get<AcisData>(data); }
    const AcisData& as_acis() const              { return std::get<AcisData>(data); }

    MLeaderData& as_mleader()                    { return *std::get<Boxed<MLeaderData>>(data); }
    const MLeaderData& as_mleader() const        { return *std::get<Boxed<MLeaderData>>(data); }

    // SURFACE, EXTRUDEDSURFACE, LOFTEDSURFACE, REVOLVEDSURFACE, SWEPTSURFACE, PLANESURFACE
    SurfaceData& as_surface()                    { return *std::get<Boxed<SurfaceData>>(data); }
    const SurfaceData& as_surface() const        { return *std::get<Boxed<SurfaceData>>(data); }
    HelixData& as_helix()                        { return *std::get<Boxed<HelixData>>(data); }
    const HelixData& as_helix() const            { return *std::get<Boxed<HelixData>>(data); }
    MeshData& as_mesh()                          { return *std::get<Boxed<MeshData>>(data); }
    const MeshData& as_mesh() const              { return *std::get<Boxed<MeshData>>(data); }
    LightData& as_light()                        { return *std::get<Boxed<LightData>>(data); }
    const LightData& as_light() const            { return *std::get<Boxed<LightData>>(data); }

    CoordinationModelData& as_coordination_model()
        { return *std::get<Boxed<CoordinationModelData>>(data); }
    const CoordinationModelData& as_coordination_model() const
        { return *std::get<Boxed<CoordinationModelData>>(data); }
    // PDFUNDERLAY, DWFUNDERLAY, DGNUNDERLAY (9.6)
    // --- 9.17 ---
    ArcDimensionData& as_arc_dimension()         { return *std::get<Boxed<ArcDimensionData>>(data); }
    const ArcDimensionData& as_arc_dimension() const { return *std::get<Boxed<ArcDimensionData>>(data); }
    LargeRadialDimensionData& as_large_radial_dimension() { return *std::get<Boxed<LargeRadialDimensionData>>(data); }
    const LargeRadialDimensionData& as_large_radial_dimension() const { return *std::get<Boxed<LargeRadialDimensionData>>(data); }
    // The AcDbDimension part of a DIMENSION, an ARC_DIMENSION or a LARGE_RADIAL_DIMENSION; null for any other entity
    const DimensionData* dimension_part() const {
        if (auto* d = std::get_if<DimensionData>(&data)) return d;
        if (auto* a = std::get_if<Boxed<ArcDimensionData>>(&data)) return &(**a).dimension;
        if (auto* l = std::get_if<Boxed<LargeRadialDimensionData>>(&data)) return &(**l).dimension;
        return nullptr;
    }
    DimensionData* dimension_part() { return const_cast<DimensionData*>(static_cast<const Entity*>(this)->dimension_part()); }
    TcPointData& as_tc_point()                   { return *std::get<Boxed<TcPointData>>(data); }
    const TcPointData& as_tc_point() const       { return *std::get<Boxed<TcPointData>>(data); }
    SectionObjectData& as_section_object()       { return *std::get<Boxed<SectionObjectData>>(data); }
    const SectionObjectData& as_section_object() const { return *std::get<Boxed<SectionObjectData>>(data); }

    UnderlayData& as_underlay()                  { return *std::get<Boxed<UnderlayData>>(data); }
    const UnderlayData& as_underlay() const      { return *std::get<Boxed<UnderlayData>>(data); }

    // OLEFRAME, OLE2FRAME and ACAD_PROXY_ENTITY (9.3)
    OleFrameData& as_ole_frame()                 { return std::get<OleFrameData>(data); }
    const OleFrameData& as_ole_frame() const     { return std::get<OleFrameData>(data); }
    Ole2FrameData& as_ole2_frame()               { return std::get<Ole2FrameData>(data); }
    const Ole2FrameData& as_ole2_frame() const   { return std::get<Ole2FrameData>(data); }
    ProxyEntityData& as_proxy_entity()           { return std::get<ProxyEntityData>(data); }
    const ProxyEntityData& as_proxy_entity() const { return std::get<ProxyEntityData>(data); }
    // ACAD_TABLE
    TableData& as_table()                        { return *std::get<Boxed<TableData>>(data); }
    const TableData& as_table() const            { return *std::get<Boxed<TableData>>(data); }

    UnknownEntityData& as_unknown()              { return std::get<UnknownEntityData>(data); }
    const UnknownEntityData& as_unknown() const  { return std::get<UnknownEntityData>(data); }
};

// ---------------------------------------------------------------------------
// Layer
// ---------------------------------------------------------------------------

struct Layer {
    std::string name = "0";
    Color color = Color::white();  // Layer color is always explicit (not BYLAYER)
    std::string linetype = "CONTINUOUS";
    bool on = true;
    bool frozen = false;
    bool frozen_in_new_vp = false;
    bool locked = false;
    int flags = 0;  // raw standard flags (bits 16, 32, 64)
    bool plot = true;                   // 290 (2000+)
    int lineweight = -3;                // 370 (2000+): 1/100 mm, -3 = default
    std::string plot_style_handle;      // 390 (2000+): empty = the default (Normal)
    // XDATA AcCmTransparency (2000+, as AutoCAD writes it): not in `xdata`. 0 = none
    // (opaque); otherwise Transparency::from_alpha() …
    Transparency transparency;
    std::vector<XdataList> xdata;       // the rest of the layer's XDATA (2000+): also an
                                        //     AcCmTransparency 1071 0 as AutoCAD writes it on
                                        //     some layers (no transparency; replaced by
                                        //     `transparency` when that is set)
    std::string material_handle;        // 347 (2007+): empty = the material Global
    std::string pointer_348;            // 348 (2013+; in no reference): AutoCAD writes 0 on every
                                        //     layer; empty = 0
    std::string xdictionary;            // 360 in 102 {ACAD_XDICTIONARY (2000+): its extension
                                        //     dictionary; empty = none
    std::string handle;
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// Linetype
// ---------------------------------------------------------------------------

// A shape or text in a complex linetype (2000+): belongs to the dash length with the
// same index in Linetype::pattern
struct LinetypeElement {
    int type = 0;                       // 74: 0 simple, bit 1 rotation absolute,
                                        //     bit 2 text, bit 4 shape, bit 8 upright (U= in a
                                        //     .lin file, AutoCAD 2011). Bit 8 is in 74 in
                                        //     2013; before 2013 74 is without it and the
                                        //     values are the round-trip XRECORD
                                        //     ACAD_ROUNDTRIP_2011_LINETYPE in the linetype's
                                        //     extension dictionary, as AutoCAD writes it (B60)
    int shape_number = 0;               // 75
    std::string style;                  // 340: the STYLE by name (the font, or the shape file);
                                        //     a shape file's style, which has no name (70 bit
                                        //     1), by its file (3), e.g. "ltypeshp.shx"
    double scale = 1.0;                 // 46
    double rotation = 0.0;              // 50
    Point2 offset;                      // 44, 45
    std::string text;                   // 9
};

struct Linetype {
    std::string name;
    std::string description;
    int alignment = 65;                 // group 72, always 'A' (65) in R12
    std::vector<double> pattern;        // group 49: dash lengths (positive=dash, negative=gap, 0=dot)
    std::vector<LinetypeElement> elements;  // 2000+: one per pattern entry, or empty if all simple
    double total_length = 0.0;          // group 40
    int flags = 0;
    std::string handle;
    std::string xdictionary;            // 360 in 102 {ACAD_XDICTIONARY (2000+): its extension
                                        //     dictionary; empty = none
    std::vector<XdataList> xdata;       // its XDATA (2000+)
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// TextStyle
// ---------------------------------------------------------------------------

struct TextStyle {
    std::string name = "Standard";
    double fixed_height = 0.0;
    double width_factor = 1.0;
    double oblique_angle = 0.0;
    int generation_flags = 0;           // group 71
    double last_height = 2.5;
    std::string font_file = "txt";      // group 3
    std::string big_font;               // group 4
    int flags = 0;
    // XDATA ACAD (2000+, as AutoCAD writes it for a TrueType font): the family name (1000,
    // e.g. "Arial") and its flags (1071: 0x01000000 italic, 0x02000000 bold, the character
    // set in bits 8-15, pitch and family in bits 0-7) — not in `xdata`. AutoCAD finds the
    // font by the family when there is one. Empty = none; R12 has no TrueType fonts, so a
    // TrueType style is written to R12 with "txt", as AutoCAD does
    std::string font_family;
    int font_flags = 0;
    std::vector<XdataList> xdata;       // the rest of the style's XDATA (2000+)
    std::string xdictionary;            // 360 in 102 {ACAD_XDICTIONARY (2000+); empty = none
    std::string handle;
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// DimStyle (dimension style)
// ---------------------------------------------------------------------------

struct DimStyle {
    std::string name = "Standard";
    int flags = 0;
    // String variables
    std::string dimpost;        // 3
    std::string dimapost;       // 4
    std::string dimblk;         // 5 (R12, R13, R14: block name, NOT handle; 2000+: 342)
    std::string dimblk1;        // 6
    std::string dimblk2;        // 7
    // Double variables (group 40-48)
    double dimscale = 1.0;      // 40
    double dimasz = 2.5;        // 41
    double dimexo = 0.625;      // 42
    double dimdli = 3.75;       // 43
    double dimexe = 0.625;      // 44
    double dimrnd = 0.0;        // 45
    double dimdle = 0.0;        // 46
    double dimtp = 0.0;         // 47
    double dimtm = 0.0;         // 48
    // Double variables (group 140-147)
    double dimtxt = 2.5;        // 140
    double dimcen = 2.5;        // 141
    double dimtsz = 0.0;        // 142
    double dimaltf = 25.4;      // 143
    double dimlfac = 1.0;       // 144
    double dimtvp = 0.0;        // 145
    double dimtfac = 1.0;       // 146
    double dimgap = 0.625;      // 147
    // Int variables (group 71-78)
    int dimtol = 0;             // 71
    int dimlim = 0;             // 72
    int dimtih = 0;             // 73
    int dimtoh = 0;             // 74
    int dimse1 = 0;             // 75
    int dimse2 = 0;             // 76
    int dimtad = 1;             // 77
    int dimzin = 0;             // 78
    // Int variables (group 170-178)
    int dimalt = 0;             // 170
    int dimaltd = 2;            // 171
    int dimtofl = 0;            // 172
    int dimsah = 0;             // 173
    int dimtix = 0;             // 174
    int dimsoxd = 0;            // 175
    int dimclrd = 0;            // 176
    int dimclre = 0;            // 177
    int dimclrt = 0;            // 178
    // 2000+ (defaults: AutoCAD's). In a 2000+ file a value AutoCAD's default has is not
    // written; the reader starts from AutoCAD's defaults for every variable.
    double dimaltrnd = 0.0;     // 148
    int dimazin = 0;            // 79
    int dimadec = 0;            // 179
    int dimdec = 4;             // 271
    int dimtdec = 4;            // 272
    int dimaltu = 2;            // 273
    int dimalttd = 2;           // 274
    int dimaunit = 0;           // 275
    int dimfrac = 0;            // 276
    int dimlunit = 2;           // 277
    int dimdsep = '.';          // 278
    int dimtmove = 0;           // 279
    int dimjust = 0;            // 280
    int dimsd1 = 0;             // 281
    int dimsd2 = 0;             // 282
    int dimtolj = 1;            // 283
    int dimtzin = 0;            // 284
    int dimaltz = 0;            // 285
    int dimalttz = 0;           // 286
    int dimupt = 0;             // 288
    int dimatfit = 3;           // 289
    std::string dimtxsty;       // 340: text style name, empty = Standard
    std::string dimldrblk;      // 341: leader arrow block name, empty = closed filled
    int dimlwd = -2;            // 371
    int dimlwe = -2;            // 372
    // AutoCAD 2000/2004 keep these in the style's XDATA (ACAD_DSTYLE_DIMTEXT_FILL 376/377,
    // ACAD_DSTYLE_DIM_LINETYPE 380, …_DIM_EXT1_LINETYPE 381, …_DIM_EXT2_LINETYPE 382,
    // ACAD_DSTYLE_DIMEXT_ENABLED 383, ACAD_DSTYLE_DIMEXT_LENGTH 378,
    // ACAD_DSTYLE_DIMJOGGED_JOGANGLE_SYMBOL 384), 2007+ as group codes
    int dimtfill = 0;           // 69: the text's background: 0 none, 1 the drawing's, 2 dimtfillclr
    Color dimtfillclr = Color::by_block();  // 70
    std::string dimltype;       // 345: the dimension line's linetype, empty = ByBlock
    std::string dimltex1;       // 346: extension line 1's
    std::string dimltex2;       // 347: extension line 2's
    int dimfxlon = 0;           // 290: extension lines of a fixed length
    double dimfxl = 1.0;        // 49: that length, from the dimension line
    double dimjogang = 45.0;    // 50: a jogged radius's jog angle, degrees (XDATA 384: radians)
    int dimtxtdirection = 0;    // 295 (2010+): 1 = the text from right to left
    int dimarcsym = 0;          // 90 (2007+; in no reference, as $DIMARCSYM): an arc length's
                                //     symbol, 0 before the text, 1 above it, 2 none
    // The older form (DXF R13 and R14, plan phase 7, B91). DIMUNIT (270) and DIMFIT (287) are one
    // variable each where 2000 has DIMLUNIT + DIMFRAC and DIMATFIT + DIMTMOVE (Autodesk: "obsolete,
    // now use ..."). A style read from an R13/R14 file has them as read, and dimlunit/dimfrac and
    // dimatfit/dimtmove set from them: DIMUNIT 1-3 are DIMLUNIT 1-3, 4 and 5 are DIMLUNIT 4 and 5
    // stacked (DIMFRAC 0), 6 and 7 the same not stacked (DIMFRAC 2), 8 is DIMLUNIT 6; DIMFIT 0-3 are
    // DIMATFIT 0-3 (DIMTMOVE 0), 4 and 5 are DIMATFIT 3 with DIMTMOVE 1 and 2. R13/R14 writes the
    // value as read while it still says what dimlunit/dimfrac (dimatfit/dimtmove) say — a value
    // outside those ranges, which has no 2000 counterpart, stays as it was — and derives it from
    // them otherwise; a combination without an older value (DIMFRAC 1 on DIMLUNIT 4 or 5, DIMTMOVE
    // 1 or 2 without DIMATFIT 3) is an error, "<variable> requires DXF 2000 or later"
    std::optional<int> dimunit;     // 270 (R13, R14); empty = the file had none, or not an older file
    std::optional<int> dimfit;      // 287 (R13, R14)
    std::vector<XdataList> xdata;   // the rest of the style's XDATA (2000+)
    // 330 in 102 {ACAD_REACTORS (2000+): AutoCAD's dimensions of the style, as read (not
    // every dimension that uses it); one to a record that is gone is left out (B19)
    std::vector<std::string> reactors;
    std::string handle;
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// View (named view)
// ---------------------------------------------------------------------------

struct View {
    std::string name;
    int flags = 0;
    double height = 0.0;           // 40
    double width = 0.0;            // 41
    Point2 center;                 // 10, 20
    Point3 direction;              // 11, 21, 31
    Point3 target;                 // 12, 22, 32
    double lens_length = 0.0;      // 42
    double front_clip = 0.0;       // 43
    double back_clip = 0.0;        // 44
    double twist = 0.0;            // 50
    int view_mode = 0;             // 71
    // 2000+
    int render_mode = 0;           // 281
    bool has_ucs = false;          // 72: the UCS below is saved with the view
    Point3 ucs_origin;             // 110, 120, 130
    Point3 ucs_x_axis = {1.0, 0.0, 0.0};  // 111, 121, 131
    Point3 ucs_y_axis = {0.0, 1.0, 0.0};  // 112, 122, 132
    int ucs_ortho_type = 0;        // 79
    double elevation = 0.0;        // 146
    std::string ucs;               // 345: a named UCS, empty = the one above
    std::string base_ucs;          // 346: UCS name, empty = WORLD
    // 2007+
    bool camera_plottable = false;      // 73
    std::string background_handle;      // 332
    std::string live_section_handle;    // 334
    std::string visual_style_handle;    // 348: empty = none
    std::string sun_handle;             // 361
    // 2013+: the view's preview image (a PNG in ACDSDATA, as LayoutData::thumbnail)
    std::vector<uint8_t> thumbnail;
    std::string xdictionary;       // 360 in 102 {ACAD_XDICTIONARY (2000+); empty = none
    std::string handle;
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// UCS (user coordinate system)
// ---------------------------------------------------------------------------

// The origin a UCS has in one orthographic view of it (2000+)
struct UcsOrthographic {
    int type = 1;                  // 71: 1 top, 2 bottom, 3 front, 4 back, 5 left, 6 right
    Point3 origin;                 // 13, 23, 33
};

struct Ucs {
    std::string name;
    int flags = 0;
    Point3 origin;                 // 10, 20, 30
    Point3 x_dir = {1.0, 0.0, 0.0};  // 11, 21, 31
    Point3 y_dir = {0.0, 1.0, 0.0};  // 12, 22, 32
    int ortho_type = 0;            // 79 (2000+)
    double elevation = 0.0;        // 146 (2000+)
    std::string base_ucs;          // 346 (2000+): UCS name, empty = WORLD
    std::vector<UcsOrthographic> orthographic;  // 71 + 13 (2000+)
    std::string handle;
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// Vport (viewport configuration)
// ---------------------------------------------------------------------------

struct Vport {
    std::string name = "*ACTIVE";
    int flags = 0;
    Point2 lower_left;              // 10, 20
    Point2 upper_right = {1.0, 1.0}; // 11, 21
    Point2 center;                  // 12, 22
    Point2 snap_base;               // 13, 23
    Point2 snap_spacing = {10.0, 10.0}; // 14, 24
    Point2 grid_spacing = {10.0, 10.0}; // 15, 25
    Point3 view_dir = {0.0, 0.0, 1.0}; // 16, 26, 36
    Point3 target;                  // 17, 27, 37
    double view_height = 10.0;     // 40
    double aspect_ratio = 1.0;     // 41
    double lens_length = 50.0;     // 42
    double front_clip = 0.0;       // 43
    double back_clip = 0.0;        // 44
    double snap_angle = 0.0;       // 50
    double twist_angle = 0.0;      // 51
    int view_mode = 0;             // 71
    int circle_zoom = 100;         // 72
    int fast_zoom = 1;             // 73
    int ucsicon = 3;               // 74
    int snap_on = 0;               // 75
    int grid_on = 0;               // 76
    int snap_style = 0;            // 77
    int snap_isopair = 0;          // 78
    int status = 0;                // 68
    int id = 0;                    // 69
    // 2000+
    int render_mode = 0;           // 281
    bool ucs_per_viewport = true;  // 65: the UCS below is saved with the viewport
    Point3 ucs_origin;             // 110, 120, 130
    Point3 ucs_x_axis = {1.0, 0.0, 0.0};  // 111, 121, 131
    Point3 ucs_y_axis = {0.0, 1.0, 0.0};  // 112, 122, 132
    int ucs_ortho_type = 0;        // 79
    double elevation = 0.0;        // 146
    std::string ucs;               // 345: a named UCS, empty = the one above
    std::string base_ucs;          // 346: UCS name, empty = WORLD
    // 2007+, as AutoCAD writes a new drawing's *Active
    std::string visual_style_handle;    // 348: the VISUALSTYLE, empty = 2dWireframe
    int grid_flags = 3;                 // 60: not in the reference; AutoCAD writes 3
    int grid_major = 5;                 // 61: a major grid line every n lines
    bool default_lighting = true;       // 292
    int default_lighting_type = 1;      // 282: 0 one distant light, 1 two
    double brightness = 0.0;            // 141
    double contrast = 0.0;              // 142
    Color ambient_color = Color::from_rgb(51, 51, 51);  // 63, 421, 431 (ACI 250)
    std::string background_handle;      // 332
    std::string shade_plot_handle;      // 333
    std::string sun_handle;             // 361
    std::string xdictionary;            // 360 in 102 {ACAD_XDICTIONARY (2000+); empty = none
    std::vector<XdataList> xdata;       // its XDATA (2000+; AutoCAD's ACAD_NAV_VCDISPLAY …)
    std::string handle;
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// AppId (application identification for XDATA)
// ---------------------------------------------------------------------------

struct AppId {
    std::string name;
    int flags = 0;
    std::string handle;
    RawRecord raw;  // the table entry as read (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// HeaderVar (header variable)
// ---------------------------------------------------------------------------

struct HeaderVar {
    std::string name;
    std::vector<std::pair<int, std::string>> values;
};

// ---------------------------------------------------------------------------
// BlockRecord (BLOCK_RECORD table entry, R13+)
// ---------------------------------------------------------------------------
//
// Every block has one in R13+ files; its handle is the owner (330) of the block's BLOCK,
// ENDBLK and entities. Empty for R12 files and blocks built in memory.

struct BlockRecord {
    std::string handle;                 // 5
    std::string layout_handle;          // 340: the LAYOUT of *Model_Space / *Paper_Space*
    std::vector<std::string> block_refs; // 331 in 102 {BLKREFS
    std::string xdictionary;            // 360 in 102 {ACAD_XDICTIONARY: its extension dictionary
    int insert_units = 0;               // 70 (2007+)
    int explodable = 1;                 // 280 (2007+)
    int scalable = 0;                   // 281 (2007+)
    // 310: the block's preview image (a BITMAPINFOHEADER and its pixels, as DesignCenter
    // shows it), as read; HDxf does not make one. Empty = none
    std::vector<uint8_t> preview;
    std::vector<XdataList> xdata;
    RawRecord raw;                      // the BLOCK_RECORD record as read
};

// ---------------------------------------------------------------------------
// Block (block definition)
// ---------------------------------------------------------------------------

struct Block {
    std::string name;
    Point3 base_point;
    int flags = 0;
    std::string layer = "0";
    int paper_space = 0;                // 67 on BLOCK and ENDBLK: AutoCAD's R12 file has 1 on $PAPER_SPACE
    int xref_unloaded = 0;              // 71 (2000+): 1 for an xref that is unloaded, 0 (no 71) otherwise
    std::string xref_path;
    std::string description;            // 4 (2000+): the block's description
    // The BLOCK's and ENDBLK's colors (62, 420 and 430 from 2004; in R12 and R13/R14 62 as well), each
    // its own record's and absent where the record has none (AutoCAD writes none for ByLayer; the *D
    // block of a coloured dimension has the same color on both, the R14 file 7H12R10F_schneider has
    // one on BLOCK and none on ENDBLK). A color of one is never given to the other, nor to the block's
    // entities, nor does it change the block's geometry or extents (plan 9.2, F9.21)
    OptionalBoxed<Color> block_color;
    OptionalBoxed<Color> endblk_color;
    // The BLOCK's and ENDBLK's other entity properties (2000+, AcDbEntity), which AutoCAD writes
    // alike on both: linetype (6, empty = ByLayer), lineweight (370), linetype scale (48),
    // visibility (60)
    std::string linetype;
    int lineweight = -1;
    double linetype_scale = 1.0;
    bool visible = true;
    std::vector<XdataList> xdata;       // the BLOCK's XDATA (2000+)
    std::vector<Entity> entities;
    std::string handle;
    std::string endblk_handle;
    BlockRecord record;                 // its BLOCK_RECORD (R13+)
    RawRecord raw;                      // the BLOCK record (DXF 2000+ files)
    RawRecord endblk_raw;               // the ENDBLK record (DXF 2000+ files)
};

// ---------------------------------------------------------------------------
// Objects (OBJECTS section, R13+)
// ---------------------------------------------------------------------------
//
// One Object per record in the OBJECTS section, in file order. The object types HDxf
// interprets have typed data; any other is UnknownObjectData with its groups. Handles
// that point to other records are kept as the hex strings of the file.

enum class ObjectType {
    Dictionary,             // DICTIONARY
    DictionaryWithDefault,  // ACDBDICTIONARYWDFLT
    XRecord,                // XRECORD
    Group,                  // GROUP
    Layout,                 // LAYOUT
    PlotSettings,           // PLOTSETTINGS
    DictionaryVar,          // DICTIONARYVAR
    Placeholder,            // ACDBPLACEHOLDER
    MlineStyle,             // MLINESTYLE
    ImageDef,               // IMAGEDEF
    ImageDefReactor,        // IMAGEDEF_REACTOR
    RasterVariables,        // RASTERVARIABLES
    DbColor,                // DBCOLOR (a color book color, in ACAD_COLOR)
    MLeaderStyle,           // MLEADERSTYLE (in ACAD_MLEADERSTYLE)
    GeoData,                // GEODATA (the model space's ACAD_GEOGRAPHICDATA)
    WipeoutVariables,       // WIPEOUTVARIABLES (ACAD_WIPEOUT_VARS)
    Scale,                  // SCALE (an annotation scale, in ACAD_SCALELIST)
    Sun,                    // SUN (a viewport's or the model space's sun)
    DetailViewStyle,        // ACDBDETAILVIEWSTYLE (in ACAD_DETAILVIEWSTYLE; plan phase 6, B77)
    SectionViewStyle,       // ACDBSECTIONVIEWSTYLE (in ACAD_SECTIONVIEWSTYLE)
    NavisworksModelDef,     // NAVISWORKSMODELDEF (in ACAD_BIM_DEFINITIONS; 2013+, B78)
    // --- 9.9 evaluation graph and dynamic block bases ---
    EvaluationGraph,        // ACAD_EVALUATION_GRAPH (in ACAD_ENHANCEDBLOCK; EvaluationGraphData)
    BlockGripLocation,      // BLOCKGRIPLOCATIONCOMPONENT (owned by the graph; BlockGripLocationData)
    BlockLinearParameter,   // BLOCKLINEARPARAMETER (BlockLinearParameterData)
    BlockLinearGrip,        // BLOCKLINEARGRIP (BlockLinearGripData)
    BlockVisibilityParameter, // BLOCKVISIBILITYPARAMETER (BlockVisibilityParameterData)
    BlockVisibilityGrip,    // BLOCKVISIBILITYGRIP (BlockVisibilityGripData)
    BlockScaleAction,       // BLOCKSCALEACTION (BlockScaleActionData)
    BlockRepresentation,    // ACDB_BLOCKREPRESENTATION_DATA (in an INSERT's AcDbBlockRepresentation; BlockRepresentationData)
    DynamicBlockPurgePreventer, // ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION (DynamicBlockPurgePreventerData)
    // --- 9.6 underlay definitions ---
    PdfDefinition,          // PDFDEFINITION (in ACAD_PDFDEFINITIONS; 2000+): UnderlayDefinitionData
    DwfDefinition,          // DWFDEFINITION (in ACAD_DWFDEFINITIONS)
    DgnDefinition,          // DGNDEFINITION (in ACAD_DGNDEFINITIONS)
    RapidRtRenderSettings, // RAPIDRTRENDERSETTINGS (a preset, in ACAD_RENDER_RAPIDRT_SETTINGS; 2013+)
    RapidRtRenderEnvironment, // RAPIDRTRENDERENVIRONMENT (the IBL environment, ACAD_BACKGROUND; 2013+)
    // Draw order, clipping and indexes (phase 9, step 9.4)
    SortentsTable,          // SORTENTSTABLE (a space's draw order, in the block record's ACAD_SORTENTS)
    SpatialFilter,          // SPATIAL_FILTER (an INSERT's or xref's clip, in its ACAD_FILTER/SPATIAL)
    LayerIndex,             // LAYER_INDEX (in a space's ACAD_INDEX)
    SpatialIndex,           // SPATIAL_INDEX (in a space's ACAD_INDEX)
    IdBuffer,               // IDBUFFER (a list of objects: a layer index's members)
    LayerFilter,            // LAYER_FILTER (an INSERT's or xref's layer filter, in its ACAD_FILTER/LAYER)
    // --- 9.3 OLE and proxy classes ---
    ProxyObject,            // ACAD_PROXY_OBJECT (a custom object whose application is not loaded): ProxyObjectData
    TableStyle,             // TABLESTYLE (in ACAD_TABLESTYLE; phase 9, step 9.1)
    CellStyleMap,           // CELLSTYLEMAP (in a TABLESTYLE's extension dictionary)
    TableContent,           // TABLECONTENT (a table's round-trip content)
    TableGeometry,          // TABLEGEOMETRY (a table's round-trip cell sizes)
    // --- 9.8 fields and document data ---
    Field,                  // FIELD (a text's, a table cell's or an attribute's field; AcDbField)
    FieldList,              // FIELDLIST (the drawing's fields, in ACAD_FIELDLIST)
    DataTable,              // DATATABLE (AcDbDataTable; 2004 and later: a table's pre-2007 round-trip cells)
    LegacyDataTable,        // ACDBDATATABLE (the same data, as DXF 2000 names the record)
    ObjectPtr,              // OBJECT_PTR (an AutoCAD SQL Extension link's object: the base record and its DC015 XDATA)
    VbaProject,             // VBA_PROJECT (the drawing's embedded VBA project: AcDbVbaProject)
    // --- 9.7 materials and visual styles ---
    Material,               // MATERIAL (in ACAD_MATERIAL; MaterialData)
    VisualStyle,            // VISUALSTYLE (in ACAD_VISUALSTYLE; VisualStyleData)
    // --- 9.11 render, lights and sun ---
    RenderSettings,         // RENDERSETTINGS (the base of the presets, AcDbRenderSettings; RenderSettingsData)
    MentalRayRenderSettings, // MENTALRAYRENDERSETTINGS (root ACAD_RENDER_ACTIVE_SETTINGS; MentalRayRenderSettingsData)
    RenderEnvironment,      // RENDERENVIRONMENT (root ACAD_RENDER_ENVIRONMENT; RenderEnvironmentData)
    RenderGlobal,           // RENDERGLOBAL (root ACAD_RENDER_GLOBAL; RenderGlobalData)
    LightList,              // LIGHTLIST (root ACAD_LIGHT; LightListData)
    SunStudy,               // SUNSTUDY (AcDbSunStudy; SunStudyData)
    // --- 9.10 ACDBASSOC framework ---
    AssocAction,                            // ACDBASSOCACTION
    AssocNetwork,                           // ACDBASSOCNETWORK
    AssocDependency,                        // ACDBASSOCDEPENDENCY
    AssocGeomDependency,                    // ACDBASSOCGEOMDEPENDENCY
    AssocPersSubentManager,                 // ACDBASSOCPERSSUBENTMANAGER
    PersSubentManager,                      // ACDBPERSSUBENTMANAGER
    AssocPathActionParam,                   // ACDBASSOCPATHACTIONPARAM
    AssocVertexActionParam,                 // ACDBASSOCVERTEXACTIONPARAM
    AssocOsnapPointRefActionParam,          // ACDBASSOCOSNAPPOINTREFACTIONPARAM
    AssocEdgeActionParam,                   // ACDBASSOCEDGEACTIONPARAM
    AssocExtrudedSurfaceActionBody,         // ACDBASSOCEXTRUDEDSURFACEACTIONBODY
    AssocLoftedSurfaceActionBody,           // ACDBASSOCLOFTEDSURFACEACTIONBODY
    AssocRevolvedSurfaceActionBody,         // ACDBASSOCREVOLVEDSURFACEACTIONBODY
    AssocSweptSurfaceActionBody,            // ACDBASSOCSWEPTSURFACEACTIONBODY
    AssocPlaneSurfaceActionBody,            // ACDBASSOCPLANESURFACEACTIONBODY
    AssocAlignedDimActionBody,              // ACDBASSOCALIGNEDDIMACTIONBODY
    // --- 9.14 solid history ---
    ShHistory,              // ACSH_HISTORY_CLASS (a 3DSOLID's history, owned by the solid; ShHistoryData)
    ShBox,                  // ACSH_BOX_CLASS (a node of the history's graph; ShBoxData)
    ShWedge,                // ACSH_WEDGE_CLASS (ShWedgeData)
    ShSphere,               // ACSH_SPHERE_CLASS (ShSphereData)
    ShCylinder,             // ACSH_CYLINDER_CLASS (ShCylinderData)
    ShCone,                 // ACSH_CONE_CLASS (ShConeData)
    ShTorus,                // ACSH_TORUS_CLASS (ShTorusData)
    ShPyramid,              // ACSH_PYRAMID_CLASS (ShPyramidData)
    ShExtrusion,            // ACSH_EXTRUSION_CLASS (ShExtrusionData)
    ShSweep,                // ACSH_SWEEP_CLASS (ShSweepData)
    ShRevolve,              // ACSH_REVOLVE_CLASS (ShRevolveData)
    ShLoft,                 // ACSH_LOFT_CLASS (ShLoftData)
    ShBoolean,              // ACSH_BOOLEAN_CLASS (ShBooleanData)
    ShBrep,                 // ACSH_BREP_CLASS (ShBrepData)
    ShChamfer,              // ACSH_CHAMFER_CLASS (ShChamferData)
    ShFillet,               // ACSH_FILLET_CLASS (ShFilletData)
    // --- 9.15 other ACDBASSOC classes ---
    AssocVariable, // ACDBASSOCVARIABLE
    AssocValueDependency, // ACDBASSOCVALUEDEPENDENCY
    AssocDimDependencyBody, // ASSOCDIMDEPENDENCYBODY
    Assoc2dConstraintGroup, // ACDBASSOC2DCONSTRAINTGROUP
    AssocOffsetSurfaceActionBody, // ACDBASSOCOFFSETSURFACEACTIONBODY
    AssocTrimSurfaceActionBody, // ACDBASSOCTRIMSURFACEACTIONBODY
    AssocPatchSurfaceActionBody, // ACDBASSOCPATCHSURFACEACTIONBODY
    AssocNetworkSurfaceActionBody, // ACDBASSOCNETWORKSURFACEACTIONBODY
    AssocBlendSurfaceActionBody, // ACDBASSOCBLENDSURFACEACTIONBODY
    AssocExtendSurfaceActionBody, // ACDBASSOCEXTENDSURFACEACTIONBODY
    AssocFilletSurfaceActionBody, // ACDBASSOCFILLETSURFACEACTIONBODY
    AssocEdgeFilletActionBody, // ACDBASSOCEDGEFILLETACTIONBODY
    AssocEdgeChamferActionBody, // ACDBASSOCEDGECHAMFERACTIONBODY
    AssocRestoreEntityStateActionBody, // ACDBASSOCRESTOREENTITYSTATEACTIONBODY
    AssocImpliedSurfaceOrSolidActionBody, // ACDBASSOCIMPLIEDSURFACEORSOLIACTIONBODY
    AssocRotatedDimActionBody, // ACDBASSOCROTATEDDIMACTIONBODY
    Assoc3PointAngularDimActionBody, // ACDBASSOC3POINTANGULARDIMACTIONBODY
    AssocOrdinateDimActionBody, // ACDBASSOCORDINATEDIMACTIONBODY
    AssocDiametricDimActionBody, // ACDBASSOCDIAMETRICDIMACTIONBODY
    AssocRadialDimActionBody, // ACDBASSOCRADIALDIMACTIONBODY
    AssocMLeaderActionBody, // ACDBASSOCMLEADERACTIONBODY
    AssocEntityCloneActionBody, // ACDBASSOCENTITYCLONEACTIONBODY
    AssocAsmBodyActionParam, // ACDBASSOCASMBODYACTIONPARAM
    AssocFaceActionParam, // ACDBASSOCFACEACTIONPARAM
    AssocTrimmingPathActionParam, // ACDBASSOCTRIMMINGPATHACTIONPARAM
    AssocTrimmingBodyActionParam, // ACDBASSOCTRIMMINGBODYACTIONPARAM
    AssocCompoundActionParam, // ACDBASSOCCOMPOUNDACTIONPARAM
    AssocObjectActionParam, // ACDBASSOCOBJECTACTIONPARAM
    AssocArrayActionBody, // ACDBASSOCARRAYACTIONBODY
    AssocArrayModifyActionBody, // ACDBASSOCARRAYMODIFYACTIONBODY
    // --- 9.12 sections ---
    SectionManager,         // SECTION_MANAGER (the NOD's ACAD_SECTION_MANAGER; SectionManagerData)
    SectionSettings,        // SECTION_SETTINGS (a section's settings, owned by it; SectionSettingsData)
    // --- 9.17 annotation contexts and DIMASSOC ---
    TextContext,            // ACDB_TEXTOBJECTCONTEXTDATA_CLASS (TextContextData)
    LeaderContext,          // ACDB_LEADEROBJECTCONTEXTDATA_CLASS (LeaderContextData)
    MTextContext,           // ACDB_MTEXTOBJECTCONTEXTDATA_CLASS (MTextContextData)
    MTextAttributeContext,  // ACDB_MTEXTATTRIBUTEOBJECTCONTEXTDATA_CLASS (MTextAttributeContextData)
    MLeaderContext,         // ACDB_MLEADEROBJECTCONTEXTDATA_CLASS (MLeaderContextData)
    BlkRefContext,          // ACDB_BLKREFOBJECTCONTEXTDATA_CLASS (BlkRefContextData)
    AlDimContext,           // ACDB_ALDIMOBJECTCONTEXTDATA_CLASS (AlDimContextData)
    AngDimContext,          // ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS (AngDimContextData)
    DmDimContext,           // ACDB_DMDIMOBJECTCONTEXTDATA_CLASS (DmDimContextData)
    OrdDimContext,          // ACDB_ORDDIMOBJECTCONTEXTDATA_CLASS (OrdDimContextData)
    RadimContext,           // ACDB_RADIMOBJECTCONTEXTDATA_CLASS (RadimContextData)
    RadimLgContext,         // ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS (RadimLgContextData)
    FcfContext,             // ACDB_FCFOBJECTCONTEXTDATA_CLASS (FcfContextData)
    AnnotScaleContext,      // ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS (AnnotScaleContextData)
    HatchScaleContext,      // ACDB_HATCHSCALECONTEXTDATA_CLASS (HatchScaleContextData)
    HatchViewContext,       // ACDB_HATCHVIEWCONTEXTDATA_CLASS (HatchViewContextData)
    DimAssoc,               // DIMASSOC (DimAssocData)
    // --- 9.13 dynamic blocks: the remaining parameters, grips, actions and constraints ---
    BlockAlignmentParameter,             // BLOCKALIGNMENTPARAMETER (BlockAlignmentParameterData)
    BlockAlignmentGrip,                  // BLOCKALIGNMENTGRIP (BlockAlignmentGripData)
    BlockBasepointParameter,             // BLOCKBASEPOINTPARAMETER (BlockBasepointParameterData)
    BlockFlipParameter,                  // BLOCKFLIPPARAMETER (BlockFlipParameterData)
    BlockFlipGrip,                       // BLOCKFLIPGRIP (BlockFlipGripData)
    BlockFlipAction,                     // BLOCKFLIPACTION (BlockFlipActionData)
    BlockLookupParameter,                // BLOCKLOOKUPPARAMETER (BlockLookupParameterData)
    BlockLookupGrip,                     // BLOCKLOOKUPGRIP (BlockLookupGripData)
    BlockLookupAction,                   // BLOCKLOOKUPACTION (BlockLookupActionData)
    BlockMoveAction,                     // BLOCKMOVEACTION (BlockMoveActionData)
    BlockPointParameter,                 // BLOCKPOINTPARAMETER (BlockPointParameterData)
    BlockPolarParameter,                 // BLOCKPOLARPARAMETER (BlockPolarParameterData)
    BlockPolarGrip,                      // BLOCKPOLARGRIP (BlockPolarGripData)
    BlockPolarStretchAction,             // BLOCKPOLARSTRETCHACTION (BlockPolarStretchActionData)
    BlockPropertiesTable,                // BLOCKPROPERTIESTABLE (BlockPropertiesTableData)
    BlockPropertiesTableGrip,            // BLOCKPROPERTIESTABLEGRIP (BlockPropertiesTableGripData)
    BlockRotationParameter,              // BLOCKROTATIONPARAMETER (BlockRotationParameterData)
    BlockRotationGrip,                   // BLOCKROTATIONGRIP (BlockRotationGripData)
    BlockRotateAction,                   // BLOCKROTATEACTION (BlockRotateActionData)
    BlockStretchAction,                  // BLOCKSTRETCHACTION (BlockStretchActionData)
    BlockUserParameter,                  // BLOCKUSERPARAMETER (BlockUserParameterData)
    BlockXYParameter,                    // BLOCKXYPARAMETER (BlockXYParameterData)
    BlockXYGrip,                         // BLOCKXYGRIP (BlockXYGripData)
    BlockArrayAction,                    // BLOCKARRAYACTION (BlockArrayActionData)
    BlockHorizontalConstraintParameter,  // BLOCKHORIZONTALCONSTRAINTPARAMETER (BlockHorizontalConstraintParameterData)
    BlockVerticalConstraintParameter,    // BLOCKVERTICALCONSTRAINTPARAMETER (BlockVerticalConstraintParameterData)
    BlockAlignedConstraintParameter,     // BLOCKALIGNEDCONSTRAINTPARAMETER (BlockAlignedConstraintParameterData)
    BlockLinearConstraintParameter,      // BLOCKLINEARCONSTRAINTPARAMETER (BlockLinearConstraintParameterData)
    BlockRadialConstraintParameter,      // BLOCKRADIALCONSTRAINTPARAMETER (BlockRadialConstraintParameterData)
    BlockParameterDependencyBody,        // BLOCKPARAMDEPENDENCYBODY (BlockParameterDependencyBodyData)
    DynamicBlockProxyNode,               // ACDB_DYNAMICBLOCKPROXYNODE (DynamicBlockProxyNodeData)
    // --- 9.18 Map, TC and ZWM ---
    MapManager,                              // DMMAPMANAGER (MapManagerData)
    MapStyleLibrary,                         // DMSTYLELIBRARY (MapStyleLibraryData)
    MapStyleCategory,                        // DMSTYLECATEGORY (MapStyleCategoryData)
    MapDisplayMap,                           // DMMAP (MapDisplayMapData)
    MapBaseElement,                          // DMBASEELEMENT (MapBaseElementData)
    MapDefaultStyle,                         // DMDEFAULTSTYLE (MapDefaultStyleData)
    MapStyleReference,                       // DMSTYLEREFERENCE (MapStyleReferenceData)
    MapSurrogateStyleSets,                   // DMSURROGATESTYLESETS (MapSurrogateStyleSetsData)
    MapStylizedEntitiesTable,                // DMSTYLIZEDENTITIESTABLE (MapStylizedEntitiesTableData)
    MapMetadata,                             // MAPMETADATAOBJECT (MapMetadataData)
    MapResourceManager,                      // MAPRESOURCEMANAGEROBJECT (MapResourceManagerData)
    TcOptionalData,                          // TCOPTIONALDATA (TcOptionalDataData)
    ZwmBalloonStd,                           // ZWMBALLOONSTD (ZwmBalloonStdData)
    ZwmBomStyleStd,                          // ZWMBOMSTYLESTD (ZwmBomStyleStdData)
    ZwmBreakSymStd,                          // ZWMBREAKSYMSTD (ZwmBreakSymStdData)
    ZwmCenterHoleStd,                        // ZWMCENTERHOLESTD (ZwmCenterHoleStdData)
    ZwmChamferStd,                           // ZWMCHAMFERSTD (ZwmChamferStdData)
    ZwmCircleMarkStd,                        // ZWMCIRCLEMARKSTD (ZwmCircleMarkStdData)
    ZwmColorStd,                             // ZWMCOLORSTD (ZwmColorStdData)
    ZwmDatumIdStd,                           // ZWMDATUMIDSTD (ZwmDatumIdStdData)
    ZwmDatumTargetStd,                       // ZWMDATUMTARGETSTD (ZwmDatumTargetStdData)
    ZwmDetailStd,                            // ZWMDETAILSTD (ZwmDetailStdData)
    ZwmDimStyleStd,                          // ZWMDIMSTYLESTD (ZwmDimStyleStdData)
    ZwmElevSymStd,                           // ZWMELEVSYMSTD (ZwmElevSymStdData)
    ZwmFCFrameStd,                           // ZWMFCFRAMESTD (ZwmFCFrameStdData)
    ZwmFeatureIdStd,                         // ZWMFEATUREIDSTD (ZwmFeatureIdStdData)
    ZwmFrameStyleStd,                        // ZWMFRAMESTYLESTD (ZwmFrameStyleStdData)
    ZwmHoleChartStd,                         // ZWMHOLECHARTSTD (ZwmHoleChartStdData)
    ZwmLayerStd,                             // ZWMLAYERSTD (ZwmLayerStdData)
    ZwmLineTypeStd,                          // ZWMLINETYPESTD (ZwmLineTypeStdData)
    ZwmNoteStd,                              // ZWMNOTESTD (ZwmNoteStdData)
    ZwmPartRefStd,                           // ZWMPARTREFSTD (ZwmPartRefStdData)
    ZwmSectionSymStd,                        // ZWMSECTIONSYMSTD (ZwmSectionSymStdData)
    ZwmStandard,                             // ZWMSTANDARD (ZwmStandardData)
    ZwmSurfSymStd,                           // ZWMSURFSYMSTD (ZwmSurfSymStdData)
    ZwmTaperSlopeStd,                        // ZWMTAPERSLOPESTD (ZwmTaperSlopeStdData)
    ZwmTextStyleStd,                         // ZWMTEXTSTYLESTD (ZwmTextStyleStdData)
    ZwmTitleStyleStd,                        // ZWMTITLESTYLESTD (ZwmTitleStyleStdData)
    ZwmViewDirectionStd,                     // ZWMVIEWDIRECTIONSTD (ZwmViewDirectionStdData)
    ZwmWeldingStd,                           // ZWMWELDINGSTD (ZwmWeldingStdData)
    Unknown
};

// ---------------------------------------------------------------------------
// ACDBASSOC (phase 9, step 9.10): the associative framework's network, actions, dependencies, parameters and action
// bodies, and the two persistent-subentity managers. The groups are AutoCAD's own (doc/facit/acad_<version>_dxf_
// associative.dxf, _surfaces.dxf, the corpus' Surface_2004, example_2007 … 2018 and 3Dlines); the DXF reference has
// nothing for them. Every handle is held as text ("0" is none), a count is the size of its list, and a group whose
// meaning AutoCAD's files do not show has a name of its place (reserved_…, first_…): no role is invented.
//
// The form follows the record's own version groups, as AutoCAD writes it: an action of version 1 (every file up to
// 2010) keeps its parameters in its action body (AssocParamBasedActionBody), version 2 (2013 and later) in the action
// (params, value_params), and an action parameter of version 1 has one more group than version 0.
// ---------------------------------------------------------------------------

// One value of an AssocValueParameter: the group that holds it is 40 (a double), 70 (an int16) or 90 (an int32), the
// type of AutoCAD's AcDbEvalVariant; every value is followed by a soft pointer (330), 0 in every file
struct AssocParameterValue {
    int code = 40;                      // 40 real, 70 integer (int16), 90 integer (int32)
    double real = 0.0;                  // 40
    int32_t integer = 0;                // 70, 90
    std::string pointer = "0";          // 330 after the value
};

// AcDbAssocValueParam: 90 version, 1 name, 90 unit type (0 none, 1 distance, 2 angle), 90 count + the values, 330 dependency
struct AssocValueParameter {
    int32_t version = 0;
    std::string name;                   // 1: "ExtrusionHeight", "RevolveAngle", "Continuity", "Bulge" …
    int32_t unit_type = 0;              // 90
    std::vector<AssocParameterValue> values;
    std::string dependency = "0";       // 330 after the values
};

// A member of a network's list of actions: 360 when the network owns the action, 330 when it only points to it
struct AssocActionMember {
    std::string handle = "0";
    bool owned = true;
};

// The AcDbAssocAction subclass, the base of every ACDBASSOCACTION and ACDBASSOCNETWORK (and of the other action
// classes of step 9.15)
struct AssocActionBase {
    int32_t version = 2;                // 90: 1 until 2010, 2 from 2013 (decides the form below)
    int32_t status = 0;                 // 90
    std::string owning_network = "0";   // 330
    std::string action_body = "0";      // 360
    int32_t action_index = 0;           // 90: the action's number in its network
    int32_t max_dependency_index = 0;   // 90: the highest index of its dependencies
    std::vector<std::string> dependencies;      // 90 count + 360 (hard owner)
    // version 2 (2013 and later) only
    int32_t reserved_after_dependencies = 0;    // 90
    std::vector<std::string> params;            // 90 count + 360: the action's parameters
    int32_t reserved_after_params = 0;          // 90
    std::vector<AssocValueParameter> value_params;     // 90 count + the value parameters
};

struct AssocActionData {                // ACDBASSOCACTION
    AssocActionBase action;
};

struct AssocNetworkData {               // ACDBASSOCNETWORK
    AssocActionBase action;
    int32_t version = 0;                // 90 (AcDbAssocNetwork)
    int32_t max_action_index = 0;       // 90: the highest index of its actions
    std::vector<AssocActionMember> actions;     // 90 count + 360/330
    std::vector<std::string> soft_actions;      // 90 count + 330 (AutoCAD fills it in 2000 … 2013, not in 2007 and 2018)
};

// The AcDbAssocDependency subclass, the base of ACDBASSOCDEPENDENCY, ACDBASSOCGEOMDEPENDENCY (and 9.15's value
// dependency): the object the action reads (is_read) or writes (is_write)
struct AssocDependencyBase {
    int32_t version = 2;                // 90
    int32_t status = 0;                 // 90
    bool is_read = true;                // 290
    bool is_write = true;               // 290
    bool is_object_state_dependent = true;      // 290
    bool is_delegating_to_owning_action = true; // 290
    int32_t order = 0;                  // 90: -10000, -2147483648 (the result) or 0
    std::string dependent_on_object = "0";      // 330: the geometry or object depended on (it gets this as reactor)
    bool depends_on_compound_object = false;    // 290
    std::string previous = "0";         // 330: the previous dependency of the chain
    std::string next = "0";             // 330
    std::string dependency_body = "0";  // 360 (hard owner)
    int32_t index = 0;                  // 90: the dependency's index in its action
};

struct AssocDependencyData {            // ACDBASSOCDEPENDENCY
    AssocDependencyBase dependency;
};

// A persistent subentity id (AcDbAssocPersSubentId): the classes AutoCAD's files have. An unknown class makes the
// record stay as it was read
enum class PersistentSubentityKind : uint8_t {
    SingleEdge,         // AcDbAssocSingleEdgePersSubentId: no data
    Edge,               // AcDbAssocEdgePersSubentId: 90 version, the ids of the edge's start and end vertices
    Simple,             // AcDbAssocSimplePersSubentId: 90 version, 90 subentity type (-1003), 90 subentity index
    AsmBasedEntity      // AcDbAssocAsmBasedEntityPersSubentId: the same three groups
};

struct PersistentSubentityId {
    PersistentSubentityKind kind = PersistentSubentityKind::SingleEdge;
    int32_t version = 0;                // 90 (every kind but SingleEdge)
    int32_t start_vertex = 0;           // Edge: 90
    int32_t end_vertex = 0;             // Edge: 90
    int32_t subentity_type = 0;         // Simple, AsmBasedEntity: 90
    int32_t subentity_index = 0;        // Simple, AsmBasedEntity: 90
};

// AutoCAD's class name of a kind ("AcDbAssocEdgePersSubentId") and the kind of a class name (false: not a class HDxf has)
const char* persistent_subentity_class_name(PersistentSubentityKind kind);
bool persistent_subentity_kind_of(const std::string& class_name, PersistentSubentityKind& kind);

struct AssocGeomDependencyData {        // ACDBASSOCGEOMDEPENDENCY
    AssocDependencyBase dependency;
    int32_t version = 0;                // 90 (AcDbAssocGeomDependency)
    bool has_subentity_id = true;       // 290 before the class name: 1 in every file
    PersistentSubentityId subentity_id; // 1 (the class name) and its groups
    bool trailing_flag = false;         // 290 last
};

// The AcDbAssocActionParam subclass: 90 version, [90 when version 1 or later], 1 name
struct AssocActionParamBase {
    int32_t version = 1;                // 90: 0 until 2010, 1 from 2013
    int32_t extra = 0;                  // 90 (version 1 and later)
    std::string name;                   // 1: "ExtrudeProfile", "CrossSection", "PathCurve", "basePoint" … (may be empty)
};

// AcDbAssocCompoundActionParam: 90 version, 90, 90 count + 360 children (hard owner)
struct AssocCompoundActionParamBase {
    int32_t version = 0;
    int32_t flags = 0;                  // 90
    std::vector<std::string> children;
};

// AcDbAssocSingleDependencyActionParam: 90 version, 330 dependency
struct AssocSingleDependencyActionParamBase {
    int32_t version = 0;
    std::string dependency = "0";
};

struct AssocPathActionParamData {       // ACDBASSOCPATHACTIONPARAM
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
    int32_t version = 0;                // 90 (AcDbAssocPathActionParam)
};

struct AssocVertexActionParamData {     // ACDBASSOCVERTEXACTIONPARAM
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;                // 90 (AcDbAssocVertexActionParam)
    Point3 point;                       // 10
};

// ACDBASSOCOSNAPPOINTREFACTIONPARAM: the subclass names are AutoCAD's own upper-case ones
struct AssocOsnapPointRefActionParamData {
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
    int32_t compound_extra_1 = 0;       // 90 after the children, in the compound subclass: only this class has them
    int32_t compound_extra_2 = 0;       // 90
    std::string compound_pointer = "0"; // 330
    int32_t version = 0;                // 90 (ACDBASSOCOSNAPPOINTREFACTIONPARAM)
    int32_t osnap_type = 0;             // 90
    double parameter = 0.0;             // 40
};

// A curve of an edge parameter (AcGe's curves, as AutoCAD stores them): the curve kind is the 90 before its groups
enum class AssocCurveKind : int32_t {
    CircleArc = 11,
    EllipseArc = 17,
    Line = 23,
    Spline = 42,
    Composite = 47
};

struct AssocLineCurve {                 // 23: two points
    Point3 start;                       // 10
    Point3 offset;                      // 10: the vector from the start to the end
};

struct AssocCircleArcCurve {            // 11
    Point3 center;                      // 10
    Point3 normal;                      // 10
    Point3 reference;                   // 10: the reference vector (the direction of the start angle's zero)
    double radius = 0.0;                // 40
    double start_angle = 0.0;           // 40: radians
    double end_angle = 0.0;             // 40: radians
    std::optional<double> trailing;     // 40: the 2013 form (an action parameter of version 1) has one more
};

struct AssocEllipseArcCurve {           // 17
    Point3 center;                      // 10
    Point3 major_axis;                  // 10
    Point3 minor_axis;                  // 10
    double major_radius = 0.0;          // 40
    double minor_radius = 0.0;          // 40
    double start_angle = 0.0;           // 40: radians
    double end_angle = 0.0;             // 40: radians
};

// An AcArray's lengths as AutoCAD writes them before its items: the logical length is the item count (not kept here),
// then the physical one and the growth length
struct AssocArrayLengths {
    int32_t physical = 0;               // 90
    int32_t growth = 8;                 // 90
};

struct AssocSplineCurve {               // 42
    int16_t first_flag = 0;             // 70
    int16_t second_flag = 0;            // 70
    int32_t degree = 3;                 // 90
    double knot_tolerance = 1e-9;       // 40
    std::vector<double> knots;          // 90 count, 90, 90, then 40 each
    AssocArrayLengths knots_lengths;
    std::vector<double> weights;        // the same for the weights (empty: not rational)
    AssocArrayLengths weights_lengths;
    std::vector<Point3> control_points; // the same for the control points: 10 20 30 each
    AssocArrayLengths control_points_lengths;
};

struct AssocCurve;

struct AssocCompositeCurve {            // 47: 90 count, then each curve with its own 90 kind
    std::vector<AssocCurve> curves;
};

struct AssocCurve {
    std::variant<AssocLineCurve, AssocCircleArcCurve, AssocEllipseArcCurve, AssocSplineCurve, AssocCompositeCurve> shape;
    AssocCurveKind kind() const;
};

struct AssocEdgeActionParamData {       // ACDBASSOCEDGEACTIONPARAM
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;                // 90 (AcDbAssocEdgeActionParam)
    std::string geometry_pointer = "0"; // 330
    AssocCurve curve;                   // 290 1 (the curve is there), 90 kind and its groups
};

// The AcDbAssocActionBody subclass (90 version) and what follows it
struct AssocParamBasedActionBody {      // AcDbAssocParamBasedActionBody: 2004 … 2010 files (action body version 1)
    int32_t version = 0;
    int32_t reserved_before_params = 0; // 90
    std::vector<std::string> params;    // 90 count + 360 (hard owner)
    int32_t reserved_after_params = 0;  // 90
    std::vector<AssocValueParameter> value_params;     // 90 count + the value parameters
};

struct AssocSurfaceActionBody {         // AcDbAssocSurfaceActionBody
    int32_t version = 0;                // 90
    std::string result_dependency = "0";    // 330: the dependency that writes the surface
    bool flag_after_result = false;     // 290
    int32_t step_id = 0;                // 90: the same number as the action's ACAD_ASSOCPERSSUBENTMANAGER XDATA
    bool flag_after_step = false;       // 290
    int16_t value_after_flag = 0;       // 70
};

struct AssocPathBasedSurfaceActionBody {    // AcDbAssocPathBasedSurfaceActionBody
    int32_t version = 0;                // 90
};

// The common head of the six action bodies: AcDbAssocActionBody's 90 version and, when it is 1, the parameters
struct AssocActionBodyHead {
    int32_t version = 2;                // 90: 1 until 2010, 2 from 2013
    std::optional<AssocParamBasedActionBody> param_based;   // present when version is 1
};

// ACDBASSOCEXTRUDEDSURFACEACTIONBODY: AcDbAssocExtrudedSurfaceActionBody 90 version
struct AssocExtrudedSurfaceActionBodyData {
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

struct AssocLoftedSurfaceActionBodyData {   // AcDbAssocLoftedSurfaceActionBody 90 version
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

struct AssocRevolvedSurfaceActionBodyData { // AcDbAssocRevolvedSurfaceActionBody 90 version, 290
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    bool flag = false;                  // 290
};

struct AssocSweptSurfaceActionBodyData {    // AcDbAssocSweptSurfaceActionBody 90 version
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

struct AssocPlaneSurfaceActionBodyData {    // AcDbAssocPlaneSurfaceActionBody 90 version
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

// ACDBASSOCALIGNEDDIMACTIONBODY: the subclass name is AutoCAD's own upper-case one. A 90 and a 330 follow the
// action body's 90 (version 2) or the parameters (version 1)
struct AssocAlignedDimActionBodyData {
    AssocActionBodyHead head;
    int32_t dimension_flag = 0;         // 90
    std::string dimension_dependency = "0";     // 330
    int32_t version = 0;                // 90 (ACDBASSOCALIGNEDDIMACTIONBODY)
    std::string first_pointer = "0";    // 330
    std::string second_pointer = "0";   // 330
};

// --- 9.15 persistent subentity history (plan phase 9, step 9.15; assoc_history.cpp): the entries of the managers' history ---
// What AutoCAD's own files show (doc/facit/fas9_assoc_manager.scr, fas9_assoc_extended.scr, fas9_assoc_core.scr, the surfaces
// and solids files of 9.10, the corpus' Surface_2004 and example_*, in 2000 … 2018): every entry is an item (an edge, a
// vertex, a vertex of four faces, a pair of lists of ids …), a container of items, a trim (kind 8) or a surface fillet (kind
// 10). A number AutoCAD writes as 1, an
// id and a parameter is a PersHistoryId; a name record is an optional id, a kind (0 an edge, 1 a vertex), the ids of the
// faces that meet there and the slots after them (101000 is "none"). The sizes of the blocks, the counts of the
// items and the payload of a container follow from what is written.
struct PersHistoryId {
    int32_t id = 0;                     // 90 after the 1
    int32_t parameter = 0;              // 90: 0, 10, 20, 1001 …
    // A face of a name record named by more than one id: AutoCAD writes the face as `count, then id, parameter` for each, and
    // `1 id param` is the face of one id (a wall extruded from a polyline of two segments: `2, 1 21, 1 22`; the SURFFILLET of
    // doc/facit/fas9_assoc_history_surf.scr). The ids after the first; their own `more` stays empty
    std::vector<PersHistoryId> more;
};

struct PersHistoryRecord {              // a naming record: [id], 0, kind, count + faces, the slots
    std::optional<PersHistoryId> id;    // 1 and the id, or 0 when the record has none
    int32_t reserved = 0;               // 90 (0 in every file)
    int32_t kind = 0;                   // 90: 0 for an edge, 1 for a vertex
    std::vector<PersHistoryId> faces;   // 90 count, then 1 + id + parameter each
    std::vector<int32_t> slots;         // the ids after them (101000: none; 900, 301002, 103003 …); their number is the record's
};

struct PersHistoryList {                // a counted list of ids; a member is `count, id, parameter ...`, the ids after the first in `more`
    std::vector<PersHistoryId> ids;
};

// An item: AutoCAD's `kind, 0, count of the payload, the payload, the size of the block, the block, [new form] a count of
// doubles and the doubles`. The payload is a step id (0 inside a container) and then groups of ids that AutoCAD separates with
// 0. The block is name records (kinds 3, 4 and 5), two lists (kind 9) or zeros (kind 7)
struct PersHistoryItem {
    int32_t kind = 0;                   // 3, 4, 5, 7, 9 (the kinds 8 and 10 are PersHistoryTrim and PersHistoryFillet)
    int32_t reserved = 0;               // 90 after the kind (0 in every file)
    int32_t step = 0;                   // the first of the payload: the step the item belongs to, 0 in a container
    std::vector<std::vector<int32_t>> groups;   // the rest of the payload, the groups separated by 0
    std::vector<PersHistoryRecord> records;     // kinds 3, 4, 5: the block
    std::vector<PersHistoryList> lists;         // kind 9: the block's two lists
    int32_t zero_count = 0;             // kind 7: the number of zeros the block holds (6 in the new form, 4 in the old)
    std::vector<double> parameters;     // the doubles after the block (new form; 0 or 2 in AutoCAD's files)
};

// Kind 6: the payload is the container's step and the payloads of its items, the block three zeros (one in the old form), the
// number of items and the items without their payloads
struct PersHistoryContainer {
    int32_t step = 0;                   // the first of the payload
    int32_t reserved = 0;               // 90 after the kind
    std::vector<PersHistoryItem> items;
    std::vector<double> parameters;
};

// The kinds 8 and 10 (SURFTRIM and SURFFILLET; doc/facit/fas9_assoc_history_surf.scr, fas9_assoc_extended.scr, fas9_surfaces.scr)
// name subentities by paths: `90 count, then id, parameter` for each id of the path (no 1 before them). Every id of a path has
// one int of the entry's payload after the payload of what it belongs to: the step that made the subentity (the extruded wall's
// step for the face of that wall)
struct PersHistoryPathId {
    int32_t id = 0;                     // 90
    int32_t parameter = 0;              // 90: 0, 1, 1001 …
    int32_t step = 0;                   // its int in the entry's payload
};

struct PersHistoryPath {                // 90 count + the ids
    std::vector<PersHistoryPathId> ids;
};

// An item of a kind 8 entry: the item as in a container (kind, 0, n, S, block, 0 doubles) and a counted list of paths after it
struct PersHistoryTrimItem {
    PersHistoryItem item;               // the payload is the item's own n ints (step 0 and its groups)
    std::vector<PersHistoryPath> paths;
};

// Kind 8 (SURFTRIM, one entry per trimmed surface and cutting edge): the payload is the step, then for every item its payload and
// the steps of its paths' ids; the block three zeros, the number of items and the items (new form only, as AutoCAD's files have
// it). The doubles after the block are the entry's (2 per cutting edge in AutoCAD's files)
struct PersHistoryTrim {
    int32_t step = 0;                   // the first of the payload
    int32_t reserved = 0;               // 90 after the kind
    std::vector<PersHistoryTrimItem> items;
    std::vector<double> parameters;
};

// Kind 10 (SURFFILLET): the payload is the step and the steps of the ids of the surfaces' paths, in order; the block three zeros,
// 90 count of surfaces and for each a counted list of paths (its faces), then 90 count of groups, each a count of lists and
// the lists as `90 count + ints`: for each surface the indices of its paths the fillet stands on (a wall of two faces: 0 and 1)
struct PersHistoryFillet {
    int32_t step = 0;                   // the first of the payload
    int32_t reserved = 0;               // 90 after the kind
    std::vector<std::vector<PersHistoryPath>> surfaces;
    std::vector<std::vector<std::vector<int32_t>>> groups;
    std::vector<double> parameters;     // none in AutoCAD's files
};

using PersHistoryEntry = std::variant<PersHistoryItem, PersHistoryContainer, PersHistoryTrim, PersHistoryFillet>;

// ACDBASSOCPERSSUBENTMANAGER (AcDbAssocPersSubentManager): the drawing's table of persistent subentity ids and the history entries
// of what was done to the drawing's solids and surfaces (step 9.15; before it a manager with entries stayed as it was read)
struct AssocPersSubentManagerData {
    int32_t version = 2;                // 90: 1 until 2010, 2 from 2013
    int32_t header_1 = 3;               // 90
    int32_t header_2 = 0;               // 90
    int32_t header_3 = 2;               // 90
    int32_t next_id = 0;                // 90: the highest id given
    std::vector<PersHistoryEntry> history;      // 90 count + the entries (step 9.15)
    std::vector<int32_t> first_list;    // 90 count + ids
    std::vector<int32_t> second_list;   // 90 count + ids
    std::vector<int32_t> third_list;    // version 2 only: 90 count + ids (none in most of AutoCAD's files; [15] in the 2013 and
                                        // 2018 files of fas9_assoc_extended.scr, step 9.15)
    bool flag = false;                  // 290
};

// ACDBPERSSUBENTMANAGER (AcDbPersSubentManager): the base class's own record
struct PersSubentManagerData {
    int32_t version = 2;                // 90
    int32_t header_1 = 0;               // 90
    int32_t header_2 = 2;               // 90
    int32_t next_id = 0;                // 90
    std::vector<PersHistoryEntry> history;      // 90 count + the entries (step 9.15)
    std::vector<int32_t> ids;           // 90 count + ids
};

// ---------------------------------------------------------------------------
// --- 9.15 other ACDBASSOC classes (plan phase 9, step 9.15): the 2D constraint group, variables, the other
// action bodies and parameters, the dimension and multileader bodies, the arrays. AutoCAD's own files (the facit of
// doc/facit/fas9_assoc_extended.scr in 2000 … 2018 and both formats, Autodesk's installed sample drawings opened and saved
// by AutoCAD 2025's core, the corpus' acad_*_dxf_associative.dxf and _surfaces.dxf). Built on the 9.10 bases above: the form
// follows the record's own version groups (B110); a group whose meaning AutoCAD's files do not show is named by its code.
// ---------------------------------------------------------------------------

// The value of a variable or a value dependency (AcDbEvalVariant as these records write it): 40 real, 90 integer, 70 short,
// 1 string
struct AssocEvalValue {
    int code = 40;
    double real = 0.0;                  // 40
    int32_t integer = 0;                // 90, 70
    std::string text;                   // 1
};

// A member of a variable's list of the dependencies it owns: 360 and the 90 after it
struct AssocVariableMember {
    std::string dependency = "0";       // 360 (hard owner)
    int32_t value_90 = 0;               // 90 after it (0 in every file)
};

// ACDBASSOCVARIABLE: AcDbAssocAction (the action base) and AcDbAssocVariable. A user parameter (-PARAMETERS) or the value of a
// dimensional constraint (d1, ang1 …): name, the expression the user typed, the evaluator, the value
struct AssocVariableData {
    AssocActionBase action;
    int32_t version = 2;                // 90
    std::string name;                   // 1: "d1", "width", "ang1"
    std::string expression;             // 1: "80", "width*2" (empty for a parameter without one)
    std::string evaluator = "AcDbCalc:1.0";     // 1
    std::string description;            // 1
    AssocEvalValue value;               // 40 (real), 90 (integer) or 1 (string)
    bool flag_290_a = false;            // 290
    bool flag_290_b = false;            // 290
    std::vector<AssocVariableMember> members;   // 90 count + (360, 90) each: the value dependencies the variable owns
};

// ACDBASSOCVALUEDEPENDENCY: AcDbAssocDependency and AcDbAssocValueDependency
struct AssocValueDependencyData {
    AssocDependencyBase dependency;
    int32_t version = 0;                // 90
    std::string name;                   // 1 (empty in every file)
    AssocEvalValue value;               // 40 or 90
};

// ASSOCDIMDEPENDENCYBODY (the plan's ACDBASSOCDIMDEPENDENCYBODY: AutoCAD writes the record without the ACDB): the body of the
// dependency on a dimensional constraint's dimension (360 of the dependency). AcDbAssocDependencyBody 90, AcDbImpAssocDim
// DependencyBodyBase 90 and 1 (the dimension's text as "d1=80.0000" or "d2=width*2"), AcDbAssocDimDependencyBody 90
struct AssocDimDependencyBodyData {
    int32_t body_version = 1;           // 90 (AcDbAssocDependencyBody)
    int32_t base_version = 1;           // 90 (AcDbImpAssocDimDependencyBodyBase)
    std::string dimension_text;         // 1: name=expression
    int32_t version = 1;                // 90 (AcDbAssocDimDependencyBody)
};

// The nodes of a 2D constraint group (AcConstraintGroupNode's classes): a node is its AutoCAD class and its groups. Every
// node starts with its number (90), the numbers of the nodes it is connected to (90 count + 90 each) and 70 flags; a geometry
// node then has 330 (the geometry dependency it stands for) and 90, a constraint node 90 and two 290
struct AssocConstraintNodeHead {
    int32_t id = 0;                     // 90
    std::vector<int32_t> connected;     // 90 count + 90 each
    int16_t flags = 0;                  // 70: 0, 1 or 4
    int32_t reserved = 0;               // 90 (0 in every file)
};
struct AssocConstraintGeometryHead {
    AssocConstraintNodeHead head;
    std::string dependency = "0";       // 330: the ACDBASSOCGEOMDEPENDENCY of the geometry (0 for a datum line and an implicit point)
};
struct AssocConstraintHead {
    AssocConstraintNodeHead head;
    bool flag_290_a = false;            // 290
    bool flag_290_b = true;             // 290
};

struct AcConstrainedBoundedLine {       // a line of the drawing: its line and its two end points
    AssocConstraintGeometryHead node;
    Point3 origin;                      // 10
    Point3 direction;                   // 10
    bool bounded_flag = false;          // 290
    Point3 start;                       // 10
    Point3 end;                         // 11
};
struct AcConstrainedImplicitPoint {     // the end point (280: 0 start, 1 end, 2 middle, 3 centre) of a curve
    AssocConstraintGeometryHead node;
    int16_t kind = 0;                   // 280
    int32_t value_90 = -1;              // 90 (-1 in every file)
    int32_t curve = 0;                  // 90: the number of the curve the point belongs to
};
struct AcConstrainedDatumLine {         // the datum line of a horizontal or vertical constraint
    AssocConstraintGeometryHead node;
    Point3 origin;                      // 10
    Point3 direction;                   // 10
};
struct AcConstrained2PointsConstructionLine {
    AssocConstraintGeometryHead node;
    Point3 origin;                      // 10
    Point3 direction;                   // 10
};
struct AcConstrainedArc {
    AssocConstraintGeometryHead node;
    Point3 center;                      // 10
    Point3 normal;                      // 10
    Point3 reference;                   // 10
    double radius = 0.0;                // 40
    double start_angle = 0.0;           // 40
    double end_angle = 0.0;             // 40
    double value_40 = 0.0;              // 40, 2013 and later (version 2); 0 in every file
    Point3 start;                       // 10
    Point3 end;                         // 11
};
struct AcConstrainedCircle {
    AssocConstraintGeometryHead node;
    Point3 center;                      // 10
    Point3 normal;                      // 10
    Point3 reference;                   // 10
    double radius = 0.0;                // 40
    double start_angle = 0.0;           // 40
    double end_angle = 0.0;             // 40
    double value_40 = 0.0;              // 40, 2013 and later (version 2); 0 in every file
};
// The constraints with no groups of their own
struct AcPointCurveConstraint { AssocConstraintHead node; };
struct AcPointCoincidenceConstraint { AssocConstraintHead node; };
struct AcParallelConstraint { AssocConstraintHead node; };
struct AcPerpendicularConstraint { AssocConstraintHead node; };
struct AcColinearConstraint { AssocConstraintHead node; };
struct AcTangentConstraint { AssocConstraintHead node; };
struct AcEqualLengthConstraint { AssocConstraintHead node; };
struct AcEqualRadiusConstraint { AssocConstraintHead node; };
struct AcMidPointConstraint { AssocConstraintHead node; };
struct AcCenterPointConstraint { AssocConstraintHead node; };
struct AcFixedConstraint { AssocConstraintHead node; };
struct AcHorizontalConstraint {
    AssocConstraintHead node;
    int32_t datum_line = 0;             // 90: the number of the datum line node
};
struct AcVerticalConstraint {
    AssocConstraintHead node;
    int32_t datum_line = 0;             // 90
};
// The dimensional constraints: two 340 (the value dependency of the dimension and the one of its variable) and 280
struct AcDistanceConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280: 0 or 1; 1 has the direction
    std::optional<Point3> direction;    // 10 (when kind is 1)
};
struct AcAngleConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280
};
struct Ac3PointAngleConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280
};
struct AcRadiusDiameterConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280
};

using AssocConstraintNode = std::variant<
    AcConstrainedBoundedLine, AcConstrainedImplicitPoint, AcConstrainedDatumLine, AcConstrained2PointsConstructionLine,
    AcConstrainedArc, AcConstrainedCircle, AcPointCurveConstraint, AcPointCoincidenceConstraint, AcParallelConstraint,
    AcPerpendicularConstraint, AcColinearConstraint, AcTangentConstraint, AcEqualLengthConstraint, AcEqualRadiusConstraint,
    AcMidPointConstraint, AcCenterPointConstraint, AcFixedConstraint, AcHorizontalConstraint, AcVerticalConstraint,
    AcDistanceConstraint, AcAngleConstraint, Ac3PointAngleConstraint, AcRadiusDiameterConstraint>;

// AutoCAD's class name of a node ("AcPointCurveConstraint") and the number of the node
const char* assoc_constraint_node_class(const AssocConstraintNode& node);
int32_t assoc_constraint_node_id(const AssocConstraintNode& node);
const AssocConstraintNodeHead& assoc_constraint_node_head(const AssocConstraintNode& node);
AssocConstraintNodeHead& assoc_constraint_node_head(AssocConstraintNode& node);

// ACDBASSOC2DCONSTRAINTGROUP: the base AcDbAssocAction (whose own dependency list is empty) and, in the same subclass, the
// group (AutoCAD writes no marker of its own): the plane, the geometry dependencies the group owns and the graph of nodes
struct Assoc2dConstraintGroupData {
    AssocActionBase action;
    int32_t version = 2;                // 90
    int16_t flags = 0;                  // 70
    Point3 plane_origin;                // 10
    Point3 plane_x_axis{1.0, 0.0, 0.0}; // 10
    Point3 plane_y_axis{0.0, 1.0, 0.0}; // 10
    std::string workplane = "0";        // 360
    std::vector<std::string> dependencies;  // 90 count + 360 (hard owner): the geometry dependencies
    int32_t next_id = 0;                // 90: the highest node number given
    int32_t reserved_1 = 0;             // 90
    int32_t reserved_2 = 0;             // 90
    bool flag = false;                  // 290
    std::vector<AssocConstraintNode> nodes;     // 90 count, then (1 class, 90 number) each, then every node
};

// ---- the other action bodies ----
struct AssocOffsetSurfaceActionBodyData {   // AcDbAssocOffsetSurfaceActionBody 90, 290
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    int32_t version = 0;
    bool flag = true;                   // 290
};
struct AssocTrimSurfaceActionBodyData {     // AcDbAssocTrimSurfaceActionBody 90, 290, 290, 40
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    bool flag_290_a = false;            // 290
    bool flag_290_b = false;            // 290
    double value = 0.0;                 // 40
};
struct AssocPatchSurfaceActionBodyData {    // AcDbAssocPatchSurfaceActionBody 90
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};
struct AssocNetworkSurfaceActionBodyData {  // AcDbAssocNetworkSurfaceActionBody 90
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};
struct AssocBlendSurfaceActionBodyData {    // AcDbAssocBlendSurfaceActionBody 90, 290, 291, 292, 72, 293, 294, 73
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    bool flag_290 = false;
    bool flag_291 = false;
    bool flag_292 = false;
    int16_t value_72 = 0;
    bool flag_293 = false;
    bool flag_294 = false;
    int16_t value_73 = 0;
};
struct AssocExtendSurfaceActionBodyData {   // AcDbAssocExtendSurfaceActionBody 90, 280
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    int16_t value_280 = 0;
};
struct AssocFilletSurfaceActionBodyData {   // AcDbAssocFilletSurfaceActionBody 90, 70, two 2D points
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    int32_t version = 0;
    int16_t value_70 = 0;
    Point2 first_point;                 // 10
    Point2 second_point;                // 10
};
// ACDBASSOCEDGEFILLETACTIONBODY and ACDBASSOCEDGECHAMFERACTIONBODY share one subclass, AcDbAssocEdgeFilletChamferActionBody
struct AssocEdgeFilletChamferActionBodyData {
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 1;                // 90
    std::optional<PersistentSubentityId> subentity_id;  // 290 and the id (1 and its groups)
    std::vector<Point3> first_points;   // 90 count + 10
    std::vector<Point3> second_points;  // 90 count + 10
    std::vector<Point3> third_points;   // 90 count + 10
};
struct AssocRestoreEntityStateActionBodyData {  // AcDbAssocRestoreEntityStateActionBody 90, 330
    AssocActionBodyHead head;
    int32_t version = 0;
    std::string dependency = "0";       // 330
};
// ACDBASSOCIMPLIEDSURFACEORSOLIACTIONBODY (AutoCAD 2025's trim-surface result, doc/facit/*_surfaces_trim_trimmed.dxf): the
// action body's head (version 1 without parameters up to 2010, 2 from 2013) and AcDbAssocImpliedSurfaceOrSolidActionBody 90
struct AssocImpliedSurfaceOrSolidActionBodyData {
    AssocActionBodyHead head;
    int32_t version = 0;
};
// A body of AcDbAssocActionBody's own 90 and 330 after its version 2 form (or after the parameters of version 1), as the
// aligned dimension's: shared by the dimensions and the multileader
struct AssocDimensionBodyBase {
    AssocActionBodyHead head;
    int32_t dimension_flag = 0;         // 90
    std::string dimension_dependency = "0";     // 330
    int32_t version = 0;                // 90 (the own subclass)
};
struct AssocRotatedDimActionBodyData {      // ACDBASSOCROTATEDDIMACTIONBODY: 90, 330, 330
    AssocDimensionBodyBase body;
    std::string first_pointer = "0";
    std::string second_pointer = "0";
};
struct Assoc3PointAngularDimActionBodyData {    // ACDBASSOC3POINTANGULARDIMACTIONBODY: 90, 330, 330, 330
    AssocDimensionBodyBase body;
    std::string first_pointer = "0";
    std::string second_pointer = "0";
    std::string third_pointer = "0";
};
struct AssocOrdinateDimActionBodyData {     // ACDBASSOCORDINATEDIMACTIONBODY: AutoCAD's own subclass name is ACDBASSOC3POINTANGULARDIMACTIONBODY: 90, 330, 330
    AssocDimensionBodyBase body;
    std::string first_pointer = "0";
    std::string second_pointer = "0";
};
struct AssocDiametricDimActionBodyData {    // ACDBASSOCDIAMETRICDIMACTIONBODY: 90, 330
    AssocDimensionBodyBase body;
    std::string pointer = "0";
};
struct AssocRadialDimActionBodyData {       // ACDBASSOCRADIALDIMACTIONBODY: 90, 330
    AssocDimensionBodyBase body;
    std::string pointer = "0";
};
struct AssocMLeaderActionBodyData {         // ACDBASSOCMLEADERACTIONBODY: 90, 90, 90, 330
    AssocDimensionBodyBase body;
    int32_t value_90_a = 0;
    int32_t value_90_b = 0;
    std::string pointer = "0";
};

// ---- the other action parameters ----
struct AssocAsmBodyActionParamData {        // ACDBASSOCASMBODYACTIONPARAM: AcDbAssocAsmBodyActionParam 90, 290
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;
    bool flag = false;                  // 290
};
struct AssocFaceActionParamData {           // ACDBASSOCFACEACTIONPARAM: the ASM body part, then AcDbAssocFaceActionParam 90, 90
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t asm_version = 0;            // 90 (AcDbAssocAsmBodyActionParam)
    bool asm_flag = false;              // 290
    int32_t version = 0;                // 90
    int32_t value_90 = 0;               // 90
};
struct AssocTrimmingPathActionParamData {   // ACDBASSOCTRIMMINGPATHACTIONPARAM: compound, AcDbAssocPathActionParam 90, own 90 + point + 90
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
    int32_t path_version = 0;           // 90 (AcDbAssocPathActionParam)
    int32_t version = 0;                // 90
    Point3 direction;                   // 10
    int32_t value_90 = 0;               // 90
};
// ACDBASSOCTRIMMINGBODYACTIONPARAM: the parameter of a surface SURFTRIM cuts with (doc/facit/fas9_assoc_history_surf.scr, 2000 …
// 2018): the face parameter's subclasses (AcDbAssocAsmBodyActionParam 90, 290; AcDbAssocFaceActionParam 90, 90), then
// AcDbAssocTrimmingBodyActionParam 90 and 280 (0 for a plane cut by a vertical surface, 1 for two walls that trim each other)
struct AssocTrimmingBodyActionParamData {
    AssocFaceActionParamData face;      // the subclasses of ACDBASSOCFACEACTIONPARAM
    int32_t version = 0;                // 90 (AcDbAssocTrimmingBodyActionParam)
    int32_t value_280 = 0;              // 280
};
struct AssocEntityCloneActionBodyData {     // ACDBASSOCENTITYCLONEACTIONBODY: the body head and AcDbAssocEntityCloneActionBody 90 (format not established: no AutoCAD file has one)
    AssocActionBodyHead head;
    int32_t version = 0;
};
struct AssocCompoundActionParamData {       // ACDBASSOCCOMPOUNDACTIONPARAM: only the two base subclasses
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
};
struct AssocObjectActionParamData {         // ACDBASSOCOBJECTACTIONPARAM: the base subclasses and AcDbAssocObjectActionParam 90
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;
};

// ---- the arrays ----
// The parameters object an array body names (1, then 90): the class decides which array it is
enum class AssocArrayKind : uint8_t { Rectangular, Polar, Path, Modify };
const char* assoc_array_parameters_class(AssocArrayKind kind);
bool assoc_array_kind_of(const std::string& class_name, AssocArrayKind& kind);
struct AssocArrayParameters {
    AssocArrayKind kind = AssocArrayKind::Rectangular;
    int32_t version = 0;                // 90 after the class name
};
// AcDbAssocArrayItem: 90 version, 90 item, 90 row, 90 level, 90 flags (bit 4: the placement is a 4 x 4 matrix of 16 values (40),
// else a point (11); bit 16: a second pointer), then 330 for the item's object (and 330 for its modify body)
struct AssocArrayItem {
    int32_t version = 0;                // 90
    int32_t item_index = 0;             // 90: the item (column or position)
    int32_t row_index = 0;              // 90
    int32_t level_index = 0;            // 90
    int32_t flags = 8;                  // 90
    Point3 position;                    // 11 (flags without bit 4)
    std::vector<double> matrix;         // 16 x 40 (flags with bit 4): a 4 x 4 matrix, row by row
    std::string entity = "0";           // 330
    std::string modify_body = "0";      // 330 (flags with bit 16)
};
struct AssocArrayBody {                     // AcDbAssocArrayActionBody
    int32_t version = 1;                // 90
    AssocArrayParameters parameters;    // 1 class name, 90
    std::string item_class = "AcDbAssocArrayItem";   // 1
    std::vector<AssocArrayItem> items;  // 90 count, then a subclass of each
    std::vector<double> matrix;         // 16 x 40 after the last item (the array's placement)
};
struct AssocArrayActionBodyData {           // ACDBASSOCARRAYACTIONBODY
    AssocActionBodyHead head;
    AssocArrayBody array;
};
struct AssocArrayModifyActionBodyData {     // ACDBASSOCARRAYMODIFYACTIONBODY: the array part, then AcDbAssocArrayModifyActionBody 5 x 90
    AssocActionBodyHead head;
    AssocArrayBody array;
    int32_t version = 0;                // 90
    int32_t value_90_a = 1;
    int32_t value_90_b = 1;
    int32_t value_90_c = 0;
    int32_t value_90_d = 0;
};

struct DictionaryEntry {
    std::string name;                   // 3
    std::string handle;                 // 350 (soft owner) or 360 (hard owner)
    bool hard_owner = false;            // true for 360
};

struct DictionaryData {
    bool hard_owner = false;            // 280: entries are hard-owned
    int cloning = 1;                    // 281: duplicate record cloning flag
    std::vector<DictionaryEntry> entries;
};

struct DictionaryWithDefaultData {
    DictionaryData dictionary;
    std::string default_handle;         // 340
};

struct XRecordData {
    int cloning = 1;                    // 280 (2000+)
    std::vector<DxfGroup> data;         // the application's groups, every code is data
};

struct GroupData {
    std::string description;            // 300
    bool unnamed = false;               // 70
    bool selectable = true;             // 71
    std::vector<std::string> entities;  // 340: handles of the members
};

// AcDbPlotSettings — a PLOTSETTINGS object, and the first part of every LAYOUT
struct PlotSettings {
    std::string page_setup_name;        // 1
    std::string printer_name;           // 2: printer or configuration file
    std::string paper_size;             // 4
    std::string plot_view_name;         // 6
    double margin_left = 0.0;           // 40 (mm)
    double margin_bottom = 0.0;         // 41
    double margin_right = 0.0;          // 42
    double margin_top = 0.0;            // 43
    double paper_width = 0.0;           // 44
    double paper_height = 0.0;          // 45
    Point2 plot_origin;                 // 46, 47
    Point2 window_min;                  // 48, 49
    Point2 window_max;                  // 140, 141
    double scale_numerator = 1.0;       // 142: real world units
    double scale_denominator = 1.0;     // 143: drawing units
    int flags = 0;                      // 70
    int paper_units = 0;                // 72: 0 inches, 1 mm, 2 pixels
    int rotation = 0;                   // 73: 0, 90, 180, 270 degrees as 0-3
    int plot_type = 0;                  // 74
    std::string style_sheet;            // 7
    int standard_scale_type = 0;        // 75
    // 2004+: as AutoCAD writes a new drawing's layouts
    int shade_plot_mode = 0;            // 76: 0 as displayed, 1 wireframe, 2 hidden, 3 rendered
    int shade_plot_resolution = 2;      // 77: 0 draft, 1 preview, 2 normal, 3 presentation,
                                        //     4 maximum, 5 custom (78)
    int shade_plot_dpi = 300;           // 78: 100-32767
    double scale_factor = 1.0;          // 147
    Point2 image_origin;                // 148, 149
    std::string shade_plot_handle;      // 333
};

struct LayoutData {
    PlotSettings plot_settings;         // AcDbPlotSettings
    std::string name;                   // 1
    int flags = 0;                      // 70
    int tab_order = 0;                  // 71
    Point2 limits_min;                  // 10, 20
    Point2 limits_max;                  // 11, 21
    Point3 insertion_base;              // 12, 22, 32
    Point3 extents_min;                 // 14, 24, 34
    Point3 extents_max;                 // 15, 25, 35
    double elevation = 0.0;             // 146
    Point3 ucs_origin;                  // 13, 23, 33
    Point3 ucs_x_axis = {1.0, 0.0, 0.0}; // 16, 26, 36
    Point3 ucs_y_axis = {0.0, 1.0, 0.0}; // 17, 27, 37
    int ucs_ortho_type = 0;             // 76
    std::string block_record_handle;    // 330: the layout's *Model_Space / *Paper_Space*
    std::string last_viewport_handle;   // 331
    std::string ucs_handle;             // 345
    std::string base_ucs_handle;        // 346
    // 2013+: the layout's preview image (a PNG, its Thumbnail_Data record in the ACDSDATA
    // section; plan phase 5, B61) as read; HDxf neither decodes nor makes it. Not written before
    // 2013, where AutoCAD's files have none (a warning). Empty = none.
    std::vector<uint8_t> thumbnail;
};

struct DictionaryVarData {
    int schema = 0;                     // 280
    std::string value;                  // 1
};

struct PlaceholderData {};

// MLINESTYLE (in ACAD_MLINESTYLE)
struct MlineStyleElement {
    double offset = 0.0;                // 49
    Color color;                        // 62
    std::string linetype = "BYLAYER";   // 6
};

struct MlineStyleData {
    std::string name;                   // 2
    int flags = 0;                      // 70: 1 fill, 2 miters, 16/32 start/end square cap,
                                        //     64/128 inner arcs, 256/512 outer arcs
    std::string description;            // 3
    Color fill_color;                   // 62
    double start_angle = 90.0;          // 51
    double end_angle = 90.0;            // 52
    std::vector<MlineStyleElement> elements;   // 71 (the count)
};

// IMAGEDEF (in ACAD_IMAGE_DICT): the image file of IMAGE entities
struct ImageDefData {
    int class_version = 0;              // 90
    std::string file_name;              // 1
    Point2 size;                        // 10, 20: in pixels
    Point2 pixel_size = {1.0, 1.0};     // 11, 21: of one pixel, in AutoCAD units
    bool loaded = true;                 // 280
    int resolution_units = 0;           // 281: 0 none, 2 centimeters, 5 inches
};

// IMAGEDEF_REACTOR: owned by an IMAGE, a reactor of its IMAGEDEF
struct ImageDefReactorData {
    int class_version = 2;              // 90
    std::string image_handle;           // 330
};

// RASTERVARIABLES (ACAD_IMAGE_VARS)
struct RasterVariablesData {
    int class_version = 0;              // 90
    bool frame = true;                  // 70: image frames shown
    int quality = 1;                    // 71: 0 draft, 1 high
    int units = 0;                      // 72: 0 none, 1 mm, 2 cm, 3 m, 4 km, 5 inch, 6 foot,
                                        //     7 yard, 8 mile
};

// DBCOLOR: a color from a color book the drawing uses, in the dictionary ACAD_COLOR under
// its name (plan phase 2, B21). AutoCAD writes the color from 2004 (62, 420, 430, and the
// XDATA ACAD "DBColXD", which HDxf writes from the color); in 2000 the object is empty.
struct DbColorData {
    Color color;
};

// MLEADERSTYLE (plan phase 3, B37), in ACAD_MLEADERSTYLE: what a MULTILEADER has unless it
// overrides it. Defaults: AutoCAD's Standard. Table records and blocks by name, as in DIMSTYLE.
struct MLeaderStyleData {
    int content_type = 2;               // 170: 0 none, 1 block, 2 MTEXT, 3 tolerance
    int draw_mleader_order = 1;         // 171
    int draw_leader_order = 0;          // 172
    int max_leader_points = 2;          // 90
    double first_segment_angle = 0.0;   // 40
    double second_segment_angle = 0.0;  // 41
    int leader_type = 1;                // 173: 0 invisible, 1 straight, 2 spline
    CmColor leader_color;               // 91
    std::string leader_linetype = "ByBlock";    // 340: LTYPE name
    int leader_lineweight = -2;         // 92
    bool landing = true;                // 290
    double landing_gap = 0.09;          // 42
    bool dogleg = true;                 // 291
    double dogleg_length = 0.36;        // 43
    std::string description = "Standard";  // 3 (AutoCAD's own styles all have "Standard")
    std::string arrow_block;            // 341: empty = closed filled
    double arrow_size = 0.18;           // 44
    std::string default_text;           // 300
    std::string text_style = "Standard";        // 342: STYLE name
    int text_left_attachment = 1;       // 174
    int text_right_attachment = 6;      // 178
    int text_angle_type = 1;            // 175
    int text_alignment = 0;             // 176
    CmColor text_color;                 // 93
    double text_height = 0.18;          // 45
    bool text_frame = false;            // 292
    bool text_always_left = false;      // 297
    double align_space = 0.18;          // 46
    std::string block;                  // 343: block name, empty = none
    CmColor block_color;                // 94
    Point3 block_scale = {1.0, 1.0, 1.0};       // 47, 49, 140
    bool block_scale_enabled = true;    // 293
    double block_rotation = 0.0;        // 141
    bool block_rotation_enabled = true; // 294
    int block_connection = 0;           // 177
    double scale = 1.0;                 // 142
    bool overwrite_property = false;    // 295: written 0 before 2013, as AutoCAD (B60)
    bool annotative = false;            // 296
    double break_gap = 0.125;           // 143
    int version = 2;                    // 2000-2007 XDATA ACAD_MLEADERVER, 2010+ 179
    // 2010+
    int text_attachment_direction = 0;  // 271
    int bottom_attachment_direction = 9;    // 272
    int top_attachment_direction = 9;       // 273
    // 298 (2013+; in no reference): AutoCAD's Standard has 1. Before 2013 an XRECORD
    // ACAD_ROUNDTRIP_2012_MLEADER_STYLE (298 1) in the style's extension dictionary (B58, B60)
    bool text_extended = false;
};

// GEODATA (plan phase 4, B50): where the drawing is on the earth — in the model space
// BLOCK_RECORD's extension dictionary under ACAD_GEOGRAPHICDATA. AutoCAD writes three forms: version
// 1 (AutoCAD 2009; AutoCAD writes it so in 2007 files), 2 (2010) and 3 (2013; as 2 but the
// coordinate system in 303 parts). The defaults: AutoCAD's own GEODATA in LibreDWG's gh209_1 (2010).
struct GeoMeshPoint {
    Point2 source;                      // 13, 23 (version 1: 12, 22)
    Point2 destination;                 // 14, 24 (version 1: 13, 23)
};

struct GeoMeshFace {
    int a = 0;                          // 97
    int b = 0;                          // 98
    int c = 0;                          // 99
};

struct GeoDataData {
    int version = 2;                    // 90: 1 2009, 2 2010, 3 2013
    int coordinate_type = 1;            // 70: 0 unknown, 1 local grid, 2 projected grid,
                                        //     3 geographic (latitude/longitude)
    std::string host_block_handle;      // 330: the model space's BLOCK_RECORD; empty = it
    Point3 design_point;                // 10: in the WCS
    Point3 reference_point = {-122.394, 37.795, 0.0};  // 11: in the coordinate system
                                        //     (version 1: 41, 40, 42)
    double horizontal_unit_scale = 1.0; // 40 (version 1: 46): to meters
    int horizontal_units = 6;           // 91: AutoCAD's units (6 meters)
    double vertical_unit_scale = 1.0;   // 41
    int vertical_units = 6;             // 92
    Point3 up_direction = {0.0, 0.0, 1.0};  // 210
    Point2 north_direction = {0.0, 1.0};    // 12, 22
    int scale_estimation = 3;           // 95: 1 none, 2 the user's factor, 3 the grid scale at the
                                        //     reference point, 4 prismoidal
    double user_scale = 1.0;            // 141
    bool sea_level_correction = false;  // 294
    double sea_level_elevation = 0.0;   // 142
    double projection_radius = 0.0;     // 143
    std::string coordinate_system;      // 301 (version 3: in 303 parts of 250 characters, the
                                        //     last in 301; version 1: a WKT string)
    std::string geo_rss = "<georss:point>37.7950 -122.3940</georss:point>";   // 302
    std::string observation_from;       // 305
    std::string observation_to;         // 306
    std::string observation_coverage;   // 307
    std::vector<GeoMeshPoint> mesh_points;  // 93 (the count)
    std::vector<GeoMeshFace> mesh_faces;    // 96 (the count)
    // Version 1 only
    double north_angle = 0.0;           // 52: radians, clockwise from (0, 1)
    Point3 obsolete_point;              // 11 (ODA's specification: 0, 0, 0)
    Point3 obsolete_scale = {1.0, 1.0, 1.0};    // 43, 44, 45
    std::string datum_name;             // 303
    std::string coordinate_system_wkt;  // 304
    // Version 1: its CIVIL3D_DATA_BEGIN … CIVIL3D_DATA_END groups (3, 4) as read — written with
    // the 2010 values above in their places (95, 141, 294, 142, 143); the others are not documented
    std::vector<DxfGroup> civil3d;
};

// WIPEOUTVARIABLES: the wipeouts' frames (ACAD_WIPEOUT_VARS in the named object dictionary)
struct WipeoutVariablesData {
    int display_frame = 1;              // 70: 0 the frames are not shown, 1 shown (AutoCAD's: 1)
};

// SCALE (2007; AutoCAD 2008 writes them in 2004 files too): an annotation scale in
// ACAD_SCALELIST — paper units to drawing units
struct ScaleData {
    int flags = 0;                      // 70 (AutoCAD: 0)
    std::string name;                   // 300: "1:1"
    double paper_units = 1.0;           // 140
    double drawing_units = 1.0;         // 141
    bool unit_scale = false;            // 290: the 1:1 scale
};

// SUN (2007+): owned by a VPORT or VIEWPORT (its 361), as AutoCAD writes it
struct SunData {
    int version = 1;                    // 90
    bool on = false;                    // 290
    Color color = {7, 16777215, {}};    // 63, 421 (AutoCAD: 7 and white)
    double intensity = 1.0;             // 40
    bool shadows = true;                // 291
    int julian_day = 2460940;           // 91
    int time = 54000000;                // 92: milliseconds after midnight
    bool daylight_saving = false;       // 292
    int shadow_type = 0;                // 70: 0 ray traced, 1 shadow maps
    int shadow_map_size = 256;          // 71
    int shadow_softness = 1;            // 280
};

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

// SECTION_MANAGER (the NOD's ACAD_SECTION_MANAGER): AutoCAD 2025 writes 70 only (its sections are not listed there:
// the live sections' are, in AutoCAD 2018's LiveSection file and with a count of 0 in ACadSharp's 2013 sample)
struct SectionManagerData {
    bool needs_update = true;           // 70 (requires full update)
    std::vector<std::string> sections;  // 90 counts them (written when there are some), 330 each: soft pointers to SECTIONOBJECTs
};

// --- 9.18 AutoCAD Map, TurboCAD and ZWCAD records (plan phase 9, step 9.18; map_objects.cpp, tc_records.cpp, zwm_records.cpp) -------------
// Records of other programs' classes that AutoCAD keeps in a drawing and does not document. The grammar of each is what the files
// that program saved show (the old plan's appendix B.47 ... B.55, B.59, B.60, B.74, B.75, B.77 ... B.104): third-party observations,
// not AutoCAD facit (F9.24, F9.25: ej testat). Where a file does not explain a group it keeps its place as its code and its position
// (`reserved_70_0`, `text_1_1`, `handle_340_0`, `list_70_a` ...), never as a raw group. A list a count (70 or 90) gives is a vector
// whose size the writer counts; an empty one writes a count of 0.

// AutoCAD Map 3D (the application AcMapDisplayManagement): the display manager of a drawing. The unlimited scale AutoCAD Map writes is
// 1.797693134862314E+99.
inline constexpr double kMapNoLimit = 1.797693134862314e+99;

struct MapManagerData {                 // DMMAPMANAGER
    DictionaryData dictionary;          // AcDbDictionary: 281 and (3 the map's name, 350 the DMMAP it soft-owns) for each display map
    std::string default_handle = "0";   // AcDbDictionaryWithDefault 340: the default map
    int16_t version = 1;                // AcMapDMMapManager 70
    std::string current_map = "0";      // 340: the display map in use
};

struct MapStyleLibraryData {            // DMSTYLELIBRARY: AcMapDMStyleLibrary 70, 70 (count), 360 each
    int16_t version = 1;
    std::vector<std::string> categories;    // 360: the DMSTYLECATEGORYs it owns
};

struct MapStyleCategoryData {           // DMSTYLECATEGORY: AcMapDMStyleCategory 70, 1, 70
    int16_t version = 1;
    std::string name;                   // 1 ("Display Styles")
    int16_t reserved_70_0 = 0;          // 70 (0 in the one file: what a non-zero value is followed by is not established)
};

struct MapMetadataData {                // MAPMETADATAOBJECT: AcMapMetadataPersistObject 90, 90
    int32_t version = 1;
    int32_t reserved_90_0 = 0;          // 90 (0 in the one file)
};

struct MapResourceManagerData {         // MAPRESOURCEMANAGEROBJECT: AcMapResourceManager 70, 90
    int16_t version = 1;
    int32_t reserved_90_0 = 0;          // 90 (0 in both files)
};

struct MapStylizedEntitiesTableData {   // DMSTYLIZEDENTITIESTABLE: AcMapDMStylizedEntitiesTable 70
    int16_t version = 1;
};

// DMMAP has three AcMapDMMap subclasses of its own, told apart by their occurrence (0, 1, 2)
struct MapDisplayMapHeader {            // occurrence 0: 70, 1, 70, 330
    int16_t version = 1;
    std::string name;                   // 1 ("Default")
    int16_t reserved_70_0 = 1;          // 70
    std::string parent_map = "0";       // 330 (0 for none)
};

struct MapDisplayMapElements {          // occurrence 1: 70, 90 (count), 360 each, 90 (count), 40 each, 70
    int16_t version = 1;
    std::vector<std::string> elements;  // 360: the DMBASEELEMENTs it owns
    std::vector<double> scales;         // 40 (kMapNoLimit: no limit)
    int16_t reserved_70_0 = 1;          // 70
};

struct MapDisplayMapLegend {            // occurrence 2: the legend
    int16_t version = 2;                // 70
    int16_t reserved_70_0 = 0;          // 70
    double reserved_40_0 = kMapNoLimit; // 40
    bool reserved_290_0 = true;         // 290
    bool reserved_290_1 = true;         // 290
    double reserved_40_1 = 1.0;         // 40
    double reserved_40_2 = 3.0;         // 40
    std::string name;                   // 1 ("Legend")
    std::vector<double> scales;         // 90 (count), 40 each
    std::string text_1_0;               // 1
    int16_t reserved_70_1 = 0;          // 70
    double reserved_40_3 = 1.0;         // 40
    int16_t reserved_70_2 = 1;          // 70
    int32_t reserved_90_0 = 0;          // 90 (0 in the one file)
    std::vector<std::string> items;     // 90 (count), 360 each: owned records (a DMDEFAULTSTYLE in the one file)
};

struct MapDisplayMapData {              // DMMAP
    MapDisplayMapHeader header;
    MapDisplayMapElements elements;
    MapDisplayMapLegend legend;
};

// DMBASEELEMENT has three AcMapDMBaseElement subclasses
struct MapBaseElementHeader {           // occurrence 0: 70, 1, 70, 330
    int16_t version = 1;
    std::string name;                   // 1 ("Map Base")
    int16_t reserved_70_0 = 1;          // 70
    std::string map = "0";              // 330: the DMMAP it belongs to
};

struct MapBaseElementStyle {            // occurrence 1: 70, 1, 70, 1, 70, 40, 70 x4, 360
    int16_t version = 1;
    std::string text_1_0;               // 1
    int16_t reserved_70_0 = 2;          // 70
    std::string text_1_1;               // 1
    int16_t reserved_70_1 = 1;          // 70
    double reserved_40_0 = kMapNoLimit; // 40
    std::array<int16_t, 4> reserved_70_2 = {1, 1, 1, 1};    // 70 x4
    std::string style_reference = "0";  // 360: the DMSTYLEREFERENCE it owns
};

struct MapBaseElementData {             // DMBASEELEMENT
    MapBaseElementHeader header;
    MapBaseElementStyle style;
    int16_t version_c = 1;              // occurrence 2: 70
};

// DMDEFAULTSTYLE has two AcMapDMDefaultStyle subclasses; AcMapDMDisplayStyleRegapp is the record's XDATA
struct MapDefaultStyleData {            // DMDEFAULTSTYLE
    int16_t version = 1;                // occurrence 0: 70
    int32_t reserved_90_0 = 1;          // 90
    std::string name;                   // 1 ("Default")
    int16_t version_b = 1;              // occurrence 1: 70
    int16_t reserved_280_0 = 100;       // 280
    int32_t reserved_90_1 = 0;          // 90
};

struct MapStyleReferenceData {          // DMSTYLEREFERENCE: AcMapDMStyleReference 70, 70, 340, 330
    int16_t version = 1;
    int16_t flags = 0;                  // 70
    std::string style = "0";            // 340: the DMDEFAULTSTYLE it refers to
    std::string map = "0";              // 330: the DMMAP
};

struct MapSurrogateStyleSetsData {      // DMSURROGATESTYLESETS: AcMapDMSurrogateStyleSets 70, 1, 70, 40, 70 x4, 330, 70
    int16_t version = 1;
    std::string name;                   // 1 ("Map Base")
    int16_t reserved_70_0 = 1;          // 70
    double reserved_40_0 = kMapNoLimit; // 40
    std::array<int16_t, 4> reserved_70_1 = {1, 1, 1, 1};    // 70 x4
    std::string style_reference = "0";  // 330: the DMSTYLEREFERENCE
    int16_t reserved_70_2 = 0;          // 70
};

// TurboCAD (the application ExTcadCustObjs): TCOPTIONALDATA, a property set of an entity or a drawing. Its second 70 is a set of
// flags that say which groups follow, in this order: 1 (the text), 10 (the point) when 2, 90 and the properties when 128, 1 and
// 70 (the font) when 16, 40 when 32. A property is 1 (its name), 70 (its type: 5 a real, 8 a string, 18 an integer) and its value;
// a string of a version 1 record is group 1, of version 2 the bytes (90 their number, 310). Another type or another flag bit
// leaves the groups after the ones that are known in the preserved record.
struct TcProperty {
    std::string name;                   // 1
    int16_t type = 8;                   // 70: 5 (40), 8 (1 in version 1, 90 and 310 in version 2), 18 (70)
    std::string text;                   // type 8, version 1: 1
    std::vector<uint8_t> blob;          // type 8, version 2: 90 counts them, 310
    double real = 0.0;                  // type 5: 40
    int16_t integer = 0;                // type 18: 70
};

struct TcFont {
    std::string name;                   // 1 ("Arial")
    int16_t reserved_70_0 = 0;          // 70
};

struct TcOptionalDataData {             // TCOPTIONALDATA
    int16_t version = 2;                // 70 (1 or 2)
    uint16_t other_flags = 0;           // the bits of the second 70 that are none of the five below (kept as they are)
    std::optional<std::string> text;    // flag 1: group 1 ("Line/Polyline")
    std::optional<Point3> point;        // flag 2: 10 20 30
    std::optional<std::vector<TcProperty>> properties;      // flag 128: 90 counts them
    std::optional<TcFont> font;         // flag 16: 1 and 70
    std::optional<double> real;         // flag 32: 40
};

// ZWCAD Mechanical (the application ZWMCUSTCORE): ZWM*STD, the standard's styles. The components: ZwmObject, ZwmStd, ZwmSymbolStd and
// ZwmSymProperties, the repeated ZWMATTRIBUTEDEF, and what a style type has of its own
struct ZwmObjectHeader {                // ZwmObject: 70, 1 x4, 70
    int16_t version = 1;
    std::array<std::string, 4> text_1;  // 1 x4 ("", the standard's name, "", "")
    int16_t reserved_70_0 = 0;          // 70: the number of attribute definitions that follow (the style types COLOR, DIMSTYLE, LAYER,
                                        //     LINETYPE and TEXTSTYLE: the writer counts them); 0 in the others
};

// The eight handles, 16 + 8 integers and 8 strings that end ZwmStd, ZwmSymbolStd and a style type's last subclass (every handle 0 and
// every integer -1 in the files)
struct ZwmStdTail {
    std::array<std::string, 8> handle_340;
    std::array<int16_t, 16> reserved_70;
    std::array<int32_t, 8> reserved_90;
    std::array<std::string, 8> text_1;
    ZwmStdTail() {
        handle_340.fill("0");
        reserved_70.fill(-1);
        reserved_90.fill(-1);
    }
};

struct ZwmStd {                         // ZwmStd: 70 x3, 1 x2, the tail
    int16_t version = 1;
    int16_t reserved_70_0 = 1;
    int16_t reserved_70_1 = 0;
    std::array<std::string, 2> text_1;  // ("", the standard's name)
    ZwmStdTail tail;
};

struct ZwmSymbolStd {                   // ZwmSymbolStd: 70, the tail
    int16_t version = 1;
    ZwmStdTail tail;
};

struct ZwmTypeStd {                     // the type's own first subclass (ZwmBalloonStd ...): 70, 70, 1
    int16_t version = 1;
    int16_t reserved_70_0 = 1;
    std::string text_1_0;               // the standard's name
};

// What a ZWMATTRIBUTEDEF's value is follows from its fourth string (the value type): VT_R8 a real (40), VT_BOOL an integer (70),
// anything else a string (1)
struct ZwmAttributeValue {
    std::string text;                   // 1
    int16_t integer = 0;                // 70
    double real = 0.0;                  // 40
};

struct ZwmAttributeDefinition {         // ZWMATTRIBUTEDEF: 70, 70, 1 x7, 70 x11, the value
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
    std::array<std::string, 7> text_1;  // the name, the label, ..., [3] a label of the value type ("VT_BOOL", "VT_R8", "VT_BSTR" or ""), [4] the editor
    std::array<int16_t, 11> reserved_70_1 = {};     // [5] the OLE variant type, which says what the value is (8 text, 11 bool, 5
                                                    // real); when it is 0 the label in text_1[3] does
    ZwmAttributeValue value;
};

// An attribute definition of a map (ZwmSymProperties' and the properties of the BOM and TITLE types): `key` is the 1 before the
// definition's subclass (in the subclass before it), empty in a list that has none
struct ZwmAttributeEntry {
    std::string key;
    ZwmAttributeDefinition definition;
};

struct ZwmSymProperties {               // ZwmSymProperties: the symbol's properties and its attributes
    int16_t version = 2;                // 70
    std::array<std::string, 4> text_1;  // 1 x4 (the standard's name twice, "", its xml file "ISO.xml")
    int16_t reserved_70_0 = 0;          // 70 (91, 221, 401 ...: it differs per type)
    int32_t reserved_70_1 = 1;          // 70 (1; ZWMSTANDARD's 483351136 does not fit the 16 bits of a binary group)
    double reserved_40_0 = 0.0;         // 40
    double reserved_40_1 = 0.0;         // 40
    std::array<int16_t, 4> reserved_70_2 = {-1, -1, -1, -1};    // 70 x4
    double reserved_40_2 = 0.0;         // 40
    int16_t reserved_70_3 = 0;          // 70
    int16_t reserved_70_4 = 0;          // 70
    std::vector<int16_t> list_70_a;     // 70 counts them, then each
    std::vector<int16_t> list_70_b;     // 70 counts them, then each
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (the sizes "2.5" ... "20")
    std::vector<std::string> list_1_b;  // 70 counts them, then each 1
    std::string handle_340_0 = "0";
    std::array<double, 12> reserved_40_3 = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 3.5, 3.5, 3.5, 3.5, 3.5, 3.5};
    std::array<int16_t, 12> reserved_70_5 = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::array<std::string, 14> handle_340_1;
    std::array<int16_t, 16> reserved_70_6;
    std::array<int32_t, 8> reserved_90_0;
    std::array<std::string, 8> text_1_1;
    std::vector<ZwmAttributeEntry> attributes;      // 70 counts them; each is 1 (its key) and a ZWMATTRIBUTEDEF subclass
    ZwmSymProperties() {
        handle_340_1.fill("0");
        reserved_70_6.fill(-1);
        reserved_90_0.fill(-1);
    }
};

struct ZwmTextPair {                    // two strings: a name and its value
    std::string text_1_0;
    std::string text_1_1;
};

struct ZwmIntPair {
    int16_t reserved_70_0 = 0;
    int16_t reserved_70_1 = 0;
};

struct ZwmNameValue {                   // 1 and 70
    std::string text_1_0;
    int16_t reserved_70_0 = 0;
};

struct ZwmRealInt {                     // 40 and 70
    double reserved_40_0 = 0.0;
    int16_t reserved_70_0 = 0;
};

// ZWMBALLOONSTD
struct ZwmBalloonShape {                // 1, 1, 1, 70
    std::string text_1_0;
    std::string text_1_1;
    std::string text_1_2;               // a drawing file ("Table.dwg")
    int16_t reserved_70_0 = 0;
};

struct ZwmBalloonProperties {           // ZwmBalloonProperties
    int16_t version = 1;
    int16_t reserved_70_0 = 1;
    std::vector<int16_t> list_70_a;     // 70 counts them, then each
    std::vector<ZwmBalloonShape> shapes;    // 70 counts them
    std::array<std::string, 2> text_1;  // ("[RELPOS]", "[BALLOON_PART_COUNT]")
    int16_t reserved_70_1 = 0;
    std::array<double, 5> reserved_40 = {};
    std::string handle_340_0 = "0";
    std::string text_1_2;
    int16_t reserved_70_2 = 0;
};

struct ZwmBalloonStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmBalloonProperties properties;
};

// ZWMBOMSTYLESTD: the attribute definitions are the parts list's columns
struct ZwmBomProperties {               // ZwmBomProperties: 70, 1 x6, 70 x4, 70 (the count), the definitions
    int16_t version = 1;
    std::array<std::string, 6> text_1;  // (the style's name twice, the head and body drawing files, the xml file, "")
    std::array<int16_t, 4> reserved_70 = {};
    std::vector<ZwmAttributeEntry> attributes;      // 70 counts them; no key
};

struct ZwmBomStyleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmTypeStd type_std;
    ZwmBomProperties properties;
};

// ZWMBREAKSYMSTD
struct ZwmBreakSymProperties {
    int16_t version = 1;
    std::array<double, 2> reserved_40 = {};
};

struct ZwmBreakSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmBreakSymProperties properties;
};

// ZWMCENTERHOLESTD
struct ZwmCenterHoleProperties {
    int16_t version = 1;
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 ("R1.0/2.12" ...)
    int16_t reserved_70_0 = 0;
};

struct ZwmCenterHoleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmCenterHoleProperties properties;
};

// ZWMCHAMFERSTD
struct ZwmChamferProperties {
    int16_t version = 1;
    std::array<int16_t, 2> reserved_70 = {};
};

struct ZwmChamferStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmChamferProperties properties;
};

// ZWMCIRCLEMARKSTD
struct ZwmCircleMarkProperties {
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
};

struct ZwmCircleMarkStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmCircleMarkProperties properties;
};

// ZWMCOLORSTD: the attribute definitions follow ZwmObject (counted there); the color names and their values are the style's own
struct ZwmColorStdBody {                // ZwmColorStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> colors;    // the name of a color ("ZWMCL_CYAN") and its number as text
    ZwmStdTail tail;
};

struct ZwmColorStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;     // after ZwmObject, which counts them
    ZwmStd standard;
    ZwmColorStdBody color;
};

// ZWMDATUMIDSTD
struct ZwmDatumIdProperties {
    int16_t version = 1;
    std::array<double, 2> reserved_40 = {};
};

struct ZwmDatumIdStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmDatumIdProperties properties;
};

// ZWMDATUMTARGETSTD
struct ZwmDatumTargetProperties {
    int16_t version = 1;
    std::array<double, 3> reserved_40 = {};
    std::array<std::string, 2> handle_340 = {"0", "0"};
    std::array<int16_t, 2> reserved_70 = {};
};

struct ZwmDatumTargetStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmDatumTargetProperties properties;
};

// ZWMDETAILSTD
struct ZwmDetailProperties {
    int16_t version = 2;
    std::string handle_340_0 = "0";
    std::array<int16_t, 3> reserved_70_0 = {};
    std::string text_1_0;               // the label's text with its fields ("[DETAIL_LABEL] ([DETAIL_ABSSCALE])")
    int16_t reserved_70_1 = 0;
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (the scales "1000:1" ...)
    std::vector<std::string> list_1_b;  // 70 counts them, then each 1 (the identifiers "A" ...)
};

struct ZwmDetailStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmDetailProperties properties;
};

// ZWMDIMSTYLESTD
struct ZwmDimStyleStdBody {             // ZwmDimStyleStd: 70, 70 x3, 40 x2, 70 x3, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::array<int16_t, 3> reserved_70_0 = {};
    std::array<double, 2> reserved_40 = {};
    std::array<int16_t, 3> reserved_70_1 = {};
    std::vector<ZwmTextPair> styles;    // the name of a dimension style ("ZWMDS_MAIN") and AutoCAD's ("ISO_DIMSTYLE")
    ZwmStdTail tail;
};

struct ZwmDimStyleStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmDimStyleStdBody dim_style;
};

// ZWMELEVSYMSTD
struct ZwmElevSymProperties {
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
    std::array<double, 5> reserved_40 = {};
};

struct ZwmElevSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmElevSymProperties properties;
};

// ZWMFCFRAMESTD
struct ZwmFCFrameProperties {
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
    std::array<double, 2> reserved_40_0 = {};
    std::array<int16_t, 8> reserved_70_1 = {};
    std::vector<ZwmIntPair> rows_a;     // 70 counts them, then 70 70 each
    std::vector<ZwmIntPair> rows_b;     // 70 counts them, then 70 70 each
    std::array<double, 2> reserved_40_1 = {};
};

struct ZwmFCFrameStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmFCFrameProperties properties;
};

// ZWMFEATUREIDSTD
struct ZwmFeatureIdProperties {
    int16_t version = 1;
    std::array<double, 3> reserved_40 = {};
};

struct ZwmFeatureIdStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmFeatureIdProperties properties;
};

// ZWMFRAMESTYLESTD: a frame style is a list of frames; a frame is 1 (its name, in the subclass before it), its ZwmFrameProperties
// subclass and a 70 that ends it
struct ZwmFrameProperties {             // ZwmFrameProperties: 70, 1, 40 x9, 70, 1 x6, 90
    int16_t version = 1;
    std::string text_1_0;               // the sheet ("A3")
    std::array<double, 9> reserved_40 = {};     // the sheet's size (420, 297) and margins
    int16_t reserved_70_0 = 0;
    std::array<std::string, 6> text_1;  // ("Partition", "Partition", "L", the drawing file, "", "landscape")
    int32_t reserved_90_0 = 0;
};

struct ZwmFrameEntry {
    std::string name;                   // 1, before the subclass ("A3_Partition_landscape")
    ZwmFrameProperties properties;
    int16_t reserved_70_0 = 0;          // the 70 after the subclass's groups
};

struct ZwmFrameStyleStd {               // ZwmFrameStyleStd: 70, 70 (the count), 1 (the first frame's name)
    int16_t version = 1;
    std::vector<ZwmFrameEntry> frames;
};

struct ZwmFrameStyleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmFrameStyleStd style;
};

// ZWMHOLECHARTSTD
struct ZwmHoleChartProperties {
    int16_t version = 2;
    std::array<int16_t, 8> reserved_70_0 = {};
    std::array<ZwmRealInt, 5> pairs = {};
    std::array<int16_t, 8> reserved_70_1 = {};
    std::array<double, 6> reserved_40_0 = {};
    std::array<int16_t, 6> reserved_70_2 = {};
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (the columns of a table)
    std::vector<std::string> list_1_b;
    std::vector<std::string> list_1_c;
    std::array<std::vector<ZwmTextPair>, 6> rows;   // six lists of 70 (their count) and a pair of strings each
    std::array<int16_t, 5> reserved_70_3 = {};
    std::array<double, 2> reserved_40_1 = {};
};

struct ZwmHoleChartStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmHoleChartProperties properties;
};

// ZWMLAYERSTD
struct ZwmLayerStdBody {                // ZwmLayerStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> layers;    // ZWCAD's name of a layer ("ZWMLY_DATUMID_ARROW") and the layer's ("AM_5")
    ZwmStdTail tail;
};

struct ZwmLayerStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmLayerStdBody layer;
};

// ZWMLINETYPESTD
struct ZwmLineTypeStdBody {             // ZwmLineStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> linetypes; // ZWCAD's name of a linetype ("ZWMLT_PHANTOM") and the linetype's ("AM_ISO09W050")
    ZwmStdTail tail;
};

struct ZwmLineTypeStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmLineTypeStdBody linetype;
};

// ZWMNOTESTD
struct ZwmNoteProperties {
    int16_t version = 1;
    std::array<int16_t, 2> reserved_70 = {};
    std::array<double, 4> reserved_40 = {};
};

struct ZwmNoteStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmNoteProperties properties;
};

// ZWMPARTREFSTD
struct ZwmPartRefProperties {
    int16_t version = 1;
    double reserved_40_0 = 0.0;
};

struct ZwmPartRefStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmPartRefProperties properties;
};

// ZWMSECTIONSYMSTD
struct ZwmSectionSymProperties {
    int16_t version = 2;
    std::array<int16_t, 3> reserved_70_0 = {};
    std::string text_1_0;               // the label's text with its fields
    int16_t reserved_70_1 = 0;
    std::string handle_340_0 = "0";
    std::array<int16_t, 4> reserved_70_2 = {};
    std::array<double, 2> reserved_40 = {};
    std::vector<std::string> list_1_a;  // the scales
    std::vector<std::string> list_1_b;  // the identifiers
};

struct ZwmSectionSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmSectionSymProperties properties;
};

// ZWMSTANDARD: the root of the standard; it owns the other ZWM*STD records (360)
struct ZwmStandardProperties {
    int16_t version = 1;
    std::array<double, 8> reserved_40_0 = {};
    std::array<std::string, 2> handle_340 = {"0", "0"};
    std::array<int16_t, 7> reserved_70_0 = {};
    double reserved_40_1 = 0.0;
    std::vector<std::string> owned;     // 70 counts them, then each 360: the ZWM*STD records it owns
    std::vector<ZwmNameValue> names;    // 70 counts them; the names of the colors ("ZWMCL_CYAN") and their numbers
};

struct ZwmStandardData {
    ZwmObjectHeader header;
    ZwmStd standard;
    int16_t version = 1;                // ZwmStandard: 70
    ZwmSymProperties symbol;
    ZwmStandardProperties properties;
};

// ZWMSURFSYMSTD
struct ZwmSurfSymProperties {
    int16_t version = 2;
    std::array<int16_t, 6> reserved_70_0 = {};
    std::array<double, 3> reserved_40_0 = {};
    std::array<int16_t, 14> reserved_70_1 = {};
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (roughness values)
    std::vector<std::string> list_1_b;
    int16_t reserved_70_2 = 0;
    std::array<double, 2> reserved_40_1 = {};
};

struct ZwmSurfSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmSurfSymProperties properties;
};

// ZWMTAPERSLOPESTD
struct ZwmTaperSlopeProperties {
    int16_t version = 1;
    std::array<int16_t, 11> reserved_70 = {};
};

struct ZwmTaperSlopeStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmTaperSlopeProperties properties;
};

// ZWMTEXTSTYLESTD
struct ZwmTextStyleStdBody {            // ZwmTextStyleStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> styles;    // ZWCAD's name of a text style ("ZWMTS_KAITI") and the style's ("Regular_Script")
    ZwmStdTail tail;
};

struct ZwmTextStyleStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmTextStyleStdBody text_style;
};

// ZWMTITLESTYLESTD: the attribute definitions are the title block's fields
struct ZwmTitleProperties {             // ZwmTitleProperties: 70, 1 x7, 70 x2, 70 (the count), the definitions, the tail
    int16_t version = 1;
    std::array<std::string, 7> text_1;  // (the style's name twice, "", the drawing file, the xml file, "", "")
    std::array<int16_t, 2> reserved_70 = {};
    std::vector<ZwmAttributeEntry> attributes;      // 70 counts them; no key
    ZwmStdTail tail;                    // in the last definition's groups (or after the count when there is none)
};

struct ZwmTitleStyleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmTypeStd type_std;
    ZwmTitleProperties properties;
};

// ZWMVIEWDIRECTIONSTD
struct ZwmViewDirectionProperties {
    int16_t version = 1;
    std::string text_1_0;               // the label's text with its fields ("[VIEW_NAME]\P\O[VIEW_SCALE]")
    double reserved_40_0 = 0.0;
    int16_t reserved_70_0 = 0;
    std::vector<std::string> list_1_a;  // the scales
    std::vector<std::string> list_1_b;  // the identifiers
};

struct ZwmViewDirectionStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmViewDirectionProperties properties;
};

// ZWMWELDINGSTD
struct ZwmWeldingProperties {
    int16_t version = 1;
    std::string handle_340_0 = "0";
    double reserved_40_0 = 0.0;
    std::vector<ZwmIntPair> rows_a;     // 70 counts them, then 70 70 each
    std::vector<ZwmIntPair> rows_b;
    std::vector<ZwmIntPair> rows_c;
};

struct ZwmWeldingStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmWeldingProperties properties;
};

struct UnknownObjectData {
    std::string object_type;
    std::vector<std::pair<int, std::string>> group_codes;   // after the owner, before XDATA
};


// NAVISWORKSMODELDEF: the attached model's file and extents (AcDbNavisworksModelDef)
struct NavisworksModelDefData {
    int version = 1;                    // 70 (AutoCAD's core requires it before the path)
    std::string path;                   // 1: the model's path and file name (UTF-8)
    bool load = false;                  // 290: persistent load state (loaded when the file is opened)
    Point3 min;                         // 10, 20, 30: minimum extent in WCS
    Point3 max;                         // 11, 21, 31: maximum extent in WCS
    bool host_visible = true;           // 290 (the second): the host drawing's geometry is drawn
};

// RAPIDRTRENDERSETTINGS: a render preset (AcDbRenderSettings then AcDbRapidRTRenderSettings)
struct RapidRtRenderSettingsData {
    int base_version = 2;               // 90 (AcDbRenderSettings)
    std::string name;                   // 1: render preset name
    bool materials = true;              // 290
    bool texture_sampling = false;      // 290
    bool back_faces = true;             // 290
    bool shadows = true;                // 290
    std::string preview_file;           // 1 (reserved for future use)
    std::string description;            // 1
    int display_index = 0;              // 90
    int version = 3;                    // 90 (AcDbRapidRTRenderSettings)
    RenderDurationType duration_type = RenderDurationType::ByLevel;   // 70
    int levels = 5;                     // 90: levels when rendering by level
    int duration = 1;                   // 90: duration (minutes) when rendering by time
    RenderAccuracy accuracy = RenderAccuracy::Draft;                  // 70
    SamplingFilter filter = SamplingFilter::Gauss;                    // 70
    double filter_width = 3.0;          // 40
    double filter_height = 3.0;         // 40
    bool predefined = false;            // 290, last: AutoCAD writes it after the filter size (its own
                                        // file), the reference after the display index
};

// RAPIDRTRENDERENVIRONMENT: image-based lighting (AcDbIBLBackground)
struct RapidRtRenderEnvironmentData {
    int version = 2;                    // 90 (AutoCAD's core reads 1 as a premature end of the object: 2, B78)
    bool enabled = false;               // 290
    std::string ibl_file;               // 1: the IBL map's file name
    double rotation = 0.0;              // 40: degrees, -180 ... 180
    bool ibl_as_background = false;     // 290: the IBL map is the background
    std::string background_handle;      // 340: the custom background; empty = none (written as 0)
};

// ---------------------------------------------------------------------------
// Draw order, clipping and indexes (phase 9, step 9.4; plan 2026-10-27-fas-9-typer.md)
// ---------------------------------------------------------------------------

// One entity of a draw order: the entity (331) and the sort handle (5) it is given. The sort handle is
// an ordering key, not a pointer: the entities are drawn in ascending order of it, and it need not be
// the handle of any record. A pair whose entity is removed goes (the key goes with it).
struct SortEntry {
    std::string entity_handle;          // 331: soft pointer to the entity
    std::string sort_key;               // 5: the sort handle (hexadecimal text, as written)
};

// SORTENTSTABLE (AcDbSortentsTable): the draw order DRAWORDER makes of a model or paper space. Its owner is the
// ACAD_SORTENTS dictionary of the block record's extension dictionary. Entities that are not listed are drawn
// in the order of their handles.
struct SortentsTableData {
    std::string block_record_handle;    // 330 in AcDbSortentsTable: the block record (*Model_Space, *Paper_Space); empty = none
    std::vector<SortEntry> entries;     // 331 and 5, in file order
};

// SPATIAL_FILTER (AcDbFilter, AcDbSpatialFilter): the clip boundary XCLIP puts on an INSERT or an xref; its
// owner is the SPATIAL entry of the ACAD_FILTER dictionary in the INSERT's extension dictionary. An
// inverted clip is the same record with the inverted polygon; the boundary the user drew and the polygon
// AutoCAD derived from it are the XRECORD ACAD_XREC_ROUNDTRIP in the filter's own extension dictionary.
struct SpatialFilterData {
    // 10, 20: the clip boundary (in the clip coordinate system); two points are a rectangle (lower left, upper
    // right), more a polygon. 70 is their number
    std::vector<Point2> boundary;
    Point3 normal{0.0, 0.0, 1.0};       // 210, 220, 230: normal of the plane of the boundary
    Point3 origin;                      // 11, 21, 31: origin of the boundary's local coordinate system
    bool enabled = true;                // 71: clip boundary display enabled
    std::optional<double> front_distance;   // 72 (the flag) and 40: the front clipping plane
    std::optional<double> back_distance;    // 73 (the flag) and 41: the back clipping plane
    // 40 x 12: the inverse of the INSERT's transformation, a 4x3 matrix written in column major order
    std::array<double, 12> inverse_insert_transform{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0};
    // 40 x 12: the transformation into the coordinate system of the clip boundary (4x3, column major)
    std::array<double, 12> clip_transform{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0};
};

// One layer of a layer index: its name, the IDBUFFER that holds its entities (hard owner) and the
// number of entities in it. The number follows the IDBUFFER when the file is written (the live
// members of the IDBUFFER the drawing has); `count` is what is written for an IDBUFFER the drawing does not have.
struct LayerIndexEntry {
    std::string layer_name;             // 8
    std::string idbuffer_handle;        // 360: hard owner of the IDBUFFER
    int count = 0;                      // 90: the number of entries in the IDBUFFER
};

// LAYER_INDEX (AcDbIndex, AcDbLayerIndex): the entities of each layer of a space, built by AutoCAD when it
// saves a drawing with INDEXCTL 1 or 3. Its owner is the *A<n> entry of the space's ACAD_INDEX dictionary.
struct LayerIndexData {
    double timestamp = 0.0;             // 40 (AcDbIndex): Julian date of the last update
    std::vector<LayerIndexEntry> entries;
};

// The tree of a SPATIAL_INDEX as AutoCAD writes it in a DXF file (INDEXCTL 2 or 3). The reference says the index
// is written empty; AutoCAD's own files have six numbers, a length and the tree's bytes. HDxf keeps the bytes as
// they are: it does not interpret, change or make a tree.
struct SpatialIndexTree {
    std::array<double, 6> values{};     // 40 x 6 after the subclass marker (their meaning is not documented)
    int reserved = 0;                   // 90: the first of the two (0 in every AutoCAD file)
    std::vector<uint8_t> data;          // 310: the tree (the second 90 is its length)
    std::string end_marker = "END ACDBSPATIALINDEX BINARY DATA";   // 1
};

// SPATIAL_INDEX (AcDbIndex, AcDbSpatialIndex): the space's spatial index. Empty in the reference's form
// (`tree` has no value), with the tree in AutoCAD's own files.
struct SpatialIndexData {
    double timestamp = 0.0;             // 40 (AcDbIndex): Julian date of the last update
    std::optional<SpatialIndexTree> tree;
};

// IDBUFFER (AcDbIdBuffer): a list of references to objects, as many as the group 330 repeats. A layer index owns one
// for each layer (its members are the layer's entities); an empty one is as valid as a full one.
struct IdBufferData {
    std::vector<std::string> object_handles;    // 330: soft pointers
};

// LAYER_FILTER (AcDbFilter, AcDbLayerFilter): the layer names an INSERT's or xref's filter shows.
struct LayerFilterData {
    std::vector<std::string> layers;    // 8
};

// ---------------------------------------------------------------------------
// FIELD, FIELDLIST, DATATABLE, OBJECT_PTR and VBA_PROJECT (phase 9, step 9.8; plan 2026-10-27-fas-9-typer.md)
// ---------------------------------------------------------------------------

// One key of a FIELD's data set (6) and its value: the same AcValue a table cell has (TableValue), so a field
// and a table share one value model.
struct FieldKeyValue {
    std::string key;                    // 6 (ObjectPropertyId, Variable, ACFD_FIELDTEXT_CHECKSUM, ...)
    TableValue value;                   // an AcValue (DXF 2007 and later: 93 ... 304 ACVALUE_END; before: 90 type, value)
};

// FIELD (AcDbField): a field in a text, an attribute, a table cell or a dimension text. The field code (2) is what
// the user typed (`\AcVar Filename \f "%fn6"`), the evaluator (1) says who evaluates it, `cache` is the value
// the evaluator gave last time (7 ACFD_FIELD_VALUE) with its formatted text, and the evaluation state says whether
// that value is current. HDxf does not evaluate a field: the cache, the status and the error are what AutoCAD wrote
// (or what the program sets) and are written as they are.
//
// A field owns its child fields (360, hard owner: a text of two fields is a `_text` field with two children) and
// points at the objects its code names (331). A top-level field is owned by a dictionary of its text's extension
// dictionary (ACAD_FIELD) and listed in the drawing's FIELDLIST. A field removed since the file was read leaves its
// parent's child list, and the counts (90, 97) follow.
struct FieldData {
    std::string evaluator_id;           // 1: "_text", "AcVar", "AcExpr", "AcObjProp", "AcDiesel", ...
    std::string code;                   // 2 (the rest of a long code in 3)
    std::vector<std::string> child_fields;      // 90 (the count) and 360: hard owners
    std::vector<std::string> objects;           // 97 (the count) and 331: soft pointers, the code's `Object(%<\_ObjIdx 0>%)`
    // 4, DXF 2000 and 2004 only: the format of the field code (`%lu2%pr3`), as AutoCAD wrote it before 2007
    std::optional<std::string> format_pre2007;
    int32_t evaluation_option = 0;      // 91 (AcDbField's evaluation option: 63 = automatic)
    int32_t filing_option = 0;          // 92
    int32_t state = 0;                  // 94 (field state flags)
    int32_t evaluation_status = 0;      // 95 (2 = evaluated; 16 = invalid)
    int32_t error_code = 0;             // 96 (the evaluator's error: 600 for an object that is not found)
    std::string error_message;          // 300
    std::vector<FieldKeyValue> data;    // 93 (the count), then 6 and an AcValue for each
    std::string cache_key = "ACFD_FIELD_VALUE";     // 7: hard-coded in AutoCAD
    TableValue cache;                   // the AcValue after 7
    std::string format;                 // 301 (the rest of a long format in 9): the cache's text
    // 98: AutoCAD's count of the cache's characters. It is the length of 301 in most records, but not in all (AutoCAD's own
    // record of an object property has 301 empty and 98 = 7, the length of "100.000"): kept when the file's differs from 301's
    // length, otherwise written as 301's length (UTF-16 units)
    std::optional<int32_t> format_length;

    // The text the field shows as it was last evaluated: the cache's formatted text (302), or 301 where the value has none
    // (before 2007) or is not evaluated. Nothing is evaluated here: a field whose cache is empty shows nothing.
    std::string display_text() const { return !cache.text.empty() ? cache.text : format; }
};

// FIELDLIST (AcDbIdSet, AcDbFieldList): the fields of a drawing, as handles (330, soft pointers). 90 is the number of
// them: in every AutoCAD file it equals the handles listed, also when some of them are no longer there. An entry
// for a field HDxf removes goes, and 90 follows.
struct FieldListData {
    std::vector<std::string> fields;    // 330
};

// What a value of a DATATABLE's column refers to when it is an object: the group code AutoCAD writes it in.
enum class DataTableObjectKind : int {
    ObjectValue = 331,      // 331: soft pointer to the object value
    SoftPointer = 330,      // 330: soft pointer
    HardPointer = 340,      // 340: hard pointer
    SoftOwner = 350,        // 350: soft owner
    HardOwner = 360         // 360: hard owner (the round-trip cells of a table: the XRECORDs of the cells)
};

struct DataTableObject {
    DataTableObjectKind kind = DataTableObjectKind::ObjectValue;
    std::string handle;

    bool operator==(const DataTableObject& o) const { return kind == o.kind && handle == o.handle; }
};

// A 2D point (the group 10, 20, 30: AutoCAD writes three coordinates) and a 3D point (11, 21, 31) of a DATATABLE
struct DataTablePoint2D {
    Point3 point;
    bool operator==(const DataTablePoint2D& o) const { return point.x == o.point.x && point.y == o.point.y && point.z == o.point.z; }
};
struct DataTablePoint3D {
    Point3 point;
    bool operator==(const DataTablePoint3D& o) const { return point.x == o.point.x && point.y == o.point.y && point.z == o.point.z; }
};

// One value of a DATATABLE: bool (71), integer (93), double (40), text (3), 2D point (10), 3D point (11) or an object.
using DataTableValue = std::variant<bool, int32_t, double, std::string, DataTablePoint2D, DataTablePoint3D, DataTableObject>;

// The type numbers (92) AutoCAD's files show. A column of another number is read by the groups its values are in and
// kept with its number.
namespace DataColumnType {
constexpr int32_t Integer = 1;          // 93
constexpr int32_t Double = 2;           // 40
constexpr int32_t Text = 3;             // 3
constexpr int32_t Point2D = 4;          // 10, 20, 30
constexpr int32_t ObjectId = 5;         // 331
constexpr int32_t HardOwnerId = 6;      // 360
constexpr int32_t Boolean = 10;         // 71
constexpr int32_t Point3D = 11;         // 11, 21, 31
}

struct DataColumn {
    int32_t type = DataColumnType::Integer;     // 92
    std::string name;                           // 2
    std::vector<DataTableValue> values;         // one for each row
};

// DATATABLE (AcDbDataTable) and ACDBDATATABLE (DXF 2000: the same record under another name and subclass marker): a
// table of typed values by column. AutoCAD keeps every ACAD_TABLE of a DXF 2000 or 2004 file there too (the
// cells as it wrote them before 2007: a column of the cells' checksums, two of integers, and one that owns an XRECORD
// of each cell's value), and AutoCAD Map keeps its style tables in it. `columns` (90) and the number of rows (91)
// are derived from the columns when the record is written.
struct DataTableData {
    int version = 2;                    // 70
    std::string name;                   // 1
    int32_t rows = 0;                   // 91: written as the number of values of the first column when there are columns
    std::vector<DataColumn> columns;    // 90 (the count), then 92, 2 and the values of each
};

// OBJECT_PTR: the base record of an AutoCAD SQL Extension (ASE) link's object; what it carries is the ASE
// application's XDATA (1001 DC015), which is Object::xdata. Autodesk documents nothing else about it, and no
// AutoCAD file of the corpus has one: a record that has more than its head and XDATA is not typed.
struct ObjectPointerData {
};

// VBA_PROJECT (AcDbVbaProject): the drawing's embedded VBA project, as the bytes AutoCAD wrote (90 is their number,
// 310 their hex). HDxf neither reads, runs nor changes the project.
struct VbaProjectData {
    std::vector<uint8_t> project;
};

// --- 9.9 evaluation graph and dynamic block bases ---
//
// The evaluation graph (ACAD_EVALUATION_GRAPH) and the bases the dynamic block elements are made of (plan
// 2026-10-27-fas-9-typer.md, step 9.9; B109). AutoCAD's files are the only source of the group lists: the DXF reference
// does not document any of these records, so the field names are the ones the observed values suggest, and a group whose
// meaning no file shows keeps its code in its name (`flag_280`). The values AutoCAD writes in every file are the defaults.
//
// The record types the step types are the graph (EvaluationGraphData), BLOCKGRIPLOCATIONCOMPONENT, BLOCKLINEARPARAMETER,
// BLOCKLINEARGRIP, BLOCKVISIBILITYPARAMETER, BLOCKVISIBILITYGRIP, BLOCKSCALEACTION, ACDB_BLOCKREPRESENTATION_DATA and
// ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION. The element types are made of the shared parts below, one subclass each
// (AcDbEvalExpr is EvalExprData); step 9.13 builds the other parameters, grips and actions of the Block Editor from the
// same parts. By convention a data type that holds an AcDbEvalExpr has it as its member `eval`
// (Drawing::add_dynamic_block_element() sets its node id).
//
// Ids: a graph node has an `index` (its place in the graph's node list: the 91 of the node and what an edge's `from`
// and `to` are), an `id` (95: the node's own number, which stays when other nodes are removed; it is the 90 of the element
// the node owns, and what the elements' 91/92/93 … groups name) and the `object` (360: the handle of that element). A
// handle and an id are never the same thing.

// A node of the evaluation graph: 91, 93, 95, 360 and four 92 (the first and last edge into and out of the node)
struct EvalGraphNode {
    int32_t index = 0;                  // 91: the node's place in the node list
    int32_t flags = 32;                 // 93: 32 in every AutoCAD file
    int32_t id = 0;                     // 95: the node's number, the element's EvalExpr node_id (90)
    std::string object;                 // 360: the element the graph owns (a BLOCK* object); empty = none
    int32_t incoming_first = -1;        // 92: the first and last edge that ends at this node (-1 none)
    int32_t incoming_last = -1;
    int32_t outgoing_first = -1;        // 92: the first and last edge that starts at this node
    int32_t outgoing_last = -1;
};

// An edge: 92 (its index), 93, 94, 91 and 91 (the node indexes it goes from and to) and five 92: the edge before and
// after it among the edges that end at the same node, the edge before and after it among the edges that start at the same
// node, and its inverse (-1 none; AutoCAD's bidirectional pairs have flags 4 and name each other)
struct EvalGraphEdge {
    int32_t index = 0;                  // 92: the edge's place in the edge list
    int32_t flags = 0;                  // 93: 0, or 4 for the edges of a bidirectional pair
    int32_t ref_count = 1;              // 94
    int32_t from = 0;                   // 91: the index of the node the edge starts at
    int32_t to = 0;                     // 91: the index of the node it ends at
    int32_t previous_incoming = -1;     // 92
    int32_t next_incoming = -1;         // 92
    int32_t previous_outgoing = -1;     // 92
    int32_t next_outgoing = -1;         // 92
    int32_t inverse = -1;               // 92
};

// ACAD_EVALUATION_GRAPH (AcDbEvalGraph; 2000+): the dependency graph of a dynamic block (or of a solid's history, step
// 9.14), in the block's ACAD_ENHANCEDBLOCK. 96 and 97 are not counts: in AutoCAD's files both are the highest node id (1 in
// a graph with no node and in one with a single node). The node and edge lists have no counts either: the nodes come
// first (each starts with a 91), then the edges (each starts with a 92 that follows a 360 no longer).
struct EvaluationGraphData {
    int32_t last_node_id = 1;           // 96
    int32_t last_node_id_2 = 1;         // 97: equal to 96 in every AutoCAD file
    std::vector<EvalGraphNode> nodes;
    std::vector<EvalGraphEdge> edges;

    // The node with this id (the 95), or null
    const EvalGraphNode* find_node_by_id(int32_t id) const;
    // The node that owns the object with this handle (case-insensitive), or null
    const EvalGraphNode* find_node_by_object(const std::string& handle) const;
    // A node for `object`: the next id (it becomes `last_node_id`), the next index, no edges. Returns the node's id
    int32_t add_node(const std::string& object);
    // An edge from the node with index `from` to the node with index `to`, with its place in the chains of both nodes.
    // `bidirectional` adds the inverse edge too (flags 4 on both). Returns the first edge's index, -1 when an index is
    // outside the node list
    int32_t add_edge(int32_t from, int32_t to, int32_t ref_count = 1, bool bidirectional = false);
    // Removes a node and the edges that start or end at it (and an inverse that goes with them); the nodes and edges that
    // stay are numbered again (their ids are not) and every chain is made again. False when there is no such node
    bool remove_node(int32_t index);
    // The same for the node that owns `object` (what the drawing does when an element is removed)
    bool remove_object(const std::string& handle);
    // Removes one edge (and its inverse) and makes the chains again
    bool remove_edge(int32_t index);
    // Numbers nodes and edges by their place and makes every chain (the first and last edges of the nodes, the
    // previous and next edges of the edges) from the edges' `from` and `to`, in the order of the edges, as AutoCAD's
    // graphs have them. The inverse links stay as they are
    void relink();
    // Whether the chains are what relink() makes of the edges
    bool links_consistent() const;
};

// AcDbBlockElement: the part every dynamic block parameter, grip and action has after its AcDbEvalExpr
struct BlockElementData {
    std::string name;                   // 300: "Linear", "End Grip", "Visibility State"
    // 98 and 99 are the element's class version and build, which AutoCAD writes after the DXF version it saves: 25 and 104 in
    // 2000 and 2004, 33 and 327 from 2007 (AutoCAD 2025's files; 329 in the ACadSharp samples). Absent (a record built
    // in memory): the values of the version it is saved as
    std::optional<int32_t> version;     // 98
    std::optional<int32_t> maintenance; // 99
    int32_t flags = 0;                  // 1071 (class data, not XDATA): 0 in every AutoCAD file
};

// AcDbBlockParameter
struct BlockParameterData {
    int16_t show_properties = 1;        // 280: 1 in every AutoCAD file
    int16_t chain_actions = 0;          // 281: 0 in every AutoCAD file
};

// A grip's or an action's link to a property of a parameter: the id of the node it is connected to and the property's name
struct BlockPropertyLink {
    int32_t node_id = 0;                // 91 … 95 and 92 … 96 by the slot
    std::string name;                   // 301 … 305: "DisplacementX", "UpdatedBaseX"
};

// AcDbBlock1PtParameter: a parameter with one point. Its two slots are present (flag 170/171 not 0) with the
// connected node's id and the property's name (91/301, 92/302), or absent (a flag 0 and nothing after it)
struct Block1PtParameterData {
    Point3 position;                    // 1010
    int32_t grip_node_id = 0;           // 93: the node id of the grip
    std::array<std::optional<BlockPropertyLink>, 2> links;
};

// AcDbBlock2PtParameter: a parameter with two points. 170 is the number of 91 (grip node ids; 4 in every AutoCAD file);
// its four property slots are present or absent like the 1Pt ones (flags 171 … 174, ids 92 … 95, names 301 … 304)
struct Block2PtParameterData {
    Point3 first;                       // 1010
    Point3 second;                      // 1011
    std::vector<int32_t> grip_node_ids; // 170 (the count), 91
    std::array<std::optional<BlockPropertyLink>, 4> links;
    int16_t flag_177 = 0;               // 177
};

// AcDbBlockGrip: the node ids of the two components that hold the grip's location (BLOCKGRIPLOCATIONCOMPONENT) and where
// the grip is
struct BlockGripData {
    int32_t x_node_id = 0;              // 91
    int32_t y_node_id = 0;              // 92
    Point3 location;                    // 1010 (class data)
    int16_t flag_280 = 1;               // 280: 1 for linear, alignment and XY grips, 0 for rotation, flip and visibility
    int32_t value_93 = -1;              // 93: -1 in every AutoCAD file
};

// AcDbBlockAction: the graph nodes the action is connected to (91, counted by 70), the entities it works on (330, counted by
// 71) and where its glyph is. 70 is the number of 91 (step 9.13 found lists of them; step 9.9's files all had none)
struct BlockActionData {
    std::vector<int32_t> node_ids;      // 70 (the count), 91: node ids of the graph (the actions and parameters it is connected to)
    std::vector<std::string> selection; // 71 (the count), 330: handles of entities in the block (soft pointers)
    Point3 location;                    // 1010 (class data)
};

// AcDbBlockActionWithBasePt: an action with a base point; the two components 92/93 name hold its X and Y
struct BlockActionBasePointData {
    int32_t x_node_id = 0;              // 92
    int32_t y_node_id = 0;              // 93
    std::string x_name;                 // 301: "UpdatedBaseX"
    std::string y_name;                 // 302
    Point3 base_point;                  // 1011
    int16_t flag_280 = 1;               // 280
    Point3 point_1012;                  // 1012
};

// BLOCKGRIPLOCATIONCOMPONENT (AcDbEvalExpr, AcDbBlockGripExpr): one coordinate of a grip's location, which the graph
// computes from a property of a parameter. The expression is "" and its value the coordinate (a real: tag 40; a flip
// state is an int16: tag 70)
struct BlockGripLocationData {
    EvalExprData eval;
    int32_t parameter_node_id = 0;      // 91: the node id of the parameter whose property it is
    std::string property_name;          // 300: "UpdatedBaseX", "UpdatedEndY", "UpdatedFlip"
};

// BLOCKLINEARPARAMETER (AcDbBlockLinearParameter after the 2Pt parameter). The value set's list (175 and the groups 144) is
// typed since step 9.13: AutoCAD 2025's dynamic block samples have lists in the linear parameters and the linear constraints
struct BlockLinearParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string label;                  // 305: the distance's name ("HDXF_Length")
    std::string description;            // 306
    double label_offset = 0.0;          // 140
    std::string expression;             // 307
    int32_t value_set_flags = 1;        // 96
    std::array<double, 3> constraints = {0.0, 0.0, 0.0};    // 141, 142, 143
    std::vector<double> values;         // 175 (the count), 144: the list of values the parameter can have (step 9.13)
};

// BLOCKLINEARGRIP (AcDbBlockLinearGrip): the grip's movement vector (140, 141, 142: not its location, which is 1010)
struct BlockLinearGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
    Point3 vector;                      // 140, 141, 142
};

// One visibility state of a BLOCKVISIBILITYPARAMETER: its name, the entities that are visible in it (332, counted by 94)
// and the elements (parameters, grips, actions) it applies to (333, counted by 95). A state can have none of either
struct BlockVisibilityState {
    std::string name;                   // 303
    std::vector<std::string> entities;  // 94 (the count), 332: handles of entities in the block
    std::vector<std::string> elements;  // 95 (the count), 333: handles of BLOCK* objects in the graph
};

// BLOCKVISIBILITYPARAMETER (AcDbBlockVisibilityParameter after the 1Pt parameter)
struct BlockVisibilityParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    int16_t flag_281 = 1;               // 281
    std::string name;                   // 301
    std::string description;            // 302
    int32_t value_91 = 0;               // 91
    std::vector<std::string> entities;  // 93 (the count), 331: the entities that have visibility states (soft pointers)
    std::vector<BlockVisibilityState> states;   // 92 (the count), then each state
};

// BLOCKVISIBILITYGRIP (AcDbBlockVisibilityGrip: no group of its own)
struct BlockVisibilityGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKSCALEACTION (AcDbBlockScaleAction after the action with a base point): the node ids and names of the three
// properties it sets (94/303 Scale, 95/304 XScale, 96/305 YScale)
struct BlockScaleActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    BlockActionBasePointData base_point_action;
    std::array<BlockPropertyLink, 3> properties;
};

// ACDB_BLOCKREPRESENTATION_DATA (AcDbBlockRepresentationData; in an INSERT's AcDbBlockRepresentation/AcDbRepData): the
// anonymous block's link back to the dynamic block it is a representation of
struct BlockRepresentationData {
    int16_t version = 1;                // 70
    std::string original_block_record;  // 340: the handle of the original block's BLOCK_RECORD (a hard pointer)
};

// ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION (AcDbDynamicBlockPurgePreventer): in the block record's extension dictionary
// under AcDbDynamicBlockRoundTripPurgePreventer; keeps AutoCAD's PURGE from removing a block a dynamic block needs
struct DynamicBlockPurgePreventerData {
    int16_t version = 1;                // 70
};
// --- end 9.9 ---

// --- 9.13 dynamic blocks: the remaining parameters, grips, actions and constraints ---
//
// Step 9.13 of phase 9 (plan 2026-10-27-fas-9-typer.md, F9.9; B109): the Block Editor's other record types, each with its own
// data type made of the shared parts of step 9.9 (BlockElementData, BlockParameterData, Block1PtParameterData,
// Block2PtParameterData, BlockGripData, BlockActionData, BlockActionBasePointData) and the fields of its own subclass. The groups
// are what AutoCAD 2025's own dynamic block libraries have (the DWG samples of AutoCAD's Sample/Dynamic Blocks, saved by the core
// as DXF 2000 … 2018 in ASCII and binary: doc/facit/dyntypes, script fas9_dynamic_types.scr); the DXF reference documents none of
// them. A field without a role the files show has the code of its group in its name (`flag_280`, `text_309`). By the convention of
// step 9.9 a type that holds an AcDbEvalExpr has it as its member `eval`; the node ids a record holds are graph ids, never handles.
//
// Not typed, because no AutoCAD file has the record and no group list can be derived from its siblings: BLOCKANGULARCONSTRAINTPARAMETER,
// BLOCKANGULARCONSTRAINTPARAMETERENTITY, BLOCKDIAMETRICCONSTRAINTPARAMETER (registered in CLASSES or named by the plan only). The
// two constraint parameters that follow from a sibling's chain (BLOCKALIGNEDCONSTRAINTPARAMETER, BLOCKLINEARCONSTRAINTPARAMETER)
// are typed from it and are not verified by an AutoCAD record.

// A parameter's value set: the flags (96 or 97), three limits (minimum, maximum, increment) and a list of values. In the distance
// family (linear, the linear constraints, radial) the values are the groups 144 after the count 175; no file shows the group of
// a value in the angle and the XY/polar parameters, whose sets are read only when they have no value
struct BlockValueSet {
    int32_t flags = 0;                          // 96 / 97: 0 none, 1 list, 4 increment … as AutoCAD sets them
    std::array<double, 3> limits = {0.0, 0.0, 0.0};   // 141 142 143 (145 146 147, 146 147 148, 142 143 144 in the others)
    std::vector<double> values;                 // 175/176 (the count) and the values: 144
};

// The own part of the distance family (the linear parameter's AcDbBlockLinearParameter after the 2Pt parameter, the linear
// constraints', the radial constraint's): its labels, the label offset, the expression and the value set
struct BlockDistancePart {
    std::string label;                          // 305
    std::string description;                    // 306
    double label_offset = 0.0;                  // 140
    std::string expression;                     // 307
    BlockValueSet value_set;                    // 96, 141 142 143, 175, 144
};

// BLOCKALIGNMENTPARAMETER (AcDbBlockAlignmentParameter after the 2Pt parameter)
struct BlockAlignmentParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    int16_t flag_280 = 1;               // 280
};

// BLOCKALIGNMENTGRIP (AcDbBlockAlignmentGrip): the grip's movement vector (140 141 142)
struct BlockAlignmentGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
    Point3 vector;
};

// BLOCKBASEPOINTPARAMETER (AcDbBlockBasepointParameter after the 1Pt parameter)
struct BlockBasepointParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    Point3 point_1011;
    Point3 point_1012;
};

// BLOCKFLIPPARAMETER (AcDbBlockFlipParameter after the 2Pt parameter)
struct BlockFlipParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string label;                  // 305: "Side"
    std::string description;            // 306: "Flips the symbol"
    std::string text_307;               // "Other Side"
    std::string text_308;               // "Arrow Side"
    Point3 point_1012;
    std::string property_name;          // 309: "UpdatedFlip"
    int32_t flag_96 = 0;                // 96
};

// BLOCKFLIPGRIP (AcDbBlockFlipGrip): the movement vector (140 141 142) and a node id (93)
struct BlockFlipGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
    Point3 vector;
    int32_t value_93 = 0;               // 93 after the vector
};

// BLOCKFLIPACTION (AcDbBlockFlipAction): four property links (the ids 92 … 95, the names 301 … 304: "Flip", "UpdatedFlip",
// "UpdatedBase", "UpdatedEnd")
struct BlockFlipActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 4> links;
};

// BLOCKLOOKUPPARAMETER (AcDbBlockLookUpParameter after the 1Pt parameter)
struct BlockLookupParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    std::string label;                  // 303: "Size"
    std::string description;            // 304: "Select the nut size"
    int32_t action_node_id = 0;         // 94: the node id of the lookup action
};

// BLOCKLOOKUPGRIP (AcDbBlockLookUpGrip: no group of its own)
struct BlockLookupGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// One column of a lookup table: the property of a parameter whose value the row decides (303 94 95 96 282 305 281 304)
struct BlockLookupColumn {
    std::string name;                   // 303
    int32_t node_id = 0;                // 94: the node id of the parameter
    int32_t value_type = 40;            // 95: the type of the values (AcDbEvalVariant: 40 real, 1 text)
    int32_t unit_type = 0;              // 96: 2 for a distance, 0 none
    int16_t flag_282 = 0;               // 282: 1 for the column that is looked up, 0 for the inputs
    std::string category = "Custom";    // 305
    int16_t flag_281 = 0;               // 281
    std::string property_name;          // 304: "UpdatedDistance", "lookupString", "VisibilityState"
};

// BLOCKLOOKUPACTION (AcDbBlockLookupAction after the action): the lookup table. 92 is the number of rows and 93 the number of
// columns; the 302 are the cells row by row, every row with one cell per column
struct BlockLookupActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::string text_301;               // 301: "" in every file
    std::vector<std::vector<std::string>> rows;     // 92 (the count), 302 × the columns
    std::vector<BlockLookupColumn> columns;         // 93 (the count)
    int16_t flag_280 = 1;               // 280
};

// BLOCKMOVEACTION (AcDbBlockMoveAction): the two property links (92/301 and 93/302: "XDelta"/"YDelta" or "EndXDelta"/"EndYDelta"
// …), the distance multiplier and the angle offset of the action, and a flag
struct BlockMoveActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 2> links;     // 92 301 / 93 302
    double distance_multiplier = 1.0;   // 140
    double angle_offset = 0.0;          // 141: radians
    int16_t flag_280 = 0;               // 280
};

// BLOCKPOINTPARAMETER (AcDbBlockPointParameter after the 1Pt parameter)
struct BlockPointParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    std::string label;                  // 303: "Position"
    std::string description;            // 304
    Point3 point_1011;
};

// BLOCKPOLARPARAMETER (AcDbBlockPolarParameter after the 2Pt parameter): the distance and the angle with a labelled value set
// each; the value sets are the groups 96 … 175 and 97 … 176
struct BlockPolarParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string distance_label;         // 305: "Length"
    std::string distance_description;   // 306
    std::string angle_label;            // 307: "Angle"
    std::string angle_description;      // 308
    double label_offset = 0.0;          // 140
    std::string text_309;               // 309
    BlockValueSet set_96;               // 96, 141 142 143, 175
    std::string text_410;               // 410
    BlockValueSet set_97;               // 97, 145 146 147, 176
};

// BLOCKPOLARGRIP (AcDbBlockPolarGrip: no group of its own)
struct BlockPolarGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKPOLARSTRETCHACTION (AcDbBlockPolarStretchAction after the action): six property links (92/301 … 97/306), the frame (73
// and its 2D points 1011), the entities (74, 332), and the groups 72, 75, 76, 77, 78, 140 and 141 whose roles no file shows
struct BlockPolarStretchActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 6> links;     // 92 301, 93 302, 94 303, 95 304, 96 305, 97 306
    std::vector<Point2> frame;          // 73 (the count), 1011
    int16_t flag_72 = 0;                // 72
    std::vector<std::string> entities;  // 74 (the count), 332: entities of the block (soft pointers)
    int16_t value_75 = 0;               // 75
    int16_t value_76 = 0;               // 76
    int16_t value_78 = 0;               // 78
    double real_141 = 1.0;              // 141
    double real_140 = 0.0;              // 140
    int16_t flag_77 = 0;                // 77
};

// A value of a properties table: AutoCAD's AcDbEvalVariant as the table writes it — the tag (170: 40 a real in 140, 1 a text in
// 300, -9999 none)
struct BlockTableValue {
    int16_t tag = -9999;                // 170
    double real = 0.0;                  // 140
    std::string text;                   // 300
};

// A column of a properties table: the parameter whose property it holds (340) and the column's own groups
struct BlockTableColumn {
    std::string parameter;              // 340: the parameter (a hard pointer; "0" none)
    int16_t flag_170 = 0;               // 170
    int16_t flag_171 = -1;              // 171
    std::string text_300;               // 300
    std::string property_name;          // 301: "UserVariable", "UpdatedDistance"
    int32_t node_id = 0;                // 90: the node id of the parameter
    int16_t type_170 = -9999;           // 170
    BlockTableValue value;              // 170, 300 | 140
    bool flag_290 = false;              // 290
    bool flag_291 = true;               // 291
    bool flag_292 = true;               // 292
    bool flag_293 = false;              // 293
    bool flag_294 = false;              // 294
    std::string text_302;               // 302
    std::string pointer_340 = "0";      // 340
};

// A row of a properties table: its number (90) and one value for each column
struct BlockTableRow {
    int32_t index = 0;                  // 90
    std::vector<BlockTableValue> values;
};

// BLOCKPROPERTIESTABLE (AcDbBlockPropertiesTable after the 1Pt parameter): the lookup table of a block table. 91 is the number
// of columns, 92 the number of rows
struct BlockPropertiesTableData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    int32_t version = 2;                // 90
    std::string name;                   // 300: "Block Table1"
    std::string description;            // 301
    std::vector<BlockTableColumn> columns;
    std::vector<BlockTableRow> rows;
    int32_t value_93 = 0;               // 93
    bool flag_290 = false;              // 290
    bool flag_291 = true;               // 291
    bool flag_292 = false;              // 292
};

// BLOCKPROPERTIESTABLEGRIP (no subclass of its own: AcDbBlockGrip is the last)
struct BlockPropertiesTableGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKROTATIONPARAMETER (AcDbBlockRotationParameter after the 2Pt parameter)
struct BlockRotationParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string label;                  // 305: "Arrow Rotation"
    std::string description;            // 306
    Point3 point_1011;
    double label_offset = 0.0;          // 140
    std::string expression;             // 307
    BlockValueSet value_set;            // 96, 141 142 143, 175: the limits are angles in radians
};

// BLOCKROTATIONGRIP (AcDbBlockRotationGrip: no group of its own)
struct BlockRotationGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKROTATEACTION (AcDbBlockRotationAction after the action with a base point): the property link 94/303 ("AngleDelta")
struct BlockRotateActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    BlockActionBasePointData base_point_action;
    BlockPropertyLink angle;            // 94 303
};

// What a stretch action holds for one entity of the block: the entity (331, a soft pointer) and the indexes (94) of its vertices
// the action moves (74 counts them)
struct BlockStretchEntity {
    std::string entity;                 // 331
    std::vector<int32_t> vertices;      // 74 (the count), 94
};

// What a stretch action holds for another action it is connected to: that action's node id (95) and the vertex indexes (76 counts
// the 94)
struct BlockStretchConnection {
    int32_t node_id = 0;                // 95
    std::vector<int32_t> vertices;      // 76 (the count), 94
};

// BLOCKSTRETCHACTION (AcDbBlockStretchAction after the action): the links 92/301 and 93/302, the stretch frame (72 and its 2D
// points 1011), the entities (73), the connections (75), the distance multiplier (140), the angle offset (141) and a flag (280)
struct BlockStretchActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 2> links;     // 92 301 / 93 302: "EndXDelta" / "EndYDelta"
    std::vector<Point2> frame;          // 72 (the count), 1011
    std::vector<BlockStretchEntity> entities;           // 73 (the count)
    std::vector<BlockStretchConnection> connections;    // 75 (the count)
    double distance_multiplier = 1.0;   // 140
    double angle_offset = 0.0;          // 141: radians
    int16_t flag_280 = 0;               // 280
};

// BLOCKUSERPARAMETER (AcDbBlockUserParameter after the 1Pt parameter)
struct BlockUserParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    int32_t value_90 = 0;               // 90
    std::string variable;               // 330: the ACDBASSOCVARIABLE that holds the value (a soft pointer; "0" none)
    std::string name;                   // 301
    std::string text_309 = "AcDbEvalVariant NULL = -9999";    // 309
    int16_t parameter_type = 0;         // 170
};

// BLOCKXYPARAMETER (AcDbBlockXYParameter after the 2Pt parameter): two labelled distances, each with a value set
struct BlockXYParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string first_label;            // 305: "Height"
    std::string second_label;           // 306: "Width"
    std::string first_description;      // 307
    std::string second_description;     // 308
    double offset_141 = 0.0;            // 141
    double offset_140 = 0.0;            // 140
    std::string text_309;               // 309
    BlockValueSet set_97;               // 97, 146 147 148, 176
    std::string text_410;               // 410
    BlockValueSet set_96;               // 96, 142 143 144, 175
};

// BLOCKXYGRIP (AcDbBlockXYGrip: no group of its own)
struct BlockXYGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKARRAYACTION (AcDbBlockArrayAction after the action): four property links (92 … 95, 301 … 304: "Base", "End",
// "UpdatedBase", "UpdatedEnd") and the two spacings (140, 141)
struct BlockArrayActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 4> links;
    double value_140 = 1.0;             // 140
    double value_141 = 1.0;             // 141
};

// The constraint parameters (a dimensional constraint of a dynamic block: AcDbBlockConstraintParameter after the 2Pt parameter,
// then the distance part). `dependency` (330) is the ACDBASSOCDEPENDENCY of the constraint (a soft pointer; the dependency
// also has the parameter as its object and is one of the parameter's reactors)
struct BlockHorizontalConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";       // 330
    BlockDistancePart distance;         // AcDbBlockLinearConstraintParameter
};
struct BlockVerticalConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};
// (typed from the chain of the horizontal and vertical ones: AutoCAD's samples have no aligned constraint)
struct BlockAlignedConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};
// (the linear constraint itself, the base of the horizontal, vertical and aligned ones: no AutoCAD sample has a record of it)
struct BlockLinearConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};
// BLOCKRADIALCONSTRAINTPARAMETER (AcDbBlockRadialConstraintParameter after the constraint parameter: the distance part)
struct BlockRadialConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};

// BLOCKPARAMDEPENDENCYBODY: the body of the ACDBASSOCDEPENDENCY of a constraint parameter (AcDbAssocDependencyBody 90, then
// AcDbImpAssocDimDependencyBodyBase 90 and 1, then AcDbBlockParameterDependencyBody 90). It has no AcDbEvalExpr
struct BlockParameterDependencyBodyData {
    int32_t body_version = 1;           // AcDbAssocDependencyBody 90
    int32_t base_version = 1;           // AcDbImpAssocDimDependencyBodyBase 90
    std::string expression;             // 1: "Head=7.18", the parameter's name and value
    int32_t value_90 = 0;               // AcDbBlockParameterDependencyBody 90
};

// ACDB_DYNAMICBLOCKPROXYNODE (AcDbEvalExpr only): a node of the evaluation graph that stands for an object of an application
// that is not loaded
struct DynamicBlockProxyNodeData {
    EvalExprData eval;
};
// --- end 9.13 ---

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

// --- 9.11 render, lights and sun (plan 2026-10-27-fas-9-typer.md, step 9.11) ---

// AcDbRenderSettings: the base of every render preset (the RENDERSETTINGS record by itself, the base of the MentalRay
// preset). AutoCAD's files write the four flags as 290 (the reference's older table has the second as 90) and the
// version 2 (the reference says 1).
struct RenderSettingsData {
    int32_t version = 2;                // 90
    std::string name;                   // 1: the preset's name
    bool materials = true;              // 290
    bool texture_sampling = true;       // 290
    bool back_faces = true;             // 290
    bool shadows = true;                // 290
    std::string preview_file;           // 1 (can be blank)
    std::string description;            // 1
    int32_t display_index = 0;          // 90
    // The reference puts the predefined flag here (290 after the display index); AutoCAD's own files have it at the end of
    // the RapidRT subclass and none in the MentalRay record. Present only where the record has it
    std::optional<bool> predefined;
};

// MENTALRAYRENDERSETTINGS: AcDbRenderSettings then AcDbMentalRayRenderSettings (from AutoCAD 2007; the feature set is gone
// from AutoCAD 2016 but the data stays in the file). The groups are by position; the reference's names are the comments.
struct MentalRayRenderSettingsData {
    RenderSettingsData base;
    int32_t version = 2;                // 90
    int32_t min_samples = 0;            // 90: sampling rate (minimum)
    int32_t max_samples = 1;            // 90: sampling rate (maximum)
    SamplingFilter filter = SamplingFilter::Gauss;   // 70
    double filter_width = 3.0;          // 40
    double filter_height = 3.0;         // 40
    std::array<double, 4> contrast = {0.05, 0.05, 0.05, 0.05};   // 40 x 4: the sampling contrast color's red, green, blue, alpha
    int16_t shadow_mode = 0;            // 70: 0 simple, 1 sort, 2 segment
    bool shadow_maps = false;           // 290
    bool ray_tracing = true;            // 290
    int32_t trace_reflections = 5;      // 90
    int32_t trace_refractions = 5;      // 90
    int32_t trace_max = 5;              // 90
    bool global_illumination = false;   // 290
    int32_t photons_per_sample = 500;   // 90
    bool use_radius = false;            // 290: the global illumination radius flag
    double radius = 1.0;                // 40
    int32_t photons_per_light = 10000;  // 90
    int32_t photon_reflections = 5;     // 90
    int32_t photon_refractions = 5;     // 90
    int32_t photon_max = 5;             // 90
    bool final_gather = false;          // 290
    int32_t final_gather_rays = 200;    // 90
    bool use_min_radius = false;        // 290
    bool use_max_radius = false;        // 290
    bool radius_in_pixels = false;      // 290
    double min_radius = 1.0;            // 40
    double max_radius = 1.0;            // 40
    double luminance_scale = 1500.0;    // 40: the energy multiplier
    int16_t diagnostic_mode = 0;        // 70: 0 off, 1 grid, 2 photon, 4 BSP
    int16_t grid_mode = 0;              // 70: 0 object, 1 world, 2 camera
    double grid_size = 10.0;            // 40
    int16_t photon_mode = 0;            // 70: 0 density, 1 irradiance
    int16_t bsp_mode = 0;               // 70: 0 depth, 1 size
    bool export_statistics = false;     // 290
    std::string export_file;            // 1 (can be blank)
    int32_t tile_size = 32;             // 90
    int16_t tile_order = 0;             // 70: 0 Hilbert, 1 spiral, 2 left to right, 3 right to left, 4 top to bottom, 5 bottom to top
    int32_t memory_limit = 1048;        // 90
    // The last two groups of AutoCAD's own files (290 and 40, after the memory limit) that the reference's table lacks:
    // their meaning is not documented, so they are named by their place
    bool extra_flag = false;            // 290
    double extra_factor = 1.0;          // 40
};

// RENDERENVIRONMENT (AcDbRenderEnvironment; the root dictionary's ACAD_RENDER_ENVIRONMENT)
struct RenderEnvironmentData {
    int32_t version = 1;                // 90
    bool fog_enabled = false;           // 290
    bool fog_background = false;        // 290
    std::array<uint8_t, 3> fog_color = {0, 0, 0};   // 280 x 3: red, green, blue
    double near_density = 0.0;          // 40 (a percentage)
    double far_density = 100.0;         // 40
    double near_distance = 0.0;         // 40 (a percentage of the distance to the far clipping plane)
    double far_distance = 100.0;        // 40
    bool use_image = false;             // 290: the environment image flag
    std::string image_file;             // 1 (blank when the flag is 0)
};

// RENDERGLOBAL (AcDbRenderGlobal; the root dictionary's ACAD_RENDER_GLOBAL)
struct RenderGlobalData {
    int32_t version = 2;                // 90
    int32_t procedure = 0;              // 90: 0 view, 1 crop, 2 selection
    int32_t destination = 0;            // 90: 0 render window, 1 viewport
    bool save_to_file = false;          // 290
    std::string file_name;              // 1: the rendered image's file
    int32_t width = 640;                // 90
    int32_t height = 480;               // 90
    bool predefined_first = false;      // 290
    bool high_info = false;             // 290
};

// LIGHTLIST (AcDbLightList; the root dictionary's ACAD_LIGHT): the drawing's lights. Each member's 5 is the LIGHT entity's
// handle (a pointer: the list's own handle is the record's 5), the 1 its name.
struct LightListEntry {
    std::string light_handle;           // 5
    std::string name;                   // 1
};

struct LightListData {
    int32_t version = 1;                // 90
    std::vector<LightListEntry> lights; // 90 (the count) then 5 and 1 for each
};

// SUNSTUDY (AcDbSunStudy): the Sun Study wizard's setup. Groups as the reference has them; no AutoCAD file has one
// (the core has no command), so the whole record is ej testat. The hour is an integer: the reference gives it the code 290
// (a flag in the binary format), and HDxf writes and reads the number as it is, never as a flag.
struct SunStudyDate {
    int32_t julian_day = 0;             // 90
    int32_t seconds = 0;                // 90: seconds past midnight
};

struct SunStudyData {
    int32_t version = 1;                // 90
    std::string name;                   // 1: the sun setup's name
    std::string description;            // 2
    int16_t output_type = 0;            // 70
    // Included only when the output type is a sheet set
    std::optional<std::string> sheet_set_name;      // 3
    std::optional<bool> use_subset;                 // 290
    std::optional<std::string> sheet_subset_name;   // 4
    bool select_dates = false;          // 291: select dates from the calendar
    std::vector<SunStudyDate> dates;    // 91 (the count) then 90, 90 for each date picked
    bool select_range = false;          // 292: select a range of dates
    // Included only when the range flag is set
    std::optional<int32_t> start_time;  // 93
    std::optional<int32_t> end_time;    // 94
    std::optional<int32_t> interval;    // 95: seconds
    std::vector<int32_t> hours;         // 73 (the count) then 290 for each
    std::string page_setup_handle;      // 340: hard pointer; empty = none (written as 0)
    std::string view_handle;            // 341
    std::string visual_style_handle;    // 342
    int16_t shade_plot_type = 0;        // 74
    int16_t viewports_per_page = 1;     // 75
    int16_t rows = 1;                   // 76
    int16_t columns = 1;                // 77
    double spacing = 0.0;               // 40
    bool lock_viewports = false;        // 293
    bool label_viewports = false;       // 294
    std::optional<std::string> text_style_handle;   // 343 (2013's reference; not in 2007's)
};

using ObjectDataVariant = std::variant<
    DictionaryData,
    DictionaryWithDefaultData,
    XRecordData,
    GroupData,
    LayoutData,
    PlotSettings,
    DictionaryVarData,
    PlaceholderData,
    MlineStyleData,
    ImageDefData,
    ImageDefReactorData,
    RasterVariablesData,
    DbColorData,
    MLeaderStyleData,
    GeoDataData,
    WipeoutVariablesData,
    ScaleData,
    SunData,
    NavisworksModelDefData,
    UnderlayDefinitionData,         // 9.6: PDFDEFINITION, DWFDEFINITION, DGNDEFINITION
    // --- 9.9 evaluation graph and dynamic block bases ---
    EvaluationGraphData,
    Boxed<BlockGripLocationData>,
    Boxed<BlockLinearParameterData>,
    Boxed<BlockLinearGripData>,
    Boxed<BlockVisibilityParameterData>,
    Boxed<BlockVisibilityGripData>,
    Boxed<BlockScaleActionData>,
    BlockRepresentationData,
    DynamicBlockPurgePreventerData,
    RapidRtRenderSettingsData,
    RapidRtRenderEnvironmentData,
    // --- 9.3 OLE and proxy classes ---
    ProxyObjectData,
    UnknownObjectData,
    // Phase 9, step 9.4
    SortentsTableData,
    SpatialFilterData,
    LayerIndexData,
    SpatialIndexData,
    IdBufferData,
    LayerFilterData,
    // Phase 9, step 9.1
    Boxed<TableStyleData>,
    Boxed<CellStyleMapData>,
    Boxed<TableContentData>,
    Boxed<TableGeometryData>,
    // Phase 9, step 9.8 (fields and document data)
    Boxed<FieldData>,
    FieldListData,
    Boxed<DataTableData>,
    ObjectPointerData,
    VbaProjectData,
    // Phase 9, step 9.7
    Boxed<MaterialData>,
    Boxed<VisualStyleData>,
    // Phase 9, step 9.11
    Boxed<RenderSettingsData>,
    Boxed<MentalRayRenderSettingsData>,
    Boxed<RenderEnvironmentData>,
    Boxed<RenderGlobalData>,
    Boxed<LightListData>,
    Boxed<SunStudyData>,
    // Phase 9, step 9.10 (ACDBASSOC framework)
    Boxed<AssocActionData>,
    Boxed<AssocNetworkData>,
    Boxed<AssocDependencyData>,
    Boxed<AssocGeomDependencyData>,
    Boxed<AssocPersSubentManagerData>,
    Boxed<PersSubentManagerData>,
    Boxed<AssocPathActionParamData>,
    Boxed<AssocVertexActionParamData>,
    Boxed<AssocOsnapPointRefActionParamData>,
    Boxed<AssocEdgeActionParamData>,
    Boxed<AssocExtrudedSurfaceActionBodyData>,
    Boxed<AssocLoftedSurfaceActionBodyData>,
    Boxed<AssocRevolvedSurfaceActionBodyData>,
    Boxed<AssocSweptSurfaceActionBodyData>,
    Boxed<AssocPlaneSurfaceActionBodyData>,
    Boxed<AssocAlignedDimActionBodyData>,
    // Phase 9, step 9.14 (solid history)
    Boxed<ShHistoryData>,
    Boxed<ShBoxData>,
    Boxed<ShWedgeData>,
    Boxed<ShSphereData>,
    Boxed<ShCylinderData>,
    Boxed<ShConeData>,
    Boxed<ShTorusData>,
    Boxed<ShPyramidData>,
    Boxed<ShExtrusionData>,
    Boxed<ShSweepData>,
    Boxed<ShRevolveData>,
    Boxed<ShLoftData>,
    Boxed<ShBooleanData>,
    Boxed<ShBrepData>,
    Boxed<ShChamferData>,
    Boxed<ShFilletData>,
    // Phase 9, step 9.15 (other ACDBASSOC classes)
    Boxed<AssocVariableData>,
    Boxed<AssocValueDependencyData>,
    Boxed<AssocDimDependencyBodyData>,
    Boxed<Assoc2dConstraintGroupData>,
    Boxed<AssocOffsetSurfaceActionBodyData>,
    Boxed<AssocTrimSurfaceActionBodyData>,
    Boxed<AssocPatchSurfaceActionBodyData>,
    Boxed<AssocNetworkSurfaceActionBodyData>,
    Boxed<AssocBlendSurfaceActionBodyData>,
    Boxed<AssocExtendSurfaceActionBodyData>,
    Boxed<AssocFilletSurfaceActionBodyData>,
    Boxed<AssocEdgeFilletChamferActionBodyData>,
    Boxed<AssocRestoreEntityStateActionBodyData>,
    Boxed<AssocImpliedSurfaceOrSolidActionBodyData>,
    Boxed<AssocRotatedDimActionBodyData>,
    Boxed<Assoc3PointAngularDimActionBodyData>,
    Boxed<AssocOrdinateDimActionBodyData>,
    Boxed<AssocDiametricDimActionBodyData>,
    Boxed<AssocRadialDimActionBodyData>,
    Boxed<AssocMLeaderActionBodyData>,
    Boxed<AssocEntityCloneActionBodyData>,
    Boxed<AssocAsmBodyActionParamData>,
    Boxed<AssocFaceActionParamData>,
    Boxed<AssocTrimmingPathActionParamData>,
    Boxed<AssocTrimmingBodyActionParamData>,
    Boxed<AssocCompoundActionParamData>,
    Boxed<AssocObjectActionParamData>,
    Boxed<AssocArrayActionBodyData>,
    Boxed<AssocArrayModifyActionBodyData>,
    // Phase 9, step 9.12 (the view styles replace ModelDocViewStyleData here: it is their base)
    Boxed<DetailViewStyleData>,
    Boxed<SectionViewStyleData>,
    Boxed<SectionSettingsData>,
    SectionManagerData,
    // Phase 9, step 9.17 (annotation contexts and DIMASSOC)
    Boxed<TextContextData>,
    Boxed<LeaderContextData>,
    Boxed<MTextContextData>,
    Boxed<MTextAttributeContextData>,
    Boxed<MLeaderContextData>,
    Boxed<BlkRefContextData>,
    Boxed<AlDimContextData>,
    Boxed<AngDimContextData>,
    Boxed<DmDimContextData>,
    Boxed<OrdDimContextData>,
    Boxed<RadimContextData>,
    Boxed<RadimLgContextData>,
    Boxed<FcfContextData>,
    Boxed<AnnotScaleContextData>,
    Boxed<HatchScaleContextData>,
    Boxed<HatchViewContextData>,
    Boxed<DimAssocData>,
    // Phase 9, step 9.13 (the remaining dynamic block records)
    Boxed<BlockAlignmentParameterData>,
    Boxed<BlockAlignmentGripData>,
    Boxed<BlockBasepointParameterData>,
    Boxed<BlockFlipParameterData>,
    Boxed<BlockFlipGripData>,
    Boxed<BlockFlipActionData>,
    Boxed<BlockLookupParameterData>,
    Boxed<BlockLookupGripData>,
    Boxed<BlockLookupActionData>,
    Boxed<BlockMoveActionData>,
    Boxed<BlockPointParameterData>,
    Boxed<BlockPolarParameterData>,
    Boxed<BlockPolarGripData>,
    Boxed<BlockPolarStretchActionData>,
    Boxed<BlockPropertiesTableData>,
    Boxed<BlockPropertiesTableGripData>,
    Boxed<BlockRotationParameterData>,
    Boxed<BlockRotationGripData>,
    Boxed<BlockRotateActionData>,
    Boxed<BlockStretchActionData>,
    Boxed<BlockUserParameterData>,
    Boxed<BlockXYParameterData>,
    Boxed<BlockXYGripData>,
    Boxed<BlockArrayActionData>,
    Boxed<BlockHorizontalConstraintParameterData>,
    Boxed<BlockVerticalConstraintParameterData>,
    Boxed<BlockAlignedConstraintParameterData>,
    Boxed<BlockLinearConstraintParameterData>,
    Boxed<BlockRadialConstraintParameterData>,
    Boxed<BlockParameterDependencyBodyData>,
    Boxed<DynamicBlockProxyNodeData>,
    // Phase 9, step 9.18 (Map, TC and ZWM)
    Boxed<MapManagerData>,
    Boxed<MapStyleLibraryData>,
    Boxed<MapStyleCategoryData>,
    Boxed<MapDisplayMapData>,
    Boxed<MapBaseElementData>,
    Boxed<MapDefaultStyleData>,
    Boxed<MapStyleReferenceData>,
    Boxed<MapSurrogateStyleSetsData>,
    Boxed<MapStylizedEntitiesTableData>,
    Boxed<MapMetadataData>,
    Boxed<MapResourceManagerData>,
    Boxed<TcOptionalDataData>,
    Boxed<ZwmBalloonStdData>,
    Boxed<ZwmBomStyleStdData>,
    Boxed<ZwmBreakSymStdData>,
    Boxed<ZwmCenterHoleStdData>,
    Boxed<ZwmChamferStdData>,
    Boxed<ZwmCircleMarkStdData>,
    Boxed<ZwmColorStdData>,
    Boxed<ZwmDatumIdStdData>,
    Boxed<ZwmDatumTargetStdData>,
    Boxed<ZwmDetailStdData>,
    Boxed<ZwmDimStyleStdData>,
    Boxed<ZwmElevSymStdData>,
    Boxed<ZwmFCFrameStdData>,
    Boxed<ZwmFeatureIdStdData>,
    Boxed<ZwmFrameStyleStdData>,
    Boxed<ZwmHoleChartStdData>,
    Boxed<ZwmLayerStdData>,
    Boxed<ZwmLineTypeStdData>,
    Boxed<ZwmNoteStdData>,
    Boxed<ZwmPartRefStdData>,
    Boxed<ZwmSectionSymStdData>,
    Boxed<ZwmStandardData>,
    Boxed<ZwmSurfSymStdData>,
    Boxed<ZwmTaperSlopeStdData>,
    Boxed<ZwmTextStyleStdData>,
    Boxed<ZwmTitleStyleStdData>,
    Boxed<ZwmViewDirectionStdData>,
    Boxed<ZwmWeldingStdData>
>;

// ObjectData's alternative for a value of type T: the one std::variant's converting constructor takes (SomeData's is
// Boxed<SomeData> when only that is an alternative)
template <typename T>
struct ObjectDataPick {
    static T pick(T);
};

template <typename V>
struct ObjectDataPicks;

template <typename... Ts>
struct ObjectDataPicks<std::variant<Ts...>> : ObjectDataPick<Ts>... {
    using ObjectDataPick<Ts>::pick...;
};

// std::in_place_type / std::in_place_index, which ObjectData takes as std::variant does
template <typename T>
struct ObjectDataInPlace : std::false_type {};
template <typename T>
struct ObjectDataInPlace<std::in_place_type_t<T>> : std::true_type {};
template <std::size_t I>
struct ObjectDataInPlace<std::in_place_index_t<I>> : std::true_type {};

template <typename T>
using ObjectDataPicked = decltype(ObjectDataPicks<ObjectDataVariant>::pick(std::declval<T>()));

template <typename T, typename... Ts>
consteval std::size_t object_data_index_in(const std::variant<Ts...>*) {
    constexpr bool same[] = {std::is_same_v<T, Ts>...};
    for (std::size_t i = 0; i < sizeof...(Ts); ++i) {
        if (same[i]) return i;
    }
    return sizeof...(Ts);
}

// The index of the alternative T in ObjectData
template <typename T>
inline constexpr std::size_t object_data_index = object_data_index_in<T>(static_cast<const ObjectDataVariant*>(nullptr));

// The data of an object: a std::variant whose copying, moving, destruction, construction and access go through
// object_data.cpp. With some 200 alternatives, libstdc++ writes out its nested helpers for each in every file that uses
// them, with symbol names that grow as the cube of the number of alternatives (objects of 750 MB that ld cannot link).
// std::get, std::get_if, std::holds_alternative and std::visit take it as the std::variant it is; get<T>() and
// get_if<T>() are the same without those helpers. It is assigned as a whole (`data = SomeData{...}` converts first).
struct ObjectData : ObjectDataVariant {
    ObjectData() = default;

    // From an alternative or a value that converts to one, as std::variant converts it. The alternative is made here
    // and moved in by object_data.cpp, so that nothing of the variant itself is made in the caller's file
    template <typename T>
        requires (!std::is_same_v<std::remove_cvref_t<T>, ObjectData>) && (!ObjectDataInPlace<std::remove_cvref_t<T>>::value) &&
                 std::is_constructible_v<ObjectDataVariant, T>
    ObjectData(T&& value)
        : ObjectData(object_data_index<ObjectDataPicked<T>>, address_of(ObjectDataPicked<T>(std::forward<T>(value)))) {}

    // In place, as std::variant constructs (these are made in the caller's file)
    template <typename T, typename... Args>
    explicit ObjectData(std::in_place_type_t<T> tag, Args&&... args) : ObjectDataVariant(tag, std::forward<Args>(args)...) {}
    template <std::size_t I, typename... Args>
    explicit ObjectData(std::in_place_index_t<I> tag, Args&&... args) : ObjectDataVariant(tag, std::forward<Args>(args)...) {}

    ObjectData(const ObjectData& other);
    ObjectData(ObjectData&& other) noexcept;
    ObjectData& operator=(const ObjectData& other);
    ObjectData& operator=(ObjectData&& other) noexcept;
    ~ObjectData();

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
        return const_cast<T*>(static_cast<const ObjectData*>(this)->get_if<T>());
    }
    template <typename T>
    const T* get_if() const noexcept {
        static_assert(object_data_index<T> < std::variant_size_v<ObjectDataVariant>, "not an alternative of ObjectData");
        return index() == object_data_index<T> ? static_cast<const T*>(held()) : nullptr;
    }

    // The address of the alternative held (the Boxed<T> itself for a boxed one); null when an exception left it valueless
    const void* held() const noexcept;

private:
    // Holds the alternative at index, moved from *value
    ObjectData(std::size_t index, void* value);

    template <typename A>
    static void* address_of(A&& made) { return &made; }
};

// A proxy object's class data (ProxyObjectData::class_data): the class's type and its data as that type has it — the
// subclasses of the class's own record (TCOPTIONALDATA: `type` ObjectType::TcOptionalData, `data` Boxed<TcOptionalDataData>)
struct ProxyObjectClass {
    ObjectType type = ObjectType::Unknown;
    ObjectData data;
};

struct Object {
    ObjectType type = ObjectType::Unknown;
    std::string handle;                 // 5
    std::string owner;                  // 330 before the first subclass; "0" for none
    std::vector<std::string> reactors;  // 330 in 102 {ACAD_REACTORS
    std::string xdictionary;            // 360 in 102 {ACAD_XDICTIONARY
    ObjectData data;
    std::vector<XdataList> xdata;
    RawRecord raw;                      // the record as read

    DictionaryData& as_dictionary()             { return data.get<DictionaryData>(); }
    const DictionaryData& as_dictionary() const { return data.get<DictionaryData>(); }

    DictionaryWithDefaultData& as_dictionary_with_default()
        { return data.get<DictionaryWithDefaultData>(); }
    const DictionaryWithDefaultData& as_dictionary_with_default() const
        { return data.get<DictionaryWithDefaultData>(); }

    XRecordData& as_xrecord()                   { return data.get<XRecordData>(); }
    const XRecordData& as_xrecord() const       { return data.get<XRecordData>(); }

    GroupData& as_group()                       { return data.get<GroupData>(); }
    const GroupData& as_group() const           { return data.get<GroupData>(); }

    // The AcDbModelDocViewStyle part of a detail or section view style
    ModelDocViewStyleData& as_view_style() {
        if (auto* d = data.get_if<Boxed<DetailViewStyleData>>()) return (**d).base;
        return (*data.get<Boxed<SectionViewStyleData>>()).base;
    }
    const ModelDocViewStyleData& as_view_style() const {
        if (auto* d = data.get_if<Boxed<DetailViewStyleData>>()) return (**d).base;
        return (*data.get<Boxed<SectionViewStyleData>>()).base;
    }
    DetailViewStyleData& as_detail_view_style() { return *data.get<Boxed<DetailViewStyleData>>(); }
    const DetailViewStyleData& as_detail_view_style() const { return *data.get<Boxed<DetailViewStyleData>>(); }
    SectionViewStyleData& as_section_view_style() { return *data.get<Boxed<SectionViewStyleData>>(); }
    const SectionViewStyleData& as_section_view_style() const { return *data.get<Boxed<SectionViewStyleData>>(); }

    SectionSettingsData& as_section_settings() { return *data.get<Boxed<SectionSettingsData>>(); }
    const SectionSettingsData& as_section_settings() const { return *data.get<Boxed<SectionSettingsData>>(); }
    // Phase 9, step 9.18: AutoCAD Map, TurboCAD and ZWCAD records
    MapManagerData& as_map_manager() { return *data.get<Boxed<MapManagerData>>(); }
    const MapManagerData& as_map_manager() const { return *data.get<Boxed<MapManagerData>>(); }
    MapStyleLibraryData& as_map_style_library() { return *data.get<Boxed<MapStyleLibraryData>>(); }
    const MapStyleLibraryData& as_map_style_library() const { return *data.get<Boxed<MapStyleLibraryData>>(); }
    MapStyleCategoryData& as_map_style_category() { return *data.get<Boxed<MapStyleCategoryData>>(); }
    const MapStyleCategoryData& as_map_style_category() const { return *data.get<Boxed<MapStyleCategoryData>>(); }
    MapDisplayMapData& as_map_display_map() { return *data.get<Boxed<MapDisplayMapData>>(); }
    const MapDisplayMapData& as_map_display_map() const { return *data.get<Boxed<MapDisplayMapData>>(); }
    MapBaseElementData& as_map_base_element() { return *data.get<Boxed<MapBaseElementData>>(); }
    const MapBaseElementData& as_map_base_element() const { return *data.get<Boxed<MapBaseElementData>>(); }
    MapDefaultStyleData& as_map_default_style() { return *data.get<Boxed<MapDefaultStyleData>>(); }
    const MapDefaultStyleData& as_map_default_style() const { return *data.get<Boxed<MapDefaultStyleData>>(); }
    MapStyleReferenceData& as_map_style_reference() { return *data.get<Boxed<MapStyleReferenceData>>(); }
    const MapStyleReferenceData& as_map_style_reference() const { return *data.get<Boxed<MapStyleReferenceData>>(); }
    MapSurrogateStyleSetsData& as_map_surrogate_style_sets() { return *data.get<Boxed<MapSurrogateStyleSetsData>>(); }
    const MapSurrogateStyleSetsData& as_map_surrogate_style_sets() const { return *data.get<Boxed<MapSurrogateStyleSetsData>>(); }
    MapStylizedEntitiesTableData& as_map_stylized_entities_table() { return *data.get<Boxed<MapStylizedEntitiesTableData>>(); }
    const MapStylizedEntitiesTableData& as_map_stylized_entities_table() const { return *data.get<Boxed<MapStylizedEntitiesTableData>>(); }
    MapMetadataData& as_map_metadata() { return *data.get<Boxed<MapMetadataData>>(); }
    const MapMetadataData& as_map_metadata() const { return *data.get<Boxed<MapMetadataData>>(); }
    MapResourceManagerData& as_map_resource_manager() { return *data.get<Boxed<MapResourceManagerData>>(); }
    const MapResourceManagerData& as_map_resource_manager() const { return *data.get<Boxed<MapResourceManagerData>>(); }
    TcOptionalDataData& as_tc_optional_data() { return *data.get<Boxed<TcOptionalDataData>>(); }
    const TcOptionalDataData& as_tc_optional_data() const { return *data.get<Boxed<TcOptionalDataData>>(); }
    ZwmBalloonStdData& as_zwm_balloon_std() { return *data.get<Boxed<ZwmBalloonStdData>>(); }
    const ZwmBalloonStdData& as_zwm_balloon_std() const { return *data.get<Boxed<ZwmBalloonStdData>>(); }
    ZwmBomStyleStdData& as_zwm_bom_style_std() { return *data.get<Boxed<ZwmBomStyleStdData>>(); }
    const ZwmBomStyleStdData& as_zwm_bom_style_std() const { return *data.get<Boxed<ZwmBomStyleStdData>>(); }
    ZwmBreakSymStdData& as_zwm_break_sym_std() { return *data.get<Boxed<ZwmBreakSymStdData>>(); }
    const ZwmBreakSymStdData& as_zwm_break_sym_std() const { return *data.get<Boxed<ZwmBreakSymStdData>>(); }
    ZwmCenterHoleStdData& as_zwm_center_hole_std() { return *data.get<Boxed<ZwmCenterHoleStdData>>(); }
    const ZwmCenterHoleStdData& as_zwm_center_hole_std() const { return *data.get<Boxed<ZwmCenterHoleStdData>>(); }
    ZwmChamferStdData& as_zwm_chamfer_std() { return *data.get<Boxed<ZwmChamferStdData>>(); }
    const ZwmChamferStdData& as_zwm_chamfer_std() const { return *data.get<Boxed<ZwmChamferStdData>>(); }
    ZwmCircleMarkStdData& as_zwm_circle_mark_std() { return *data.get<Boxed<ZwmCircleMarkStdData>>(); }
    const ZwmCircleMarkStdData& as_zwm_circle_mark_std() const { return *data.get<Boxed<ZwmCircleMarkStdData>>(); }
    ZwmColorStdData& as_zwm_color_std() { return *data.get<Boxed<ZwmColorStdData>>(); }
    const ZwmColorStdData& as_zwm_color_std() const { return *data.get<Boxed<ZwmColorStdData>>(); }
    ZwmDatumIdStdData& as_zwm_datum_id_std() { return *data.get<Boxed<ZwmDatumIdStdData>>(); }
    const ZwmDatumIdStdData& as_zwm_datum_id_std() const { return *data.get<Boxed<ZwmDatumIdStdData>>(); }
    ZwmDatumTargetStdData& as_zwm_datum_target_std() { return *data.get<Boxed<ZwmDatumTargetStdData>>(); }
    const ZwmDatumTargetStdData& as_zwm_datum_target_std() const { return *data.get<Boxed<ZwmDatumTargetStdData>>(); }
    ZwmDetailStdData& as_zwm_detail_std() { return *data.get<Boxed<ZwmDetailStdData>>(); }
    const ZwmDetailStdData& as_zwm_detail_std() const { return *data.get<Boxed<ZwmDetailStdData>>(); }
    ZwmDimStyleStdData& as_zwm_dim_style_std() { return *data.get<Boxed<ZwmDimStyleStdData>>(); }
    const ZwmDimStyleStdData& as_zwm_dim_style_std() const { return *data.get<Boxed<ZwmDimStyleStdData>>(); }
    ZwmElevSymStdData& as_zwm_elev_sym_std() { return *data.get<Boxed<ZwmElevSymStdData>>(); }
    const ZwmElevSymStdData& as_zwm_elev_sym_std() const { return *data.get<Boxed<ZwmElevSymStdData>>(); }
    ZwmFCFrameStdData& as_zwm_fc_frame_std() { return *data.get<Boxed<ZwmFCFrameStdData>>(); }
    const ZwmFCFrameStdData& as_zwm_fc_frame_std() const { return *data.get<Boxed<ZwmFCFrameStdData>>(); }
    ZwmFeatureIdStdData& as_zwm_feature_id_std() { return *data.get<Boxed<ZwmFeatureIdStdData>>(); }
    const ZwmFeatureIdStdData& as_zwm_feature_id_std() const { return *data.get<Boxed<ZwmFeatureIdStdData>>(); }
    ZwmFrameStyleStdData& as_zwm_frame_style_std() { return *data.get<Boxed<ZwmFrameStyleStdData>>(); }
    const ZwmFrameStyleStdData& as_zwm_frame_style_std() const { return *data.get<Boxed<ZwmFrameStyleStdData>>(); }
    ZwmHoleChartStdData& as_zwm_hole_chart_std() { return *data.get<Boxed<ZwmHoleChartStdData>>(); }
    const ZwmHoleChartStdData& as_zwm_hole_chart_std() const { return *data.get<Boxed<ZwmHoleChartStdData>>(); }
    ZwmLayerStdData& as_zwm_layer_std() { return *data.get<Boxed<ZwmLayerStdData>>(); }
    const ZwmLayerStdData& as_zwm_layer_std() const { return *data.get<Boxed<ZwmLayerStdData>>(); }
    ZwmLineTypeStdData& as_zwm_line_type_std() { return *data.get<Boxed<ZwmLineTypeStdData>>(); }
    const ZwmLineTypeStdData& as_zwm_line_type_std() const { return *data.get<Boxed<ZwmLineTypeStdData>>(); }
    ZwmNoteStdData& as_zwm_note_std() { return *data.get<Boxed<ZwmNoteStdData>>(); }
    const ZwmNoteStdData& as_zwm_note_std() const { return *data.get<Boxed<ZwmNoteStdData>>(); }
    ZwmPartRefStdData& as_zwm_part_ref_std() { return *data.get<Boxed<ZwmPartRefStdData>>(); }
    const ZwmPartRefStdData& as_zwm_part_ref_std() const { return *data.get<Boxed<ZwmPartRefStdData>>(); }
    ZwmSectionSymStdData& as_zwm_section_sym_std() { return *data.get<Boxed<ZwmSectionSymStdData>>(); }
    const ZwmSectionSymStdData& as_zwm_section_sym_std() const { return *data.get<Boxed<ZwmSectionSymStdData>>(); }
    ZwmStandardData& as_zwm_standard() { return *data.get<Boxed<ZwmStandardData>>(); }
    const ZwmStandardData& as_zwm_standard() const { return *data.get<Boxed<ZwmStandardData>>(); }
    ZwmSurfSymStdData& as_zwm_surf_sym_std() { return *data.get<Boxed<ZwmSurfSymStdData>>(); }
    const ZwmSurfSymStdData& as_zwm_surf_sym_std() const { return *data.get<Boxed<ZwmSurfSymStdData>>(); }
    ZwmTaperSlopeStdData& as_zwm_taper_slope_std() { return *data.get<Boxed<ZwmTaperSlopeStdData>>(); }
    const ZwmTaperSlopeStdData& as_zwm_taper_slope_std() const { return *data.get<Boxed<ZwmTaperSlopeStdData>>(); }
    ZwmTextStyleStdData& as_zwm_text_style_std() { return *data.get<Boxed<ZwmTextStyleStdData>>(); }
    const ZwmTextStyleStdData& as_zwm_text_style_std() const { return *data.get<Boxed<ZwmTextStyleStdData>>(); }
    ZwmTitleStyleStdData& as_zwm_title_style_std() { return *data.get<Boxed<ZwmTitleStyleStdData>>(); }
    const ZwmTitleStyleStdData& as_zwm_title_style_std() const { return *data.get<Boxed<ZwmTitleStyleStdData>>(); }
    ZwmViewDirectionStdData& as_zwm_view_direction_std() { return *data.get<Boxed<ZwmViewDirectionStdData>>(); }
    const ZwmViewDirectionStdData& as_zwm_view_direction_std() const { return *data.get<Boxed<ZwmViewDirectionStdData>>(); }
    ZwmWeldingStdData& as_zwm_welding_std() { return *data.get<Boxed<ZwmWeldingStdData>>(); }
    const ZwmWeldingStdData& as_zwm_welding_std() const { return *data.get<Boxed<ZwmWeldingStdData>>(); }
    SectionManagerData& as_section_manager() { return data.get<SectionManagerData>(); }
    const SectionManagerData& as_section_manager() const { return data.get<SectionManagerData>(); }

    // Phase 9, step 9.17: annotation contexts and DIMASSOC
    TextContextData& as_text_context() { return *data.get<Boxed<TextContextData>>(); }
    const TextContextData& as_text_context() const { return *data.get<Boxed<TextContextData>>(); }
    LeaderContextData& as_leader_context() { return *data.get<Boxed<LeaderContextData>>(); }
    const LeaderContextData& as_leader_context() const { return *data.get<Boxed<LeaderContextData>>(); }
    MTextContextData& as_mtext_context() { return *data.get<Boxed<MTextContextData>>(); }
    const MTextContextData& as_mtext_context() const { return *data.get<Boxed<MTextContextData>>(); }
    MTextAttributeContextData& as_mtext_attribute_context() { return *data.get<Boxed<MTextAttributeContextData>>(); }
    const MTextAttributeContextData& as_mtext_attribute_context() const { return *data.get<Boxed<MTextAttributeContextData>>(); }
    MLeaderContextData& as_mleader_context() { return *data.get<Boxed<MLeaderContextData>>(); }
    const MLeaderContextData& as_mleader_context() const { return *data.get<Boxed<MLeaderContextData>>(); }
    BlkRefContextData& as_blkref_context() { return *data.get<Boxed<BlkRefContextData>>(); }
    const BlkRefContextData& as_blkref_context() const { return *data.get<Boxed<BlkRefContextData>>(); }
    AlDimContextData& as_aldim_context() { return *data.get<Boxed<AlDimContextData>>(); }
    const AlDimContextData& as_aldim_context() const { return *data.get<Boxed<AlDimContextData>>(); }
    AngDimContextData& as_angdim_context() { return *data.get<Boxed<AngDimContextData>>(); }
    const AngDimContextData& as_angdim_context() const { return *data.get<Boxed<AngDimContextData>>(); }
    DmDimContextData& as_dmdim_context() { return *data.get<Boxed<DmDimContextData>>(); }
    const DmDimContextData& as_dmdim_context() const { return *data.get<Boxed<DmDimContextData>>(); }
    OrdDimContextData& as_orddim_context() { return *data.get<Boxed<OrdDimContextData>>(); }
    const OrdDimContextData& as_orddim_context() const { return *data.get<Boxed<OrdDimContextData>>(); }
    RadimContextData& as_radim_context() { return *data.get<Boxed<RadimContextData>>(); }
    const RadimContextData& as_radim_context() const { return *data.get<Boxed<RadimContextData>>(); }
    RadimLgContextData& as_radimlg_context() { return *data.get<Boxed<RadimLgContextData>>(); }
    const RadimLgContextData& as_radimlg_context() const { return *data.get<Boxed<RadimLgContextData>>(); }
    FcfContextData& as_fcf_context() { return *data.get<Boxed<FcfContextData>>(); }
    const FcfContextData& as_fcf_context() const { return *data.get<Boxed<FcfContextData>>(); }
    AnnotScaleContextData& as_annot_scale_context() { return *data.get<Boxed<AnnotScaleContextData>>(); }
    const AnnotScaleContextData& as_annot_scale_context() const { return *data.get<Boxed<AnnotScaleContextData>>(); }
    HatchScaleContextData& as_hatch_scale_context() { return *data.get<Boxed<HatchScaleContextData>>(); }
    const HatchScaleContextData& as_hatch_scale_context() const { return *data.get<Boxed<HatchScaleContextData>>(); }
    HatchViewContextData& as_hatch_view_context() { return *data.get<Boxed<HatchViewContextData>>(); }
    const HatchViewContextData& as_hatch_view_context() const { return *data.get<Boxed<HatchViewContextData>>(); }
    DimAssocData& as_dim_assoc() { return *data.get<Boxed<DimAssocData>>(); }
    const DimAssocData& as_dim_assoc() const { return *data.get<Boxed<DimAssocData>>(); }
    // The AcDbObjectContextData + AcDbAnnotScaleObjectContextData part of any of the 16 context objects; null for another object
    ObjectContextBase* context_base();
    const ObjectContextBase* context_base() const;
    // Phase 9, step 9.13: the remaining dynamic block parameters, grips, actions and constraints
    BlockAlignmentParameterData& as_block_alignment_parameter() { return *data.get<Boxed<BlockAlignmentParameterData>>(); }
    const BlockAlignmentParameterData& as_block_alignment_parameter() const { return *data.get<Boxed<BlockAlignmentParameterData>>(); }
    BlockAlignmentGripData& as_block_alignment_grip() { return *data.get<Boxed<BlockAlignmentGripData>>(); }
    const BlockAlignmentGripData& as_block_alignment_grip() const { return *data.get<Boxed<BlockAlignmentGripData>>(); }
    BlockBasepointParameterData& as_block_basepoint_parameter() { return *data.get<Boxed<BlockBasepointParameterData>>(); }
    const BlockBasepointParameterData& as_block_basepoint_parameter() const { return *data.get<Boxed<BlockBasepointParameterData>>(); }
    BlockFlipParameterData& as_block_flip_parameter() { return *data.get<Boxed<BlockFlipParameterData>>(); }
    const BlockFlipParameterData& as_block_flip_parameter() const { return *data.get<Boxed<BlockFlipParameterData>>(); }
    BlockFlipGripData& as_block_flip_grip() { return *data.get<Boxed<BlockFlipGripData>>(); }
    const BlockFlipGripData& as_block_flip_grip() const { return *data.get<Boxed<BlockFlipGripData>>(); }
    BlockFlipActionData& as_block_flip_action() { return *data.get<Boxed<BlockFlipActionData>>(); }
    const BlockFlipActionData& as_block_flip_action() const { return *data.get<Boxed<BlockFlipActionData>>(); }
    BlockLookupParameterData& as_block_lookup_parameter() { return *data.get<Boxed<BlockLookupParameterData>>(); }
    const BlockLookupParameterData& as_block_lookup_parameter() const { return *data.get<Boxed<BlockLookupParameterData>>(); }
    BlockLookupGripData& as_block_lookup_grip() { return *data.get<Boxed<BlockLookupGripData>>(); }
    const BlockLookupGripData& as_block_lookup_grip() const { return *data.get<Boxed<BlockLookupGripData>>(); }
    BlockLookupActionData& as_block_lookup_action() { return *data.get<Boxed<BlockLookupActionData>>(); }
    const BlockLookupActionData& as_block_lookup_action() const { return *data.get<Boxed<BlockLookupActionData>>(); }
    BlockMoveActionData& as_block_move_action() { return *data.get<Boxed<BlockMoveActionData>>(); }
    const BlockMoveActionData& as_block_move_action() const { return *data.get<Boxed<BlockMoveActionData>>(); }
    BlockPointParameterData& as_block_point_parameter() { return *data.get<Boxed<BlockPointParameterData>>(); }
    const BlockPointParameterData& as_block_point_parameter() const { return *data.get<Boxed<BlockPointParameterData>>(); }
    BlockPolarParameterData& as_block_polar_parameter() { return *data.get<Boxed<BlockPolarParameterData>>(); }
    const BlockPolarParameterData& as_block_polar_parameter() const { return *data.get<Boxed<BlockPolarParameterData>>(); }
    BlockPolarGripData& as_block_polar_grip() { return *data.get<Boxed<BlockPolarGripData>>(); }
    const BlockPolarGripData& as_block_polar_grip() const { return *data.get<Boxed<BlockPolarGripData>>(); }
    BlockPolarStretchActionData& as_block_polar_stretch_action() { return *data.get<Boxed<BlockPolarStretchActionData>>(); }
    const BlockPolarStretchActionData& as_block_polar_stretch_action() const { return *data.get<Boxed<BlockPolarStretchActionData>>(); }
    BlockPropertiesTableData& as_block_properties_table() { return *data.get<Boxed<BlockPropertiesTableData>>(); }
    const BlockPropertiesTableData& as_block_properties_table() const { return *data.get<Boxed<BlockPropertiesTableData>>(); }
    BlockPropertiesTableGripData& as_block_properties_table_grip() { return *data.get<Boxed<BlockPropertiesTableGripData>>(); }
    const BlockPropertiesTableGripData& as_block_properties_table_grip() const { return *data.get<Boxed<BlockPropertiesTableGripData>>(); }
    BlockRotationParameterData& as_block_rotation_parameter() { return *data.get<Boxed<BlockRotationParameterData>>(); }
    const BlockRotationParameterData& as_block_rotation_parameter() const { return *data.get<Boxed<BlockRotationParameterData>>(); }
    BlockRotationGripData& as_block_rotation_grip() { return *data.get<Boxed<BlockRotationGripData>>(); }
    const BlockRotationGripData& as_block_rotation_grip() const { return *data.get<Boxed<BlockRotationGripData>>(); }
    BlockRotateActionData& as_block_rotate_action() { return *data.get<Boxed<BlockRotateActionData>>(); }
    const BlockRotateActionData& as_block_rotate_action() const { return *data.get<Boxed<BlockRotateActionData>>(); }
    BlockStretchActionData& as_block_stretch_action() { return *data.get<Boxed<BlockStretchActionData>>(); }
    const BlockStretchActionData& as_block_stretch_action() const { return *data.get<Boxed<BlockStretchActionData>>(); }
    BlockUserParameterData& as_block_user_parameter() { return *data.get<Boxed<BlockUserParameterData>>(); }
    const BlockUserParameterData& as_block_user_parameter() const { return *data.get<Boxed<BlockUserParameterData>>(); }
    BlockXYParameterData& as_block_xy_parameter() { return *data.get<Boxed<BlockXYParameterData>>(); }
    const BlockXYParameterData& as_block_xy_parameter() const { return *data.get<Boxed<BlockXYParameterData>>(); }
    BlockXYGripData& as_block_xy_grip() { return *data.get<Boxed<BlockXYGripData>>(); }
    const BlockXYGripData& as_block_xy_grip() const { return *data.get<Boxed<BlockXYGripData>>(); }
    BlockArrayActionData& as_block_array_action() { return *data.get<Boxed<BlockArrayActionData>>(); }
    const BlockArrayActionData& as_block_array_action() const { return *data.get<Boxed<BlockArrayActionData>>(); }
    BlockHorizontalConstraintParameterData& as_block_horizontal_constraint_parameter() { return *data.get<Boxed<BlockHorizontalConstraintParameterData>>(); }
    const BlockHorizontalConstraintParameterData& as_block_horizontal_constraint_parameter() const { return *data.get<Boxed<BlockHorizontalConstraintParameterData>>(); }
    BlockVerticalConstraintParameterData& as_block_vertical_constraint_parameter() { return *data.get<Boxed<BlockVerticalConstraintParameterData>>(); }
    const BlockVerticalConstraintParameterData& as_block_vertical_constraint_parameter() const { return *data.get<Boxed<BlockVerticalConstraintParameterData>>(); }
    BlockAlignedConstraintParameterData& as_block_aligned_constraint_parameter() { return *data.get<Boxed<BlockAlignedConstraintParameterData>>(); }
    const BlockAlignedConstraintParameterData& as_block_aligned_constraint_parameter() const { return *data.get<Boxed<BlockAlignedConstraintParameterData>>(); }
    BlockLinearConstraintParameterData& as_block_linear_constraint_parameter() { return *data.get<Boxed<BlockLinearConstraintParameterData>>(); }
    const BlockLinearConstraintParameterData& as_block_linear_constraint_parameter() const { return *data.get<Boxed<BlockLinearConstraintParameterData>>(); }
    BlockRadialConstraintParameterData& as_block_radial_constraint_parameter() { return *data.get<Boxed<BlockRadialConstraintParameterData>>(); }
    const BlockRadialConstraintParameterData& as_block_radial_constraint_parameter() const { return *data.get<Boxed<BlockRadialConstraintParameterData>>(); }
    BlockParameterDependencyBodyData& as_block_parameter_dependency_body() { return *data.get<Boxed<BlockParameterDependencyBodyData>>(); }
    const BlockParameterDependencyBodyData& as_block_parameter_dependency_body() const { return *data.get<Boxed<BlockParameterDependencyBodyData>>(); }
    DynamicBlockProxyNodeData& as_dynamic_block_proxy_node() { return *data.get<Boxed<DynamicBlockProxyNodeData>>(); }
    const DynamicBlockProxyNodeData& as_dynamic_block_proxy_node() const { return *data.get<Boxed<DynamicBlockProxyNodeData>>(); }

    LayoutData& as_layout()                     { return data.get<LayoutData>(); }
    const LayoutData& as_layout() const         { return data.get<LayoutData>(); }

    PlotSettings& as_plot_settings()            { return data.get<PlotSettings>(); }
    const PlotSettings& as_plot_settings() const { return data.get<PlotSettings>(); }

    DictionaryVarData& as_dictionary_var()      { return data.get<DictionaryVarData>(); }
    const DictionaryVarData& as_dictionary_var() const
        { return data.get<DictionaryVarData>(); }

    MlineStyleData& as_mline_style()            { return data.get<MlineStyleData>(); }
    const MlineStyleData& as_mline_style() const { return data.get<MlineStyleData>(); }

    ImageDefData& as_image_def()                { return data.get<ImageDefData>(); }
    const ImageDefData& as_image_def() const    { return data.get<ImageDefData>(); }

    ImageDefReactorData& as_image_def_reactor() { return data.get<ImageDefReactorData>(); }
    const ImageDefReactorData& as_image_def_reactor() const
        { return data.get<ImageDefReactorData>(); }

    RasterVariablesData& as_raster_variables()  { return data.get<RasterVariablesData>(); }
    const RasterVariablesData& as_raster_variables() const
        { return data.get<RasterVariablesData>(); }

    DbColorData& as_db_color()                  { return data.get<DbColorData>(); }
    const DbColorData& as_db_color() const      { return data.get<DbColorData>(); }

    MLeaderStyleData& as_mleader_style()        { return data.get<MLeaderStyleData>(); }
    const MLeaderStyleData& as_mleader_style() const { return data.get<MLeaderStyleData>(); }

    GeoDataData& as_geodata()                   { return data.get<GeoDataData>(); }
    const GeoDataData& as_geodata() const       { return data.get<GeoDataData>(); }

    WipeoutVariablesData& as_wipeout_variables()             { return data.get<WipeoutVariablesData>(); }
    const WipeoutVariablesData& as_wipeout_variables() const { return data.get<WipeoutVariablesData>(); }

    ScaleData& as_scale()                       { return data.get<ScaleData>(); }
    const ScaleData& as_scale() const           { return data.get<ScaleData>(); }
    SunData& as_sun()                           { return data.get<SunData>(); }
    const SunData& as_sun() const               { return data.get<SunData>(); }

    NavisworksModelDefData& as_navisworks_model_def() { return data.get<NavisworksModelDefData>(); }
    const NavisworksModelDefData& as_navisworks_model_def() const { return data.get<NavisworksModelDefData>(); }
    // PDFDEFINITION, DWFDEFINITION, DGNDEFINITION (9.6)
    UnderlayDefinitionData& as_underlay_definition() { return data.get<UnderlayDefinitionData>(); }
    const UnderlayDefinitionData& as_underlay_definition() const { return data.get<UnderlayDefinitionData>(); }
    RapidRtRenderSettingsData& as_rapidrt_settings() { return data.get<RapidRtRenderSettingsData>(); }
    const RapidRtRenderSettingsData& as_rapidrt_settings() const { return data.get<RapidRtRenderSettingsData>(); }
    RapidRtRenderEnvironmentData& as_rapidrt_environment() { return data.get<RapidRtRenderEnvironmentData>(); }
    const RapidRtRenderEnvironmentData& as_rapidrt_environment() const { return data.get<RapidRtRenderEnvironmentData>(); }

    // ACAD_PROXY_OBJECT (9.3)
    ProxyObjectData& as_proxy_object()          { return data.get<ProxyObjectData>(); }
    const ProxyObjectData& as_proxy_object() const { return data.get<ProxyObjectData>(); }
    // TABLESTYLE, CELLSTYLEMAP, TABLECONTENT, TABLEGEOMETRY
    TableStyleData& as_table_style()            { return *data.get<Boxed<TableStyleData>>(); }
    const TableStyleData& as_table_style() const { return *data.get<Boxed<TableStyleData>>(); }
    CellStyleMapData& as_cell_style_map()       { return *data.get<Boxed<CellStyleMapData>>(); }
    const CellStyleMapData& as_cell_style_map() const { return *data.get<Boxed<CellStyleMapData>>(); }
    TableContentData& as_table_content()        { return *data.get<Boxed<TableContentData>>(); }
    const TableContentData& as_table_content() const { return *data.get<Boxed<TableContentData>>(); }
    TableGeometryData& as_table_geometry()      { return *data.get<Boxed<TableGeometryData>>(); }
    const TableGeometryData& as_table_geometry() const { return *data.get<Boxed<TableGeometryData>>(); }

    // Phase 9, step 9.9: the evaluation graph and the dynamic block bases
    EvaluationGraphData& as_evaluation_graph()  { return data.get<EvaluationGraphData>(); }
    const EvaluationGraphData& as_evaluation_graph() const { return data.get<EvaluationGraphData>(); }
    BlockGripLocationData& as_block_grip_location() { return *data.get<Boxed<BlockGripLocationData>>(); }
    const BlockGripLocationData& as_block_grip_location() const { return *data.get<Boxed<BlockGripLocationData>>(); }
    BlockLinearParameterData& as_block_linear_parameter() { return *data.get<Boxed<BlockLinearParameterData>>(); }
    const BlockLinearParameterData& as_block_linear_parameter() const { return *data.get<Boxed<BlockLinearParameterData>>(); }
    BlockLinearGripData& as_block_linear_grip() { return *data.get<Boxed<BlockLinearGripData>>(); }
    const BlockLinearGripData& as_block_linear_grip() const { return *data.get<Boxed<BlockLinearGripData>>(); }
    BlockVisibilityParameterData& as_block_visibility_parameter() { return *data.get<Boxed<BlockVisibilityParameterData>>(); }
    const BlockVisibilityParameterData& as_block_visibility_parameter() const { return *data.get<Boxed<BlockVisibilityParameterData>>(); }
    BlockVisibilityGripData& as_block_visibility_grip() { return *data.get<Boxed<BlockVisibilityGripData>>(); }
    const BlockVisibilityGripData& as_block_visibility_grip() const { return *data.get<Boxed<BlockVisibilityGripData>>(); }
    BlockScaleActionData& as_block_scale_action() { return *data.get<Boxed<BlockScaleActionData>>(); }
    const BlockScaleActionData& as_block_scale_action() const { return *data.get<Boxed<BlockScaleActionData>>(); }
    BlockRepresentationData& as_block_representation() { return data.get<BlockRepresentationData>(); }
    const BlockRepresentationData& as_block_representation() const { return data.get<BlockRepresentationData>(); }
    DynamicBlockPurgePreventerData& as_dynamic_block_purge_preventer() { return data.get<DynamicBlockPurgePreventerData>(); }
    const DynamicBlockPurgePreventerData& as_dynamic_block_purge_preventer() const { return data.get<DynamicBlockPurgePreventerData>(); }

    // Phase 9, step 9.7: MATERIAL and VISUALSTYLE
    MaterialData& as_material()                 { return *data.get<Boxed<MaterialData>>(); }
    const MaterialData& as_material() const     { return *data.get<Boxed<MaterialData>>(); }
    VisualStyleData& as_visual_style()          { return *data.get<Boxed<VisualStyleData>>(); }
    const VisualStyleData& as_visual_style() const { return *data.get<Boxed<VisualStyleData>>(); }

    // Phase 9, step 9.11: render settings, environment, light list and sun study
    RenderSettingsData& as_render_settings()    { return *data.get<Boxed<RenderSettingsData>>(); }
    const RenderSettingsData& as_render_settings() const { return *data.get<Boxed<RenderSettingsData>>(); }
    MentalRayRenderSettingsData& as_mentalray_settings() { return *data.get<Boxed<MentalRayRenderSettingsData>>(); }
    const MentalRayRenderSettingsData& as_mentalray_settings() const { return *data.get<Boxed<MentalRayRenderSettingsData>>(); }
    RenderEnvironmentData& as_render_environment() { return *data.get<Boxed<RenderEnvironmentData>>(); }
    const RenderEnvironmentData& as_render_environment() const { return *data.get<Boxed<RenderEnvironmentData>>(); }
    RenderGlobalData& as_render_global()        { return *data.get<Boxed<RenderGlobalData>>(); }
    const RenderGlobalData& as_render_global() const { return *data.get<Boxed<RenderGlobalData>>(); }
    LightListData& as_light_list()              { return *data.get<Boxed<LightListData>>(); }
    const LightListData& as_light_list() const  { return *data.get<Boxed<LightListData>>(); }
    SunStudyData& as_sun_study()                { return *data.get<Boxed<SunStudyData>>(); }
    const SunStudyData& as_sun_study() const    { return *data.get<Boxed<SunStudyData>>(); }
    // Phase 9, step 9.10: the ACDBASSOC framework
    AssocActionData& as_assoc_action()                           { return *data.get<Boxed<AssocActionData>>(); }
    const AssocActionData& as_assoc_action() const                     { return *data.get<Boxed<AssocActionData>>(); }
    AssocNetworkData& as_assoc_network()                          { return *data.get<Boxed<AssocNetworkData>>(); }
    const AssocNetworkData& as_assoc_network() const                    { return *data.get<Boxed<AssocNetworkData>>(); }
    AssocDependencyData& as_assoc_dependency()                       { return *data.get<Boxed<AssocDependencyData>>(); }
    const AssocDependencyData& as_assoc_dependency() const                 { return *data.get<Boxed<AssocDependencyData>>(); }
    AssocGeomDependencyData& as_assoc_geom_dependency()                  { return *data.get<Boxed<AssocGeomDependencyData>>(); }
    const AssocGeomDependencyData& as_assoc_geom_dependency() const            { return *data.get<Boxed<AssocGeomDependencyData>>(); }
    AssocPersSubentManagerData& as_assoc_pers_subent_manager()              { return *data.get<Boxed<AssocPersSubentManagerData>>(); }
    const AssocPersSubentManagerData& as_assoc_pers_subent_manager() const        { return *data.get<Boxed<AssocPersSubentManagerData>>(); }
    PersSubentManagerData& as_pers_subent_manager()                    { return *data.get<Boxed<PersSubentManagerData>>(); }
    const PersSubentManagerData& as_pers_subent_manager() const              { return *data.get<Boxed<PersSubentManagerData>>(); }
    AssocPathActionParamData& as_assoc_path_action_param()                { return *data.get<Boxed<AssocPathActionParamData>>(); }
    const AssocPathActionParamData& as_assoc_path_action_param() const          { return *data.get<Boxed<AssocPathActionParamData>>(); }
    AssocVertexActionParamData& as_assoc_vertex_action_param()              { return *data.get<Boxed<AssocVertexActionParamData>>(); }
    const AssocVertexActionParamData& as_assoc_vertex_action_param() const        { return *data.get<Boxed<AssocVertexActionParamData>>(); }
    AssocOsnapPointRefActionParamData& as_assoc_osnap_point_ref_action_param()     { return *data.get<Boxed<AssocOsnapPointRefActionParamData>>(); }
    const AssocOsnapPointRefActionParamData& as_assoc_osnap_point_ref_action_param() const { return *data.get<Boxed<AssocOsnapPointRefActionParamData>>(); }
    AssocEdgeActionParamData& as_assoc_edge_action_param()                { return *data.get<Boxed<AssocEdgeActionParamData>>(); }
    const AssocEdgeActionParamData& as_assoc_edge_action_param() const          { return *data.get<Boxed<AssocEdgeActionParamData>>(); }
    AssocExtrudedSurfaceActionBodyData& as_assoc_extruded_surface_action_body()     { return *data.get<Boxed<AssocExtrudedSurfaceActionBodyData>>(); }
    const AssocExtrudedSurfaceActionBodyData& as_assoc_extruded_surface_action_body() const { return *data.get<Boxed<AssocExtrudedSurfaceActionBodyData>>(); }
    AssocLoftedSurfaceActionBodyData& as_assoc_lofted_surface_action_body()       { return *data.get<Boxed<AssocLoftedSurfaceActionBodyData>>(); }
    const AssocLoftedSurfaceActionBodyData& as_assoc_lofted_surface_action_body() const { return *data.get<Boxed<AssocLoftedSurfaceActionBodyData>>(); }
    AssocRevolvedSurfaceActionBodyData& as_assoc_revolved_surface_action_body()     { return *data.get<Boxed<AssocRevolvedSurfaceActionBodyData>>(); }
    const AssocRevolvedSurfaceActionBodyData& as_assoc_revolved_surface_action_body() const { return *data.get<Boxed<AssocRevolvedSurfaceActionBodyData>>(); }
    AssocSweptSurfaceActionBodyData& as_assoc_swept_surface_action_body()        { return *data.get<Boxed<AssocSweptSurfaceActionBodyData>>(); }
    const AssocSweptSurfaceActionBodyData& as_assoc_swept_surface_action_body() const  { return *data.get<Boxed<AssocSweptSurfaceActionBodyData>>(); }
    AssocPlaneSurfaceActionBodyData& as_assoc_plane_surface_action_body()        { return *data.get<Boxed<AssocPlaneSurfaceActionBodyData>>(); }
    const AssocPlaneSurfaceActionBodyData& as_assoc_plane_surface_action_body() const  { return *data.get<Boxed<AssocPlaneSurfaceActionBodyData>>(); }
    AssocAlignedDimActionBodyData& as_assoc_aligned_dim_action_body()          { return *data.get<Boxed<AssocAlignedDimActionBodyData>>(); }
    const AssocAlignedDimActionBodyData& as_assoc_aligned_dim_action_body() const    { return *data.get<Boxed<AssocAlignedDimActionBodyData>>(); }
    // Phase 9, step 9.14: solid history
    ShHistoryData& as_sh_history() { return *data.get<Boxed<ShHistoryData>>(); }
    const ShHistoryData& as_sh_history() const { return *data.get<Boxed<ShHistoryData>>(); }
    ShBoxData& as_sh_box() { return *data.get<Boxed<ShBoxData>>(); }
    const ShBoxData& as_sh_box() const { return *data.get<Boxed<ShBoxData>>(); }
    ShWedgeData& as_sh_wedge() { return *data.get<Boxed<ShWedgeData>>(); }
    const ShWedgeData& as_sh_wedge() const { return *data.get<Boxed<ShWedgeData>>(); }
    ShSphereData& as_sh_sphere() { return *data.get<Boxed<ShSphereData>>(); }
    const ShSphereData& as_sh_sphere() const { return *data.get<Boxed<ShSphereData>>(); }
    ShCylinderData& as_sh_cylinder() { return *data.get<Boxed<ShCylinderData>>(); }
    const ShCylinderData& as_sh_cylinder() const { return *data.get<Boxed<ShCylinderData>>(); }
    ShConeData& as_sh_cone() { return *data.get<Boxed<ShConeData>>(); }
    const ShConeData& as_sh_cone() const { return *data.get<Boxed<ShConeData>>(); }
    ShTorusData& as_sh_torus() { return *data.get<Boxed<ShTorusData>>(); }
    const ShTorusData& as_sh_torus() const { return *data.get<Boxed<ShTorusData>>(); }
    ShPyramidData& as_sh_pyramid() { return *data.get<Boxed<ShPyramidData>>(); }
    const ShPyramidData& as_sh_pyramid() const { return *data.get<Boxed<ShPyramidData>>(); }
    ShExtrusionData& as_sh_extrusion() { return *data.get<Boxed<ShExtrusionData>>(); }
    const ShExtrusionData& as_sh_extrusion() const { return *data.get<Boxed<ShExtrusionData>>(); }
    ShSweepData& as_sh_sweep() { return *data.get<Boxed<ShSweepData>>(); }
    const ShSweepData& as_sh_sweep() const { return *data.get<Boxed<ShSweepData>>(); }
    ShRevolveData& as_sh_revolve() { return *data.get<Boxed<ShRevolveData>>(); }
    const ShRevolveData& as_sh_revolve() const { return *data.get<Boxed<ShRevolveData>>(); }
    ShLoftData& as_sh_loft() { return *data.get<Boxed<ShLoftData>>(); }
    const ShLoftData& as_sh_loft() const { return *data.get<Boxed<ShLoftData>>(); }
    ShBooleanData& as_sh_boolean() { return *data.get<Boxed<ShBooleanData>>(); }
    const ShBooleanData& as_sh_boolean() const { return *data.get<Boxed<ShBooleanData>>(); }
    ShBrepData& as_sh_brep() { return *data.get<Boxed<ShBrepData>>(); }
    const ShBrepData& as_sh_brep() const { return *data.get<Boxed<ShBrepData>>(); }
    ShChamferData& as_sh_chamfer() { return *data.get<Boxed<ShChamferData>>(); }
    const ShChamferData& as_sh_chamfer() const { return *data.get<Boxed<ShChamferData>>(); }
    ShFilletData& as_sh_fillet() { return *data.get<Boxed<ShFilletData>>(); }
    const ShFilletData& as_sh_fillet() const { return *data.get<Boxed<ShFilletData>>(); }
    // Phase 9, step 9.15: the other ACDBASSOC classes
    AssocVariableData& as_assoc_variable() { return *data.get<Boxed<AssocVariableData>>(); }
    const AssocVariableData& as_assoc_variable() const { return *data.get<Boxed<AssocVariableData>>(); }
    AssocValueDependencyData& as_assoc_value_dependency() { return *data.get<Boxed<AssocValueDependencyData>>(); }
    const AssocValueDependencyData& as_assoc_value_dependency() const { return *data.get<Boxed<AssocValueDependencyData>>(); }
    AssocDimDependencyBodyData& as_assoc_dim_dependency_body() { return *data.get<Boxed<AssocDimDependencyBodyData>>(); }
    const AssocDimDependencyBodyData& as_assoc_dim_dependency_body() const { return *data.get<Boxed<AssocDimDependencyBodyData>>(); }
    Assoc2dConstraintGroupData& as_assoc_2d_constraint_group() { return *data.get<Boxed<Assoc2dConstraintGroupData>>(); }
    const Assoc2dConstraintGroupData& as_assoc_2d_constraint_group() const { return *data.get<Boxed<Assoc2dConstraintGroupData>>(); }
    AssocOffsetSurfaceActionBodyData& as_assoc_offset_surface_action_body() { return *data.get<Boxed<AssocOffsetSurfaceActionBodyData>>(); }
    const AssocOffsetSurfaceActionBodyData& as_assoc_offset_surface_action_body() const { return *data.get<Boxed<AssocOffsetSurfaceActionBodyData>>(); }
    AssocTrimSurfaceActionBodyData& as_assoc_trim_surface_action_body() { return *data.get<Boxed<AssocTrimSurfaceActionBodyData>>(); }
    const AssocTrimSurfaceActionBodyData& as_assoc_trim_surface_action_body() const { return *data.get<Boxed<AssocTrimSurfaceActionBodyData>>(); }
    AssocPatchSurfaceActionBodyData& as_assoc_patch_surface_action_body() { return *data.get<Boxed<AssocPatchSurfaceActionBodyData>>(); }
    const AssocPatchSurfaceActionBodyData& as_assoc_patch_surface_action_body() const { return *data.get<Boxed<AssocPatchSurfaceActionBodyData>>(); }
    AssocNetworkSurfaceActionBodyData& as_assoc_network_surface_action_body() { return *data.get<Boxed<AssocNetworkSurfaceActionBodyData>>(); }
    const AssocNetworkSurfaceActionBodyData& as_assoc_network_surface_action_body() const { return *data.get<Boxed<AssocNetworkSurfaceActionBodyData>>(); }
    AssocBlendSurfaceActionBodyData& as_assoc_blend_surface_action_body() { return *data.get<Boxed<AssocBlendSurfaceActionBodyData>>(); }
    const AssocBlendSurfaceActionBodyData& as_assoc_blend_surface_action_body() const { return *data.get<Boxed<AssocBlendSurfaceActionBodyData>>(); }
    AssocExtendSurfaceActionBodyData& as_assoc_extend_surface_action_body() { return *data.get<Boxed<AssocExtendSurfaceActionBodyData>>(); }
    const AssocExtendSurfaceActionBodyData& as_assoc_extend_surface_action_body() const { return *data.get<Boxed<AssocExtendSurfaceActionBodyData>>(); }
    AssocFilletSurfaceActionBodyData& as_assoc_fillet_surface_action_body() { return *data.get<Boxed<AssocFilletSurfaceActionBodyData>>(); }
    const AssocFilletSurfaceActionBodyData& as_assoc_fillet_surface_action_body() const { return *data.get<Boxed<AssocFilletSurfaceActionBodyData>>(); }
    AssocEdgeFilletChamferActionBodyData& as_assoc_edge_fillet_chamfer_action_body() { return *data.get<Boxed<AssocEdgeFilletChamferActionBodyData>>(); }
    const AssocEdgeFilletChamferActionBodyData& as_assoc_edge_fillet_chamfer_action_body() const { return *data.get<Boxed<AssocEdgeFilletChamferActionBodyData>>(); }
    AssocRestoreEntityStateActionBodyData& as_assoc_restore_entity_state_action_body() { return *data.get<Boxed<AssocRestoreEntityStateActionBodyData>>(); }
    const AssocRestoreEntityStateActionBodyData& as_assoc_restore_entity_state_action_body() const { return *data.get<Boxed<AssocRestoreEntityStateActionBodyData>>(); }
    AssocImpliedSurfaceOrSolidActionBodyData& as_assoc_implied_surface_or_solid_action_body() { return *data.get<Boxed<AssocImpliedSurfaceOrSolidActionBodyData>>(); }
    const AssocImpliedSurfaceOrSolidActionBodyData& as_assoc_implied_surface_or_solid_action_body() const { return *data.get<Boxed<AssocImpliedSurfaceOrSolidActionBodyData>>(); }
    AssocRotatedDimActionBodyData& as_assoc_rotated_dim_action_body() { return *data.get<Boxed<AssocRotatedDimActionBodyData>>(); }
    const AssocRotatedDimActionBodyData& as_assoc_rotated_dim_action_body() const { return *data.get<Boxed<AssocRotatedDimActionBodyData>>(); }
    Assoc3PointAngularDimActionBodyData& as_assoc_3point_angular_dim_action_body() { return *data.get<Boxed<Assoc3PointAngularDimActionBodyData>>(); }
    const Assoc3PointAngularDimActionBodyData& as_assoc_3point_angular_dim_action_body() const { return *data.get<Boxed<Assoc3PointAngularDimActionBodyData>>(); }
    AssocOrdinateDimActionBodyData& as_assoc_ordinate_dim_action_body() { return *data.get<Boxed<AssocOrdinateDimActionBodyData>>(); }
    const AssocOrdinateDimActionBodyData& as_assoc_ordinate_dim_action_body() const { return *data.get<Boxed<AssocOrdinateDimActionBodyData>>(); }
    AssocDiametricDimActionBodyData& as_assoc_diametric_dim_action_body() { return *data.get<Boxed<AssocDiametricDimActionBodyData>>(); }
    const AssocDiametricDimActionBodyData& as_assoc_diametric_dim_action_body() const { return *data.get<Boxed<AssocDiametricDimActionBodyData>>(); }
    AssocRadialDimActionBodyData& as_assoc_radial_dim_action_body() { return *data.get<Boxed<AssocRadialDimActionBodyData>>(); }
    const AssocRadialDimActionBodyData& as_assoc_radial_dim_action_body() const { return *data.get<Boxed<AssocRadialDimActionBodyData>>(); }
    AssocMLeaderActionBodyData& as_assoc_mleader_action_body() { return *data.get<Boxed<AssocMLeaderActionBodyData>>(); }
    const AssocMLeaderActionBodyData& as_assoc_mleader_action_body() const { return *data.get<Boxed<AssocMLeaderActionBodyData>>(); }
    AssocEntityCloneActionBodyData& as_assoc_entity_clone_action_body() { return *data.get<Boxed<AssocEntityCloneActionBodyData>>(); }
    const AssocEntityCloneActionBodyData& as_assoc_entity_clone_action_body() const { return *data.get<Boxed<AssocEntityCloneActionBodyData>>(); }
    AssocAsmBodyActionParamData& as_assoc_asm_body_action_param() { return *data.get<Boxed<AssocAsmBodyActionParamData>>(); }
    const AssocAsmBodyActionParamData& as_assoc_asm_body_action_param() const { return *data.get<Boxed<AssocAsmBodyActionParamData>>(); }
    AssocFaceActionParamData& as_assoc_face_action_param() { return *data.get<Boxed<AssocFaceActionParamData>>(); }
    const AssocFaceActionParamData& as_assoc_face_action_param() const { return *data.get<Boxed<AssocFaceActionParamData>>(); }
    AssocTrimmingPathActionParamData& as_assoc_trimming_path_action_param() { return *data.get<Boxed<AssocTrimmingPathActionParamData>>(); }
    const AssocTrimmingPathActionParamData& as_assoc_trimming_path_action_param() const { return *data.get<Boxed<AssocTrimmingPathActionParamData>>(); }
    AssocTrimmingBodyActionParamData& as_assoc_trimming_body_action_param() { return *data.get<Boxed<AssocTrimmingBodyActionParamData>>(); }
    const AssocTrimmingBodyActionParamData& as_assoc_trimming_body_action_param() const { return *data.get<Boxed<AssocTrimmingBodyActionParamData>>(); }
    AssocCompoundActionParamData& as_assoc_compound_action_param() { return *data.get<Boxed<AssocCompoundActionParamData>>(); }
    const AssocCompoundActionParamData& as_assoc_compound_action_param() const { return *data.get<Boxed<AssocCompoundActionParamData>>(); }
    AssocObjectActionParamData& as_assoc_object_action_param() { return *data.get<Boxed<AssocObjectActionParamData>>(); }
    const AssocObjectActionParamData& as_assoc_object_action_param() const { return *data.get<Boxed<AssocObjectActionParamData>>(); }
    AssocArrayActionBodyData& as_assoc_array_action_body() { return *data.get<Boxed<AssocArrayActionBodyData>>(); }
    const AssocArrayActionBodyData& as_assoc_array_action_body() const { return *data.get<Boxed<AssocArrayActionBodyData>>(); }
    AssocArrayModifyActionBodyData& as_assoc_array_modify_action_body() { return *data.get<Boxed<AssocArrayModifyActionBodyData>>(); }
    const AssocArrayModifyActionBodyData& as_assoc_array_modify_action_body() const { return *data.get<Boxed<AssocArrayModifyActionBodyData>>(); }

    UnknownObjectData& as_unknown()             { return data.get<UnknownObjectData>(); }
    const UnknownObjectData& as_unknown() const { return data.get<UnknownObjectData>(); }

    // Phase 9, step 9.4: draw order, clipping and indexes
    SortentsTableData& as_sortents_table()      { return data.get<SortentsTableData>(); }
    const SortentsTableData& as_sortents_table() const { return data.get<SortentsTableData>(); }
    SpatialFilterData& as_spatial_filter()      { return data.get<SpatialFilterData>(); }
    const SpatialFilterData& as_spatial_filter() const { return data.get<SpatialFilterData>(); }
    LayerIndexData& as_layer_index()            { return data.get<LayerIndexData>(); }
    const LayerIndexData& as_layer_index() const { return data.get<LayerIndexData>(); }
    SpatialIndexData& as_spatial_index()        { return data.get<SpatialIndexData>(); }
    const SpatialIndexData& as_spatial_index() const { return data.get<SpatialIndexData>(); }
    IdBufferData& as_id_buffer()                { return data.get<IdBufferData>(); }
    const IdBufferData& as_id_buffer() const    { return data.get<IdBufferData>(); }
    LayerFilterData& as_layer_filter()          { return data.get<LayerFilterData>(); }
    const LayerFilterData& as_layer_filter() const { return data.get<LayerFilterData>(); }

    // Phase 9, step 9.8: fields and document data (DATATABLE and ACDBDATATABLE share as_data_table())
    FieldData& as_field()                       { return *data.get<Boxed<FieldData>>(); }
    const FieldData& as_field() const           { return *data.get<Boxed<FieldData>>(); }
    FieldListData& as_field_list()              { return data.get<FieldListData>(); }
    const FieldListData& as_field_list() const  { return data.get<FieldListData>(); }
    DataTableData& as_data_table()              { return *data.get<Boxed<DataTableData>>(); }
    const DataTableData& as_data_table() const  { return *data.get<Boxed<DataTableData>>(); }
    ObjectPointerData& as_object_pointer()      { return data.get<ObjectPointerData>(); }
    const ObjectPointerData& as_object_pointer() const { return data.get<ObjectPointerData>(); }
    VbaProjectData& as_vba_project()            { return data.get<VbaProjectData>(); }
    const VbaProjectData& as_vba_project() const { return data.get<VbaProjectData>(); }
};

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

class Drawing {
public:
    Drawing();
    Drawing(DxfVersion version);

    DxfVersion version() const;
    void set_version(DxfVersion version);

    // --- Entities ---
    Entity& add_line(Point3 start, Point3 end, const std::string& layer = "0");
    Entity& add_circle(Point3 center, double radius, const std::string& layer = "0");
    Entity& add_arc(Point3 center, double radius, double start_angle, double end_angle,
                    const std::string& layer = "0");
    Entity& add_point(Point3 location, const std::string& layer = "0");
    Entity& add_text(Point3 position, const std::string& content, double height = 2.5,
                     const std::string& layer = "0");
    Entity& add_polyline(const std::vector<Point2>& vertices, bool closed = false,
                         const std::string& layer = "0");
    Entity& add_trace(Point3 p0, Point3 p1, Point3 p2, Point3 p3,
                      const std::string& layer = "0");
    Entity& add_solid(Point3 p0, Point3 p1, Point3 p2, Point3 p3,
                      const std::string& layer = "0");
    Entity& add_face3d(Point3 p0, Point3 p1, Point3 p2, Point3 p3,
                       const std::string& layer = "0");
    Entity& add_shape(Point3 insertion_point, const std::string& name, double size,
                      const std::string& layer = "0");
    Entity& add_insert(const std::string& block_name, Point3 insertion_point,
                       const std::string& layer = "0");
    Entity& add_dimension(DimensionType dim_type, Point3 def_point, Point3 text_midpoint,
                          const std::string& layer = "0");
    // An ARC_DIMENSION (2000+; step 9.17): the arc's center, its two ends (counterclockwise from `start` to `end`) and a
    // point of the dimension arc (10). The block is built when it is written (the arc length with AutoCAD's arc symbol); the
    // text stays at the arc's middle until the program moves `dimension.text_midpoint` and sets bit 128 of its flags
    Entity& add_arc_dimension(const Point3& center, const Point3& start, const Point3& end, const Point3& arc_point,
                              const std::string& layer = "0");
    // A LARGE_RADIAL_DIMENSION (DIMJOGGED; 2000+; step 9.17): the circle's center, a point on the circle, where the center is
    // shown instead (the dimension line starts there) and the text's place; the jog is half way between the two points
    Entity& add_large_radial_dimension(const Point3& center, const Point3& circle_point, const Point3& center_override,
                                       const Point3& text_point, const std::string& layer = "0");
    Entity& add_viewport(Point3 center, double width, double height,
                         const std::string& layer = "0");
    // The types after R12 (EntityType::LwPolyline …): saving the drawing as an older version fails with
    // "<TYPE> requires DXF <version> or later", the first version that has the type — R13 for ELLIPSE,
    // XLINE, RAY, MTEXT, TOLERANCE, LEADER, SPLINE, MLINE, 3DSOLID, REGION and BODY, R14 for LWPOLYLINE, HATCH
    // and IMAGE, 2000 for the rest (WIPEOUT, MULTILEADER, HELIX, MESH)
    Entity& add_lwpolyline(const std::vector<Point2>& vertices, bool closed = false,
                           const std::string& layer = "0");
    // major_axis: the end of the major axis from the center; params in radians
    Entity& add_ellipse(Point3 center, Point3 major_axis, double ratio,
                        double start_param = 0.0, double end_param = 6.283185307179586,
                        const std::string& layer = "0");
    Entity& add_xline(Point3 base_point, Point3 direction, const std::string& layer = "0");
    Entity& add_ray(Point3 base_point, Point3 direction, const std::string& layer = "0");
    Entity& add_mtext(Point3 insertion_point, const std::string& content, double height = 2.5,
                      const std::string& layer = "0");
    Entity& add_tolerance(Point3 insertion_point, const std::string& content,
                          const std::string& layer = "0");
    // A leader through vertices, from the arrowhead. An annotation (MTEXT, TOLERANCE or
    // INSERT) gets a handle if it has none; the leader points to it and the annotation
    // gets the leader as reactor when written. The pointer is used only during the call.
    Entity& add_leader(const std::vector<Point3>& vertices, Entity* annotation = nullptr,
                       const std::string& layer = "0");
    // A planar spline through its control points, with clamped uniform knots 0 … 1
    Entity& add_spline(const std::vector<Point3>& control_points, int degree = 3,
                       const std::string& layer = "0");
    // A solid fill inside a closed polyline boundary (one path); a pattern is set in
    // as_hatch() (pattern_name, solid = false, pattern_lines)
    Entity& add_hatch(const std::vector<Point2>& boundary, const std::string& layer = "0");
    // A gradient fill (2004+) in a polyline, as AutoCAD writes one: solid (70 1, "SOLID")
    // with the gradient after the seed points
    Entity& add_hatch(const std::vector<Point2>& boundary, const HatchGradient& gradient,
                      const std::string& layer = "0");
    // A multiline in a style of ACAD_MLINESTYLE; the vertices' directions, miters and
    // element parameters are computed from the style as AutoCAD does
    Entity& add_mline(const std::vector<Point3>& vertices, bool closed = false, double scale = 1.0,
                      int justification = 0, const std::string& style = "STANDARD",
                      const std::string& layer = "0");
    // An image width units wide at insertion_point, size in pixels (square pixels). Its
    // IMAGEDEF (one per file, in ACAD_IMAGE_DICT), IMAGEDEF_REACTOR and RASTERVARIABLES
    // are created as AutoCAD does.
    Entity& add_image(const std::string& file_name, Point3 insertion_point, Point2 size,
                      double width, const std::string& layer = "0");
    // A WIPEOUT (2000+) covering the polygon (in the WCS, z its elevation; closed by
    // itself) as AutoCAD makes it: its frame the square around the polygon's box, the boundary
    // in -0.5 … 0.5 of it; ACAD_WIPEOUT_VARS (frames shown) made when there is none
    Entity& add_wipeout(const std::vector<Point3>& polygon, const std::string& layer = "0");
    // 3DSOLID, REGION or BODY (type Solid3D, Region or Body) from ACIS data in the file's
    // form, e.g. copied from another drawing — HDxf does not create ACIS data
    Entity& add_acis(EntityType type, AcisData data, const std::string& layer = "0");
    // A surface (type Surface, ExtrudedSurface … PlaneSurface; 2007) from its data — its ACIS
    // body and what it was made from in the file's form, e.g. copied from another drawing
    Entity& add_surface(EntityType type, SurfaceData data, const std::string& layer = "0");
    // A MESH (2000+, plan phase 4, B48): its vertices and faces (each a list of vertex indices),
    // smoothed to `level`; its edges made from the faces as AutoCAD lists them — face by face,
    // each edge once, the smaller index first — without creases
    Entity& add_mesh(const std::vector<Point3>& vertices, const std::vector<std::vector<int>>& faces,
                     int level = 0, const std::string& layer = "0");
    Entity& add_entity(Entity entity);

    const std::vector<Entity>& entities() const;
    std::vector<Entity>& entities();

    // --- Layers ---
    Layer& add_layer(const std::string& name, Color color = Color::white());
    const std::vector<Layer>& layers() const;
    std::vector<Layer>& layers();

    // --- Blocks ---
    Block& add_block(const std::string& name, Point3 base_point = {});
    Block* find_block(const std::string& name);
    const Block* find_block(const std::string& name) const;
    // The entity with this handle in the model, a layout or a block (the other columns and the
    // frame of an MTEXT up to 2013 are looked up so); none when the drawing has no such entity
    const Entity* find_entity(const std::string& handle) const;
    const std::vector<Block>& blocks() const;
    // Renaming or removing a block here is seen by find_block() (its index checks itself)
    std::vector<Block>& blocks();

    // --- Linetypes ---
    Linetype& add_linetype(const std::string& name, const std::string& description,
                           const std::vector<double>& pattern);
    const std::vector<Linetype>& linetypes() const;
    std::vector<Linetype>& linetypes();

    // --- Text styles ---
    TextStyle& add_text_style(const std::string& name, const std::string& font_file = "txt");
    const std::vector<TextStyle>& text_styles() const;
    std::vector<TextStyle>& text_styles();

    // --- Dimension styles ---
    DimStyle& add_dim_style(const std::string& name);
    const std::vector<DimStyle>& dim_styles() const;
    std::vector<DimStyle>& dim_styles();

    // --- Views ---
    const std::vector<View>& views() const;
    std::vector<View>& views();

    // --- UCS ---
    const std::vector<Ucs>& ucs_list() const;
    std::vector<Ucs>& ucs_list();

    // --- Viewports ---
    const std::vector<Vport>& vports() const;
    std::vector<Vport>& vports();

    // --- App IDs ---
    AppId& add_app_id(const std::string& name);
    const std::vector<AppId>& app_ids() const;
    std::vector<AppId>& app_ids();

    // --- Header variables ---
    const std::vector<HeaderVar>& header_vars() const;
    HeaderVar* find_header(const std::string& name);
    const HeaderVar* find_header(const std::string& name) const;
    void set_header_string(const std::string& name, int code, const std::string& val);
    void set_header_int(const std::string& name, int code, int val);
    void set_header_double(const std::string& name, int code, double val);
    void set_header_point(const std::string& name, const Point3& p);

    // --- The size of entities, as AutoCAD computes it (text with the fonts in font_paths()) ---
    // An entity's extents in the WCS (AutoCAD's geometric extents): what $EXTMIN/$EXTMAX
    // are made of. False when it has none (XLINE, RAY, a text of spaces).
    bool entity_extents(const Entity& e, Point3& min, Point3& max) const;
    // MTEXT's actual width (42, its widest line) and height (43), as AutoCAD 2018 writes
    // them — AutoCAD's 2000 and 2004 files do not, so HDxf does not write them either.
    // False when e is not MTEXT or no font for it is found.
    bool mtext_size(const Entity& e, double& width, double& height) const;

    // --- Layers (removal) ---
    bool remove_layer(const std::string& name);

    // --- Save/Load ---
    bool save(DxfFormat format = DxfFormat::Ascii);
    bool save_as(const std::filesystem::path& path, DxfFormat format = DxfFormat::Ascii);
    bool load(const std::filesystem::path& path);

    // --- Memory-based Save/Load ---
    bool load_from_memory(const uint8_t* data, size_t size);
    std::vector<uint8_t> save_to_memory(DxfFormat format = DxfFormat::Ascii);

    const std::string& last_error() const;
    // Things a successful load/save had to change or could not do, e.g. a font that was not
    // found. Cleared by every load/save call.
    const std::vector<std::string>& warnings() const;

    // --- Objects (OBJECTS section, R13+; see Object) ---
    const std::vector<Object>& objects() const;
    std::vector<Object>& objects();
    Object* find_object(const std::string& handle);
    const Object* find_object(const std::string& handle) const;
    // The LAYOUT objects, in file order
    std::vector<Object*> layouts();
    std::vector<const Object*> layouts() const;
    // A paper space layout: LAYOUT in ACAD_LAYOUT, a *Paper_SpaceN block and its
    // BLOCK_RECORD, linked both ways. Its entities go in that block. An existing layout
    // with the name is returned as it is.
    Object& add_layout(const std::string& name);
    // A named GROUP in ACAD_GROUP. Members without a handle get one now; each member
    // gets the group as reactor when the drawing is written.
    Object& add_group(const std::string& name, const std::vector<Entity*>& members,
                      const std::string& description = "");
    // An MLEADERSTYLE in ACAD_MLEADERSTYLE with AutoCAD's Standard values (MLeaderStyleData;
    // overwrite_property (295) and text_extended (298) true, as AutoCAD 2017's new drawing), as
    // AutoCAD makes one. Before 2013 text_extended is the round-trip XRECORD in the style's
    // extension dictionary, made or removed at every save after the value (plan phase 5, B60).
    // An existing style of the name is returned as it is.
    Object& add_mleader_style(const std::string& name);
    // A MULTILEADER as AutoCAD's MLEADER command makes it (plan phase 3, B38): one leader
    // through `leader` — the arrowhead first, its last point where the landing starts —
    // and the content after the landing, with the style's values (Standard is made when the
    // drawing has none). The landing goes to the side the leader comes from; the text is
    // placed as AutoCAD places it for the style's attachments, left- or right-justified by
    // that side (unless the style keeps it left). A DXF 2000+ entity.
    Entity& add_mleader(const std::vector<Point3>& leader, const std::string& text,
                        const std::string& style = "Standard", const std::string& layer = "0");
    // The same with a block as content: its insertion point at the landing's end, scaled and
    // turned as the style says; its attributes get their ATTDEF's default value
    Entity& add_mleader_block(const std::vector<Point3>& leader, const std::string& block,
                              const std::string& style = "Standard", const std::string& layer = "0");
    // The drawing's GEODATA (2010, plan phase 4, B50): in the model space BLOCK_RECORD's extension
    // dictionary (made as AutoCAD makes it when there is none) under ACAD_GEOGRAPHICDATA; one there
    // already is replaced. Written as data.version: 2 in DXF 2010; 3 in DXF 2013 (phase 5, B64 —
    // set version = 3 for a 2013 drawing; its coordinate system is then written in 303 parts of
    // 250 characters, the last in 301). Another version is an error when saved.
    Object& set_geodata(const GeoDataData& data);

    // --- Coordination model and RAPIDRT render settings (2013+; plan phase 6, B78) ---
    // An attached Navisworks model: its definition (NAVISWORKSMODELDEF, one per path) under the
    // file's name in ACAD_BIM_DEFINITIONS, and the entity placing it with `matrix` (4x4, row by
    // row, the translation in the fourth column) and the insertion unit factor. A definition
    // that is removed makes the entity's 340 a 0 (none) when written. DXF before 2013: an error.
    Entity& add_coordination_model(const std::string& path, Point3 min, Point3 max,
                                   const std::array<double, 16>& matrix = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                                   double unit_factor = 1.0, const std::string& layer = "0");
    // --- Underlays (2000+; plan phase 9, step 9.6; underlay.cpp) ---
    // An underlay of a PDF, DWF or DGN file: the entity (PDFUNDERLAY, DWFUNDERLAY or DGNUNDERLAY) at `insertion_point`
    // and its definition, which is shared by every underlay of the same file, kind and `item_name` (the PDF page's
    // number as text, a DWF sheet's or a DGN model's name) — one in ACAD_PDFDEFINITIONS, ACAD_DWFDEFINITIONS or
    // ACAD_DGNDEFINITIONS under "<file name without folder and extension> - <item name>", as AutoCAD's file has it.
    // The underlay is on, with no clip, scale 1, no rotation, contrast 100 and no fade (set the rest through
    // as_underlay()); HDxf has no PDF/DWF/DGN reader, so its proxy graphics are AutoCAD's empty stream and it adds
    // nothing to the drawing's extents. DXF before 2000: an error when saved.
    Entity& add_underlay(UnderlayKind kind, const std::string& file_name, const std::string& item_name,
                         Point3 insertion_point, const std::string& layer = "0");
    // The definition an underlay entity points to (null when the handle leads nowhere or `underlay` is none)
    Object* underlay_definition(const Entity& underlay);
    const Object* underlay_definition(const Entity& underlay) const;

    // --- Sections (2000+; plan phase 9, step 9.12; section.cpp) ---
    // A section plane as AutoCAD's SECTIONPLANE makes it: the SECTIONOBJECT along `vertices` (the section line in the WCS,
    // at least two points; state 1, plane), its SECTION_SETTINGS (owned by it, no type settings) and, the first time, the
    // SECTION_MANAGER under ACAD_SECTION_MANAGER in the root dictionary, the layer Defpoints (AutoCAD puts the indicator's
    // outline on it) and the registered applications IsSlice and ThicknessDepth of the section's XDATA. The indicator
    // (proxy graphics) is made from the section's vertices, heights, color and layer when the drawing is saved, and so
    // follows what is changed through as_section_object(). DXF before 2000: an error when saved.
    Entity& add_section(const std::string& name, const std::vector<Point3>& vertices, const std::string& layer = "0");
    // The SECTION_SETTINGS a section owns (null when the handle leads nowhere or `section` is none)
    Object* section_settings(const Entity& section);
    const Object* section_settings(const Entity& section) const;
    // The drawing's SECTION_MANAGER (null when it has none)
    Object* section_manager();
    const Object* section_manager() const;

    // A RAPIDRT render preset in ACAD_RENDER_RAPIDRT_SETTINGS under its name (replaced when it is there)
    Object& add_rapidrt_render_settings(const RapidRtRenderSettingsData& data);
    // The IBL environment: the root dictionary's ACAD_BACKGROUND entry; the background it replaces
    // becomes its custom background (340) unless that is set
    Object& set_rapidrt_render_environment(const RapidRtRenderEnvironmentData& data);

    // --- Render objects (plan phase 9, step 9.11; objects_render.cpp) ---
    // Each is the one of its kind in the root dictionary under the name AutoCAD's reference gives (a second call replaces the
    // data of the one that is there): the MentalRay preset in ACAD_RENDER_ACTIVE_SETTINGS (as AutoCAD's files have it), the
    // environment in ACAD_RENDER_ENVIRONMENT, the global settings in ACAD_RENDER_GLOBAL and the light list in ACAD_LIGHT.
    // DXF before 2000: an error when saved (MENTALRAYRENDERSETTINGS: before 2007).
    Object& set_mentalray_render_settings(const MentalRayRenderSettingsData& data);
    Object& set_render_environment(const RenderEnvironmentData& data);
    Object& set_render_global(const RenderGlobalData& data);
    Object& set_light_list(const LightListData& data);

    // --- Tables (2000+; plan phase 9, step 9.1; table_sync.cpp) ---
    // A TABLESTYLE named `name` in ACAD_TABLESTYLE with its CELLSTYLEMAP (in its extension dictionary), as AutoCAD
    // makes Standard (the one that is there is returned). `data` is the style's values.
    Object& add_table_style(const std::string& name = "Standard", const TableStyleData& data = TableStyleData());
    // A table of `rows` x `columns` empty cells at `insertion`: row 0 is the title row and row 1 the header row,
    // the others data (unless the style suppresses them); the anonymous block *T and, when it is saved (or
    // sync_table() is called), the TABLECONTENT, the TABLEGEOMETRY and the round-trip dictionary are made. The
    // style is made when it is Standard and there is none. DXF before 2000: an error when saved.
    Entity& add_table(Point3 insertion, size_t rows, size_t columns, double row_height, double column_width,
                      const std::string& style = "Standard", const std::string& layer = "0");
    // A text cell's text, as the entity holds it (an AcValue of the text with its formatted text)
    void set_table_cell_text(Entity& table, size_t row, size_t column, const std::string& text);
    // The text a cell shows: its own text, or the text its field was last evaluated to (FieldData::display_text()) when the
    // cell is a field (step 9.8; nothing is evaluated). Empty for a cell that is not there.
    std::string table_cell_text(const Entity& table, size_t row, size_t column) const;
    // The table's TABLECONTENT and TABLEGEOMETRY (null when it has none, or `table` is not an ACAD_TABLE)
    Object* table_content(const Entity& table);
    const Object* table_content(const Entity& table) const;
    Object* table_geometry(const Entity& table);
    const Object* table_geometry(const Entity& table) const;
    // The table's TABLESTYLE and the CELLSTYLEMAP of that style (null when the handle leads nowhere)
    const Object* table_style(const Entity& table) const;
    const Object* table_cell_style_map(const Object& style) const;
    // Makes the table's TABLECONTENT, TABLEGEOMETRY and round-trip dictionary say what the entity says, as
    // AutoCAD does when it finds the cell's checksum changed: a table without them gets them; the cells whose
    // text changed (their text no longer has the checksum the content keeps) get the text, and the new checksum;
    // rows, columns, merged ranges and the number of cells follow the entity. The sizes of the cells are the
    // layout's (step 9.5). Called for every table when the drawing is saved.
    void sync_table(Entity& table);

    // --- Document data (fields, value tables; plan phase 9, step 9.8; field.cpp, data_table.cpp) ---
    // The text a FIELD shows as it was last evaluated: its cache's text (FieldData::display_text()); for a `_text`
    // field (a text of fields: AutoCAD's group 1 of the TEXT) its code with every `%<\_FldIdx n>%` replaced by the text of
    // child n, which is how AutoCAD's own texts are made of their fields. Nothing is evaluated: a field AutoCAD has not
    // evaluated shows what it holds. Empty for an object that is not a FIELD.
    std::string field_text(const Object& field) const;
    // A DATATABLE (DXF 2004 and later) or ACDBDATATABLE (DXF 2000) of `data`, owned by `owner` (an XRECORD, or a
    // dictionary): the record AutoCAD's version has. DXF before 2000: an error when saved.
    Object& add_data_table(const DataTableData& data, const std::string& owner);
    // The table's pre-2007 round-trip cells (DXF 2000 and 2004: AutoCAD keeps them for every table, empty or not):
    // the ACDBDATATABLE/DATATABLE the table's ACAD_XREC_ROUNDTRIP record points at; null when there is none. A new
    // table of those versions gets it when the drawing is saved (or sync_table() is called); a text cell changed
    // since follows (the cell's checksum and its value record).
    Object* table_round_trip_data(const Entity& table);
    const Object* table_round_trip_data(const Entity& table) const;

    // --- Dynamic block bases (2000+; plan phase 9, step 9.9; dynamic_block_base.cpp, evaluation_graph.cpp) ---
    // The evaluation graph of the block `block_name` (the block is made when there is none), made as AutoCAD makes it for
    // a dynamic block definition: the block record's extension dictionary (hard-owning, owned by the record) with
    // ACAD_ENHANCEDBLOCK (the graph, owned by that dictionary, with no node) and AcDbDynamicBlockRoundTripPurgePreventer
    // (ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION), and the block record's XDATA AcDbDynamicBlockTrueName and
    // AcDbDynamicBlockGUID (the GUID is made from the block's name and handle). The graph that is there is returned as it
    // is. Saved before DXF 2010 the block is written as AutoCAD writes a dynamic block there: the anonymous block *U<n>
    // (BLOCK flag 1) with its name in the XDATA, and the INSERTs of it name that block. DXF before 2000: an error when saved.
    Object& add_dynamic_block_graph(const std::string& block_name);
    // The block's graph; null when it has none (or no extension dictionary, or no such block)
    Object* dynamic_block_graph(const std::string& block_name);
    const Object* dynamic_block_graph(const std::string& block_name) const;
    // A dynamic block element (`type`: BlockGripLocation, BlockLinearParameter, BlockLinearGrip, BlockVisibilityParameter,
    // BlockVisibilityGrip, BlockScaleAction, or a type of step 9.13) in the block's graph (made when there is none): an
    // object owned by the graph, with a new node (add_node: the next id, which becomes the element's EvalExpr node_id
    // and its AcDbEvalExpr version 33 and maintenance 327 when those are 0). The element's own links
    // (the grip node ids, the action's selection …) are the program's: it knows the ids the other elements were given. An
    // `eval` in `data` is replaced by the node's; `data` must be the type's data.
    Object& add_dynamic_block_element(const std::string& block_name, ObjectType type, ObjectData data);
    // The element of the block's graph that has this node id (null when there is none)
    Object* dynamic_block_element(const std::string& block_name, int32_t node_id);
    const Object* dynamic_block_element(const std::string& block_name, int32_t node_id) const;
    // Removes an element: its object, its node and the edges of it in the graph, and the element's handle from every
    // visibility state that lists it. False when `handle` is not an element of a graph
    bool remove_dynamic_block_element(const std::string& handle);
    // --- 9.14 solid history (solid_history.cpp) ---
    // The history (ACSH_HISTORY_CLASS) of the 3DSOLID with this handle; null when the solid has none or there is no such solid
    Object* solid_history(const std::string& solid_handle);
    const Object* solid_history(const std::string& solid_handle) const;
    // A history for the 3DSOLID (the one it has is returned): the ShHistory object owned by the solid, which points at it
    // with its 350 (AcisData::history_handle), and its ACAD_EVALUATION_GRAPH (owned by the history, no node yet). Null when
    // there is no such solid. The history is not the solid's geometry: nothing of the solid's ACIS data changes. Saved before
    // DXF 2007 it is an error ("<what> requires DXF 2007 or later")
    Object* add_solid_history(const std::string& solid_handle);
    // A node (`type`: ShBox … ShFillet, with its own data) in the history's graph: an object owned by the graph, with a new
    // node (the next id, which becomes the node's EvalExpr node_id and the history's last_node_id; the AcDbEvalExpr version and
    // maintenance are 33 and 327 when they are 0), an edge to it from the graph node of each operand id in
    // `operand_node_ids` (the node ids of the bodies it works on: a Boolean's two, a fillet's one) and, when the node's
    // material is empty, the drawing's "Global" material. Null when `history_handle` is not a history
    Object* add_solid_history_node(const std::string& history_handle, ObjectType type, ObjectData data,
                                   const std::vector<int32_t>& operand_node_ids = {});
    // The node with this id (the EvalExpr node id) of the history's graph; null when there is none
    Object* solid_history_node(const std::string& history_handle, int32_t node_id);
    const Object* solid_history_node(const std::string& history_handle, int32_t node_id) const;
    // Removes a node: its object and its graph node with the edges of it (the node ids that other nodes name stay: a link
    // that leads nowhere is kept). False when `handle` is not a history node
    bool remove_solid_history_node(const std::string& handle);
    // Removes the solid's history: the history, its graph and every node, and the solid's 350. False when it has none
    bool remove_solid_history(const std::string& solid_handle);
    // --- 9.17 annotation contexts and DIMASSOC (object_context.cpp, dimassoc.cpp) ---
    // The handle of an entity of this drawing: its own, or, when it has none yet (an entity added in memory has its handle only
    // when saved), the next free one, which it keeps
    std::string entity_handle(Entity& entity);
    // A SCALE in ACAD_SCALELIST (made when the drawing has none): `paper_units` to `drawing_units` (1 and 2 for "1:2"). The
    // existing one of the name is returned. Null before DXF 2000
    Object* add_annotation_scale(const std::string& name, double paper_units, double drawing_units);
    // The SCALE object (ACAD_SCALELIST) with this name ("1:2") or handle; null when there is none
    const Object* find_scale(const std::string& name_or_handle) const;
    // The context objects an entity (or any object that has an extension dictionary) has, in the order of its *A<n> dictionary
    std::vector<const Object*> object_contexts(const std::string& owner_handle) const;
    // The context of the entity for a scale (a SCALE's handle); null when it has none
    const Object* object_context(const std::string& owner_handle, const std::string& scale_handle) const;
    Object* object_context(const std::string& owner_handle, const std::string& scale_handle);
    // A context for the scale in the entity's AcDbContextDataManager → ACDB_ANNOTATIONSCALES → "*A<n>" dictionaries (made when
    // the entity has none, with the entity's XDATA AcadAnnotative that says it is annotative). `type` is one of the 16 context
    // types and `data` its data; the scale (340) is set to `scale_handle`, the owner is the *A dictionary, and a first context
    // of an entity is its default. Null when the entity or the scale is not there, or `data` is not the type's. Saved before
    // DXF 2000 the entity's contexts are an error ("<what> requires DXF 2000 or later")
    Object* add_object_context(const std::string& owner_handle, const std::string& scale_handle, ObjectType type, ObjectData data);
    // Removes the entity's context for the scale (the object and its entry in the *A dictionary); false when it has none. The
    // other contexts, the scale and every number in them are left as they are
    bool remove_object_context(const std::string& owner_handle, const std::string& scale_handle);
    // Removes a SCALE and every context that points at it, in every entity; false when there is no such scale
    bool remove_annotation_scale(const std::string& scale_handle);
    // The DIMASSOC of a dimension (the one in its extension dictionary's ACAD_DIMASSOC); null when it has none
    const Object* dimension_association(const std::string& dimension_handle) const;
    // Makes a dimension associative: a DIMASSOC (written as AutoCAD does: in the dimension's extension dictionary, with the
    // dimension and the geometry of its points as reactors) with `data`'s points; the dimension's handle is set. Replaces the
    // dimension's DIMASSOC when it has one. Null when the dimension is not there. Saved before DXF 2000: an error
    Object* add_dimension_association(const std::string& dimension_handle, DimAssocData data);
    // Removes the dimension's DIMASSOC (DIMDISASSOCIATE); false when it has none
    bool remove_dimension_association(const std::string& dimension_handle);
    // An INSERT of the dynamic block as AutoCAD keeps one that has had a dynamic property changed: an anonymous block *U<n>
    // (a copy of the original's entities; its BLOCK_RECORD has the XDATA AcDbBlockRepBTag with the original's handle),
    // the INSERT of it, and in the INSERT's extension dictionary AcDbBlockRepresentation with AcDbRepData
    // (ACDB_BLOCKREPRESENTATION_DATA pointing back at the original's BLOCK_RECORD). The original's AcDbBlockRepETag counts
    // the representations. The cache of evaluated values (AppDataCache) is AutoCAD's own evaluation: it is not made, and
    // AutoCAD makes it when it opens the drawing. DXF before 2000: an error when saved.
    Entity& add_dynamic_block_representation(const std::string& block_name, Point3 insertion_point,
                                             const std::string& layer = "0");
    // The dynamic block an anonymous block is a representation of (its AcDbBlockRepBTag); null when it is none
    const Block* dynamic_block_original(const Block& anonymous) const;
    // A dynamic block definition's own name: its XDATA AcDbDynamicBlockTrueName, which is what AutoCAD's DXF before 2010 names
    // it by while the block itself is the anonymous *U<n> (a definition built in memory keeps its name here and the writer
    // makes it anonymous in DXF 2000 … 2007). The block's name when it has no such XDATA
    std::string dynamic_block_true_name(const Block& block) const;

    // --- The preview image (THUMBNAILIMAGE, 2000+; plan phase 2, B26) ---
    // The image's bytes as the file has them (a Windows DIB); empty = none. HDxf neither
    // decodes nor makes it. Written by the 2000 and 2004 writers, not in R12.
    const std::vector<uint8_t>& thumbnail() const;
    void set_thumbnail(std::vector<uint8_t> bytes);
    // The section as read (from 0 SECTION to ENDSEC); written back as it is while the
    // bytes are unchanged
    const RawRecord& thumbnail_raw() const;

    // --- Preserved records (DXF 2000+ files, see RawRecord) ---
    // BLOCK_RECORD entries are in Block::record; raw_tables() keeps the table record and
    // any BLOCK_RECORD that belongs to no block. OBJECTS is in objects(), THUMBNAILIMAGE in
    // thumbnail(), ACDSDATA (2013+) in AcisData::sab, LayoutData::thumbnail and
    // View::thumbnail (its other records are kept internally).
    const std::vector<RawTable>& raw_tables() const;
    const std::vector<RawSection>& raw_sections() const;

private:
    friend class internal::DxfReader;
    friend class internal::DxfWriter;
    friend std::string internal::template_text(const Drawing& dwg, bool source_binary);

    void init_defaults();
    bool scan_handles();
    // Gives a handle to every record that has none; each one assigned is listed, so a
    // failed save can take them back. r2000: also the records only R13+ writes with a
    // handle (DIMSTYLE, VERTEX, SEQEND, ATTRIB, BLOCK_RECORD, objects).
    bool assign_handles(bool r2000, std::vector<std::string*>& assigned);
    // The records a DXF 2000 drawing always has: *Model_Space and *Paper_Space with their
    // BLOCK_RECORDs and LAYOUTs, and the standard objects (drawing_r2000.cpp)
    void ensure_r2000_structure();
    // The records a new R13 or R14 drawing has (plan phase 7, B89; drawing_r13_r14.cpp):
    // *Model_Space and *Paper_Space with their BLOCK_RECORDs, the root dictionary with ACAD_GROUP
    // and ACAD_MLINESTYLE (STANDARD) — no LAYOUT, plot settings or plot styles. A drawing that has
    // 2000's records (made as 2000 or later, then set to R13/R14) loses the ones that are only
    // AutoCAD's own starting values; layouts the program made stay, and the drawing is then
    // refused when it is written.
    void ensure_older_structure();
    // 2000's layouts and plot style dictionary in a drawing that has R13's structure (the
    // other direction, drawing_r2000.cpp)
    void add_layout_structure();
    // And a DXF 2007 drawing the materials and visual styles its records point to (B34,
    // drawing_r2007.cpp)
    void ensure_r2007_objects();
    // $XCLIPFRAME before 2010: the DICTIONARYVAR XCLIPFRAME where AutoCAD keeps a value the
    // header cannot have — read into the header, and made, given the header's value or removed
    // for the version written (plan phase 4, B43; drawing_r2000.cpp)
    void read_xclipframe_variable(DxfVersion version);
    void sync_xclipframe_variable(bool from_file);
    // What 2013 has in its records and 2000 … 2010 as round-trip XRECORDs and XDATA (plan
    // phase 5, B60; drawing_roundtrip.cpp): read into the typed values from a 2000 … 2010 file,
    // and made, changed or removed after them for the version written
    void read_roundtrip_records(DxfVersion version);
    void read_multiline_attributes();
    // MTEXT's other columns and its frame as entities of their own, up to 2013 (plan phase 6,
    // B74; drawing_mtext.cpp): folded into the first text and told whose they are when read,
    // made, updated and removed when the drawing is saved (`original`: the drawing as read)
    void link_mtext_columns();
    void sync_mtext_columns(const Drawing* original);
    // The same for multiline attributes (plan B75, B76; drawing_attribute.cpp): the ATTRIBs of the
    // other lines and the XRECORDs ACAD_MLATT and ACAD_MATT_HEIGHT before 2018
    void link_multiline_attributes();
    void sync_multiline_attributes(const Drawing* original);
    // The detail and section view styles' display name and flags before 2018 (B77; drawing_object_fields.cpp):
    // read from the XRECORD of the extension dictionary, and made, changed or removed with it
    void read_view_style_records();
    void sync_view_style_records(const Drawing* original);
    // The tables' TABLECONTENT, TABLEGEOMETRY and round-trip dictionaries (step 9.1; table_sync.cpp): made for
    // a table that has none and brought up to date for one whose cells changed since `original` (the drawing
    // as read; null for a drawing built in memory), when the drawing is saved
    void sync_table_records(const Drawing* original);
    void sync_roundtrip_records();
    // The post-2010 properties of a visual style in the older forms, which AutoCAD keeps in the XRECORD of the style's
    // extension dictionary (step 9.7; visual_style.cpp): read into the style, and made, changed or removed for it
    void read_visual_style_records();
    void sync_visual_style_records();
    // A DBCOLOR for every color book color in use (B21); an error for a conflict.
    // `backup` gets objects() as it was when anything is added.
    std::string ensure_color_objects(std::vector<Object>& backup, bool& added);
    std::string named_dictionary(const std::string& name);
    // Step 9.11: the object of `type` the root dictionary has under `entry_name` (data replaced), or a new one in it
    Object& set_root_object(const std::string& entry_name, ObjectType type, ObjectData data);
    void rebuild_block_index() const;
    // A new drawing's R13 or R14 header: AutoCAD's own values where its R13/R14 files differ from R12's
    // new drawing ($MIRRTEXT, $PDMODE, $MENU …), for the variables still at R12's value (format profile D2)
    void apply_older_header_defaults();
    // After a successful reading (load, load_from_memory): the R12 template state
    void note_loaded(bool ok, DxfVersion version);
    bool write_dxf(DxfFormat format, std::string& out);
    bool write_file(const std::filesystem::path& path, DxfFormat format);
    std::string next_handle();

    DxfVersion version_ = DxfVersion::R12;
    DxfVersion source_version_ = DxfVersion::R12;   // set by load(); see write_dxf()
    bool source_binary_ = false;                      // the file read was binary DXF
    // A drawing read from an R13 or R14 file: whether its records have 330 owner groups outside
    // {ACAD_REACTORS (the form of AutoCAD 2002 … 2005 saving as R13/R14; AutoCAD's own R13 and R14
    // have none, format profile D1). A new drawing has none. Set by note_loaded().
    bool owner_groups_ = false;
    bool source_crlf_ = true;                         // ASCII line ending: CRLF as AutoCAD writes, LF only if the file read had it
    // ASCII file read: its text form where it is not AutoCAD's (code lines without padding, no line ending
    // after EOF, ...), written back as it was (plan phase 7, B12); none for a binary file
    std::shared_ptr<const internal::AsciiForm> source_form_;
    std::vector<uint64_t> loaded_handles_;            // the records the file had, sorted (B19)
    // The file's block entities: (BLOCK_RECORD, entity) handles, sorted — what a block held as
    // read, for the proxy graphics drawn from a block (B38)
    std::vector<std::pair<uint64_t, uint64_t>> loaded_block_entities_;
    // The objects a typed object owned by a hard pointer without being their owner group (a layer
    // index's IDBUFFERs): (target, owner) handles, sorted — they go with an owner removed since (B19)
    std::vector<std::pair<uint64_t, uint64_t>> loaded_hard_owned_;
    // The blocks, text styles and linetypes the file had ("B:", "S:", "L:" and the name in upper
    // case, sorted): a reference to one removed since is a removal, any other unknown name an
    // error (B36)
    std::vector<std::string> loaded_names_;
    // A drawing read from an R12 file: its preserved records are the template when it is written
    // as R12 (plan F9–F22). Set when the reading succeeded, cleared by every load.
    bool loaded_r12_ = false;
    // How many records each R12 table had when the file was read (table name, count)
    std::vector<std::pair<std::string, size_t>> loaded_table_sizes_;
    std::vector<Entity> entities_;
    std::vector<Layer> layers_;
    std::vector<Block> blocks_;
    mutable std::unordered_map<std::string, std::size_t> block_index_;  // see find_block()
    std::vector<Linetype> linetypes_;
    std::vector<TextStyle> text_styles_;
    std::vector<DimStyle> dim_styles_;
    std::vector<View> views_;
    std::vector<Ucs> ucs_list_;
    std::vector<Vport> vports_;
    std::vector<AppId> app_ids_;
    std::vector<HeaderVar> header_vars_;
    std::vector<Object> objects_;
    std::vector<RawTable> raw_tables_;
    std::vector<RawSection> raw_sections_;
    std::vector<uint8_t> thumbnail_;
    RawRecord thumbnail_raw_;
    // ACDSDATA as read (2013+, plan phase 5, B61; acds.cpp): the section's head (0 SECTION … 71),
    // its schemas and its records, in file order. A record's data is typed in the body, layout
    // or view it belongs to; the writer writes it as read while that is unchanged, and a record
    // HDxf does not type while its handle lives.
    RawRecord acds_head_;
    std::vector<RawRecord> acds_schemas_;
    std::vector<RawRecord> acds_records_;
    uint64_t next_handle_ = 0x20;
    std::filesystem::path filename_;
    std::string last_error_;
    std::vector<std::string> warnings_;
};

// ---------------------------------------------------------------------------
// ECS (Entity Coordinate System) utilities
// ---------------------------------------------------------------------------

namespace ecs {

void arbitrary_axis(const Point3& extrusion, Point3& x_axis, Point3& y_axis);
Point3 ecs_to_wcs(const Point3& ecs_point, const Point3& extrusion);
Point3 wcs_to_ecs(const Point3& wcs_point, const Point3& extrusion);

} // namespace ecs

} // namespace hdxf

// ObjectData is the std::variant it derives from for std::variant_size and std::variant_alternative
template <>
struct std::variant_size<hdxf::ObjectData> : std::variant_size<hdxf::ObjectDataVariant> {};
template <std::size_t I>
struct std::variant_alternative<I, hdxf::ObjectData> : std::variant_alternative<I, hdxf::ObjectDataVariant> {};
