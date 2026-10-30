#pragma once

#include "hdxf.hpp"
#include "extents.hpp"
#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The foundation of phase 9 (plan 2026-10-27-fas-9-typer.md, step 9.0): what every family step
// (9.1 … 9.18) connects its record types to. Internal; the public values the families share are in
// hdxf.hpp (EvalVariant, EvalExprData).
//
// A family adds, per record type, ONE codec to the registry (typed_families.cpp lists the families).
// The codec is a table of plain functions and descriptors; nothing is subclassed and no big switch is
// edited:
//
//   read        a decoded record (RecordView) → the typed Entity/Object, with GroupCursor over the
//               subclasses; the head (handle, owner, reactors, extension dictionary, XDATA) is read
//               by the framework; a failed read leaves the record as an untyped one, with a warning
//   write       RecordWriter → the whole record (head helpers, then the groups, then the XDATA)
//   references  the handles the typed data holds, with what each one is (B104), for the relations
//   class       the CLASSES entry (B113)
//   lists       the groups that are one unit when the record is written back from its preserved
//               record (B12: a list written whole, a scalar by subclass occurrence/code/ordinal)
//   refs        how each handle-like group is treated when its target is gone (B19/B104)
//   versions    nine versions × two formats: what can be generated, the named error, the AutoCAD
//               status — three separate axes (B101)
//   extents     entities: what the type adds to $EXTMIN/$EXTMAX (B107)
//
// The dispatch is one `find` in the reader (object_reader.cpp, dxf_reader.cpp), one in the writer
// (dxf_writer_2000.cpp), one each in the class lists, the relation collector, the B12 merge
// (dxf_writer_template.cpp), the version check (drawing.cpp) and the extents (extents.cpp).

namespace hdxf {
namespace internal {

class DxfWriter;
struct CapturedGroup;
struct ProxyTables;
struct ClassEntry;

// ---------------------------------------------------------------------------
// The type register: every 0-record name of the inventory (the old plan's appendices A, B and C)
// ---------------------------------------------------------------------------

enum class TypedKind { Entity, Object, Structural };

// What the name was at the inventory (v1.8.1 and the old plan's appendix A and C)
enum class InventoryStatus {
    AlreadyTyped,   // the reader of 1.8.1 has typed data for it
    Structural,     // BLOCK, ENDBLK, VERTEX, SEQEND: a part of another record's structure
    Pending,        // untyped (UnknownObjectData/UnknownEntityData) and has samples
    ZeroSamples,    // appendix C: requested, registered in CLASSES at most, no record in any sample
    DerivedAlias,   // PDFREFERENCE: a read alias only (appendix B.105)
    ExternalClass   // another program's class found in the corpus (AMLOADER …): stays untyped by design (B105)
};

struct TypeRegistryEntry {
    std::string_view dxf_name;
    TypedKind kind = TypedKind::Object;
    InventoryStatus status = InventoryStatus::Pending;
    std::string_view planned_type;      // the C++ type the old plan names ("TableData"), "" none
    int step = 0;                       // 9.<step> completes it; 0: nothing left to do (typed, or kept untyped by design)
    std::string_view source;            // "A", "A+B.3", "C.1", "C.2": where the old plan lists it
};

// ---------------------------------------------------------------------------
// A decoded record, split as AutoCAD writes it
// ---------------------------------------------------------------------------

// The head of a record, before its first subclass: 5, the 102 groups {ACAD_REACTORS and
// {ACAD_XDICTIONARY, and the owner (330)
struct TypedRecordHead {
    std::string handle;
    std::string owner;                  // empty when the record has no owner group (R13/R14, family N)
    std::vector<std::string> reactors;
    std::string xdictionary;
};

// One "100 name" and the groups after it up to the next 100, an embedded object (101) or the XDATA.
// 102 blocks inside it and embedded objects are not in `indices`. Groups of 1010 … 1071 before the
// first 1001 are in the body: they are class data (BLOCKLINEARPARAMETER's 1010/1071), not XDATA.
struct Subclass {
    std::string name;                   // "" for groups before the first 100
    size_t occurrence = 0;              // 0 for the first of this name, 1 for the second (Map's repeated ones)
    std::vector<size_t> indices;        // into RecordView::groups
};

// A 102 group {name … }: an application's (or the record's unknown) group, kept apart from the subclasses
struct Block102 {
    std::string name;                   // "{ACAD_REACTORS" etc., as written
    size_t open = 0;                    // index of the 102 { group
    size_t close = 0;                   // index of the closing 102 group
    size_t subclass = 0;                // the subclass it is in (index into RecordView::subclasses)
};

// An embedded object (101) with the groups that belong to it
struct EmbeddedPart {
    size_t first = 0;                   // index of the 101 group
    size_t last = 0;                    // one past the last group of it
};

class RecordView {
public:
    // Splits `groups` (a record, values decoded to UTF-8, the first group the 0 with the type). False
    // with `error` for a record that is not one (no type, an unclosed 102 block).
    static bool parse(std::vector<DxfGroup> groups, RecordView& out, std::string& error);

