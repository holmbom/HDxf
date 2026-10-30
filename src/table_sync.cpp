#include "hdxf.hpp"
#include "TableTypes.hpp"
#include "DocumentDataTypes.hpp"
#include "TableLayout.hpp"
#include "handles.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

// The tables in a drawing (phase 9, step 9.1; plan B108): the table style with its cell style map, a table with
// its anonymous block *T, and the three records that hold what the entity cannot — the TABLECONTENT, the
// TABLEGEOMETRY and the round-trip dictionary (the table's extension dictionary, ACAD_XREC_ROUNDTRIP, whose
// XRECORD owns them). AutoCAD 2008 and later write both the entity and the companions, and take the entity's text when a cell's
// text no longer has the checksum the content keeps for it (ACAD_ROUNDTRIP_2008_CELL_CHECKSUM, the sum of every
// UTF-16 unit of the text times its position): that is the rule here too. A table's companions are brought up to
// date for the cells that changed, never for the ones that did not (a table read from a file and unchanged is
// written as it was).
//
// The sizes of the cells (TABLEGEOMETRY), the drawing of the table (the *T block, the proxy graphics) are the
// layout of step 9.5; table_layout_hook() is its hook, called after a table's companions are made or changed.

namespace hdxf {

namespace {

bool same_name_ci(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

uint64_t handle_number(const std::string& h) {
    uint64_t v = 0;
    return internal::parse_handle(h, v) == internal::HandleParse::Ok ? v : 0;
}

bool same_handle_text(const std::string& a, const std::string& b) {
    uint64_t x = handle_number(a), y = handle_number(b);
    return x != 0 && x == y;
}

void insert_entry_sorted(DictionaryData& dict, const std::string& name, const std::string& handle, bool hard_owner) {
    auto at = std::find_if(dict.entries.begin(), dict.entries.end(), [&](const DictionaryEntry& e) { return e.name > name; });
    dict.entries.insert(at, {name, handle, hard_owner});
}

Object make_object(ObjectType type, const std::string& handle, const std::string& owner, ObjectData data) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = owner;
    if (owner != "0") o.reactors.push_back(owner);
    o.data = std::move(data);
    return o;
}

constexpr const char* kStyleDictionary = "ACAD_TABLESTYLE";
constexpr const char* kStyleMapEntry = "ACAD_ROUNDTRIP_2008_TABLESTYLE_CELLSTYLEMAP";
constexpr const char* kRoundTripEntry = "ACAD_XREC_ROUNDTRIP";
constexpr const char* kTableEntityMarker = "ACAD_ROUNDTRIP_2008_TABLE_ENTITY";

// The default table flag AutoCAD's tables have (90)
constexpr uint32_t kTableFlags = 22;

// ---- the cell styles of a style: AutoCAD's three (_TITLE, _HEADER, _DATA) as its own file has them

TableCellStyle cell_style_of(const TableStyleData& style, int row_index, int id, int kind, const char* name) {
    const TableStyleRow& row = style.rows[row_index];
    TableCellStyle cs;
    cs.id = id;
    cs.kind = kind;
    cs.name = name;
    TableFormatData& f = cs.format;
    f.kind = 5;
    f.has_format = true;
    f.property_flags = 0;
    f.merge_flags = row_index == 1 ? 32768 : 0;         // the title row merges all its cells
    f.background = Color::from_aci(257);
    f.flags = 1;
    f.content.property_flags = 0;
    f.content.extended_flags = 0;
    f.content.data_type = 512;
    f.content.unit_type = 0;
    f.content.rotation = 0.0;
    f.content.scale = 1.0;
    f.content.alignment = row.alignment;
    f.content.color = Color::from_aci(row.text_color);
    f.content.text_style_handle = "0";
    f.content.text_height = row.text_height;
    f.has_margins = true;
    f.margins.top = style.vertical_margin;
    f.margins.left = style.horizontal_margin;
    f.margins.bottom = style.vertical_margin;
    f.margins.right = style.horizontal_margin;
    return cs;
}

CellStyleMapData cell_style_map_of(const TableStyleData& style) {
    CellStyleMapData map;
    map.styles.push_back(cell_style_of(style, 1, 1, 1, "_TITLE"));
    map.styles.push_back(cell_style_of(style, 2, 2, 1, "_HEADER"));
    map.styles.push_back(cell_style_of(style, 0, 3, 2, "_DATA"));
    return map;
}

} // namespace

// ---------------------------------------------------------------------------
// The layout hook
// ---------------------------------------------------------------------------

namespace internal {

namespace {
TableLayoutHook g_layout_hook = nullptr;
} // namespace

void set_table_layout_hook(TableLayoutHook hook) { g_layout_hook = hook; }
TableLayoutHook table_layout_hook() { return g_layout_hook ? g_layout_hook : &apply_table_layout; }   // step 9.5

namespace {

TableValue checksum_value(double sum) {
    TableValue v;
    v.type = TableValueType::Double;
    v.flags = 2;
    v.value = sum;
    return v;
}

TableContentFormat content_format_for(const TableValue& value) {
    TableContentFormat f;
    f.property_flags = 3;
    f.data_type = static_cast<int32_t>(value.type);
    f.format_string = value.format;
    return f;
}

// The content of a text cell: its AcValue, with the format AutoCAD's own cells have; none for an empty cell
void set_text_content(TableContentCell& cell, const TableCell& inline_cell) {
    // The content's own format stays (its alignment, text height …); the value's type and format string are the
    // new value's, as AutoCAD's own file does when a cell's text is set (the type becomes 4, the format string goes)
    std::optional<TableContentFormat> kept;
    if (!cell.contents.empty() && cell.contents[0].kind == TableContentKind::Value) kept = cell.contents[0].format;
    cell.contents.clear();
    const std::string text = table_value_text(inline_cell.value);
    if (!inline_cell.value.has_value() && text.empty()) return;
    TableCellContent content;
    content.kind = TableContentKind::Value;
    content.value = inline_cell.value;
    content.format = content_format_for(inline_cell.value);
    if (kept) {
        content.format = *kept;
        content.format->data_type = static_cast<int32_t>(inline_cell.value.type);
        content.format->format_string = inline_cell.value.format;
    }
    cell.contents.push_back(std::move(content));
}

TableContentCell make_content_cell(const TableCell& inline_cell) {
    TableContentCell cell;
    TableCustomData sum;
    sum.name = kTableChecksumName;
    sum.value = checksum_value(inline_cell.type == TableCellType::Text && inline_cell.field_handle.empty()
                                   ? table_text_checksum(table_value_text(inline_cell.value))
                                   : 0.0);
    cell.custom_data.push_back(std::move(sum));
    cell.format.kind = 1;
    cell.format.has_format = false;
    if (inline_cell.type == TableCellType::Block) {
        TableCellContent content;
        content.kind = TableContentKind::Block;
        content.handle = inline_cell.block_handle;
        content.format = TableContentFormat();
        cell.contents.push_back(std::move(content));
    } else if (inline_cell.field_handle.empty()) {
        set_text_content(cell, inline_cell);
    }
    return cell;
}

TableContentColumn make_content_column(double width) {
    TableContentColumn column;
    column.format.kind = 3;
    column.width = width;
    return column;
}

TableContentRow make_content_row(size_t row, double height, size_t columns) {
    TableContentRow r;
    r.format.kind = 2;
    r.row_type = row == 0 ? 1 : (row == 1 ? 2 : 3);     // the title row, the header row, data (AutoCAD's own)
    r.height = height;
    r.cells.resize(columns);
    return r;
}

// The merged ranges the entity's cells say: the first cell of a range has the extent (175 columns, 176 rows)
std::vector<TableMergeRange> merges_of(const TableData& t) {
    std::vector<TableMergeRange> out;
    for (size_t r = 0; r < t.rows(); ++r) {
        for (size_t c = 0; c < t.columns(); ++c) {
            const TableCell& cell = t.cells[r * t.columns() + c];
            if (cell.merged != 0 || (cell.merge_columns <= 1 && cell.merge_rows <= 1)) continue;
            TableMergeRange m;
            m.top_row = static_cast<int32_t>(r);
            m.left_column = static_cast<int32_t>(c);
            m.bottom_row = static_cast<int32_t>(r) + std::max<int>(cell.merge_rows, 1) - 1;
            m.right_column = static_cast<int32_t>(c) + std::max<int>(cell.merge_columns, 1) - 1;
            out.push_back(m);
        }
    }
    return out;
}

} // namespace

TableContentData table_content_from_entity(const TableData& t, const std::string& style_handle, bool full_form) {
    (void)full_form;
    TableContentData d;
    d.table_format.kind = 4;
    d.style_handle = style_handle;
    for (double w : t.column_widths) d.columns.push_back(make_content_column(w));
    for (size_t r = 0; r < t.rows(); ++r) {
        TableContentRow row = make_content_row(r, t.row_heights[r], t.columns());
        for (size_t c = 0; c < t.columns(); ++c) row.cells[c] = make_content_cell(t.cells[r * t.columns() + c]);
        d.rows.push_back(std::move(row));
    }
    d.merges = merges_of(t);
    return d;
}

TableGeometryData table_geometry_from_entity(const TableData& t) {
    TableGeometryData g;
    g.rows = static_cast<int32_t>(t.rows());
    g.columns = static_cast<int32_t>(t.columns());
    for (size_t r = 0; r < t.rows(); ++r) {
        for (size_t c = 0; c < t.columns(); ++c) {
            const TableCell& cell = t.cells[r * t.columns() + c];
            TableCellGeometry cg;
            // AutoCAD's smallest cell (flags 7, 3 x 2: what its own files have for an empty cell, merged away or not)
            // until the layout gives the cell its size
            (void)cell;
            cg.flags = 7;
            cg.width = 3.0;
            cg.height = 2.0;
            cg.handle = "0";
            g.cells.push_back(std::move(cg));
        }
    }
    return g;
}

} // namespace internal

// ---------------------------------------------------------------------------
// A table style
// ---------------------------------------------------------------------------

namespace {

Object* style_in(Drawing& dwg, const std::string& dictionary_handle, const std::string& name) {
    Object* dict = dwg.find_object(dictionary_handle);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const auto& entry : dict->as_dictionary().entries) {
        Object* o = dwg.find_object(entry.handle);
        if (same_name_ci(entry.name, name) && o && o->type == ObjectType::TableStyle) return o;
    }
    return nullptr;
}

} // namespace

