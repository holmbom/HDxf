#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// Underlays (phase 9, step 9.6; plan 2026-10-27-fas-9-typer.md, "Underlägg och deras definitioner"): the entities
// PDFUNDERLAY, DWFUNDERLAY and DGNUNDERLAY (AcDbUnderlayReference) and their definitions PDFDEFINITION, DWFDEFINITION
// and DGNDEFINITION (AcDbUnderlayDefinition), one data type for each pair (UnderlayData, UnderlayDefinitionData).
//
// What AutoCAD's own files show (doc/facit/acad_<version>_dxf_underlay_pdf*.dxf, made by doc/facit/fas9_underlays.scr with
// AutoCAD 2025's core from ACadSharp's sample_AC1032, which has a PDF underlay; ACadSharp's sample_AC1015 … AC1032):
//   - the entity: AcDbEntity (its proxy graphics 92/310 in 2000 … 2007, 160/310 from 2010: the 8 bytes of an empty
//     stream when AutoCAD cannot read the PDF), AcDbUnderlayReference 340 (the definition), 10 20 30, then 41/42/43 each
//     only when it is not 1 (a mirror in X gives 41 -1 alone), 50 when it is not 0, 210 220 230 together when the
//     normal is not the Z axis, 280 (flags), 281 (contrast), 282 (fade) and, for a clip, 11/21 per point (Autodesk's
//     reference; AutoCAD's core has no -PDFCLIP: ej testat)
//   - the definition: AcDbUnderlayDefinition 1 (the file as AutoCAD stores it, "..\pdf-definition.pdf") and 2 (the page,
//     "1"); reactors: the dictionary that owns it, then every entity that points to it (AutoCAD keeps an erased one)
//   - the definition is entered in ACAD_PDFDEFINITIONS under "<file stem> - <page>" and is not removed when its last
//     underlay is erased or purged
//   - R12 has none of them (AutoCAD drops them when it saves R12); R13 and R14: no file, no claim
// DWFUNDERLAY and DGNUNDERLAY (and their definitions) have the same groups in Autodesk's reference; AutoCAD's core crashes
// on -DWFATTACH and -DGNATTACH, so there is no AutoCAD file for them: their classes (AcDbDwfReference …) and dictionaries
// are the reference's and every cell of their matrices is ej testat (F9.3).

