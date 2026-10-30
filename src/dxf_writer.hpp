#pragma once

#include "hdxf.hpp"
#include "group_io.hpp"
#include "mleader_graphics.hpp"
#include "proxy_graphics.hpp"
#include <memory>
#include <ostream>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace hdxf {
namespace internal {

// Symbol table names are case-insensitive in DXF ("STANDARD" == "Standard")
bool names_equal(const std::string& a, const std::string& b);

template <typename T>
bool has_entry_named(const std::vector<T>& entries, const std::string& name) {
    for (const auto& e : entries) {
        if (names_equal(e.name, name)) return true;
    }
    return false;
}

// A style with a TrueType/OpenType font (.ttf, .ttc, .otf, .otc), or a family without a
// shape font file
bool is_truetype_style(const TextStyle& s);

// *Model_Space and *Paper_Space, *Paper_Space0 … — the blocks of model and paper space
bool is_model_space(const std::string& block_name);
bool is_paper_space(const std::string& block_name);

// A CLASSES entry as AutoCAD writes it (application 3, proxy flags 90, entity 281;
// instance count 91 from 2004)
struct ClassEntry {
    const char* name;
    const char* cpp_name;
    const char* application;
    int flags;
    bool entity;
    int count = 0;
};

// The classes of the objects and entities the drawing has, in AutoCAD's order, with the
// number of each. LAYOUT and ACDBPLACEHOLDER are built-in types from 2004 on and not
// classes there (AutoCAD's 2004 files, plan B24). `dropped`: objects that are not written
// (their owner is gone, B19) — not counted.
std::vector<ClassEntry> classes_used(const Drawing& dwg, DxfVersion version,
                                     const std::unordered_set<uint64_t>* dropped = nullptr,
                                     const std::vector<Block>* generated = nullptr);

// The same for R13 and R14 (classes_older.cpp): R14's default classes with AutoCAD's values, `90` 32768
// in R13, no instance count
// `generated`: the blocks the writer makes (the dimensions' and their arrows), whose entities count too
std::vector<ClassEntry> older_classes_used(const Drawing& dwg, DxfVersion version,
                                           const std::unordered_set<uint64_t>* dropped = nullptr,
                                           const std::vector<Block>* generated = nullptr);

// What building a dimension's block gave for the DIMENSION itself: 10 (WCS), 11 (OCS), 42
struct BuiltDimension {
    Point3 definition_point;
    Point3 text_position;
    double measurement = 0.0;
    Point3 arc_point;                       // 16 of a 2-line angular dimension (OCS)
};

// What a MULTILEADER or MLEADERSTYLE names (dxf_writer_mleader.cpp)
enum class MLeaderRef { Block, TextStyle, Linetype };

// Where a pointer stands in a MULTILEADER's or MLEADERSTYLE's record: its part, its code, and
// which of that code's groups in the part
enum class MLeaderPart { Context, Common, Style, Line };       // Line: in any leader line
struct MLeaderSlot {
    MLeaderPart part;
    int code;
    int occurrence;
};

class DxfWriter {
public:
    DxfWriter(std::ostream& out, DxfVersion version = DxfVersion::R12,
              DxfFormat format = DxfFormat::Ascii);

    bool write(const Drawing& dwg);

    // DXF 2000: the first free handle, for the records the writer makes itself (table
    // records, standard entries the drawing lacks, dimension blocks)
    void set_next_handle(uint64_t next) { next_handle_ = next; first_handle_ = next; }
    // A drawing read from a file: how many records had no handle in it and were given one for this
    // writing (none: the file's $HANDSEED stays while nothing else changes)
    void set_handles_given(size_t count) { handles_given_ = count; }
    // ASCII with code lines and integers without padding, as the file it was read from has them
    void set_plain_ascii(bool plain) { groups_.set_plain_ascii(plain); }
    // The code line widths of the keywords that mark the structure in the ASCII file read (SECTION, its
    // name, ENDSEC, ENDTAB, EOF), where they are not AutoCAD's; written as they were
    void set_marker_columns(const std::map<std::string, int>* columns) { marker_columns_ = columns; }

