#include "AssocExtended.hpp"
#include <type_traits>

// The action bodies of the associative surface operations (phase 9, step 9.15; plan 2026-10-27-fas-9-typer.md, F9.12):
// ACDBASSOCOFFSETSURFACEACTIONBODY, ...EXTENDSURFACE..., ...TRIMSURFACE..., ...PATCHSURFACE..., ...NETWORKSURFACE...,
// ...BLENDSURFACE..., ...FILLETSURFACE..., ACDBASSOCEDGEFILLETACTIONBODY, ACDBASSOCEDGECHAMFERACTIONBODY and
// ACDBASSOCRESTOREENTITYSTATEACTIONBODY. AutoCAD 2025's own files (doc/facit/fas9_assoc_extended.scr and
// fas9_assoc_core.scr in 2000 … 2018, ASCII and binary; the older surfaces files of 9.10) have them as the 9.10 bodies are
// built: AcDbAssocActionBody (and, in a body of version 1, AcDbAssocParamBasedActionBody), AcDbAssocSurfaceActionBody, in
// most of them AcDbAssocPathBasedSurfaceActionBody (not in the offset, the fillet and the restore), then the type's own:
//   offset     AcDbAssocOffsetSurfaceActionBody       90, 290
//   extend     AcDbAssocExtendSurfaceActionBody       90, 280
//   trim       AcDbAssocTrimSurfaceActionBody         90, 290, 290, 40
//   patch      AcDbAssocPatchSurfaceActionBody        90
//   network    AcDbAssocNetworkSurfaceActionBody      90
//   blend      AcDbAssocBlendSurfaceActionBody        90, 290, 291, 292, 72, 293, 294, 73
//   fillet     AcDbAssocFilletSurfaceActionBody       90, 70 and two 2D points
//   edge fillet and edge chamfer, both AcDbAssocEdgeFilletChamferActionBody: 90, 290 (whether a persistent subentity id
//              follows: 1 and the groups of its class), then three arrays of 3D points (90 count + 10 each)
//   restore    no surface base: AcDbAssocActionBody, AcDbAssocRestoreEntityStateActionBody 90, 330

