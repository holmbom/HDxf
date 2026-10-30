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
// Step 9.9 of phase 9 (plan 2026-10-27-fas-9-typer.md; B100-B105, B109, B113-B116; B12 and B19): the version matrix of the
// nine types, changes (a scalar, an optional slot, a list's length), what a removal takes with it (an element, an entity of
// the block, the block), and a complete typed definition built in memory — block, graph, elements, INSERT, the anonymous
// representation — in every version and format.
//
// The originals are the ones of test_graph_block_base.cpp: doc/facit/acad_<version>_dxf_dynbase*.dxf (made by
// fas9_dynamic_base.scr), the lookup block of doc/facit/web and ACadSharp's samples. They are local files: a test whose
// file is missing is skipped.
// ---------------------------------------------------------------------------

using hdxf::BlockGripLocationData;
using hdxf::Boxed;

namespace {

Object base_object(ObjectType type) {
    Object o;
    o.type = type;
    o.owner = "0";
    return o;
}

Object graph_for_matrix() {
    Object o = base_object(ObjectType::EvaluationGraph);
    EvaluationGraphData g;
    g.add_node("F01");
    g.add_node("F02");
    g.add_node("F03");
    g.add_edge(0, 1);
    g.add_edge(1, 2, 2);
    g.add_edge(2, 0, 1, true);
    o.data = g;
    return o;
}

hdxf::EvalExprData eval_with(int node_id) {
    hdxf::EvalExprData e;
    e.node_id = node_id;
    e.version = 33;
    e.maintenance = 327;
    return e;
}

Object grip_location_for_matrix() {
    Object o = base_object(ObjectType::BlockGripLocation);
    BlockGripLocationData d;
    d.eval = eval_with(4);
    d.eval.expression = hdxf::EvalExprValue{"", hdxf::EvalVariant{40, 12.5, 0}};
    d.parameter_node_id = 2;
    d.property_name = "UpdatedEndX";
    o.data = Boxed<BlockGripLocationData>(d);
    return o;
}

Object linear_parameter_for_matrix() {
    Object o = base_object(ObjectType::BlockLinearParameter);
    hdxf::BlockLinearParameterData d;
    d.eval = eval_with(6);
    d.element.name = "Linear";
    d.two_point.first = {1, 2, 3};
    d.two_point.second = {4, 5, 6};
    d.two_point.grip_node_ids = {0, 7, 0, 0};
    d.two_point.links[0] = hdxf::BlockPropertyLink{5, "DisplacementX"};
    d.two_point.links[3] = hdxf::BlockPropertyLink{7, "DisplacementY"};
    d.label = "L\xC3\xA4ngd";
    d.description = "ett m\xC3\xA5tt";
    d.label_offset = -10.0;
    d.expression = "d1 + 2";
    d.constraints = {1.5, 2.5, 3.5};
    o.data = Boxed<hdxf::BlockLinearParameterData>(d);
    return o;
}

Object linear_grip_for_matrix() {
    Object o = base_object(ObjectType::BlockLinearGrip);
    hdxf::BlockLinearGripData d;
    d.eval = eval_with(7);
    d.element.name = "End Grip";
    d.grip.x_node_id = 8;
    d.grip.y_node_id = 9;
    d.grip.location = {40, 0, 0};
    d.vector = {40, 1, 2};
    o.data = Boxed<hdxf::BlockLinearGripData>(d);
    return o;
}

Object visibility_parameter_for_matrix() {
    Object o = base_object(ObjectType::BlockVisibilityParameter);
    hdxf::BlockVisibilityParameterData d;
    d.eval = eval_with(27);
    d.element.name = "Visibility State";
    d.one_point.position = {60, 25, 0};
    d.one_point.grip_node_id = 28;
    d.name = "Visibility1";
    d.entities = {"F10", "F11", "F12"};
    hdxf::BlockVisibilityState all;
    all.name = "Alla";
    all.entities = {"F10", "F11", "F12"};
    all.elements = {"F01", "F02"};
    hdxf::BlockVisibilityState none;
    none.name = "Inga";
    d.states = {all, none};
    o.data = Boxed<hdxf::BlockVisibilityParameterData>(d);
    return o;
}

Object visibility_grip_for_matrix() {
    Object o = base_object(ObjectType::BlockVisibilityGrip);
    hdxf::BlockVisibilityGripData d;
    d.eval = eval_with(28);
    d.element.name = "Grip";
    d.grip.x_node_id = 29;
    d.grip.y_node_id = 30;
    d.grip.location = {60, 25, 0};
    d.grip.flag_280 = 0;
    o.data = Boxed<hdxf::BlockVisibilityGripData>(d);
    return o;
}

Object scale_action_for_matrix() {
    Object o = base_object(ObjectType::BlockScaleAction);
    hdxf::BlockScaleActionData d;
    d.eval = eval_with(10);
    d.element.name = "Scale";
    d.action.selection = {"F20", "F21"};
    d.action.location = {3, -4, 0};
    d.base_point_action.x_node_id = 1;
    d.base_point_action.y_node_id = 1;
    d.base_point_action.x_name = "UpdatedBaseX";
    d.base_point_action.y_name = "UpdatedBaseY";
    d.properties = {hdxf::BlockPropertyLink{1, "Scale"}, hdxf::BlockPropertyLink{1, "XScale"}, hdxf::BlockPropertyLink{1, "YScale"}};
    o.data = Boxed<hdxf::BlockScaleActionData>(d);
    return o;
}

Object representation_for_matrix() {
    Object o = base_object(ObjectType::BlockRepresentation);
    hdxf::BlockRepresentationData d;
    d.original_block_record = "F30";
    o.data = d;
    return o;
}

Object purge_preventer_for_matrix() {
    Object o = base_object(ObjectType::DynamicBlockPurgePreventer);
    o.data = hdxf::DynamicBlockPurgePreventerData{};
    return o;
}

hdxf_test::MatrixReport run_matrix(const char* name, ObjectType type, const std::function<Object()>& make,
                                   const std::function<std::string(const Object&)>& check) {
    const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object(name);
    assert_true(codec != nullptr, std::string(name) + " has a codec");
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
        name, codec->versions, [&](Drawing& d) {
            // the records the handles of the test objects point to (a drawing built in memory drops a pointer to nothing)
            for (const char* h : {"F01", "F02", "F03", "F20", "F21"}) {
                Object x = base_object(ObjectType::XRecord);
                x.handle = h;
                x.data = hdxf::XRecordData{};
                d.objects().push_back(x);
            }
            for (const char* h : {"F10", "F11", "F12"}) {
                hdxf::Entity e = d.add_line({0, 0, 0}, {1, 1, 0});
                d.entities().back().props.handle = h;
            }
            d.add_block("PLACEHOLDER").record.handle = "F30";
            d.objects().push_back(make());
        },
        [&](const Drawing& r, DxfVersion, DxfFormat) {
            int n = 0;
            std::string why;
            for (const auto& o : r.objects()) {
                if (o.type != type) continue;
                ++n;
                why = check(o);
            }
            if (n != 1) return "expected one typed record, found " + std::to_string(n);
            return why;
        });
    std::cout << name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, "
              << report.not_established << " not established, AutoCAD verified " << report.autocad_verified << ", ej testat "
              << report.autocad_not_tested << ", not savable " << report.autocad_not_savable << "\n";
    assert_eq(12, report.generated, std::string(name) + ": 2000 … 2018, both formats, are made");
    assert_eq(2, report.named_errors, std::string(name) + ": R12 has the named error (no OBJECTS section)");
    assert_eq(4, report.not_established, std::string(name) + ": R13 and R14 are not established (no file, no grammar)");
    assert_eq(12, report.autocad_verified, std::string(name) + ": AutoCAD's own files of 2000 … 2018");
    assert_eq(4, report.autocad_not_savable, std::string(name) + ": AutoCAD 2025 cannot save R13/R14");
    return report;
}

