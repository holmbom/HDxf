#include "test_framework.hpp"
#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The solid history of step 9.14 (plan 2026-10-27-fas-9-typer.md; B100-B105, B111, B113-B116): ACSH_HISTORY_CLASS and its 15
// nodes. The AutoCAD originals are doc/facit/acad_<version>_dxf_history*.dxf (fas9_history.scr: AutoCAD 2025's core, 2007 ...
// 2018, ASCII and binary), the older acad_<version>_dxf_solids*.dxf and assoc_solids*.dxf, and the AutoCAD-saved files of the
// corpus (ACadSharp's sample_AC1021 ... AC1032, jscad's 3Ddraw, Aspose's conic_pyramid, LibreDWG's example_2007). They are local files
// (the facit is not in git): a test whose file is missing is skipped.

using namespace hdxf;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double K = 57.29577951308232;     // 180/pi, the factor of 50, 51 and 55 of the node's matrix

const char* const kNames[] = {"ACSH_HISTORY_CLASS", "ACSH_BOX_CLASS", "ACSH_WEDGE_CLASS", "ACSH_SPHERE_CLASS",
                              "ACSH_CYLINDER_CLASS", "ACSH_CONE_CLASS", "ACSH_TORUS_CLASS", "ACSH_PYRAMID_CLASS",
                              "ACSH_EXTRUSION_CLASS", "ACSH_SWEEP_CLASS", "ACSH_REVOLVE_CLASS", "ACSH_LOFT_CLASS",
                              "ACSH_BOOLEAN_CLASS", "ACSH_BREP_CLASS", "ACSH_CHAMFER_CLASS", "ACSH_FILLET_CLASS"};
const ObjectType kTypes[] = {ObjectType::ShHistory, ObjectType::ShBox, ObjectType::ShWedge, ObjectType::ShSphere,
                             ObjectType::ShCylinder, ObjectType::ShCone, ObjectType::ShTorus, ObjectType::ShPyramid,
                             ObjectType::ShExtrusion, ObjectType::ShSweep, ObjectType::ShRevolve, ObjectType::ShLoft,
                             ObjectType::ShBoolean, ObjectType::ShBrep, ObjectType::ShChamfer, ObjectType::ShFillet};

bool is_sh(ObjectType t) {
    for (ObjectType x : kTypes) if (x == t) return true;
    return false;
}

const char* name_of(ObjectType t) {
    for (size_t i = 0; i < 16; ++i) if (kTypes[i] == t) return kNames[i];
    return "";
}

bool is_node(ObjectType t) { return is_sh(t) && t != ObjectType::ShHistory; }

void eq(int a, int b, const std::string& what) {
    if (a != b) throw hdxf_test::AssertFailure{what + ": " + std::to_string(a) + " is not " + std::to_string(b)};
}
void eq(size_t a, size_t b, const std::string& what) { eq(static_cast<int>(a), static_cast<int>(b), what); }
void eq(const std::string& a, const std::string& b, const std::string& what) {
    if (a != b) throw hdxf_test::AssertFailure{what + ": \"" + a + "\" is not \"" + b + "\""};
}
void near(double a, double b, const std::string& what, double eps = 1e-9) {
    if (!(std::fabs(a - b) <= eps * std::max(1.0, std::fabs(b)))) {
        throw hdxf_test::AssertFailure{what + ": " + std::to_string(a) + " is not " + std::to_string(b)};
    }
}

std::filesystem::path facit_path(const std::string& tag, const std::string& theme, bool binary) {
    return hdxf_test::source_path("doc/facit/acad_" + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf");
}

// A facit file, loaded; the test is skipped when the file is not there
Drawing load_facit(const std::string& tag, const std::string& theme, bool binary = false) {
    std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(facit_path(tag, theme, binary));
    if (bytes.empty()) hdxf_test::skip("doc/facit/acad_" + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf is not here");
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), "cannot read " + theme + ": " + d.last_error());
    return d;
}

std::vector<uint8_t> facit_bytes(const std::string& tag, const std::string& theme, bool binary = false) {
    std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(facit_path(tag, theme, binary));
    if (bytes.empty()) hdxf_test::skip("doc/facit/acad_" + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf is not here");
    return bytes;
}

std::vector<const Object*> of_type(const Drawing& d, ObjectType t) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == t) out.push_back(&o);
    return out;
}

const Object* object_by_handle(const Drawing& d, const std::string& handle) { return d.find_object(handle); }

const Object* only(const Drawing& d, ObjectType t, const std::string& what) {
    std::vector<const Object*> all = of_type(d, t);
    eq(all.size(), static_cast<size_t>(1), what);
    return all[0];
}

// The first node of the type for which `pick` is true
const Object* find_node(const Drawing& d, ObjectType t, const std::function<bool(const Object&)>& pick) {
    for (const Object& o : d.objects()) if (o.type == t && pick(o)) return &o;
    return nullptr;
}

const Object* need(const Object* o, const std::string& what) {
    assert_true(o != nullptr, "no " + what);
    return o;
}

// The history the node belongs to: its graph's owner
const Object* history_of(const Drawing& d, const Object& node) {
    const Object* graph = d.find_object(node.owner);
    if (!graph || graph->type != ObjectType::EvaluationGraph) return nullptr;
    const Object* history = d.find_object(graph->owner);
    return history && history->type == ObjectType::ShHistory ? history : nullptr;
}

bool is_zero_handle(const std::string& h) { return h.empty() || h == "0"; }

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A record's text in an ASCII file
std::string record_text(const std::vector<uint8_t>& bytes, const Object& o) {
    return assoc_test::ascii_record(bytes, name_of(o.type), o.handle);
}

// The groups of a record's text as "code value" pairs
std::vector<std::pair<int, std::string>> groups_of(const std::string& text) {
    std::vector<std::pair<int, std::string>> out;
    std::istringstream in(text);
    std::string a, b;
    while (std::getline(in, a) && std::getline(in, b)) out.push_back({std::atoi(a.c_str()), b});
    return out;
}

