#include "SolidHistoryTypes.hpp"
#include "DynamicBlockTypes.hpp"
#include "dxf_writer.hpp"
#include "object_data_visit.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// The solid history of step 9.14 (phase 9; plan 2026-10-27-fas-9-typer.md, B100–B105, B111, B113–B116):
//
//   ACSH_HISTORY_CLASS    AcDbShHistory
//   the nodes             AcDbEvalExpr, AcDbShHistoryNode, [AcDbShPrimitive,] the type's own subclass(es):
//     ACSH_BOX_CLASS (AcDbShBox), ACSH_WEDGE_CLASS (AcDbShWedge), ACSH_SPHERE_CLASS (AcDbShSphere), ACSH_CYLINDER_CLASS
//     (AcDbShCylinder), ACSH_CONE_CLASS (AcDbShCylinder, AcDbShCone), ACSH_TORUS_CLASS (AcDbShTorus), ACSH_PYRAMID_CLASS
//     (AcDbShPyramid), ACSH_EXTRUSION_CLASS (AcDbShSweepBase, AcDbShExtrusion), ACSH_SWEEP_CLASS (AcDbShSweepBase, AcDbShSweep),
//     ACSH_REVOLVE_CLASS (AcDbShRevolve), ACSH_LOFT_CLASS (AcDbShLoft), ACSH_BREP_CLASS (AcDbShBrep); and without
//     AcDbShPrimitive ACSH_BOOLEAN_CLASS (AcDbShBoolean), ACSH_CHAMFER_CLASS (AcDbShChamfer), ACSH_FILLET_CLASS (AcDbShFillet)
//
// Every group is what AutoCAD 2025's own files have (doc/facit/fas9_history.scr and the older acad_<version>_dxf_solids.dxf,
// 2007 … 2018, ASCII and binary), checked against ACadSharp's, jscad's and LibreDWG's AutoCAD-saved samples. The history is a
// record of how the solid was made, not its geometry: nothing here touches the solid's ACIS data, and no node has extents.

namespace hdxf {

namespace {

bool same_handle(const std::string& a, const std::string& b) {
    auto strip = [](const std::string& s) {
        size_t i = 0;
        while (i + 1 < s.size() && s[i] == '0') ++i;
        std::string out = s.substr(i);
        for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    };
    return !a.empty() && !b.empty() && strip(a) == strip(b);
}

bool is_history_node(ObjectType t) {
    switch (t) {
        case ObjectType::ShBox: case ObjectType::ShWedge: case ObjectType::ShSphere: case ObjectType::ShCylinder:
        case ObjectType::ShCone: case ObjectType::ShTorus: case ObjectType::ShPyramid: case ObjectType::ShExtrusion:
        case ObjectType::ShSweep: case ObjectType::ShRevolve: case ObjectType::ShLoft: case ObjectType::ShBoolean:
        case ObjectType::ShBrep: case ObjectType::ShChamfer: case ObjectType::ShFillet:
            return true;
        default:
            return false;
    }
}

// Whether `data` is the data type of the node type
bool data_matches(ObjectType type, const ObjectData& data) {
    switch (type) {
        case ObjectType::ShBox: return std::holds_alternative<Boxed<ShBoxData>>(data);
        case ObjectType::ShWedge: return std::holds_alternative<Boxed<ShWedgeData>>(data);
        case ObjectType::ShSphere: return std::holds_alternative<Boxed<ShSphereData>>(data);
        case ObjectType::ShCylinder: return std::holds_alternative<Boxed<ShCylinderData>>(data);
        case ObjectType::ShCone: return std::holds_alternative<Boxed<ShConeData>>(data);
        case ObjectType::ShTorus: return std::holds_alternative<Boxed<ShTorusData>>(data);
        case ObjectType::ShPyramid: return std::holds_alternative<Boxed<ShPyramidData>>(data);
        case ObjectType::ShExtrusion: return std::holds_alternative<Boxed<ShExtrusionData>>(data);
        case ObjectType::ShSweep: return std::holds_alternative<Boxed<ShSweepData>>(data);
        case ObjectType::ShRevolve: return std::holds_alternative<Boxed<ShRevolveData>>(data);
        case ObjectType::ShLoft: return std::holds_alternative<Boxed<ShLoftData>>(data);
        case ObjectType::ShBoolean: return std::holds_alternative<Boxed<ShBooleanData>>(data);
        case ObjectType::ShBrep: return std::holds_alternative<Boxed<ShBrepData>>(data);
        case ObjectType::ShChamfer: return std::holds_alternative<Boxed<ShChamferData>>(data);
        case ObjectType::ShFillet: return std::holds_alternative<Boxed<ShFilletData>>(data);
        default: return false;
    }
}

// The AcDbEvalExpr and the AcDbShHistoryNode of a node's data (every node type has them as `eval` and `node`)
template <typename T>
EvalExprData* eval_in(T& value) {
    if constexpr (requires { (*value).eval; }) return &(*value).eval;
    else return nullptr;
}

template <typename T>
ShHistoryNodeData* node_in(T& value) {
    if constexpr (requires { (*value).node; }) return &(*value).node;
    else return nullptr;
}

EvalExprData* eval_of(Object& o) {
    return internal::visit_object_data([](auto& value) { return eval_in(value); }, o.data);
}

ShHistoryNodeData* node_of(Object& o) {
    return internal::visit_object_data([](auto& value) { return node_in(value); }, o.data);
}

const ShHistoryNodeData* node_of(const Object& o) {
    return node_of(const_cast<Object&>(o));
}

Object make_object(ObjectType type, const std::string& handle, const std::string& owner, ObjectData data) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = owner;
    o.data = std::move(data);
    return o;
}

} // namespace

