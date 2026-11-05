#pragma once

// The symbol table entries, the header variables and blocks.

#include "boxed.hpp"
#include "core.hpp"
#include "fwd.hpp"

namespace hdxf {

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

} // namespace hdxf
