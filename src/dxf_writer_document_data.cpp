#include "DocumentDataTypes.hpp"
#include "TableTypes.hpp"
#include "text_encoding.hpp"

// Fields and document data (phase 9, step 9.8): writing FIELD, FIELDLIST, DATATABLE, ACDBDATATABLE, OBJECT_PTR and
// VBA_PROJECT as AutoCAD writes them (the groups in the order of its own files). A pointer to a record that is gone
// is left out where it is a member of a list (a field's children and objects, a FIELDLIST's fields) and the count
// follows; a pointer that stands alone (an object value) is a 0 in its place (B19).

namespace hdxf {
namespace internal {

namespace {

// A text in the pieces AutoCAD's "overflow" groups hold: the first `limit` characters in the first group, the rest
// after it. Autodesk's reference names the second group of FIELD's code (3) and format (9) the overflow of the first.
void write_overflowing(RecordWriter& w, int first_code, int overflow_code, const std::string& text) {
    constexpr size_t kLimit = 250;
    if (utf16_length(text) <= kLimit) {
        w.text(first_code, text);
        return;
    }
    std::vector<std::string> pieces = split_encoded(text, utf8_code_page, false, kLimit, true);
    for (size_t i = 0; i < pieces.size(); ++i) w.text(i == 0 ? first_code : overflow_code, pieces[i]);
}

std::string handle_or_zero(const std::string& h) {
    return h.empty() ? std::string("0") : h;
}

// The members of a list of handles that are still there
std::vector<std::string> live_members(const RecordWriter& w, const std::vector<std::string>& list) {
    std::vector<std::string> out;
    for (const std::string& h : list) {
        std::string member = handle_or_zero(h);
        if (w.live(member)) out.push_back(std::move(member));
    }
    return out;
}

// A FIELD's children and objects, which its code refers to by position (%<\_FldIdx n>%, %<\_ObjIdx n>%): the ones still
// there. One that is gone at the end goes from the list; one that is gone before others that are there is an error, as
// dropping it would move the others to the places of the code that are not theirs (Codex 9.S, 4)
std::vector<std::string> positional_members(RecordWriter& w, const Object& o, const std::vector<std::string>& list, const char* what,
                                            const char* index) {
    std::vector<std::string> out;
    size_t gone = list.size();
    for (size_t i = 0; i < list.size(); ++i) {
        std::string member = handle_or_zero(list[i]);
        if (w.live(member)) {
            if (gone < i) {
                w.fail("FIELD " + o.handle + ": its " + what + " " + std::to_string(gone) + " is gone and its code refers to the ones after it by position (" +
                       index + "): remove the field, or the " + what + "s after it too");
                return out;
            }
            out.push_back(std::move(member));
        } else if (gone == list.size()) {
            gone = i;
        }
    }
    return out;
}

} // namespace

void write_field(RecordWriter& w, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail("FIELD in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    const FieldData& d = o.as_field();
    const bool full = w.version() >= DxfVersion::R2007;
    w.object_head("FIELD", o);
    w.subclass("AcDbField");
    w.text(1, d.evaluator_id);
    write_overflowing(w, 2, 3, d.code);
    const std::vector<std::string> children = positional_members(w, o, d.child_fields, "child field", "_FldIdx");
    w.integer(90, static_cast<int64_t>(children.size()));
    for (const std::string& h : children) w.text(360, h);
    const std::vector<std::string> objects = positional_members(w, o, d.objects, "object", "_ObjIdx");
    w.integer(97, static_cast<int64_t>(objects.size()));
    for (const std::string& h : objects) w.text(331, h);
    if (!full) w.text(4, d.format_pre2007.value_or(std::string()));
    w.integer(91, d.evaluation_option);
    w.integer(92, d.filing_option);
    w.integer(94, d.state);
    w.integer(95, d.evaluation_status);
    w.integer(96, d.error_code);
    w.text(300, d.error_message);
    w.integer(93, static_cast<int64_t>(d.data.size()));
    for (const FieldKeyValue& kv : d.data) {
        w.text(6, kv.key);
        write_ac_value(w, full, kv.value);
    }
    w.text(7, d.cache_key);
    write_ac_value(w, full, d.cache);
    write_overflowing(w, 301, 9, d.format);
    w.integer(98, d.format_length ? static_cast<int64_t>(*d.format_length) : static_cast<int64_t>(utf16_length(d.format)));
    w.xdata(o.xdata);
}

void write_field_list(RecordWriter& w, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail("FIELDLIST in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    w.object_head("FIELDLIST", o);
    w.subclass("AcDbIdSet");
    const std::vector<std::string> fields = live_members(w, o.as_field_list().fields);
    w.integer(90, static_cast<int64_t>(fields.size()));
    for (const std::string& h : fields) w.text(330, h);
    w.subclass("AcDbFieldList");
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// DATATABLE and ACDBDATATABLE
// ---------------------------------------------------------------------------

namespace {

// The group code a column of this type has its values in; 0 = a type HDxf has not seen (any value is accepted)
int code_of_type(int32_t type) {
    switch (type) {
        case DataColumnType::Integer: return 93;
        case DataColumnType::Double: return 40;
        case DataColumnType::Text: return 3;
        case DataColumnType::Point2D: return 10;
        case DataColumnType::ObjectId: return 331;
        case DataColumnType::HardOwnerId: return 360;
        case DataColumnType::Boolean: return 71;
        case DataColumnType::Point3D: return 11;
        default: return 0;
    }
}

int code_of_value(const DataTableValue& v) {
    struct Visitor {
        int operator()(bool) const { return 71; }
        int operator()(int32_t) const { return 93; }
        int operator()(double) const { return 40; }
        int operator()(const std::string&) const { return 3; }
        int operator()(const DataTablePoint2D&) const { return 10; }
        int operator()(const DataTablePoint3D&) const { return 11; }
        int operator()(const DataTableObject& o) const { return static_cast<int>(o.kind); }
    };
    return std::visit(Visitor{}, v);
}

void write_data_table_value(RecordWriter& w, const DataTableValue& v) {
    if (const bool* b = std::get_if<bool>(&v)) w.boolean(71, *b);
    else if (const int32_t* i = std::get_if<int32_t>(&v)) w.integer(93, *i);
    else if (const double* d = std::get_if<double>(&v)) w.real(40, *d);
    else if (const std::string* s = std::get_if<std::string>(&v)) w.text(3, *s);
    else if (const DataTablePoint2D* p = std::get_if<DataTablePoint2D>(&v)) w.point3(10, p->point);
    else if (const DataTablePoint3D* p3 = std::get_if<DataTablePoint3D>(&v)) w.point3(11, p3->point);
    else if (const DataTableObject* ob = std::get_if<DataTableObject>(&v)) {
        // an object that is gone is a 0 in its place: a row is not taken out of a column
        w.pointer(static_cast<int>(ob->kind), handle_or_zero(ob->handle), PointerWrite::ZeroDead);
    }
}

void write_data_table_record(RecordWriter& w, const Object& o, const char* name, const char* subclass) {
    if (w.older() && o.raw.empty()) {
        w.fail(std::string(name) + " in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    const DataTableData& d = o.as_data_table();
    // Every column has a value for each row (91): the first column says how many
    const size_t rows = d.columns.empty() ? static_cast<size_t>(std::max(d.rows, 0)) : d.columns[0].values.size();
    for (const DataColumn& column : d.columns) {
        if (column.values.size() != rows) {
            w.fail(std::string(name) + " " + o.handle + ": the column \"" + column.name + "\" has " +
                   std::to_string(column.values.size()) + " values and the first column " + std::to_string(rows) +
                   "; every column has one value for each row");
            return;
        }
        const int expected = code_of_type(column.type);
        for (const DataTableValue& v : column.values) {
            const int code = code_of_value(v);
            // A known column type has its value group; a type HDxf has not seen takes any single value kind
            if (expected != 0 && expected != code) {
                w.fail(std::string(name) + " " + o.handle + ": a value of group " + std::to_string(code) + " in the column \"" +
                       column.name + "\" of type " + std::to_string(column.type) + " (group " + std::to_string(expected) + ")");
                return;
            }
        }
    }
    w.object_head(name, o);
    w.subclass(subclass);
    w.integer(70, d.version);
    w.integer(90, static_cast<int64_t>(d.columns.size()));
    w.integer(91, static_cast<int64_t>(rows));
    w.text(1, d.name);
    for (const DataColumn& column : d.columns) {
        w.integer(92, column.type);
        w.text(2, column.name);
        for (const DataTableValue& v : column.values) write_data_table_value(w, v);
    }
    w.xdata(o.xdata);
}

} // namespace

void write_data_table(RecordWriter& w, const Object& o) {
    write_data_table_record(w, o, "DATATABLE", "AcDbDataTable");
}

void write_legacy_data_table(RecordWriter& w, const Object& o) {
    write_data_table_record(w, o, "ACDBDATATABLE", "ACDBDATATABLE");
}

// ---------------------------------------------------------------------------
// OBJECT_PTR and VBA_PROJECT
// ---------------------------------------------------------------------------

void write_object_pointer(RecordWriter& w, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail("OBJECT_PTR in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    // The base record and the ASE application's XDATA (1001 DC015) and nothing else (Autodesk's reference)
    w.object_head("OBJECT_PTR", o);
    w.xdata(o.xdata);
}

void write_vba_project(RecordWriter& w, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail("VBA_PROJECT in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    const VbaProjectData& d = o.as_vba_project();
    w.object_head("VBA_PROJECT", o);
    w.subclass("AcDbVbaProject");
    w.integer(90, static_cast<int64_t>(d.project.size()));
    if (!d.project.empty()) w.bytes(310, d.project);
    w.xdata(o.xdata);
}

} // namespace internal
} // namespace hdxf