// The corpus files that have an ACSH record, ASCII (the AutoCAD-saved ones; a binary twin has the same records)
std::vector<std::filesystem::path> acsh_files() {
    static std::vector<std::filesystem::path> files;
    static bool done = false;
    if (done) return files;
    done = true;
    auto has_record = [](const std::vector<uint8_t>& bytes) {
        return assoc_test::contains(bytes, "  0\r\nACSH_") || assoc_test::contains(bytes, "  0\nACSH_");
    };
    auto take = [&](const std::filesystem::path& p, bool facit_only) {
        std::string ext = p.extension().string();
        for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext != ".dxf") return;
        const std::string name = p.filename().string();
        if (facit_only) {
            // the AutoCAD originals of 2007 and later with a history (the older ones have none)
            bool tag = false;
            for (const char* t : {"acad_2007_", "acad_2010_", "acad_2013_", "acad_2018_"}) tag = tag || name.rfind(t, 0) == 0;
            if (!tag || name.size() > 8 && name.compare(name.size() - 8, 8, "_bin.dxf") == 0) return;
        }
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(p);
        if (assoc_test::is_binary_bytes(bytes) || !has_record(bytes)) return;
        files.push_back(p);
    };
    std::error_code ec;
    const std::filesystem::path data = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR);
    if (std::filesystem::exists(data, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(data, ec); it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file()) take(it->path(), false);
        }
    }
    const std::filesystem::path facit = hdxf_test::source_path("doc/facit");
    if (std::filesystem::exists(facit, ec)) {
        for (auto it = std::filesystem::directory_iterator(facit, ec); it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (it->is_regular_file()) take(it->path(), true);
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

std::vector<std::filesystem::path> distinct_acsh_files() {
    std::set<std::string> seen;
    std::vector<std::filesystem::path> out;
    for (const auto& p : acsh_files()) {
        if (seen.insert(hdxf_test::sha256_hex(hdxf_test::read_file_bytes(p))).second) out.push_back(p);
    }
    return out;
}

int forget_sh_records(Drawing& d) {
    int n = 0;
    for (Object& o : d.objects()) {
        if (is_sh(o.type)) {
            o.raw = RawRecord();
            ++n;
        }
    }
    return n;
}

// The first warning that names an ACSH record ("" when there is none): the other families' warnings are theirs
std::string acsh_warning(const Drawing& d) {
    for (const std::string& w : d.warnings()) if (w.find("ACSH_") != std::string::npos) return w;
    return std::string();
}

bool has_warning(const Drawing& d, const std::string& needle) {
    for (const std::string& w : d.warnings()) if (w.find(needle) != std::string::npos) return true;
    return false;
}

// The text of a file as ASCII lines with the record `handle` edited: the first line of `from` after the record's start replaced
// by `to` (to change one value of AutoCAD's own record)
std::vector<uint8_t> edited(const std::vector<uint8_t>& bytes, const std::string& type, const std::string& handle,
                            const std::vector<std::pair<std::string, std::string>>& replacements) {
    std::string text(bytes.begin(), bytes.end());
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string key = "  0" + eol + type + eol + "  5" + eol + handle + eol;
    size_t at = text.find(key);
    assert_true(at != std::string::npos, "the record " + type + " " + handle + " is not in the file");
    size_t end = text.find(eol + "  0" + eol, at + 4);
    std::string record = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
    for (const auto& [from, to] : replacements) {
        size_t p = record.find(from);
        assert_true(p != std::string::npos, "\"" + from + "\" is not in the record " + type + " " + handle);
        record.replace(p, from.size(), to);
    }
    text.replace(at, end == std::string::npos ? std::string::npos : end - at, record);
    return std::vector<uint8_t>(text.begin(), text.end());
}

// A record of an ASCII file with the nth (0 is the first) group of `code` given the value `value`; an integer keeps the width
// AutoCAD pads it to
std::vector<uint8_t> set_group(const std::vector<uint8_t>& bytes, const std::string& type, const std::string& handle, int code,
                               const std::string& value, size_t nth = 0) {
    std::string text(bytes.begin(), bytes.end());
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string key = "  0" + eol + type + eol + "  5" + eol + handle + eol;
    size_t at = text.find(key);
    assert_true(at != std::string::npos, "the record " + type + " " + handle + " is not in the file");
    size_t end = text.find(eol + "  0" + eol, at + 4);
    if (end == std::string::npos) end = text.size();
    auto trim = [](std::string v) {
        size_t a = v.find_first_not_of(' ');
        return a == std::string::npos ? std::string() : v.substr(a);
    };
    size_t pos = at;
    size_t seen = 0;
    while (pos < end) {
        const size_t code_end = text.find(eol, pos);
        const size_t value_begin = code_end + eol.size();
        const size_t value_end = text.find(eol, value_begin);
        if (code_end == std::string::npos || value_end == std::string::npos) break;
        if (std::atoi(text.substr(pos, code_end - pos).c_str()) == code && trim(text.substr(pos, code_end - pos)) == std::to_string(code)) {
            if (seen++ == nth) {
                const std::string old = text.substr(value_begin, value_end - value_begin);
                const bool padded = !old.empty() && old[0] == ' ';
                std::string replacement = value;
                if (padded && value.size() < old.size()) replacement = std::string(old.size() - value.size(), ' ') + value;
                text.replace(value_begin, value_end - value_begin, replacement);
                return std::vector<uint8_t>(text.begin(), text.end());
            }
        }
        pos = value_end + eol.size();
    }
    throw hdxf_test::AssertFailure{"group " + std::to_string(code) + " (" + std::to_string(nth) + ") is not in the record " + type + " " + handle};
}

// A record of an ASCII file with one more group after its last (a later version of the record)
std::vector<uint8_t> append_group(const std::vector<uint8_t>& bytes, const std::string& type, const std::string& handle, int code,
                                  const std::string& value) {
    std::string text(bytes.begin(), bytes.end());
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string key = "  0" + eol + type + eol + "  5" + eol + handle + eol;
    const size_t at = text.find(key);
    assert_true(at != std::string::npos, "the record " + type + " " + handle + " is not in the file");
    size_t end = text.find(eol + "  0" + eol, at + 4);
    assert_true(end != std::string::npos, "the record's end");
    end += eol.size();
    std::string c = std::to_string(code);
    while (c.size() < 3) c = " " + c;
    text.insert(end, c + eol + value + eol);
    return std::vector<uint8_t>(text.begin(), text.end());
}

std::string group_text(int code, const std::string& value, const std::string& eol = "\r\n") {
    std::string c = std::to_string(code);
    while (c.size() < 3) c = " " + c;
    return c + eol + value + eol;
}

// ---------------------------------------------------------------------------
// What a new history is made of (a solid with no geometry: the history is not its B-rep)
// ---------------------------------------------------------------------------

ShSourceGeometry bits_source(int class_id, int bits, size_t extra_bytes = 1) {
    ShSourceGeometry g;
    g.class_id = class_id;
    ShBitStream s;
    s.bits = bits;
    for (size_t i = 0; i < (static_cast<size_t>(bits) + 7) / 8 + extra_bytes; ++i) s.data.push_back(static_cast<uint8_t>(i * 7 + 3));
    g.data = std::move(s);
    return g;
}

ShSourceGeometry sat_source(int class_id, std::vector<std::string> lines) {
    ShSourceGeometry g;
    g.class_id = class_id;
    ShSatSource s;
    s.lines = std::move(lines);
    g.data = std::move(s);
    return g;
}

std::vector<std::string> some_sat_lines() {
    return {"mmloo mh m k", "ni ^ *+0;:,4 ^ *+0\\^ [ mo", "mjqlfffffffffffffgjg fqf", std::string(300, 'q') + "tail", "last line"};
}

// A solid "A01" with a history and the node of `type` (the operations after the bodies they work on)
std::string build_solid_history(Drawing& d, ObjectType type) {
    Entity& solid = d.add_acis(EntityType::Solid3D, AcisData{});
    solid.props.handle = "A01";
    Object* h = d.add_solid_history("A01");
    assert_true(h != nullptr, "add_solid_history");
    const std::string history = h->handle;
    auto add = [&](ObjectType t, ObjectData data, std::vector<int32_t> operands = {}) {
        Object* o = d.add_solid_history_node(history, t, std::move(data), operands);
        assert_true(o != nullptr, std::string("add_solid_history_node ") + name_of(t));
        return o->handle;
    };
    switch (type) {
        case ObjectType::ShBox: {
            ShBoxData n;
            n.length = 21.5; n.width = 7.25; n.height = 3.125;
            n.node.set_transform({1, 0, 0, 4, 0, 1, 0, 5, 0, 0, 1, 6, 0, 0, 0, 1});
            add(type, Boxed<ShBoxData>(n));
            break;
        }
        case ObjectType::ShWedge: {
            ShWedgeData n;
            n.length = 5; n.width = 6; n.height = 7;
            add(type, Boxed<ShWedgeData>(n));
            break;
        }
        case ObjectType::ShSphere: {
            ShSphereData n;
            n.radius = 12.75;
            add(type, Boxed<ShSphereData>(n));
            break;
        }
        case ObjectType::ShCylinder: {
            ShCylinderData n;
            n.body.height = 20; n.body.base_radius_1 = 4; n.body.base_radius_2 = 10; n.body.top_radius = 4;
            add(type, Boxed<ShCylinderData>(n));
            break;
        }
        case ObjectType::ShCone: {
            ShConeData n;
            n.body.height = 30; n.body.base_radius_1 = 12; n.body.base_radius_2 = 12; n.body.top_radius = 5;
            add(type, Boxed<ShConeData>(n));
            break;
        }
        case ObjectType::ShTorus: {
            ShTorusData n;
            n.major_radius = 15; n.minor_radius = 4;
            add(type, Boxed<ShTorusData>(n));
            break;
        }
        case ObjectType::ShPyramid: {
            ShPyramidData n;
            n.height = 25; n.sides = 7; n.radius = 18.5; n.top_radius = 3.5;
            add(type, Boxed<ShPyramidData>(n));
            break;
        }
        case ObjectType::ShExtrusion: {
            ShExtrusionData n;
            n.base.vector = {0, 0, 25};
            n.base.profile = bits_source(77, 672);
            n.base.options.draft_angle = 0.17453292519943295;
            n.base.options.alignment = 2;
            add(type, Boxed<ShExtrusionData>(n));
            break;
        }
        case ObjectType::ShSweep: {
            ShSweepData n;
            n.base.profile = bits_source(18, 464);
            n.base.path = sat_source(16, some_sat_lines());
            n.base.options.sweep_matrix = {1, 0, 0, 100, 0, 1, 0, 60, 0, 0, 1, 0, 0, 0, 0, 1};
            n.base.options.path_matrix = {0.5, 0.25, 0.125, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
            n.base.options.bank = true;
            add(type, Boxed<ShSweepData>(n));
            break;
        }
        case ObjectType::ShRevolve: {
            ShRevolveData n;
            n.axis_point = {50, 162.5, 0};
            n.axis_vector = {0, 1, 0};
            n.angle = 4.71238898038469;
            n.profile = bits_source(77, 544);
            n.twist = 0.5;
            add(type, Boxed<ShRevolveData>(n));
            break;
        }
        case ObjectType::ShLoft: {
            ShLoftData n;
            n.sections = {bits_source(18, 144), bits_source(18, 272), sat_source(19, some_sat_lines())};
            n.guides = {bits_source(36, 592), bits_source(36, 592)};
            n.path = bits_source(36, 592);
            n.plane_normal_lofting = 3;
            n.ruled = true;
            add(type, Boxed<ShLoftData>(n));
            break;
        }
        case ObjectType::ShBoolean: {
            add(ObjectType::ShBox, Boxed<ShBoxData>(ShBoxData{}));
            add(ObjectType::ShSphere, Boxed<ShSphereData>(ShSphereData{}));
            ShBooleanData n;
            n.operation = ShBooleanOperation::Subtract;
            n.first_node_id = 1;
            n.second_node_id = 2;
            add(type, Boxed<ShBooleanData>(n), {1, 2});
            break;
        }
        case ObjectType::ShBrep: {
            ShBrepData n;
            n.sat.lines = some_sat_lines();
            add(type, Boxed<ShBrepData>(n));
            break;
        }
        case ObjectType::ShChamfer: {
            add(ObjectType::ShBox, Boxed<ShBoxData>(ShBoxData{}));
            ShChamferData n;
            n.base_node_id = 1;
            n.distance_1 = 2; n.distance_2 = 3;
            n.edge_ids = {13, 14, 15};
            n.face_id = 16;
            add(type, Boxed<ShChamferData>(n), {1});
            break;
        }
        case ObjectType::ShFillet: {
            add(ObjectType::ShBox, Boxed<ShBoxData>(ShBoxData{}));
            ShFilletData n;
            n.base_node_id = 1;
            n.edges = {{9, 2.0, 0.0, 0.0}, {10, 2.5, 0.5, 0.25}, {11, 3.0, 0.0, 0.0}};
            add(type, Boxed<ShFilletData>(n), {1});
            break;
        }
        default:
            break;
    }
    return history;
}

// What build_solid_history made, in the drawing read back: "" or what differs
std::string check_built(const Drawing& r, ObjectType type) {
    const Object* history = need(of_type(r, ObjectType::ShHistory).empty() ? nullptr : of_type(r, ObjectType::ShHistory)[0], "history");
    const Object* graph = r.find_object(history->as_sh_history().graph);
    if (!graph || graph->type != ObjectType::EvaluationGraph) return "the history has no graph";
    if (upper(graph->owner) != upper(history->handle)) return "the graph's owner is not the history";
    const EvaluationGraphData& g = graph->as_evaluation_graph();
    if (history->as_sh_history().last_node_id != g.last_node_id) return "the history's 92 is not the graph's highest id";
    const Entity* solid = r.find_entity(history->owner);
    if (!solid || solid->type != EntityType::Solid3D) return "the history's owner is not a solid";
    if (upper(solid->as_acis().history_handle) != upper(history->handle)) return "the solid's 350 is not the history";
    for (const EvalGraphNode& n : g.nodes) {
        const Object* node = r.find_object(n.object);
        if (!node || !is_node(node->type)) return "a graph node holds no history node";
        if (upper(node->owner) != upper(graph->handle)) return "a node's owner is not the graph";
        const ShHistoryNodeData* data = nullptr;
        switch (node->type) {
            case ObjectType::ShBox: data = &node->as_sh_box().node; break;
            case ObjectType::ShSphere: data = &node->as_sh_sphere().node; break;
            default: break;
        }
        if (data && data->material.empty()) return "a node has no material";
    }
    const Object* o = nullptr;
    for (const Object& c : r.objects()) if (c.type == type) o = &c;
    if (!o) return std::string("no ") + name_of(type);
    switch (type) {
        case ObjectType::ShBox: {
            const ShBoxData& d = o->as_sh_box();
            if (d.length != 21.5 || d.width != 7.25 || d.height != 3.125) return "box sizes";
            const std::array<double, 16> t = d.node.transform();
            if (std::fabs(t[3] - 4) > 1e-12 || std::fabs(t[7] - 5) > 1e-12 || std::fabs(t[11] - 6) > 1e-12) return "box placement";
            if (std::fabs(d.node.matrix[11] - 6.0 * K) > 1e-9) return "box 51 is K times the z placement";
            if (d.eval.node_id != 1 || d.eval.version != 33 || d.eval.maintenance != 327) return "box eval";
            if (d.node.history_id != 1 || d.node.color != 256) return "box node 92, 62";
            break;
        }
        case ObjectType::ShWedge: if (o->as_sh_wedge().height != 7.0 || o->as_sh_wedge().width != 6.0) return "wedge"; break;
        case ObjectType::ShSphere: if (o->as_sh_sphere().radius != 12.75) return "sphere"; break;
        case ObjectType::ShCylinder: {
            const ShCylinderBody& b = o->as_sh_cylinder().body;
            if (b.height != 20 || b.base_radius_1 != 4 || b.base_radius_2 != 10 || b.top_radius != 4) return "cylinder";
            break;
        }
        case ObjectType::ShCone: {
            const ShCylinderBody& b = o->as_sh_cone().body;
            if (b.height != 30 || b.base_radius_1 != 12 || b.top_radius != 5) return "cone";
            break;
        }
        case ObjectType::ShTorus: if (o->as_sh_torus().major_radius != 15 || o->as_sh_torus().minor_radius != 4) return "torus"; break;
        case ObjectType::ShPyramid: {
            const ShPyramidData& d = o->as_sh_pyramid();
            if (d.height != 25 || d.sides != 7 || d.radius != 18.5 || d.top_radius != 3.5) return "pyramid";
            break;
        }
        case ObjectType::ShExtrusion: {
            const ShSweepBaseData& b = o->as_sh_extrusion().base;
            if (b.vector.z != 25 || b.profile.class_id != 77 || !b.profile.bit_stream()) return "extrusion profile";
            if (b.profile.bit_stream()->bits != 672 || b.profile.bit_stream()->data.size() != 85) return "the 672 bits and their 85 bytes";
            if (b.path.class_id != 0 || b.path.has_data()) return "extrusion path";
            if (std::fabs(b.options.draft_angle - 0.17453292519943295) > 1e-15 || b.options.alignment != 2) return "extrusion options";
            if (b.options.sweep_matrix.size() != 16 || b.options.sweep_matrix[0] != 1.0) return "extrusion matrices";
            break;
        }
        case ObjectType::ShSweep: {
            const ShSweepBaseData& b = o->as_sh_sweep().base;
            if (!b.profile.bit_stream() || b.profile.bit_stream()->bits != 464) return "sweep profile";
            if (!b.path.sat() || b.path.class_id != 16 || b.path.sat()->lines != some_sat_lines()) return "sweep path lines";
            if (b.options.sweep_matrix[3] != 100.0 || b.options.path_matrix[0] != 0.5 || b.options.path_matrix[15] != 13.0) return "sweep matrices";
            if (!b.options.bank) return "sweep bank";
            break;
        }
        case ObjectType::ShRevolve: {
            const ShRevolveData& d = o->as_sh_revolve();
            if (d.axis_point.y != 162.5 || d.axis_vector.y != 1.0 || d.angle != 4.71238898038469 || d.twist != 0.5) return "revolve";
            if (!d.profile.bit_stream() || d.profile.bit_stream()->bits != 544) return "revolve profile";
            break;
        }
        case ObjectType::ShLoft: {
            const ShLoftData& d = o->as_sh_loft();
            if (d.sections.size() != 3 || d.guides.size() != 2 || d.path.class_id != 36) return "loft counts";
            if (!d.sections[2].sat() || d.sections[2].sat()->lines != some_sat_lines()) return "loft SAT section";
            if (!d.sections[0].bit_stream() || d.sections[0].bit_stream()->bits != 144) return "loft section bits";
            if (d.plane_normal_lofting != 3 || !d.ruled) return "loft options";
            break;
        }
        case ObjectType::ShBoolean: {
            const ShBooleanData& d = o->as_sh_boolean();
            if (d.operation != ShBooleanOperation::Subtract || d.first_node_id != 1 || d.second_node_id != 2) return "boolean";
            if (g.edges.size() != 2 || g.edges[0].from != 0 || g.edges[0].to != 2 || g.edges[1].from != 1 || g.edges[1].to != 2) {
                return "the boolean's two edges";
            }
            break;
        }
        case ObjectType::ShBrep:
            if (o->as_sh_brep().sat.lines != some_sat_lines()) return "brep lines";
            break;
        case ObjectType::ShChamfer: {
            const ShChamferData& d = o->as_sh_chamfer();
            if (d.base_node_id != 1 || d.distance_1 != 2 || d.distance_2 != 3 || d.edge_ids.size() != 3 || d.edge_ids[2] != 15 ||
                d.face_id != 16) {
                return "chamfer";
            }
            break;
        }
        case ObjectType::ShFillet: {
            const ShFilletData& d = o->as_sh_fillet();
            if (d.edges.size() != 3 || d.edges[1].edge_id != 10 || d.edges[1].radius != 2.5 || d.edges[1].value_42 != 0.5 ||
                d.edges[1].value_43 != 0.25) {
                return "fillet";
            }
            break;
        }
        default:
            break;
    }
    return "";
}

// The text of the facit's own record with its graph and owners: used to see what a change touched
std::vector<std::string> record_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// What AutoCAD's own files hold
// ---------------------------------------------------------------------------

TEST(sh_every_record_of_the_corpus_is_typed) {
    std::vector<std::filesystem::path> files = distinct_acsh_files();
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::map<std::string, int> total;
    std::vector<std::string> failures;
    int loaded = 0;
    for (const auto& path : files) {
        Drawing d;
        if (!assoc_test::load_file(d, path)) continue;
        ++loaded;
        const std::string name = path.filename().string();
        for (const Object& o : d.objects()) {
            if (is_sh(o.type)) {
                ++total[name_of(o.type)];
            } else if (o.type == ObjectType::Unknown) {
                for (const char* n : kNames) {
                    if (o.as_unknown().object_type == n) failures.push_back(name + ": " + n + " " + o.handle + " is untyped");
                }
            }
        }
        for (const std::string& w : d.warnings()) {
            if (w.find("ACSH_") != std::string::npos) failures.push_back(name + ": " + w);
        }
    }
    std::cout << "ACSH corpus: " << loaded << " files";
    for (const auto& [n, c] : total) std::cout << ", " << c << " " << n;
    std::cout << "\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 12; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " records are not typed:" + message);
    if (std::filesystem::exists(facit_path("2018", "history_brep", false))) {
        for (const char* n : kNames) assert_true(total[n] > 0, std::string("the corpus has no ") + n);
    }
}

TEST(sh_written_unchanged_is_the_file_and_from_the_typed_values_is_autocads_record) {
    std::vector<std::filesystem::path> files = distinct_acsh_files();
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::set<std::string> seen_records;
    std::vector<std::string> failures;
    int checked = 0, records = 0;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = assoc_test::bytes_of(path);
        const std::string name = path.filename().string();
        if (path.generic_string().find("/ezdxf/") != std::string::npos) continue;
        Drawing d;
        if (!assoc_test::load_bytes(d, bytes)) continue;
        std::vector<std::pair<std::string, std::string>> ids;
        for (const Object& o : d.objects()) if (is_sh(o.type)) ids.push_back({name_of(o.type), o.handle});
        if (ids.empty()) continue;
        // A file whose records all have the text of records already checked adds nothing
        bool fresh = false;
        for (const auto& [type, handle] : ids) {
            if (seen_records.insert(type + "\n" + assoc_test::ascii_record(bytes, type, handle)).second) fresh = true;
        }
        if (!fresh) continue;
        // unchanged: the file's own bytes (B12)
        std::vector<uint8_t> again = d.save_to_memory(DxfFormat::Ascii);
        if (again != bytes) {
            const std::string difference = assoc_test::first_difference(bytes, again);
            if (!difference.empty()) {
                failures.push_back(name + " unchanged: " + difference);
                continue;
            }
        }
        // from the typed values alone: the records are AutoCAD's own
        forget_sh_records(d);
        std::vector<uint8_t> regenerated = d.save_to_memory(DxfFormat::Ascii);
        ++checked;
        if (regenerated.empty()) {
            failures.push_back(name + " from the typed values: not written: " + d.last_error());
            continue;
        }
        for (const auto& [type, handle] : ids) {
            const std::string expected = assoc_test::ascii_record(bytes, type, handle);
            const std::string got = assoc_test::ascii_record(regenerated, type, handle);
            ++records;
            const std::string difference = expected.empty() ? std::string("not in the file")
                                           : got.empty() ? std::string("not written") : assoc_test::record_difference(expected, got);
            if (!difference.empty()) {
                failures.push_back(name + " " + type + " " + handle + ": " + difference);
                break;
            }
        }
    }
    std::cout << "ACSH regeneration: " << checked << " files, " << records << " records\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 10; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " files differ:" + message);
}

TEST(sh_binary_facit_is_read_like_its_ascii_twin_and_written_from_typed_values_as_the_twin) {
    int checked = 0;
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        for (const char* theme : {"history_primitives", "history_profiles", "history_brep"}) {
            if (!std::filesystem::exists(facit_path(tag, theme, true))) continue;
            std::vector<uint8_t> ascii_bytes = facit_bytes(tag, theme, false);
            std::vector<uint8_t> binary_bytes = facit_bytes(tag, theme, true);
            Drawing a, b;
            assert_true(assoc_test::load_bytes(a, ascii_bytes) && assoc_test::load_bytes(b, binary_bytes), "load");
            const std::string what = std::string(tag) + " " + theme + ": ";
            // the same history records, typed alike
            size_t records = 0;
            for (const Object& o : a.objects()) {
                if (!is_sh(o.type)) continue;
                ++records;
                const Object* twin = b.find_object(o.handle);
                assert_true(twin && twin->type == o.type, what + o.handle + " has no typed twin in the binary file");
            }
            assert_true(records > 0, what + "no records");
            // unchanged: the binary bytes; and from the typed values alone: the twin's records
            assert_true(b.save_to_memory(DxfFormat::Binary) == binary_bytes, what + "binary written unchanged is not the file");
            forget_sh_records(b);
            std::vector<uint8_t> regenerated = b.save_to_memory(DxfFormat::Binary);
            assert_true(!regenerated.empty(), what + "not written: " + b.last_error());
            Drawing r;
            assert_true(assoc_test::load_bytes(r, regenerated), what + "cannot be read");
            std::vector<uint8_t> as_text = assoc_test::as_ascii(regenerated);
            const std::vector<uint8_t> twin_text = assoc_test::as_ascii(binary_bytes);
            for (const Object& o : a.objects()) {
                if (!is_sh(o.type)) continue;
                const std::string expected = assoc_test::ascii_record(twin_text, name_of(o.type), o.handle);
                const std::string got = assoc_test::ascii_record(as_text, name_of(o.type), o.handle);
                const std::string difference = assoc_test::record_difference(expected, got);
                assert_true(difference.empty(), what + name_of(o.type) + " " + o.handle + ": " + difference);
            }
            ++checked;
        }
    }
    if (checked == 0) hdxf_test::skip("no binary history facit");
}

TEST(sh_primitives_from_autocads_facit_in_every_version) {
    int checked = 0;
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            if (!std::filesystem::exists(facit_path(tag, "history_primitives", binary))) continue;
            Drawing d = load_facit(tag, "history_primitives", binary);
            const std::string what = std::string(tag) + (binary ? " binary: " : ": ");
            assert_true(acsh_warning(d).empty(), what + "no ACSH warning on reading: " + acsh_warning(d));
            // the primitives, by their sizes (AutoCAD's own numbers)
            const Object* box = need(find_node(d, ObjectType::ShBox, [](const Object&) { return true; }), what + "box");
            const ShBoxData& b = box->as_sh_box();
            near(b.length, 20, what + "box 40");
            near(b.width, 15, what + "box 41");
            near(b.height, 20, what + "box 42");
            eq(b.part.version, 33, what + "AcDbShBox 90");
            eq(b.part.maintenance, 327, what + "AcDbShBox 91");
            eq(b.eval.node_id, 1, what + "AcDbEvalExpr 90");
            eq(b.eval.version, 33, what + "AcDbEvalExpr 98");
            eq(b.node.color, 256, what + "62");
            eq(b.node.history_id, 1, what + "92");
            assert_false(b.node.material.empty(), what + "347");
            // the matrix: 50, 51 and 55 are 180/pi times the elements; the box's centre is (10, 7.5, 10)
            near(b.node.matrix[10], K, what + "50");
            near(b.node.matrix[15], K, what + "55");
            near(b.node.matrix[3], 10, what + "43");
            near(b.node.matrix[7], 7.5, what + "47");
            near(b.node.matrix[11], 10 * K, what + "51");
            const std::array<double, 16> t = b.node.transform();
            near(t[10], 1, what + "the matrix's (2,2)");
            near(t[11], 10, what + "the matrix's z placement");
            near(t[15], 1, what + "the matrix's (3,3)");
            const Object* sphere = need(find_node(d, ObjectType::ShSphere, [](const Object&) { return true; }), what + "sphere");
            near(sphere->as_sh_sphere().radius, 12, what + "sphere 40");
            near(sphere->as_sh_sphere().node.matrix[3], 50, what + "sphere 43");
            const Object* wedge = need(find_node(d, ObjectType::ShWedge, [](const Object&) { return true; }), what + "wedge");
            near(wedge->as_sh_wedge().length, 25, what + "wedge 40");
            near(wedge->as_sh_wedge().width, 20, what + "wedge 41");
            near(wedge->as_sh_wedge().height, 20, what + "wedge 42");
            const Object* torus = need(find_node(d, ObjectType::ShTorus, [](const Object&) { return true; }), what + "torus");
            near(torus->as_sh_torus().major_radius, 15, what + "torus 40");
            near(torus->as_sh_torus().minor_radius, 4, what + "torus 41");
            // cylinders: round (r 10, h 25), elliptical (4 and 10, h 20; the node is turned a quarter turn)
            const Object* cyl = need(find_node(d, ObjectType::ShCylinder, [](const Object& o) { return o.as_sh_cylinder().body.height == 25.0; }),
                                     what + "round cylinder");
            const ShCylinderBody& cb = cyl->as_sh_cylinder().body;
            near(cb.base_radius_1, 10, what + "cylinder 41");
            near(cb.base_radius_2, 10, what + "cylinder 42");
            near(cb.top_radius, 10, what + "cylinder 43");
            const Object* ell = need(find_node(d, ObjectType::ShCylinder, [](const Object& o) { return o.as_sh_cylinder().body.height == 20.0; }),
                                     what + "elliptical cylinder");
            const ShCylinderBody& eb = ell->as_sh_cylinder().body;
            near(eb.base_radius_1, 4, what + "ellipse 41");
            near(eb.base_radius_2, 10, what + "ellipse 42");
            near(eb.top_radius, 4, what + "ellipse 43");
            near(ell->as_sh_cylinder().node.transform()[1], -1, what + "the ellipse's node is turned: -sin");
            // cones: the cylinder subclass with a top radius 0 and a frustum's 5
            const Object* cone = need(find_node(d, ObjectType::ShCone, [](const Object& o) { return o.as_sh_cone().body.top_radius == 0.0; }),
                                      what + "cone");
            near(cone->as_sh_cone().body.height, 30, what + "cone 40");
            near(cone->as_sh_cone().body.base_radius_1, 12, what + "cone 41");
            const Object* frustum = need(find_node(d, ObjectType::ShCone, [](const Object& o) { return o.as_sh_cone().body.top_radius == 5.0; }),
                                         what + "frustum");
            near(frustum->as_sh_cone().body.height, 30, what + "frustum 40");
            // pyramids: PYRAMID 15 with 5 sides is the circumscribed radius 15 / cos(36 degrees)
            const Object* pyramid = need(find_node(d, ObjectType::ShPyramid, [](const Object& o) { return o.as_sh_pyramid().sides == 5; }), what + "pyramid");
            near(pyramid->as_sh_pyramid().radius, 15 / std::cos(kPi / 5), what + "pyramid 41", 1e-12);
            near(pyramid->as_sh_pyramid().top_radius, 0, what + "pyramid 42");
            const Object* top = need(find_node(d, ObjectType::ShPyramid, [](const Object& o) { return o.as_sh_pyramid().sides == 6; }), what + "pyramid frustum");
            near(top->as_sh_pyramid().radius, 14 / std::cos(kPi / 6), what + "pyramid frustum 41", 1e-12);
            near(top->as_sh_pyramid().top_radius, 8 / std::cos(kPi / 6), what + "pyramid frustum 42", 1e-12);
            // every history: the solid owns it, it owns its graph, the graph's highest id is the history's 92
            for (const Object* h : of_type(d, ObjectType::ShHistory)) {
                const Entity* solid = d.find_entity(h->owner);
                assert_true(solid && solid->type == EntityType::Solid3D, what + "the history's owner is a solid");
                eq(upper(solid->as_acis().history_handle), upper(h->handle), what + "the solid's 350");
                const Object* graph = d.find_object(h->as_sh_history().graph);
                assert_true(graph && graph->type == ObjectType::EvaluationGraph, what + "the history's graph");
                eq(h->as_sh_history().last_node_id, graph->as_evaluation_graph().last_node_id, what + "92 is the graph's highest id");
                eq(h->as_sh_history().version, 33, what + "AcDbShHistory 90");
                eq(h->as_sh_history().flag_280, 0, what + "280");
                for (const EvalGraphNode& n : graph->as_evaluation_graph().nodes) {
                    const Object* node = d.find_object(n.object);
                    assert_true(node && is_node(node->type), what + "the node of the graph is a history node");
                    eq(upper(node->owner), upper(graph->handle), what + "the graph owns its nodes");
                }
            }
            ++checked;
        }
    }
    if (checked == 0) hdxf_test::skip("no history facit");
}

