#pragma once

// ACAD_TABLE and the objects it is made of: TABLESTYLE, CELLSTYLEMAP, TABLECONTENT, TABLEGEOMETRY.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// Tables (phase 9, step 9.1): ACAD_TABLE (2000+) with TABLESTYLE, CELLSTYLEMAP, TABLECONTENT and
// TABLEGEOMETRY. The group codes are AutoCAD's: its own files (doc/facit/acad_*_dxf_tables.dxf and the
// corpus of build/test-data) decide, the DXF reference (TABLE, TABLESTYLE group codes) where they show nothing.
// ---------------------------------------------------------------------------

// AcValue's data type (90): which value groups follow
enum class TableValueType : int32_t {
    Unset = 0,          // no value
    Long = 1,           // 91
    Double = 2,         // 140
    String = 4,         // 1
    Date = 8,           // 92 + 310: a Windows SYSTEMTIME (16 bytes, TableDate); 2000 and 2004 write eight bytes
                        // (a 64-bit count of seconds since 1970, or -1), held as the bytes
    Point2D = 16,       // 11, 21
    Point3D = 32,       // 11, 21, 31
    ObjectId = 64,      // 330
    Buffer = 128,       // 92 + 310
    ResBuf = 256,       // (no AutoCAD file has a value: not typed)
    General = 512       // no value in any AutoCAD file
};

// A date of an AcValue as AutoCAD writes it: the 16 bytes of a Windows SYSTEMTIME, eight 16-bit values
struct TableDate {
    uint16_t year = 0;
    uint16_t month = 0;
    uint16_t day_of_week = 0;
    uint16_t day = 0;
    uint16_t hour = 0;
    uint16_t minute = 0;
    uint16_t second = 0;
    uint16_t millisecond = 0;

    bool operator==(const TableDate& o) const {
        return year == o.year && month == o.month && day_of_week == o.day_of_week && day == o.day &&
               hour == o.hour && minute == o.minute && second == o.second && millisecond == o.millisecond;
    }
};

// A handle held by an AcValue (type ObjectId, 330)
struct TableHandle {
    std::string handle;
    bool operator==(const TableHandle& o) const { return handle == o.handle; }
};

// AcValue: a typed value with its unit type, format string and formatted text. DXF 2007 and later write
// `93 flags, 90 type, <value>, 94 unit, 300 format, 302 text, 304 ACVALUE_END`; 2000 and 2004 only
// `90 type, <value>`. The type says which value groups AutoCAD wrote; a type with no value there (an empty
// cell: Unset, flags 7) has no value. The empty string and no value are two things.
struct TableValue {
    TableValueType type = TableValueType::Unset;
    uint32_t flags = 7;                 // 93: the bits AutoCAD sets (7 unset, 6 a value, 4 a value with a format)
    std::variant<std::monostate, int32_t, double, std::string, TableDate, Point2, Point3, TableHandle,
                 std::vector<uint8_t>> value;
    int32_t unit_type = 0;              // 94
    std::string format;                 // 300: the format string ("%lu6%ds[\,]")
    std::string text;                   // 302: the formatted text

    bool has_value() const { return !std::holds_alternative<std::monostate>(value); }

    // An empty cell
    static TableValue unset() { return TableValue{}; }
    // A text value (the text is the string and its formatted text)
    static TableValue from_text(const std::string& text);
    // A number with its format (the formatted text as AutoCAD would show it is given by the caller)
    static TableValue from_double(double v, const std::string& format = std::string(),
                                  const std::string& text = std::string());
    static TableValue from_long(int32_t v, const std::string& format = std::string(),
                                const std::string& text = std::string());

    // Equal in every member (a point is equal by its coordinates)
    bool operator==(const TableValue& o) const;
};

// The six border positions of a table style and of a cell format (AcDb::GridLineType order of the
// TABLESTYLE groups 274 + i, 284 + i, 64 + i)
enum class TableGridEdge : int {
    HorizontalTop = 0,
    HorizontalInside = 1,
    HorizontalBottom = 2,
    VerticalLeft = 3,
    VerticalInside = 4,
    VerticalRight = 5
};

// ---- ACAD_TABLE (the entity) ----

// One border of a cell the table overrides: lineweight (275, 276, 278, 279), color (65, 66, 68, 69) and
// visibility (285, 286, 288, 289), each only when the cell overrides it
struct TableEdgeOverride {
    std::optional<int16_t> lineweight;
    std::optional<int16_t> color;
    std::optional<bool> visible;

