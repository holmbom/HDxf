#include "DocumentDataTypes.hpp"
#include "TableTypes.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cstdlib>

// DATATABLE and ACDBDATATABLE (phase 9, step 9.8; plan B100-B105, B108): the table of typed values AutoCAD keeps
// the cells of every ACAD_TABLE of a DXF 2000 or 2004 file in (the round trip of what it wrote before 2007), and
// AutoCAD Map keeps its style tables in.
//
//   DATATABLE:     S(AcDbDataTable) 70 90 91 1 (92 2 <one value for each row>)*
//   ACDBDATATABLE: the same under S(ACDBDATATABLE), DXF 2000's name of the record and of its subclass
//
// The values are in the group of their type: 71 bool, 93 integer, 40 double, 3 text, 10 20 30 (2D) and 11 21 31 (3D)
// points, and an object in 331, 330, 340, 350 or 360 (the reference lists all five; AutoCAD's own files show 331 and
// 360). The files of AutoCAD 2025 show the column types 1, 2, 3, 4, 5, 6, 10 and 11 (hdxf.hpp, DataColumnType).
//
// The pre-2007 round trip of a table (doc/facit/acad_2000_dxf_table_empty.dxf, acad_2000_dxf_tables.dxf): the table's
// extension dictionary has ACAD_XREC_ROUNDTRIP, an XRECORD whose `102 ACAD_ROUNDTRIP_PRE2007_TABLE, 90 rows, 91
// columns, 102 ACAD_ROUNDTRIP_PRE2007_TABLECELL, 360 <data table>` part names a data table of four columns: the
// checksum of each cell's text (a double; the sum of every UTF-16 unit times its position, the table content's
// ACAD_ROUNDTRIP_2008_CELL_CHECKSUM), two integers whose meaning AutoCAD's files do not show (0 for a cell without
// overrides) and the XRECORD of each cell's value (93 flags, 90 type, the value, 94, 300, 302: an AcValue without its
// ACVALUE_END), row by row. AutoCAD makes it for every table of those versions, also an empty one.

