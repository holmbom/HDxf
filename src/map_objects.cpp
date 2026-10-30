#include "ExtensionTypes.hpp"
#include <string>
#include <vector>

// AutoCAD Map 3D's display management (phase 9, step 9.18; plan 2026-10-27-fas-9-typer.md, "AutoCAD Map"; the old plan's B.47 ... B.55,
// B.59, B.60): the eleven records DMMAPMANAGER, DMSTYLELIBRARY, DMSTYLECATEGORY, DMMAP, DMBASEELEMENT, DMDEFAULTSTYLE,
// DMSTYLEREFERENCE, DMSURROGATESTYLESETS, DMSTYLIZEDENTITIESTABLE, MAPMETADATAOBJECT and MAPRESOURCEMANAGEROBJECT, as codecs of the
// registry. Map's own files are what they are read from (an AC1018 drawing of Map 3D with one display map, one base element, and the
// AC1018/AC1032 files that have MAPRESOURCEMANAGEROBJECT): the lists they show are all one element long or empty, so what a longer
// list is followed by is the count and the same group (F9.24: ej testat for any other branch). DMMAP, DMBASEELEMENT and DMDEFAULTSTYLE
// have several subclasses with the same name; they are told apart by their occurrence. AcMapManagementAppName and
// AcMapDMDisplayStyleRegapp are the records' XDATA.