std::string expect(bool ok, const char* what) { return ok ? std::string() : std::string(what); }

} // namespace

TEST(dynamic_block_relations_version_matrix_of_the_graph_and_the_grip_location_component) {
    run_matrix("ACAD_EVALUATION_GRAPH", ObjectType::EvaluationGraph, graph_for_matrix, [](const Object& o) {
        const EvaluationGraphData& g = o.as_evaluation_graph();
        if (g.nodes.size() != 3 || g.edges.size() != 4) return std::string("nodes and edges");
        if (g.nodes[2].object != "F03" || g.nodes[2].id != 3 || g.last_node_id != 3) return std::string("a node");
        if (g.edges[3].flags != 4 || g.edges[3].inverse != 2 || g.edges[1].ref_count != 2) return std::string("an edge");
        return expect(g.links_consistent(), "chains");
    });
    run_matrix("BLOCKGRIPLOCATIONCOMPONENT", ObjectType::BlockGripLocation, grip_location_for_matrix, [](const Object& o) {
        const BlockGripLocationData& d = o.as_block_grip_location();
        return expect(d.eval.node_id == 4 && d.eval.expression && d.eval.expression->value.real == 12.5 &&
                          d.parameter_node_id == 2 && d.property_name == "UpdatedEndX",
                      "the component's values");
    });
}

TEST(dynamic_block_relations_version_matrix_of_the_linear_parameter_and_grip) {
    run_matrix("BLOCKLINEARPARAMETER", ObjectType::BlockLinearParameter, linear_parameter_for_matrix, [](const Object& o) {
        const hdxf::BlockLinearParameterData& d = o.as_block_linear_parameter();
        if (d.label != "L\xC3\xA4ngd" || d.description != "ett m\xC3\xA5tt" || d.expression != "d1 + 2") return std::string("the strings");
        if (d.two_point.grip_node_ids.size() != 4 || d.two_point.grip_node_ids[1] != 7) return std::string("170 and 91");
        if (!d.two_point.links[0] || d.two_point.links[1] || d.two_point.links[2] || !d.two_point.links[3]) return std::string("the slots");
        if (d.two_point.links[3]->name != "DisplacementY" || d.two_point.links[0]->node_id != 5) return std::string("a slot");
        if (d.constraints[2] != 3.5 || d.label_offset != -10.0 || d.two_point.second.z != 6.0) return std::string("the reals");
        return std::string();
    });
    run_matrix("BLOCKLINEARGRIP", ObjectType::BlockLinearGrip, linear_grip_for_matrix, [](const Object& o) {
        const hdxf::BlockLinearGripData& d = o.as_block_linear_grip();
        return expect(d.grip.x_node_id == 8 && d.grip.y_node_id == 9 && d.grip.location.x == 40 && d.vector.z == 2 &&
                          d.element.name == "End Grip" && d.eval.node_id == 7,
                      "the grip's values");
    });
}

TEST(dynamic_block_relations_version_matrix_of_the_visibility_parameter_and_grip) {
    run_matrix("BLOCKVISIBILITYPARAMETER", ObjectType::BlockVisibilityParameter, visibility_parameter_for_matrix, [](const Object& o) {
        const hdxf::BlockVisibilityParameterData& d = o.as_block_visibility_parameter();
        if (d.entities.size() != 3 || d.states.size() != 2) return std::string("93/331 and 92");
        if (d.states[0].name != "Alla" || d.states[0].entities.size() != 3 || d.states[0].elements.size() != 2) return std::string("the first state");
        if (d.states[1].name != "Inga" || !d.states[1].entities.empty() || !d.states[1].elements.empty()) return std::string("the empty state");
        return expect(d.one_point.grip_node_id == 28 && d.one_point.position.x == 60, "the 1Pt part");
    });
    run_matrix("BLOCKVISIBILITYGRIP", ObjectType::BlockVisibilityGrip, visibility_grip_for_matrix, [](const Object& o) {
        const hdxf::BlockVisibilityGripData& d = o.as_block_visibility_grip();
        return expect(d.grip.x_node_id == 29 && d.grip.y_node_id == 30 && d.grip.flag_280 == 0 && d.element.name == "Grip", "the grip");
    });
}

TEST(dynamic_block_relations_version_matrix_of_the_scale_action_the_representation_and_the_purge_preventer) {
    run_matrix("BLOCKSCALEACTION", ObjectType::BlockScaleAction, scale_action_for_matrix, [](const Object& o) {
        const hdxf::BlockScaleActionData& d = o.as_block_scale_action();
        if (d.action.selection.size() != 2 || d.action.selection[1] != "F21") return std::string("71 and 330");
        return expect(d.properties[2].name == "YScale" && d.base_point_action.x_name == "UpdatedBaseX" && d.action.location.x == 3,
                      "the action's values");
    });
    run_matrix("ACDB_BLOCKREPRESENTATION_DATA", ObjectType::BlockRepresentation, representation_for_matrix, [](const Object& o) {
        return expect(o.as_block_representation().original_block_record == "F30" && o.as_block_representation().version == 1, "70 and 340");
    });
    run_matrix("ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION", ObjectType::DynamicBlockPurgePreventer, purge_preventer_for_matrix,
               [](const Object& o) { return expect(o.as_dynamic_block_purge_preventer().version == 1, "70"); });
}

