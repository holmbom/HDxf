#include "AssocExtended.hpp"

// The other action parameters and the dimension dependency body (phase 9, step 9.15; plan 2026-10-27-fas-9-typer.md, F9.12):
// ACDBASSOCASMBODYACTIONPARAM, ACDBASSOCFACEACTIONPARAM, ACDBASSOCTRIMMINGPATHACTIONPARAM, ACDBASSOCCOMPOUNDACTIONPARAM,
// ACDBASSOCOBJECTACTIONPARAM and ASSOCDIMDEPENDENCYBODY. AutoCAD 2025's own files (doc/facit/fas9_assoc_extended.scr and
// fas9_assoc_core.scr in 2000 … 2018, ASCII and binary) have the first three and the last as the 9.10 parameters are built:
//   AcDbAssocActionParam (90 version, [90], 1 name), then either AcDbAssocSingleDependencyActionParam (90, 330) or
//   AcDbAssocCompoundActionParam (90, 90, 90 count + 360), then
//   asm body      AcDbAssocAsmBodyActionParam       90, 290
//   face          AcDbAssocAsmBodyActionParam, AcDbAssocFaceActionParam 90, 90
//   trimming body the face's subclasses, AcDbAssocTrimmingBodyActionParam 90, 280 (ACDBASSOCTRIMMINGBODYACTIONPARAM: the surface
//                 SURFTRIM cuts with; doc/facit/fas9_assoc_history_surf.scr, 2000 … 2018, ASCII and binary; 280 is 0 for a
//                 plane cut by a vertical surface, 1 for two walls that trim each other)
//   trimming path AcDbAssocPathActionParam 90, AcDbAssocTrimmingPathActionParam 90, 10 (a direction), 90
// ASSOCDIMDEPENDENCYBODY (the plan's ACDBASSOCDIMDEPENDENCYBODY; AutoCAD writes it without ACDB) is the body of the dependency
// on the dimension of a dimensional constraint: AcDbAssocDependencyBody 90, AcDbImpAssocDimDependencyBodyBase 90 and 1 (the
// dimension's text "d1=80.0000"), AcDbAssocDimDependencyBody 90.
// ACDBASSOCCOMPOUNDACTIONPARAM and ACDBASSOCOBJECTACTIONPARAM: no AutoCAD 2025 command makes one (fas9_assoc_extended.scr
// says what was tried); they are the bases (and, for the object parameter, a version of its own) and stay "ej testat".

