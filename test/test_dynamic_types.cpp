#include "test_framework.hpp"
#include "hdxf.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include "object_data_visit.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "dynamic_block_helpers.hpp"

// ---------------------------------------------------------------------------
// Step 9.13 of phase 9 (plan 2026-10-27-fas-9-typer.md; B100-B105, B109, B113-B116; B12 and B19): the dynamic block parameters,
// grips, actions and constraints that step 9.9 did not type — BLOCKALIGNMENTPARAMETER/GRIP, BLOCKBASEPOINTPARAMETER,
// BLOCKFLIPPARAMETER/GRIP/ACTION, BLOCKLOOKUPPARAMETER/GRIP/ACTION, BLOCKMOVEACTION, BLOCKPOINTPARAMETER, BLOCKPOLARPARAMETER/GRIP,
// BLOCKPOLARSTRETCHACTION, BLOCKPROPERTIESTABLE/GRIP, BLOCKROTATIONPARAMETER/GRIP, BLOCKROTATEACTION, BLOCKSTRETCHACTION,
// BLOCKARRAYACTION, BLOCKUSERPARAMETER, BLOCKXYPARAMETER/GRIP, BLOCKHORIZONTAL/VERTICAL/RADIALCONSTRAINTPARAMETER,
// BLOCKPARAMDEPENDENCYBODY and ACDB_DYNAMICBLOCKPROXYNODE (and, from the chain of their siblings, BLOCKALIGNEDCONSTRAINTPARAMETER and
// BLOCKLINEARCONSTRAINTPARAMETER, which no AutoCAD file has).
//
// The AutoCAD originals: doc/facit/dyntypes/<sample>/acad_<version>_dxf_dyntypes[_erased|_purged][_bin].dxf, made by
// fas9_dynamic_types.scr from the DWG samples of AutoCAD 2025's Sample/Dynamic Blocks (thirteen drawings; R12 … 2018, ASCII and
// binary). They are local files (the facit is not in git): a test whose files are missing is skipped.
// ---------------------------------------------------------------------------

using hdxf::Boxed;
using hdxf::Point2;
using hdxf::Point3;

