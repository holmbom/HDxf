#include "dxf_writer.hpp"
#include "mtext_data.hpp"
#include "attribute_mirror.hpp"
#include "header_defaults.hpp"
#include "handles.hpp"
#include "text_encoding.hpp"
#include "proxy_graphics.hpp"
#include "extents.hpp"
#include "roundtrip.hpp"
#include "dimension_block.hpp"
#include "dimstyle_legacy.hpp"
#include "viewport_mview.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <sstream>

// Writing DXF 2000 (AC1015) — plan phase 1, step 1.2. Every record has a handle and an
// owner (330), and the groups follow the subclass markers (100) in the order AutoCAD
// writes them (doc/plans/2026-10-23-fas-1-dxf-2000.md, "Vad AutoCAD skriver i 2000").
// Text is ASCII with \U+XXXX for everything else, numbers as AutoCAD writes them (B16).
// 2004 (phase 2) and 2007 (phase 3) are this writer with the version deciding what differs;
// 2007's text is UTF-8 (B31).

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hdxf {
namespace internal {

bool is_model_space(const std::string& block_name) {
    return names_equal(block_name, "*Model_Space");
}

bool is_paper_space(const std::string& block_name) {
    static const std::string prefix = "*Paper_Space";
    return block_name.size() >= prefix.size() &&
           names_equal(block_name.substr(0, prefix.size()), prefix);
}

static std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// The name a block is written with. AutoCAD's R13 and R14 files name the model and paper space
// blocks in capitals (*MODEL_SPACE, *PAPER_SPACE, plan phase 7, B89); the API's name stays
// *Model_Space (find_block). A block read from a file keeps the name it had.
static std::string written_block_name(const Block& b, DxfVersion version) {
    if (version < DxfVersion::R2000 && b.raw.empty() &&
        (is_model_space(b.name) || is_paper_space(b.name))) {
        return upper(b.name);
    }
    return b.name;
}

// AcCmTransparency 1071 0: what AutoCAD writes on some layers that have no transparency
static bool is_no_transparency(const XdataList& x) {
    return x.app_name == "AcCmTransparency" && x.items.size() == 1 && x.items[0].code == 1071 &&
           std::atol(x.items[0].value.c_str()) == 0;
}

static bool is_zero(const Point3& p) {
    return p.x == 0.0 && p.y == 0.0 && p.z == 0.0;
}

static bool is_x_axis(const Point3& v) {
    return v.x == 1.0 && v.y == 0.0 && v.z == 0.0;
}

std::string DxfWriter::new_handle() {
    if (next_handle_ == UINT64_MAX) {
        if (error_.empty()) error_ = "Out of handles: the drawing uses handles up to FFFFFFFFFFFFFFFF";
        return "0";
    }
    return format_handle(next_handle_++);
}

// The name a block is written with: the anonymous name of a dynamic block definition before 2010 (step 9.9), else
// written_block_name()
std::string DxfWriter::written_name(const Block& b) const {
    if (!dynamic_block_names_.empty()) {
        auto it = dynamic_block_names_.find(upper(b.name));
        if (it != dynamic_block_names_.end()) return it->second;
    }
    return written_block_name(b, version_);
}

std::string DxfWriter::symbol_ref(SymbolKind kind, const std::string& name) const {
    if (kind == SymbolKind::Block && !dynamic_block_names_.empty()) {
        auto it = dynamic_block_names_.find(upper(name));
        if (it != dynamic_block_names_.end()) return it->second;
    }
    if (version_ >= DxfVersion::R2000 || version_ == DxfVersion::R12 || name.empty()) return name;
    const auto& table = spellings_[static_cast<int>(kind)];
    auto it = table.find(upper(name));
    return it != table.end() ? it->second : name;
}

// The spelling every table record is written with in R13 and R14: the program's own records as named,
// the standard ones the writer makes when the drawing has none in capitals (DxfWriter::write_table_r2000)
void DxfWriter::prepare_symbol_spellings(const Drawing& dwg) {
    if (version_ >= DxfVersion::R2000 || version_ == DxfVersion::R12) return;
    auto table = [&](SymbolKind kind) -> std::unordered_map<std::string, std::string>& {
        auto& t = spellings_[static_cast<int>(kind)];
        t.clear();
        return t;
    };
    auto& layers = table(SymbolKind::Layer);
    for (const auto& l : dwg.layers()) layers.emplace(upper(l.name), l.name);
    layers.emplace("0", "0");
    if (needs_defpoints_) layers.emplace("DEFPOINTS", "DEFPOINTS");
    auto& linetypes = table(SymbolKind::Linetype);
    for (const auto& lt : dwg.linetypes()) linetypes.emplace(upper(lt.name), lt.name);
    for (const char* lt : {"BYBLOCK", "BYLAYER", "CONTINUOUS"}) linetypes.emplace(lt, lt);
    auto& styles = table(SymbolKind::Style);
    for (const auto& s : dwg.text_styles()) if (!s.name.empty()) styles.emplace(upper(s.name), s.name);
    styles.emplace("STANDARD", "STANDARD");
    auto& dim_styles = table(SymbolKind::DimStyle);
    for (const auto& ds : dwg.dim_styles()) dim_styles.emplace(upper(ds.name), ds.name);
    if (!template_mode() || standard_dimstyle_needed_) dim_styles.emplace("STANDARD", "STANDARD");
    auto& blocks = table(SymbolKind::Block);
    for (const auto& b : dwg.blocks()) blocks.emplace(upper(b.name), written_block_name(b, version_));
    for (const auto& b : generated_dim_blocks_) blocks.emplace(upper(b.name), written_block_name(b, version_));
}

std::string DxfWriter::record_handle(const std::string& block_name) const {
    auto it = record_handles_.find(upper(block_name));
    return it != record_handles_.end() ? it->second : std::string();
}

// Table records named in the API are handles in the file; a name that does not exist
// is an error (the file would point nowhere)
std::string DxfWriter::style_handle(const std::string& name) {
    if (name.empty()) return standard_style_handle_;
    auto it = style_handles_.find(upper(name));
    if (it != style_handles_.end()) return it->second;
    if (error_.empty()) error_ = "Text style \"" + name + "\" does not exist";
    return standard_style_handle_;
}

// A linetype element's style (340): by its name, or — a shape file's style, which has no name
// (70 bit 1) — by its file
std::string DxfWriter::element_style_handle(const std::string& name) {
    auto it = style_handles_.find(upper(name));
    if (it == style_handles_.end() || name.empty()) {
        auto file = shape_style_handles_.find(upper(name));
        if (file != shape_style_handles_.end()) return file->second;
    }
    return style_handle(name);
}

// 345 (a named UCS) and 346 (the base UCS) of VPORT, VIEW and VIEWPORT
void DxfWriter::write_ucs_refs(const std::string& ucs, const std::string& base_ucs) {
    for (const auto& [code, name] : {std::pair<int, const std::string*>{345, &ucs},
                                     std::pair<int, const std::string*>{346, &base_ucs}}) {
        if (name->empty()) continue;
        auto it = ucs_handles_.find(upper(*name));
        if (it != ucs_handles_.end()) {
            write_code(code, it->second);
        } else if (error_.empty()) {
            error_ = "UCS \"" + *name + "\" does not exist";
        }
    }
}

std::string DxfWriter::linetype_handle(const std::string& what, const std::string& name) {
    auto it = ltype_handles_.find(upper(name));
    if (it != ltype_handles_.end()) return it->second;
    if (error_.empty()) error_ = what + ": linetype \"" + name + "\" does not exist";
    return "0";
}

// The AcCmColor AutoCAD 2000/2004 write as binary XDATA (1004): 24 bytes, the color in
// bytes 8-11 as a little-endian 0xMMRRGGBB (method 0xC0 ByLayer, 0xC1 ByBlock, 0xC2 RGB,
// 0xC3 ACI) — as in ACadSharp's sample_AC1018 (DIMTFILLCLR)
static std::string color_binary(const Color& c) {
    uint32_t value = c.has_rgb() ? 0xC2000000u | static_cast<uint32_t>(c.rgb)
                   : c.aci == 0 ? 0xC1000000u
                   : c.aci == 256 ? 0xC0000000u
                   : 0xC3000000u | static_cast<uint32_t>(c.aci & 0xFF);
    static const char* digits = "0123456789ABCDEF";
    std::string out = "1800000001000000";
    for (int i = 0; i < 4; ++i) {
        uint8_t b = static_cast<uint8_t>(value >> (8 * i));
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    out += std::string(24, '0');
    return out;
}

// A dimension style's newer variables as AutoCAD 2000/2004 keep them: each in its own
// application, 1070 <the variable's number>, then its value — those that differ from
// AutoCAD's own. `handle` gives a linetype's handle.
static std::vector<XdataList> dimstyle_variables(const DimStyle& ds,
                                                 const std::function<std::string(const std::string&)>& handle) {
    const DimStyle d0 = autocad_dim_style();
    std::vector<XdataList> out;
    const Color& c = ds.dimtfillclr;
    const Color& c0 = d0.dimtfillclr;
    if (ds.dimtfill != d0.dimtfill || c.aci != c0.aci || c.rgb != c0.rgb) {
        out.push_back({"ACAD_DSTYLE_DIMTEXT_FILL", {{1070, "376"}, {1070, std::to_string(ds.dimtfill)},
                                                   {1070, "377"}, {1004, color_binary(c)}}});
    }
    const std::tuple<const char*, const char*, const std::string*> linetypes[] = {
        {"ACAD_DSTYLE_DIM_LINETYPE", "380", &ds.dimltype},
        {"ACAD_DSTYLE_DIM_EXT1_LINETYPE", "381", &ds.dimltex1},
        {"ACAD_DSTYLE_DIM_EXT2_LINETYPE", "382", &ds.dimltex2},
    };
    for (const auto& [app, number, name] : linetypes) {
        if (name->empty()) continue;
        out.push_back({app, {{1070, number}, {1005, handle(*name)}}});
    }
    if (ds.dimfxlon != d0.dimfxlon) {
        out.push_back({"ACAD_DSTYLE_DIMEXT_ENABLED", {{1070, "383"}, {1070, std::to_string(ds.dimfxlon)}}});
    }
    if (ds.dimfxl != d0.dimfxl) {
        out.push_back({"ACAD_DSTYLE_DIMEXT_LENGTH",
                       {{1070, "378"}, {1040, format_double(ds.dimfxl, NumberStyle::AutoCad)}}});
    }
    if (ds.dimjogang != d0.dimjogang) {
        out.push_back({"ACAD_DSTYLE_DIMJOGGED_JOGANGLE_SYMBOL",
                       {{1070, "384"}, {1040, format_double(ds.dimjogang * M_PI / 180.0, NumberStyle::AutoCad)}}});
    }
    return out;
}

std::string DxfWriter::block_handle_for(const std::string& what, const std::string& name) {
    std::string record = record_handle(name);
    if (record.empty() && error_.empty()) {
        error_ = what + ": block \"" + name + "\" does not exist";
    }
    return record;
}

void DxfWriter::write_point(int code, const Point3& p) {
    groups_.write_double(code, p.x);
    groups_.write_double(code + 10, p.y);
    groups_.write_double(code + 20, p.z);
}

// ---------------------------------------------------------------------------
// Top level
// ---------------------------------------------------------------------------

// A dimension's DIMLFAC, by its style with its overrides (a negative one only in paper space);
// 1 for an angle. The original's for the original (written for comparison)
double DxfWriter::dimension_scale(const Entity& e) const {
    const DimensionData& d = *e.dimension_part();
    if (d.dim_type == DimensionType::Angular || d.dim_type == DimensionType::Angular3Pt) return 1.0;
    const Drawing* dwg = !filter_live_ && template_mode() && original_ ? original_ : drawing_;
    if (!dwg) return 1.0;
    double factor = internal::dimension_style(*dwg, e).dimlfac;
    if (factor < 0.0) factor = e.props.paper_space != 0 ? -factor : 1.0;
    return factor;
}

bool DxfWriter::write_r2000(const Drawing& dwg) {
    drawing_ = &dwg;
    // B16: everything outside ASCII as \U+XXXX, as AutoCAD writes 2000 and 2004 (code page 0
    // = only ASCII as bytes); 2007 in UTF-8, the file read too (B31); numbers as AutoCAD
    // writes them
    text_code_page_ = version_ >= DxfVersion::R2007 ? utf8_code_page : 0;
    groups_.set_code_page(text_code_page_);
    groups_.set_number_style(NumberStyle::AutoCad);
    if (version_ >= DxfVersion::R2007) {
        groups_.set_source_code_page(utf8_code_page);
    } else if (const HeaderVar* page = dwg.find_header("$DWGCODEPAGE"); page && !page->values.empty()) {
        // The preserved strings are in the code page the file had; converted if $DWGCODEPAGE changed
        int now = code_page_from_name(page->values[0].second);
        groups_.set_source_code_page(original_ ? header_code_page(*original_, now) : now);
        groups_.set_raw_code_page(now);
    }

    generate_dim_blocks(dwg);
    // A read dimension that changed gets its block anew before the extents are taken: they count the geometry written (9.S)
    if (template_mode()) prepare_dimension_regeneration(dwg);
    compute_extents(dwg);
    prepare_r2000(dwg);
    prepare_symbol_spellings(dwg);
    prepare_dynamic_block_names(dwg);
    if (template_mode()) prepare_template(dwg);
    else prepare_owners_built_in_memory(dwg);
    if (!error_.empty()) return false;

    // The header comes first in the file but is written last: $HANDSEED and (for a
    // drawing read from a file) $VERSIONGUID depend on everything else
    std::ostringstream body(std::ios::out | std::ios::binary);
    groups_.set_stream(body);
    const RawSection* classes = nullptr;
    for (const auto& s : dwg.raw_sections()) if (s.name == "CLASSES") classes = &s;
    if (template_mode() && classes) write_classes_template(dwg, *classes);
    else if (template_mode() && version_ < DxfVersion::R2000) write_classes_template_without_section(dwg);
    else write_classes_r2000(dwg);
    write_tables_r2000(dwg);
    write_blocks_r2000(dwg);
    write_entities_r2000(dwg);
    write_objects_r2000(dwg);
    write_thumbnail(dwg);
    if (template_mode()) write_raw_sections_after_objects(dwg);
    write_acdsdata(dwg);
    write_eof();

    groups_.set_stream(out_);
    if (format_ == DxfFormat::Binary) write_sentinel();
    bool has_header = false;
    for (const auto& s : dwg.raw_sections()) has_header = has_header || s.name == "HEADER";
    if (template_mode() && has_header) write_header_template(dwg);
    else write_header_r2000(dwg);
    out_ << body.str();

    if (dangling_ > 0) {
        warnings_.push_back(std::to_string(dangling_) +
                            " pointer(s) to records that no longer exist set to none (0)");
    }
    return out_.good() && groups_.error().empty() && error_.empty();
}

// The INSERTs of each block, and the records that are reactors of another: a leader's annotation,
// a hatch's boundary objects and a group's members (DXF reference, GROUP)
void DxfWriter::collect_relations(const Drawing& dwg, Relations& block_refs, Relations& reactors) {
    auto collect_inserts = [&](const std::vector<Entity>& entities) {
        for (const auto& e : entities) {
            if (e.type == EntityType::Insert) {
                block_refs[upper(e.as_insert().block_name)].push_back(e.props.handle);
            }
            if (e.type == EntityType::Leader && !e.as_leader().annotation_handle.empty()) {
                reactors[upper(e.as_leader().annotation_handle)].push_back(e.props.handle);
            }
            if (e.type == EntityType::Hatch) {
                for (const auto& path : e.as_hatch().paths) {
                    for (const auto& h : path.source_handles) reactors[upper(h)].push_back(e.props.handle);
                }
            }
        }
    };
    collect_inserts(dwg.entities());
    for (const auto& b : dwg.blocks()) collect_inserts(b.entities);
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::Group) continue;
        for (const auto& member : o.as_group().entities) reactors[upper(member)].push_back(o.handle);
    }
    // The typed records that are reactors of what they point to (phase 9 families)
    collect_typed_reactors(dwg, reactors);
}

// Handles for everything the writer makes itself, before anything is written: $HANDSEED
// comes first in the file and must be above all of them (plan 1.2, order of writing)
void DxfWriter::prepare_r2000(const Drawing& dwg) {
    // A drawing read from a file keeps its TABLE records (and their handles)
    if (template_mode()) {
        for (const auto& t : dwg.raw_tables()) {
            for (const auto& g : t.header.groups()) {
                if (g.code == 5) { table_handles_[t.name] = g.value; break; }
            }
        }
    }
    for (const char* table : {"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW", "UCS", "APPID",
                              "DIMSTYLE", "BLOCK_RECORD"}) {
        if (table_handles_[table].empty()) table_handles_[table] = new_handle();
    }

    // Standard entries the drawing does not have. An R13 or R14 file read is as it was: a table it had
    // without the layer 0, STANDARD or *ACTIVE gets none made (plan B12, B96) unless the writer builds
    // something of its own that uses them (dimension blocks)
    standard_entries_ = !(template_mode() && version_ < DxfVersion::R2000) || !generated_dim_blocks_.empty();
    if (standard_entries_ && dwg.vports().empty()) default_handles_["VPORT/*ACTIVE"] = new_handle();
    for (const char* lt : {"BYBLOCK", "BYLAYER", "CONTINUOUS"}) {
        if (standard_entries_ && !has_entry_named(dwg.linetypes(), lt)) default_handles_[std::string("LTYPE/") + lt] = new_handle();
    }
    if (standard_entries_ && !has_entry_named(dwg.layers(), "0")) default_handles_["LAYER/0"] = new_handle();
    if (needs_defpoints_) default_handles_["LAYER/DEFPOINTS"] = new_handle();
    if (standard_entries_ && !has_entry_named(dwg.text_styles(), "Standard")) default_handles_["STYLE/STANDARD"] = new_handle();
    if (standard_entries_ && !has_entry_named(dwg.app_ids(), "ACAD")) default_handles_["APPID/ACAD"] = new_handle();
    // APPIDs of the writer's own XDATA: a layer's transparency is XDATA of AcCmTransparency
    // (B22), a dimension style's newer variables of ACAD_DSTYLE_… (R.7)
    std::vector<std::string> apps;
    bool transparent_layer = std::any_of(dwg.layers().begin(), dwg.layers().end(),
                                         [](const Layer& l) { return !l.transparency.is_by_layer(); });
    if (transparent_layer) apps.push_back("AcCmTransparency");
    // … and before 2013 a body's change GUID of ACAD_OBJECT_CHANGE_GUID (B60)
    if (version_ < DxfVersion::R2013) {
        bool guid = false;
        auto any_guid = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) {
                const AcisData* acis = acis_of(e);
                guid = guid || (acis && !acis->guid.empty());
            }
        };
        any_guid(dwg.entities());
        for (const auto& b : dwg.blocks()) any_guid(b.entities);
        if (guid) apps.push_back("ACAD_OBJECT_CHANGE_GUID");
    }
    for (const auto& app : missing_app_ids(dwg)) apps.push_back(app);
    for (const auto& ds : dwg.dim_styles()) {
        if (version_ >= DxfVersion::R2007) break;          // group codes in 2007 (B33)
        for (const auto& x : dimstyle_variables(ds, [](const std::string&) { return std::string(); })) {
            apps.push_back(x.app_name);
        }
    }
    for (const auto& app : apps) {
        if (has_entry_named(dwg.app_ids(), app)) continue;
        bool listed = std::any_of(implied_app_ids_.begin(), implied_app_ids_.end(),
                                  [&](const auto& a) { return names_equal(a.first, app); });
        if (!listed) implied_app_ids_.push_back({app, new_handle()});
    }
    for (const auto& l : dwg.layers()) {
        bool twice = !l.transparency.is_by_layer() &&
                     std::any_of(l.xdata.begin(), l.xdata.end(),
                                 [](const XdataList& x) {
                                     return x.app_name == "AcCmTransparency" && !is_no_transparency(x);
                                 });
        if (twice && error_.empty()) {
            error_ = "Layer \"" + l.name + "\" has its transparency both in xdata and in transparency";
        }
    }
    // A default Standard for a drawing built in memory; a file without one (LibreDWG's
    // example_2010/2013, saved by AutoCAD, has only ISO-25) is written as read
    // (an R13 or R14 file read that has no STANDARD gets it when a dimension made since names it: AutoCAD's
    // drawing always has the style, and the dimension's name must lead to a record)
    standard_dimstyle_needed_ = false;
    if (template_mode() && version_ < DxfVersion::R2000 && !has_entry_named(dwg.dim_styles(), "Standard")) {
        auto uses = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) {
                if (e.type == EntityType::Dimension && e.raw.empty() && names_equal(e.as_dimension().dim_style, "Standard")) {
                    standard_dimstyle_needed_ = true;
                }
            }
        };
        uses(dwg.entities());
        for (const auto& b : dwg.blocks()) uses(b.entities);
    }
    if ((!template_mode() || standard_dimstyle_needed_) && !has_entry_named(dwg.dim_styles(), "Standard")) {
        default_handles_["DIMSTYLE/STANDARD"] = new_handle();
    }

    // Dimension blocks: BLOCK_RECORD, BLOCK, ENDBLK and every entity
    for (auto& b : generated_dim_blocks_) {
        b.record.handle = new_handle();
        b.handle = new_handle();
        b.endblk_handle = new_handle();
        for (auto& e : b.entities) {
            e.props.handle = new_handle();
            // R13's built-in arrows (_Dot) are wide POLYLINEs: their VERTEXes and SEQEND have handles too
            if (e.type == EntityType::Polyline) {
                for (auto& v : e.as_polyline().vertices) v.handle = new_handle();
                e.as_polyline().seqend_handle = new_handle();
            } else if (e.type == EntityType::Insert && !e.as_insert().attributes.empty()) {
                for (auto& a : e.as_insert().attributes) a.props.handle = new_handle();
                e.as_insert().seqend_handle = new_handle();
            }
        }
    }

    for (const auto& b : dwg.blocks()) record_handles_.emplace(upper(b.name), b.record.handle);
    for (const auto& b : generated_dim_blocks_) record_handles_.emplace(upper(b.name), b.record.handle);
    if (record_handle("*Model_Space").empty() || record_handle("*Paper_Space").empty()) {
        error_ = "DXF 2000 needs the blocks *Model_Space and *Paper_Space with their BLOCK_RECORD";
        return;
    }

    // A BLOCK_RECORD lists the INSERTs of its block (102 {BLKREFS), as AutoCAD writes it;
    // a leader's annotation and a hatch's boundary objects have it as reactor. A drawing read from
    // a file has the relations of the file's own drawing too (the original, written for
    // comparison): only a relation that changed since is added to a record that was read.
    collect_relations(dwg, block_refs_, reactors_);
    // A drawing made here has no file to tell a removed record by: its entities are what is live, so a
    // hatch keeps no boundary object that was removed (B97; Codex review, phase 7)
    entity_handles_.clear();
    if (!template_mode()) {
        auto add = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) if (!e.props.handle.empty()) entity_handles_.insert(upper(e.props.handle));
        };
        add(dwg.entities());
        for (const auto& b : dwg.blocks()) add(b.entities);
    }
    if (template_mode()) collect_relations(*original_, orig_block_refs_, orig_reactors_);
    prepare_mleaders(dwg);

    // An MLINE names its style; the style's handle is found by the name (write_mline_r2000)
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::MlineStyle) continue;
        mline_style_handles_.emplace(upper(o.as_mline_style().name), o.handle);
        mline_style_names_.emplace(upper(o.handle), o.as_mline_style().name);
    }

    for (const auto& s : dwg.text_styles()) {
        if (names_equal(s.name, "Standard")) standard_style_handle_ = s.handle;
        if (s.name.empty() && (s.flags & 1)) shape_style_handles_.emplace(upper(s.font_file), s.handle);
        else style_handles_.emplace(upper(s.name), s.handle);
    }
    if (standard_style_handle_.empty()) standard_style_handle_ = default_handles_["STYLE/STANDARD"];
    if (standard_style_handle_.empty()) standard_style_handle_ = "0";      // a file without STANDARD: none
    style_handles_.emplace("STANDARD", standard_style_handle_);
    for (const auto& vp : dwg.vports()) {
        if (active_vport_.empty() && names_equal(vp.name, "*Active")) active_vport_ = vp.handle;
    }
    if (active_vport_.empty()) active_vport_ = default_handles_["VPORT/*ACTIVE"];
    for (const auto& l : dwg.layers()) layer_handles_.emplace(upper(l.name), l.handle);
    for (const auto& lt : dwg.linetypes()) ltype_handles_.emplace(upper(lt.name), lt.handle);
    for (const char* lt : {"BYBLOCK", "BYLAYER", "CONTINUOUS"}) {
        const std::string& h = default_handles_[std::string("LTYPE/") + lt];
        if (!h.empty()) ltype_handles_.emplace(lt, h);
    }
    for (const auto& u : dwg.ucs_list()) ucs_handles_.emplace(upper(u.name), u.handle);
    if (!default_handles_["LAYER/0"].empty()) layer_handles_.emplace("0", default_handles_["LAYER/0"]);
    if (needs_defpoints_) layer_handles_.emplace("DEFPOINTS", default_handles_["LAYER/DEFPOINTS"]);

    // 2007: a layer's material is Global unless it has one, a viewport's visual style
    // 2dWireframe (B33, as AutoCAD's new drawing)
    if (version_ >= DxfVersion::R2007) {
        global_material_ = material_handle(dwg, "Global");
        wireframe_style_ = visual_style_handle(dwg, "2dWireframe");
    }

    // The default plot style (Normal): root dictionary → ACAD_PLOTSTYLENAME → its default
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::Dictionary || o.owner != "0") continue;
        for (const auto& entry : o.as_dictionary().entries) {
            if (entry.name != "ACAD_PLOTSTYLENAME") continue;
            const Object* styles = dwg.find_object(entry.handle);
            if (styles && styles->type == ObjectType::DictionaryWithDefault) {
                normal_plot_style_ = styles->as_dictionary_with_default().default_handle;
            }
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// HEADER: AutoCAD's 2000 variables in AutoCAD's order, with the drawing's values
// ---------------------------------------------------------------------------

// $XCLIPFRAME is 280 (0, 1, 2) in 2010 and 290 (0, 1) in 2004/2007, where AutoCAD keeps a 2
// in the DICTIONARYVAR XCLIPFRAME and writes 0 here (ACadSharp's sample_AC1021, B43); 2000's
// header has none (the value is the variable's)
void xclipframe_as_written(std::vector<HeaderVar>& vars, DxfVersion version) {
    if (version < DxfVersion::R2004) {
        vars.erase(std::remove_if(vars.begin(), vars.end(), [](const HeaderVar& v) { return v.name == "$XCLIPFRAME"; }),
                   vars.end());
        return;
    }
    for (auto& v : vars) {
        if (v.name != "$XCLIPFRAME" || v.values.empty()) continue;
        int value = std::atoi(v.values[0].second.c_str());
        if (version >= DxfVersion::R2010) v.values = {{280, std::to_string(value)}};
        else v.values = {{290, value == 1 ? "1" : "0"}};
    }
}

// $ACADMAINTVER is group 90 in 2018 and 70 before (B70), whichever group the program gave it
void maintenance_as_written(std::vector<HeaderVar>& vars, DxfVersion version) {
    for (auto& v : vars) {
        if (v.name != "$ACADMAINTVER" || v.values.size() != 1) continue;
        v.values[0].first = version >= DxfVersion::R2018 ? 90 : 70;
    }
}

void DxfWriter::write_header_r2000(const Drawing& dwg) {
    std::vector<HeaderVar> vars = version_ >= DxfVersion::R2018   ? default_r2018_headers()
                                : version_ >= DxfVersion::R2013   ? default_r2013_headers()
                                : version_ >= DxfVersion::R2010   ? default_r2010_headers()
                                : version_ == DxfVersion::R2007 ? default_r2007_headers()
                                : version_ == DxfVersion::R2004 ? default_r2004_headers()
                                : version_ == DxfVersion::R2000 ? default_r2000_headers()
                                : version_ == DxfVersion::R14   ? default_r14_headers()
                                                                : default_r13_headers();
    std::set<std::string> in_2000;
    for (const auto& v : vars) in_2000.insert(v.name);
    // Variables R12 has in its header but the version's list not ($VIEWSIZE, $SNAPMODE … in R13/R14,
    // $HANDLING, $BLIPMODE … from 2000) are left out, and those of a later version than this one (a
    // drawing made as 2007 saved as 2004). For R13 and R14 the list is their own, so $HANDLING and
    // $BLIPMODE stay; what only 2000 or later has is a version error before the writing (the
    // program's variable, older_version_error()) — the ones the library itself gives a drawing made as
    // 2000+ ($FINGERPRINTGUID, $VERSIONGUID, $CMATERIAL …) are left out here (format profile 3.6)
    std::set<std::string> r12_only;
    for (const auto& v : default_r12_headers()) {
        if (!in_2000.count(v.name)) r12_only.insert(v.name);
    }
    for (const auto& v : default_r2013_headers()) {
        if (!in_2000.count(v.name)) r12_only.insert(v.name);
    }

    for (const auto& hv : dwg.header_vars()) {
        if (r12_only.count(hv.name)) continue;
        auto it = std::find_if(vars.begin(), vars.end(),
                               [&](const HeaderVar& v) { return v.name == hv.name; });
        if (it != vars.end()) it->values = hv.values;
        else vars.push_back(hv);
    }
    // $DIMBLK is group 1 from R13 (2 in R12's reference: a drawing made as R12)
    for (auto& v : vars) {
        if (v.name == "$DIMBLK" && v.values.size() == 1) v.values[0].first = 1;
    }
    // R13 and R14: the names of the header are spelled as the table records they name (symbol_ref)
    if (version_ < DxfVersion::R2000 && !template_mode()) {
        struct HeaderName { const char* variable; SymbolKind kind; };
        const HeaderName names[] = {{"$CLAYER", SymbolKind::Layer},      {"$CELTYPE", SymbolKind::Linetype},
                                    {"$TEXTSTYLE", SymbolKind::Style},   {"$DIMTXSTY", SymbolKind::Style},
                                    {"$DIMSTYLE", SymbolKind::DimStyle}, {"$DIMBLK", SymbolKind::Block},
                                    {"$DIMBLK1", SymbolKind::Block},     {"$DIMBLK2", SymbolKind::Block}};
        for (auto& v : vars) {
            for (const auto& n : names) {
                if (v.name == n.variable && v.values.size() == 1) v.values[0].second = symbol_ref(n.kind, v.values[0].second);
            }
        }
    }

    auto set = [&](const std::string& name, std::vector<std::pair<int, std::string>> values) {
        for (auto& v : vars) {
            if (v.name == name) v.values = std::move(values);
        }
    };
    set("$ACADVER", {{1, std::string(acadver_string(version_))}});
    // R13 and R14 (B91): $DIMUNIT and $DIMFIT are the current dimension style's, as the style writes them.
    // A value the program set, or the one in the header of a drawing read from the version, stays.
    if ((version_ == DxfVersion::R13 || version_ == DxfVersion::R14) && !template_mode()) {
        std::string current;
        for (const auto& v : vars) {
            if (v.name == "$DIMSTYLE" && !v.values.empty()) current = v.values[0].second;
        }
        const DimStyle style = find_dim_style(current, dwg);
        if (!dwg.find_header("$DIMUNIT")) {
            if (std::optional<int> unit = dimunit_of(style)) set("$DIMUNIT", {{70, std::to_string(*unit)}});
        }
        if (!dwg.find_header("$DIMFIT")) {
            if (std::optional<int> fit = dimfit_of(style)) set("$DIMFIT", {{70, std::to_string(*fit)}});
        }
    }
    // R13 and R14 always have handles: $HANDLING is 1 whatever R12's value in the drawing was
    if (version_ == DxfVersion::R13 || version_ == DxfVersion::R14) set("$HANDLING", {{70, "1"}});
    xclipframe_as_written(vars, version_);
    maintenance_as_written(vars, version_);
    // AutoCAD writes the login name at every save; a name the program set stays
    for (auto& v : vars) {
        if (v.name == "$LASTSAVEDBY" && (v.values.empty() || v.values[0].second.empty())) {
            v.values = {{1, login_name()}};
        }
    }
    set("$HANDSEED", {{5, format_handle(next_handle_)}});
    auto point = [](const Point3& p) -> std::vector<std::pair<int, std::string>> {
        return {{10, format_double(p.x, NumberStyle::AutoCad)},
                {20, format_double(p.y, NumberStyle::AutoCad)},
                {30, format_double(p.z, NumberStyle::AutoCad)}};
    };
    set("$EXTMIN", point(ext_min_));
    set("$EXTMAX", point(ext_max_));
    set("$PEXTMIN", point(pext_min_));
    set("$PEXTMAX", point(pext_max_));

    write_code(0, "SECTION");
    write_code(2, "HEADER");
    for (const auto& hv : vars) {
        // Empty strings are written, as AutoCAD writes them in 2000 ($UCSNAME, $DIMPOST …)
        write_code(9, hv.name);
        for (const auto& [code, value] : hv.values) {
            switch (group_value_type(code)) {
                case GroupValueType::Double:
                    groups_.write_double(code, std::strtod(value.c_str(), nullptr));
                    break;
                case GroupValueType::Int16:
                case GroupValueType::Int32:
                case GroupValueType::Int64:
                case GroupValueType::Bool:
                    groups_.write_int(code, std::strtoll(value.c_str(), nullptr, 10));
                    break;
                default:
                    write_code(code, value);
                    break;
            }
        }
    }
    write_code(0, "ENDSEC");
}

// ---------------------------------------------------------------------------
// CLASSES: the classes of the objects that are written (values as AutoCAD writes them)
// ---------------------------------------------------------------------------

std::vector<ClassEntry> classes_used(const Drawing& dwg, DxfVersion version,
                                     const std::unordered_set<uint64_t>* dropped,
                                     const std::vector<Block>* generated) {
    if (version < DxfVersion::R2000) return older_classes_used(dwg, version, dropped, generated);
    struct Use {
        ClassEntry entry;
        ObjectType object;
        EntityType entity;
        const char* unknown = nullptr;      // an untyped object of this DXF name
    };
    const EntityType none = EntityType::Unknown;
    const ObjectType no_object = ObjectType::Unknown;
    const Use classes[] = {
        {{"ACDBDICTIONARYWDFLT", "AcDbDictionaryWithDefault", "ObjectDBX Classes", 0, false},
         ObjectType::DictionaryWithDefault, none},
        // A new 2007 drawing's materials and visual styles (B34; typed since phase 9, step 9.7: the codecs have the same
        // class; an untyped record of the name, one a reader could not type, still has its class)
        {{"MATERIAL", "AcDbMaterial", "ObjectDBX Classes", 1153, false}, ObjectType::Material, none, "MATERIAL"},
        {{"VISUALSTYLE", "AcDbVisualStyle", "ObjectDBX Classes", 4095, false}, ObjectType::VisualStyle, none, "VISUALSTYLE"},
        // MULTILEADER and its style, as AutoCAD writes them (plan phase 3, B36)
        {{"MLEADERSTYLE", "AcDbMLeaderStyle", "ACDB_MLEADERSTYLE_CLASS", 4095, false}, ObjectType::MLeaderStyle, none},
        {{"MULTILEADER", "AcDbMLeader", "ACDB_MLEADER_CLASS", 1025, true}, no_object, EntityType::MLeader},
        {{"ACDBPLACEHOLDER", "AcDbPlaceHolder", "ObjectDBX Classes", 0, false}, ObjectType::Placeholder, none},
        {{"LAYOUT", "AcDbLayout", "ObjectDBX Classes", 0, false}, ObjectType::Layout, none},
        {{"DICTIONARYVAR", "AcDbDictionaryVar", "ObjectDBX Classes", 0, false}, ObjectType::DictionaryVar, none},
        {{"PLOTSETTINGS", "AcDbPlotSettings", "ObjectDBX Classes", 0, false}, ObjectType::PlotSettings, none},
        // The raster image classes, as AutoCAD writes them (application ISM)
        {{"RASTERVARIABLES", "AcDbRasterVariables", "ISM", 0, false}, ObjectType::RasterVariables, none},
        {{"IMAGEDEF", "AcDbRasterImageDef", "ISM", 0, false}, ObjectType::ImageDef, none},
        {{"IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor", "ISM", 1, false}, ObjectType::ImageDefReactor, none},
        {{"IMAGE", "AcDbRasterImage", "ISM", 2175, true}, no_object, EntityType::Image},
        // WIPEOUT and its variables, as AutoCAD writes their classes (ACadSharp's samples)
        {{"WIPEOUTVARIABLES", "AcDbWipeoutVariables", "\"WipeOut|Product Desc:     WipeOut Dbx Application|Company:          Autodesk, Inc.|WEB Address:      www.autodesk.com\"", 0, false}, ObjectType::WipeoutVariables, none},
        // SCALE, SUN and LIGHT, as AutoCAD writes their classes (LibreDWG's example_2007/2010)
        {{"SCALE", "AcDbScale", "ObjectDBX Classes", 1153, false}, ObjectType::Scale, none},
        {{"SUN", "AcDbSun", "SCENEOE", 1153, false}, ObjectType::Sun, none},
        {{"LIGHT", "AcDbLight", "SCENEOE", 1025, true}, no_object, EntityType::Light},
        {{"WIPEOUT", "AcDbWipeout", "WipeOut|Product Desc: Object Enabler for WipeOut entity | Company: Autodesk, Inc. | WEB Address: www.autodesk.com", 2175, true}, no_object, EntityType::Wipeout},
        {{"DBCOLOR", "AcDbColor", "ObjectDBX Classes", 0, false}, ObjectType::DbColor, none},
        // The coordination model and the RAPIDRT objects (plan phase 6, B78): the classes' DXF names
        // and applications from AutoCAD's core (AcBIMUnderlayDbx) and its 2013/2018 samples
        {{"COORDINATION_MODEL", "AcDbNavisworksModel", "AcBIMUnderlayDbx", 1025, true}, no_object, EntityType::CoordinationModel},
        {{"NAVISWORKSMODELDEF", "AcDbNavisworksModelDef", "AcBIMUnderlayDbx", 1153, false}, ObjectType::NavisworksModelDef, none},
        {{"RAPIDRTRENDERSETTINGS", "AcDbRapidRTRenderSettings", "ObjectDBX Classes", 1024, false}, ObjectType::RapidRtRenderSettings, none},
        {{"RAPIDRTRENDERENVIRONMENT", "AcDbIBLBackground", "SCENEOE", 1024, false}, ObjectType::RapidRtRenderEnvironment, none},
        // GEODATA, as AutoCAD writes its class (gh209_1, geoloc_AC1021, geoloc)
        {{"GEODATA", "AcDbGeoData", "ObjectDBX Classes", 4095, false}, ObjectType::GeoData, none},
        // The detail and section view styles, as AutoCAD writes their classes (ACadSharp's samples 2013, 2018)
        {{"ACDBSECTIONVIEWSTYLE", "AcDbSectionViewStyle", "ObjectDBX Classes", 1025, false}, ObjectType::SectionViewStyle, none},
        {{"ACDBDETAILVIEWSTYLE", "AcDbDetailViewStyle", "ObjectDBX Classes", 1025, false}, ObjectType::DetailViewStyle, none},
        // Surfaces and HELIX, as AutoCAD writes their classes (LibreDWG's Surface.dxf and
        // Helix.dxf; SURFACE in none of AutoCAD's files: as the surfaces with 0)
        {{"SURFACE", "AcDbSurface", "ObjectDBX Classes", 0, true}, no_object, EntityType::Surface},
        {{"EXTRUDEDSURFACE", "AcDbExtrudedSurface", "ObjectDBX Classes", 0, true}, no_object, EntityType::ExtrudedSurface},
        {{"LOFTEDSURFACE", "AcDbLoftedSurface", "ObjectDBX Classes", 0, true}, no_object, EntityType::LoftedSurface},
        {{"REVOLVEDSURFACE", "AcDbRevolvedSurface", "ObjectDBX Classes", 0, true}, no_object, EntityType::RevolvedSurface},
        {{"SWEPTSURFACE", "AcDbSweptSurface", "ObjectDBX Classes", 0, true}, no_object, EntityType::SweptSurface},
        {{"PLANESURFACE", "AcDbPlaneSurface", "ObjectDBX Classes", 4095, true}, no_object, EntityType::PlaneSurface},
        // NURBSURFACE: AutoCAD's CLASSES have it in 2004 … 2018 (flags 0), the entity itself from 2013
        {{"NURBSURFACE", "AcDbNurbSurface", "ObjectDBX Classes", 0, true}, no_object, EntityType::NurbsSurface},
        {{"HELIX", "AcDbHelix", "ObjectDBX Classes", 4095, true}, no_object, EntityType::Helix},
        // MESH, as AutoCAD writes its class (ACadSharp's samples 2000 … 2018)
        {{"MESH", "AcDbSubDMesh", "ObjectDBX Classes", 0, true}, no_object, EntityType::Mesh},
    };
    auto entities = [&](EntityType type) {
        auto in = [&](const std::vector<Entity>& list) {
            return static_cast<int>(std::count_if(list.begin(), list.end(),
                                                  [&](const Entity& e) { return e.type == type; }));
        };
        int n = in(dwg.entities());
        for (const auto& b : dwg.blocks()) n += in(b.entities);
        // Before 2013 a NURBS surface is a SURFACE record (step 9.S): its class is SURFACE's too, as in AutoCAD's files of
        // 2000 … 2010 (surfaces_nurbs_converted: SURFACE and NURBSURFACE both in CLASSES)
        if (type == EntityType::Surface && version < DxfVersion::R2013) {
            type = EntityType::NurbsSurface;
            n += in(dwg.entities());
            for (const auto& b : dwg.blocks()) n += in(b.entities);
        }
        return n;
    };
    bool r2004 = version >= DxfVersion::R2004;
    std::vector<ClassEntry> out;
    for (const auto& c : classes) {
        if (r2004 && (c.object == ObjectType::Layout || c.object == ObjectType::Placeholder)) continue;
        auto written = [&](const Object& o) {
            uint64_t h = 0;
            bool kind = c.unknown ? (o.type == ObjectType::Unknown && o.as_unknown().object_type == c.unknown) ||
                                        (c.object != ObjectType::Unknown && o.type == c.object)
                                  : o.type == c.object;
            return kind && !(dropped && parse_handle(o.handle, h) == HandleParse::Ok && dropped->count(h));
        };
        int count = c.entity != none
                        ? entities(c.entity)
                        : static_cast<int>(std::count_if(dwg.objects().begin(), dwg.objects().end(), written));
        if (count == 0) continue;
        out.push_back(c.entry);
        out.back().count = count;
    }
    append_typed_classes(dwg, version, dropped, out);
    return out;
}

void DxfWriter::write_class(const ClassEntry& c) {
    class_register_.push_back(c.name);
    write_code(0, "CLASS");
    write_code(1, c.name);
    write_code(2, c.cpp_name);
    write_code(3, c.application);
    groups_.write_int(90, c.flags);
    if (version_ >= DxfVersion::R2004) groups_.write_int(91, c.count);
    groups_.write_int(280, 0);
    groups_.write_int(281, c.entity ? 1 : 0);
}

void DxfWriter::write_classes_r2000(const Drawing& dwg) {
    write_code(0, "SECTION");
    write_code(2, "CLASSES");
    class_register_.clear();
    for (const auto& c : classes_used(dwg, version_, &dropped_objects_, &generated_dim_blocks_)) write_class(c);
    write_code(0, "ENDSEC");
}

// ---------------------------------------------------------------------------
// TABLES
// ---------------------------------------------------------------------------

bool DxfWriter::owner_groups_written() const {
    if (version_ >= DxfVersion::R2000) return true;
    return version_ != DxfVersion::R12 && drawing_ && drawing_->owner_groups_;
}

void DxfWriter::write_table_head(const char* name, const std::string& handle, int count) {
    write_code(0, "TABLE");
    write_code(2, name);
    write_code(5, handle);
    if (owner_groups_written()) write_code(330, "0");
    write_code(100, "AcDbSymbolTable");
    groups_.write_int(70, count);
    // 2013: the VPORT table's XDATA as AutoCAD 2017 writes a new drawing (meaning unknown; B59)
    // (2018 has none: ACadSharp's sample_AC1032, B70/B71)
    if (version_ == DxfVersion::R2013 && std::string(name) == "VPORT") {
        write_code(1001, "ACAD");
        write_code(1000, "DbSaveVer");
        groups_.write_int(1071, 26);
    }
}

void DxfWriter::write_table_entry_head(const char* type, const std::string& handle,
                                       const std::string& table, const char* subclass,
                                       const std::string& xdictionary,
                                       const std::vector<std::string>* reactors) {
    write_code(0, type);
    write_code(std::string(type) == "DIMSTYLE" ? 105 : 5, handle);
    // R13 and R14: the reactors first, then the extension dictionary (as the entities, 7.1)
    const bool older = version_ < DxfVersion::R2000;
    if (older && reactors) write_reactors(*reactors, handle);
    if (!xdictionary.empty() && (!filter_live_ || is_live(xdictionary))) {
        write_code(102, "{ACAD_XDICTIONARY");
        write_code(360, xdictionary);
        write_code(102, "}");
    }
    if (!older && reactors) write_reactors(*reactors, handle);
    if (owner_groups_written()) write_code(330, table);
    write_code(100, "AcDbSymbolTableRecord");
    write_code(100, subclass);
}

void DxfWriter::write_tables_r2000(const Drawing& dwg) {
    write_code(0, "SECTION");
    write_code(2, "TABLES");
    // In the file's order for a drawing read from one, then any table it lacked
    std::vector<std::string> names;
    if (template_mode()) {
        for (const auto& t : dwg.raw_tables()) names.push_back(t.name);
    }
    for (const char* name : {"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW", "UCS", "APPID",
                             "DIMSTYLE", "BLOCK_RECORD"}) {
        if (std::find(names.begin(), names.end(), name) == names.end()) names.push_back(name);
    }
    for (const auto& name : names) write_table_r2000(dwg, name);
    write_code(0, "ENDSEC");
}

static uint64_t handle_value(const std::string& h) {
    uint64_t v = 0;
    return parse_handle(h, v) == HandleParse::Ok ? v : 0;
}

template <typename T>
static std::unordered_map<uint64_t, const T*> by_handle(const std::vector<T>* list) {
    std::unordered_map<uint64_t, const T*> m;
    if (!list) return m;
    for (const auto& x : *list) {
        if (uint64_t v = handle_value(x.handle)) m[v] = &x;
    }
    return m;
}

// One table: its TABLE record (as read, or made here), the standard entries the drawing
// lacks, its entries (each from its preserved record when it has one), and entries of the
// file HDxf does not interpret, as they were
void DxfWriter::write_table_r2000(const Drawing& dwg, const std::string& name) {
    const RawTable* raw = nullptr;
    if (template_mode()) {
        for (const auto& t : dwg.raw_tables()) if (t.name == name) raw = &t;
    }
    const Drawing* o = original_;
    // How many of the table's records the drawing has that the file it was read from did not have: those
    // with a handle the file did not hold
    auto added = [&]() -> int {
        if (!o) return 0;
        int n = 0;
        auto is_new = [&](const std::string& handle) {
            uint64_t v = handle_value(handle);
            return v == 0 || !orig_handles_.count(v);
        };
        auto count_added = [&](const auto& list) {
            for (const auto& x : list) if (is_new(x.handle)) ++n;
        };
        if (name == "VPORT") count_added(dwg.vports());
        else if (name == "LTYPE") count_added(dwg.linetypes());
        else if (name == "LAYER") count_added(dwg.layers());
        else if (name == "STYLE") count_added(dwg.text_styles());
        else if (name == "VIEW") count_added(dwg.views());
        else if (name == "UCS") count_added(dwg.ucs_list());
        else if (name == "APPID") count_added(dwg.app_ids());
        else if (name == "DIMSTYLE") count_added(dwg.dim_styles());
        else if (name == "BLOCK_RECORD") {
            for (const auto& b : dwg.blocks()) if (is_new(b.record.handle)) ++n;
        }
        return n;
    };
    // R13 and R14: the standard records are upper case ("STANDARD", "CONTINUOUS", "*ACTIVE": every
    // AutoCAD file), and a new table's 70 is what AutoCAD wrote: the number of records, LTYPE's and
    // BLOCK_RECORD's less the two it does not count, an R13 VPORT table's one more (doc/reference/
    // hdxf_r13_r14_format.md, 5.1, D5)
    const bool older = version_ < DxfVersion::R2000;
    auto older_count = [&](int records) {
        if (!older) return records;
        if (name == "LTYPE" || name == "BLOCK_RECORD") return std::max(records - 2, 0);
        if (name == "VPORT" && version_ == DxfVersion::R13 && records == 1) return 2;
        return records;
    };
    // A table read from a file keeps the file's count and raises it by each record added, none lowers it: the
    // count of AutoCAD's files (F10, doc/facit/14_annotative.scr, 2000 ... 2018, _added and _removed)
    int made = 0;       // standard records the writer makes here, which the file's table did not have
    auto head = [&](int count) {
        if (raw) write_table_head_template(*raw, added() + made);
        else write_table_head(name.c_str(), table_handles_[name], older_count(count));
    };
    // Each entry from its template, compared with the original's entry of the same handle
    auto entries = [&](const auto& list, const auto* original_list, auto write) {
        auto originals = by_handle(original_list);
        for (const auto& x : list) {
            auto it = originals.find(handle_value(x.handle));
            const auto* orig = it != originals.end() ? it->second : nullptr;
            emit({&x.raw}, false, [&] { write(x); },
                 orig ? std::function<void()>([&] { write(*orig); }) : nullptr);
        }
    };

    if (name == "VPORT") {
        // The default *Active viewport looks at the drawing's extents, as in R12
        if (dwg.vports().empty() && standard_entries_) {
            Vport vp;
            vp.name = older ? "*ACTIVE" : "*Active";
            double width = view_max_.x - view_min_.x;
            double height = view_max_.y - view_min_.y;
            vp.center = {(view_min_.x + view_max_.x) / 2.0, (view_min_.y + view_max_.y) / 2.0};
            vp.view_height = height * 1.1;
            vp.aspect_ratio = vp.view_height > 0.0 ? (width * 1.1) / vp.view_height : 1.0;
            made = 1;
            head(1);
            write_vport_r2000(vp, default_handles_["VPORT/*ACTIVE"]);
        } else {
            head(static_cast<int>(dwg.vports().size()));
            entries(dwg.vports(), o ? &o->vports() : nullptr,
                    [&](const Vport& vp) { write_vport_r2000(vp, vp.handle); });
        }
    } else if (name == "LTYPE") {
        // ByBlock, ByLayer and Continuous unless the drawing has them
        struct StandardLinetype { const char* key; const char* name; const char* description; };
        const StandardLinetype standard[] = {
            {"BYBLOCK", older ? "BYBLOCK" : "ByBlock", ""},
            {"BYLAYER", older ? "BYLAYER" : "ByLayer", ""},
            {"CONTINUOUS", older ? "CONTINUOUS" : "Continuous", "Solid line"},
        };
        int count = static_cast<int>(dwg.linetypes().size());
        for (const auto& st : standard) {
            if (standard_entries_ && !has_entry_named(dwg.linetypes(), st.key)) count++;
        }
        made = count - static_cast<int>(dwg.linetypes().size());
        head(count);
        for (const auto& st : standard) {
            if (!standard_entries_ || has_entry_named(dwg.linetypes(), st.key)) continue;
            Linetype lt;
            lt.name = st.name;
            lt.description = st.description;
            write_ltype_r2000(lt, default_handles_[std::string("LTYPE/") + st.key]);
        }
        entries(dwg.linetypes(), o ? &o->linetypes() : nullptr,
                [&](const Linetype& lt) { write_ltype_r2000(lt, lt.handle); });
    } else if (name == "LAYER") {
        bool has_layer_0 = !standard_entries_ || has_entry_named(dwg.layers(), "0");
        made = (has_layer_0 ? 0 : 1) + (needs_defpoints_ ? 1 : 0);
        head(static_cast<int>(dwg.layers().size()) + made);
        if (!has_layer_0) {
            Layer l;
            l.linetype = older ? "CONTINUOUS" : "Continuous";
            write_layer_r2000(l, default_handles_["LAYER/0"]);
        }
        if (needs_defpoints_) {
            // For the dimension blocks built here, as AutoCAD makes it: not plotted
            Layer l;
            l.name = older ? "DEFPOINTS" : "Defpoints";
            l.linetype = older ? "CONTINUOUS" : "Continuous";
            l.plot = false;
            write_layer_r2000(l, default_handles_["LAYER/DEFPOINTS"]);
        }
        entries(dwg.layers(), o ? &o->layers() : nullptr,
                [&](const Layer& l) { write_layer_r2000(l, l.handle); });
    } else if (name == "STYLE") {
        bool has_standard = !standard_entries_ || has_entry_named(dwg.text_styles(), "Standard");
        made = has_standard ? 0 : 1;
        head(static_cast<int>(dwg.text_styles().size()) + made);
        if (!has_standard) {
            TextStyle standard;
            if (older) {
                // STANDARD with txt and 0.2 as the last height, as the new R13/R14 drawings of AutoCAD have it
                standard.name = "STANDARD";
                standard.last_height = 0.2;
            }
            write_style_r2000(standard, default_handles_["STYLE/STANDARD"]);
        }
        entries(dwg.text_styles(), o ? &o->text_styles() : nullptr,
                [&](const TextStyle& s) { write_style_r2000(s, s.handle); });
    } else if (name == "VIEW") {
        head(static_cast<int>(dwg.views().size()));
        entries(dwg.views(), o ? &o->views() : nullptr, [&](const View& v) { write_view_r2000(v); });
    } else if (name == "UCS") {
        head(static_cast<int>(dwg.ucs_list().size()));
        entries(dwg.ucs_list(), o ? &o->ucs_list() : nullptr, [&](const Ucs& u) { write_ucs_r2000(u); });
    } else if (name == "APPID") {
        bool has_acad = !standard_entries_ || has_entry_named(dwg.app_ids(), "ACAD");
        made = static_cast<int>(implied_app_ids_.size()) + (has_acad ? 0 : 1);
        head(static_cast<int>(dwg.app_ids().size()) + made);
        if (!has_acad) {
            AppId acad;
            acad.name = "ACAD";
            acad.handle = default_handles_["APPID/ACAD"];
            write_appid_r2000(acad);
        }
        entries(dwg.app_ids(), o ? &o->app_ids() : nullptr, [&](const AppId& a) { write_appid_r2000(a); });
        for (const auto& [app_name, handle] : implied_app_ids_) {
            AppId app;
            app.name = app_name;
            app.handle = handle;
            write_appid_r2000(app);
        }
    } else if (name == "DIMSTYLE") {
        bool has_standard = (template_mode() && !standard_dimstyle_needed_) || has_entry_named(dwg.dim_styles(), "Standard");
        if (raw) {
            write_table_head_template(*raw, added() + (has_standard ? 0 : 1));
        } else {
            write_table_head("DIMSTYLE", table_handles_["DIMSTYLE"],
                             static_cast<int>(dwg.dim_styles().size()) + (has_standard ? 0 : 1));
            // 71 + 340: not described in the DXF reference; in AutoCAD's files a list of
            // the dimension styles that have been current (getDimRecentStyleList). A
            // drawing written here has had one: $DIMSTYLE.
            std::string current = default_handles_["DIMSTYLE/STANDARD"];
            if (const HeaderVar* hv = dwg.find_header("$DIMSTYLE")) {
                for (const auto& ds : dwg.dim_styles()) {
                    if (!hv->values.empty() && names_equal(ds.name, hv->values[0].second)) {
                        current = ds.handle;
                    }
                }
            }
            if (current.empty()) {
                for (const auto& ds : dwg.dim_styles()) {
                    if (names_equal(ds.name, "Standard")) current = ds.handle;
                }
            }
            // R13 and R14 have no such list: the table is 5, 330, 100 AcDbSymbolTable, 70 (small_r13.dxf)
            if (version_ >= DxfVersion::R2000) {
                write_code(100, "AcDbDimStyleTable");
                groups_.write_int(71, current.empty() ? 0 : 1);
                if (!current.empty()) write_code(340, current);
            }
        }
        if (!has_standard) {
            DimStyle standard;
            standard.dimtad = 0;      // as the R12 default
            if (older) standard.name = "STANDARD";
            write_dimstyle_r2000(standard, default_handles_["DIMSTYLE/STANDARD"]);
        }
        entries(dwg.dim_styles(), o ? &o->dim_styles() : nullptr,
                [&](const DimStyle& ds) { write_dimstyle_r2000(ds, ds.handle); });
    } else if (name == "BLOCK_RECORD") {
        // One per block, the dimension blocks too
        made = static_cast<int>(generated_dim_blocks_.size());
        head(static_cast<int>(dwg.blocks().size()) + made);
        for (const auto& b : dwg.blocks()) {
            const Block* orig = nullptr;
            if (template_mode()) {
                auto it = orig_records_.find(handle_value(b.record.handle));
                if (it != orig_records_.end()) orig = it->second;
            }
            emit({&b.record.raw}, false, [&] { write_block_record_r2000(b); },
                 orig ? std::function<void()>([&] { write_block_record_r2000(*orig); }) : nullptr);
        }
        for (const auto& b : generated_dim_blocks_) write_block_record_r2000(b);
    } else if (raw) {
        head(0);                // a table HDxf does not know: as read
    }

    // Entries of the file that no parser took (BLOCK_RECORDs of no block, unknown records)
    if (raw) {
        for (const auto& r : raw->entries) write_raw_record(r);
    }
    write_code(0, "ENDTAB");
}

void DxfWriter::write_view_r2000(const View& v) {
    if (version_ < DxfVersion::R2000) {
        write_view_older(v);
        return;
    }
    write_table_entry_head("VIEW", v.handle, table_handles_["VIEW"], "AcDbViewTableRecord", v.xdictionary);
    write_code(2, v.name);
    groups_.write_int(70, v.flags);
    groups_.write_double(40, v.height);
    groups_.write_double(10, v.center.x);
    groups_.write_double(20, v.center.y);
    groups_.write_double(41, v.width);
    write_point(11, v.direction);
    write_point(12, v.target);
    groups_.write_double(42, v.lens_length);
    groups_.write_double(43, v.front_clip);
    groups_.write_double(44, v.back_clip);
    groups_.write_double(50, v.twist);
    groups_.write_int(71, v.view_mode);
    groups_.write_int(281, v.render_mode);
    groups_.write_int(72, v.has_ucs ? 1 : 0);
    if (v.has_ucs) {
        write_point(110, v.ucs_origin);
        write_point(111, v.ucs_x_axis);
        write_point(112, v.ucs_y_axis);
        groups_.write_int(79, v.ucs_ortho_type);
        groups_.write_double(146, v.elevation);
        write_ucs_refs(v.ucs, v.base_ucs);
    }
    // 2007: after the UCS, as AutoCAD writes it (73, 348); the reference's 332, 334 and 361
    // where it lists them
    if (version_ >= DxfVersion::R2007) {
        groups_.write_int(73, v.camera_plottable ? 1 : 0);
        write_pointer(332, v.background_handle);
        write_pointer(334, v.live_section_handle);
        // 2dWireframe for a drawing built in memory; one read without a style keeps none
        // (Fusion 360's files, AutoCAD's writer)
        const std::string& style = v.visual_style_handle.empty() && !template_mode() ? wireframe_style_
                                                                                      : v.visual_style_handle;
        write_pointer(348, style);
        write_pointer(361, v.sun_handle);
    }
}

void DxfWriter::write_ucs_r2000(const Ucs& u) {
    if (version_ < DxfVersion::R2000) {
        write_ucs_older(u);
        return;
    }
    write_table_entry_head("UCS", u.handle, table_handles_["UCS"], "AcDbUCSTableRecord");
    write_code(2, u.name);
    groups_.write_int(70, u.flags);
    write_point(10, u.origin);
    write_point(11, u.x_dir);
    write_point(12, u.y_dir);
    groups_.write_int(79, u.ortho_type);
    groups_.write_double(146, u.elevation);
    write_ucs_refs("", u.base_ucs);
    for (const auto& orth : u.orthographic) {
        groups_.write_int(71, orth.type);
        write_point(13, orth.origin);
    }
}

void DxfWriter::write_appid_r2000(const AppId& a) {
    write_table_entry_head("APPID", a.handle, table_handles_["APPID"], "AcDbRegAppTableRecord");
    write_code(2, a.name);
    groups_.write_int(70, a.flags);
}

void DxfWriter::write_vport_r2000(const Vport& vp, const std::string& handle) {
    if (version_ < DxfVersion::R2000) {
        write_vport_older(vp, handle);
        return;
    }
    write_table_entry_head("VPORT", handle, table_handles_["VPORT"], "AcDbViewportTableRecord", vp.xdictionary);
    write_code(2, vp.name);
    groups_.write_int(70, vp.flags);
    groups_.write_double(10, vp.lower_left.x);
    groups_.write_double(20, vp.lower_left.y);
    groups_.write_double(11, vp.upper_right.x);
    groups_.write_double(21, vp.upper_right.y);
    groups_.write_double(12, vp.center.x);
    groups_.write_double(22, vp.center.y);
    groups_.write_double(13, vp.snap_base.x);
    groups_.write_double(23, vp.snap_base.y);
    groups_.write_double(14, vp.snap_spacing.x);
    groups_.write_double(24, vp.snap_spacing.y);
    groups_.write_double(15, vp.grid_spacing.x);
    groups_.write_double(25, vp.grid_spacing.y);
    write_point(16, vp.view_dir);
    write_point(17, vp.target);
    groups_.write_double(40, vp.view_height);
    groups_.write_double(41, vp.aspect_ratio);
    groups_.write_double(42, vp.lens_length);
    groups_.write_double(43, vp.front_clip);
    groups_.write_double(44, vp.back_clip);
    groups_.write_double(50, vp.snap_angle);
    groups_.write_double(51, vp.twist_angle);
    groups_.write_int(71, vp.view_mode);
    groups_.write_int(72, vp.circle_zoom);
    groups_.write_int(73, vp.fast_zoom);
    groups_.write_int(74, vp.ucsicon);
    groups_.write_int(75, vp.snap_on);
    groups_.write_int(76, vp.grid_on);
    groups_.write_int(77, vp.snap_style);
    groups_.write_int(78, vp.snap_isopair);
    groups_.write_int(281, vp.render_mode);
    groups_.write_int(65, vp.ucs_per_viewport ? 1 : 0);
    write_point(110, vp.ucs_origin);
    write_point(111, vp.ucs_x_axis);
    write_point(112, vp.ucs_y_axis);
    groups_.write_int(79, vp.ucs_ortho_type);
    groups_.write_double(146, vp.elevation);
    write_ucs_refs(vp.ucs, vp.base_ucs);
    // 2007, in AutoCAD's order (348, 60, 61, 292, 282, 141, 142, 63/421, 361); 332 and 333,
    // which no AutoCAD file has, before 348 as the reference lists them
    if (version_ >= DxfVersion::R2007) {
        write_pointer(332, vp.background_handle);
        write_pointer(333, vp.shade_plot_handle);
        const std::string& style = vp.visual_style_handle.empty() && !template_mode() ? wireframe_style_
                                                                                        : vp.visual_style_handle;
        write_pointer(348, style);
        groups_.write_int(60, vp.grid_flags);
        groups_.write_int(61, vp.grid_major);
        groups_.write_int(292, vp.default_lighting ? 1 : 0);
        groups_.write_int(282, vp.default_lighting_type);
        groups_.write_double(141, vp.brightness);
        groups_.write_double(142, vp.contrast);
        write_ambient_color(vp.ambient_color);
        write_pointer(361, vp.sun_handle);
    }
    write_xdata(vp.xdata);
}

// A pointer of a typed field: left out when empty, or when it points to a record that is
// gone (B19) — not when the original is written for comparison
void DxfWriter::write_pointer(int code, const std::string& handle) {
    if (handle.empty() || (filter_live_ && !is_live(handle))) return;
    write_code(code, handle);
}

// A viewport's ambient light color (2007): its ACI (63), RGB (421) and color book name (431)
void DxfWriter::write_ambient_color(const Color& c) {
    groups_.write_int(63, c.aci);
    if (c.has_rgb()) groups_.write_int(421, c.rgb);
    if (!c.name.empty()) write_code(431, c.name);
}

// 2000 has an element type (74) after each dash length (49), and for a shape or text
// its style and placement
void DxfWriter::write_ltype_r2000(const Linetype& lt, const std::string& handle) {
    write_table_entry_head("LTYPE", handle, table_handles_["LTYPE"], "AcDbLinetypeTableRecord", lt.xdictionary);
    write_code(2, lt.name);
    groups_.write_int(70, lt.flags);
    write_code(3, lt.description);
    groups_.write_int(72, lt.alignment);
    groups_.write_int(73, static_cast<int>(lt.pattern.size()));
    groups_.write_double(40, lt.total_length);
    for (size_t i = 0; i < lt.pattern.size(); ++i) {
        groups_.write_double(49, lt.pattern[i]);
        // Upright (bit 8) is in 74 from 2013; before, in the round-trip XRECORD (B60)
        int type = i < lt.elements.size() ? lt.elements[i].type : 0;
        if (version_ < DxfVersion::R2013) type &= ~8;
        if (type == 0) {
            groups_.write_int(74, 0);
            continue;
        }
        const LinetypeElement& el = lt.elements[i];
        groups_.write_int(74, type);
        groups_.write_int(75, el.shape_number);
        write_code(340, element_style_handle(el.style));
        groups_.write_double(46, el.scale);
        groups_.write_double(50, el.rotation);
        groups_.write_double(44, el.offset.x);
        groups_.write_double(45, el.offset.y);
        if (el.type & 2) write_code(9, el.text);
    }
    write_xdata(lt.xdata);
}

void DxfWriter::write_layer_r2000(const Layer& l, const std::string& handle) {
    if (version_ < DxfVersion::R2000) {
        write_layer_older(l, handle);
        return;
    }
    write_table_entry_head("LAYER", handle, table_handles_["LAYER"], "AcDbLayerTableRecord", l.xdictionary);
    write_code(2, l.name);
    // The bits HDxf does not interpret (16, 32, 64 …) as they were read
    int flags = l.flags & ~7;
    if (l.frozen) flags |= 1;
    if (l.frozen_in_new_vp) flags |= 2;
    if (l.locked) flags |= 4;
    groups_.write_int(70, flags);
    groups_.write_int(62, l.on ? l.color.aci : -l.color.aci);
    write_color(l.color, false);
    write_code(6, symbol_ref(SymbolKind::Linetype, l.linetype));
    if (!l.plot) groups_.write_int(290, 0);
    groups_.write_int(370, l.lineweight);
    const std::string& plot_style = l.plot_style_handle.empty() ? normal_plot_style_
                                                                : l.plot_style_handle;
    if (!plot_style.empty()) write_code(390, plot_style);
    // 2007: the material, Global unless the layer has another (AutoCAD writes it on every layer)
    if (version_ >= DxfVersion::R2007) {
        const std::string& material = l.material_handle.empty() ? global_material_ : l.material_handle;
        write_pointer(347, material);
    }
    // 2013: 348 last, 0 on every layer of AutoCAD's 2013 files (in no reference; B58)
    if (version_ >= DxfVersion::R2013) write_code(348, l.pointer_348.empty() ? std::string("0") : l.pointer_348);
    // XDATA, then the transparency where AutoCAD has it: last (B22). The AcCmTransparency
    // 1071 0 AutoCAD writes on some layers (no transparency) is left as read, and replaced by
    // a transparency that is set
    if (l.transparency.is_by_layer()) {
        write_xdata(l.xdata);
    } else {
        std::vector<XdataList> rest;
        for (const auto& x : l.xdata) if (!is_no_transparency(x)) rest.push_back(x);
        write_xdata(rest);
    }
    if (!l.transparency.is_by_layer()) {
        write_code(1001, "AcCmTransparency");
        groups_.write_int(1071, l.transparency.raw);
    }
}

void DxfWriter::write_style_r2000(const TextStyle& s, const std::string& handle) {
    write_table_entry_head("STYLE", handle, table_handles_["STYLE"], "AcDbTextStyleTableRecord", s.xdictionary);
    write_code(2, s.name);
    groups_.write_int(70, s.flags);
    groups_.write_double(40, s.fixed_height);
    groups_.write_double(41, s.width_factor);
    groups_.write_double(50, s.oblique_angle);
    groups_.write_int(71, s.generation_flags);
    groups_.write_double(42, s.last_height);
    write_code(3, s.font_file);
    write_code(4, s.big_font);
    // XDATA, then the font family where AutoCAD has it: last (rättningsfas R.8, ACadSharp's
    // sample_AC1018: AcadAnnotative, then ACAD)
    write_xdata(s.xdata);
    if (!s.font_family.empty()) {
        write_code(1001, "ACAD");
        write_code(1000, s.font_family);
        groups_.write_int(1071, s.font_flags);
    }
}

// The arrow blocks are names in R12 (5, 6, 7) and BLOCK_RECORD handles in 2000 (342-344)
void DxfWriter::write_dimstyle_r2000(const DimStyle& ds, const std::string& handle) {
    // R13 and R14 have their own form: every variable, the arrow blocks as names, DIMUNIT and DIMFIT
    if (version_ < DxfVersion::R2000) {
        write_dimstyle_legacy(ds, handle);
        return;
    }
    write_table_entry_head("DIMSTYLE", handle, table_handles_["DIMSTYLE"],
                           "AcDbDimStyleTableRecord", {}, &ds.reactors);
    write_code(2, ds.name);
    groups_.write_int(70, ds.flags);

    // As AutoCAD writes R13+: only what differs from AutoCAD's own values (Standard in its
    // files is 2, 70 and 340), in ascending group code order; the text style (340) always
    // (rättningsfas R.2)
    const DimStyle d0 = autocad_dim_style();
    auto str = [&](int code, const std::string& v, const std::string& v0) {
        if (v != v0) write_code(code, v);
    };
    auto dbl = [&](int code, double v, double v0) {
        if (v != v0) groups_.write_double(code, v);
    };
    auto num = [&](int code, int v, int v0) {
        if (v != v0) groups_.write_int(code, v);
    };
    str(3, ds.dimpost, d0.dimpost);
    str(4, ds.dimapost, d0.dimapost);
    dbl(40, ds.dimscale, d0.dimscale);
    dbl(41, ds.dimasz, d0.dimasz);
    dbl(42, ds.dimexo, d0.dimexo);
    dbl(43, ds.dimdli, d0.dimdli);
    dbl(44, ds.dimexe, d0.dimexe);
    dbl(45, ds.dimrnd, d0.dimrnd);
    dbl(46, ds.dimdle, d0.dimdle);
    dbl(47, ds.dimtp, d0.dimtp);
    dbl(48, ds.dimtm, d0.dimtm);
    // 2007: the variables 2000/2004 keep in XDATA are group codes, in the same ascending
    // order (DIMTFILLCLR is the second 70, its ACI) — ACadSharp's sample_AC1021, AutoCAD 2016's
    // new drawing (50 between 44 and 73)
    bool codes = version_ >= DxfVersion::R2007;
    if (codes) {
        dbl(49, ds.dimfxl, d0.dimfxl);
        dbl(50, ds.dimjogang, d0.dimjogang);
        num(69, ds.dimtfill, d0.dimtfill);
        num(70, ds.dimtfillclr.aci, d0.dimtfillclr.aci);
    }
    num(71, ds.dimtol, d0.dimtol);
    num(72, ds.dimlim, d0.dimlim);
    num(73, ds.dimtih, d0.dimtih);
    num(74, ds.dimtoh, d0.dimtoh);
    num(75, ds.dimse1, d0.dimse1);
    num(76, ds.dimse2, d0.dimse2);
    num(77, ds.dimtad, d0.dimtad);
    num(78, ds.dimzin, d0.dimzin);
    num(79, ds.dimazin, d0.dimazin);
    if (codes) num(90, ds.dimarcsym, d0.dimarcsym);
    dbl(140, ds.dimtxt, d0.dimtxt);
    dbl(141, ds.dimcen, d0.dimcen);
    dbl(142, ds.dimtsz, d0.dimtsz);
    dbl(143, ds.dimaltf, d0.dimaltf);
    dbl(144, ds.dimlfac, d0.dimlfac);
    dbl(145, ds.dimtvp, d0.dimtvp);
    dbl(146, ds.dimtfac, d0.dimtfac);
    dbl(147, ds.dimgap, d0.dimgap);
    dbl(148, ds.dimaltrnd, d0.dimaltrnd);
    num(170, ds.dimalt, d0.dimalt);
    num(171, ds.dimaltd, d0.dimaltd);
    num(172, ds.dimtofl, d0.dimtofl);
    num(173, ds.dimsah, d0.dimsah);
    num(174, ds.dimtix, d0.dimtix);
    num(175, ds.dimsoxd, d0.dimsoxd);
    num(176, ds.dimclrd, d0.dimclrd);
    num(177, ds.dimclre, d0.dimclre);
    num(178, ds.dimclrt, d0.dimclrt);
    num(179, ds.dimadec, d0.dimadec);
    num(271, ds.dimdec, d0.dimdec);
    num(272, ds.dimtdec, d0.dimtdec);
    num(273, ds.dimaltu, d0.dimaltu);
    num(274, ds.dimalttd, d0.dimalttd);
    num(275, ds.dimaunit, d0.dimaunit);
    num(276, ds.dimfrac, d0.dimfrac);
    num(277, ds.dimlunit, d0.dimlunit);
    num(278, ds.dimdsep, d0.dimdsep);
    num(279, ds.dimtmove, d0.dimtmove);
    num(280, ds.dimjust, d0.dimjust);
    num(281, ds.dimsd1, d0.dimsd1);
    num(282, ds.dimsd2, d0.dimsd2);
    num(283, ds.dimtolj, d0.dimtolj);
    num(284, ds.dimtzin, d0.dimtzin);
    num(285, ds.dimaltz, d0.dimaltz);
    num(286, ds.dimalttz, d0.dimalttz);
    num(288, ds.dimupt, d0.dimupt);
    num(289, ds.dimatfit, d0.dimatfit);
    if (codes) num(290, ds.dimfxlon, d0.dimfxlon);
    if (version_ >= DxfVersion::R2010) num(295, ds.dimtxtdirection, d0.dimtxtdirection);
    write_code(340, style_handle(ds.dimtxsty));

    const std::string what = "Dimension style \"" + ds.name + "\"";
    const std::pair<int, const std::string*> arrows[] = {
        {341, &ds.dimldrblk}, {342, &ds.dimblk}, {343, &ds.dimblk1}, {344, &ds.dimblk2},
    };
    for (const auto& [code, name] : arrows) {
        if (name->empty()) continue;
        std::string record = block_handle_for(what, *name);
        if (!record.empty()) write_code(code, record);
    }
    if (codes) {
        const std::pair<int, const std::string*> linetypes[] = {
            {345, &ds.dimltype}, {346, &ds.dimltex1}, {347, &ds.dimltex2},
        };
        // ByBlock too when the style names it (AutoCAD Mechanical's AM_ISO: 345 to ByBlock)
        for (const auto& [code, name] : linetypes) {
            if (name->empty()) continue;
            write_code(code, linetype_handle(what, *name));
        }
    }
    num(371, ds.dimlwd, d0.dimlwd);
    num(372, ds.dimlwe, d0.dimlwe);
    // XDATA, then (2000/2004) the newer variables where AutoCAD has them: last (ACadSharp's
    // sample_AC1018: ACAD_DSTYLE_DIMBREAK … DIMTALN, then DIMTEXT_FILL, DIM_LINETYPE …)
    write_xdata(ds.xdata);
    if (!codes) {
        write_xdata(dimstyle_variables(ds, [&](const std::string& name) { return linetype_handle(what, name); }));
    }
}

void DxfWriter::write_block_record_r2000(const Block& b) {
    write_table_entry_head("BLOCK_RECORD", b.record.handle, table_handles_["BLOCK_RECORD"],
                           "AcDbBlockTableRecord", b.record.xdictionary);
    write_code(2, written_name(b));
    // The layout of the record: 2000's, R13 and R14 have no LAYOUT objects of a new drawing
    // (AutoCAD's files have no 340 in BLOCK_RECORD, B89)
    if (version_ >= DxfVersion::R2000) {
        write_code(340, b.record.layout_handle.empty() ? "0" : b.record.layout_handle);
    }
    // The preview after 340, before the references (AutoCAD's files; ACadSharp's my_block_v2)
    if (!b.record.preview.empty() && version_ >= DxfVersion::R2000) write_binary_chunks(b.record.preview);
    // The INSERTs of the block, and any other reference the record already had
    // (the original's relations when it is the original that is written, for comparison)
    const Relations& relations = !filter_live_ && template_mode() ? orig_block_refs_ : block_refs_;
    std::vector<std::string> refs;
    if (auto found = relations.find(upper(b.name)); found != relations.end()) refs = found->second;
    for (const auto& r : b.record.block_refs) {
        if (std::find(refs.begin(), refs.end(), r) == refs.end()) refs.push_back(r);
    }
    if (filter_live_) {
        refs.erase(std::remove_if(refs.begin(), refs.end(),
                                  [&](const std::string& r) { return !is_live(r); }),
                   refs.end());
    }
    // R13 and R14 have no list of the references: the record is the name only (the R13 and R14 DXF
    // references, AutoCAD's files)
    if (!refs.empty() && version_ >= DxfVersion::R2000) {
        write_code(102, "{BLKREFS");
        for (const auto& r : refs) write_code(331, r);
        write_code(102, "}");
    }
    // 2007: units, explodability and scaling after the references (in 2004 AutoCAD keeps
    // them in an XRECORD of the record's extension dictionary)
    if (version_ >= DxfVersion::R2007) {
        groups_.write_int(70, b.record.insert_units);
        groups_.write_int(280, b.record.explodable);
        groups_.write_int(281, b.record.scalable);
    }
    write_xdata(b.record.xdata);
}

// ---------------------------------------------------------------------------
// BLOCKS
// ---------------------------------------------------------------------------

void DxfWriter::write_blocks_r2000(const Drawing& dwg) {
    write_code(0, "SECTION");
    write_code(2, "BLOCKS");
    for (const auto& b : dwg.blocks()) write_block_r2000(b);
    for (const auto& b : generated_dim_blocks_) write_block_r2000(b);
    write_code(0, "ENDSEC");
}

// `end`: the ENDBLK's, whose color is its own (plan 9.2)
static EntityProperties block_props(const Block& b, bool end = false) {
    EntityProperties props;
    props.layer = b.layer;
    props.paper_space = is_paper_space(b.name) ? 1 : 0;
    const OptionalBoxed<Color>& color = end ? b.endblk_color : b.block_color;
    if (color) props.color = *color;
    props.linetype = b.linetype;
    props.lineweight = b.lineweight;
    props.linetype_scale = b.linetype_scale;
    props.visible = b.visible;
    return props;
}

void DxfWriter::write_block_begin_r2000(const Block& b) {
    write_entity_head("BLOCK", b.handle, b.record.handle, block_props(b));
    write_code(100, "AcDbBlockBegin");
    write_code(2, written_name(b));
    int flags = b.flags;
    if (!dynamic_block_names_.empty() && dynamic_block_names_.count(upper(b.name))) flags |= 1;     // anonymous (step 9.9)
    for (const auto& e : b.entities) {
        if (e.type == EntityType::Attdef) { flags |= 2; break; }
    }
    groups_.write_int(70, flags);
    // An unloaded xref: AutoCAD 2025 writes 71 1 after 70 in 2000 … 2018 (xref_unloaded); R12 has none
    if (b.xref_unloaded != 0 && version_ >= DxfVersion::R2000) groups_.write_int(71, b.xref_unloaded);
    write_point(10, b.base_point);
    write_code(3, written_name(b));
    write_code(1, b.xref_path);
    // The description is 2000's: R13's BLOCK ends with 3 and 1 (B93)
    if (!b.description.empty() && version_ >= DxfVersion::R2000) write_code(4, b.description);
    write_xdata(b.xdata);
}

void DxfWriter::write_block_end_r2000(const Block& b) {
    write_entity_head("ENDBLK", b.endblk_handle, b.record.handle, block_props(b, true));
    write_code(100, "AcDbBlockEnd");
}

// BLOCK, its entities and ENDBLK — each from its template for a drawing read from a
// file. A dimension block whose DIMENSION changed gets the new geometry (B12).
void DxfWriter::write_block_r2000(const Block& b) {
    const Block* orig = nullptr;
    if (template_mode()) {
        auto it = orig_blocks_.find(handle_value(b.handle));
        if (it != orig_blocks_.end()) orig = it->second;
    }
    emit({&b.raw}, false, [&] { write_block_begin_r2000(b); },
         orig ? std::function<void()>([&] { write_block_begin_r2000(*orig); }) : nullptr);

    auto regen = regenerated_.find(upper(b.name));
    if (regen != regenerated_.end()) {
        changed_ = true;
        for (const auto& e : regen->second) write_entity_r2000(e, b.record.handle);
    } else {
        for (const auto& e : b.entities) write_entity_template(e, b.record.handle);
    }

    emit({&b.endblk_raw}, false, [&] { write_block_end_r2000(b); },
         orig ? std::function<void()>([&] { write_block_end_r2000(*orig); }) : nullptr);
}

// ---------------------------------------------------------------------------
// ENTITIES
// ---------------------------------------------------------------------------

void DxfWriter::write_entities_r2000(const Drawing& dwg) {
    std::string model = record_handle("*Model_Space");
    std::string paper = record_handle("*Paper_Space");
    write_code(0, "SECTION");
    write_code(2, "ENTITIES");
    // R13 (plan phase 7, step 7.16, D6): Autodesk's R13 reference says "all paper space entities will be
    // output, followed by model space entities" — the stable partition, for a drawing built here (one
    // read from a file keeps the order it had). R14's AutoCAD files have the paper space last (puppenhaus:
    // model space, then the two VIEWPORTs): a drawing is written in its list order, as 2000 and later.
    if (version_ == DxfVersion::R13 && !template_mode()) {
        for (const auto& e : dwg.entities()) {
            if (e.props.paper_space) write_entity_template(e, paper);
        }
        for (const auto& e : dwg.entities()) {
            if (!e.props.paper_space) write_entity_template(e, model);
        }
    } else {
        for (const auto& e : dwg.entities()) write_entity_template(e, e.props.paper_space ? paper : model);
    }
    write_code(0, "ENDSEC");
}

// 0, 5, 330, then AcDbEntity with the common properties in AutoCAD's order: 60 before
// 370 as in AutoCAD's files 2000-2018, 48 before 60 as in the reference (plan B29).
// Thickness and extrusion belong to each type's own subclass in 2000.
void DxfWriter::write_entity_head(const char* type, const std::string& handle,
                                  const std::string& owner, const EntityProperties& props,
                                  const std::vector<uint8_t>* proxy) {
    write_code(0, type);
    write_code(5, handle);
    // The extension dictionary before the reactors, as in AutoCAD's files (the reference has
    // them the other way round; plan phase 5, 5.3)
    // R13 and R14: the reactors first, then the extension dictionary, as their references have them
    // (plan phase 7, format profile 7.1)
    const bool older = version_ < DxfVersion::R2000;
    if (older) write_reactors(props.reactors, handle);
    if (!props.xdictionary.empty() && (!filter_live_ || is_live(props.xdictionary))) {
        write_code(102, "{ACAD_XDICTIONARY");
        write_code(360, props.xdictionary);
        write_code(102, "}");
    }
    if (!older) write_reactors(props.reactors, handle);
    if (owner_groups_written()) write_code(330, owner);
    write_code(100, "AcDbEntity");
    if (props.paper_space != 0) groups_.write_int(67, props.paper_space);
    write_code(8, symbol_ref(SymbolKind::Layer, props.layer));
    if (!props.linetype.empty()) write_code(6, symbol_ref(SymbolKind::Linetype, props.linetype));
    // 2007: the material after the linetype, before the color (ACadSharp's sample_AC1021)
    if (version_ >= DxfVersion::R2007) write_pointer(347, props.material_handle);
    write_color(props.color, !props.color.is_by_layer());
    if (version_ >= DxfVersion::R2004 && !props.transparency.is_by_layer()) {
        groups_.write_int(440, props.transparency.raw);
    }
    if (props.linetype_scale != 1.0) groups_.write_double(48, props.linetype_scale);
    if (!props.visible) groups_.write_int(60, 1);
    // Proxy graphics: the size (92; 160 from 2010) and the bytes in 310 (the MULTILEADERs of
    // ACadSharp's samples, after 60 and before 370)
    if (proxy && !proxy->empty()) {
        groups_.write_int(version_ >= DxfVersion::R2010 ? 160 : 92, static_cast<int64_t>(proxy->size()));
        write_binary_chunks(*proxy);
    }
    // 370, 380 and 390 are 2000's: R13 and R14 have no lineweight or plot style (plan phase 7, B93; a
    // program that sets one gets "Lineweight requires DXF 2000 or later" before anything is written)
    if (version_ >= DxfVersion::R2000) {
        if (props.lineweight != -1) groups_.write_int(370, props.lineweight);
        if (props.plot_style_type != 0) groups_.write_int(380, props.plot_style_type);
        if (!props.plot_style_handle.empty()) write_code(390, props.plot_style_handle);
    }
    // 2007: the shadow mode last (the 2009 … 2018 references' common entity codes; AutoCAD's
    // files have none set)
    if (version_ >= DxfVersion::R2007 && props.shadow_mode != 0) groups_.write_int(284, props.shadow_mode);
}

// 102 {ACAD_REACTORS: the reactors the record was read with, then those HDxf's relations give
// it (a GROUP's members, a hatch's boundary, a leader's annotation) — each once, none to a
// record that is gone (B19)
void DxfWriter::write_reactors(const std::vector<std::string>& read, const std::string& handle) {
    std::vector<std::string> live;
    auto add = [&](const std::string& r) {
        if (r.empty() || (filter_live_ && !is_live(r))) return;
        if (std::find(live.begin(), live.end(), r) == live.end()) live.push_back(r);
    };
    for (const auto& r : read) add(r);
    const Relations& relations = !filter_live_ && template_mode() ? orig_reactors_ : reactors_;
    auto related = relations.find(upper(handle));
    if (related != relations.end()) {
        for (const auto& r : related->second) add(r);
    }
    if (live.empty()) return;
    write_code(102, "{ACAD_REACTORS");
    for (const auto& r : live) write_code(330, r);
    write_code(102, "}");
}

// Bytes in 310 groups of 127, as AutoCAD writes every 310 — in ASCII and binary alike
void DxfWriter::write_binary_chunks(const std::vector<uint8_t>& bytes) {
    static const char digits[] = "0123456789ABCDEF";
    const size_t chunk = 127;
    for (size_t at = 0; at < bytes.size(); at += chunk) {
        std::string hex;
        for (size_t i = at; i < bytes.size() && i < at + chunk; ++i) {
            hex += digits[bytes[i] >> 4];
            hex += digits[bytes[i] & 15];
        }
        write_code_typed(310, hex);
    }
}

// THUMBNAILIMAGE (B26): as read while the bytes are unchanged, else 90 and the bytes in
// 310 groups of 127, as AutoCAD (and ODA) write every 310 — in ASCII and binary alike
void DxfWriter::write_thumbnail(const Drawing& dwg) {
    const std::vector<uint8_t>& bytes = dwg.thumbnail();
    bool had = template_mode() && !original_->thumbnail().empty();
    if (bytes.empty()) {
        if (had) changed_ = true;
        return;
    }
    if (had && original_->thumbnail() == bytes && !dwg.thumbnail_raw().empty()) {
        write_raw_record(dwg.thumbnail_raw());
        write_code(0, "ENDSEC");
        return;
    }
    if (template_mode()) changed_ = true;
    write_code(0, "SECTION");
    write_code(2, "THUMBNAILIMAGE");
    groups_.write_int(90, static_cast<int64_t>(bytes.size()));
    write_binary_chunks(bytes);
    write_code(0, "ENDSEC");
}

// 62 (when `aci`), then from 2004 the true color (420) and color book name (430), as
// AutoCAD writes them after 62 (B21)
void DxfWriter::write_color(const Color& c, bool aci) {
    if (aci) groups_.write_int(62, c.aci);
    if (version_ < DxfVersion::R2004) return;
    if (c.has_rgb()) groups_.write_int(420, c.rgb);
    if (!c.name.empty()) write_code(430, c.name);
}

void DxfWriter::write_thickness(const EntityProperties& props) {
    if (props.thickness != 0.0) groups_.write_double(39, props.thickness);
}

void DxfWriter::write_extrusion(const EntityProperties& props) {
    const Point3& x = props.extrusion;
    if (x.x != 0.0 || x.y != 0.0 || x.z != 1.0) write_point(210, x);
}

// The XDATA a LIGHT, SUN or VISUALSTYLE of 2018 has: not the list ACAD with AcDbSavedByObjectVersion
// alone, which 2013 and earlier write on them (ACadSharp's sample_AC1027 against sample_AC1032; B77)
static std::vector<XdataList> without_saved_by_version(const std::vector<XdataList>& xdata) {
    std::vector<XdataList> out;
    for (const auto& x : xdata) {
        bool saved_by = x.app_name == "ACAD" && x.items.size() == 2 && x.items[0].code == 1000 &&
                        x.items[0].value == "AcDbSavedByObjectVersion" && x.items[1].code == 1070;
        if (!saved_by) out.push_back(x);
    }
    return out;
}

void DxfWriter::write_entity_r2000(const Entity& e, const std::string& owner) {
    const EntityProperties& p = e.props;
    // The typed records of phase 9: the family's codec writes the whole record, head and XDATA included
    if (const EntityCodec* codec = TypedRegistry::instance().find_entity(e.type); codec && codec->write) {
        RecordWriter writer(*this);
        codec->write(writer, e, owner);
        return;
    }
    switch (e.type) {
        case EntityType::Line: {
            const auto& d = e.as_line();
            write_entity_head("LINE", p.handle, owner, p);
            write_code(100, "AcDbLine");
            write_thickness(p);
            write_point(10, d.start);
            write_point(11, d.end);
            write_extrusion(p);
            break;
        }
        case EntityType::Circle:
        case EntityType::Arc: {
            bool arc = e.type == EntityType::Arc;
            const Point3& center = arc ? e.as_arc().center : e.as_circle().center;
            double radius = arc ? e.as_arc().radius : e.as_circle().radius;
            write_entity_head(arc ? "ARC" : "CIRCLE", p.handle, owner, p);
            write_code(100, "AcDbCircle");
            write_thickness(p);
            write_point(10, center);
            groups_.write_double(40, radius);
            // ARC too: 210 in AcDbCircle, before AcDbArc and its angles — as AutoCAD's own DXF
            // writer has it (Fusion 360's files, f360_shaker; ODA's and ACadSharp's alike); the
            // references put it after the angles (Autodesk's DXF Reference 2014, p. 84). R13 and R14 too (R13:ARC lists
            // 210 last), but no AutoCAD-origin R13/R14 file has an ARC with a normal, and the AutoCAD writer
            // of 2000 on is one dxfOut of AcDbCircle; QDXDivide's R14 files have 210 here: kept (step 7.11)
            write_extrusion(p);
            if (arc) {
                write_code(100, "AcDbArc");
                groups_.write_double(50, e.as_arc().start_angle);
                groups_.write_double(51, e.as_arc().end_angle);
            }
            break;
        }
        case EntityType::Point: {
            const auto& d = e.as_point();
            write_entity_head("POINT", p.handle, owner, p);
            write_code(100, "AcDbPoint");
            write_point(10, d.location);
            write_thickness(p);
            write_extrusion(p);
            if (d.x_axis_angle != 0.0) groups_.write_double(50, d.x_axis_angle);
            break;
        }
        case EntityType::Text:
            write_text_r2000(e, owner);
            break;
        case EntityType::Polyline:
            write_polyline_r2000(e, owner);
            return;                         // XDATA is written with the POLYLINE record
        case EntityType::Trace:
        case EntityType::Solid: {
            bool solid = e.type == EntityType::Solid;
            const Point3* corners[4];
            if (solid) {
                const auto& d = e.as_solid();
                corners[0] = &d.p0; corners[1] = &d.p1; corners[2] = &d.p2; corners[3] = &d.p3;
            } else {
                const auto& d = e.as_trace();
                corners[0] = &d.p0; corners[1] = &d.p1; corners[2] = &d.p2; corners[3] = &d.p3;
            }
            write_entity_head(solid ? "SOLID" : "TRACE", p.handle, owner, p);
            write_code(100, "AcDbTrace");
            for (int i = 0; i < 4; ++i) write_point(10 + i, *corners[i]);
            write_thickness(p);
            write_extrusion(p);
            break;
        }
        case EntityType::Face3D: {
            const auto& d = e.as_face3d();
            write_entity_head("3DFACE", p.handle, owner, p);
            write_code(100, "AcDbFace");
            write_point(10, d.p0);
            write_point(11, d.p1);
            write_point(12, d.p2);
            write_point(13, d.p3);
            if (d.invisible_edges != 0) groups_.write_int(70, d.invisible_edges);
            break;
        }
        case EntityType::Shape: {
            const auto& d = e.as_shape();
            write_entity_head("SHAPE", p.handle, owner, p);
            write_code(100, "AcDbShape");
            write_thickness(p);
            write_point(10, d.insertion_point);
            groups_.write_double(40, d.size);
            write_code(2, d.name);
            if (d.rotation != 0.0) groups_.write_double(50, d.rotation);
            if (d.x_scale != 1.0) groups_.write_double(41, d.x_scale);
            if (d.oblique_angle != 0.0) groups_.write_double(51, d.oblique_angle);
            write_extrusion(p);
            break;
        }
        case EntityType::Insert:
            write_insert_r2000(e, owner);
            return;                         // XDATA is written with the INSERT record
        case EntityType::Attdef:
            write_attdef_r2000(e, owner);
            break;
        case EntityType::Attrib: {
            InsertAttribute a;
            a.data = e.as_attrib();
            a.props = p;
            write_attrib_r2000(a, owner);
            break;
        }
        case EntityType::Dimension:
            write_dimension_r2000(e, owner);
            break;
        case EntityType::Viewport:
            write_viewport_r2000(e, owner);
            return;                         // writes its own XDATA
        case EntityType::LwPolyline:
            write_lwpolyline_r2000(e, owner);
            break;
        case EntityType::Ellipse: {
            const auto& d = e.as_ellipse();
            write_entity_head("ELLIPSE", p.handle, owner, p);
            write_code(100, "AcDbEllipse");
            write_point(10, d.center);
            write_point(11, d.major_axis);
            write_point(210, p.extrusion);  // always, as AutoCAD writes it
            groups_.write_double(40, d.ratio);
            groups_.write_double(41, d.start_param);
            groups_.write_double(42, d.end_param);
            break;
        }
        case EntityType::Xline:
        case EntityType::Ray: {
            bool ray = e.type == EntityType::Ray;
            write_entity_head(ray ? "RAY" : "XLINE", p.handle, owner, p);
            write_code(100, ray ? "AcDbRay" : "AcDbXline");
            write_point(10, ray ? e.as_ray().base_point : e.as_xline().base_point);
            write_point(11, ray ? e.as_ray().direction : e.as_xline().direction);
            break;
        }
        case EntityType::MText:
            write_mtext_r2000(e, owner);
            break;
        case EntityType::Tolerance: {
            const auto& d = e.as_tolerance();
            write_entity_head("TOLERANCE", p.handle, owner, p);
            write_code(100, "AcDbFcf");
            write_code(3, symbol_ref(SymbolKind::DimStyle, d.dim_style));
            write_point(10, d.insertion_point);
            write_code(1, d.content);
            write_extrusion(p);
            if (!is_x_axis(d.x_axis_direction)) write_point(11, d.x_axis_direction);
            break;
        }
        case EntityType::Leader:
            write_leader_r2000(e, owner);
            break;
        case EntityType::Spline:
            write_spline_r2000(e, owner);
            break;
        case EntityType::Hatch:
            write_hatch_r2000(e, owner);
            break;
        case EntityType::Mline:
            write_mline_r2000(e, owner);
            break;
        case EntityType::Image:
        case EntityType::Wipeout:
            write_image_r2000(e, owner);
            break;
        case EntityType::Solid3D:
        case EntityType::Region:
        case EntityType::Body: {
            const auto& d = e.as_acis();
            const char* name = e.type == EntityType::Solid3D ? "3DSOLID"
                             : e.type == EntityType::Region ? "REGION" : "BODY";
            write_entity_head(name, p.handle, owner, p);
            write_modeler_geometry(d);
            // 2007: a 3DSOLID's history (owned by it), 0 without one (B33)
            if (e.type == EntityType::Solid3D && version_ >= DxfVersion::R2007) {
                write_code(100, "AcDb3dSolid");
                bool history = !d.history_handle.empty() && (!filter_live_ || is_live(d.history_handle));
                write_code(350, history ? d.history_handle : "0");
            }
            break;
        }
        case EntityType::MLeader:
            write_mleader_r2000(e, owner);
            break;
        case EntityType::Surface:
        case EntityType::ExtrudedSurface:
        case EntityType::LoftedSurface:
        case EntityType::RevolvedSurface:
        case EntityType::SweptSurface:
        case EntityType::PlaneSurface:
        case EntityType::NurbsSurface:
            write_surface_r2000(e, owner);
            break;
        case EntityType::Helix:
            write_helix_r2000(e, owner);
            break;
        case EntityType::Light:                 // written by its codec (above, step 9.11)
        case EntityType::SectionObject:         // written by its codec (above, step 9.12)
        case EntityType::ArcDimension:          // written by its codec (above, step 9.17)
        case EntityType::LargeRadialDimension:
        case EntityType::CoordinationModel:     // written by its codec (above, step 9.6)
        case EntityType::PdfUnderlay:
        case EntityType::DwfUnderlay:
        case EntityType::DgnUnderlay:
            break;
        case EntityType::Mesh:
            write_mesh_r2000(e, owner);
            break;
        case EntityType::OleFrame:
        case EntityType::Ole2Frame:
        case EntityType::ProxyEntity:
        case EntityType::Table:
        case EntityType::TcPoint:
            break;                  // written by their codecs (above)
        case EntityType::Unknown:
            write_unknown_r2000(e, owner);
            break;
    }
    // An MTEXT's columns, their links, its frame and its defined height are XDATA up to 2013 (B73,
    // B74): the blocks first in the entity's list ACAD (AutoCAD's 2000 file has MTEXTBEGIN … after
    // them)
    if (e.type == EntityType::MText) {
        std::vector<XdataList> xdata = p.xdata;
        XdataList blocks = mtext_xdata(e.as_mtext(), version_);
        if (!blocks.app_name.empty()) {
            auto acad = std::find_if(xdata.begin(), xdata.end(), [](const XdataList& x) { return x.app_name == "ACAD"; });
            if (acad == xdata.end()) xdata.push_back(std::move(blocks));
            else acad->items.insert(acad->items.begin(), blocks.items.begin(), blocks.items.end());
        }
        write_xdata(xdata);
    } else {
        write_xdata(p.xdata);
    }
    // Before 2013 a body's change GUID is XDATA, last (B60)
    if (version_ < DxfVersion::R2013) {
        const AcisData* acis = acis_of(e);
        XdataList guid;
        if (acis && !acis->guid.empty()) {
            if (change_guid_xdata(acis->guid, guid)) write_xdata({guid});
            else if (error_.empty()) error_ = "The change GUID \"" + acis->guid + "\" is not a GUID";
        }
    }
    if (e.type == EntityType::MLeader) write_mleader_version(e.as_mleader().version);
    if (e.type == EntityType::Attdef) write_attribute_lock(e.as_attdef().lock_position);
    if (e.type == EntityType::Attrib) write_attribute_lock(e.as_attrib().lock_position);
}

// The text in parts of at most 250 characters as written: 3 …, then the last in 1 (B16).
// A style other than Standard (7), a direction other than X (11) — as AutoCAD writes it.
// `props`: the entity's, for its extrusion (210); none for the text of a multiline attribute,
// whose normal is the attribute's.
void DxfWriter::write_mtext_body(const MTextData& d, const EntityProperties* props) {
    write_point(10, d.insertion_point);
    groups_.write_double(40, d.height);
    groups_.write_double(41, d.reference_width);
    if (version_ >= DxfVersion::R2007) groups_.write_double(46, d.defined_height);
    groups_.write_int(71, d.attachment);
    groups_.write_int(72, d.drawing_direction);
    // 250 characters a part from 2004 (^J one; UTF-16 units, as ODA File Converter's 2010), 250
    // as written in 2000 (^J two) — ACadSharp's samples, AC1015 against AC1018 … AC1032
    // (R13: 255, its reference; R14 250, as 2000)
    std::vector<std::string> parts = split_encoded(d.content, text_code_page_, format_ != DxfFormat::Binary,
                                                   version_ == DxfVersion::R13 ? 255 : 250,
                                                   version_ >= DxfVersion::R2004);
    for (size_t i = 0; i + 1 < parts.size(); ++i) write_code(3, parts[i]);
    write_code(1, parts.back());
    if (!names_equal(d.style, "Standard")) write_code(7, symbol_ref(SymbolKind::Style, d.style));
    if (props) write_extrusion(*props);
    if (!is_x_axis(d.x_axis_direction)) write_point(11, d.x_axis_direction);
    // Line spacing is 2000's (R13 and R14: 10 40 41 71 72 1 3 7 210 11, as AutoCAD's R14 files have it)
    if (version_ >= DxfVersion::R2000) {
        groups_.write_int(73, d.line_spacing_style);
        groups_.write_double(44, d.line_spacing_factor);
    }
    // The background fill (2004+), as AutoCAD writes it when it is on
    const MTextBackground& bg = d.background;
    if ((bg.flags & 2) && !(bg.flags & 1) && error_.empty()) {
        error_ = "An MTEXT background with the window's color (2) is 3: 2 needs 1";
    }
    if (bg.flags != 0 && version_ >= DxfVersion::R2004) {
        groups_.write_int(90, bg.flags);
        // The fill's color, scale and transparency: before 2018 only with a fill (1, 2) — a frame
        // alone (16) has none, and AutoCAD's core refuses a file with them (ezdxf's
        // mtext_text_frame.dxf has 90 16 alone); 2018's core refuses a frame without them (B74)
        if ((bg.flags & 3) || version_ >= DxfVersion::R2018) {
            groups_.write_int(63, bg.color.aci);
            if (bg.color.has_rgb()) groups_.write_int(421, bg.color.rgb);
            if (!bg.color.name.empty()) write_code(431, bg.color.name);
            groups_.write_double(45, bg.scale);
            groups_.write_int(441, bg.transparency.raw);
        }
    }
}

// 2018's embedded object of an MTEXT: the attachment point (70), the direction (10), the insertion
// point (11), the width (40) and the defined height (41) of the entity, the size as drawn (42, 43)
// and the columns (71, 72, 44, 45, 73, 74, 46 per height) — ACadSharp's sample_AC1032. AutoCAD takes
// these values over the entity's own, which are the same (B73, B74).
void DxfWriter::write_mtext_embedded(const MTextData& d) {
    const MTextColumns& c = *d.columns;
    // The size as drawn: what the layout measures, or AutoCAD's values as read when it has no font
    double measured_width = d.embedded ? d.embedded->measured_width : 0.0;
    double measured_height = d.embedded ? d.embedded->measured_height : 0.0;
    if (drawing_) {
        std::unique_ptr<TextLayout>& cache = filter_live_ ? mtext_layout_ : original_mtext_layout_;
        const Drawing* source = filter_live_ || !original_ ? drawing_ : original_;
        if (!cache) cache = std::make_unique<TextLayout>(*source);
        MTextSize size = cache->mtext(d);
        if (size.font) {
            measured_width = size.width;
            measured_height = size.height;
        }
    }
    write_code(101, "Embedded Object");
    groups_.write_int(70, d.attachment);
    write_point(10, d.x_axis_direction);
    write_point(11, d.insertion_point);
    groups_.write_double(40, d.reference_width);
    groups_.write_double(41, d.defined_height);
    groups_.write_double(42, measured_width);
    groups_.write_double(43, measured_height);
    groups_.write_int(71, c.type);
    groups_.write_int(72, c.count);
    groups_.write_double(44, c.width);
    groups_.write_double(45, c.gutter);
    groups_.write_int(73, c.auto_height ? 1 : 0);
    groups_.write_int(74, c.reversed ? 1 : 0);
    for (double h : c.heights) groups_.write_double(46, h);
}

void DxfWriter::write_mtext_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_mtext();
    const EntityProperties& p = e.props;
    write_entity_head("MTEXT", p.handle, owner, p);
    write_code(100, "AcDbMText");
    // Before 2018 the text of a text with columns is cut over its entities (B74)
    const MTextData* text = &d;
    MTextData piece;
    if (version_ < DxfVersion::R2018) {
        const Drawing* source = filter_live_ || !original_ ? drawing_ : original_;
        const MTextData* first = &d;
        size_t index = 0;
        if (!d.column_parent.empty() && source) {
            const Entity* parent = source->find_entity(d.column_parent);
            if (parent && parent->type == EntityType::MText && parent->as_mtext().columns) {
                first = &parent->as_mtext();
                const auto& linked = first->columns->linked;
                for (size_t k = 0; k < linked.size(); ++k) {
                    if (linked[k] == p.handle) index = k + 1;
                }
            }
        }
        if (first->columns && (index > 0 || !first->columns->linked.empty())) {
            std::vector<std::string> pieces = column_pieces(first->content, *first->columns);
            piece = d;
            piece.content = index < pieces.size() ? pieces[index] : std::string();
            text = &piece;
        }
    }
    write_mtext_body(*text, &p);
    if (version_ >= DxfVersion::R2018 && d.columns) write_mtext_embedded(d);
}