// ---------------------------------------------------------------------------
// Drawing: a solid's history, its graph and its nodes
// ---------------------------------------------------------------------------

const Object* Drawing::solid_history(const std::string& solid_handle) const {
    const Entity* solid = find_entity(solid_handle);
    if (!solid || solid->type != EntityType::Solid3D) return nullptr;
    const std::string& history = solid->as_acis().history_handle;
    if (history.empty() || history == "0") return nullptr;
    const Object* o = find_object(history);
    return o && o->type == ObjectType::ShHistory ? o : nullptr;
}

Object* Drawing::solid_history(const std::string& solid_handle) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->solid_history(solid_handle));
}

Object* Drawing::add_solid_history(const std::string& solid_handle) {
    const Entity* found = find_entity(solid_handle);
    if (!found || found->type != EntityType::Solid3D) return nullptr;
    if (Object* existing = solid_history(solid_handle)) return existing;
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    scan_handles();

    const std::string history_handle = next_handle();
    const std::string graph_handle = next_handle();
    const std::string solid = found->props.handle;
    ShHistoryData history;
    history.graph = graph_handle;
    // The owner of the history is the solid, the owner of the graph the history: AutoCAD's own (no reactors)
    objects_.push_back(make_object(ObjectType::ShHistory, history_handle, solid, Boxed<ShHistoryData>(std::move(history))));
    objects_.push_back(make_object(ObjectType::EvaluationGraph, graph_handle, history_handle, EvaluationGraphData()));
    const_cast<Entity*>(find_entity(solid_handle))->as_acis().history_handle = history_handle;
    return find_object(history_handle);
}

