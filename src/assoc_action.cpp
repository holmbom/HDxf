#include "AssocTypes.hpp"
#include <initializer_list>

// ACDBASSOCACTION and ACDBASSOCNETWORK, the value parameters and the helpers every ACDBASSOC codec shares (phase 9, step
// 9.10; plan 2026-10-27-fas-9-typer.md, "ACDBASSOC* — action, dependency och parametrar", appendix B.8, B.15).
//
// What AutoCAD's own files show (doc/facit/acad_<2000 … 2018>_dxf_associative.dxf and _surfaces.dxf, made by AutoCAD
// 2025's core from doc/facit/17_associative.scr and 11_surfaces.scr / fas9_assoc_core.scr; the corpus' Surface_2004,
// example_2007 … 2018, 3Dlines):
//   - AcDbAssocAction: 90 version, 90 status, 330 owning network, 360 action body, then the action's number (90), the
//     highest dependency number (90), the dependencies (90 count + 360). Version 1 (every file up to 2010) ends
//     there; version 2 (2013 and later) goes on with 90, the parameters (90 count + 360), 90, and the value
//     parameters (90 count + each): the parameters AutoCAD kept in the action body before 2013 (B110)
//   - AcDbAssocNetwork: 90 version, 90 highest action number, the actions (90 count + 360, or 330 for an action the
//     network does not own: the inner network), and a second list of soft pointers (90 count + 330) that AutoCAD
//     fills in 2000 … 2013 and leaves empty in 2007 and 2018
//   - a value parameter: 90 version, 1 name, 90 unit type, 90 count, each value in the group of its type (40 real,
//     70 int16, 90 int32) with a 330 after it, then a 330 for the parameter's dependency

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

std::string assoc_describe(const RecordView& view) {
    return view.type_name() + " " + view.head().handle;
}

void assoc_note_rest(GroupCursor& c, TypedReadContext& ctx) {
    if (!c.ok() || c.at_end()) return;
    const std::string name = c.name();
    size_t n = c.take_rest().size();
    ctx.warn(std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + name + ") are kept as they were read");
}

std::string assoc_pointer(RecordWriter& w, const std::string& handle) {
    if (handle.empty() || handle == "0") return "0";
    if (w.live(handle)) return handle;
    w.warn("a pointer to " + handle + ", which no longer exists, is set to none (0)");
    return "0";
}

std::vector<std::string> assoc_live(RecordWriter& w, const std::vector<std::string>& handles) {
    std::vector<std::string> out;
    for (const std::string& h : handles) {
        if (h.empty() || h == "0" || w.live(h)) {
            out.push_back(h.empty() ? "0" : h);
        } else {
            w.warn("a pointer to " + h + ", which no longer exists, is left out of its list");
        }
    }
    return out;
}

RefDescriptor assoc_ref(const char* subclass, int code, RefKind kind, RefRemoval removal, int ordinal) {
    RefDescriptor d;
    d.subclass = subclass;
    d.code = code;
    d.kind = kind;
    d.removal = removal;
    d.ordinal = ordinal;
    return d;
}

RefDescriptor assoc_member_ref(const char* subclass, int code, RefKind kind) {
    RefDescriptor d = assoc_ref(subclass, code, kind, RefRemoval::DropMember);
    d.counter_code = 90;
    return d;
}

ListSpec assoc_whole_subclass(const char* subclass) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

ClassSpec assoc_class(const char* cpp_name, int flags, const char* application) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = application;
    c.proxy_flags = flags;
    c.in_older = false;
    return c;
}

VersionMatrix assoc_matrix(std::initializer_list<DxfVersion> verified, std::initializer_list<DxfVersion> verified_ascii_only) {
    // R12 has no OBJECTS section (AutoCAD drops the records): a named error before anything changes. R13 and R14: AutoCAD 2025
    // cannot save them and no historical file has one: not established
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.set(v, Generation::Native);
    }
    for (DxfVersion v : verified) m.autocad(v, AutoCadStatus::Verified);
    for (DxfVersion v : verified_ascii_only) m.autocad(v, DxfFormat::Ascii, AutoCadStatus::Verified);
    return m;
}

// ---------------------------------------------------------------------------
// Value parameters
// ---------------------------------------------------------------------------

bool read_assoc_value_parameter(GroupCursor& c, AssocValueParameter& out) {
    out = AssocValueParameter();
    c.require_integer(90, out.version);
    c.require_string(1, out.name);
    c.require_integer(90, out.unit_type);
    size_t n = 0;
    if (!c.ok() || !c.count(90, 2, n)) {
        if (c.ok()) c.fail("the number of values (90) of the value parameter is missing");
        return false;
    }
    out.values.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        AssocParameterValue v;
        v.code = c.code();
        switch (v.code) {
            case 40: c.require_real(40, v.real); break;
            case 70: {
                int16_t x = 0;
                c.require_integer(70, x);
                v.integer = x;
                break;
            }
            case 90: c.require_integer(90, v.integer); break;
            default:
                c.fail("a value of a value parameter in group " + std::to_string(v.code) + " is not established (AutoCAD's files have 40, 70 and 90)");
                return false;
        }
        c.require_handle(330, v.pointer);
        out.values.push_back(std::move(v));
    }
    c.require_handle(330, out.dependency);
    return c.ok();
}