    bool any() const { return lineweight || color || visible; }
    bool operator==(const TableEdgeOverride& o) const {
        return lineweight == o.lineweight && color == o.color && visible == o.visible;
    }
};

// What a cell overrides of its table style (the groups after the cell's rotation in 2007 and later,
// after its content before). Each is present only when the cell overrides it.
struct TableCellOverrides {
    std::optional<std::string> text_style;      // 7
    std::optional<double> text_height;          // 140
    std::optional<int16_t> alignment;           // 170
    std::optional<Color> content_color;         // 64
    std::optional<Color> fill_color;            // 63 (and 421)
    std::optional<bool> fill_enabled;           // 283
    TableEdgeOverride top;                      // 279, 69, 289
    TableEdgeOverride right;                    // 275, 65, 285
    TableEdgeOverride bottom;                   // 276, 66, 286
    TableEdgeOverride left;                     // 278, 68, 288

    bool operator==(const TableCellOverrides& o) const {
        return text_style == o.text_style && text_height == o.text_height && alignment == o.alignment &&
               content_color.has_value() == o.content_color.has_value() &&
               (!content_color || (content_color->aci == o.content_color->aci && content_color->rgb == o.content_color->rgb)) &&
               fill_color.has_value() == o.fill_color.has_value() &&
               (!fill_color || (fill_color->aci == o.fill_color->aci && fill_color->rgb == o.fill_color->rgb)) &&
               fill_enabled == o.fill_enabled && top == o.top && right == o.right && bottom == o.bottom &&
               left == o.left;
    }
};

// A block attribute's value in a block cell: its ATTDEF in the block (331) and the text (300)
struct TableAttributeValue {
    std::string attdef_handle;
    std::string text;
    bool operator==(const TableAttributeValue& o) const { return attdef_handle == o.attdef_handle && text == o.text; }
};

enum class TableCellType : int16_t { Text = 1, Block = 2 };

// One cell of the ACAD_TABLE entity (the groups 171 … 178, 145, the content, the overrides)
struct TableCell {
    TableCellType type = TableCellType::Text;   // 171
    int16_t flags = 0;                  // 172
    int16_t merged = 0;                 // 173: 1 for a cell that is part of another's merged range
    bool auto_fit = false;              // 174
    int16_t merge_columns = 1;          // 175: the columns of the merged range (on its first cell)
    int16_t merge_rows = 1;             // 176: the rows of the merged range
    int32_t override_flags = 0;         // 177 (before 2007) or 91: AutoCAD's override bits; the bits of the
                                        // groups below that HDxf knows are kept in step when written
    int16_t virtual_edge = 0;           // 178
    double rotation = 0.0;              // 145
    // The text cell's content: an AcValue from 2007; before, the text only (its formatted text)
    TableValue value;
    std::string field_handle;           // 344: the FIELD the text is (a text cell with a field)
    // The block cell's content
    std::string block_handle;           // 340: the BLOCK_RECORD
    double block_scale = 1.0;           // 144
    std::vector<TableAttributeValue> attributes;    // 179, 331, 300
    int32_t extended_flags = 0;         // 92 (2007 and later)
    TableCellOverrides overrides;
};

// A row type's overrides at the table (one entry per cell type, in the order data, title, header)
struct TableRowTypeOverride {
    std::optional<double> text_height;          // 140
    bool operator==(const TableRowTypeOverride& o) const { return text_height == o.text_height; }
};

// What the table overrides of its style: AutoCAD's override flag (93) and the groups it says follow
struct TableOverrides {
    uint32_t flags = 0;                 // 93: the bits of the groups below are kept in step when written
    uint32_t border_color_flags = 0;    // 94
    uint32_t border_lineweight_flags = 0;   // 95
    uint32_t border_visibility_flags = 0;   // 96
    std::optional<int16_t> flow_direction;      // 70
    std::optional<double> horizontal_margin;    // 40
    std::optional<double> vertical_margin;      // 41
    std::optional<bool> title_suppressed;       // 280
    std::optional<bool> header_suppressed;      // 281
    TableRowTypeOverride rows[3];               // data, title, header: 140