    // DXF 2000 read from a file (plan B12): `original` is the drawing as read (its
    // preserved records read again, template_2000.hpp). Every record that has a preserved
    // one is written from it, with only what changed replaced.
    // `loaded`: the handles the file had when it was read (sorted), to tell a pointer to a
    // record removed since from one that led nowhere already in the file.
    // `block_entities`: the entities each block had when it was read (BLOCK_RECORD, entity).
    void set_template(const Drawing* original, bool source_binary,
                      const std::vector<uint64_t>* loaded,
                      const std::vector<std::pair<uint64_t, uint64_t>>* block_entities = nullptr) {
        original_ = original;
        source_binary_ = source_binary;
        if (loaded) {
            orig_handles_.insert(loaded->begin(), loaded->end());
            if (!loaded->empty()) orig_max_ = loaded->back();
        }
        if (block_entities) {
            for (const auto& [record, entity] : *block_entities) orig_block_entities_[record].push_back(entity);
        }
    }

    // The blocks, text styles and linetypes the file had (Drawing::loaded_names_)
    void set_loaded_names(const std::vector<std::string>* names) { loaded_names_ = names; }
    // The objects a typed object owned by a hard pointer as read (Drawing::loaded_hard_owned_)
    void set_loaded_hard_owned(const std::vector<std::pair<uint64_t, uint64_t>>* owned) { loaded_hard_owned_ = owned; }

    // R12 read from a file (set_template() without handles): how many records each table had
    // when it was read, for TABLE's 70
    void set_loaded_table_sizes(const std::vector<std::pair<std::string, size_t>>* sizes) {
        loaded_table_sizes_ = sizes;
    }

    const std::string& error() const {
        return error_.empty() ? groups_.error() : error_;
    }
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    // The typed records of phase 9 write through it (TypedRecordTypes.hpp)
    friend class RecordWriter;

    void write_code(int code, const std::string& value);
    void write_code_int(int code, int value);
    void write_code_double(int code, double value);
    void write_code_int32(int code, int32_t value);
    void write_code_typed(int code, const std::string& value);

    void write_sentinel();

    void compute_extents(const Drawing& dwg);

    void write_header(const std::vector<HeaderVar>& header_vars);
    void write_tables(const Drawing& dwg);
    void write_blocks(const std::vector<Block>& blocks);
    void write_entities(const std::vector<Entity>& entities);
    void write_eof();

    void write_block_begin_r12(const Block& block);
    void write_block_end_r12(const Block& block);
    void write_vport_table(const std::vector<Vport>& vports);
    void write_ltype_table(const std::vector<Linetype>& linetypes);
    void write_layer_table(const std::vector<Layer>& layers);
    void write_style_table(const std::vector<TextStyle>& styles);
    void write_dimstyle_table(const std::vector<DimStyle>& dimstyles);
    void write_ucs_table(const std::vector<Ucs>& ucs_list);
    void write_view_table(const std::vector<View>& views);
    void write_appid_table(const Drawing& dwg);
    // The XDATA applications the drawing uses that its APPID table lacks (every one must be
    // there, DXF reference), in order of first use
    std::vector<std::string> missing_app_ids(const Drawing& dwg) const;
    void write_default_dimstyle();

    void write_common_entity_codes(const Entity& entity, bool with_extrusion = true);
    void write_extrusion_r12(const Entity& entity);
    void write_xdata(const std::vector<XdataList>& xdata);
    void note_r12_only(const EntityProperties& p);
    void note_once(const std::string& what);
    void write_single_entity(const Entity& entity);

    void write_line(const Entity& entity);
    void write_circle(const Entity& entity);
    void write_arc(const Entity& entity);
    void write_point_entity(const Entity& entity);
    void write_text(const Entity& entity);
    void write_polyline(const Entity& entity);
    void write_polyline_head(const Entity& entity);
    void write_vertex(const Entity& polyline, const Vertex& v);
    void write_polyline_seqend(const Entity& polyline);
    void write_trace(const Entity& entity);
    void write_solid(const Entity& entity);
    void write_face3d(const Entity& entity);
    void write_shape(const Entity& entity);
    void write_insert(const Entity& entity);
    void write_insert_head(const Entity& entity);
    void write_insert_seqend(const Entity& entity);
    void write_attdef(const Entity& entity);
    void write_attrib_entity(const Entity& entity);
    void write_dimension(const Entity& entity);
    void write_viewport_entity(const Entity& entity);
    void write_unknown(const Entity& entity);

