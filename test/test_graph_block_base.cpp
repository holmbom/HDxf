#include "test_framework.hpp"
#include "hdxf.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "dynamic_block_helpers.hpp"

// ---------------------------------------------------------------------------
// Step 9.9 of phase 9 (plan 2026-10-27-fas-9-typer.md; B100-B105, B109, B113-B116): the evaluation graph
// (ACAD_EVALUATION_GRAPH) and the bases of the dynamic block elements — BLOCKGRIPLOCATIONCOMPONENT, BLOCKLINEARPARAMETER,
// BLOCKLINEARGRIP, BLOCKVISIBILITYPARAMETER, BLOCKVISIBILITYGRIP, BLOCKSCALEACTION, ACDB_BLOCKREPRESENTATION_DATA and
// ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION.
//
// The AutoCAD originals: doc/facit/acad_<version>_dxf_dynbase*.dxf (fas9_dynamic_base.scr: AutoCAD 2025's core opens Erik's
// Block Editor drawing, doc/facit/acad_2018_dxf_dynamic_blocks.dxf, and saves it in R12 … 2018, ASCII and binary), the
// dynamic block drawings of script 16 (acad_<version>_dxf_dynamic_blocks*.dxf), ACadSharp's sample_AC1015 … AC1032 (the
// only AutoCAD files with BLOCKSCALEACTION) and the lookup block of doc/facit/web. They are local files (the facit is not in
// git): a test whose file is missing is skipped.
// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// What AutoCAD's own files hold
// ---------------------------------------------------------------------------