TEST(dynamic_block_relations_the_older_dxf_versions_have_the_named_error_and_r13_r14_are_not_claimed) {
    // R12 has no OBJECTS section: the error names the type and 2000, before anything is changed
    for (const auto& [name, make] : std::vector<std::pair<std::string, std::function<Object()>>>{
             {"ACAD_EVALUATION_GRAPH", graph_for_matrix}, {"BLOCKLINEARPARAMETER", linear_parameter_for_matrix},
             {"BLOCKSCALEACTION", scale_action_for_matrix}, {"ACDB_BLOCKREPRESENTATION_DATA", representation_for_matrix}}) {
        Drawing d(DxfVersion::R12);
        d.objects().push_back(make());
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), name + " is not made as R12");
        assert_eq(name + " requires DXF 2000 or later", d.last_error(), "the named error");
        assert_true(d.objects().back().handle.empty(), "no handle was given");
    }
    // the matrices say it too
    const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object("BLOCKLINEARGRIP");
    assert_true(codec != nullptr, "the codec");
    assert_true(codec->versions.at(DxfVersion::R12, DxfFormat::Ascii).basis == hdxf::internal::ErrorBasis::Documented, "R12 is documented");
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14}) {
        const hdxf::internal::VersionCell& c = codec->versions.at(v, DxfFormat::Binary);
        assert_true(c.basis == hdxf::internal::ErrorBasis::NotEstablished, "R13/R14: no claim");
        assert_true(c.autocad == hdxf::internal::AutoCadStatus::NotSavable, "AutoCAD 2025 cannot save them");
    }
    for (DxfVersion v : kVersions) assert_true(codec->versions.at(v, DxfFormat::Ascii).autocad == hdxf::internal::AutoCadStatus::Verified, "2000 … 2018");
    // every type of the step is in the register with its step and a codec
    for (const char* n : {"ACAD_EVALUATION_GRAPH", "BLOCKGRIPLOCATIONCOMPONENT", "BLOCKLINEARPARAMETER", "BLOCKLINEARGRIP",
                          "BLOCKVISIBILITYPARAMETER", "BLOCKVISIBILITYGRIP", "BLOCKSCALEACTION", "ACDB_BLOCKREPRESENTATION_DATA",
                          "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION"}) {
        assert_true(hdxf::internal::TypedRegistry::instance().implemented(n), std::string(n) + " is implemented");
    }
}

// ---------------------------------------------------------------------------
// Changes (B12): a scalar, an optional slot, a list's length — only the record that changed differs
// ---------------------------------------------------------------------------

namespace {

std::map<std::string, Rec> records_by_handle(const std::vector<uint8_t>& bytes) {
    std::map<std::string, Rec> out;
    for (const Rec& r : ascii_records(bytes)) {
        if (trim(r[0].second) == "SECTION") continue;       // the HEADER's $VERSIONGUID is new at every changed save
        for (const Group& g : r) {
            if (g.first == 5) { out[trim(g.second)] = r; break; }
            if (g.first == 100) break;
        }
    }
    return out;
}

// The handles of the records that are not the same in the two files (or are in one only)
std::set<std::string> differing(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::map<std::string, Rec> x = records_by_handle(a), y = records_by_handle(b);
    std::set<std::string> out;
    for (const auto& [h, r] : x) {
        auto it = y.find(h);
        if (it == y.end() || r != it->second) out.insert(h);
    }
    for (const auto& [h, r] : y) if (!x.count(h)) out.insert(h);
    return out;
}

Object* object_by_handle(Drawing& d, const std::string& handle) {
    Object* o = d.find_object(handle);
    assert_true(o != nullptr, "the object " + handle);
    return o;
}

void remove_object(Drawing& d, const std::string& handle) {
    auto& list = d.objects();
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Object& o) { return upper(o.handle) == upper(handle); }), list.end());
}

void remove_entity_of_block(Drawing& d, const std::string& block, const std::string& handle) {
    for (hdxf::Block& b : d.blocks()) {
        if (b.name != block) continue;
        b.entities.erase(std::remove_if(b.entities.begin(), b.entities.end(),
                                        [&](const hdxf::Entity& e) { return upper(e.props.handle) == upper(handle); }),
                         b.entities.end());
    }
}

const hdxf::Block* block_named(const Drawing& d, const std::string& name) {
    const hdxf::Block* b = d.find_block(name);
    assert_true(b != nullptr, "the block " + name);
    return b;
}

// The 2018 file's records the tests change: the linear parameter, the visibility parameter, the graph, a state's element
const char* const kLinear = "269";
const char* const kVisibility = "293";
const char* const kGraph = "25D";

} // namespace