    bool operator==(const TableOverrides& o) const {
        return flags == o.flags && border_color_flags == o.border_color_flags &&
               border_lineweight_flags == o.border_lineweight_flags &&
               border_visibility_flags == o.border_visibility_flags && flow_direction == o.flow_direction &&
               horizontal_margin == o.horizontal_margin && vertical_margin == o.vertical_margin &&
               title_suppressed == o.title_suppressed && header_suppressed == o.header_suppressed &&
               rows[0] == o.rows[0] && rows[1] == o.rows[1] && rows[2] == o.rows[2];
    }
};

// ACAD_TABLE (AcDbBlockReference + AcDbTable): the table as the entity holds it. The anonymous block *T
// (block_name, block_record_handle) is its drawing; the TABLECONTENT and TABLEGEOMETRY of its round-trip
// dictionary (extension dictionary ACAD_XREC_ROUNDTRIP, Drawing::table_content()) hold the rest of it. The
// number of rows (91) and columns (92) are row_heights.size() and column_widths.size(); `cells` runs row by
// row and has rows * columns of them.
struct TableData {
    std::string block_name;             // 2: "*T1"
    Point3 insertion;                   // 10
    std::optional<int16_t> data_version;    // 280 (2010 and later: 0)
    std::string style_handle;           // 342: the TABLESTYLE
    std::string block_record_handle;    // 343: the BLOCK_RECORD of the *T block
    Point3 horizontal_direction = {1.0, 0.0, 0.0};  // 11
    uint32_t flags = 0;                 // 90
    TableOverrides overrides;           // 93 … 96, 70, 40, 41, 280, 281, 140
    std::vector<double> row_heights;    // 141
    std::vector<double> column_widths;  // 142
    std::vector<TableCell> cells;
    // The proxy graphics AutoCAD drew the table with (92 before 2010, 160 from 2010, then 310): the *T
    // block's drawing in AcGi's form; opaque (bytes as written)
    std::vector<uint8_t> proxy_graphics;
    // --- 9.5 table layout --- HDxf's own bookkeeping (not in the file): a hash of what the table's layout reads (the
    // table, its style, the blocks and fields it draws), as of reading the file or the last layout, and of where the
    // table was then. A table whose hash is the same is not laid out again; 0 = never laid out (a table made in memory)
    uint64_t layout_stamp = 0;
    uint64_t placement_stamp = 0;

    size_t rows() const { return row_heights.size(); }
    size_t columns() const { return column_widths.size(); }
    TableCell* cell(size_t row, size_t column) {
        return row < rows() && column < columns() ? &cells[row * columns() + column] : nullptr;
    }
    const TableCell* cell(size_t row, size_t column) const {
        return row < rows() && column < columns() ? &cells[row * columns() + column] : nullptr;
    }
};

// ---- TABLESTYLE ----

struct TableStyleBorder {
    int16_t lineweight = -2;            // 274 + i (-2: ByBlock)
    bool visible = true;                // 284 + i
    int16_t color = 0;                  // 64 + i (0: ByBlock)
    bool operator==(const TableStyleBorder& o) const {
        return lineweight == o.lineweight && visible == o.visible && color == o.color;
    }
};

// One row type of a table style: the data rows, the title row, the header row (in the file's order)
struct TableStyleRow {
    std::optional<std::string> text_style;      // 7 (the text style's name)
    double text_height = 0.0;           // 140
    int16_t alignment = 0;              // 170
    int16_t text_color = 0;             // 62
    int16_t fill_color = 7;             // 63
    bool fill_enabled = false;          // 283
    std::optional<int32_t> data_type;   // 90 (2007 and later)
    std::optional<int32_t> unit_type;   // 91
    std::optional<std::string> format;  // 1
    TableStyleBorder borders[6];        // by TableGridEdge: 274 + i, 284 + i, 64 + i

    bool operator==(const TableStyleRow& o) const {
        if (text_style != o.text_style || text_height != o.text_height || alignment != o.alignment ||
            text_color != o.text_color || fill_color != o.fill_color || fill_enabled != o.fill_enabled ||
            data_type != o.data_type || unit_type != o.unit_type || format != o.format) {
            return false;
        }
        for (int i = 0; i < 6; ++i) if (!(borders[i] == o.borders[i])) return false;
        return true;
    }
};