Object& Drawing::add_table_style(const std::string& name, const TableStyleData& data) {
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    const std::string dict_handle = named_dictionary(kStyleDictionary);
    if (Object* existing = style_in(*this, dict_handle, name)) return *existing;

    scan_handles();
    const std::string handle = next_handle();
    TableStyleData style = data;
    // AutoCAD's own forms: 2010 and later have the class version, 2007 and later the data type, unit type and
    // format string of a row type
    if (!style.class_version && version_ >= DxfVersion::R2010) style.class_version = int16_t{0};
    if (!style.description) style.description = std::string();
    if (version_ >= DxfVersion::R2007) {
        for (TableStyleRow& row : style.rows) {
            if (!row.data_type) row.data_type = 512;
            if (!row.unit_type) row.unit_type = 0;
            if (!row.format) row.format = std::string();
        }
    }
    if (Object* dict = find_object(dict_handle)) insert_entry_sorted(dict->as_dictionary(), name, handle, false);

    Object o = make_object(ObjectType::TableStyle, handle, dict_handle, Boxed<TableStyleData>(style));
    // Its extension dictionary with the CELLSTYLEMAP, as AutoCAD's Standard has it
    const std::string xdict = next_handle();
    const std::string map = next_handle();
    o.xdictionary = xdict;
    objects_.push_back(std::move(o));
    DictionaryData xd;
    xd.hard_owner = true;
    xd.entries.push_back({kStyleMapEntry, map, true});
    Object xdict_object = make_object(ObjectType::Dictionary, xdict, handle, xd);
    xdict_object.reactors.clear();
    objects_.push_back(std::move(xdict_object));
    objects_.push_back(make_object(ObjectType::CellStyleMap, map, xdict, Boxed<CellStyleMapData>(cell_style_map_of(style))));
    return *find_object(handle);
}