namespace hdxf {
namespace internal {

namespace {

constexpr const char* kDisplay = "AcMapDisplayManagement";

template <typename D>
const D& data_of(const Object& o) { return *o.data.get<Boxed<D>>(); }

bool refuse_older(RecordWriter& w, const char* name, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail(std::string(name) + " in DXF R13 or R14 is not established: no file of AutoCAD Map has it (a record read from a file is kept as it was)");
        return true;
    }
    return false;
}

// A subclass the record must have, at its occurrence
#define MAP_SUBCLASS(var, name, occurrence)                                  \
    GroupCursor var(view, name, occurrence);                                 \
    if (!var.valid()) return ext_missing(view, ctx, name, occurrence)

// ---------------------------------------------------------------------------
// DMMAPMANAGER
// ---------------------------------------------------------------------------

bool read_manager(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapManagerData d;
    {
        MAP_SUBCLASS(c, "AcDbDictionary", 0);
        c.boolean(280, d.dictionary.hard_owner);
        int16_t cloning = 1;
        if (c.integer(281, cloning)) d.dictionary.cloning = cloning;
        while (c.ok() && c.code() == 3) {
            DictionaryEntry e;
            rds(c, 3, e.name);
            if (c.code() == 360) {
                e.hard_owner = true;
                rdh(c, 360, e.handle);
            } else {
                rdh(c, 350, e.handle);
            }
            if (c.ok()) d.dictionary.entries.push_back(std::move(e));
        }
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        MAP_SUBCLASS(c, "AcDbDictionaryWithDefault", 0);
        rdh(c, 340, d.default_handle);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        MAP_SUBCLASS(c, "AcMapDMMapManager", 0);
        rd(c, 70, d.version);
        rdh(c, 340, d.current_map);
        if (!ext_done(c, ctx, view)) return false;
    }
    o.data = Boxed<MapManagerData>(std::move(d));
    return true;
}

void write_manager(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMMAPMANAGER", o)) return;
    const MapManagerData& d = data_of<MapManagerData>(o);
    const std::string who = "DMMAPMANAGER " + o.handle;
    w.object_head("DMMAPMANAGER", o);
    w.subclass("AcDbDictionary");
    if (d.dictionary.hard_owner) wrb(w, 280, true);
    wr16(w, 281, d.dictionary.cloning);
    for (const DictionaryEntry& e : d.dictionary.entries) {
        if (ext_member_gone(w, e.handle)) continue;         // the pair (3, 350/360) of a record that is gone goes (B19)
        wrs(w, 3, e.name);
        wrp(w, e.hard_owner ? 360 : 350, e.handle, who);
    }
    w.subclass("AcDbDictionaryWithDefault");
    wrp(w, 340, d.default_handle, who);
    w.subclass("AcMapDMMapManager");
    wr16(w, 70, d.version);
    wrp(w, 340, d.current_map, who);
    w.xdata(o.xdata);
}

void manager_references(const Object& o, std::vector<TypedReference>& out) {
    const MapManagerData& d = data_of<MapManagerData>(o);
    for (const DictionaryEntry& e : d.dictionary.entries) ext_add_ref(out, e.handle, e.hard_owner ? RefKind::HardOwner : RefKind::SoftOwner);
    ext_add_ref(out, d.default_handle, RefKind::HardPointer);
    ext_add_ref(out, d.current_map, RefKind::HardPointer);
}

// ---------------------------------------------------------------------------
// DMSTYLELIBRARY, DMSTYLECATEGORY, MAPMETADATAOBJECT, MAPRESOURCEMANAGEROBJECT, DMSTYLIZEDENTITIESTABLE
// ---------------------------------------------------------------------------

bool read_library(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapStyleLibraryData d;
    MAP_SUBCLASS(c, "AcMapDMStyleLibrary", 0);
    rd(c, 70, d.version);
    rd_list(c, 70, 360, d.categories);
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<MapStyleLibraryData>(std::move(d));
    return true;
}
void write_library(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMSTYLELIBRARY", o)) return;
    const MapStyleLibraryData& d = data_of<MapStyleLibraryData>(o);
    w.object_head("DMSTYLELIBRARY", o);
    w.subclass("AcMapDMStyleLibrary");
    wr16(w, 70, d.version);
    wr_members(w, 70, 360, d.categories, "DMSTYLELIBRARY " + o.handle);
    w.xdata(o.xdata);
}
void library_references(const Object& o, std::vector<TypedReference>& out) {
    for (const std::string& h : data_of<MapStyleLibraryData>(o).categories) ext_add_ref(out, h, RefKind::HardOwner);
}

bool read_category(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapStyleCategoryData d;
    MAP_SUBCLASS(c, "AcMapDMStyleCategory", 0);
    rd(c, 70, d.version);
    rds(c, 1, d.name);
    rd(c, 70, d.reserved_70_0);
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<MapStyleCategoryData>(std::move(d));
    return true;
}
void write_category(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMSTYLECATEGORY", o)) return;
    const MapStyleCategoryData& d = data_of<MapStyleCategoryData>(o);
    w.object_head("DMSTYLECATEGORY", o);
    w.subclass("AcMapDMStyleCategory");
    wr16(w, 70, d.version);
    wrs(w, 1, d.name);
    wr16(w, 70, d.reserved_70_0);
    w.xdata(o.xdata);
}

bool read_metadata(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapMetadataData d;
    MAP_SUBCLASS(c, "AcMapMetadataPersistObject", 0);
    rd(c, 90, d.version);
    rd(c, 90, d.reserved_90_0);
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<MapMetadataData>(std::move(d));
    return true;
}
void write_metadata(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "MAPMETADATAOBJECT", o)) return;
    const MapMetadataData& d = data_of<MapMetadataData>(o);
    w.object_head("MAPMETADATAOBJECT", o);
    w.subclass("AcMapMetadataPersistObject");
    wr32(w, 90, d.version);
    wr32(w, 90, d.reserved_90_0);
    w.xdata(o.xdata);
}

bool read_resource_manager(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapResourceManagerData d;
    MAP_SUBCLASS(c, "AcMapResourceManager", 0);
    rd(c, 70, d.version);
    rd(c, 90, d.reserved_90_0);
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<MapResourceManagerData>(std::move(d));
    return true;
}
void write_resource_manager(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "MAPRESOURCEMANAGEROBJECT", o)) return;
    const MapResourceManagerData& d = data_of<MapResourceManagerData>(o);
    w.object_head("MAPRESOURCEMANAGEROBJECT", o);
    w.subclass("AcMapResourceManager");
    wr16(w, 70, d.version);
    wr32(w, 90, d.reserved_90_0);
    w.xdata(o.xdata);
}