    const std::string& type_name() const { return type_name_; }
    const TypedRecordHead& head() const { return head_; }
    const std::vector<DxfGroup>& groups() const { return groups_; }
    const std::vector<Subclass>& subclasses() const { return subclasses_; }
    const std::vector<Block102>& blocks() const { return blocks_; }
    const std::vector<EmbeddedPart>& embedded() const { return embedded_; }
    const std::vector<XdataList>& xdata() const { return xdata_; }
    // One past the last group before the XDATA (groups().size() without XDATA)
    size_t body_end() const { return body_end_; }

    // The subclass `name`, occurrence `n` (0 = the first), null when the record has none
    const Subclass* find_subclass(std::string_view name, size_t occurrence = 0) const;
    size_t subclass_count(std::string_view name) const;
    // The subclass names in order, as a chain "AcDbEntity/AcDbLine"
    std::string chain() const;

private:
    std::string type_name_;
    TypedRecordHead head_;
    std::vector<DxfGroup> groups_;
    std::vector<Subclass> subclasses_;
    std::vector<Block102> blocks_;
    std::vector<EmbeddedPart> embedded_;
    std::vector<XdataList> xdata_;
    size_t body_end_ = 0;
};

// Reads the groups of one subclass in order (B105: grammar-driven reading). A read function takes the
// groups it expects and leaves the rest; a group that is not there is "absent" (false, no error), a
// group that is there with a value that is not a number, or does not fit the type asked for, is an
// error (ok() false). Counts are checked against the groups that remain before anything is allocated.
class GroupCursor {
public:
    GroupCursor(const RecordView& view, const Subclass* subclass);
    // The subclass `name` (occurrence `n`); an absent subclass is an empty cursor with valid() false
    GroupCursor(const RecordView& view, std::string_view name, size_t occurrence = 0);

    bool valid() const { return subclass_ != nullptr; }
    const std::string& name() const;
    size_t occurrence() const;
    bool at_end() const { return pos_ >= (subclass_ ? subclass_->indices.size() : 0); }
    size_t remaining() const { return subclass_ ? subclass_->indices.size() - pos_ : 0; }
    // The next group's code, -1 at the end
    int code() const;
    const DxfGroup* peek(size_t ahead = 0) const;
    // How many groups with `code` have been taken from this subclass
    size_t ordinal(int code) const;

    // Optional groups: taken when the next group has this code
    bool string(int code, std::string& out);
    bool handle(int code, std::string& out);        // the handle text, spaces removed
    bool boolean(int code, bool& out);
    bool real(int code, double& out);
    bool point2(int code, Point2& out);             // code, code + 10
    bool point3(int code, Point3& out);             // code, code + 10, code + 20
    template <typename Int>
    bool integer(int code, Int& out) {
        int64_t v = 0;
        if (!take_integer(code, v)) return false;
        if (!fits<Int>(v)) { fail_range(code, v, sizeof(Int) * 8); return false; }
        out = static_cast<Int>(v);
        return true;
    }
    // Consecutive groups with `code`, hex chunks (310 …), as bytes
    bool bytes(int code, std::vector<uint8_t>& out);

