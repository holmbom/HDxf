#pragma once

// FIELD and DATATABLE.

#include "core.hpp"
#include "table.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// FIELD, FIELDLIST, DATATABLE, OBJECT_PTR and VBA_PROJECT (phase 9, step 9.8; plan 2026-10-27-fas-9-typer.md)
// ---------------------------------------------------------------------------

// One key of a FIELD's data set (6) and its value: the same AcValue a table cell has (TableValue), so a field
// and a table share one value model.
struct FieldKeyValue {
    std::string key;                    // 6 (ObjectPropertyId, Variable, ACFD_FIELDTEXT_CHECKSUM, ...)
    TableValue value;                   // an AcValue (DXF 2007 and later: 93 ... 304 ACVALUE_END; before: 90 type, value)
};

// FIELD (AcDbField): a field in a text, an attribute, a table cell or a dimension text. The field code (2) is what
// the user typed (`\AcVar Filename \f "%fn6"`), the evaluator (1) says who evaluates it, `cache` is the value
// the evaluator gave last time (7 ACFD_FIELD_VALUE) with its formatted text, and the evaluation state says whether
// that value is current. HDxf does not evaluate a field: the cache, the status and the error are what AutoCAD wrote
// (or what the program sets) and are written as they are.
//
// A field owns its child fields (360, hard owner: a text of two fields is a `_text` field with two children) and
// points at the objects its code names (331). A top-level field is owned by a dictionary of its text's extension
// dictionary (ACAD_FIELD) and listed in the drawing's FIELDLIST. A field removed since the file was read leaves its
// parent's child list, and the counts (90, 97) follow.
struct FieldData {
    std::string evaluator_id;           // 1: "_text", "AcVar", "AcExpr", "AcObjProp", "AcDiesel", ...
    std::string code;                   // 2 (the rest of a long code in 3)
    std::vector<std::string> child_fields;      // 90 (the count) and 360: hard owners
    std::vector<std::string> objects;           // 97 (the count) and 331: soft pointers, the code's `Object(%<\_ObjIdx 0>%)`
    // 4, DXF 2000 and 2004 only: the format of the field code (`%lu2%pr3`), as AutoCAD wrote it before 2007
    std::optional<std::string> format_pre2007;
    int32_t evaluation_option = 0;      // 91 (AcDbField's evaluation option: 63 = automatic)
    int32_t filing_option = 0;          // 92
    int32_t state = 0;                  // 94 (field state flags)
    int32_t evaluation_status = 0;      // 95 (2 = evaluated; 16 = invalid)
    int32_t error_code = 0;             // 96 (the evaluator's error: 600 for an object that is not found)
    std::string error_message;          // 300
    std::vector<FieldKeyValue> data;    // 93 (the count), then 6 and an AcValue for each
    std::string cache_key = "ACFD_FIELD_VALUE";     // 7: hard-coded in AutoCAD
    TableValue cache;                   // the AcValue after 7
    std::string format;                 // 301 (the rest of a long format in 9): the cache's text
    // 98: AutoCAD's count of the cache's characters. It is the length of 301 in most records, but not in all (AutoCAD's own
    // record of an object property has 301 empty and 98 = 7, the length of "100.000"): kept when the file's differs from 301's
    // length, otherwise written as 301's length (UTF-16 units)
    std::optional<int32_t> format_length;

    // The text the field shows as it was last evaluated: the cache's formatted text (302), or 301 where the value has none
    // (before 2007) or is not evaluated. Nothing is evaluated here: a field whose cache is empty shows nothing.
    std::string display_text() const { return !cache.text.empty() ? cache.text : format; }
};

// What a value of a DATATABLE's column refers to when it is an object: the group code AutoCAD writes it in.
enum class DataTableObjectKind : int {
    ObjectValue = 331,      // 331: soft pointer to the object value
    SoftPointer = 330,      // 330: soft pointer
    HardPointer = 340,      // 340: hard pointer
    SoftOwner = 350,        // 350: soft owner
    HardOwner = 360         // 360: hard owner (the round-trip cells of a table: the XRECORDs of the cells)
};

struct DataTableObject {
    DataTableObjectKind kind = DataTableObjectKind::ObjectValue;
    std::string handle;

    bool operator==(const DataTableObject& o) const { return kind == o.kind && handle == o.handle; }
};

// A 2D point (the group 10, 20, 30: AutoCAD writes three coordinates) and a 3D point (11, 21, 31) of a DATATABLE
struct DataTablePoint2D {
    Point3 point;
    bool operator==(const DataTablePoint2D& o) const { return point.x == o.point.x && point.y == o.point.y && point.z == o.point.z; }
};
struct DataTablePoint3D {
    Point3 point;
    bool operator==(const DataTablePoint3D& o) const { return point.x == o.point.x && point.y == o.point.y && point.z == o.point.z; }
};

// One value of a DATATABLE: bool (71), integer (93), double (40), text (3), 2D point (10), 3D point (11) or an object.
using DataTableValue = std::variant<bool, int32_t, double, std::string, DataTablePoint2D, DataTablePoint3D, DataTableObject>;

// The type numbers (92) AutoCAD's files show. A column of another number is read by the groups its values are in and
// kept with its number.
namespace DataColumnType {
constexpr int32_t Integer = 1;          // 93
constexpr int32_t Double = 2;           // 40
constexpr int32_t Text = 3;             // 3
constexpr int32_t Point2D = 4;          // 10, 20, 30
constexpr int32_t ObjectId = 5;         // 331
constexpr int32_t HardOwnerId = 6;      // 360
constexpr int32_t Boolean = 10;         // 71
constexpr int32_t Point3D = 11;         // 11, 21, 31
}

struct DataColumn {
    int32_t type = DataColumnType::Integer;     // 92
    std::string name;                           // 2
    std::vector<DataTableValue> values;         // one for each row
};

// DATATABLE (AcDbDataTable) and ACDBDATATABLE (DXF 2000: the same record under another name and subclass marker): a
// table of typed values by column. AutoCAD keeps every ACAD_TABLE of a DXF 2000 or 2004 file there too (the
// cells as it wrote them before 2007: a column of the cells' checksums, two of integers, and one that owns an XRECORD
// of each cell's value), and AutoCAD Map keeps its style tables in it. `columns` (90) and the number of rows (91)
// are derived from the columns when the record is written.
struct DataTableData {
    int version = 2;                    // 70
    std::string name;                   // 1
    int32_t rows = 0;                   // 91: written as the number of values of the first column when there are columns
    std::vector<DataColumn> columns;    // 90 (the count), then 92, 2 and the values of each
};

} // namespace hdxf