bool read_stylized_table(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapStylizedEntitiesTableData d;
    MAP_SUBCLASS(c, "AcMapDMStylizedEntitiesTable", 0);
    rd(c, 70, d.version);
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<MapStylizedEntitiesTableData>(std::move(d));
    return true;
}
void write_stylized_table(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMSTYLIZEDENTITIESTABLE", o)) return;
    w.object_head("DMSTYLIZEDENTITIESTABLE", o);
    w.subclass("AcMapDMStylizedEntitiesTable");
    wr16(w, 70, data_of<MapStylizedEntitiesTableData>(o).version);
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// DMMAP: three AcMapDMMap subclasses
// ---------------------------------------------------------------------------

bool read_map(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapDisplayMapData d;
    {
        MAP_SUBCLASS(c, "AcMapDMMap", 0);
        MapDisplayMapHeader& h = d.header;
        rd(c, 70, h.version);
        rds(c, 1, h.name);
        rd(c, 70, h.reserved_70_0);
        rdh(c, 330, h.parent_map);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        MAP_SUBCLASS(c, "AcMapDMMap", 1);
        MapDisplayMapElements& e = d.elements;
        rd(c, 70, e.version);
        rd_list(c, 90, 360, e.elements);
        rd_list(c, 90, 40, e.scales);
        rd(c, 70, e.reserved_70_0);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        MAP_SUBCLASS(c, "AcMapDMMap", 2);
        MapDisplayMapLegend& l = d.legend;
        rd(c, 70, l.version);
        rd(c, 70, l.reserved_70_0);
        rd(c, 40, l.reserved_40_0);
        rd(c, 290, l.reserved_290_0);
        rd(c, 290, l.reserved_290_1);
        rd(c, 40, l.reserved_40_1);
        rd(c, 40, l.reserved_40_2);
        rds(c, 1, l.name);
        rd_list(c, 90, 40, l.scales);
        rds(c, 1, l.text_1_0);
        rd(c, 70, l.reserved_70_1);
        rd(c, 40, l.reserved_40_3);
        rd(c, 70, l.reserved_70_2);
        rd(c, 90, l.reserved_90_0);
        rd_list(c, 90, 360, l.items);
        if (!ext_done(c, ctx, view)) return false;
    }
    if (view.subclass_count("AcMapDMMap") != 3) {
        return ctx.fail(ext_describe(view) + " has " + std::to_string(view.subclass_count("AcMapDMMap")) + " AcMapDMMap subclasses, not three");
    }
    o.data = Boxed<MapDisplayMapData>(std::move(d));
    return true;
}

void write_map(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMMAP", o)) return;
    const MapDisplayMapData& d = data_of<MapDisplayMapData>(o);
    const std::string who = "DMMAP " + o.handle;
    w.object_head("DMMAP", o);
    w.subclass("AcMapDMMap");
    wr16(w, 70, d.header.version);
    wrs(w, 1, d.header.name);
    wr16(w, 70, d.header.reserved_70_0);
    wrp(w, 330, d.header.parent_map, who);
    w.subclass("AcMapDMMap");
    wr16(w, 70, d.elements.version);
    wr_members(w, 90, 360, d.elements.elements, who);
    wr_list(w, 90, 40, d.elements.scales);
    wr16(w, 70, d.elements.reserved_70_0);
    w.subclass("AcMapDMMap");
    const MapDisplayMapLegend& l = d.legend;
    wr16(w, 70, l.version);
    wr16(w, 70, l.reserved_70_0);
    wrd(w, 40, l.reserved_40_0);
    wrb(w, 290, l.reserved_290_0);
    wrb(w, 290, l.reserved_290_1);
    wrd(w, 40, l.reserved_40_1);
    wrd(w, 40, l.reserved_40_2);
    wrs(w, 1, l.name);
    wr_list(w, 90, 40, l.scales);
    wrs(w, 1, l.text_1_0);
    wr16(w, 70, l.reserved_70_1);
    wrd(w, 40, l.reserved_40_3);
    wr16(w, 70, l.reserved_70_2);
    wr32(w, 90, l.reserved_90_0);
    wr_members(w, 90, 360, l.items, who);
    w.xdata(o.xdata);
}

void map_references(const Object& o, std::vector<TypedReference>& out) {
    const MapDisplayMapData& d = data_of<MapDisplayMapData>(o);
    ext_add_ref(out, d.header.parent_map, RefKind::SoftPointer);
    for (const std::string& h : d.elements.elements) ext_add_ref(out, h, RefKind::HardOwner);
    for (const std::string& h : d.legend.items) ext_add_ref(out, h, RefKind::HardOwner);
}

// ---------------------------------------------------------------------------
// DMBASEELEMENT, DMDEFAULTSTYLE, DMSTYLEREFERENCE, DMSURROGATESTYLESETS
// ---------------------------------------------------------------------------

bool read_base_element(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapBaseElementData d;
    {
        MAP_SUBCLASS(c, "AcMapDMBaseElement", 0);
        MapBaseElementHeader& h = d.header;
        rd(c, 70, h.version);
        rds(c, 1, h.name);
        rd(c, 70, h.reserved_70_0);
        rdh(c, 330, h.map);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        MAP_SUBCLASS(c, "AcMapDMBaseElement", 1);
        MapBaseElementStyle& s = d.style;
        rd(c, 70, s.version);
        rds(c, 1, s.text_1_0);
        rd(c, 70, s.reserved_70_0);
        rds(c, 1, s.text_1_1);
        rd(c, 70, s.reserved_70_1);
        rd(c, 40, s.reserved_40_0);
        rd(c, 70, s.reserved_70_2);
        rdh(c, 360, s.style_reference);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        MAP_SUBCLASS(c, "AcMapDMBaseElement", 2);
        rd(c, 70, d.version_c);
        if (!ext_done(c, ctx, view)) return false;
    }
    if (view.subclass_count("AcMapDMBaseElement") != 3) {
        return ctx.fail(ext_describe(view) + " has " + std::to_string(view.subclass_count("AcMapDMBaseElement")) + " AcMapDMBaseElement subclasses, not three");
    }
    o.data = Boxed<MapBaseElementData>(std::move(d));
    return true;
}

void write_base_element(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMBASEELEMENT", o)) return;
    const MapBaseElementData& d = data_of<MapBaseElementData>(o);
    const std::string who = "DMBASEELEMENT " + o.handle;
    w.object_head("DMBASEELEMENT", o);
    w.subclass("AcMapDMBaseElement");
    wr16(w, 70, d.header.version);
    wrs(w, 1, d.header.name);
    wr16(w, 70, d.header.reserved_70_0);
    wrp(w, 330, d.header.map, who);
    w.subclass("AcMapDMBaseElement");
    const MapBaseElementStyle& s = d.style;
    wr16(w, 70, s.version);
    wrs(w, 1, s.text_1_0);
    wr16(w, 70, s.reserved_70_0);
    wrs(w, 1, s.text_1_1);
    wr16(w, 70, s.reserved_70_1);
    wrd(w, 40, s.reserved_40_0);
    wr16(w, 70, s.reserved_70_2);
    wrp(w, 360, s.style_reference, who);
    w.subclass("AcMapDMBaseElement");
    wr16(w, 70, d.version_c);
    w.xdata(o.xdata);
}

void base_element_references(const Object& o, std::vector<TypedReference>& out) {
    const MapBaseElementData& d = data_of<MapBaseElementData>(o);
    ext_add_ref(out, d.header.map, RefKind::SoftPointer);
    ext_add_ref(out, d.style.style_reference, RefKind::HardOwner);
}

bool read_default_style(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapDefaultStyleData d;
    {
        MAP_SUBCLASS(c, "AcMapDMDefaultStyle", 0);
        rd(c, 70, d.version);
        rd(c, 90, d.reserved_90_0);
        rds(c, 1, d.name);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        MAP_SUBCLASS(c, "AcMapDMDefaultStyle", 1);
        rd(c, 70, d.version_b);
        rd(c, 280, d.reserved_280_0);
        rd(c, 90, d.reserved_90_1);
        if (!ext_done(c, ctx, view)) return false;
    }
    if (view.subclass_count("AcMapDMDefaultStyle") != 2) {
        return ctx.fail(ext_describe(view) + " has " + std::to_string(view.subclass_count("AcMapDMDefaultStyle")) + " AcMapDMDefaultStyle subclasses, not two");
    }
    o.data = Boxed<MapDefaultStyleData>(std::move(d));
    return true;
}

void write_default_style(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMDEFAULTSTYLE", o)) return;
    const MapDefaultStyleData& d = data_of<MapDefaultStyleData>(o);
    w.object_head("DMDEFAULTSTYLE", o);
    w.subclass("AcMapDMDefaultStyle");
    wr16(w, 70, d.version);
    wr32(w, 90, d.reserved_90_0);
    wrs(w, 1, d.name);
    w.subclass("AcMapDMDefaultStyle");
    wr16(w, 70, d.version_b);
    wr16(w, 280, d.reserved_280_0);
    wr32(w, 90, d.reserved_90_1);
    w.xdata(o.xdata);
}

bool read_style_reference(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapStyleReferenceData d;
    MAP_SUBCLASS(c, "AcMapDMStyleReference", 0);
    rd(c, 70, d.version);
    rd(c, 70, d.flags);
    rdh(c, 340, d.style);
    rdh(c, 330, d.map);
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<MapStyleReferenceData>(std::move(d));
    return true;
}
void write_style_reference(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMSTYLEREFERENCE", o)) return;
    const MapStyleReferenceData& d = data_of<MapStyleReferenceData>(o);
    const std::string who = "DMSTYLEREFERENCE " + o.handle;
    w.object_head("DMSTYLEREFERENCE", o);
    w.subclass("AcMapDMStyleReference");
    wr16(w, 70, d.version);
    wr16(w, 70, d.flags);
    wrp(w, 340, d.style, who);
    wrp(w, 330, d.map, who);
    w.xdata(o.xdata);
}
void style_reference_references(const Object& o, std::vector<TypedReference>& out) {
    const MapStyleReferenceData& d = data_of<MapStyleReferenceData>(o);
    ext_add_ref(out, d.style, RefKind::HardPointer);
    ext_add_ref(out, d.map, RefKind::SoftPointer);
}

bool read_surrogate(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MapSurrogateStyleSetsData d;
    MAP_SUBCLASS(c, "AcMapDMSurrogateStyleSets", 0);
    rd(c, 70, d.version);
    rds(c, 1, d.name);
    rd(c, 70, d.reserved_70_0);
    rd(c, 40, d.reserved_40_0);
    rd(c, 70, d.reserved_70_1);
    rdh(c, 330, d.style_reference);
    rd(c, 70, d.reserved_70_2);
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<MapSurrogateStyleSetsData>(std::move(d));
    return true;
}
void write_surrogate(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "DMSURROGATESTYLESETS", o)) return;
    const MapSurrogateStyleSetsData& d = data_of<MapSurrogateStyleSetsData>(o);
    w.object_head("DMSURROGATESTYLESETS", o);
    w.subclass("AcMapDMSurrogateStyleSets");
    wr16(w, 70, d.version);
    wrs(w, 1, d.name);
    wr16(w, 70, d.reserved_70_0);
    wrd(w, 40, d.reserved_40_0);
    wr16(w, 70, d.reserved_70_1);
    wrp(w, 330, d.style_reference, "DMSURROGATESTYLESETS " + o.handle);
    wr16(w, 70, d.reserved_70_2);
    w.xdata(o.xdata);
}
void surrogate_references(const Object& o, std::vector<TypedReference>& out) {
    ext_add_ref(out, data_of<MapSurrogateStyleSetsData>(o).style_reference, RefKind::SoftPointer);
}