    void write_attrib(const InsertAttribute& attr, const std::string& owner_layer);

    // Dimension pseudo-block generation
    void generate_dim_blocks(const Drawing& dwg);
    DimStyle find_dim_style(const std::string& name, const Drawing& dwg) const;
    void generate_linear_dim_geometry(const DimensionData& dim, const DimStyle& style,
                                      Block& block);
    void generate_radial_dim_geometry(const DimensionData& dim, const DimStyle& style,
                                      Block& block);
    void generate_angular_dim_geometry(const DimensionData& dim, const DimStyle& style,
                                       Block& block);
    void generate_ordinate_dim_geometry(const DimensionData& dim, const DimStyle& style,
                                        Block& block);

    // ----- DXF 2000 (dxf_writer_2000.cpp) -----
    bool write_r2000(const Drawing& dwg);
    std::string new_handle();
    void prepare_r2000(const Drawing& dwg);
    using Relations = std::unordered_map<std::string, std::vector<std::string>>;
    void collect_relations(const Drawing& dwg, Relations& block_refs, Relations& reactors);
    void write_header_r2000(const Drawing& dwg);
    void write_classes_r2000(const Drawing& dwg);
    void write_class(const ClassEntry& c);
    // The DXF names of the CLASS records written, in order: a proxy's 91 is 500 + its class's position (9.3)
    std::vector<std::string> class_register_;
    void write_tables_r2000(const Drawing& dwg);
    // Whether owner groups (330 outside {ACAD_REACTORS) are written: always from 2000; in R13/R14 only for a
    // drawing read from a file that has them (AutoCAD 2002 … 2005 saving as R13/R14; format profile D1)
    bool owner_groups_written() const;
    void write_table_head(const char* name, const std::string& handle, int count);
    void write_table_entry_head(const char* type, const std::string& handle,
                                const std::string& table, const char* subclass,
                                const std::string& xdictionary = {},
                                const std::vector<std::string>* reactors = nullptr);
    void write_reactors(const std::vector<std::string>& read, const std::string& handle);
    std::string element_style_handle(const std::string& name);
    void write_vport_r2000(const Vport& vp, const std::string& handle);
    void write_ltype_r2000(const Linetype& lt, const std::string& handle);
    void write_layer_r2000(const Layer& l, const std::string& handle);
    void write_style_r2000(const TextStyle& s, const std::string& handle);
    void write_dimstyle_r2000(const DimStyle& ds, const std::string& handle);
    // R13 and R14 (dxf_writer_dimstyle.cpp): the older form, 105/5-7/270/287/340
    void write_dimstyle_legacy(const DimStyle& ds, const std::string& handle);
    // R13 and R14 (dxf_writer_older_tables.cpp): the table records without 2000's groups
    void write_vport_older(const Vport& vp, const std::string& handle);
    void write_view_older(const View& v);
    void write_ucs_older(const Ucs& u);
    void write_layer_older(const Layer& l, const std::string& handle);
    void write_block_record_r2000(const Block& b);
    void write_table_r2000(const Drawing& dwg, const std::string& name);
    void write_view_r2000(const View& v);
    void write_ucs_r2000(const Ucs& u);
    void write_appid_r2000(const AppId& a);
    void write_blocks_r2000(const Drawing& dwg);
    void write_block_r2000(const Block& b);
    void write_block_begin_r2000(const Block& b);
    void write_block_end_r2000(const Block& b);
    void dimension_geometry(const DimensionData& dim, const DimStyle& style, Block& block);
    void older_arrow_block(Block& arrow);
    void write_entities_r2000(const Drawing& dwg);
    void write_entity_r2000(const Entity& e, const std::string& owner);
    // proxy: the entity's proxy graphics (MULTILEADER), after 60 as AutoCAD writes them
    void write_entity_head(const char* type, const std::string& handle,
                           const std::string& owner, const EntityProperties& props,
                           const std::vector<uint8_t>* proxy = nullptr);
    void write_binary_chunks(const std::vector<uint8_t>& bytes);
    void write_color(const Color& c, bool aci);
    void write_ambient_color(const Color& c);
    void write_pointer(int code, const std::string& handle);
    // An attribute's locked position up to 2004 (R12 too): XDATA AcDbAttr 1070 0 1070 1
    void write_attribute_lock(bool locked);
    void write_thumbnail(const Drawing& dwg);
    void write_thickness(const EntityProperties& props);
    void write_extrusion(const EntityProperties& props);
    void write_point(int code, const Point3& p);
    // AcDbText of TEXT, ATTRIB and ATTDEF
    void write_text_body(const Point3& position, double height, const std::string& text,
                         double rotation, double width_factor, double oblique,
                         const std::string& style, int generation_flags, int h_justify,
                         int v_justify, const Point3& alignment, const EntityProperties& props);
    void write_text_r2000(const Entity& e, const std::string& owner);
    void write_polyline_r2000(const Entity& e, const std::string& owner);
    void write_insert_r2000(const Entity& e, const std::string& owner);
    // by_runs: a multiline attribute of a scaled or rotated reference is made from the text's runs
    void write_attrib_r2000(const InsertAttribute& a, const std::string& owner, bool by_runs = false);
    void write_attdef_r2000(const Entity& e, const std::string& owner);
    void write_dimension_r2000(const Entity& e, const std::string& owner);
    // AcDbDimension's groups, shared by DIMENSION, ARC_DIMENSION and LARGE_RADIAL_DIMENSION (step 9.17)
    void write_dimension_common(const Entity& e, const DimensionData& d);
    void write_viewport_r2000(const Entity& e, const std::string& owner);
    void write_viewport_older(const Entity& e, const std::string& owner, const ViewportData& d);
    void write_lwpolyline_r2000(const Entity& e, const std::string& owner);
    void write_mtext_r2000(const Entity& e, const std::string& owner);
    // MTEXT's AcDbMText groups (also the text of a multiline attribute) and, in 2018, its columns'
    // embedded object (101)
    void write_mtext_body(const MTextData& d, const EntityProperties* props);
    void write_mtext_embedded(const MTextData& d);
    void write_multiline_attribute(const MultilineAttribute& ml);
    // The layout of the text the drawing is written from (or the original, when that is written)
    TextLayout& attribute_layout();
    void write_leader_r2000(const Entity& e, const std::string& owner);
    void write_spline_r2000(const Entity& e, const std::string& owner);
    void write_spline_body(const SplineData& d, const EntityProperties& p);
    // Surfaces and HELIX (dxf_writer_surface.cpp)
    void write_matrix(int code, const std::vector<double>& m);
    void write_surface_source(int class_code, const SurfaceSource& s);
    void write_surface_r2000(const Entity& e, const std::string& owner);
    std::vector<uint8_t> helix_proxy(const HelixData& d);
    void write_helix_r2000(const Entity& e, const std::string& owner);
    // MESH (dxf_writer_mesh.cpp)
    void write_mesh_r2000(const Entity& e, const std::string& owner);
    // (LIGHT, SUN, the RAPIDRT objects and the other render objects are written by their codecs, dxf_writer_render.cpp)
    // AcDbModelerGeometry: 70 and the SAT lines (1, 3) before 2013, 290 and the GUID (2) from 2013
    void write_modeler_geometry(const AcisData& d);
    // ACDSDATA (2013+, dxf_writer_acds.cpp): the schemas and records of the drawing's bodies,
    // layouts and views; before 2013 nothing (a warning for preview images, B60)
    void write_acdsdata(const Drawing& dwg);
    // GEODATA (dxf_writer_geodata.cpp)
    void write_geodata(const GeoDataData& d);
    void write_hatch_r2000(const Entity& e, const std::string& owner);
    void write_mline_r2000(const Entity& e, const std::string& owner);
    void write_image_r2000(const Entity& e, const std::string& owner);
    void write_unknown_r2000(const Entity& e, const std::string& owner);
    // MULTILEADER and MLEADERSTYLE (dxf_writer_mleader.cpp)
    void prepare_mleaders(const Drawing& dwg);
    std::string mleader_style_of(const MLeaderData& d) const;
    std::string mleader_reference(const std::string& what, MLeaderRef kind, const std::string& name,
                                  const RawRecord& raw, const MLeaderSlot& slot);
    std::vector<std::string> mleader_style_reactors(const Object& o) const;
    void write_mleader_r2000(const Entity& e, const std::string& owner);
    void write_mleader_version(int version);
    // raw: the style's record as read (for the pointers to what has been removed since)
    void write_mleader_style(const MLeaderStyleData& s, const RawRecord& raw);
    // A MULTILEADER's proxy graphics (mleader_graphics.cpp); empty when $PROXYGRAPHICS is 0.
    // Written for the original (filter_live_ false): as the drawing as read draws it.
    std::vector<uint8_t> mleader_proxy(const Entity& e);
    // Something a read MULTILEADER draws with that has been removed since (B38)
    bool mleader_dependency_removed(const Entity& e) const;
    void write_objects_r2000(const Drawing& dwg);
    void write_object_r2000(const Object& o);
    // An object's head: its type, handle, extension dictionary, reactors (`reactors` replaces the ones it
    // holds) and owner, in the version's order (R13/R14 as AutoCAD's: reactors first, the owner only as a
    // reactor)
    void write_object_head(const char* dxf_name, const Object& o, const std::vector<std::string>* reactors);
    void write_plot_settings_r2000(const PlotSettings& p);
    std::string record_handle(const std::string& block_name) const;

