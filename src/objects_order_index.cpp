#include "OrderIndexTypes.hpp"

// Draw order, clipping and indexes (phase 9, step 9.4): reading the six object types and registering their
// codecs (relations, lists, classes and the version matrix). The groups are AutoCAD's own (doc/facit/
// acad_*_dxf_order_index*.dxf, acad_*_dxf_native_records.dxf, ACadSharp's sample_AC1015 …) where they show
// something and Autodesk's DXF reference where they do not.

namespace hdxf {
namespace internal {

namespace {

std::string describe(const char* type, const Object& o) {
    return std::string(type) + " " + o.handle;
}

// What follows the groups a codec knows (a later version of the record): kept as the file has it (the
// preserved record), with a warning (B105)
void note_rest(GroupCursor& c, TypedReadContext& ctx, const char* type, const Object& o) {
    if (!c.ok() || c.at_end()) return;
    size_t n = c.take_rest().size();
    ctx.warn(describe(type, o) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
             ") are kept as they were read");
}

// ---------------------------------------------------------------------------
// SORTENTSTABLE: S(AcDbSortentsTable) 330 (331 5)*
// ---------------------------------------------------------------------------

bool read_sortents_table(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbSortentsTable");
    if (!c.valid()) return ctx.fail(describe("SORTENTSTABLE", o) + " has no AcDbSortentsTable subclass");
    SortentsTableData d;
    c.handle(330, d.block_record_handle);
    while (c.ok() && c.code() == 331) {
        SortEntry e;
        c.handle(331, e.entity_handle);
        if (!c.handle(5, e.sort_key)) {
            return ctx.fail(describe("SORTENTSTABLE", o) + ": the entity " + e.entity_handle +
                            " (331) has no sort handle (5) after it");
        }
        d.entries.push_back(std::move(e));
    }
    if (!c.ok()) return ctx.fail(describe("SORTENTSTABLE", o) + ": " + c.error());
    note_rest(c, ctx, "SORTENTSTABLE", o);
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// SPATIAL_FILTER: S(AcDbFilter) S(AcDbSpatialFilter) 70 (10 20)* 210 220 230 11 21 31 71 72 [40] 73 [41] 40*24
// ---------------------------------------------------------------------------

bool read_spatial_filter(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbSpatialFilter");
    if (!c.valid()) return ctx.fail(describe("SPATIAL_FILTER", o) + " has no AcDbSpatialFilter subclass");
    SpatialFilterData d;
    size_t points = 0;
    if (!c.count(70, 2, points)) {
        return ctx.fail(describe("SPATIAL_FILTER", o) + ": " + (c.ok() ? "the number of boundary points (70) is missing" : c.error()));
    }
    d.boundary.reserve(points);
    for (size_t i = 0; i < points; ++i) {
        Point2 p;
        if (!c.point2(10, p)) return ctx.fail(describe("SPATIAL_FILTER", o) + ": boundary point " + std::to_string(i) + " (10, 20) is missing");
        d.boundary.push_back(p);
    }
    c.require_point3(210, d.normal);
    c.require_point3(11, d.origin);
    c.require_boolean(71, d.enabled);
    bool front = false, back = false;
    c.require_boolean(72, front);
    if (front) {
        double v = 0.0;
        c.require_real(40, v);
        d.front_distance = v;
    }
    c.require_boolean(73, back);
    if (back) {
        double v = 0.0;
        c.require_real(41, v);
        d.back_distance = v;
    }
    for (double& v : d.inverse_insert_transform) c.require_real(40, v);
    for (double& v : d.clip_transform) c.require_real(40, v);
    if (!c.ok()) return ctx.fail(describe("SPATIAL_FILTER", o) + ": " + c.error());
    note_rest(c, ctx, "SPATIAL_FILTER", o);
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// LAYER_INDEX: S(AcDbIndex) 40 S(AcDbLayerIndex) (8 360 90)*
// ---------------------------------------------------------------------------

bool read_layer_index(const RecordView& view, TypedReadContext& ctx, Object& o) {
    LayerIndexData d;
    GroupCursor index(view, "AcDbIndex");
    if (!index.valid()) return ctx.fail(describe("LAYER_INDEX", o) + " has no AcDbIndex subclass");
    index.require_real(40, d.timestamp);
    if (!index.ok()) return ctx.fail(describe("LAYER_INDEX", o) + ": " + index.error());
    note_rest(index, ctx, "LAYER_INDEX", o);
    GroupCursor c(view, "AcDbLayerIndex");
    if (!c.valid()) return ctx.fail(describe("LAYER_INDEX", o) + " has no AcDbLayerIndex subclass");
    while (c.ok() && c.code() == 8) {
        LayerIndexEntry e;
        c.string(8, e.layer_name);
        c.require_handle(360, e.idbuffer_handle);
        c.require_integer(90, e.count);
        if (!c.ok()) break;
        d.entries.push_back(std::move(e));
    }
    if (!c.ok()) return ctx.fail(describe("LAYER_INDEX", o) + ": " + c.error());
    note_rest(c, ctx, "LAYER_INDEX", o);
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// SPATIAL_INDEX: S(AcDbIndex) 40 S(AcDbSpatialIndex) [40*6 90 90 310* 1]
// ---------------------------------------------------------------------------

bool read_spatial_index(const RecordView& view, TypedReadContext& ctx, Object& o) {
    SpatialIndexData d;
    GroupCursor index(view, "AcDbIndex");
    if (!index.valid()) return ctx.fail(describe("SPATIAL_INDEX", o) + " has no AcDbIndex subclass");
    index.require_real(40, d.timestamp);
    if (!index.ok()) return ctx.fail(describe("SPATIAL_INDEX", o) + ": " + index.error());
    note_rest(index, ctx, "SPATIAL_INDEX", o);
    GroupCursor c(view, "AcDbSpatialIndex");
    if (!c.valid()) return ctx.fail(describe("SPATIAL_INDEX", o) + " has no AcDbSpatialIndex subclass");
    if (c.code() == 40) {
        SpatialIndexTree tree;
        for (double& v : tree.values) c.require_real(40, v);
        c.require_integer(90, tree.reserved);
        int64_t length = 0;
        c.require_integer(90, length);
        if (!c.ok()) return ctx.fail(describe("SPATIAL_INDEX", o) + ": " + c.error());
        c.bytes(310, tree.data);
        if (!c.ok()) return ctx.fail(describe("SPATIAL_INDEX", o) + ": " + c.error());
        if (static_cast<int64_t>(tree.data.size()) != length) {
            return ctx.fail(describe("SPATIAL_INDEX", o) + ": the length (90) is " + std::to_string(length) + " but the 310 groups hold " +
                            std::to_string(tree.data.size()) + " bytes");
        }
        c.require_string(1, tree.end_marker);
        if (!c.ok()) return ctx.fail(describe("SPATIAL_INDEX", o) + ": " + c.error());
        d.tree = std::move(tree);
    }
    note_rest(c, ctx, "SPATIAL_INDEX", o);
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// IDBUFFER: S(AcDbIdBuffer) 330*
// ---------------------------------------------------------------------------

bool read_id_buffer(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbIdBuffer");
    if (!c.valid()) return ctx.fail(describe("IDBUFFER", o) + " has no AcDbIdBuffer subclass");
    IdBufferData d;
    while (c.ok() && c.code() == 330) {
        std::string h;
        c.handle(330, h);
        d.object_handles.push_back(std::move(h));
    }
    if (!c.ok()) return ctx.fail(describe("IDBUFFER", o) + ": " + c.error());
    note_rest(c, ctx, "IDBUFFER", o);
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// LAYER_FILTER: S(AcDbFilter) S(AcDbLayerFilter) 8*
// ---------------------------------------------------------------------------

bool read_layer_filter(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbLayerFilter");
    if (!c.valid()) return ctx.fail(describe("LAYER_FILTER", o) + " has no AcDbLayerFilter subclass");
    LayerFilterData d;
    while (c.ok() && c.code() == 8) {
        std::string name;
        c.string(8, name);
        d.layers.push_back(std::move(name));
    }
    if (!c.ok()) return ctx.fail(describe("LAYER_FILTER", o) + ": " + c.error());
    note_rest(c, ctx, "LAYER_FILTER", o);
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// Relations (B19, B104)
// ---------------------------------------------------------------------------

// A layer index owns the IDBUFFERs of its layers (hard owner); an IDBUFFER owned by nothing else has the owner 0
void layer_index_references(const Object& o, std::vector<TypedReference>& out) {
    for (const LayerIndexEntry& e : o.as_layer_index().entries) {
        if (!e.idbuffer_handle.empty()) out.push_back({e.idbuffer_handle, RefKind::HardOwner, false});
    }
}

RefDescriptor pointer(const char* subclass, int code, RefKind kind, RefRemoval removal) {
    RefDescriptor d;
    d.subclass = subclass;
    d.code = code;
    d.kind = kind;
    d.removal = removal;
    return d;
}

std::vector<RefDescriptor> sortents_refs() {
    // The pair of an entity that is gone goes, as AutoCAD removes it: 331 then 5 (0, +1). The sort handle is an
    // ordering key, not a pointer, and never zeroed or taken for one (B19, B104); the block record is one pointer
    RefDescriptor pair = pointer("AcDbSortentsTable", 331, RefKind::SoftPointer, RefRemoval::DropPair);
    pair.first_offset = 0;
    pair.last_offset = 1;
    pair.partner_code = 5;
    RefDescriptor key = pointer("AcDbSortentsTable", 5, RefKind::SortKey, RefRemoval::Keep);
    RefDescriptor record = pointer("AcDbSortentsTable", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace);
    record.ordinal = 0;
    return {pair, key, record};
}

std::vector<RefDescriptor> layer_index_refs() {
    // An entry (8 360 90) whose IDBUFFER is gone goes whole; the layer name is not a pointer
    RefDescriptor entry = pointer("AcDbLayerIndex", 360, RefKind::HardOwner, RefRemoval::DropPair);
    entry.first_offset = -1;
    entry.last_offset = 1;
    entry.partner_code = 90;
    return {entry};
}

std::vector<RefDescriptor> id_buffer_refs() {
    return {pointer("AcDbIdBuffer", 330, RefKind::SoftPointer, RefRemoval::DropMember)};
}

// ---------------------------------------------------------------------------
// Lists (B12)
// ---------------------------------------------------------------------------

ListSpec list(const char* subclass, int id, ListShape shape, std::vector<int> codes) {
    ListSpec s;
    s.subclass = subclass;
    s.id = id;
    s.shape = shape;
    s.codes = std::move(codes);
    return s;
}

// ---------------------------------------------------------------------------
// Classes and versions
// ---------------------------------------------------------------------------

ClassSpec class_of(const char* cpp_name, const char* older_application) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 0;
    c.in_older = true;
    c.older_application = older_application;
    c.older_flags = 0;
    return c;
}

// R13 on (AutoCAD R14's own files list the classes of all six; R14's reference has the five that have OBJECTS
// sections here and the same groups). AutoCAD 2025 saved every one of them in 2000 … 2018, ASCII and binary
// (doc/facit/acad_*_dxf_order_index*.dxf; LAYER_FILTER: acad_*_dxf_native_records.dxf); its R12 files have
// neither the records nor an OBJECTS section. R13/R14 cannot be saved by AutoCAD 2025 (NotSavable): ej testat
VersionMatrix matrix() {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R13);
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

template <typename Codec>
void common(Codec& c, const char* name, ObjectType type, ObjectReadFn read, ObjectWriteFn write) {
    c.dxf_name = name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.versions = matrix();
    c.step = 4;
}

} // namespace

void register_order_index_codecs(TypedRegistry& registry) {
    // In the order AutoCAD lists the classes in its own files (LAYER_INDEX, SPATIAL_INDEX, IDBUFFER, SPATIAL_FILTER,
    // LAYER_FILTER, SORTENTSTABLE), which is the order they get in CLASSES of a new drawing
    {
        ObjectCodec c;
        common(c, "LAYER_INDEX", ObjectType::LayerIndex, read_layer_index, write_layer_index);
        c.references = layer_index_references;
        c.cls = class_of("AcDbLayerIndex", "\"Acad\"");
        c.refs = layer_index_refs();
        c.lists = {list("AcDbLayerIndex", 1, ListShape::FromCode, {8})};
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        common(c, "SPATIAL_INDEX", ObjectType::SpatialIndex, read_spatial_index, write_spatial_index);
        c.cls = class_of("AcDbSpatialIndex", "\"Acad\"");
        c.lists = {list("AcDbSpatialIndex", 1, ListShape::WholeSubclass, {})};
        // R13 and R14: the index is written empty (the reference); the tree is 2000's (AutoCAD's own files)
        c.versions.set(DxfVersion::R13, Generation::OlderForm).set(DxfVersion::R14, Generation::OlderForm);
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        common(c, "IDBUFFER", ObjectType::IdBuffer, read_id_buffer, write_id_buffer);
        c.cls = class_of("AcDbIdBuffer", "\"Acad\"");
        c.refs = id_buffer_refs();
        c.lists = {list("AcDbIdBuffer", 1, ListShape::Codes, {330})};
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        common(c, "SPATIAL_FILTER", ObjectType::SpatialFilter, read_spatial_filter, write_spatial_filter);
        c.cls = class_of("AcDbSpatialFilter", "\"Acad\"");
        // The boundary and its count are one list; the clip planes and the two matrices are another: 40 is the front
        // distance (when 72 is 1) and every number of the matrices, so the group is told apart by its place only
        c.lists = {list("AcDbSpatialFilter", 1, ListShape::Codes, {70, 10, 20}),
                   list("AcDbSpatialFilter", 2, ListShape::FromCode, {72})};
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        common(c, "LAYER_FILTER", ObjectType::LayerFilter, read_layer_filter, write_layer_filter);
        c.cls = class_of("AcDbLayerFilter", "\"Acad\"");
        c.lists = {list("AcDbLayerFilter", 1, ListShape::Codes, {8})};
        // R13 and R14: neither AutoCAD R14's reference nor the class lists of its files have LAYER_FILTER; no file shows
        // the record there, so no version error is claimed and a new record is not made (write_layer_filter): ej testat
        c.cls.in_older = false;
        c.versions.set(DxfVersion::R13, Generation::None, ErrorBasis::NotEstablished)
            .set(DxfVersion::R14, Generation::None, ErrorBasis::NotEstablished);
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        common(c, "SORTENTSTABLE", ObjectType::SortentsTable, read_sortents_table, write_sortents_table);
        c.cls = class_of("AcDbSortentsTable", "\"AutoCAD\"");
        c.refs = sortents_refs();
        c.lists = {list("AcDbSortentsTable", 1, ListShape::Codes, {331, 5})};
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