// ---------------------------------------------------------------------------
// Relations (B19, B104), lists (B12), classes
// ---------------------------------------------------------------------------

RefDescriptor pointer(const char* subclass, int occurrence, int code, RefKind kind) {
    RefDescriptor r;
    r.subclass = subclass;
    r.occurrence = occurrence;
    r.code = code;
    r.kind = kind;
    r.removal = RefRemoval::ZeroInPlace;
    return r;
}

RefDescriptor member(const char* subclass, int occurrence, int code, int counter) {
    RefDescriptor r = pointer(subclass, occurrence, code, RefKind::HardOwner);
    r.removal = RefRemoval::DropMember;
    r.counter_code = counter;
    return r;
}

ListSpec whole(const char* subclass, int occurrence = -1) {
    ListSpec s;
    s.subclass = subclass;
    s.occurrence = occurrence;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

void add(TypedRegistry& registry, const char* dxf, ObjectType type, ObjectReadFn read, ObjectWriteFn write,
         ObjectReferencesFn references, const char* cpp, const char* application, int flags,
         std::vector<RefDescriptor> refs, std::vector<ListSpec> lists) {
    ObjectCodec c;
    c.dxf_name = dxf;
    c.type = type;
    c.read = read;
    c.write = write;
    c.references = references;
    c.cls = ext_class(cpp, application, flags);
    c.versions = ext_object_matrix();
    c.refs = std::move(refs);
    c.lists = std::move(lists);
    c.step = 18;
    registry.add(std::move(c));
}

} // namespace

