#pragma once

// Drawing, the facade every operation goes through. It holds its entities, tables and objects by type name only: a file
// that reaches into them takes in entity.hpp, tables.hpp or object.hpp.

#include "core.hpp"
#include "fwd.hpp"
#include "object_type.hpp"

namespace hdxf {

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
    // are made of. False when it has none (XLINE, RAY, a text of spaces, an invisible entity).
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
    Object& add_table_style(const std::string& name = "Standard");
    Object& add_table_style(const std::string& name, const TableStyleData& data);
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
    // How many entities had a preserved record when the file was read: fewer now means that some were removed, whatever was added
    size_t loaded_model_records_ = 0;
    size_t loaded_paper_records_ = 0;
    size_t loaded_block_records_ = 0;
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

} // namespace hdxf