TEST(graph_block_base_reads_autocads_dynamic_block_in_every_version_and_format) {
    for (const char* tag : kTags) {
        for (bool binary : {false, true}) {
            const std::string name = dynbase(tag, binary);
            std::vector<uint8_t> in = facit_bytes(name);
            Drawing d = load(in, name);
            const std::string what = name + ": ";
            assert_true(d.warnings().empty(), what + "no warning on reading");
            for (const Object& o : d.objects()) {
                if (o.type != ObjectType::Unknown) continue;
                const std::string& n = o.as_unknown().object_type;
                for (const std::string& typed : kRecordNames) assert_true(n != typed, what + n + " " + o.handle + " is not typed");
            }

            // The graph: 30 nodes and 23 edges; 96 and 97 are the highest node id
            const Object* graph_object = the_object(d, ObjectType::EvaluationGraph);
            const EvaluationGraphData& g = graph_object->as_evaluation_graph();
            assert_eq(30, static_cast<int>(g.nodes.size()), what + "nodes");
            assert_eq(23, static_cast<int>(g.edges.size()), what + "edges");
            assert_eq(30, g.last_node_id, what + "96");
            assert_eq(30, g.last_node_id_2, what + "97");
            assert_true(g.links_consistent(), what + "the chains are what the edges' order gives");
            for (size_t i = 0; i < g.nodes.size(); ++i) {
                assert_eq(static_cast<int>(i), g.nodes[i].index, what + "a node's 91 is its place");
                assert_eq(static_cast<int>(i) + 1, g.nodes[i].id, what + "a node's id");
                assert_eq(32, g.nodes[i].flags, what + "93");
            }
            // node 1: the edge into it is 0, the first and last edge out of it 1 and 2 (edge 0 is from node 2 to node 1)
            assert_eq(0, g.nodes[1].incoming_first, what + "node 1 in first");
            assert_eq(0, g.nodes[1].incoming_last, what + "node 1 in last");
            assert_eq(1, g.nodes[1].outgoing_first, what + "node 1 out first");
            assert_eq(2, g.nodes[1].outgoing_last, what + "node 1 out last");
            assert_eq(2, g.edges[0].from, what + "edge 0 from");
            assert_eq(1, g.edges[0].to, what + "edge 0 to");
            assert_eq(1, g.edges[0].ref_count, what + "edge 0 94");
            assert_eq(2, g.edges[5].ref_count, what + "edge 5 94");
            assert_eq(6, g.edges[5].from, what + "edge 5 from");
            // the edges' chains: edge 1 is followed by edge 2 among the edges that leave node 1
            assert_eq(-1, g.edges[1].previous_outgoing, what + "edge 1 previous");
            assert_eq(2, g.edges[1].next_outgoing, what + "edge 1 next");
            assert_eq(1, g.edges[2].previous_outgoing, what + "edge 2 previous");
            // every node's object is an element of the drawing whose owner is the graph and whose id is the node's id
            for (const EvalGraphNode& n : g.nodes) {
                const Object* element = d.find_object(n.object);
                assert_true(element != nullptr, what + "the element of node " + std::to_string(n.id));
                assert_eq(upper(graph_object->handle), upper(element->owner), what + "the graph owns its elements (330)");
            }

            // the elements: AcDbEvalExpr's 90 is the node's id
            const Object* linear = the_object(d, ObjectType::BlockLinearParameter);
            const hdxf::BlockLinearParameterData& p = linear->as_block_linear_parameter();
            assert_eq(6, p.eval.node_id, what + "the linear parameter's node id");
            assert_eq(33, p.eval.version, what + "AcDbEvalExpr 98");
            assert_eq(327, p.eval.maintenance, what + "AcDbEvalExpr 99");
            assert_true(!p.eval.expression.has_value(), what + "no expression");
            assert_eq(std::string("Linear"), p.element.name, what + "300");
            assert_true(p.element.version.has_value() && p.element.maintenance.has_value(), what + "98 and 99 as read");
            const bool early = std::string(tag) == "2000" || std::string(tag) == "2004";
            assert_eq(early ? 25 : 33, *p.element.version, what + "the element's 98 follows the DXF version");
            assert_eq(early ? 104 : 327, *p.element.maintenance, what + "the element's 99 follows the DXF version");
            assert_eq(1, p.parameter.show_properties, what + "280");
            assert_eq(0, p.parameter.chain_actions, what + "281");
            assert_eq(0.0, p.two_point.first.x, what + "1010");
            assert_eq(40.0, p.two_point.second.x, what + "1011");
            assert_eq(4, static_cast<int>(p.two_point.grip_node_ids.size()), what + "170 and its four 91");
            assert_eq(7, p.two_point.grip_node_ids[1], what + "the end grip's node id");
            assert_true(!p.two_point.links[0] && !p.two_point.links[1], what + "the first two property slots are empty (flag 0)");
            assert_true(p.two_point.links[2] && p.two_point.links[3], what + "the last two are linked");
            assert_eq(7, p.two_point.links[2]->node_id, what + "94");
            assert_eq(std::string("DisplacementX"), p.two_point.links[2]->name, what + "303");
            assert_eq(std::string("DisplacementY"), p.two_point.links[3]->name, what + "304");
            assert_eq(std::string("HDXF_Length"), p.label, what + "305");
            assert_eq(-10.0, p.label_offset, what + "140");
            assert_eq(1, p.value_set_flags, what + "96");

            const Object* grip = the_object(d, ObjectType::BlockLinearGrip);
            const hdxf::BlockLinearGripData& lg = grip->as_block_linear_grip();
            assert_eq(7, lg.eval.node_id, what + "the linear grip's node id");
            assert_eq(std::string("End Grip"), lg.element.name, what + "grip name");
            assert_eq(8, lg.grip.x_node_id, what + "its X component's node id");
            assert_eq(9, lg.grip.y_node_id, what + "its Y component's node id");
            assert_eq(40.0, lg.grip.location.x, what + "1010 (class data before the XDATA)");
            assert_eq(1, lg.grip.flag_280, what + "grip 280");
            assert_eq(-1, lg.grip.value_93, what + "grip 93");
            assert_eq(40.0, lg.vector.x, what + "140");
            // the components the grip's ids name hold the coordinates the graph computes
            const EvalGraphNode* x_node = g.find_node_by_id(lg.grip.x_node_id);
            assert_true(x_node != nullptr, what + "node of the X component");
            const Object* x_component = d.find_object(x_node->object);
            assert_true(x_component && x_component->type == ObjectType::BlockGripLocation, what + "a grip location component");
            assert_eq(std::string("UpdatedEndX"), x_component->as_block_grip_location().property_name, what + "300");
            assert_eq(6, x_component->as_block_grip_location().parameter_node_id, what + "91 names the parameter");

            // the 13 grip location components: real values (tag 40, one of them AutoCAD's 1.797693134862314E+99) and a flip state
            std::vector<const Object*> locations = objects_of(d, ObjectType::BlockGripLocation);
            assert_eq(13, static_cast<int>(locations.size()), what + "grip location components");
            int reals = 0, ints = 0, huge = 0;
            for (const Object* o : locations) {
                const BlockGripLocationData& l = o->as_block_grip_location();
                assert_true(l.eval.expression.has_value(), what + "an expression (1) with a value");
                assert_eq(std::string(), l.eval.expression->expression, what + "its text is empty");
                if (l.eval.expression->value.tag == 40) {
                    ++reals;
                    if (l.eval.expression->value.real > 1e99) ++huge;
                } else {
                    assert_eq(70, l.eval.expression->value.tag, what + "the other tag is 70");
                    ++ints;
                }
            }
            assert_eq(12, reals, what + "twelve reals");
            assert_eq(1, ints, what + "one int16 (UpdatedFlip)");
            assert_eq(4, huge, what + "four unset reals (1.797693134862314E+99)");

            // visibility: three states
            const Object* visibility = the_object(d, ObjectType::BlockVisibilityParameter);
            const hdxf::BlockVisibilityParameterData& v = visibility->as_block_visibility_parameter();
            assert_eq(27, v.eval.node_id, what + "visibility node id");
            assert_eq(std::string("Visibility State"), v.element.name, what + "visibility element name");
            assert_eq(60.0, v.one_point.position.x, what + "1Pt 1010");
            assert_eq(28, v.one_point.grip_node_id, what + "1Pt 93: the grip's node id");
            assert_true(!v.one_point.links[0] && !v.one_point.links[1], what + "1Pt slots empty");
            assert_eq(std::string("Visibility1"), v.name, what + "301");
            assert_eq(3, static_cast<int>(v.entities.size()), what + "93 and its three 331");
            assert_eq(3, static_cast<int>(v.states.size()), what + "92 and its three states");
            assert_eq(std::string("VisibilityState0"), v.states[0].name, what + "303");
            assert_eq(3, static_cast<int>(v.states[0].entities.size()), what + "94 and 332");
            assert_eq(14, static_cast<int>(v.states[0].elements.size()), what + "95 and 333");
            assert_eq(std::string("HDXF_ALL"), v.states[1].name, what + "second state");
            assert_eq(3, static_cast<int>(v.states[1].entities.size()), what + "its entities");
            assert_eq(14, static_cast<int>(v.states[1].elements.size()), what + "its elements");
            assert_eq(std::string("HDXF_HIDDEN"), v.states[2].name, what + "third state");
            assert_true(v.states[2].entities.empty() && v.states[2].elements.empty(), what + "a state with no member (94 0, 95 0)");
            // the members are the block's entities and the graph's elements
            for (const std::string& h : v.states[0].elements) {
                const Object* e = d.find_object(h);
                assert_true(e != nullptr, what + "a state's element is an object");
                assert_eq(upper(graph_object->handle), upper(e->owner), what + "that the graph owns");
            }
            // The block is the one whose extension dictionary owns the graph. Before 2010 AutoCAD's file has it as the anonymous
            // *U1 (BLOCK flag 1) with its name in the XDATA AcDbDynamicBlockTrueName; from 2010 under its own name
            const hdxf::Block* block = nullptr;
            for (const hdxf::Block& b : d.blocks()) {
                if (!b.record.xdictionary.empty() && upper(b.record.xdictionary) == upper(graph_object->owner)) block = &b;
            }
            assert_true(block != nullptr, what + "the block that owns the graph");
            assert_eq(std::string("HDXF_DYNAMIC"), d.dynamic_block_true_name(*block), what + "the block's own name");
            if (std::string(tag) == "2000" || std::string(tag) == "2004" || std::string(tag) == "2007") {
                assert_eq(std::string("*U1"), block->name, what + "the anonymous name before 2010");
                assert_eq(1, block->flags & 1, what + "BLOCK flag 1");
            } else {
                assert_eq(std::string("HDXF_DYNAMIC"), block->name, what + "its own name from 2010");
            }
            for (const std::string& h : v.entities) {
                bool found = false;
                for (const hdxf::Entity& e : block->entities) found = found || upper(e.props.handle) == upper(h);
                assert_true(found, what + "a visibility entity is an entity of the block");
            }

            const Object* visibility_grip = the_object(d, ObjectType::BlockVisibilityGrip);
            assert_eq(28, visibility_grip->as_block_visibility_grip().eval.node_id, what + "visibility grip node id");
            assert_eq(0, visibility_grip->as_block_visibility_grip().grip.flag_280, what + "its 280");
            assert_eq(60.0, visibility_grip->as_block_visibility_grip().grip.location.x, what + "its location");

            // representation and purge preventer
            const Object* representation = the_object(d, ObjectType::BlockRepresentation);
            assert_eq(1, representation->as_block_representation().version, what + "70");
            assert_eq(upper(block->record.handle), upper(representation->as_block_representation().original_block_record),
                      what + "340 is the original block's BLOCK_RECORD");
            assert_eq(1, the_object(d, ObjectType::DynamicBlockPurgePreventer)->as_dynamic_block_purge_preventer().version, what + "70");
        }
    }
}

