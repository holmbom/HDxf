#include "AssocExtended.hpp"
#include <algorithm>
#include <cstdlib>

// The persistent subentity manager classes and the persistent subentity id (phase 9, step 9.10; plan
// 2026-10-27-fas-9-typer.md, appendix B.13, B.18 and B.25).
//
// ACDBASSOCPERSSUBENTMANAGER and ACDBPERSSUBENTMANAGER are the drawing's tables of persistent subentity ids (one each
// in the named object dictionary's reach; AutoCAD's files have them whenever a solid or a surface exists). Every group
// but the last is a 90; what AutoCAD's own files show (doc/facit/acad_<2000 … 2018>_dxf_*.dxf, the corpus' Surface_2004,
// example_*, 3Dlines and my experiments of doc/facit/fas9_assoc_core.scr) is one grammar for all of them:
//
//   AcDbAssocPersSubentManager: 90 version (1 until 2010, 2 from 2013), 90 3, 90 0, 90 2, 90 next id (the highest id
//   given), 90 number of history entries, [the entries], 90 count + ids, 90 count + ids, [90 count + ids in version 2: none in most files, [15] in
//   fas9_assoc_extended.scr's 2013 and 2018 files (step 9.15)], 290
//   AcDbPersSubentManager:      90 version (2), 90 0, 90 2, 90 next id, 90 number of history entries, [the entries],
//                               90 count + ids
//
// The history entries (a solid's fillet, chamfer and Boolean operations, a surface made from curves, a dimension on a solid) are
// typed in assoc_history.cpp (step 9.15, with the entries of SURFTRIM and SURFFILLET, kinds 8 and 10); an entry that does not fit
// the grammar derived keeps its manager as it was read, with a warning (B105).
//
// A persistent subentity id (AcDbAssocGeomDependency's 1 and the groups after it) has the class AutoCAD names it by:
//   AcDbAssocSingleEdgePersSubentId        no more groups
//   AcDbAssocEdgePersSubentId              90 version, 90 start vertex id, 90 end vertex id (a rectangle's edges are
//                                          1-2, 2-3, 3-4 and 4-1 in AutoCAD's own files)
//   AcDbAssocSimplePersSubentId            90 version, 90 subentity type, 90 subentity index
//   AcDbAssocAsmBasedEntityPersSubentId    90 version, 90 subentity type, 90 subentity index

