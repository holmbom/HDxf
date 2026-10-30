#include "dxf_reader.hpp"
#include "handles.hpp"
#include "mleader.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <unordered_map>

// OBJECTS and BLOCK_RECORD (plan phase 1, step 1.1). Both are read from the preserved
// record — every group, not only what interpret() lets through — because the 102 groups
// (reactors, extension dictionary) are part of the model and an XRECORD may hold any code
// as data.

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// Groups of a preserved record, decoded like read_group() does
// ---------------------------------------------------------------------------

bool DxfReader::decode_groups(const RawRecord& record, std::vector<DxfGroup>& out) {
    out = record.groups();
    for (auto& g : out) {
        if (g.code == 0 || g.code == 9) {
            while (!g.value.empty() && g.value.back() == ' ') g.value.pop_back();
        }
        if (group_value_type(g.code) != GroupValueType::String ||
            !needs_text_conversion(g.value)) {
            continue;
        }
        std::string text = groups_.binary() ? g.value : decode_carets(g.value, code_page_);
        std::string message;
        if (!decode_text(text, code_page_, code_page_name_, g.value, message)) {
            error_ = message + " (group code " + std::to_string(g.code) + ")";
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Record head and XDATA
// ---------------------------------------------------------------------------

namespace {

// The head of an R13+ record: 5, the 102 groups {ACAD_REACTORS and {ACAD_XDICTIONARY,
// and the owner 330 — in AutoCAD's order 0, 5, 102 …, 330, then the first subclass.
// Returns the index of the first group after the head.
struct RecordHead {
    std::string handle;
    std::string owner;
    std::vector<std::string> reactors;
    std::string xdictionary;
};

size_t parse_head(const std::vector<DxfGroup>& g, RecordHead& head) {
    size_t i = 1;
    bool owner_seen = false;
    while (i < g.size()) {
        if ((g[i].code == 5 || (g[i].code == 105 && g[0].value == "DIMSTYLE")) && head.handle.empty()) {
            head.handle = g[i].value;
            ++i;
        } else if (g[i].code == 102 &&
                   (g[i].value == "{ACAD_REACTORS" || g[i].value == "{ACAD_XDICTIONARY")) {
            bool reactors = g[i].value == "{ACAD_REACTORS";
            ++i;
            while (i < g.size() && g[i].code != 102) {
                if (reactors && g[i].code == 330) head.reactors.push_back(g[i].value);
                if (!reactors && g[i].code == 360) head.xdictionary = g[i].value;
                ++i;
            }
            if (i < g.size()) ++i;              // 102 }
        } else if (g[i].code == 330 && !owner_seen) {
            head.owner = g[i].value;
            owner_seen = true;
            ++i;
        } else {
            break;
        }
    }
    return i;
}

// XDATA is the tail of the record from a 1001 on; every group after it is >= 1000. A
// 1000-range group before that (an XRECORD may hold them as data) is not XDATA.
size_t xdata_start(const std::vector<DxfGroup>& g, size_t body) {
    size_t tail = g.size();
    while (tail > body && g[tail - 1].code >= 1000) --tail;
    while (tail < g.size() && g[tail].code != 1001) ++tail;
    return tail;
}

void parse_xdata_groups(const std::vector<DxfGroup>& g, size_t start,
                        std::vector<XdataList>& out) {
    for (size_t i = start; i < g.size(); ++i) {
        if (g[i].code == 1001) {
            out.push_back({g[i].value, {}});
        } else if (!out.empty()) {
            out.back().items.push_back({g[i].code, g[i].value});
        }
    }
}

int to_int(const std::string& s) { return std::atoi(s.c_str()); }
double to_double(const std::string& s) { return std::atof(s.c_str()); }

// ---------------------------------------------------------------------------
// Object types
// ---------------------------------------------------------------------------

void parse_dictionary_group(const DxfGroup& g, DictionaryData& d) {
    switch (g.code) {
        case 280: d.hard_owner = to_int(g.value) != 0; break;
        case 281: d.cloning = to_int(g.value); break;
        case 3:   d.entries.push_back({g.value, {}, false}); break;
        case 350:
        case 360:
            if (d.entries.empty() || !d.entries.back().handle.empty()) {
                d.entries.push_back({});
            }
            d.entries.back().handle = g.value;
            d.entries.back().hard_owner = g.code == 360;
            break;
    }
}

DictionaryData parse_dictionary(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    DictionaryData d;
    for (size_t i = begin; i < end; ++i) parse_dictionary_group(g[i], d);
    return d;
}

DictionaryWithDefaultData parse_dictionary_with_default(const std::vector<DxfGroup>& g,
                                                        size_t begin, size_t end) {
    DictionaryWithDefaultData d;
    std::string subclass;
    for (size_t i = begin; i < end; ++i) {
        if (g[i].code == 100) subclass = g[i].value;
        else if (subclass == "AcDbDictionaryWithDefault" && g[i].code == 340)
            d.default_handle = g[i].value;
        else parse_dictionary_group(g[i], d.dictionary);
    }
    return d;
}

// 280 (cloning) follows the subclass marker from DXF 2000; in R13/R14 the data starts
// right after it
XRecordData parse_xrecord(const std::vector<DxfGroup>& g, size_t begin, size_t end,
                          bool has_cloning) {
    XRecordData d;
    size_t i = begin;
    if (i < end && g[i].code == 100 && g[i].value == "AcDbXrecord") ++i;
    if (has_cloning && i < end && g[i].code == 280) {
        d.cloning = to_int(g[i].value);
        ++i;
    }
    for (; i < end; ++i) d.data.push_back(g[i]);
    return d;
}

GroupData parse_group(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    GroupData d;
    for (size_t i = begin; i < end; ++i) {
        switch (g[i].code) {
            case 300: d.description = g[i].value; break;
            case 70:  d.unnamed = to_int(g[i].value) != 0; break;
            case 71:  d.selectable = to_int(g[i].value) != 0; break;
            case 340: d.entities.push_back(g[i].value); break;
        }
    }
    return d;
}

void parse_plot_settings_group(const DxfGroup& g, PlotSettings& p) {
    switch (g.code) {
        case 1:   p.page_setup_name = g.value; break;
        case 2:   p.printer_name = g.value; break;
        case 4:   p.paper_size = g.value; break;
        case 6:   p.plot_view_name = g.value; break;
        case 7:   p.style_sheet = g.value; break;
        case 40:  p.margin_left = to_double(g.value); break;
        case 41:  p.margin_bottom = to_double(g.value); break;
        case 42:  p.margin_right = to_double(g.value); break;
        case 43:  p.margin_top = to_double(g.value); break;
        case 44:  p.paper_width = to_double(g.value); break;
        case 45:  p.paper_height = to_double(g.value); break;
        case 46:  p.plot_origin.x = to_double(g.value); break;
        case 47:  p.plot_origin.y = to_double(g.value); break;
        case 48:  p.window_min.x = to_double(g.value); break;
        case 49:  p.window_min.y = to_double(g.value); break;
        case 140: p.window_max.x = to_double(g.value); break;
        case 141: p.window_max.y = to_double(g.value); break;
        case 142: p.scale_numerator = to_double(g.value); break;
        case 143: p.scale_denominator = to_double(g.value); break;
        case 70:  p.flags = to_int(g.value); break;
        case 72:  p.paper_units = to_int(g.value); break;
        case 73:  p.rotation = to_int(g.value); break;
        case 74:  p.plot_type = to_int(g.value); break;
        case 75:  p.standard_scale_type = to_int(g.value); break;
        case 76:  p.shade_plot_mode = to_int(g.value); break;
        case 77:  p.shade_plot_resolution = to_int(g.value); break;
        case 78:  p.shade_plot_dpi = to_int(g.value); break;
        case 147: p.scale_factor = to_double(g.value); break;
        case 148: p.image_origin.x = to_double(g.value); break;
        case 149: p.image_origin.y = to_double(g.value); break;
        case 333: p.shade_plot_handle = g.value; break;
    }
}

PlotSettings parse_plot_settings(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    PlotSettings p;
    for (size_t i = begin; i < end; ++i) parse_plot_settings_group(g[i], p);
    return p;
}

// LAYOUT: AcDbPlotSettings, then AcDbLayout. Codes such as 1, 70 and 76 mean different
// things in the two subclasses.
LayoutData parse_layout(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    LayoutData d;
    std::string subclass;
    for (size_t i = begin; i < end; ++i) {
        const DxfGroup& gr = g[i];
        if (gr.code == 100) { subclass = gr.value; continue; }
        if (subclass == "AcDbPlotSettings") {
            parse_plot_settings_group(gr, d.plot_settings);
            continue;
        }
        if (subclass != "AcDbLayout") continue;
        switch (gr.code) {
            case 1:   d.name = gr.value; break;
            case 70:  d.flags = to_int(gr.value); break;
            case 71:  d.tab_order = to_int(gr.value); break;
            case 10:  d.limits_min.x = to_double(gr.value); break;
            case 20:  d.limits_min.y = to_double(gr.value); break;
            case 11:  d.limits_max.x = to_double(gr.value); break;
            case 21:  d.limits_max.y = to_double(gr.value); break;
            case 12:  d.insertion_base.x = to_double(gr.value); break;
            case 22:  d.insertion_base.y = to_double(gr.value); break;
            case 32:  d.insertion_base.z = to_double(gr.value); break;
            case 14:  d.extents_min.x = to_double(gr.value); break;
            case 24:  d.extents_min.y = to_double(gr.value); break;
            case 34:  d.extents_min.z = to_double(gr.value); break;
            case 15:  d.extents_max.x = to_double(gr.value); break;
            case 25:  d.extents_max.y = to_double(gr.value); break;
            case 35:  d.extents_max.z = to_double(gr.value); break;
            case 146: d.elevation = to_double(gr.value); break;
            case 13:  d.ucs_origin.x = to_double(gr.value); break;
            case 23:  d.ucs_origin.y = to_double(gr.value); break;
            case 33:  d.ucs_origin.z = to_double(gr.value); break;
            case 16:  d.ucs_x_axis.x = to_double(gr.value); break;
            case 26:  d.ucs_x_axis.y = to_double(gr.value); break;
            case 36:  d.ucs_x_axis.z = to_double(gr.value); break;
            case 17:  d.ucs_y_axis.x = to_double(gr.value); break;
            case 27:  d.ucs_y_axis.y = to_double(gr.value); break;
            case 37:  d.ucs_y_axis.z = to_double(gr.value); break;
            case 76:  d.ucs_ortho_type = to_int(gr.value); break;
            case 330: d.block_record_handle = gr.value; break;
            case 331: d.last_viewport_handle = gr.value; break;
            case 345: d.ucs_handle = gr.value; break;
            case 346: d.base_ucs_handle = gr.value; break;
        }
    }
    return d;
}

DictionaryVarData parse_dictionary_var(const std::vector<DxfGroup>& g, size_t begin,
                                       size_t end) {
    DictionaryVarData d;
    for (size_t i = begin; i < end; ++i) {
        if (g[i].code == 280) d.schema = to_int(g[i].value);
        else if (g[i].code == 1) d.value = g[i].value;
    }
    return d;
}

// The style's own colour (62) comes before its elements (71 on); in them 49 starts one
MlineStyleData parse_mline_style(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    MlineStyleData d;
    bool elements = false;
    for (size_t i = begin; i < end; ++i) {
        const DxfGroup& gr = g[i];
        if (elements) {
            if (gr.code == 49) d.elements.push_back({to_double(gr.value), Color::by_layer(), "BYLAYER"});
            else if (gr.code == 62 && !d.elements.empty()) d.elements.back().color = Color::from_aci(to_int(gr.value));
            else if (gr.code == 6 && !d.elements.empty()) d.elements.back().linetype = gr.value;
            continue;
        }
        switch (gr.code) {
            case 2:  d.name = gr.value; break;
            case 70: d.flags = to_int(gr.value); break;
            case 3:  d.description = gr.value; break;
            case 62: d.fill_color = Color::from_aci(to_int(gr.value)); break;
            case 51: d.start_angle = to_double(gr.value); break;
            case 52: d.end_angle = to_double(gr.value); break;
            case 71: elements = true; break;
        }
    }
    return d;
}

ImageDefData parse_image_def(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    ImageDefData d;
    for (size_t i = begin; i < end; ++i) {
        const DxfGroup& gr = g[i];
        switch (gr.code) {
            case 90:  d.class_version = to_int(gr.value); break;
            case 1:   d.file_name = gr.value; break;
            case 10:  d.size.x = to_double(gr.value); break;
            case 20:  d.size.y = to_double(gr.value); break;
            case 11:  d.pixel_size.x = to_double(gr.value); break;
            case 21:  d.pixel_size.y = to_double(gr.value); break;
            case 280: d.loaded = to_int(gr.value) != 0; break;
            case 281: d.resolution_units = to_int(gr.value); break;
        }
    }
    return d;
}

ImageDefReactorData parse_image_def_reactor(const std::vector<DxfGroup>& g, size_t begin,
                                            size_t end) {
    ImageDefReactorData d;
    for (size_t i = begin; i < end; ++i) {
        if (g[i].code == 90) d.class_version = to_int(g[i].value);
        else if (g[i].code == 330) d.image_handle = g[i].value;
    }
    return d;
}

RasterVariablesData parse_raster_variables(const std::vector<DxfGroup>& g, size_t begin,
                                           size_t end) {
    RasterVariablesData d;
    for (size_t i = begin; i < end; ++i) {
        switch (g[i].code) {
            case 90: d.class_version = to_int(g[i].value); break;
            case 70: d.frame = to_int(g[i].value) != 0; break;
            case 71: d.quality = to_int(g[i].value); break;
            case 72: d.units = to_int(g[i].value); break;
        }
    }
    return d;
}

WipeoutVariablesData parse_wipeout_variables(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    WipeoutVariablesData d;
    for (size_t i = begin; i < end; ++i) {
        if (g[i].code == 70) d.display_frame = to_int(g[i].value);
    }
    return d;
}

ScaleData parse_scale(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    ScaleData d;
    for (size_t i = begin; i < end; ++i) {
        switch (g[i].code) {
            case 70:  d.flags = to_int(g[i].value); break;
            case 300: d.name = g[i].value; break;
            case 140: d.paper_units = to_double(g[i].value); break;
            case 141: d.drawing_units = to_double(g[i].value); break;
            case 290: d.unit_scale = to_int(g[i].value) != 0; break;
        }
    }
    return d;
}

DbColorData parse_db_color(const std::vector<DxfGroup>& g, size_t begin, size_t end) {
    DbColorData d;
    for (size_t i = begin; i < end; ++i) {
        switch (g[i].code) {
            case 62:  d.color.aci = to_int(g[i].value); break;
            case 420: d.color.rgb = to_int(g[i].value); break;
            case 430: d.color.name = g[i].value; break;
        }
    }
    return d;
}

// DBCOLOR's XDATA "ACAD DBColXD <aci> <raw color> <name>" is its color again, as AutoCAD
// writes it from 2004: taken out of xdata, the writer makes it from the color. Anything
// else stays.
void take_db_color_xdata(std::vector<XdataList>& xdata) {
    xdata.erase(std::remove_if(xdata.begin(), xdata.end(), [](const XdataList& x) {
        const auto& it = x.items;
        return x.app_name == "ACAD" && it.size() == 4 && it[0].code == 1000 &&
               it[0].value == "DBColXD" && it[1].code == 1070 && it[2].code == 1071 &&
               it[3].code == 1000;
    }), xdata.end());
}

} // namespace

// ---------------------------------------------------------------------------
// GEODATA (plan phase 4, B50): its three forms, read in order (a code means different things
// where it stands: 11 the reference point or, in version 1, an obsolete one; 12 the north
// direction or a mesh point)
// ---------------------------------------------------------------------------

namespace {

GeoDataData parse_geodata(const std::vector<DxfGroup>& g, size_t body, size_t xdata) {
    GeoDataData d;
    size_t i = body;
    while (i < xdata && !(g[i].code == 100 && g[i].value == "AcDbGeoData")) ++i;
    if (i < xdata) ++i;
    auto code = [&]() { return i < xdata ? g[i].code : -1; };
    auto text = [&]() { return g[i++].value; };
    auto integer = [&]() { return to_int(g[i++].value); };
    auto num = [&]() { return std::atof(g[i++].value.c_str()); };
    auto point3 = [&](int c, Point3& p) {
        if (code() == c) p.x = num();
        if (code() == c + 10) p.y = num();
        if (code() == c + 20) p.z = num();
    };
    auto point2 = [&](int c, Point2& p) {
        if (code() == c) p.x = num();
        if (code() == c + 10) p.y = num();
    };
    if (code() == 90) d.version = integer();
    if (d.version == 1) {
        // 90, 70, 330, the reference point (40 its y, 41 its x, 42 its z), 91, the design point 10,
        // 11 (obsolete), 210, 52, 43-45, 301, 302, 46, 303, 304, 305-307, the mesh, CIVIL3D
        if (code() == 70) d.coordinate_type = integer();
        if (code() == 330) d.host_block_handle = text();
        if (code() == 40) d.reference_point.y = num();
        if (code() == 41) d.reference_point.x = num();
        if (code() == 42) d.reference_point.z = num();
        if (code() == 91) d.horizontal_units = integer();
        point3(10, d.design_point);
        point3(11, d.obsolete_point);
        point3(210, d.up_direction);
        if (code() == 52) d.north_angle = num();
        if (code() == 43) d.obsolete_scale.x = num();
        if (code() == 44) d.obsolete_scale.y = num();
        if (code() == 45) d.obsolete_scale.z = num();
        if (code() == 301) d.coordinate_system = text();
        if (code() == 302) d.geo_rss = text();
        if (code() == 46) d.horizontal_unit_scale = num();
        if (code() == 303) d.datum_name = text();
        if (code() == 304) d.coordinate_system_wkt = text();
    } else {
        if (code() == 330) d.host_block_handle = text();
        if (code() == 70) d.coordinate_type = integer();
        point3(10, d.design_point);
        point3(11, d.reference_point);
        if (code() == 40) d.horizontal_unit_scale = num();
        if (code() == 91) d.horizontal_units = integer();
        if (code() == 41) d.vertical_unit_scale = num();
        if (code() == 92) d.vertical_units = integer();
        point3(210, d.up_direction);
        point2(12, d.north_direction);
        if (code() == 95) d.scale_estimation = integer();
        if (code() == 141) d.user_scale = num();
        if (code() == 294) d.sea_level_correction = integer() != 0;
        if (code() == 142) d.sea_level_elevation = num();
        if (code() == 143) d.projection_radius = num();
        // The coordinate system: 303 parts (version 3), the last in 301
        std::string cs;
        while (code() == 303) cs += text();
        if (code() == 301) cs += text();
        d.coordinate_system = cs;
        if (code() == 302) d.geo_rss = text();
    }
    if (code() == 305) d.observation_from = text();
    if (code() == 306) d.observation_to = text();
    if (code() == 307) d.observation_coverage = text();
    int points = code() == 93 ? integer() : 0;
    int first = d.version == 1 ? 12 : 13;
    for (int k = 0; k < points && code() == first; ++k) {
        GeoMeshPoint p;
        point2(first, p.source);
        point2(first + 1, p.destination);
        d.mesh_points.push_back(p);
    }
    int faces = code() == 96 ? integer() : 0;
    for (int k = 0; k < faces && code() == 97; ++k) {
        GeoMeshFace f;
        f.a = integer();
        if (code() == 98) f.b = integer();
        if (code() == 99) f.c = integer();
        d.mesh_faces.push_back(f);
    }
    // Version 1: CIVIL3D_DATA_BEGIN … _END, with the 2010 values in it
    if (code() == 3) {
        for (; i < xdata; ++i) {
            d.civil3d.push_back(g[i]);
            const DxfGroup& x = g[i];
            switch (x.code) {
                case 95:  d.scale_estimation = to_int(x.value); break;
                case 141: d.user_scale = std::atof(x.value.c_str()); break;
                case 294: d.sea_level_correction = to_int(x.value) != 0; break;
                case 142: d.sea_level_elevation = std::atof(x.value.c_str()); break;
                case 143: d.projection_radius = std::atof(x.value.c_str()); break;
            }
            if (x.code == 4) { ++i; break; }
        }
    }
    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// OBJECTS section
// ---------------------------------------------------------------------------

bool DxfReader::parse_object(const RawRecord& record, Object& out, bool* owner_from_reactors) {
    std::vector<DxfGroup> g;
    if (!decode_groups(record, g)) return false;
    if (g.empty()) return true;

    RecordHead head;
    size_t body = parse_head(g, head);
    size_t xdata = xdata_start(g, body);

    out.handle = head.handle;
    out.owner = head.owner;
    out.reactors = std::move(head.reactors);
    // R13 and R14 as AutoCAD wrote them have no owner group (330) outside the reactor group: the owner
    // is the dictionary that is the first reactor (R13 DXF reference, "Common object group codes"), and
    // the root dictionary, with no reactors, has none ("0")
    // (a provisional choice: parse_objects() settles it when the object has several reactors)
    if (owner_from_reactors) *owner_from_reactors = out.owner.empty();
    if (out.owner.empty()) out.owner = out.reactors.empty() ? "0" : out.reactors.front();
    out.xdictionary = head.xdictionary;
    parse_xdata_groups(g, xdata, out.xdata);

    const std::string& type = g[0].value;
    // A type a phase 9 family has a codec for (TypedRecordTypes.hpp): read by it; a record it cannot read stays
    // as an untyped one below, with a warning, and is written back as it was (B105)
    bool typed = false;
    if (const ObjectCodec* codec = TypedRegistry::instance().find_object(type)) {
        TypedReadContext context;
        context.version = version_;
        context.binary = groups_.binary();
        context.warnings = &warnings_;
        context.class_names = &class_names_;
        typed = typed_read_object(*codec, g, context, out);
        if (!typed) warnings_.push_back(context.error + " (kept as it was read)");
    }
    if (typed) {
        // typed by the family's codec
    } else if (type == "DICTIONARY") {
        out.type = ObjectType::Dictionary;
        out.data = parse_dictionary(g, body, xdata);
    } else if (type == "ACDBDICTIONARYWDFLT") {
        out.type = ObjectType::DictionaryWithDefault;
        out.data = parse_dictionary_with_default(g, body, xdata);
    } else if (type == "XRECORD") {
        out.type = ObjectType::XRecord;
        out.data = parse_xrecord(g, body, xdata, version_ >= DxfVersion::R2000);
    } else if (type == "GROUP") {
        out.type = ObjectType::Group;
        out.data = parse_group(g, body, xdata);
    } else if (type == "LAYOUT") {
        out.type = ObjectType::Layout;
        out.data = parse_layout(g, body, xdata);
    } else if (type == "PLOTSETTINGS") {
        out.type = ObjectType::PlotSettings;
        out.data = parse_plot_settings(g, body, xdata);
    } else if (type == "DICTIONARYVAR") {
        out.type = ObjectType::DictionaryVar;
        out.data = parse_dictionary_var(g, body, xdata);
    } else if (type == "ACDBPLACEHOLDER") {
        out.type = ObjectType::Placeholder;
        out.data = PlaceholderData{};
    } else if (type == "MLINESTYLE") {
        out.type = ObjectType::MlineStyle;
        out.data = parse_mline_style(g, body, xdata);
    } else if (type == "IMAGEDEF") {
        out.type = ObjectType::ImageDef;
        out.data = parse_image_def(g, body, xdata);
    } else if (type == "IMAGEDEF_REACTOR") {
        out.type = ObjectType::ImageDefReactor;
        out.data = parse_image_def_reactor(g, body, xdata);
        // R13 and R14 (family N): an IMAGEDEF_REACTOR has no reactor group and no owner group; the image
        // it is the reactor of (330 of its own subclass) is its owner, as in 2000 and later
        if (owner_from_reactors && *owner_from_reactors && out.reactors.empty()) {
            out.owner = out.as_image_def_reactor().image_handle;
        }
    } else if (type == "RASTERVARIABLES") {
        out.type = ObjectType::RasterVariables;
        out.data = parse_raster_variables(g, body, xdata);
    } else if (type == "SCALE") {
        out.type = ObjectType::Scale;
        out.data = parse_scale(g, body, xdata);
    } else if (type == "WIPEOUTVARIABLES") {
        out.type = ObjectType::WipeoutVariables;
        out.data = parse_wipeout_variables(g, body, xdata);
    } else if (type == "DBCOLOR") {
        out.type = ObjectType::DbColor;
        out.data = parse_db_color(g, body, xdata);
        take_db_color_xdata(out.xdata);
    } else if (type == "GEODATA") {
        out.type = ObjectType::GeoData;
        out.data = parse_geodata(g, body, xdata);
    } else if (type == "MLEADERSTYLE") {
        out.type = ObjectType::MLeaderStyle;
        size_t from = body;
        while (from < xdata && !(g[from].code == 100 && g[from].value == "AcDbMLeaderStyle")) ++from;
        if (from < xdata) ++from;
        MLeaderStyleData d = parse_mleader_style(std::vector<DxfGroup>(g.begin() + static_cast<long>(from),
                                                                       g.begin() + static_cast<long>(xdata)));
        take_mleader_version(out.xdata, d.version);
        out.data = std::move(d);
    } else {
        out.type = ObjectType::Unknown;
        UnknownObjectData d;
        d.object_type = type;
        for (size_t i = body; i < xdata; ++i) d.group_codes.push_back({g[i].code, g[i].value});
        out.data = std::move(d);
    }
    out.raw = record;
    return true;
}

// Every record of the section becomes an Object. The 0/SECTION record is still open
// when this is called; the first code 0 inside the section closes it.
//
// An object without an owner group (R13 and R14 family N) is owned by one of its reactors, and a
// reactor is not always the owner: AutoCAD lists an IMAGEDEF's IMAGEDEF_REACTORs first and the
// ACAD_IMAGE_DICT last. The owner is the reactor that holds the object — a dictionary with the
// object as an entry, or the object whose extension dictionary it is — else the first reactor.
namespace {

std::string upper_handle(const std::string& handle) {
    std::string out = handle;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

void settle_owners_from_reactors(std::vector<Object>& objects, const std::vector<size_t>& derived) {
    std::map<std::string, const Object*> by_handle;
    for (const Object& o : objects) by_handle[upper_handle(o.handle)] = &o;
    for (size_t index : derived) {
        Object& obj = objects[index];
        if (obj.reactors.size() < 2) continue;
        for (const std::string& r : obj.reactors) {
            auto found = by_handle.find(upper_handle(r));
            if (found == by_handle.end()) continue;
            const Object& holder = *found->second;
            bool holds = upper_handle(holder.xdictionary) == upper_handle(obj.handle);
            if (holder.type == ObjectType::Dictionary) {
                for (const auto& entry : holder.as_dictionary().entries) {
                    holds = holds || upper_handle(entry.handle) == upper_handle(obj.handle);
                }
            }
            if (holds) { obj.owner = r; break; }
        }
    }
}

} // namespace

void DxfReader::parse_objects(Drawing& dwg) {
    bool head_taken = false;
    std::vector<size_t> derived;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code != 0) continue;
        RawRecord record = take_record();
        if (!head_taken) {
            head_taken = true;                  // the SECTION marker
        } else if (!record.empty()) {
            Object& obj = dwg.objects_.emplace_back();
            bool from_reactors = false;
            if (!parse_object(record, obj, &from_reactors)) return;
            if (from_reactors) derived.push_back(dwg.objects_.size() - 1);
        }
        if (gc.value == "ENDSEC") break;
    }
    settle_owners_from_reactors(dwg.objects_, derived);
}

// ---------------------------------------------------------------------------
// BLOCK_RECORD
// ---------------------------------------------------------------------------

namespace {

BlockRecord parse_block_record(const std::vector<DxfGroup>& g, std::string& name) {
    BlockRecord r;
    RecordHead head;
    size_t body = parse_head(g, head);
    size_t xdata = xdata_start(g, body);
    r.handle = head.handle;
    r.xdictionary = head.xdictionary;
    for (size_t i = body; i < xdata; ++i) {
        if (g[i].code == 102 && !g[i].value.empty() && g[i].value[0] == '{') {
            bool refs = g[i].value == "{BLKREFS";
            for (++i; i < xdata && g[i].code != 102; ++i) {
                if (refs && g[i].code == 331) r.block_refs.push_back(g[i].value);
            }
            continue;
        }
        switch (g[i].code) {
            case 2:   name = g[i].value; break;
            case 340: r.layout_handle = g[i].value; break;
            case 310:
                for (size_t k = 0; k + 1 < g[i].value.size(); k += 2) {
                    char pair[3] = {g[i].value[k], g[i].value[k + 1], '\0'};
                    r.preview.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
                }
                break;
            case 70:  r.insert_units = to_int(g[i].value); break;
            case 280: r.explodable = to_int(g[i].value); break;
            case 281: r.scalable = to_int(g[i].value); break;
        }
    }
    parse_xdata_groups(g, xdata, r.xdata);
    return r;
}

// A layer's transparency is XDATA "AcCmTransparency 1071 <value>" (plan B22): taken out
// of the list when it is exactly that, and the only AcCmTransparency block — anything
// else stays XDATA as it is. 1071 0, which AutoCAD writes on some layers (ACadSharp's
// sample_AC1018), is no transparency: it stays XDATA, so the layer is written as read
void take_layer_transparency(Layer& layer) {
    auto ours = [](const XdataList& x) { return x.app_name == "AcCmTransparency"; };
    if (std::count_if(layer.xdata.begin(), layer.xdata.end(), ours) != 1) return;
    auto it = std::find_if(layer.xdata.begin(), layer.xdata.end(), ours);
    if (it->items.size() != 1 || it->items[0].code != 1071 || to_int(it->items[0].value) == 0) return;
    layer.transparency.raw = to_int(it->items[0].value);
    layer.xdata.erase(it);
}

// A style's font family is XDATA "ACAD 1000 <family> 1071 <flags>" (rättningsfas R.8):
// taken out of the list when it is exactly that, and the only ACAD block
void take_style_font_family(TextStyle& style) {
    auto ours = [](const XdataList& x) { return x.app_name == "ACAD"; };
    if (std::count_if(style.xdata.begin(), style.xdata.end(), ours) != 1) return;
    auto it = std::find_if(style.xdata.begin(), style.xdata.end(), ours);
    if (it->items.size() != 2 || it->items[0].code != 1000 || it->items[1].code != 1071) return;
    if (it->items[0].value.empty()) return;
    style.font_family = it->items[0].value;
    style.font_flags = to_int(it->items[1].value);
    style.xdata.erase(it);
}

std::string owner_of(const RawRecord& raw) {
    RecordHead head;
    parse_head(raw.groups(), head);
    return head.owner;
}

} // namespace

void DxfReader::read_layer_xdata(Layer& layer) {
    read_entry_head(layer.raw, &layer.xdictionary, nullptr, &layer.xdata);
    take_layer_transparency(layer);
}

void DxfReader::read_style_xdata(TextStyle& style) {
    read_entry_head(style.raw, &style.xdictionary, nullptr, &style.xdata);
    take_style_font_family(style);
}

// A record's extension dictionary, reactors and XDATA (2000+): what of them the caller asks for
void DxfReader::read_entry_head(const RawRecord& raw, std::string* xdictionary, std::vector<std::string>* reactors,
                                std::vector<XdataList>* xdata) {
    if (raw.empty()) return;
    std::vector<DxfGroup> g;
    if (!decode_groups(raw, g)) return;
    RecordHead head;
    size_t body = parse_head(g, head);
    if (xdictionary) *xdictionary = head.xdictionary;
    if (reactors) *reactors = std::move(head.reactors);
    if (xdata) parse_xdata_groups(g, xdata_start(g, body), *xdata);
}

// Each BLOCK_RECORD goes to its block: the one whose BLOCK is owned by it (330), or — if
// no BLOCK names it as owner — the first block with its name. A BLOCK_RECORD that
// belongs to no block stays in raw_tables(), in file order.
void DxfReader::link_block_records(Drawing& dwg) {
    for (auto& table : dwg.raw_tables_) {
        if (table.name != "BLOCK_RECORD") continue;

        std::vector<BlockRecord> records(table.entries.size());
        std::vector<std::string> names(table.entries.size());
        std::vector<bool> is_record(table.entries.size(), false);
        std::unordered_map<uint64_t, size_t> by_handle;
        for (size_t i = 0; i < table.entries.size(); ++i) {
            std::vector<DxfGroup> g;
            if (!decode_groups(table.entries[i], g)) return;
            if (g.empty() || g[0].code != 0 || g[0].value != "BLOCK_RECORD") continue;
            is_record[i] = true;
            records[i] = parse_block_record(g, names[i]);
            records[i].raw = table.entries[i];
            uint64_t h = 0;
            if (parse_handle(records[i].handle, h) == HandleParse::Ok) by_handle.emplace(h, i);
        }

        std::vector<bool> used(table.entries.size(), false);
        auto give = [&](Block& block, size_t i) {
            block.record = std::move(records[i]);
            used[i] = true;
        };
        std::vector<Block*> unowned;
        for (auto& block : dwg.blocks_) {
            uint64_t h = 0;
            auto it = parse_handle(owner_of(block.raw), h) == HandleParse::Ok
                          ? by_handle.find(h) : by_handle.end();
            if (it != by_handle.end() && !used[it->second]) give(block, it->second);
            else unowned.push_back(&block);
        }
        for (Block* block : unowned) {
            for (size_t i = 0; i < table.entries.size(); ++i) {
                if (is_record[i] && !used[i] && names[i] == block->name) {
                    give(*block, i);
                    break;
                }
            }
        }

        std::vector<RawRecord> rest;
        for (size_t i = 0; i < table.entries.size(); ++i) {
            if (!used[i]) rest.push_back(std::move(table.entries[i]));
        }
        table.entries = std::move(rest);
    }
}

// ---------------------------------------------------------------------------
// Pointers to table records as names
// ---------------------------------------------------------------------------
//
// R13+ points to a text style, block or layer by handle where R12 names it. The API keeps
// names for table records (as in R12, and as Entity::props.layer and INSERT already do);
// the writer turns them back into handles. A handle that leads nowhere leaves the field
// as it was.

namespace {

using NameMap = std::unordered_map<uint64_t, std::string>;

template <typename T>
NameMap names_by_handle(const std::vector<T>& records) {
    NameMap m;
    for (const auto& r : records) {
        uint64_t h = 0;
        if (parse_handle(r.handle, h) == HandleParse::Ok) m.emplace(h, r.name);
    }
    return m;
}

bool lookup(const NameMap& m, const std::string& handle, std::string& name) {
    uint64_t h = 0;
    if (parse_handle(handle, h) != HandleParse::Ok) return false;
    auto it = m.find(h);
    if (it == m.end()) return false;
    name = it->second;
    return true;
}

// The AcCmColor AutoCAD 2000/2004 write as binary XDATA (1004) for DIMTFILLCLR: 24
// bytes, the color in bytes 8-11 as a little-endian 0xMMRRGGBB — the method (0xC0
// ByLayer, 0xC1 ByBlock, 0xC2 RGB, 0xC3 ACI) and its value. ACadSharp's sample_AC1018:
// 0xC2367542, which AutoCAD writes in 2007 as ACI 97, the nearest to RGB 0x367542.
bool color_from_binary(const std::string& hex, Color& out) {
    if (hex.size() != 48) return false;
    uint8_t b[24];
    for (size_t i = 0; i < 24; ++i) {
        int v = 0;
        for (size_t k = 0; k < 2; ++k) {
            char c = hex[2 * i + k];
            int d = c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0) return false;
            v = v * 16 + d;
        }
        b[i] = static_cast<uint8_t>(v);
    }
    switch (b[11]) {
        case 0xC0: out = Color::by_layer(); return true;
        case 0xC1: out = Color::by_block(); return true;
        case 0xC2: out = Color::from_rgb(b[10], b[9], b[8]); return true;
        case 0xC3: out = Color::from_aci(b[8]); return true;
        default: return false;
    }
}

// A DIMSTYLE's XDATA as AutoCAD 2000/2004 keep the newer variables: each in its own
// application, 1070 <the variable's number>, then its value. What is exactly that goes to
// the fields; the rest stays.
void take_dimstyle_xdata(DimStyle& ds, const NameMap& linetypes) {
    for (auto it = ds.xdata.begin(); it != ds.xdata.end();) {
        const std::vector<XdataItem>& x = it->items;
        auto key = [&](size_t i, int number) {
            return i < x.size() && x[i].code == 1070 && to_int(x[i].value) == number;
        };
        auto value = [&](int number, int code) { return x.size() == 2 && key(0, number) && x[1].code == code; };
        const std::string& app = it->app_name;
        bool taken = false;
        if (app == "ACAD_DSTYLE_DIMTEXT_FILL" && x.size() == 4 && key(0, 376) && x[1].code == 1070 &&
            key(2, 377) && x[3].code == 1004) {
            Color c;
            if (color_from_binary(x[3].value, c)) {
                ds.dimtfill = to_int(x[1].value);
                ds.dimtfillclr = c;
                taken = true;
            }
        } else if (app == "ACAD_DSTYLE_DIM_LINETYPE" && value(380, 1005)) {
            taken = lookup(linetypes, x[1].value, ds.dimltype);
        } else if (app == "ACAD_DSTYLE_DIM_EXT1_LINETYPE" && value(381, 1005)) {
            taken = lookup(linetypes, x[1].value, ds.dimltex1);
        } else if (app == "ACAD_DSTYLE_DIM_EXT2_LINETYPE" && value(382, 1005)) {
            taken = lookup(linetypes, x[1].value, ds.dimltex2);
        } else if (app == "ACAD_DSTYLE_DIMEXT_ENABLED" && value(383, 1070)) {
            ds.dimfxlon = to_int(x[1].value);
            taken = true;
        } else if (app == "ACAD_DSTYLE_DIMEXT_LENGTH" && value(378, 1040)) {
            ds.dimfxl = to_double(x[1].value);
            taken = true;
        } else if (app == "ACAD_DSTYLE_DIMJOGGED_JOGANGLE_SYMBOL" && value(384, 1040)) {
            ds.dimjogang = to_double(x[1].value) * 180.0 / 3.14159265358979323846;
            taken = true;
        }
        it = taken ? ds.xdata.erase(it) : it + 1;
    }
}

} // namespace

void DxfReader::resolve_names(Drawing& dwg) {
    NameMap styles = names_by_handle(dwg.text_styles_);
    NameMap layers = names_by_handle(dwg.layers_);
    NameMap ucs = names_by_handle(dwg.ucs_list_);

    // 345 (named UCS) and 346 (base UCS) of VPORT, VIEW, UCS and VIEWPORT
    auto ucs_names = [&](const RawRecord& raw, std::string* named, std::string& base,
                         const char* subclass_only) {
        std::string subclass;
        for (const auto& g : raw.groups()) {
            if (g.code == 100) subclass = g.value;
            if (subclass_only && subclass != subclass_only) continue;
            if (g.code == 345 && named) lookup(ucs, g.value, *named);
            if (g.code == 346) lookup(ucs, g.value, base);
        }
    };
    for (auto& vp : dwg.vports_) if (!vp.raw.empty()) ucs_names(vp.raw, &vp.ucs, vp.base_ucs, nullptr);
    for (auto& v : dwg.views_) if (!v.raw.empty()) ucs_names(v.raw, &v.ucs, v.base_ucs, nullptr);
    for (auto& u : dwg.ucs_list_) if (!u.raw.empty()) ucs_names(u.raw, nullptr, u.base_ucs, nullptr);
    NameMap blocks;
    for (const auto& b : dwg.blocks_) {
        uint64_t h = 0;
        if (parse_handle(b.record.handle, h) == HandleParse::Ok) blocks.emplace(h, b.name);
    }

    NameMap linetypes = names_by_handle(dwg.linetypes_);
    for (auto& ds : dwg.dim_styles_) {
        if (ds.raw.empty()) continue;
        for (const auto& g : ds.raw.groups()) {
            switch (g.code) {
                case 340: lookup(styles, g.value, ds.dimtxsty); break;
                case 341: lookup(blocks, g.value, ds.dimldrblk); break;
                case 342: lookup(blocks, g.value, ds.dimblk); break;
                case 343: lookup(blocks, g.value, ds.dimblk1); break;
                case 344: lookup(blocks, g.value, ds.dimblk2); break;
                case 345: lookup(linetypes, g.value, ds.dimltype); break;   // 2007+
                case 346: lookup(linetypes, g.value, ds.dimltex1); break;
                case 347: lookup(linetypes, g.value, ds.dimltex2); break;
            }
        }
        std::vector<DxfGroup> g;
        if (!decode_groups(ds.raw, g)) continue;
        RecordHead head;
        size_t body = parse_head(g, head);
        parse_xdata_groups(g, xdata_start(g, body), ds.xdata);
        take_dimstyle_xdata(ds, linetypes);
        ds.reactors = std::move(head.reactors);
    }

    // LTYPE: 340 belongs to the element of the last dash length (49) before it. A shape
    // file's style has no name (70 bit 1; ACadSharp's ltypeshp.shx): the element names it by
    // its file
    NameMap element_styles = styles;
    for (const auto& s : dwg.text_styles_) {
        uint64_t h = 0;
        if (s.name.empty() && (s.flags & 1) && parse_handle(s.handle, h) == HandleParse::Ok) {
            element_styles[h] = s.font_file;
        }
    }
    for (auto& lt : dwg.linetypes_) {
        read_entry_head(lt.raw, nullptr, nullptr, &lt.xdata);
        if (lt.raw.empty() || lt.elements.empty()) continue;
        size_t index = 0;
        for (const auto& g : lt.raw.groups()) {
            if (g.code == 49) ++index;
            if (g.code == 340 && index > 0 && index <= lt.elements.size()) {
                lookup(element_styles, g.value, lt.elements[index - 1].style);
            }
        }
    }
    for (auto& v : dwg.views_) read_entry_head(v.raw, &v.xdictionary, nullptr, nullptr);
    for (auto& vp : dwg.vports_) read_entry_head(vp.raw, &vp.xdictionary, nullptr, &vp.xdata);

    // VIEWPORT: frozen layers (331) and its UCS (345, 346), in AcDbViewport
    auto viewports = [&](std::vector<Entity>& entities) {
        for (auto& e : entities) {
            if (e.type != EntityType::Viewport || e.raw.empty()) continue;
            ViewportData& vd = e.as_viewport();
            ucs_names(e.raw, &vd.ucs, vd.base_ucs, "AcDbViewport");
            std::string subclass;
            for (const auto& g : e.raw.groups()) {
                if (g.code == 100) subclass = g.value;
                std::string name;
                if (g.code == 331 && subclass == "AcDbViewport" && lookup(layers, g.value, name)) {
                    e.as_viewport().frozen_layers.push_back(name);
                }
            }
        }
    };
    viewports(dwg.entities_);
    for (auto& b : dwg.blocks_) viewports(b.entities);

    // MULTILEADER and MLEADERSTYLE: their table records and blocks by name (a handle that
    // leads nowhere: none); a MULTILEADER's style by its name in ACAD_MLEADERSTYLE
    auto name_of = [](const NameMap& m, std::string& field) {
        std::string handle = field;
        field.clear();
        lookup(m, handle, field);
    };
    NameMap mleader_styles;
    for (const auto& o : dwg.objects_) {
        if (o.type != ObjectType::Dictionary) continue;
        bool style_dictionary = false;
        for (const auto& root : dwg.objects_) {
            if (root.type != ObjectType::Dictionary || root.owner != "0") continue;
            for (const auto& e : root.as_dictionary().entries) {
                style_dictionary = style_dictionary || (e.name == "ACAD_MLEADERSTYLE" && e.handle == o.handle);
            }
        }
        if (!style_dictionary) continue;
        for (const auto& e : o.as_dictionary().entries) {
            uint64_t h = 0;
            if (parse_handle(e.handle, h) == HandleParse::Ok) mleader_styles.emplace(h, e.name);
        }
    }
    for (auto& o : dwg.objects_) {
        if (o.type != ObjectType::MLeaderStyle) continue;
        MLeaderStyleData& s = o.as_mleader_style();
        if (!s.leader_linetype.empty()) name_of(linetypes, s.leader_linetype);
        if (!s.arrow_block.empty()) name_of(blocks, s.arrow_block);
        if (!s.text_style.empty()) name_of(styles, s.text_style);
        if (!s.block.empty()) name_of(blocks, s.block);
    }
    auto mleaders = [&](std::vector<Entity>& entities) {
        for (auto& e : entities) {
            if (e.type != EntityType::MLeader) continue;
            MLeaderData& d = e.as_mleader();
            d.style.clear();
            lookup(mleader_styles, d.style_handle, d.style);
            name_of(linetypes, d.leader_linetype);
            if (!d.arrow_block.empty()) name_of(blocks, d.arrow_block);
            name_of(styles, d.text_style);
            if (!d.block.empty()) name_of(blocks, d.block);
            for (auto& a : d.arrowheads) name_of(blocks, a.block);
            if (!d.context.text_style.empty()) name_of(styles, d.context.text_style);
            if (!d.context.block.empty()) name_of(blocks, d.context.block);
            for (auto& leader : d.context.leaders) {
                for (auto& line : leader.lines) {
                    if (line.linetype) name_of(linetypes, *line.linetype);
                    if (line.arrow_block) name_of(blocks, *line.arrow_block);
                }
            }
        }
    };
    mleaders(dwg.entities_);
    for (auto& b : dwg.blocks_) mleaders(b.entities);
}

} // namespace internal
} // namespace hdxf
