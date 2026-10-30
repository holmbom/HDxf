#include "ExtensionTypes.hpp"
#include <string>
#include <vector>

// The writer's side of step 9.18 (plan 2026-10-27-fas-9-typer.md): the group writers and helpers the Map, TurboCAD and ZWCAD codecs
// share, and the ZWM components (ZwmObject, ZwmStd, ZwmSymbolStd, ZWMATTRIBUTEDEF, ZwmSymProperties) in AutoCAD's order. The
// grammar is what the files of those programs show (third-party observations: F9.24, F9.25, ej testat).

namespace hdxf {
namespace internal {

void wr16(RecordWriter& w, int code, int64_t value) {
    if (value < -32768 || value > 32767) {
        w.fail("group " + std::to_string(code) + " is " + std::to_string(value) + ", which does not fit the 16 bits of a " +
               "DXF group of that code (70 ... 79, 280 ... 289): it is not truncated");
        return;
    }
    w.integer(code, value);
}

void wrp(RecordWriter& w, int code, const std::string& handle, const std::string& who) {
    if (handle.empty() || handle == "0") {
        w.handle(code, "0");
        return;
    }
    if (!w.live(handle)) {
        if (w.filtering()) {
            w.warn(who + ": the record " + handle + " (group " + std::to_string(code) + ") no longer exists, set to none (0)");
        }
        w.handle(code, "0");
        return;
    }
    w.handle(code, handle);
}

std::vector<RefDescriptor> ext_pointer_refs(bool with_owner_list) {
    std::vector<RefDescriptor> out;
    RefDescriptor pointer;
    pointer.code = 340;
    pointer.kind = RefKind::HardPointer;
    pointer.removal = RefRemoval::ZeroInPlace;
    out.push_back(pointer);
    if (with_owner_list) {
        RefDescriptor owned;
        owned.code = 360;
        owned.kind = RefKind::HardOwner;
        owned.removal = RefRemoval::DropMember;
        owned.counter_code = 70;
        out.push_back(owned);
    }
    return out;
}

void ext_add_ref(std::vector<TypedReference>& out, const std::string& handle, RefKind kind) {
    if (!handle.empty() && handle != "0") out.push_back({handle, kind, false});
}

ClassSpec ext_class(const char* cpp_name, const char* application, int flags, bool in_older) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = application;
    c.proxy_flags = flags;
    c.in_older = in_older;
    c.older_application = application;
    c.older_flags = flags;
    return c;
}

// The other programs' records are plain objects of 2000 on in the files that have them (AC1018 ... AC1032). R12 has no OBJECTS
// section. R13 and R14 are not established (R14 for TurboCAD's: tc_records.cpp). No AutoCAD file has any of them: ej testat
VersionMatrix ext_object_matrix() {
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.set(v, Generation::Native, ErrorBasis::NotApplicable);
    }
    return m;
}

// ---------------------------------------------------------------------------
// ZWM components
// ---------------------------------------------------------------------------

ZwmValueKind zwm_value_kind(const std::string& value_type) {
    if (value_type.empty() || value_type == "VT_BSTR") return ZwmValueKind::Text;
    if (value_type == "VT_BOOL") return ZwmValueKind::Integer;
    if (value_type == "VT_R8") return ZwmValueKind::Real;
    return ZwmValueKind::Unsupported;
}

ZwmValueKind zwm_value_kind(const ZwmAttributeDefinition& d) {
    const ZwmValueKind label = d.text_1[3].empty() ? ZwmValueKind::Text : zwm_value_kind(d.text_1[3]);
    ZwmValueKind variant = label;
    switch (d.reserved_70_1[5]) {
        case 0: break;                                  // none: the label decides
        case 8: variant = ZwmValueKind::Text; break;
        case 11: variant = ZwmValueKind::Integer; break;
        case 5: variant = ZwmValueKind::Real; break;
        default: return ZwmValueKind::Unsupported;
    }
    if (label == ZwmValueKind::Unsupported) return ZwmValueKind::Unsupported;
    // An empty label says nothing; a given one must be the variant's
    if (!d.text_1[3].empty() && d.reserved_70_1[5] != 0 && label != variant) return ZwmValueKind::Unsupported;
    return variant;
}

void zwm_write_tail(RecordWriter& w, const ZwmStdTail& t, const std::string& who) {
    wrp(w, 340, t.handle_340, who);
    wr16(w, 70, t.reserved_70);
    wr32(w, 90, t.reserved_90);
    wrs(w, 1, t.text_1);
}

void zwm_write_object(RecordWriter& w, const ZwmObjectHeader& h, size_t count) {
    w.subclass("ZwmObject");
    wr16(w, 70, h.version);
    wrs(w, 1, h.text_1);
    wr_count(w, 70, count);
}