TEST(dynamic_block_relations_a_changed_scalar_an_optional_slot_and_a_list_change_only_their_record) {
    for (bool binary : {false, true}) {
        const std::string name = dynbase("2018", binary);
        std::vector<uint8_t> in = facit_bytes(name);
        const DxfFormat format = format_of(in);
        auto change = [&](const std::function<void(Drawing&)>& edit) {
            Drawing d = load(in, name);
            edit(d);
            std::vector<uint8_t> out = save(d, format, name);
            Drawing r = load(out, name);
            return std::make_pair(std::move(out), std::move(r));
        };
        const std::string what = name + ": ";

        // A scalar: the parameter's label (a long UTF-8 text too)
        std::string label = "Avst\xC3\xA5nd";
        for (int i = 0; i < 300; ++i) label += "\xC3\xA5";
        auto [out1, r1] = change([&](Drawing& d) { object_by_handle(d, kLinear)->as_block_linear_parameter().label = label; });
        assert_eq(label, r1.find_object(kLinear)->as_block_linear_parameter().label, what + "the label");
        assert_eq(std::string("Linear"), r1.find_object(kLinear)->as_block_linear_parameter().element.name, what + "the rest of it");
        if (format == DxfFormat::Ascii) {
            std::string which;
            for (const std::string& h : differing(in, out1)) {
                const auto after_records = records_by_handle(out1), before_records = records_by_handle(in);
                auto it = after_records.count(h) ? after_records.find(h) : before_records.find(h);
                which += h + " (" + trim(it->second[0].second) + ": " +
                         (after_records.count(h) && before_records.count(h) ? same_record(before_records.at(h), after_records.at(h)) : std::string("one side only")) + ") ";
            }
            assert_eq(1, static_cast<int>(differing(in, out1).size()), what + "only the record that changed differs: " + which);
            assert_true(differing(in, out1).count(kLinear) == 1, what + "the linear parameter");
        }

        // An optional slot: the first property slot is linked (flag 0 becomes 1 with its id and name)
        auto [out2, r2] = change([&](Drawing& d) {
            object_by_handle(d, kLinear)->as_block_linear_parameter().two_point.links[0] = hdxf::BlockPropertyLink{7, "DisplacementX"};
        });
        const hdxf::Block2PtParameterData& two = r2.find_object(kLinear)->as_block_linear_parameter().two_point;
        assert_true(two.links[0] && two.links[0]->node_id == 7 && two.links[0]->name == "DisplacementX", what + "the slot is there");
        assert_true(!two.links[1] && two.links[2] && two.links[3], what + "the other slots as they were");
        if (format == DxfFormat::Ascii) {
            assert_true(differing(in, out2) == std::set<std::string>{kLinear}, what + "only the linear parameter");
            Rec before = records_by_handle(in)[kLinear], after = records_by_handle(out2)[kLinear];
            assert_eq(static_cast<int>(before.size()) + 2, static_cast<int>(after.size()), what + "two groups more (92 and 301)");
        }
        // and the slot taken away again
        auto [out3, r3] = change([&](Drawing& d) {
            object_by_handle(d, kLinear)->as_block_linear_parameter().two_point.links[2].reset();
        });
        assert_true(!r3.find_object(kLinear)->as_block_linear_parameter().two_point.links[2], what + "a slot removed");

        // A list: another grip node id (170 follows) and a state with a member in each list (92, 94 and 95 follow)
        auto [out4, r4] = change([&](Drawing& d) {
            object_by_handle(d, kLinear)->as_block_linear_parameter().two_point.grip_node_ids.push_back(9);
            hdxf::BlockVisibilityParameterData& v = object_by_handle(d, kVisibility)->as_block_visibility_parameter();
            hdxf::BlockVisibilityState extra;
            extra.name = "Extra";
            extra.entities = {v.entities[0]};
            extra.elements = {v.states[0].elements[0], v.states[0].elements[1]};
            v.states.push_back(extra);
            v.states[2].entities.push_back(v.entities[1]);
        });
        assert_eq(5, static_cast<int>(r4.find_object(kLinear)->as_block_linear_parameter().two_point.grip_node_ids.size()), what + "170");
        const hdxf::BlockVisibilityParameterData& v = r4.find_object(kVisibility)->as_block_visibility_parameter();
        assert_eq(4, static_cast<int>(v.states.size()), what + "92");
        assert_eq(std::string("Extra"), v.states[3].name, what + "the new state");
        assert_eq(2, static_cast<int>(v.states[3].elements.size()), what + "its 95");
        assert_eq(1, static_cast<int>(v.states[3].entities.size()), what + "its 94");
        assert_eq(1, static_cast<int>(v.states[2].entities.size()), what + "the empty state gained a member");
        assert_eq(3, static_cast<int>(v.states[0].entities.size()), what + "the first state as it was");
        if (format == DxfFormat::Ascii) {
            assert_true(differing(in, out4) == std::set<std::string>({kLinear, kVisibility}), what + "only the two records");
        }

        // The graph: a new edge changes the chains; the record is the only one that differs
        auto [out5, r5] = change([&](Drawing& d) {
            EvaluationGraphData& g = object_by_handle(d, kGraph)->as_evaluation_graph();
            g.add_edge(3, 10, 1);
        });
        const EvaluationGraphData& g5 = r5.find_object(kGraph)->as_evaluation_graph();
        assert_eq(24, static_cast<int>(g5.edges.size()), what + "a new edge");
        assert_true(g5.links_consistent(), what + "its chains");
        if (format == DxfFormat::Ascii) assert_true(differing(in, out5) == std::set<std::string>{kGraph}, what + "only the graph");

        // The grip location component's expression (a real changed, then an int16 flip state's value)
        auto [out6, r6] = change([&](Drawing& d) {
            for (Object& o : d.objects()) {
                if (o.type != ObjectType::BlockGripLocation) continue;
                auto& e = o.as_block_grip_location().eval.expression;
                if (e->value.tag == 70) e->value.integer = 1;
                else e->value.real = 2.5;
            }
        });
        int flips = 0;
        for (const Object* o : objects_of(r6, ObjectType::BlockGripLocation)) {
            const auto& e = o->as_block_grip_location().eval.expression;
            if (e->value.tag == 70) { ++flips; assert_eq(1, e->value.integer, what + "int value"); }
            else assert_eq(2.5, e->value.real, what + "real value");
        }
        assert_eq(1, flips, what + "one flip state");
    }
}

// ---------------------------------------------------------------------------
// What a removal takes with it (B19, B104): elements, entities, the block
// ---------------------------------------------------------------------------