// Values at their default are left out (71 1, 72 0, 210–213), as AutoCAD writes it; the
// annotation (340) only while it is there
void DxfWriter::write_leader_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_leader();
    const EntityProperties& p = e.props;
    write_entity_head("LEADER", p.handle, owner, p);
    write_code(100, "AcDbLeader");
    write_code(3, symbol_ref(SymbolKind::DimStyle, d.dim_style));
    if (!d.arrowhead) groups_.write_int(71, 0);
    if (d.path_type != 0) groups_.write_int(72, d.path_type);
    groups_.write_int(73, d.annotation_type);
    groups_.write_int(74, d.hookline_direction);
    groups_.write_int(75, d.hookline ? 1 : 0);
    groups_.write_double(40, d.text_height);
    groups_.write_double(41, d.text_width);
    groups_.write_int(76, static_cast<int64_t>(d.vertices.size()));
    for (const auto& v : d.vertices) write_point(10, v);
    if (d.byblock_color != 0) groups_.write_int(77, d.byblock_color);
    if (!d.annotation_handle.empty() && (!filter_live_ || is_live(d.annotation_handle))) {
        write_code(340, d.annotation_handle);
    }
    write_extrusion(p);
    if (!is_x_axis(d.horizontal_direction)) write_point(211, d.horizontal_direction);
    if (version_ == DxfVersion::R13) {
        // R13's reference has one offset, 212, "direction of annotation offset from last leader
        // vertex"; the block's offset (R14's 212) has no group (validated before: B93)
        if (!is_zero(d.annotation_offset)) write_point(212, d.annotation_offset);
        return;
    }
    if (!is_zero(d.block_offset)) write_point(212, d.block_offset);
    if (!is_zero(d.annotation_offset)) write_point(213, d.annotation_offset);
}

