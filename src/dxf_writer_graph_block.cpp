#include "DynamicBlockTypes.hpp"
#include "dxf_writer.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

// The writer of the step 9.9 records (phase 9; plan 2026-10-27-fas-9-typer.md, B109; B19, B103, B104): every record is
// written whole from its typed values, in the order of AutoCAD's files (see dynamic_block_base.cpp and
// evaluation_graph.cpp), with its head and, last, its XDATA. The writer is also what the B12 merge compares: the record as
// read (all pointers as they were) against the record now (pointers to records that are gone left out, with the counts that
// belong to them), so a record that did not change is written as the file had it.

namespace hdxf {
namespace internal {

namespace {

void write_slot(RecordWriter& w, int flag_code, int id_code, int name_code, const std::optional<BlockPropertyLink>& slot) {
    w.integer(flag_code, slot ? 1 : 0);
    if (!slot) return;
    w.integer(id_code, slot->node_id);
    w.text(name_code, slot->name);
}

// A list of handles counted by `count_code` and written with `code`; the members whose target is gone are not written and
// are not counted (B19)
void write_handles(RecordWriter& w, int count_code, int code, const std::vector<std::string>& handles) {
    const std::vector<std::string> live = members_still_there(w, handles);
    if (live.size() != handles.size()) {
        w.warn(std::to_string(handles.size() - live.size()) + " pointer(s) of group " + std::to_string(code) +
               " lead to records that no longer exist and are left out");
    }
    w.integer(count_code, static_cast<int64_t>(live.size()));
    for (const std::string& h : live) w.text(code, h);
}

} // namespace

EvaluationGraphData live_graph(const RecordWriter& w, const EvaluationGraphData& graph) {
    EvaluationGraphData out = graph;
    if (!w.filtering()) return out;
    for (size_t i = out.nodes.size(); i-- > 0;) {
        if (pointer_alive(w, out.nodes[i].object)) continue;
        out.remove_node(static_cast<int32_t>(i));
    }
    return out;
}

void write_block_element(RecordWriter& w, const BlockElementData& d) {
    // What AutoCAD writes after the DXF version it saves (2000 and 2004: 25 and 104; 2007 and later: 33 and 327)
    const bool early = w.version() < DxfVersion::R2007;
    w.text(300, d.name);
    w.integer(98, d.version ? *d.version : (early ? 25 : 33));
    w.integer(99, d.maintenance ? *d.maintenance : (early ? 104 : 327));
    w.integer(1071, d.flags);
}

void write_block_parameter(RecordWriter& w, const BlockParameterData& d) {
    w.integer(280, d.show_properties);
    w.integer(281, d.chain_actions);
}

void write_block_1pt_parameter(RecordWriter& w, const Block1PtParameterData& d) {
    w.point3(1010, d.position);
    w.integer(93, d.grip_node_id);
    write_slot(w, 170, 91, 301, d.links[0]);
    write_slot(w, 171, 92, 302, d.links[1]);
}

void write_block_2pt_parameter(RecordWriter& w, const Block2PtParameterData& d) {
    w.point3(1010, d.first);
    w.point3(1011, d.second);
    w.integer(170, static_cast<int64_t>(d.grip_node_ids.size()));
    for (int32_t id : d.grip_node_ids) w.integer(91, id);
    for (int k = 0; k < 4; ++k) write_slot(w, 171 + k, 92 + k, 301 + k, d.links[static_cast<size_t>(k)]);
    w.integer(177, d.flag_177);
}

void write_block_grip(RecordWriter& w, const BlockGripData& d) {
    w.integer(91, d.x_node_id);
    w.integer(92, d.y_node_id);
    w.point3(1010, d.location);
    w.integer(280, d.flag_280);
    w.integer(93, d.value_93);
}

void write_block_action(RecordWriter& w, const BlockActionData& d) {
    w.integer(70, static_cast<int64_t>(d.node_ids.size()));
    for (int32_t id : d.node_ids) w.integer(91, id);
    write_handles(w, 71, 330, d.selection);
    w.point3(1010, d.location);
}

void write_block_action_base_point(RecordWriter& w, const BlockActionBasePointData& d) {
    w.integer(92, d.x_node_id);
    w.integer(93, d.y_node_id);
    w.text(301, d.x_name);
    w.text(302, d.y_name);
    w.point3(1011, d.base_point);
    w.integer(280, d.flag_280);
    w.point3(1012, d.point_1012);
}

void write_evaluation_graph(RecordWriter& w, const Object& o) {
    const EvaluationGraphData& as_held = o.as_evaluation_graph();
    const EvaluationGraphData graph = live_graph(w, as_held);
    if (graph.nodes.size() != as_held.nodes.size()) {
        w.warn("ACAD_EVALUATION_GRAPH " + o.handle + ": " + std::to_string(as_held.nodes.size() - graph.nodes.size()) +
               " node(s) of elements that no longer exist were removed with their edges");
    }
    w.object_head("ACAD_EVALUATION_GRAPH", o);
    w.subclass("AcDbEvalGraph");
    w.integer(96, graph.last_node_id);
    w.integer(97, graph.last_node_id_2);
    for (const EvalGraphNode& n : graph.nodes) {
        w.integer(91, n.index);
        w.integer(93, n.flags);
        w.integer(95, n.id);
        w.text(360, n.object.empty() ? std::string("0") : n.object);
        w.integer(92, n.incoming_first);
        w.integer(92, n.incoming_last);
        w.integer(92, n.outgoing_first);
        w.integer(92, n.outgoing_last);
    }
    for (const EvalGraphEdge& e : graph.edges) {
        w.integer(92, e.index);
        w.integer(93, e.flags);
        w.integer(94, e.ref_count);
        w.integer(91, e.from);
        w.integer(91, e.to);
        w.integer(92, e.previous_incoming);
        w.integer(92, e.next_incoming);
        w.integer(92, e.previous_outgoing);
        w.integer(92, e.next_outgoing);
        w.integer(92, e.inverse);
    }
    w.xdata(o.xdata);
}

void write_block_grip_location(RecordWriter& w, const Object& o) {
    const BlockGripLocationData& d = o.as_block_grip_location();
    w.object_head("BLOCKGRIPLOCATIONCOMPONENT", o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, d.eval);
    w.subclass("AcDbBlockGripExpr");
    w.integer(91, d.parameter_node_id);
    w.text(300, d.property_name);
    w.xdata(o.xdata);
}

void write_block_linear_parameter(RecordWriter& w, const Object& o) {
    const BlockLinearParameterData& d = o.as_block_linear_parameter();
    w.object_head("BLOCKLINEARPARAMETER", o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, d.eval);
    w.subclass("AcDbBlockElement");
    write_block_element(w, d.element);
    w.subclass("AcDbBlockParameter");
    write_block_parameter(w, d.parameter);
    w.subclass("AcDbBlock2PtParameter");
    write_block_2pt_parameter(w, d.two_point);
    w.subclass("AcDbBlockLinearParameter");
    w.text(305, d.label);
    w.text(306, d.description);
    w.real(140, d.label_offset);
    w.text(307, d.expression);
    w.integer(96, d.value_set_flags);
    w.real(141, d.constraints[0]);
    w.real(142, d.constraints[1]);
    w.real(143, d.constraints[2]);
    w.integer(175, static_cast<int64_t>(d.values.size()));
    for (double value : d.values) w.real(144, value);
    w.xdata(o.xdata);
}

void write_block_linear_grip(RecordWriter& w, const Object& o) {
    const BlockLinearGripData& d = o.as_block_linear_grip();
    w.object_head("BLOCKLINEARGRIP", o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, d.eval);
    w.subclass("AcDbBlockElement");
    write_block_element(w, d.element);
    w.subclass("AcDbBlockGrip");
    write_block_grip(w, d.grip);
    w.subclass("AcDbBlockLinearGrip");
    w.real(140, d.vector.x);
    w.real(141, d.vector.y);
    w.real(142, d.vector.z);
    w.xdata(o.xdata);
}

void write_block_visibility_parameter(RecordWriter& w, const Object& o) {
    const BlockVisibilityParameterData& d = o.as_block_visibility_parameter();
    w.object_head("BLOCKVISIBILITYPARAMETER", o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, d.eval);
    w.subclass("AcDbBlockElement");
    write_block_element(w, d.element);
    w.subclass("AcDbBlockParameter");
    write_block_parameter(w, d.parameter);
    w.subclass("AcDbBlock1PtParameter");
    write_block_1pt_parameter(w, d.one_point);
    w.subclass("AcDbBlockVisibilityParameter");
    w.integer(281, d.flag_281);
    w.text(301, d.name);
    w.text(302, d.description);
    w.integer(91, d.value_91);
    write_handles(w, 93, 331, d.entities);
    w.integer(92, static_cast<int64_t>(d.states.size()));
    for (const BlockVisibilityState& s : d.states) {
        w.text(303, s.name);
        write_handles(w, 94, 332, s.entities);
        write_handles(w, 95, 333, s.elements);
    }
    w.xdata(o.xdata);
}

void write_block_visibility_grip(RecordWriter& w, const Object& o) {
    const BlockVisibilityGripData& d = o.as_block_visibility_grip();
    w.object_head("BLOCKVISIBILITYGRIP", o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, d.eval);
    w.subclass("AcDbBlockElement");
    write_block_element(w, d.element);
    w.subclass("AcDbBlockGrip");
    write_block_grip(w, d.grip);
    w.subclass("AcDbBlockVisibilityGrip");
    w.xdata(o.xdata);
}

void write_block_scale_action(RecordWriter& w, const Object& o) {
    const BlockScaleActionData& d = o.as_block_scale_action();
    w.object_head("BLOCKSCALEACTION", o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, d.eval);
    w.subclass("AcDbBlockElement");
    write_block_element(w, d.element);
    w.subclass("AcDbBlockAction");
    write_block_action(w, d.action);
    w.subclass("AcDbBlockActionWithBasePt");
    write_block_action_base_point(w, d.base_point_action);
    w.subclass("AcDbBlockScaleAction");
    for (int k = 0; k < 3; ++k) w.integer(94 + k, d.properties[static_cast<size_t>(k)].node_id);
    for (int k = 0; k < 3; ++k) w.text(303 + k, d.properties[static_cast<size_t>(k)].name);
    w.xdata(o.xdata);
}

void write_block_representation(RecordWriter& w, const Object& o) {
    const BlockRepresentationData& d = o.as_block_representation();
    w.object_head("ACDB_BLOCKREPRESENTATION_DATA", o);
    w.subclass("AcDbBlockRepresentationData");
    w.integer(70, d.version);
    // A hard pointer to the original block's BLOCK_RECORD: 0 when that record is gone (B19)
    const std::string& original = d.original_block_record;
    if (pointer_alive(w, original)) {
        w.text(340, original.empty() ? std::string("0") : original);
    } else {
        w.warn("ACDB_BLOCKREPRESENTATION_DATA " + o.handle + ": the original block record " + original +
               " no longer exists, set to none (0)");
        w.text(340, "0");
    }
    w.xdata(o.xdata);
}

void write_dynamic_block_purge_preventer(RecordWriter& w, const Object& o) {
    w.object_head("ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION", o);
    w.subclass("AcDbDynamicBlockPurgePreventer");
    w.integer(70, o.as_dynamic_block_purge_preventer().version);
    w.xdata(o.xdata);
}

// A dynamic block definition built in memory and written before DXF 2010 is, in AutoCAD's own files of 2000, 2004 and 2007
// (doc/facit/acad_<version>_dxf_dynbase.dxf), the anonymous block *U<n> — BLOCK flag 1, BLOCK_RECORD and BLOCK named *U<n>,
// the INSERTs naming it — with its real name in the XDATA AcDbDynamicBlockTrueName; from 2010 it has its own name. The writer
// does this to the names it writes (the drawing keeps the name). A drawing read from a file keeps what it had: it is
// written only as the version it came from.
void DxfWriter::prepare_dynamic_block_names(const Drawing& dwg) {
    dynamic_block_names_.clear();
    if (version_ < DxfVersion::R2000 || version_ >= DxfVersion::R2010 || template_mode()) return;
    auto upper = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    size_t number = 0;
    for (const Block& b : dwg.blocks()) {
        if (b.name.size() > 2 && b.name[0] == '*' && (b.name[1] == 'U' || b.name[1] == 'u')) {
            number = std::max(number, static_cast<size_t>(std::atoll(b.name.c_str() + 2)));
        }
    }
    for (const Block& b : dwg.blocks()) {
        if (b.name.empty() || b.name[0] == '*' || !dwg.dynamic_block_graph(b.name)) continue;
        dynamic_block_names_[upper(b.name)] = "*U" + std::to_string(++number);
    }
}

} // namespace internal
} // namespace hdxf