TEST(dynamic_block_relations_an_element_removed_leaves_the_graph_and_the_visibility_states) {
    for (const char* tag : {"2000", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = dynbase(tag, binary);
            std::vector<uint8_t> in = facit_bytes(name);
            Drawing d = load(in, name);
            const std::string what = name + ": ";
            const Object* grip = the_object(d, ObjectType::BlockLinearGrip);
            const std::string grip_handle = grip->handle;
            const int32_t grip_id = grip->as_block_linear_grip().eval.node_id;
            const Object* graph_object = the_object(d, ObjectType::EvaluationGraph);
            const EvaluationGraphData before = graph_object->as_evaluation_graph();
            const EvalGraphNode* node = before.find_node_by_object(grip_handle);
            assert_true(node != nullptr, what + "the grip's node");
            const int32_t index = node->index;
            size_t incident = 0;
            for (const EvalGraphEdge& e : before.edges) incident += (e.from == index || e.to == index) ? 1 : 0;
            assert_true(incident > 0, what + "the grip has edges");
            const size_t listed = the_object(d, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter().states[0].elements.size();

            remove_object(d, grip_handle);
            std::vector<uint8_t> out = save(d, format_of(in), name);
            Drawing r = load(out, name);
            assert_true(r.find_object(grip_handle) == nullptr, what + "the element is gone");
            const EvaluationGraphData& g = the_object(r, ObjectType::EvaluationGraph)->as_evaluation_graph();
            assert_eq(static_cast<int>(before.nodes.size()) - 1, static_cast<int>(g.nodes.size()), what + "its node is gone");
            assert_eq(static_cast<int>(before.edges.size() - incident), static_cast<int>(g.edges.size()), what + "with its edges, no other");
            assert_true(g.find_node_by_id(grip_id) == nullptr, what + "its id is not used again");
            assert_true(g.find_node_by_id(grip_id + 1) != nullptr && g.find_node_by_id(grip_id - 1) != nullptr, what + "the other ids stay");
            assert_eq(before.last_node_id, g.last_node_id, what + "96");
            assert_true(g.links_consistent(), what + "the chains were made again");
            for (const EvalGraphNode& n : g.nodes) assert_true(r.find_object(n.object) != nullptr, what + "every node's element is there");
            // the visibility states list it no more (333 and 95 follow); a state listing every element had it too
            const hdxf::BlockVisibilityParameterData& v = the_object(r, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter();
            assert_eq(static_cast<int>(listed) - 1, static_cast<int>(v.states[0].elements.size()), what + "95 of the first state");
            assert_eq(static_cast<int>(listed) - 1, static_cast<int>(v.states[1].elements.size()), what + "95 of the second state");
            for (const hdxf::BlockVisibilityState& s : v.states) {
                for (const std::string& h : s.elements) assert_true(upper(h) != upper(grip_handle), what + "not listed");
            }
            assert_true(!d.warnings().empty(), what + "a warning says what was left out");
            // the other records are as they were
            if (!binary) {
                std::set<std::string> changed = differing(in, out);
                assert_true(changed.count(the_object(d, ObjectType::EvaluationGraph)->handle) == 1, what + "the graph differs");
                assert_true(changed.count(the_object(d, ObjectType::BlockVisibilityParameter)->handle) == 1, what + "the visibility parameter");
                assert_true(changed.count(grip_handle) == 1, what + "the element is not there");
                for (const std::string& h : changed) {
                    const Object* o = r.find_object(h);
                    if (!o) continue;
                    assert_true(o->type == ObjectType::EvaluationGraph || o->type == ObjectType::BlockVisibilityParameter,
                                what + "no other record changed: " + h);
                }
            }
        }
    }
}

TEST(dynamic_block_relations_an_entity_removed_leaves_the_visibility_lists_and_a_broken_link_stays) {
    for (bool binary : {false, true}) {
        const std::string name = dynbase("2018", binary);
        std::vector<uint8_t> in = facit_bytes(name);
        Drawing d = load(in, name);
        const std::string what = name + ": ";
        const hdxf::BlockVisibilityParameterData before = the_object(d, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter();
        const std::string gone = before.entities[0];
        remove_entity_of_block(d, "HDXF_DYNAMIC", gone);
        std::vector<uint8_t> out = save(d, format_of(in), name);
        Drawing r = load(out, name);
        const hdxf::BlockVisibilityParameterData& v = the_object(r, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter();
        assert_eq(2, static_cast<int>(v.entities.size()), what + "93 follows the 331");
        assert_eq(2, static_cast<int>(v.states[0].entities.size()), what + "94 of the first state");
        assert_eq(2, static_cast<int>(v.states[1].entities.size()), what + "94 of the second");
        assert_eq(0, static_cast<int>(v.states[2].entities.size()), what + "the empty state is empty");
        assert_eq(14, static_cast<int>(v.states[0].elements.size()), what + "the elements are untouched");
        for (const std::string& h : v.entities) assert_true(upper(h) != upper(gone), what + "not listed");
        assert_eq(3, static_cast<int>(v.states.size()), what + "every state stays");
    }

    // A link the file had broken: a 333 handle of a record that is not there is kept as it was, and a removal beside it
    // leaves it (the count does not drop for it)
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_dynbase.dxf");
    std::string text(in.begin(), in.end());
    const std::string needle = "333\r\n262\r\n";
    size_t at = text.find(needle);
    assert_true(at != std::string::npos, "the element in the first state");
    text.replace(at, needle.size(), "333\r\nFFE\r\n");
    std::vector<uint8_t> broken(text.begin(), text.end());
    Drawing d = load(broken, "broken");
    assert_true(save(d, DxfFormat::Ascii, "broken") == broken, "written unchanged: the broken link stays");
    const std::string removed = the_object(d, ObjectType::BlockLinearGrip)->handle;
    remove_object(d, removed);
    Drawing r = load(save(d, DxfFormat::Ascii, "broken"), "broken");
    const hdxf::BlockVisibilityState& s = the_object(r, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter().states[0];
    assert_eq(13, static_cast<int>(s.elements.size()), "one member went (the removed element), not the broken link");
    assert_eq(std::string("FFE"), s.elements[0], "the broken link is where it was");
}

TEST(dynamic_block_relations_the_block_removed_takes_its_graph_and_elements_and_zeroes_the_representation) {
    for (const char* tag : {"2000", "2010", "2018"}) {
        const std::string name = dynbase(tag, false);
        std::vector<uint8_t> in = facit_bytes(name);
        Drawing d = load(in, name);
        const std::string what = name + ": ";
        const Object* graph = the_object(d, ObjectType::EvaluationGraph);
        const std::string graph_owner = graph->owner;
        const hdxf::Block* definition = nullptr;
        for (const hdxf::Block& b : d.blocks()) {
            if (!b.record.xdictionary.empty() && upper(b.record.xdictionary) == upper(graph_owner)) definition = &b;
        }
        assert_true(definition != nullptr, what + "the definition");
        const std::string definition_name = definition->name;
        const std::string record = definition->record.handle;
        // the definition and its INSERT go; the representation's block and INSERT stay
        auto& blocks = d.blocks();
        blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [&](const hdxf::Block& b) { return b.name == definition_name; }), blocks.end());
        auto& entities = d.entities();
        entities.erase(std::remove_if(entities.begin(), entities.end(), [&](const hdxf::Entity& e) {
                           return e.type == hdxf::EntityType::Insert && e.as_insert().block_name == definition_name;
                       }), entities.end());
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, name);
        Drawing r = load(out, name);
        for (ObjectType t : {ObjectType::EvaluationGraph, ObjectType::BlockGripLocation, ObjectType::BlockLinearParameter,
                             ObjectType::BlockLinearGrip, ObjectType::BlockVisibilityParameter, ObjectType::BlockVisibilityGrip,
                             ObjectType::DynamicBlockPurgePreventer}) {
            assert_eq(0, static_cast<int>(objects_of(r, t).size()), what + "the definition's owner chain is gone: its extension dictionary, graph, elements");
        }
        // the representation stays, its pointer to the original is a null pointer now
        const Object* representation = the_object(r, ObjectType::BlockRepresentation);
        assert_eq(std::string("0"), representation->as_block_representation().original_block_record, what + "340 is 0");
        bool warned = false;
        for (const std::string& w : r.warnings()) warned = warned || w.find("ACDB_BLOCKREPRESENTATION_DATA") != std::string::npos;
        (void)warned;
        // (the warning is the save's: d's)
        bool said = false;
        for (const std::string& w : d.warnings()) said = said || w.find("ACDB_BLOCKREPRESENTATION_DATA") != std::string::npos;
        assert_true(said, what + "the save said so");
    }
    // AutoCAD's PURGE of both blocks and their INSERTs: no record of the types is left (the facit file's own state)
    for (const char* tag : {"2000", "2018"}) {
        const std::string name = dynbase(tag, false, "purged");
        Drawing purged = load(facit_bytes(name), name);
        for (const Object& o : purged.objects()) assert_true(!is_base_type(o.type), std::string(tag) + ": AutoCAD's purged file has no record of the types");
        Drawing d = load(facit_bytes(dynbase(tag, false)), name);
        auto& blocks = d.blocks();
        blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](const hdxf::Block& b) { return b.name == "HDXF_DYNAMIC" || b.name[0] == '*' && (b.name[1] == 'U'); }), blocks.end());
        d.entities().clear();
        Drawing r = load(save(d, DxfFormat::Ascii, name), name);
        for (const Object& o : r.objects()) assert_true(!is_base_type(o.type), std::string(tag) + ": nor is ours after the same removal");
    }
}

