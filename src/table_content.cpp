#include "TableTypes.hpp"

// TABLECONTENT (phase 9, step 9.1): the table's content as AutoCAD 2008 and later keep it for round trip, in
// four subclasses — AcDbLinkedData, AcDbLinkedTableData (columns, rows, cells, the fields it owns),
// AcDbFormattedTableData (the table's format and its merged ranges) and AcDbTableContent (the style). The
// parts inside a subclass open and close with markers (`1 NAME_BEGIN` … `309 NAME_END`) and are named by
// the group before them (`300 CELL`, `301 ROW`, `302 CONTENT`); a marker is read where the grammar expects
// it, never taken for a cell's text. The order is AutoCAD's own (doc/facit/acad_*_dxf_tables.dxf, ACadSharp's
// sample_AC1015 … AC1032, Oxcart's files).

namespace hdxf {
namespace internal {

namespace {


// ---- custom data: 301 CUSTOMDATA, 1 DATAMAP_BEGIN, 90 count, (300 name, 301 DATAMAP_VALUE, AcValue)*, 309 DATAMAP_END

bool read_data_map(GroupCursor& c, std::vector<TableCustomData>& out) {
    if (!require_marker(c, 301, "CUSTOMDATA") || !require_marker(c, 1, "DATAMAP_BEGIN")) return false;
    size_t n = 0;
    if (!c.count(90, 4, n)) {
        if (c.ok()) c.fail("group 90 (the number of custom data) is missing");
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        TableCustomData item;
        if (!c.require_string(300, item.name) || !require_marker(c, 301, "DATAMAP_VALUE")) return false;
        bool full = c.code() == 93;
        if (!read_ac_value(c, full, item.value)) return false;
        out.push_back(std::move(item));
    }
    return require_marker(c, 309, "DATAMAP_END");
}

void write_data_map(RecordWriter& w, const std::vector<TableCustomData>& items, bool full) {
    w.text(301, "CUSTOMDATA");
    w.text(1, "DATAMAP_BEGIN");
    w.integer(90, static_cast<int64_t>(items.size()));
    for (const TableCustomData& item : items) {
        w.text(300, item.name);
        w.text(301, "DATAMAP_VALUE");
        write_ac_value(w, full, item.value);
    }
    w.text(309, "DATAMAP_END");
}

// ---- a format named by the group before it: `300 CELLTABLEFORMAT`, TABLEFORMAT block

bool read_named_format(GroupCursor& c, const char* name, const char* block, TableFormatData& out) {
    std::string begin = std::string(block) + "_BEGIN";
    std::string end = std::string(block) + "_END";
    if (!require_marker(c, 1, begin.c_str()) || !require_marker(c, 300, name) || !read_table_format(c, out)) {
        return false;
    }
    return require_marker(c, 309, end.c_str());
}

void write_named_format(RecordWriter& w, const char* name, const char* block, const TableFormatData& f) {
    w.text(1, std::string(block) + "_BEGIN");
    w.text(300, name);
    write_table_format(w, f);
    w.text(309, std::string(block) + "_END");
}

// ---- cell content

bool read_cell_content(GroupCursor& c, TableCellContent& out) {
    if (!require_marker(c, 302, "CONTENT") || !require_marker(c, 1, "CELLCONTENT_BEGIN")) return false;
    int32_t kind = 0;
    if (!c.require_integer(90, kind)) return false;
    if (kind != 1 && kind != 2 && kind != 4) {
        c.fail("a cell's content of kind " + std::to_string(kind) + " is not established");
        return false;
    }
    out.kind = static_cast<TableContentKind>(kind);
    if (out.kind == TableContentKind::Value) {
        if (!require_marker(c, 300, "VALUE")) return false;
        bool full = c.code() == 93;
        if (!read_ac_value(c, full, out.value, true)) return false;
    } else {
        if (!c.require_handle(340, out.handle)) return false;
    }
    int32_t attributes = 0;
    if (!c.require_integer(91, attributes)) return false;
    if (attributes != 0) {
        c.fail("a cell content with " + std::to_string(attributes) + " attribute values is not established");
        return false;
    }
    if (!require_marker(c, 309, "CELLCONTENT_END")) return false;
    // FORMATTEDCELLCONTENT
    if (!require_marker(c, 1, "FORMATTEDCELLCONTENT_BEGIN")) return false;
    bool has_format = false;
    if (!c.require_boolean(170, has_format)) return false;
    if (has_format) {
        TableContentFormat format;
        if (!require_marker(c, 300, "CONTENTFORMAT") || !read_content_format(c, format)) return false;
        out.format = format;
    }
    return require_marker(c, 309, "FORMATTEDCELLCONTENT_END");
}

void write_cell_content(RecordWriter& w, const TableCellContent& content, bool full) {
    w.text(302, "CONTENT");
    w.text(1, "CELLCONTENT_BEGIN");
    w.integer(90, static_cast<int32_t>(content.kind));
    if (content.kind == TableContentKind::Value) {
        w.text(300, "VALUE");
        write_ac_value(w, full, content.value);
    } else {
        w.pointer(340, content.handle.empty() ? std::string("0") : content.handle, PointerWrite::ZeroDead);
    }
    w.integer(91, 0);
    w.text(309, "CELLCONTENT_END");
    w.text(1, "FORMATTEDCELLCONTENT_BEGIN");
    w.boolean(170, content.format.has_value());
    if (content.format) {
        w.text(300, "CONTENTFORMAT");
        write_content_format(w, *content.format);
    }
    w.text(309, "FORMATTEDCELLCONTENT_END");
}

// ---- a cell: LINKEDTABLEDATACELL, FORMATTEDTABLEDATACELL, TABLECELL

bool read_cell(GroupCursor& c, TableContentCell& out) {
    if (!require_marker(c, 300, "CELL") || !require_marker(c, 1, "LINKEDTABLEDATACELL_BEGIN")) return false;
    size_t contents = 0;
    if (!c.require_integer(90, out.state) || !c.require_string(300, out.name) || !c.require_integer(91, out.flags) ||
        !read_data_map(c, out.custom_data) || !c.require_integer(92, out.extra) || !c.count(95, 10, contents)) {
        if (c.ok()) c.fail("group 95 (the number of contents of a cell) is missing");
        return false;
    }
    for (size_t i = 0; i < contents; ++i) {
        TableCellContent content;
        if (!read_cell_content(c, content)) return false;
        out.contents.push_back(std::move(content));
    }
    if (!require_marker(c, 309, "LINKEDTABLEDATACELL_END")) return false;
    if (!read_named_format(c, "CELLTABLEFORMAT", "FORMATTEDTABLEDATACELL", out.format)) return false;
    // TABLECELL
    if (!require_marker(c, 1, "TABLECELL_BEGIN")) return false;
    size_t attachments = 0;
    if (!c.require_integer(90, out.cell_flags) || !c.count(91, 5, attachments)) {
        if (c.ok()) c.fail("group 91 (the number of attachments of a cell) is missing");
        return false;
    }
    for (size_t i = 0; i < attachments; ++i) {
        TableCellAttachment a;
        if (!c.require_integer(91, a.kind) || !c.require_real(40, a.width) || !c.require_real(41, a.height) ||
            !c.require_handle(330, a.block_handle) || !c.require_integer(92, a.flags)) {
            return false;
        }
        out.attachments.push_back(std::move(a));
    }
    return require_marker(c, 309, "TABLECELL_END");
}

void write_cell(RecordWriter& w, const TableContentCell& cell, bool full) {
    w.text(300, "CELL");
    w.text(1, "LINKEDTABLEDATACELL_BEGIN");
    w.integer(90, cell.state);
    w.text(300, cell.name);
    w.integer(91, cell.flags);
    write_data_map(w, cell.custom_data, full);
    w.integer(92, cell.extra);
    w.integer(95, static_cast<int64_t>(cell.contents.size()));
    for (const TableCellContent& content : cell.contents) write_cell_content(w, content, full);
    w.text(309, "LINKEDTABLEDATACELL_END");
    write_named_format(w, "CELLTABLEFORMAT", "FORMATTEDTABLEDATACELL", cell.format);
    w.text(1, "TABLECELL_BEGIN");
    w.integer(90, cell.cell_flags);
    w.integer(91, static_cast<int64_t>(cell.attachments.size()));
    for (const TableCellAttachment& a : cell.attachments) {
        w.integer(91, a.kind);
        w.real(40, a.width);
        w.real(41, a.height);
        w.pointer(330, a.block_handle.empty() ? std::string("0") : a.block_handle, PointerWrite::ZeroDead);
        w.integer(92, a.flags);
    }
    w.text(309, "TABLECELL_END");
}

// ---- columns and rows

bool read_column(GroupCursor& c, TableContentColumn& out) {
    if (!require_marker(c, 300, "COLUMN") || !require_marker(c, 1, "LINKEDTABLEDATACOLUMN_BEGIN")) return false;
    if (!c.require_string(300, out.name) || !c.require_integer(91, out.flags) || !read_data_map(c, out.custom_data) ||
        !require_marker(c, 309, "LINKEDTABLEDATACOLUMN_END")) {
        return false;
    }
    if (!read_named_format(c, "COLUMNTABLEFORMAT", "FORMATTEDTABLEDATACOLUMN", out.format)) return false;
    if (!require_marker(c, 1, "TABLECOLUMN_BEGIN") || !c.require_integer(90, out.column_flags) ||
        !c.require_real(40, out.width)) {
        return false;
    }
    return require_marker(c, 309, "TABLECOLUMN_END");
}

void write_column(RecordWriter& w, const TableContentColumn& col, bool full) {
    w.text(300, "COLUMN");
    w.text(1, "LINKEDTABLEDATACOLUMN_BEGIN");
    w.text(300, col.name);
    w.integer(91, col.flags);
    write_data_map(w, col.custom_data, full);
    w.text(309, "LINKEDTABLEDATACOLUMN_END");
    write_named_format(w, "COLUMNTABLEFORMAT", "FORMATTEDTABLEDATACOLUMN", col.format);
    w.text(1, "TABLECOLUMN_BEGIN");
    w.integer(90, col.column_flags);
    w.real(40, col.width);
    w.text(309, "TABLECOLUMN_END");
}

bool read_row(GroupCursor& c, TableContentRow& out) {
    if (!require_marker(c, 301, "ROW") || !require_marker(c, 1, "LINKEDTABLEDATAROW_BEGIN")) return false;
    size_t cells = 0;
    if (!c.count(90, 20, cells)) {
        if (c.ok()) c.fail("group 90 (the number of cells of a row) is missing");
        return false;
    }
    for (size_t i = 0; i < cells; ++i) {
        TableContentCell cell;
        if (!read_cell(c, cell)) return false;
        out.cells.push_back(std::move(cell));
    }
    if (!c.require_integer(91, out.flags) || !read_data_map(c, out.custom_data) ||
        !require_marker(c, 309, "LINKEDTABLEDATAROW_END")) {
        return false;
    }
    if (!read_named_format(c, "ROWTABLEFORMAT", "FORMATTEDTABLEDATAROW", out.format)) return false;
    if (!require_marker(c, 1, "TABLEROW_BEGIN") || !c.require_integer(90, out.row_type) ||
        !c.require_real(40, out.height)) {
        return false;
    }
    return require_marker(c, 309, "TABLEROW_END");
}

void write_row(RecordWriter& w, const TableContentRow& row, bool full) {
    w.text(301, "ROW");
    w.text(1, "LINKEDTABLEDATAROW_BEGIN");
    w.integer(90, static_cast<int64_t>(row.cells.size()));
    for (const TableContentCell& cell : row.cells) write_cell(w, cell, full);
    w.integer(91, row.flags);
    write_data_map(w, row.custom_data, full);
    w.text(309, "LINKEDTABLEDATAROW_END");
    write_named_format(w, "ROWTABLEFORMAT", "FORMATTEDTABLEDATAROW", row.format);
    w.text(1, "TABLEROW_BEGIN");
    w.integer(90, row.row_type);
    w.real(40, row.height);
    w.text(309, "TABLEROW_END");
}

// ---- the record

bool read_table_content(const RecordView& view, TypedReadContext& context, Object& o) {
    if (context.version < DxfVersion::R2000) {
        return context.fail("TABLECONTENT in DXF R13/R14 is not established (no AutoCAD file has one): kept as it was read");
    }
    TableContentData d;
    if (!view.find_subclass("AcDbLinkedData") && !view.find_subclass("AcDbLinkedTableData") &&
        !view.find_subclass("AcDbFormattedTableData") && !view.find_subclass("AcDbTableContent")) {
        o.data = Boxed<TableContentData>(std::move(d));             // only the head: the defaults
        return true;
    }

    GroupCursor linked(view, "AcDbLinkedData");
    if (!linked.valid()) return context.fail("TABLECONTENT has no AcDbLinkedData subclass");
    if (!linked.require_string(1, d.name) || !linked.require_string(300, d.description)) return context.fail(linked.error());
    if (!linked.at_end()) {
        linked.fail("group " + std::to_string(linked.code()) + " in AcDbLinkedData is not part of any known TABLECONTENT");
        return context.fail(linked.error());
    }

    GroupCursor c(view, "AcDbLinkedTableData");
    if (!c.valid()) return context.fail("TABLECONTENT has no AcDbLinkedTableData subclass");
    size_t columns = 0;
    if (!c.count(90, 10, columns)) {
        if (c.ok()) c.fail("group 90 (the number of columns) is missing");
        return context.fail(c.error());
    }
    for (size_t i = 0; i < columns; ++i) {
        TableContentColumn column;
        if (!read_column(c, column)) return context.fail(c.error());
        d.columns.push_back(std::move(column));
    }
    size_t rows = 0;
    if (!c.count(91, 10, rows)) {
        if (c.ok()) c.fail("group 91 (the number of rows) is missing");
        return context.fail(c.error());
    }
    for (size_t i = 0; i < rows; ++i) {
        TableContentRow row;
        if (!read_row(c, row)) return context.fail(c.error());
        d.rows.push_back(std::move(row));
    }
    size_t fields = 0;
    if (!c.count(92, 1, fields)) {
        if (c.ok()) c.fail("group 92 (the number of fields) is missing");
        return context.fail(c.error());
    }
    for (size_t i = 0; i < fields; ++i) {
        std::string handle;
        if (!c.require_handle(360, handle)) return context.fail(c.error());
        d.field_handles.push_back(handle);
    }
    if (!c.at_end()) {
        c.fail("group " + std::to_string(c.code()) + " in AcDbLinkedTableData is not part of any known TABLECONTENT");
        return context.fail(c.error());
    }

    GroupCursor formatted(view, "AcDbFormattedTableData");
    if (!formatted.valid()) return context.fail("TABLECONTENT has no AcDbFormattedTableData subclass");
    size_t merges = 0;
    if (!require_marker(formatted, 300, "TABLEFORMAT") || !read_table_format(formatted, d.table_format) ||
        !formatted.count(90, 4, merges)) {
        if (formatted.ok()) formatted.fail("group 90 (the number of merged ranges) is missing");
        return context.fail(formatted.error());
    }
    for (size_t i = 0; i < merges; ++i) {
        TableMergeRange m;
        if (!formatted.require_integer(91, m.top_row) || !formatted.require_integer(92, m.left_column) ||
            !formatted.require_integer(93, m.bottom_row) || !formatted.require_integer(94, m.right_column)) {
            return context.fail(formatted.error());
        }
        d.merges.push_back(m);
    }
    if (!formatted.at_end()) {
        formatted.fail("group " + std::to_string(formatted.code()) + " in AcDbFormattedTableData is not part of any known TABLECONTENT");
        return context.fail(formatted.error());
    }

    GroupCursor content(view, "AcDbTableContent");
    if (!content.valid()) return context.fail("TABLECONTENT has no AcDbTableContent subclass");
    if (!content.require_handle(340, d.style_handle)) return context.fail(content.error());
    if (!content.at_end()) {
        content.fail("group " + std::to_string(content.code()) + " in AcDbTableContent is not part of any known TABLECONTENT");
        return context.fail(content.error());
    }

    o.data = Boxed<TableContentData>(std::move(d));
    return true;
}

void write_table_content(RecordWriter& w, const Object& o) {
    if (w.older()) {
        w.fail("TABLECONTENT in DXF R13/R14 is not established (no AutoCAD file has one)");
        return;
    }
    const TableContentData& d = o.as_table_content();
    const bool full = w.version() >= DxfVersion::R2007;
    w.object_head("TABLECONTENT", o);
    w.subclass("AcDbLinkedData");
    w.text(1, d.name);
    w.text(300, d.description);
    w.subclass("AcDbLinkedTableData");
    w.integer(90, static_cast<int64_t>(d.columns.size()));
    for (const TableContentColumn& column : d.columns) write_column(w, column, full);
    w.integer(91, static_cast<int64_t>(d.rows.size()));
    for (const TableContentRow& row : d.rows) write_row(w, row, full);
    // The fields it owns: one that is gone is not listed, and the count follows (B19)
    std::vector<std::string> fields;
    for (const std::string& h : d.field_handles) if (w.live(h)) fields.push_back(h);
    w.integer(92, static_cast<int64_t>(fields.size()));
    for (const std::string& h : fields) w.handle(360, h);
    w.subclass("AcDbFormattedTableData");
    w.text(300, "TABLEFORMAT");
    write_table_format(w, d.table_format);
    w.integer(90, static_cast<int64_t>(d.merges.size()));
    for (const TableMergeRange& m : d.merges) {
        w.integer(91, m.top_row);
        w.integer(92, m.left_column);
        w.integer(93, m.bottom_row);
        w.integer(94, m.right_column);
    }
    w.subclass("AcDbTableContent");
    w.pointer(340, d.style_handle.empty() ? std::string("0") : d.style_handle, PointerWrite::ZeroDead);
    w.xdata(o.xdata);
}

void table_content_references(const Object& o, std::vector<TypedReference>& out) {
    const TableContentData& d = o.as_table_content();
    if (!d.style_handle.empty()) out.push_back({d.style_handle, RefKind::HardPointer, false});
    for (const std::string& h : d.field_handles) out.push_back({h, RefKind::HardOwner, false});
}

} // namespace


void register_table_content_codec(TypedRegistry& registry) {
    ObjectCodec c;
    c.dxf_name = "TABLECONTENT";
    c.type = ObjectType::TableContent;
    c.read = read_table_content;
    c.write = write_table_content;
    c.references = table_content_references;
    c.cls = table_class_spec("AcDbTableContent", 1152);
    // 2000 and 2004 keep the table in a round-trip DATATABLE (step 9.8) as well; a table with content has this record
    // there too (AutoCAD's own acad_2000/2004_dxf_tables.dxf), made by sync_table() like the 2007 and later tables'
    c.versions = table_versions();
    // The fields it owns (360): a field that is gone goes from the list, and the count (92) follows
    RefDescriptor fields;
    fields.subclass = "AcDbLinkedTableData";
    fields.code = 360;
    fields.kind = RefKind::HardOwner;
    fields.removal = RefRemoval::DropMember;
    fields.counter_code = 92;
    c.refs.push_back(fields);
    // The table style, the blocks and fields of the contents, the text styles and linetypes of the formats: pointers
    for (const char* subclass : {"AcDbLinkedTableData", "AcDbTableContent"}) {
        RefDescriptor pointer;
        pointer.subclass = subclass;
        pointer.code = 340;
        pointer.kind = RefKind::HardPointer;
        pointer.removal = RefRemoval::ZeroInPlace;
        c.refs.push_back(pointer);
    }
    RefDescriptor attached;
    attached.subclass = "AcDbLinkedTableData";
    attached.code = 330;
    attached.kind = RefKind::SoftPointer;
    attached.removal = RefRemoval::ZeroInPlace;
    c.refs.push_back(attached);
    for (const char* subclass : {"AcDbLinkedData", "AcDbLinkedTableData", "AcDbFormattedTableData", "AcDbTableContent"}) {
        c.lists.push_back(table_whole_subclass(subclass));
    }
    c.step = 1;
    registry.add(std::move(c));
}

} // namespace internal
} // namespace hdxf
