#pragma once

#include "TypedRecordTypes.hpp"
#include <string>
#include <vector>

// The internal side of the table family (phase 9, step 9.1): the pieces the five codecs share
// (table_value.cpp, table_format.cpp) and the functions the codecs register themselves with. Internal;
// the public model is in hdxf.hpp (TableData, TableStyleData, CellStyleMapData, TableContentData,
// TableGeometryData).

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// Markers: the strings AutoCAD puts in a group to open and close a part ("300 CELL", "1 TABLEFORMAT_BEGIN",
// "309 TABLEFORMAT_END", "304 ACVALUE_END"). A marker is read by its position in the grammar: the same
// text is a user's string where a value is expected (a cell that says "CELL" is a cell with that text).
// ---------------------------------------------------------------------------

// True and consumed when the next group has `code` and exactly the text `text`
bool take_marker(GroupCursor& c, int code, const char* text);
// The same, and a failure of the cursor when it is not there
bool require_marker(GroupCursor& c, int code, const char* text);
// True when the next group is that marker (nothing consumed)
bool at_marker(const GroupCursor& c, int code, const char* text);

// A group read as an unsigned 32-bit bitmask: AutoCAD writes them as signed 32-bit numbers (a flag with the
// high bit set is negative in the file)
bool take_u32(GroupCursor& c, int code, uint32_t& out);
bool require_u32(GroupCursor& c, int code, uint32_t& out);

// A text of 250 characters or more in the pieces AutoCAD writes it in: the pieces in `chunk_code` and the
// last in `last_code`. read_chunked_text() takes `chunk_code`* then `last_code`; absent when the next group
// has neither.
bool read_chunked_text(GroupCursor& c, int chunk_code, int last_code, std::string& out);
void write_chunked_text(RecordWriter& w, int chunk_code, int last_code, const std::string& text);

// ---------------------------------------------------------------------------
// AcValue (table_value.cpp)
// ---------------------------------------------------------------------------

// The body of an AcValue after its marker line: `93 flags, 90 type, <value>, 94 unit, 300 format, 302 text,
// 304 ACVALUE_END` (`full_form`, DXF 2007 and later) or `90 type, <value>` (2000 and 2004)
// `91 follows`: a 91 comes after the value (a cell content's own 91), which tells the long 0 of an empty 2000/2004 value
bool read_ac_value(GroupCursor& c, bool full_form, TableValue& out, bool followed_by_91 = false);
void write_ac_value(RecordWriter& w, bool full_form, const TableValue& value);

// A value as the text of the cell: its formatted text, the string itself when it has none
std::string table_value_text(const TableValue& value);

// A date from / as the 16 bytes of its SYSTEMTIME
bool table_date_from_bytes(const std::vector<uint8_t>& bytes, TableDate& out);
std::vector<uint8_t> table_date_to_bytes(const TableDate& date);

// The custom data of a TABLECONTENT cell that keeps the checksum of its text
constexpr const char* kTableChecksumName = "ACAD_ROUNDTRIP_2008_CELL_CHECKSUM";

// AutoCAD's checksum of a cell's text (ACAD_ROUNDTRIP_2008_CELL_CHECKSUM): the sum of every UTF-16 code unit
// of the text times its position (the first is 1). The text is UTF-8.
double table_text_checksum(const std::string& text);

// ---------------------------------------------------------------------------
// The format parts (table_format.cpp), used by CELLSTYLEMAP and TABLECONTENT
// ---------------------------------------------------------------------------

// `1 CONTENTFORMAT_BEGIN … 309 CONTENTFORMAT_END` (the marker `300 CONTENTFORMAT` before it is the caller's)
bool read_content_format(GroupCursor& c, TableContentFormat& out);
void write_content_format(RecordWriter& w, const TableContentFormat& format);
// `1 TABLEFORMAT_BEGIN … 309 TABLEFORMAT_END` (what is before it is the caller's)
bool read_table_format(GroupCursor& c, TableFormatData& out);
void write_table_format(RecordWriter& w, const TableFormatData& format);
// A color in the form of the format groups: `62 aci` and, when it has one, `420 rgb` (and `430 name`)
bool read_format_color(GroupCursor& c, Color& out);
void write_format_color(RecordWriter& w, const Color& color);

// ---------------------------------------------------------------------------
// What the five codecs have alike (table_format.cpp)
// ---------------------------------------------------------------------------

// The version cells of the table family: R12 a named error, R13/R14 not established, 2000 … 2018 native and
// verified by AutoCAD's own files in both formats
VersionMatrix table_versions();
// A CLASSES entry of the family: "ObjectDBX Classes", not in R13/R14
ClassSpec table_class_spec(const char* cpp_name, int proxy_flags);
// Every group of the subclass as one B12 list
ListSpec table_whole_subclass(const char* subclass, int id = 1);

// ---------------------------------------------------------------------------
// The families' registration (called from typed_families.cpp)
// ---------------------------------------------------------------------------

void register_table_codecs(TypedRegistry& registry);

// The pieces each file registers
void register_table_entity_codec(TypedRegistry& registry);          // dxf_writer_table.cpp: ACAD_TABLE
void register_table_style_codecs(TypedRegistry& registry);          // table_format.cpp: TABLESTYLE, CELLSTYLEMAP
void register_table_content_codec(TypedRegistry& registry);         // table_content.cpp: TABLECONTENT
void register_table_geometry_codec(TypedRegistry& registry);        // table_geometry.cpp: TABLEGEOMETRY

// ---------------------------------------------------------------------------
// Synchronisation (table_sync.cpp)
// ---------------------------------------------------------------------------

// What the layout of step 9.5 gives the table's synchronisation: it fills in the sizes of the cells (the
// TABLEGEOMETRY) and the *T block. None by default; set by the layout code.
// (Step 9.5: table_layout.cpp / table_graphics.cpp is the default; the companions are null for a table that has none;
// `warnings` are the drawing's.)
using TableLayoutHook = void (*)(Drawing& dwg, Entity& table, TableContentData* content, TableGeometryData* geometry,
                                 std::vector<std::string>& warnings);
void set_table_layout_hook(TableLayoutHook hook);
TableLayoutHook table_layout_hook();

// The default content of a TABLECONTENT / TABLEGEOMETRY made from the entity (no layout)
TableContentData table_content_from_entity(const TableData& table, const std::string& style_handle,
                                           bool full_form);
TableGeometryData table_geometry_from_entity(const TableData& table);

} // namespace internal
} // namespace hdxf