TEST(graph_block_base_reads_blockscaleaction_from_autocads_samples) {
    // ACadSharp's samples (AutoCAD's own files of 2000 … 2018) are the only ones with a scale action
    const std::pair<const char*, const char*> files[] = {
        {"acadsharp/sample_AC1015_ascii.dxf", "2000"}, {"acadsharp/sample_AC1018_ascii.dxf", "2004"},
        {"acadsharp/sample_AC1021_ascii.dxf", "2007"}, {"acadsharp/sample_AC1024_ascii.dxf", "2010"},
        {"acadsharp/sample_AC1027_ascii.dxf", "2013"}, {"acadsharp/sample_AC1032_ascii.dxf", "2018"},
        {"acadsharp/sample_AC1015_binary.dxf", "2000 binary"}, {"acadsharp/sample_AC1032_binary.dxf", "2018 binary"}};
    for (const auto& [file, label] : files) {
        std::vector<uint8_t> in = external_bytes(file);
        Drawing d = load(in, file);
        const std::string what = std::string(label) + ": ";
        std::vector<const Object*> all = objects_of(d, ObjectType::BlockScaleAction);
        assert_eq(1, static_cast<int>(all.size()), what + "one scale action");
        const hdxf::BlockScaleActionData& a = all[0]->as_block_scale_action();
        assert_eq(std::string("Scale"), a.element.name, what + "300");
        assert_eq(10, a.eval.node_id, what + "node id");
        assert_eq(0, static_cast<int>(a.action.node_ids.size()), what + "70");
        assert_eq(1, static_cast<int>(a.action.selection.size()), what + "71 and its 330");
        assert_true(!a.action.selection[0].empty() && a.action.selection[0] != "0", what + "the selected entity's handle");
        assert_true(std::fabs(a.action.location.x - 3.162400550851376) < 1e-12, what + "1010 (class data)");
        assert_eq(1, a.base_point_action.x_node_id, what + "92");
        assert_eq(1, a.base_point_action.y_node_id, what + "93");
        assert_eq(std::string("UpdatedBaseX"), a.base_point_action.x_name, what + "301");
        assert_eq(std::string("UpdatedBaseY"), a.base_point_action.y_name, what + "302");
        assert_eq(1, a.base_point_action.flag_280, what + "280");
        assert_eq(std::string("Scale"), a.properties[0].name, what + "303");
        assert_eq(std::string("XScale"), a.properties[1].name, what + "304");
        assert_eq(std::string("YScale"), a.properties[2].name, what + "305");
        assert_eq(1, a.properties[0].node_id, what + "94");
        assert_eq(1, a.properties[2].node_id, what + "96");
        // The sample's graph is the one of twelve nodes (96 and 97 are the highest id, not a count)
        const Object* graph = nullptr;
        for (const Object* g : objects_of(d, ObjectType::EvaluationGraph)) {
            if (g->as_evaluation_graph().nodes.size() == 12) graph = g;
        }
        assert_true(graph != nullptr, what + "the graph of twelve nodes");
        assert_eq(12, static_cast<int>(graph->as_evaluation_graph().nodes.size()), what + "twelve nodes");
        assert_eq(10, static_cast<int>(graph->as_evaluation_graph().edges.size()), what + "ten edges");
        assert_eq(26, graph->as_evaluation_graph().last_node_id, what + "96");
        assert_eq(26, graph->as_evaluation_graph().last_node_id_2, what + "97");
        assert_true(graph->as_evaluation_graph().links_consistent(), what + "its chains");
        // The linear parameter has ACAUTHENVIRON in its XDATA and the class data 1010 before it: XDATA stays XDATA
        const Object* linear = the_object(d, ObjectType::BlockLinearParameter);
        bool has_environment = false;
        for (const hdxf::XdataList& x : linear->xdata) has_environment = has_environment || x.app_name == "ACAUTHENVIRON";
        assert_true(has_environment, what + "ACAUTHENVIRON stays XDATA");
        const hdxf::BlockLinearParameterData& p = linear->as_block_linear_parameter();
        assert_eq(std::string("dynamic-diameter"), p.label, what + "305");
        assert_eq(std::string("dinamic parameter for the circle diameter"), p.description, what + "306");
        assert_true(p.two_point.links[0] && p.two_point.links[1] && p.two_point.links[2] && p.two_point.links[3],
                    what + "all four property slots are linked in this file");
        assert_eq(5, p.two_point.links[0]->node_id, what + "92");
        assert_eq(2, p.two_point.links[2]->node_id, what + "94");
        assert_eq(std::string("DisplacementX"), p.two_point.links[0]->name, what + "301");
        // Four visibility states: the first with one entity and four elements, the second empty
        const hdxf::BlockVisibilityParameterData& v =
            the_object(d, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter();
        assert_eq(4, static_cast<int>(v.states.size()), what + "four states");
        assert_eq(std::string("visibility_state_default"), v.states[0].name, what + "303");
        assert_eq(1, static_cast<int>(v.states[0].entities.size()), what + "one entity");
        assert_eq(4, static_cast<int>(v.states[0].elements.size()), what + "four elements");
        assert_true(v.states[1].entities.empty() && v.states[1].elements.empty(), what + "the hidden state is empty");
    }
}

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

TEST(graph_block_base_every_record_of_the_corpus_is_typed_and_written_back_unchanged) {
    std::vector<std::filesystem::path> files = files_with(kRecordNames);
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    int loaded = 0;
    std::map<ObjectType, int> counts;
    std::vector<std::string> failures;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
        Drawing d;
        if (!d.load_from_memory(bytes.data(), bytes.size())) continue;
        ++loaded;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::Unknown) {
                const std::string& n = o.as_unknown().object_type;
                for (const std::string& typed : kRecordNames) {
                    if (n == typed) failures.push_back(path.filename().string() + ": " + n + " " + o.handle + " is not typed");
                }
            }
            if (is_base_type(o.type)) ++counts[o.type];
        }
        for (const std::string& w : d.warnings()) {
            for (const std::string& n : kRecordNames) {
                if (w.find(n) != std::string::npos) failures.push_back(path.filename().string() + ": warning: " + w);
            }
        }
        // B12: written unchanged it is the file (ezdxf's files are another producer's: not held to it)
        if (lower_path(path).find("/ezdxf/") == std::string::npos) {
            std::vector<uint8_t> again = d.save_to_memory(format_of(bytes));
            if (again != bytes) failures.push_back(path.filename().string() + ": written unchanged, the bytes differ");
        }
    }
    std::cout << "graph and dynamic block bases, corpus: " << loaded << " files; graph " << counts[ObjectType::EvaluationGraph]
              << ", grip location " << counts[ObjectType::BlockGripLocation] << ", linear parameter "
              << counts[ObjectType::BlockLinearParameter] << ", linear grip " << counts[ObjectType::BlockLinearGrip]
              << ", visibility parameter " << counts[ObjectType::BlockVisibilityParameter] << ", visibility grip "
              << counts[ObjectType::BlockVisibilityGrip] << ", scale action " << counts[ObjectType::BlockScaleAction]
              << ", representation " << counts[ObjectType::BlockRepresentation] << ", purge preventer "
              << counts[ObjectType::DynamicBlockPurgePreventer] << "\n";
    for (size_t i = 0; i < failures.size() && i < 12; ++i) std::cout << "  " << failures[i] << "\n";
    assert_true(failures.empty(), std::to_string(failures.size()) + " failure(s), the first: " + (failures.empty() ? std::string() : failures[0]));
    for (ObjectType t : {ObjectType::EvaluationGraph, ObjectType::BlockGripLocation, ObjectType::BlockLinearParameter,
                         ObjectType::BlockLinearGrip, ObjectType::BlockVisibilityParameter, ObjectType::BlockVisibilityGrip,
                         ObjectType::BlockScaleAction, ObjectType::BlockRepresentation, ObjectType::DynamicBlockPurgePreventer}) {
        assert_true(counts[t] > 0, "the corpus has the type");
    }
}

