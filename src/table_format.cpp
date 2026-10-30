#include "TableTypes.hpp"

// The format parts of the table records and the two style objects (phase 9, step 9.1): CONTENTFORMAT,
// CELLMARGIN, GRIDFORMAT and TABLEFORMAT (shared by CELLSTYLEMAP and TABLECONTENT), TABLESTYLE and
// CELLSTYLEMAP. The order of the groups is AutoCAD's own (doc/facit/acad_*_dxf_tables.dxf, ACadSharp's
// geoloc and sample_AC1015 … AC1032, Oxcart's files), the TABLESTYLE group codes of the DXF reference where
// they show nothing more.

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// Colors in the format groups: 62 and, when the color has them, 420 and 430
// ---------------------------------------------------------------------------

bool read_format_color(GroupCursor& c, Color& out) {
    int32_t aci = 0;
    if (!c.require_integer(62, aci)) return false;
    out = Color::from_aci(aci);
    int32_t rgb = 0;
    if (c.integer(420, rgb)) out.rgb = rgb;
    std::string name;
    if (c.string(430, name)) out.name = name;
    return c.ok();
}

void write_format_color(RecordWriter& w, const Color& color) {
    w.integer(62, color.aci);
    if (color.has_rgb()) w.integer(420, color.rgb);
    if (!color.name.empty()) w.text(430, color.name);
}

// ---------------------------------------------------------------------------
// CONTENTFORMAT
// ---------------------------------------------------------------------------

bool read_content_format(GroupCursor& c, TableContentFormat& out) {
    out = TableContentFormat();
    if (!require_marker(c, 1, "CONTENTFORMAT_BEGIN")) return false;
    if (!c.require_integer(90, out.property_flags) || !c.require_integer(91, out.extended_flags) ||
        !c.require_integer(92, out.data_type) || !c.require_integer(93, out.unit_type) ||
        !c.require_string(300, out.format_string) || !c.require_real(40, out.rotation) ||
        !c.require_real(140, out.scale) || !c.require_integer(94, out.alignment) ||
        !read_format_color(c, out.color) || !c.require_handle(340, out.text_style_handle) ||
        !c.require_real(144, out.text_height)) {
        return false;
    }
    return require_marker(c, 309, "CONTENTFORMAT_END");
}

void write_content_format(RecordWriter& w, const TableContentFormat& f) {
    w.text(1, "CONTENTFORMAT_BEGIN");
    w.integer(90, f.property_flags);
    w.integer(91, f.extended_flags);
    w.integer(92, f.data_type);
    w.integer(93, f.unit_type);
    w.text(300, f.format_string);
    w.real(40, f.rotation);
    w.real(140, f.scale);
    w.integer(94, f.alignment);
    write_format_color(w, f.color);
    w.pointer(340, f.text_style_handle.empty() ? std::string("0") : f.text_style_handle, PointerWrite::ZeroDead);
    w.real(144, f.text_height);
    w.text(309, "CONTENTFORMAT_END");
}

// ---------------------------------------------------------------------------
// TABLEFORMAT with its margins and grid formats
// ---------------------------------------------------------------------------