void write_assoc_value_parameter(RecordWriter& w, const AssocValueParameter& v) {
    w.integer(90, v.version);
    w.text(1, v.name);
    w.integer(90, v.unit_type);
    w.integer(90, static_cast<int64_t>(v.values.size()));
    for (const AssocParameterValue& value : v.values) {
        switch (value.code) {
            case 70: w.integer(70, value.integer); break;
            case 90: w.integer(90, value.integer); break;
            default: w.real(40, value.real); break;
        }
        w.handle(330, assoc_pointer(w, value.pointer));
    }
    w.handle(330, assoc_pointer(w, v.dependency));
}

bool read_assoc_value_parameters(GroupCursor& c, std::vector<AssocValueParameter>& out) {
    size_t n = 0;
    if (!c.count(90, 4, n)) {
        if (c.ok()) c.fail("the number of value parameters (90) is missing");
        return false;
    }
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        AssocValueParameter p;
        if (!read_assoc_value_parameter(c, p)) return false;
        out.push_back(std::move(p));
    }
    return c.ok();
}

void write_assoc_value_parameters(RecordWriter& w, const std::vector<AssocValueParameter>& v) {
    w.integer(90, static_cast<int64_t>(v.size()));
    for (const AssocValueParameter& p : v) write_assoc_value_parameter(w, p);
}

// ---------------------------------------------------------------------------
// AcDbAssocAction
// ---------------------------------------------------------------------------

namespace {

bool read_handle_list(GroupCursor& c, int code, std::vector<std::string>& out) {
    size_t n = 0;
    if (!c.count(90, 1, n)) {
        if (c.ok()) c.fail("a count (90) is missing");
        return false;
    }
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        std::string h;
        c.require_handle(code, h);
        out.push_back(std::move(h));
    }
    return c.ok();
}

void write_handle_list(RecordWriter& w, int code, const std::vector<std::string>& handles) {
    std::vector<std::string> live = assoc_live(w, handles);
    w.integer(90, static_cast<int64_t>(live.size()));
    for (const std::string& h : live) w.handle(code, h);
}

} // namespace

bool read_assoc_action_base(const RecordView& view, TypedReadContext& ctx, AssocActionBase& out) {
    GroupCursor c(view, "AcDbAssocAction");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocAction subclass");
    out = AssocActionBase();
    c.require_integer(90, out.version);
    c.require_integer(90, out.status);
    c.require_handle(330, out.owning_network);
    c.require_handle(360, out.action_body);
    c.require_integer(90, out.action_index);
    c.require_integer(90, out.max_dependency_index);
    if (!c.ok()) return ctx.fail(c.error());
    if (out.version != 1 && out.version != 2) {
        return ctx.fail(assoc_describe(view) + ": AcDbAssocAction version " + std::to_string(out.version) +
                        " is not established (AutoCAD's files have 1 and 2)");
    }
    read_handle_list(c, 360, out.dependencies);
    if (out.version >= 2 && c.ok()) {
        c.require_integer(90, out.reserved_after_dependencies);
        read_handle_list(c, 360, out.params);
        c.require_integer(90, out.reserved_after_params);
        if (c.ok()) read_assoc_value_parameters(c, out.value_params);
    }
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    return true;
}

void write_assoc_action_base(RecordWriter& w, const AssocActionBase& v) {
    if (v.version != 1 && v.version != 2) {
        w.fail("AcDbAssocAction version " + std::to_string(v.version) + " is not established (AutoCAD's files have 1 and 2)");
        return;
    }
    w.subclass("AcDbAssocAction");
    w.integer(90, v.version);
    w.integer(90, v.status);
    w.handle(330, assoc_pointer(w, v.owning_network));
    w.handle(360, assoc_pointer(w, v.action_body));
    w.integer(90, v.action_index);
    w.integer(90, v.max_dependency_index);
    write_handle_list(w, 360, v.dependencies);
    if (v.version >= 2) {
        w.integer(90, v.reserved_after_dependencies);
        write_handle_list(w, 360, v.params);
        w.integer(90, v.reserved_after_params);
        write_assoc_value_parameters(w, v.value_params);
    }
}

void assoc_action_references(const AssocActionBase& v, std::vector<TypedReference>& out) {
    auto add = [&](const std::string& h, RefKind kind) {
        if (!h.empty() && h != "0") out.push_back({h, kind, false});
    };
    add(v.owning_network, RefKind::SoftPointer);
    add(v.action_body, RefKind::HardOwner);
    for (const std::string& h : v.dependencies) add(h, RefKind::HardOwner);
    for (const std::string& h : v.params) add(h, RefKind::HardOwner);
    for (const AssocValueParameter& p : v.value_params) {
        for (const AssocParameterValue& value : p.values) add(value.pointer, RefKind::SoftPointer);
        add(p.dependency, RefKind::SoftPointer);
    }
}