    // A group that must be there: absent is an error
    bool require_string(int code, std::string& out) { return required(code, string(code, out)); }
    bool require_handle(int code, std::string& out) { return required(code, handle(code, out)); }
    bool require_boolean(int code, bool& out) { return required(code, boolean(code, out)); }
    bool require_real(int code, double& out) { return required(code, real(code, out)); }
    bool require_point3(int code, Point3& out) { return required(code, point3(code, out)); }
    template <typename Int>
    bool require_integer(int code, Int& out) { return required(code, integer(code, out)); }

    // A count (`code`) followed by that many items of at least `min_groups_per_item` groups each: the
    // count is checked against the groups that remain, so a corrupt count cannot cause an allocation
    bool count(int code, size_t min_groups_per_item, size_t& n);
    // The groups that have not been taken, in order, and the cursor at the end; for what a codec keeps
    // as it was (a future delversion's tail) with `warning`
    std::vector<DxfGroup> take_rest();
    void skip(size_t n) { pos_ = std::min(pos_ + n, subclass_ ? subclass_->indices.size() : 0); }

    bool ok() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    void fail(const std::string& message);

private:
    template <typename Int>
    static bool fits(int64_t v) {
        if constexpr (std::is_same_v<Int, bool>) return v == 0 || v == 1;
        else if constexpr (std::is_signed_v<Int>) return v >= std::numeric_limits<Int>::min() && v <= std::numeric_limits<Int>::max();
        else return v >= 0 && static_cast<uint64_t>(v) <= std::numeric_limits<Int>::max();
    }
    bool required(int code, bool present);
    bool take_integer(int code, int64_t& out);
    void fail_range(int code, int64_t value, size_t bits);
    const DxfGroup* group_at(size_t pos) const;
    void taken(int code) { ++taken_[code]; }

    const RecordView& view_;
    const Subclass* subclass_;
    size_t pos_ = 0;
    std::string error_;
    std::unordered_map<int, size_t> taken_;
};

// ---------------------------------------------------------------------------
// Reading and writing context
// ---------------------------------------------------------------------------

struct TypedReadContext {
    DxfVersion version = DxfVersion::R2000;
    bool binary = false;
    std::vector<std::string>* warnings = nullptr;
    std::string error;                  // the first failure, set by fail()
    // The DXF names of the CLASS records of the file, in their order (null: none read yet): a proxy's class id (91) is
    // 500 + the position of its class here (step 9.S, proxy_record.cpp)
    const std::vector<std::string>* class_names = nullptr;

    bool fail(const std::string& message) {
        if (error.empty()) error = message;
        return false;
    }
    void warn(const std::string& message) {
        if (warnings) warnings->push_back(message);
    }
};

// How a handle is written when its target may be gone (B19): left out, 0 in its place, or as it is
enum class PointerWrite { OmitDead, ZeroDead, AsIs };

// The writer's face for a codec: friend of DxfWriter, so a codec uses its group writers and head
// helpers without the writer's private interface. Everything a codec writes goes through it.
class RecordWriter {
public:
    explicit RecordWriter(DxfWriter& writer) : w_(writer) {}

    DxfVersion version() const;
    DxfFormat format() const;
    // R13 and R14 (written through the target profile, B101)
    bool older() const { return version() < DxfVersion::R2000; }
    // True while the drawing is written, false while the drawing as it was read is written for
    // comparison (B12): pointers to records that are gone are filtered only in the first
    bool filtering() const;
    // True while a drawing built in memory is written (no preserved records): every handle is "live" there, so a codec
    // that drops a pointer to a record the program removed has to look the record up itself (step 9.9)
    bool built_in_memory() const;
    // A handle that is written, or — in a drawing read from a file — one that led nowhere already
    // in the file (kept, B19). In a drawing built in memory every handle is live: nothing is
    // known to be removed
    bool live(const std::string& handle) const;
    // The drawing was read from a file (its records are written against the file's, B12): a pointer that leads nowhere
    // and is not one the program removed was already dangling in the file. A drawing built in memory has no such pointer
    // and knows what was removed only by looking (live() is true for every handle there). Step 9.11
    bool read_from_file() const;
    const Drawing& drawing() const;
    std::string new_handle();