namespace hdxf {
namespace internal {

bool assoc_read_body_parts(const RecordView& view, TypedReadContext& ctx, AssocActionBodyHead& head, AssocSurfaceActionBody* surface,
                           AssocPathBasedSurfaceActionBody* path_based, const std::function<bool(GroupCursor&)>& tail) {
    if (!read_assoc_action_body_head(view, ctx, head, tail)) return false;
    if (surface && !read_assoc_surface_body(view, ctx, *surface)) return false;
    if (path_based && !read_assoc_path_based_body(view, ctx, *path_based)) return false;
    return true;
}

void assoc_write_body_parts(RecordWriter& w, const AssocActionBodyHead& head, const AssocSurfaceActionBody* surface,
                            const AssocPathBasedSurfaceActionBody* path_based, const std::function<void()>& tail) {
    write_assoc_action_body_head(w, head, tail);
    if (surface) write_assoc_surface_body(w, *surface);
    if (path_based) write_assoc_path_based_body(w, *path_based);
}

namespace {

template <typename Data>
Data& data_of(Object& o) { return *o.data.get<Boxed<Data>>(); }
template <typename Data>
const Data& data_of(const Object& o) { return *o.data.get<Boxed<Data>>(); }

// ---------------------------------------------------------------------------
// One body: the bases it has and the codec that reads and writes its own subclass
// ---------------------------------------------------------------------------

template <typename Data>
void references_of(const Object& o, std::vector<TypedReference>& out) {
    const Data& d = data_of<Data>(o);
    assoc_body_references(d.head, out);
    assoc_add_reference(out, d.surface.result_dependency, RefKind::SoftPointer);
}

// ---- offset ----
bool read_offset(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocOffsetSurfaceActionBodyData d;
    if (!assoc_read_body_parts(view, ctx, d.head, &d.surface, nullptr)) return false;
    GroupCursor c(view, "AcDbAssocOffsetSurfaceActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocOffsetSurfaceActionBody subclass");
    c.require_integer(90, d.version);
    c.require_boolean(290, d.flag);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocOffsetSurfaceActionBodyData>(std::move(d));
    return true;
}
void write_offset(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocOffsetSurfaceActionBodyData>(o);
    w.object_head("ACDBASSOCOFFSETSURFACEACTIONBODY", o);
    assoc_write_body_parts(w, d.head, &d.surface, nullptr);
    w.subclass("AcDbAssocOffsetSurfaceActionBody");
    w.integer(90, d.version);
    w.boolean(290, d.flag);
    w.xdata(o.xdata);
}

// ---- extend ----
bool read_extend(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocExtendSurfaceActionBodyData d;
    if (!assoc_read_body_parts(view, ctx, d.head, &d.surface, &d.path_based)) return false;
    GroupCursor c(view, "AcDbAssocExtendSurfaceActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocExtendSurfaceActionBody subclass");
    c.require_integer(90, d.version);
    c.require_integer(280, d.value_280);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocExtendSurfaceActionBodyData>(std::move(d));
    return true;
}
void write_extend(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocExtendSurfaceActionBodyData>(o);
    w.object_head("ACDBASSOCEXTENDSURFACEACTIONBODY", o);
    assoc_write_body_parts(w, d.head, &d.surface, &d.path_based);
    w.subclass("AcDbAssocExtendSurfaceActionBody");
    w.integer(90, d.version);
    w.integer(280, d.value_280);
    w.xdata(o.xdata);
}

// ---- trim ----
bool read_trim(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocTrimSurfaceActionBodyData d;
    if (!assoc_read_body_parts(view, ctx, d.head, &d.surface, &d.path_based)) return false;
    GroupCursor c(view, "AcDbAssocTrimSurfaceActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocTrimSurfaceActionBody subclass");
    c.require_integer(90, d.version);
    c.require_boolean(290, d.flag_290_a);
    c.require_boolean(290, d.flag_290_b);
    c.require_real(40, d.value);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocTrimSurfaceActionBodyData>(std::move(d));
    return true;
}
void write_trim(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocTrimSurfaceActionBodyData>(o);
    w.object_head("ACDBASSOCTRIMSURFACEACTIONBODY", o);
    assoc_write_body_parts(w, d.head, &d.surface, &d.path_based);
    w.subclass("AcDbAssocTrimSurfaceActionBody");
    w.integer(90, d.version);
    w.boolean(290, d.flag_290_a);
    w.boolean(290, d.flag_290_b);
    w.real(40, d.value);
    w.xdata(o.xdata);
}

// ---- patch and network: a version only ----
template <typename Data>
bool read_version_only(const RecordView& view, TypedReadContext& ctx, Object& o, const char* subclass) {
    Data d;
    if (!assoc_read_body_parts(view, ctx, d.head, &d.surface, &d.path_based)) return false;
    GroupCursor c(view, subclass);
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no " + subclass + " subclass");
    c.require_integer(90, d.version);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<Data>(std::move(d));
    return true;
}
template <typename Data>
void write_version_only(RecordWriter& w, const Object& o, const char* dxf_name, const char* subclass) {
    const Data& d = data_of<Data>(o);
    w.object_head(dxf_name, o);
    assoc_write_body_parts(w, d.head, &d.surface, &d.path_based);
    w.subclass(subclass);
    w.integer(90, d.version);
    w.xdata(o.xdata);
}
bool read_patch(const RecordView& view, TypedReadContext& ctx, Object& o) {
    return read_version_only<AssocPatchSurfaceActionBodyData>(view, ctx, o, "AcDbAssocPatchSurfaceActionBody");
}
void write_patch(RecordWriter& w, const Object& o) {
    write_version_only<AssocPatchSurfaceActionBodyData>(w, o, "ACDBASSOCPATCHSURFACEACTIONBODY", "AcDbAssocPatchSurfaceActionBody");
}
bool read_network_surface(const RecordView& view, TypedReadContext& ctx, Object& o) {
    return read_version_only<AssocNetworkSurfaceActionBodyData>(view, ctx, o, "AcDbAssocNetworkSurfaceActionBody");
}
void write_network_surface(RecordWriter& w, const Object& o) {
    write_version_only<AssocNetworkSurfaceActionBodyData>(w, o, "ACDBASSOCNETWORKSURFACEACTIONBODY", "AcDbAssocNetworkSurfaceActionBody");
}

// ---- blend ----
bool read_blend(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocBlendSurfaceActionBodyData d;
    if (!assoc_read_body_parts(view, ctx, d.head, &d.surface, &d.path_based)) return false;
    GroupCursor c(view, "AcDbAssocBlendSurfaceActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocBlendSurfaceActionBody subclass");
    c.require_integer(90, d.version);
    c.require_boolean(290, d.flag_290);
    c.require_boolean(291, d.flag_291);
    c.require_boolean(292, d.flag_292);
    c.require_integer(72, d.value_72);
    c.require_boolean(293, d.flag_293);
    c.require_boolean(294, d.flag_294);
    c.require_integer(73, d.value_73);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocBlendSurfaceActionBodyData>(std::move(d));
    return true;
}
void write_blend(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocBlendSurfaceActionBodyData>(o);
    w.object_head("ACDBASSOCBLENDSURFACEACTIONBODY", o);
    assoc_write_body_parts(w, d.head, &d.surface, &d.path_based);
    w.subclass("AcDbAssocBlendSurfaceActionBody");
    w.integer(90, d.version);
    w.boolean(290, d.flag_290);
    w.boolean(291, d.flag_291);
    w.boolean(292, d.flag_292);
    w.integer(72, d.value_72);
    w.boolean(293, d.flag_293);
    w.boolean(294, d.flag_294);
    w.integer(73, d.value_73);
    w.xdata(o.xdata);
}

// ---- fillet ----
bool read_fillet(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocFilletSurfaceActionBodyData d;
    if (!assoc_read_body_parts(view, ctx, d.head, &d.surface, nullptr)) return false;
    GroupCursor c(view, "AcDbAssocFilletSurfaceActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocFilletSurfaceActionBody subclass");
    c.require_integer(90, d.version);
    c.require_integer(70, d.value_70);
    if (c.ok() && !c.point2(10, d.first_point)) c.fail("the first point (10) is missing");
    if (c.ok() && !c.point2(10, d.second_point)) c.fail("the second point (10) is missing");
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocFilletSurfaceActionBodyData>(std::move(d));
    return true;
}
void write_fillet(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocFilletSurfaceActionBodyData>(o);
    w.object_head("ACDBASSOCFILLETSURFACEACTIONBODY", o);
    assoc_write_body_parts(w, d.head, &d.surface, nullptr);
    w.subclass("AcDbAssocFilletSurfaceActionBody");
    w.integer(90, d.version);
    w.integer(70, d.value_70);
    w.point2(10, d.first_point);
    w.point2(10, d.second_point);
    w.xdata(o.xdata);
}

// ---- edge fillet and edge chamfer ----
bool read_points(GroupCursor& c, std::vector<Point3>& out) {
    size_t n = 0;
    if (!c.count(90, 3, n)) {
        if (c.ok()) c.fail("a count (90) of points is missing");
        return false;
    }
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        Point3 p;
        c.require_point3(10, p);
        out.push_back(p);
    }
    return c.ok();
}
void write_points(RecordWriter& w, const std::vector<Point3>& points) {
    w.integer(90, static_cast<int64_t>(points.size()));
    for (const Point3& p : points) w.point3(10, p);
}

bool read_edge_fillet_chamfer(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocEdgeFilletChamferActionBodyData d;
    if (!assoc_read_body_parts(view, ctx, d.head, &d.surface, &d.path_based)) return false;
    GroupCursor c(view, "AcDbAssocEdgeFilletChamferActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocEdgeFilletChamferActionBody subclass");
    c.require_integer(90, d.version);
    bool has_id = false;
    c.require_boolean(290, has_id);
    if (c.ok() && has_id) {
        PersistentSubentityId id;
        if (read_persistent_subentity_id(c, id)) d.subentity_id = id;
    }
    if (c.ok()) read_points(c, d.first_points);
    if (c.ok()) read_points(c, d.second_points);
    if (c.ok()) read_points(c, d.third_points);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocEdgeFilletChamferActionBodyData>(std::move(d));
    return true;
}
void write_edge_fillet_chamfer(RecordWriter& w, const Object& o, const char* dxf_name) {
    const auto& d = data_of<AssocEdgeFilletChamferActionBodyData>(o);
    w.object_head(dxf_name, o);
    assoc_write_body_parts(w, d.head, &d.surface, &d.path_based);
    w.subclass("AcDbAssocEdgeFilletChamferActionBody");
    w.integer(90, d.version);
    w.boolean(290, d.subentity_id.has_value());
    if (d.subentity_id) write_persistent_subentity_id(w, *d.subentity_id);
    write_points(w, d.first_points);
    write_points(w, d.second_points);
    write_points(w, d.third_points);
    w.xdata(o.xdata);
}
void write_edge_fillet(RecordWriter& w, const Object& o) { write_edge_fillet_chamfer(w, o, "ACDBASSOCEDGEFILLETACTIONBODY"); }
void write_edge_chamfer(RecordWriter& w, const Object& o) { write_edge_fillet_chamfer(w, o, "ACDBASSOCEDGECHAMFERACTIONBODY"); }

// ---- restore entity state ----
// A restore body of version 1 (every file up to 2010) has AcDbAssocActionBody's 90 and no AcDbAssocParamBasedActionBody: the
// head's parameters are absent (doc/facit/acad_2000_dxf_assoc_extended.dxf …)
bool read_restore_head(const RecordView& view, TypedReadContext& ctx, AssocActionBodyHead& head) {
    if (GroupCursor(view, "AcDbAssocParamBasedActionBody").valid()) return read_assoc_action_body_head(view, ctx, head);
    GroupCursor c(view, "AcDbAssocActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocActionBody subclass");
    head = AssocActionBodyHead();
    c.require_integer(90, head.version);
    if (!c.ok()) return ctx.fail(c.error());
    if (head.version != 1 && head.version != 2) {
        return ctx.fail(assoc_describe(view) + ": AcDbAssocActionBody version " + std::to_string(head.version) + " is not established");
    }
    assoc_note_rest(c, ctx);
    return true;
}

void write_restore_head(RecordWriter& w, const AssocActionBodyHead& head) {
    if (head.version == 1 && !head.param_based) {
        w.subclass("AcDbAssocActionBody");
        w.integer(90, head.version);
        return;
    }
    write_assoc_action_body_head(w, head);
}

bool read_restore(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocRestoreEntityStateActionBodyData d;
    if (!read_restore_head(view, ctx, d.head)) return false;
    GroupCursor c(view, "AcDbAssocRestoreEntityStateActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocRestoreEntityStateActionBody subclass");
    c.require_integer(90, d.version);
    c.require_handle(330, d.dependency);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocRestoreEntityStateActionBodyData>(std::move(d));
    return true;
}
void write_restore(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocRestoreEntityStateActionBodyData>(o);
    w.object_head("ACDBASSOCRESTOREENTITYSTATEACTIONBODY", o);
    write_restore_head(w, d.head);
    w.subclass("AcDbAssocRestoreEntityStateActionBody");
    w.integer(90, d.version);
    w.handle(330, assoc_pointer(w, d.dependency));
    w.xdata(o.xdata);
}
void restore_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocRestoreEntityStateActionBodyData>(o);
    assoc_body_references(d.head, out);
    assoc_add_reference(out, d.dependency, RefKind::SoftPointer);
}

// ---- implied surface or solid ----
bool read_implied(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocImpliedSurfaceOrSolidActionBodyData d;
    if (!read_restore_head(view, ctx, d.head)) return false;
    GroupCursor c(view, "AcDbAssocImpliedSurfaceOrSolidActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocImpliedSurfaceOrSolidActionBody subclass");
    c.require_integer(90, d.version);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocImpliedSurfaceOrSolidActionBodyData>(std::move(d));
    return true;
}
void write_implied(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocImpliedSurfaceOrSolidActionBodyData>(o);
    w.object_head("ACDBASSOCIMPLIEDSURFACEORSOLIACTIONBODY", o);
    write_restore_head(w, d.head);
    w.subclass("AcDbAssocImpliedSurfaceOrSolidActionBody");
    w.integer(90, d.version);
    w.xdata(o.xdata);
}
void implied_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_body_references(data_of<AssocImpliedSurfaceOrSolidActionBodyData>(o).head, out);
}