// TABLESTYLE (AcDbTableStyle); its name is its entry in ACAD_TABLESTYLE. The CELLSTYLEMAP of its extension
// dictionary (ACAD_ROUNDTRIP_2008_TABLESTYLE_CELLSTYLEMAP) holds the cell styles.
struct TableStyleData {
    std::optional<int16_t> class_version;       // 280 first (2010 and later: 0)
    std::optional<std::string> description;     // 3
    int16_t flow_direction = 0;         // 70: 0 down, 1 up
    int16_t flags = 0;                  // 71
    double horizontal_margin = 1.5;     // 40
    double vertical_margin = 1.5;       // 41
    bool title_suppressed = false;      // 280
    bool header_suppressed = false;     // 281
    TableStyleRow rows[3];              // data, title, header
};

// ---- Format parts shared by CELLSTYLEMAP and TABLECONTENT ----

// CONTENTFORMAT: how a cell's content looks
struct TableContentFormat {
    int32_t property_flags = 0;         // 90: the properties overridden (AcDb::CellProperty bits)
    int32_t extended_flags = 0;         // 91
    int32_t data_type = 512;            // 92 (the AcValue type; 512 general)
    int32_t unit_type = 0;              // 93
    std::string format_string;          // 300
    double rotation = 0.0;              // 40
    double scale = 1.0;                 // 140
    int32_t alignment = 1;              // 94
    Color color = Color::from_aci(0);   // 62 (and 420)
    std::string text_style_handle = "0";    // 340
    double text_height = 0.18;          // 144
};

// CELLMARGIN: six values of AutoCAD's own files; in these top, left, bottom, right are the table style's
// vertical and horizontal margins (40, 41); what the last two are is not established
struct TableCellMargins {
    double top = 0.0;
    double left = 0.0;
    double bottom = 0.0;
    double right = 0.0;
    double spacing_1 = 0.18;
    double spacing_2 = 0.18;
};

// GRIDFORMAT: one border
struct TableGridFormat {
    int32_t edge = 0;                   // 95: AcDb::GridProperty edge (1 top, 2 horizontal inside, 4 bottom, 8 left,
                                        //     16 vertical inside, 32 right)
    int32_t property_flags = 0;         // 90: the properties overridden (63: all)
    int32_t kind = 0;                   // 91
    Color color = Color::from_aci(0);   // 62
    int32_t lineweight = -2;            // 92
    std::string linetype_handle = "0";  // 340
    int32_t visibility = 0;             // 93
    double double_line_spacing = 0.0;   // 40
};

// TABLEFORMAT: the format of a cell, row, column, table or cell style
struct TableFormatData {
    int32_t kind = 0;                   // 90: 1 cell, 2 row, 3 column, 4 table, 5 cell style
    bool has_format = false;            // 170: the format below is present
    int32_t property_flags = 0;         // 91 (AcDb::CellProperty bits)
    int32_t merge_flags = 0;            // 92
    Color background = Color::from_aci(257);    // 62 (and 420, 430)
    int32_t flags = 1;                  // 93
    TableContentFormat content;         // 300 CONTENTFORMAT
    bool has_margins = false;           // 171
    TableCellMargins margins;           // 301 MARGIN
    std::vector<TableGridFormat> grids; // 94 counts them
};

// ---- CELLSTYLEMAP ----

struct TableCellStyle {
    TableFormatData format;             // 300 CELLSTYLE, TABLEFORMAT
    int32_t id = 0;                     // 90
    int32_t kind = 0;                   // 91
    std::string name;                   // 300: "_TITLE", "_HEADER", "_DATA" or a style of the user's
};

struct CellStyleMapData {
    std::vector<TableCellStyle> styles; // 90 counts them
};

// ---- TABLECONTENT ----

// A custom data value of a cell, row or column: its name and AcValue (AutoCAD keeps the cell's checksum as
// ACAD_ROUNDTRIP_2008_CELL_CHECKSUM in its custom data)
struct TableCustomData {
    std::string name;                   // 300
    TableValue value;                   // 301 DATAMAP_VALUE and the value
};

enum class TableContentKind : int32_t { Value = 1, Field = 2, Block = 4 };

struct TableCellContent {
    TableContentKind kind = TableContentKind::Value;   // 90
    TableValue value;                   // 300 VALUE and the AcValue (a Value content)
    std::string handle;                 // 340: the FIELD of a Field content (owned by the TABLECONTENT) or the
                                        //      BLOCK_RECORD of a Block content
    std::optional<TableContentFormat> format;       // 170, 300 CONTENTFORMAT: the content's own format
};