// ---------------------------------------------------------------------------
// A complete typed definition built in memory
// ---------------------------------------------------------------------------

namespace {

struct Ids {
    int32_t parameter = 0, grip = 0, x = 0, y = 0, visibility = 0, visibility_grip = 0, vx = 0, vy = 0, scale = 0;
};

Drawing build_definition(DxfVersion v, Ids& ids) {
    Drawing d(v);
    d.add_block("DYN");
    hdxf::Entity line = d.add_line({0, 0, 0}, {40, 0, 0});
    line.props.handle = "A01";
    hdxf::Entity circle = d.add_circle({10, 7, 0}, 4);
    circle.props.handle = "A02";
    d.entities().clear();
    d.find_block("DYN")->entities = {line, circle};
    d.add_dynamic_block_graph("DYN");

    hdxf::BlockLinearParameterData p;
    p.element.name = "Linear";
    p.two_point.second = {40, 0, 0};
    p.label = "Length";
    p.label_offset = -10;
    p.two_point.grip_node_ids = {0, 0, 0, 0};
    ids.parameter = d.add_dynamic_block_element("DYN", ObjectType::BlockLinearParameter, Boxed<hdxf::BlockLinearParameterData>(p))
                        .as_block_linear_parameter().eval.node_id;
    auto component = [&](int32_t parameter, const char* property, double value) {
        BlockGripLocationData c;
        c.parameter_node_id = parameter;
        c.property_name = property;
        c.eval.expression = hdxf::EvalExprValue{"", hdxf::EvalVariant{40, value, 0}};
        return d.add_dynamic_block_element("DYN", ObjectType::BlockGripLocation, Boxed<BlockGripLocationData>(c))
            .as_block_grip_location().eval.node_id;
    };
    ids.x = component(ids.parameter, "UpdatedEndX", 40.0);
    ids.y = component(ids.parameter, "UpdatedEndY", 0.0);
    hdxf::BlockLinearGripData g;
    g.element.name = "End Grip";
    g.grip.x_node_id = ids.x;
    g.grip.y_node_id = ids.y;
    g.grip.location = {40, 0, 0};
    g.vector = {40, 0, 0};
    ids.grip = d.add_dynamic_block_element("DYN", ObjectType::BlockLinearGrip, Boxed<hdxf::BlockLinearGripData>(g))
                   .as_block_linear_grip().eval.node_id;
    {
        Object* po = d.dynamic_block_element("DYN", ids.parameter);
        auto& two = po->as_block_linear_parameter().two_point;
        two.grip_node_ids[1] = ids.grip;
        two.links[2] = hdxf::BlockPropertyLink{ids.grip, "DisplacementX"};
        two.links[3] = hdxf::BlockPropertyLink{ids.grip, "DisplacementY"};
    }
    hdxf::BlockVisibilityGripData vg;
    vg.element.name = "Grip";
    vg.grip.flag_280 = 0;
    ids.visibility_grip = d.add_dynamic_block_element("DYN", ObjectType::BlockVisibilityGrip, Boxed<hdxf::BlockVisibilityGripData>(vg))
                              .as_block_visibility_grip().eval.node_id;
    hdxf::BlockVisibilityParameterData vp;
    vp.element.name = "Visibility State";
    vp.one_point.grip_node_id = ids.visibility_grip;
    vp.name = "Visibility1";
    vp.entities = {"A01", "A02"};
    ids.visibility = d.add_dynamic_block_element("DYN", ObjectType::BlockVisibilityParameter, Boxed<hdxf::BlockVisibilityParameterData>(vp))
                         .as_block_visibility_parameter().eval.node_id;
    ids.vx = component(ids.visibility, "UpdatedX", 0.0);
    ids.vy = component(ids.visibility, "UpdatedY", 0.0);
    hdxf::BlockScaleActionData sa;
    sa.element.name = "Scale";
    sa.action.selection = {"A01", "A02"};
    sa.base_point_action.x_node_id = ids.x;
    sa.base_point_action.y_node_id = ids.y;
    sa.base_point_action.x_name = "UpdatedBaseX";
    sa.base_point_action.y_name = "UpdatedBaseY";
    sa.properties = {hdxf::BlockPropertyLink{ids.parameter, "Scale"}, hdxf::BlockPropertyLink{ids.parameter, "XScale"},
                     hdxf::BlockPropertyLink{ids.parameter, "YScale"}};
    ids.scale = d.add_dynamic_block_element("DYN", ObjectType::BlockScaleAction, Boxed<hdxf::BlockScaleActionData>(sa))
                    .as_block_scale_action().eval.node_id;

    // the visibility parameter's states name the entities and the elements
    {
        Object* vo = d.dynamic_block_element("DYN", ids.visibility);
        auto& vv = vo->as_block_visibility_parameter();
        hdxf::BlockVisibilityState all;
        all.name = "All";
        all.entities = {"A01", "A02"};
        for (int32_t id : {ids.parameter, ids.grip, ids.scale}) all.elements.push_back(d.dynamic_block_element("DYN", id)->handle);
        hdxf::BlockVisibilityState none;
        none.name = "None";
        vv.states = {all, none};
    }
    // the dependencies
    Object* graph = d.dynamic_block_graph("DYN");
    EvaluationGraphData& eg = graph->as_evaluation_graph();
    auto index = [&](int32_t id) { return eg.find_node_by_id(id)->index; };
    eg.add_edge(index(ids.parameter), index(ids.x));
    eg.add_edge(index(ids.parameter), index(ids.y));
    eg.add_edge(index(ids.x), index(ids.grip));
    eg.add_edge(index(ids.y), index(ids.grip));
    eg.add_edge(index(ids.grip), index(ids.parameter), 2, true);
    eg.add_edge(index(ids.visibility), index(ids.vx));
    eg.add_edge(index(ids.visibility), index(ids.vy));

    d.add_insert("DYN", {0, 0, 0});
    d.add_dynamic_block_representation("DYN", {100, 0, 0});
    return d;
}

} // namespace