namespace {

const std::vector<std::string> kSamples = {
    "Annotation_-_Imperial",   "Annotation_-_Metric",  "Architectural_-_Imperial", "Architectural_-_Metric", "Civil_-_Imperial",
    "Civil_-_Metric",          "Electrical_-_Imperial", "Electrical_-_Metric",       "Mechanical_-_Imperial",  "Mechanical_-_Metric",
    "Multileader_Tools",       "Structural_-_Imperial", "Structural_-_Metric"};

// The three drawings every version and format is read from; the others only in 2018
const std::vector<std::string> kVersionSamples = {"Mechanical_-_Metric", "Architectural_-_Metric", "Annotation_-_Metric"};

// The DXF names of the types of the step (the ones AutoCAD's samples have)
const std::vector<std::string> kNewNames = {
    "BLOCKALIGNMENTPARAMETER", "BLOCKALIGNMENTGRIP", "BLOCKBASEPOINTPARAMETER", "BLOCKFLIPPARAMETER", "BLOCKFLIPGRIP",
    "BLOCKFLIPACTION", "BLOCKLOOKUPPARAMETER", "BLOCKLOOKUPGRIP", "BLOCKLOOKUPACTION", "BLOCKMOVEACTION", "BLOCKPOINTPARAMETER",
    "BLOCKPOLARPARAMETER", "BLOCKPOLARGRIP", "BLOCKPOLARSTRETCHACTION", "BLOCKPROPERTIESTABLE", "BLOCKPROPERTIESTABLEGRIP",
    "BLOCKROTATIONPARAMETER", "BLOCKROTATIONGRIP", "BLOCKROTATEACTION", "BLOCKSTRETCHACTION", "BLOCKUSERPARAMETER",
    "BLOCKXYPARAMETER", "BLOCKXYGRIP", "BLOCKARRAYACTION", "BLOCKHORIZONTALCONSTRAINTPARAMETER",
    "BLOCKVERTICALCONSTRAINTPARAMETER", "BLOCKRADIALCONSTRAINTPARAMETER", "BLOCKPARAMDEPENDENCYBODY",
    "ACDB_DYNAMICBLOCKPROXYNODE"};

// The two types that follow from their siblings' chain: no AutoCAD file has a record of them
const std::vector<std::string> kUnverifiedNames = {"BLOCKALIGNEDCONSTRAINTPARAMETER", "BLOCKLINEARCONSTRAINTPARAMETER"};

// Registered by AutoCAD's CLASSES or named by the plan; no record in any file, and no chain to derive: not typed
const std::vector<std::string> kUntypedNames = {"BLOCKANGULARCONSTRAINTPARAMETER", "BLOCKANGULARCONSTRAINTPARAMETERENTITY",
                                                "BLOCKDIAMETRICCONSTRAINTPARAMETER"};

bool is_new_type(ObjectType t) { return t >= ObjectType::BlockAlignmentParameter && t <= ObjectType::DynamicBlockProxyNode; }

std::string dyn_file(const char* tag, bool binary, const char* state = "") {
    return std::string("acad_") + tag + "_dxf_dyntypes" + (*state ? std::string("_") + state : std::string()) + (binary ? "_bin" : "") + ".dxf";
}

std::vector<uint8_t> dyn_bytes(const std::string& sample, const char* tag, bool binary, const char* state = "") {
    return hdxf_test::read_file_bytes(facit_path("dyntypes/" + sample + "/" + dyn_file(tag, binary, state)));
}

// The 2018 ASCII drawing of a sample, read once
const Drawing& sample(const std::string& name) {
    static std::map<std::string, std::unique_ptr<Drawing>> cache;
    auto it = cache.find(name);
    if (it == cache.end()) {
        std::vector<uint8_t> bytes = dyn_bytes(name, "2018", false);
        if (bytes.empty()) hdxf_test::skip("missing doc/facit/dyntypes/" + name);
        it = cache.emplace(name, std::make_unique<Drawing>(load(bytes, name))).first;
    }
    return *it->second;
}

template <class Pred>
const Object* first_of(const Drawing& d, ObjectType t, Pred pred) {
    for (const Object& o : d.objects()) {
        if (o.type == t && pred(o)) return &o;
    }
    return nullptr;
}

const Object* with_handle(const Drawing& d, const char* handle) {
    const Object* o = d.find_object(handle);
    assert_true(o != nullptr, std::string("the record ") + handle);
    return o;
}

std::map<std::string, Rec> by_handle(const std::vector<uint8_t>& bytes) {
    std::map<std::string, Rec> out;
    for (const Rec& r : ascii_records(bytes)) {
        if (trim(r[0].second) == "SECTION") continue;
        for (const Group& g : r) {
            if (g.first == 5) { out[trim(g.second)] = r; break; }
            if (g.first == 100) break;
        }
    }
    return out;
}

std::set<std::string> differing(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    std::map<std::string, Rec> x = by_handle(a), y = by_handle(b);
    std::set<std::string> out;
    for (const auto& [h, r] : x) {
        auto it = y.find(h);
        if (it == y.end() || r != it->second) out.insert(h);
    }
    for (const auto& [h, r] : y) if (!x.count(h)) out.insert(h);
    return out;
}

// Two records as groups; numbers to `relative` (a binary file has the full double where the ASCII twin has AutoCAD's 16 digits)
std::string same_record_within(const Rec& a, const Rec& b, double relative) {
    if (a.size() != b.size()) return "the records have " + std::to_string(a.size()) + " and " + std::to_string(b.size()) + " groups";
    for (size_t i = 0; i < a.size(); ++i) {
        bool same = a[i].first == b[i].first;
        if (same && trim(a[i].second) != trim(b[i].second)) {
            char* ex = nullptr;
            char* ey = nullptr;
            const std::string x = trim(a[i].second), y = trim(b[i].second);
            const double dx = std::strtod(x.c_str(), &ex), dy = std::strtod(y.c_str(), &ey);
            same = !x.empty() && !y.empty() && *ex == 0 && *ey == 0 && std::fabs(dx - dy) <= relative * std::max(1.0, std::max(std::fabs(dx), std::fabs(dy)));
        }
        if (!same) {
            return "group " + std::to_string(i) + ": " + std::to_string(a[i].first) + " \"" + trim(a[i].second) + "\" and " +
                   std::to_string(b[i].first) + " \"" + trim(b[i].second) + "\"";
        }
    }
    return {};
}

bool near(double a, double b) { return std::fabs(a - b) <= 1e-12 * std::max(1.0, std::max(std::fabs(a), std::fabs(b))); }

bool names_typed_record(const std::string& text) {
    for (const std::string& n : kNewNames) {
        if (text.find(n) != std::string::npos) return true;
    }
    for (const std::string& n : kUnverifiedNames) {
        if (text.find(n) != std::string::npos) return true;
    }
    return false;
}

std::string ascii_text(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

// All of the files of the originals that exist: sample, version tag, format, state
struct Original {
    std::string sample;
    const char* tag;
    bool binary;
    std::string state;
    std::vector<uint8_t> bytes;
};

std::vector<Original> originals(bool all_versions, const char* state = "") {
    std::vector<Original> out;
    for (const std::string& s : kSamples) {
        for (size_t t = 0; t < 6; ++t) {
            const char* tag = kTags[t];
            const bool every = all_versions && std::find(kVersionSamples.begin(), kVersionSamples.end(), s) != kVersionSamples.end();
            if (!every && std::string(tag) != "2018") continue;
            for (bool binary : {false, true}) {
                std::vector<uint8_t> b = dyn_bytes(s, tag, binary, state);
                if (b.empty()) continue;
                out.push_back({s, tag, binary, state, std::move(b)});
            }
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

TEST(dynamic_types_the_registry_has_every_codec_and_names_what_is_not_typed) {
    const hdxf::internal::TypedRegistry& registry = hdxf::internal::TypedRegistry::instance();
    for (const std::string& n : kNewNames) {
        const hdxf::internal::ObjectCodec* c = registry.find_object(n);
        assert_true(c != nullptr, n + " has a codec");
        assert_eq(13, c->step, n + ": step 9.13");
        assert_true(c->cls.cpp_name != nullptr, n + " has its CLASSES entry");
        assert_true(c->versions.at(DxfVersion::R12, DxfFormat::Ascii).basis == hdxf::internal::ErrorBasis::Documented, n + ": R12 has the named error");
        for (DxfVersion v : kVersions) {
            assert_true(c->versions.at(v, DxfFormat::Ascii).autocad == hdxf::internal::AutoCadStatus::Verified, n + ": AutoCAD's own files of 2000 … 2018");
        }
    }
    for (const std::string& n : kUnverifiedNames) {
        const hdxf::internal::ObjectCodec* c = registry.find_object(n);
        assert_true(c != nullptr, n + " has a codec");
        for (DxfVersion v : kVersions) {
            assert_true(c->versions.at(v, DxfFormat::Ascii).generation == hdxf::internal::Generation::Native, n + ": made in 2000 … 2018");
            assert_true(c->versions.at(v, DxfFormat::Ascii).autocad == hdxf::internal::AutoCadStatus::NotTested, n + ": ej testat, no AutoCAD record");
        }
    }
    // the classes are the ones AutoCAD's CLASSES have (the record's own subclass can be spelled otherwise: "LookUp")
    assert_eq(std::string("AcDbBlockLookupParameter"), std::string(registry.find_object("BLOCKLOOKUPPARAMETER")->cls.cpp_name), "the lookup parameter's class");
    assert_eq(1, registry.find_object("BLOCKPARAMDEPENDENCYBODY")->cls.proxy_flags, "the dependency body's proxy flags are 1");
    assert_eq(1153, registry.find_object("BLOCKFLIPACTION")->cls.proxy_flags, "an element's proxy flags are 1153");
    for (const std::string& n : kUntypedNames) {
        assert_false(registry.implemented(n), n + ": no AutoCAD record and no chain to derive: not typed");
        const hdxf::internal::TypeRegistryEntry* e = registry.find_entry(n);
        assert_true(e != nullptr, n + " is in the register");
    }
}

// ---------------------------------------------------------------------------
// What AutoCAD's own files hold, field by field
// ---------------------------------------------------------------------------

TEST(dynamic_types_autocads_mechanical_metric_sample_field_by_field) {
    const Drawing& d = sample("Mechanical_-_Metric");

    // alignment
    {
        const hdxf::BlockAlignmentParameterData& p = with_handle(d, "9C8")->as_block_alignment_parameter();
        assert_eq(13, p.eval.node_id, "alignment: node id");
        assert_eq(std::string("Alignment"), p.element.name, "alignment: 300");
        assert_eq(0.0, p.two_point.first.x, "alignment: 1010");
        assert_eq(6.349999999999999, p.two_point.second.x, "alignment: 1011");
        assert_eq(4, static_cast<int>(p.two_point.grip_node_ids.size()), "alignment: 170");
        assert_eq(14, p.two_point.grip_node_ids[0], "alignment: its grip's node");
        assert_true(!p.two_point.links[0] && !p.two_point.links[3], "alignment: no slots");
        assert_eq(1, p.flag_280, "alignment: own 280");
        const hdxf::BlockAlignmentGripData& g = with_handle(d, "9C9")->as_block_alignment_grip();
        assert_eq(14, g.eval.node_id, "alignment grip: node id");
        assert_eq(15, g.grip.x_node_id, "alignment grip: 91");
        assert_eq(16, g.grip.y_node_id, "alignment grip: 92");
        assert_eq(0, g.grip.flag_280, "alignment grip: 280");
        assert_eq(6.349999999999999, g.vector.x, "alignment grip: 140");
    }
    // flip: parameter, grip and action
    {
        const hdxf::BlockFlipParameterData& p = with_handle(d, "9C2")->as_block_flip_parameter();
        assert_eq(5, p.eval.node_id, "flip: node id");
        assert_eq(std::string("Flip"), p.element.name, "flip: 300");
        assert_eq(-5.08, p.two_point.first.x, "flip: 1010");
        assert_eq(6, p.two_point.grip_node_ids[0], "flip: its grip");
        assert_eq(std::string("Side"), p.label, "flip: 305");
        assert_eq(std::string("Flips the symbol"), p.description, "flip: 306");
        assert_eq(std::string("Other Side"), p.text_307, "flip: 307");
        assert_eq(std::string("Arrow Side"), p.text_308, "flip: 308");
        assert_eq(std::string("UpdatedFlip"), p.property_name, "flip: 309");
        assert_eq(6, p.flag_96, "flip: 96");
        const hdxf::BlockFlipGripData& g = with_handle(d, "9C3")->as_block_flip_grip();
        assert_eq(8, g.grip.x_node_id, "flip grip: 91");
        assert_eq(9, g.grip.y_node_id, "flip grip: 92");
        assert_eq(-1.269999999999999, g.vector.x, "flip grip: 140");
        assert_eq(7, g.value_93, "flip grip: own 93");
        const hdxf::BlockFlipActionData& a = with_handle(d, "9C7")->as_block_flip_action();
        assert_eq(11, a.eval.node_id, "flip action: node id");
        assert_eq(std::string("Flipsym X"), a.element.name, "flip action: 300");
        assert_eq(3, static_cast<int>(a.action.node_ids.size()), "flip action: 70");
        assert_eq(28, a.action.node_ids[0], "flip action: 91");
        assert_eq(1, a.action.node_ids[2], "flip action: last 91");
        assert_eq(47, static_cast<int>(a.action.selection.size()), "flip action: 71");
        assert_eq(std::string("9BF"), a.action.selection[0], "flip action: first 330");
        assert_eq(-12.7, a.action.location.x, "flip action: 1010");
        assert_eq(5, a.links[0].node_id, "flip action: 92");
        assert_eq(std::string("Flip"), a.links[0].name, "flip action: 301");
        assert_eq(std::string("UpdatedFlip"), a.links[1].name, "flip action: 302");
        assert_eq(std::string("UpdatedBase"), a.links[2].name, "flip action: 303");
        assert_eq(std::string("UpdatedEnd"), a.links[3].name, "flip action: 304");
    }
    // point and move
    {
        const hdxf::BlockPointParameterData& p = with_handle(d, "9CC")->as_block_point_parameter();
        assert_eq(23, p.eval.node_id, "point: node id");
        assert_eq(0, p.parameter.show_properties, "point: 280");
        assert_eq(1, p.parameter.chain_actions, "point: 281");
        assert_eq(2.966128060534576, p.one_point.position.x, "point: 1010");
        assert_eq(std::string("Position"), p.label, "point: 303");
        assert_eq(std::string(), p.description, "point: 304");
        assert_eq(4.490128060534576, p.point_1011.y, "point: 1011");
        const hdxf::BlockMoveActionData& m = with_handle(d, "9CD")->as_block_move_action();
        assert_eq(27, m.eval.node_id, "move: node id");
        assert_eq(std::string("Move Scarf"), m.element.name, "move: 300");
        assert_eq(2, static_cast<int>(m.action.selection.size()), "move: 71");
        assert_eq(23, m.links[0].node_id, "move: 92");
        assert_eq(std::string("XDelta"), m.links[0].name, "move: 301");
        assert_eq(23, m.links[1].node_id, "move: 93");
        assert_eq(std::string("YDelta"), m.links[1].name, "move: 302");
        assert_eq(1.0, m.distance_multiplier, "move: 140");
        assert_eq(5.497787143782137, m.angle_offset, "move: 141 (315 degrees)");
        assert_eq(0, m.flag_280, "move: 280");
    }
    // lookup: parameter, grip and the table
    {
        const hdxf::BlockLookupParameterData& p = with_handle(d, "887")->as_block_lookup_parameter();
        assert_eq(96, p.eval.node_id, "lookup parameter: node id");
        assert_eq(std::string("Size"), p.label, "lookup parameter: 303");
        assert_eq(std::string("Select the nut size"), p.description, "lookup parameter: 304");
        assert_eq(128, p.action_node_id, "lookup parameter: own 94 is the action's node");
        assert_eq(104, p.one_point.grip_node_id, "lookup parameter: 93");
        const hdxf::BlockLookupGripData& g = with_handle(d, "888")->as_block_lookup_grip();
        assert_eq(104, g.eval.node_id, "lookup grip: node id");
        assert_eq(112, g.grip.x_node_id, "lookup grip: 91");
        assert_eq(0, g.grip.value_93, "lookup grip: 93");
        const hdxf::BlockLookupActionData& a = with_handle(d, "88B")->as_block_lookup_action();
        assert_eq(128, a.eval.node_id, "lookup action: node id");
        assert_eq(std::string("Lookup NutSizes"), a.element.name, "lookup action: 300");
        assert_eq(8, static_cast<int>(a.rows.size()), "lookup action: 92");
        assert_eq(4, static_cast<int>(a.columns.size()), "lookup action: 93");
        assert_eq(4, static_cast<int>(a.rows[0].size()), "lookup action: a cell for each column");
        assert_eq(std::string("1.500000000000000"), a.rows[0][0], "lookup action: first cell");
        assert_eq(std::string("M3"), a.rows[0][3], "lookup action: last cell of the first row");
        assert_eq(std::string("M14"), a.rows[7][3], "lookup action: last cell");
        assert_eq(144, a.columns[0].node_id, "lookup action: 94 of the first column");
        assert_eq(40, a.columns[0].value_type, "lookup action: 95");
        assert_eq(2, a.columns[0].unit_type, "lookup action: 96");
        assert_eq(std::string("UpdatedDistance"), a.columns[0].property_name, "lookup action: 304");
        assert_eq(std::string("Custom"), a.columns[0].category, "lookup action: 305");
        assert_eq(1, a.columns[3].value_type, "lookup action: the looked up column holds text");
        assert_eq(1, a.columns[3].flag_282, "lookup action: 282");
        assert_eq(1, a.columns[3].flag_281, "lookup action: 281");
        assert_eq(std::string("lookupString"), a.columns[3].property_name, "lookup action: the looked up property");
        assert_eq(1, a.flag_280, "lookup action: 280");
    }
    // stretch
    {
        const hdxf::BlockStretchActionData& s = with_handle(d, "126")->as_block_stretch_action();
        assert_eq(608, s.eval.node_id, "stretch: node id");
        assert_eq(std::string("Stretch d1b - 0"), s.element.name, "stretch: 300");
        assert_eq(3, static_cast<int>(s.action.selection.size()), "stretch: 71");
        assert_eq(8, s.links[0].node_id, "stretch: 92");
        assert_eq(std::string("EndXDelta"), s.links[0].name, "stretch: 301");
        assert_eq(8, s.links[1].node_id, "stretch: 93");
        assert_eq(std::string("EndYDelta"), s.links[1].name, "stretch: 302");
        assert_eq(2, static_cast<int>(s.frame.size()), "stretch: 72");
        assert_eq(62.0, s.frame[0].x, "stretch: 1011");
        assert_eq(4.0, s.frame[0].y, "stretch: 1021");
        assert_eq(3, static_cast<int>(s.entities.size()), "stretch: 73");
        assert_eq(std::string("10F"), s.entities[0].entity, "stretch: 331");
        assert_eq(1, static_cast<int>(s.entities[0].vertices.size()), "stretch: 74");
        assert_eq(1, s.entities[0].vertices[0], "stretch: 94");
        assert_eq(2, static_cast<int>(s.entities[2].vertices.size()), "stretch: a polyline's two vertices");
        assert_eq(0, static_cast<int>(s.connections.size()), "stretch: 75");
        assert_eq(0.4, s.distance_multiplier, "stretch: 140");
        assert_eq(0.0, s.angle_offset, "stretch: 141");
        // the stretch with connections to other actions: 70 91 on the action, 75 95 76 94 in its own part
        const hdxf::BlockStretchActionData& m3 = with_handle(d, "14E")->as_block_stretch_action();
        assert_eq(std::string("StretchM3"), m3.element.name, "stretch M3: 300");
        assert_eq(14, static_cast<int>(m3.action.node_ids.size()), "stretch M3: 70");
        assert_eq(26, static_cast<int>(m3.action.selection.size()), "stretch M3: 71");
        assert_eq(9, static_cast<int>(m3.entities.size()), "stretch M3: 73");
        assert_eq(9, static_cast<int>(m3.connections.size()), "stretch M3: 75");
        assert_eq(8, m3.connections[0].node_id, "stretch M3: 95");
        assert_eq(2, static_cast<int>(m3.connections[0].vertices.size()), "stretch M3: 76");
        assert_eq(959, m3.connections[8].node_id, "stretch M3: the last connection");
        assert_eq(1, static_cast<int>(m3.connections[8].vertices.size()), "stretch M3: its one vertex");
        assert_eq(1.0, m3.distance_multiplier, "stretch M3: 140");
    }
    // user parameter, properties table and its grip
    {
        const hdxf::BlockUserParameterData& u = with_handle(d, "2B44")->as_block_user_parameter();
        assert_eq(30, u.eval.node_id, "user: node id");
        assert_eq(1, u.element.flags, "user: 1071");
        assert_eq(std::string("2B34"), u.variable, "user: 330 is the ACDBASSOCVARIABLE");
        assert_eq(5, u.parameter_type, "user: 170");
        assert_eq(std::string("AcDbEvalVariant NULL = -9999"), u.text_309, "user: 309");
        const hdxf::BlockPropertiesTableData& t = with_handle(d, "2B40")->as_block_properties_table();
        assert_eq(26, t.eval.node_id, "table: node id");
        assert_eq(2, t.version, "table: 90");
        assert_eq(std::string("Block Table1"), t.name, "table: 300");
        assert_eq(27, t.one_point.grip_node_id, "table: 93 of the 1Pt parameter");
        assert_eq(4, static_cast<int>(t.columns.size()), "table: 91");
        assert_eq(std::string("2B44"), t.columns[0].parameter, "table: the first column's 340 is the user parameter");
        assert_eq(30, t.columns[0].node_id, "table: its node id");
        assert_eq(std::string("UserVariable"), t.columns[0].property_name, "table: 301");
        assert_eq(1, t.columns[0].value.tag, "table: a text value");
        assert_eq(std::string("Custom"), t.columns[0].value.text, "table: its text");
        assert_eq(-9999, t.columns[1].value.tag, "table: no value");
        assert_eq(std::string("2B3C"), t.columns[1].parameter, "table: the second column's parameter");
        assert_eq(std::string("UpdatedDistance"), t.columns[1].property_name, "table: 301");
        assert_true(t.columns[1].flag_291 && t.columns[1].flag_292 && !t.columns[1].flag_290, "table: 290 … 292");
        assert_eq(8, static_cast<int>(t.rows.size()), "table: 92");
        assert_eq(4, static_cast<int>(t.rows[0].values.size()), "table: a value for each column");
        assert_eq(1, t.rows[0].values[0].tag, "table: the row's first value is text");
        assert_eq(std::string("M3"), t.rows[0].values[0].text, "table: M3");
        assert_eq(40, t.rows[0].values[1].tag, "table: a real");
        assert_eq(3.0, t.rows[0].values[1].real, "table: 3.0");
        assert_eq(2.125, t.rows[0].values[3].real, "table: 2.125");
        assert_eq(7, t.rows[7].index, "table: the last row's number");
        assert_eq(8, t.value_93, "table: 93");
        assert_true(!t.flag_290 && t.flag_291 && !t.flag_292, "table: 290 291 292");
        const hdxf::BlockPropertiesTableGripData& tg = with_handle(d, "2B41")->as_block_properties_table_grip();
        assert_eq(27, tg.eval.node_id, "table grip: node id");
        assert_eq(28, tg.grip.x_node_id, "table grip: 91");
        assert_eq(-1, tg.grip.value_93, "table grip: 93");
    }
    // the constraints and the dependency body
    {
        const hdxf::BlockHorizontalConstraintParameterData& h = with_handle(d, "2B38")->as_block_horizontal_constraint_parameter();
        assert_eq(2, h.eval.node_id, "horizontal: node id");
        assert_eq(std::string("Length"), h.element.name, "horizontal: 300");
        assert_eq(std::string("2B24"), h.dependency, "horizontal: 330 is the ACDBASSOCDEPENDENCY");
        assert_eq(std::string("Length"), h.distance.label, "horizontal: 305");
        assert_eq(-28.31559984856503, h.distance.label_offset, "horizontal: 140");
        assert_eq(7, h.distance.value_set.flags, "horizontal: 96");
        assert_eq(50.0, h.distance.value_set.limits[0], "horizontal: 141");
        assert_eq(200.0, h.distance.value_set.limits[1], "horizontal: 142");
        assert_eq(10.0, h.distance.value_set.limits[2], "horizontal: 143");
        assert_eq(0, static_cast<int>(h.distance.value_set.values.size()), "horizontal: 175");
        assert_true(h.two_point.links[2] && h.two_point.links[3], "horizontal: the two slots");
        assert_eq(3, h.two_point.links[2]->node_id, "horizontal: 94");
        assert_eq(std::string("DisplacementX"), h.two_point.links[2]->name, "horizontal: 303");
        assert_eq(std::string("DisplacementY"), h.two_point.links[3]->name, "horizontal: 304");
        const hdxf::BlockVerticalConstraintParameterData& v = with_handle(d, "2B3C")->as_block_vertical_constraint_parameter();
        assert_eq(6, v.eval.node_id, "vertical: node id");
        assert_eq(std::string("DIA"), v.element.name, "vertical: 300");
        assert_eq(std::string("2B19"), v.dependency, "vertical: 330");
        assert_eq(8, static_cast<int>(v.distance.value_set.values.size()), "vertical: 175");
        assert_eq(3.0, v.distance.value_set.values[0], "vertical: 144");
        assert_eq(14.0, v.distance.value_set.values[7], "vertical: the last 144");
        assert_eq(50.0, v.two_point.first.x, "vertical: 1010");
        assert_eq(5.0, v.two_point.second.y, "vertical: 1021");
        const hdxf::BlockParameterDependencyBodyData& b = with_handle(d, "2AFC")->as_block_parameter_dependency_body();
        assert_eq(1, b.body_version, "dependency body: AcDbAssocDependencyBody 90");
        assert_eq(1, b.base_version, "dependency body: the dimension base's 90");
        assert_eq(std::string("Head=7.18"), b.expression, "dependency body: 1");
        assert_eq(0, b.value_90, "dependency body: own 90");
        assert_eq(std::string("2AFB"), with_handle(d, "2AFC")->owner, "the body's owner is the dependency");
        // the dependency of the vertical constraint owns a body, and points back at the parameter
        const Object* dependency = with_handle(d, "2B19");
        assert_true(dependency->type == ObjectType::AssocDependency, "2B19 is the constraint's ACDBASSOCDEPENDENCY");
    }
    // the proxy node: AcDbEvalExpr only
    {
        std::vector<const Object*> nodes = objects_of(d, ObjectType::DynamicBlockProxyNode);
        assert_eq(1, static_cast<int>(nodes.size()), "one proxy node");
        assert_eq(33, nodes[0]->as_dynamic_block_proxy_node().eval.version, "its 98");
        assert_eq(327, nodes[0]->as_dynamic_block_proxy_node().eval.maintenance, "its 99");
    }
}

TEST(dynamic_types_autocads_annotation_architectural_and_structural_samples_field_by_field) {
    // polar parameter, grip and stretch (the Imperial drawing: 0.35 and 0.15 are inches)
    {
        const Drawing& d = sample("Annotation_-_Imperial");
        const Object* po = first_of(d, ObjectType::BlockPolarParameter, [](const Object& o) { return o.as_block_polar_parameter().element.name == "Polar"; });
        assert_true(po != nullptr, "a polar parameter");
        const hdxf::BlockPolarParameterData& p = po->as_block_polar_parameter();
        assert_eq(std::string("Length"), p.distance_label, "polar: 305");
        assert_eq(std::string("Sets the length of the tag line"), p.distance_description, "polar: 306");
        assert_eq(std::string("Angle"), p.angle_label, "polar: 307");
        assert_eq(std::string("Sets the angle of the tag line"), p.angle_description, "polar: 308");
        assert_eq(0.35, p.label_offset, "polar: 140");
        assert_eq(4, static_cast<int>(p.two_point.grip_node_ids.size()), "polar: 170");
        assert_true(p.two_point.links[2] && p.two_point.links[3], "polar: slots");
        assert_eq(std::string("UpdatedX"), p.two_point.links[2]->name, "polar: 303");
        assert_eq(0, p.set_96.flags, "polar: 96");
        assert_eq(0, p.set_97.flags, "polar: 97");
        assert_eq(0, static_cast<int>(p.set_96.values.size() + p.set_97.values.size()), "polar: 175 and 176");
        assert_eq(1, static_cast<int>(objects_of(d, ObjectType::BlockPolarGrip).size()), "a polar grip");
        const Object* so = first_of(d, ObjectType::BlockPolarStretchAction, [](const Object&) { return true; });
        assert_true(so != nullptr, "a polar stretch");
        const hdxf::BlockPolarStretchActionData& s = so->as_block_polar_stretch_action();
        assert_eq(std::string("Polar Stretch"), s.element.name, "polar stretch: 300");
        assert_eq(2, static_cast<int>(s.action.selection.size()), "polar stretch: 71");
        assert_eq(std::string("EndXDelta"), s.links[0].name, "polar stretch: 301");
        assert_eq(std::string("UpdatedEnd"), s.links[5].name, "polar stretch: 306");
        assert_eq(2, static_cast<int>(s.frame.size()), "polar stretch: 73");
        assert_eq(1, static_cast<int>(s.entities.size()), "polar stretch: 74");
        assert_eq(1, s.value_75, "polar stretch: 75");
        assert_eq(1.0, s.real_141, "polar stretch: 141");
        assert_eq(0.0, s.real_140, "polar stretch: 140");
    }
    // rotation, rotate action, XY, array
    {
        const Drawing& d = sample("Annotation_-_Imperial");
        const Object* ro = first_of(d, ObjectType::BlockRotationParameter, [](const Object& o) { return o.as_block_rotation_parameter().label == "Arrow Rotation"; });
        assert_true(ro != nullptr, "a rotation parameter");
        const hdxf::BlockRotationParameterData& r = ro->as_block_rotation_parameter();
        assert_eq(std::string("Set the rotation angle of the symbol"), r.description, "rotation: 306");
        assert_eq(0.125, r.label_offset, "rotation: 140");
        assert_eq(4, r.value_set.flags, "rotation: 96");
        assert_eq(0.2617993877991494, r.value_set.limits[2], "rotation: 143 (15 degrees)");
        assert_true(!objects_of(d, ObjectType::BlockRotationGrip).empty(), "rotation grips");
        const Object* ra = first_of(d, ObjectType::BlockRotateAction, [](const Object& o) { return o.as_block_rotate_action().element.name == "Rotate Plant" || true; });
        assert_true(ra != nullptr, "a rotate action");
        const hdxf::BlockRotateActionData& a = ra->as_block_rotate_action();
        assert_eq(std::string("AngleDelta"), a.angle.name, "rotate: 303");
        assert_eq(a.base_point_action.x_node_id, a.angle.node_id, "rotate: 94 is the base point's node");
        assert_eq(std::string("UpdatedBaseX"), a.base_point_action.x_name, "rotate: 301");
        const Object* xo = first_of(d, ObjectType::BlockXYParameter, [](const Object&) { return true; });
        assert_true(xo != nullptr, "an XY parameter");
        const hdxf::BlockXYParameterData& x = xo->as_block_xy_parameter();
        assert_eq(std::string("Height"), x.first_label, "xy: 305");
        assert_eq(std::string("Width"), x.second_label, "xy: 306");
        assert_eq(std::string("Set the overall grid height"), x.first_description, "xy: 307");
        assert_eq(0.15, x.offset_141, "xy: 141");
        assert_eq(0.15, x.offset_140, "xy: 140");
        assert_eq(5, x.set_97.flags, "xy: 97");
        assert_eq(5, x.set_96.flags, "xy: 96");
        assert_eq(1.0, x.set_97.limits[0], "xy: 146");
        assert_eq(1.0, x.set_96.limits[2], "xy: 144");
        assert_true(!objects_of(d, ObjectType::BlockXYGrip).empty(), "XY grips");
        const Object* yo = first_of(d, ObjectType::BlockArrayAction, [](const Object& o) { return o.as_block_array_action().element.name == "Array Grid"; });
        assert_true(yo != nullptr, "the grid array");
        const hdxf::BlockArrayActionData& y = yo->as_block_array_action();
        assert_eq(2, static_cast<int>(y.action.selection.size()), "array: 71");
        assert_eq(std::string("Base"), y.links[0].name, "array: 301");
        assert_eq(std::string("UpdatedEnd"), y.links[3].name, "array: 304");
        assert_eq(1.0, y.value_140, "array: 140");
    }
    // base point
    {
        const Drawing& d = sample("Architectural_-_Metric");
        std::vector<const Object*> all = objects_of(d, ObjectType::BlockBasepointParameter);
        assert_true(!all.empty(), "a base point parameter");
        const hdxf::BlockBasepointParameterData& b = all[0]->as_block_basepoint_parameter();
        assert_eq(std::string("Base Point"), b.element.name, "base point: 300");
        assert_eq(1, b.parameter.show_properties, "base point: 280");
        assert_true(!b.one_point.links[0] && !b.one_point.links[1], "base point: the 1Pt slots are empty");
        assert_eq(0.0, b.point_1011.x, "base point: 1011");
    }
    // radial constraint
    {
        const Drawing& d = sample("Structural_-_Metric");
        const Object* ro = first_of(d, ObjectType::BlockRadialConstraintParameter, [](const Object& o) { return o.as_block_radial_constraint_parameter().element.name == "R"; });
        assert_true(ro != nullptr, "a radial constraint named R");
        const hdxf::BlockRadialConstraintParameterData& r = ro->as_block_radial_constraint_parameter();
        assert_eq(std::string("R"), r.distance.label, "radial: 305");
        assert_true(!r.dependency.empty() && r.dependency != "0", "radial: 330");
        assert_true(r.distance.value_set.values.size() == 10 || r.distance.value_set.values.size() == 8, "radial: its list of values");
        assert_true(near(r.distance.value_set.values[0], 6.0) || near(r.distance.value_set.values[0], 5.0), "radial: the first value");
        assert_eq(0, r.distance.value_set.flags, "radial: 96");
    }
    // 0 records of the siblings that have none in any file
    for (const std::string& s : kSamples) {
        for (const Object& o : sample(s).objects()) {
            assert_true(o.type != ObjectType::BlockAlignedConstraintParameter && o.type != ObjectType::BlockLinearConstraintParameter,
                        s + ": no AutoCAD sample has the two unverified constraints");
        }
    }
}

// ---------------------------------------------------------------------------
// Every record of the samples, in every version and format
// ---------------------------------------------------------------------------

TEST(dynamic_types_every_record_of_the_samples_is_typed_and_written_back_unchanged) {
    std::vector<Original> files = originals(true);
    if (files.empty()) hdxf_test::skip("missing doc/facit/dyntypes");
    std::map<ObjectType, int> counts;
    std::vector<std::string> failures;
    int whole_files = 0, same_files = 0;
    for (const Original& f : files) {
        const std::string what = f.sample + " " + f.tag + (f.binary ? " binary" : " ASCII") + ": ";
        Drawing d;
        if (!d.load_from_memory(f.bytes.data(), f.bytes.size())) { failures.push_back(what + d.last_error()); continue; }
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::Unknown) {
                const std::string& n = o.as_unknown().object_type;
                for (const std::string& typed : kNewNames) {
                    if (n == typed) failures.push_back(what + n + " " + o.handle + " is not typed");
                }
            }
            if (is_new_type(o.type)) ++counts[o.type];
        }
        for (const std::string& w : d.warnings()) {
            if (names_typed_record(w)) failures.push_back(what + "warning: " + w);
        }
        // B12: written unchanged, every record of the types is the file's
        std::vector<uint8_t> again = d.save_to_memory(format_of(f.bytes));
        if (again.empty()) { failures.push_back(what + "cannot be written: " + d.last_error()); continue; }
        ++whole_files;
        if (again == f.bytes) ++same_files;
        if (!f.binary) {
            std::map<std::string, Rec> before = by_handle(f.bytes), after = by_handle(again);
            for (const Object& o : d.objects()) {
                if (!is_new_type(o.type)) continue;
                auto a = before.find(trim(o.handle)), b = after.find(trim(o.handle));
                if (a == before.end() || b == after.end() || a->second != b->second) failures.push_back(what + o.handle + " written unchanged, the record differs");
            }
        }
    }
    std::cout << "dynamic types, samples: " << files.size() << " files, " << same_files << " of " << whole_files << " written back byte for byte\n";
    for (const auto& [t, n] : counts) std::cout << "  type " << static_cast<int>(t) << ": " << n << "\n";
    for (size_t i = 0; i < failures.size() && i < 12; ++i) std::cout << "  " << failures[i] << "\n";
    assert_true(failures.empty(), std::to_string(failures.size()) + " failure(s), the first: " + (failures.empty() ? std::string() : failures[0]));
    for (ObjectType t = ObjectType::BlockAlignmentParameter; t <= ObjectType::DynamicBlockProxyNode; t = static_cast<ObjectType>(static_cast<int>(t) + 1)) {
        if (t == ObjectType::BlockAlignedConstraintParameter || t == ObjectType::BlockLinearConstraintParameter) continue;
        assert_true(counts[t] > 0, "the samples have the type " + std::to_string(static_cast<int>(t)));
    }
    assert_eq(whole_files, same_files, "every sample is written back byte for byte");
}

TEST(dynamic_types_from_the_typed_values_alone_is_autocads_record) {
    std::vector<Original> files = originals(true);
    if (files.empty()) hdxf_test::skip("missing doc/facit/dyntypes");
    int compared = 0;
    std::map<ObjectType, int> per_type;
    std::vector<std::string> failures;
    for (const Original& f : files) {
        const std::string what = f.sample + " " + f.tag + (f.binary ? " binary" : " ASCII") + ": ";
        // the ASCII twin is what the record is compared with
        std::vector<uint8_t> twin = f.binary ? dyn_bytes(f.sample, f.tag, false, f.state.c_str()) : f.bytes;
        if (twin.empty()) continue;
        Drawing d;
        if (!d.load_from_memory(f.bytes.data(), f.bytes.size())) continue;
        std::set<std::string> handles;
        std::map<std::string, ObjectType> types;
        for (Object& o : d.objects()) {
            if (!is_new_type(o.type) || o.raw.empty()) continue;
            handles.insert(trim(o.handle));
            types[trim(o.handle)] = o.type;
            o.raw = hdxf::RawRecord();
        }
        if (handles.empty()) continue;
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        if (out.empty()) { failures.push_back(what + d.last_error()); continue; }
        std::map<std::string, Rec> before = by_handle(twin), after = by_handle(out);
        for (const std::string& h : handles) {
            auto a = before.find(h), b = after.find(h);
            if (a == before.end()) continue;
            std::string why = b == after.end() ? std::string("missing") : same_record_within(a->second, b->second, f.binary ? 1e-12 : 1e-15);
            if (!why.empty()) failures.push_back(what + trim(a->second[0].second) + " " + h + ": " + why);
            ++compared;
            ++per_type[types[h]];
        }
    }
    std::cout << "dynamic types from typed values: " << compared << " records compared in " << per_type.size() << " types\n";
    for (size_t i = 0; i < failures.size() && i < 12; ++i) std::cout << "  " << failures[i] << "\n";
    assert_true(failures.empty(), std::to_string(failures.size()) + " failure(s), the first: " + (failures.empty() ? std::string() : failures[0]));
    assert_true(compared > 3000, "AutoCAD's records were compared (" + std::to_string(compared) + ")");
    assert_eq(29, static_cast<int>(per_type.size()), "every type of the samples was compared");
}

TEST(dynamic_types_autocads_erased_and_purged_files) {
    // ERASE ALL leaves the definitions; -PURGE of the blocks takes the definitions with graph, elements, dependencies and the
    // representations (AutoCAD's cascade): the files of AutoCAD itself show what the drawing then holds
    int checked = 0, purged_whole = 0;
    for (const std::string& s : kSamples) {
        std::vector<uint8_t> erased = dyn_bytes(s, "2018", false, "erased");
        std::vector<uint8_t> purged = dyn_bytes(s, "2018", false, "purged");
        if (erased.empty() || purged.empty()) continue;
        Drawing e = load(erased, s + " erased");
        Drawing p = load(purged, s + " purged");
        int definitions = 0, left = 0;
        for (const Object& o : e.objects()) definitions += is_new_type(o.type) ? 1 : 0;
        for (const Object& o : p.objects()) left += is_new_type(o.type) ? 1 : 0;
        if (definitions == 0) continue;       // a drawing without dynamic blocks (Multileader Tools)
        // the definitions are still there after ERASE ALL, and PURGE takes them out unless another block still uses them
        assert_true(left < definitions, s + ": the purged drawing has fewer records of the types (" + std::to_string(left) + " of " + std::to_string(definitions) + ")");
        for (const Object& o : p.objects()) assert_true(o.type != ObjectType::Unknown || !names_typed_record(o.as_unknown().object_type), s + ": a record of the types is typed");
        purged_whole += left == 0 ? 1 : 0;
        // the purged drawing is written back as AutoCAD saved it
        assert_true(save(p, DxfFormat::Ascii, s) == purged, s + ": the purged file is written back unchanged");
        ++checked;
    }
    if (checked == 0) hdxf_test::skip("missing doc/facit/dyntypes");
    std::cout << "dynamic types, AutoCAD's PURGE: " << purged_whole << " of " << checked << " drawings have no record of the types left\n";
    assert_true(purged_whole > 0, "PURGE takes the definitions of some drawings out whole");
}

// ---------------------------------------------------------------------------
// The version matrix of every type: a record built in memory, written and read back in every version and format
// ---------------------------------------------------------------------------

namespace {

hdxf::EvalExprData eval_with(int node_id) {
    hdxf::EvalExprData e;
    e.node_id = node_id;
    e.version = 33;
    e.maintenance = 327;
    return e;
}

template <class D>
Object made(ObjectType type, D data) {
    Object o;
    o.type = type;
    o.owner = "0";
    o.data = Boxed<D>(std::move(data));
    return o;
}

template <class D>
void two_point_of(D& d) {
    d.element.name = "Element";
    d.two_point.first = {1, 2, 3};
    d.two_point.second = {4, 5, 6};
    d.two_point.grip_node_ids = {0, 11, 0, 0};
    d.two_point.links[2] = hdxf::BlockPropertyLink{11, "UpdatedX"};
    d.two_point.links[3] = hdxf::BlockPropertyLink{11, "UpdatedY"};
}

template <class D>
void grip_of(D& d, int x, int y) {
    d.element.name = "Grip";
    d.grip.x_node_id = x;
    d.grip.y_node_id = y;
    d.grip.location = {7, 8, 0};
}

template <class D>
void action_of(D& d) {
    d.element.name = "Action";
    d.action.node_ids = {5, 6};
    d.action.selection = {"F20", "F21"};
    d.action.location = {3, -4, 0};
}

struct Case {
    const char* name;
    ObjectType type;
    std::function<Object()> make;
    std::function<std::string(const Object&)> check;
};

std::string expect(bool ok, const char* what) { return ok ? std::string() : std::string(what); }

template <class D>
D constraint_data() {
    D d;
    d.eval = eval_with(2);
    two_point_of(d);
    d.dependency = "F21";
    d.distance.label = "Length";
    d.distance.description = "m\xC3\xA5tt";
    d.distance.label_offset = -2.5;
    d.distance.expression = "d1";
    d.distance.value_set.flags = 7;
    d.distance.value_set.limits = {50, 200, 10};
    d.distance.value_set.values = {3, 4.5, 6};
    return d;
}

template <class D>
std::string constraint_check(const D& d) {
    return expect(d.dependency == "F21" && d.distance.label == "Length" && d.distance.description == "m\xC3\xA5tt" && d.distance.value_set.values.size() == 3 &&
                  d.distance.value_set.values[1] == 4.5 && d.distance.value_set.limits[1] == 200 && d.distance.label_offset == -2.5, "constraint");
}

std::vector<Case> cases() {
    std::vector<Case> c;
    c.push_back({"BLOCKALIGNMENTPARAMETER", ObjectType::BlockAlignmentParameter, [] {
        hdxf::BlockAlignmentParameterData d;
        d.eval = eval_with(13);
        two_point_of(d);
        d.flag_280 = 0;
        return made(ObjectType::BlockAlignmentParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_alignment_parameter();
        return expect(d.eval.node_id == 13 && d.flag_280 == 0 && d.two_point.second.z == 6 && d.two_point.links[3] && d.two_point.links[3]->name == "UpdatedY", "alignment");
    }});
    c.push_back({"BLOCKALIGNMENTGRIP", ObjectType::BlockAlignmentGrip, [] {
        hdxf::BlockAlignmentGripData d;
        d.eval = eval_with(14);
        grip_of(d, 15, 16);
        d.vector = {6.35, 1, 2};
        return made(ObjectType::BlockAlignmentGrip, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_alignment_grip();
        return expect(d.vector.x == 6.35 && d.vector.z == 2 && d.grip.y_node_id == 16, "alignment grip");
    }});
    c.push_back({"BLOCKBASEPOINTPARAMETER", ObjectType::BlockBasepointParameter, [] {
        hdxf::BlockBasepointParameterData d;
        d.eval = eval_with(79);
        d.element.name = "Base Point";
        d.one_point.position = {1, 2, 3};
        d.point_1011 = {4, 5, 6};
        d.point_1012 = {7, 8, 9};
        return made(ObjectType::BlockBasepointParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_basepoint_parameter();
        return expect(d.point_1011.y == 5 && d.point_1012.z == 9 && d.one_point.position.x == 1, "base point");
    }});
    c.push_back({"BLOCKFLIPPARAMETER", ObjectType::BlockFlipParameter, [] {
        hdxf::BlockFlipParameterData d;
        d.eval = eval_with(5);
        two_point_of(d);
        d.label = "Sida \xC3\xA5t";
        d.description = "V\xC3\xA4nd symbolen";
        d.text_307 = "Other Side";
        d.text_308 = "Arrow Side";
        d.property_name = "UpdatedFlip";
        d.flag_96 = 6;
        return made(ObjectType::BlockFlipParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_flip_parameter();
        return expect(d.label == "Sida \xC3\xA5t" && d.description == "V\xC3\xA4nd symbolen" && d.property_name == "UpdatedFlip" && d.flag_96 == 6, "flip parameter");
    }});
    c.push_back({"BLOCKFLIPGRIP", ObjectType::BlockFlipGrip, [] {
        hdxf::BlockFlipGripData d;
        d.eval = eval_with(6);
        grip_of(d, 8, 9);
        d.vector = {-1.27, 0, 0};
        d.value_93 = 7;
        return made(ObjectType::BlockFlipGrip, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_flip_grip();
        return expect(d.vector.x == -1.27 && d.value_93 == 7, "flip grip");
    }});
    c.push_back({"BLOCKFLIPACTION", ObjectType::BlockFlipAction, [] {
        hdxf::BlockFlipActionData d;
        d.eval = eval_with(11);
        action_of(d);
        d.links = {hdxf::BlockPropertyLink{5, "Flip"}, hdxf::BlockPropertyLink{5, "UpdatedFlip"}, hdxf::BlockPropertyLink{5, "UpdatedBase"},
                   hdxf::BlockPropertyLink{5, "UpdatedEnd"}};
        return made(ObjectType::BlockFlipAction, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_flip_action();
        return expect(d.links[3].name == "UpdatedEnd" && d.action.selection.size() == 2 && d.action.node_ids.size() == 2 && d.action.node_ids[1] == 6, "flip action");
    }});
    c.push_back({"BLOCKLOOKUPPARAMETER", ObjectType::BlockLookupParameter, [] {
        hdxf::BlockLookupParameterData d;
        d.eval = eval_with(96);
        d.element.name = "Lookup";
        d.one_point.position = {10, 0, 0};
        d.one_point.grip_node_id = 104;
        d.label = "Storlek";
        d.description = "V\xC3\xA4lj";
        d.action_node_id = 128;
        return made(ObjectType::BlockLookupParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_lookup_parameter();
        return expect(d.label == "Storlek" && d.description == "V\xC3\xA4lj" && d.action_node_id == 128 && d.one_point.grip_node_id == 104, "lookup parameter");
    }});
    c.push_back({"BLOCKLOOKUPGRIP", ObjectType::BlockLookupGrip, [] {
        hdxf::BlockLookupGripData d;
        d.eval = eval_with(104);
        grip_of(d, 112, 120);
        d.grip.value_93 = 0;
        return made(ObjectType::BlockLookupGrip, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_lookup_grip();
        return expect(d.grip.x_node_id == 112 && d.grip.value_93 == 0, "lookup grip");
    }});
    c.push_back({"BLOCKLOOKUPACTION", ObjectType::BlockLookupAction, [] {
        hdxf::BlockLookupActionData d;
        d.eval = eval_with(128);
        action_of(d);
        d.rows = {{"1.5", "M3", "a\xC3\xB6"}, {"2.5", "M4", ""}};
        hdxf::BlockLookupColumn a, b, k;
        a.node_id = 144; a.value_type = 40; a.unit_type = 2; a.property_name = "UpdatedDistance";
        b.node_id = 152; b.value_type = 1; b.unit_type = 0; b.flag_282 = 1; b.flag_281 = 1; b.property_name = "lookupString";
        k.node_id = 160; k.value_type = 1; k.property_name = "VisibilityState";
        d.columns = {a, b, k};
        return made(ObjectType::BlockLookupAction, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_lookup_action();
        return expect(d.rows.size() == 2 && d.columns.size() == 3 && d.rows[0][2] == "a\xC3\xB6" && d.rows[1][2].empty() && d.columns[1].flag_282 == 1 &&
                      d.columns[2].property_name == "VisibilityState" && d.columns[0].unit_type == 2, "lookup action");
    }});
    c.push_back({"BLOCKMOVEACTION", ObjectType::BlockMoveAction, [] {
        hdxf::BlockMoveActionData d;
        d.eval = eval_with(27);
        action_of(d);
        d.links = {hdxf::BlockPropertyLink{23, "XDelta"}, hdxf::BlockPropertyLink{23, "YDelta"}};
        d.distance_multiplier = 0.5;
        d.angle_offset = 1.25;
        d.flag_280 = 1;
        return made(ObjectType::BlockMoveAction, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_move_action();
        return expect(d.distance_multiplier == 0.5 && d.angle_offset == 1.25 && d.flag_280 == 1 && d.links[1].name == "YDelta", "move action");
    }});
    c.push_back({"BLOCKPOINTPARAMETER", ObjectType::BlockPointParameter, [] {
        hdxf::BlockPointParameterData d;
        d.eval = eval_with(23);
        d.element.name = "Point";
        d.one_point.position = {1, 2, 0};
        d.one_point.links[0] = hdxf::BlockPropertyLink{36, "DisplacementX"};
        d.label = "Position";
        d.description = "L\xC3\xA4ge";
        d.point_1011 = {3, 4, 0};
        return made(ObjectType::BlockPointParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_point_parameter();
        return expect(d.label == "Position" && d.description == "L\xC3\xA4ge" && d.point_1011.y == 4 && d.one_point.links[0] && !d.one_point.links[1], "point parameter");
    }});
    c.push_back({"BLOCKPOLARPARAMETER", ObjectType::BlockPolarParameter, [] {
        hdxf::BlockPolarParameterData d;
        d.eval = eval_with(336);
        two_point_of(d);
        d.distance_label = "Length";
        d.distance_description = "Sets the length";
        d.angle_label = "Angle";
        d.angle_description = "Sets the angle";
        d.label_offset = 0.35;
        d.set_96.flags = 3;
        d.set_96.limits = {1, 2, 3};
        d.text_410 = "x";
        d.set_97.flags = 4;
        d.set_97.limits = {4, 5, 6};
        return made(ObjectType::BlockPolarParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_polar_parameter();
        return expect(d.angle_label == "Angle" && d.label_offset == 0.35 && d.set_96.flags == 3 && d.set_97.limits[2] == 6 && d.text_410 == "x", "polar parameter");
    }});
    c.push_back({"BLOCKPOLARGRIP", ObjectType::BlockPolarGrip, [] {
        hdxf::BlockPolarGripData d;
        d.eval = eval_with(344);
        grip_of(d, 352, 360);
        return made(ObjectType::BlockPolarGrip, d);
    }, [](const Object& o) { return expect(o.as_block_polar_grip().grip.y_node_id == 360, "polar grip"); }});
    c.push_back({"BLOCKPOLARSTRETCHACTION", ObjectType::BlockPolarStretchAction, [] {
        hdxf::BlockPolarStretchActionData d;
        d.eval = eval_with(409);
        action_of(d);
        for (size_t k = 0; k < d.links.size(); ++k) d.links[k] = hdxf::BlockPropertyLink{336, "Link" + std::to_string(k)};
        d.frame = {Point2{1, 2}, Point2{3, 4}, Point2{5, 6}};
        d.flag_72 = 1;
        d.entities = {"F10", "F11"};
        d.value_75 = 1;
        d.real_141 = 2.5;
        d.real_140 = -1;
        d.flag_77 = 1;
        return made(ObjectType::BlockPolarStretchAction, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_polar_stretch_action();
        return expect(d.frame.size() == 3 && d.frame[2].y == 6 && d.entities.size() == 2 && d.links[5].name == "Link5" && d.real_141 == 2.5 && d.flag_77 == 1 && d.flag_72 == 1, "polar stretch");
    }});
    c.push_back({"BLOCKPROPERTIESTABLE", ObjectType::BlockPropertiesTable, [] {
        hdxf::BlockPropertiesTableData d;
        d.eval = eval_with(26);
        d.element.name = "Block Table";
        d.one_point.grip_node_id = 27;
        d.name = "Tabell1";
        hdxf::BlockTableColumn a, b;
        a.parameter = "F01"; a.node_id = 30; a.property_name = "UserVariable"; a.value.tag = 1; a.value.text = "Custom";
        b.parameter = "F02"; b.node_id = 6; b.property_name = "UpdatedDistance";
        d.columns = {a, b};
        hdxf::BlockTableRow r0, r1;
        r0.index = 0;
        r0.values = {hdxf::BlockTableValue{1, 0, "M3 \xC3\xA4"}, hdxf::BlockTableValue{40, 3.5, ""}};
        r1.index = 1;
        r1.values = {hdxf::BlockTableValue{1, 0, "M4"}, hdxf::BlockTableValue{-9999, 0, ""}};
        d.rows = {r0, r1};
        d.value_93 = 2;
        return made(ObjectType::BlockPropertiesTable, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_properties_table();
        return expect(d.columns.size() == 2 && d.rows.size() == 2 && d.rows[0].values[0].text == "M3 \xC3\xA4" && d.rows[0].values[1].real == 3.5 && d.rows[1].values[1].tag == -9999 &&
                      d.columns[0].value.text == "Custom" && d.columns[1].parameter == "F02" && d.name == "Tabell1", "properties table");
    }});
    c.push_back({"BLOCKPROPERTIESTABLEGRIP", ObjectType::BlockPropertiesTableGrip, [] {
        hdxf::BlockPropertiesTableGripData d;
        d.eval = eval_with(27);
        grip_of(d, 28, 29);
        d.grip.value_93 = -1;
        return made(ObjectType::BlockPropertiesTableGrip, d);
    }, [](const Object& o) { return expect(o.as_block_properties_table_grip().grip.x_node_id == 28, "properties table grip"); }});
    c.push_back({"BLOCKROTATIONPARAMETER", ObjectType::BlockRotationParameter, [] {
        hdxf::BlockRotationParameterData d;
        d.eval = eval_with(480);
        two_point_of(d);
        d.label = "Arrow Rotation";
        d.description = "Set the angle";
        d.point_1011 = {0.35, 0, 0};
        d.label_offset = 0.125;
        d.value_set.flags = 4;
        d.value_set.limits = {0, 0, 0.2617993877991494};
        return made(ObjectType::BlockRotationParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_rotation_parameter();
        return expect(d.label == "Arrow Rotation" && d.point_1011.x == 0.35 && d.value_set.flags == 4 && d.value_set.limits[2] == 0.2617993877991494, "rotation parameter");
    }});
    c.push_back({"BLOCKROTATIONGRIP", ObjectType::BlockRotationGrip, [] {
        hdxf::BlockRotationGripData d;
        d.eval = eval_with(488);
        grip_of(d, 496, 504);
        return made(ObjectType::BlockRotationGrip, d);
    }, [](const Object& o) { return expect(o.as_block_rotation_grip().grip.x_node_id == 496, "rotation grip"); }});
    c.push_back({"BLOCKROTATEACTION", ObjectType::BlockRotateAction, [] {
        hdxf::BlockRotateActionData d;
        d.eval = eval_with(649);
        action_of(d);
        d.base_point_action.x_node_id = 480;
        d.base_point_action.y_node_id = 480;
        d.base_point_action.x_name = "UpdatedBaseX";
        d.base_point_action.y_name = "UpdatedBaseY";
        d.angle = hdxf::BlockPropertyLink{480, "AngleDelta"};
        return made(ObjectType::BlockRotateAction, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_rotate_action();
        return expect(d.angle.node_id == 480 && d.angle.name == "AngleDelta" && d.base_point_action.y_name == "UpdatedBaseY", "rotate action");
    }});
    c.push_back({"BLOCKSTRETCHACTION", ObjectType::BlockStretchAction, [] {
        hdxf::BlockStretchActionData d;
        d.eval = eval_with(608);
        action_of(d);
        d.links = {hdxf::BlockPropertyLink{8, "EndXDelta"}, hdxf::BlockPropertyLink{8, "EndYDelta"}};
        d.frame = {Point2{62, 4}, Point2{25, 2}};
        hdxf::BlockStretchEntity a, b;
        a.entity = "F10"; a.vertices = {1};
        b.entity = "F11"; b.vertices = {0, 1, 2};
        d.entities = {a, b};
        hdxf::BlockStretchConnection k;
        k.node_id = 184;
        k.vertices = {0, 1};
        d.connections = {k};
        d.distance_multiplier = 0.4;
        d.angle_offset = 3.141592653589793;
        return made(ObjectType::BlockStretchAction, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_stretch_action();
        return expect(d.entities.size() == 2 && d.entities[1].vertices.size() == 3 && d.connections.size() == 1 && d.connections[0].node_id == 184 &&
                      d.connections[0].vertices[1] == 1 && d.frame[1].x == 25 && d.angle_offset == 3.141592653589793, "stretch");
    }});
    c.push_back({"BLOCKUSERPARAMETER", ObjectType::BlockUserParameter, [] {
        hdxf::BlockUserParameterData d;
        d.eval = eval_with(30);
        d.element.flags = 1;
        d.one_point.position = {1, 1, 0};
        d.variable = "F03";
        d.name = "Egen";
        d.parameter_type = 5;
        return made(ObjectType::BlockUserParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_user_parameter();
        return expect(d.variable == "F03" && d.name == "Egen" && d.parameter_type == 5 && d.element.flags == 1, "user parameter");
    }});
    c.push_back({"BLOCKXYPARAMETER", ObjectType::BlockXYParameter, [] {
        hdxf::BlockXYParameterData d;
        d.eval = eval_with(640);
        two_point_of(d);
        d.first_label = "H\xC3\xB6jd";
        d.second_label = "Width";
        d.first_description = "a";
        d.second_description = "b";
        d.offset_141 = 0.15;
        d.offset_140 = 0.25;
        d.set_97.flags = 5;
        d.set_97.limits = {1, 0, 1};
        d.set_96.flags = 6;
        return made(ObjectType::BlockXYParameter, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_xy_parameter();
        return expect(d.first_label == "H\xC3\xB6jd" && d.offset_141 == 0.15 && d.offset_140 == 0.25 && d.set_97.flags == 5 && d.set_96.flags == 6, "XY parameter");
    }});
    c.push_back({"BLOCKXYGRIP", ObjectType::BlockXYGrip, [] {
        hdxf::BlockXYGripData d;
        d.eval = eval_with(648);
        grip_of(d, 656, 664);
        return made(ObjectType::BlockXYGrip, d);
    }, [](const Object& o) { return expect(o.as_block_xy_grip().grip.y_node_id == 664, "XY grip"); }});
    c.push_back({"BLOCKARRAYACTION", ObjectType::BlockArrayAction, [] {
        hdxf::BlockArrayActionData d;
        d.eval = eval_with(776);
        action_of(d);
        d.links = {hdxf::BlockPropertyLink{640, "Base"}, hdxf::BlockPropertyLink{640, "End"}, hdxf::BlockPropertyLink{640, "UpdatedBase"},
                   hdxf::BlockPropertyLink{640, "UpdatedEnd"}};
        d.value_140 = 4;
        d.value_141 = 1.5;
        return made(ObjectType::BlockArrayAction, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_array_action();
        return expect(d.value_140 == 4 && d.value_141 == 1.5 && d.links[2].name == "UpdatedBase", "array action");
    }});
    c.push_back({"BLOCKHORIZONTALCONSTRAINTPARAMETER", ObjectType::BlockHorizontalConstraintParameter,
                 [] { return made(ObjectType::BlockHorizontalConstraintParameter, constraint_data<hdxf::BlockHorizontalConstraintParameterData>()); },
                 [](const Object& o) { return constraint_check(o.as_block_horizontal_constraint_parameter()); }});
    c.push_back({"BLOCKVERTICALCONSTRAINTPARAMETER", ObjectType::BlockVerticalConstraintParameter,
                 [] { return made(ObjectType::BlockVerticalConstraintParameter, constraint_data<hdxf::BlockVerticalConstraintParameterData>()); },
                 [](const Object& o) { return constraint_check(o.as_block_vertical_constraint_parameter()); }});
    c.push_back({"BLOCKRADIALCONSTRAINTPARAMETER", ObjectType::BlockRadialConstraintParameter,
                 [] { return made(ObjectType::BlockRadialConstraintParameter, constraint_data<hdxf::BlockRadialConstraintParameterData>()); },
                 [](const Object& o) { return constraint_check(o.as_block_radial_constraint_parameter()); }});
    c.push_back({"BLOCKALIGNEDCONSTRAINTPARAMETER", ObjectType::BlockAlignedConstraintParameter,
                 [] { return made(ObjectType::BlockAlignedConstraintParameter, constraint_data<hdxf::BlockAlignedConstraintParameterData>()); },
                 [](const Object& o) { return constraint_check(o.as_block_aligned_constraint_parameter()); }});
    c.push_back({"BLOCKLINEARCONSTRAINTPARAMETER", ObjectType::BlockLinearConstraintParameter,
                 [] { return made(ObjectType::BlockLinearConstraintParameter, constraint_data<hdxf::BlockLinearConstraintParameterData>()); },
                 [](const Object& o) { return constraint_check(o.as_block_linear_constraint_parameter()); }});
    c.push_back({"BLOCKPARAMDEPENDENCYBODY", ObjectType::BlockParameterDependencyBody, [] {
        hdxf::BlockParameterDependencyBodyData d;
        d.expression = "L\xC3\xA4ngd=7.18";
        d.value_90 = 2;
        return made(ObjectType::BlockParameterDependencyBody, d);
    }, [](const Object& o) {
        const auto& d = o.as_block_parameter_dependency_body();
        return expect(d.expression == "L\xC3\xA4ngd=7.18" && d.value_90 == 2 && d.body_version == 1 && d.base_version == 1, "dependency body");
    }});
    c.push_back({"ACDB_DYNAMICBLOCKPROXYNODE", ObjectType::DynamicBlockProxyNode, [] {
        hdxf::DynamicBlockProxyNodeData d;
        d.eval = eval_with(5);
        return made(ObjectType::DynamicBlockProxyNode, d);
    }, [](const Object& o) { return expect(o.as_dynamic_block_proxy_node().eval.node_id == 5, "proxy node"); }});
    return c;
}

} // namespace

TEST(dynamic_types_version_matrix_of_every_type) {
    const hdxf::internal::TypedRegistry& registry = hdxf::internal::TypedRegistry::instance();
    int types = 0;
    for (const Case& k : cases()) {
        const hdxf::internal::ObjectCodec* codec = registry.find_object(k.name);
        assert_true(codec != nullptr, std::string(k.name) + " has a codec");
        const bool unverified = std::find(kUnverifiedNames.begin(), kUnverifiedNames.end(), std::string(k.name)) != kUnverifiedNames.end();
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
            k.name, codec->versions, [&](Drawing& d) {
                // the records the handles of the test objects point to (a drawing built in memory drops a pointer to nothing)
                for (const char* h : {"F01", "F02", "F03", "F20", "F21"}) {
                    Object x;
                    x.type = ObjectType::XRecord;
                    x.owner = "0";
                    x.handle = h;
                    x.data = hdxf::XRecordData{};
                    d.objects().push_back(x);
                }
                for (const char* h : {"F10", "F11", "F12"}) {
                    d.add_line({0, 0, 0}, {1, 1, 0});
                    d.entities().back().props.handle = h;
                }
                d.objects().push_back(k.make());
            },
            [&](const Drawing& r, DxfVersion, DxfFormat) {
                int n = 0;
                std::string why;
                for (const auto& o : r.objects()) {
                    if (o.type != k.type) continue;
                    ++n;
                    why = k.check(o);
                }
                if (n != 1) return "expected one typed record, found " + std::to_string(n);
                return why;
            });
        assert_eq(12, report.generated, std::string(k.name) + ": 2000 … 2018, both formats, are made");
        assert_eq(2, report.named_errors, std::string(k.name) + ": R12 has the named error (no OBJECTS section)");
        assert_eq(4, report.not_established, std::string(k.name) + ": R13 and R14 are not established (no file, no grammar)");
        assert_eq(unverified ? 0 : 12, report.autocad_verified, std::string(k.name) + ": AutoCAD's own files of 2000 … 2018");
        assert_eq(4, report.autocad_not_savable, std::string(k.name) + ": AutoCAD 2025 cannot save R13/R14");
        ++types;
    }
    std::cout << "dynamic types, matrix: " << types << " types, each 12 cells generated, 2 named errors, 4 not established; "
              << "BLOCKALIGNEDCONSTRAINTPARAMETER and BLOCKLINEARCONSTRAINTPARAMETER: ej testat (no AutoCAD record)\n";
    assert_eq(31, types, "every type of the step is in the matrix");
}

TEST(dynamic_types_the_older_dxf_versions_have_the_named_error_and_r13_r14_are_not_claimed) {
    for (const Case& k : cases()) {
        Drawing d(DxfVersion::R12);
        d.objects().push_back(k.make());
        d.objects().back().handle.clear();
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), std::string(k.name) + ": R12 cannot hold it");
        assert_eq(std::string(k.name) + " requires DXF 2000 or later", d.last_error(), std::string(k.name) + ": the named error");
        assert_true(d.objects().back().handle.empty(), std::string(k.name) + ": no handle was given");
    }
    const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object("BLOCKSTRETCHACTION");
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14}) {
        const hdxf::internal::VersionCell& c = codec->versions.at(v, DxfFormat::Binary);
        assert_true(c.basis == hdxf::internal::ErrorBasis::NotEstablished, "R13/R14: no claim");
        assert_true(c.autocad == hdxf::internal::AutoCadStatus::NotSavable, "AutoCAD 2025 cannot save them");
    }
}

// ---------------------------------------------------------------------------
// Changes (B12): a scalar, an optional slot, a list's length — only the record that changed differs
// ---------------------------------------------------------------------------

namespace {

const char* const kMechanical = "Mechanical_-_Metric";

std::vector<uint8_t> mechanical_2018() {
    std::vector<uint8_t> in = dyn_bytes(kMechanical, "2018", false);
    if (in.empty()) hdxf_test::skip("missing doc/facit/dyntypes/" + std::string(kMechanical));
    return in;
}

// A change of one record of the Mechanical sample: `mutate` changes the record, `check` looks at it after it was written and read
// again; only that record may differ in the file
void change_one(const std::vector<uint8_t>& in, const char* handle, const std::function<void(Object&)>& mutate,
                const std::function<std::string(const Object&)>& check) {
    Drawing d = load(in, "the sample");
    Object* o = d.find_object(handle);
    assert_true(o != nullptr, std::string("the record ") + handle);
    mutate(*o);
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, handle);
    std::set<std::string> changed = differing(in, out);
    assert_true(changed.size() == 1 && changed.count(handle) == 1,
                std::string(handle) + ": only the changed record differs (" + std::to_string(changed.size()) + " records differ)");
    Drawing r = load(out, handle);
    const Object* again = r.find_object(handle);
    assert_true(again != nullptr, std::string(handle) + " is still there");
    std::string why = check(*again);
    assert_true(why.empty(), std::string(handle) + ": " + why);
    // and what was read is written again as it is
    assert_true(save(r, DxfFormat::Ascii, handle) == out, std::string(handle) + ": written again unchanged");
}

} // namespace

TEST(dynamic_types_a_changed_scalar_an_optional_slot_and_a_list_change_only_their_record) {
    const std::vector<uint8_t> in = mechanical_2018();

    change_one(in, "9C2", [](Object& o) { o.as_block_flip_parameter().label = "Sida \xC3\xA5t"; },
               [](const Object& o) { return expect(o.as_block_flip_parameter().label == "Sida \xC3\xA5t", "the label"); });
    // a slot that was empty (flag 0) and has an id and a name now
    change_one(in, "9C8", [](Object& o) { o.as_block_alignment_parameter().two_point.links[0] = hdxf::BlockPropertyLink{5, "DisplacementX"}; },
               [](const Object& o) {
                   const auto& p = o.as_block_alignment_parameter();
                   return expect(p.two_point.links[0] && p.two_point.links[0]->node_id == 5 && p.two_point.links[0]->name == "DisplacementX" && !p.two_point.links[1], "the slot");
               });
    change_one(in, "9CD", [](Object& o) { o.as_block_move_action().distance_multiplier = 2.0; },
               [](const Object& o) { return expect(o.as_block_move_action().distance_multiplier == 2.0 && o.as_block_move_action().angle_offset == 5.497787143782137, "140"); });
    // 70 and its 91: a node more
    change_one(in, "9C7", [](Object& o) { o.as_block_flip_action().action.node_ids.push_back(99); },
               [](const Object& o) {
                   const auto& a = o.as_block_flip_action().action;
                   return expect(a.node_ids.size() == 4 && a.node_ids[3] == 99 && a.selection.size() == 47, "70 and 91");
               });
    // a stretch's entity with its vertices, and a connection to another action
    change_one(in, "126", [](Object& o) {
        hdxf::BlockStretchEntity e;
        e.entity = "111";
        e.vertices = {0, 1, 2};
        o.as_block_stretch_action().entities.push_back(e);
    }, [](const Object& o) {
        const auto& s = o.as_block_stretch_action();
        return expect(s.entities.size() == 4 && s.entities[3].entity == "111" && s.entities[3].vertices.size() == 3 && s.entities[2].vertices.size() == 2 && s.connections.empty(), "73");
    });
    change_one(in, "126", [](Object& o) {
        hdxf::BlockStretchConnection k;
        k.node_id = 999;
        k.vertices = {0};
        o.as_block_stretch_action().connections.push_back(k);
        o.as_block_stretch_action().frame.push_back(Point2{1, 2});
    }, [](const Object& o) {
        const auto& s = o.as_block_stretch_action();
        return expect(s.connections.size() == 1 && s.connections[0].node_id == 999 && s.frame.size() == 3 && s.entities.size() == 3, "75 and 72");
    });
    // a lookup table with a row more
    change_one(in, "88B", [](Object& o) { o.as_block_lookup_action().rows.push_back({"16", "12", "5", "M16"}); },
               [](const Object& o) {
                   const auto& a = o.as_block_lookup_action();
                   return expect(a.rows.size() == 9 && a.rows[8][3] == "M16" && a.columns.size() == 4 && a.rows[7][3] == "M14", "92");
               });
    // a properties table with a row more and another name
    change_one(in, "2B40", [](Object& o) {
        hdxf::BlockPropertiesTableData& t = o.as_block_properties_table();
        hdxf::BlockTableRow row;
        row.index = 8;
        row.values = {hdxf::BlockTableValue{1, 0, "M16"}, hdxf::BlockTableValue{40, 16, ""}, hdxf::BlockTableValue{40, 28, ""}, hdxf::BlockTableValue{40, 10, ""}};
        t.rows.push_back(row);
        t.name = "Tabell 2";
    }, [](const Object& o) {
        const auto& t = o.as_block_properties_table();
        return expect(t.rows.size() == 9 && t.rows[8].values[0].text == "M16" && t.name == "Tabell 2" && t.columns.size() == 4 && t.rows[0].values[0].text == "M3", "92");
    });
    // a constraint's value list: one value more (175 and 144)
    change_one(in, "2B3C", [](Object& o) { o.as_block_vertical_constraint_parameter().distance.value_set.values.push_back(16.0); },
               [](const Object& o) {
                   const auto& v = o.as_block_vertical_constraint_parameter().distance.value_set.values;
                   return expect(v.size() == 9 && v[8] == 16.0 && v[7] == 14.0, "175");
               });
    // the linear parameter's value list (step 9.9's type): values where there were none
    {
        Drawing d = load(in, "the sample");
        std::string handle;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::BlockLinearParameter && o.as_block_linear_parameter().values.empty()) { handle = o.handle; break; }
        }
        assert_true(!handle.empty(), "a linear parameter without values");
        change_one(in, handle.c_str(), [](Object& o) { o.as_block_linear_parameter().values = {1.5, 2.5}; },
                   [](const Object& o) {
                       const auto& v = o.as_block_linear_parameter().values;
                       return expect(v.size() == 2 && v[1] == 2.5, "175 of the linear parameter");
                   });
        // and one with values: the list has them as AutoCAD wrote them
        std::string with_values;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::BlockLinearParameter && !o.as_block_linear_parameter().values.empty()) { with_values = o.handle; break; }
        }
        assert_true(!with_values.empty(), "a linear parameter with values (AutoCAD's lists, typed since step 9.13)");
    }
    change_one(in, "2B44", [](Object& o) {
        o.as_block_user_parameter().name = "Egen \xC3\xA5";
        o.as_block_user_parameter().parameter_type = 3;
    }, [](const Object& o) {
        const auto& u = o.as_block_user_parameter();
        return expect(u.name == "Egen \xC3\xA5" && u.parameter_type == 3 && u.variable == "2B34", "301 and 170");
    });
    change_one(in, "2AFC", [](Object& o) { o.as_block_parameter_dependency_body().expression = "Head=8.5"; },
               [](const Object& o) { return expect(o.as_block_parameter_dependency_body().expression == "Head=8.5", "1"); });
}

// ---------------------------------------------------------------------------
// What a removal takes with it (B19)
// ---------------------------------------------------------------------------

namespace {

void remove_entity_anywhere(Drawing& d, const std::string& handle) {
    bool found = false;
    for (hdxf::Block& b : d.blocks()) {
        const size_t before = b.entities.size();
        b.entities.erase(std::remove_if(b.entities.begin(), b.entities.end(),
                                        [&](const hdxf::Entity& e) { return upper(e.props.handle) == upper(handle); }),
                         b.entities.end());
        found = found || b.entities.size() != before;
    }
    assert_true(found, "the entity " + handle + " is in a block");
}

// The handles of the records that point at `handle` with one of the pointer groups (330 … 333, 340), not as their owner
std::set<std::string> pointing_at(const std::vector<uint8_t>& bytes, const std::string& handle) {
    std::set<std::string> out;
    for (const Rec& r : ascii_records(bytes)) {
        std::string own;
        bool head = true;
        bool in_102 = false;
        for (const Group& g : r) {
            if (g.first == 5) own = trim(g.second);
            if (g.first == 100) head = false;
            if (g.first == 102) in_102 = trim(g.second).rfind("{", 0) == 0;
            if (g.first == 330 && head && !in_102) continue;     // the owner
            if ((g.first == 330 || g.first == 331 || g.first == 332 || g.first == 333 || g.first == 340) && trim(g.second) == handle) out.insert(own);
        }
    }
    out.erase("");
    return out;
}

} // namespace

TEST(dynamic_types_an_entity_removed_leaves_the_selections_the_stretch_lists_and_every_count_follows) {
    const std::vector<uint8_t> in = mechanical_2018();
    for (const char* gone : {"992", "113"}) {
        Drawing d = load(in, "the sample");
        const std::set<std::string> pointing = pointing_at(in, gone);
        assert_true(!pointing.empty(), std::string(gone) + " is pointed at by the actions");
        remove_entity_anywhere(d, gone);
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, gone);
        Drawing r = load(out, gone);
        std::set<std::string> expected = pointing;
        expected.insert(gone);
        assert_true(differing(in, out) == expected, std::string(gone) + ": the records that pointed at it and no other differ");
        assert_true(pointing_at(out, gone).empty(), std::string(gone) + ": nothing points at it any more");
        assert_true(!d.warnings().empty(), "a warning says what was left out");
        // every action the entity was in has it no more and counts what is left
        for (const std::string& h : pointing) {
            const Object* before = load(in, "the sample").find_object(h);
            (void)before;
            const Object* o = r.find_object(h);
            if (!o) continue;
            if (o->type == ObjectType::BlockMoveAction) {
                for (const std::string& s : o->as_block_move_action().action.selection) assert_true(upper(s) != gone, "the move action");
            }
            if (o->type == ObjectType::BlockStretchAction) {
                const auto& s = o->as_block_stretch_action();
                for (const std::string& e : s.action.selection) assert_true(upper(e) != gone, "the stretch's selection");
                for (const auto& e : s.entities) assert_true(upper(e.entity) != gone, "the stretch's entities");
            }
        }
    }
    // the move action of 992 and 993: one entity left
    {
        Drawing d = load(in, "the sample");
        remove_entity_anywhere(d, "992");
        Drawing r = load(save(d, DxfFormat::Ascii, "992"), "992");
        assert_eq(1, static_cast<int>(r.find_object("9CD")->as_block_move_action().action.selection.size()), "71 of the move action");
    }
    // the stretch with three entities: 113 goes with its vertices, 73 and 71 follow
    {
        Drawing d = load(in, "the sample");
        remove_entity_anywhere(d, "113");
        Drawing r = load(save(d, DxfFormat::Ascii, "113"), "113");
        const hdxf::BlockStretchActionData& s = r.find_object("126")->as_block_stretch_action();
        assert_eq(2, static_cast<int>(s.entities.size()), "73 of the stretch");
        assert_eq(2, static_cast<int>(s.action.selection.size()), "71 of the stretch");
        assert_eq(std::string("10F"), s.entities[0].entity, "the first entity stays");
        assert_eq(std::string("110"), s.entities[1].entity, "the second entity stays");
        assert_eq(2, static_cast<int>(s.frame.size()), "the frame stays");
    }
}

TEST(dynamic_types_an_element_a_dependency_and_a_variable_removed) {
    const std::vector<uint8_t> in = mechanical_2018();
    // the vertical constraint parameter goes (with its graph node); the properties table loses its column and the value in every row
    {
        Drawing d = load(in, "the sample");
        const size_t nodes = d.find_object("2B36")->as_evaluation_graph().nodes.size();
        assert_true(d.remove_dynamic_block_element("2B3C"), "the element is removed");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "removed");
        Drawing r = load(out, "removed");
        assert_true(r.find_object("2B3C") == nullptr, "the element is gone");
        assert_eq(static_cast<int>(nodes) - 1, static_cast<int>(r.find_object("2B36")->as_evaluation_graph().nodes.size()), "its node is gone");
        const hdxf::BlockPropertiesTableData& t = r.find_object("2B40")->as_block_properties_table();
        assert_eq(3, static_cast<int>(t.columns.size()), "91 of the table");
        for (const auto& c : t.columns) assert_true(upper(c.parameter) != "2B3C", "no column of the removed parameter");
        assert_eq(8, static_cast<int>(t.rows.size()), "the rows stay");
        for (const auto& row : t.rows) assert_eq(3, static_cast<int>(row.values.size()), "a value less in every row");
        assert_eq(std::string("M3"), t.rows[0].values[0].text, "the first column is the same");
        assert_eq(2.125, t.rows[0].values[2].real, "the last column is the same");
        assert_true(!d.warnings().empty(), "a warning says what was left out");
        for (const std::string& h : differing(in, out)) {
            const Object* o = r.find_object(h);
            if (!o || !is_new_type(o->type)) continue;
            assert_true(o->type == ObjectType::BlockPropertiesTable, "no other record of the types changed: " + h);
        }
    }
    // the constraint's dependency goes: the pointer is 0, the other constraint's is as it was
    {
        Drawing d = load(in, "the sample");
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const Object& o) { return upper(o.handle) == "2B19"; }), objects.end());
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "no dependency");
        Drawing r = load(out, "no dependency");
        assert_eq(std::string("0"), r.find_object("2B3C")->as_block_vertical_constraint_parameter().dependency, "330 is 0");
        assert_eq(std::string("2B24"), r.find_object("2B38")->as_block_horizontal_constraint_parameter().dependency, "the other is as it was");
        bool said = false;
        for (const std::string& w : d.warnings()) said = said || w.find("2B19") != std::string::npos;
        assert_true(said, "the save says so");
    }
    // the user parameter's variable goes
    {
        Drawing d = load(in, "the sample");
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [](const Object& o) { return upper(o.handle) == "2B34"; }), objects.end());
        Drawing r = load(save(d, DxfFormat::Ascii, "no variable"), "no variable");
        assert_eq(std::string("0"), r.find_object("2B44")->as_block_user_parameter().variable, "330 of the user parameter is 0");
        assert_eq(std::string("UserVariable"), r.find_object("2B40")->as_block_properties_table().columns[0].property_name, "the table's column stays");
    }
    // a link the file had broken stays as it was: a removal beside it does not touch it
    {
        std::string text = ascii_text(in);
        const size_t at = text.find("100\r\nAcDbBlockMoveAction\r\n");
        assert_true(at != std::string::npos, "a move action");
        // the first move action's first 330 becomes a handle that is not there
        const size_t action = text.find("100\r\nAcDbBlockAction\r\n", text.rfind("  0\r\nBLOCKMOVEACTION\r\n", at));
        assert_true(action != std::string::npos && action < at, "its AcDbBlockAction");
        const size_t pointer = text.find("330\r\n", action);
        assert_true(pointer != std::string::npos && pointer < at, "its selection");
        const size_t value = pointer + 5;
        const size_t end = text.find("\r\n", value);
        text.replace(value, end - value, "FFFE");
        std::vector<uint8_t> broken(text.begin(), text.end());
        Drawing d = load(broken, "broken");
        assert_true(save(d, DxfFormat::Ascii, "broken") == broken, "written unchanged: the broken link stays");
    }
}

// ---------------------------------------------------------------------------
// A complete typed definition built in memory: every kind of element, an INSERT and its anonymous representation
// ---------------------------------------------------------------------------

namespace {

template <class D>
int32_t add_element(Drawing& d, ObjectType type, D data) {
    Object& o = d.add_dynamic_block_element("DYN2", type, Boxed<D>(std::move(data)));
    return (*o.data.get<Boxed<D>>()).eval.node_id;
}

Drawing build_everything(DxfVersion v) {
    Drawing d(v);
    d.add_block("DYN2");
    hdxf::Entity line = d.add_line({0, 0, 0}, {40, 0, 0});
    line.props.handle = "A01";
    hdxf::Entity circle = d.add_circle({10, 7, 0}, 4);
    circle.props.handle = "A02";
    hdxf::Entity line3 = d.add_line({0, 5, 0}, {40, 5, 0});
    line3.props.handle = "A03";
    d.entities().clear();
    d.find_block("DYN2")->entities = {line, circle, line3};
    for (const char* h : {"B01", "B02", "B03"}) {
        Object x;
        x.type = ObjectType::XRecord;
        x.owner = "0";
        x.handle = h;
        x.data = hdxf::XRecordData{};
        d.objects().push_back(x);
    }
    d.add_dynamic_block_graph("DYN2");

    std::vector<int32_t> ids;
    auto two = [](auto& p, const char* name) { two_point_of(p); p.element.name = name; };
    auto grip = [](auto& g, int x, int y) { grip_of(g, x, y); };
    auto action = [](auto& a, std::vector<std::string> selection) {
        a.element.name = "Action";
        a.action.selection = std::move(selection);
        a.action.location = {1, 2, 0};
    };

    { hdxf::BlockBasepointParameterData p; p.element.name = "Base Point"; ids.push_back(add_element(d, ObjectType::BlockBasepointParameter, p)); }
    int32_t flip;
    { hdxf::BlockFlipParameterData p; two(p, "Flip"); p.label = "Side"; p.property_name = "UpdatedFlip"; flip = add_element(d, ObjectType::BlockFlipParameter, p); }
    { hdxf::BlockFlipGripData g; grip(g, 1, 2); ids.push_back(add_element(d, ObjectType::BlockFlipGrip, g)); }
    { hdxf::BlockFlipActionData a; action(a, {"A01", "A02"}); a.links[0] = hdxf::BlockPropertyLink{flip, "Flip"}; ids.push_back(add_element(d, ObjectType::BlockFlipAction, a)); }
    { hdxf::BlockAlignmentParameterData p; two(p, "Alignment"); ids.push_back(add_element(d, ObjectType::BlockAlignmentParameter, p)); }
    { hdxf::BlockAlignmentGripData g; grip(g, 1, 2); g.vector = {1, 0, 0}; ids.push_back(add_element(d, ObjectType::BlockAlignmentGrip, g)); }
    { hdxf::BlockPointParameterData p; p.element.name = "Point"; p.label = "Position"; ids.push_back(add_element(d, ObjectType::BlockPointParameter, p)); }
    int32_t xy;
    { hdxf::BlockXYParameterData p; two(p, "XY"); p.first_label = "Height"; p.second_label = "Width"; xy = add_element(d, ObjectType::BlockXYParameter, p); }
    { hdxf::BlockXYGripData g; grip(g, 1, 2); ids.push_back(add_element(d, ObjectType::BlockXYGrip, g)); }
    { hdxf::BlockPolarParameterData p; two(p, "Polar"); p.distance_label = "Length"; p.angle_label = "Angle"; ids.push_back(add_element(d, ObjectType::BlockPolarParameter, p)); }
    { hdxf::BlockPolarGripData g; grip(g, 1, 2); ids.push_back(add_element(d, ObjectType::BlockPolarGrip, g)); }
    {
        hdxf::BlockPolarStretchActionData a;
        action(a, {"A01"});
        a.entities = {"A01", "A03"};
        a.frame = {Point2{0, 0}, Point2{5, 5}};
        for (size_t k = 0; k < a.links.size(); ++k) a.links[k] = hdxf::BlockPropertyLink{xy, "Link" + std::to_string(k)};
        ids.push_back(add_element(d, ObjectType::BlockPolarStretchAction, a));
    }
    { hdxf::BlockRotationParameterData p; two(p, "Rotation"); p.label = "Angle"; ids.push_back(add_element(d, ObjectType::BlockRotationParameter, p)); }
    { hdxf::BlockRotationGripData g; grip(g, 1, 2); ids.push_back(add_element(d, ObjectType::BlockRotationGrip, g)); }
    { hdxf::BlockRotateActionData a; action(a, {"A03"}); a.angle = hdxf::BlockPropertyLink{1, "AngleDelta"}; a.base_point_action.x_name = "UpdatedBaseX"; a.base_point_action.y_name = "UpdatedBaseY"; ids.push_back(add_element(d, ObjectType::BlockRotateAction, a)); }
    { hdxf::BlockMoveActionData a; action(a, {"A01"}); a.links = {hdxf::BlockPropertyLink{1, "XDelta"}, hdxf::BlockPropertyLink{1, "YDelta"}}; ids.push_back(add_element(d, ObjectType::BlockMoveAction, a)); }
    {
        hdxf::BlockStretchActionData a;
        action(a, {"A01", "A03"});
        a.links = {hdxf::BlockPropertyLink{1, "EndXDelta"}, hdxf::BlockPropertyLink{1, "EndYDelta"}};
        a.frame = {Point2{1, 1}, Point2{2, 2}};
        hdxf::BlockStretchEntity e1, e2;
        e1.entity = "A01"; e1.vertices = {1};
        e2.entity = "A03"; e2.vertices = {0, 1};
        a.entities = {e1, e2};
        hdxf::BlockStretchConnection k;
        k.node_id = 3;
        k.vertices = {0};
        a.connections = {k};
        ids.push_back(add_element(d, ObjectType::BlockStretchAction, a));
    }
    { hdxf::BlockArrayActionData a; action(a, {"A02"}); a.value_140 = 3; ids.push_back(add_element(d, ObjectType::BlockArrayAction, a)); }
    { hdxf::BlockLookupParameterData p; p.element.name = "Lookup"; p.label = "Size"; ids.push_back(add_element(d, ObjectType::BlockLookupParameter, p)); }
    { hdxf::BlockLookupGripData g; grip(g, 1, 2); ids.push_back(add_element(d, ObjectType::BlockLookupGrip, g)); }
    {
        hdxf::BlockLookupActionData a;
        action(a, {});
        a.rows = {{"1", "a"}, {"2", "b"}, {"3", "c"}};
        hdxf::BlockLookupColumn c0, c1;
        c0.node_id = 4; c0.value_type = 40; c0.property_name = "UpdatedDistance";
        c1.node_id = 5; c1.value_type = 1; c1.flag_282 = 1; c1.flag_281 = 1; c1.property_name = "lookupString";
        a.columns = {c0, c1};
        ids.push_back(add_element(d, ObjectType::BlockLookupAction, a));
    }
    int32_t user;
    { hdxf::BlockUserParameterData p; p.element.name = "User"; p.variable = "B01"; p.name = "Mine"; p.parameter_type = 5; user = add_element(d, ObjectType::BlockUserParameter, p); }
    {
        hdxf::BlockPropertiesTableData t;
        t.element.name = "Block Table";
        t.name = "Table1";
        hdxf::BlockTableColumn a, b;
        a.parameter = d.dynamic_block_element("DYN2", user)->handle; a.node_id = user; a.property_name = "UserVariable"; a.value.tag = 1; a.value.text = "Custom";
        b.parameter = d.dynamic_block_element("DYN2", xy)->handle; b.node_id = xy; b.property_name = "UpdatedDistance";
        t.columns = {a, b};
        hdxf::BlockTableRow r0, r1;
        r0.index = 0; r0.values = {hdxf::BlockTableValue{1, 0, "A"}, hdxf::BlockTableValue{40, 1.5, ""}};
        r1.index = 1; r1.values = {hdxf::BlockTableValue{1, 0, "B"}, hdxf::BlockTableValue{40, 2.5, ""}};
        t.rows = {r0, r1};
        ids.push_back(add_element(d, ObjectType::BlockPropertiesTable, t));
    }
    { hdxf::BlockPropertiesTableGripData g; grip(g, 1, 2); ids.push_back(add_element(d, ObjectType::BlockPropertiesTableGrip, g)); }
    auto constraint = [&](auto data, ObjectType type, const char* name) {
        two(data, name);
        data.dependency = "B02";
        data.distance.label = name;
        data.distance.value_set.values = {1, 2};
        ids.push_back(add_element(d, type, data));
    };
    constraint(hdxf::BlockHorizontalConstraintParameterData(), ObjectType::BlockHorizontalConstraintParameter, "Length");
    constraint(hdxf::BlockVerticalConstraintParameterData(), ObjectType::BlockVerticalConstraintParameter, "Height");
    constraint(hdxf::BlockRadialConstraintParameterData(), ObjectType::BlockRadialConstraintParameter, "Radius");
    constraint(hdxf::BlockAlignedConstraintParameterData(), ObjectType::BlockAlignedConstraintParameter, "Aligned");
    constraint(hdxf::BlockLinearConstraintParameterData(), ObjectType::BlockLinearConstraintParameter, "Linear");
    { hdxf::DynamicBlockProxyNodeData n; ids.push_back(add_element(d, ObjectType::DynamicBlockProxyNode, n)); }
    {
        // the dependency body of the constraint's dependency
        Object body;
        body.type = ObjectType::BlockParameterDependencyBody;
        body.owner = "B02";
        hdxf::BlockParameterDependencyBodyData b;
        b.expression = "Length=40";
        body.data = Boxed<hdxf::BlockParameterDependencyBodyData>(b);
        d.objects().push_back(body);
    }

    // the dependencies between the nodes
    Object* graph = d.dynamic_block_graph("DYN2");
    hdxf::EvaluationGraphData& eg = graph->as_evaluation_graph();
    for (size_t i = 0; i + 1 < ids.size(); i += 2) {
        eg.add_edge(eg.find_node_by_id(ids[i])->index, eg.find_node_by_id(ids[i + 1])->index);
    }
    d.add_insert("DYN2", {0, 0, 0});
    d.add_dynamic_block_representation("DYN2", {100, 0, 0});
    return d;
}

int count_of(const Drawing& d, ObjectType t) { return static_cast<int>(objects_of(d, t).size()); }

// The CLASSES entries of an ASCII file by DXF name: the application and the proxy flags of each
std::map<std::string, std::vector<std::pair<std::string, int>>> classes_of(const std::vector<uint8_t>& bytes) {
    std::map<std::string, std::vector<std::pair<std::string, int>>> out;
    for (const Rec& r : ascii_records(bytes)) {
        if (trim(r[0].second) != "CLASS") continue;
        std::string name, application;
        int flags = 0;
        for (const Group& g : r) {
            if (g.first == 1) name = trim(g.second);
            if (g.first == 3) application = trim(g.second);
            if (g.first == 90) flags = std::atoi(g.second.c_str());
        }
        out[name].push_back({application, flags});
    }
    return out;
}

} // namespace

TEST(dynamic_types_a_new_complete_definition_with_every_kind_in_every_version_and_format) {
    for (DxfVersion v : kVersions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string what = std::string(hdxf_test::version_label(v)) + (f == DxfFormat::Binary ? " binary: " : " ASCII: ");
            Drawing d = build_everything(v);
            std::vector<uint8_t> a = save(d, f, what);
            Drawing r = load(a, what);
            if (f == DxfFormat::Ascii) {
                // CLASSES: each class once, as AutoCAD's (ObjectDBX Classes; 1153, the dependency body's 1)
                auto classes = classes_of(a);
                std::vector<std::string> all = kNewNames;
                all.insert(all.end(), kUnverifiedNames.begin(), kUnverifiedNames.end());
                for (const std::string& n : all) {
                    assert_eq(1, static_cast<int>(classes[n].size()), what + n + " is in CLASSES once");
                    assert_eq(std::string("ObjectDBX Classes"), classes[n][0].first, what + n + ": the application");
                    assert_eq(n == "BLOCKPARAMDEPENDENCYBODY" ? 1 : 1153, classes[n][0].second, what + n + ": the proxy flags");
                }
            }
            assert_true(hdxf_test::warnings_besides_fonts(r.warnings()).empty(), what + "no warning on reading");
            for (const Object& o : r.objects()) {
                assert_true(o.type != ObjectType::Unknown, what + "every record is typed (" + o.handle + ")");
            }
            // one record of each type (the constraints: five kinds, the elements the graph owns)
            for (ObjectType t = ObjectType::BlockAlignmentParameter; t <= ObjectType::DynamicBlockProxyNode; t = static_cast<ObjectType>(static_cast<int>(t) + 1)) {
                assert_eq(1, count_of(r, t), what + "one record of the type " + std::to_string(static_cast<int>(t)));
            }
            const hdxf::EvaluationGraphData& g = the_object(r, ObjectType::EvaluationGraph)->as_evaluation_graph();
            assert_eq(30, static_cast<int>(g.nodes.size()), what + "a node for every element");
            assert_true(g.links_consistent(), what + "the graph's chains");
            for (const hdxf::EvalGraphNode& n : g.nodes) assert_true(r.find_object(n.object) != nullptr, what + "an element for every node");
            // the fields
            const hdxf::BlockStretchActionData& s = the_object(r, ObjectType::BlockStretchAction)->as_block_stretch_action();
            assert_eq(2, static_cast<int>(s.entities.size()), what + "the stretch's entities");
            assert_eq(std::string("A03"), s.entities[1].entity, what + "331");
            assert_eq(2, static_cast<int>(s.entities[1].vertices.size()), what + "74 and 94");
            assert_eq(1, static_cast<int>(s.connections.size()), what + "75");
            const hdxf::BlockLookupActionData& l = the_object(r, ObjectType::BlockLookupAction)->as_block_lookup_action();
            assert_eq(3, static_cast<int>(l.rows.size()), what + "the lookup table's rows");
            assert_eq(std::string("c"), l.rows[2][1], what + "its last cell");
            const hdxf::BlockPropertiesTableData& t = the_object(r, ObjectType::BlockPropertiesTable)->as_block_properties_table();
            assert_eq(2, static_cast<int>(t.columns.size()), what + "the table's columns");
            assert_eq(2.5, t.rows[1].values[1].real, what + "its last value");
            assert_eq(std::string("Length"), the_object(r, ObjectType::BlockHorizontalConstraintParameter)->as_block_horizontal_constraint_parameter().distance.label, what + "the horizontal constraint");
            assert_eq(2, static_cast<int>(the_object(r, ObjectType::BlockRadialConstraintParameter)->as_block_radial_constraint_parameter().distance.value_set.values.size()), what + "the radial constraint's values");
            assert_eq(std::string("Length=40"), the_object(r, ObjectType::BlockParameterDependencyBody)->as_block_parameter_dependency_body().expression, what + "the dependency body");
            // B12: written again unchanged
            assert_true(save(r, f, what) == a, what + "written again unchanged");
            // the typed values alone are the same record (B103: an emptied raw gives the same model)
            for (Object& o : r.objects()) o.raw = hdxf::RawRecord();
            // (a file with a record written anew has a new $VERSIONGUID: the records are compared)
            std::vector<uint8_t> b = save(r, f, what);
            assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + "from the typed values alone: the same size");
            {
                Drawing da = load(a, what), db = load(b, what);
                assert_true(differing(save(da, DxfFormat::Ascii, what), save(db, DxfFormat::Ascii, what)).empty(), what + "from the typed values alone: the same records");
            }
            // the other format has the same content
            const DxfFormat other = f == DxfFormat::Ascii ? DxfFormat::Binary : DxfFormat::Ascii;
            Drawing again = load(a, what);
            Drawing r2 = load(save(again, other, what), what);
            assert_eq(30, static_cast<int>(the_object(r2, ObjectType::EvaluationGraph)->as_evaluation_graph().nodes.size()), what + "the other format");
        }
    }
}

TEST(dynamic_types_a_removed_entity_or_element_of_a_definition_built_in_memory_follows_through) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2018}) {
        const std::string what = std::string(hdxf_test::version_label(v)) + ": ";
        // an entity of the block
        {
            Drawing d = build_everything(v);
            hdxf::Block* block = d.find_block("DYN2");
            block->entities.erase(std::remove_if(block->entities.begin(), block->entities.end(),
                                                 [](const hdxf::Entity& e) { return upper(e.props.handle) == "A01"; }),
                                  block->entities.end());
            Drawing r = load(save(d, DxfFormat::Ascii, what), what);
            const auto& flip = the_object(r, ObjectType::BlockFlipAction)->as_block_flip_action().action;
            assert_eq(1, static_cast<int>(flip.selection.size()), what + "71 of the flip action");
            assert_eq(std::string("A02"), flip.selection[0], what + "330");
            assert_eq(0, static_cast<int>(the_object(r, ObjectType::BlockMoveAction)->as_block_move_action().action.selection.size()), what + "the move action has none");
            const auto& s = the_object(r, ObjectType::BlockStretchAction)->as_block_stretch_action();
            assert_eq(1, static_cast<int>(s.entities.size()), what + "73 of the stretch");
            assert_eq(std::string("A03"), s.entities[0].entity, what + "its other entity");
            assert_eq(1, static_cast<int>(s.action.selection.size()), what + "71 of the stretch");
            assert_eq(1, static_cast<int>(the_object(r, ObjectType::BlockPolarStretchAction)->as_block_polar_stretch_action().entities.size()), what + "74 of the polar stretch");
            assert_true(!d.warnings().empty(), what + "a warning says what was left out");
        }
        // the user parameter (a column of the table), the dependency and the variable
        {
            Drawing d = build_everything(v);
            const int32_t user_id = the_object(d, ObjectType::BlockUserParameter)->as_block_user_parameter().eval.node_id;
            const std::string user_handle = the_object(d, ObjectType::BlockUserParameter)->handle;
            assert_true(d.remove_dynamic_block_element(user_handle), what + "the element is removed");
            auto& objects = d.objects();
            objects.erase(std::remove_if(objects.begin(), objects.end(), [](const Object& o) { return upper(o.handle) == "B02"; }), objects.end());
            Drawing r = load(save(d, DxfFormat::Ascii, what), what);
            const auto& t = the_object(r, ObjectType::BlockPropertiesTable)->as_block_properties_table();
            assert_eq(1, static_cast<int>(t.columns.size()), what + "the user parameter's column went");
            assert_eq(2, static_cast<int>(t.rows[0].values.size() + 1), what + "and its value in every row");
            assert_eq(std::string("0"), the_object(r, ObjectType::BlockVerticalConstraintParameter)->as_block_vertical_constraint_parameter().dependency, what + "the dependency is gone: 330 is 0");
            assert_true(r.dynamic_block_element("DYN2", user_id) == nullptr, what + "its id is not used again");
        }
    }
}