// The style by its handle (340): the one set, or the MLINESTYLE of that name — it must be
// in the drawing. The normal (210) always, as AutoCAD writes it.
void DxfWriter::write_mline_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_mline();
    const EntityProperties& p = e.props;
    std::string style = d.style_handle;
    if (style.empty() || (filter_live_ && !is_live(style))) {
        auto it = mline_style_handles_.find(upper(d.style));
        if (it != mline_style_handles_.end()) style = it->second;
        else if (error_.empty()) error_ = "MLINE style \"" + d.style + "\" is not in ACAD_MLINESTYLE";
    }
    write_entity_head("MLINE", p.handle, owner, p);
    write_code(100, "AcDbMline");
    // R13 and R14: the name the style has (one name is not written in two forms)
    auto style_name = mline_style_names_.find(upper(style));
    write_code(2, version_ < DxfVersion::R2000 && style_name != mline_style_names_.end() ? style_name->second : d.style);
    write_code(340, style);
    groups_.write_double(40, d.scale);
    groups_.write_int(70, d.justification);
    groups_.write_int(71, d.flags);
    groups_.write_int(72, static_cast<int64_t>(d.vertices.size()));
    groups_.write_int(73, d.style_elements);
    write_point(10, d.start_point);
    write_point(210, p.extrusion);
    for (const auto& v : d.vertices) {
        write_point(11, v.position);
        groups_.set_number_style(NumberStyle::AutoCadExponent);
        write_point(12, v.direction);
        write_point(13, v.miter);
        groups_.set_number_style(NumberStyle::AutoCad);
        for (const auto& el : v.elements) {
            groups_.write_int(74, static_cast<int64_t>(el.params.size()));
            for (double x : el.params) groups_.write_double(41, x);
            groups_.write_int(75, static_cast<int64_t>(el.fill_params.size()));
            for (double x : el.fill_params) groups_.write_double(42, x);
        }
    }
}