TEST(dynamic_block_relations_a_new_complete_typed_definition_in_every_version_and_format) {
    for (DxfVersion v : kVersions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Ids ids;
            Drawing d = build_definition(v, ids);
            const std::string what = std::string(hdxf_test::version_label(v)) + " " + hdxf_test::format_label(f) + ": ";
            std::vector<uint8_t> out = save(d, f, what);
            Drawing r = load(out, what);
            assert_true(r.warnings().empty(), what + "no warning");
            // B12: written again unchanged
            assert_true(save(r, f, what) == out, what + "written again, the same bytes");

            const Object* graph_object = the_object(r, ObjectType::EvaluationGraph);
            const EvaluationGraphData& g = graph_object->as_evaluation_graph();
            assert_eq(9, static_cast<int>(g.nodes.size()), what + "nine elements, nine nodes");
            assert_eq(8, static_cast<int>(g.edges.size()), what + "eight edges");
            assert_eq(9, g.last_node_id, what + "96");
            assert_true(g.links_consistent(), what + "chains");
            // every element: owned by the graph, its AcDbEvalExpr id is its node's
            for (const EvalGraphNode& n : g.nodes) {
                const Object* e = r.find_object(n.object);
                assert_true(e != nullptr, what + "the node's element");
                assert_eq(upper(graph_object->handle), upper(e->owner), what + "owned by the graph");
            }
            // the definition: found by its true name; before 2010 the anonymous *U block with BLOCK flag 1
            const hdxf::Block* definition = nullptr;
            const hdxf::Block* anonymous = nullptr;
            for (const hdxf::Block& b : r.blocks()) {
                if (!b.record.xdictionary.empty() && upper(b.record.xdictionary) == upper(graph_object->owner)) definition = &b;
                if (r.dynamic_block_original(b)) anonymous = &b;
            }
            assert_true(definition != nullptr && anonymous != nullptr, what + "the definition and its representation");
            assert_eq(std::string("DYN"), r.dynamic_block_true_name(*definition), what + "the true name");
            if (v < DxfVersion::R2010) {
                assert_true(definition->name.rfind("*U", 0) == 0, what + "the anonymous name before 2010: " + definition->name);
                assert_eq(1, definition->flags & 1, what + "BLOCK flag 1");
            } else {
                assert_eq(std::string("DYN"), definition->name, what + "its own name from 2010");
            }
            assert_true(r.dynamic_block_original(*anonymous) == definition, what + "the representation's original");
            assert_eq(1, anonymous->flags & 1, what + "the anonymous block's flag");
            assert_eq(2, static_cast<int>(anonymous->entities.size()), what + "its copy of the entities");
            // both INSERTs name those blocks
            int named_definition = 0, named_anonymous = 0;
            for (const hdxf::Entity& e : r.entities()) {
                if (e.type != hdxf::EntityType::Insert) continue;
                if (e.as_insert().block_name == definition->name) ++named_definition;
                if (e.as_insert().block_name == anonymous->name) ++named_anonymous;
            }
            assert_eq(1, named_definition, what + "the INSERT of the definition");
            assert_eq(1, named_anonymous, what + "the INSERT of the representation");
            // the representation record points back at the original's BLOCK_RECORD
            assert_eq(upper(definition->record.handle), upper(the_object(r, ObjectType::BlockRepresentation)->as_block_representation().original_block_record),
                      what + "340");
            assert_true(!definition->record.xdata.empty() && definition->record.xdata[0].app_name == "AcDbBlockRepETag", what + "the tag AutoCAD keeps first");
            // the typed values
            const hdxf::BlockLinearParameterData& p = the_object(r, ObjectType::BlockLinearParameter)->as_block_linear_parameter();
            assert_eq(ids.parameter, p.eval.node_id, what + "the parameter's id");
            assert_eq(ids.grip, p.two_point.grip_node_ids[1], what + "the grip's id in the parameter");
            assert_eq(33, p.eval.version, what + "eval version");
            const bool early = v < DxfVersion::R2007;
            assert_eq(early ? 25 : 33, *p.element.version, what + "the element version follows the DXF version");
            assert_eq(early ? 104 : 327, *p.element.maintenance, what + "and its build");
            const hdxf::BlockVisibilityParameterData& vis = the_object(r, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter();
            assert_eq(2, static_cast<int>(vis.states.size()), what + "two states");
            assert_eq(3, static_cast<int>(vis.states[0].elements.size()), what + "three elements in the first");
            for (const std::string& h : vis.entities) {
                bool found = false;
                for (const hdxf::Entity& e : definition->entities) found = found || upper(e.props.handle) == upper(h);
                assert_true(found, what + "a visibility entity is an entity of the block");
            }
            const hdxf::BlockScaleActionData& sa = the_object(r, ObjectType::BlockScaleAction)->as_block_scale_action();
            assert_eq(2, static_cast<int>(sa.action.selection.size()), what + "the action's selection");
            for (const std::string& h : sa.action.selection) assert_true(r.find_entity(h) != nullptr, what + "an entity of the block");
        }
    }
}

TEST(dynamic_block_relations_a_removed_element_or_entity_of_a_definition_built_in_memory_follows_through) {
    Ids ids;
    Drawing d = build_definition(DxfVersion::R2018, ids);
    // the program removes the grip through the drawing: its node, its edges, and the visibility state's member go
    const std::string grip = d.dynamic_block_element("DYN", ids.grip)->handle;
    const size_t objects_before = d.objects().size();
    assert_true(d.remove_dynamic_block_element(grip), "removed");
    assert_eq(static_cast<int>(objects_before) - 1, static_cast<int>(d.objects().size()), "one object less");
    assert_false(d.remove_dynamic_block_element(grip), "not twice");
    const EvaluationGraphData& g = d.dynamic_block_graph("DYN")->as_evaluation_graph();
    assert_eq(8, static_cast<int>(g.nodes.size()), "its node");
    assert_true(g.find_node_by_id(ids.grip) == nullptr, "by id too");
    assert_eq(4, static_cast<int>(g.edges.size()), "the edges of the grip (two into it and the pair) went: parameter->x, parameter->y and the two visibility ones stay");
    assert_true(g.links_consistent(), "chains");
    // an entity of the block removed with no API: the written lists leave it
    remove_entity_of_block(d, "DYN", "A01");
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "memory");
    Drawing r = load(out, "memory");
    const hdxf::BlockVisibilityParameterData& v = the_object(r, ObjectType::BlockVisibilityParameter)->as_block_visibility_parameter();
    assert_eq(1, static_cast<int>(v.entities.size()), "93 and 331: one entity left");
    assert_eq(1, static_cast<int>(v.states[0].entities.size()), "94");
    assert_eq(2, static_cast<int>(v.states[0].elements.size()), "95: the removed grip is not listed");
    assert_eq(1, static_cast<int>(the_object(r, ObjectType::BlockScaleAction)->as_block_scale_action().action.selection.size()), "the selection");
    // and an element removed from the objects of the drawing built in memory is a dangling node no more
    Ids ids2;
    Drawing e = build_definition(DxfVersion::R2018, ids2);
    remove_object(e, e.dynamic_block_element("DYN", ids2.x)->handle);
    Drawing r2 = load(save(e, DxfFormat::Binary, "memory"), "memory");
    assert_eq(8, static_cast<int>(the_object(r2, ObjectType::EvaluationGraph)->as_evaluation_graph().nodes.size()), "the node of a removed object goes");
    assert_true(!e.warnings().empty(), "and the save says so");
}

