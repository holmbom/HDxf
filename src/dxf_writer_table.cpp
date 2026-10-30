#include "TableTypes.hpp"
#include "TableLayout.hpp"
#include "extents.hpp"

// ACAD_TABLE (phase 9, step 9.1): the table entity, read into TableData and written from it, in the forms of
// AutoCAD's own files — 2000 and 2004 (the cell's text in 1, its override flag 177, the overrides after the
// content), 2007 (91 and the AcValue of 301 CELL_VALUE … 304 ACVALUE_END, the overrides before it) and 2010 and
// later (a 280 version number after AcDbTable). The files: doc/facit/acad_*_dxf_tables.dxf and ACadSharp's
// sample_AC1015 … AC1032 (plan B.3 F1 … F8, F12, F15), the DXF reference's Table group codes where they show
// nothing more.

namespace hdxf {
namespace internal {

namespace {

// ---- the cell's overrides, in AutoCAD's order: 170, 63 (421), 64, 283, 7, 140, then the borders right, bottom,
// left, top as (lineweight, color, visibility). 170, 63, 64, 283 and 140 are the order AutoCAD's files show;
// the 7 and the bottom border and the visibilities have no AutoCAD file (ej testat, F9.5).

struct EdgeCodes {
    int lineweight;
    int color;
    int visible;
};
constexpr EdgeCodes kRight = {275, 65, 285};
constexpr EdgeCodes kBottom = {276, 66, 286};
constexpr EdgeCodes kLeft = {278, 68, 288};
constexpr EdgeCodes kTop = {279, 69, 289};

bool read_edge(GroupCursor& c, const EdgeCodes& codes, TableEdgeOverride& out) {
    int16_t v = 0;
    bool b = false;
    if (c.integer(codes.lineweight, v)) out.lineweight = v;
    if (c.integer(codes.color, v)) out.color = v;
    if (c.boolean(codes.visible, b)) out.visible = b;
    return c.ok();
}

void write_edge(RecordWriter& w, const EdgeCodes& codes, const TableEdgeOverride& e) {
    if (e.lineweight) w.integer(codes.lineweight, *e.lineweight);
    if (e.color) w.integer(codes.color, *e.color);
    if (e.visible) w.boolean(codes.visible, *e.visible);
}

bool read_overrides(GroupCursor& c, TableCellOverrides& o) {
    int16_t i16 = 0;
    bool b = false;
    double d = 0.0;
    std::string s;
    if (c.integer(170, i16)) o.alignment = i16;
    if (c.code() == 63) {
        Color color;
        int32_t aci = 0;
        c.integer(63, aci);
        color = Color::from_aci(aci);
        int32_t rgb = 0;
        if (c.integer(421, rgb)) color.rgb = rgb;
        o.fill_color = color;
    }
    int32_t aci = 0;
    if (c.integer(64, aci)) o.content_color = Color::from_aci(aci);
    if (c.boolean(283, b)) o.fill_enabled = b;
    if (c.string(7, s)) o.text_style = s;
    if (c.real(140, d)) o.text_height = d;
    return read_edge(c, kRight, o.right) && read_edge(c, kBottom, o.bottom) && read_edge(c, kLeft, o.left) &&
           read_edge(c, kTop, o.top);
}

void write_overrides(RecordWriter& w, const TableCellOverrides& o) {
    if (o.alignment) w.integer(170, *o.alignment);
    if (o.fill_color) {
        w.integer(63, o.fill_color->aci);
        if (o.fill_color->has_rgb()) w.integer(421, o.fill_color->rgb);
    }
    if (o.content_color) w.integer(64, o.content_color->aci);
    if (o.fill_enabled) w.boolean(283, *o.fill_enabled);
    if (o.text_style) w.text(7, *o.text_style);
    if (o.text_height) w.real(140, *o.text_height);
    write_edge(w, kRight, o.right);
    write_edge(w, kBottom, o.bottom);
    write_edge(w, kLeft, o.left);
    write_edge(w, kTop, o.top);
}

// The bits of a cell's override flag that belong to one group each (AutoCAD's files: 170 is 0x1, 63 with 283 are
// 0x2 and 0x4, 64 is 0x8, 140 is 0x20); the other bits are kept as they are
int32_t cell_flags_with_overrides(int32_t flags, const TableCellOverrides& o) {
    auto set = [&](int32_t bit, bool on) { flags = on ? (flags | bit) : (flags & ~bit); };
    set(0x1, o.alignment.has_value());
    set(0x2, o.fill_color.has_value());
    set(0x4, o.fill_enabled.has_value());
    set(0x8, o.content_color.has_value());
    set(0x20, o.text_height.has_value());
    return flags;
}

// ---- the cell

// DXF 2007 and later: 91, field or block, overrides, 92, 301 CELL_VALUE … ; before: 177, the content, the overrides
bool read_cell(GroupCursor& c, TableCell& cell) {
    int16_t type = 0;
    if (!c.require_integer(171, type) || !c.require_integer(172, cell.flags) || !c.require_integer(173, cell.merged) ||
        !c.require_boolean(174, cell.auto_fit) || !c.require_integer(175, cell.merge_columns) ||
        !c.require_integer(176, cell.merge_rows)) {
        return false;
    }
    if (type != 1 && type != 2) {
        c.fail("a table cell of type " + std::to_string(type) + " is not established");
        return false;
    }
    cell.type = static_cast<TableCellType>(type);
    const bool full = c.code() == 91;
    if (full ? !c.require_integer(91, cell.override_flags) : !c.require_integer(177, cell.override_flags)) return false;
    if (!c.require_integer(178, cell.virtual_edge) || !c.require_real(145, cell.rotation)) return false;

    auto read_block = [&]() {
        size_t attributes = 0;
        if (!c.require_handle(340, cell.block_handle) || !c.require_real(144, cell.block_scale) ||
            !c.count(179, 2, attributes)) {
            if (c.ok()) c.fail("group 179 (the number of attribute values of a block cell) is missing");
            return false;
        }
        for (size_t i = 0; i < attributes; ++i) {
            TableAttributeValue a;
            if (!c.require_handle(331, a.attdef_handle) || !c.require_string(300, a.text)) return false;
            cell.attributes.push_back(std::move(a));
        }
        return true;
    };

    if (full) {
        // the field or the block, the overrides (AutoCAD's 170 of a block cell follows its 179), 92 and the AcValue
        if (c.code() == 344) c.handle(344, cell.field_handle);
        if (c.code() == 340 && !read_block()) return false;
        if (!read_overrides(c, cell.overrides)) return false;
        if (!c.require_integer(92, cell.extended_flags) || !require_marker(c, 301, "CELL_VALUE")) return false;
        return read_ac_value(c, true, cell.value);
    }
    // Before 2007: a field, a block with its attribute values, or the text (in pieces of 250 characters)
    if (c.code() == 344) {
        c.handle(344, cell.field_handle);
    } else if (c.code() == 340) {
        if (!read_block()) return false;
    } else {
        std::string text;
        if (read_chunked_text(c, 2, 1, text) && !text.empty()) cell.value = TableValue::from_text(text);
        if (!c.ok()) return false;
    }
    return read_overrides(c, cell.overrides);
}

void write_cell(RecordWriter& w, const TableCell& cell, bool full) {
    w.integer(171, static_cast<int16_t>(cell.type));
    w.integer(172, cell.flags);
    w.integer(173, cell.merged);
    w.boolean(174, cell.auto_fit);
    w.integer(175, cell.merge_columns);
    w.integer(176, cell.merge_rows);
    const int32_t flags = cell_flags_with_overrides(cell.override_flags, cell.overrides);
    w.integer(full ? 91 : 177, flags);
    w.integer(178, cell.virtual_edge);
    w.real(145, cell.rotation);

    auto write_block = [&]() {
        w.pointer(340, cell.block_handle.empty() ? std::string("0") : cell.block_handle, PointerWrite::ZeroDead);
        w.real(144, cell.block_scale);
        std::vector<const TableAttributeValue*> live;
        for (const TableAttributeValue& a : cell.attributes) if (w.live(a.attdef_handle)) live.push_back(&a);
        w.integer(179, static_cast<int64_t>(live.size()));
        for (const TableAttributeValue* a : live) {
            w.pointer(331, a->attdef_handle, PointerWrite::AsIs);
            w.text(300, a->text);
        }
    };

    if (full) {
        if (!cell.field_handle.empty()) w.pointer(344, cell.field_handle, PointerWrite::ZeroDead);
        if (cell.type == TableCellType::Block) write_block();
        write_overrides(w, cell.overrides);
        w.integer(92, cell.extended_flags);
        w.text(301, "CELL_VALUE");
        write_ac_value(w, true, cell.value);
        return;
    }
    if (!cell.field_handle.empty()) {
        w.pointer(344, cell.field_handle, PointerWrite::ZeroDead);
    } else if (cell.type == TableCellType::Block) {
        write_block();
    } else {
        write_chunked_text(w, 2, 1, table_value_text(cell.value));
    }
    write_overrides(w, cell.overrides);
}

// ---- the table's own overrides: 93 … 96, then 70, 40, 41, 280, 281 and the text heights of the row types

bool read_table_overrides(GroupCursor& c, TableOverrides& o) {
    if (!require_u32(c, 93, o.flags) || !require_u32(c, 94, o.border_color_flags) ||
        !require_u32(c, 95, o.border_lineweight_flags) || !require_u32(c, 96, o.border_visibility_flags)) {
        return false;
    }
    int16_t v = 0;
    double d = 0.0;
    bool b = false;
    if (c.integer(70, v)) o.flow_direction = v;
    if (c.real(40, d)) o.horizontal_margin = d;
    if (c.real(41, d)) o.vertical_margin = d;
    if (c.boolean(280, b)) o.title_suppressed = b;
    if (c.boolean(281, b)) o.header_suppressed = b;
    for (int i = 0; i < 3; ++i) {
        if (c.real(140, d)) o.rows[i].text_height = d;
    }
    return c.ok();
}

// The bits of the table's override flag that belong to the groups: 70 is 0x4, 40 is 0x8, 41 is 0x10 (AutoCAD's
// files), and the three text heights together 0x700000; the other bits are kept
uint32_t table_flags_with_overrides(uint32_t flags, const TableOverrides& o) {
    auto set = [&](uint32_t bit, bool on) { flags = on ? (flags | bit) : (flags & ~bit); };
    set(0x4, o.flow_direction.has_value());
    set(0x8, o.horizontal_margin.has_value());
    set(0x10, o.vertical_margin.has_value());
    int heights = 0;
    for (const TableRowTypeOverride& r : o.rows) heights += r.text_height.has_value() ? 1 : 0;
    if (heights == 3) flags |= 0x700000;
    else if (heights == 0) flags &= ~0x700000u;
    return flags;
}

void write_table_overrides(RecordWriter& w, const TableOverrides& o) {
    w.integer(93, static_cast<int32_t>(table_flags_with_overrides(o.flags, o)));
    w.integer(94, static_cast<int32_t>(o.border_color_flags));
    w.integer(95, static_cast<int32_t>(o.border_lineweight_flags));
    w.integer(96, static_cast<int32_t>(o.border_visibility_flags));
    if (o.flow_direction) w.integer(70, *o.flow_direction);
    if (o.horizontal_margin) w.real(40, *o.horizontal_margin);
    if (o.vertical_margin) w.real(41, *o.vertical_margin);
    if (o.title_suppressed) w.boolean(280, *o.title_suppressed);
    if (o.header_suppressed) w.boolean(281, *o.header_suppressed);
    for (const TableRowTypeOverride& r : o.rows) {
        if (r.text_height) w.real(140, *r.text_height);
    }
}

// ---- the record

bool read_table_entity(const RecordView& view, TypedReadContext& context, Entity& e) {
    if (context.version < DxfVersion::R2000) {
        return context.fail("ACAD_TABLE in DXF R13/R14 is not established (no AutoCAD file has one): kept as it was read");
    }
    TableData d;
    std::string error;
    EntityProperties scratch;
    if (!read_entity_properties(view, scratch, &d.proxy_graphics, error)) return context.fail(error);

    // A record that is only its head and AcDbEntity has none of the subclasses: nothing in it to read, the defaults. One
    // that has some and not the others is cut off (B105)
    if (!view.find_subclass("AcDbBlockReference") && !view.find_subclass("AcDbTable")) {
        e.data = Boxed<TableData>(std::move(d));
        return true;
    }
    GroupCursor b(view, "AcDbBlockReference");
    if (!b.valid()) return context.fail("ACAD_TABLE has no AcDbBlockReference subclass");
    if (!b.require_string(2, d.block_name) || !b.require_point3(10, d.insertion)) return context.fail(b.error());
    Point3 extrusion;
    if (b.point3(210, extrusion)) e.props.extrusion = extrusion;
    if (!b.at_end()) {
        b.fail("group " + std::to_string(b.code()) + " in AcDbBlockReference is not part of any known ACAD_TABLE");
        return context.fail(b.error());
    }

    GroupCursor c(view, "AcDbTable");
    if (!c.valid()) return context.fail("ACAD_TABLE has no AcDbTable subclass");
    int16_t version = 0;
    if (c.integer(280, version)) d.data_version = version;
    int32_t rows = 0, columns = 0;
    if (!c.require_handle(342, d.style_handle) || !c.require_handle(343, d.block_record_handle) ||
        !c.require_point3(11, d.horizontal_direction) || !require_u32(c, 90, d.flags) || !c.require_integer(91, rows) ||
        !c.require_integer(92, columns) || !read_table_overrides(c, d.overrides)) {
        return context.fail(c.error());
    }
    if (rows < 0 || columns < 0 ||
        static_cast<uint64_t>(rows) + static_cast<uint64_t>(columns) > c.remaining() ||
        static_cast<uint64_t>(rows) * static_cast<uint64_t>(columns) > c.remaining() / 8) {
        c.fail("a table of " + std::to_string(rows) + " rows and " + std::to_string(columns) +
               " columns is more than the groups that remain can hold");
        return context.fail(c.error());
    }
    d.row_heights.assign(static_cast<size_t>(rows), 0.0);
    d.column_widths.assign(static_cast<size_t>(columns), 0.0);
    for (double& h : d.row_heights) if (!c.require_real(141, h)) return context.fail(c.error());
    for (double& w : d.column_widths) if (!c.require_real(142, w)) return context.fail(c.error());
    const size_t cells = static_cast<size_t>(rows) * static_cast<size_t>(columns);
    d.cells.resize(cells);
    for (TableCell& cell : d.cells) if (!read_cell(c, cell)) return context.fail(c.error());
    if (!c.at_end()) {
        c.fail("group " + std::to_string(c.code()) + " after the cells is not part of any known ACAD_TABLE");
        return context.fail(c.error());
    }
    e.data = Boxed<TableData>(std::move(d));
    return true;
}

void write_table_entity(RecordWriter& w, const Entity& e, const std::string& owner) {
    if (w.older()) {
        w.fail("ACAD_TABLE in DXF R13/R14 is not established (no AutoCAD file has one)");
        return;
    }
    const TableData& d = e.as_table();
    if (d.cells.size() != d.rows() * d.columns()) {
        w.fail("A table of " + std::to_string(d.rows()) + " rows and " + std::to_string(d.columns()) + " columns has " +
               std::to_string(d.cells.size()) + " cells, not " + std::to_string(d.rows() * d.columns()));
        return;
    }
    const bool full = w.version() >= DxfVersion::R2007;
    static const std::vector<uint8_t> none;
    w.entity_head("ACAD_TABLE", e, owner, w.proxy_graphics_enabled() ? &d.proxy_graphics : &none);
    w.subclass("AcDbBlockReference");
    w.text(2, d.block_name);
    w.point3(10, d.insertion);
    const Point3& x = e.props.extrusion;
    if (x.x != 0.0 || x.y != 0.0 || x.z != 1.0) w.point3(210, x);
    w.subclass("AcDbTable");
    if (d.data_version) w.integer(280, *d.data_version);
    w.pointer(342, d.style_handle.empty() ? std::string("0") : d.style_handle, PointerWrite::ZeroDead);
    w.pointer(343, d.block_record_handle.empty() ? std::string("0") : d.block_record_handle, PointerWrite::ZeroDead);
    w.point3(11, d.horizontal_direction);
    w.integer(90, static_cast<int32_t>(d.flags));
    w.integer(91, static_cast<int64_t>(d.rows()));
    w.integer(92, static_cast<int64_t>(d.columns()));
    write_table_overrides(w, d.overrides);
    for (double h : d.row_heights) w.real(141, h);
    for (double cw : d.column_widths) w.real(142, cw);
    for (const TableCell& cell : d.cells) write_cell(w, cell, full);
    w.xdata(e.props.xdata);
}

void table_references(const Entity& e, std::vector<TypedReference>& out) {
    const TableData& d = e.as_table();
    // The style has the table as a reactor (TABLESTYLE's {ACAD_REACTORS: AutoCAD's own files)
    if (!d.style_handle.empty()) out.push_back({d.style_handle, RefKind::HardPointer, true});
    if (!d.block_record_handle.empty()) out.push_back({d.block_record_handle, RefKind::HardPointer, false});
    for (const TableCell& cell : d.cells) {
        if (!cell.field_handle.empty()) out.push_back({cell.field_handle, RefKind::HardPointer, false});
        if (!cell.block_handle.empty()) out.push_back({cell.block_handle, RefKind::HardPointer, false});
        for (const TableAttributeValue& a : cell.attributes) {
            if (!a.attdef_handle.empty()) out.push_back({a.attdef_handle, RefKind::SoftPointer, false});
        }
    }
}

// What the table draws in $EXTMIN/$EXTMAX: the box of its cells (a table of ROW heights x COLUMN widths from its
// insertion point, to the right along the horizontal direction and down). AutoCAD's own *T block is the
// layout of step 9.5; until it, the box of the grid
void table_extents(const Entity& e, TypedExtentsSink& sink) {
    // The *T block as an INSERT of it has its extents (step 9.5, table_graphics.cpp); the grid below when the table has
    // no block to measure
    if (table_block_extents(e, sink)) return;
    const TableData& d = e.as_table();
    double width = 0.0, height = 0.0;
    for (double w : d.column_widths) width += w;
    for (double h : d.row_heights) height += h;
    if (width <= 0.0 && height <= 0.0) return;
    const Point3& o = d.insertion;
    Point3 x = d.horizontal_direction;
    double len = std::sqrt(x.x * x.x + x.y * x.y + x.z * x.z);
    if (len == 0.0) return;
    x = {x.x / len, x.y / len, x.z / len};
    // the table's own vertical: the normal (0, 0, 1) crossed with the horizontal direction, pointing up
    Point3 y = {-x.y, x.x, 0.0};
    auto corner = [&](double dx, double dy) {
        sink.add_wcs({o.x + x.x * dx + y.x * dy, o.y + x.y * dx + y.y * dy, o.z + x.z * dx});
    };
    corner(0.0, 0.0);
    corner(width, 0.0);
    corner(0.0, -height);
    corner(width, -height);
}

} // namespace

void register_table_entity_codec(TypedRegistry& registry) {
    EntityCodec c;
    c.dxf_name = "ACAD_TABLE";
    c.type = EntityType::Table;
    c.read = read_table_entity;
    c.write = write_table_entity;
    c.references = table_references;
    c.extents = table_extents;
    c.cls = table_class_spec("AcDbTable", 1025);
    c.versions = table_versions();
    // The block records and fields of the cells and the attribute definitions of their blocks
    auto pointer = [&](int code, RefKind kind) {
        RefDescriptor r;
        r.subclass = "AcDbTable";
        r.code = code;
        r.kind = kind;
        r.removal = RefRemoval::ZeroInPlace;
        c.refs.push_back(r);
    };
    pointer(342, RefKind::HardPointer);
    pointer(343, RefKind::HardPointer);
    pointer(340, RefKind::HardPointer);
    pointer(344, RefKind::HardPointer);
    pointer(331, RefKind::SoftPointer);
    c.lists.push_back(table_whole_subclass("AcDbTable"));
    c.step = 1;
    registry.add(std::move(c));
}

void register_table_codecs(TypedRegistry& registry) {
    register_table_entity_codec(registry);
    register_table_style_codecs(registry);
    register_table_content_codec(registry);
    register_table_geometry_codec(registry);
}

} // namespace internal
} // namespace hdxf