void zwm_write_std(RecordWriter& w, const ZwmStd& s, const std::string& who) {
    w.subclass("ZwmStd");
    wr16(w, 70, s.version);
    wr16(w, 70, s.reserved_70_0);
    wr16(w, 70, s.reserved_70_1);
    wrs(w, 1, s.text_1);
    zwm_write_tail(w, s.tail, who);
}

void zwm_write_symbol_std(RecordWriter& w, const ZwmSymbolStd& s, const std::string& who) {
    w.subclass("ZwmSymbolStd");
    wr16(w, 70, s.version);
    zwm_write_tail(w, s.tail, who);
}

void zwm_write_type_std(RecordWriter& w, const char* subclass, const ZwmTypeStd& s) {
    w.subclass(subclass);
    wr16(w, 70, s.version);
    wr16(w, 70, s.reserved_70_0);
    wrs(w, 1, s.text_1_0);
}

void zwm_write_definition(RecordWriter& w, const ZwmAttributeDefinition& d) {
    w.subclass("ZWMATTRIBUTEDEF");
    wr16(w, 70, d.version);
    wr16(w, 70, d.reserved_70_0);
    wrs(w, 1, d.text_1);
    wr16(w, 70, d.reserved_70_1);
    switch (zwm_value_kind(d)) {
        case ZwmValueKind::Text:
            wrs(w, 1, d.value.text);
            break;
        case ZwmValueKind::Integer:
            wr16(w, 70, d.value.integer);
            break;
        case ZwmValueKind::Real:
            wrd(w, 40, d.value.real);
            break;
        case ZwmValueKind::Unsupported:
            w.fail("ZWMATTRIBUTEDEF \"" + d.text_1[0] + "\" has the value type \"" + d.text_1[3] + "\" (variant " +
                   std::to_string(d.reserved_70_1[5]) + "), whose value is not established: no file of the application has one");
            break;
    }
}

void zwm_write_attributes(RecordWriter& w, const std::vector<ZwmAttributeEntry>& entries, bool keyed) {
    for (const ZwmAttributeEntry& e : entries) {
        if (keyed) wrs(w, 1, e.key);
        zwm_write_definition(w, e.definition);
    }
}

namespace {

// ZWMSTANDARD's third 70 of ZwmSymProperties is 483351136 in the file the type was observed in: more than the 16 bits of a
// binary group (and of a group of that code). It is kept as it was when the record is written from the file as ASCII (B12);
// a value that is new, changed or written as binary is a named error, not truncated
bool wide_70_allowed(RecordWriter& w, const Object& o, int32_t value) {
    if (value >= -32768 && value <= 32767) return true;
    if (w.format() != DxfFormat::Ascii || o.raw.empty()) return false;
    std::string subclass;
    int ordinal = 0;
    for (const DxfGroup& g : o.raw.groups()) {
        if (g.code == 100) {
            subclass = g.value;
            ordinal = 0;
            continue;
        }
        if (subclass == "ZwmSymProperties" && g.code == 70) {
            if (ordinal++ == 2) {
                try {
                    return std::stoll(g.value) == value;
                } catch (...) {
                    return false;
                }
            }
        }
    }
    return false;
}

} // namespace

void zwm_write_symbol_properties(RecordWriter& w, const Object& o, const ZwmSymProperties& s, const std::string& who) {
    w.subclass("ZwmSymProperties");
    wr16(w, 70, s.version);
    wrs(w, 1, s.text_1);
    wr16(w, 70, s.reserved_70_0);
    if (wide_70_allowed(w, o, s.reserved_70_1)) {
        w.integer(70, s.reserved_70_1);
    } else {
        w.fail(who + ": group 70 is " + std::to_string(s.reserved_70_1) + ", which does not fit the 16 bits of a DXF group (ZwmSymProperties' " +
               "third 70): a file that has it is kept as ASCII as it was read, a new or changed value and a binary file are refused");
    }
    wrd(w, 40, s.reserved_40_0);
    wrd(w, 40, s.reserved_40_1);
    wr16(w, 70, s.reserved_70_2);
    wrd(w, 40, s.reserved_40_2);
    wr16(w, 70, s.reserved_70_3);
    wr16(w, 70, s.reserved_70_4);
    wr_list(w, 70, 70, s.list_70_a);
    wr_list(w, 70, 70, s.list_70_b);
    wr_list(w, 70, 1, s.list_1_a);
    wr_list(w, 70, 1, s.list_1_b);
    wrp(w, 340, s.handle_340_0, who);
    wrd(w, 40, s.reserved_40_3);
    wr16(w, 70, s.reserved_70_5);
    wrp(w, 340, s.handle_340_1, who);
    wr16(w, 70, s.reserved_70_6);
    wr32(w, 90, s.reserved_90_0);
    wrs(w, 1, s.text_1_1);
    wr_count(w, 70, s.attributes.size());
    zwm_write_attributes(w, s.attributes, true);
}

} // namespace internal
} // namespace hdxf