TEST(sh_rotate_move_mirror_change_the_nodes_matrix_as_the_command_does) {
    if (!std::filesystem::exists(facit_path("2018", "history_primitives_rotated", false))) hdxf_test::skip("no facit");
    auto box_of = [](const Drawing& d, bool second) {
        std::vector<const Object*> boxes = of_type(d, ObjectType::ShBox);
        assert_true(!boxes.empty(), "a box");
        return second ? boxes.back() : boxes.front();
    };
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        const std::string what = std::string(tag) + ": ";
        // rotated 30 degrees about the X axis through the origin: the centre (10, 7.5, 10) goes to (10, 7.5 cos30 - 10 sin30,
        // 7.5 sin30 + 10 cos30) and the rotation is in the rows
        std::array<double, 16> t = box_of(load_facit(tag, "history_primitives_rotated"), false)->as_sh_box().node.transform();
        const double c = std::cos(kPi / 6), s = std::sin(kPi / 6);
        near(t[3], 10, what + "x");
        near(t[7], 7.5 * c - 10 * s, what + "y", 1e-12);
        near(t[11], 7.5 * s + 10 * c, what + "z", 1e-12);
        near(t[5], c, what + "cos"); near(t[6], -s, what + "-sin"); near(t[9], s, what + "sin"); near(t[10], c, what + "cos 2,2");
        near(t[0] * t[0] + t[1] * t[1] + t[2] * t[2], 1, what + "an orthonormal row");
        // moved by (10, 5): the history stays and the placement follows
        std::array<double, 16> m = box_of(load_facit(tag, "history_primitives_moved"), false)->as_sh_box().node.transform();
        near(m[3], 20, what + "moved x");
        near(m[7], 12.5, what + "moved y");
        near(m[11], 10, what + "moved z");
        // mirrored in the plane y = -10: a second history (the mirror is a new solid) whose matrix has determinant -1
        Drawing mirrored = load_facit(tag, "history_primitives_mirrored");
        std::vector<const Object*> boxes = of_type(mirrored, ObjectType::ShBox);
        eq(boxes.size(), static_cast<size_t>(2), what + "two boxes after MIRROR3D");
        std::array<double, 16> q = box_of(mirrored, true)->as_sh_box().node.transform();
        near(q[7], -27.5, what + "mirrored y");
        const double det = q[0] * (q[5] * q[10] - q[6] * q[9]) - q[1] * (q[4] * q[10] - q[6] * q[8]) + q[2] * (q[4] * q[9] - q[5] * q[8]);
        near(det, -1, what + "determinant", 1e-9);
        near(box_of(mirrored, true)->as_sh_box().length, 20, what + "same size");
    }
}