    // The head of a record: its type, handle, reactors, extension dictionary and owner as the version
    // has them (R13/R14: reactors first, owner only in a file read that has them), then nothing. An
    // object's `reactors` replaces the ones it holds (MLEADERSTYLE's are computed)
    void object_head(const char* dxf_name, const Object& object,
                     const std::vector<std::string>* reactors = nullptr);
    // An entity's head up to and including AcDbEntity with its common groups; `proxy` (the entity's
    // proxy graphics, 92/160 + 310) after 60 as AutoCAD writes it
    void entity_head(const char* dxf_name, const Entity& entity, const std::string& owner,
                     const std::vector<uint8_t>* proxy = nullptr);
    void xdata(const std::vector<XdataList>& xdata);

    void subclass(const char* name);                    // 100
    void text(int code, const std::string& value);
    void integer(int code, int64_t value);              // the width its code has (int16/int32/int64/bool)
    void boolean(int code, bool value);
    void real(int code, double value);
    void point2(int code, const Point2& p);
    void point3(int code, const Point3& p);
    void bytes(int code, const std::vector<uint8_t>& data);     // hex chunks of 127 bytes
    void handle(int code, const std::string& handle);   // as it is, nothing when empty
    void pointer(int code, const std::string& handle, PointerWrite how = PointerWrite::OmitDead);
    void raw(int code, const std::string& value);       // a value held as text, with its code's type
    void warn(const std::string& message);
    void fail(const std::string& message);

    // What proxy graphics and extents need of the writer
    bool proxy_graphics_enabled() const;
    const ProxyTables& proxy_tables() const;

    // A proxy's class id (91): 500 + the position of the CLASS record `dxf_name` in the CLASSES section as it is
    // written; 0 when the section has no such class (dxf_writer_proxy.cpp, step 9.3)
    int class_id(const std::string& dxf_name) const;

    // AcDbDimension's groups of a dimension of any of the three types, with the block the writer made for it (step 9.17,
    // dxf_writer_context_dimension.cpp)
    void dimension_common(const Entity& e, const DimensionData& d);
    // 9.S (Codex 9): the dimension changed since it was read and its block is written anew — its proxy graphics show it as it was
    bool dimension_regenerated(const Entity& e) const;

    // A surface (NURBSURFACE) as the writer of the surfaces writes it: the head, AcDbModelerGeometry, AcDbSurface and the type's
    // subclass, with the proxy graphics of the version (dxf_writer_surface.cpp; step 9.S)
    void surface(const Entity& e, const std::string& owner);