// ---------------------------------------------------------------------------
// Damaged and future records
// ---------------------------------------------------------------------------

namespace {

// The first `from` after `anchor` becomes `to`
std::vector<uint8_t> replace_after(const std::vector<uint8_t>& in, const std::string& anchor, const std::string& from, const std::string& to) {
    std::string text = ascii_text(in);
    const size_t a = text.find(anchor);
    assert_true(a != std::string::npos, "anchor " + anchor);
    const size_t at = text.find(from, a);
    assert_true(at != std::string::npos, "text " + from);
    text.replace(at, from.size(), to);
    return std::vector<uint8_t>(text.begin(), text.end());
}

// The value of the first group `code` after `anchor` becomes `value`
std::vector<uint8_t> set_group_after(const std::vector<uint8_t>& in, const std::string& anchor, int code, const std::string& value) {
    std::string text = ascii_text(in);
    const size_t a = text.find(anchor);
    assert_true(a != std::string::npos, "anchor " + anchor);
    char line[16];
    std::snprintf(line, sizeof(line), "\r\n%3d\r\n", code);
    const size_t at = text.find(line, a);
    assert_true(at != std::string::npos, "group " + std::to_string(code) + " after " + anchor);
    const size_t v = at + std::string(line).size();
    const size_t end = text.find("\r\n", v);
    text.replace(v, end - v, value);
    return std::vector<uint8_t>(text.begin(), text.end());
}

bool warned_about(const Drawing& d, const std::string& text) {
    for (const std::string& w : d.warnings()) {
        if (w.find(text) != std::string::npos) return true;
    }
    return false;
}

int unknown_named(const Drawing& d, const std::string& name) {
    int n = 0;
    for (const Object& o : d.objects()) n += (o.type == ObjectType::Unknown && o.as_unknown().object_type == name) ? 1 : 0;
    return n;
}

} // namespace