void DxfWriter::write_image_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_image();
    const EntityProperties& p = e.props;
    // Its outline as proxy graphics, as AutoCAD writes every IMAGE in 2000 … 2010 (ACadSharp's
    // samples: a closed polyline of the clip boundary)
    // (R13 and R14 have no proxy graphics on an IMAGE: the R14 reference's IMAGE has none, and
    // `92` is not among R14's entity groups)
    std::vector<uint8_t> proxy;
    if (proxy_graphics_ && version_ >= DxfVersion::R2000) {
        ProxyGraphics g;
        g.polyline(image_outline(d));
        proxy = g.bytes();
    }
    // WIPEOUT: the same groups under AcDbWipeout, without an IMAGEDEF or reactor (340 0, 360 0)
    bool wipeout = e.type == EntityType::Wipeout;
    write_entity_head(wipeout ? "WIPEOUT" : "IMAGE", p.handle, owner, p, &proxy);
    write_code(100, wipeout ? "AcDbWipeout" : "AcDbRasterImage");
    groups_.write_int(90, d.class_version);
    write_point(10, d.insertion_point);
    write_point(11, d.u_vector);
    write_point(12, d.v_vector);
    groups_.write_double(13, d.size.x);
    groups_.write_double(23, d.size.y);
    write_code(340, d.image_def_handle.empty() ? std::string("0") : d.image_def_handle);
    groups_.write_int(70, d.display_flags);
    groups_.write_int(280, d.clipping ? 1 : 0);
    groups_.write_int(281, d.brightness);
    groups_.write_int(282, d.contrast);
    groups_.write_int(283, d.fade);
    // 2010: the clip mode after the fade (ACadSharp's sample_AC1024, B44)
    if (version_ >= DxfVersion::R2010) groups_.write_int(290, d.clip_inside ? 1 : 0);
    write_code(360, d.reactor_handle.empty() ? std::string("0") : d.reactor_handle);
    groups_.write_int(71, d.clip_boundary_type);
    // R13 and R14: a polygonal boundary is written closed, its first vertex again at the end, as
    // AutoCAD writes it; AutoCAD's core does not read an open one ("Xdata wasn't read", 7.24)
    std::vector<Point2> clip = d.clip_boundary;
    if (version_ < DxfVersion::R2000 && d.clip_boundary_type == 2 && clip.size() >= 3 &&
        (clip.front().x != clip.back().x || clip.front().y != clip.back().y)) {
        clip.push_back(clip.front());
    }
    groups_.write_int(91, static_cast<int64_t>(clip.size()));
    for (const auto& c : clip) {
        groups_.write_double(14, c.x);
        groups_.write_double(24, c.y);
    }
}