TEST(sh_profiles_from_autocads_facit) {
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            if (!std::filesystem::exists(facit_path(tag, "history_profiles", binary))) continue;
            Drawing d = load_facit(tag, "history_profiles", binary);
            const std::string what = std::string(tag) + (binary ? " binary: " : ": ");
            assert_true(acsh_warning(d).empty(), what + "no ACSH warning on reading: " + acsh_warning(d));

            // the extrusions: the rectangle (a bit stream, class 77, 544 bits) and the region (SAT lines, class 37, a taper of 10 degrees)
            std::vector<const Object*> extrusions = of_type(d, ObjectType::ShExtrusion);
            eq(extrusions.size(), static_cast<size_t>(2), what + "two extrusions");
            const Object* rect = nullptr;
            const Object* region = nullptr;
            for (const Object* o : extrusions) (o->as_sh_extrusion().base.profile.sat() ? region : rect) = o;
            assert_true(rect && region, what + "one extrusion of each source form");
            const ShSweepBaseData& rb = rect->as_sh_extrusion().base;
            eq(rb.profile.class_id, 77, what + "92");
            eq(rb.profile.bit_stream()->bits, 544, what + "90");
            eq(rb.profile.bit_stream()->data.size(), static_cast<size_t>(69), what + "544 bits are 68 bytes and the file has 69");
            eq(rb.path.class_id, 0, what + "93");
            assert_false(rb.path.has_data(), what + "no path");
            near(rb.vector.z, 25, what + "10 20 30");
            eq(rb.options.sweep_matrix.size(), static_cast<size_t>(16), what + "46 x 16");
            eq(rb.options.path_matrix.size(), static_cast<size_t>(16), what + "47 x 16");
            assert_true(rb.options.solid, what + "290");
            eq(rb.options.alignment, 0, what + "70");
            eq(rb.options.option_71, 2, what + "71");
            const ShSweepBaseData& gb = region->as_sh_extrusion().base;
            eq(gb.profile.class_id, 37, what + "92 of the region");
            eq(gb.profile.sat()->version, 1, what + "70 of the SAT form");
            assert_true(gb.profile.sat()->lines.size() > 10, what + "the SAT lines");
            near(gb.options.draft_angle, 10 * kPi / 180, what + "42, a taper of 10 degrees", 1e-12);
            near(gb.vector.z, 25, what + "10 20 30");

            // the sweeps: the circle swept along the 3D polyline and the circle extruded along it: two source geometries each
            std::vector<const Object*> sweeps = of_type(d, ObjectType::ShSweep);
            eq(sweeps.size(), static_cast<size_t>(2), what + "two sweeps");
            for (const Object* o : sweeps) {
                const ShSweepBaseData& s = o->as_sh_sweep().base;
                eq(s.profile.class_id, 18, what + "the circle's class");
                assert_true(s.profile.bit_stream() != nullptr, what + "the circle is a bit stream");
                eq(s.path.class_id, 16, what + "the 3D polyline's class");
                assert_true(s.path.sat() != nullptr && s.path.sat()->lines.size() > 10, what + "the path is SAT lines");
                eq(s.options.sweep_matrix.size(), static_cast<size_t>(16), what + "46");
                assert_true(s.options.base_point_set && s.options.sweep_transform_computed && s.options.path_transform_computed,
                            what + "294, 295, 296");
            }
            // the sweep's profile matrix places the circle at (150, 70, 0), the extrusion along a path at (100, 70, 0)
            std::set<int> sweep_x;
            for (const Object* o : sweeps) sweep_x.insert(static_cast<int>(std::lround(o->as_sh_sweep().base.options.sweep_matrix[3])));
            assert_true(sweep_x.count(100) == 1 && sweep_x.count(150) == 1, what + "the profile matrices");

            // the revolve: 270 degrees about the axis (50, 150) - (50, 175): the axis point is the middle of it
            const ShRevolveData& rv = only(d, ObjectType::ShRevolve, what + "one revolve")->as_sh_revolve();
            near(rv.angle, 1.5 * kPi, what + "40", 1e-12);
            near(rv.axis_point.x, 50, what + "10");
            near(rv.axis_point.y, 162.5, what + "20");
            near(rv.axis_vector.y, 1, what + "21");
            eq(rv.profile.class_id, 77, what + "92");
            eq(rv.profile.bit_stream()->bits, 544, what + "90");
            assert_true(rv.solid && !rv.close_to_axis, what + "290, 291");

            // the lofts: three cross sections; two with a guide each pair; with a path
            std::vector<const Object*> lofts = of_type(d, ObjectType::ShLoft);
            eq(lofts.size(), static_cast<size_t>(3), what + "three lofts");
            const Object* plain = nullptr;
            const Object* guided = nullptr;
            const Object* pathed = nullptr;
            for (const Object* o : lofts) {
                const ShLoftData& l = o->as_sh_loft();
                if (l.sections.size() == 3) plain = o;
                else if (!l.guides.empty()) guided = o;
                else if (l.path.class_id != 0) pathed = o;
            }
            assert_true(plain && guided && pathed, what + "the three kinds of loft");
            const ShLoftData& pl = plain->as_sh_loft();
            eq(pl.guides.size(), static_cast<size_t>(0), what + "no guides");
            eq(pl.path.class_id, 0, what + "98 0");
            assert_false(pl.path.has_data(), what + "no path data");
            eq(pl.sections[0].bit_stream()->bits, 208, what + "94");
            eq(pl.sections[1].bit_stream()->bits, 272, what + "94");
            for (const ShSourceGeometry& s : pl.sections) {
                eq(s.class_id, 18, what + "93");
                assert_true(s.bit_stream()->data.size() >= (static_cast<size_t>(s.bit_stream()->bits) + 7) / 8, what + "310");
            }
            near(pl.start_draft_angle, kPi / 2, what + "41", 1e-12);
            assert_true(pl.solid && pl.no_twist && pl.align_direction && pl.simple_surfaces && pl.virtual_guide, what + "the flags");
            assert_true(!pl.closed && !pl.ruled && !pl.arc_length_parameters, what + "the other flags");
            const ShLoftData& gl = guided->as_sh_loft();
            eq(gl.sections.size(), static_cast<size_t>(2), what + "92 of the guided loft");
            eq(gl.guides.size(), static_cast<size_t>(2), what + "95");
            eq(gl.guides[0].class_id, 36, what + "96");
            // AutoCAD 2007 and 2010 write the spline as 576 bits, 2013 and later as 592
            eq(gl.guides[0].bit_stream()->bits, (std::string(tag) == "2007" || std::string(tag) == "2010") ? 576 : 592, what + "97");
            eq(gl.path.class_id, 0, what + "no path");
            const ShLoftData& ph = pathed->as_sh_loft();
            eq(ph.guides.size(), static_cast<size_t>(0), what + "95 0");
            eq(ph.path.class_id, 36, what + "98");
            eq(ph.path.bit_stream()->bits, (std::string(tag) == "2007" || std::string(tag) == "2010") ? 576 : 592, what + "99");
        }
    }
}

