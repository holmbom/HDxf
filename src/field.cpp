#include "DocumentDataTypes.hpp"
#include "TableTypes.hpp"
#include "text_encoding.hpp"
#include <cstdlib>

// FIELD and FIELDLIST (phase 9, step 9.8; plan B100-B105, B108): reading the two object types and registering their
// codecs (relations, lists, classes and the version matrix). The groups are AutoCAD's own (the fields of its tables
// in doc/facit/acad_<ver>_dxf_tables*.dxf, the fields of TEXT made by fas9_document_data.scr, FIELDLIST in all of
// them) where they show something and Autodesk's DXF reference (FIELD, "Field code string", "Overflow ...") where they
// do not.
//
//   FIELD:     S(AcDbField) 1 2 (3)* 90 (360)* 97 (331)* [4] 91 92 94 95 96 300 93 (6 <AcValue>)* 7 <AcValue> 301 (9)* 98
//   FIELDLIST: S(AcDbIdSet) 90 (330)* S(AcDbFieldList)
//
// The AcValue is the table's (table_value.cpp): DXF 2007 and later `93 flags, 90 type, <value>, 94, 300, 302,
// 304 ACVALUE_END`, before it `90 type, <value>`. Group 4 (the field code's format) is only in 2000 and 2004.

namespace hdxf {
namespace internal {

namespace {

std::string describe(const char* type, const Object& o) {
    return std::string(type) + " " + o.handle;
}

// What follows the groups a codec knows (a later version of the record): kept as the file has it (the preserved
// record), with a warning (B105)
void note_rest(GroupCursor& c, TypedReadContext& ctx, const char* type, const Object& o) {
    if (!c.ok() || c.at_end()) return;
    size_t n = c.take_rest().size();
    ctx.warn(describe(type, o) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
             ") are kept as they were read");
}

// ---------------------------------------------------------------------------
// FIELD
// ---------------------------------------------------------------------------

bool read_field(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (ctx.version < DxfVersion::R2000) {
        return ctx.fail(describe("FIELD", o) + " in DXF R13/R14: no AutoCAD file shows the grammar there");
    }
    GroupCursor c(view, "AcDbField");
    if (!c.valid()) return ctx.fail(describe("FIELD", o) + " has no AcDbField subclass");
    const bool full = ctx.version >= DxfVersion::R2007;
    FieldData d;
    c.require_string(1, d.evaluator_id);
    c.require_string(2, d.code);
    // The rest of a long code: Autodesk's "Overflow of field code string", after the 2 (no AutoCAD file has a code
    // that long: ej testat)
    while (c.ok() && c.code() == 3) {
        std::string piece;
        c.string(3, piece);
        d.code += piece;
    }
    size_t n = 0;
    if (!c.ok() || !c.count(90, 1, n)) return ctx.fail(describe("FIELD", o) + ": " + (c.ok() ? "the number of child fields (90) is missing" : c.error()));
    for (size_t i = 0; i < n; ++i) {
        std::string h;
        if (!c.handle(360, h)) return ctx.fail(describe("FIELD", o) + ": child field " + std::to_string(i) + " (360) is missing");
        d.child_fields.push_back(std::move(h));
    }
    if (!c.count(97, 1, n)) return ctx.fail(describe("FIELD", o) + ": " + (c.ok() ? "the number of objects (97) is missing" : c.error()));
    for (size_t i = 0; i < n; ++i) {
        std::string h;
        if (!c.handle(331, h)) return ctx.fail(describe("FIELD", o) + ": object " + std::to_string(i) + " (331) is missing");
        d.objects.push_back(std::move(h));
    }
    std::string format4;
    if (c.string(4, format4)) d.format_pre2007 = std::move(format4);
    c.require_integer(91, d.evaluation_option);
    c.require_integer(92, d.filing_option);
    c.require_integer(94, d.state);
    c.require_integer(95, d.evaluation_status);
    c.require_integer(96, d.error_code);
    c.string(300, d.error_message);
    if (!c.ok()) return ctx.fail(describe("FIELD", o) + ": " + c.error());

    // The data set: 93 keys, each with an AcValue. A key-value is at least two groups (the key and the value type)
    if (!c.count(93, 2, n)) return ctx.fail(describe("FIELD", o) + ": " + (c.ok() ? "the number of data keys (93) is missing" : c.error()));
    d.data.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        FieldKeyValue kv;
        if (!c.require_string(6, kv.key)) return ctx.fail(describe("FIELD", o) + ": " + c.error());
        if (!read_ac_value(c, full, kv.value)) return ctx.fail(describe("FIELD", o) + ": the value of key \"" + kv.key + "\": " + c.error());
        d.data.push_back(std::move(kv));
    }
    // The cache
    c.require_string(7, d.cache_key);
    if (!c.ok()) return ctx.fail(describe("FIELD", o) + ": " + c.error());
    if (!read_ac_value(c, full, d.cache)) return ctx.fail(describe("FIELD", o) + ": the cache value: " + c.error());
    c.require_string(301, d.format);
    while (c.ok() && c.code() == 9) {
        std::string piece;
        c.string(9, piece);
        d.format += piece;
    }
    int32_t length = 0;
    c.require_integer(98, length);
    if (!c.ok()) return ctx.fail(describe("FIELD", o) + ": " + c.error());
    // 98 is the count of the cache's characters as AutoCAD counted them; kept when it is not the length of 301
    if (static_cast<size_t>(length) != utf16_length(d.format)) d.format_length = length;
    note_rest(c, ctx, "FIELD", o);
    o.data = Boxed<FieldData>(std::move(d));
    return true;
}

void field_references(const Object& o, std::vector<TypedReference>& out) {
    const FieldData& d = o.as_field();
    for (const std::string& h : d.child_fields) {
        if (!h.empty()) out.push_back({h, RefKind::HardOwner, false});
    }
    for (const std::string& h : d.objects) {
        if (!h.empty()) out.push_back({h, RefKind::SoftPointer, false});
    }
    auto value = [&](const TableValue& v) {
        if (const TableHandle* h = std::get_if<TableHandle>(&v.value)) {
            if (!h->handle.empty()) out.push_back({h->handle, RefKind::SoftPointer, false});
        }
    };
    for (const FieldKeyValue& kv : d.data) value(kv.value);
    value(d.cache);
}

// ---------------------------------------------------------------------------
// FIELDLIST
// ---------------------------------------------------------------------------

bool read_field_list(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (ctx.version < DxfVersion::R2000) {
        return ctx.fail(describe("FIELDLIST", o) + " in DXF R13/R14: no AutoCAD file shows the grammar there");
    }
    GroupCursor c(view, "AcDbIdSet");
    if (!c.valid()) return ctx.fail(describe("FIELDLIST", o) + " has no AcDbIdSet subclass");
    FieldListData d;
    int64_t count = 0;
    if (!c.require_integer(90, count)) return ctx.fail(describe("FIELDLIST", o) + ": " + c.error());
    while (c.ok() && c.code() == 330) {
        std::string h;
        c.handle(330, h);
        d.fields.push_back(std::move(h));
    }
    if (!c.ok()) return ctx.fail(describe("FIELDLIST", o) + ": " + c.error());
    if (static_cast<size_t>(count) != d.fields.size()) {
        ctx.warn(describe("FIELDLIST", o) + ": the count (90) is " + std::to_string(count) + " and the list has " +
                 std::to_string(d.fields.size()) + " fields; it is written as the latter when the list changes");
    }
    note_rest(c, ctx, "FIELDLIST", o);
    GroupCursor list(view, "AcDbFieldList");
    if (!list.valid()) return ctx.fail(describe("FIELDLIST", o) + " has no AcDbFieldList subclass");
    note_rest(list, ctx, "FIELDLIST", o);
    o.data = std::move(d);
    return true;
}

void field_list_references(const Object& o, std::vector<TypedReference>& out) {
    for (const std::string& h : o.as_field_list().fields) {
        if (!h.empty()) out.push_back({h, RefKind::SoftPointer, false});
    }
}

// ---------------------------------------------------------------------------
// Relations (B19, B104), lists (B12), classes and versions
// ---------------------------------------------------------------------------

RefDescriptor pointer(const char* subclass, int code, RefKind kind, RefRemoval removal, int counter = 0) {
    RefDescriptor d;
    d.subclass = subclass;
    d.code = code;
    d.kind = kind;
    d.removal = removal;
    d.counter_code = counter;
    return d;
}

ListSpec list(const char* subclass, int id, ListShape shape, std::vector<int> codes) {
    ListSpec s;
    s.subclass = subclass;
    s.id = id;
    s.shape = shape;
    s.codes = std::move(codes);
    return s;
}

ClassSpec class_of(const char* cpp_name) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 1152;
    c.in_older = false;
    return c;
}

// AutoCAD's own files have both in 2000 … 2018, ASCII and binary (doc/facit/acad_<ver>_dxf_tables*.dxf, fas9_document_data.scr).
// R12 has no OBJECTS section: a named error before anything changes. R13 and R14 (AutoCAD cannot save them now, no
// historical file shows a FIELD): not established
VersionMatrix matrix() {
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.set(v, Generation::Native);
        m.autocad(v, AutoCadStatus::Verified);
    }
    m.autocad(DxfVersion::R13, AutoCadStatus::NotSavable);
    m.autocad(DxfVersion::R14, AutoCadStatus::NotSavable);
    return m;
}

} // namespace