// The normal (210) for a planar spline; weights (41) when it is rational; the fit
// tolerance (44) and tangents (12, 13) with fit points — as AutoCAD writes it
void DxfWriter::write_spline_r2000(const Entity& e, const std::string& owner) {
    const EntityProperties& p = e.props;
    write_entity_head("SPLINE", p.handle, owner, p);
    write_spline_body(e.as_spline(), p);
}

// AcDbSpline: SPLINE's and HELIX's
void DxfWriter::write_spline_body(const SplineData& d, const EntityProperties& p) {
    write_code(100, "AcDbSpline");
    if (d.flags & 8) write_point(210, p.extrusion);
    else write_extrusion(p);
    groups_.write_int(70, d.flags);
    groups_.write_int(71, d.degree);
    groups_.write_int(72, static_cast<int64_t>(d.knots.size()));
    groups_.write_int(73, static_cast<int64_t>(d.control_points.size()));
    groups_.write_int(74, static_cast<int64_t>(d.fit_points.size()));
    groups_.write_double(42, d.knot_tolerance);
    groups_.write_double(43, d.control_tolerance);
    if (!d.fit_points.empty()) groups_.write_double(44, d.fit_tolerance);
    if (!is_zero(d.start_tangent)) write_point(12, d.start_tangent);
    if (!is_zero(d.end_tangent)) write_point(13, d.end_tangent);
    for (double k : d.knots) groups_.write_double(40, k);
    // A rational spline's weight after each control point (AutoCAD 2018's, Fusion 360's and
    // ODA's files; the references list 41 after the knots)
    for (size_t i = 0; i < d.control_points.size(); ++i) {
        write_point(10, d.control_points[i]);
        if (d.is_rational()) groups_.write_double(41, i < d.weights.size() ? d.weights[i] : 1.0);
    }
    for (const auto& f : d.fit_points) write_point(11, f);
}