TEST(sh_booleans_fillet_chamfer_and_brep_from_autocads_facit) {
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            if (!std::filesystem::exists(facit_path(tag, "history_brep", binary))) continue;
            Drawing d = load_facit(tag, "history_brep", binary);
            const std::string what = std::string(tag) + (binary ? " binary: " : ": ");
            assert_true(acsh_warning(d).empty(), what + "no ACSH warning on reading: " + acsh_warning(d));

            // the three Booleans of the pairs: 0 union, 2 subtract, 1 intersect, each between the nodes 1 and 2 of its graph
            std::map<int, int> operations;
            for (const Object* o : of_type(d, ObjectType::ShBoolean)) {
                const ShBooleanData& b = o->as_sh_boolean();
                const Object* history = need(history_of(d, *o), what + "the Boolean's history");
                const Object* graph = d.find_object(history->as_sh_history().graph);
                const EvaluationGraphData& g = graph->as_evaluation_graph();
                assert_true(g.find_node_by_id(b.first_node_id) && g.find_node_by_id(b.second_node_id), what + "92 and 93 are graph nodes");
                const EvalGraphNode* mine = g.find_node_by_object(o->handle);
                assert_true(mine != nullptr, what + "the Boolean's own node");
                eq(mine->id, o->as_sh_boolean().eval.node_id, what + "the node's id is its AcDbEvalExpr 90");
                // an edge from the first operand's node into the Boolean's node
                bool edge = false;
                for (const EvalGraphEdge& e : g.edges) {
                    edge = edge || (e.to == mine->index && g.nodes[static_cast<size_t>(e.from)].id == b.first_node_id);
                }
                assert_true(edge, what + "an edge from the first operand to the Boolean");
                eq(b.node.history_id, 0, what + "92 of an operation is 0");
                ++operations[static_cast<int>(b.operation)];
            }
            eq(operations[0] >= 1 ? 1 : 0, 1, what + "a union");
            eq(operations[1] >= 1 ? 1 : 0, 1, what + "an intersect");
            eq(operations[2] >= 1 ? 1 : 0, 1, what + "a subtract");

            // FILLETEDGE of three edges with radius 2
            const ShFilletData& f = only(d, ObjectType::ShFillet, what + "one fillet")->as_sh_fillet();
            eq(f.edges.size(), static_cast<size_t>(3), what + "93");
            eq(f.base_node_id, 1, what + "92");
            for (const ShFilletEdge& e : f.edges) {
                near(e.radius, 2, what + "41");
                near(e.value_42, 0, what + "42");
                near(e.value_43, 0, what + "43");
            }
            eq(f.edges[1].edge_id, f.edges[0].edge_id + 1, what + "the edges' ids follow each other");
            eq(f.edges[2].edge_id, f.edges[0].edge_id + 2, what + "the edges' ids follow each other");
            // CHAMFEREDGE of two edges, 2 and 3, and the base face after them
            const ShChamferData& c = only(d, ObjectType::ShChamfer, what + "one chamfer")->as_sh_chamfer();
            near(c.distance_1, 2, what + "41");
            near(c.distance_2, 3, what + "42");
            eq(c.edge_ids.size(), static_cast<size_t>(2), what + "93");
            eq(c.face_id, c.edge_ids[1] + 1, what + "95 follows the last edge's id");
            eq(c.base_node_id, 1, what + "92");
            // BREP: a body kept as ACIS text
            const ShBrepData& brep = only(d, ObjectType::ShBrep, what + "one BREP")->as_sh_brep();
            eq(brep.sat.version, 1, what + "70");
            assert_true(brep.sat.lines.size() > 20, what + "the SAT lines");
            eq(only(d, ObjectType::ShBrep, "")->as_sh_brep().part.version, 33, what + "AcDbShBrep 90");
        }
    }
}

TEST(sh_a_bit_stream_keeps_its_extra_byte) {
    // ACadSharp's AutoCAD 2007 sample: 672 bits and 85 bytes, not 84 (the bytes held as written, the bits as the 90 says)
    const std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "acadsharp" / "sample_AC1021_ascii.dxf";
    std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
    if (bytes.empty()) hdxf_test::skip("ACadSharp's sample_AC1021_ascii.dxf is not here");
    Drawing d;
    assert_true(assoc_test::load_bytes(d, bytes), "load");
    int found = 0;
    for (const Object* o : of_type(d, ObjectType::ShExtrusion)) {
        const ShBitStream* s = o->as_sh_extrusion().base.profile.bit_stream();
        if (!s || s->bits != 672) continue;
        ++found;
        eq(s->data.size(), static_cast<size_t>(85), "the bytes of 672 bits");
        // written from the typed value alone: the same 85 bytes
        Drawing copy = d;
        for (Object& c : copy.objects()) if (c.handle == o->handle) c.raw = RawRecord();
        std::vector<uint8_t> out = copy.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), "write: " + copy.last_error());
        eq(assoc_test::record_difference(assoc_test::ascii_record(bytes, "ACSH_EXTRUSION_CLASS", o->handle),
                                         assoc_test::ascii_record(out, "ACSH_EXTRUSION_CLASS", o->handle)), "", "the record");
        // a bit stream with fewer bytes than its bits fill is not typed (a truncated source)
        // the 90 of the profile's bits is the fourth 90 of the record
        std::vector<uint8_t> broken_bytes = set_group(bytes, "ACSH_EXTRUSION_CLASS", o->handle, 90, "6720", 3);
        Drawing broken;
        assert_true(assoc_test::load_bytes(broken, broken_bytes), "a file with a bad record still loads");
        const Object* survivor = broken.find_object(o->handle);
        assert_true(survivor && survivor->type == ObjectType::Unknown, "a truncated source stays untyped");
        assert_true(has_warning(broken, "bit stream"), "the warning names the bit stream");
        assert_true(broken.save_to_memory(DxfFormat::Ascii) == broken_bytes, "the untyped record is written as it was");
        break;
    }
    assert_true(found > 0, "no extrusion of 672 bits in the sample");
}

// ---------------------------------------------------------------------------
// New histories, in every version and format
// ---------------------------------------------------------------------------

TEST(sh_new_histories_in_every_version_and_format) {
    int generated = 0, errors = 0, not_established = 0;
    for (size_t i = 1; i < 16; ++i) {
        const ObjectType type = kTypes[i];
        const internal::ObjectCodec* codec = internal::TypedRegistry::instance().find_object(type);
        assert_true(codec != nullptr, std::string(kNames[i]) + " has a codec");
        // every type's matrix is the history's
        const internal::ObjectCodec* history = internal::TypedRegistry::instance().find_object(ObjectType::ShHistory);
        for (DxfVersion v : hdxf_test::all_versions) {
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                assert_true(codec->versions.at(v, f).generation == history->versions.at(v, f).generation &&
                                codec->versions.at(v, f).basis == history->versions.at(v, f).basis,
                            std::string(kNames[i]) + ": the matrix is the history's");
            }
        }
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
            "ACSH_HISTORY_CLASS", history->versions, [&](Drawing& d) { build_solid_history(d, type); },
            [&](const Drawing& r, DxfVersion, DxfFormat) { return check_built(r, type); });
        generated += report.generated;
        errors += report.named_errors;
        not_established += report.not_established;
        // R12 … 2004 name the version, R13 and R14 too: no AutoCAD file has a history there
        eq(report.generated, 8, std::string(kNames[i]) + ": 2007 … 2018 in two formats");
        eq(report.named_errors, 10, std::string(kNames[i]) + ": R12 … 2004 in two formats");
    }
    std::cout << "ACSH matrix: " << generated << " generated cells, " << errors << " named errors, " << not_established
              << " not established\n";
}