namespace {

std::vector<RefDescriptor> action_refs() {
    // The network is a pointer, the action body hard-owned; the dependencies and parameters are hard-owned members of
    // counted lists (the count follows when one goes); the value parameters' pointers are soft (every one 0 in AutoCAD's files)
    return {assoc_ref("AcDbAssocAction", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace),
            assoc_ref("AcDbAssocAction", 360, RefKind::HardOwner, RefRemoval::ZeroInPlace, 0),
            assoc_member_ref("AcDbAssocAction", 360, RefKind::HardOwner)};
}

// ---------------------------------------------------------------------------
// ACDBASSOCACTION
// ---------------------------------------------------------------------------

bool read_action(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocActionData d;
    if (!read_assoc_action_base(view, ctx, d.action)) return false;
    o.data = Boxed<AssocActionData>(std::move(d));
    return true;
}

void write_action(RecordWriter& w, const Object& o) {
    w.object_head("ACDBASSOCACTION", o);
    write_assoc_action_base(w, o.as_assoc_action().action);
    w.xdata(o.xdata);
}

void action_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_action_references(o.as_assoc_action().action, out);
}

// ---------------------------------------------------------------------------
// ACDBASSOCNETWORK
// ---------------------------------------------------------------------------

bool read_network(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocNetworkData d;
    if (!read_assoc_action_base(view, ctx, d.action)) return false;
    GroupCursor c(view, "AcDbAssocNetwork");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocNetwork subclass");
    c.require_integer(90, d.version);
    c.require_integer(90, d.max_action_index);
    size_t n = 0;
    if (c.ok() && !c.count(90, 1, n)) {
        if (c.ok()) c.fail("the number of actions (90) is missing");
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        AssocActionMember m;
        if (c.code() == 360) {
            m.owned = true;
            c.require_handle(360, m.handle);
        } else {
            m.owned = false;
            c.require_handle(330, m.handle);
        }
        d.actions.push_back(std::move(m));
    }
    if (c.ok()) read_handle_list(c, 330, d.soft_actions);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocNetworkData>(std::move(d));
    return true;
}

void write_network(RecordWriter& w, const Object& o) {
    const AssocNetworkData& d = o.as_assoc_network();
    w.object_head("ACDBASSOCNETWORK", o);
    write_assoc_action_base(w, d.action);
    w.subclass("AcDbAssocNetwork");
    w.integer(90, d.version);
    w.integer(90, d.max_action_index);
    std::vector<AssocActionMember> live;
    for (const AssocActionMember& m : d.actions) {
        if (m.handle.empty() || m.handle == "0" || w.live(m.handle)) {
            live.push_back(m);
        } else {
            w.warn("a pointer to " + m.handle + ", which no longer exists, is left out of its list");
        }
    }
    w.integer(90, static_cast<int64_t>(live.size()));
    for (const AssocActionMember& m : live) w.handle(m.owned ? 360 : 330, m.handle.empty() ? "0" : m.handle);
    write_handle_list(w, 330, d.soft_actions);
    w.xdata(o.xdata);
}

void network_references(const Object& o, std::vector<TypedReference>& out) {
    const AssocNetworkData& d = o.as_assoc_network();
    assoc_action_references(d.action, out);
    for (const AssocActionMember& m : d.actions) {
        if (!m.handle.empty() && m.handle != "0") out.push_back({m.handle, m.owned ? RefKind::HardOwner : RefKind::SoftPointer, false});
    }
    for (const std::string& h : d.soft_actions) {
        if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
    }
}

std::vector<RefDescriptor> network_refs() {
    std::vector<RefDescriptor> refs = action_refs();
    refs.push_back(assoc_member_ref("AcDbAssocNetwork", 360, RefKind::HardOwner));
    refs.push_back(assoc_member_ref("AcDbAssocNetwork", 330, RefKind::SoftPointer));
    return refs;
}

} // namespace

void register_assoc_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCACTION";
        c.type = ObjectType::AssocAction;
        c.read = read_action;
        c.write = write_action;
        c.references = action_references;
        c.cls = assoc_class("AcDbAssocAction");
        // AutoCAD's own files: 2000 … 2018 in doc/facit/acad_*_dxf_surfaces.dxf and _associative.dxf, both formats
        c.versions = assoc_matrix({DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                                   DxfVersion::R2018});
        c.refs = action_refs();
        c.lists = {assoc_whole_subclass("AcDbAssocAction")};
        c.step = 10;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCNETWORK";
        c.type = ObjectType::AssocNetwork;
        c.read = read_network;
        c.write = write_network;
        c.references = network_references;
        c.cls = assoc_class("AcDbAssocNetwork");
        c.versions = assoc_matrix({DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                                   DxfVersion::R2018});
        c.refs = network_refs();
        c.lists = {assoc_whole_subclass("AcDbAssocAction"), assoc_whole_subclass("AcDbAssocNetwork")};
        c.step = 10;
        registry.add(std::move(c));
    }
    register_assoc_dependency_codecs(registry);
    register_assoc_param_codecs(registry);
    register_assoc_body_codecs(registry);
    register_assoc_perssubent_codecs(registry);
}

} // namespace internal
} // namespace hdxf