TEST(graph_block_base_from_the_typed_values_alone_is_autocads_record) {
    std::vector<std::filesystem::path> files = files_with(kRecordNames);
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    int compared = 0;
    std::vector<std::string> failures;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
        if (format_of(bytes) == DxfFormat::Binary) continue;
        if (lower_path(path).find("/ezdxf/") != std::string::npos) continue;
        Drawing d;
        if (!d.load_from_memory(bytes.data(), bytes.size())) continue;
        std::set<std::string> handles;
        for (Object& o : d.objects()) {
            if (!is_base_type(o.type) || o.raw.empty()) continue;
            handles.insert(o.handle);
            o.raw = hdxf::RawRecord();
        }
        if (handles.empty()) continue;
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        if (out.empty()) { failures.push_back(path.filename().string() + ": " + d.last_error()); continue; }
        std::vector<Rec> before = ascii_records(bytes), after = ascii_records(out);
        for (const std::string& h : handles) {
            Rec a = record_with_handle(before, h), b = record_with_handle(after, h);
            if (a.empty()) continue;
            std::string why = b.empty() ? std::string("missing") : same_record(a, b);
            if (!why.empty()) failures.push_back(path.filename().string() + " " + trim(a[0].second) + " " + h + ": " + why);
            ++compared;
        }
    }
    std::cout << "graph and dynamic block bases from typed values: " << compared << " records compared\n";
    for (size_t i = 0; i < failures.size() && i < 12; ++i) std::cout << "  " << failures[i] << "\n";
    assert_true(failures.empty(), std::to_string(failures.size()) + " failure(s), the first: " + (failures.empty() ? std::string() : failures[0]));
    assert_true(compared > 300, "AutoCAD's records were compared (" + std::to_string(compared) + ")");
}