TEST(sh_the_matrix_says_2007_and_what_autocad_verified) {
    for (size_t i = 0; i < 16; ++i) {
        const internal::ObjectCodec* c = internal::TypedRegistry::instance().find_object(kTypes[i]);
        assert_true(c != nullptr, kNames[i]);
        for (DxfVersion v : hdxf_test::all_versions) {
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                const internal::VersionCell& cell = c->versions.at(v, f);
                const bool has = v >= DxfVersion::R2007;
                const std::string where = std::string(kNames[i]) + " " + hdxf_test::version_label(v) + " " + hdxf_test::format_label(f);
                assert_true((cell.generation != internal::Generation::None) == has, where + ": generation");
                if (!has) assert_true(cell.basis == internal::ErrorBasis::Documented, where + ": a documented error");
                if (has) assert_true(cell.autocad == internal::AutoCadStatus::Verified, where + ": AutoCAD verified");
                if (v == DxfVersion::R13 || v == DxfVersion::R14) assert_true(cell.autocad == internal::AutoCadStatus::NotSavable, where);
            }
        }
        eq(std::string(c->cls.cpp_name), std::string(i == 0 ? "AcDbShHistory" : c->cls.cpp_name), "the class");
        assert_true(std::string(c->cls.application) == "ObjectDBX Classes" && c->cls.proxy_flags == 1153, "the class entry");
        assert_false(c->cls.in_older, "not in R13/R14");
        eq(c->step, 14, "the step");
        // the owner of the history is not a handle held in the record: the record's own references
    }
}

TEST(sh_a_new_history_is_written_with_the_class_entries_once) {
    Drawing d(DxfVersion::R2018);
    build_solid_history(d, ObjectType::ShBoolean);
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!bytes.empty(), d.last_error());
    std::string text = hdxf_test::lf_text(bytes);
    for (const char* name : {"ACSH_HISTORY_CLASS", "ACSH_BOX_CLASS", "ACSH_SPHERE_CLASS", "ACSH_BOOLEAN_CLASS", "ACAD_EVALUATION_GRAPH"}) {
        size_t count = 0;
        for (size_t at = text.find("  1\n" + std::string(name) + "\n"); at != std::string::npos;
             at = text.find("  1\n" + std::string(name) + "\n", at + 1)) {
            ++count;
        }
        eq(static_cast<int>(count), 1, std::string("CLASS ") + name + " once");
    }
    {
        const std::string head = "  2\nAcDbShBoolean\n  3\nObjectDBX Classes\n 90\n";
        const size_t at = text.find(head);
        assert_true(at != std::string::npos, "the class entry as AutoCAD's");
        const size_t value = at + head.size();
        eq(std::atoi(text.substr(value, text.find('\n', value) - value).c_str()), 1153, "the class's proxy flags");
    }
    // not in the file of a drawing without a history
    Drawing none(DxfVersion::R2018);
    std::string plain = hdxf_test::lf_text(none.save_to_memory(DxfFormat::Ascii));
    assert_true(plain.find("ACSH_") == std::string::npos, "no ACSH in a drawing without a history");
}

TEST(sh_a_new_drawing_with_every_node_type_round_trips_values_and_bytes) {
    for (DxfVersion v : {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            Entity& solid = d.add_acis(EntityType::Solid3D, AcisData{});
            solid.props.handle = "A01";
            Object* h = d.add_solid_history("A01");
            const std::string history = h->handle;
            ShBoxData box;
            box.length = 3;
            Object* b = d.add_solid_history_node(history, ObjectType::ShBox, Boxed<ShBoxData>(box));
            assert_true(b != nullptr, "box");
            ShFilletData fillet;
            fillet.base_node_id = 1;
            fillet.edges = {{4, 1.5, 0, 0}};
            assert_true(d.add_solid_history_node(history, ObjectType::ShFillet, Boxed<ShFilletData>(fillet), {1}) != nullptr, "fillet");
            ShBrepData brep;
            brep.sat.lines = some_sat_lines();
            assert_true(d.add_solid_history_node(history, ObjectType::ShBrep, Boxed<ShBrepData>(brep)) != nullptr, "brep");
            ShBooleanData boolean;
            boolean.operation = ShBooleanOperation::Intersect;
            boolean.first_node_id = 2;
            boolean.second_node_id = 3;
            assert_true(d.add_solid_history_node(history, ObjectType::ShBoolean, Boxed<ShBooleanData>(boolean), {2, 3}) != nullptr, "boolean");
            const std::string where = std::string(hdxf_test::version_label(v)) + " " + hdxf_test::format_label(f) + ": ";
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), where + d.last_error());
            Drawing r;
            assert_true(assoc_test::load_bytes(r, bytes), where + "read");
            assert_true(acsh_warning(r).empty(), where + "no ACSH warning: " + acsh_warning(r));
            eq(of_type(r, ObjectType::ShHistory).size(), static_cast<size_t>(1), where + "the history");
            const Object* graph = r.find_object(of_type(r, ObjectType::ShHistory)[0]->as_sh_history().graph);
            eq(graph->as_evaluation_graph().nodes.size(), static_cast<size_t>(4), where + "four nodes");
            eq(graph->as_evaluation_graph().edges.size(), static_cast<size_t>(3), where + "the fillet's edge and the Boolean's two");
            eq(only(r, ObjectType::ShBoolean, where)->as_sh_boolean().eval.node_id, 4, where + "the Boolean is the fourth node");
            assert_true(only(r, ObjectType::ShBrep, where)->as_sh_brep().sat.lines == some_sat_lines(), where + "the SAT lines, one of 304 characters");
            // written again unchanged: the same bytes; the other format has the same values
            assert_true(r.save_to_memory(f) == bytes, where + "written again, the bytes differ");
            // emptied raw: written from the typed values, the same bytes
            forget_sh_records(r);
            std::vector<uint8_t> typed_only = r.save_to_memory(f);
            // binary: the bytes; ASCII: the lines (a double AutoCAD's 16 digits cut off prints again as the same number)
            // (the saved drawing has a new $VERSIONGUID: the lines are compared, a binary file as its ASCII text)
            const std::vector<uint8_t> text_a = f == DxfFormat::Binary ? assoc_test::as_ascii(bytes) : bytes;
            const std::vector<uint8_t> text_b = f == DxfFormat::Binary ? assoc_test::as_ascii(typed_only) : typed_only;
            assert_true(assoc_test::first_difference(text_a, text_b).empty(), where + "from the typed values alone: " + assoc_test::first_difference(text_a, text_b));
        }
    }
}

// ---------------------------------------------------------------------------
// Changes
// ---------------------------------------------------------------------------

TEST(sh_changing_a_scalar_changes_one_group) {
    if (!std::filesystem::exists(facit_path("2018", "history_primitives", false))) hdxf_test::skip("no facit");
    std::vector<uint8_t> bytes = facit_bytes("2018", "history_primitives");
    Drawing d = load_facit("2018", "history_primitives");
    const Object* box = need(find_node(d, ObjectType::ShBox, [](const Object&) { return true; }), "box");
    const std::string handle = box->handle;
    for (Object& o : d.objects()) if (o.handle == handle) o.as_sh_box().height = 33.5;
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    // only the box record differs, and in one line
    const std::string before = assoc_test::ascii_record(bytes, "ACSH_BOX_CLASS", handle);
    const std::string after = assoc_test::ascii_record(out, "ACSH_BOX_CLASS", handle);
    std::vector<std::string> a = record_lines(before), b = record_lines(after);
    eq(a.size(), b.size(), "the same number of lines");
    int differing = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            ++differing;
            assert_true(i >= 2 && a[i - 1] == " 42", "the line is the height's 42");
        }
    }
    eq(differing, 1, "one line differs");
    for (const Object& o : d.objects()) {
        if (!is_sh(o.type) || o.handle == handle) continue;
        eq(assoc_test::ascii_record(bytes, name_of(o.type), o.handle), assoc_test::ascii_record(out, name_of(o.type), o.handle),
           std::string("the untouched ") + name_of(o.type));
    }
    // the matrix by transform(): a placement set through it is what the file holds
    Drawing e = load_facit("2018", "history_primitives");
    for (Object& o : e.objects()) {
        if (o.handle != handle) continue;
        std::array<double, 16> t = o.as_sh_box().node.transform();
        t[3] = 100; t[7] = 200; t[11] = 300;
        o.as_sh_box().node.set_transform(t);
    }
    std::vector<uint8_t> moved = e.save_to_memory(DxfFormat::Ascii);
    Drawing r;
    assert_true(assoc_test::load_bytes(r, moved), "read");
    const Object* rb = r.find_object(handle);
    near(rb->as_sh_box().node.matrix[3], 100, "43");
    near(rb->as_sh_box().node.matrix[7], 200, "47");
    near(rb->as_sh_box().node.matrix[11], 300 * K, "51 is K times the z placement");
}

TEST(sh_a_list_that_changes_length_is_written_whole_with_its_counts) {
    if (!std::filesystem::exists(facit_path("2018", "history_brep", false))) hdxf_test::skip("no facit");
    std::vector<uint8_t> bytes = facit_bytes("2018", "history_brep");
    Drawing d = load_facit("2018", "history_brep");
    // the fillet loses an edge: all four counts follow
    const Object* fillet = only(d, ObjectType::ShFillet, "fillet");
    const std::string fh = fillet->handle;
    const Object* chamfer = only(d, ObjectType::ShChamfer, "chamfer");
    const std::string ch = chamfer->handle;
    const Object* guided = find_node(d, ObjectType::ShLoft, [](const Object& o) { return !o.as_sh_loft().guides.empty(); });
    const std::string gh = need(guided, "guided loft")->handle;
    for (Object& o : d.objects()) {
        if (o.handle == fh) o.as_sh_fillet().edges.pop_back();
        if (o.handle == ch) {
            o.as_sh_chamfer().edge_ids.push_back(77);
            o.as_sh_chamfer().edge_ids.push_back(78);
        }
        if (o.handle == gh) o.as_sh_loft().guides.pop_back();
    }
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    auto count_of = [&](const std::string& handle, const std::string& type, int code) {
        std::vector<std::pair<int, std::string>> g = groups_of(assoc_test::ascii_record(out, type, handle));
        for (const auto& [c, v] : g) if (c == code) return std::atoi(v.c_str());
        return -1;
    };
    eq(count_of(fh, "ACSH_FILLET_CLASS", 93), 2, "93");
    eq(count_of(fh, "ACSH_FILLET_CLASS", 95), 2, "95");
    eq(count_of(fh, "ACSH_FILLET_CLASS", 96), 2, "96");
    eq(count_of(fh, "ACSH_FILLET_CLASS", 97), 2, "97");
    eq(count_of(ch, "ACSH_CHAMFER_CLASS", 93), 4, "chamfer 93");
    eq(count_of(gh, "ACSH_LOFT_CLASS", 95), 1, "loft 95");
    Drawing r;
    assert_true(assoc_test::load_bytes(r, out), "read");
    assert_true(acsh_warning(r).empty(), "no ACSH warning: " + acsh_warning(r));
    eq(only(r, ObjectType::ShFillet, "fillet")->as_sh_fillet().edges.size(), static_cast<size_t>(2), "two edges");
    eq(only(r, ObjectType::ShChamfer, "chamfer")->as_sh_chamfer().edge_ids.size(), static_cast<size_t>(4), "four edges");
    eq(r.find_object(gh)->as_sh_loft().guides.size(), static_cast<size_t>(1), "one guide");
    // the untouched records are the file's
    for (const Object& o : d.objects()) {
        if (!is_sh(o.type) || o.handle == fh || o.handle == ch || o.handle == gh) continue;
        eq(assoc_test::ascii_record(bytes, name_of(o.type), o.handle), assoc_test::ascii_record(out, name_of(o.type), o.handle),
           std::string("the untouched ") + name_of(o.type));
    }
    // a source that changes form: the extrusion's profile as SAT lines instead of a bit stream moves the groups of its subclass
    Drawing e = load_facit("2018", "history_brep");
    std::string eh;
    for (const Object* o : of_type(e, ObjectType::ShExtrusion)) if (o->as_sh_extrusion().base.profile.bit_stream()) eh = o->handle;
    assert_true(!eh.empty(), "an extrusion with a bit stream");
    for (Object& o : e.objects()) {
        if (o.handle == eh) o.as_sh_extrusion().base.profile = sat_source(37, some_sat_lines());
    }
    std::vector<uint8_t> changed = e.save_to_memory(DxfFormat::Ascii);
    Drawing r2;
    assert_true(assoc_test::load_bytes(r2, changed), "read the changed file");
    const ShSourceGeometry& profile = r2.find_object(eh)->as_sh_extrusion().base.profile;
    assert_true(profile.sat() != nullptr && profile.sat()->lines == some_sat_lines() && profile.class_id == 37, "the profile as SAT lines");
    assert_true(acsh_warning(r2).empty(), "no ACSH warning: " + acsh_warning(r2));
}

