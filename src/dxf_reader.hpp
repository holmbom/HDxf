#pragma once

#include "hdxf.hpp"
#include "group_io.hpp"
#include "text_encoding.hpp"
#include "mtext_data.hpp"
#include <istream>
#include <map>

namespace hdxf {
namespace internal {

// The same for a multiline attribute (plan phase 6, B75): how many embedded objects (101) have
// begun, and its text's state
struct MultilineParseState {
    int embedded = 0;
    MTextParseState text;
};

class DxfReader {
public:
    DxfReader(std::istream& in);

    bool read(Drawing& dwg);
    DxfVersion detected_version() const { return version_; }
    bool binary() const { return groups_.binary(); }
    const std::string& error() const { return error_; }

private:
    bool read_group(GroupCode& gc);     // skips comments (999)
    void push_back(const GroupCode& gc);

    // R13+ interpretation filter
    bool interpret(const GroupCode& gc);
    // 71/72 are generation flags / horizontal justification only in AcDbText (or in
    // R12, which has no subclasses); in AcDbAttribute 2018 they mean something else
    bool text_subclass() const { return subclass_.empty() || subclass_ == "AcDbText"; }

    // MTEXT (plan phase 6, B72/B73/B75; mtext_data.hpp). The text whole: parts joined and decoded,
    // the rotation as a direction in the OCS of `extrusion`
    void finish_mtext(MTextData& d, MTextParseState& state, const Point3& extrusion);
    // A group of an ATTRIB/ATTDEF that belongs to a multiline attribute (71, 72, 340 and 11 of
    // AcDbAttribute, the embedded object): true when it was one
    bool multiline_group(OptionalBoxed<MultilineAttribute>& ml, MultilineParseState& state, const GroupCode& gc);
    void finish_multiline(OptionalBoxed<MultilineAttribute>& ml, MultilineParseState& state, const Point3& extrusion);

    // Record capture (DXF 2000+)
    void capture(const GroupCode& gc);
    RawRecord take_record();
    // The extension dictionary (360 in 102 {ACAD_XDICTIONARY) of a record's head, before its
    // first subclass; empty = none
    static std::string head_xdictionary(const RawRecord& record);
    // Its reactors (330 in 102 {ACAD_REACTORS), likewise
    static std::vector<std::string> head_reactors(const RawRecord& record);
    void stop_capture();
    void keep_strays(Drawing& dwg, const std::string& section);
    void capture_section(Drawing& dwg, const std::string& name);
    void capture_table(RawTable& table);

    // OBJECTS and BLOCK_RECORD, from the preserved records (object_reader.cpp)
    bool decode_groups(const RawRecord& record, std::vector<DxfGroup>& out);
    void parse_objects(Drawing& dwg);
    bool parse_object(const RawRecord& record, Object& out, bool* owner_from_reactors = nullptr);
    void link_block_records(Drawing& dwg);
    // THUMBNAILIMAGE (2000+): its bytes to thumbnail(), the section to thumbnail_raw()
    void parse_thumbnail(Drawing& dwg);
    // ACDSDATA (2013+, plan phase 5, B61; acds.cpp): the section's records as read, then their data
    // to the bodies, layouts and views they belong to
    void parse_acdsdata(Drawing& dwg);
    void type_acdsdata(Drawing& dwg);
    // A layer's XDATA from its preserved record, its AcCmTransparency as the field (B22)
    void read_layer_xdata(Layer& layer);
    void read_style_xdata(TextStyle& style);
    void read_entry_head(const RawRecord& raw, std::string* xdictionary, std::vector<std::string>* reactors,
                         std::vector<XdataList>* xdata);
    // R13+ pointers to table records (DIMSTYLE 340-344, LTYPE 340, VIEWPORT 331) as names
    void resolve_names(Drawing& dwg);

    void parse_header(Drawing& dwg);
    void parse_tables(Drawing& dwg);
    void parse_entities(std::vector<Entity>& entities);
    void parse_blocks(Drawing& dwg);
    void skip_section();
    void skip_table();