namespace hdxf {
namespace internal {

namespace {

std::string describe(const char* type, const Object& o) {
    return std::string(type) + " " + o.handle;
}

void note_rest(GroupCursor& c, TypedReadContext& ctx, const char* type, const Object& o) {
    if (!c.ok() || c.at_end()) return;
    size_t n = c.take_rest().size();
    ctx.warn(describe(type, o) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
             ") are kept as they were read");
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// One value, by the group it is in (a row of a column of a type HDxf has not seen is read the same way)
bool read_value(GroupCursor& c, DataTableValue& out) {
    switch (c.code()) {
        case 71: {
            bool v = false;
            if (!c.boolean(71, v)) return false;
            out = v;
            return true;
        }
        case 93: {
            int32_t v = 0;
            if (!c.integer(93, v)) return false;
            out = v;
            return true;
        }
        case 40: {
            double v = 0.0;
            if (!c.real(40, v)) return false;
            out = v;
            return true;
        }
        case 3: {
            std::string v;
            if (!c.string(3, v)) return false;
            out = std::move(v);
            return true;
        }
        case 10: {
            Point3 p;
            if (!c.point3(10, p)) return false;
            out = DataTablePoint2D{p};
            return true;
        }
        case 11: {
            Point3 p;
            if (!c.point3(11, p)) return false;
            out = DataTablePoint3D{p};
            return true;
        }
        case 331: case 330: case 340: case 350: case 360: {
            const int code = c.code();
            DataTableObject object;
            object.kind = static_cast<DataTableObjectKind>(code);
            if (!c.handle(code, object.handle)) return false;
            out = std::move(object);
            return true;
        }
        default:
            c.fail("group " + std::to_string(c.code()) + " is not a value of a data table column");
            return false;
    }
}

bool read_data_table_record(const RecordView& view, TypedReadContext& ctx, Object& o, const char* name, const char* subclass) {
    if (ctx.version < DxfVersion::R2000) {
        return ctx.fail(describe(name, o) + " in DXF R13/R14: no AutoCAD file shows the grammar there");
    }
    GroupCursor c(view, subclass);
    if (!c.valid()) return ctx.fail(describe(name, o) + " has no " + subclass + " subclass");
    DataTableData d;
    c.require_integer(70, d.version);
    size_t columns = 0;
    if (!c.ok() || !c.count(90, 2, columns)) {
        return ctx.fail(describe(name, o) + ": " + (c.ok() ? "the number of columns (90) is missing" : c.error()));
    }
    c.require_integer(91, d.rows);
    c.require_string(1, d.name);
    if (!c.ok()) return ctx.fail(describe(name, o) + ": " + c.error());
    // A negative row count or one that the groups cannot hold is a corrupt record, found before anything is allocated
    if (d.rows < 0 || (columns > 0 && static_cast<size_t>(d.rows) > c.remaining())) {
        return ctx.fail(describe(name, o) + ": the number of rows (91) is " + std::to_string(d.rows) + " and only " +
                        std::to_string(c.remaining()) + " group(s) follow");
    }
    d.columns.reserve(columns);
    for (size_t i = 0; i < columns; ++i) {
        DataColumn column;
        c.require_integer(92, column.type);
        c.require_string(2, column.name);
        if (!c.ok()) return ctx.fail(describe(name, o) + ": column " + std::to_string(i) + ": " + c.error());
        column.values.reserve(static_cast<size_t>(d.rows));
        for (int32_t row = 0; row < d.rows; ++row) {
            DataTableValue value;
            if (!read_value(c, value)) {
                return ctx.fail(describe(name, o) + ": column \"" + column.name + "\", row " + std::to_string(row) + ": " +
                                (c.ok() ? "the value is missing" : c.error()));
            }
            column.values.push_back(std::move(value));
        }
        d.columns.push_back(std::move(column));
    }
    note_rest(c, ctx, name, o);
    o.data = Boxed<DataTableData>(std::move(d));
    return true;
}

bool read_data_table(const RecordView& view, TypedReadContext& ctx, Object& o) {
    return read_data_table_record(view, ctx, o, "DATATABLE", "AcDbDataTable");
}

bool read_legacy_data_table(const RecordView& view, TypedReadContext& ctx, Object& o) {
    return read_data_table_record(view, ctx, o, "ACDBDATATABLE", "ACDBDATATABLE");
}

// Every object value is a pointer, of the kind its group says (B19, B104)
void data_table_references(const Object& o, std::vector<TypedReference>& out) {
    for (const DataColumn& column : o.as_data_table().columns) {
        for (const DataTableValue& v : column.values) {
            const DataTableObject* object = std::get_if<DataTableObject>(&v);
            if (!object || object->handle.empty()) continue;
            RefKind kind = RefKind::SoftPointer;
            switch (object->kind) {
                case DataTableObjectKind::HardOwner: kind = RefKind::HardOwner; break;
                case DataTableObjectKind::SoftOwner: kind = RefKind::SoftOwner; break;
                case DataTableObjectKind::HardPointer: kind = RefKind::HardPointer; break;
                case DataTableObjectKind::ObjectValue:
                case DataTableObjectKind::SoftPointer: kind = RefKind::SoftPointer; break;
            }
            out.push_back({object->handle, kind, false});
        }
    }
}

// The object values of a data table are pointers: a target that is gone is a 0 in its place (a row is not taken out
// of one column only: the columns stay as long as each other)
std::vector<RefDescriptor> data_table_refs(const char* subclass) {
    std::vector<RefDescriptor> out;
    struct Kind { int code; RefKind kind; };
    for (const Kind& k : {Kind{331, RefKind::SoftPointer}, Kind{330, RefKind::SoftPointer}, Kind{340, RefKind::HardPointer},
                          Kind{350, RefKind::SoftOwner}, Kind{360, RefKind::HardOwner}}) {
        RefDescriptor d;
        d.subclass = subclass;
        d.code = k.code;
        d.kind = k.kind;
        d.removal = RefRemoval::ZeroInPlace;
        out.push_back(d);
    }
    return out;
}

ListSpec whole_subclass(const char* subclass) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

ClassSpec class_of() {
    ClassSpec c;
    c.cpp_name = "AcDbDataTable";
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 0;
    c.in_older = false;
    return c;
}

// AutoCAD 2025 saved DATATABLE in 2004 … 2018 and ACDBDATATABLE in 2000, ASCII and binary (the tables of
// doc/facit/acad_<ver>_dxf_table_empty*.dxf and acad_<ver>_dxf_tables*.dxf; doc/facit/acad_<ver>_dxf_native_records.dxf has
// every value type but the owner pointers). R12 has no OBJECTS section; R13 and R14: not established
VersionMatrix data_table_matrix() {
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    m.set(DxfVersion::R2000, Generation::None, ErrorBasis::Documented);
    m.error(DxfVersion::R2000, "DATATABLE is written as ACDBDATATABLE in DXF 2000");
    for (DxfVersion v : {DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        m.set(v, Generation::Native);
        m.autocad(v, AutoCadStatus::Verified);
    }
    m.autocad(DxfVersion::R2000, AutoCadStatus::Verified);
    m.autocad(DxfVersion::R13, AutoCadStatus::NotSavable);
    m.autocad(DxfVersion::R14, AutoCadStatus::NotSavable);
    return m;
}

VersionMatrix legacy_data_table_matrix() {
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    m.set(DxfVersion::R2000, Generation::Native);
    m.autocad(DxfVersion::R2000, AutoCadStatus::Verified);
    for (DxfVersion v : {DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        m.set(v, Generation::None, ErrorBasis::Documented);
        m.error(v, "ACDBDATATABLE is DXF 2000's record: DXF 2004 and later write DATATABLE");
        m.autocad(v, AutoCadStatus::Verified);
    }
    m.autocad(DxfVersion::R13, AutoCadStatus::NotSavable);
    m.autocad(DxfVersion::R14, AutoCadStatus::NotSavable);
    return m;
}

// ---------------------------------------------------------------------------
// The pre-2007 round trip of a table
// ---------------------------------------------------------------------------

std::string number(int64_t v) { return std::to_string(v); }

DxfGroup group(int code, std::string value) {
    DxfGroup g;
    g.code = code;
    g.value = std::move(value);
    return g;
}

std::string hex_of(const std::vector<uint8_t>& bytes) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (uint8_t b : bytes) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

// The groups of a cell's value in its XRECORD: AutoCAD's AcValue without the closing marker
std::vector<DxfGroup> cell_value_groups(const TableValue& v) {
    std::vector<DxfGroup> out;
    out.push_back(group(93, number(static_cast<int32_t>(v.flags))));
    out.push_back(group(90, number(static_cast<int32_t>(v.type))));
    if (v.has_value()) {
        if (const int32_t* integer = std::get_if<int32_t>(&v.value)) {
            out.push_back(group(91, number(*integer)));
        } else if (const double* real = std::get_if<double>(&v.value)) {
            out.push_back(group(140, format_double(*real, NumberStyle::AutoCad)));
        } else if (const std::string* text = std::get_if<std::string>(&v.value)) {
            out.push_back(group(1, *text));
        } else if (const TableDate* date = std::get_if<TableDate>(&v.value)) {
            std::vector<uint8_t> bytes = table_date_to_bytes(*date);
            out.push_back(group(92, number(static_cast<int64_t>(bytes.size()))));
            out.push_back(group(310, hex_of(bytes)));
        } else if (const auto* buffer = std::get_if<std::vector<uint8_t>>(&v.value)) {
            out.push_back(group(92, number(static_cast<int64_t>(buffer->size()))));
            if (!buffer->empty()) out.push_back(group(310, hex_of(*buffer)));
        } else if (const Point2* p2 = std::get_if<Point2>(&v.value)) {
            out.push_back(group(11, format_double(p2->x, NumberStyle::AutoCad)));
            out.push_back(group(21, format_double(p2->y, NumberStyle::AutoCad)));
        } else if (const Point3* p3 = std::get_if<Point3>(&v.value)) {
            out.push_back(group(11, format_double(p3->x, NumberStyle::AutoCad)));
            out.push_back(group(21, format_double(p3->y, NumberStyle::AutoCad)));
            out.push_back(group(31, format_double(p3->z, NumberStyle::AutoCad)));
        } else if (const TableHandle* handle = std::get_if<TableHandle>(&v.value)) {
            out.push_back(group(330, handle->handle.empty() ? std::string("0") : handle->handle));
        }
    }
    out.push_back(group(94, number(v.unit_type)));
    out.push_back(group(300, v.format));
    out.push_back(group(302, v.text));
    return out;
}

Object cell_record(const std::string& handle, const std::string& owner, const TableValue& value) {
    Object o;
    o.type = ObjectType::XRecord;
    o.handle = handle;
    o.owner = owner;
    XRecordData x;
    x.cloning = 1;
    x.data = cell_value_groups(value);
    o.data = std::move(x);
    return o;
}

// The checksum a cell's text has in the data table: its text's (0 for a cell that is a field or a block: the
// expanded text of a field is not the cell's)
double cell_checksum(const TableCell& cell) {
    if (cell.type != TableCellType::Text || !cell.field_handle.empty()) return 0.0;
    return table_text_checksum(table_value_text(cell.value));
}

bool is_cells_shape(const DataTableData& d) {
    return d.columns.size() == 4 && d.columns[0].type == DataColumnType::Double && d.columns[1].type == DataColumnType::Integer &&
           d.columns[2].type == DataColumnType::Integer && d.columns[3].type == DataColumnType::HardOwnerId;
}

} // namespace

std::vector<DxfGroup> pre2007_table_groups(size_t rows, size_t columns, const std::string& data_table_handle) {
    return {group(102, kPre2007TableMarker), group(90, number(static_cast<int64_t>(rows))),
            group(91, number(static_cast<int64_t>(columns))), group(102, kPre2007CellMarker),
            group(360, data_table_handle)};
}

std::string pre2007_data_table_handle(const XRecordData& x) {
    bool in_cells = false;
    for (const DxfGroup& g : x.data) {
        if (g.code == 102) in_cells = g.value == kPre2007CellMarker;
        else if (in_cells && g.code == 360) return g.value;
    }
    return std::string();
}

bool pre2007_table_size(const XRecordData& x, size_t& rows, size_t& columns) {
    bool in_table = false;
    int seen = 0;
    for (const DxfGroup& g : x.data) {
        if (g.code == 102) {
            in_table = g.value == kPre2007TableMarker;
            seen = 0;
        } else if (in_table && g.code == 90 && seen == 0) {
            rows = static_cast<size_t>(std::strtoull(g.value.c_str(), nullptr, 10));
            seen = 1;
        } else if (in_table && g.code == 91 && seen == 1) {
            columns = static_cast<size_t>(std::strtoull(g.value.c_str(), nullptr, 10));
            return true;
        }
    }
    return false;
}

void set_pre2007_table_size(XRecordData& x, size_t rows, size_t columns) {
    bool in_table = false;
    int seen = 0;
    for (DxfGroup& g : x.data) {
        if (g.code == 102) {
            in_table = g.value == kPre2007TableMarker;
            seen = 0;
        } else if (in_table && g.code == 90 && seen == 0) {
            g.value = number(static_cast<int64_t>(rows));
            seen = 1;
        } else if (in_table && g.code == 91 && seen == 1) {
            g.value = number(static_cast<int64_t>(columns));
            return;
        }
    }
}

std::string make_pre2007_table_records(const TableData& t, DxfVersion version, const std::string& owner,
                                       const std::function<std::string()>& handle, std::vector<Object>& out) {
    const size_t cells = t.rows() * t.columns();
    const std::string table_handle = handle();
    DataTableData d;
    d.version = 2;
    d.rows = static_cast<int32_t>(cells);
    DataColumn checksums, flags_a, flags_b, owned;
    checksums.type = DataColumnType::Double;
    flags_a.type = DataColumnType::Integer;
    flags_b.type = DataColumnType::Integer;
    owned.type = DataColumnType::HardOwnerId;
    std::vector<Object> records;
    for (size_t i = 0; i < cells; ++i) {
        const TableCell& cell = t.cells[i];
        checksums.values.push_back(cell_checksum(cell));
        flags_a.values.push_back(int32_t(0));
        flags_b.values.push_back(int32_t(0));
        const std::string h = handle();
        owned.values.push_back(DataTableObject{DataTableObjectKind::HardOwner, h});
        records.push_back(cell_record(h, table_handle, cell.value));
    }
    d.columns = {std::move(checksums), std::move(flags_a), std::move(flags_b), std::move(owned)};

    Object table;
    table.type = version == DxfVersion::R2000 ? ObjectType::LegacyDataTable : ObjectType::DataTable;
    table.handle = table_handle;
    table.owner = owner;
    table.data = Boxed<DataTableData>(std::move(d));
    out.push_back(std::move(table));
    for (Object& r : records) out.push_back(std::move(r));
    return table_handle;
}

Pre2007Update update_pre2007_table_records(Drawing& dwg, Object& data_table, const TableData& t,
                                           const std::function<std::string()>& handle) {
    Pre2007Update result;
    DataTableData& d = data_table.as_data_table();
    // A data table that is not the cells' (AutoCAD's four columns) is somebody else's: not touched
    if (!is_cells_shape(d)) return result;
    const size_t cells = t.rows() * t.columns();
    if (d.columns[0].values.size() != cells) {
        // The number of cells changed: the cells move in the row-by-row order, so the whole table is made anew from the entity
        for (const DataTableValue& v : d.columns[3].values) {
            if (const DataTableObject* o = std::get_if<DataTableObject>(&v)) result.removed.push_back(o->handle);
        }
        std::vector<Object> fresh;
        make_pre2007_table_records(t, data_table.type == ObjectType::LegacyDataTable ? DxfVersion::R2000 : DxfVersion::R2004,
                                   data_table.owner, handle, fresh);
        // The data table keeps its handle; its cells are the new ones
        DataTableData made = std::move(fresh.front().as_data_table());
        const std::string table_handle = data_table.handle;
        for (size_t i = 1; i < fresh.size(); ++i) {
            fresh[i].owner = table_handle;
            result.added.push_back(std::move(fresh[i]));
        }
        // the handle of the data table that was made is not used
        d = std::move(made);
        return result;
    }
    for (size_t i = 0; i < cells; ++i) {
        const TableCell& cell = t.cells[i];
        if (cell.type != TableCellType::Text || !cell.field_handle.empty()) continue;      // a field or a block: as it is
        const double expected = cell_checksum(cell);
        const double* stored = std::get_if<double>(&d.columns[0].values[i]);
        if (stored && *stored == expected) continue;                                       // the data table has this text
        d.columns[0].values[i] = expected;
        const DataTableObject* record = std::get_if<DataTableObject>(&d.columns[3].values[i]);
        Object* x = record ? dwg.find_object(record->handle) : nullptr;
        if (x && x->type == ObjectType::XRecord) {
            x->as_xrecord().data = cell_value_groups(cell.value);
        } else {
            const std::string h = handle();
            d.columns[3].values[i] = DataTableObject{DataTableObjectKind::HardOwner, h};
            result.added.push_back(cell_record(h, data_table.handle, cell.value));
        }
    }
    return result;
}

void register_data_table_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "ACDBDATATABLE";
        c.type = ObjectType::LegacyDataTable;
        c.read = read_legacy_data_table;
        c.write = write_legacy_data_table;
        c.references = data_table_references;
        c.cls = class_of();
        c.versions = legacy_data_table_matrix();
        c.refs = data_table_refs("ACDBDATATABLE");
        c.lists = {whole_subclass("ACDBDATATABLE")};
        c.step = 8;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "DATATABLE";
        c.type = ObjectType::DataTable;
        c.read = read_data_table;
        c.write = write_data_table;
        c.references = data_table_references;
        c.cls = class_of();
        c.versions = data_table_matrix();
        c.refs = data_table_refs("AcDbDataTable");
        c.lists = {whole_subclass("AcDbDataTable")};
        c.step = 8;
        registry.add(std::move(c));
    }
}

// ---------------------------------------------------------------------------
// The drawing's side
// ---------------------------------------------------------------------------

} // namespace internal

Object& Drawing::add_data_table(const DataTableData& data, const std::string& owner) {
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    scan_handles();
    Object o;
    o.type = version_ == DxfVersion::R2000 ? ObjectType::LegacyDataTable : ObjectType::DataTable;
    o.handle = next_handle();
    o.owner = owner.empty() ? std::string("0") : owner;
    o.data = Boxed<DataTableData>(data);
    objects_.push_back(std::move(o));
    return objects_.back();
}

Object* Drawing::table_round_trip_data(const Entity& table) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->table_round_trip_data(table));
}

const Object* Drawing::table_round_trip_data(const Entity& table) const {
    if (table.type != EntityType::Table || table.props.xdictionary.empty()) return nullptr;
    const Object* dict = find_object(table.props.xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const DictionaryEntry& entry : dict->as_dictionary().entries) {
        if (entry.name != "ACAD_XREC_ROUNDTRIP") continue;
        const Object* x = find_object(entry.handle);
        if (!x || x->type != ObjectType::XRecord) return nullptr;
        const Object* data = find_object(internal::pre2007_data_table_handle(x->as_xrecord()));
        if (data && (data->type == ObjectType::DataTable || data->type == ObjectType::LegacyDataTable)) return data;
        return nullptr;
    }
    return nullptr;
}

} // namespace hdxf