    // The groups written until end_capture() go to `sink` instead of the file; captures nest (the one before is returned and
    // given back to end_capture()). A proxy writes its class's subclasses through the class's codec so (step 9.S)
    std::vector<CapturedGroup>* begin_capture(std::vector<CapturedGroup>& sink);
    void end_capture(std::vector<CapturedGroup>* before);
    void captured(const CapturedGroup& group);

private:
    DxfWriter& w_;
};

// ---------------------------------------------------------------------------
// Relations (B19, B104)
// ---------------------------------------------------------------------------

// What a handle-like group is. Only the pointer kinds are cleared when their target is gone; a sort
// key (SORTENTSTABLE's 5), an internal id (a graph's node, a subentity) and a handle held as text
// (an xref's) are never touched, whatever the group code
enum class RefKind {
    HardOwner,          // 360: the record owns the target
    SoftOwner,          // 350
    HardPointer,        // 340 …
    SoftPointer,        // 330 …
    ExternalText,       // a handle of another drawing, kept as the text it is
    SortKey,            // an ordering key (SORTENTSTABLE's 5)
    InternalId          // an id inside the record or a graph (not a handle)
};

inline bool is_pointer_kind(RefKind k) {
    return k == RefKind::HardOwner || k == RefKind::SoftOwner || k == RefKind::HardPointer ||
           k == RefKind::SoftPointer;
}

// What happens to the group when its target is gone
enum class RefRemoval {
    ZeroInPlace,        // the group stays, 0 in its place (a scalar pointer: AutoCAD writes a null pointer)
    DropMember,         // a member of a list: its groups go and the count (`counter_code`) follows
    DropPair,           // a member that is a pair/tuple of groups (first_offset … last_offset): all of them go
    Keep                // never touched (the kinds that are not pointers)
};

// One handle-like group of a type, found by subclass, its occurrence, the code and the ordinal of the
// code in the subclass. `subclass` null/empty: any subclass; occurrence and ordinal -1: every one.
struct RefDescriptor {
    const char* subclass = nullptr;
    int occurrence = -1;
    int code = 0;
    int ordinal = -1;
    RefKind kind = RefKind::SoftPointer;
    RefRemoval removal = RefRemoval::ZeroInPlace;
    // DropMember/DropPair: the groups of the member around the pointer — `first_offset` groups before
    // it (0 or negative) and `last_offset` after (0 or positive). SORTENTSTABLE's pair is 331 then 5
    // (0, +1). `partner_code`: the code the group at the far end must have for the member to be
    // dropped (0 = not checked)
    int first_offset = 0;
    int last_offset = 0;
    int partner_code = 0;
    // DropMember/DropPair: the count that holds the number of members, the nearest group with this
    // code before the member in the same subclass (0 = the record has none)
    int counter_code = 0;
    // The target gets this record as a reactor (a leader's annotation, a group's members)
    bool back_reactor = false;
};

struct TypeRelations {
    std::string dxf_name;
    std::vector<RefDescriptor> refs;
};

// A handle in a typed record, as the codec's `references` lists them
struct TypedReference {
    std::string handle;
    RefKind kind = RefKind::SoftPointer;
    bool back_reactor = false;          // the target gets this record as a reactor
};

// ---------------------------------------------------------------------------
// B12 lists
// ---------------------------------------------------------------------------

enum class ListShape {
    Codes,              // every group of the subclass with one of `codes`
    FromCode,           // every group from the first one with codes[0] to the end of the subclass
    WholeSubclass       // every group of the subclass (its codes mean different things where they stand)
};

// A list the writer treats as one unit when the record is written back from its preserved record: if it
// changed, it is written whole where it stood; if not, as it was. A count that belongs to the list is
// one of its codes. `id` tells two lists of the same subclass apart (1, 2, …).
struct ListSpec {
    const char* subclass = nullptr;
    int occurrence = -1;                // -1: every occurrence of the subclass
    int id = 1;
    ListShape shape = ListShape::Codes;
    std::vector<int> codes;
};

// ---------------------------------------------------------------------------
// Versions and generation (B101): three separate axes per version and format
// ---------------------------------------------------------------------------

constexpr size_t kVersionCount = 9;          // R12, R13, R14, 2000, 2004, 2007, 2010, 2013, 2018

// What the writer can make anew (without a preserved record) in the version
enum class Generation : uint8_t {
    Native,             // AutoCAD's own form
    OlderForm,          // AutoCAD's proxy/round-trip form (an older version holding a newer type)
    None                // nothing is written; see ErrorBasis
};

// Why Generation::None is what it is
enum class ErrorBasis : uint8_t {
    NotApplicable,      // the type can be generated
    Documented,         // AutoCAD's files or Autodesk's reference show the type is not in that version: a
                        // named error "<TYPE> requires DXF … or later" before anything is written
    NotEstablished      // the grammar or the form is not established (no file): no version error is claimed;
                        // a record read from a file is still preserved and an honest change error is given
};

// What AutoCAD has verified
enum class AutoCadStatus : uint8_t {
    NotTested,          // ej testat: no AutoCAD file for this version/format
    Verified,           // an AutoCAD-saved file of this version and format shows the form
    NotSavable          // AutoCAD 2025 cannot save the version (R13, R14): no AutoCAD file is possible
};

struct VersionCell {
    Generation generation = Generation::None;
    ErrorBasis basis = ErrorBasis::NotEstablished;
    AutoCadStatus autocad = AutoCadStatus::NotTested;
};

struct VersionMatrix {
    // [version][0 ASCII, 1 binary]
    VersionCell cells[kVersionCount][2];
    // The error of a Documented cell; null: "<NAME> requires DXF <first version that has it> or later"
    const char* error_text[kVersionCount] = {};

    // Nothing established: every cell None/NotEstablished, R13/R14 NotSavable
    static VersionMatrix unestablished();
    // A type that exists from `first` on: Native from there, Documented errors before it
    static VersionMatrix native_from(DxfVersion first);