namespace hdxf {

const char* persistent_subentity_class_name(PersistentSubentityKind kind) {
    switch (kind) {
        case PersistentSubentityKind::SingleEdge: return "AcDbAssocSingleEdgePersSubentId";
        case PersistentSubentityKind::Edge: return "AcDbAssocEdgePersSubentId";
        case PersistentSubentityKind::Simple: return "AcDbAssocSimplePersSubentId";
        case PersistentSubentityKind::AsmBasedEntity: return "AcDbAssocAsmBasedEntityPersSubentId";
    }
    return "AcDbAssocSingleEdgePersSubentId";
}

bool persistent_subentity_kind_of(const std::string& class_name, PersistentSubentityKind& kind) {
    for (PersistentSubentityKind k : {PersistentSubentityKind::SingleEdge, PersistentSubentityKind::Edge,
                                      PersistentSubentityKind::Simple, PersistentSubentityKind::AsmBasedEntity}) {
        if (class_name == persistent_subentity_class_name(k)) {
            kind = k;
            return true;
        }
    }
    return false;
}

namespace internal {

bool read_persistent_subentity_id(GroupCursor& c, PersistentSubentityId& out) {
    out = PersistentSubentityId();
    std::string name;
    if (!c.require_string(1, name)) return false;
    if (!persistent_subentity_kind_of(name, out.kind)) {
        c.fail("the persistent subentity id class \"" + name + "\" is not established (AutoCAD's files have " +
               "AcDbAssocSingleEdgePersSubentId, AcDbAssocEdgePersSubentId, AcDbAssocSimplePersSubentId and " +
               "AcDbAssocAsmBasedEntityPersSubentId)");
        return false;
    }
    switch (out.kind) {
        case PersistentSubentityKind::SingleEdge:
            break;
        case PersistentSubentityKind::Edge:
            c.require_integer(90, out.version);
            c.require_integer(90, out.start_vertex);
            c.require_integer(90, out.end_vertex);
            break;
        case PersistentSubentityKind::Simple:
        case PersistentSubentityKind::AsmBasedEntity:
            c.require_integer(90, out.version);
            c.require_integer(90, out.subentity_type);
            c.require_integer(90, out.subentity_index);
            break;
    }
    return c.ok();
}

void write_persistent_subentity_id(RecordWriter& w, const PersistentSubentityId& v) {
    w.text(1, persistent_subentity_class_name(v.kind));
    switch (v.kind) {
        case PersistentSubentityKind::SingleEdge:
            break;
        case PersistentSubentityKind::Edge:
            w.integer(90, v.version);
            w.integer(90, v.start_vertex);
            w.integer(90, v.end_vertex);
            break;
        case PersistentSubentityKind::Simple:
        case PersistentSubentityKind::AsmBasedEntity:
            w.integer(90, v.version);
            w.integer(90, v.subentity_type);
            w.integer(90, v.subentity_index);
            break;
    }
}

namespace {

bool read_id_list(GroupCursor& c, std::vector<int32_t>& out) {
    size_t n = 0;
    if (!c.count(90, 1, n)) {
        if (c.ok()) c.fail("a count (90) is missing");
        return false;
    }
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        int32_t id = 0;
        c.require_integer(90, id);
        out.push_back(id);
    }
    return c.ok();
}

void write_id_list(RecordWriter& w, const std::vector<int32_t>& ids) {
    w.integer(90, static_cast<int64_t>(ids.size()));
    for (int32_t id : ids) w.integer(90, id);
}

// The number of history entries and the entries (step 9.15: assoc_history.cpp has their grammar; before it only none was
// established). `new_form`: the assoc manager's form, false the base manager's
bool read_history(GroupCursor& c, const RecordView& view, TypedReadContext& ctx, bool new_form, std::vector<PersHistoryEntry>& out) {
    int32_t entries = 0;
    c.require_integer(90, entries);
    if (!c.ok()) return ctx.fail(c.error());
    if (entries < 0 || static_cast<size_t>(entries) > c.remaining()) {
        return ctx.fail(assoc_describe(view) + ": " + std::to_string(entries) + " persistent subentity history entries but not as many groups");
    }
    std::string error;
    if (!read_persistent_history(c, static_cast<size_t>(entries), new_form, out, error)) {
        return ctx.fail(assoc_describe(view) + ": persistent subentity history entries: " + error);
    }
    return true;
}

bool read_assoc_manager(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbAssocPersSubentManager");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocPersSubentManager subclass");
    AssocPersSubentManagerData d;
    c.require_integer(90, d.version);
    c.require_integer(90, d.header_1);
    c.require_integer(90, d.header_2);
    c.require_integer(90, d.header_3);
    c.require_integer(90, d.next_id);
    if (!c.ok()) return ctx.fail(c.error());
    if (d.version != 1 && d.version != 2) {
        return ctx.fail(assoc_describe(view) + ": AcDbAssocPersSubentManager version " + std::to_string(d.version) +
                        " is not established (AutoCAD's files have 1 and 2)");
    }
    if (!read_history(c, view, ctx, true, d.history)) return false;
    read_id_list(c, d.first_list);
    read_id_list(c, d.second_list);
    if (d.version >= 2) read_id_list(c, d.third_list);
    c.require_boolean(290, d.flag);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocPersSubentManagerData>(std::move(d));
    return true;
}

// The step ids of the actions that are gone (step 9.15; AutoCAD's own erases: acad_*_dxf_assoc_core_surface_erased.dxf and
// profile_erased.dxf: the ids 2, and 1 and 2, leave the first list when those actions go). The step id is the 1071 of the
// action's ACAD_ASSOCPERSSUBENTMANAGER XDATA
std::vector<int32_t> removed_step_ids(RecordWriter& w) {
    std::vector<int32_t> ids;
    if (!w.filtering() || !w.read_from_file()) return ids;
    for (const Object& o : w.drawing().objects()) {
        if (o.type != ObjectType::AssocAction || w.live(o.handle)) continue;
        for (const XdataList& x : o.xdata) {
            if (x.app_name != "ACAD_ASSOCPERSSUBENTMANAGER") continue;
            for (const XdataItem& item : x.items) {
                if (item.code == 1071) ids.push_back(static_cast<int32_t>(std::atoi(item.value.c_str())));
            }
        }
    }
    return ids;
}

void write_assoc_manager(RecordWriter& w, const Object& o) {
    const AssocPersSubentManagerData& d = o.as_assoc_pers_subent_manager();
    if (d.version != 1 && d.version != 2) {
        w.fail("AcDbAssocPersSubentManager version " + std::to_string(d.version) + " is not established (AutoCAD's files have 1 and 2)");
        return;
    }
    w.object_head("ACDBASSOCPERSSUBENTMANAGER", o);
    w.subclass("AcDbAssocPersSubentManager");
    w.integer(90, d.version);
    w.integer(90, d.header_1);
    w.integer(90, d.header_2);
    w.integer(90, d.header_3);
    w.integer(90, d.next_id);
    w.integer(90, static_cast<int64_t>(d.history.size()));
    if (!write_persistent_history(w, true, d.history)) return;
    std::vector<int32_t> first = d.first_list;
    for (int32_t gone : removed_step_ids(w)) first.erase(std::remove(first.begin(), first.end(), gone), first.end());
    write_id_list(w, first);
    write_id_list(w, d.second_list);
    if (d.version >= 2) write_id_list(w, d.third_list);
    w.boolean(290, d.flag);
    w.xdata(o.xdata);
}

bool read_manager(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbPersSubentManager");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbPersSubentManager subclass");
    PersSubentManagerData d;
    c.require_integer(90, d.version);
    c.require_integer(90, d.header_1);
    c.require_integer(90, d.header_2);
    c.require_integer(90, d.next_id);
    if (!c.ok()) return ctx.fail(c.error());
    if (d.version != 2) {
        return ctx.fail(assoc_describe(view) + ": AcDbPersSubentManager version " + std::to_string(d.version) +
                        " is not established (AutoCAD's files have 2)");
    }
    if (!read_history(c, view, ctx, false, d.history)) return false;
    read_id_list(c, d.ids);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<PersSubentManagerData>(std::move(d));
    return true;
}