TEST(dynamic_types_a_damaged_or_future_record_stays_as_read_with_a_warning) {
    const std::vector<uint8_t> in = mechanical_2018();
    const int stretches = count_of(load(in, "the sample"), ObjectType::BlockStretchAction);
    const int lookups = count_of(load(in, "the sample"), ObjectType::BlockLookupAction);
    const int tables = count_of(load(in, "the sample"), ObjectType::BlockPropertiesTable);
    // a count above what the record can hold (73 of a stretch): no allocation, an untyped record
    {
        std::vector<uint8_t> big = set_group_after(in, "100\r\nAcDbBlockStretchAction\r\n", 73, "30000");
        Drawing d = load(big, "big");
        assert_eq(stretches - 1, count_of(d, ObjectType::BlockStretchAction), "one stretch is not typed");
        assert_eq(1, unknown_named(d, "BLOCKSTRETCHACTION"), "it stays as an unknown record");
        assert_true(warned_about(d, "BLOCKSTRETCHACTION"), "the warning names it");
        assert_true(save(d, DxfFormat::Ascii, "big") == big, "kept as read");
    }
    // rows and columns that the groups cannot hold (92 of a lookup table)
    {
        std::vector<uint8_t> big = set_group_after(in, "100\r\nAcDbBlockLookupAction\r\n", 92, "2000000000");
        Drawing d = load(big, "big");
        assert_eq(lookups - 1, count_of(d, ObjectType::BlockLookupAction), "one lookup action is not typed");
        assert_true(warned_about(d, "BLOCKLOOKUPACTION"), "the warning");
        assert_true(save(d, DxfFormat::Ascii, "big") == big, "kept as read");
    }
    // rows with no column
    {
        std::vector<uint8_t> none = set_group_after(in, "100\r\nAcDbBlockLookupAction\r\n", 93, "0");
        Drawing d = load(none, "none");
        assert_eq(lookups - 1, count_of(d, ObjectType::BlockLookupAction), "one lookup action is not typed");
        assert_true(save(d, DxfFormat::Ascii, "none") == none, "kept as read");
    }
    // a value of a properties table with a tag no file shows
    {
        std::vector<uint8_t> tag = replace_after(in, "100\r\nAcDbBlockPropertiesTable\r\n", "170\r\n    40\r\n140\r\n", "170\r\n    77\r\n140\r\n");
        Drawing d = load(tag, "tag");
        assert_eq(tables - 1, count_of(d, ObjectType::BlockPropertiesTable), "one table is not typed");
        assert_true(warned_about(d, "BLOCKPROPERTIESTABLE"), "the warning");
        assert_true(save(d, DxfFormat::Ascii, "tag") == tag, "kept as read");
    }
    // a group after the ones HDxf knows (a later version's): the record is typed, the group stays with the warning
    {
        std::string text = ascii_text(in);
        const size_t a = text.find("100\r\nAcDbBlockFlipParameter\r\n");
        assert_true(a != std::string::npos, "the flip parameter");
        const size_t line96 = text.find(" 96\r\n", a);
        const size_t after = text.find("\r\n", line96 + 5) + 2;
        text.insert(after, "300\r\nFUTURE\r\n");
        std::vector<uint8_t> future(text.begin(), text.end());
        Drawing d = load(future, "future");
        assert_eq(1, count_of(d, ObjectType::BlockFlipParameter), "the record is typed");
        assert_true(warned_about(d, "group(s) after the ones HDxf knows"), "the warning says so");
        assert_true(save(d, DxfFormat::Ascii, "future") == future, "the group is kept");
    }
    // a group missing from the record
    {
        std::vector<uint8_t> cut = replace_after(in, "100\r\nAcDbBlockMoveAction\r\n", "280\r\n     0\r\n", "");
        Drawing d = load(cut, "cut");
        assert_true(warned_about(d, "BLOCKMOVEACTION"), "the warning");
        assert_true(save(d, DxfFormat::Ascii, "cut") == cut, "kept as read");
    }
}