    // Both formats of the version
    VersionMatrix& set(DxfVersion v, Generation g, ErrorBasis b = ErrorBasis::NotApplicable);
    VersionMatrix& set(DxfVersion v, DxfFormat f, Generation g, ErrorBasis b = ErrorBasis::NotApplicable);
    VersionMatrix& autocad(DxfVersion v, AutoCadStatus s);
    VersionMatrix& autocad(DxfVersion v, DxfFormat f, AutoCadStatus s);
    VersionMatrix& error(DxfVersion v, const char* text);

    const VersionCell& at(DxfVersion v, DxfFormat f) const {
        return cells[static_cast<size_t>(v)][f == DxfFormat::Binary ? 1 : 0];
    }
    // Native or OlderForm
    bool can_generate(DxfVersion v, DxfFormat f) const { return at(v, f).generation != Generation::None; }
    // The first version a type can be generated in (both formats), nullopt when none
    std::optional<DxfVersion> first_version() const;
};

// "<name> requires DXF <label> or later" for the first version of `matrix` that can generate the type
std::string version_error_text(const std::string& dxf_name, const VersionMatrix& matrix, DxfVersion target);

// ---------------------------------------------------------------------------
// Codecs
// ---------------------------------------------------------------------------

// A class AutoCAD registers together with another because an object of it is embedded in the other's record (an array body's
// parameters): its CLASSES entry has no record of its own (count 0). Step 9.15
struct ClassCompanion {
    const char* dxf_name = nullptr;
    const char* cpp_name = nullptr;
};

// A CLASSES entry as AutoCAD writes it (B113). Strings are static.
struct ClassSpec {
    const char* cpp_name = nullptr;     // "AcDbTable"; null: the type has no class
    const char* application = nullptr;
    int proxy_flags = 0;
    // The classes written with this one when the drawing has a record of the type (same application and flags, count 0)
    const ClassCompanion* companions = nullptr;
    size_t companion_count = 0;
    // R13 and R14 (family N): the class as the older register has it; none: the type is not in R13/R14
    // (a class is not written there)
    bool in_older = false;
    const char* older_application = nullptr;
    int older_flags = 0;
};

// What an entity's extents code adds to: points in the entity's own coordinates (its OCS, with the
// extrusion applied, and its thickness) and in the WCS, each mapped by the INSERT it is in
struct TypedExtentsSink {
    std::function<void(const Point3&)> add_ocs;
    std::function<void(const Point3&)> add_wcs;
    const Drawing* drawing = nullptr;
    bool in_block = false;
};

using ObjectReadFn = bool (*)(const RecordView&, TypedReadContext&, Object&);
using ObjectWriteFn = void (*)(RecordWriter&, const Object&);
using ObjectReferencesFn = void (*)(const Object&, std::vector<TypedReference>&);
// Whether a record goes with what it refers to (step 9.17: a DIMASSOC none of whose geometry is left): `dead` says whether
// a handle is of a record that was in the file and is gone now. A link that was broken in the file does not count
using ObjectDroppedFn = bool (*)(const Object&, const std::function<bool(const std::string&)>& dead);
using EntityReadFn = bool (*)(const RecordView&, TypedReadContext&, Entity&);
using EntityWriteFn = void (*)(RecordWriter&, const Entity&, const std::string& owner);
using EntityReferencesFn = void (*)(const Entity&, std::vector<TypedReference>&);
using EntityExtentsFn = void (*)(const Entity&, TypedExtentsSink&);
// What an entity's values cannot be written as in a version whose matrix cell can generate the type (a value only a newer
// version holds): the named error, checked before anything changes as the matrix's are; empty when they can (step 9.S)
using EntityValueErrorFn = std::string (*)(const Entity&, DxfVersion);

struct ObjectCodec {
    std::string_view dxf_name;          // the 0 name; one codec per name
    ObjectType type = ObjectType::Unknown;
    ObjectReadFn read = nullptr;
    ObjectWriteFn write = nullptr;
    ObjectReferencesFn references = nullptr;
    ClassSpec cls;
    VersionMatrix versions = VersionMatrix::unestablished();
    std::vector<RefDescriptor> refs;
    std::vector<ListSpec> lists;
    int step = 0;                       // the family step that registered it
    ObjectDroppedFn dropped_when = nullptr;     // the record is not written when this says so
};

struct EntityCodec {
    std::string_view dxf_name;
    EntityType type = EntityType::Unknown;
    EntityReadFn read = nullptr;
    EntityWriteFn write = nullptr;
    EntityReferencesFn references = nullptr;
    EntityExtentsFn extents = nullptr;
    EntityValueErrorFn value_error = nullptr;
    ClassSpec cls;
    VersionMatrix versions = VersionMatrix::unestablished();
    std::vector<RefDescriptor> refs;
    std::vector<ListSpec> lists;
    int step = 0;
};

// The registry. `instance()` is built once with every family registered (typed_families.cpp) and the
// register of names (typed_registry_data.inc); tests add and remove codecs around a check. Building it is
// thread safe, changing it afterwards is not: a program never does, and a test does it between two drawings.
class TypedRegistry {
public:
    static TypedRegistry& instance();

