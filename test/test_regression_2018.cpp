#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The regression matrix of DXF 2018 (phase 6, step 6.9): AutoCAD's 2018 files with every typed
// record written anew in 2018 are AutoCAD's records (ASCII and binary), the record graph is the
// same, a change touches only the record changed, and a drawing with all of 2018's additions is
// stable (written, read and written again byte for byte).
// ---------------------------------------------------------------------------

namespace {

using Record = std::vector<std::pair<int, std::string>>;

std::vector<Record> records_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Record> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0 || out.empty()) out.emplace_back();
        out.back().push_back({gc.code, dxf_structure::number_text(gc.code, gc.value)});
    }
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

bool has_files(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (!std::filesystem::exists(external(n))) {
            hdxf_test::skip("external test file missing: " + n);
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

Record record(const std::vector<Record>& records, const std::string& handle) {
    for (const auto& r : records) {
        for (const auto& [code, value] : r) {
            if ((code == 5 || code == 105) && value == handle) return r;
            if (code == 100) break;
        }
    }
    return {};
}

std::string value(const Record& r, int code) {
    for (const auto& [c, v] : r) if (c == code) return v;
    return {};
}

void drop_entities(std::vector<hdxf::Entity>& list) {
    for (auto& e : list) {
        if (e.type == hdxf::EntityType::Unknown) continue;
        e.raw = hdxf::RawRecord();
        if (e.type == hdxf::EntityType::Insert) {
            for (auto& a : e.as_insert().attributes) a.raw = hdxf::RawRecord();
            e.as_insert().seqend_raw = hdxf::RawRecord();
        }
        if (e.type == hdxf::EntityType::Polyline) {
            for (auto& v : e.as_polyline().vertices) v.raw = hdxf::RawRecord();
            e.as_polyline().seqend_raw = hdxf::RawRecord();
        }
    }
}

// Every record HDxf types written anew
void drop_records(hdxf::Drawing& dwg) {
    drop_entities(dwg.entities());
    for (auto& b : dwg.blocks()) {
        drop_entities(b.entities);
        b.raw = hdxf::RawRecord();
        b.endblk_raw = hdxf::RawRecord();
        b.record.raw = hdxf::RawRecord();
    }
    for (auto& x : dwg.layers()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.linetypes()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.text_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.dim_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.views()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.ucs_list()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.vports()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.app_ids()) x.raw = hdxf::RawRecord();
    for (auto& o : dwg.objects()) {
        if (o.type != hdxf::ObjectType::Unknown) o.raw = hdxf::RawRecord();
    }
}

bool same_record(const Record& a, const Record& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first) return false;
        if (a[i].second == b[i].second) continue;
        char* end_a = nullptr;
        char* end_b = nullptr;
        double x = std::strtod(a[i].second.c_str(), &end_a), y = std::strtod(b[i].second.c_str(), &end_b);
        if (*end_a != '\0' || *end_b != '\0' || a[i].second.empty()) return false;
        if (std::fabs(x - y) > 1e-12 * std::max(1.0, std::fabs(x))) return false;
    }
    return true;
}

const char* const kFiles[] = {"acadsharp/sample_AC1032_ascii.dxf", "acadsharp/sample_AC1032_binary.dxf"};

// The records of a drawing by (type, handle)
std::set<std::string> handles_of(const std::vector<Record>& recs) {
    std::set<std::string> out;
    for (const auto& r : recs) {
        std::string h = value(r, r[0].second == "DIMSTYLE" ? 105 : 5);
        if (!h.empty() && r[0].second != "SECTION") out.insert(r[0].second + " " + h);
    }
    return out;
}

} // namespace

// AutoCAD's 2018 files with every typed record written anew: the records are AutoCAD's, but the
// proxy graphics AutoCAD computes itself and what the other tests of the phase hold apart
// (MTEXT with an embedded object and multiline attributes: test_mtext_2018, test_attrib_2018)
TEST(regression_2018_every_record_written_anew_as_autocad) {
    for (const char* name : kFiles) {
        if (!has_files({name})) return;
        bool binary = std::string(name).find("binary") != std::string::npos;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        drop_records(dwg);
        std::vector<uint8_t> out = save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        std::vector<Record> theirs = records_of(in), ours = records_of(out);
        int checked = 0;
        std::string failures;
        for (const auto& t : theirs) {
            const std::string& type = t[0].second;
            if (type == "SECTION" || type == "MULTILEADER" || type == "IMAGE" || type == "WIPEOUT" || type == "MESH" ||
                type == "HELIX" || type == "LAYOUT" || type == "MTEXT" || type == "ATTRIB" || type == "ATTDEF" ||
                type == "INSERT" || type == "DIMENSION") {
                continue;
            }
            std::string handle = value(t, type == "DIMSTYLE" ? 105 : 5);
            if (handle.empty()) continue;
            ++checked;
            if (!same_record(t, record(ours, handle)) && failures.size() < 400) failures += " " + type + ":" + handle;
        }
        hdxf_test::assert_true(failures.empty(), std::string(name) + ": differs:" + failures);
        hdxf_test::assert_true(checked > 20, std::string(name) + ": records compared");
    }
}

