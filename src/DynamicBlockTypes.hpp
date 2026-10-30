#pragma once

#include "TypedRecordTypes.hpp"
#include <functional>
#include <string>

// The bases of the dynamic block elements (phase 9, step 9.9; plan 2026-10-27-fas-9-typer.md, B109): the functions
// that read and write the subclasses the elements share, and the pieces of the family's registration. Internal.
//
// Step 9.13 builds the other parameters, grips and actions of the Block Editor from these. A type reads its subclasses
// in AutoCAD's order with read_subclass(), one lambda per subclass that calls the base functions below; it writes them with
// RecordWriter::subclass() and the matching write_* function. The data types are in hdxf.hpp (BlockElementData …); a type
// that holds an AcDbEvalExpr has it as `eval`.

namespace hdxf {
namespace internal {

// "<TYPE> <handle>", the start of a failure or warning
std::string describe_record(const RecordView& view);

// Reads the subclass `name` of the record with `read`: a record without it fails; a failure of the cursor fails the
// record; groups after the ones `read` took are kept as they are in the preserved record, with a warning (B105)
bool read_subclass(const RecordView& view, TypedReadContext& ctx, const char* name,
                   const std::function<void(GroupCursor&)>& read);

// ---------------------------------------------------------------------------
// The shared subclasses: read from a cursor on the subclass, written after its marker
// ---------------------------------------------------------------------------

// AcDbEvalExpr is read_eval_expr()/write_eval_expr() (TypedRecordTypes.hpp)

// AcDbBlockElement: 300, 98, 99, 1071
void read_block_element(GroupCursor& c, BlockElementData& d);
void write_block_element(RecordWriter& w, const BlockElementData& d);

// AcDbBlockParameter: 280, 281
void read_block_parameter(GroupCursor& c, BlockParameterData& d);
void write_block_parameter(RecordWriter& w, const BlockParameterData& d);

// AcDbBlock1PtParameter: 1010, 93, then two slots (flag 170/171, id 91/92, name 301/302)
void read_block_1pt_parameter(GroupCursor& c, Block1PtParameterData& d);
void write_block_1pt_parameter(RecordWriter& w, const Block1PtParameterData& d);

// AcDbBlock2PtParameter: 1010, 1011, 170 (the number of 91), 91, four slots (flag 171 … 174, id 92 … 95, name 301 … 304), 177
void read_block_2pt_parameter(GroupCursor& c, Block2PtParameterData& d);
void write_block_2pt_parameter(RecordWriter& w, const Block2PtParameterData& d);

// AcDbBlockGrip: 91, 92, 1010, 280, 93
void read_block_grip(GroupCursor& c, BlockGripData& d);
void write_block_grip(RecordWriter& w, const BlockGripData& d);

// AcDbBlockAction: 70, 71 (the number of 330), 330, 1010. `selection` is written without the handles of removed entities
void read_block_action(GroupCursor& c, BlockActionData& d);
void write_block_action(RecordWriter& w, const BlockActionData& d);

// AcDbBlockActionWithBasePt: 92, 93, 301, 302, 1011, 280, 1012
void read_block_action_base_point(GroupCursor& c, BlockActionBasePointData& d);
void write_block_action_base_point(RecordWriter& w, const BlockActionBasePointData& d);

// The 331/332/333 members of a visibility state or of an action's selection that are still there: a handle of a record
// that was removed is left out (B19) while the drawing is written; a record that leads nowhere already in the file stays
std::vector<std::string> members_still_there(const RecordWriter& w, const std::vector<std::string>& handles);
// Whether a pointer is kept: false for a record that is gone (in a drawing built in memory the record is looked up)
bool pointer_alive(const RecordWriter& w, const std::string& handle);

// The references of the shared parts: an action's selection (330, soft pointers)
void add_action_references(const BlockActionData& d, std::vector<TypedReference>& out);

// The lists of an action's AcDbBlockAction subclass: 70 with its 91 (the nodes) and 71 with its 330 (the selection), two lists
std::vector<ListSpec> block_action_lists();

// The relation descriptor of a counted list of soft pointers that drops a member (and counts it) when its target is gone
RefDescriptor counted_soft_pointer(const char* subclass, int code, int counter_code);

// A cell of the version matrix of every dynamic block element type: 2000 on (AutoCAD's files of 2000 … 2018 have each of
// them, ASCII and binary), a named error before
VersionMatrix dynamic_block_matrix();

// The class data of an element's matrix and class for CLASSES: ObjectDBX Classes, proxy flags 1153
ClassSpec dynamic_block_class(const char* cpp_name);

// ---------------------------------------------------------------------------
// The codecs of the step (evaluation_graph.cpp and dynamic_block_base.cpp), registered by
// register_graph_block_codecs() in typed_families.cpp
// ---------------------------------------------------------------------------

void register_evaluation_graph_codec(TypedRegistry& registry);
void register_dynamic_block_base_codecs(TypedRegistry& registry);

// What the codecs write (dxf_writer_graph_block.cpp)
void write_evaluation_graph(RecordWriter& w, const Object& o);
void write_block_grip_location(RecordWriter& w, const Object& o);
void write_block_linear_parameter(RecordWriter& w, const Object& o);
void write_block_linear_grip(RecordWriter& w, const Object& o);
void write_block_visibility_parameter(RecordWriter& w, const Object& o);
void write_block_visibility_grip(RecordWriter& w, const Object& o);
void write_block_scale_action(RecordWriter& w, const Object& o);
void write_block_representation(RecordWriter& w, const Object& o);
void write_dynamic_block_purge_preventer(RecordWriter& w, const Object& o);

// The graph as it is written now: nodes whose element is gone are taken out with their edges (B19)
EvaluationGraphData live_graph(const RecordWriter& w, const EvaluationGraphData& graph);

} // namespace internal
} // namespace hdxf