Object* Drawing::add_solid_history_node(const std::string& history_handle, ObjectType type, ObjectData data,
                                        const std::vector<int32_t>& operand_node_ids) {
    if (!is_history_node(type) || !data_matches(type, data)) return nullptr;
    Object* history = find_object(history_handle);
    if (!history || history->type != ObjectType::ShHistory) return nullptr;
    Object* graph = history->as_sh_history().graph.empty() ? nullptr : find_object(history->as_sh_history().graph);
    if (!graph || graph->type != ObjectType::EvaluationGraph) return nullptr;
    const std::string graph_handle = graph->handle;
    const std::string history_key = history->handle;

    const std::string handle = next_handle();
    Object o = make_object(type, handle, graph_handle, std::move(data));
    ShHistoryNodeData* node = node_of(o);
    EvalExprData* eval = eval_of(o);
    if (!node || !eval) return nullptr;

    // The node: the next id, which is the object's own number in its AcDbEvalExpr, and an edge to it from each operand
    EvaluationGraphData& g = graph->as_evaluation_graph();
    const int32_t id = g.add_node(handle);
    const int32_t index = static_cast<int32_t>(g.nodes.size()) - 1;
    eval->node_id = id;
    if (eval->version == 0) eval->version = 33;
    if (eval->maintenance == 0) eval->maintenance = 327;
    for (int32_t operand : operand_node_ids) {
        if (const EvalGraphNode* from = g.find_node_by_id(operand)) g.add_edge(from->index, index);
    }
    history = find_object(history_key);
    history->as_sh_history().last_node_id = g.last_node_id;

    // The material AutoCAD's nodes point at: the drawing's Global one
    if (node->material.empty()) node->material = internal::material_handle(*this, "Global");
    if (node->material == "0") node->material.clear();
    // A primitive is numbered in the order they were made, over the whole drawing (AutoCAD's 92: 1, 2, 3 …)
    const bool primitive = type != ObjectType::ShBoolean && type != ObjectType::ShChamfer && type != ObjectType::ShFillet;
    if (primitive && node->history_id == 0) {
        int32_t highest = 0;
        for (const Object& other : objects_) {
            if (const ShHistoryNodeData* n = node_of(other)) highest = std::max(highest, n->history_id);
        }
        node->history_id = highest + 1;
    }
    objects_.push_back(std::move(o));
    return &objects_.back();
}

Object* Drawing::solid_history_node(const std::string& history_handle, int32_t node_id) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->solid_history_node(history_handle, node_id));
}

const Object* Drawing::solid_history_node(const std::string& history_handle, int32_t node_id) const {
    const Object* history = find_object(history_handle);
    if (!history || history->type != ObjectType::ShHistory) return nullptr;
    const Object* graph = find_object(history->as_sh_history().graph);
    if (!graph || graph->type != ObjectType::EvaluationGraph) return nullptr;
    const EvalGraphNode* node = graph->as_evaluation_graph().find_node_by_id(node_id);
    if (!node || node->object.empty()) return nullptr;
    const Object* o = find_object(node->object);
    return o && is_history_node(o->type) ? o : nullptr;
}

bool Drawing::remove_solid_history_node(const std::string& handle) {
    auto it = std::find_if(objects_.begin(), objects_.end(), [&](const Object& o) { return same_handle(o.handle, handle); });
    if (it == objects_.end() || !is_history_node(it->type)) return false;
    if (Object* graph = find_object(it->owner); graph && graph->type == ObjectType::EvaluationGraph) {
        graph->as_evaluation_graph().remove_object(it->handle);
    }
    objects_.erase(it);
    return true;
}

bool Drawing::remove_solid_history(const std::string& solid_handle) {
    Object* history = solid_history(solid_handle);
    if (!history) return false;
    std::vector<std::string> doomed;
    doomed.push_back(history->handle);
    if (Object* graph = history->as_sh_history().graph.empty() ? nullptr : find_object(history->as_sh_history().graph)) {
        doomed.push_back(graph->handle);
        if (graph->type == ObjectType::EvaluationGraph) {
            for (const EvalGraphNode& n : graph->as_evaluation_graph().nodes) {
                if (!n.object.empty() && n.object != "0") doomed.push_back(n.object);
            }
        }
    }
    objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                  [&](const Object& o) {
                                      return std::any_of(doomed.begin(), doomed.end(),
                                                         [&](const std::string& h) { return same_handle(o.handle, h); });
                                  }),
                   objects_.end());
    const_cast<Entity*>(find_entity(solid_handle))->as_acis().history_handle.clear();
    return true;
}

// ---------------------------------------------------------------------------
// The codecs
// ---------------------------------------------------------------------------

