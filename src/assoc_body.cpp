#include "AssocTypes.hpp"
#include <type_traits>

// The action bodies of the surface actions and the aligned dimension (phase 9, step 9.10; plan
// 2026-10-27-fas-9-typer.md, appendix B.9, B.12, B.14, B.19, B.20, B.21) and the bases step 9.15's bodies build on.
// AutoCAD's own files (doc/facit/acad_*_dxf_surfaces.dxf in 2000 … 2018, the corpus' Surface_2004, 3Dlines, example_2007 …
// 2018):
//   - AcDbAssocActionBody: 90 version. Version 1 (every file up to 2010) is followed by AcDbAssocParamBasedActionBody:
//     90 version, 90, 90 count + 360 (the parameters the body owns), 90, and the value parameters (90 count + each:
//     ExtrusionHeight, RevolveAngle …). Version 2 (2013 and later) has neither: the parameters are in the action (B110)
//   - AcDbAssocSurfaceActionBody: 90 version, 330 result dependency (the dependency that writes the surface), 290, 90
//     (the same number as the action's ACAD_ASSOCPERSSUBENTMANAGER XDATA), 290, 70
//   - AcDbAssocPathBasedSurfaceActionBody: 90 version
//   - the body's own: AcDbAssocExtrudedSurfaceActionBody, …LoftedSurface…, …SweptSurface…, …PlaneSurface… 90; …RevolvedSurface…
//     90 and 290
//   - the aligned dimension body: 90 and 330 after the action body's 90 (version 2) or the parameters (version 1), then
//     ACDBASSOCALIGNEDDIMACTIONBODY (AutoCAD's upper case) 90, 330, 330