// ---------------------------------------------------------------------------
// The graph model
// ---------------------------------------------------------------------------

namespace {

// Builds the graph of `g` again with add_node and add_edge (a pair of edges with flags 4 that name each other with one
// add_edge of the first)
EvaluationGraphData rebuilt(const EvaluationGraphData& g) {
    EvaluationGraphData h;
    for (const EvalGraphNode& n : g.nodes) h.add_node(n.object);
    // A graph whose ids have gaps (a block that lost elements) keeps them
    for (size_t i = 0; i < g.nodes.size(); ++i) h.nodes[i].id = g.nodes[i].id;
    h.last_node_id = g.last_node_id;
    h.last_node_id_2 = g.last_node_id_2;
    std::set<int32_t> made;
    for (const EvalGraphEdge& e : g.edges) {
        if (made.count(e.index)) continue;
        const bool pair = e.inverse >= 0;
        h.add_edge(e.from, e.to, e.ref_count, pair);
        made.insert(e.index);
        if (pair) made.insert(e.inverse);
    }
    return h;
}

std::string same_graph(const EvaluationGraphData& a, const EvaluationGraphData& b) {
    if (a.last_node_id != b.last_node_id || a.last_node_id_2 != b.last_node_id_2) return "96/97";
    if (a.nodes.size() != b.nodes.size()) return "the number of nodes";
    if (a.edges.size() != b.edges.size()) return "the number of edges";
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        const EvalGraphNode &x = a.nodes[i], &y = b.nodes[i];
        if (x.index != y.index || x.flags != y.flags || x.id != y.id || x.object != y.object || x.incoming_first != y.incoming_first ||
            x.incoming_last != y.incoming_last || x.outgoing_first != y.outgoing_first || x.outgoing_last != y.outgoing_last) {
            return "node " + std::to_string(i);
        }
    }
    for (size_t i = 0; i < a.edges.size(); ++i) {
        const EvalGraphEdge &x = a.edges[i], &y = b.edges[i];
        if (x.index != y.index || x.flags != y.flags || x.ref_count != y.ref_count || x.from != y.from || x.to != y.to ||
            x.previous_incoming != y.previous_incoming || x.next_incoming != y.next_incoming ||
            x.previous_outgoing != y.previous_outgoing || x.next_outgoing != y.next_outgoing || x.inverse != y.inverse) {
            return "edge " + std::to_string(i);
        }
    }
    return {};
}

} // namespace