    std::ostream& out_;
    DxfVersion version_;
    DxfFormat format_;
    GroupWriter groups_;
    std::string error_;

    // DXF 2000: handles the writer gives out itself
    uint64_t next_handle_ = 1;
    uint64_t first_handle_ = 1;         // next_handle_ as the writing began
    size_t handles_given_ = 0;          // records given a handle by the drawing before the writing
    std::map<std::string, std::string> space_endblk_handles_;   // R12: ENDBLK handle of $MODEL_SPACE / $PAPER_SPACE
    std::unordered_map<std::string, std::string> table_handles_;     // table name → handle
    std::unordered_map<std::string, std::string> default_handles_;   // "LTYPE/BYBLOCK" → handle
    std::unordered_map<std::string, std::string> record_handles_;    // block name (upper) → record
    Relations reactors_;            // entity → GROUPs, a leader's annotation → its LEADERs …
    Relations block_refs_;          // block → INSERTs
    std::unordered_set<std::string> entity_handles_;   // a drawing made here: its entities (upper case)
    Relations orig_reactors_;       // the same for the drawing as read (template mode)
    Relations orig_block_refs_;
    std::unordered_map<std::string, std::string> style_handles_;     // STYLE name (upper) → handle
    std::unordered_map<std::string, std::string> shape_style_handles_;  // an unnamed shape file's
                                                                        // STYLE: file (upper) → handle
    std::unordered_map<std::string, std::string> mline_style_handles_; // MLINESTYLE name (upper) → handle
    std::unordered_map<std::string, std::string> mline_style_names_;   // MLINESTYLE handle (upper) → name
    std::unordered_map<std::string, std::string> mleader_style_handles_;    // MLEADERSTYLE name (upper) → handle
    std::unordered_map<std::string, std::vector<std::string>> mleader_reactors_;  // style (upper) → MULTILEADERs
    std::unordered_set<std::string> mleader_handles_;   // every MULTILEADER (upper)
    std::unordered_map<std::string, std::string> layer_handles_;     // LAYER name (upper) → handle
    std::unordered_map<std::string, std::string> ltype_handles_;     // LTYPE name (upper) → handle
    // APPIDs the writer's own XDATA needs that the drawing lacks (AcCmTransparency, the
    // ACAD_DSTYLE_… of dimension styles): name, handle
    std::vector<std::pair<std::string, std::string>> implied_app_ids_;
    std::unordered_map<std::string, std::string> ucs_handles_;       // UCS name (upper) → handle
    void write_ucs_refs(const std::string& ucs, const std::string& base_ucs);
    std::string style_handle(const std::string& name);
    // R13 and R14 (plan phase 7, 7.x integration): the symbol tables of AutoCAD's files are upper case for what
    // the writer makes itself (STANDARD, BYLAYER, DEFPOINTS ...) and as the program named the rest. A name that
    // is written as a reference (an entity's layer, linetype or style, a header variable, a dimension style)
    // gets the spelling of the table record it names, whatever case the program used for the reference,
    // so one name is never written in two forms. Other versions: the name as it is.
    enum class SymbolKind { Layer, Linetype, Style, DimStyle, Block };
    std::string symbol_ref(SymbolKind kind, const std::string& name) const;
    void prepare_symbol_spellings(const Drawing& dwg);
    std::unordered_map<std::string, std::string> spellings_[5];     // kind -> name (upper) -> written name
    // A dynamic block definition built in memory and written before DXF 2010 is the anonymous block *U<n> with its name in
    // XDATA (step 9.9, dxf_writer_graph_block.cpp): name (upper) -> the anonymous name
    void prepare_dynamic_block_names(const Drawing& dwg);
    std::string written_name(const Block& b) const;
    std::unordered_map<std::string, std::string> dynamic_block_names_;
    std::string block_handle_for(const std::string& what, const std::string& name);
    std::string linetype_handle(const std::string& what, const std::string& name);
    std::string standard_style_handle_;     // STYLE Standard, for DIMSTYLE 340
    std::string normal_plot_style_;         // ACDBPLACEHOLDER Normal, for LAYER 390
    std::string global_material_;           // MATERIAL Global, for LAYER 347 (2007)
    std::string wireframe_style_;           // VISUALSTYLE 2dWireframe, for VPORT/VIEW/VIEWPORT 348
    int text_code_page_ = 0;                // 2000/2004: ASCII and \U+XXXX; 2007: UTF-8
    std::string active_vport_;              // VPORT *Active, the Model layout's viewport (331)