namespace hdxf {
namespace internal {

namespace {

bool read_param_based(const RecordView& view, TypedReadContext& ctx, AssocParamBasedActionBody& out,
                      const std::function<bool(GroupCursor&)>& tail) {
    GroupCursor c(view, "AcDbAssocParamBasedActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocParamBasedActionBody subclass");
    out = AssocParamBasedActionBody();
    c.require_integer(90, out.version);
    c.require_integer(90, out.reserved_before_params);
    size_t n = 0;
    if (c.ok() && !c.count(90, 1, n)) {
        if (c.ok()) c.fail("the number of parameters (90) is missing");
    }
    out.params.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        std::string h;
        c.require_handle(360, h);
        out.params.push_back(std::move(h));
    }
    c.require_integer(90, out.reserved_after_params);
    if (c.ok()) read_assoc_value_parameters(c, out.value_params);
    if (c.ok() && tail) tail(c);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_param_based(RecordWriter& w, const AssocParamBasedActionBody& v, const std::function<void()>& tail) {
    w.subclass("AcDbAssocParamBasedActionBody");
    w.integer(90, v.version);
    w.integer(90, v.reserved_before_params);
    std::vector<std::string> live = assoc_live(w, v.params);
    w.integer(90, static_cast<int64_t>(live.size()));
    for (const std::string& h : live) w.handle(360, h);
    w.integer(90, v.reserved_after_params);
    write_assoc_value_parameters(w, v.value_params);
    if (tail) tail();
}

void add_reference(std::vector<TypedReference>& out, const std::string& h, RefKind kind) {
    if (!h.empty() && h != "0") out.push_back({h, kind, false});
}

} // namespace

bool read_assoc_action_body_head(const RecordView& view, TypedReadContext& ctx, AssocActionBodyHead& out,
                                 const std::function<bool(GroupCursor&)>& tail) {
    GroupCursor c(view, "AcDbAssocActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocActionBody subclass");
    out = AssocActionBodyHead();
    c.require_integer(90, out.version);
    if (!c.ok()) return ctx.fail(c.error());
    if (out.version != 1 && out.version != 2) {
        return ctx.fail(assoc_describe(view) + ": AcDbAssocActionBody version " + std::to_string(out.version) +
                        " is not established (AutoCAD's files have 1 and 2)");
    }
    if (out.version == 1) {
        AssocParamBasedActionBody p;
        if (!read_param_based(view, ctx, p, tail)) return false;
        out.param_based = std::move(p);
    } else if (tail && !tail(c)) {
        return ctx.fail(c.error());
    }
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_assoc_action_body_head(RecordWriter& w, const AssocActionBodyHead& v, const std::function<void()>& tail) {
    if (v.version != 1 && v.version != 2) {
        w.fail("AcDbAssocActionBody version " + std::to_string(v.version) + " is not established (AutoCAD's files have 1 and 2)");
        return;
    }
    w.subclass("AcDbAssocActionBody");
    w.integer(90, v.version);
    if (v.version == 1) {
        write_param_based(w, v.param_based ? *v.param_based : AssocParamBasedActionBody(), tail);
    } else if (tail) {
        tail();
    }
}

bool read_assoc_surface_body(const RecordView& view, TypedReadContext& ctx, AssocSurfaceActionBody& out) {
    GroupCursor c(view, "AcDbAssocSurfaceActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocSurfaceActionBody subclass");
    out = AssocSurfaceActionBody();
    c.require_integer(90, out.version);
    c.require_handle(330, out.result_dependency);
    c.require_boolean(290, out.flag_after_result);
    c.require_integer(90, out.step_id);
    c.require_boolean(290, out.flag_after_step);
    c.require_integer(70, out.value_after_flag);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_assoc_surface_body(RecordWriter& w, const AssocSurfaceActionBody& v) {
    w.subclass("AcDbAssocSurfaceActionBody");
    w.integer(90, v.version);
    w.handle(330, assoc_pointer(w, v.result_dependency));
    w.boolean(290, v.flag_after_result);
    w.integer(90, v.step_id);
    w.boolean(290, v.flag_after_step);
    w.integer(70, v.value_after_flag);
}

bool read_assoc_path_based_body(const RecordView& view, TypedReadContext& ctx, AssocPathBasedSurfaceActionBody& out) {
    GroupCursor c(view, "AcDbAssocPathBasedSurfaceActionBody");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocPathBasedSurfaceActionBody subclass");
    out = AssocPathBasedSurfaceActionBody();
    c.require_integer(90, out.version);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_assoc_path_based_body(RecordWriter& w, const AssocPathBasedSurfaceActionBody& v) {
    w.subclass("AcDbAssocPathBasedSurfaceActionBody");
    w.integer(90, v.version);
}

void assoc_body_references(const AssocActionBodyHead& head, std::vector<TypedReference>& out) {
    if (!head.param_based) return;
    for (const std::string& h : head.param_based->params) add_reference(out, h, RefKind::HardOwner);
    for (const AssocValueParameter& p : head.param_based->value_params) {
        for (const AssocParameterValue& value : p.values) add_reference(out, value.pointer, RefKind::SoftPointer);
        add_reference(out, p.dependency, RefKind::SoftPointer);
    }
}

namespace {

// ---------------------------------------------------------------------------
// The five surface bodies: the same bases, an own subclass of one 90 (the revolved one has a 290 too)
// ---------------------------------------------------------------------------

template <typename Data>
Data& body_of(Object& o) { return *o.data.get<Boxed<Data>>(); }
template <typename Data>
const Data& body_of(const Object& o) { return *o.data.get<Boxed<Data>>(); }

template <typename Data>
bool read_surface_body(const RecordView& view, TypedReadContext& ctx, Object& o, const char* own_subclass) {
    Data d;
    if (!read_assoc_action_body_head(view, ctx, d.head)) return false;
    if (!read_assoc_surface_body(view, ctx, d.surface)) return false;
    if (!read_assoc_path_based_body(view, ctx, d.path_based)) return false;
    GroupCursor c(view, own_subclass);
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no " + own_subclass + " subclass");
    c.require_integer(90, d.version);
    if constexpr (std::is_same_v<Data, AssocRevolvedSurfaceActionBodyData>) c.require_boolean(290, d.flag);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<Data>(std::move(d));
    return true;
}

template <typename Data>
void write_surface_body(RecordWriter& w, const Object& o, const char* dxf_name, const char* own_subclass) {
    const Data& d = body_of<Data>(o);
    w.object_head(dxf_name, o);
    write_assoc_action_body_head(w, d.head);
    write_assoc_surface_body(w, d.surface);
    write_assoc_path_based_body(w, d.path_based);
    w.subclass(own_subclass);
    w.integer(90, d.version);
    if constexpr (std::is_same_v<Data, AssocRevolvedSurfaceActionBodyData>) w.boolean(290, d.flag);
    w.xdata(o.xdata);
}

template <typename Data>
void surface_body_references(const Object& o, std::vector<TypedReference>& out) {
    const Data& d = body_of<Data>(o);
    assoc_body_references(d.head, out);
    add_reference(out, d.surface.result_dependency, RefKind::SoftPointer);
}

std::vector<RefDescriptor> body_refs() {
    return {assoc_member_ref("AcDbAssocParamBasedActionBody", 360, RefKind::HardOwner),
            assoc_ref("AcDbAssocParamBasedActionBody", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace),
            assoc_ref("AcDbAssocSurfaceActionBody", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace)};
}

// ---------------------------------------------------------------------------
// ACDBASSOCALIGNEDDIMACTIONBODY
// ---------------------------------------------------------------------------

constexpr const char* kAlignedSubclass = "ACDBASSOCALIGNEDDIMACTIONBODY";

bool read_aligned(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocAlignedDimActionBodyData d;
    auto tail = [&](GroupCursor& c) {
        c.require_integer(90, d.dimension_flag);
        c.require_handle(330, d.dimension_dependency);
        return c.ok();
    };
    if (!read_assoc_action_body_head(view, ctx, d.head, tail)) return false;
    GroupCursor c(view, kAlignedSubclass);
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no " + kAlignedSubclass + " subclass");
    c.require_integer(90, d.version);
    c.require_handle(330, d.first_pointer);
    c.require_handle(330, d.second_pointer);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocAlignedDimActionBodyData>(std::move(d));
    return true;
}

void write_aligned(RecordWriter& w, const Object& o) {
    const AssocAlignedDimActionBodyData& d = o.as_assoc_aligned_dim_action_body();
    w.object_head("ACDBASSOCALIGNEDDIMACTIONBODY", o);
    write_assoc_action_body_head(w, d.head, [&] {
        w.integer(90, d.dimension_flag);
        w.handle(330, assoc_pointer(w, d.dimension_dependency));
    });
    w.subclass(kAlignedSubclass);
    w.integer(90, d.version);
    w.handle(330, assoc_pointer(w, d.first_pointer));
    w.handle(330, assoc_pointer(w, d.second_pointer));
    w.xdata(o.xdata);
}

void aligned_references(const Object& o, std::vector<TypedReference>& out) {
    const AssocAlignedDimActionBodyData& d = o.as_assoc_aligned_dim_action_body();
    assoc_body_references(d.head, out);
    add_reference(out, d.dimension_dependency, RefKind::SoftPointer);
    add_reference(out, d.first_pointer, RefKind::SoftPointer);
    add_reference(out, d.second_pointer, RefKind::SoftPointer);
}

std::vector<RefDescriptor> aligned_refs() {
    std::vector<RefDescriptor> refs = body_refs();
    refs.push_back(assoc_ref("AcDbAssocActionBody", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace));
    refs.push_back(assoc_ref(kAlignedSubclass, 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace));
    return refs;
}

#define ASSOC_SURFACE_BODY(Data, dxf, subclass)                                                                 \
    bool read_##Data(const RecordView& view, TypedReadContext& ctx, Object& o) {                                \
        return read_surface_body<Data>(view, ctx, o, subclass);                                                 \
    }                                                                                                           \
    void write_##Data(RecordWriter& w, const Object& o) { write_surface_body<Data>(w, o, dxf, subclass); }

ASSOC_SURFACE_BODY(AssocExtrudedSurfaceActionBodyData, "ACDBASSOCEXTRUDEDSURFACEACTIONBODY", "AcDbAssocExtrudedSurfaceActionBody")
ASSOC_SURFACE_BODY(AssocLoftedSurfaceActionBodyData, "ACDBASSOCLOFTEDSURFACEACTIONBODY", "AcDbAssocLoftedSurfaceActionBody")
ASSOC_SURFACE_BODY(AssocRevolvedSurfaceActionBodyData, "ACDBASSOCREVOLVEDSURFACEACTIONBODY", "AcDbAssocRevolvedSurfaceActionBody")
ASSOC_SURFACE_BODY(AssocSweptSurfaceActionBodyData, "ACDBASSOCSWEPTSURFACEACTIONBODY", "AcDbAssocSweptSurfaceActionBody")
ASSOC_SURFACE_BODY(AssocPlaneSurfaceActionBodyData, "ACDBASSOCPLANESURFACEACTIONBODY", "AcDbAssocPlaneSurfaceActionBody")

#undef ASSOC_SURFACE_BODY

template <typename Data>
void add_surface_codec(TypedRegistry& registry, const char* name, ObjectType type, ObjectReadFn read, ObjectWriteFn write,
                       const char* cpp_name, VersionMatrix versions) {
    ObjectCodec c;
    c.dxf_name = name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.references = surface_body_references<Data>;
    c.cls = assoc_class(cpp_name);
    c.versions = std::move(versions);
    c.refs = body_refs();
    // The parameters of a version 1 body have as many groups as they have: the subclass is one list
    c.lists = {assoc_whole_subclass("AcDbAssocParamBasedActionBody")};
    c.step = 10;
    registry.add(std::move(c));
}

} // namespace

void register_assoc_body_codecs(TypedRegistry& registry) {
    const auto all = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                      DxfVersion::R2018};
    add_surface_codec<AssocExtrudedSurfaceActionBodyData>(registry, "ACDBASSOCEXTRUDEDSURFACEACTIONBODY",
        ObjectType::AssocExtrudedSurfaceActionBody, read_AssocExtrudedSurfaceActionBodyData, write_AssocExtrudedSurfaceActionBodyData,
        "AcDbAssocExtrudedSurfaceActionBody", assoc_matrix(all));
    add_surface_codec<AssocLoftedSurfaceActionBodyData>(registry, "ACDBASSOCLOFTEDSURFACEACTIONBODY",
        ObjectType::AssocLoftedSurfaceActionBody, read_AssocLoftedSurfaceActionBodyData, write_AssocLoftedSurfaceActionBodyData,
        "AcDbAssocLoftedSurfaceActionBody", assoc_matrix(all));
    add_surface_codec<AssocRevolvedSurfaceActionBodyData>(registry, "ACDBASSOCREVOLVEDSURFACEACTIONBODY",
        ObjectType::AssocRevolvedSurfaceActionBody, read_AssocRevolvedSurfaceActionBodyData, write_AssocRevolvedSurfaceActionBodyData,
        "AcDbAssocRevolvedSurfaceActionBody", assoc_matrix(all));
    add_surface_codec<AssocSweptSurfaceActionBodyData>(registry, "ACDBASSOCSWEPTSURFACEACTIONBODY",
        ObjectType::AssocSweptSurfaceActionBody, read_AssocSweptSurfaceActionBodyData, write_AssocSweptSurfaceActionBodyData,
        "AcDbAssocSweptSurfaceActionBody", assoc_matrix(all));
    // The plane surface's body: AutoCAD's own acad_*_dxf_assoc_core.dxf (fas9_assoc_core.scr) and LibreDWG's Surface_2004
    add_surface_codec<AssocPlaneSurfaceActionBodyData>(registry, "ACDBASSOCPLANESURFACEACTIONBODY",
        ObjectType::AssocPlaneSurfaceActionBody, read_AssocPlaneSurfaceActionBodyData, write_AssocPlaneSurfaceActionBodyData,
        "AcDbAssocPlaneSurfaceActionBody", assoc_matrix(all));
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCALIGNEDDIMACTIONBODY";
        c.type = ObjectType::AssocAlignedDimActionBody;
        c.read = read_aligned;
        c.write = write_aligned;
        c.references = aligned_references;
        c.cls = assoc_class("AcDbAssocAlignedDimActionBody");
        // AutoCAD's own files with it are the Autodesk example drawing saved in 2007 … 2018 (ASCII only)
        c.versions = assoc_matrix({}, {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018});
        c.refs = aligned_refs();
        c.lists = {assoc_whole_subclass("AcDbAssocParamBasedActionBody")};
        c.step = 10;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
