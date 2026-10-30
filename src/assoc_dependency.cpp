#include "AssocTypes.hpp"

// ACDBASSOCDEPENDENCY and ACDBASSOCGEOMDEPENDENCY (phase 9, step 9.10; plan 2026-10-27-fas-9-typer.md, appendix B.10 and
// B.13). AutoCAD's files (doc/facit/acad_*_dxf_associative.dxf, _surfaces.dxf, the corpus' Surface_2004 and example_*):
//   - AcDbAssocDependency: 90 version (2), 90 status, four 290 (read, write, object state dependent, delegating to the
//     owning action), 90 order (-10000 for the geometry a constraint group depends on in both directions,
//     -2147483648 for the result an action writes, 0 for what it reads), 330 the object depended on, 290 whether it is
//     a compound object, 330 previous and 330 next dependency of the chain, 360 the dependency body, 90 the
//     dependency's index in its action. The object depended on has the dependency as a reactor
//   - AcDbAssocGeomDependency: 90 version, 290 (1), 1 the class name of the persistent subentity id and its groups,
//     290

namespace hdxf {
namespace internal {

bool read_assoc_dependency_base(const RecordView& view, TypedReadContext& ctx, AssocDependencyBase& out) {
    GroupCursor c(view, "AcDbAssocDependency");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocDependency subclass");
    out = AssocDependencyBase();
    c.require_integer(90, out.version);
    c.require_integer(90, out.status);
    c.require_boolean(290, out.is_read);
    c.require_boolean(290, out.is_write);
    c.require_boolean(290, out.is_object_state_dependent);
    c.require_boolean(290, out.is_delegating_to_owning_action);
    c.require_integer(90, out.order);
    c.require_handle(330, out.dependent_on_object);
    c.require_boolean(290, out.depends_on_compound_object);
    c.require_handle(330, out.previous);
    c.require_handle(330, out.next);
    c.require_handle(360, out.dependency_body);
    c.require_integer(90, out.index);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_assoc_dependency_base(RecordWriter& w, const AssocDependencyBase& v) {
    w.subclass("AcDbAssocDependency");
    w.integer(90, v.version);
    w.integer(90, v.status);
    w.boolean(290, v.is_read);
    w.boolean(290, v.is_write);
    w.boolean(290, v.is_object_state_dependent);
    w.boolean(290, v.is_delegating_to_owning_action);
    w.integer(90, v.order);
    w.handle(330, assoc_pointer(w, v.dependent_on_object));
    w.boolean(290, v.depends_on_compound_object);
    w.handle(330, assoc_pointer(w, v.previous));
    w.handle(330, assoc_pointer(w, v.next));
    w.handle(360, assoc_pointer(w, v.dependency_body));
    w.integer(90, v.index);
}

void assoc_dependency_references(const AssocDependencyBase& v, std::vector<TypedReference>& out) {
    auto add = [&](const std::string& h, RefKind kind, bool reactor) {
        if (!h.empty() && h != "0") out.push_back({h, kind, reactor});
    };
    // What the dependency depends on has it as a reactor (AutoCAD's own files: the line, the surface …)
    add(v.dependent_on_object, RefKind::SoftPointer, true);
    add(v.previous, RefKind::SoftPointer, false);
    add(v.next, RefKind::SoftPointer, false);
    add(v.dependency_body, RefKind::HardOwner, false);
}

namespace {

std::vector<RefDescriptor> dependency_refs() {
    // 330: the object depended on, the previous and the next dependency (a pointer each, 0 when the target is gone);
    // 360: the body the dependency owns. The first 330 makes its target keep the dependency as a reactor
    RefDescriptor on_object = assoc_ref("AcDbAssocDependency", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace, 0);
    on_object.back_reactor = true;
    return {on_object, assoc_ref("AcDbAssocDependency", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace),
            assoc_ref("AcDbAssocDependency", 360, RefKind::HardOwner, RefRemoval::ZeroInPlace)};
}

bool read_dependency(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocDependencyData d;
    if (!read_assoc_dependency_base(view, ctx, d.dependency)) return false;
    o.data = Boxed<AssocDependencyData>(std::move(d));
    return true;
}

void write_dependency(RecordWriter& w, const Object& o) {
    w.object_head("ACDBASSOCDEPENDENCY", o);
    write_assoc_dependency_base(w, o.as_assoc_dependency().dependency);
    w.xdata(o.xdata);
}

void dependency_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_dependency_references(o.as_assoc_dependency().dependency, out);
}

bool read_geom_dependency(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocGeomDependencyData d;
    if (!read_assoc_dependency_base(view, ctx, d.dependency)) return false;
    GroupCursor c(view, "AcDbAssocGeomDependency");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocGeomDependency subclass");
    c.require_integer(90, d.version);
    c.require_boolean(290, d.has_subentity_id);
    if (c.ok() && !d.has_subentity_id) {
        return ctx.fail(assoc_describe(view) + ": a geometry dependency without a persistent subentity id (290 0) is not established");
    }
    if (c.ok()) read_persistent_subentity_id(c, d.subentity_id);
    c.require_boolean(290, d.trailing_flag);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocGeomDependencyData>(std::move(d));
    return true;
}

void write_geom_dependency(RecordWriter& w, const Object& o) {
    const AssocGeomDependencyData& d = o.as_assoc_geom_dependency();
    w.object_head("ACDBASSOCGEOMDEPENDENCY", o);
    write_assoc_dependency_base(w, d.dependency);
    w.subclass("AcDbAssocGeomDependency");
    w.integer(90, d.version);
    w.boolean(290, d.has_subentity_id);
    write_persistent_subentity_id(w, d.subentity_id);
    w.boolean(290, d.trailing_flag);
    w.xdata(o.xdata);
}

void geom_dependency_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_dependency_references(o.as_assoc_geom_dependency().dependency, out);
}

} // namespace

void register_assoc_dependency_codecs(TypedRegistry& registry) {
    // 2000 … 2018 in AutoCAD's own files, both formats (doc/facit/acad_*_dxf_associative.dxf)
    const auto verified = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                           DxfVersion::R2018};
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCDEPENDENCY";
        c.type = ObjectType::AssocDependency;
        c.read = read_dependency;
        c.write = write_dependency;
        c.references = dependency_references;
        c.cls = assoc_class("AcDbAssocDependency");
        c.versions = assoc_matrix(verified);
        c.refs = dependency_refs();
        c.step = 10;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCGEOMDEPENDENCY";
        c.type = ObjectType::AssocGeomDependency;
        c.read = read_geom_dependency;
        c.write = write_geom_dependency;
        c.references = geom_dependency_references;
        c.cls = assoc_class("AcDbAssocGeomDependency");
        c.versions = assoc_matrix(verified);
        c.refs = dependency_refs();
        // The persistent subentity id has as many groups as its class has: the subclass is one list
        c.lists = {assoc_whole_subclass("AcDbAssocGeomDependency")};
        c.step = 10;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