namespace internal {

namespace {

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

void read_part(GroupCursor& c, ShPartVersion& p) {
    c.require_integer(90, p.version);
    c.require_integer(91, p.maintenance);
}

bool read_node(const RecordView& view, TypedReadContext& ctx, EvalExprData& eval, ShHistoryNodeData& node, bool primitive) {
    if (!read_subclass(view, ctx, "AcDbEvalExpr", [&](GroupCursor& c) { read_eval_expr(c, eval); })) return false;
    if (!read_subclass(view, ctx, "AcDbShHistoryNode", [&](GroupCursor& c) {
            c.require_integer(90, node.version);
            c.require_integer(91, node.maintenance);
            for (size_t i = 0; i < node.matrix.size() && c.ok(); ++i) c.require_real(40 + static_cast<int>(i), node.matrix[i]);
            c.require_integer(62, node.color);
            c.require_integer(92, node.history_id);
            c.handle(347, node.material);
            if (node.material == "0") node.material.clear();
        })) {
        return false;
    }
    // AcDbShPrimitive has no group of its own, but its marker is there
    return !primitive || read_subclass(view, ctx, "AcDbShPrimitive", [](GroupCursor&) {});
}

// A source geometry: its class id (`class_code`) and then, when the groups say so, a bit stream (`bits_code`, 310) or SAT
// lines (70, then 1 and 3). A class id with neither is a source with no data. The bits are checked against the bytes
// before the data is kept (B105): the bytes may be more than the bits fill, never fewer
void read_source(GroupCursor& c, int class_code, int bits_code, ShSourceGeometry& out) {
    if (!c.require_integer(class_code, out.class_id)) return;
    if (c.code() == bits_code) {
        ShBitStream s;
        c.integer(bits_code, s.bits);
        if (!c.ok()) return;
        if (s.bits < 0) {
            c.fail("a bit stream has " + std::to_string(s.bits) + " bits");
            return;
        }
        c.bytes(310, s.data);
        const size_t needed = (static_cast<size_t>(s.bits) + 7) / 8;
        if (s.data.size() < needed) {
            c.fail("a bit stream of " + std::to_string(s.bits) + " bits needs " + std::to_string(needed) + " bytes and has " +
                   std::to_string(s.data.size()));
            return;
        }
        out.data = std::move(s);
    } else if (c.code() == 70 && c.peek(1) && c.peek(1)->code == 1) {
        ShSatSource s;
        c.integer(70, s.version);
        std::string pending;
        bool has_pending = false;
        while (c.ok() && (c.code() == 1 || c.code() == 3)) {
            std::string part;
            if (c.code() == 3) {
                c.string(3, part);
                pending += part;
                has_pending = true;
            } else {
                c.string(1, part);
                s.lines.push_back(pending + part);
                pending.clear();
                has_pending = false;
            }
        }
        if (has_pending) s.lines.push_back(pending);
        out.data = std::move(s);
    }
}

bool read_counted_ints(GroupCursor& c, int count_code, int code, std::vector<int32_t>& out) {
    size_t n = 0;
    if (!c.count(count_code, 1, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " is missing");
        return false;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        int32_t v = 0;
        c.require_integer(code, v);
        out.push_back(v);
    }
    return c.ok();
}

bool read_counted_reals(GroupCursor& c, int count_code, int code, std::vector<double>& out) {
    size_t n = 0;
    if (!c.count(count_code, 1, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " is missing");
        return false;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        double v = 0;
        c.require_real(code, v);
        out.push_back(v);
    }
    return c.ok();
}

// The 16 values of a matrix (46 or 47) of AcDbShSweepBase: row by row, all of them
void read_matrix(GroupCursor& c, int code, std::vector<double>& out) {
    out.clear();
    for (int i = 0; i < 16 && c.ok(); ++i) {
        double v = 0;
        c.require_real(code, v);
        out.push_back(v);
    }
}

bool read_history(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShHistoryData d;
    if (!read_subclass(view, ctx, "AcDbShHistory", [&](GroupCursor& c) {
            c.require_integer(90, d.version);
            c.require_integer(91, d.maintenance);
            c.handle(360, d.graph);
            if (d.graph == "0") d.graph.clear();
            c.require_integer(92, d.last_node_id);
            c.require_integer(280, d.flag_280);
            c.require_integer(281, d.flag_281);
        })) {
        return false;
    }
    o.data = Boxed<ShHistoryData>(std::move(d));
    return true;
}

bool read_box(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShBoxData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShBox", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_real(40, d.length);
            c.require_real(41, d.width);
            c.require_real(42, d.height);
        })) {
        return false;
    }
    o.data = Boxed<ShBoxData>(std::move(d));
    return true;
}