// Paths, then the pattern (not for solid fill) and the seed points. A source boundary
// that is gone is left out of its path; when none is left, the hatch is no longer
// associative, as AutoCAD does when its boundary is erased (B19).
// R14 writes the same groups (R14 DXF Reference, HATCH, boundary path data, pattern data; the five
// HATCHes of AutoCAD 2000's R14 file BL15-1): what is newer is gated on the version below — the
// gradient (450 – 470) from 2004, a spline edge's fit points from 2010 — and a drawing that has
// them is refused as R13/R14 before this is reached (drawing.cpp, plan phase 7, B94).
void DxfWriter::write_hatch_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_hatch();
    const EntityProperties& p = e.props;
    auto keep = [&](const std::string& h) {
        if (!template_mode() && !entity_handles_.count(upper(h))) return false;
        return !filter_live_ || is_live(h);
    };
    bool had_sources = false;
    bool live_sources = false;
    for (const auto& path : d.paths) {
        for (const auto& h : path.source_handles) {
            had_sources = true;
            live_sources = live_sources || keep(h);
        }
    }
    auto xy = [&](int code, const Point2& v) {
        groups_.write_double(code, v.x);
        groups_.write_double(code + 10, v.y);
    };

    write_entity_head("HATCH", p.handle, owner, p);
    write_code(100, "AcDbHatch");
    write_point(10, {0.0, 0.0, d.elevation});
    write_point(210, p.extrusion);         // always, as AutoCAD writes it
    write_code(2, d.pattern_name);
    groups_.write_int(70, d.solid ? 1 : 0);
    groups_.write_int(71, d.associative && !(had_sources && !live_sources) ? 1 : 0);
    groups_.write_int(91, static_cast<int64_t>(d.paths.size()));
    for (const auto& path : d.paths) {
        groups_.write_int(92, path.flags);
        if (path.is_polyline()) {
            bool bulges = std::any_of(path.vertices.begin(), path.vertices.end(),
                                      [](const HatchVertex& v) { return v.bulge != 0.0; });
            groups_.write_int(72, bulges ? 1 : 0);
            groups_.write_int(73, path.closed ? 1 : 0);
            groups_.write_int(93, static_cast<int64_t>(path.vertices.size()));
            for (const auto& v : path.vertices) {
                xy(10, v.position);
                if (bulges) groups_.write_double(42, v.bulge);
            }
        } else {
            groups_.write_int(93, static_cast<int64_t>(path.edges.size()));
            for (const auto& edge : path.edges) {
                groups_.write_int(72, static_cast<int>(edge.type));
                switch (edge.type) {
                    case HatchEdgeType::Line:
                        xy(10, edge.start);
                        xy(11, edge.end);
                        break;
                    case HatchEdgeType::Arc:
                    case HatchEdgeType::Ellipse:
                        xy(10, edge.center);
                        if (edge.type == HatchEdgeType::Ellipse) xy(11, edge.major_axis);
                        groups_.write_double(40, edge.type == HatchEdgeType::Arc ? edge.radius : edge.ratio);
                        groups_.write_double(50, edge.start_angle);
                        groups_.write_double(51, edge.end_angle);
                        groups_.write_int(73, edge.counterclockwise ? 1 : 0);
                        break;
                    case HatchEdgeType::Spline:
                        // Fit points and tangents are 2010+ (not in DXF 2000)
                        groups_.write_int(94, edge.degree);
                        groups_.write_int(73, edge.rational ? 1 : 0);
                        groups_.write_int(74, edge.periodic ? 1 : 0);
                        groups_.write_int(95, static_cast<int64_t>(edge.knots.size()));
                        groups_.write_int(96, static_cast<int64_t>(edge.control_points.size()));
                        for (double k : edge.knots) groups_.write_double(40, k);
                        for (size_t i = 0; i < edge.control_points.size(); ++i) {
                            xy(10, edge.control_points[i]);
                            if (edge.rational) {
                                groups_.write_double(42, i < edge.weights.size() ? edge.weights[i] : 1.0);
                            }
                        }
                        // 2010: the fit points (97, 11), and with them the tangents (12, 13) —
                        // as AutoCAD 2027's DXFIN reads a 2010 file: 97 required, the tangents
                        // only after fit points (plan phase 4, B44)
                        if (version_ >= DxfVersion::R2010) {
                            groups_.write_int(97, static_cast<int64_t>(edge.fit_points.size()));
                            if (!edge.fit_points.empty()) {
                                for (const auto& f : edge.fit_points) xy(11, f);
                                xy(12, edge.start_tangent);
                                xy(13, edge.end_tangent);
                            }
                        }
                        break;
                }
            }
        }
        std::vector<std::string> sources;
        for (const auto& h : path.source_handles) if (keep(h)) sources.push_back(h);
        groups_.write_int(97, static_cast<int64_t>(sources.size()));
        for (const auto& h : sources) write_code(330, h);
    }
    groups_.write_int(75, d.style);
    groups_.write_int(76, d.pattern_type);
    if (!d.solid) {
        groups_.write_double(52, d.pattern_angle);
        groups_.write_double(41, d.pattern_scale);
        groups_.write_int(77, d.pattern_double ? 1 : 0);
        groups_.write_int(78, static_cast<int64_t>(d.pattern_lines.size()));
        for (const auto& line : d.pattern_lines) {
            groups_.write_double(53, line.angle);
            groups_.write_double(43, line.base_point.x);
            groups_.write_double(44, line.base_point.y);
            groups_.write_double(45, line.offset.x);
            groups_.write_double(46, line.offset.y);
            groups_.write_int(79, static_cast<int64_t>(line.dashes.size()));
            for (double dash : line.dashes) groups_.write_double(49, dash);
        }
    }
    // R13 and R14: 47 is there exactly when a boundary path is derived (92 bit 4); AutoCAD 2025 reads 47
    // after 76 only then ("expected group code 47" without it, "expected group code 98" with it on a hatch
    // with no derived path; release 1.8.1, tried against the core). AutoCAD's own R14 files agree: BL15-1
    // (92 7, 47), BenF18/FYP/bne (92 3, no 47). Format profile D13.16
    bool derived = false;
    for (const auto& path : d.paths) derived = derived || (path.flags & 4) != 0;
    if (version_ < DxfVersion::R2000 ? derived : d.pixel_size != 0.0) groups_.write_double(47, d.pixel_size);
    groups_.write_int(98, static_cast<int64_t>(d.seed_points.size()));
    for (const auto& s : d.seed_points) xy(10, s);

    // The gradient (2004+), in AutoCAD's order; each color with its ACI and RGB (B21)
    if (d.gradient && version_ >= DxfVersion::R2004) {
        const HatchGradient& gr = *d.gradient;
        if (gr.type == 1 && gr.colors.size() != 2 && error_.empty()) {
            error_ = "A gradient fill has two colors (" + std::to_string(gr.colors.size()) + " given)";
        }
        groups_.write_int(450, gr.type);
        groups_.write_int(451, gr.reserved);
        groups_.write_double(460, gr.angle);
        groups_.write_double(461, gr.shift);
        groups_.write_int(452, gr.single_color ? 1 : 0);
        groups_.write_double(462, gr.tint);
        groups_.write_int(453, static_cast<int64_t>(gr.colors.size()));
        for (const auto& c : gr.colors) {
            groups_.write_double(463, c.value);
            groups_.write_int(63, c.color.aci);
            int rgb = c.color.has_rgb() ? c.color.rgb : aci_rgb(c.color.aci);
            if (rgb >= 0) groups_.write_int(421, rgb);
        }
        write_code(470, gr.name);
    }
}

// One width for all vertices (43) when they all have the same, else each vertex's 40/41;
// a bulge only where there is one — as AutoCAD writes it
void DxfWriter::write_lwpolyline_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_lwpolyline();
    const EntityProperties& p = e.props;
    bool constant = true;
    for (const auto& v : d.vertices) {
        constant = constant && v.start_width == d.vertices.front().start_width &&
                   v.end_width == d.vertices.front().start_width;
    }
    write_entity_head("LWPOLYLINE", p.handle, owner, p);
    write_code(100, "AcDbPolyline");
    groups_.write_int(90, static_cast<int64_t>(d.vertices.size()));
    groups_.write_int(70, d.flags);
    if (constant) groups_.write_double(43, d.vertices.empty() ? 0.0 : d.vertices.front().start_width);
    if (d.elevation != 0.0) groups_.write_double(38, d.elevation);
    write_thickness(p);
    for (const auto& v : d.vertices) {
        groups_.write_double(10, v.position.x);
        groups_.write_double(20, v.position.y);
        // 2010: the vertex's identifier after its point, on the vertices that have one (Autodesk's
        // reference; AutoCAD's files: the polyline's flags are 1, no flag 1024)
        if (version_ >= DxfVersion::R2010 && v.id) groups_.write_int(91, *v.id);
        if (!constant) {
            groups_.write_double(40, v.start_width);
            groups_.write_double(41, v.end_width);
        }
        if (v.bulge != 0.0) groups_.write_double(42, v.bulge);
    }
    write_extrusion(p);
}

void DxfWriter::write_text_body(const Point3& position, double height, const std::string& text,
                                double rotation, double width_factor, double oblique,
                                const std::string& style, int generation_flags, int h_justify,
                                int v_justify, const Point3& alignment,
                                const EntityProperties& props) {
    write_code(100, "AcDbText");
    write_thickness(props);
    write_point(10, position);
    groups_.write_double(40, height);
    write_code(1, text);
    if (rotation != 0.0) groups_.write_double(50, rotation);
    if (width_factor != 1.0) groups_.write_double(41, width_factor);
    if (oblique != 0.0) groups_.write_double(51, oblique);
    // (R13 and R14: the standard style in any case is left out, as AutoCAD's own TEXT records have it)
    if (version_ < DxfVersion::R2000 ? !names_equal(style, "Standard") : style != "Standard") {
        write_code(7, symbol_ref(SymbolKind::Style, style));
    }
    if (generation_flags != 0) groups_.write_int(71, generation_flags);
    if (h_justify != 0) groups_.write_int(72, h_justify);
    if (h_justify != 0 || v_justify != 0) write_point(11, alignment);
    write_extrusion(props);
}

void DxfWriter::write_text_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_text();
    write_entity_head("TEXT", e.props.handle, owner, e.props);
    write_text_body(d.position, d.height, d.content, d.rotation, d.width_factor,
                    d.oblique_angle, d.style, d.generation_flags, d.h_justify, d.v_justify,
                    d.alignment_point, e.props);
    write_code(100, "AcDbText");
    if (d.v_justify != 0) groups_.write_int(73, d.v_justify);
}

// A multiline attribute in 2018 (B75; ACadSharp's sample_AC1032, 79D and 796): after the lock flag
// 71 (2 attribute, 4 definition), 72 (the number of secondary attributes), their 340, 11, and the
// embedded object (101) with the text's AcDbMText groups — no entity of its own
void DxfWriter::write_multiline_attribute(const MultilineAttribute& ml) {
    groups_.write_int(71, ml.type);
    groups_.write_int(72, static_cast<int64_t>(ml.secondary.size()));
    for (const auto& h : ml.secondary) write_code(340, h);
    if (ml.has_alignment_point) write_point(11, ml.alignment_point);
    if (!ml.has_text) return;
    write_code(101, "Embedded Object");
    write_mtext_body(ml.text, nullptr);
    if (ml.text.columns) write_mtext_embedded(ml.text);
}

// The groups of a multiline attribute's own TEXT as AutoCAD writes them for its text (B75, B76)
template <typename Data>
static void apply_mirror(Data& d, const AttributeMirror& m) {
    d.start_point = m.start_point;
    d.alignment_point = m.alignment_point;
    d.h_justify = m.h_justify;
    d.v_justify = m.v_justify;
    d.height = m.height;
    d.rotation = m.rotation;
    d.style = m.style;
    d.tag = m.tag;
}

TextLayout& DxfWriter::attribute_layout() {
    std::unique_ptr<TextLayout>& cache = filter_live_ ? mtext_layout_ : original_mtext_layout_;
    const Drawing* source = filter_live_ || !original_ ? drawing_ : original_;
    if (!cache) cache = std::make_unique<TextLayout>(*source);
    return *cache;
}

void DxfWriter::write_attrib_r2000(const InsertAttribute& a, const std::string& owner, bool by_runs) {
    AttribData d = a.data;
    if (d.multiline && d.multiline->has_text && drawing_) {
        AttributeMirror m = attribute_mirror(*d.multiline, d.tag, false, version_, a.props.extrusion, attribute_layout(),
                                           d.width_factor, d.oblique_angle, d.generation_flags, by_runs);
        apply_mirror(d, m);
        // 2018 keeps its own 1 (AutoCAD 2025: the text, ACadSharp's sample_AC1032: empty); before it the mirror's
        if (version_ < DxfVersion::R2018) d.value = m.value;
    }
    write_entity_head("ATTRIB", a.props.handle, owner, a.props);
    write_text_body(d.start_point, d.height, d.value, d.rotation, d.width_factor,
                    d.oblique_angle, d.style, d.generation_flags, d.h_justify, d.v_justify,
                    d.alignment_point, a.props);
    write_code(100, "AcDbAttribute");
    if (version_ >= DxfVersion::R2010) groups_.write_int(280, d.version);   // B44
    write_code(2, d.tag);
    groups_.write_int(70, d.flags);
    if (d.field_length != 0) groups_.write_int(73, d.field_length);
    if (d.v_justify != 0) groups_.write_int(74, d.v_justify);
    if (version_ >= DxfVersion::R2007) groups_.write_int(280, d.lock_position ? 1 : 0);
    if (version_ >= DxfVersion::R2018 && d.multiline) write_multiline_attribute(*d.multiline);
}

void DxfWriter::write_attdef_r2000(const Entity& e, const std::string& owner) {
    AttdefData d = e.as_attdef();
    if (d.multiline && d.multiline->has_text && drawing_) {
        AttributeMirror m = attribute_mirror(*d.multiline, d.tag, true, version_, e.props.extrusion, attribute_layout(),
                                           d.width_factor, d.oblique_angle, d.generation_flags);
        apply_mirror(d, m);
        if (version_ < DxfVersion::R2018) d.default_value = m.value;     // 2018: its own 1, as for ATTRIB
    }
    write_entity_head("ATTDEF", e.props.handle, owner, e.props);
    write_text_body(d.start_point, d.height, d.default_value, d.rotation, d.width_factor,
                    d.oblique_angle, d.style, d.generation_flags, d.h_justify, d.v_justify,
                    d.alignment_point, e.props);
    write_code(100, "AcDbAttributeDefinition");
    if (version_ >= DxfVersion::R2010) groups_.write_int(280, d.version);   // B44
    write_code(3, d.prompt);
    write_code(2, d.tag);
    groups_.write_int(70, d.flags);
    if (d.field_length != 0) groups_.write_int(73, d.field_length);
    if (d.v_justify != 0) groups_.write_int(74, d.v_justify);
    if (version_ >= DxfVersion::R2007) groups_.write_int(280, d.lock_position ? 1 : 0);
    if (version_ >= DxfVersion::R2018 && d.multiline) write_multiline_attribute(*d.multiline);
}