namespace hdxf {

namespace {

// The dictionary and the entry name of a definition kind
const char* dictionary_name(UnderlayKind kind) {
    switch (kind) {
        case UnderlayKind::Pdf: return "ACAD_PDFDEFINITIONS";
        case UnderlayKind::Dwf: return "ACAD_DWFDEFINITIONS";
        case UnderlayKind::Dgn: return "ACAD_DGNDEFINITIONS";
    }
    return "ACAD_PDFDEFINITIONS";
}

EntityType entity_type_of(UnderlayKind kind) {
    switch (kind) {
        case UnderlayKind::Pdf: return EntityType::PdfUnderlay;
        case UnderlayKind::Dwf: return EntityType::DwfUnderlay;
        case UnderlayKind::Dgn: return EntityType::DgnUnderlay;
    }
    return EntityType::PdfUnderlay;
}

ObjectType definition_type_of(UnderlayKind kind) {
    switch (kind) {
        case UnderlayKind::Pdf: return ObjectType::PdfDefinition;
        case UnderlayKind::Dwf: return ObjectType::DwfDefinition;
        case UnderlayKind::Dgn: return ObjectType::DgnDefinition;
    }
    return ObjectType::PdfDefinition;
}

// AutoCAD's name for a definition: the file's name without folder and extension, " - ", the page or sheet
std::string definition_key(const std::string& file_name, const std::string& item_name) {
    std::string stem = file_name;
    size_t slash = stem.find_last_of("/\\");
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    size_t dot = stem.rfind('.');
    if (dot != std::string::npos && dot > 0) stem = stem.substr(0, dot);
    return stem + " - " + item_name;
}

bool same_name_ci(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

Object make_definition_object(ObjectType type, const std::string& handle, const std::string& owner, UnderlayDefinitionData data) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = owner;
    if (owner != "0") o.reactors.push_back(owner);
    o.data = std::move(data);
    return o;
}

// AutoCAD's proxy graphics of an underlay whose source it cannot draw: the 8 bytes of an empty stream (the size, 8, and
// the number of commands, 0)
std::vector<uint8_t> empty_graphics() { return {8, 0, 0, 0, 0, 0, 0, 0}; }

} // namespace

Entity& Drawing::add_underlay(UnderlayKind kind, const std::string& file_name, const std::string& item_name,
                              Point3 insertion_point, const std::string& layer) {
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    const std::string dict_handle = named_dictionary(dictionary_name(kind));
    scan_handles();

    // The definition of this file, kind and item is shared
    std::string def_handle;
    const ObjectType def_type = definition_type_of(kind);
    for (const Object& o : objects_) {
        if (o.type != def_type) continue;
        const UnderlayDefinitionData& d = o.as_underlay_definition();
        if (d.file_name == file_name && d.item_name == item_name) {
            def_handle = o.handle;
            break;
        }
    }
    if (def_handle.empty()) {
        def_handle = next_handle();
        UnderlayDefinitionData def;
        def.file_name = file_name;
        def.item_name = item_name;
        if (Object* dict = find_object(dict_handle); dict && dict->type == ObjectType::Dictionary) {
            DictionaryData& entries = dict->as_dictionary();
            const std::string key = definition_key(file_name, item_name);
            std::string unique = key;
            for (int n = 1; std::any_of(entries.entries.begin(), entries.entries.end(),
                                        [&](const DictionaryEntry& e) { return same_name_ci(e.name, unique); });
                 ++n) {
                unique = key + "_" + std::to_string(n);
            }
            auto at = std::find_if(entries.entries.begin(), entries.entries.end(),
                                   [&](const DictionaryEntry& e) { return e.name > unique; });
            entries.entries.insert(at, {unique, def_handle, false});
        }
        objects_.push_back(make_definition_object(def_type, def_handle, dict_handle, std::move(def)));
    }

    Entity e;
    e.type = entity_type_of(kind);
    e.props.layer = layer;
    e.props.handle = next_handle();
    UnderlayData d;
    d.definition_handle = def_handle;
    d.insertion = insertion_point;
    d.proxy_graphics = empty_graphics();
    e.data = Boxed<UnderlayData>(std::move(d));
    entities_.push_back(std::move(e));
    return entities_.back();
}

const Object* Drawing::underlay_definition(const Entity& underlay) const {
    if (underlay.type != EntityType::PdfUnderlay && underlay.type != EntityType::DwfUnderlay &&
        underlay.type != EntityType::DgnUnderlay) {
        return nullptr;
    }
    const std::string& handle = underlay.as_underlay().definition_handle;
    if (handle.empty() || handle == "0") return nullptr;
    const Object* o = find_object(handle);
    if (!o) return nullptr;
    if (o->type != ObjectType::PdfDefinition && o->type != ObjectType::DwfDefinition && o->type != ObjectType::DgnDefinition) {
        return nullptr;
    }
    return o;
}

Object* Drawing::underlay_definition(const Entity& underlay) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->underlay_definition(underlay));
}

namespace internal {

namespace {

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

std::string describe_record(const RecordView& view) {
    return view.type_name() + " " + view.head().handle;
}

// What follows the groups a codec knows (a later version of the record): kept as the file has it (the preserved
// record), with a warning (B105)
void note_rest(GroupCursor& c, TypedReadContext& ctx, const RecordView& view) {
    if (!c.ok() || c.at_end()) return;
    size_t n = c.take_rest().size();
    ctx.warn(describe_record(view) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
             ") are kept as they were read");
}

bool read_underlay(const RecordView& view, TypedReadContext& ctx, Entity& e) {
    GroupCursor c(view, "AcDbUnderlayReference");
    if (!c.valid()) return ctx.fail(view.type_name() + " " + view.head().handle + " has no AcDbUnderlayReference subclass");
    UnderlayData d;
    c.require_handle(340, d.definition_handle);
    c.require_point3(10, d.insertion);
    c.real(41, d.scale.x);
    c.real(42, d.scale.y);
    c.real(43, d.scale.z);
    c.real(50, d.rotation);
    if (c.code() == 210) c.require_point3(210, d.normal);
    int16_t flags = static_cast<int16_t>(d.flags);
    int16_t contrast = static_cast<int16_t>(d.contrast);
    int16_t fade = static_cast<int16_t>(d.fade);
    c.require_integer(280, flags);
    c.integer(281, contrast);
    c.integer(282, fade);
    while (c.ok() && c.code() == 11) {
        Point2 p;
        if (!c.point2(11, p)) break;
        d.clip.push_back(p);
    }
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    d.flags = flags;
    d.contrast = contrast;
    d.fade = fade;
    note_rest(c, ctx, view);
    // The entity's own proxy graphics (AcDbEntity 92/160 + 310)
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &d.proxy_graphics, error)) return ctx.fail(error);
    e.data = Boxed<UnderlayData>(std::move(d));
    return true;
}