size_t utf16_length(const std::string& s) {
    size_t units = 0;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char b = static_cast<unsigned char>(s[i]);
        size_t len = 1;
        if ((b & 0xF8) == 0xF0) len = 4;
        else if ((b & 0xF0) == 0xE0) len = 3;
        else if ((b & 0xE0) == 0xC0) len = 2;
        if (i + len > s.size()) len = 1;
        for (size_t k = 1; k < len; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) { len = 1; break; }
        }
        units += len == 4 ? 2 : 1;
        i += len;
    }
    return units;
}

namespace {

// A `_text` field's code with `%<\_FldIdx n>%` replaced by the text of its child n (AutoCAD's own texts: group 1 of a
// TEXT is the code of its `_text` field with its fields' texts in it). A child that is not there is empty.
std::string text_of(const Drawing& dwg, const Object& field, int depth) {
    const FieldData& f = field.as_field();
    if (f.evaluator_id != "_text" || depth > 16) return f.display_text();
    static const std::string kOpen = "%<\\_FldIdx ";
    std::string out;
    size_t at = 0;
    while (at < f.code.size()) {
        size_t open = f.code.find(kOpen, at);
        if (open == std::string::npos) {
            out += f.code.substr(at);
            break;
        }
        size_t close = f.code.find(">%", open);
        if (close == std::string::npos) {
            out += f.code.substr(at);
            break;
        }
        out += f.code.substr(at, open - at);
        const std::string number = f.code.substr(open + kOpen.size(), close - open - kOpen.size());
        char* end = nullptr;
        unsigned long index = std::strtoul(number.c_str(), &end, 10);
        if (end && *end == 0 && !number.empty() && index < f.child_fields.size()) {
            const Object* child = dwg.find_object(f.child_fields[index]);
            if (child && child->type == ObjectType::Field) out += text_of(dwg, *child, depth + 1);
        }
        at = close + 2;
    }
    return out;
}

} // namespace

} // namespace internal

