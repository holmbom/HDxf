#include "DynamicBlockElements.hpp"
#include <cstdint>
#include <string>
#include <vector>

// The actions of step 9.13 (phase 9; plan 2026-10-27-fas-9-typer.md, F9.9, B109):
//
//   BLOCKFLIPACTION          AcDbEvalExpr, AcDbBlockElement, AcDbBlockAction, AcDbBlockFlipAction (92 93 94 95, 301 302 303 304)
//   BLOCKMOVEACTION          … AcDbBlockAction, AcDbBlockMoveAction (92 301 93 302, 140 141 280)
//   BLOCKARRAYACTION         … AcDbBlockAction, AcDbBlockArrayAction (92 93 94 95, 301 … 304, 140 141)
//   BLOCKROTATEACTION        … AcDbBlockAction, AcDbBlockActionWithBasePt, AcDbBlockRotationAction (94 303)
//   BLOCKSTRETCHACTION       … AcDbBlockAction, AcDbBlockStretchAction (the links, the frame, the entities with their vertices, the
//                            connections to other actions, 140 141 280)
//   BLOCKPOLARSTRETCHACTION  … AcDbBlockAction, AcDbBlockPolarStretchAction (six links, the frame, the entities, 72 75 76 77 78 140 141)
//   BLOCKLOOKUPACTION        … AcDbBlockAction, AcDbBlockLookupAction (the table of a lookup parameter)
//
// AcDbBlockAction (step 9.9's BlockActionData) is: 70 and its 91 (the graph nodes the action is connected to), 71 and its 330 (the
// entities it works on: soft pointers) and the location of its glyph (1010, class data). The groups are AutoCAD 2025's own, the same in
// every version 2000 … 2018 (the Sample/Dynamic Blocks DWGs saved as DXF; doc/facit/dyntypes, fas9_dynamic_types.scr).

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// The own subclasses
// ---------------------------------------------------------------------------

void FlipActionSpec::read_own(GroupCursor& c, Data& d) { read_links_grouped(c, 92, 301, d.links.data(), d.links.size()); }

void MoveActionSpec::read_own(GroupCursor& c, Data& d) {
    read_links_interleaved(c, 92, 301, d.links.data(), d.links.size());
    c.require_real(140, d.distance_multiplier);
    c.require_real(141, d.angle_offset);
    c.require_integer(280, d.flag_280);
}

void ArrayActionSpec::read_own(GroupCursor& c, Data& d) {
    read_links_grouped(c, 92, 301, d.links.data(), d.links.size());
    c.require_real(140, d.value_140);
    c.require_real(141, d.value_141);
}

void RotateActionSpec::read_own(GroupCursor& c, Data& d) {
    c.require_integer(94, d.angle.node_id);
    c.require_string(303, d.angle.name);
}

namespace {

// An integer list: `count_code` and then that many `code` (a vertex index list)
void read_indexes(GroupCursor& c, int count_code, int code, std::vector<int32_t>& out) {
    size_t n = 0;
    if (!c.count(count_code, 1, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " is missing");
        return;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        int32_t v = 0;
        c.require_integer(code, v);
        out.push_back(v);
    }
}

} // namespace

void StretchActionSpec::read_own(GroupCursor& c, Data& d) {
    read_links_interleaved(c, 92, 301, d.links.data(), d.links.size());
    if (!c.ok() || !read_frame_points(c, 72, 1011, d.frame)) return;
    size_t entities = 0;
    if (!c.count(73, 2, entities)) {
        if (c.ok()) c.fail("group 73 is missing");
        return;
    }
    for (size_t i = 0; i < entities && c.ok(); ++i) {
        BlockStretchEntity e;
        c.require_handle(331, e.entity);
        read_indexes(c, 74, 94, e.vertices);
        if (c.ok()) d.entities.push_back(std::move(e));
    }
    size_t connections = 0;
    if (!c.ok()) return;
    if (!c.count(75, 2, connections)) {
        if (c.ok()) c.fail("group 75 is missing");
        return;
    }
    for (size_t i = 0; i < connections && c.ok(); ++i) {
        BlockStretchConnection k;
        c.require_integer(95, k.node_id);
        read_indexes(c, 76, 94, k.vertices);
        if (c.ok()) d.connections.push_back(std::move(k));
    }
    c.require_real(140, d.distance_multiplier);
    c.require_real(141, d.angle_offset);
    c.require_integer(280, d.flag_280);
}