TEST(graph_block_base_the_graph_model_makes_the_chains_autocad_has) {
    // The graph of the dynamic block in doc/facit (30 nodes, 23 edges), the 12 of ACadSharp's AutoCAD sample, and the lookup
    // block's (ids with gaps, edges that are pairs with flags 4: the inverse links): add_node/add_edge in the order of the
    // file give every link of every node and edge as AutoCAD has them
    int graphs = 0;
    auto check = [&](const std::vector<uint8_t>& bytes, const std::string& what) {
        Drawing d = load(bytes, what);
        for (const Object* o : objects_of(d, ObjectType::EvaluationGraph)) {
            const EvaluationGraphData& g = o->as_evaluation_graph();
            if (g.edges.empty()) continue;
            std::string why = same_graph(g, rebuilt(g));
            assert_true(why.empty(), what + " " + o->handle + ": the rebuilt graph differs at " + why);
            ++graphs;
        }
    };
    check(facit_bytes("acad_2018_dxf_dynbase.dxf"), "dynbase 2018");
    check(external_bytes("acadsharp/sample_AC1032_ascii.dxf"), "ACadSharp 2018");
    check(external_bytes("acadsharp/sample_AC1015_ascii.dxf"), "ACadSharp 2000");
    check(facit_bytes("web/acad_AC1032_dxf_web_dynblock_lookup.dxf"), "lookup block");
    assert_true(graphs >= 4, "four graphs with edges");
    // The lookup graph really has the pairs and the gaps
    Drawing lookup = load(facit_bytes("web/acad_AC1032_dxf_web_dynblock_lookup.dxf"), "lookup");
    bool pairs = false, gaps = false;
    for (const Object* o : objects_of(lookup, ObjectType::EvaluationGraph)) {
        const EvaluationGraphData& g = o->as_evaluation_graph();
        for (const EvalGraphEdge& e : g.edges) {
            pairs = pairs || (e.flags == 4 && e.inverse >= 0 && g.edges[static_cast<size_t>(e.inverse)].inverse == e.index);
        }
        for (size_t i = 0; i < g.nodes.size(); ++i) gaps = gaps || g.nodes[i].id != static_cast<int32_t>(i) + 1;
    }
    assert_true(pairs, "edges with flags 4 name each other");
    assert_true(gaps, "ids with gaps");
}