// The record graph survives a rewrite: every record (type and handle) of AutoCAD's file is in ours —
// ACAD_TABLE with its anonymous block, style and extension dictionaries included — and the file is
// stable (read and written again: the same bytes)
TEST(regression_2018_record_graph_and_stability) {
    for (const char* name : kFiles) {
        if (!has_files({name})) return;
        bool binary = std::string(name).find("binary") != std::string::npos;
        hdxf::DxfFormat format = binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii;
        std::vector<uint8_t> in = read_bytes(external(name));
        hdxf::Drawing dwg = load(in);
        drop_records(dwg);
        std::vector<uint8_t> out = save(dwg, format);
        std::set<std::string> theirs = handles_of(records_of(in)), ours = handles_of(records_of(out));
        std::string missing;
        for (const auto& h : theirs) if (!ours.count(h) && missing.size() < 300) missing += " " + h;
        hdxf_test::assert_true(missing.empty(), std::string(name) + ": records lost:" + missing);
        hdxf::Drawing again = load(out);
        hdxf_test::assert_true(again.save_to_memory(format) == out, std::string(name) + ": stable");
    }
}

// A change touches only the record changed: every entity kind in turn is moved in AutoCAD's 2018 file
// and exactly its record differs from the file
TEST(regression_2018_a_change_touches_one_record) {
    if (!has_files({kFiles[0]})) return;
    std::vector<uint8_t> in = read_bytes(external(kFiles[0]));
    hdxf::Drawing probe = load(in);
    std::set<hdxf::EntityType> kinds;
    std::vector<size_t> picks;
    for (size_t i = 0; i < probe.entities().size(); ++i) {
        // a DIMENSION's block *Dn follows the dimension (it is rebuilt with it), so its layer is not "one record"
        if (probe.entities()[i].type == hdxf::EntityType::Dimension) continue;
        // so does a table's block *Tn and its cells' geometry: AutoCAD draws the block on the table's layer (doc/facit/fas9_table_layout.scr,
        // CHPROP of the layer), so the table is laid out again with it (step 9.5)
        if (probe.entities()[i].type == hdxf::EntityType::Table) continue;
        if (kinds.insert(probe.entities()[i].type).second) picks.push_back(i);
    }
    hdxf_test::assert_true(picks.size() > 5, "several kinds of entity");
    std::vector<Record> before = records_of(in);
    for (size_t pick : picks) {
        hdxf::Drawing dwg = load(in);
        hdxf::Entity& e = dwg.entities()[pick];
        std::string handle = e.props.handle;
        if (handle.empty()) continue;
        e.props.layer = "Regression";
        dwg.add_layer("Regression");
        std::vector<uint8_t> out = save(dwg);
        std::vector<Record> after = records_of(out);
        hdxf_test::assert_true(!after.empty(), "written");
        Record was = record(before, handle), is = record(after, handle);
        hdxf_test::assert_false(is.empty(), "the record is there: " + handle);
        hdxf_test::assert_eq(value(is, 8), "Regression", "layer changed: " + handle);
        // every other entity record is as it was
        int different = 0;
        std::string which;
        for (const auto& r : before) {
            if (r[0].second == "SECTION") continue;
            std::string h = value(r, r[0].second == "DIMSTYLE" ? 105 : 5);
            if (h.empty() || h == handle) continue;
            const std::string& type = r[0].second;
            if (type == "VERTEX" || type == "SEQEND" || type == "ATTRIB") continue;   // follow their parent's layer
            bool entity_kind = type != "LAYER" && type != "TABLE" && type != "DICTIONARY" && type != "BLOCK_RECORD" &&
                               type != "VPORT" && type != "ENDBLK" && type != "BLOCK" && type != "CLASS" && type != "APPID" &&
                               type != "LTYPE" && type != "STYLE" && type != "VIEW" && type != "UCS" && type != "DIMSTYLE" &&
                               type != "ENDTAB" && type != "LAYOUT";
            if (!entity_kind) continue;
            if (record(after, h) != r) { ++different; which += " " + type + ":" + h; }
        }
        hdxf_test::assert_eq(different, 0, "no other entity record changed after moving " + handle + ":" + which);
    }
}