namespace hdxf {
namespace internal {

namespace {

template <typename Data>
Data& data_of(Object& o) { return *o.data.get<Boxed<Data>>(); }
template <typename Data>
const Data& data_of(const Object& o) { return *o.data.get<Boxed<Data>>(); }

RefDescriptor single_ref() {
    return assoc_ref("AcDbAssocSingleDependencyActionParam", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace);
}

// The two bases of a parameter with one dependency
bool read_single_bases(const RecordView& view, TypedReadContext& ctx, AssocActionParamBase& param, AssocSingleDependencyActionParamBase& single) {
    return read_assoc_action_param_base(view, ctx, param) && read_assoc_single_dependency_param_base(view, ctx, single);
}

// ---- ASM body ----
bool read_asm_body(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocAsmBodyActionParamData d;
    if (!read_single_bases(view, ctx, d.param, d.single)) return false;
    GroupCursor c(view, "AcDbAssocAsmBodyActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocAsmBodyActionParam subclass");
    c.require_integer(90, d.version);
    c.require_boolean(290, d.flag);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocAsmBodyActionParamData>(std::move(d));
    return true;
}
void write_asm_body(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocAsmBodyActionParamData>(o);
    w.object_head("ACDBASSOCASMBODYACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_single_dependency_param_base(w, d.single);
    w.subclass("AcDbAssocAsmBodyActionParam");
    w.integer(90, d.version);
    w.boolean(290, d.flag);
    w.xdata(o.xdata);
}
void asm_body_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_add_reference(out, data_of<AssocAsmBodyActionParamData>(o).single.dependency, RefKind::SoftPointer);
}

// ---- face ----
// The face parameter's subclasses (also the first part of the trimming body parameter)
bool read_face_parts(const RecordView& view, TypedReadContext& ctx, AssocFaceActionParamData& d) {
    if (!read_single_bases(view, ctx, d.param, d.single)) return false;
    GroupCursor a(view, "AcDbAssocAsmBodyActionParam");
    if (!a.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocAsmBodyActionParam subclass");
    a.require_integer(90, d.asm_version);
    a.require_boolean(290, d.asm_flag);
    if (!a.ok()) return ctx.fail(a.error());
    assoc_note_rest(a, ctx);
    GroupCursor c(view, "AcDbAssocFaceActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocFaceActionParam subclass");
    c.require_integer(90, d.version);
    c.require_integer(90, d.value_90);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}
void write_face_parts(RecordWriter& w, const AssocFaceActionParamData& d) {
    write_assoc_action_param_base(w, d.param);
    write_assoc_single_dependency_param_base(w, d.single);
    w.subclass("AcDbAssocAsmBodyActionParam");
    w.integer(90, d.asm_version);
    w.boolean(290, d.asm_flag);
    w.subclass("AcDbAssocFaceActionParam");
    w.integer(90, d.version);
    w.integer(90, d.value_90);
}
bool read_face(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocFaceActionParamData d;
    if (!read_face_parts(view, ctx, d)) return false;
    o.data = Boxed<AssocFaceActionParamData>(std::move(d));
    return true;
}
void write_face(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocFaceActionParamData>(o);
    w.object_head("ACDBASSOCFACEACTIONPARAM", o);
    write_face_parts(w, d);
    w.xdata(o.xdata);
}
void face_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_add_reference(out, data_of<AssocFaceActionParamData>(o).single.dependency, RefKind::SoftPointer);
}

// ---- trimming body (the surface SURFTRIM cuts with) ----
bool read_trimming_body(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocTrimmingBodyActionParamData d;
    if (!read_face_parts(view, ctx, d.face)) return false;
    GroupCursor c(view, "AcDbAssocTrimmingBodyActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocTrimmingBodyActionParam subclass");
    c.require_integer(90, d.version);
    c.require_integer(280, d.value_280);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocTrimmingBodyActionParamData>(std::move(d));
    return true;
}
void write_trimming_body(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocTrimmingBodyActionParamData>(o);
    w.object_head("ACDBASSOCTRIMMINGBODYACTIONPARAM", o);
    write_face_parts(w, d.face);
    w.subclass("AcDbAssocTrimmingBodyActionParam");
    w.integer(90, d.version);
    w.integer(280, d.value_280);
    w.xdata(o.xdata);
}
void trimming_body_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_add_reference(out, data_of<AssocTrimmingBodyActionParamData>(o).face.single.dependency, RefKind::SoftPointer);
}

// ---- trimming path ----
bool read_trimming_path(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocTrimmingPathActionParamData d;
    if (!read_assoc_action_param_base(view, ctx, d.param)) return false;
    GroupCursor compound(view, "AcDbAssocCompoundActionParam");
    if (!read_assoc_compound_param_base(view, ctx, compound, d.compound)) return false;
    assoc_note_rest(compound, ctx);
    GroupCursor path(view, "AcDbAssocPathActionParam");
    if (!path.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocPathActionParam subclass");
    path.require_integer(90, d.path_version);
    if (!path.ok()) return ctx.fail(path.error());
    assoc_note_rest(path, ctx);
    GroupCursor c(view, "AcDbAssocTrimmingPathActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocTrimmingPathActionParam subclass");
    c.require_integer(90, d.version);
    c.require_point3(10, d.direction);
    c.require_integer(90, d.value_90);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocTrimmingPathActionParamData>(std::move(d));
    return true;
}
void write_trimming_path(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocTrimmingPathActionParamData>(o);
    w.object_head("ACDBASSOCTRIMMINGPATHACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_compound_param_base(w, d.compound);
    w.subclass("AcDbAssocPathActionParam");
    w.integer(90, d.path_version);
    w.subclass("AcDbAssocTrimmingPathActionParam");
    w.integer(90, d.version);
    w.point3(10, d.direction);
    w.integer(90, d.value_90);
    w.xdata(o.xdata);
}
void trimming_path_references(const Object& o, std::vector<TypedReference>& out) {
    for (const std::string& h : data_of<AssocTrimmingPathActionParamData>(o).compound.children) assoc_add_reference(out, h, RefKind::HardOwner);
}

// ---- compound ----
bool read_compound(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocCompoundActionParamData d;
    if (!read_assoc_action_param_base(view, ctx, d.param)) return false;
    GroupCursor compound(view, "AcDbAssocCompoundActionParam");
    if (!read_assoc_compound_param_base(view, ctx, compound, d.compound)) return false;
    assoc_note_rest(compound, ctx);
    o.data = Boxed<AssocCompoundActionParamData>(std::move(d));
    return true;
}
void write_compound(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocCompoundActionParamData>(o);
    w.object_head("ACDBASSOCCOMPOUNDACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_compound_param_base(w, d.compound);
    w.xdata(o.xdata);
}
void compound_references(const Object& o, std::vector<TypedReference>& out) {
    for (const std::string& h : data_of<AssocCompoundActionParamData>(o).compound.children) assoc_add_reference(out, h, RefKind::HardOwner);
}

// ---- object ----
bool read_object_param(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocObjectActionParamData d;
    if (!read_single_bases(view, ctx, d.param, d.single)) return false;
    GroupCursor c(view, "AcDbAssocObjectActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocObjectActionParam subclass");
    c.require_integer(90, d.version);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocObjectActionParamData>(std::move(d));
    return true;
}
void write_object_param(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocObjectActionParamData>(o);
    w.object_head("ACDBASSOCOBJECTACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_single_dependency_param_base(w, d.single);
    w.subclass("AcDbAssocObjectActionParam");
    w.integer(90, d.version);
    w.xdata(o.xdata);
}
void object_param_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_add_reference(out, data_of<AssocObjectActionParamData>(o).single.dependency, RefKind::SoftPointer);
}

// ---- ASSOCDIMDEPENDENCYBODY ----
bool read_dim_dependency_body(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocDimDependencyBodyData d;
    GroupCursor a(view, "AcDbAssocDependencyBody");
    if (!a.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocDependencyBody subclass");
    a.require_integer(90, d.body_version);
    if (!a.ok()) return ctx.fail(a.error());
    assoc_note_rest(a, ctx);
    GroupCursor b(view, "AcDbImpAssocDimDependencyBodyBase");
    if (!b.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbImpAssocDimDependencyBodyBase subclass");
    b.require_integer(90, d.base_version);
    b.require_string(1, d.dimension_text);
    if (!b.ok()) return ctx.fail(b.error());
    assoc_note_rest(b, ctx);
    GroupCursor c(view, "AcDbAssocDimDependencyBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocDimDependencyBody subclass");
    c.require_integer(90, d.version);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocDimDependencyBodyData>(std::move(d));
    return true;
}
void write_dim_dependency_body(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocDimDependencyBodyData>(o);
    w.object_head("ASSOCDIMDEPENDENCYBODY", o);
    w.subclass("AcDbAssocDependencyBody");
    w.integer(90, d.body_version);
    w.subclass("AcDbImpAssocDimDependencyBodyBase");
    w.integer(90, d.base_version);
    w.text(1, d.dimension_text);
    w.subclass("AcDbAssocDimDependencyBody");
    w.integer(90, d.version);
    w.xdata(o.xdata);
}

void add_param(TypedRegistry& registry, const char* name, ObjectType type, ObjectReadFn read, ObjectWriteFn write,
               ObjectReferencesFn references, const char* cpp_name, std::vector<RefDescriptor> refs, std::vector<ListSpec> lists,
               bool verified) {
    ObjectCodec c;
    c.dxf_name = name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.references = references;
    c.cls = assoc_class(cpp_name);
    c.versions = verified ? assoc_matrix(assoc_all_versions()) : assoc_matrix({});
    c.refs = std::move(refs);
    c.lists = std::move(lists);
    c.step = 15;
    registry.add(std::move(c));
}

} // namespace

void register_assoc_extended_param_codecs(TypedRegistry& registry) {
    add_param(registry, "ACDBASSOCASMBODYACTIONPARAM", ObjectType::AssocAsmBodyActionParam, read_asm_body, write_asm_body,
              asm_body_references, "AcDbAssocAsmBodyActionParam", {single_ref()}, {}, true);
    add_param(registry, "ACDBASSOCFACEACTIONPARAM", ObjectType::AssocFaceActionParam, read_face, write_face, face_references,
              "AcDbAssocFaceActionParam", {single_ref()}, {}, true);
    add_param(registry, "ACDBASSOCTRIMMINGBODYACTIONPARAM", ObjectType::AssocTrimmingBodyActionParam, read_trimming_body,
              write_trimming_body, trimming_body_references, "AcDbAssocTrimmingBodyActionParam", {single_ref()}, {}, true);
    add_param(registry, "ACDBASSOCTRIMMINGPATHACTIONPARAM", ObjectType::AssocTrimmingPathActionParam, read_trimming_path,
              write_trimming_path, trimming_path_references, "AcDbAssocTrimmingPathActionParam",
              {assoc_member_ref("AcDbAssocCompoundActionParam", 360, RefKind::HardOwner)},
              {assoc_whole_subclass("AcDbAssocCompoundActionParam")}, true);
    add_param(registry, "ACDBASSOCCOMPOUNDACTIONPARAM", ObjectType::AssocCompoundActionParam, read_compound, write_compound,
              compound_references, "AcDbAssocCompoundActionParam",
              {assoc_member_ref("AcDbAssocCompoundActionParam", 360, RefKind::HardOwner)},
              {assoc_whole_subclass("AcDbAssocCompoundActionParam")}, false);
    add_param(registry, "ACDBASSOCOBJECTACTIONPARAM", ObjectType::AssocObjectActionParam, read_object_param, write_object_param,
              object_param_references, "AcDbAssocObjectActionParam", {single_ref()}, {}, false);
    add_param(registry, "ASSOCDIMDEPENDENCYBODY", ObjectType::AssocDimDependencyBody, read_dim_dependency_body,
              write_dim_dependency_body, nullptr, "AcDbAssocDimDependencyBody", {}, {}, true);
}

} // namespace internal
} // namespace hdxf