const Object* Drawing::table_style(const Entity& table) const {
    if (table.type != EntityType::Table) return nullptr;
    const Object* o = find_object(table.as_table().style_handle);
    return o && o->type == ObjectType::TableStyle ? o : nullptr;
}

const Object* Drawing::table_cell_style_map(const Object& style) const {
    if (style.type != ObjectType::TableStyle || style.xdictionary.empty()) return nullptr;
    const Object* dict = find_object(style.xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const auto& entry : dict->as_dictionary().entries) {
        if (entry.name != kStyleMapEntry) continue;
        const Object* o = find_object(entry.handle);
        return o && o->type == ObjectType::CellStyleMap ? o : nullptr;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// A table
// ---------------------------------------------------------------------------

Entity& Drawing::add_table(Point3 insertion, size_t rows, size_t columns, double row_height, double column_width,
                           const std::string& style, const std::string& layer) {
    TableData d;
    d.insertion = insertion;
    d.flags = kTableFlags;
    if (version_ >= DxfVersion::R2010) d.data_version = int16_t{0};
    d.row_heights.assign(rows, row_height);
    d.column_widths.assign(columns, column_width);
    TableCell empty;
    empty.merge_columns = 1;
    empty.merge_rows = 1;
    d.cells.assign(rows * columns, empty);
    // The title row, merged across the table as AutoCAD's own TABLE command makes it
    if (rows > 0 && columns > 1) {
        d.cells[0].merge_columns = static_cast<int16_t>(columns);
        for (size_t c = 1; c < columns; ++c) d.cells[c].merged = 1;
    }

    // The style: the one of that name, Standard made when it is not there
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    const std::string dict_handle = named_dictionary(kStyleDictionary);
    Object* style_object = style_in(*this, dict_handle, style);
    if (!style_object && same_name_ci(style, "Standard")) style_object = &add_table_style("Standard");
    d.style_handle = style_object ? style_object->handle : std::string("0");

    // Its anonymous block *Tn, the table's drawing (empty until the layout of step 9.5)
    scan_handles();
    size_t number = 0;
    for (const Block& b : blocks_) {
        if (b.name.size() > 2 && b.name[0] == '*' && (b.name[1] == 'T' || b.name[1] == 't')) {
            size_t n = static_cast<size_t>(std::atoll(b.name.c_str() + 2));
            number = std::max(number, n);
        }
    }
    Block block;
    block.name = "*T" + std::to_string(number + 1);
    block.flags = 1;
    block.record.handle = next_handle();
    block.handle = next_handle();
    block.endblk_handle = next_handle();
    d.block_name = block.name;
    d.block_record_handle = block.record.handle;
    std::string table_handle = next_handle();
    block.record.block_refs.push_back(table_handle);
    blocks_.push_back(std::move(block));
    rebuild_block_index();

    Entity e;
    e.type = EntityType::Table;
    e.props.layer = layer;
    e.props.handle = table_handle;
    e.data = Boxed<TableData>(std::move(d));
    entities_.push_back(std::move(e));
    return entities_.back();
}

void Drawing::set_table_cell_text(Entity& table, size_t row, size_t column, const std::string& text) {
    if (table.type != EntityType::Table) return;
    TableData& d = table.as_table();
    TableCell* cell = d.cell(row, column);
    if (!cell) return;
    cell->value = text.empty() ? TableValue::unset() : TableValue::from_text(text);
    // AutoCAD's cells with content have 0x40000 in their override flag (2007 and later)
    if (version_ >= DxfVersion::R2007) {
        cell->override_flags = text.empty() ? (cell->override_flags & ~0x40000) : (cell->override_flags | 0x40000);
    }
}

std::string Drawing::table_cell_text(const Entity& table, size_t row, size_t column) const {
    if (table.type != EntityType::Table) return {};
    const TableCell* cell = table.as_table().cell(row, column);
    if (!cell) return {};
    if (!cell->field_handle.empty()) {
        // a field cell shows what its field was last evaluated to (the field is not evaluated here)
        const Object* field = find_object(cell->field_handle);
        return field ? field_text(*field) : std::string();
    }
    return internal::table_value_text(cell->value);
}

// ---- the companions

namespace {

struct Companions {
    Object* xrecord = nullptr;
    Object* content = nullptr;
    Object* geometry = nullptr;
};

// The XRECORD of the extension dictionary's ACAD_XREC_ROUNDTRIP and the TABLECONTENT (360) and TABLEGEOMETRY (361)
// its ACAD_ROUNDTRIP_2008_TABLE_ENTITY names
Companions companions_of(Drawing& dwg, const Entity& table) {
    Companions out;
    if (table.type != EntityType::Table || table.props.xdictionary.empty()) return out;
    Object* dict = dwg.find_object(table.props.xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return out;
    for (const auto& entry : dict->as_dictionary().entries) {
        if (entry.name != kRoundTripEntry) continue;
        Object* x = dwg.find_object(entry.handle);
        if (!x || x->type != ObjectType::XRecord) return out;
        out.xrecord = x;
        bool in_entity = false;
        for (const DxfGroup& g : x->as_xrecord().data) {
            if (g.code == 102) in_entity = g.value == kTableEntityMarker;
            else if (in_entity && g.code == 360 && !out.content) {
                Object* o = dwg.find_object(g.value);
                if (o && o->type == ObjectType::TableContent) out.content = o;
            } else if (in_entity && g.code == 361 && !out.geometry) {
                Object* o = dwg.find_object(g.value);
                if (o && o->type == ObjectType::TableGeometry) out.geometry = o;
            }
        }
        break;
    }
    return out;
}

} // namespace

Object* Drawing::table_content(const Entity& table) { return companions_of(*this, table).content; }
const Object* Drawing::table_content(const Entity& table) const {
    return companions_of(const_cast<Drawing&>(*this), table).content;
}
Object* Drawing::table_geometry(const Entity& table) { return companions_of(*this, table).geometry; }
const Object* Drawing::table_geometry(const Entity& table) const {
    return companions_of(const_cast<Drawing&>(*this), table).geometry;
}

namespace {

// A table's companions as the entity says them: made when there are none
// The 2008 part of the round-trip XRECORD, the groups of AutoCAD's files from 2000 on: the content (360), the geometry
// (361) and a 90 before it that is the number of rows less one in every one of them
std::vector<DxfGroup> entity_part_groups(const TableData& t, const std::string& content_handle,
                                         const std::string& geometry_handle) {
    std::vector<DxfGroup> out;
    auto group = [&](int code, const std::string& value) { out.push_back({code, value, 0}); };
    group(102, kTableEntityMarker);
    group(360, content_handle);
    group(70, "2");
    group(90, "1");
    group(10, "0.0");
    group(20, "0.0");
    group(30, "0.0");
    group(90, "0");
    group(90, std::to_string(t.rows() > 0 ? t.rows() - 1 : 0));
    group(361, geometry_handle);
    return out;
}

// The TABLECONTENT and TABLEGEOMETRY of the entity's cells, owned by the round-trip XRECORD
void make_entity_records(const TableData& t, const std::string& record_handle, const std::string& content_handle,
                         const std::string& geometry_handle, std::vector<Object>& objects) {
    objects.push_back(make_object(ObjectType::TableContent, content_handle, record_handle,
                                  Boxed<TableContentData>(internal::table_content_from_entity(t, t.style_handle, true))));
    objects.back().reactors.clear();
    objects.push_back(make_object(ObjectType::TableGeometry, geometry_handle, record_handle,
                                  Boxed<TableGeometryData>(internal::table_geometry_from_entity(t))));
    objects.back().reactors.clear();
}

// A table's companions as the entity says them: made when there are none. DXF 2000 and 2004 (step 9.8) have the data
// table of the cells in the same XRECORD (and in an empty table nothing else: `entity_part` false); the TABLECONTENT
// and TABLEGEOMETRY are made when a cell has content
void make_companions(Drawing& dwg, Entity& table, std::vector<Object>& objects, const std::function<std::string()>& handle,
                     bool entity_part, DxfVersion pre2007_version) {
    TableData& t = table.as_table();
    const bool pre2007 = pre2007_version < DxfVersion::R2007;
    Object* existing_dict = table.props.xdictionary.empty() ? nullptr : dwg.find_object(table.props.xdictionary);
    const bool new_dict = !existing_dict || existing_dict->type != ObjectType::Dictionary;
    std::string dict_handle = table.props.xdictionary;
    // AutoCAD's empty 2000/2004 table: the dictionary, its XRECORD, the data table and the cells' XRECORDs, in that order
    if (!entity_part && new_dict) dict_handle = handle();
    const std::string content_handle = entity_part ? handle() : std::string();
    const std::string geometry_handle = entity_part ? handle() : std::string();
    const std::string record_handle = handle();

    if (new_dict) {
        if (entity_part) dict_handle = handle();
        DictionaryData dd;
        dd.hard_owner = true;
        dd.entries.push_back({kRoundTripEntry, record_handle, true});
        Object dict = make_object(ObjectType::Dictionary, dict_handle, table.props.handle, dd);
        dict.reactors.clear();
        objects.push_back(std::move(dict));
        table.props.xdictionary = dict_handle;
    } else {
        insert_entry_sorted(existing_dict->as_dictionary(), kRoundTripEntry, record_handle, true);
    }

    // AutoCAD's round-trip record: the 2008 part (the groups of the files of AutoCAD 2000 and later) for a table with
    // content, then, before 2007, the part of the data table of the cells
    XRecordData x;
    x.cloning = 1;
    if (entity_part) x.data = entity_part_groups(t, content_handle, geometry_handle);
    std::vector<Object> pre2007_records;
    if (pre2007) {
        const std::string data_table = internal::make_pre2007_table_records(t, pre2007_version, record_handle, handle, pre2007_records);
        for (DxfGroup& g : internal::pre2007_table_groups(t.rows(), t.columns(), data_table)) x.data.push_back(std::move(g));
    }
    objects.push_back(make_object(ObjectType::XRecord, record_handle, dict_handle, x));
    for (Object& o : pre2007_records) objects.push_back(std::move(o));
    if (entity_part) make_entity_records(t, record_handle, content_handle, geometry_handle, objects);
}

bool table_has_content(const TableData& t) {
    for (const TableCell& cell : t.cells) {
        if (cell.value.has_value() || !internal::table_value_text(cell.value).empty() || !cell.field_handle.empty() ||
            cell.type == TableCellType::Block) {
            return true;
        }
    }
    return false;
}

bool cells_equal(const TableCell& a, const TableCell& b) {
    return a.type == b.type && a.flags == b.flags && a.merged == b.merged && a.auto_fit == b.auto_fit &&
           a.merge_columns == b.merge_columns && a.merge_rows == b.merge_rows && a.override_flags == b.override_flags &&
           a.virtual_edge == b.virtual_edge && a.rotation == b.rotation && a.value == b.value &&
           a.field_handle == b.field_handle && a.block_handle == b.block_handle && a.block_scale == b.block_scale &&
           a.attributes == b.attributes && a.extended_flags == b.extended_flags && a.overrides == b.overrides;
}

bool tables_equal(const TableData& a, const TableData& b) {
    if (a.style_handle != b.style_handle || a.row_heights != b.row_heights || a.column_widths != b.column_widths ||
        a.cells.size() != b.cells.size() || !(a.overrides == b.overrides) || a.flags != b.flags) {
        return false;
    }
    for (size_t i = 0; i < a.cells.size(); ++i) if (!cells_equal(a.cells[i], b.cells[i])) return false;
    return true;
}

TableCustomData* checksum_item(TableContentCell& cell) {
    for (TableCustomData& item : cell.custom_data) if (item.name == internal::kTableChecksumName) return &item;
    return nullptr;
}

} // namespace

// Brings a table's companions up to date with its entity: the cells whose text no longer has the checksum the
// content keeps for it get the text and the new checksum; rows, columns, merged ranges, sizes and the style follow
void Drawing::sync_table(Entity& table) {
    if (table.type != EntityType::Table) return;
    if (version_ < DxfVersion::R2000) return;               // R12, R13 and R14 have none of these records
    TableData& t = table.as_table();
    scan_handles();
    if (table.props.handle.empty()) table.props.handle = next_handle();

    // DXF 2000 and 2004 keep the table's cells in a DATATABLE of their own too (step 9.8): AutoCAD makes it with every
    // table, also an empty one (the XRECORD of the round-trip dictionary names it)
    const bool pre2007 = version_ < DxfVersion::R2007;
    const std::function<std::string()> new_handle = [this] { return next_handle(); };
    Companions c = companions_of(*this, table);
    if (!c.content || !c.geometry) {
        // (a half-made set is not touched; the layout below runs whatever is made here)
        if (c.content || c.geometry) {
        } else if (!pre2007) {
            // AutoCAD's own empty table (the -TABLE command, fas9_tables.scr) has no extension dictionary and none of the
            // three records: they are made when a cell has a value, a field or a block
            if (!c.xrecord && table_has_content(t)) {
                std::vector<Object> made;
                make_companions(*this, table, made, new_handle, true, version_);
                for (Object& o : made) objects_.push_back(std::move(o));
                c = companions_of(*this, table);
            }
        } else if (!c.xrecord) {
            // A table without the round-trip record: its XRECORD with the data table of the cells; with the content and the
            // geometry when a cell has content (AutoCAD's empty 2000/2004 table has the data table alone)
            std::vector<Object> made;
            make_companions(*this, table, made, new_handle, table_has_content(t), version_);
            for (Object& o : made) objects_.push_back(std::move(o));
            c = companions_of(*this, table);
        } else if (table_has_content(t)) {
            // An empty table's XRECORD (AutoCAD's own, or the one made above) when a cell has content: the 2008 part
            // and its two records join the data table's part
            const std::string content_handle = next_handle();
            const std::string geometry_handle = next_handle();
            std::vector<DxfGroup> part = entity_part_groups(t, content_handle, geometry_handle);
            XRecordData& x = c.xrecord->as_xrecord();
            x.data.insert(x.data.begin(), part.begin(), part.end());
            const std::string record_handle = c.xrecord->handle;
            std::vector<Object> made;
            make_entity_records(t, record_handle, content_handle, geometry_handle, made);
            for (Object& o : made) objects_.push_back(std::move(o));
            c = companions_of(*this, table);
        }
    } else {
        TableContentData& content = c.content->as_table_content();
        TableGeometryData& geometry = c.geometry->as_table_geometry();
        const size_t rows = t.rows(), cols = t.columns();
        // columns and rows
        while (content.columns.size() < cols) content.columns.push_back(internal::make_content_column(0.0));
        content.columns.resize(cols);
        while (content.rows.size() < rows) content.rows.push_back(internal::make_content_row(content.rows.size(), 0.0, cols));
        content.rows.resize(rows);
        for (size_t col = 0; col < cols; ++col) content.columns[col].width = t.column_widths[col];
        for (size_t r = 0; r < rows; ++r) {
            TableContentRow& row = content.rows[r];
            row.height = t.row_heights[r];
            while (row.cells.size() < cols) {
                row.cells.push_back(internal::make_content_cell(t.cells[r * cols + row.cells.size()]));
            }
            row.cells.resize(cols);
            for (size_t col = 0; col < cols; ++col) {
                const TableCell& ic = t.cells[r * cols + col];
                TableContentCell& cc = row.cells[col];
                if (ic.type != TableCellType::Text || !ic.field_handle.empty()) continue;   // a field or a block: as it is
                TableCustomData* sum = checksum_item(cc);
                const std::string text = internal::table_value_text(ic.value);
                const double expected = internal::table_text_checksum(text);
                const double* stored = sum ? std::get_if<double>(&sum->value.value) : nullptr;
                if (stored && *stored == expected) continue;                                // the content has this text
                internal::set_text_content(cc, ic);
                if (sum) {
                    sum->value = internal::checksum_value(expected);
                } else {
                    TableCustomData item;
                    item.name = internal::kTableChecksumName;
                    item.value = internal::checksum_value(expected);
                    cc.custom_data.insert(cc.custom_data.begin(), std::move(item));
                }
            }
        }
        content.merges = internal::merges_of(t);
        content.style_handle = t.style_handle;
        // the geometry: its cells follow the entity's (new ones as AutoCAD's smallest)
        TableGeometryData wanted = internal::table_geometry_from_entity(t);
        geometry.rows = wanted.rows;
        geometry.columns = wanted.columns;
        std::vector<TableCellGeometry> cells = std::move(geometry.cells);
        geometry.cells.clear();
        for (size_t i = 0; i < wanted.cells.size(); ++i) {
            if (i < cells.size()) {
                geometry.cells.push_back(std::move(cells[i]));
            } else {
                geometry.cells.push_back(std::move(wanted.cells[i]));
            }
        }
        // the number of rows less one in the round-trip record
        if (c.xrecord) {
            bool in_entity = false;
            int nineties = 0;
            for (DxfGroup& g : c.xrecord->as_xrecord().data) {
                if (g.code == 102) { in_entity = g.value == kTableEntityMarker; nineties = 0; continue; }
                if (in_entity && g.code == 90 && ++nineties == 3 && rows > 0) g.value = std::to_string(rows - 1);
            }
        }
    }
    if (pre2007 && c.xrecord) {
        // The data table of the cells (before 2007) follows the entity: a table that has none gets it, one that has
        // brings the cells whose text changed up to date (their checksum and value record), the number of rows and columns
        XRecordData& x = c.xrecord->as_xrecord();
        const std::string data_handle = internal::pre2007_data_table_handle(x);
        if (data_handle.empty()) {
            const std::string record_handle = c.xrecord->handle;
            std::vector<Object> made;
            const std::string data_table = internal::make_pre2007_table_records(t, version_, record_handle, new_handle, made);
            for (DxfGroup& g : internal::pre2007_table_groups(t.rows(), t.columns(), data_table)) x.data.push_back(std::move(g));
            for (Object& o : made) objects_.push_back(std::move(o));
        } else if (Object* data = find_object(data_handle);
                   data && (data->type == ObjectType::DataTable || data->type == ObjectType::LegacyDataTable)) {
            internal::set_pre2007_table_size(x, t.rows(), t.columns());
            internal::Pre2007Update update = internal::update_pre2007_table_records(*this, *data, t, new_handle);
            for (const std::string& h : update.removed) {
                objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                              [&](const Object& o) { return same_handle_text(o.handle, h); }),
                               objects_.end());
            }
            for (Object& o : update.added) objects_.push_back(std::move(o));
        }
        c = companions_of(*this, table);
    }
    // The layout (step 9.5): the sizes of the cells, the rows' heights, the *T block, the proxy graphics
    if (internal::TableLayoutHook hook = internal::table_layout_hook()) {
        hook(*this, table, c.content ? &c.content->as_table_content() : nullptr,
             c.geometry ? &c.geometry->as_table_geometry() : nullptr, warnings_);
    }
}

// Every table: the new ones get their companions, a table read from a file and changed since gets its companions
// brought up to date
void Drawing::sync_table_records(const Drawing* original) {
    if (version_ < DxfVersion::R2000) return;
    auto run_layout = [&](Entity& e) {
        Companions c = companions_of(*this, e);
        if (internal::TableLayoutHook hook = internal::table_layout_hook()) {
            hook(*this, e, c.content ? &c.content->as_table_content() : nullptr,
                 c.geometry ? &c.geometry->as_table_geometry() : nullptr, warnings_);
        }
    };
    auto sync_list = [&](std::vector<Entity>& list) {
        for (size_t i = 0; i < list.size(); ++i) {
            Entity& e = list[i];
            if (e.type != EntityType::Table) continue;
            // A table the file had (found by its handle, whether or not its preserved record is still there)
            if (original && !e.props.handle.empty()) {
                const Entity* was = nullptr;
                for (const Entity& o : original->entities()) {
                    if (o.type == EntityType::Table && same_handle_text(o.props.handle, e.props.handle)) { was = &o; break; }
                }
                if (!was) {
                    for (const Block& b : original->blocks()) {
                        for (const Entity& o : b.entities) {
                            if (o.type == EntityType::Table && same_handle_text(o.props.handle, e.props.handle)) { was = &o; break; }
                        }
                        if (was) break;
                    }
                }
                if (was && tables_equal(was->as_table(), e.as_table())) {
                    // Unchanged: its companions as they were; its layout by its stamp (what it is laid out with - its
                    // style, a block its cells draw - may have changed, or it has been moved)
                    run_layout(e);
                    continue;
                }
                const Companions had = companions_of(*this, e);
                if (was && !had.content && !had.xrecord) {
                    // The file had none (neither the content nor, in DXF 2000/2004, the round-trip record): not made
                    // now, but the table is laid out
                    run_layout(e);
                    continue;
                }
            }
            sync_table(e);
        }
    };
    sync_list(entities_);
    for (Block& b : blocks_) sync_list(b.entities);
}

} // namespace hdxf