// Everything 2018 adds in one drawing, written, read and written again: the same bytes in ASCII and
// binary, a structure that checks, and the types read back
TEST(regression_2018_all_additions_in_one_drawing) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
        dwg.add_line({0, 0, 0}, {10, 0, 0});
        dwg.add_circle({5, 5, 0}, 2);
        dwg.add_text({1, 1, 0}, "text");
        hdxf::Entity& mt = dwg.add_mtext({20, 20, 0}, "Column text that goes on and on", 2.5);
        mt.as_mtext().reference_width = 30.0;
        dwg.add_coordination_model("D:\\bim\\site.nwd", {-5, -5, 0}, {50, 40, 12});
        hdxf::RapidRtRenderSettingsData p;
        p.name = "Regression";
        dwg.add_rapidrt_render_settings(p);
        hdxf::RapidRtRenderEnvironmentData env;
        env.enabled = true;
        env.ibl_file = "sky.hdr";
        dwg.set_rapidrt_render_environment(env);
        std::vector<uint8_t> data = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(data, "all 2018 additions");
        hdxf::Drawing back = load(data);
        hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R2018, "version");
        int models = 0;
        for (const auto& e : back.entities()) models += e.type == hdxf::EntityType::CoordinationModel;
        hdxf_test::assert_eq(models, 1, "the model");
        int presets = 0, envs = 0, defs = 0;
        for (const auto& o : back.objects()) {
            presets += o.type == hdxf::ObjectType::RapidRtRenderSettings;
            envs += o.type == hdxf::ObjectType::RapidRtRenderEnvironment;
            defs += o.type == hdxf::ObjectType::NavisworksModelDef;
        }
        hdxf_test::assert_eq(presets, 1, "the preset");
        hdxf_test::assert_eq(envs, 1, "the environment");
        hdxf_test::assert_eq(defs, 1, "the definition");
        hdxf_test::assert_true(back.save_to_memory(format) == data, "stable");
    }
}

// The model's extents count in the drawing's: $EXTMAX covers the transformed definition bounds
TEST(regression_2018_model_in_the_drawing_extents) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    dwg.add_coordination_model("D:\\bim\\site.nwd", {0, 0, 0}, {10, 20, 30},
                               {1, 0, 0, 100, 0, 1, 0, 200, 0, 0, 1, 300, 0, 0, 0, 1});
    std::vector<uint8_t> data = save(dwg);
    hdxf::Drawing back = load(data);
    const hdxf::HeaderVar* max = back.find_header("$EXTMAX");
    hdxf_test::assert_true(max != nullptr && max->values.size() >= 3, "$EXTMAX");
    hdxf_test::assert_eq(std::atof(max->values[0].second.c_str()), 110.0, "x");
    hdxf_test::assert_eq(std::atof(max->values[1].second.c_str()), 220.0, "y");
    hdxf_test::assert_eq(std::atof(max->values[2].second.c_str()), 330.0, "z");
}