TEST(dynamic_types_the_types_with_no_record_in_any_file_are_not_typed_and_stay_as_read) {
    // BLOCKANGULARCONSTRAINTPARAMETER, BLOCKANGULARCONSTRAINTPARAMETERENTITY and BLOCKDIAMETRICCONSTRAINTPARAMETER have no AutoCAD record
    // and no chain to derive: a record of them (made here as the text of one) is an unknown record, kept as it was
    const std::vector<uint8_t> in = mechanical_2018();
    for (const std::string& name : kUntypedNames) {
        std::string text = ascii_text(in);
        const size_t objects = text.find("OBJECTS\r\n");
        assert_true(objects != std::string::npos, "the OBJECTS section");
        const size_t end = text.find("  0\r\nENDSEC", objects);
        assert_true(end != std::string::npos, "its end");
        text.insert(end, "  0\r\n" + name + "\r\n  5\r\nFFFFD\r\n330\r\n2B36\r\n100\r\nAcDbEvalExpr\r\n 90\r\n       99\r\n 98\r\n       33\r\n 99\r\n      327\r\n");
        std::vector<uint8_t> modified(text.begin(), text.end());
        Drawing d = load(modified, name);
        assert_eq(1, unknown_named(d, name), name + " is an unknown record");
        assert_true(save(d, DxfFormat::Ascii, name) == modified, name + " is kept byte for byte");
    }
}