    Point3 ext_min_;
    Point3 ext_max_;
    Point3 pext_min_;
    Point3 pext_max_;
    Point3 view_min_;       // what the default *Active view shows
    Point3 view_max_;

    // Dimension block name mapping: entity pointer -> generated block name
    std::unordered_map<const Entity*, std::string> dim_block_names_;
    std::unordered_map<const Entity*, BuiltDimension> built_dimensions_;
    std::vector<Block> generated_dim_blocks_;
    bool needs_defpoints_ = false;      // built dimension blocks put points on Defpoints
    // The tables get the standard records the drawing lacks (layer 0, STANDARD, *ACTIVE, the three
    // linetypes, ACAD): not in an R13 or R14 file read, unless the writer builds something that needs them
    bool standard_entries_ = true;
    bool standard_dimstyle_needed_ = false;     // an R13/R14 file read without STANDARD: a new dimension names it
    const std::map<std::string, int>* marker_columns_ = nullptr;
    std::string marker_section_;        // the section being written, for its ENDSEC
    bool section_open_ = false;         // a "0 SECTION" is held until its name is written
    int endtabs_written_ = 0;

    // MULTILEADER proxy graphics: the drawing, its layers and linetypes as the file has
    // them, its text laid out
    const Drawing* mleader_drawing_ = nullptr;
    const Drawing* drawing_ = nullptr;      // the drawing written (2000+)
    double dimension_scale(const Entity& e) const;
    ProxyTables proxy_tables_;
    std::unique_ptr<TextLayout> proxy_layout_;
    bool proxy_graphics_ = true;        // $PROXYGRAPHICS
    // The same for the drawing as read (template mode): what the original's graphics are drawn with
    ProxyTables original_proxy_tables_;
    std::unique_ptr<TextLayout> original_proxy_layout_;
    // The size of MTEXT as drawn (2018's 42, 43), for the drawing and for the drawing as read
    std::unique_ptr<TextLayout> mtext_layout_;
    std::unique_ptr<TextLayout> original_mtext_layout_;
    bool original_proxy_graphics_ = true;
    std::unordered_map<uint64_t, std::vector<uint64_t>> orig_block_entities_;  // BLOCK_RECORD → entities