bool read_definition(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbUnderlayDefinition");
    if (!c.valid()) return ctx.fail(view.type_name() + " " + view.head().handle + " has no AcDbUnderlayDefinition subclass");
    UnderlayDefinitionData d;
    c.require_string(1, d.file_name);
    c.require_string(2, d.item_name);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

// The entity's proxy graphics: only from 2000 on and when the drawing keeps proxy graphics ($PROXYGRAPHICS)
const std::vector<uint8_t>* graphics_to_write(const RecordWriter& w, const std::vector<uint8_t>& graphics) {
    if (graphics.empty() || w.older() || !w.proxy_graphics_enabled()) return nullptr;
    return &graphics;
}

// A pointer to an object: the handle when the object is there (a drawing built in memory knows what was removed
// from it only by looking: every handle is "live" there), 0 when it is gone (B19). While the drawing as it was read
// is written for comparison the handle stays as it was read
std::string object_pointer(const RecordWriter& w, const std::string& handle) {
    if (handle.empty() || handle == "0") return "0";
    if (!w.filtering()) return handle;
    return w.live(handle) && w.drawing().find_object(handle) ? handle : "0";
}

void write_underlay_as(const char* name, RecordWriter& w, const Entity& e, const std::string& owner) {
    const UnderlayData& d = e.as_underlay();
    if (w.older() && e.raw.empty()) {
        w.fail(std::string(name) + " in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    w.entity_head(name, e, owner, graphics_to_write(w, d.proxy_graphics));
    w.subclass("AcDbUnderlayReference");
    const std::string definition = object_pointer(w, d.definition_handle);
    if (definition == "0" && !d.definition_handle.empty() && d.definition_handle != "0") {
        w.warn(std::string(name) + " " + e.props.handle + ": the definition " + d.definition_handle + " no longer exists, set to none (0)");
    }
    w.handle(340, definition);
    w.point3(10, d.insertion);
    if (d.scale.x != 1.0) w.real(41, d.scale.x);
    if (d.scale.y != 1.0) w.real(42, d.scale.y);
    if (d.scale.z != 1.0) w.real(43, d.scale.z);
    if (d.rotation != 0.0) w.real(50, d.rotation);
    if (d.normal.x != 0.0 || d.normal.y != 0.0 || d.normal.z != 1.0) w.point3(210, d.normal);
    w.integer(280, d.flags);
    w.integer(281, d.contrast);
    w.integer(282, d.fade);
    for (const Point2& p : d.clip) w.point2(11, p);
    w.xdata(e.props.xdata);
}

void write_pdf_underlay(RecordWriter& w, const Entity& e, const std::string& owner) { write_underlay_as("PDFUNDERLAY", w, e, owner); }
void write_dwf_underlay(RecordWriter& w, const Entity& e, const std::string& owner) { write_underlay_as("DWFUNDERLAY", w, e, owner); }
void write_dgn_underlay(RecordWriter& w, const Entity& e, const std::string& owner) { write_underlay_as("DGNUNDERLAY", w, e, owner); }

void write_definition_as(const char* name, RecordWriter& w, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail(std::string(name) + " in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    const UnderlayDefinitionData& d = o.as_underlay_definition();
    w.object_head(name, o);
    w.subclass("AcDbUnderlayDefinition");
    w.text(1, d.file_name);
    w.text(2, d.item_name);
    w.xdata(o.xdata);
}

void write_pdf_definition(RecordWriter& w, const Object& o) { write_definition_as("PDFDEFINITION", w, o); }
void write_dwf_definition(RecordWriter& w, const Object& o) { write_definition_as("DWFDEFINITION", w, o); }
void write_dgn_definition(RecordWriter& w, const Object& o) { write_definition_as("DGNDEFINITION", w, o); }

// ---------------------------------------------------------------------------
// Relations (B19, B104), lists (B12), classes and versions
// ---------------------------------------------------------------------------

// The entity points to its definition (a hard pointer); the definition has the entity as a reactor, as AutoCAD's
// file does (the owner dictionary first, then every underlay)
void underlay_references(const Entity& e, std::vector<TypedReference>& out) {
    const std::string& h = e.as_underlay().definition_handle;
    if (!h.empty() && h != "0") out.push_back({h, RefKind::HardPointer, true});
}

std::vector<RefDescriptor> underlay_refs() {
    RefDescriptor definition;
    definition.subclass = "AcDbUnderlayReference";
    definition.code = 340;
    definition.kind = RefKind::HardPointer;
    definition.removal = RefRemoval::ZeroInPlace;
    definition.back_reactor = true;
    return {definition};
}

// The clip: 11 and 21 of every point are one list (there is no count)
std::vector<ListSpec> underlay_lists() {
    ListSpec clip;
    clip.subclass = "AcDbUnderlayReference";
    clip.id = 1;
    clip.shape = ListShape::Codes;
    clip.codes = {11, 21};
    return {clip};
}

ClassSpec class_of(const char* cpp_name, int flags) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = flags;
    c.in_older = false;
    return c;
}

// 2000 … 2018 (AutoCAD's PDF files in all six versions, both formats; R12's files have neither the entity nor the
// definition: AutoCAD drops them). R13 and R14: a new record is refused (no AutoCAD file; AutoCAD 2025 cannot save them)
VersionMatrix underlay_matrix(bool autocad_verified) {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2000);
    if (autocad_verified) {
        for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                             DxfVersion::R2013, DxfVersion::R2018}) {
            m.autocad(v, AutoCadStatus::Verified);
        }
    }
    return m;
}