namespace {

bool read_cell_margins(GroupCursor& c, TableCellMargins& out) {
    if (!require_marker(c, 301, "MARGIN") || !require_marker(c, 1, "CELLMARGIN_BEGIN")) return false;
    if (!c.require_real(40, out.top) || !c.require_real(40, out.left) || !c.require_real(40, out.bottom) ||
        !c.require_real(40, out.right) || !c.require_real(40, out.spacing_1) || !c.require_real(40, out.spacing_2)) {
        return false;
    }
    return require_marker(c, 309, "CELLMARGIN_END");
}

void write_cell_margins(RecordWriter& w, const TableCellMargins& m) {
    w.text(301, "MARGIN");
    w.text(1, "CELLMARGIN_BEGIN");
    w.real(40, m.top);
    w.real(40, m.left);
    w.real(40, m.bottom);
    w.real(40, m.right);
    w.real(40, m.spacing_1);
    w.real(40, m.spacing_2);
    w.text(309, "CELLMARGIN_END");
}

bool read_grid_format(GroupCursor& c, TableGridFormat& out) {
    if (!c.require_integer(95, out.edge) || !require_marker(c, 302, "GRIDFORMAT") ||
        !require_marker(c, 1, "GRIDFORMAT_BEGIN")) {
        return false;
    }
    if (!c.require_integer(90, out.property_flags) || !c.require_integer(91, out.kind) ||
        !read_format_color(c, out.color) || !c.require_integer(92, out.lineweight) ||
        !c.require_handle(340, out.linetype_handle) || !c.require_integer(93, out.visibility) ||
        !c.require_real(40, out.double_line_spacing)) {
        return false;
    }
    return require_marker(c, 309, "GRIDFORMAT_END");
}

void write_grid_format(RecordWriter& w, const TableGridFormat& g) {
    w.integer(95, g.edge);
    w.text(302, "GRIDFORMAT");
    w.text(1, "GRIDFORMAT_BEGIN");
    w.integer(90, g.property_flags);
    w.integer(91, g.kind);
    write_format_color(w, g.color);
    w.integer(92, g.lineweight);
    w.pointer(340, g.linetype_handle.empty() ? std::string("0") : g.linetype_handle, PointerWrite::ZeroDead);
    w.integer(93, g.visibility);
    w.real(40, g.double_line_spacing);
    w.text(309, "GRIDFORMAT_END");
}

} // namespace

bool read_table_format(GroupCursor& c, TableFormatData& out) {
    out = TableFormatData();
    if (!require_marker(c, 1, "TABLEFORMAT_BEGIN")) return false;
    if (!c.require_integer(90, out.kind) || !c.require_boolean(170, out.has_format)) return false;
    if (out.has_format) {
        if (!c.require_integer(91, out.property_flags) || !c.require_integer(92, out.merge_flags) ||
            !read_format_color(c, out.background) || !c.require_integer(93, out.flags) ||
            !require_marker(c, 300, "CONTENTFORMAT") || !read_content_format(c, out.content) ||
            !c.require_boolean(171, out.has_margins)) {
            return false;
        }
        if (out.has_margins && !read_cell_margins(c, out.margins)) return false;
        size_t grids = 0;
        if (!c.count(94, 10, grids)) {
            if (c.ok()) c.fail("group 94 (the number of grid formats) is missing");
            return false;
        }
        for (size_t i = 0; i < grids; ++i) {
            TableGridFormat g;
            if (!read_grid_format(c, g)) return false;
            out.grids.push_back(std::move(g));
        }
    }
    return require_marker(c, 309, "TABLEFORMAT_END");
}

void write_table_format(RecordWriter& w, const TableFormatData& f) {
    w.text(1, "TABLEFORMAT_BEGIN");
    w.integer(90, f.kind);
    w.boolean(170, f.has_format);
    if (f.has_format) {
        w.integer(91, f.property_flags);
        w.integer(92, f.merge_flags);
        write_format_color(w, f.background);
        w.integer(93, f.flags);
        w.text(300, "CONTENTFORMAT");
        write_content_format(w, f.content);
        w.boolean(171, f.has_margins);
        if (f.has_margins) write_cell_margins(w, f.margins);
        w.integer(94, static_cast<int64_t>(f.grids.size()));
        for (const TableGridFormat& g : f.grids) write_grid_format(w, g);
    }
    w.text(309, "TABLEFORMAT_END");
}

// ---------------------------------------------------------------------------
// TABLESTYLE
// ---------------------------------------------------------------------------