void add_body(TypedRegistry& registry, const char* name, ObjectType type, ObjectReadFn read, ObjectWriteFn write,
              ObjectReferencesFn references, const char* cpp_name, std::vector<RefDescriptor> extra_refs = {},
              bool verified = true, int step = 15) {
    ObjectCodec c;
    c.dxf_name = name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.references = references;
    c.cls = assoc_class(cpp_name);
    // AutoCAD's own files: doc/facit/fas9_assoc_extended.scr (2000 … 2018, both formats)
    c.versions = verified ? assoc_matrix(assoc_all_versions()) : assoc_matrix({});
    c.refs = assoc_body_descriptors();
    for (const RefDescriptor& r : extra_refs) c.refs.push_back(r);
    // The parameters of a version 1 body have as many groups as they have: the subclass is one list
    c.lists = {assoc_whole_subclass("AcDbAssocParamBasedActionBody")};
    c.step = step;
    registry.add(std::move(c));
}

} // namespace

void register_assoc_surface_op_codecs(TypedRegistry& registry) {
    add_body(registry, "ACDBASSOCOFFSETSURFACEACTIONBODY", ObjectType::AssocOffsetSurfaceActionBody, read_offset, write_offset,
             references_of<AssocOffsetSurfaceActionBodyData>, "AcDbAssocOffsetSurfaceActionBody");
    add_body(registry, "ACDBASSOCEXTENDSURFACEACTIONBODY", ObjectType::AssocExtendSurfaceActionBody, read_extend, write_extend,
             references_of<AssocExtendSurfaceActionBodyData>, "AcDbAssocExtendSurfaceActionBody");
    add_body(registry, "ACDBASSOCTRIMSURFACEACTIONBODY", ObjectType::AssocTrimSurfaceActionBody, read_trim, write_trim,
             references_of<AssocTrimSurfaceActionBodyData>, "AcDbAssocTrimSurfaceActionBody");
    add_body(registry, "ACDBASSOCPATCHSURFACEACTIONBODY", ObjectType::AssocPatchSurfaceActionBody, read_patch, write_patch,
             references_of<AssocPatchSurfaceActionBodyData>, "AcDbAssocPatchSurfaceActionBody");
    add_body(registry, "ACDBASSOCNETWORKSURFACEACTIONBODY", ObjectType::AssocNetworkSurfaceActionBody, read_network_surface,
             write_network_surface, references_of<AssocNetworkSurfaceActionBodyData>, "AcDbAssocNetworkSurfaceActionBody");
    add_body(registry, "ACDBASSOCBLENDSURFACEACTIONBODY", ObjectType::AssocBlendSurfaceActionBody, read_blend, write_blend,
             references_of<AssocBlendSurfaceActionBodyData>, "AcDbAssocBlendSurfaceActionBody");
    add_body(registry, "ACDBASSOCFILLETSURFACEACTIONBODY", ObjectType::AssocFilletSurfaceActionBody, read_fillet, write_fillet,
             references_of<AssocFilletSurfaceActionBodyData>, "AcDbAssocFilletSurfaceActionBody");
    add_body(registry, "ACDBASSOCEDGEFILLETACTIONBODY", ObjectType::AssocEdgeFilletActionBody, read_edge_fillet_chamfer,
             write_edge_fillet, references_of<AssocEdgeFilletChamferActionBodyData>, "AcDbAssocEdgeFilletActionBody");
    add_body(registry, "ACDBASSOCEDGECHAMFERACTIONBODY", ObjectType::AssocEdgeChamferActionBody, read_edge_fillet_chamfer,
             write_edge_chamfer, references_of<AssocEdgeFilletChamferActionBodyData>, "AcDbAssocEdgeChamferActionBody");
    add_body(registry, "ACDBASSOCRESTOREENTITYSTATEACTIONBODY", ObjectType::AssocRestoreEntityStateActionBody, read_restore,
             write_restore, restore_references, "AcDbAssocRestoreEntityStateActionBody",
             {assoc_ref("AcDbAssocRestoreEntityStateActionBody", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace)});
    // AutoCAD 2025's own files: doc/facit/acad_*_dxf_surfaces_trim_trimmed.dxf (2000 … 2018)
    add_body(registry, "ACDBASSOCIMPLIEDSURFACEORSOLIACTIONBODY", ObjectType::AssocImpliedSurfaceOrSolidActionBody, read_implied,
             write_implied, implied_references, "AcDbAssocImpliedSurfaceOrSolidActionBody", {}, true, 15);
}

} // namespace internal
} // namespace hdxf