bool read_wedge(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShWedgeData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShWedge", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_real(40, d.length);
            c.require_real(41, d.width);
            c.require_real(42, d.height);
        })) {
        return false;
    }
    o.data = Boxed<ShWedgeData>(std::move(d));
    return true;
}

bool read_sphere(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShSphereData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShSphere", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_real(40, d.radius);
        })) {
        return false;
    }
    o.data = Boxed<ShSphereData>(std::move(d));
    return true;
}

bool read_cylinder_body(const RecordView& view, TypedReadContext& ctx, ShCylinderBody& b) {
    return read_subclass(view, ctx, "AcDbShCylinder", [&](GroupCursor& c) {
        read_part(c, b.part);
        c.require_real(40, b.height);
        c.require_real(41, b.base_radius_1);
        c.require_real(42, b.base_radius_2);
        c.require_real(43, b.top_radius);
    });
}

bool read_cylinder(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShCylinderData d;
    if (!read_node(view, ctx, d.eval, d.node, true) || !read_cylinder_body(view, ctx, d.body)) return false;
    o.data = Boxed<ShCylinderData>(std::move(d));
    return true;
}

bool read_cone(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShConeData d;
    if (!read_node(view, ctx, d.eval, d.node, true) || !read_cylinder_body(view, ctx, d.body)) return false;
    // AcDbShCone has no group of its own, but its marker must be there
    if (!read_subclass(view, ctx, "AcDbShCone", [](GroupCursor&) {})) return false;
    o.data = Boxed<ShConeData>(std::move(d));
    return true;
}

bool read_torus(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShTorusData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShTorus", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_real(40, d.major_radius);
            c.require_real(41, d.minor_radius);
        })) {
        return false;
    }
    o.data = Boxed<ShTorusData>(std::move(d));
    return true;
}

bool read_pyramid(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShPyramidData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShPyramid", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_real(40, d.height);
            c.require_integer(92, d.sides);
            c.require_real(41, d.radius);
            c.require_real(42, d.top_radius);
        })) {
        return false;
    }
    o.data = Boxed<ShPyramidData>(std::move(d));
    return true;
}

bool read_sweep_base(const RecordView& view, TypedReadContext& ctx, ShSweepBaseData& b) {
    return read_subclass(view, ctx, "AcDbShSweepBase", [&](GroupCursor& c) {
        read_part(c, b.part);
        c.require_point3(10, b.vector);
        read_source(c, 92, 90, b.profile);
        if (!c.ok()) return;
        read_source(c, 93, 90, b.path);
        SurfaceSweep& w = b.options;
        c.require_real(42, w.draft_angle);
        c.require_real(43, w.draft_start);
        c.require_real(44, w.draft_end);
        c.require_real(45, w.twist);
        c.require_real(48, w.scale);
        c.require_real(49, w.align_angle);
        read_matrix(c, 46, w.sweep_matrix);
        read_matrix(c, 47, w.path_matrix);
        c.require_boolean(290, w.solid);
        c.require_integer(70, w.alignment);
        c.require_integer(71, w.option_71);
        c.require_boolean(292, w.align_start);
        c.require_boolean(293, w.bank);
        c.require_boolean(294, w.base_point_set);
        c.require_boolean(295, w.sweep_transform_computed);
        c.require_boolean(296, w.path_transform_computed);
        c.require_point3(11, w.reference_vector);
    });
}

bool read_extrusion(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShExtrusionData d;
    if (!read_node(view, ctx, d.eval, d.node, true) || !read_sweep_base(view, ctx, d.base)) return false;
    if (!read_subclass(view, ctx, "AcDbShExtrusion", [](GroupCursor&) {})) return false;
    o.data = Boxed<ShExtrusionData>(std::move(d));
    return true;
}

bool read_sweep(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShSweepData d;
    if (!read_node(view, ctx, d.eval, d.node, true) || !read_sweep_base(view, ctx, d.base)) return false;
    if (!read_subclass(view, ctx, "AcDbShSweep", [](GroupCursor&) {})) return false;
    o.data = Boxed<ShSweepData>(std::move(d));
    return true;
}