TEST(sh_changing_the_history_changes_neither_the_solids_acis_nor_the_extents) {
    if (!std::filesystem::exists(facit_path("2010", "history_brep", false))) hdxf_test::skip("no facit");
    // 2010 holds the SAT lines in the 3DSOLID: compare them and the header's extents before and after a history change
    std::vector<uint8_t> bytes = facit_bytes("2010", "history_brep");
    Drawing d = load_facit("2010", "history_brep");
    auto extents_of = [](const std::vector<uint8_t>& b) {
        std::string t = hdxf_test::lf_text(b);
        std::string out;
        for (const char* var : {"$EXTMIN", "$EXTMAX", "$PEXTMIN", "$PEXTMAX"}) {
            size_t at = t.find(std::string("  9\n") + var + "\n");
            assert_true(at != std::string::npos, std::string(var) + " in the header");
            out += t.substr(at, t.find("  9\n", at + 4) - at);
        }
        return out;
    };
    std::vector<AcisData> acis_before;
    for (const Entity& e : d.entities()) if (e.type == EntityType::Solid3D) acis_before.push_back(e.as_acis());
    assert_true(!acis_before.empty(), "solids");
    const std::string extents_before = extents_of(d.save_to_memory(DxfFormat::Ascii));
    for (Object& o : d.objects()) {
        if (o.type == ObjectType::ShBox) {
            o.as_sh_box().length *= 10;
            o.as_sh_box().node.matrix[3] += 5000;
        }
        if (o.type == ObjectType::ShSphere) o.as_sh_sphere().radius += 100;
    }
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    eq(extents_of(out), extents_before, "the header's extents");
    size_t k = 0;
    for (const Entity& e : d.entities()) {
        if (e.type != EntityType::Solid3D) continue;
        assert_true(e.as_acis().lines == acis_before[k].lines && e.as_acis().version == acis_before[k].version, "the solid's SAT lines");
        ++k;
    }
    // and the solids' own records are the file's
    Drawing r;
    assert_true(assoc_test::load_bytes(r, out), "read");
    for (const Entity& e : d.entities()) {
        if (e.type != EntityType::Solid3D) continue;
        eq(assoc_test::ascii_record(bytes, "3DSOLID", e.props.handle), assoc_test::ascii_record(out, "3DSOLID", e.props.handle),
           "the 3DSOLID record " + e.props.handle);
    }
    // a history built on a solid adds no extents: a drawing with a solid, built with and without history
    Drawing with(DxfVersion::R2018), without(DxfVersion::R2018);
    for (Drawing* x : {&with, &without}) {
        Entity& s = x->add_acis(EntityType::Solid3D, AcisData{});
        s.props.handle = "A01";
    }
    ShBoxData big;
    big.length = 1000; big.width = 1000; big.height = 1000;
    with.add_solid_history("A01");
    with.add_solid_history_node(with.solid_history("A01")->handle, ObjectType::ShBox, Boxed<ShBoxData>(big));
    eq(extents_of(with.save_to_memory(DxfFormat::Ascii)), extents_of(without.save_to_memory(DxfFormat::Ascii)), "extents of a history");
}

// ---------------------------------------------------------------------------
// Relations (B19)
// ---------------------------------------------------------------------------

TEST(sh_removing_the_solid_takes_its_history_graph_and_nodes_with_it) {
    if (!std::filesystem::exists(facit_path("2018", "history_boolean_applied", false))) hdxf_test::skip("no facit");
    std::vector<uint8_t> bytes = facit_bytes("2018", "history_boolean_applied");
    Drawing d = load_facit("2018", "history_boolean_applied");
    // the solid of a Boolean: its history has three nodes
    const Object* boolean = need(find_node(d, ObjectType::ShBoolean, [](const Object&) { return true; }), "a Boolean");
    const Object* history = need(history_of(d, *boolean), "history");
    const std::string solid_handle = history->owner;
    std::vector<std::string> doomed = {history->handle, history->as_sh_history().graph};
    const Object* graph = d.find_object(history->as_sh_history().graph);
    for (const EvalGraphNode& n : graph->as_evaluation_graph().nodes) doomed.push_back(n.object);
    eq(doomed.size(), static_cast<size_t>(5), "history, graph and three nodes");
    const size_t before = of_type(d, ObjectType::ShHistory).size();
    auto& entities = d.entities();
    auto it = std::find_if(entities.begin(), entities.end(), [&](const Entity& e) { return upper(e.props.handle) == upper(solid_handle); });
    assert_true(it != entities.end(), "the solid");
    entities.erase(it);
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    Drawing r;
    assert_true(assoc_test::load_bytes(r, out), "read");
    for (const std::string& h : doomed) assert_true(r.find_object(h) == nullptr, "the record " + h + " went with the solid");
    eq(of_type(r, ObjectType::ShHistory).size(), before - 1, "the other histories stay");
    // every other history is the file's
    for (const Object& o : d.objects()) {
        if (!is_sh(o.type) || std::find(doomed.begin(), doomed.end(), o.handle) != doomed.end()) continue;
        eq(assoc_test::ascii_record(bytes, name_of(o.type), o.handle), assoc_test::ascii_record(out, name_of(o.type), o.handle),
           std::string("the other ") + name_of(o.type));
    }
}

TEST(sh_removing_a_material_a_graph_or_a_node_follows_b19) {
    if (!std::filesystem::exists(facit_path("2018", "history_boolean_applied", false))) hdxf_test::skip("no facit");
    std::vector<uint8_t> bytes = facit_bytes("2018", "history_boolean_applied");
    // the material: every node of the file shares the same one, and it goes: 347 0 in every node, the nodes stay
    {
        Drawing d = load_facit("2018", "history_boolean_applied");
        const Object* box = need(find_node(d, ObjectType::ShBox, [](const Object&) { return true; }), "box");
        const std::string material = box->as_sh_box().node.material;
        size_t nodes = 0, sharing = 0;
        for (const Object& o : d.objects()) {
            if (!is_node(o.type)) continue;
            ++nodes;
            std::string m;
            switch (o.type) {
                case ObjectType::ShBox: m = o.as_sh_box().node.material; break;
                case ObjectType::ShBoolean: m = o.as_sh_boolean().node.material; break;
                case ObjectType::ShSphere: m = o.as_sh_sphere().node.material; break;
                default: m = material; break;
            }
            if (upper(m) == upper(material)) ++sharing;
        }
        assert_true(nodes > 10 && sharing == nodes, "the nodes share one material (AutoCAD's Global)");
        auto& objects = d.objects();
        auto it = std::find_if(objects.begin(), objects.end(), [&](const Object& o) { return upper(o.handle) == upper(material); });
        assert_true(it != objects.end() && it->type == ObjectType::Material, "the material is an object of the file");
        objects.erase(it);
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        Drawing r;
        assert_true(assoc_test::load_bytes(r, out), "read");
        size_t zero = 0;
        for (const Object& o : r.objects()) {
            if (o.type == ObjectType::ShBox) {
                assert_true(is_zero_handle(o.as_sh_box().node.material), "347 is 0 where the material is gone");
                ++zero;
            }
        }
        assert_true(zero > 0, "boxes");
    }
    // the graph: the history's 360 becomes 0, the nodes (owned by the graph) go with it
    {
        Drawing d = load_facit("2018", "history_boolean_applied");
        const Object* boolean = need(find_node(d, ObjectType::ShBoolean, [](const Object&) { return true; }), "boolean");
        const Object* history = need(history_of(d, *boolean), "history");
        const std::string history_handle = history->handle;
        const std::string graph_handle = history->as_sh_history().graph;
        std::vector<std::string> nodes;
        for (const EvalGraphNode& n : d.find_object(graph_handle)->as_evaluation_graph().nodes) nodes.push_back(n.object);
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                                     [&](const Object& o) { return upper(o.handle) == upper(graph_handle); }),
                      objects.end());
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        Drawing r;
        assert_true(assoc_test::load_bytes(r, out), "read");
        const Object* h = r.find_object(history_handle);
        assert_true(h && h->type == ObjectType::ShHistory, "the history stays");
        assert_true(is_zero_handle(h->as_sh_history().graph), "360 is 0 where the graph is gone");
        for (const std::string& n : nodes) assert_true(r.find_object(n) == nullptr, "node " + n + " went with its owner");
    }
    // the history itself: the solid's 350 becomes 0, the graph and the nodes (owned by it, in turn) go
    {
        Drawing d = load_facit("2018", "history_boolean_applied");
        const Object* boolean = need(find_node(d, ObjectType::ShBoolean, [](const Object&) { return true; }), "boolean");
        const Object* history = need(history_of(d, *boolean), "history");
        const std::string history_handle = history->handle;
        const std::string solid_handle = history->owner;
        const std::string graph_handle = history->as_sh_history().graph;
        const std::string node_handle = boolean->handle;
        auto& objects = d.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                                     [&](const Object& o) { return upper(o.handle) == upper(history_handle); }),
                      objects.end());
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        Drawing r;
        assert_true(assoc_test::load_bytes(r, out), "read");
        const Entity* solid = r.find_entity(solid_handle);
        assert_true(solid && is_zero_handle(solid->as_acis().history_handle), "the solid's 350 is 0 where its history is gone");
        assert_true(r.find_object(graph_handle) == nullptr && r.find_object(node_handle) == nullptr, "the graph and the nodes went");
    }
    // a node taken out of a history through the API: its object and its graph node and edges go, the others' ids stay
    {
        Drawing d(DxfVersion::R2018);
        build_solid_history(d, ObjectType::ShBoolean);
        const Object* history = of_type(d, ObjectType::ShHistory)[0];
        const std::string hh = history->handle;
        const Object* sphere = d.solid_history_node(hh, 2);
        assert_true(sphere && sphere->type == ObjectType::ShSphere, "node 2 is the sphere");
        assert_true(d.remove_solid_history_node(sphere->handle), "remove");
        const Object* graph = d.find_object(d.find_object(hh)->as_sh_history().graph);
        eq(graph->as_evaluation_graph().nodes.size(), static_cast<size_t>(2), "two nodes left");
        eq(graph->as_evaluation_graph().edges.size(), static_cast<size_t>(1), "the sphere's edge went");
        assert_true(d.solid_history_node(hh, 2) == nullptr, "no node 2");
        const Object* boolean = d.solid_history_node(hh, 3);
        assert_true(boolean && boolean->as_sh_boolean().second_node_id == 2, "the link to the gone node is kept (a link that leads nowhere)");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        // and the whole history
        assert_true(d.remove_solid_history("A01"), "remove the history");
        assert_true(of_type(d, ObjectType::ShHistory).empty() && of_type(d, ObjectType::ShBox).empty() &&
                        of_type(d, ObjectType::EvaluationGraph).empty(), "no history, node or graph left");
        assert_true(d.find_entity("A01")->as_acis().history_handle.empty(), "the solid's 350");
        assert_false(d.remove_solid_history("A01"), "a solid without a history");
        std::string text = hdxf_test::lf_text(d.save_to_memory(DxfFormat::Ascii));
        assert_true(text.find("ACSH_") == std::string::npos, "no ACSH in the file");
    }
}