namespace {

bool read_table_style_row(GroupCursor& c, TableStyleRow& row) {
    std::string text;
    if (c.string(7, text)) row.text_style = text;
    if (!c.require_real(140, row.text_height) || !c.require_integer(170, row.alignment) ||
        !c.require_integer(62, row.text_color) || !c.require_integer(63, row.fill_color) ||
        !c.require_boolean(283, row.fill_enabled)) {
        return false;
    }
    int32_t v = 0;
    if (c.integer(90, v)) row.data_type = v;
    if (c.integer(91, v)) row.unit_type = v;
    if (c.string(1, text)) row.format = text;
    for (int i = 0; i < 6; ++i) {
        if (!c.require_integer(274 + i, row.borders[i].lineweight) || !c.require_boolean(284 + i, row.borders[i].visible) ||
            !c.require_integer(64 + i, row.borders[i].color)) {
            return false;
        }
    }
    return c.ok();
}

void write_table_style_row(RecordWriter& w, const TableStyleRow& row) {
    if (row.text_style) w.text(7, *row.text_style);
    w.real(140, row.text_height);
    w.integer(170, row.alignment);
    w.integer(62, row.text_color);
    w.integer(63, row.fill_color);
    w.boolean(283, row.fill_enabled);
    if (row.data_type) w.integer(90, *row.data_type);
    if (row.unit_type) w.integer(91, *row.unit_type);
    if (row.format) w.text(1, *row.format);
    for (int i = 0; i < 6; ++i) {
        w.integer(274 + i, row.borders[i].lineweight);
        w.boolean(284 + i, row.borders[i].visible);
        w.integer(64 + i, row.borders[i].color);
    }
}

bool read_table_style(const RecordView& view, TypedReadContext& context, Object& o) {
    if (context.version < DxfVersion::R2000) {
        return context.fail("TABLESTYLE in DXF R13/R14 is not established (no AutoCAD file has one): kept as it was read");
    }
    if (!view.find_subclass("AcDbTableStyle")) {
        // only the head: no subclass to read, the defaults (a cut-off record has some subclasses and not the others, B105)
        o.data = Boxed<TableStyleData>(TableStyleData());
        return true;
    }
    GroupCursor c(view, "AcDbTableStyle");
    TableStyleData d;
    int16_t v = 0;
    if (c.code() == 280 && c.integer(280, v)) d.class_version = v;
    std::string text;
    if (c.string(3, text)) d.description = text;
    if (!c.require_integer(70, d.flow_direction) || !c.require_integer(71, d.flags) ||
        !c.require_real(40, d.horizontal_margin) || !c.require_real(41, d.vertical_margin) ||
        !c.require_boolean(280, d.title_suppressed) || !c.require_boolean(281, d.header_suppressed)) {
        return context.fail(c.error());
    }
    for (int i = 0; i < 3; ++i) {
        if (!read_table_style_row(c, d.rows[i])) return context.fail(c.error());
    }
    if (!c.at_end()) {
        c.fail("group " + std::to_string(c.code()) + " after the row styles is not part of any known TABLESTYLE");
        return context.fail(c.error());
    }
    o.data = Boxed<TableStyleData>(std::move(d));
    return true;
}

void write_table_style(RecordWriter& w, const Object& o) {
    if (w.older()) {
        w.fail("TABLESTYLE in DXF R13/R14 is not established (no AutoCAD file has one)");
        return;
    }
    const TableStyleData& d = o.as_table_style();
    w.object_head("TABLESTYLE", o);
    w.subclass("AcDbTableStyle");
    if (d.class_version) w.integer(280, *d.class_version);
    if (d.description) w.text(3, *d.description);
    w.integer(70, d.flow_direction);
    w.integer(71, d.flags);
    w.real(40, d.horizontal_margin);
    w.real(41, d.vertical_margin);
    w.boolean(280, d.title_suppressed);
    w.boolean(281, d.header_suppressed);
    for (int i = 0; i < 3; ++i) write_table_style_row(w, d.rows[i]);
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// CELLSTYLEMAP
// ---------------------------------------------------------------------------

bool read_cell_style_map(const RecordView& view, TypedReadContext& context, Object& o) {
    if (context.version < DxfVersion::R2000) {
        return context.fail("CELLSTYLEMAP in DXF R13/R14 is not established (no AutoCAD file has one): kept as it was read");
    }
    if (!view.find_subclass("AcDbCellStyleMap")) {
        o.data = Boxed<CellStyleMapData>(CellStyleMapData());       // only the head: the defaults
        return true;
    }
    GroupCursor c(view, "AcDbCellStyleMap");
    CellStyleMapData d;
    size_t n = 0;
    if (!c.count(90, 8, n)) {
        if (c.ok()) c.fail("group 90 (the number of cell styles) is missing");
        return context.fail(c.error());
    }
    for (size_t i = 0; i < n; ++i) {
        TableCellStyle style;
        if (!require_marker(c, 300, "CELLSTYLE") || !read_table_format(c, style.format) ||
            !require_marker(c, 1, "CELLSTYLE_BEGIN") || !c.require_integer(90, style.id) ||
            !c.require_integer(91, style.kind) || !c.require_string(300, style.name) ||
            !require_marker(c, 309, "CELLSTYLE_END")) {
            return context.fail(c.error());
        }
        d.styles.push_back(std::move(style));
    }
    if (!c.at_end()) {
        c.fail("group " + std::to_string(c.code()) + " after the cell styles is not part of any known CELLSTYLEMAP");
        return context.fail(c.error());
    }
    o.data = Boxed<CellStyleMapData>(std::move(d));
    return true;
}

void write_cell_style_map(RecordWriter& w, const Object& o) {
    if (w.older()) {
        w.fail("CELLSTYLEMAP in DXF R13/R14 is not established (no AutoCAD file has one)");
        return;
    }
    const CellStyleMapData& d = o.as_cell_style_map();
    w.object_head("CELLSTYLEMAP", o);
    w.subclass("AcDbCellStyleMap");
    w.integer(90, static_cast<int64_t>(d.styles.size()));
    for (const TableCellStyle& style : d.styles) {
        w.text(300, "CELLSTYLE");
        write_table_format(w, style.format);
        w.text(1, "CELLSTYLE_BEGIN");
        w.integer(90, style.id);
        w.integer(91, style.kind);
        w.text(300, style.name);
        w.text(309, "CELLSTYLE_END");
    }
    w.xdata(o.xdata);
}

// The version cells of the table family: R12 has no table (named error), R13/R14 are not established
// (AutoCAD 2025 cannot save them and no file has one), 2000 … 2018 are native, verified by AutoCAD's own
// files in both formats (doc/facit/acad_*_dxf_tables.dxf)
VersionMatrix table_family_versions() {
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        m.set(v, Generation::Native, ErrorBasis::NotApplicable);
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

ClassSpec table_class(const char* cpp_name, int flags) {
    ClassSpec cls;
    cls.cpp_name = cpp_name;
    cls.application = "ObjectDBX Classes";
    cls.proxy_flags = flags;
    cls.in_older = false;
    return cls;
}

// TABLESTYLE and CELLSTYLEMAP are written whole as their subclass is: every group of the subclass is one list (B12),
// a change to one value writes the subclass anew from the typed values, an unchanged style as it was
ListSpec whole(const char* subclass, int id = 1) {
    ListSpec s;
    s.subclass = subclass;
    s.id = id;
    s.shape = ListShape::WholeSubclass;
    return s;
}

} // namespace

VersionMatrix table_versions() { return table_family_versions(); }
ClassSpec table_class_spec(const char* cpp_name, int flags) { return table_class(cpp_name, flags); }
ListSpec table_whole_subclass(const char* subclass, int id) { return whole(subclass, id); }

void register_table_style_codecs(TypedRegistry& registry) {
    ObjectCodec style;
    style.dxf_name = "TABLESTYLE";
    style.type = ObjectType::TableStyle;
    style.read = read_table_style;
    style.write = write_table_style;
    style.cls = table_class("AcDbTableStyle", 4095);
    style.versions = table_family_versions();
    style.lists.push_back(whole("AcDbTableStyle"));
    style.step = 1;
    registry.add(std::move(style));

    ObjectCodec map;
    map.dxf_name = "CELLSTYLEMAP";
    map.type = ObjectType::CellStyleMap;
    map.read = read_cell_style_map;
    map.write = write_cell_style_map;
    map.cls = table_class("AcDbCellStyleMap", 1152);
    map.versions = table_family_versions();
    map.lists.push_back(whole("AcDbCellStyleMap"));
    map.step = 1;
    registry.add(std::move(map));
}

} // namespace internal
} // namespace hdxf