TEST(graph_block_base_the_graph_model_cycles_shared_nodes_and_removal) {
    EvaluationGraphData g;
    // An empty graph is 96 = 97 = 1 (AutoCAD's empty graphs)
    assert_eq(1, g.last_node_id, "96 of an empty graph");
    assert_eq(1, g.last_node_id_2, "97");
    assert_true(g.links_consistent() && g.nodes.empty() && g.edges.empty(), "nothing");
    // The first node has id 1 and 96/97 stay 1 (a graph of one node is the same); then they follow the highest id
    assert_eq(1, g.add_node("A1"), "first id");
    assert_eq(1, g.last_node_id, "96 after one node");
    assert_eq(2, g.add_node("A2"), "second id");
    assert_eq(3, g.add_node("A3"), "third id");
    assert_eq(4, g.add_node("A4"), "fourth id");
    assert_eq(4, g.last_node_id, "96 follows");
    assert_eq(4, g.last_node_id_2, "97 follows");
    assert_eq(-1, g.add_edge(0, 9), "an edge to no node is refused");
    assert_true(g.edges.empty(), "and makes nothing");
    // A cycle 0 -> 1 -> 2 -> 0 and a node (3) two edges end at
    assert_eq(0, g.add_edge(0, 1), "edge 0");
    assert_eq(1, g.add_edge(1, 2), "edge 1");
    assert_eq(2, g.add_edge(2, 0), "edge 2");
    assert_eq(3, g.add_edge(0, 3), "edge 3");
    assert_eq(4, g.add_edge(1, 3), "edge 4: the second edge into node 3");
    assert_true(g.links_consistent(), "chains made");
    assert_eq(3, g.nodes[3].incoming_first, "node 3: first edge in");
    assert_eq(4, g.nodes[3].incoming_last, "node 3: last edge in");
    assert_eq(4, g.edges[3].next_incoming, "edge 3 is followed by edge 4 into node 3");
    assert_eq(3, g.edges[4].previous_incoming, "and edge 4 follows edge 3");
    assert_eq(0, g.nodes[0].outgoing_first, "node 0 starts edges 0 and 3");
    assert_eq(3, g.nodes[0].outgoing_last, "node 0 last out");
    assert_eq(2, g.nodes[0].incoming_first, "the cycle comes back into node 0 with edge 2");
    // A bidirectional pair: two edges with flags 4 that are each other's inverse
    const int32_t pair = g.add_edge(2, 3, 2, true);
    assert_eq(5, pair, "the pair's first edge");
    assert_eq(4, g.edges[5].flags, "flags 4");
    assert_eq(4, g.edges[6].flags, "flags 4");
    assert_eq(6, g.edges[5].inverse, "inverse");
    assert_eq(5, g.edges[6].inverse, "inverse");
    assert_eq(3, g.edges[6].from, "the inverse goes the other way");
    assert_eq(2, g.edges[6].ref_count, "with the same 94");

    // Removing node 1: its edges (0, 1 and 4) go with it, the others are numbered again and the ids stay
    EvaluationGraphData h = g;
    assert_true(h.remove_node(1), "removed");
    assert_eq(3, static_cast<int>(h.nodes.size()), "three nodes");
    assert_eq(4, static_cast<int>(h.edges.size()), "edges 2, 3, 5 and 6 remain");
    assert_true(h.links_consistent(), "chains");
    assert_eq(1, h.nodes[0].id, "ids stay");
    assert_eq(3, h.nodes[1].id, "node 2 is at index 1 and keeps its id 3");
    assert_eq(4, h.nodes[2].id, "node 3 is at index 2 and keeps its id 4");
    assert_eq(4, h.last_node_id, "96 and 97 stay");
    assert_eq(std::string("A3"), h.nodes[1].object, "the element follows the node");
    assert_eq(1, h.edges[0].from, "edge 2 (2 -> 0) is now 1 -> 0");
    assert_eq(0, h.edges[0].to, "to");
    assert_eq(0, h.edges[1].from, "edge 3 (0 -> 3) is now 0 -> 2");
    assert_eq(2, h.edges[1].to, "to");
    assert_eq(3, h.edges[2].inverse, "the pair's inverse links follow the numbering");
    assert_eq(2, h.edges[3].inverse, "both ways");
    assert_false(h.remove_node(7), "no such node");
    // Removing a node by its element's handle (a handle is not an id): case and leading zeros do not matter
    EvaluationGraphData k = g;
    assert_true(k.remove_object("a3"), "by handle");
    assert_true(k.find_node_by_object("A3") == nullptr, "gone");
    assert_true(k.find_node_by_id(3) == nullptr, "its id too");
    assert_true(k.find_node_by_id(2) != nullptr, "the others stay");
    assert_false(k.remove_object("A3"), "not twice");
    // Removing one edge of a pair removes both
    EvaluationGraphData m = g;
    assert_true(m.remove_edge(5), "the pair");
    assert_eq(5, static_cast<int>(m.edges.size()), "both edges went");
    assert_true(m.links_consistent(), "chains");
    for (const EvalGraphEdge& e : m.edges) assert_eq(-1, e.inverse, "no inverse is left");
    // Removing the last node and adding another: the id goes on from 96, nothing is reused
    EvaluationGraphData n;
    n.add_node("B1");
    n.add_node("B2");
    n.remove_node(1);
    assert_eq(3, n.add_node("B3"), "ids are not reused");
    assert_eq(3, n.last_node_id, "96");
}