// TABLECELL's attachments: a cell that holds a block of its own (an anonymous BLOCK_RECORD): 91, 40, 41, 330, 92
struct TableCellAttachment {
    int32_t kind = 0;                   // 91
    double width = 0.0;                 // 40
    double height = 0.0;                // 41
    std::string block_handle;           // 330: the BLOCK_RECORD
    int32_t flags = 0;                  // 92
};

struct TableContentCell {
    int32_t state = 0;                  // 90
    std::string name;                   // 300
    int32_t flags = 0;                  // 91
    std::vector<TableCustomData> custom_data;       // 301 CUSTOMDATA, DATAMAP
    int32_t extra = 0;                  // 92
    std::vector<TableCellContent> contents;         // 95 counts them
    TableFormatData format;             // FORMATTEDTABLEDATACELL
    int32_t cell_flags = 0;             // TABLECELL 90
    std::vector<TableCellAttachment> attachments;   // TABLECELL 91 counts them
};

struct TableContentColumn {
    std::string name;                   // 300
    int32_t flags = 0;                  // 91
    std::vector<TableCustomData> custom_data;
    TableFormatData format;             // FORMATTEDTABLEDATACOLUMN
    int32_t column_flags = 0;           // TABLECOLUMN 90
    double width = 0.0;                 // 40
};

struct TableContentRow {
    int32_t flags = 0;                  // 91
    std::vector<TableCustomData> custom_data;
    TableFormatData format;             // FORMATTEDTABLEDATAROW
    int32_t row_type = 0;               // TABLEROW 90 (1 the title row, 2 the header, 3 data in AutoCAD's files)
    double height = 0.0;                // 40
    std::vector<TableContentCell> cells;            // 90 counts them
};

// A merged range of cells: top row, left column, bottom row, right column (91, 92, 93, 94)
struct TableMergeRange {
    int32_t top_row = 0;
    int32_t left_column = 0;
    int32_t bottom_row = 0;
    int32_t right_column = 0;
    bool operator==(const TableMergeRange& o) const {
        return top_row == o.top_row && left_column == o.left_column && bottom_row == o.bottom_row &&
               right_column == o.right_column;
    }
};

// TABLECONTENT (AcDbLinkedData, AcDbLinkedTableData, AcDbFormattedTableData, AcDbTableContent): the table's
// content as AutoCAD 2008 keeps it for round trip, owned by the ACAD_XREC_ROUNDTRIP XRECORD of the table's
// extension dictionary. The number of columns and rows is the lists' length.
struct TableContentData {
    std::string name;                   // 1
    std::string description;            // 300
    std::vector<TableContentColumn> columns;        // 90 counts them
    std::vector<TableContentRow> rows;              // 91 counts them
    std::vector<std::string> field_handles;         // 92 counts them, 360: the FIELDs it owns (the fields of
                                                    // the cells with a Field content)
    TableFormatData table_format;       // AcDbFormattedTableData 300 TABLEFORMAT
    std::vector<TableMergeRange> merges;            // 90 counts them
    std::string style_handle;           // AcDbTableContent 340: the TABLESTYLE
};

// ---- TABLEGEOMETRY ----

// The place and size of one content of a cell (the groups 10 … 46, 95)
struct TableContentGeometry {
    Point3 point;                       // 10
    Point3 anchor;                      // 11
    double width = 0.0;                 // 43
    double height = 0.0;                // 44
    double content_width = 0.0;         // 45
    double content_height = 0.0;        // 46
    int32_t flags = 0;                  // 95
};

struct TableCellGeometry {
    int32_t flags = 0;                  // 93 (7: the cell has been laid out; 4 a merged-away cell)
    double width = 0.0;                 // 40
    double height = 0.0;                // 41
    std::string handle;                 // 330: a soft pointer (0 in every AutoCAD file seen), not the owner
    std::vector<TableContentGeometry> contents;     // 94 counts them
};

// TABLEGEOMETRY: the laid out size of every cell (AcDbTableGeometry); `cells` has rows * columns, row by row,
// and its length is 92
struct TableGeometryData {
    int32_t rows = 0;                   // 90
    int32_t columns = 0;                // 91
    std::vector<TableCellGeometry> cells;
};

} // namespace hdxf