    // ----- DXF 2000 read from a file: records from their template (dxf_writer_template.cpp)
    const Drawing* original_ = nullptr;
    const std::vector<std::string>* loaded_names_ = nullptr;
    const std::vector<std::pair<uint64_t, uint64_t>>* loaded_hard_owned_ = nullptr;
    bool source_binary_ = false;
    bool changed_ = false;          // the output differs from the file read ($VERSIONGUID)
    bool filter_live_ = true;       // writing the drawing now (not the original)
    std::vector<std::string> warnings_;
    int dangling_ = 0;              // pointers kept to records that are gone (B19)
    std::unordered_set<uint64_t> live_;              // every handle that is written
    std::unordered_set<uint64_t> orig_handles_;      // every handle the file read had
    uint64_t orig_max_ = 0;                          // the largest of them
    std::unordered_set<uint64_t> dropped_objects_;   // objects whose owner is gone
    std::unordered_set<uint64_t> dropped_entities_;  // dimensional constraints whose dependency is gone
    std::unordered_map<uint64_t, const Entity*> orig_entities_;
    std::unordered_map<uint64_t, const Block*> orig_blocks_;    // by BLOCK handle
    std::unordered_map<uint64_t, const Block*> orig_records_;   // by BLOCK_RECORD handle
    std::unordered_map<uint64_t, const Object*> orig_objects_;
    std::unordered_map<std::string, std::vector<Entity>> regenerated_;  // block (upper) → geometry
    // 9.S (Codex 7, 9): the blocks of regenerated_ in the order their dimensions were found (handles are given in it), the
    // same blocks with their new geometry for the extents, and the dimensions whose block was regenerated (an ARC_DIMENSION's or
    // LARGE_RADIAL_DIMENSION's proxy graphics show the dimension as it was: not written)
    std::vector<std::string> regenerated_order_;
    std::vector<Block> regenerated_blocks_;
    std::unordered_set<const Entity*> regenerated_dimensions_;
    void prepare_dimension_regeneration(const Drawing& dwg);
public:
    // An ARC_DIMENSION or LARGE_RADIAL_DIMENSION whose block is written anew (dxf_writer_context_dimension.cpp)
    bool dimension_regenerated(const Entity& e) const { return regenerated_dimensions_.count(&e) > 0; }
private:

    bool template_mode() const { return original_ != nullptr; }
    static int header_code_page(const Drawing& dwg, int fallback);

    // ----- R12 read from a file (plan F9–F22; dxf_writer_template.cpp): every record is written
    // from the one the file had (T), with only what differs between the drawing now (N) and the
    // drawing as read (O) written anew. Table entries, blocks and entities are matched with the
    // drawing as read by their place among the records the file had (R12 has no handles on most).
    bool r12_template() const { return original_ != nullptr && version_ == DxfVersion::R12; }
    const std::vector<std::pair<std::string, size_t>>* loaded_table_sizes_ = nullptr;
    std::unordered_map<const void*, const void*> r12_original_;   // a record of the drawing → the same as read
    bool model_extents_changed_ = false;        // the drawing's model / paper space extents against the ones as read
    bool paper_extents_changed_ = false;
    template <typename T>
    const T* original_of(const T& x) const {
        auto it = r12_original_.find(&x);
        return it == r12_original_.end() ? nullptr : static_cast<const T*>(it->second);
    }
    template <typename T>
    void pair_originals(const std::vector<T>& now, const std::vector<T>& was);
    void pair_entities(const std::vector<Entity>& now, const std::vector<Entity>& was);
    void prepare_r12_template(const Drawing& dwg);
    void emit_r12(const RawRecord& t, const std::function<void()>& now, const std::function<void()>& orig);
    // A record with a preserved record is written from it (emit_r12), any other as `write_one` writes it
    template <typename T, typename F>
    void write_entry_r12(const T& x, const RawRecord& raw, F&& write_one);
    bool table_in_file(const std::string& name) const;
    bool table_wanted(const std::string& name, bool empty) const;
    size_t records_read(const std::string& name) const;
    void write_table_head_r12(const std::string& name, int count);
    void write_entity_r12(const Entity& e);