void write_manager(RecordWriter& w, const Object& o) {
    const PersSubentManagerData& d = o.as_pers_subent_manager();
    if (d.version != 2) {
        w.fail("AcDbPersSubentManager version " + std::to_string(d.version) + " is not established (AutoCAD's files have 2)");
        return;
    }
    w.object_head("ACDBPERSSUBENTMANAGER", o);
    w.subclass("AcDbPersSubentManager");
    w.integer(90, d.version);
    w.integer(90, d.header_1);
    w.integer(90, d.header_2);
    w.integer(90, d.next_id);
    w.integer(90, static_cast<int64_t>(d.history.size()));
    if (!write_persistent_history(w, false, d.history)) return;
    write_id_list(w, d.ids);
    w.xdata(o.xdata);
}

} // namespace

void register_assoc_perssubent_codecs(TypedRegistry& registry) {
    const auto all = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                      DxfVersion::R2018};
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCPERSSUBENTMANAGER";
        c.type = ObjectType::AssocPersSubentManager;
        c.read = read_assoc_manager;
        c.write = write_assoc_manager;
        c.cls = assoc_class("AcDbAssocPersSubentManager", 1024);
        c.versions = assoc_matrix(all);
        c.lists = {assoc_whole_subclass("AcDbAssocPersSubentManager")};
        c.step = 10;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBPERSSUBENTMANAGER";
        c.type = ObjectType::PersSubentManager;
        c.read = read_manager;
        c.write = write_manager;
        c.cls = assoc_class("AcDbPersSubentManager", 1024, "AcDbPersSubentManager");
        c.versions = assoc_matrix(all);
        c.lists = {assoc_whole_subclass("AcDbPersSubentManager")};
        c.step = 10;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
