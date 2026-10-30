#include "AssocTypes.hpp"

// The action parameters: ACDBASSOCPATHACTIONPARAM, ACDBASSOCVERTEXACTIONPARAM, ACDBASSOCOSNAPPOINTREFACTIONPARAM and
// ACDBASSOCEDGEACTIONPARAM, and the bases step 9.15's parameters build on (phase 9, step 9.10; plan
// 2026-10-27-fas-9-typer.md, appendix B.11, B.16, B.17, B.22). AutoCAD's own files (doc/facit/acad_*_dxf_associative.dxf,
// _surfaces.dxf, the corpus' Surface_2004, example_2007 … 2018, 3Dlines):
//   - AcDbAssocActionParam: 90 version (0 until 2010, 1 from 2013, which has one more 90), 1 name
//   - AcDbAssocCompoundActionParam: 90 version, 90, 90 count + 360 children. ACDBASSOCOSNAPPOINTREFACTIONPARAM has
//     three more groups after the children (90, 90, 330): only that class does
//   - AcDbAssocSingleDependencyActionParam: 90 version, 330 dependency
//   - the own subclass: AcDbAssocPathActionParam 90; AcDbAssocVertexActionParam 90 and a point;
//     ACDBASSOCOSNAPPOINTREFACTIONPARAM (AutoCAD's upper case) 90, 90 and a 40; AcDbAssocEdgeActionParam 90, 330 and
//     290 1 and the curve (assoc_geometry.cpp)