// The order of the CLASSES of the Map 3D file the records were seen in
void register_map_codecs(TypedRegistry& registry) {
    // The dictionary's entries are a (3, 350 or 360) pair each: the pair of a map that is gone goes
    RefDescriptor soft_entry = pointer("AcDbDictionary", -1, 350, RefKind::SoftOwner);
    soft_entry.removal = RefRemoval::DropPair;
    soft_entry.first_offset = -1;
    soft_entry.partner_code = 3;
    RefDescriptor hard_entry = soft_entry;
    hard_entry.code = 360;
    hard_entry.kind = RefKind::HardOwner;
    ListSpec entries;
    entries.subclass = "AcDbDictionary";
    entries.id = 1;
    entries.shape = ListShape::Codes;
    entries.codes = {3, 350, 360};
    add(registry, "DMMAPMANAGER", ObjectType::MapManager, read_manager, write_manager, manager_references, "AcMapDMMapManager",
        kDisplay, 1024,
        {soft_entry, hard_entry, pointer("AcDbDictionaryWithDefault", -1, 340, RefKind::HardPointer),
         pointer("AcMapDMMapManager", -1, 340, RefKind::HardPointer)},
        {entries});
    add(registry, "DMSTYLELIBRARY", ObjectType::MapStyleLibrary, read_library, write_library, library_references, "AcMapDMStyleLibrary",
        kDisplay, 1024, {member("AcMapDMStyleLibrary", -1, 360, 70)}, {whole("AcMapDMStyleLibrary")});
    add(registry, "DMSTYLECATEGORY", ObjectType::MapStyleCategory, read_category, write_category, nullptr, "AcMapDMStyleCategory",
        kDisplay, 1024, {}, {});
    add(registry, "MAPRESOURCEMANAGEROBJECT", ObjectType::MapResourceManager, read_resource_manager, write_resource_manager, nullptr,
        "AcMapResourceManager", "AcMapSharedComponents", 1024, {}, {});
    add(registry, "DMMAP", ObjectType::MapDisplayMap, read_map, write_map, map_references, "AcMapDMMap", kDisplay, 1024,
        {pointer("AcMapDMMap", 0, 330, RefKind::SoftPointer), member("AcMapDMMap", 1, 360, 90), member("AcMapDMMap", 2, 360, 90)},
        {whole("AcMapDMMap", 1), whole("AcMapDMMap", 2)});
    add(registry, "DMBASEELEMENT", ObjectType::MapBaseElement, read_base_element, write_base_element, base_element_references,
        "AcMapDMBaseElement", kDisplay, 1024,
        {pointer("AcMapDMBaseElement", 0, 330, RefKind::SoftPointer), pointer("AcMapDMBaseElement", 1, 360, RefKind::HardOwner)}, {});
    add(registry, "DMDEFAULTSTYLE", ObjectType::MapDefaultStyle, read_default_style, write_default_style, nullptr,
        "AcMapDMDefaultStyle", kDisplay, 1024, {}, {});
    add(registry, "DMSTYLEREFERENCE", ObjectType::MapStyleReference, read_style_reference, write_style_reference,
        style_reference_references, "AcMapDMStyleReference", kDisplay, 1024,
        {pointer("AcMapDMStyleReference", -1, 340, RefKind::HardPointer), pointer("AcMapDMStyleReference", -1, 330, RefKind::SoftPointer)}, {});
    add(registry, "DMSURROGATESTYLESETS", ObjectType::MapSurrogateStyleSets, read_surrogate, write_surrogate, surrogate_references,
        "AcMapDMSurrogateStyleSets", kDisplay, 1153, {pointer("AcMapDMSurrogateStyleSets", -1, 330, RefKind::SoftPointer)}, {});
    add(registry, "DMSTYLIZEDENTITIESTABLE", ObjectType::MapStylizedEntitiesTable, read_stylized_table, write_stylized_table, nullptr,
        "AcMapDMStylizedEntitiesTable", kDisplay, 1024, {}, {});
    add(registry, "MAPMETADATAOBJECT", ObjectType::MapMetadata, read_metadata, write_metadata, nullptr,
        "AcMapMetadataPersistObject", "AcMapFeatureSourceManager", 1025, {}, {});
}

} // namespace internal
} // namespace hdxf