// ---------------------------------------------------------------------------
// Graph ids against handles, and the shared definition: the INSERTs erased as AutoCAD's ERASE ALL does
// ---------------------------------------------------------------------------

namespace {

const hdxf::EvalExprData* eval_of(const Object& o) {
    return hdxf::internal::visit_object_data([](const auto& v) -> const hdxf::EvalExprData* {
        if constexpr (requires { v.eval; }) {
            return &v.eval;
        } else if constexpr (requires { (*v).eval; }) {
            return &(*v).eval;
        } else {
            return nullptr;
        }
    }, o.data);
}

} // namespace

TEST(dynamic_types_a_node_id_is_not_a_handle_every_element_is_the_node_that_owns_it) {
    int graphs = 0, elements = 0;
    for (const std::string& s : kSamples) {
        std::vector<uint8_t> bytes = dyn_bytes(s, "2018", false);
        if (bytes.empty()) continue;
        const Drawing& d = sample(s);
        for (const Object& g : d.objects()) {
            if (g.type != ObjectType::EvaluationGraph) continue;
            ++graphs;
            for (const hdxf::EvalGraphNode& n : g.as_evaluation_graph().nodes) {
                const Object* element = d.find_object(n.object);
                assert_true(element != nullptr, s + ": the element of node " + std::to_string(n.id));
                assert_eq(upper(g.handle), upper(element->owner), s + ": the graph owns the element (330)");
                const hdxf::EvalExprData* eval = eval_of(*element);
                if (!eval) continue;
                ++elements;
                assert_eq(n.id, eval->node_id, s + ": the element's AcDbEvalExpr 90 is its node's id (95), not a handle");
            }
        }
    }
    assert_true(graphs > 20 && elements > 1000, "the samples' graphs were read (" + std::to_string(graphs) + " graphs, " + std::to_string(elements) + " elements)");
}