void PolarStretchActionSpec::read_own(GroupCursor& c, Data& d) {
    read_links_interleaved(c, 92, 301, d.links.data(), d.links.size());
    if (!c.ok() || !read_frame_points(c, 73, 1011, d.frame)) return;
    c.require_integer(72, d.flag_72);
    size_t entities = 0;
    if (!c.ok()) return;
    if (!c.count(74, 1, entities)) {
        if (c.ok()) c.fail("group 74 is missing");
        return;
    }
    for (size_t i = 0; i < entities && c.ok(); ++i) {
        std::string handle;
        c.require_handle(332, handle);
        d.entities.push_back(std::move(handle));
    }
    c.require_integer(75, d.value_75);
    c.require_integer(76, d.value_76);
    c.require_integer(78, d.value_78);
    c.require_real(141, d.real_141);
    c.require_real(140, d.real_140);
    c.require_integer(77, d.flag_77);
}

void LookupActionSpec::read_own(GroupCursor& c, Data& d) {
    int32_t rows = 0, columns = 0;
    c.require_integer(92, rows);
    c.require_integer(93, columns);
    c.require_string(301, d.text_301);
    if (!c.ok()) return;
    if (rows < 0 || columns < 0) {
        c.fail("the lookup table has " + std::to_string(rows) + " rows and " + std::to_string(columns) + " columns");
        return;
    }
    // Every cell is a group and every column eight: the counts are checked against what remains before anything is allocated
    if (columns == 0 && rows > 0) {
        c.fail("the lookup table has " + std::to_string(rows) + " rows and no column");
        return;
    }
    if (static_cast<uint64_t>(rows) * static_cast<uint64_t>(columns) + static_cast<uint64_t>(columns) * 8 > c.remaining()) {
        c.fail("the lookup table of " + std::to_string(rows) + " rows and " + std::to_string(columns) +
               " columns is more than the groups that remain can hold");
        return;
    }
    for (int32_t r = 0; r < rows && c.ok(); ++r) {
        std::vector<std::string> row;
        for (int32_t k = 0; k < columns && c.ok(); ++k) {
            std::string cell;
            c.require_string(302, cell);
            row.push_back(std::move(cell));
        }
        if (c.ok()) d.rows.push_back(std::move(row));
    }
    for (int32_t k = 0; k < columns && c.ok(); ++k) {
        BlockLookupColumn col;
        c.require_string(303, col.name);
        c.require_integer(94, col.node_id);
        c.require_integer(95, col.value_type);
        c.require_integer(96, col.unit_type);
        c.require_integer(282, col.flag_282);
        c.require_string(305, col.category);
        c.require_integer(281, col.flag_281);
        c.require_string(304, col.property_name);
        if (c.ok()) d.columns.push_back(std::move(col));
    }
    c.require_integer(280, d.flag_280);
}

// ---------------------------------------------------------------------------
// The codecs
// ---------------------------------------------------------------------------

namespace {

template <class Spec>
void references_of_action(const Object& o, std::vector<TypedReference>& out) {
    add_action_references(Spec::get(o).action, out);
}

void stretch_references(const Object& o, std::vector<TypedReference>& out) {
    const BlockStretchActionData& d = o.as_block_stretch_action();
    add_action_references(d.action, out);
    for (const BlockStretchEntity& e : d.entities) {
        if (!e.entity.empty() && e.entity != "0") out.push_back({e.entity, RefKind::SoftPointer, false});
    }
}

void polar_stretch_references(const Object& o, std::vector<TypedReference>& out) {
    const BlockPolarStretchActionData& d = o.as_block_polar_stretch_action();
    add_action_references(d.action, out);
    for (const std::string& h : d.entities) {
        if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
    }
}

template <class Spec>
ObjectCodec action_codec(ObjectReferencesFn references) {
    ObjectCodec c = dynamic_codec(Spec::name, Spec::type, read_action<Spec>, write_action<Spec>, Spec::cpp_name);
    c.references = references ? references : references_of_action<Spec>;
    c.lists = block_action_lists();
    c.refs = {counted_soft_pointer("AcDbBlockAction", 330, 71)};
    return c;
}

} // namespace

void register_dynamic_action_codecs(TypedRegistry& registry) {
    registry.add(action_codec<FlipActionSpec>(nullptr));
    registry.add(action_codec<MoveActionSpec>(nullptr));
    registry.add(action_codec<ArrayActionSpec>(nullptr));
    registry.add(action_codec<RotateActionSpec>(nullptr));
    {
        // The entities of a stretch (331, each with its vertices) are one list with the frame and the connections: the whole subclass
        ObjectCodec c = action_codec<StretchActionSpec>(stretch_references);
        c.lists.push_back(whole_subclass_list("AcDbBlockStretchAction"));
        registry.add(std::move(c));
    }
    {
        ObjectCodec c = action_codec<PolarStretchActionSpec>(polar_stretch_references);
        c.lists.push_back(whole_subclass_list("AcDbBlockPolarStretchAction"));
        registry.add(std::move(c));
    }
    {
        ObjectCodec c = action_codec<LookupActionSpec>(nullptr);
        c.lists.push_back(whole_subclass_list("AcDbBlockLookupAction"));
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
