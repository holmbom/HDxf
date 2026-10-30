#pragma once

#include "TypedRecordTypes.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

// Phase 9, step 9.18 (plan 2026-10-27-fas-9-typer.md): what the codecs of AutoCAD Map (map_objects.cpp), TurboCAD
// (tc_records.cpp) and ZWCAD (zwm_records.cpp) share. The group readers and writers below are the small steps those
// grammars are made of: a run of groups of one code, a counted list, a handle that may lead nowhere. The implementation
// of the writers and of the ZWM components is dxf_writer_extension_types.cpp.

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

std::string ext_describe(const RecordView& view);

// The end of a read: a failed cursor makes the record untyped (ctx.fail), groups that remain after the ones HDxf knows are kept in
// the preserved record with a warning (B105)
bool ext_done(GroupCursor& c, TypedReadContext& ctx, const RecordView& view);

// A subclass the record lacks: the failure that names it (the record stays untyped and preserved)
bool ext_missing(const RecordView& view, TypedReadContext& ctx, const char* name, size_t occurrence = 0);

inline void rd(GroupCursor& c, int code, int16_t& v) { if (c.ok()) c.require_integer(code, v); }
inline void rd(GroupCursor& c, int code, int32_t& v) { if (c.ok()) c.require_integer(code, v); }
inline void rd(GroupCursor& c, int code, double& v) { if (c.ok()) c.require_real(code, v); }
inline void rd(GroupCursor& c, int code, bool& v) { if (c.ok()) c.require_boolean(code, v); }
inline void rds(GroupCursor& c, int code, std::string& v) { if (c.ok()) c.require_string(code, v); }
inline void rdh(GroupCursor& c, int code, std::string& v) { if (c.ok()) c.require_handle(code, v); }

template <typename T, size_t N>
void rd(GroupCursor& c, int code, std::array<T, N>& a) {
    for (T& v : a) rd(c, code, v);
}
template <size_t N>
void rds(GroupCursor& c, int code, std::array<std::string, N>& a) {
    for (std::string& v : a) rds(c, code, v);
}
template <size_t N>
void rdh(GroupCursor& c, int code, std::array<std::string, N>& a) {
    for (std::string& v : a) rdh(c, code, v);
}

// A count in `count_code` followed by that many items of `per` groups at least; `item` reads one. The count is checked against the
// groups that remain before anything is allocated (B105)
template <typename F>
void rd_counted(GroupCursor& c, int count_code, size_t per, F&& item) {
    if (!c.ok()) return;
    size_t n = 0;
    if (!c.count(count_code, per, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " (a count) is missing");
        return;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) item();
}