    // The type register
    const std::vector<TypeRegistryEntry>& entries() const { return entries_; }
    const TypeRegistryEntry* find_entry(std::string_view dxf_name) const;

    const ObjectCodec* find_object(std::string_view dxf_name) const;
    const ObjectCodec* find_object(ObjectType type) const;
    const EntityCodec* find_entity(std::string_view dxf_name) const;
    const EntityCodec* find_entity(EntityType type) const;
    const std::vector<ObjectCodec>& object_codecs() const { return objects_; }
    const std::vector<EntityCodec>& entity_codecs() const { return entities_; }

    // The relations/lists of a type by its 0 name: a codec's, or the ones registered on their own
    // (the types a family has not typed yet, SORTENTSTABLE's pairs)
    const std::vector<RefDescriptor>* refs_for(std::string_view dxf_name) const;
    const std::vector<ListSpec>* lists_for(std::string_view dxf_name) const;

    // Registration. A name is registered once: a second codec for the same name or type replaces
    // the first and is returned by `replaced`
    void add(ObjectCodec codec);
    void add(EntityCodec codec);
    void add_relations(TypeRelations relations);
    void add_lists(const std::string& dxf_name, std::vector<ListSpec> lists);
    // A name a record is also read under that is another type's: the reference's "Coordination Model", ODA's
    // PDFREFERENCE for PDFUNDERLAY. find_object/find_entity (and so refs_for, lists_for, implemented) resolve it;
    // the record keeps its own name in its preserved record, and a new record is written under the type's name
    void add_alias(std::string alias, std::string dxf_name);
    // Tests: take a codec away again
    bool remove_object(std::string_view dxf_name);
    bool remove_entity(std::string_view dxf_name);
    void remove_relations(const std::string& dxf_name);
    void remove_lists(const std::string& dxf_name);

    // Whether a codec is registered for the name (an implemented family type)
    bool implemented(std::string_view dxf_name) const;

private:
    TypedRegistry();
    void reindex();

    struct NameHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
        size_t operator()(const std::string& s) const { return std::hash<std::string_view>{}(s); }
    };
    template <typename V>
    using NameMap = std::unordered_map<std::string, V, NameHash, std::equal_to<>>;