    void parse_layer_table(std::vector<Layer>& layers);
    void parse_ltype_table(std::vector<Linetype>& linetypes);
    void parse_style_table(std::vector<TextStyle>& styles);
    void parse_view_table(std::vector<View>& views);
    void parse_ucs_table(std::vector<Ucs>& ucs_list);
    void parse_vport_table(std::vector<Vport>& vports);
    void parse_dimstyle_table(std::vector<DimStyle>& dimstyles);
    void parse_appid_table(std::vector<AppId>& app_ids);

    bool parse_single_entity(const GroupCode& type_gc, Entity& out);
    bool parse_entity_record(const GroupCode& type_gc, Entity& out);

    Entity parse_line();
    Entity parse_circle();
    Entity parse_arc();
    Entity parse_point();
    Entity parse_text();
    Entity parse_polyline();
    Entity parse_trace();
    Entity parse_solid();
    Entity parse_3dface();
    Entity parse_shape();
    Entity parse_insert();
    Entity parse_attdef();
    Entity parse_attrib();
    Entity parse_dimension();
    Entity parse_viewport();
    Entity parse_lwpolyline();
    Entity parse_ellipse();
    Entity parse_xline(bool ray);
    Entity parse_mtext();
    Entity parse_tolerance();
    Entity parse_leader();
    Entity parse_spline();
    Entity parse_hatch();
    Entity parse_mline();
    Entity parse_image();
    Entity parse_acis(EntityType type);
    Entity parse_mleader();
    Entity parse_surface(EntityType type);
    Entity parse_helix();
    Entity parse_mesh();
    // A SPLINE's group (AcDbSpline) into d; false when it is not one of its codes
    static bool spline_group(SplineData& d, const GroupCode& gc);
    bool decode_string(std::string& value, int code);
    void note_marker(const GroupCode& gc);
    Entity parse_unknown(const std::string& entity_type);

    void parse_common_entity_codes(Entity& entity, const GroupCode& gc);
    void parse_xdata(Entity& entity, const GroupCode& first_gc);

    GroupReader groups_;
    GroupCode pending_;
    bool has_pending_ = false;
    // The code line widths of the keywords that mark the structure (SECTION, its name, ENDSEC, ENDTAB, EOF)
    // where they are not AutoCAD's: written back as they were (Drawing::marker_columns_). Keys are
    // "SECTION:<name>", "NAME:<name>", "ENDSEC:<name>", "ENDTAB#<n>" and "EOF"
    std::map<std::string, int> markers_;
    std::string section_;               // the section being read
    int endtabs_ = 0;                   // ENDTAB groups read
    // The last group's value as long as it stands in the file (before carets and the code page
    // are undone): a SAT line of 255 characters so written is continued by a 3 after it
    size_t written_size_ = 0;
    // The DXF names of the CLASS records, in the file's order: a proxy's class is the one its 91 names (step 9.S)
    std::vector<std::string> class_names_;

    bool r13_plus_ = false;
    bool capture_ = true;               // until the file turns out to be R12
    bool keep_text_encoded_ = false;    // MTEXT: 1 and 3 decoded once joined (B16)
    int code_page_ = 1252;                  // $DWGCODEPAGE; UTF-8 from 2007
    std::string code_page_name_ = "ANSI_1252";
    bool in_102_ = false;
    bool in_embedded_ = false;
    bool want_embedded_ = false;        // the parser takes an embedded object (101): MTEXT, ATTRIB, ATTDEF
    std::string subclass_;
    RawRecord open_record_;
    RawRecord finished_record_;
    std::vector<RawRecord> strays_;
    DxfVersion version_ = DxfVersion::R12;
    std::string error_;
    // What a read recovered from or could not do (a typed record that was kept as it was read)
    std::vector<std::string> warnings_;
};

} // namespace internal
} // namespace hdxf