void add_underlay_codec(TypedRegistry& registry, const char* name, EntityType type, EntityWriteFn write, const char* cpp_name,
                        bool pdf) {
    EntityCodec c;
    c.dxf_name = name;
    c.type = type;
    c.read = read_underlay;
    c.write = write;
    c.references = underlay_references;
    c.cls = class_of(cpp_name, 4095);
    c.versions = underlay_matrix(pdf);
    c.refs = underlay_refs();
    c.lists = underlay_lists();
    c.step = 6;
    registry.add(std::move(c));
}

void add_definition_codec(TypedRegistry& registry, const char* name, ObjectType type, ObjectWriteFn write, const char* cpp_name,
                          bool pdf) {
    ObjectCodec c;
    c.dxf_name = name;
    c.type = type;
    c.read = read_definition;
    c.write = write;
    c.cls = class_of(cpp_name, 1153);
    c.versions = underlay_matrix(pdf);
    c.step = 6;
    registry.add(std::move(c));
}

} // namespace

void register_underlay_codecs(TypedRegistry& registry) {
    // The order of AutoCAD's own CLASSES: the definition before the entity
    add_definition_codec(registry, "PDFDEFINITION", ObjectType::PdfDefinition, write_pdf_definition, "AcDbPdfDefinition", true);
    add_underlay_codec(registry, "PDFUNDERLAY", EntityType::PdfUnderlay, write_pdf_underlay, "AcDbPdfReference", true);
    add_definition_codec(registry, "DWFDEFINITION", ObjectType::DwfDefinition, write_dwf_definition, "AcDbDwfDefinition", false);
    add_underlay_codec(registry, "DWFUNDERLAY", EntityType::DwfUnderlay, write_dwf_underlay, "AcDbDwfReference", false);
    add_definition_codec(registry, "DGNDEFINITION", ObjectType::DgnDefinition, write_dgn_definition, "AcDbDgnDefinition", false);
    add_underlay_codec(registry, "DGNUNDERLAY", EntityType::DgnUnderlay, write_dgn_underlay, "AcDbDgnReference", false);
    // ODA File Converter writes the entity as PDFREFERENCE (39 files of the old inventory, appendix B.105): read as
    // a PDFUNDERLAY, its own name kept in the preserved record; a new one is written as AutoCAD names it
    registry.add_alias("PDFREFERENCE", "PDFUNDERLAY");
}

} // namespace internal
} // namespace hdxf