    bool is_live(const std::string& handle) const;
    void prepare_template(const Drawing& dwg);
    void prepare_owners_built_in_memory(const Drawing& dwg);
    bool drop_constraint_dimensions(const Drawing& dwg);
    // One element (entity with its VERTEX/ATTRIB/SEQEND, table entry, BLOCK, object …):
    // `now` writes it as it is, `orig` as it was read (null: no counterpart). With a
    // template every record is written from it, else as `now` writes it.
    void emit(const std::vector<const RawRecord*>& templates, bool entity,
              const std::function<void()>& now, const std::function<void()>& orig);
    void merge_record(const std::vector<DxfGroup>& t, const std::vector<CapturedGroup>* o,
                      const std::vector<CapturedGroup>& n, bool entity);
    void write_raw_record(const RawRecord& record, int added_to_count = 0);
    void write_table_head_template(const RawTable& table, int added = 0);
    void write_header_template(const Drawing& dwg);
    void write_classes_template(const Drawing& dwg, const RawSection& classes);
    void write_classes_template_without_section(const Drawing& dwg);
    std::vector<ClassEntry> classes_added_since_read(const Drawing& dwg, const std::set<std::string>& have,
                                                     const std::set<std::string>& have_cpp);
    void write_entity_template(const Entity& e, const std::string& owner);
    void write_raw_sections_after_objects(const Drawing& dwg);
};

template <typename T, typename F>
void DxfWriter::write_entry_r12(const T& x, const RawRecord& raw, F&& write_one) {
    if (!r12_template() || raw.empty()) {
        write_one(x);
        return;
    }
    const T* was = original_of(x);
    std::function<void()> orig;
    if (was) orig = [&write_one, was] { write_one(*was); };
    emit_r12(raw, [&write_one, &x] { write_one(x); }, orig);
}

template <typename T>
void DxfWriter::pair_originals(const std::vector<T>& now, const std::vector<T>& was) {
    size_t with_raw = 0;
    for (const auto& x : now) if (!x.raw.empty()) ++with_raw;
    if (with_raw != was.size()) return;         // not the same records: nothing to compare with
    size_t k = 0;
    for (const auto& x : now) {
        if (!x.raw.empty()) r12_original_[&x] = &was[k++];
    }
}

// The handle of a material (ByLayer, Global …) in ACAD_MATERIAL and of a visual style
// (2dWireframe …) in ACAD_VISUALSTYLE; empty when the drawing has none (drawing_r2007.cpp)
std::string material_handle(const Drawing& dwg, const std::string& name);
std::string visual_style_handle(const Drawing& dwg, const std::string& name);

// $XCLIPFRAME in the header as the version writes it: its code, a 2 as 0 in 2004/2007 (the
// DICTIONARYVAR has it), none in 2000 (dxf_writer_2000.cpp, B43)
void xclipframe_as_written(std::vector<HeaderVar>& vars, DxfVersion version);
void maintenance_as_written(std::vector<HeaderVar>& vars, DxfVersion version);

// A new GUID as AutoCAD writes them: {8-4-4-4-12}, upper-case hex (drawing_r2000.cpp)
std::string new_guid();

// The login name, as AutoCAD writes it in $LASTSAVEDBY (2004+), UTF-8 (drawing_r2000.cpp)
std::string login_name();

// Every color the drawing's records have that can be a true color or a color book color
// in 2004: layers, and entities, attributes, MTEXT backgrounds and gradient colors (in
// ENTITIES and blocks) — `entity` tells which (drawing_r2000.cpp)
void for_each_color(const Drawing& dwg, const std::function<void(const Color&, bool entity)>& f);

} // namespace internal
} // namespace hdxf