namespace {

// A drawing with the types the earlier phases wrote, built in memory for `version`: the matrix
// of step 6.9 (what 2018 writes anew is what 2013 writes, with 2018's forms)
hdxf::Drawing matrix_drawing(hdxf::DxfVersion version) {
    hdxf::Drawing dwg(version);
    dwg.add_layer("Matrix");
    dwg.add_line({0, 0, 0}, {10, 0, 0}, "Matrix");
    dwg.add_circle({5, 5, 0}, 2);
    dwg.add_arc({0, 0, 0}, 4, 0, 90);
    dwg.add_text({1, 1, 0}, "Svenska \xC3\xA5\xC3\xA4\xC3\xB6 \xF0\x9F\x98\x80", 2.0);
    dwg.add_polyline({{0, 0}, {5, 0}, {5, 5}}, true);
    dwg.add_dimension(hdxf::DimensionType::Rotated, {50, 10, 0}, {25, 12, 0});
    hdxf::Block& block = dwg.add_block("Nested", {0, 0, 0});
    hdxf::Entity inner;
    inner.type = hdxf::EntityType::Line;
    inner.data = hdxf::LineData{{0, 0, 0}, {1, 1, 0}};
    block.entities.push_back(inner);
    dwg.add_insert("Nested", {30, 30, 0});
    if (version >= hdxf::DxfVersion::R2000) {
        dwg.add_lwpolyline({{0, 0}, {3, 0}, {3, 3}}, false);
        dwg.add_ellipse({0, 0, 0}, {5, 0, 0}, 0.5);
        hdxf::Entity& mt = dwg.add_mtext({20, 20, 0}, "Two\\Pparagraphs of column text", 2.5);
        mt.as_mtext().reference_width = 30.0;
        dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
        dwg.add_image("C:\\bilder\\karta.png", {20, 0, 0}, {200, 100}, 40.0);
        dwg.add_mesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0.5, 0.5, 1}}, {{0, 1, 2, 3}, {0, 1, 4}, {1, 2, 4}}, 0);
        dwg.add_layout("Second");
    }
    if (version >= hdxf::DxfVersion::R2010) {
        hdxf::GeoDataData geo;
        geo.version = version >= hdxf::DxfVersion::R2013 ? 3 : 2;
        geo.coordinate_type = 2;
        geo.design_point = {100, 200, 0};
        geo.reference_point = {674000, 6580000, 0};
        geo.coordinate_system = std::string(600, 'x');
        geo.mesh_points = {{{1, 2}, {3, 4}}};
        dwg.set_geodata(geo);
    }
    return dwg;
}

} // namespace

// Every version from R12 to 2018 writes the matrix drawing: the structure checks, it reads back as the
// version and writing it again is the same bytes (ASCII and binary)
TEST(regression_2018_matrix_in_every_version) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004,
                               hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013,
                               hdxf::DxfVersion::R2018}) {
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            hdxf::Drawing dwg = matrix_drawing(v);
            std::vector<uint8_t> data = save(dwg, format);
            if (format == hdxf::DxfFormat::Ascii && v != hdxf::DxfVersion::R12) dxf_structure::check_structure(data, "matrix");
            hdxf::Drawing back = load(data);
            hdxf_test::assert_true(back.version() == v, "version");
            // R12 has no preserved records, so a read drawing is rebuilt (not a byte contract)
            if (v != hdxf::DxfVersion::R12)
            hdxf_test::assert_true(back.save_to_memory(format) == data,
                                   "stable, version " + std::to_string(static_cast<int>(v)) +
                                       (format == hdxf::DxfFormat::Binary ? " binary" : " ascii"));
            hdxf_test::assert_eq(static_cast<int>(back.entities().size()), static_cast<int>(dwg.entities().size()), "entities");
        }
    }
}

// Removing an entity and a layout from a read 2018 drawing leaves no dangling handle: the structure
// checks, the other records are as they were, and the drawing reads back with one entity less
TEST(regression_2018_removal_and_addition) {
    hdxf::Drawing dwg = matrix_drawing(hdxf::DxfVersion::R2018);
    std::vector<uint8_t> first = save(dwg);
    hdxf::Drawing back = load(first);
    size_t count = back.entities().size();
    size_t mesh_at = back.entities().size();
    for (size_t i = 0; i < back.entities().size(); ++i) {
        if (back.entities()[i].type == hdxf::EntityType::Mesh) mesh_at = i;
    }
    hdxf_test::assert_true(mesh_at < back.entities().size(), "the mesh");
    back.entities().erase(back.entities().begin() + static_cast<long>(mesh_at));
    back.add_line({0, 0, 0}, {0, 20, 0});
    for (size_t i = 0; i < back.objects().size(); ++i) {
        if (back.objects()[i].type == hdxf::ObjectType::Layout && back.objects()[i].as_layout().name == "Second") {
            back.objects().erase(back.objects().begin() + static_cast<long>(i));
            break;
        }
    }
    std::vector<uint8_t> second = save(back);
    dxf_structure::check_structure(second, "removal and addition");
    hdxf::Drawing again = load(second);
    hdxf_test::assert_eq(static_cast<int>(again.entities().size()), static_cast<int>(count), "one removed, one added");
    for (const auto& e : again.entities()) hdxf_test::assert_true(e.type != hdxf::EntityType::Mesh, "mesh gone");
    hdxf_test::assert_true(again.save_to_memory(hdxf::DxfFormat::Ascii) == second, "stable");
}
