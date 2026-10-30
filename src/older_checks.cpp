#include "older_checks.hpp"
#include "dxf_writer.hpp"
#include "header_defaults.hpp"
#include "dimstyle_legacy.hpp"
#include "older_tables.hpp"
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// The faults of a drawing that is written as DXF R12, R13 or R14 (plan phase 7, step 7.20, B94, B98).
// One order, in which the first fault is the error (older_version_error()):
//   1. a layout: a LAYOUT or PLOTSETTINGS object, a *Paper_SpaceN block
//   2. the entities of model space and paper space, then each block's entities and its own properties:
//      per entity its lineweight and plot style, the names it refers to, what only a later form of its
//      type has (MTEXT's columns, a leader's block offset …), a viewport's 2000 fields, its type if a
//      later version introduced it; an INSERT's attributes after the INSERT
//   3. the header's variables a later version introduced, and the names it holds
//   4. the objects of a type a later version introduced (the raster image objects are R14's)
//   5. the dimension styles
//   6. the tables (older_tables_error())
// A record of the file the drawing was read from (`original`) with the same fault is written from its
// preserved record and is not an error (an entity, an attribute, an object: by its handle; a block, a
// dimension style: by its name; a header variable: when its value is the file's).

namespace hdxf {
namespace internal {

namespace {

// ---------------------------------------------------------------------------
// The version that introduced a type
// ---------------------------------------------------------------------------

const char* version_label(DxfVersion version) {
    switch (version) {
        case DxfVersion::R13:   return "R13";
        case DxfVersion::R14:   return "R14";
        case DxfVersion::R2004: return "2004";
        case DxfVersion::R2007: return "2007";
        default:                return "2000";
    }
}

std::string needs(const std::string& what, DxfVersion first) {
    return what + " requires DXF " + version_label(first) + " or later";
}

struct TypeSupport {
    const char* name;
    DxfVersion first;
};

// The entity types that came after R12 and are R13's, R14's or 2000's. Not the surfaces, LIGHT and
// COORDINATION_MODEL (2007 and 2013: the checks of those versions name them) and not the R12 types
std::optional<TypeSupport> entity_type_support(EntityType type) {
    switch (type) {
        case EntityType::Ellipse:    return TypeSupport{"ELLIPSE", DxfVersion::R13};
        case EntityType::Xline:      return TypeSupport{"XLINE", DxfVersion::R13};
        case EntityType::Ray:        return TypeSupport{"RAY", DxfVersion::R13};
        case EntityType::MText:      return TypeSupport{"MTEXT", DxfVersion::R13};
        case EntityType::Tolerance:  return TypeSupport{"TOLERANCE", DxfVersion::R13};
        case EntityType::Leader:     return TypeSupport{"LEADER", DxfVersion::R13};
        case EntityType::Spline:     return TypeSupport{"SPLINE", DxfVersion::R13};
        case EntityType::Mline:      return TypeSupport{"MLINE", DxfVersion::R13};
        case EntityType::Solid3D:    return TypeSupport{"3DSOLID", DxfVersion::R13};
        case EntityType::Region:     return TypeSupport{"REGION", DxfVersion::R13};
        case EntityType::Body:       return TypeSupport{"BODY", DxfVersion::R13};
        case EntityType::LwPolyline: return TypeSupport{"LWPOLYLINE", DxfVersion::R14};
        case EntityType::Hatch:      return TypeSupport{"HATCH", DxfVersion::R14};
        case EntityType::Image:      return TypeSupport{"IMAGE", DxfVersion::R14};
        case EntityType::Wipeout:    return TypeSupport{"WIPEOUT", DxfVersion::R2000};
        case EntityType::MLeader:    return TypeSupport{"MULTILEADER", DxfVersion::R2000};
        case EntityType::Helix:      return TypeSupport{"HELIX", DxfVersion::R2000};
        case EntityType::Mesh:       return TypeSupport{"MESH", DxfVersion::R2000};
        default:                     return std::nullopt;
    }
}

// The object types that came after R12 (R13 and R14 have DICTIONARY, XRECORD, GROUP, DICTIONARYVAR and
// MLINESTYLE; R14 has the raster image objects) and their first version. Not the objects of 2013
// (the coordination model, RAPIDRT: their checks name them) and not an unknown record
std::optional<TypeSupport> object_type_support(ObjectType type) {
    switch (type) {
        case ObjectType::ImageDef:                 return TypeSupport{"IMAGEDEF", DxfVersion::R14};
        case ObjectType::ImageDefReactor:          return TypeSupport{"IMAGEDEF_REACTOR", DxfVersion::R14};
        case ObjectType::RasterVariables:          return TypeSupport{"RASTERVARIABLES", DxfVersion::R14};
        case ObjectType::Layout:                   return TypeSupport{"LAYOUT", DxfVersion::R2000};
        case ObjectType::PlotSettings:             return TypeSupport{"PLOTSETTINGS", DxfVersion::R2000};
        case ObjectType::DictionaryWithDefault:    return TypeSupport{"ACDBDICTIONARYWDFLT", DxfVersion::R2000};
        case ObjectType::Placeholder:              return TypeSupport{"ACDBPLACEHOLDER", DxfVersion::R2000};
        case ObjectType::MLeaderStyle:             return TypeSupport{"MLEADERSTYLE", DxfVersion::R2000};
        case ObjectType::GeoData:                  return TypeSupport{"GEODATA", DxfVersion::R2000};
        case ObjectType::WipeoutVariables:         return TypeSupport{"WIPEOUTVARIABLES", DxfVersion::R2000};
        case ObjectType::Scale:                    return TypeSupport{"SCALE", DxfVersion::R2000};
        case ObjectType::Sun:                      return TypeSupport{"SUN", DxfVersion::R2007};
        case ObjectType::DetailViewStyle:          return TypeSupport{"ACDBDETAILVIEWSTYLE", DxfVersion::R2000};
        case ObjectType::SectionViewStyle:         return TypeSupport{"ACDBSECTIONVIEWSTYLE", DxfVersion::R2000};
        case ObjectType::DbColor:                  return TypeSupport{"DBCOLOR", DxfVersion::R2004};
        default:                                   return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// What the file had
// ---------------------------------------------------------------------------

class ReadState {
public:
    explicit ReadState(const Drawing* original) : original_(original) {
        if (!original) return;
        auto add = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) {
                if (!e.props.handle.empty()) entities_.emplace(e.props.handle, &e);
                if (e.type != EntityType::Insert) continue;
                for (const auto& a : e.as_insert().attributes) {
                    if (!a.props.handle.empty()) attributes_.emplace(a.props.handle, &a);
                }
            }
        };
        add(original->entities());
        for (const auto& b : original->blocks()) add(b.entities);
        for (const auto& o : original->objects()) {
            if (!o.handle.empty()) objects_[o.handle] = &o;
        }
    }

    const Drawing* drawing() const { return original_; }

    const Entity* entity(const Entity& e) const {
        auto it = entities_.find(e.props.handle);
        return !e.props.handle.empty() && it != entities_.end() && it->second->type == e.type ? it->second : nullptr;
    }

    const InsertAttribute* attribute(const InsertAttribute& a) const {
        auto it = attributes_.find(a.props.handle);
        return !a.props.handle.empty() && it != attributes_.end() ? it->second : nullptr;
    }

    // A read object of the same type (and, for an MLINE style, the same name): written from its record
    bool has_object(const Object& o) const {
        auto it = objects_.find(o.handle);
        if (o.handle.empty() || it == objects_.end() || it->second->type != o.type) return false;
        if (o.type == ObjectType::MlineStyle) return it->second->as_mline_style().name == o.as_mline_style().name;
        return true;
    }

    const Block* block(const std::string& name) const {
        if (!original_) return nullptr;
        for (const auto& b : original_->blocks()) {
            if (names_equal(b.name, name)) return &b;
        }
        return nullptr;
    }

private:
    const Drawing* original_;
    std::unordered_map<std::string, const Entity*> entities_;
    std::unordered_map<std::string, const InsertAttribute*> attributes_;
    std::unordered_map<std::string, const Object*> objects_;
};

// A name an entity, a style or the header refers to: the record it names may be xref-dependent (70 bit
// 16, "|" allowed) and is itself checked with its own flags among the tables (Codex review, phase 7)
std::string older_reference_name_error(const std::string& what, const std::string& name) {
    return older_symbol_name_error(what, name, true);
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

// Lineweight and plot style are 2000's: R13 and R14 have no group for them (B93, B98)
std::string older_properties(const EntityProperties& p) {
    if (p.lineweight != -1) return "Lineweight requires DXF 2000 or later";
    if (p.plot_style_type != 0 || !p.plot_style_handle.empty()) return "Plot style requires DXF 2000 or later";
    return {};
}

// The names an entity's own properties refer to
std::string property_names_error(const EntityProperties& p) {
    std::string error = older_reference_name_error("Layer", p.layer);
    if (error.empty()) error = older_reference_name_error("Linetype", p.linetype);
    return error;
}

// A dimension style override in the entity's XDATA (ACAD, DSTYLE): the arrow blocks it names (5, 6, 7)
// are block names, as DIMBLK is in the header and in the style
std::string override_names_error(const EntityProperties& p) {
    for (const auto& list : p.xdata) {
        if (!names_equal(list.app_name, "ACAD")) continue;
        bool in = false;
        const auto& x = list.items;
        for (size_t i = 0; i < x.size(); ++i) {
            if (x[i].code == 1000 && x[i].value == "DSTYLE") {
                in = true;
                continue;
            }
            if (!in) continue;
            if (x[i].code == 1002 && x[i].value == "}") break;
            if (x[i].code != 1070 || i + 1 >= x.size()) continue;
            int code = std::atoi(x[i].value.c_str());
            if (code >= 5 && code <= 7) {
                std::string error = older_reference_name_error("Block", x[i + 1].value);
                if (!error.empty()) return error;
            }
            ++i;
        }
    }
    return {};
}

std::string attribute_fault(const AttribData& a) {
    if (a.version != 0) return "An attribute's version requires DXF 2010 or later";
    std::string error = older_reference_name_error("Text style", a.style);
    return error;
}

// What a viewport has that DXF 2000 added as group codes of the entity: a clip boundary, the plot style
// sheet, the render mode, the viewport's own UCS, its elevation. The viewport's view is the XDATA ACAD /
// MVIEW (the typed fields of ViewportData); 2000's fields have no place in it (7.16)
std::string viewport_fault(const ViewportData& d) {
    const ViewportData d0;
    std::string found;
    if (!d.clip_boundary_handle.empty()) found = "A viewport's clip boundary";
    else if (!d.plot_style_sheet.empty()) found = "A viewport's plot style sheet";
    else if (d.render_mode != d0.render_mode) found = "A viewport's render mode";
    else if (d.elevation != d0.elevation) found = "A viewport's elevation";
    else if (d.ucs_per_viewport != d0.ucs_per_viewport || d.ucs_icon != d0.ucs_icon ||
             d.ucs_ortho_type != d0.ucs_ortho_type || !d.ucs.empty() || !d.base_ucs.empty() ||
             d.ucs_origin.x != d0.ucs_origin.x || d.ucs_origin.y != d0.ucs_origin.y ||
             d.ucs_origin.z != d0.ucs_origin.z || d.ucs_x_axis.x != d0.ucs_x_axis.x ||
             d.ucs_x_axis.y != d0.ucs_x_axis.y || d.ucs_x_axis.z != d0.ucs_x_axis.z ||
             d.ucs_y_axis.x != d0.ucs_y_axis.x || d.ucs_y_axis.y != d0.ucs_y_axis.y ||
             d.ucs_y_axis.z != d0.ucs_y_axis.z) {
        found = "A viewport's own UCS";
    }
    if (!found.empty()) found += " requires DXF 2000 or later";
    return found;
}

// What an entity has that R13 or R14 cannot hold: the first fault of the entity itself (not its
// attributes), in the order of the header comment
std::string entity_fault(const Entity& e, DxfVersion version) {
    std::string error = older_properties(e.props);
    if (error.empty()) error = property_names_error(e.props);
    if (error.empty()) error = override_names_error(e.props);
    if (!error.empty()) return error;

    switch (e.type) {
        case EntityType::Text:
            error = older_reference_name_error("Text style", e.as_text().style);
            break;
        case EntityType::Insert:
            error = older_reference_name_error("Block", e.as_insert().block_name);
            break;
        case EntityType::Attrib:
            error = attribute_fault(e.as_attrib());
            break;
        case EntityType::Attdef:
            if (e.as_attdef().version != 0) return "An attribute's version requires DXF 2010 or later";
            error = older_reference_name_error("Text style", e.as_attdef().style);
            break;
        case EntityType::Dimension: {
            const DimensionData& d = e.as_dimension();
            error = older_reference_name_error("Dimension style", d.dim_style);
            if (error.empty()) error = older_reference_name_error("Block", d.block_name);
            break;
        }
        case EntityType::Tolerance:
            error = older_reference_name_error("Dimension style", e.as_tolerance().dim_style);
            break;
        case EntityType::MText: {
            const MTextData& d = e.as_mtext();
            error = older_reference_name_error("Text style", d.style);
            if (!error.empty()) break;
            if (d.columns || d.embedded || !d.column_parent.empty()) return "An MTEXT with columns requires DXF 2000 or later";
            if (d.defined_height != 0.0) return "An MTEXT's defined height requires DXF 2000 or later";
            if (d.line_spacing_style != 1 || d.line_spacing_factor != 1.0) return "An MTEXT's line spacing requires DXF 2000 or later";
            // R13: 1 left to right, 2 right to left, 3 top to bottom, 4 bottom to top; R14: 1, 3 and
            // 5 by style (R14 revision u14.1.04); HDxf's drawing_direction is R14's and 2000's
            if (version == DxfVersion::R13 && d.drawing_direction == 5) return "An MTEXT's drawing direction by style requires DXF R14 or later";
            if (version == DxfVersion::R14 && (d.drawing_direction == 2 || d.drawing_direction == 4)) {
                return "An MTEXT's right to left or bottom to top drawing direction is DXF R13's only";
            }
            break;
        }
        case EntityType::Leader:
            error = older_reference_name_error("Dimension style", e.as_leader().dim_style);
            if (!error.empty()) break;
            // R13 has one offset (212, the annotation's); the block's offset is R14's
            if (version == DxfVersion::R13 && (e.as_leader().block_offset.x != 0.0 || e.as_leader().block_offset.y != 0.0 ||
                                               e.as_leader().block_offset.z != 0.0)) {
                return "A leader's block offset requires DXF R14 or later";
            }
            break;
        case EntityType::Mline:
            error = older_mline_style_name_error(e.as_mline().style);
            if (!error.empty()) break;
            // R13's 71 is 1 open, 3 closed; the cap bits (4, 8) are R14's
            if (version == DxfVersion::R13 && (e.as_mline().flags & ~3) != 0) {
                return "An MLINE's suppressed caps require DXF R14 or later";
            }
            break;
        case EntityType::LwPolyline:
            for (const auto& v : e.as_lwpolyline().vertices) {
                if (v.id) return "An LWPOLYLINE's vertex identifiers require DXF 2010 or later";
            }
            break;
        case EntityType::Polyline:
            for (const auto& v : e.as_polyline().vertices) {
                if (v.id) return "A polyline vertex's identifier requires DXF 2010 or later";
            }
            break;
        case EntityType::Viewport:
            error = viewport_fault(e.as_viewport());
            break;
        case EntityType::Unknown: {
            // The dimension types of 2000 and later are not the typed DIMENSION
            const std::string& type = e.as_unknown().entity_type;
            if (type == "ARC_DIMENSION" || type == "LARGE_RADIAL_DIMENSION") return type + " requires DXF 2000 or later";
            break;
        }
        default:
            break;
    }
    if (!error.empty()) return error;
    return entity_type_error(e.type, version);
}

// The values of everything an entity can have that R13 and R14 cannot hold (whether it is a fault or
// not): a read entity is written from its preserved record only while these are as they were read, so
// an edit of a value the older record has no group for is never lost silently (B98; Codex review)
std::string fault_values(const Entity& e) {
    std::ostringstream out;
    out.precision(17);
    const EntityProperties& p = e.props;
    out << p.lineweight << '|' << p.plot_style_type << '|' << p.plot_style_handle << '|' << p.layer << '|' << p.linetype;
    switch (e.type) {
        case EntityType::Text: out << '|' << e.as_text().style; break;
        case EntityType::Insert: out << '|' << e.as_insert().block_name; break;
        case EntityType::Attrib: out << '|' << e.as_attrib().version << '|' << e.as_attrib().style; break;
        case EntityType::Attdef: out << '|' << e.as_attdef().version << '|' << e.as_attdef().style; break;
        case EntityType::Dimension: out << '|' << e.as_dimension().dim_style << '|' << e.as_dimension().block_name; break;
        case EntityType::Tolerance: out << '|' << e.as_tolerance().dim_style; break;
        case EntityType::MText: {
            const MTextData& d = e.as_mtext();
            out << '|' << d.style << '|' << static_cast<bool>(d.columns) << '|' << static_cast<bool>(d.embedded) << '|'
                << d.column_parent << '|' << d.defined_height << '|' << d.line_spacing_style << '|'
                << d.line_spacing_factor << '|' << d.drawing_direction;
            break;
        }
        case EntityType::Leader: {
            const auto& d = e.as_leader();
            out << '|' << d.dim_style << '|' << d.block_offset.x << ',' << d.block_offset.y << ',' << d.block_offset.z;
            break;
        }
        case EntityType::Mline: out << '|' << e.as_mline().style << '|' << e.as_mline().flags; break;
        case EntityType::LwPolyline:
            out << '|' << e.as_lwpolyline().flags;
            for (const auto& v : e.as_lwpolyline().vertices) out << ',' << (v.id ? std::to_string(*v.id) : std::string("-"));
            break;
        case EntityType::Polyline:
            for (const auto& v : e.as_polyline().vertices) out << ',' << (v.id ? std::to_string(*v.id) : std::string("-"));
            break;
        case EntityType::Viewport: {
            const ViewportData& d = e.as_viewport();
            out << '|' << d.clip_boundary_handle << '|' << d.plot_style_sheet << '|' << d.render_mode << '|'
                << d.elevation << '|' << d.ucs_per_viewport << '|' << d.ucs_icon << '|' << d.ucs_ortho_type << '|'
                << d.ucs << '|' << d.base_ucs << '|' << d.ucs_origin.x << ',' << d.ucs_origin.y << ','
                << d.ucs_origin.z << '|' << d.ucs_x_axis.x << ',' << d.ucs_x_axis.y << ',' << d.ucs_x_axis.z << '|'
                << d.ucs_y_axis.x << ',' << d.ucs_y_axis.y << ',' << d.ucs_y_axis.z;
            break;
        }
        default: break;
    }
    for (const auto& list : p.xdata) {
        if (!names_equal(list.app_name, "ACAD")) continue;
        for (const auto& x : list.items) out << '|' << x.code << '=' << x.value;
    }
    return out.str();
}

std::string attribute_fault_values(const InsertAttribute& a) {
    std::ostringstream out;
    const EntityProperties& p = a.props;
    out << p.lineweight << '|' << p.plot_style_type << '|' << p.plot_style_handle << '|' << p.layer << '|'
        << p.linetype << '|' << a.data.version << '|' << a.data.style;
    return out.str();
}

// The entity's fault unless the file had the entity with the same fault and the same plotting
// properties (written from its preserved record); its attributes after it
std::string entity_error(const Entity& e, DxfVersion version, const ReadState& read) {
    std::string error = entity_fault(e, version);
    if (!error.empty()) {
        const Entity* was = read.entity(e);
        if (!was || fault_values(*was) != fault_values(e)) return error;
    }
    if (e.type != EntityType::Insert) return {};
    for (const auto& a : e.as_insert().attributes) {
        auto fault = [&](const InsertAttribute& x) {
            std::string f = older_properties(x.props);
            if (f.empty()) f = property_names_error(x.props);
            if (f.empty()) f = attribute_fault(x.data);
            return f;
        };
        std::string attribute_error = fault(a);
        if (attribute_error.empty()) continue;
        const InsertAttribute* was = read.attribute(a);
        if (was && attribute_fault_values(*was) == attribute_fault_values(a)) continue;
        return attribute_error;
    }
    return {};
}

std::string entities_error(const std::vector<Entity>& list, DxfVersion version, const ReadState& read) {
    for (const auto& e : list) {
        std::string error = entity_error(e, version, read);
        if (!error.empty()) return error;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Objects and the header
// ---------------------------------------------------------------------------

std::string object_error(const Object& o, DxfVersion version, const ReadState& read) {
    if (read.has_object(o)) return {};
    if (std::optional<TypeSupport> t = object_type_support(o.type); t && version < t->first) {
        return needs(t->name, t->first);
    }
    if (o.type == ObjectType::MlineStyle) return older_mline_style_name_error(o.as_mline_style().name);
    // The layers a filter and a layer index name are table records (9.4)
    if (o.type == ObjectType::LayerFilter) {
        for (const auto& layer : o.as_layer_filter().layers) {
            std::string error = older_reference_name_error("Layer", layer);
            if (!error.empty()) return error;
        }
    }
    if (o.type == ObjectType::LayerIndex) {
        for (const auto& entry : o.as_layer_index().entries) {
            std::string error = older_reference_name_error("Layer", entry.layer_name);
            if (!error.empty()) return error;
        }
    }
    return {};
}

// A header variable that holds the name of a table record
struct HeaderName {
    const char* variable;
    const char* what;
};

std::string header_error(const Drawing& dwg, DxfVersion version, const ReadState& read) {
    // A header variable only a later version has ($CELWEIGHT, $INSUNITS, $LASTSAVEDBY …): the header
    // of R13 and R14 is AutoCAD's own list of variables (format profile 3.6, B98)
    std::set<std::string> own, r14, later;
    for (const auto& v : version == DxfVersion::R13 ? default_r13_headers() : default_r14_headers()) own.insert(v.name);
    for (const auto& v : default_r14_headers()) r14.insert(v.name);
    for (const auto& v : default_r2018_headers()) later.insert(v.name);
    for (const auto& v : default_r2013_headers()) later.insert(v.name);

    const HeaderName names[] = {{"$CLAYER", "Layer"},         {"$CELTYPE", "Linetype"},  {"$TEXTSTYLE", "Text style"},
                                {"$DIMTXSTY", "Text style"},  {"$DIMSTYLE", "Dimension style"},
                                {"$DIMBLK", "Block"},         {"$DIMBLK1", "Block"},     {"$DIMBLK2", "Block"},
                                {"$UCSNAME", "UCS"},          {"$PUCSNAME", "UCS"}};
    for (const auto& hv : dwg.header_vars()) {
        // The file's own variable, as it was read, is written from the file
        if (const Drawing* original = read.drawing()) {
            if (const HeaderVar* was = original->find_header(hv.name); was && was->values == hv.values) continue;
        }
        if (!own.count(hv.name)) {
            // What the library itself gives a drawing made as 2000+ (its identity, the pointers to its
            // material and visual styles) is not the program's variable
            if (hv.name == "$FINGERPRINTGUID" || hv.name == "$VERSIONGUID" || hv.name == "$CMATERIAL" ||
                hv.name == "$INTERFEREOBJVS" || hv.name == "$INTERFEREVPVS" || hv.name == "$XCLIPFRAME") continue;
            if (version == DxfVersion::R13 && r14.count(hv.name)) return hv.name + " requires DXF R14 or later";
            if (later.count(hv.name)) return hv.name + " requires DXF 2000 or later";
            continue;
        }
        if (hv.values.empty()) continue;
        for (const auto& n : names) {
            if (hv.name != n.variable) continue;
            std::string error = older_reference_name_error(n.what, hv.values[0].second);
            if (!error.empty()) return error;
        }
        if (hv.name == "$CMLSTYLE") {
            std::string error = older_mline_style_name_error(hv.values[0].second);
            if (!error.empty()) return error;
        }
    }
    return {};
}

// A dimension style with something the older record has no group for (B91, B98), and the names it
// holds: the arrow blocks and the text style
std::string dimension_style_error(const DimStyle& s) {
    std::string error = dimstyle_older_error(s);
    if (error.empty()) error = older_reference_name_error("Block", s.dimblk);
    if (error.empty()) error = older_reference_name_error("Block", s.dimblk1);
    if (error.empty()) error = older_reference_name_error("Block", s.dimblk2);
    if (error.empty()) error = older_reference_name_error("Text style", s.dimtxsty);
    return error;
}

} // namespace

std::string entity_type_error(EntityType type, DxfVersion target) {
    std::optional<TypeSupport> t = entity_type_support(type);
    if (!t || target >= t->first) return {};
    return needs(t->name, t->first);
}

std::string older_version_error(const Drawing& dwg, DxfVersion version, const Drawing* original) {
    const ReadState read(original);

    // 1. A layout (R13 and R14 have one paper space and no layout tabs, B89)
    for (const auto& o : dwg.objects()) {
        if (!read.has_object(o) && (o.type == ObjectType::Layout || o.type == ObjectType::PlotSettings)) {
            return object_error(o, version, read);
        }
    }
    const std::string paper = "*Paper_Space";
    for (const auto& b : dwg.blocks()) {
        if (is_paper_space(b.name) && !names_equal(b.name, paper) && !read.block(b.name)) {
            return "LAYOUT requires DXF 2000 or later";
        }
    }

    // 2. Model space and paper space, then every block with its own properties: its description (4)
    // and lineweight are 2000's
    std::string error = entities_error(dwg.entities(), version, read);
    if (!error.empty()) return error;
    for (const auto& b : dwg.blocks()) {
        error = entities_error(b.entities, version, read);
        if (!error.empty()) return error;
        const Block* was = read.block(b.name);
        if (!b.description.empty() && !(was && was->description == b.description)) {
            return "A block's description requires DXF 2000 or later";
        }
        if (b.lineweight != -1 && !(was && was->lineweight == b.lineweight)) return "Lineweight requires DXF 2000 or later";
    }

    // 3. The header
    error = header_error(dwg, version, read);
    if (!error.empty()) return error;

    // 4. The objects of a type a later version introduced (the raster image objects are R14's)
    for (const auto& o : dwg.objects()) {
        error = object_error(o, version, read);
        if (!error.empty()) return error;
    }

    // 5. The dimension styles
    for (const auto& style : dwg.dim_styles()) {
        std::string fault = dimension_style_error(style);
        if (fault.empty()) continue;
        if (original) {
            const DimStyle* was = nullptr;
            for (const auto& s : original->dim_styles()) {
                if (names_equal(s.name, style.name)) was = &s;
            }
            if (was && dimension_style_error(*was) == fault) continue;
        }
        return fault;
    }

    // 6. The tables: the names, and the table fields R13 and R14 have no groups for (B90)
    return older_tables_error(dwg, original);
}

} // namespace internal
} // namespace hdxf