TEST(dynamic_types_erasing_the_inserts_leaves_the_definitions_as_autocad_does) {
    for (const std::string& s : {std::string("Mechanical_-_Metric"), std::string("Architectural_-_Metric"), std::string("Annotation_-_Imperial")}) {
        std::vector<uint8_t> in = dyn_bytes(s, "2018", false);
        std::vector<uint8_t> erased = dyn_bytes(s, "2018", false, "erased");
        if (in.empty() || erased.empty()) hdxf_test::skip("missing doc/facit/dyntypes/" + s);
        Drawing d = load(in, s);
        d.entities().clear();                                   // ERASE ALL of the model space
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, s);
        std::map<std::string, Rec> mine = by_handle(out), autocad = by_handle(erased);
        int compared = 0;
        for (const Object& o : sample(s).objects()) {
            if (!is_new_type(o.type) && o.type != ObjectType::EvaluationGraph) continue;
            auto a = autocad.find(trim(o.handle)), m = mine.find(trim(o.handle));
            assert_true(a != autocad.end(), s + ": AutoCAD's erased drawing has " + o.handle);
            assert_true(m != mine.end(), s + ": the definition's record " + o.handle + " stays");
            assert_eq(std::string(), same_record_within(a->second, m->second, 1e-15), s + ": " + o.handle + " is AutoCAD's record");
            ++compared;
        }
        assert_true(compared > 20, s + ": records were compared (" + std::to_string(compared) + ")");
    }
}