// ---------------------------------------------------------------------------
// Hostile and future input (B105)
// ---------------------------------------------------------------------------

namespace {

std::vector<uint8_t> replaced(const std::vector<uint8_t>& in, const std::string& from, const std::string& to) {
    std::string text(in.begin(), in.end());
    size_t at = text.find(from);
    assert_true(at != std::string::npos, "the text to change is in the file: " + from);
    text.replace(at, from.size(), to);
    return std::vector<uint8_t>(text.begin(), text.end());
}

bool warned_about(const Drawing& d, const std::string& text) {
    for (const std::string& w : d.warnings()) if (w.find(text) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(dynamic_block_relations_a_damaged_or_future_record_stays_as_read_with_a_warning) {
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_dynbase.dxf");
    // a node cut short (the last of its four 92 is missing): the graph is not typed, it is kept and written as it was
    {
        std::vector<uint8_t> cut = replaced(in, " 92\r\n       -1\r\n 92\r\n       -1\r\n 92\r\n       -1\r\n 92\r\n       -1\r\n 91\r\n        1\r\n",
                                            " 92\r\n       -1\r\n 92\r\n       -1\r\n 92\r\n       -1\r\n 91\r\n        1\r\n");
        Drawing d = load(cut, "cut");
        bool untyped = false;
        for (const Object& o : d.objects()) untyped = untyped || (o.type == ObjectType::Unknown && o.as_unknown().object_type == "ACAD_EVALUATION_GRAPH");
        assert_true(untyped, "the damaged graph is not typed");
        assert_true(warned_about(d, "ACAD_EVALUATION_GRAPH"), "and the warning names it");
        assert_eq(1, static_cast<int>(objects_of(d, ObjectType::BlockLinearParameter).size()), "the other records are typed");
        assert_true(save(d, DxfFormat::Ascii, "cut") == cut, "written back as it was read");
    }
    // a count above what the record can hold (92 of the visibility parameter): no allocation, an untyped record
    {
        std::vector<uint8_t> big = replaced(in, " 92\r\n        3\r\n303\r\nVisibilityState0", " 92\r\n  2000000000\r\n303\r\nVisibilityState0");
        Drawing d = load(big, "big");
        assert_eq(0, static_cast<int>(objects_of(d, ObjectType::BlockVisibilityParameter).size()), "not typed");
        assert_true(warned_about(d, "BLOCKVISIBILITYPARAMETER"), "the warning");
        assert_true(save(d, DxfFormat::Ascii, "big") == big, "kept as read");
    }
    // a value set that counts values it does not have (175 is 2, no 144 follows): since step 9.13 the values are the groups 144
    {
        std::vector<uint8_t> values = replaced(in, "143\r\n0.0\r\n175\r\n     0\r\n", "143\r\n0.0\r\n175\r\n     2\r\n");
        Drawing d = load(values, "values");
        assert_eq(0, static_cast<int>(objects_of(d, ObjectType::BlockLinearParameter).size()), "not typed");
        bool said = false;
        for (const std::string& w : d.warnings()) said = said || (w.find("group 175") != std::string::npos);
        std::string all_warnings;
        for (const std::string& w : d.warnings()) all_warnings += w + " | ";
        assert_true(said, "the warning names the count of the value set: " + all_warnings);
        assert_true(save(d, DxfFormat::Ascii, "values") == values, "kept as read");
    }
    // a property slot flag that is neither 0 nor 1
    {
        std::vector<uint8_t> flag = replaced(in, "173\r\n     1\r\n 94\r\n        7\r\n", "173\r\n     5\r\n 94\r\n        7\r\n");
        Drawing d = load(flag, "flag");
        assert_eq(0, static_cast<int>(objects_of(d, ObjectType::BlockLinearParameter).size()), "not typed");
        assert_true(warned_about(d, "BLOCKLINEARPARAMETER"), "the warning");
    }
    // a later version of the record: groups after the ones HDxf knows are kept with a warning, and survive a change
    {
        std::vector<uint8_t> future = replaced(in, "100\r\nAcDbBlockVisibilityGrip\r\n", "100\r\nAcDbBlockVisibilityGrip\r\n 90\r\n        7\r\n");
        Drawing d = load(future, "future");
        assert_eq(1, static_cast<int>(objects_of(d, ObjectType::BlockVisibilityGrip).size()), "still typed");
        assert_true(warned_about(d, "BLOCKVISIBILITYGRIP"), "with a warning");
        assert_true(save(d, DxfFormat::Ascii, "future") == future, "written as it was");
        the_object(d, ObjectType::BlockVisibilityGrip);
        for (Object& o : d.objects()) if (o.type == ObjectType::BlockVisibilityGrip) o.as_block_visibility_grip().element.name = "Changed";
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "future");
        const std::string tail = "AcDbBlockVisibilityGrip\r\n 90\r\n        7\r\n";
        assert_true(std::search(out.begin(), out.end(), tail.begin(), tail.end()) != out.end(),
                    "the group of the future version is still in the record after a change");
    }
    // class data (1010) is not XDATA and XDATA is not class data: a record whose group 1010 is damaged is not typed
    {
        std::vector<uint8_t> text = replaced(in, "1010\r\n40.0\r\n1020\r\n0.0\r\n1030\r\n0.0\r\n280\r\n     1\r\n 93\r\n       -1\r\n100\r\nAcDbBlockLinearGrip",
                                             "1010\r\nabc\r\n1020\r\n0.0\r\n1030\r\n0.0\r\n280\r\n     1\r\n 93\r\n       -1\r\n100\r\nAcDbBlockLinearGrip");
        Drawing d = load(text, "text");
        assert_eq(0, static_cast<int>(objects_of(d, ObjectType::BlockLinearGrip).size()), "a damaged number is not typed");
    }
}

TEST(dynamic_block_relations_the_older_dxf_forms_are_not_made_where_they_are_not_known) {
    // R13 and R14 are not claimed: a graph read from there would be an error, and the types are not in a new R12
    Ids ids;
    Drawing d = build_definition(DxfVersion::R2000, ids);
    // before 2010 the definition's anonymous name is the writer's: the drawing keeps its own
    assert_true(d.find_block("DYN") != nullptr, "the drawing keeps the name");
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "2000");
    assert_true(d.find_block("DYN") != nullptr, "after the save too");
    assert_eq(std::string("DYN"), d.dynamic_block_true_name(*d.find_block("DYN")), "the true name");
    std::string text = hdxf_test::lf_text(out);
    assert_true(text.find("\n  2\n*U2\n") != std::string::npos || text.find("\n  2\n*U1\n") != std::string::npos,
                "an anonymous name in the file");
    assert_true(text.find("\n1000\nDYN\n") != std::string::npos, "and the true name in the XDATA");
}

