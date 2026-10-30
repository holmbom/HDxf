#pragma once

#include "TypedRecordTypes.hpp"
#include <functional>
#include <string>
#include <vector>

// The internal side of the document data family (phase 9, step 9.8; plan 2026-10-27-fas-9-typer.md): FIELD and
// FIELDLIST (field.cpp), DATATABLE and ACDBDATATABLE (data_table.cpp), OBJECT_PTR and VBA_PROJECT
// (objects_document_data.cpp), their writers (dxf_writer_document_data.cpp) and what the table synchronisation
// (table_sync.cpp) takes from them. The public model is in hdxf.hpp (FieldData, FieldListData, DataTableData,
// ObjectPointerData, VbaProjectData).

namespace hdxf {
namespace internal {

// typed_families.cpp calls this one; it registers every codec of the family
void register_document_data_codecs(TypedRegistry& registry);

// The pieces each file registers
void register_field_codecs(TypedRegistry& registry);               // field.cpp: FIELD, FIELDLIST
void register_data_table_codecs(TypedRegistry& registry);          // data_table.cpp: DATATABLE, ACDBDATATABLE
void register_object_pointer_codecs(TypedRegistry& registry);      // objects_document_data.cpp: OBJECT_PTR, VBA_PROJECT

// The writers (dxf_writer_document_data.cpp): the whole record, head and XDATA included
void write_field(RecordWriter& w, const Object& o);
void write_field_list(RecordWriter& w, const Object& o);
void write_data_table(RecordWriter& w, const Object& o);            // DATATABLE (AcDbDataTable)
void write_legacy_data_table(RecordWriter& w, const Object& o);     // ACDBDATATABLE (ACDBDATATABLE)
void write_object_pointer(RecordWriter& w, const Object& o);
void write_vba_project(RecordWriter& w, const Object& o);

// The length of a UTF-8 text in UTF-16 code units: the number of characters AutoCAD counts (FIELD's 98)
size_t utf16_length(const std::string& utf8);

// ---------------------------------------------------------------------------
// A table's pre-2007 round trip (DXF 2000 and 2004), data_table.cpp
// ---------------------------------------------------------------------------

// The markers of the table's round-trip XRECORD for what AutoCAD wrote before 2007
constexpr const char* kPre2007TableMarker = "ACAD_ROUNDTRIP_PRE2007_TABLE";
constexpr const char* kPre2007CellMarker = "ACAD_ROUNDTRIP_PRE2007_TABLECELL";

// The groups the pre-2007 part of the table's round-trip XRECORD has: `102 PRE2007_TABLE, 90 rows, 91 columns, 102
// PRE2007_TABLECELL, 360 <data table>`
std::vector<DxfGroup> pre2007_table_groups(size_t rows, size_t columns, const std::string& data_table_handle);

// The handle of the data table the pre-2007 part names (empty: the XRECORD has no such part)
std::string pre2007_data_table_handle(const XRecordData& xrecord);
// The same part of the XRECORD as the number of rows and columns it holds (false: none)
bool pre2007_table_size(const XRecordData& xrecord, size_t& rows, size_t& columns);
// Sets the rows and columns the part says
void set_pre2007_table_size(XRecordData& xrecord, size_t rows, size_t columns);

// The DATATABLE (or ACDBDATATABLE: DXF 2000) of a table's cells as AutoCAD writes it before 2007: four columns, the
// checksum of each cell's text (double), two integers (0 here) and the handle of an XRECORD of each cell's value, row
// by row. Both the data table and the cell XRECORDs are appended to `out`; `owner` is the XRECORD that owns the data
// table and `handle` gives the new handles. The data table's handle is returned.
std::string make_pre2007_table_records(const TableData& table, DxfVersion version, const std::string& owner,
                                       const std::function<std::string()>& handle, std::vector<Object>& out);

// What bringing a data table up to date with the table's cells changed: the objects to add, the handles of the cell
// XRECORDs that are no longer needed (the table has fewer cells)
struct Pre2007Update {
    std::vector<Object> added;
    std::vector<std::string> removed;
};

// The cells of `table` into `data_table` and the cell XRECORDs of `drawing`: a cell whose text no longer has the
// checksum the data table keeps for it gets the new checksum and its value record anew; a cell that is not there yet
// gets both; the cells the table no longer has go. A cell that did not change is left as it is.
Pre2007Update update_pre2007_table_records(Drawing& drawing, Object& data_table, const TableData& table,
                                           const std::function<std::string()>& handle);

} // namespace internal
} // namespace hdxf
