#pragma once

// The object data ObjectData holds directly, which a file that has Object needs complete.

#include "core.hpp"
#include "proxy.hpp"

namespace hdxf {

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

struct UnderlayDefinitionData {
    std::string file_name;              // 1: the file's path as AutoCAD stores it (UTF-8; relative or absolute)
    std::string item_name;              // 2: the page of a PDF (its number as text), a DWF sheet, a DGN model
};

// SECTION_MANAGER (the NOD's ACAD_SECTION_MANAGER): AutoCAD 2025 writes 70 only (its sections are not listed there:
// the live sections' are, in AutoCAD 2018's LiveSection file and with a count of 0 in ACadSharp's 2013 sample)
struct SectionManagerData {
    bool needs_update = true;           // 70 (requires full update)
    std::vector<std::string> sections;  // 90 counts them (written when there are some), 330 each: soft pointers to SECTIONOBJECTs
};

struct UnknownObjectData {
    std::string object_type;
    std::vector<std::pair<int, std::string>> group_codes;   // after the owner, before XDATA
};

// The enumerations of RAPIDRTRENDERSETTINGS
enum class RenderDurationType { ByTime = 0, ByLevel = 1, UntilSatisfactory = 2 };
enum class RenderAccuracy { Low = 0, Draft = 1, High = 2 };
enum class SamplingFilter { Box = 0, Triangle = 1, Gauss = 2, Mitchell = 3, Lanczos = 4 };

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

// FIELDLIST (AcDbIdSet, AcDbFieldList): the fields of a drawing, as handles (330, soft pointers). 90 is the number of
// them: in every AutoCAD file it equals the handles listed, also when some of them are no longer there. An entry
// for a field HDxf removes goes, and 90 follows.
struct FieldListData {
    std::vector<std::string> fields;    // 330
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

} // namespace hdxf