bool read_revolve(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShRevolveData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShRevolve", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_point3(10, d.axis_point);
            c.require_point3(11, d.axis_vector);
            c.require_real(40, d.angle);
            c.require_real(41, d.start_angle);
            read_source(c, 92, 90, d.profile);
            if (!c.ok()) return;
            c.require_real(43, d.draft_angle);
            c.require_real(44, d.start_draft_distance);
            c.require_real(45, d.end_draft_distance);
            c.require_real(46, d.twist);
            c.require_boolean(290, d.solid);
            c.require_boolean(291, d.close_to_axis);
        })) {
        return false;
    }
    o.data = Boxed<ShRevolveData>(std::move(d));
    return true;
}

// A counted list of sources: the count (`count_code`) and, for each, its class id, its bits and its bytes
void read_sources(GroupCursor& c, int count_code, int class_code, int bits_code, std::vector<ShSourceGeometry>& out) {
    size_t n = 0;
    if (!c.count(count_code, 2, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " is missing");
        return;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        ShSourceGeometry g;
        read_source(c, class_code, bits_code, g);
        out.push_back(std::move(g));
    }
}

bool read_loft(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShLoftData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShLoft", [&](GroupCursor& c) {
            read_part(c, d.part);
            read_sources(c, 92, 93, 94, d.sections);
            if (c.ok()) read_sources(c, 95, 96, 97, d.guides);
            if (!c.ok()) return;
            read_source(c, 98, 99, d.path);
            if (!c.ok()) return;
            c.require_integer(70, d.plane_normal_lofting);
            c.require_real(41, d.start_draft_angle);
            c.require_real(42, d.end_draft_angle);
            c.require_real(43, d.start_draft_magnitude);
            c.require_real(44, d.end_draft_magnitude);
            c.require_boolean(290, d.arc_length_parameters);
            c.require_boolean(291, d.no_twist);
            c.require_boolean(292, d.align_direction);
            c.require_boolean(293, d.simple_surfaces);
            c.require_boolean(294, d.closed);
            c.require_boolean(295, d.solid);
            c.require_boolean(296, d.ruled);
            c.require_boolean(297, d.virtual_guide);
        })) {
        return false;
    }
    o.data = Boxed<ShLoftData>(std::move(d));
    return true;
}

bool read_boolean(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShBooleanData d;
    if (!read_node(view, ctx, d.eval, d.node, false)) return false;
    if (!read_subclass(view, ctx, "AcDbShBoolean", [&](GroupCursor& c) {
            read_part(c, d.part);
            int16_t operation = 0;
            c.require_integer(280, operation);
            if (c.ok() && (operation < 0 || operation > 2)) {
                c.fail("the Boolean operation " + std::to_string(operation) +
                       " is none of AutoCAD's 0 (union), 1 (intersect) and 2 (subtract)");
                return;
            }
            d.operation = static_cast<ShBooleanOperation>(operation);
            c.require_integer(92, d.first_node_id);
            c.require_integer(93, d.second_node_id);
        })) {
        return false;
    }
    o.data = Boxed<ShBooleanData>(std::move(d));
    return true;
}

bool read_brep(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShBrepData d;
    if (!read_node(view, ctx, d.eval, d.node, true)) return false;
    if (!read_subclass(view, ctx, "AcDbShBrep", [&](GroupCursor& c) {
            read_part(c, d.part);
            // 70 and the SAT lines (the only form AutoCAD's files have)
            if (!(c.code() == 70 && c.peek(1) && c.peek(1)->code == 1)) {
                c.fail("the body has no SAT lines (70, 1 …)");
                return;
            }
            c.integer(70, d.sat.version);
            std::string pending;
            bool has_pending = false;
            while (c.ok() && (c.code() == 1 || c.code() == 3)) {
                std::string part;
                if (c.code() == 3) {
                    c.string(3, part);
                    pending += part;
                    has_pending = true;
                } else {
                    c.string(1, part);
                    d.sat.lines.push_back(pending + part);
                    pending.clear();
                    has_pending = false;
                }
            }
            if (has_pending) d.sat.lines.push_back(pending);
        })) {
        return false;
    }
    o.data = Boxed<ShBrepData>(std::move(d));
    return true;
}