// POLYLINE, its VERTEX records and SEQEND. The subclasses follow the kind of polyline:
// 2D, 3D, polygon mesh or polyface mesh (whose face records have no AcDbVertex).
void DxfWriter::write_polyline_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_polyline();
    const EntityProperties& p = e.props;
    const char* subclass = "AcDb2dPolyline";
    const char* vertex_subclass = "AcDb2dVertex";
    if (d.is_polyface_mesh()) {
        subclass = "AcDbPolyFaceMesh";
        vertex_subclass = "AcDbPolyFaceMeshVertex";
    } else if (d.is_polygon_mesh()) {
        subclass = "AcDbPolygonMesh";
        vertex_subclass = "AcDbPolygonMeshVertex";
    } else if (d.is_3d_polyline()) {
        subclass = "AcDb3dPolyline";
        vertex_subclass = "AcDb3dPolylineVertex";
    }
    bool two_d = !d.is_polyface_mesh() && !d.is_polygon_mesh() && !d.is_3d_polyline();

    write_entity_head("POLYLINE", p.handle, owner, p);
    write_code(100, subclass);
    groups_.write_int(66, 1);
    write_point(10, {d.header_x, d.header_y, d.elevation});     // 30: the elevation (the references)
    write_thickness(p);
    // R13 and R14 leave the flags out at 0, as AutoCAD's own R13 file and R14 files have them (2000 writes 70 always)
    if (d.flags != 0 || version_ >= DxfVersion::R2000) groups_.write_int(70, d.flags);
    if (d.default_start_width != 0.0) groups_.write_double(40, d.default_start_width);
    if (d.default_end_width != 0.0) groups_.write_double(41, d.default_end_width);
    if (d.m_count != 0) groups_.write_int(71, d.m_count);
    if (d.n_count != 0) groups_.write_int(72, d.n_count);
    if (d.smooth_m != 0) groups_.write_int(73, d.smooth_m);
    if (d.smooth_n != 0) groups_.write_int(74, d.smooth_n);
    if (d.smooth_type != 0) groups_.write_int(75, d.smooth_type);
    write_extrusion(p);
    write_xdata(p.xdata);

    EntityProperties child;
    child.layer = p.layer;
    child.paper_space = p.paper_space;
    for (const auto& v : d.vertices) {
        child.reactors = v.reactors;
        child.layer = v.layer.empty() ? p.layer : v.layer;
        child.linetype = v.linetype;
        child.color = v.color;
        write_entity_head("VERTEX", v.handle, p.handle, child);
        bool face_record = d.is_polyface_mesh() && (v.flags & 128) && !(v.flags & 64);
        if (face_record) {
            write_code(100, "AcDbFaceRecord");
            write_point(10, {0.0, 0.0, 0.0});
            groups_.write_int(70, v.flags);
            for (int i = 0; i < 4; ++i) {
                if (v.face[i] != 0) groups_.write_int(71 + i, v.face[i]);
            }
            write_xdata(v.xdata);
            continue;
        }
        write_code(100, "AcDbVertex");
        write_code(100, vertex_subclass);
        write_point(10, v.position);
        if (two_d) {
            if (v.start_width != 0.0) groups_.write_double(40, v.start_width);
            if (v.end_width != 0.0) groups_.write_double(41, v.end_width);
            if (v.bulge != 0.0) groups_.write_double(42, v.bulge);
        }
        if (v.flags != 0 || version_ >= DxfVersion::R2000) groups_.write_int(70, v.flags);
        // The tangent where bit 2 says it is defined; 0 may be left out (the references)
        if (two_d && ((v.flags & 2) || v.tangent != 0.0)) groups_.write_double(50, v.tangent);
        if (version_ >= DxfVersion::R2010 && v.id) groups_.write_int(91, *v.id);
        write_xdata(v.xdata);
    }
    child.reactors.clear();
    child.layer = d.seqend_layer.empty() ? p.layer : d.seqend_layer;
    child.linetype = d.seqend_linetype;
    child.color = d.seqend_color;
    write_entity_head("SEQEND", d.seqend_handle, p.handle, child);
}

void DxfWriter::write_insert_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_insert();
    const EntityProperties& p = e.props;
    write_entity_head("INSERT", p.handle, owner, p);
    // A MINSERT (more than one row or column) is AcDbMInsertBlock in AutoCAD's 2000 … 2018 files
    // (blocks_attributes); with AcDbBlockReference AutoCAD draws one copy. R13/R14: no file shows it
    const bool minsert = version_ >= DxfVersion::R2000 && (d.column_count > 1 || d.row_count > 1);
    write_code(100, minsert ? "AcDbMInsertBlock" : "AcDbBlockReference");
    if (!d.attributes.empty()) groups_.write_int(66, 1);
    write_code(2, symbol_ref(SymbolKind::Block, d.block_name));
    write_point(10, d.insertion_point);
    if (d.x_scale != 1.0) groups_.write_double(41, d.x_scale);
    if (d.y_scale != 1.0) groups_.write_double(42, d.y_scale);
    if (d.z_scale != 1.0) groups_.write_double(43, d.z_scale);
    if (d.rotation != 0.0) groups_.write_double(50, d.rotation);
    if (d.column_count > 1) groups_.write_int(70, d.column_count);
    if (d.row_count > 1) groups_.write_int(71, d.row_count);
    if (d.column_spacing != 0.0) groups_.write_double(44, d.column_spacing);
    if (d.row_spacing != 0.0) groups_.write_double(45, d.row_spacing);
    write_extrusion(p);
    write_xdata(p.xdata);

    if (d.attributes.empty()) return;
    for (const auto& a : d.attributes) {
        InsertAttribute attr = a;
        if (attr.props.layer.empty()) attr.props.layer = p.layer;
        // R13 and R14: the attributes of a paper space insert are in paper space too (R13:Common Entity Group Codes)
        if (version_ < DxfVersion::R2000) attr.props.paper_space = p.paper_space;
        write_attrib_r2000(attr, p.handle, attributes_by_runs(d));
        write_xdata(attr.props.xdata);
        write_attribute_lock(attr.data.lock_position);
    }
    EntityProperties seqend;
    seqend.layer = p.layer;
    seqend.paper_space = p.paper_space;
    write_entity_head("SEQEND", d.seqend_handle, p.handle, seqend);
}

// The groups of AcDbDimension from its marker to the style (3), for a DIMENSION, an ARC_DIMENSION and a LARGE_RADIAL_DIMENSION
// (step 9.17: the three share them)
void DxfWriter::write_dimension_common(const Entity& e, const DimensionData& d) {
    auto generated = dim_block_names_.find(&e);
    bool own_block = generated != dim_block_names_.end();
    const bool older = version_ < DxfVersion::R2000;
    write_code(100, "AcDbDimension");
    // 2010: the version first (B44)
    if (version_ >= DxfVersion::R2010) groups_.write_int(280, d.version);
    // A dimensional constraint has no block, and AutoCAD writes no 2 for it
    if (own_block || !d.block_name.empty()) write_code(2, own_block ? generated->second : symbol_ref(SymbolKind::Block, d.block_name));
    // A block built here: 10, 11 and 42 as it put them
    auto built = built_dimensions_.find(&e);
    bool computed = built != built_dimensions_.end();
    write_point(10, computed ? built->second.definition_point : d.definition_point);
    write_point(11, computed ? built->second.text_position : d.text_midpoint);
    // 12: in this subclass in R13 and R14 (all types) and, from 2000, for an ARC_DIMENSION and a LARGE_RADIAL_DIMENSION (AutoCAD's
    // files: `2 10 11 12 70 71 42`; a DIMENSION has it in AcDbAlignedDimension) — 9.S, Codex 10
    const bool special = e.type == EntityType::ArcDimension || e.type == EntityType::LargeRadialDimension;
    if ((older || special) && !is_zero(d.block_translation)) write_point(12, d.block_translation);
    // Bit 32: the block is referenced by this dimension only — set for the blocks
    // written here, as AutoCAD does for its own (2000 and later); R13 and R14: always
    int type_bits = static_cast<int>(d.dim_type) & 0x0F;
    groups_.write_int(70, d.flags | type_bits | (own_block || older ? 32 : 0));
    // 1 after 70, before 71: AutoCAD's files 2000 … 2018 (`70 1 71 42`, the constraints and the dimensions with a text of their
    // own) and R14's (`70 1 3`)
    if (!d.text_override.empty()) write_code(1, d.text_override);
    // 71 (the text's attachment), 72 and 41 (line spacing, a special dimension's when read): as read (9.S, Codex 10); a new
    // dimension has 71 5
    if (!older) {
        groups_.write_int(71, d.attachment_point);
        const std::optional<int16_t>* style = nullptr;
        const std::optional<double>* factor = nullptr;
        if (e.type == EntityType::ArcDimension) {
            style = &e.as_arc_dimension().line_spacing_style;
            factor = &e.as_arc_dimension().line_spacing_factor;
        } else if (e.type == EntityType::LargeRadialDimension) {
            style = &e.as_large_radial_dimension().line_spacing_style;
            factor = &e.as_large_radial_dimension().line_spacing_factor;
        }
        if (style && *style) groups_.write_int(72, **style);
        if (factor && *factor) groups_.write_double(41, **factor);
    }
    // 42: the measurement scaled by DIMLFAC, as AutoCAD writes it (Oxcart's paper-space details
    // at 5:1: 42 0.5 for 2.5 between the points, DIMLFAC 0.2); an angle as it is
    if (!older) {
        double measured = computed ? built->second.measurement : internal::dimension_entity_measurement(e, d);
        groups_.write_double(42, measured * dimension_scale(e));
    }
    // 2007: 73, 74, 75 — not in the reference; 0 in every dimension of AutoCAD's 2007-2018
    // files (B33), as read otherwise (9.S)
    if (version_ >= DxfVersion::R2007) {
        groups_.write_int(73, d.value_73);
        groups_.write_int(74, d.value_74);
        groups_.write_int(75, d.value_75);
    }
    if (d.text_rotation != 0.0) groups_.write_double(53, d.text_rotation);
    if (d.horizontal_dir != 0.0) groups_.write_double(51, d.horizontal_dir);
    write_extrusion(e.props);
    write_code(3, symbol_ref(SymbolKind::DimStyle, d.dim_style));

}

// AcDbDimension, then the subclass of the dimension type (exact chains: plan 1.2)
void DxfWriter::write_dimension_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_dimension();
    // R13 and R14 (plan phase 7, step 7.16, B93; format profile 7.3): AutoCAD's own R14 records
    // (BL15-1: 2 10 11 [12] 70 [1] 3, then the type's subclasses) and Autodesk's R13/R14 references —
    // bit 32 of 70 always set ("always set in R13 and later releases"), the clone point (12) in the
    // common part (as R12 has it), and none of 2000's 71 (text attachment) and 42 (the measurement)
    const bool older = version_ < DxfVersion::R2000;
    write_entity_head("DIMENSION", e.props.handle, owner, e.props);
    write_dimension_common(e, d);
    auto built = built_dimensions_.find(&e);
    bool computed = built != built_dimensions_.end();

    switch (d.dim_type) {
        case DimensionType::Rotated:
        case DimensionType::Aligned:
            write_code(100, "AcDbAlignedDimension");
            // 12, the clones' insertion point, in this subclass (the references); none at 0, as
            // AutoCAD writes it. R13 and R14 have it in the common part (above)
            if (!older && !is_zero(d.block_translation)) write_point(12, d.block_translation);
            write_point(13, d.def_point2);
            write_point(14, d.def_point3);
            if (d.dim_type == DimensionType::Rotated && d.rotation != 0.0) {
                groups_.write_double(50, d.rotation);
            }
            if (d.oblique_angle != 0.0) groups_.write_double(52, d.oblique_angle);
            if (d.dim_type == DimensionType::Rotated) write_code(100, "AcDbRotatedDimension");
            break;
        case DimensionType::Angular:
            // R13 and R14: the one marker both references give the angular dimensions (format
            // profile D13.8, open question 6); 2000 and later name the 2-line one
            write_code(100, "AcDb2LineAngularDimension");   // AutoCAD's core reads type 2 only with this marker, also in R13/R14
            write_point(13, d.def_point2);
            write_point(14, d.def_point3);
            write_point(15, d.def_point4);
            write_point(16, computed ? built->second.arc_point : d.arc_point);
            break;
        case DimensionType::Diameter:
        case DimensionType::Radius:
            write_code(100, d.dim_type == DimensionType::Diameter ? "AcDbDiametricDimension"
                                                                   : "AcDbRadialDimension");
            write_point(15, d.def_point4);
            groups_.write_double(40, d.leader_length);
            break;
        case DimensionType::Angular3Pt:
            write_code(100, "AcDb3PointAngularDimension");
            write_point(13, d.def_point2);
            write_point(14, d.def_point3);
            write_point(15, d.def_point4);
            break;
        case DimensionType::Ordinate:
            write_code(100, "AcDbOrdinateDimension");
            write_point(13, d.def_point2);
            write_point(14, d.def_point3);
            break;
    }
}

bool is_mview(const XdataList& xd) {
    return xd.app_name == "ACAD" && !xd.items.empty() && xd.items[0].code == 1000 &&
           xd.items[0].value == "MVIEW";
}

// R12 keeps a viewport's view in ACAD/MVIEW XDATA (R12 DXF reference, table 24-25):
// 1000 MVIEW, 1002 {, 1070 version, target, direction, then the values below in order,
// 1002 { frozen layers (1003) 1002 }, 1002 }. The bits of the view mode and the on/off
// settings are the bits of the 2000 status flags (90).
bool view_from_mview(const XdataList& xd, ViewportData& d) {
    std::vector<double> reals;
    std::vector<int> ints;
    std::vector<Point3> points;
    std::vector<std::string> frozen;
    for (const auto& item : xd.items) {
        double v = std::strtod(item.value.c_str(), nullptr);
        switch (item.code) {
            case 1010: points.push_back({v, 0.0, 0.0}); break;
            case 1020: if (!points.empty()) points.back().y = v; break;
            case 1030: if (!points.empty()) points.back().z = v; break;
            case 1040: reals.push_back(v); break;
            case 1070: ints.push_back(static_cast<int>(v)); break;
            case 1003: frozen.push_back(item.value); break;
        }
    }
    // ints: version, view mode, circle zoom, fast zoom, UCSICON, snap, grid, snap style,
    // isopair, hidden in plot; reals: twist, height, center x/y, lens, front, back, snap
    // angle, snap base x/y, snap x/y, grid x/y
    if (points.size() < 2 || reals.size() < 14 || ints.size() < 10) return false;
    d.view_target = points[0];
    d.view_direction = points[1];
    d.twist_angle = reals[0];
    d.view_height = reals[1];
    d.view_center = {reals[2], reals[3]};
    d.lens_length = reals[4];
    d.front_clip = reals[5];
    d.back_clip = reals[6];
    d.snap_angle = reals[7];
    d.snap_base = {reals[8], reals[9]};
    d.snap_spacing = {reals[10], reals[11]};
    d.grid_spacing = {reals[12], reals[13]};
    d.circle_zoom = ints[2];
    int flags = 32768 | (ints[1] & 31);         // 32768: always set (DXF reference)
    if (ints[3]) flags |= 128;                  // fast zoom
    if (ints[4] & 1) flags |= 32;               // UCS icon on
    if (ints[4] & 2) flags |= 64;               // UCS icon at origin
    if (ints[5]) flags |= 256;                  // snap
    if (ints[6]) flags |= 512;                  // grid
    if (ints[7]) flags |= 1024;                 // isometric snap style
    if (ints[8] == 1) flags |= 4096;            // isopair top
    if (ints[8] == 2) flags |= 8192;            // isopair right
    if (ints[9]) flags |= 2048;                 // hidden in plot
    d.status_flags = flags;
    d.frozen_layers = frozen;
    return true;
}

// VIEWPORT with the 2000 fields; R12's MVIEW XDATA, if there, gives the view and is not
// written (it is R12 only)
void DxfWriter::write_viewport_r2000(const Entity& e, const std::string& owner) {
    ViewportData d = e.as_viewport();
    for (const auto& xd : e.props.xdata) {
        if (is_mview(xd)) view_from_mview(xd, d);
    }
    if (version_ < DxfVersion::R2000) {
        write_viewport_older(e, owner, d);
        return;
    }

    write_entity_head("VIEWPORT", e.props.handle, owner, e.props);
    // 2007: the visual style last in AcDbEntity where the file had it so (KioskStation's)
    const std::string& style = d.visual_style_handle.empty() && !template_mode() ? wireframe_style_
                                                                                 : d.visual_style_handle;
    if (version_ >= DxfVersion::R2007 && d.visual_style_in_entity) write_pointer(348, style);
    write_code(100, "AcDbViewport");
    write_point(10, d.center);
    groups_.write_double(40, d.width);
    groups_.write_double(41, d.height);
    groups_.write_int(68, d.status);
    groups_.write_int(69, d.id);
    groups_.write_double(12, d.view_center.x);
    groups_.write_double(22, d.view_center.y);
    groups_.write_double(13, d.snap_base.x);
    groups_.write_double(23, d.snap_base.y);
    groups_.write_double(14, d.snap_spacing.x);
    groups_.write_double(24, d.snap_spacing.y);
    groups_.write_double(15, d.grid_spacing.x);
    groups_.write_double(25, d.grid_spacing.y);
    write_point(16, d.view_direction);
    write_point(17, d.view_target);
    groups_.write_double(42, d.lens_length);
    groups_.write_double(43, d.front_clip);
    groups_.write_double(44, d.back_clip);
    double view_height = d.view_height > 0.0 ? d.view_height : d.height;
    groups_.write_double(45, view_height > 0.0 ? view_height : 1.0);
    groups_.write_double(50, d.snap_angle);
    groups_.write_double(51, d.twist_angle);
    groups_.write_int(72, d.circle_zoom);
    // Frozen layers that do not exist are left out: there is nothing to point to
    for (const auto& layer : d.frozen_layers) {
        auto it = layer_handles_.find(upper(layer));
        if (it != layer_handles_.end()) write_code(331, it->second);
    }
    groups_.write_int(90, d.status_flags);
    if (!d.clip_boundary_handle.empty()) write_code(340, d.clip_boundary_handle);
    write_code(1, d.plot_style_sheet);
    groups_.write_int(281, d.render_mode);
    groups_.write_int(71, d.ucs_per_viewport);
    groups_.write_int(74, d.ucs_icon);
    write_point(110, d.ucs_origin);
    write_point(111, d.ucs_x_axis);
    write_point(112, d.ucs_y_axis);
    write_ucs_refs(d.ucs, d.base_ucs);
    groups_.write_int(79, d.ucs_ortho_type);
    groups_.write_double(146, d.elevation);
    if (version_ >= DxfVersion::R2004) groups_.write_int(170, d.shade_plot_mode);
    // 2007, in AutoCAD's order (61, 348, 292, 282, 141, 142, 63/421); 332/333 where the
    // reference lists them (after 61) and 361 last — no AutoCAD file has them
    if (version_ >= DxfVersion::R2007) {
        groups_.write_int(61, d.grid_major);
        write_pointer(332, d.background_handle);
        write_pointer(333, d.shade_plot_handle);
        if (!d.visual_style_in_entity) write_pointer(348, style);
        groups_.write_int(292, d.default_lighting ? 1 : 0);
        groups_.write_int(282, d.default_lighting_type);
        groups_.write_double(141, d.brightness);
        groups_.write_double(142, d.contrast);
        write_ambient_color(d.ambient_color);
        write_pointer(361, d.sun_handle);
    }

    for (const auto& xd : e.props.xdata) {
        if (!is_mview(xd)) write_xdata({xd});
    }
}

// VIEWPORT in R13 and R14 (plan phase 7, step 7.16, B93): AcDbViewport with 10, 40, 41, 68 and 69
// only, and the view in the XDATA of the application ACAD, MVIEW (Autodesk's R13 reference, VIEWPORT
// and its extended data; AutoCAD's own R14 files have exactly this: 10 40 41 68 69, then 1001 ACAD
// 1000 MVIEW). The frozen layers are names (1003) there, not the handles (331) of 2000. What the
// MVIEW has no place for is refused before the drawing is written (viewport_older_error()); the
// view is the typed fields, or the MVIEW the entity was read with (`d`).
void DxfWriter::write_viewport_older(const Entity& e, const std::string& owner, const ViewportData& d) {
    write_entity_head("VIEWPORT", e.props.handle, owner, e.props);
    write_code(100, "AcDbViewport");
    write_point(10, d.center);
    groups_.write_double(40, d.width);
    groups_.write_double(41, d.height);
    groups_.write_int(68, d.status);
    groups_.write_int(69, d.id);

    // The flags 2000 keeps the view's on/off settings in are the bits of the view mode and the
    // settings of the XDATA (view_from_mview() reads them the other way)
    const int flags = d.status_flags;
    double view_height = d.view_height > 0.0 ? d.view_height : d.height;
    write_code(1001, "ACAD");
    write_code(1000, "MVIEW");
    write_code(1002, "{");
    groups_.write_int(1070, 16);                    // the extended data's version
    write_point(1010, d.view_target);
    write_point(1010, d.view_direction);
    groups_.write_double(1040, d.twist_angle);
    groups_.write_double(1040, view_height > 0.0 ? view_height : 1.0);
    groups_.write_double(1040, d.view_center.x);
    groups_.write_double(1040, d.view_center.y);
    groups_.write_double(1040, d.lens_length);
    groups_.write_double(1040, d.front_clip);
    groups_.write_double(1040, d.back_clip);
    groups_.write_int(1070, flags & 31);            // view mode
    groups_.write_int(1070, d.circle_zoom);
    groups_.write_int(1070, (flags & 128) ? 1 : 0); // fast zoom
    groups_.write_int(1070, ((flags & 32) ? 1 : 0) | ((flags & 64) ? 2 : 0));   // UCSICON
    groups_.write_int(1070, (flags & 256) ? 1 : 0); // snap
    groups_.write_int(1070, (flags & 512) ? 1 : 0); // grid
    groups_.write_int(1070, (flags & 1024) ? 1 : 0);                    // snap style
    groups_.write_int(1070, (flags & 4096) ? 1 : (flags & 8192) ? 2 : 0);   // snap ISOPAIR
    groups_.write_double(1040, d.snap_angle);
    groups_.write_double(1040, d.snap_base.x);
    groups_.write_double(1040, d.snap_base.y);
    groups_.write_double(1040, d.snap_spacing.x);
    groups_.write_double(1040, d.snap_spacing.y);
    groups_.write_double(1040, d.grid_spacing.x);
    groups_.write_double(1040, d.grid_spacing.y);
    groups_.write_int(1070, (flags & 2048) ? 1 : 0);                    // hidden in plot
    write_code(1002, "{");
    for (const auto& layer : d.frozen_layers) write_code(1003, symbol_ref(SymbolKind::Layer, layer));
    write_code(1002, "}");
    write_code(1002, "}");

    for (const auto& xd : e.props.xdata) {
        if (!is_mview(xd)) write_xdata({xd});
    }
}