std::string Drawing::field_text(const Object& field) const {
    return field.type == ObjectType::Field ? internal::text_of(*this, field, 0) : std::string();
}

namespace internal {

void register_field_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "FIELD";
        c.type = ObjectType::Field;
        c.read = read_field;
        c.write = write_field;
        c.references = field_references;
        c.cls = class_of("AcDbField");
        c.versions = matrix();
        // A child that is gone goes from the list and the count (90) follows; so does an object (97); an object
        // value in the data (AcValue type 64) or the cache is one pointer, set to 0 (the same 330 in AcDbField)
        c.refs.push_back(pointer("AcDbField", 360, RefKind::HardOwner, RefRemoval::DropMember, 90));
        c.refs.push_back(pointer("AcDbField", 331, RefKind::SoftPointer, RefRemoval::DropMember, 97));
        c.refs.push_back(pointer("AcDbField", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace));
        // The tail from the first key (93) is the data set, the cache and the format: one unit, written whole when
        // any of it changed (its codes mean other things before it). Before it the children (90, 360), the objects
        // (97, 331) and the code (2, 3) are lists of their own. The tail comes first: it takes every group from its
        // start, the others only the groups before it
        c.lists = {list("AcDbField", 3, ListShape::FromCode, {93}),
                   list("AcDbField", 1, ListShape::Codes, {90, 360}),
                   list("AcDbField", 2, ListShape::Codes, {97, 331}),
                   list("AcDbField", 4, ListShape::Codes, {2, 3})};
        c.step = 8;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "FIELDLIST";
        c.type = ObjectType::FieldList;
        c.read = read_field_list;
        c.write = write_field_list;
        c.references = field_list_references;
        c.cls = class_of("AcDbFieldList");
        c.versions = matrix();
        // A field that is gone goes from the list and 90 (its count) follows. The fields are not owned by the list
        // (their owner is the dictionary of the text's extension dictionary, or the parent field)
        c.refs.push_back(pointer("AcDbIdSet", 330, RefKind::SoftPointer, RefRemoval::DropMember, 90));
        c.lists = {list("AcDbIdSet", 1, ListShape::Codes, {90, 330})};
        c.step = 8;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