bool read_chamfer(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShChamferData d;
    if (!read_node(view, ctx, d.eval, d.node, false)) return false;
    if (!read_subclass(view, ctx, "AcDbShChamfer", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_integer(92, d.base_node_id);
            c.require_real(41, d.distance_1);
            c.require_real(42, d.distance_2);
            if (!read_counted_ints(c, 93, 94, d.edge_ids)) return;
            c.require_integer(95, d.face_id);
        })) {
        return false;
    }
    o.data = Boxed<ShChamferData>(std::move(d));
    return true;
}

bool read_fillet(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ShFilletData d;
    if (!read_node(view, ctx, d.eval, d.node, false)) return false;
    if (!read_subclass(view, ctx, "AcDbShFillet", [&](GroupCursor& c) {
            read_part(c, d.part);
            c.require_integer(92, d.base_node_id);
            std::vector<int32_t> ids;
            std::vector<double> radii, values_42, values_43;
            if (!read_counted_ints(c, 93, 94, ids) || !read_counted_reals(c, 95, 41, radii) ||
                !read_counted_reals(c, 96, 42, values_42) || !read_counted_reals(c, 97, 43, values_43)) {
                return;
            }
            if (radii.size() != ids.size() || values_42.size() != ids.size() || values_43.size() != ids.size()) {
                c.fail("the four lists of a fillet (93, 95, 96, 97) have counts " + std::to_string(ids.size()) + ", " +
                       std::to_string(radii.size()) + ", " + std::to_string(values_42.size()) + " and " +
                       std::to_string(values_43.size()) + ": AutoCAD's files have the same count in all four");
                return;
            }
            for (size_t i = 0; i < ids.size(); ++i) d.edges.push_back({ids[i], radii[i], values_42[i], values_43[i]});
        })) {
        return false;
    }
    o.data = Boxed<ShFilletData>(std::move(d));
    return true;
}

// ---------------------------------------------------------------------------
// References (B19, B104): the history owns its graph (360); a node's material is a hard pointer (347). The node ids of a
// Boolean (92, 93), a chamfer's and a fillet's base node (92) and the edge and face ids are numbers inside the graph and the
// solid's persistent subentity ids, never handles: they are not references and nothing clears them
// ---------------------------------------------------------------------------

void history_references(const Object& o, std::vector<TypedReference>& out) {
    const std::string& graph = o.as_sh_history().graph;
    if (!graph.empty() && graph != "0") out.push_back({graph, RefKind::HardOwner, false});
}

template <typename Data>
void node_references(const Object& o, std::vector<TypedReference>& out) {
    const std::string& material = (*o.data.get<Boxed<Data>>()).node.material;
    if (!material.empty() && material != "0") out.push_back({material, RefKind::HardPointer, false});
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

ClassSpec sh_class(const char* cpp_name) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 1153;
    c.in_older = false;
    return c;
}