// An unknown entity built in memory: the head is written here, then its groups as given
// (plan B13 — whoever builds them answers for them). Groups the head already has are
// not repeated.
void DxfWriter::write_unknown_r2000(const Entity& e, const std::string& owner) {
    const auto& d = e.as_unknown();
    write_entity_head(d.entity_type.c_str(), e.props.handle, owner, e.props);
    // The head (handle, owner) and AcDbEntity are written above from props; from the
    // entity's own subclass on, its groups as they are
    int markers = 0;
    for (const auto& [code, value] : d.group_codes) {
        if (code == 100) ++markers;
        bool common = code == 5 || code == 330 || code == 8 || code == 6 || code == 62 ||
                      code == 420 || code == 430 || code == 440 || code == 347 ||
                      code == 48 || code == 60 || code == 67 || code == 370 || code == 390 ||
                      (code == 100 && value == "AcDbEntity");
        if (common && markers <= 1) continue;
        write_code_typed(code, value);
    }
}

// ---------------------------------------------------------------------------
// OBJECTS
// ---------------------------------------------------------------------------

void DxfWriter::write_objects_r2000(const Drawing& dwg) {
    write_code(0, "SECTION");
    write_code(2, "OBJECTS");
    for (const auto& o : dwg.objects()) {
        if (dropped_objects_.count(handle_value(o.handle))) continue;   // its owner is gone
        const Object* orig = nullptr;
        if (template_mode()) {
            auto it = orig_objects_.find(handle_value(o.handle));
            if (it != orig_objects_.end()) orig = it->second;
        }
        emit({&o.raw}, false, [&] { write_object_r2000(o); },
             orig ? std::function<void()>([&] { write_object_r2000(*orig); }) : nullptr);
    }
    write_code(0, "ENDSEC");
}

static const char* object_type_name(const Object& o) {
    switch (o.type) {
        case ObjectType::Dictionary:            return "DICTIONARY";
        case ObjectType::DictionaryWithDefault: return "ACDBDICTIONARYWDFLT";
        case ObjectType::XRecord:               return "XRECORD";
        case ObjectType::Group:                 return "GROUP";
        case ObjectType::Layout:                return "LAYOUT";
        case ObjectType::PlotSettings:          return "PLOTSETTINGS";
        case ObjectType::DictionaryVar:         return "DICTIONARYVAR";
        case ObjectType::Placeholder:           return "ACDBPLACEHOLDER";
        case ObjectType::MlineStyle:            return "MLINESTYLE";
        case ObjectType::ImageDef:              return "IMAGEDEF";
        case ObjectType::ImageDefReactor:       return "IMAGEDEF_REACTOR";
        case ObjectType::RasterVariables:       return "RASTERVARIABLES";
        case ObjectType::WipeoutVariables:      return "WIPEOUTVARIABLES";
        case ObjectType::Scale:                 return "SCALE";
        case ObjectType::Sun:                   return "SUN";
        case ObjectType::DbColor:               return "DBCOLOR";
        case ObjectType::MLeaderStyle:          return "MLEADERSTYLE";
        case ObjectType::GeoData:               return "GEODATA";
        case ObjectType::DetailViewStyle:       return "ACDBDETAILVIEWSTYLE";
        case ObjectType::SectionViewStyle:      return "ACDBSECTIONVIEWSTYLE";
        case ObjectType::SectionManager:        return "SECTION_MANAGER";
        case ObjectType::SectionSettings:       return "SECTION_SETTINGS";
        case ObjectType::NavisworksModelDef:    return "NAVISWORKSMODELDEF";
        case ObjectType::PdfDefinition:         return "PDFDEFINITION";
        case ObjectType::DwfDefinition:         return "DWFDEFINITION";
        case ObjectType::DgnDefinition:         return "DGNDEFINITION";
        case ObjectType::RapidRtRenderSettings: return "RAPIDRTRENDERSETTINGS";
        case ObjectType::RapidRtRenderEnvironment: return "RAPIDRTRENDERENVIRONMENT";
        case ObjectType::SortentsTable:         return "SORTENTSTABLE";
        case ObjectType::SpatialFilter:         return "SPATIAL_FILTER";
        case ObjectType::LayerIndex:            return "LAYER_INDEX";
        case ObjectType::SpatialIndex:          return "SPATIAL_INDEX";
        case ObjectType::IdBuffer:              return "IDBUFFER";
        case ObjectType::LayerFilter:           return "LAYER_FILTER";
        case ObjectType::ProxyObject:           return "ACAD_PROXY_OBJECT";
        case ObjectType::Material:              return "MATERIAL";
        case ObjectType::VisualStyle:           return "VISUALSTYLE";
        case ObjectType::RenderSettings:        return "RENDERSETTINGS";
        case ObjectType::MentalRayRenderSettings: return "MENTALRAYRENDERSETTINGS";
        case ObjectType::RenderEnvironment:     return "RENDERENVIRONMENT";
        case ObjectType::RenderGlobal:          return "RENDERGLOBAL";
        case ObjectType::LightList:             return "LIGHTLIST";
        case ObjectType::SunStudy:              return "SUNSTUDY";
        case ObjectType::Unknown:             return o.as_unknown().object_type.c_str();
        default:                              break;    // the types phase 9 typed: written by their codecs
    }
    return "";
}

void DxfWriter::write_plot_settings_r2000(const PlotSettings& p) {
    write_code(100, "AcDbPlotSettings");
    write_code(1, p.page_setup_name);
    write_code(2, p.printer_name);
    write_code(4, p.paper_size);
    write_code(6, p.plot_view_name);
    groups_.write_double(40, p.margin_left);
    groups_.write_double(41, p.margin_bottom);
    groups_.write_double(42, p.margin_right);
    groups_.write_double(43, p.margin_top);
    groups_.write_double(44, p.paper_width);
    groups_.write_double(45, p.paper_height);
    groups_.write_double(46, p.plot_origin.x);
    groups_.write_double(47, p.plot_origin.y);
    groups_.write_double(48, p.window_min.x);
    groups_.write_double(49, p.window_min.y);
    groups_.write_double(140, p.window_max.x);
    groups_.write_double(141, p.window_max.y);
    groups_.write_double(142, p.scale_numerator);
    groups_.write_double(143, p.scale_denominator);
    groups_.write_int(70, p.flags);
    groups_.write_int(72, p.paper_units);
    groups_.write_int(73, p.rotation);
    groups_.write_int(74, p.plot_type);
    write_code(7, p.style_sheet);
    groups_.write_int(75, p.standard_scale_type);
    groups_.write_double(147, p.scale_factor);
    if (version_ >= DxfVersion::R2004) {
        groups_.write_int(76, p.shade_plot_mode);
        groups_.write_int(77, p.shade_plot_resolution);
        groups_.write_int(78, p.shade_plot_dpi);
    }
    groups_.write_double(148, p.image_origin.x);
    groups_.write_double(149, p.image_origin.y);
    // A pointer to a record that is gone is left out (B19) — not when the original is
    // written for comparison
    if (version_ >= DxfVersion::R2004 && !p.shade_plot_handle.empty() &&
        (!filter_live_ || is_live(p.shade_plot_handle))) {
        write_code(333, p.shade_plot_handle);
    }
}

// An object's head: type, handle, extension dictionary, reactors and owner (the same for every type, typed
// records of phase 9 too)
void DxfWriter::write_object_head(const char* dxf_name, const Object& o, const std::vector<std::string>* reactors_given) {
    // Pointers to records that are gone are left out (B19) — not when the original is
    // written for comparison, so the difference shows
    auto keep = [&](const std::string& h) { return !filter_live_ || is_live(h); };

    write_code(0, dxf_name);
    write_code(5, o.handle);
    auto write_xdictionary = [&]() {
        if (!o.xdictionary.empty() && keep(o.xdictionary)) {
            write_code(102, "{ACAD_XDICTIONARY");
            write_code(360, o.xdictionary);
            write_code(102, "}");
        }
    };
    // R13 and R14: the reactors first, then the extension dictionary, as their references have them
    // (as the entities and the table records, plan phase 7, format profile 7.1)
    const bool older = version_ < DxfVersion::R2000;
    if (!older) write_xdictionary();
    std::vector<std::string> reactors;
    const std::vector<std::string>& own = reactors_given ? *reactors_given : o.reactors;
    for (const auto& r : own) if (keep(r)) reactors.push_back(r);
    // The records that have this object as a target of a typed relation (phase 9: a table's style has the table as a
    // reactor), as an entity has them in write_reactors()
    {
        const Relations& relations = !filter_live_ && template_mode() ? orig_reactors_ : reactors_;
        auto related = relations.find(upper(o.handle));
        if (related != relations.end()) {
            for (const auto& r : related->second) {
                if (keep(r) && std::find(reactors.begin(), reactors.end(), r) == reactors.end()) reactors.push_back(r);
            }
        }
    }
    // R13 and R14 as AutoCAD wrote them (family N) have the owner only as a reactor: an object
    // whose owner is not among its reactors gets it as the last one, so a read file finds it
    // (an IMAGEDEF_REACTOR's owner is the image, which it names in its own subclass: no reactor group)
    if (older && !owner_groups_written() && o.type != ObjectType::ImageDefReactor && !o.owner.empty() &&
        o.owner != "0" && keep(o.owner) && std::find(reactors.begin(), reactors.end(), o.owner) == reactors.end()) {
        reactors.push_back(o.owner);
    }
    if (!reactors.empty()) {
        write_code(102, "{ACAD_REACTORS");
        for (const auto& r : reactors) write_code(330, r);
        write_code(102, "}");
    }
    if (older) write_xdictionary();
    if (owner_groups_written()) write_code(330, o.owner.empty() ? "0" : o.owner);
}

void DxfWriter::write_object_r2000(const Object& o) {
    // The typed records of phase 9: the family's codec writes the whole record, head and XDATA included
    if (const ObjectCodec* codec = TypedRegistry::instance().find_object(o.type); codec && codec->write) {
        RecordWriter writer(*this);
        codec->write(writer, o);
        return;
    }

    // Pointers to records that are gone are left out (B19) — not when the original is
    // written for comparison, so the difference shows
    auto keep = [&](const std::string& h) { return !filter_live_ || is_live(h); };

    // An MLEADERSTYLE's reactors are its MULTILEADERs (B36): those that use it now
    std::vector<std::string> style_reactors;
    if (o.type == ObjectType::MLeaderStyle) style_reactors = mleader_style_reactors(o);
    write_object_head(object_type_name(o), o, o.type == ObjectType::MLeaderStyle ? &style_reactors : nullptr);

    auto write_dictionary = [&](const DictionaryData& d) {
        write_code(100, "AcDbDictionary");
        // R13 and R14: a DICTIONARY has neither the hard owner flag (280) nor the duplicate record
        // cloning flag (281) - the R13 and R14 references list neither, and none of AutoCAD's files has
        // them in a dictionary. What a file read from R13/R14 has stays in its preserved record (B12)
        if (version_ >= DxfVersion::R2000) {
            if (d.hard_owner) groups_.write_int(280, 1);
            groups_.write_int(281, d.cloning);
        }
        for (const auto& entry : d.entries) {
            if (!keep(entry.handle)) continue;
            write_code(3, entry.name);
            write_code(entry.hard_owner ? 360 : 350, entry.handle);
        }
    };

    switch (o.type) {
        case ObjectType::Dictionary:
            write_dictionary(o.as_dictionary());
            break;
        case ObjectType::DictionaryWithDefault:
            write_dictionary(o.as_dictionary_with_default().dictionary);
            write_code(100, "AcDbDictionaryWithDefault");
            write_code(340, o.as_dictionary_with_default().default_handle);
            break;
        case ObjectType::XRecord:
            write_code(100, "AcDbXrecord");
            // (R13 and R14: no 280, the data follows the subclass marker)
            if (version_ >= DxfVersion::R2000) groups_.write_int(280, o.as_xrecord().cloning);
            for (const auto& g : o.as_xrecord().data) write_code_typed(g.code, g.value);
            break;
        case ObjectType::Group: {
            const auto& g = o.as_group();
            write_code(100, "AcDbGroup");
            write_code(300, g.description);
            groups_.write_int(70, g.unnamed ? 1 : 0);
            groups_.write_int(71, g.selectable ? 1 : 0);
            for (const auto& h : g.entities) if (keep(h)) write_code(340, h);
            break;
        }
        case ObjectType::Layout: {
            const auto& l = o.as_layout();
            write_plot_settings_r2000(l.plot_settings);
            write_code(100, "AcDbLayout");
            write_code(1, l.name);
            groups_.write_int(70, l.flags);
            groups_.write_int(71, l.tab_order);
            groups_.write_double(10, l.limits_min.x);
            groups_.write_double(20, l.limits_min.y);
            groups_.write_double(11, l.limits_max.x);
            groups_.write_double(21, l.limits_max.y);
            write_point(12, l.insertion_base);
            write_point(14, l.extents_min);
            write_point(15, l.extents_max);
            groups_.write_double(146, l.elevation);
            write_point(13, l.ucs_origin);
            write_point(16, l.ucs_x_axis);
            write_point(17, l.ucs_y_axis);
            groups_.write_int(76, l.ucs_ortho_type);
            write_code(330, l.block_record_handle);
            if (!l.last_viewport_handle.empty() && keep(l.last_viewport_handle)) {
                write_code(331, l.last_viewport_handle);
            } else if (l.last_viewport_handle.empty() && !active_vport_.empty() && !template_mode() &&
                       names_equal(l.block_record_handle, record_handle("*Model_Space"))) {
                // The Model layout's last active viewport is VPORT *Active, as in AutoCAD's
                // files (2000, 2004, and AutoCAD 2016's new drawings) — in a drawing built in
                // memory; one read without it keeps none (Fusion 360's files)
                write_code(331, active_vport_);
            }
            if (!l.ucs_handle.empty()) write_code(345, l.ucs_handle);
            if (!l.base_ucs_handle.empty()) write_code(346, l.base_ucs_handle);
            break;
        }
        case ObjectType::PlotSettings:
            write_plot_settings_r2000(o.as_plot_settings());
            break;
        case ObjectType::DictionaryVar:
            write_code(100, "DictionaryVariables");
            groups_.write_int(280, o.as_dictionary_var().schema);
            write_code(1, o.as_dictionary_var().value);
            break;
        case ObjectType::Placeholder:
            break;
        case ObjectType::MlineStyle: {
            const auto& s = o.as_mline_style();
            write_code(100, "AcDbMlineStyle");
            write_code(2, s.name);
            groups_.write_int(70, s.flags);
            write_code(3, s.description);
            groups_.write_int(62, s.fill_color.aci);
            groups_.write_double(51, s.start_angle);
            groups_.write_double(52, s.end_angle);
            groups_.write_int(71, static_cast<int64_t>(s.elements.size()));
            for (const auto& el : s.elements) {
                groups_.write_double(49, el.offset);
                groups_.write_int(62, el.color.aci);
                write_code(6, symbol_ref(SymbolKind::Linetype, el.linetype));
            }
            break;
        }
        case ObjectType::ImageDef: {
            const auto& d = o.as_image_def();
            write_code(100, "AcDbRasterImageDef");
            groups_.write_int(90, d.class_version);
            write_code(1, d.file_name);
            groups_.write_double(10, d.size.x);
            groups_.write_double(20, d.size.y);
            groups_.write_double(11, d.pixel_size.x);
            groups_.write_double(21, d.pixel_size.y);
            groups_.write_int(280, d.loaded ? 1 : 0);
            groups_.write_int(281, d.resolution_units);
            break;
        }
        case ObjectType::ImageDefReactor:
            write_code(100, "AcDbRasterImageDefReactor");
            groups_.write_int(90, o.as_image_def_reactor().class_version);
            write_code(330, o.as_image_def_reactor().image_handle);
            break;
        case ObjectType::RasterVariables: {
            const auto& r = o.as_raster_variables();
            write_code(100, "AcDbRasterVariables");
            groups_.write_int(90, r.class_version);
            groups_.write_int(70, r.frame ? 1 : 0);
            groups_.write_int(71, r.quality);
            groups_.write_int(72, r.units);
            break;
        }
        case ObjectType::Scale: {
            const auto& d = o.as_scale();
            write_code(100, "AcDbScale");
            groups_.write_int(70, d.flags);
            write_code(300, d.name);
            groups_.write_double(140, d.paper_units);
            groups_.write_double(141, d.drawing_units);
            groups_.write_int(290, d.unit_scale ? 1 : 0);
            break;
        }
        case ObjectType::WipeoutVariables:
            write_code(100, "AcDbWipeoutVariables");
            groups_.write_int(70, o.as_wipeout_variables().display_frame);
            break;
        case ObjectType::DbColor: {
            // In 2000 AutoCAD writes the object without its color (B23)
            write_code(100, "AcDbColor");
            const Color& c = o.as_db_color().color;
            if (version_ >= DxfVersion::R2004) {
                groups_.write_int(62, c.aci);
                if (c.has_rgb()) groups_.write_int(420, c.rgb);
                if (!c.name.empty()) write_code(430, c.name);
            }
            break;
        }
        case ObjectType::MLeaderStyle:
            write_mleader_style(o.as_mleader_style(), o.raw);
            break;
        case ObjectType::GeoData:
            write_geodata(o.as_geodata());
            break;
        // The ModelDoc view styles and the section objects: written by their codecs (phase 9, step 9.12), before this switch
        case ObjectType::DetailViewStyle:
        case ObjectType::SectionViewStyle:
        case ObjectType::SectionManager:
        case ObjectType::SectionSettings:
            break;
        // Written by their codecs (phase 9, step 9.6), before this switch
        case ObjectType::NavisworksModelDef:
        case ObjectType::PdfDefinition:
        case ObjectType::DwfDefinition:
        case ObjectType::DgnDefinition:
            break;
        // Written by their codecs (phase 9, step 9.4), before this switch
        case ObjectType::SortentsTable:
        case ObjectType::SpatialFilter:
        case ObjectType::LayerIndex:
        case ObjectType::SpatialIndex:
        case ObjectType::IdBuffer:
        case ObjectType::LayerFilter:
            break;
        case ObjectType::ProxyObject:
            break;                  // written by its codec (above)
        // MATERIAL and VISUALSTYLE: written by their codecs (phase 9, step 9.7), before this switch
        case ObjectType::Material:
        case ObjectType::VisualStyle:
            break;
        // Written by their codecs (phase 9, step 9.11), before this switch
        case ObjectType::Sun:
        case ObjectType::RapidRtRenderSettings:
        case ObjectType::RapidRtRenderEnvironment:
        case ObjectType::RenderSettings:
        case ObjectType::MentalRayRenderSettings:
        case ObjectType::RenderEnvironment:
        case ObjectType::RenderGlobal:
        case ObjectType::LightList:
        case ObjectType::SunStudy:
            break;
        case ObjectType::Unknown:
            for (const auto& [code, value] : o.as_unknown().group_codes) write_code_typed(code, value);
            break;
        default:
            break;                  // the types phase 9 typed: written by their codecs
    }
    // The color again, as AutoCAD writes it: ACI, AutoCAD's raw color (method 0xC2, "by
    // color", over the RGB) and the name — unless the object has ACAD XDATA of its own
    // (one the reader did not recognize as this), which is written as it is
    bool own_acad = std::any_of(o.xdata.begin(), o.xdata.end(),
                                [](const XdataList& x) { return x.app_name == "ACAD"; });
    // (2018 has no such XDATA: ACadSharp's sample_AC1032, B77)
    if (o.type == ObjectType::DbColor && version_ >= DxfVersion::R2004 && version_ < DxfVersion::R2018 &&
        o.as_db_color().color.has_rgb() && !own_acad) {
        const Color& c = o.as_db_color().color;
        write_code(1001, "ACAD");
        write_code(1000, "DBColXD");
        groups_.write_int(1070, c.aci);
        groups_.write_int(1071, static_cast<int32_t>(0xC2000000u | static_cast<uint32_t>(c.rgb & 0xFFFFFF)));
        write_code(1000, c.name);
    }
    bool saved_by = version_ >= DxfVersion::R2018 && o.type == ObjectType::Unknown && o.as_unknown().object_type == "VISUALSTYLE";
    write_xdata(saved_by ? without_saved_by_version(o.xdata) : o.xdata);
    if (o.type == ObjectType::MLeaderStyle) write_mleader_version(o.as_mleader_style().version);
}

} // namespace internal
} // namespace hdxf
