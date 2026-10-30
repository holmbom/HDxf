#include "TableTypes.hpp"

// TABLEGEOMETRY (phase 9, step 9.1): the laid out size of every cell of a table, as AutoCAD keeps it in the
// table's round-trip dictionary. One subclass, AcDbTableGeometry: 90 rows, 91 columns, 92 the number of
// cells; per cell 93 flags, 40/41 its size, 330 a pointer (0 in every AutoCAD file seen), 94 the number of
// contents and per content its point (10), anchor (11), four sizes (43 … 46) and 95 flags.

namespace hdxf {
namespace internal {

namespace {

bool read_table_geometry(const RecordView& view, TypedReadContext& context, Object& o) {
    if (context.version < DxfVersion::R2000) {
        return context.fail("TABLEGEOMETRY in DXF R13/R14 is not established (no AutoCAD file has one): kept as it was read");
    }
    if (!view.find_subclass("AcDbTableGeometry")) {
        o.data = Boxed<TableGeometryData>(TableGeometryData());     // only the head: the defaults
        return true;
    }
    GroupCursor c(view, "AcDbTableGeometry");
    TableGeometryData d;
    size_t cells = 0;
    if (!c.require_integer(90, d.rows) || !c.require_integer(91, d.columns) || !c.count(92, 5, cells)) {
        if (c.ok()) c.fail("group 92 (the number of cells) is missing");
        return context.fail(c.error());
    }
    for (size_t i = 0; i < cells; ++i) {
        TableCellGeometry cell;
        size_t contents = 0;
        if (!c.require_integer(93, cell.flags) || !c.require_real(40, cell.width) || !c.require_real(41, cell.height) ||
            !c.require_handle(330, cell.handle) || !c.count(94, 11, contents)) {
            if (c.ok()) c.fail("group 94 (the number of contents of a cell) is missing");
            return context.fail(c.error());
        }
        for (size_t k = 0; k < contents; ++k) {
            TableContentGeometry g;
            if (!c.require_point3(10, g.point) || !c.require_point3(11, g.anchor) || !c.require_real(43, g.width) ||
                !c.require_real(44, g.height) || !c.require_real(45, g.content_width) ||
                !c.require_real(46, g.content_height) || !c.require_integer(95, g.flags)) {
                return context.fail(c.error());
            }
            cell.contents.push_back(g);
        }
        d.cells.push_back(std::move(cell));
    }
    if (!c.at_end()) {
        c.fail("group " + std::to_string(c.code()) + " after the cells is not part of any known TABLEGEOMETRY");
        return context.fail(c.error());
    }
    o.data = Boxed<TableGeometryData>(std::move(d));
    return true;
}

void write_table_geometry(RecordWriter& w, const Object& o) {
    if (w.older()) {
        w.fail("TABLEGEOMETRY in DXF R13/R14 is not established (no AutoCAD file has one)");
        return;
    }
    const TableGeometryData& d = o.as_table_geometry();
    w.object_head("TABLEGEOMETRY", o);
    w.subclass("AcDbTableGeometry");
    w.integer(90, d.rows);
    w.integer(91, d.columns);
    w.integer(92, static_cast<int64_t>(d.cells.size()));
    for (const TableCellGeometry& cell : d.cells) {
        w.integer(93, cell.flags);
        w.real(40, cell.width);
        w.real(41, cell.height);
        w.pointer(330, cell.handle.empty() ? std::string("0") : cell.handle, PointerWrite::ZeroDead);
        w.integer(94, static_cast<int64_t>(cell.contents.size()));
        for (const TableContentGeometry& g : cell.contents) {
            w.point3(10, g.point);
            w.point3(11, g.anchor);
            w.real(43, g.width);
            w.real(44, g.height);
            w.real(45, g.content_width);
            w.real(46, g.content_height);
            w.integer(95, g.flags);
        }
    }
    w.xdata(o.xdata);
}

} // namespace

void register_table_geometry_codec(TypedRegistry& registry) {
    ObjectCodec c;
    c.dxf_name = "TABLEGEOMETRY";
    c.type = ObjectType::TableGeometry;
    c.read = read_table_geometry;
    c.write = write_table_geometry;
    c.cls = table_class_spec("AcDbTableGeometry", 1152);
    // see TABLECONTENT: 2000 and 2004 have it with the round-trip DATATABLE of step 9.8 (AutoCAD's own files)
    c.versions = table_versions();
    // The 330 of a cell is a soft pointer, not the owner (that is the head's)
    RefDescriptor pointer;
    pointer.subclass = "AcDbTableGeometry";
    pointer.code = 330;
    pointer.kind = RefKind::SoftPointer;
    pointer.removal = RefRemoval::ZeroInPlace;
    c.refs.push_back(pointer);
    c.lists.push_back(table_whole_subclass("AcDbTableGeometry"));
    c.step = 1;
    registry.add(std::move(c));
}

} // namespace internal
} // namespace hdxf