// AutoCAD's files have the history from 2007 (SOLIDHIST arrived with it, and the 3DSOLID's 350): 2000 and 2004 files
// saved by AutoCAD hold no history, so a record built in memory is the named error there; ASCII and binary of 2007 …
// 2018 are AutoCAD-verified (doc/facit/fas9_history.scr), R13 and R14 cannot be saved by AutoCAD 2025
VersionMatrix sh_matrix() {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2007);
    for (DxfVersion v : {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

ListSpec whole(const char* subclass) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

RefDescriptor material_ref() {
    RefDescriptor d;
    d.subclass = "AcDbShHistoryNode";
    d.code = 347;
    d.kind = RefKind::HardPointer;
    d.removal = RefRemoval::ZeroInPlace;
    return d;
}

template <typename Data>
void add_node_codec(TypedRegistry& registry, const char* dxf_name, ObjectType type, ObjectReadFn read, ObjectWriteFn write,
                    const char* cpp_name, std::vector<ListSpec> lists = {}) {
    ObjectCodec c;
    c.dxf_name = dxf_name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.references = node_references<Data>;
    c.cls = sh_class(cpp_name);
    c.versions = sh_matrix();
    c.refs = {material_ref()};
    c.lists = std::move(lists);
    c.step = 14;
    registry.add(std::move(c));
}

} // namespace

void register_solid_history_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "ACSH_HISTORY_CLASS";
        c.type = ObjectType::ShHistory;
        c.read = read_history;
        c.write = write_sh_history;
        c.references = history_references;
        c.cls = sh_class("AcDbShHistory");
        c.versions = sh_matrix();
        // The hard pointer to the history's graph: 0 when that graph is gone
        RefDescriptor graph;
        graph.subclass = "AcDbShHistory";
        graph.code = 360;
        graph.kind = RefKind::HardOwner;
        graph.removal = RefRemoval::ZeroInPlace;
        c.refs = {graph};
        c.step = 14;
        registry.add(std::move(c));
    }
    add_node_codec<ShBoxData>(registry, "ACSH_BOX_CLASS", ObjectType::ShBox, read_box, write_sh_box, "AcDbShBox");
    add_node_codec<ShWedgeData>(registry, "ACSH_WEDGE_CLASS", ObjectType::ShWedge, read_wedge, write_sh_wedge, "AcDbShWedge");
    add_node_codec<ShSphereData>(registry, "ACSH_SPHERE_CLASS", ObjectType::ShSphere, read_sphere, write_sh_sphere,
                                 "AcDbShSphere");
    add_node_codec<ShCylinderData>(registry, "ACSH_CYLINDER_CLASS", ObjectType::ShCylinder, read_cylinder, write_sh_cylinder,
                                   "AcDbShCylinder");
    add_node_codec<ShConeData>(registry, "ACSH_CONE_CLASS", ObjectType::ShCone, read_cone, write_sh_cone, "AcDbShCone");
    add_node_codec<ShTorusData>(registry, "ACSH_TORUS_CLASS", ObjectType::ShTorus, read_torus, write_sh_torus, "AcDbShTorus");
    add_node_codec<ShPyramidData>(registry, "ACSH_PYRAMID_CLASS", ObjectType::ShPyramid, read_pyramid, write_sh_pyramid,
                                  "AcDbShPyramid");
    // A source that changes form (SAT lines, bit stream, none) changes the groups around it: the subclass is one list
    add_node_codec<ShExtrusionData>(registry, "ACSH_EXTRUSION_CLASS", ObjectType::ShExtrusion, read_extrusion,
                                    write_sh_extrusion, "AcDbShExtrusion", {whole("AcDbShSweepBase")});
    add_node_codec<ShSweepData>(registry, "ACSH_SWEEP_CLASS", ObjectType::ShSweep, read_sweep, write_sh_sweep, "AcDbShSweep",
                                {whole("AcDbShSweepBase")});
    add_node_codec<ShRevolveData>(registry, "ACSH_REVOLVE_CLASS", ObjectType::ShRevolve, read_revolve, write_sh_revolve,
                                  "AcDbShRevolve", {whole("AcDbShRevolve")});
    add_node_codec<ShLoftData>(registry, "ACSH_LOFT_CLASS", ObjectType::ShLoft, read_loft, write_sh_loft, "AcDbShLoft",
                               {whole("AcDbShLoft")});
    add_node_codec<ShBooleanData>(registry, "ACSH_BOOLEAN_CLASS", ObjectType::ShBoolean, read_boolean, write_sh_boolean,
                                  "AcDbShBoolean");
    add_node_codec<ShBrepData>(registry, "ACSH_BREP_CLASS", ObjectType::ShBrep, read_brep, write_sh_brep, "AcDbShBrep",
                               {whole("AcDbShBrep")});
    add_node_codec<ShChamferData>(registry, "ACSH_CHAMFER_CLASS", ObjectType::ShChamfer, read_chamfer, write_sh_chamfer,
                                  "AcDbShChamfer", {whole("AcDbShChamfer")});
    add_node_codec<ShFilletData>(registry, "ACSH_FILLET_CLASS", ObjectType::ShFillet, read_fillet, write_sh_fillet,
                                 "AcDbShFillet", {whole("AcDbShFillet")});
}

} // namespace internal
} // namespace hdxf