namespace hdxf {
namespace internal {

bool read_assoc_action_param_base(const RecordView& view, TypedReadContext& ctx, AssocActionParamBase& out) {
    GroupCursor c(view, "AcDbAssocActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocActionParam subclass");
    out = AssocActionParamBase();
    c.require_integer(90, out.version);
    if (!c.ok()) return ctx.fail(c.error());
    if (out.version != 0 && out.version != 1) {
        return ctx.fail(assoc_describe(view) + ": AcDbAssocActionParam version " + std::to_string(out.version) +
                        " is not established (AutoCAD's files have 0 and 1)");
    }
    if (out.version >= 1) c.require_integer(90, out.extra);
    c.require_string(1, out.name);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_assoc_action_param_base(RecordWriter& w, const AssocActionParamBase& v) {
    if (v.version != 0 && v.version != 1) {
        w.fail("AcDbAssocActionParam version " + std::to_string(v.version) + " is not established (AutoCAD's files have 0 and 1)");
        return;
    }
    w.subclass("AcDbAssocActionParam");
    w.integer(90, v.version);
    if (v.version >= 1) w.integer(90, v.extra);
    w.text(1, v.name);
}

bool read_assoc_compound_param_base(const RecordView& view, TypedReadContext& ctx, GroupCursor& c, AssocCompoundActionParamBase& out) {
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocCompoundActionParam subclass");
    out = AssocCompoundActionParamBase();
    c.require_integer(90, out.version);
    c.require_integer(90, out.flags);
    size_t n = 0;
    if (c.ok() && !c.count(90, 1, n)) {
        if (c.ok()) c.fail("the number of children (90) is missing");
    }
    out.children.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        std::string h;
        c.require_handle(360, h);
        out.children.push_back(std::move(h));
    }
    if (!c.ok()) return ctx.fail(c.error());
    return true;
}

void write_assoc_compound_param_base(RecordWriter& w, const AssocCompoundActionParamBase& v) {
    w.subclass("AcDbAssocCompoundActionParam");
    w.integer(90, v.version);
    w.integer(90, v.flags);
    std::vector<std::string> live = assoc_live(w, v.children);
    w.integer(90, static_cast<int64_t>(live.size()));
    for (const std::string& h : live) w.handle(360, h);
}

bool read_assoc_single_dependency_param_base(const RecordView& view, TypedReadContext& ctx, AssocSingleDependencyActionParamBase& out) {
    GroupCursor c(view, "AcDbAssocSingleDependencyActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocSingleDependencyActionParam subclass");
    out = AssocSingleDependencyActionParamBase();
    c.require_integer(90, out.version);
    c.require_handle(330, out.dependency);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_assoc_single_dependency_param_base(RecordWriter& w, const AssocSingleDependencyActionParamBase& v) {
    w.subclass("AcDbAssocSingleDependencyActionParam");
    w.integer(90, v.version);
    w.handle(330, assoc_pointer(w, v.dependency));
}

namespace {

void add_reference(std::vector<TypedReference>& out, const std::string& h, RefKind kind) {
    if (!h.empty() && h != "0") out.push_back({h, kind, false});
}

std::vector<RefDescriptor> compound_refs(bool tail) {
    std::vector<RefDescriptor> refs = {assoc_member_ref("AcDbAssocCompoundActionParam", 360, RefKind::HardOwner)};
    if (tail) refs.push_back(assoc_ref("AcDbAssocCompoundActionParam", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace));
    return refs;
}

RefDescriptor single_ref() {
    return assoc_ref("AcDbAssocSingleDependencyActionParam", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace);
}

// ---------------------------------------------------------------------------
// ACDBASSOCPATHACTIONPARAM
// ---------------------------------------------------------------------------

bool read_path(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocPathActionParamData d;
    if (!read_assoc_action_param_base(view, ctx, d.param)) return false;
    GroupCursor compound(view, "AcDbAssocCompoundActionParam");
    if (!read_assoc_compound_param_base(view, ctx, compound, d.compound)) return false;
    assoc_note_rest(compound, ctx);
    GroupCursor c(view, "AcDbAssocPathActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocPathActionParam subclass");
    c.require_integer(90, d.version);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocPathActionParamData>(std::move(d));
    return true;
}

void write_path(RecordWriter& w, const Object& o) {
    const AssocPathActionParamData& d = o.as_assoc_path_action_param();
    w.object_head("ACDBASSOCPATHACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_compound_param_base(w, d.compound);
    w.subclass("AcDbAssocPathActionParam");
    w.integer(90, d.version);
    w.xdata(o.xdata);
}

void path_references(const Object& o, std::vector<TypedReference>& out) {
    for (const std::string& h : o.as_assoc_path_action_param().compound.children) add_reference(out, h, RefKind::HardOwner);
}

// ---------------------------------------------------------------------------
// ACDBASSOCVERTEXACTIONPARAM
// ---------------------------------------------------------------------------

bool read_vertex(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocVertexActionParamData d;
    if (!read_assoc_action_param_base(view, ctx, d.param)) return false;
    if (!read_assoc_single_dependency_param_base(view, ctx, d.single)) return false;
    GroupCursor c(view, "AcDbAssocVertexActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocVertexActionParam subclass");
    c.require_integer(90, d.version);
    c.require_point3(10, d.point);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocVertexActionParamData>(std::move(d));
    return true;
}

void write_vertex(RecordWriter& w, const Object& o) {
    const AssocVertexActionParamData& d = o.as_assoc_vertex_action_param();
    w.object_head("ACDBASSOCVERTEXACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_single_dependency_param_base(w, d.single);
    w.subclass("AcDbAssocVertexActionParam");
    w.integer(90, d.version);
    w.point3(10, d.point);
    w.xdata(o.xdata);
}

void vertex_references(const Object& o, std::vector<TypedReference>& out) {
    add_reference(out, o.as_assoc_vertex_action_param().single.dependency, RefKind::SoftPointer);
}

// ---------------------------------------------------------------------------
// ACDBASSOCOSNAPPOINTREFACTIONPARAM: the own subclass has AutoCAD's upper-case name
// ---------------------------------------------------------------------------

constexpr const char* kOsnapSubclass = "ACDBASSOCOSNAPPOINTREFACTIONPARAM";

bool read_osnap(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocOsnapPointRefActionParamData d;
    if (!read_assoc_action_param_base(view, ctx, d.param)) return false;
    GroupCursor compound(view, "AcDbAssocCompoundActionParam");
    if (!read_assoc_compound_param_base(view, ctx, compound, d.compound)) return false;
    compound.require_integer(90, d.compound_extra_1);
    compound.require_integer(90, d.compound_extra_2);
    compound.require_handle(330, d.compound_pointer);
    if (!compound.ok()) return ctx.fail(compound.error());
    assoc_note_rest(compound, ctx);
    GroupCursor c(view, kOsnapSubclass);
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no " + kOsnapSubclass + " subclass");
    c.require_integer(90, d.version);
    c.require_integer(90, d.osnap_type);
    c.require_real(40, d.parameter);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocOsnapPointRefActionParamData>(std::move(d));
    return true;
}

void write_osnap(RecordWriter& w, const Object& o) {
    const AssocOsnapPointRefActionParamData& d = o.as_assoc_osnap_point_ref_action_param();
    w.object_head("ACDBASSOCOSNAPPOINTREFACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_compound_param_base(w, d.compound);
    w.integer(90, d.compound_extra_1);
    w.integer(90, d.compound_extra_2);
    w.handle(330, assoc_pointer(w, d.compound_pointer));
    w.subclass(kOsnapSubclass);
    w.integer(90, d.version);
    w.integer(90, d.osnap_type);
    w.real(40, d.parameter);
    w.xdata(o.xdata);
}

void osnap_references(const Object& o, std::vector<TypedReference>& out) {
    const AssocOsnapPointRefActionParamData& d = o.as_assoc_osnap_point_ref_action_param();
    for (const std::string& h : d.compound.children) add_reference(out, h, RefKind::HardOwner);
    add_reference(out, d.compound_pointer, RefKind::SoftPointer);
}

// ---------------------------------------------------------------------------
// ACDBASSOCEDGEACTIONPARAM
// ---------------------------------------------------------------------------

bool read_edge(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocEdgeActionParamData d;
    if (!read_assoc_action_param_base(view, ctx, d.param)) return false;
    if (!read_assoc_single_dependency_param_base(view, ctx, d.single)) return false;
    GroupCursor c(view, "AcDbAssocEdgeActionParam");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocEdgeActionParam subclass");
    c.require_integer(90, d.version);
    c.require_handle(330, d.geometry_pointer);
    bool has_geometry = false;
    c.require_boolean(290, has_geometry);
    if (c.ok() && !has_geometry) {
        return ctx.fail(assoc_describe(view) + ": an edge parameter without its curve (290 0) is not established");
    }
    if (c.ok()) read_assoc_curve(c, d.param.version >= 1, d.curve);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocEdgeActionParamData>(std::move(d));
    return true;
}

void write_edge(RecordWriter& w, const Object& o) {
    const AssocEdgeActionParamData& d = o.as_assoc_edge_action_param();
    w.object_head("ACDBASSOCEDGEACTIONPARAM", o);
    write_assoc_action_param_base(w, d.param);
    write_assoc_single_dependency_param_base(w, d.single);
    w.subclass("AcDbAssocEdgeActionParam");
    w.integer(90, d.version);
    w.handle(330, assoc_pointer(w, d.geometry_pointer));
    w.boolean(290, true);
    write_assoc_curve(w, d.curve);
    w.xdata(o.xdata);
}

void edge_references(const Object& o, std::vector<TypedReference>& out) {
    const AssocEdgeActionParamData& d = o.as_assoc_edge_action_param();
    add_reference(out, d.single.dependency, RefKind::SoftPointer);
    add_reference(out, d.geometry_pointer, RefKind::SoftPointer);
}

} // namespace

void register_assoc_param_codecs(TypedRegistry& registry) {
    const auto all = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                      DxfVersion::R2018};
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCPATHACTIONPARAM";
        c.type = ObjectType::AssocPathActionParam;
        c.read = read_path;
        c.write = write_path;
        c.references = path_references;
        c.cls = assoc_class("AcDbAssocPathActionParam");
        c.versions = assoc_matrix(all);
        c.refs = compound_refs(false);
        c.lists = {assoc_whole_subclass("AcDbAssocCompoundActionParam")};
        c.step = 10;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCVERTEXACTIONPARAM";
        c.type = ObjectType::AssocVertexActionParam;
        c.read = read_vertex;
        c.write = write_vertex;
        c.references = vertex_references;
        c.cls = assoc_class("AcDbAssocVertexActionParam");
        c.versions = assoc_matrix(all);
        c.refs = {single_ref()};
        c.step = 10;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCOSNAPPOINTREFACTIONPARAM";
        c.type = ObjectType::AssocOsnapPointRefActionParam;
        c.read = read_osnap;
        c.write = write_osnap;
        c.references = osnap_references;
        c.cls = assoc_class("AcDbAssocOsnapPointRefActionParam");
        // AutoCAD's own files with it are the Autodesk example drawing saved in 2007 … 2018 (ASCII only); 2000 and 2004 are ej testat
        c.versions = assoc_matrix({}, {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018});
        c.refs = compound_refs(true);
        c.lists = {assoc_whole_subclass("AcDbAssocCompoundActionParam")};
        c.step = 10;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCEDGEACTIONPARAM";
        c.type = ObjectType::AssocEdgeActionParam;
        c.read = read_edge;
        c.write = write_edge;
        c.references = edge_references;
        c.cls = assoc_class("AcDbAssocEdgeActionParam");
        c.versions = assoc_matrix(all);
        c.refs = {single_ref(), assoc_ref("AcDbAssocEdgeActionParam", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace)};
        // The curve has as many groups as its kind and parts have: the subclass is one list
        c.lists = {assoc_whole_subclass("AcDbAssocEdgeActionParam")};
        c.step = 10;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