    std::vector<TypeRegistryEntry> entries_;
    NameMap<size_t> entry_by_name_;
    std::vector<ObjectCodec> objects_;
    std::vector<EntityCodec> entities_;
    NameMap<size_t> object_by_name_;
    NameMap<size_t> entity_by_name_;
    std::unordered_map<int, size_t> object_by_type_;
    std::unordered_map<int, size_t> entity_by_type_;
    NameMap<std::vector<RefDescriptor>> relations_;
    NameMap<std::vector<ListSpec>> lists_;
    NameMap<std::string> aliases_;
};

// Called once by TypedRegistry: the families register their codecs (typed_families.cpp)
void register_typed_families(TypedRegistry& registry);

// ---------------------------------------------------------------------------
// Functions the framework gives the codecs (typed_record.cpp)
// ---------------------------------------------------------------------------

// A record as the reader gives it: the decoded groups, the codec reads it. On success `out` is the typed
// record (head, XDATA and raw as the caller sets them); on failure the reason is in `context.error`.
bool typed_read_object(const ObjectCodec& codec, std::vector<DxfGroup> groups, TypedReadContext& context,
                       Object& out);
// The same for an entity: its properties are read from AcDbEntity and the head before `codec.read`
bool typed_read_entity(const EntityCodec& codec, std::vector<DxfGroup> groups, TypedReadContext& context,
                       Entity& out);

// AcDbEntity's common groups into `props` (layer, linetype, color, visibility, lineweight …), the head's
// handle, reactors and extension dictionary and the XDATA; `proxy` the entity's proxy graphics (92/160 +
// 310) when given. The read function of an entity codec need not do it: typed_read_entity() has.
bool read_entity_properties(const RecordView& view, EntityProperties& props, std::vector<uint8_t>* proxy,
                            std::string& error);

// AcDbEvalExpr (90, 98, 99 and, when it is there, 1, 70 and the value) — the value contract the
// dynamic block and associative families share (EvalExprData, EvalVariant in hdxf.hpp)
bool read_eval_expr(GroupCursor& cursor, EvalExprData& out);
void write_eval_expr(RecordWriter& writer, const EvalExprData& value);
// An AcDbEvalVariant on its own (the associative value parameters): the tag in `tag_code` (70 in AcDbEvalExpr)
// then its value in the group the tag names: a tag 40 in `real_code` (140 in AcDbEvalExpr, 40 elsewhere),
// a tag 70 in `integer_code` (70)
bool read_eval_variant(GroupCursor& cursor, int tag_code, int real_code, int integer_code, EvalVariant& out);
void write_eval_variant(RecordWriter& writer, int tag_code, int real_code, int integer_code,
                        const EvalVariant& value);

// The classes the typed codecs add to CLASSES for the objects and entities the drawing has: after the
// built-in ones, in registry order. `dropped`: objects that are not written (B19)
void append_typed_classes(const Drawing& dwg, DxfVersion version, const std::unordered_set<uint64_t>* dropped,
                          std::vector<ClassEntry>& out);

// The text of a typed record's proxy graphics when a codec has them is the codec's business; the
// writer's tables are RecordWriter::proxy_tables()

// ---------------------------------------------------------------------------
// Relations (typed_relations.cpp)
// ---------------------------------------------------------------------------

// The descriptor of the group with `code` (ordinal `ordinal` of that code in the subclass) in the
// subclass `subclass` (occurrence `occurrence`) of a record of the 0 name `dxf_name`; null when the type
// has none for it
const RefDescriptor* find_reference(std::string_view dxf_name, std::string_view subclass, size_t occurrence,
                                    int code, size_t ordinal);
// The same over the descriptors of one type, looked up once by the caller (TypedRegistry::refs_for())
const RefDescriptor* find_reference_in(const std::vector<RefDescriptor>* refs, std::string_view subclass,
                                       size_t occurrence, int code, size_t ordinal);

// The list a group of the subclass belongs to (ListSpec::id), 0 when it is in none. `position` is the
// number of groups into the subclass; `from_started` is the state FromCode needs (the caller keeps one
// per subclass: set once its first code has been seen).
int find_list(std::string_view dxf_name, std::string_view subclass, size_t occurrence, int code,
              bool& from_started);
// The same over the specs of one type, looked up once by the caller (TypedRegistry::lists_for())
int find_list_in(const std::vector<ListSpec>* specs, std::string_view subclass, size_t occurrence, int code,
                 bool& from_started);

// The groups of the references of every typed record of the drawing that make this record a reactor of
// its targets: `reactors` (upper-case target handle → the reactors) gets them, as GROUP's members do
void collect_typed_reactors(const Drawing& dwg, std::unordered_map<std::string, std::vector<std::string>>& reactors);

// ---------------------------------------------------------------------------
// Versions (typed_versions.cpp)
// ---------------------------------------------------------------------------

// The first fault of a drawing written as `version`/`format` that comes from a typed record: a record the
// drawing built (no preserved record) of a type whose matrix cell for the version cannot generate it, with
// a Documented basis. Empty when there is none. A record read from the file has its preserved record and
// is not the program's fault.
std::string typed_version_error(const Drawing& dwg, DxfVersion version, DxfFormat format);

} // namespace internal
} // namespace hdxf