// ---------------------------------------------------------------------------
// The block removed, and long UTF-8 text
// ---------------------------------------------------------------------------

TEST(dynamic_types_the_block_removed_takes_its_graph_and_every_element_and_the_other_blocks_keep_theirs) {
    const std::vector<uint8_t> in = mechanical_2018();
    Drawing d = load(in, "the sample");
    // the definition whose graph owns the constraints, the properties table and the user parameter
    const Object* graph = nullptr;
    for (const Object& o : d.objects()) {
        if (o.type != ObjectType::EvaluationGraph) continue;
        for (const hdxf::EvalGraphNode& n : o.as_evaluation_graph().nodes) {
            const Object* e = d.find_object(n.object);
            if (e && e->type == ObjectType::BlockPropertiesTable) graph = &o;
        }
    }
    assert_true(graph != nullptr, "a graph with a properties table");
    const std::string graph_handle = graph->handle;
    std::set<std::string> elements;
    for (const hdxf::EvalGraphNode& n : graph->as_evaluation_graph().nodes) elements.insert(upper(n.object));
    const hdxf::Block* definition = nullptr;
    for (const hdxf::Block& b : d.blocks()) {
        if (!b.record.xdictionary.empty() && upper(b.record.xdictionary) == upper(graph->owner)) definition = &b;
    }
    assert_true(definition != nullptr, "the definition");
    const std::string name = definition->name;
    // the elements: the records that have an AcDbEvalExpr (the representation data and the purge preventer are not nodes of a graph)
    size_t before = 0;
    for (const Object& o : d.objects()) before += eval_of(o) ? 1 : 0;
    size_t owned = 0;
    for (const Object& o : d.objects()) owned += (eval_of(o) && elements.count(upper(o.handle))) ? 1 : 0;
    assert_true(owned > 10, "the definition has elements (" + std::to_string(owned) + ")");

    auto& blocks = d.blocks();
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [&](const hdxf::Block& b) { return b.name == name; }), blocks.end());
    auto& entities = d.entities();
    entities.erase(std::remove_if(entities.begin(), entities.end(), [&](const hdxf::Entity& e) {
                       return e.type == hdxf::EntityType::Insert && e.as_insert().block_name == name;
                   }), entities.end());
    Drawing r = load(save(d, DxfFormat::Ascii, "removed"), "removed");
    assert_true(r.find_object(graph_handle) == nullptr, "the graph is gone");
    size_t after = 0;
    for (const Object& o : r.objects()) {
        after += eval_of(o) ? 1 : 0;
        assert_true(!elements.count(upper(o.handle)), "an element of the definition is gone: " + o.handle);
    }
    assert_eq(static_cast<int>(before - owned), static_cast<int>(after), "the other definitions keep every element");
}

TEST(dynamic_types_long_utf8_text_is_kept_in_every_version_and_format) {
    // about 1000 characters of Swedish text (UTF-8, two bytes each for the letters with a ring or dots), in a label, a lookup cell, a
    // table value and an expression
    std::string text;
    for (int i = 0; i < 150; ++i) text += "\xC3\xA5\xC3\xA4\xC3\xB6 abc";
    for (DxfVersion v : kVersions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string what = std::string(hdxf_test::version_label(v)) + (f == DxfFormat::Binary ? " binary: " : " ASCII: ");
            Drawing d = build_everything(v);
            for (Object& o : d.objects()) {
                if (o.type == ObjectType::BlockFlipParameter) o.as_block_flip_parameter().label = text;
                if (o.type == ObjectType::BlockLookupAction) o.as_block_lookup_action().rows[1][1] = text;
                if (o.type == ObjectType::BlockPropertiesTable) o.as_block_properties_table().rows[0].values[0].text = text;
                if (o.type == ObjectType::BlockHorizontalConstraintParameter) o.as_block_horizontal_constraint_parameter().distance.expression = text;
            }
            Drawing r = load(save(d, f, what), what);
            assert_eq(text, the_object(r, ObjectType::BlockFlipParameter)->as_block_flip_parameter().label, what + "the label");
            assert_eq(text, the_object(r, ObjectType::BlockLookupAction)->as_block_lookup_action().rows[1][1], what + "the lookup cell");
            assert_eq(text, the_object(r, ObjectType::BlockPropertiesTable)->as_block_properties_table().rows[0].values[0].text, what + "the table value");
            assert_eq(text, the_object(r, ObjectType::BlockHorizontalConstraintParameter)->as_block_horizontal_constraint_parameter().distance.expression, what + "the expression");
        }
    }
}