inline void rd_list(GroupCursor& c, int count_code, int code, std::vector<int16_t>& out) {
    out.clear();
    rd_counted(c, count_code, 1, [&] { int16_t v = 0; rd(c, code, v); if (c.ok()) out.push_back(v); });
}
inline void rd_list(GroupCursor& c, int count_code, int code, std::vector<double>& out) {
    out.clear();
    rd_counted(c, count_code, 1, [&] { double v = 0; rd(c, code, v); if (c.ok()) out.push_back(v); });
}
// code 1 reads a string, any other string code is a handle
inline void rd_list(GroupCursor& c, int count_code, int code, std::vector<std::string>& out) {
    out.clear();
    rd_counted(c, count_code, 1, [&] {
        std::string v;
        if (code == 1) rds(c, code, v); else rdh(c, code, v);
        if (c.ok()) out.push_back(v);
    });
}
inline void rd_list(GroupCursor& c, int count_code, std::vector<ZwmIntPair>& out) {
    out.clear();
    rd_counted(c, count_code, 2, [&] {
        ZwmIntPair p;
        rd(c, 70, p.reserved_70_0);
        rd(c, 70, p.reserved_70_1);
        if (c.ok()) out.push_back(p);
    });
}
inline void rd_list(GroupCursor& c, int count_code, std::vector<ZwmTextPair>& out) {
    out.clear();
    rd_counted(c, count_code, 2, [&] {
        ZwmTextPair p;
        rds(c, 1, p.text_1_0);
        rds(c, 1, p.text_1_1);
        if (c.ok()) out.push_back(p);
    });
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

// A group of 16 bits (70 ... 79, 280 ... 289): a value outside them is a named error, never truncated
void wr16(RecordWriter& w, int code, int64_t value);
inline void wr32(RecordWriter& w, int code, int64_t value) { w.integer(code, value); }
inline void wrd(RecordWriter& w, int code, double value) { w.real(code, value); }
inline void wrs(RecordWriter& w, int code, const std::string& value) { w.text(code, value); }
inline void wrb(RecordWriter& w, int code, bool value) { w.boolean(code, value); }
// A pointer: the handle, 0 for none, 0 with a warning for a record that is gone (B19)
void wrp(RecordWriter& w, int code, const std::string& handle, const std::string& who);

template <size_t N>
void wr16(RecordWriter& w, int code, const std::array<int16_t, N>& a) {
    for (int16_t v : a) wr16(w, code, v);
}
template <size_t N>
void wr32(RecordWriter& w, int code, const std::array<int32_t, N>& a) {
    for (int32_t v : a) wr32(w, code, v);
}
template <size_t N>
void wrd(RecordWriter& w, int code, const std::array<double, N>& a) {
    for (double v : a) wrd(w, code, v);
}
template <size_t N>
void wrs(RecordWriter& w, int code, const std::array<std::string, N>& a) {
    for (const std::string& v : a) wrs(w, code, v);
}
template <size_t N>
void wrp(RecordWriter& w, int code, const std::array<std::string, N>& a, const std::string& who) {
    for (const std::string& v : a) wrp(w, code, v, who);
}
inline void wr_count(RecordWriter& w, int code, size_t n) {
    if (code >= 70 && code < 80) wr16(w, code, static_cast<int64_t>(n)); else w.integer(code, static_cast<int64_t>(n));
}
inline void wr_list(RecordWriter& w, int count_code, int code, const std::vector<int16_t>& v) {
    wr_count(w, count_code, v.size());
    for (int16_t x : v) wr16(w, code, x);
}
inline void wr_list(RecordWriter& w, int count_code, int code, const std::vector<double>& v) {
    wr_count(w, count_code, v.size());
    for (double x : v) wrd(w, code, x);
}
inline void wr_list(RecordWriter& w, int count_code, int code, const std::vector<std::string>& v, const std::string& who = {}) {
    wr_count(w, count_code, v.size());
    for (const std::string& x : v) {
        if (code == 1) wrs(w, code, x); else wrp(w, code, x, who);
    }
}
// A list of members that are records (a hard owner list: B19): a member whose record is gone is no member any more, and the
// count that goes before the list follows. A 0 (none) the file had stays
inline bool ext_member_gone(RecordWriter& w, const std::string& handle) {
    return !handle.empty() && handle != "0" && !w.live(handle);
}
inline void wr_members(RecordWriter& w, int count_code, int code, const std::vector<std::string>& v, const std::string& who) {
    size_t live = 0;
    for (const std::string& x : v) if (!ext_member_gone(w, x)) ++live;
    wr_count(w, count_code, live);
    for (const std::string& x : v) {
        if (!ext_member_gone(w, x)) wrp(w, code, x, who);
    }
}
inline void wr_list(RecordWriter& w, int count_code, const std::vector<ZwmIntPair>& v) {
    wr_count(w, count_code, v.size());
    for (const ZwmIntPair& p : v) { wr16(w, 70, p.reserved_70_0); wr16(w, 70, p.reserved_70_1); }
}
inline void wr_list(RecordWriter& w, int count_code, const std::vector<ZwmTextPair>& v) {
    wr_count(w, count_code, v.size());
    for (const ZwmTextPair& p : v) { wrs(w, 1, p.text_1_0); wrs(w, 1, p.text_1_1); }
}

// The relation of every 340 (a hard pointer, 0 for none) of a type, and 360 (a hard owner) where it has them
std::vector<RefDescriptor> ext_pointer_refs(bool with_owner_list);

// A handle for a record's references: not empty and not 0
void ext_add_ref(std::vector<TypedReference>& out, const std::string& handle, RefKind kind);

// What every record of these applications shares: AutoCAD wrote them in its own files only as proxies, so the matrix says what the
// other programs' files show (a plain object from 2000 on; R13/R14 and R12 follow `older`)
ClassSpec ext_class(const char* cpp_name, const char* application, int flags, bool in_older = false);
VersionMatrix ext_object_matrix();

// ---------------------------------------------------------------------------
// ZWM components (zwm_records.cpp reads, dxf_writer_extension_types.cpp writes)
// ---------------------------------------------------------------------------

// The value type of an attribute definition: how its value is written
enum class ZwmValueKind { Text, Integer, Real, Unsupported };
ZwmValueKind zwm_value_kind(const std::string& value_type);
// The definition's own: the OLE variant type in its sixth 70 of the eleven (8 VT_BSTR, 11 VT_BOOL, 5 VT_R8) is what the value
// is; the label in the fourth string ("VT_BOOL", "VT_R8") is empty in some records (ZWMSURFSYMSTD's, ZWMVIEWDIRECTIONSTD's) and is
// the type when the variant is 0 (a record made in memory). A label and a variant that disagree, or a type of neither, are not
// established
ZwmValueKind zwm_value_kind(const ZwmAttributeDefinition& d);

bool zwm_read_tail(GroupCursor& c, ZwmStdTail& t);
bool zwm_read_object(GroupCursor& c, ZwmObjectHeader& h, size_t& count);
bool zwm_read_std(GroupCursor& c, ZwmStd& s);
bool zwm_read_symbol_std(GroupCursor& c, ZwmSymbolStd& s);
bool zwm_read_type_std(GroupCursor& c, ZwmTypeStd& s);
bool zwm_read_definition(GroupCursor& c, ZwmAttributeDefinition& d);
// `count` definitions, from the subclass occurrences of ZWMATTRIBUTEDEF from `first`; `keyed`: the 1 before each is its key, read
// from the subclass before it (`c` for the first). `last` is the cursor after the last definition (null when there is none)
bool zwm_read_attributes(const RecordView& view, TypedReadContext& ctx, GroupCursor& c, size_t count, bool keyed,
                         std::vector<ZwmAttributeEntry>& out, std::optional<GroupCursor>& last);
bool zwm_read_symbol_properties(const RecordView& view, TypedReadContext& ctx, ZwmSymProperties& s);

void zwm_write_tail(RecordWriter& w, const ZwmStdTail& t, const std::string& who);
void zwm_write_object(RecordWriter& w, const ZwmObjectHeader& h, size_t count);
void zwm_write_std(RecordWriter& w, const ZwmStd& s, const std::string& who);
void zwm_write_symbol_std(RecordWriter& w, const ZwmSymbolStd& s, const std::string& who);
void zwm_write_type_std(RecordWriter& w, const char* subclass, const ZwmTypeStd& s);
void zwm_write_definition(RecordWriter& w, const ZwmAttributeDefinition& d);
void zwm_write_attributes(RecordWriter& w, const std::vector<ZwmAttributeEntry>& entries, bool keyed);
void zwm_write_symbol_properties(RecordWriter& w, const Object& o, const ZwmSymProperties& s, const std::string& who);

void register_map_codecs(TypedRegistry& registry);
void register_tc_codecs(TypedRegistry& registry);
void register_zwm_codecs(TypedRegistry& registry);

} // namespace internal
} // namespace hdxf