TEST(sh_a_link_that_was_broken_in_the_file_is_kept) {
    if (!std::filesystem::exists(facit_path("2018", "history_primitives", false))) hdxf_test::skip("no facit");
    std::vector<uint8_t> bytes = facit_bytes("2018", "history_primitives");
    Drawing probe;
    assert_true(assoc_test::load_bytes(probe, bytes), "load");
    const Object* box = need(find_node(probe, ObjectType::ShBox, [](const Object&) { return true; }), "box");
    const std::string handle = box->handle;
    const std::string material = box->as_sh_box().node.material;
    // a 347 that leads nowhere in the file itself
    std::vector<uint8_t> broken = edited(bytes, "ACSH_BOX_CLASS", handle, {{group_text(347, material), group_text(347, "FFFFE")}});
    Drawing d;
    assert_true(assoc_test::load_bytes(d, broken), "load the file with the broken link");
    assert_true(d.save_to_memory(DxfFormat::Ascii) == broken, "written unchanged: the file's bytes");
    for (Object& o : d.objects()) if (o.handle == handle) o.as_sh_box().length = 41;
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    const std::string text = assoc_test::ascii_record(out, "ACSH_BOX_CLASS", handle);
    assert_true(text.find("347\nFFFFE\n") != std::string::npos, "the broken 347 is still FFFFE after the record was written anew");
}

// ---------------------------------------------------------------------------
// Faulty and future records (B105)
// ---------------------------------------------------------------------------

TEST(sh_faulty_records_stay_untyped_and_are_kept_with_a_warning) {
    if (!std::filesystem::exists(facit_path("2018", "history_brep", false))) hdxf_test::skip("no facit");
    std::vector<uint8_t> bytes = facit_bytes("2018", "history_brep");
    Drawing probe;
    assert_true(assoc_test::load_bytes(probe, bytes), "load");
    auto handle_of = [&](ObjectType t, const std::function<bool(const Object&)>& pick = [](const Object&) { return true; }) {
        return need(find_node(probe, t, pick), name_of(t))->handle;
    };
    auto untyped_with_warning = [&](const std::vector<uint8_t>& file, const std::string& handle, const std::string& type,
                                    const std::string& needle, const std::string& what) {
        Drawing d;
        assert_true(assoc_test::load_bytes(d, file), what + ": a file with a faulty record still loads");
        const Object* o = d.find_object(handle);
        assert_true(o && o->type == ObjectType::Unknown, what + ": the record stays untyped");
        assert_true(has_warning(d, needle), what + ": the warning names \"" + needle + "\" (" +
                                                (d.warnings().empty() ? std::string("none") : d.warnings()[0]) + ")");
        assert_true(d.save_to_memory(DxfFormat::Ascii) == file, what + ": the untyped record is written as it was");
        (void)type;
    };
    // a Boolean operation AutoCAD does not have
    {
        const std::string h = handle_of(ObjectType::ShBoolean);
        std::vector<uint8_t> g = set_group(bytes, "ACSH_BOOLEAN_CLASS", h, 280, "7");
        untyped_with_warning(g, h, "ACSH_BOOLEAN_CLASS", "ACSH_BOOLEAN_CLASS", "Boolean operation 7");
    }
    // a bit stream with fewer bytes than its bits (a truncated source), a count that cannot be there, a missing group
    {
        const std::string h = handle_of(ObjectType::ShRevolve);
        // the 90 of the profile's bits is the fourth 90: AcDbEvalExpr, AcDbShHistoryNode, AcDbShRevolve, then the bits
        std::vector<uint8_t> f = set_group(bytes, "ACSH_REVOLVE_CLASS", h, 90, "5440", 3);
        untyped_with_warning(f, h, "ACSH_REVOLVE_CLASS", "bit stream", "truncated revolve profile");
    }
    {
        const std::string h = handle_of(ObjectType::ShLoft, [](const Object& o) { return o.as_sh_loft().sections.size() == 3; });
        // the loft's count is the second 92 (the first is the node's)
        std::vector<uint8_t> f = set_group(bytes, "ACSH_LOFT_CLASS", h, 92, "300000", 1);
        untyped_with_warning(f, h, "ACSH_LOFT_CLASS", "ACSH_LOFT_CLASS", "a loft with an impossible section count");
    }
    {
        const std::string h = handle_of(ObjectType::ShFillet);
                std::vector<uint8_t> f = set_group(bytes, "ACSH_FILLET_CLASS", h, 95, "2");
        untyped_with_warning(f, h, "ACSH_FILLET_CLASS", "ACSH_FILLET_CLASS", "a fillet whose lists have different counts");
    }
    {
        // a real that is not a number
        const std::string h = handle_of(ObjectType::ShBox);
        // the box's length is the second 40 (the first is the node matrix's)
        std::vector<uint8_t> f = set_group(bytes, "ACSH_BOX_CLASS", h, 40, "abc", 1);
        untyped_with_warning(f, h, "ACSH_BOX_CLASS", "ACSH_BOX_CLASS", "a length that is not a number");
    }
    // a later version of a record: groups after the ones HDxf knows are kept as they are, with a warning
    {
        const std::string h = handle_of(ObjectType::ShBox);
        // a new group 43 after the box's 42
        std::vector<uint8_t> f = append_group(bytes, "ACSH_BOX_CLASS", h, 43, "99.0");
        Drawing d;
        assert_true(assoc_test::load_bytes(d, f), "load");
        const Object* o = d.find_object(h);
        assert_true(o && o->type == ObjectType::ShBox, "a record with a later group is typed");
        assert_true(has_warning(d, "after the ones HDxf knows"), "with a warning");
        assert_true(d.save_to_memory(DxfFormat::Ascii) == f, "written unchanged: the file's bytes, the later group too");
    }
}

TEST(sh_a_history_in_a_drawing_before_2007_is_the_named_error_before_any_mutation) {
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R13, DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2004}) {
        Drawing d(v);
        build_solid_history(d, ObjectType::ShFillet);
        const size_t objects = d.objects().size();
        std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
        assert_true(bytes.empty(), std::string(hdxf_test::version_label(v)) + ": a history saved before 2007");
        eq(d.last_error(), "ACSH_HISTORY_CLASS requires DXF 2007 or later", "the named error");
        eq(d.objects().size(), objects, "nothing changed");
    }
}

TEST(sh_apis_return_null_for_what_is_not_there) {
    Drawing d(DxfVersion::R2018);
    assert_true(d.add_solid_history("A01") == nullptr, "no such solid");
    d.add_acis(EntityType::Solid3D, AcisData{}).props.handle = "A01";
    Entity& line = d.add_line({0, 0, 0}, {1, 1, 0});
    line.props.handle = "A02";
    assert_true(d.add_solid_history("A02") == nullptr, "not a solid");
    assert_true(d.solid_history("A01") == nullptr, "no history yet");
    Object* h = d.add_solid_history("A01");
    assert_true(h != nullptr, "history");
    assert_true(d.add_solid_history("A01") == h || d.add_solid_history("A01")->handle == h->handle, "the history it has is returned");
    assert_true(d.add_solid_history_node(h->handle, ObjectType::Dictionary, DictionaryData{}) == nullptr, "not a node type");
    assert_true(d.add_solid_history_node("FFF", ObjectType::ShBox, Boxed<ShBoxData>(ShBoxData{})) == nullptr, "no such history");
    assert_true(d.add_solid_history_node(h->handle, ObjectType::ShBox, Boxed<ShSphereData>(ShSphereData{})) == nullptr, "data of another type");
    assert_true(d.solid_history_node(h->handle, 1) == nullptr, "no node yet");
    assert_false(d.remove_solid_history_node("FFF"), "no such node");
    // a node's id is the next one and ids are not reused
    Object* a = d.add_solid_history_node(h->handle, ObjectType::ShBox, Boxed<ShBoxData>(ShBoxData{}));
    Object* b = d.add_solid_history_node(h->handle, ObjectType::ShSphere, Boxed<ShSphereData>(ShSphereData{}));
    assert_true(a && b, "nodes");
    const std::string bh = b->handle;
    eq(d.find_object(a->handle)->as_sh_box().eval.node_id, 1, "first id");
    eq(d.find_object(bh)->as_sh_sphere().eval.node_id, 2, "second id");
    eq(d.find_object(a->handle)->as_sh_box().node.history_id, 1, "92 of the first primitive");
    eq(d.find_object(bh)->as_sh_sphere().node.history_id, 2, "92 of the second");
    assert_true(d.remove_solid_history_node(a->handle), "remove the first");
    Object* c = d.add_solid_history_node(h->handle, ObjectType::ShTorus, Boxed<ShTorusData>(ShTorusData{}));
    eq(d.find_object(c->handle)->as_sh_torus().eval.node_id, 3, "an id is not reused");
    eq(d.find_object(h->handle)->as_sh_history().last_node_id, 3, "92 of the history");
}

TEST(sh_record_sizes_stay_within_the_budget) {
    static_assert(sizeof(Boxed<ShBoxData>) == sizeof(void*), "boxed");
    assert_true(sizeof(Object) <= 1000, "sizeof(Object) is " + std::to_string(sizeof(Object)));
}

// A solid erased in a drawing built in memory takes its history with it, as AutoCAD's ERASE does: the history is the
// solid's hard-owned object, and the graph and its nodes are the history's (9.S)
TEST(sh_an_erased_solid_of_a_drawing_built_in_memory_takes_its_history) {
    for (DxfVersion v : {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        Drawing d(v);
        build_solid_history(d, ObjectType::ShBox);
        auto& entities = d.entities();
        entities.erase(std::remove_if(entities.begin(), entities.end(), [](const Entity& e) { return e.props.handle == "A01"; }),
                       entities.end());
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), d.last_error());
            Drawing back;
            assert_true(back.load_from_memory(bytes.data(), bytes.size()), back.last_error());
            for (const Object& o : back.objects()) {
                assert_true(o.type != ObjectType::ShHistory && o.type != ObjectType::EvaluationGraph && o.type != ObjectType::ShBox,
                            "a record of the erased solid's history is written: " + o.handle);
            }
        }
    }
}
