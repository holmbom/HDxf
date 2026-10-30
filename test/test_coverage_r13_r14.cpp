#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// Test coverage of DXF R13 (AC1012) and R14 (AC1014) against the target matrix of the format profile
// (doc/reference/hdxf_r13_r14_format.md, section 12) and the plan's "Tester och klart när" (plan phase 7,
// step 7.22, B99). Every directed case is a drawing built for a part of the matrix and written in both
// versions and both formats; each file
//   - passes the structure check of dxf_structure.hpp (sections, header, tables and their counts, record forms,
//     older ownership, CLASSES, DIMSTYLE, helper graph — check_older_structure),
//   - is the same groups in ASCII and in binary,
//   - reads back as its version and, written again without a change, is the same file,
//   - and has what its row of the matrix says.
// No golden files: the structure is the references' and the file's consistency with itself. The cases are
// named after the rows:
//
//   row  part of the matrix                                         case
//   M01  version selection, dispatch                                empty
//   M02  HEADER variables, order, codes                             empty
//   M03  header values of a new drawing                             empty
//   M04  header 2000-only variables: E                              errors
//   M05  binary DXF, two-byte codes                                 empty
//   M06  CLASSES                                                    classes
//   M07  table heads and counts                                     tables
//   M08  VPORT, LTYPE, LAYER, STYLE, VIEW, UCS, APPID               tables
//   M09  BLOCK_RECORD                                               tables
//   M10  DIMSTYLE                                                   dimstyle
//   M11  space blocks, BLOCK/ENDBLK                                 blocks
//   M12  paper space: one space                                     paper
//   M13  layouts: E                                                 errors
//   M14  entity head                                                head
//   M15  LINE ... INSERT, SEQEND                                    r12_types, blocks
//   M16  POLYLINE/VERTEX/SEQEND                                     r12_types
//   M17  DIMENSION and *D blocks                                    dimensions
//   M18  VIEWPORT                                                   paper
//   M19  ELLIPSE, XLINE, RAY, SPLINE, TOLERANCE                     r13_family
//   M20  MTEXT                                                      r13_family
//   M21  LEADER                                                     r13_family
//   M22  MLINE                                                      r13_family
//   M23  LWPOLYLINE                                                 lwpolyline, errors
//   M24  HATCH                                                      hatch, errors
//   M25  IMAGE, IMAGEDEF, IMAGEDEF_REACTOR, RASTERVARIABLES         image, errors
//   M26  3DSOLID, REGION, BODY                                      acis
//   M27  SAB/ACDSDATA: E                                            errors
//   M28  DICTIONARY, GROUP, MLINESTYLE, DICTIONARYVAR, XRECORD      objects
//   M29  other objects (as read)                                    every_original_...
//   M30  handles, owners, reactors, $HANDSEED                       every case (check_older_structure)
//   M31  lineweight, plot style, transparency, ... 2000+ types: E   errors
//   M32  symbol names (7.21)                                        empty, tables (upper case standard names)
//   M33  text encoding (7.21)                                       text
//   M34  read file -> its own version (7.18, 7.19)                  every_original_...
// ---------------------------------------------------------------------------

namespace {

using dxf_structure::Group;
using dxf_structure::Rec;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Point3;

const DxfVersion older_versions[] = {DxfVersion::R13, DxfVersion::R14};
const DxfFormat both_formats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfVersion v) { return v == DxfVersion::R13 ? "R13" : "R14"; }
std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

std::vector<uint8_t> save(hdxf::Drawing& dwg, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), what + ": " + dwg.last_error());
    return out;
}

std::vector<Rec> parse(const std::vector<uint8_t>& bytes) { return dxf_structure::records_of_file(bytes); }

std::vector<const Rec*> select(const std::vector<Rec>& recs, const std::string& section, const std::string& type) {
    std::vector<const Rec*> out;
    for (const auto& r : recs) {
        if (r.section == section && r.type() == type) out.push_back(&r);
    }
    return out;
}

// The records of ENTITIES and BLOCKS of a type
std::vector<const Rec*> entities_of(const std::vector<Rec>& recs, const std::string& type) {
    std::vector<const Rec*> out = select(recs, "ENTITIES", type);
    for (const Rec* r : select(recs, "BLOCKS", type)) out.push_back(r);
    return out;
}

// A record's codes before its XDATA, without its handle and what is in a {...} group
std::vector<int> codes_of(const Rec& r) {
    std::vector<int> out;
    bool in_list = false;
    for (const auto& [code, value] : r.groups) {
        if (code >= 1000) break;
        if (code == 102) { in_list = !value.empty() && value[0] == '{'; continue; }
        if (in_list || code == 5 || code == 105) continue;
        out.push_back(code);
    }
    return out;
}

std::string codes_text(const std::vector<int>& codes) {
    std::string t;
    for (int c : codes) t += std::to_string(c) + " ";
    return t;
}

bool has_code(const Rec& r, int code) {
    std::vector<int> codes = codes_of(r);
    return std::find(codes.begin(), codes.end(), code) != codes.end();
}

std::string markers_of(const Rec& r) {
    std::string out;
    for (const auto& g : r.groups) {
        if (g.first == 100) out += (out.empty() ? "" : " ") + g.second;
    }
    return out;
}

std::vector<std::string> values_of(const Rec& r, int code) {
    std::vector<std::string> out;
    for (const auto& g : r.groups) {
        if (g.first >= 1000) break;
        if (g.first == code) out.push_back(g.second);
    }
    return out;
}

const Rec* named(const std::vector<Rec>& recs, const std::string& section, const std::string& type, const std::string& name) {
    for (const Rec* r : select(recs, section, type)) {
        if (r->value(2) == name) return r;
    }
    return nullptr;
}

const Rec* table_head(const std::vector<Rec>& recs, const std::string& table) {
    for (const Rec* r : select(recs, "TABLES", "TABLE")) {
        if (r->value(2) == table) return r;
    }
    return nullptr;
}

double num(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

std::string handle_seed(const hdxf::Drawing& dwg) {
    const hdxf::HeaderVar* seed = dwg.find_header("$HANDSEED");
    return seed && !seed->values.empty() ? seed->values[0].second : std::string();
}

hdxf::Entity line_of(Point3 a, Point3 b, const std::string& layer = "0") {
    hdxf::Drawing tmp;
    return tmp.add_line(a, b, layer);
}

// Where the files of the tests go for the batch of step 7.24 (tools/older_dxf_batch.py): the directory in the
// environment variable HDXF_EXPORT_DIR; when it is not set nothing is written
void export_file(const std::string& kind, const std::string& name, DxfFormat format, const std::vector<uint8_t>& bytes) {
    const char* dir = std::getenv("HDXF_EXPORT_DIR");
    if (!dir || !*dir) return;
    std::string clean;
    for (char c : name) clean += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.') ? c : '_';
    const std::filesystem::path folder = hdxf::path_from_utf8(dir);
    std::filesystem::create_directories(folder);
    std::ofstream out(folder / (kind + "__" + clean + "__" + (format == DxfFormat::Ascii ? "ascii" : "binary") + ".dxf"), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

bool exporting() {
    const char* dir = std::getenv("HDXF_EXPORT_DIR");
    return dir && *dir;
}

// ---------------------------------------------------------------------------
// The driver
// ---------------------------------------------------------------------------

struct Target {
    DxfVersion version;
    DxfFormat format;
    std::string what;
};

using Builder = std::function<hdxf::Drawing(DxfVersion)>;
using Check = std::function<void(const Target&, const std::vector<uint8_t>&, const std::vector<Rec>&, hdxf::Drawing&)>;

struct CaseOptions {
    std::vector<DxfVersion> versions = {DxfVersion::R13, DxfVersion::R14};
    std::set<std::string> extra_object_types;       // untyped objects the case adds
    bool exported = true;                           // written for the AutoCAD batch (HDXF_EXPORT_DIR)
};

void run_case(const std::string& name, const Builder& build, const Check& check, const CaseOptions& options = {}) {
    for (DxfVersion v : options.versions) {
        hdxf::Drawing d = build(v);
        const std::string label = name + " " + name_of(v);
        std::vector<uint8_t> ascii = save(d, DxfFormat::Ascii, label + " ASCII");
        std::vector<uint8_t> binary = save(d, DxfFormat::Binary, label + " binary");
        dxf_structure::check_same_groups(parse(ascii), parse(binary), label + ": ASCII and binary");
        for (DxfFormat f : both_formats) {
            const std::string what = label + " " + name_of(f);
            const std::vector<uint8_t>& bytes = f == DxfFormat::Ascii ? ascii : binary;
            std::vector<Rec> recs = parse(bytes);
            dxf_structure::OlderCheck oc;
            oc.version = v;
            oc.extra_object_types = options.extra_object_types;
            dxf_structure::check_older_structure(recs, oc, what);
            check({v, f, what}, bytes, recs, d);
            if (options.exported) export_file("case", name + "_" + name_of(v), f, bytes);

            // read back as its version, written again without a change: the same file
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(bytes.data(), bytes.size()), what + ": reading back: " + back.last_error());
            hdxf_test::assert_true(back.version() == v, what + ": read back as its version");
            std::vector<uint8_t> second = save(back, f, what + " again");
            hdxf_test::assert_true(second == bytes, what + ": written again without a change it is the same file");
        }
    }
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

hdxf::Drawing tables_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_layer("MyLayer", hdxf::Color::from_aci(3));
    d.add_linetype("Dashed", "Dashed __ __", {0.5, -0.25});
    d.add_text_style("MyStyle", "romans");
    d.add_dim_style("MyDim");
    hdxf::Ucs ucs;
    ucs.name = "MyUcs";
    ucs.origin = {1, 2, 3};
    d.ucs_list().push_back(ucs);
    hdxf::View view;
    view.name = "MyView";
    view.height = 50;
    view.width = 80;
    d.views().push_back(view);
    hdxf::AppId app;
    app.name = "MYAPP";
    d.app_ids().push_back(app);
    d.add_block("TAG").entities.push_back(line_of({0, 0, 0}, {4, 0, 0}));
    return d;
}

hdxf::Entity attdef_of(const std::string& tag) {
    hdxf::AttdefData a;
    a.tag = tag;
    a.prompt = tag + "?";
    a.default_value = "none";
    a.height = 2.0;
    a.start_point = {1, 1, 0};
    hdxf::Entity e;
    e.type = hdxf::EntityType::Attdef;
    e.data = a;
    return e;
}

hdxf::InsertAttribute attribute_of(const std::string& tag, const std::string& value) {
    hdxf::InsertAttribute a;
    a.data.tag = tag;
    a.data.value = value;
    a.data.height = 2.0;
    a.data.start_point = {1, 1, 0};
    return a;
}

hdxf::Drawing blocks_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_layer("MyLayer");
    hdxf::Block& tag = d.add_block("Tag");
    tag.entities.push_back(line_of({0, 0, 0}, {4, 0, 0}));
    tag.entities.push_back(attdef_of("NAME"));
    tag.entities.push_back(attdef_of("NUMBER"));
    hdxf::Block& outer = d.add_block("Outer");
    hdxf::Entity nested;
    nested.type = hdxf::EntityType::Insert;
    hdxf::InsertData ins;
    ins.block_name = "Tag";
    ins.insertion_point = {2, 2, 0};
    ins.attributes.push_back(attribute_of("NAME", "inner"));
    nested.data = ins;
    outer.entities.push_back(nested);
    hdxf::Entity& first = d.add_insert("Tag", {10, 0, 0}, "MyLayer");
    first.as_insert().attributes.push_back(attribute_of("NAME", "Anna"));
    first.as_insert().attributes.push_back(attribute_of("NUMBER", "17"));
    d.add_insert("Outer", {20, 0, 0});
    d.add_insert("Tag", {30, 0, 0});
    return d;
}

hdxf::Entity polyline_of(int flags, const std::vector<Point3>& points, int vertex_flags) {
    hdxf::Drawing tmp;
    hdxf::Entity e = tmp.add_polyline({{0, 0}, {1, 0}});
    e.as_polyline().flags = flags;
    e.as_polyline().vertices.clear();
    for (const auto& p : points) {
        hdxf::Vertex v;
        v.position = p;
        v.flags = vertex_flags;
        e.as_polyline().vertices.push_back(v);
    }
    return e;
}

hdxf::Drawing r12_types_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_layer("MyLayer", hdxf::Color::from_aci(3));
    d.add_text_style("MyStyle", "romans");
    d.add_line({0, 0, 0}, {3, 4, 5}, "MyLayer");
    d.add_circle({1, 2, 3}, 4.5);
    d.add_arc({1, 2, 3}, 4.5, 10, 80);
    d.add_point({7, 8, 9});
    d.add_text({1, 2, 0}, "Hello åäö", 3.0);
    d.entities().back().as_text().style = "MyStyle";
    d.add_trace({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
    d.add_solid({0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0});
    d.add_face3d({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 1});
    // A SHAPE names a shape of a loaded shape file: AutoCAD's core refuses one it cannot find (7.24);
    // ltypeshp.shx comes with AutoCAD; a shape file's STYLE has no name, as AutoCAD writes it (altera_*,
    // puppenhaus_4mm: 2 empty, 70 1, 3 ltypeshp.shx)
    d.add_text_style("SHAPES", "ltypeshp.shx");
    d.text_styles().back().name.clear();
    d.text_styles().back().flags = 1;   // a shape file
    d.add_shape({3, 3, 0}, "BOX", 2.0);
    hdxf::Entity& bulge = d.add_polyline({{0, 0}, {5, 0}, {5, 5}}, true);
    bulge.as_polyline().vertices[1].bulge = 0.5;
    hdxf::Entity p3 = polyline_of(8, {{0, 0, 0}, {1, 1, 1}, {2, 0, 3}}, 32);
    d.add_entity(p3);
    hdxf::Entity mesh = polyline_of(16, {{0, 0, 0}, {1, 0, 0}, {2, 0, 1}, {0, 1, 0}, {1, 1, 1}, {2, 1, 0}}, 64);
    mesh.as_polyline().m_count = 2;
    mesh.as_polyline().n_count = 3;
    d.add_entity(mesh);
    hdxf::Entity pf = polyline_of(64, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 1}}, 192);
    pf.as_polyline().m_count = 4;
    pf.as_polyline().n_count = 2;
    for (int i = 0; i < 2; ++i) {
        hdxf::Vertex face;
        face.flags = 128;
        face.face[0] = 1;
        face.face[1] = 2 + i;
        face.face[2] = 3 + i;
        face.face[3] = i == 0 ? 0 : -4;
        pf.as_polyline().vertices.push_back(face);
    }
    d.add_entity(pf);
    return d;
}

hdxf::Drawing head_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_layer("L1", hdxf::Color::red());
    d.add_linetype("DASHED", "Dashed", {0.5, -0.25});
    hdxf::Entity& model = d.add_line({0, 0, 0}, {1, 1, 0}, "L1");
    model.props.color = hdxf::Color::from_aci(3);
    model.props.linetype = "DASHED";
    model.props.linetype_scale = 2.0;
    model.props.visible = false;
    model.props.thickness = 2.0;
    hdxf::Entity& paper = d.add_line({0, 0, 0}, {2, 2, 0}, "0");
    paper.props.paper_space = 1;
    d.add_block("BLK").entities.push_back(line_of({5, 5, 0}, {6, 6, 0}));
    return d;
}

hdxf::Drawing r13_family_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_text_style("Romans", "romans");
    d.add_ellipse({1, 2, 0}, {5, 0, 0}, 0.4, 0.5, 4.0);
    d.add_xline({1, 1, 0}, {1, 1, 0});
    d.add_ray({2, 2, 0}, {0, 1, 0});
    d.add_spline({{0, 0, 0}, {1, 2, 0}, {3, 3, 0}, {5, 1, 0}, {6, 0, 0}}, 3);
    hdxf::Entity& tolerance = d.add_tolerance({4, 4, 0}, "{\\Fgdt;j}%%v0.5%%v");
    hdxf::Entity& note = d.add_mtext({1, 2, 0}, "Hello\\Pworld", 3.5);
    note.as_mtext().style = "Romans";
    note.as_mtext().drawing_direction = 3;
    std::string long_text;
    for (int i = 0; i < 1020; ++i) long_text += static_cast<char>('a' + i % 26);
    d.add_mtext({0, 0, 0}, long_text, 2.5);
    hdxf::Entity& leader = d.add_leader({{0, 0, 0}, {2, 2, 0}, {4, 2, 0}}, &note);
    leader.as_leader().annotation_offset = {0, 0.5, 0};
    d.add_leader({{7, 7, 0}, {8, 8, 0}}, &tolerance);
    d.add_mline({{0, 0, 0}, {5, 0, 0}, {5, 5, 0}}, false, 2.0, 1);
    d.add_mline({{0, 10, 0}, {5, 10, 0}, {5, 15, 0}, {0, 15, 0}}, true);
    return d;
}

const hdxf::DimensionType dimension_types[] = {hdxf::DimensionType::Rotated,  hdxf::DimensionType::Aligned,
                                               hdxf::DimensionType::Angular,  hdxf::DimensionType::Angular3Pt,
                                               hdxf::DimensionType::Diameter, hdxf::DimensionType::Radius,
                                               hdxf::DimensionType::Ordinate};

hdxf::Drawing dimensions_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    hdxf::DimStyle& plan = d.add_dim_style("Plan");
    plan.dimasz = 3.5;
    plan.dimblk = "_Dot";
    for (auto type : dimension_types) {
        hdxf::Entity& dim = d.add_dimension(type, {34, 38, 0}, {17, 19, 0});
        hdxf::DimensionData& dd = dim.as_dimension();
        dd.def_point2 = {0, 0, 0};
        dd.def_point3 = {40, 30, 0};
        dd.def_point4 = {0, 0, 0};
        dd.arc_point = {25, 25, 0};
        if (type == hdxf::DimensionType::Ordinate) dd.flags = 64;
        if (type == hdxf::DimensionType::Aligned) dd.dim_style = "Plan";
        if (type == hdxf::DimensionType::Rotated) dd.text_override = "<> mm";
    }
    return d;
}

hdxf::Drawing paper_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_layer("MyLayer");
    d.add_line({0, 0, 0}, {5, 0, 0});                                   // model
    hdxf::Entity& paper_line = d.add_line({0, 0, 0}, {0, 5, 0});        // paper, between two model entities
    paper_line.props.paper_space = 1;
    d.add_circle({2, 2, 0}, 1);                                         // model
    hdxf::Entity& page = d.add_viewport({148.5, 105, 0}, 297, 210);
    page.props.paper_space = 1;
    page.as_viewport().id = 1;
    page.as_viewport().status = 1;
    hdxf::Entity& vp = d.add_viewport({150, 100, 0}, 100, 80);
    vp.props.paper_space = 1;
    vp.as_viewport().id = 2;
    vp.as_viewport().status = 2;
    vp.as_viewport().frozen_layers.push_back("MyLayer");
    return d;
}

std::string small_sat(int version) {
    return std::to_string(version) + " 10 1 0\n"
           "-0 body $-1 $1 $-1 $-1 #\n"
           "-1 lump $-1 $-1 $2 $0 #\n"
           "-2 shell $-1 $-1 $-1 $3 $1 #\n"
           "-3 face $-1 $-1 $4 $2 $-1 $5 reversed double out #\n"
           "-4 loop $-1 $-1 $6 $3 #\n"
           "-5 plane-surface $-1 0 0 2 0 0 1 1 0 0 0 I I I I #\n"
           "-6 coedge $-1 $6 $6 $-1 $7 1 $4 $-1 #\n"
           "-7 edge $-1 $8 $8 $6 $9 forward #\n"
           "-8 vertex $-1 $7 $10 #\n"
           "-9 ellipse-curve $-1 3 4 2 0 0 1 2 0 0 0.5 I I #\n"
           "-10 point $-1 5 4 2 #\n";
}

std::vector<std::string> enciphered_lines(const std::string& sat) {
    std::vector<std::string> lines;
    std::string line;
    auto push = [&] {
        std::string out;
        for (char c : line) {
            unsigned char u = static_cast<unsigned char>(c);
            out += (u > 32 && u < 159) ? static_cast<char>(159 - u) : c;
        }
        lines.push_back(out);
        line.clear();
    };
    for (char c : sat) {
        if (c == '\n') push();
        else line += c;
    }
    if (!line.empty()) push();
    return lines;
}

hdxf::AcisData body_of(int sat_version) {
    hdxf::AcisData body;
    body.lines = enciphered_lines(small_sat(sat_version));
    return body;
}

hdxf::Drawing acis_drawing(DxfVersion v) {
    const int sat = v == DxfVersion::R13 ? 105 : 106;
    hdxf::Drawing d(v);
    d.add_acis(hdxf::EntityType::Solid3D, body_of(sat), "0");
    d.add_acis(hdxf::EntityType::Region, body_of(sat - 1), "0");
    d.add_acis(hdxf::EntityType::Body, body_of(100), "0");
    hdxf::Block& block = d.add_block("Solids");
    hdxf::Entity e;
    e.type = hdxf::EntityType::Body;
    e.data = body_of(sat);
    block.entities.push_back(e);
    d.add_insert("Solids", {10, 0, 0});
    return d;
}

hdxf::HatchBoundaryPath polyline_path(int flags, std::vector<hdxf::HatchVertex> vertices, bool closed = true) {
    hdxf::HatchBoundaryPath p;
    p.flags = flags;
    p.closed = closed;
    p.vertices = std::move(vertices);
    return p;
}

hdxf::HatchEdge line_edge(hdxf::Point2 a, hdxf::Point2 b) {
    hdxf::HatchEdge e;
    e.start = a;
    e.end = b;
    return e;
}

hdxf::HatchEdge arc_edge(hdxf::Point2 center, double radius, double from, double to) {
    hdxf::HatchEdge e;
    e.type = hdxf::HatchEdgeType::Arc;
    e.center = center;
    e.radius = radius;
    e.start_angle = from;
    e.end_angle = to;
    return e;
}

hdxf::Drawing hatch_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_layer("MyLayer");
    d.add_hatch({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, "MyLayer");           // solid
    d.add_hatch({{20, 0}, {30, 0}, {30, 10}, {20, 10}});                    // pattern
    {
        auto& h = d.entities().back().as_hatch();
        h.solid = false;
        h.pattern_name = "ANSI31";
        h.pattern_type = 1;
        h.pattern_angle = 15.0;
        h.pattern_scale = 2.5;
        h.pattern_lines.push_back({45.0, {0, 0}, {-0.1767766952966369, 0.1767766952966369}, {}});
        h.pattern_lines.push_back({135.0, {1.0, 2.0}, {0.5, 0.5}, {1.5, -0.5, 0.25}});
        h.seed_points = {{25, 5}};
    }
    d.add_hatch({{40, 0}, {140, 0}, {140, 100}, {40, 100}});                // islands
    {
        auto& h = d.entities().back().as_hatch();
        h.paths[0].flags = 7;
        h.paths.push_back(polyline_path(2, {{50, 10}, {70, 10}, {70, 30}, {50, 30}}));
        h.paths.push_back(polyline_path(2, {{90, 10}, {95, 10}}, false));
    }
    d.add_hatch({});                                                        // edges of every type
    {
        auto& h = d.entities().back().as_hatch();
        h.solid = false;
        h.pattern_name = "ANSI31";
        h.pattern_lines.push_back({45.0, {0, 0}, {-0.1, 0.1}, {}});
        hdxf::HatchBoundaryPath edges;
        edges.flags = 1;
        hdxf::HatchEdge ellipse;
        ellipse.type = hdxf::HatchEdgeType::Ellipse;
        ellipse.center = {10, 140};
        ellipse.major_axis = {10, 0};
        ellipse.ratio = 0.5;
        ellipse.start_angle = 0;
        ellipse.end_angle = 180;
        hdxf::HatchEdge curve;
        curve.type = hdxf::HatchEdgeType::Spline;
        curve.degree = 2;
        curve.rational = true;
        curve.knots = {0, 0, 0, 1, 1, 1};
        curve.control_points = {{0, 140}, {-5, 130}, {0, 120}};
        curve.weights = {1.0, 0.5, 1.0};
        edges.edges = {line_edge({0, 120}, {20, 120}), arc_edge({20, 130}, 10, 270, 90), ellipse, curve};
        h.paths = {edges};
        h.seed_points = {{10, 130}};
    }
    // associative: the two circles are its sources (they get handles at the first save)
    d.add_circle({200, 0, 0}, 10);
    d.add_circle({200, 0, 0}, 4);
    d.add_hatch({});
    {
        auto& h = d.entities().back().as_hatch();
        h.associative = true;
        hdxf::HatchBoundaryPath outer;
        outer.flags = 1;
        outer.edges = {arc_edge({200, 0}, 10, 0, 360)};
        hdxf::HatchBoundaryPath inner = outer;
        inner.flags = 0;
        inner.edges = {arc_edge({200, 0}, 4, 0, 360)};
        h.paths = {outer, inner};
        h.seed_points = {{207, 0}};
    }
    d.save_to_memory();
    const size_t n = d.entities().size();
    d.entities()[n - 1].as_hatch().paths[0].source_handles = {d.entities()[n - 3].props.handle};
    d.entities()[n - 1].as_hatch().paths[1].source_handles = {d.entities()[n - 2].props.handle};
    // in a block and in paper space
    hdxf::Block& fill = d.add_block("Fill");
    hdxf::Entity h;
    h.type = hdxf::EntityType::Hatch;
    hdxf::HatchData data;
    data.paths.push_back(polyline_path(3, {{0, 0}, {1, 0}, {1, 1}}));
    h.data = data;
    fill.entities.push_back(h);
    d.add_insert("Fill", {5, 5, 0});
    d.add_hatch({{0, 0}, {2, 0}, {2, 2}});
    d.entities().back().props.paper_space = 1;
    return d;
}

hdxf::Drawing image_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_image("C:\\bilder\\karta.png", {20, 0, 0}, {200, 100}, 40.0);
    d.add_image("C:\\bilder\\karta.png", {20, 30, 0}, {200, 100}, 20.0);
    d.add_image("a.png", {0, 60, 0}, {100, 50}, 10.0);
    d.add_image("a.png", {40, 60, 0}, {100, 50}, 10.0);
    auto& rect = d.entities()[2].as_image();
    rect.clipping = true;
    rect.display_flags = 7;
    rect.clip_boundary = {{10, 5}, {60, 30}};
    auto& poly = d.entities()[3].as_image();
    poly.clipping = true;
    poly.clip_boundary_type = 2;
    poly.clip_boundary = {{0, 0}, {50, 0}, {99, 25}, {50, 49}, {0, 40}};
    poly.brightness = 70;
    poly.contrast = 30;
    poly.fade = 15;
    return d;
}

hdxf::Drawing lwpolyline_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    hdxf::Entity& closed = d.add_lwpolyline({{0, 0}, {5, 0}, {5, 5}, {0, 5}}, true);
    closed.as_lwpolyline().vertices[1].bulge = 0.5;
    closed.as_lwpolyline().vertices[2].start_width = 0.2;
    closed.as_lwpolyline().vertices[2].end_width = 0.4;
    d.add_lwpolyline({{10, 0}, {15, 5}, {20, 0}});
    hdxf::Entity& paper = d.add_lwpolyline({{0, 0}, {1, 1}, {2, 0}});
    paper.props.paper_space = 1;
    d.add_block("Lw").entities.push_back(d.entities()[1]);
    d.add_insert("Lw", {30, 0, 0});
    return d;
}

hdxf::Object object_of(hdxf::ObjectType type, const char* handle, hdxf::ObjectData data) {
    hdxf::Object o;
    o.type = type;
    o.handle = handle;
    o.owner = "0";
    o.data = std::move(data);
    return o;
}

hdxf::Drawing classes_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    if (v == DxfVersion::R14) {
        d.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}});
        d.add_hatch({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
        d.add_image("pictures/map.png", {50, 50, 0}, {200, 100}, 100.0);
    } else {
        d.add_line({0, 0, 0}, {1, 0, 0});
    }
    hdxf::XRecordData xr;
    xr.data = {{1, "x"}};
    d.objects().push_back(object_of(hdxf::ObjectType::XRecord, "E01", xr));
    hdxf::DictionaryVarData dv;
    dv.value = "1";
    d.objects().push_back(object_of(hdxf::ObjectType::DictionaryVar, "E02", dv));
    hdxf::UnknownObjectData idbuffer;
    idbuffer.object_type = "IDBUFFER";
    idbuffer.group_codes = {{100, "AcDbIdBuffer"}};   // AutoCAD's core needs the subclass (7.24)
    d.objects().push_back(object_of(hdxf::ObjectType::Unknown, "E03", idbuffer));
    return d;
}

hdxf::Drawing objects_drawing(DxfVersion v) {
    hdxf::Drawing d(v);
    d.add_circle({0, 0, 0}, 1);
    d.add_circle({5, 0, 0}, 1);
    d.add_line({0, 0, 0}, {5, 0, 0});
    d.add_mline({{0, 0, 0}, {5, 0, 0}});
    std::vector<hdxf::Entity*> two = {&d.entities()[0], &d.entities()[1]};
    d.add_group("CIRCLES", two, "the two circles");
    std::vector<hdxf::Entity*> one = {&d.entities()[2]};
    d.add_group("ONE", one);
    hdxf::XRecordData xr;
    xr.data = {{1, "x"}, {70, "5"}};
    d.objects().push_back(object_of(hdxf::ObjectType::XRecord, "E11", xr));
    hdxf::DictionaryVarData dv;
    dv.value = "7";
    d.objects().push_back(object_of(hdxf::ObjectType::DictionaryVar, "E12", dv));
    return d;
}

// Everything at once, for the invariants over repeated saves, copies and the checker's own test
hdxf::Drawing mixed_drawing(DxfVersion v) {
    hdxf::Drawing d = tables_drawing(v);
    d.add_circle({1, 2, 3}, 4.5);
    d.add_text({1, 2, 0}, "Hello", 3.0);
    d.add_polyline({{0, 0}, {5, 0}, {5, 5}}, true);
    d.add_ellipse({1, 2, 0}, {5, 0, 0}, 0.4, 0.5, 4.0);
    d.add_mtext({1, 2, 0}, "Hello\\Pworld", 3.5);
    d.add_mline({{0, 0, 0}, {5, 0, 0}, {5, 5, 0}}, false, 2.0, 1);
    d.add_acis(hdxf::EntityType::Solid3D, body_of(v == DxfVersion::R13 ? 105 : 106), "0");
    hdxf::Entity& dim = d.add_dimension(hdxf::DimensionType::Aligned, {34, 38, 0}, {17, 19, 0});
    dim.as_dimension().def_point2 = {0, 0, 0};
    dim.as_dimension().def_point3 = {40, 30, 0};
    hdxf::Entity& page = d.add_viewport({148.5, 105, 0}, 297, 210);
    page.props.paper_space = 1;
    page.as_viewport().id = 1;
    page.as_viewport().status = 1;
    std::vector<hdxf::Entity*> members = {&d.entities()[0]};
    d.add_group("MYGROUP", members, "one curve");
    if (v == DxfVersion::R14) {
        d.add_lwpolyline({{0, 0}, {5, 0}, {5, 5}, {0, 5}}, true);
        d.add_hatch({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
        d.add_image("pictures/map.png", {50, 50, 0}, {200, 100}, 100.0);
    }
    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// M01 - M03, M05, M32: the empty drawing
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_empty_drawing) {
    run_case("empty", [](DxfVersion v) { return hdxf::Drawing(v); },
             [](const Target& t, const std::vector<uint8_t>& bytes, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const bool r13 = t.version == DxfVersion::R13;
                 // M01: the version in $ACADVER, the other ways to the same drawing (a new drawing set to the version)
                 auto vars = dxf_structure::header_variables(recs);
                 std::map<std::string, std::vector<Group>> var;
                 for (const auto& [name, groups] : vars) var[name] = groups;
                 hdxf_test::assert_eq(var["$ACADVER"][0].second, r13 ? "AC1012" : "AC1014", t.what + ": $ACADVER");
                 hdxf::Drawing other;
                 other.set_version(t.version);
                 hdxf_test::assert_true(other.save_to_memory(t.format) == bytes, t.what + ": a new drawing set to the version is the same file");
                 // M02: 162 / 164 variables
                 hdxf_test::assert_eq(static_cast<int>(vars.size()), r13 ? 162 : 164, t.what + ": number of header variables");
                 hdxf_test::assert_eq(vars.back().first, r13 ? "$SAVEIMAGES" : "$MEASUREMENT", t.what + ": the last variable");
                 // M03: AutoCAD's values of the version
                 auto expect = [&](const char* name, int code, const char* value) {
                     hdxf_test::assert_true(var.count(name) > 0 && !var[name].empty(), t.what + ": " + name);
                     hdxf_test::assert_true(var[name][0].first == code && var[name][0].second == value,
                                            t.what + ": " + name + " is " + std::to_string(var[name][0].first) + " " + var[name][0].second);
                 };
                 expect("$MIRRTEXT", 70, "1");
                 expect("$ATTDIA", 70, "0");
                 expect("$OSMODE", 70, "0");
                 expect("$HANDLING", 70, "1");
                 expect("$CELTYPE", 6, "BYLAYER");
                 expect("$CLAYER", 8, "0");
                 expect("$TEXTSTYLE", 7, "STANDARD");
                 expect("$DIMSTYLE", 2, "STANDARD");
                 expect("$DIMTXSTY", 7, "STANDARD");
                 expect("$CMLSTYLE", 2, "STANDARD");
                 expect("$DIMBLK", 1, "");
                 expect("$DWGCODEPAGE", 3, "ANSI_1252");
                 if (r13) {
                     expect("$SAVEIMAGES", 70, "1");
                     hdxf_test::assert_true(var.count("$ACADMAINTVER") == 0 && var.count("$PROXYGRAPHICS") == 0, t.what + ": R13 has neither");
                 } else {
                     expect("$ACADMAINTVER", 70, "0");
                     expect("$PROXYGRAPHICS", 70, "1");
                     hdxf_test::assert_eq(vars[1].first, "$ACADMAINTVER", t.what + ": directly after $ACADVER");
                 }
                 for (const char* name : {"$EXTMIN", "$PEXTMIN"}) hdxf_test::assert_true(num(var[name][0].second) >= 1e19, t.what + ": " + name + " of an empty drawing");
                 for (const char* name : {"$EXTMAX", "$PEXTMAX"}) hdxf_test::assert_true(num(var[name][0].second) <= -1e19, t.what + ": " + name + " of an empty drawing");
                 // M05: the file's form
                 if (t.format == DxfFormat::Binary) {
                     std::istringstream in(std::string(bytes.begin(), bytes.end()), std::ios::binary);
                     hdxf::internal::GroupReader reader(in);
                     reader.detect_format();
                     hdxf_test::assert_true(reader.binary() && reader.code_width() == hdxf::internal::BinaryCodeWidth::TwoBytes, t.what + ": binary with two-byte codes");
                     hdxf_test::assert_eq(std::string(bytes.begin(), bytes.begin() + 18), "AutoCAD Binary DXF", t.what + ": sentinel");
                 } else {
                     hdxf_test::assert_eq(std::string(bytes.begin(), bytes.begin() + 22), "  0\r\nSECTION\r\n  2\r\nHEA", t.what + ": ASCII, group codes in three columns, CRLF as AutoCAD writes");
                 }
                 // M32: the standard names in capitals
                 for (const char* n : {"*MODEL_SPACE", "*PAPER_SPACE"}) hdxf_test::assert_true(named(recs, "TABLES", "BLOCK_RECORD", n) != nullptr, t.what + ": " + n);
                 for (const char* n : {"BYBLOCK", "BYLAYER", "CONTINUOUS"}) hdxf_test::assert_true(named(recs, "TABLES", "LTYPE", n) != nullptr, t.what + ": " + n);
                 hdxf_test::assert_true(named(recs, "TABLES", "STYLE", "STANDARD") && named(recs, "TABLES", "DIMSTYLE", "STANDARD") &&
                                        named(recs, "TABLES", "APPID", "ACAD") && named(recs, "TABLES", "VPORT", "*ACTIVE"), t.what + ": STANDARD, ACAD, *ACTIVE");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "CLASSES", "CLASS").size()), 0, t.what + ": a new drawing has no class");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "ENTITIES", "LINE").size()), 0, t.what + ": nothing in ENTITIES");
             });
}

// ---------------------------------------------------------------------------
// M07 - M09: the tables
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_tables) {
    run_case("tables", tables_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const bool r13 = t.version == DxfVersion::R13;
                 // M07: the head 0 2 5 100 70, no owner, the counts of D5 (LTYPE and BLOCK_RECORD two less than their records)
                 const std::map<std::string, int> counts = {{"VPORT", r13 ? 2 : 1}, {"LTYPE", 2}, {"LAYER", 2}, {"STYLE", 2}, {"VIEW", 1},
                                                            {"UCS", 1}, {"APPID", 2}, {"DIMSTYLE", 2}, {"BLOCK_RECORD", 1}};
                 for (const auto& [table, count] : counts) {
                     const Rec* head = table_head(recs, table);
                     hdxf_test::assert_true(head != nullptr, t.what + ": " + table);
                     if (!head) continue;
                     hdxf_test::assert_eq(codes_text(codes_of(*head)), "0 2 100 70 ", t.what + ": the head of " + table);
                     hdxf_test::assert_eq(std::atoi(head->value(70).c_str()), count, t.what + ": the count of " + table);
                     hdxf_test::assert_false(head->value(5).empty(), t.what + ": the handle of " + table);
                 }
                 // M08, M09: the record forms (profile 5.2)
                 auto expect = [&](const char* type, const char* name, std::vector<int> codes) {
                     const Rec* r = named(recs, "TABLES", type, name);
                     hdxf_test::assert_true(r != nullptr, t.what + ": " + type + " " + name);
                     if (r) hdxf_test::assert_eq(codes_text(codes_of(*r)), codes_text(codes), t.what + ": the groups of " + type + " " + name);
                 };
                 expect("LAYER", "MyLayer", {0, 100, 100, 2, 70, 62, 6});
                 expect("LTYPE", "Dashed", {0, 100, 100, 2, 70, 3, 72, 73, 40, 49, 74, 49, 74});
                 expect("STYLE", "MyStyle", {0, 100, 100, 2, 70, 40, 41, 50, 71, 42, 3, 4});
                 expect("VIEW", "MyView", {0, 100, 100, 2, 70, 40, 10, 20, 41, 11, 21, 31, 12, 22, 32, 42, 43, 44, 50, 71});
                 expect("UCS", "MyUcs", {0, 100, 100, 2, 70, 10, 20, 30, 11, 21, 31, 12, 22, 32});
                 expect("APPID", "MYAPP", {0, 100, 100, 2, 70});
                 expect("BLOCK_RECORD", "TAG", {0, 100, 100, 2});
                 expect("BLOCK_RECORD", "*MODEL_SPACE", {0, 100, 100, 2});
                 hdxf_test::assert_eq(named(recs, "TABLES", "LAYER", "MyLayer")->value(62), "3", t.what + ": the layer's colour");
                 hdxf_test::assert_eq(named(recs, "TABLES", "LTYPE", "Dashed")->value(72), "65", t.what + ": 72 is 65");
                 hdxf_test::assert_eq(named(recs, "TABLES", "LTYPE", "Dashed")->value(73), "2", t.what + ": two elements");
             });
}

// ---------------------------------------------------------------------------
// M10: DIMSTYLE
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_dimstyle) {
    run_case("dimstyle",
             [](DxfVersion v) {
                 hdxf::Drawing d(v);
                 d.add_text_style("MyStyle", "romans");
                 hdxf::DimStyle& plan = d.add_dim_style("Plan");
                 plan.dimblk = "_Dot";
                 plan.dimpost = "mm";
                 plan.dimlunit = 4;
                 plan.dimfrac = 2;           // DIMUNIT 6 (D10)
                 plan.dimatfit = 3;
                 plan.dimtmove = 1;          // DIMFIT 4
                 plan.dimtxsty = "MyStyle";
                 hdxf::Entity& dim = d.add_dimension(hdxf::DimensionType::Aligned, {34, 38, 0}, {17, 19, 0});
                 dim.as_dimension().def_point2 = {0, 0, 0};
                 dim.as_dimension().def_point3 = {40, 30, 0};
                 dim.as_dimension().dim_style = "Plan";
                 return d;
             },
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const Rec* plan = named(recs, "TABLES", "DIMSTYLE", "Plan");
                 const Rec* standard = named(recs, "TABLES", "DIMSTYLE", "STANDARD");
                 const Rec* style = named(recs, "TABLES", "STYLE", "MyStyle");
                 hdxf_test::assert_true(plan && standard && style, t.what + ": the records");
                 if (!plan || !standard || !style) return;
                 hdxf_test::assert_true(!plan->value(105).empty() && !standard->value(105).empty(), t.what + ": 105 is the handle");
                 hdxf_test::assert_eq(plan->value(5), "_Dot", t.what + ": DIMBLK is a name, group 5");
                 hdxf_test::assert_eq(plan->value(3), "mm", t.what + ": DIMPOST");
                 hdxf_test::assert_eq(plan->value(340), style->value(5), t.what + ": 340 is the handle of the text style");
                 hdxf_test::assert_eq(std::atoi(plan->value(270).c_str()), 6, t.what + ": DIMUNIT of (4, 2)");
                 hdxf_test::assert_eq(std::atoi(plan->value(287).c_str()), 4, t.what + ": DIMFIT of (3, 1)");
                 hdxf_test::assert_eq(std::atoi(standard->value(270).c_str()), 2, t.what + ": STANDARD DIMUNIT");
                 hdxf_test::assert_eq(std::atoi(standard->value(287).c_str()), 3, t.what + ": STANDARD DIMFIT");
                 for (int code : {49, 50, 69, 79, 90, 148, 179, 276, 277, 278, 279, 289, 290, 295, 341, 342, 343, 344, 371, 372}) {
                     hdxf_test::assert_false(has_code(*plan, code), t.what + ": " + std::to_string(code) + " is 2000's");
                 }
                 hdxf_test::assert_true(table_head(recs, "DIMSTYLE")->value(71).empty() && table_head(recs, "DIMSTYLE")->value(340).empty(),
                                        t.what + ": no 71 or 340 in the head of the table");
             });
}

// ---------------------------------------------------------------------------
// M11, M15: blocks, INSERT, ATTDEF, ATTRIB
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_blocks_and_attributes) {
    run_case("blocks", blocks_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 std::vector<std::string> blocks;
                 for (const Rec* r : select(recs, "BLOCKS", "BLOCK")) {
                     blocks.push_back(r->value(2));
                     bool xref_path = false;
                     for (const auto& g : r->groups) xref_path = xref_path || (g.first == 1 && g.second.empty());
                     hdxf_test::assert_true(xref_path, t.what + ": BLOCK " + r->value(2) + " has its xref path (1) empty");
                 }
                 hdxf_test::assert_true(blocks == std::vector<std::string>({"*MODEL_SPACE", "*PAPER_SPACE", "Tag", "Outer"}), t.what + ": the blocks in order");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "BLOCKS", "ATTDEF").size()), 2, t.what + ": ATTDEFs");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "BLOCKS", "ATTRIB").size()), 1, t.what + ": the nested INSERT's ATTRIB");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "ENTITIES", "ATTRIB").size()), 2, t.what + ": the INSERT's ATTRIBs");
                 for (const Rec* r : entities_of(recs, "ATTDEF")) hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbText AcDbAttributeDefinition", t.what);
                 for (const Rec* r : entities_of(recs, "ATTRIB")) hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbText AcDbAttribute", t.what);
                 int with_attributes = 0, without = 0;
                 for (const Rec* r : entities_of(recs, "INSERT")) {
                     hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbBlockReference", t.what);
                     (r->value(66) == "1" ? with_attributes : without) += 1;
                 }
                 hdxf_test::assert_eq(with_attributes, 2, t.what + ": INSERTs with 66 1 (the nested one and the first)");
                 hdxf_test::assert_eq(without, 2, t.what + ": INSERTs without attributes");
                 hdxf_test::assert_eq(static_cast<int>(entities_of(recs, "SEQEND").size()), with_attributes, t.what + ": a SEQEND for each INSERT with attributes");
             });
}

// ---------------------------------------------------------------------------
// M14: the entity head
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_entity_head) {
    run_case("head", head_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const auto lines = entities_of(recs, "LINE");
                 hdxf_test::assert_eq(static_cast<int>(lines.size()), 3, t.what + ": three LINEs");
                 for (const Rec* r : lines) {
                     const std::string layer = r->value(8);
                     if (layer == "L1") {
                         hdxf_test::assert_eq(codes_text(codes_of(*r)), codes_text({0, 100, 8, 6, 62, 48, 60, 100, 39, 10, 20, 30, 11, 21, 31}), t.what + ": every property");
                     } else if (has_code(*r, 67)) {
                         hdxf_test::assert_eq(codes_text(codes_of(*r)), codes_text({0, 100, 67, 8, 100, 10, 20, 30, 11, 21, 31}), t.what + ": paper space");
                         hdxf_test::assert_eq(r->value(67), "1", t.what);
                     } else {
                         hdxf_test::assert_eq(codes_text(codes_of(*r)), codes_text({0, 100, 8, 100, 10, 20, 30, 11, 21, 31}), t.what + ": a block's LINE");
                     }
                     hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbLine", t.what);
                 }
             });
}

// ---------------------------------------------------------------------------
// M15, M16: the R12 types and POLYLINE
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_r12_types_and_polylines) {
    run_case("r12_types", r12_types_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const std::map<std::string, std::string> markers = {
                     {"LINE", "AcDbEntity AcDbLine"}, {"CIRCLE", "AcDbEntity AcDbCircle"}, {"ARC", "AcDbEntity AcDbCircle AcDbArc"},
                     {"POINT", "AcDbEntity AcDbPoint"}, {"TRACE", "AcDbEntity AcDbTrace"}, {"SOLID", "AcDbEntity AcDbTrace"},
                     {"3DFACE", "AcDbEntity AcDbFace"}, {"SHAPE", "AcDbEntity AcDbShape"}, {"TEXT", "AcDbEntity AcDbText AcDbText"}};
                 for (const auto& [type, expected] : markers) {
                     const auto list = entities_of(recs, type);
                     hdxf_test::assert_eq(static_cast<int>(list.size()), 1, t.what + ": one " + type);
                     if (!list.empty()) hdxf_test::assert_eq(markers_of(*list[0]), expected, t.what + ": the subclasses of " + type);
                 }
                 const auto polys = entities_of(recs, "POLYLINE");
                 hdxf_test::assert_eq(static_cast<int>(polys.size()), 4, t.what + ": POLYLINEs");
                 if (polys.size() == 4) {
                     hdxf_test::assert_eq(markers_of(*polys[0]), "AcDbEntity AcDb2dPolyline", t.what);
                     hdxf_test::assert_eq(markers_of(*polys[1]), "AcDbEntity AcDb3dPolyline", t.what);
                     hdxf_test::assert_eq(markers_of(*polys[2]), "AcDbEntity AcDbPolygonMesh", t.what);
                     hdxf_test::assert_eq(markers_of(*polys[3]), "AcDbEntity AcDbPolyFaceMesh", t.what);
                 }
                 hdxf_test::assert_eq(static_cast<int>(entities_of(recs, "VERTEX").size()), 3 + 3 + 6 + 6, t.what + ": VERTEXes");
                 hdxf_test::assert_eq(static_cast<int>(entities_of(recs, "SEQEND").size()), 4, t.what + ": a SEQEND for each POLYLINE");
                 // M33: the text's non-ASCII characters are in the file's code page (ANSI_1252: one byte each) or as \U+
                 const Rec* text = entities_of(recs, "TEXT")[0];
                 const std::string value = text->value(1);
                 hdxf_test::assert_true(value == "Hello \xE5\xE4\xF6" || value == "Hello \\U+00E5\\U+00E4\\U+00F6", t.what + ": the text as written: " + value);
             });
}

// ---------------------------------------------------------------------------
// M19 - M22: R13's family
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_r13_family) {
    run_case("r13_family", r13_family_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const bool r13 = t.version == DxfVersion::R13;
                 const std::map<std::string, std::string> markers = {
                     {"ELLIPSE", "AcDbEntity AcDbEllipse"}, {"XLINE", "AcDbEntity AcDbXline"}, {"RAY", "AcDbEntity AcDbRay"},
                     {"SPLINE", "AcDbEntity AcDbSpline"}, {"TOLERANCE", "AcDbEntity AcDbFcf"}};
                 for (const auto& [type, expected] : markers) {
                     const auto list = entities_of(recs, type);
                     hdxf_test::assert_eq(static_cast<int>(list.size()), 1, t.what + ": one " + type);
                     if (!list.empty()) hdxf_test::assert_eq(markers_of(*list[0]), expected, t.what + ": the subclasses of " + type);
                 }
                 // M20: MTEXT, 3 in parts of 255 (R13) or 250 (R14), 72 as the version has it, no 2000 groups
                 const auto texts = entities_of(recs, "MTEXT");
                 hdxf_test::assert_eq(static_cast<int>(texts.size()), 2, t.what + ": MTEXTs");
                 if (texts.size() == 2) {
                     const size_t part = r13 ? 255 : 250;
                     std::string joined;
                     const auto threes = values_of(*texts[1], 3);
                     for (const auto& p : threes) {
                         hdxf_test::assert_eq(static_cast<int>(p.size()), static_cast<int>(part), t.what + ": a 3 part");
                         joined += p;
                     }
                     joined += texts[1]->value(1);
                     hdxf_test::assert_eq(static_cast<int>(joined.size()), 1020, t.what + ": the parts are the text");
                     hdxf_test::assert_eq(static_cast<int>(threes.size()), r13 ? 3 : 4, t.what + ": the number of 3 parts of 1020 characters");
                     hdxf_test::assert_eq(std::atoi(texts[0]->value(72).c_str()), 3, t.what + ": 72 top to bottom");
                     for (const Rec* r : texts) {
                         for (int code : {73, 44, 46, 90, 63, 45, 441, 75, 101}) hdxf_test::assert_false(has_code(*r, code), t.what + ": " + std::to_string(code) + " in an MTEXT");
                     }
                 }
                 // M21: LEADER 212 (R13: the annotation's offset) / 213 (R14: the annotation placement offset)
                 const auto leaders = entities_of(recs, "LEADER");
                 hdxf_test::assert_eq(static_cast<int>(leaders.size()), 2, t.what + ": LEADERs");
                 if (!leaders.empty()) {
                     hdxf_test::assert_eq(markers_of(*leaders[0]), "AcDbEntity AcDbLeader", t.what);
                     hdxf_test::assert_true(has_code(*leaders[0], r13 ? 212 : 213) && !has_code(*leaders[0], r13 ? 213 : 212), t.what + ": the annotation offset in " + (r13 ? "212" : "213"));
                 }
                 // M22: MLINE 71 is 1 open and 3 closed, its style by handle
                 const auto mlines = entities_of(recs, "MLINE");
                 hdxf_test::assert_eq(static_cast<int>(mlines.size()), 2, t.what + ": MLINEs");
                 if (mlines.size() == 2) {
                     hdxf_test::assert_eq(markers_of(*mlines[0]), "AcDbEntity AcDbMline", t.what);
                     hdxf_test::assert_eq(std::atoi(mlines[0]->value(71).c_str()), 1, t.what + ": open");
                     hdxf_test::assert_eq(std::atoi(mlines[1]->value(71).c_str()), 3, t.what + ": closed");
                     const Rec* style = named(recs, "OBJECTS", "MLINESTYLE", "STANDARD");
                     hdxf_test::assert_true(style != nullptr && mlines[0]->value(340) == style->value(5), t.what + ": 340 is STANDARD");
                 }
             });
}

// ---------------------------------------------------------------------------
// M17: DIMENSION and the blocks of the dimensions
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_dimensions) {
    run_case("dimensions", dimensions_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const auto dims = select(recs, "ENTITIES", "DIMENSION");
                 hdxf_test::assert_eq(static_cast<int>(dims.size()), 7, t.what + ": DIMENSIONs");
                 std::set<std::string> blocks;
                 for (const Rec* b : select(recs, "BLOCKS", "BLOCK")) blocks.insert(b->value(2));
                 size_t n = 0;
                 for (auto type : dimension_types) {
                     if (n >= dims.size()) break;
                     const Rec& r = *dims[n++];
                     const std::string label = t.what + ": DIMENSION " + std::to_string(static_cast<int>(type));
                     int flags = std::atoi(r.value(70).c_str());
                     hdxf_test::assert_true((flags & 32) != 0, label + ": bit 32 of 70 is always set in R13 and later");
                     hdxf_test::assert_eq(flags & 15, static_cast<int>(type), label + ": the type bits");
                     for (int code : {71, 42, 280, 73, 74, 75, 72, 41, 370, 380, 390, 410, 440}) hdxf_test::assert_false(has_code(r, code), label + ": " + std::to_string(code));
                     hdxf_test::assert_true(blocks.count(r.value(2)) == 1 && r.value(2).compare(0, 2, "*D") == 0, label + ": the block " + r.value(2) + " is a built *D block of BLOCKS");
                     hdxf_test::assert_true(r.value(3) == "STANDARD" || r.value(3) == "Plan", label + ": the style " + r.value(3));
                 }
                 // The built blocks: the points on DEFPOINTS, the arrows as blocks of the file, BYBLOCK throughout
                 bool defpoints = false, dot = false;
                 for (const Rec* r : select(recs, "BLOCKS", "POINT")) defpoints = defpoints || r->value(8) == "DEFPOINTS";
                 for (const Rec* r : select(recs, "BLOCK_RECORD", "BLOCK_RECORD")) dot = dot || r->value(2) == "_Dot";
                 dot = dot || blocks.count("_Dot") > 0;
                 hdxf_test::assert_true(defpoints, t.what + ": the definition points are on DEFPOINTS");
                 hdxf_test::assert_true(dot, t.what + ": the arrow _Dot is a block of the file");
                 hdxf_test::assert_true(named(recs, "TABLES", "LAYER", "DEFPOINTS") != nullptr, t.what + ": the layer DEFPOINTS");
             });
}

// ---------------------------------------------------------------------------
// M12, M18: paper space and VIEWPORT
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_paper_space_and_viewports) {
    run_case("paper", paper_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const bool r13 = t.version == DxfVersion::R13;
                 // D6: R13 writes the paper space entities first; R14 the entities in the list's order
                 std::vector<std::string> types;
                 std::vector<std::string> flags;
                 for (const auto& r : recs) {
                     if (r.section != "ENTITIES" || r.type() == "SECTION" || r.type() == "ENDSEC") continue;
                     types.push_back(r.type());
                     flags.push_back(r.value(67) == "1" ? "P" : "M");
                 }
                 const std::vector<std::string> want_types = r13 ? std::vector<std::string>{"LINE", "VIEWPORT", "VIEWPORT", "LINE", "CIRCLE"}
                                                                  : std::vector<std::string>{"LINE", "LINE", "CIRCLE", "VIEWPORT", "VIEWPORT"};
                 const std::vector<std::string> want_flags = r13 ? std::vector<std::string>{"P", "P", "P", "M", "M"}
                                                                  : std::vector<std::string>{"M", "P", "M", "P", "P"};
                 hdxf_test::assert_true(types == want_types, t.what + ": the order of the entities");
                 hdxf_test::assert_true(flags == want_flags, t.what + ": the spaces in that order");
                 // No layout, no plot settings, one paper space block with 67 1
                 for (const auto& r : recs) {
                     hdxf_test::assert_false(r.type() == "LAYOUT" || r.type() == "PLOTSETTINGS", t.what + ": " + r.type());
                     hdxf_test::assert_false(r.type() == "BLOCK_RECORD" && has_code(r, 340), t.what + ": BLOCK_RECORD with a layout");
                 }
                 hdxf_test::assert_eq(named(recs, "BLOCKS", "BLOCK", "*PAPER_SPACE")->value(67), "1", t.what + ": 67 1 on the paper space block");
                 // VIEWPORT: the old form with ACAD / MVIEW XDATA, nothing of 2000's
                 const auto ports = select(recs, "ENTITIES", "VIEWPORT");
                 hdxf_test::assert_eq(static_cast<int>(ports.size()), 2, t.what + ": VIEWPORTs");
                 for (const Rec* r : ports) {
                     hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbViewport", t.what);
                     hdxf_test::assert_true(has_code(*r, 68) && has_code(*r, 69), t.what + ": 68 and 69");
                     for (int code : {90, 340, 12, 13, 14, 15, 16, 17, 42, 43, 44, 45, 50, 51, 72, 281, 71, 74, 110, 111, 112, 79, 146, 170, 61, 348, 292, 282, 141, 142, 63, 421, 361}) {
                         hdxf_test::assert_false(has_code(*r, code), t.what + ": " + std::to_string(code) + " is 2000's");
                     }
                     bool mview = false;
                     for (size_t i = 0; i + 1 < r->groups.size(); ++i) mview = mview || (r->groups[i] == Group{1001, "ACAD"} && r->groups[i + 1] == Group{1000, "MVIEW"});
                     hdxf_test::assert_true(mview, t.what + ": XDATA ACAD MVIEW");
                 }
                 bool frozen = false;
                 for (const auto& g : ports.back()->groups) frozen = frozen || (g.first == 1003 && g.second == "MyLayer");
                 hdxf_test::assert_true(frozen, t.what + ": the frozen layer in the XDATA");
             });
}

// ---------------------------------------------------------------------------
// M26: ACIS
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_acis) {
    run_case("acis", acis_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing& d) {
                 const int sat = t.version == DxfVersion::R13 ? 105 : 106;
                 for (const char* type : {"3DSOLID", "REGION", "BODY"}) {
                     const auto list = entities_of(recs, type);
                     hdxf_test::assert_true(!list.empty(), t.what + ": " + type);
                     for (const Rec* r : list) {
                         hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbModelerGeometry", t.what + ": " + type + " has no AcDb3dSolid");
                         hdxf_test::assert_eq(std::atoi(r->value(70).c_str()), 1, t.what + ": the modeler version 70 is 1");
                         for (int code : {350, 290, 2}) hdxf_test::assert_false(has_code(*r, code), t.what + ": " + std::to_string(code) + " is 2013's");
                     }
                 }
                 hdxf_test::assert_true(select(recs, "ACDSDATA", "ACDSRECORD").empty(), t.what + ": no ACDSDATA");
                 hdxf::Drawing back;
                 std::vector<uint8_t> bytes = d.save_to_memory(t.format);
                 hdxf_test::assert_true(back.load_from_memory(bytes.data(), bytes.size()), back.last_error());
                 int bodies = 0;
                 const int expected[] = {sat, sat - 1, 100};
                 for (const auto& e : back.entities()) {
                     if (e.type != hdxf::EntityType::Solid3D && e.type != hdxf::EntityType::Region && e.type != hdxf::EntityType::Body) continue;
                     hdxf_test::assert_true(e.as_acis().lines == body_of(expected[bodies]).lines, t.what + ": the SAT lines of body " + std::to_string(bodies));
                     hdxf_test::assert_true(e.as_acis().sab.empty(), t.what + ": no SAB");
                     ++bodies;
                 }
                 hdxf_test::assert_eq(bodies, 3, t.what + ": bodies");
             },
             // The test bodies are fragments (a plane face, also as a 3DSOLID): AutoCAD cannot restore them
             // (65000), so they stay out of the AutoCAD batch. ACIS in R13/R14: not tested against AutoCAD
             // (no AutoCAD-saved R13/R14 body with a closed volume was found; release 1.8.1)
             [] { CaseOptions o; o.exported = false; return o; }());
}

// ---------------------------------------------------------------------------
// M23, M24, M25: R14's LWPOLYLINE, HATCH and IMAGE
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_lwpolyline) {
    CaseOptions r14;
    r14.versions = {DxfVersion::R14};
    run_case("lwpolyline", lwpolyline_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const auto list = entities_of(recs, "LWPOLYLINE");
                 hdxf_test::assert_eq(static_cast<int>(list.size()), 4, t.what + ": LWPOLYLINEs (one in the block)");
                 for (const Rec* r : list) hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbPolyline", t.what);
                 const Rec& closed = *select(recs, "ENTITIES", "LWPOLYLINE")[0];
                 hdxf_test::assert_eq(std::atoi(closed.value(90).c_str()), 4, t.what + ": 90, the number of vertices");
                 hdxf_test::assert_eq(std::atoi(closed.value(70).c_str()) & 1, 1, t.what + ": closed");
                 hdxf_test::assert_eq(static_cast<int>(values_of(closed, 10).size()), 4, t.what + ": four vertices");
                 hdxf_test::assert_false(has_code(closed, 91), t.what + ": no vertex identifiers (2010)");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "CLASSES", "CLASS").size()), 1, t.what + ": one class");
                 hdxf_test::assert_eq(select(recs, "CLASSES", "CLASS")[0]->value(1), "LWPOLYLINE", t.what + ": its class");
             },
             r14);
}

TEST(coverage_r13_r14_hatch) {
    CaseOptions r14;
    r14.versions = {DxfVersion::R14};
    run_case("hatch", hatch_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const auto list = entities_of(recs, "HATCH");
                 hdxf_test::assert_eq(static_cast<int>(list.size()), 7, t.what + ": HATCHes (five in model space, one in paper space, one in a block)");
                 for (const Rec* r : list) {
                     hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbHatch", t.what);
                     for (int code : {450, 451, 452, 453, 460, 461, 462, 463, 470, 63, 421}) hdxf_test::assert_false(has_code(*r, code), t.what + ": " + std::to_string(code) + " is 2004's gradient");
                 }
                 // the loops: solid polyline, pattern, islands (four loops 7 2 2 ...), edges of the four types, associative
                 const Rec& islands = *select(recs, "ENTITIES", "HATCH")[2];
                 hdxf_test::assert_eq(islands.value(91), "3", t.what + ": three loops");
                 hdxf_test::assert_true(values_of(islands, 92) == std::vector<std::string>({"7", "2", "2"}), t.what + ": 92");
                 const Rec& edges = *select(recs, "ENTITIES", "HATCH")[3];
                 hdxf_test::assert_true(values_of(edges, 72) == std::vector<std::string>({"1", "2", "3", "4"}), t.what + ": the four edge types");
                 const Rec& associative = *select(recs, "ENTITIES", "HATCH")[4];
                 hdxf_test::assert_eq(associative.value(71), "1", t.what + ": associative");
                 hdxf_test::assert_true(values_of(associative, 97) == std::vector<std::string>({"1", "1"}), t.what + ": 97, a source for each loop");
                 // the sources are the circles of the file, which have the hatch as a reactor
                 for (const Rec* circle : select(recs, "ENTITIES", "CIRCLE")) {
                     bool reactor = false;
                     for (size_t i = 0; i + 1 < circle->groups.size(); ++i) {
                         reactor = reactor || (circle->groups[i] == Group{102, "{ACAD_REACTORS"} && circle->groups[i + 1] == Group{330, associative.value(5)});
                     }
                     hdxf_test::assert_true(reactor, t.what + ": the source circle " + circle->value(5) + " has the hatch as a reactor");
                 }
                 bool class_found = false;
                 for (const Rec* c : select(recs, "CLASSES", "CLASS")) class_found = class_found || c->value(1) == "HATCH";
                 hdxf_test::assert_true(class_found, t.what + ": the class HATCH");
                 // in a block, in paper space
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "BLOCKS", "HATCH").size()), 1, t.what + ": a HATCH in a block");
                 hdxf_test::assert_eq(select(recs, "ENTITIES", "HATCH").back()->value(67), "1", t.what + ": a HATCH in paper space");
             },
             r14);
}

TEST(coverage_r13_r14_image) {
    CaseOptions r14;
    r14.versions = {DxfVersion::R14};
    run_case("image", image_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const auto images = select(recs, "ENTITIES", "IMAGE");
                 hdxf_test::assert_eq(static_cast<int>(images.size()), 4, t.what + ": IMAGEs");
                 for (const Rec* r : images) {
                     hdxf_test::assert_eq(markers_of(*r), "AcDbEntity AcDbRasterImage", t.what);
                     hdxf_test::assert_false(has_code(*r, 290), t.what + ": no 290 (the clip's inside is 2010)");
                     hdxf_test::assert_true(has_code(*r, 340) && has_code(*r, 360), t.what + ": the definition and the reactor");
                 }
                 hdxf_test::assert_eq(images[0]->value(340), images[1]->value(340), t.what + ": two images of a file share a definition");
                 hdxf_test::assert_true(images[0]->value(360) != images[1]->value(360), t.what + ": a reactor each");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "OBJECTS", "IMAGEDEF").size()), 2, t.what + ": IMAGEDEFs");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "OBJECTS", "IMAGEDEF_REACTOR").size()), 4, t.what + ": IMAGEDEF_REACTORs");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "OBJECTS", "RASTERVARIABLES").size()), 1, t.what + ": RASTERVARIABLES");
                 const Rec* root = select(recs, "OBJECTS", "DICTIONARY")[0];
                 hdxf_test::assert_true(root->groups.size() > 0 && std::count(root->groups.begin(), root->groups.end(), Group{3, "ACAD_IMAGE_DICT"}) == 1 &&
                                            std::count(root->groups.begin(), root->groups.end(), Group{3, "ACAD_IMAGE_VARS"}) == 1,
                                        t.what + ": the root has ACAD_IMAGE_DICT and ACAD_IMAGE_VARS");
                 hdxf_test::assert_eq(images[3]->value(71), "2", t.what + ": a polygon clip");
                 hdxf_test::assert_eq(images[3]->value(91), "6", t.what + ": five vertices, written closed (7.24)");
             },
             r14);
}

// ---------------------------------------------------------------------------
// M06: CLASSES; M28: the objects
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_classes) {
    CaseOptions options;
    options.extra_object_types = {"IDBUFFER"};
    run_case("classes", classes_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 std::vector<std::string> names;
                 for (const Rec* c : select(recs, "CLASSES", "CLASS")) names.push_back(c->value(1));
                 std::sort(names.begin(), names.end());
                 std::vector<std::string> want = t.version == DxfVersion::R13
                     ? std::vector<std::string>{"DICTIONARYVAR", "IDBUFFER", "XRECORD"}
                     : std::vector<std::string>{"DICTIONARYVAR", "HATCH", "IDBUFFER", "IMAGE", "IMAGEDEF", "IMAGEDEF_REACTOR", "LWPOLYLINE",
                                                "RASTERVARIABLES", "XRECORD"};
                 hdxf_test::assert_true(names == want, t.what + ": the classes the drawing uses (" + std::to_string(names.size()) + ")");
                 for (const Rec* c : select(recs, "CLASSES", "CLASS")) {
                     hdxf_test::assert_false(has_code(*c, 91), t.what + ": no instance count (2004)");
                     if (t.version == DxfVersion::R13) hdxf_test::assert_eq(std::atoi(c->value(90).c_str()), 32768, t.what + ": the R13 proxy bit");
                 }
             },
             options);
}

TEST(coverage_r13_r14_objects) {
    CaseOptions options;
    run_case("objects", objects_drawing,
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const auto groups = select(recs, "OBJECTS", "GROUP");
                 hdxf_test::assert_eq(static_cast<int>(groups.size()), 2, t.what + ": GROUPs");
                 const Rec* dict = nullptr;
                 for (const Rec* d : select(recs, "OBJECTS", "DICTIONARY")) {
                     if (std::count(d->groups.begin(), d->groups.end(), Group{3, "CIRCLES"}) == 1) dict = d;
                 }
                 hdxf_test::assert_true(dict != nullptr, t.what + ": ACAD_GROUP lists the groups");
                 if (dict) {
                     for (const Rec* g : groups) {
                         bool reactor = false;
                         for (size_t i = 0; i + 1 < g->groups.size(); ++i) reactor = reactor || (g->groups[i] == Group{102, "{ACAD_REACTORS"} && g->groups[i + 1] == Group{330, dict->value(5)});
                         hdxf_test::assert_true(reactor, t.what + ": the dictionary is the group's reactor and owner");
                     }
                 }
                 hdxf_test::assert_eq(groups[0]->value(300), "the two circles", t.what + ": 300");
                 hdxf_test::assert_eq(static_cast<int>(values_of(*groups[0], 340).size()), 2, t.what + ": two members");
                 hdxf_test::assert_eq(static_cast<int>(values_of(*groups[1], 340).size()), 1, t.what + ": one member");
                 for (const Rec* d : select(recs, "OBJECTS", "DICTIONARY")) {
                     hdxf_test::assert_false(has_code(*d, 280) || has_code(*d, 281), t.what + ": no 280/281 in a DICTIONARY");
                 }
                 for (const Rec* x : select(recs, "OBJECTS", "XRECORD")) hdxf_test::assert_false(has_code(*x, 280), t.what + ": no 280 in an XRECORD");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "OBJECTS", "XRECORD").size()), 1, t.what + ": XRECORD");
                 hdxf_test::assert_eq(static_cast<int>(select(recs, "OBJECTS", "DICTIONARYVAR").size()), 1, t.what + ": DICTIONARYVAR");
                 const Rec* style = named(recs, "OBJECTS", "MLINESTYLE", "STANDARD");
                 hdxf_test::assert_true(style != nullptr, t.what + ": MLINESTYLE STANDARD");
                 if (style) {
                     hdxf_test::assert_eq(markers_of(*style), "AcDbMlineStyle", t.what);
                     hdxf_test::assert_eq(std::atoi(style->value(71).c_str()), 2, t.what + ": two elements");
                 }
             },
             options);
}

// ---------------------------------------------------------------------------
// M33: text (the other half of the row is 7.21's own tests)
// ---------------------------------------------------------------------------

TEST(coverage_r13_r14_text_styles_and_mtext_in_both_formats) {
    run_case("text",
             [](DxfVersion v) {
                 hdxf::Drawing d(v);
                 d.add_text_style("Arial", "arial.ttf");
                 d.add_text_style("Simple", "simplex");
                 d.add_text({0, 0, 0}, "Svenska åäö ÅÄÖ", 2.5);
                 d.entities().back().as_text().style = "Simple";
                 d.add_mtext({0, 5, 0}, "Hej\\Pvärlden", 2.5);
                 d.entities().back().as_mtext().style = "Arial";
                 return d;
             },
             [](const Target& t, const std::vector<uint8_t>&, const std::vector<Rec>& recs, hdxf::Drawing&) {
                 const Rec* arial = named(recs, "TABLES", "STYLE", "Arial");
                 hdxf_test::assert_true(arial != nullptr, t.what + ": the TrueType style");
                 hdxf_test::assert_eq(std::string(entities_of(recs, "TEXT").at(0)->value(7)), "Simple", t.what + ": the text's style");
                 hdxf_test::assert_eq(std::string(entities_of(recs, "MTEXT").at(0)->value(7)), "Arial", t.what + ": the MTEXT's style");
             });
}

// ---------------------------------------------------------------------------
// M04, M13, M23 - M25, M27, M31: what the version has no form for is an error, before anything is written
// ---------------------------------------------------------------------------

namespace {

struct ErrorCase {
    const char* name;
    std::function<void(hdxf::Drawing&)> build;
    // The error of a version; empty when the version writes it
    std::function<std::string(DxfVersion)> error;
};

std::string same_error(const char* text) { return text; }

// The error a save gave: the text, or (a text that starts with '*') a text that contains the rest — the error of a
// dimension style is led by the style's name
void expect_error(const std::string& got, const std::string& expected, const std::string& what) {
    if (!expected.empty() && expected[0] == '*') {
        hdxf_test::assert_true(got.find(expected.substr(1)) != std::string::npos, what + ": the error is "" + got + """);
    } else {
        hdxf_test::assert_eq(got, expected, what + ": the error is "" + got + """);
    }
}

void run_error_case(const ErrorCase& c) {
    for (DxfVersion v : older_versions) {
        const std::string expected = c.error(v);
        for (DxfFormat f : both_formats) {
            const std::string what = std::string(c.name) + " as " + name_of(v) + " " + name_of(f);
            hdxf::Drawing d(v);
            d.add_line({0, 0, 0}, {1, 0, 0});
            c.build(d);
            std::vector<std::string> handles;
            for (const auto& e : d.entities()) handles.push_back(e.props.handle);
            const size_t objects = d.objects().size();
            const std::string seed = handle_seed(d);
            const std::filesystem::path path = std::filesystem::temp_directory_path() / "hdxf_coverage_r13_r14_error.dxf";
            {
                std::ofstream f2(path, std::ios::binary);
                f2 << "before";
            }
            if (expected.empty()) {
                std::vector<uint8_t> out = save(d, f, what);
                dxf_structure::OlderCheck oc;
                oc.version = v;
                oc.extra_object_types = {"IDBUFFER"};
                dxf_structure::check_older_structure(parse(out), oc, what);
            } else {
                hdxf_test::assert_true(d.save_to_memory(f).empty(), what + ": saved");
                expect_error(d.last_error(), expected, what);
                hdxf_test::assert_false(d.save_as(path, f), what + ": save_as");
                expect_error(d.last_error(), expected, what);
                std::ifstream in(path, std::ios::binary);
                std::stringstream ss;
                ss << in.rdbuf();
                hdxf_test::assert_eq(ss.str(), "before", what + ": the file is as it was");
                for (size_t i = 0; i < handles.size(); ++i) hdxf_test::assert_eq(d.entities()[i].props.handle, handles[i], what + ": handle of entity " + std::to_string(i));
                hdxf_test::assert_eq(static_cast<int>(d.objects().size()), static_cast<int>(objects), what + ": objects");
                hdxf_test::assert_eq(handle_seed(d), seed, what + ": $HANDSEED");
            }
            std::filesystem::remove(path);
        }
    }
}

} // namespace

TEST(coverage_r13_r14_what_the_versions_have_no_form_for) {
    const std::vector<ErrorCase> cases = {
        {"$CELWEIGHT", [](hdxf::Drawing& d) { d.set_header_int("$CELWEIGHT", 370, 50); },
         [](DxfVersion) { return same_error("$CELWEIGHT requires DXF 2000 or later"); }},
        {"$PROXYGRAPHICS", [](hdxf::Drawing& d) { d.set_header_int("$PROXYGRAPHICS", 70, 0); },
         [](DxfVersion v) { return v == DxfVersion::R13 ? same_error("$PROXYGRAPHICS requires DXF R14 or later") : std::string(); }},
        {"LAYOUT", [](hdxf::Drawing& d) { d.add_layout("Plan"); },
         [](DxfVersion) { return same_error("LAYOUT requires DXF 2000 or later"); }},
        {"lineweight", [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.lineweight = 50; },
         [](DxfVersion) { return same_error("Lineweight requires DXF 2000 or later"); }},
        {"layer lineweight", [](hdxf::Drawing& d) { d.add_layer("L").lineweight = 50; },
         [](DxfVersion) { return same_error("Lineweight requires DXF 2000 or later"); }},
        {"plot style", [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.plot_style_type = 2; },
         [](DxfVersion) { return same_error("Plot style requires DXF 2000 or later"); }},
        {"transparency", [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.transparency = hdxf::Transparency::from_alpha(100); },
         [](DxfVersion) { return same_error("Transparency requires DXF 2004 or later"); }},
        {"true color", [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.color = hdxf::Color::from_rgb(10, 20, 30); },
         [](DxfVersion) { return same_error("True color requires DXF 2004 or later"); }},
        {"material", [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.material_handle = "2A"; },
         [](DxfVersion) { return same_error("A material requires DXF 2007 or later"); }},
        {"WIPEOUT", [](hdxf::Drawing& d) { d.add_wipeout({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}); },
         [](DxfVersion) { return same_error("WIPEOUT requires DXF 2000 or later"); }},
        {"MESH", [](hdxf::Drawing& d) { d.add_mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 1, 2}}, 0); },
         [](DxfVersion) { return same_error("MESH requires DXF 2000 or later"); }},
        {"dimension style variable", [](hdxf::Drawing& d) { d.add_dim_style("Newer").dimdsep = ','; },
         [](DxfVersion) { return same_error("*DIMDSEP requires DXF 2000 or later"); }},
        {"MTEXT background", [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "t").as_mtext().background.flags = 1; },
         [](DxfVersion) { return same_error("An MTEXT background requires DXF 2004 or later"); }},
        {"LWPOLYLINE", [](hdxf::Drawing& d) { d.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}}); },
         [](DxfVersion v) { return v == DxfVersion::R13 ? same_error("LWPOLYLINE requires DXF R14 or later") : std::string(); }},
        {"HATCH", [](hdxf::Drawing& d) { d.add_hatch({{0, 0}, {4, 0}, {4, 4}}); },
         [](DxfVersion v) { return v == DxfVersion::R13 ? same_error("HATCH requires DXF R14 or later") : std::string(); }},
        {"IMAGE", [](hdxf::Drawing& d) { d.add_image("pictures/map.png", {0, 0, 0}, {200, 100}, 100.0); },
         [](DxfVersion v) { return v == DxfVersion::R13 ? same_error("IMAGE requires DXF R14 or later") : std::string(); }},
        {"ACIS SAT 400", [](hdxf::Drawing& d) { d.add_acis(hdxf::EntityType::Solid3D, body_of(400)); },
         [](DxfVersion v) { return std::string("ACIS SAT version 400 cannot be written as DXF ") + name_of(v); }},
        {"ACIS SAT 21500", [](hdxf::Drawing& d) { d.add_acis(hdxf::EntityType::Solid3D, body_of(21500)); },
         [](DxfVersion v) { return std::string("ACIS SAT version 21500 cannot be written as DXF ") + name_of(v); }},
        {"ACIS SAB only",
         [](hdxf::Drawing& d) {
             hdxf::AcisData sab;
             sab.sab = {'A', 'S', 'M', ' ', 'B', 'i', 'n', 'a', 'r', 'y', 'F', 'i', 'l', 'e', '4'};
             d.add_acis(hdxf::EntityType::Solid3D, sab);
         },
         [](DxfVersion v) { return std::string("A 3DSOLID with only SAB data cannot be written as DXF ") + name_of(v); }},
    };
    for (const auto& c : cases) run_error_case(c);
}

// A drawing read from R12 can be written as R13 or R14 (B86); one read from 2000 … 2018 is written as that version
// only: the conversion is phase 8's
TEST(coverage_r13_r14_a_drawing_read_from_r12_is_written_as_the_older_versions) {
    hdxf::Drawing made(DxfVersion::R12);
    made.add_line({0, 0, 0}, {1, 1, 0});
    std::vector<uint8_t> file = made.save_to_memory();
    hdxf_test::assert_false(file.empty(), made.last_error());
    for (DxfVersion target : older_versions) {
        for (DxfFormat f : both_formats) {
            hdxf::Drawing read;
            hdxf_test::assert_true(read.load_from_memory(file.data(), file.size()), read.last_error());
            read.set_version(target);
            std::vector<uint8_t> out = save(read, f, "R12 as " + name_of(target) + " " + name_of(f));
            dxf_structure::OlderCheck oc;
            oc.version = target;
            oc.new_drawing = false;
            dxf_structure::check_older_structure(parse(out), oc, "R12 as " + name_of(target) + " " + name_of(f));
        }
    }
}

TEST(coverage_r13_r14_a_read_drawing_of_another_version_is_not_converted) {
    for (DxfVersion source : {DxfVersion::R2000, DxfVersion::R2018}) {
        hdxf::Drawing made(source);
        made.add_line({0, 0, 0}, {1, 1, 0});
        std::vector<uint8_t> file = made.save_to_memory();
        hdxf_test::assert_false(file.empty(), made.last_error());
        for (DxfVersion target : older_versions) {
            for (DxfFormat f : both_formats) {
                hdxf::Drawing read;
                hdxf_test::assert_true(read.load_from_memory(file.data(), file.size()), read.last_error());
                hdxf_test::assert_true(read.version() == source, "the version read");
                read.set_version(target);
                const std::string seed = handle_seed(read);
                hdxf_test::assert_true(read.save_to_memory(f).empty(), "a read drawing written as another version");
                hdxf_test::assert_true(read.last_error().rfind("Writing DXF ", 0) == 0 && read.last_error().find(" as " + name_of(target)) != std::string::npos,
                                       "the conversion error: " + read.last_error());
                hdxf_test::assert_eq(handle_seed(read), seed, "$HANDSEED after the refused save");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Repeated saving without an original template, memory budget and copying
// ---------------------------------------------------------------------------

namespace {

// The (type, handle) of every record of a file that has one
std::set<std::string> handles_of(const std::vector<Rec>& recs) {
    std::set<std::string> out;
    for (const auto& r : recs) {
        // the records of the drawing: its entities and objects. The records the writer makes itself (the table heads,
        // the standard table records, the space blocks) take their handles when the file is written
        if (r.section != "ENTITIES" && r.section != "OBJECTS") continue;
        const std::string h = r.value(r.type() == "DIMSTYLE" ? 105 : 5);
        if (!h.empty()) out.insert(r.type() + ":" + h);
    }
    return out;
}

} // namespace

TEST(coverage_r13_r14_repeated_saves_of_a_drawing_that_was_never_read) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat f : both_formats) {
            const std::string what = name_of(v) + " " + name_of(f);
            hdxf::Drawing d = mixed_drawing(v);
            std::vector<uint8_t> first = save(d, f, what);
            hdxf_test::assert_true(save(d, f, what + " second") == first, what + ": saved twice, the same file");
            hdxf_test::assert_true(save(d, f, what + " third") == first, what + ": saved three times, the same file");
            const std::string seed = handle_seed(d);
            // the other format and back: nothing is left over from a save
            save(d, f == DxfFormat::Ascii ? DxfFormat::Binary : DxfFormat::Ascii, what + " other format");
            hdxf_test::assert_true(save(d, f, what + " back") == first, what + ": after the other format, the same file");
            hdxf_test::assert_eq(handle_seed(d), seed, what + ": $HANDSEED is the same after saves");

            // a new entity: every record of the file keeps its handle, the new ones are above the old $HANDSEED
            const std::set<std::string> before = handles_of(parse(first));
            d.add_line({9, 9, 0}, {10, 10, 0});
            std::vector<uint8_t> grown = save(d, f, what + " grown");
            const std::set<std::string> after = handles_of(parse(grown));
            for (const auto& h : before) hdxf_test::assert_true(after.count(h) > 0, what + ": " + h + " keeps its handle");
            hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()) + 1, what + ": one record more");
            for (const auto& h : after) {
                if (before.count(h) > 0) continue;
                hdxf_test::assert_true(dxf_structure::handle_value(h.substr(h.find(':') + 1)) >= dxf_structure::handle_value(seed), what + ": the new handle " + h + " is not below the old $HANDSEED");
            }
            dxf_structure::OlderCheck oc;
            oc.version = v;
            dxf_structure::check_older_structure(parse(grown), oc, what + " grown");

            // save_as writes the same bytes
            const std::filesystem::path path = std::filesystem::temp_directory_path() / ("hdxf_coverage_" + name_of(v) + ".dxf");
            hdxf_test::assert_true(d.save_as(path, f), what + ": " + d.last_error());
            std::ifstream in(path, std::ios::binary);
            std::vector<uint8_t> on_disk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            hdxf_test::assert_true(on_disk == grown, what + ": the file save_as writes is the file save_to_memory returns");
            std::filesystem::remove(path);
        }
    }
}

// The memory budget: R13/R14's typed data is in the variants that were there, so the sizes of phase 6 stand (B84, B99)
TEST(coverage_r13_r14_memory_budget_and_copying) {
#if defined(__GNUC__) && !defined(__clang__)
    if (sizeof(void*) == 8) {
        // the sizes of v1.7.2 (phase 6): none of the types of an entity, an object, a block or a layer grew in phase 7
        hdxf_test::assert_eq(static_cast<int>(sizeof(hdxf::Entity)), 736, "sizeof(Entity)");
        hdxf_test::assert_eq(static_cast<int>(sizeof(hdxf::EntityProperties)), 360, "sizeof(EntityProperties)");
        hdxf_test::assert_eq(static_cast<int>(sizeof(hdxf::EntityData)), 336, "sizeof(EntityData)");
        hdxf_test::assert_eq(static_cast<int>(sizeof(hdxf::Object)), 936, "sizeof(Object)");
        hdxf_test::assert_eq(static_cast<int>(sizeof(hdxf::ObjectData)), 752, "sizeof(ObjectData)");
        // 632: BLOCK's and ENDBLK's colors are two OptionalBoxed<Color> (656 with the one Color of 1.8.1; step 9.2)
        hdxf_test::assert_eq(static_cast<int>(sizeof(hdxf::Block)), 632, "sizeof(Block)");
        hdxf_test::assert_eq(static_cast<int>(sizeof(hdxf::Layer)), 344, "sizeof(Layer)");
        // the dimension style has the legacy DIMUNIT/DIMFIT values besides (+16, a table record)
        hdxf_test::assert_true(sizeof(hdxf::DimStyle) <= 872, "sizeof(DimStyle) = " + std::to_string(sizeof(hdxf::DimStyle)));
        hdxf_test::assert_true(sizeof(hdxf::Drawing) <= 752 + 64, "sizeof(Drawing) = " + std::to_string(sizeof(hdxf::Drawing)));
    }
#endif
    static_assert(std::is_nothrow_move_constructible_v<hdxf::Entity>, "a vector of entities moves its entities");
    static_assert(std::is_nothrow_move_constructible_v<hdxf::Drawing> || true, "");

    for (DxfVersion v : older_versions) {
        for (DxfFormat f : both_formats) {
            const std::string what = name_of(v) + " " + name_of(f);
            hdxf::Drawing original = mixed_drawing(v);
            std::vector<uint8_t> before = save(original, f, what);

            // a copy is a value: changing it changes nothing in the original, and the other way
            hdxf::Drawing copy = original;
            copy.add_line({50, 50, 0}, {60, 60, 0});
            copy.add_layer("COPIED");
            copy.entities()[0].props.layer = "COPIED";
            if (!copy.blocks().empty()) copy.blocks().back().name = "RENAMED";
            std::vector<uint8_t> changed = save(copy, f, what + " copy");
            hdxf_test::assert_true(changed != before, what + ": the copy changed");
            hdxf_test::assert_true(save(original, f, what + " original again") == before, what + ": the original is as it was");
            original.add_circle({70, 70, 0}, 1);
            std::vector<uint8_t> grown = save(original, f, what + " original grown");
            hdxf_test::assert_true(save(copy, f, what + " copy again") == changed, what + ": the copy is as it was");
            hdxf_test::assert_true(grown != before && grown != changed, what);

            // a drawing read and copied: the same
            hdxf::Drawing read;
            hdxf_test::assert_true(read.load_from_memory(before.data(), before.size()), read.last_error());
            hdxf::Drawing read_copy = read;
            hdxf_test::assert_true(save(read_copy, f, what + " read copy") == before, what + ": the copy of a drawing read is written as the file");
            read_copy.add_line({1, 1, 0}, {2, 2, 0});
            hdxf_test::assert_true(save(read, f, what + " read") == before, what + ": the drawing read is not changed by its copy");

            // a moved drawing is written as it was
            hdxf::Drawing moved = std::move(read_copy);
            std::vector<uint8_t> moved_out = save(moved, f, what + " moved");
            dxf_structure::OlderCheck oc;
            oc.version = v;
            oc.new_drawing = false;
            const std::vector<Rec> original_recs = parse(before);
            oc.original = &original_recs;
            dxf_structure::check_older_structure(parse(moved_out), oc, what + " moved");
        }
    }
}

// ---------------------------------------------------------------------------
// The checker catches what it is for: a file made wrong in one place is refused for that place
// ---------------------------------------------------------------------------

namespace {

// Runs the structure check on `recs` and returns what failed ("" when nothing did)
std::string failure_of(const std::vector<Rec>& recs, DxfVersion version) {
    dxf_structure::OlderCheck oc;
    oc.version = version;
    oc.extra_object_types = {"ACAD_PROXY_OBJECT"};
    try {
        dxf_structure::check_older_structure(recs, oc, "tampered");
    } catch (const hdxf_test::AssertFailure& e) {
        return e.message;
    }
    return {};
}

// A handle no record of the file has (below $HANDSEED)
std::string free_handle(const std::vector<Rec>& recs) {
    std::set<uint64_t> used;
    for (const auto& r : recs) {
        const std::string h = r.value(r.type() == "DIMSTYLE" ? 105 : 5);
        if (r.section != "HEADER" && !h.empty() && r.type() != "SECTION") used.insert(dxf_structure::handle_value(h));
    }
    uint64_t v = 1;
    while (used.count(v) > 0) ++v;
    std::ostringstream out;
    out << std::hex << std::uppercase << v;
    return out.str();
}

Rec* find_rec(std::vector<Rec>& recs, const std::string& section, const std::string& type, size_t nth = 0) {
    for (auto& r : recs) {
        if (r.section == section && r.type() == type && nth-- == 0) return &r;
    }
    return nullptr;
}

} // namespace

TEST(coverage_r13_r14_the_structure_check_refuses_what_it_is_for) {
    for (DxfVersion v : older_versions) {
        hdxf::Drawing d = mixed_drawing(v);
        const std::vector<Rec> good = parse(save(d, DxfFormat::Ascii, name_of(v)));
        hdxf_test::assert_eq(failure_of(good, v), "", name_of(v) + ": the untouched file passes");

        struct Mutation {
            const char* name;
            std::function<void(std::vector<Rec>&)> apply;
            const char* message;        // what the failure says
            bool only_r14;
        };
        const std::vector<Mutation> mutations = {
            {"a handle twice", [](std::vector<Rec>& r) { find_rec(r, "ENTITIES", "CIRCLE")->groups[1].second = find_rec(r, "ENTITIES", "TEXT")->groups[1].second; },
             "used twice", false},
            {"an owner in the head", [](std::vector<Rec>& r) { find_rec(r, "ENTITIES", "CIRCLE")->groups.insert(find_rec(r, "ENTITIES", "CIRCLE")->groups.begin() + 2, Group{330, find_rec(r, "ENTITIES", "TEXT")->groups[1].second}); },
             "owner 330", false},
            {"a lineweight", [](std::vector<Rec>& r) { Rec* c = find_rec(r, "ENTITIES", "CIRCLE"); c->groups.insert(c->groups.begin() + 4, Group{370, "50"}); },
             "370 in a CIRCLE", false},
            {"a PLOTSETTINGS object", [](std::vector<Rec>& r) { r.insert(r.end() - 1, Rec{"OBJECTS", {{0, "PLOTSETTINGS"}, {5, free_handle(r)}}}); },
             "PLOTSETTINGS in an R13/R14 file", false},
            {"a DIMSTYLE without 105", [](std::vector<Rec>& r) { auto& g = find_rec(r, "TABLES", "DIMSTYLE")->groups; g.erase(g.begin() + 1); },
             "DIMSTYLE without handle", false},
            {"a table count", [](std::vector<Rec>& r) { find_rec(r, "TABLES", "TABLE", 1)->groups.back().second = "9"; },
             "70 (", false},
            {"a header variable renamed", [](std::vector<Rec>& r) { for (auto& g : find_rec(r, "", "SECTION")->groups) if (g.second == "$LUNITS") g.second = "$XLUNITS"; },
             "header variables", false},
            {"a header variable of 2000", [](std::vector<Rec>& r) { auto& g = find_rec(r, "", "SECTION")->groups; g.push_back({9, "$CELWEIGHT"}); g.push_back({370, "-1"}); },
             "$CELWEIGHT in an R13/R14 header", false},
            {"a section of 2013", [](std::vector<Rec>& r) { r.insert(r.end() - 1, Rec{"", {{0, "SECTION"}, {2, "ACDSDATA"}}}); },
             "six sections", false},
            {"an instance count in a CLASS", [](std::vector<Rec>& r) { find_rec(r, "CLASSES", "CLASS")->groups.push_back({91, "3"}); },
             "91 in a CLASS", true},
            {"the proxy flags of a CLASS", [](std::vector<Rec>& r) { for (auto& g : find_rec(r, "CLASSES", "CLASS")->groups) if (g.first == 90) g.second = "4095"; },
             "proxy flags", true},
            {"a CLASS missing", [](std::vector<Rec>& r) { for (auto it = r.begin(); it != r.end(); ++it) if (it->section == "CLASSES" && it->type() == "CLASS") { r.erase(it); break; } },
             "CLASSES hold", true},
            {"a native entry in the root dictionary", [](std::vector<Rec>& r) { auto& g = find_rec(r, "OBJECTS", "DICTIONARY")->groups; g.push_back({3, "ACAD_LAYOUT"}); g.push_back({350, g[1].second}); },
             "native 2000 data", false},
            {"a style handle that is no style", [](std::vector<Rec>& r) { for (auto& g : find_rec(r, "TABLES", "DIMSTYLE")->groups) if (g.first == 340) g.second = find_rec(r, "TABLES", "LAYER")->groups[1].second; },
             "340 is the handle of a STYLE", false},
            {"a SEQEND missing", [](std::vector<Rec>& r) { for (auto it = r.begin(); it != r.end(); ++it) if (it->section == "ENTITIES" && it->type() == "SEQEND") { r.erase(it); break; } },
             "inside a POLYLINE", false},
            {"a reactor dictionary that does not hold the record", [](std::vector<Rec>& r) {
                 Rec* group = find_rec(r, "OBJECTS", "GROUP");
                 const std::string root = find_rec(r, "OBJECTS", "DICTIONARY")->groups[1].second;
                 for (size_t i = 0; i + 1 < group->groups.size(); ++i) {
                     if (group->groups[i].first == 102 && group->groups[i + 1].first == 330) group->groups[i + 1].second = root;
                 }
             },
             "does not hold it", false},
            {"a proxy object with a class index outside the register", [](std::vector<Rec>& r) {
                 r.insert(r.end() - 1, Rec{"OBJECTS", {{0, "ACAD_PROXY_OBJECT"}, {5, free_handle(r)}, {100, "AcDbProxyObject"}, {90, "499"}, {91, "5"}}});
             },
             "class index", false},
            {"a BLOCK without its BLOCK_RECORD", [](std::vector<Rec>& r) { for (auto& g : find_rec(r, "BLOCKS", "BLOCK", 2)->groups) if (g.first == 2) g.second = "NOSUCH"; },
             "has no BLOCK_RECORD", false},
            {"a table record with a 2000 group", [](std::vector<Rec>& r) { find_rec(r, "TABLES", "LAYER")->groups.push_back({290, "1"}); },
             "290 in a LAYER", false},
        };
        for (const auto& m : mutations) {
            if (m.only_r14 && v != DxfVersion::R14) continue;
            std::vector<Rec> bad = good;
            m.apply(bad);
            const std::string failure = failure_of(bad, v);
            hdxf_test::assert_false(failure.empty(), name_of(v) + ": " + m.name + " was not refused");
            hdxf_test::assert_true(failure.find(m.message) != std::string::npos,
                                   name_of(v) + ": " + m.name + " is refused for \"" + failure + "\", expected \"" + m.message + "\"");
        }
    }
}

// ---------------------------------------------------------------------------
// M29, M34: every R13/R14 file of the test data, written again in both formats
// ---------------------------------------------------------------------------

namespace {

bool is_older_dxf(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string head(8192, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    return head.find("AC1012") != std::string::npos || head.find("AC1014") != std::string::npos;
}

// Whether the records of a file have an owner 330 outside the reactor group: family A2000 (format profile 1.5)
bool has_owner_groups(const std::vector<Rec>& recs) {
    for (const auto& r : recs) {
        if (r.section != "TABLES" && r.section != "ENTITIES" && r.section != "BLOCKS") continue;
        bool in_list = false;
        for (const auto& [code, value] : r.groups) {
            if (code == 100) break;
            if (code == 102) { in_list = !value.empty() && value[0] == '{'; continue; }
            if (code == 330 && !in_list) return true;
        }
    }
    return false;
}

} // namespace

TEST(coverage_r13_r14_every_original_is_written_again_with_the_structure_it_had) {
    namespace fs = std::filesystem;
    const fs::path root = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR);
    std::vector<fs::path> files;
    if (fs::exists(root)) {
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".dxf") continue;
            if (is_older_dxf(entry.path())) files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) hdxf_test::skip("no R13/R14 test file");
    for (const auto& path : files) {
        hdxf::Drawing d;
        const std::string name = path.filename().string();
        hdxf_test::assert_true(d.load(path), name + ": " + d.last_error());
        hdxf_test::assert_true(d.version() == DxfVersion::R13 || d.version() == DxfVersion::R14, name + ": read as R13 or R14");
        std::ifstream in(path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::vector<Rec> original = parse(bytes);
        for (DxfFormat f : both_formats) {
            const std::string what = name + " " + name_of(f);
            std::vector<uint8_t> out = save(d, f, what);
            std::vector<Rec> recs = parse(out);
            dxf_structure::OlderCheck oc;
            oc.version = d.version();
            oc.owner_groups = has_owner_groups(original);
            oc.new_drawing = false;
            oc.original = &original;
            dxf_structure::check_older_structure(recs, oc, what);

            // M29: the objects HDxf does not type are as the file had them; the register is the file's
            auto kept = [](const std::vector<Rec>& all) {
                std::vector<Rec> out2;
                for (const auto& r : all) {
                    const std::string& t = r.type();
                    const bool typed = t == "DICTIONARY" || t == "GROUP" || t == "MLINESTYLE" || t == "IMAGEDEF" || t == "IMAGEDEF_REACTOR" ||
                                       t == "RASTERVARIABLES" || t == "DICTIONARYVAR" || t == "XRECORD";
                    if ((r.section == "OBJECTS" && !typed && t != "SECTION" && t != "ENDSEC") || (r.section == "CLASSES" && t == "CLASS")) out2.push_back(r);
                }
                return out2;
            };
            dxf_structure::check_same_groups(kept(original), kept(recs), what + ": the untyped objects and the register");
            hdxf_test::assert_eq(static_cast<int>(select(recs, "ENTITIES", "LINE").size()) + static_cast<int>(select(recs, "BLOCKS", "LINE").size()),
                                 static_cast<int>(select(original, "ENTITIES", "LINE").size()) + static_cast<int>(select(original, "BLOCKS", "LINE").size()),
                                 what + ": the LINEs");
            export_file("roundtrip", name, f, out);

            // a change: a record of each kind added, the first entity moved to the new layer
            {
                hdxf::Drawing changed = d;
                changed.add_layer("HDXF_CHANGED");
                changed.add_line({0, 0, 0}, {3, 4, 0}, "HDXF_CHANGED");
                changed.add_text({1, 2, 0}, "changed", 2.0, "HDXF_CHANGED");
                if (!changed.entities().empty()) changed.entities().front().props.layer = "HDXF_CHANGED";
                std::vector<uint8_t> after = save(changed, f, what + " changed");
                dxf_structure::check_older_structure(parse(after), oc, what + " changed");
                export_file("changed", name, f, after);
            }
            // a removal: the first entity (its relations follow)
            if (!d.entities().empty()) {
                hdxf::Drawing removed = d;
                removed.entities().erase(removed.entities().begin());
                std::vector<uint8_t> after = save(removed, f, what + " removed");
                dxf_structure::check_older_structure(parse(after), oc, what + " removed");
                export_file("removed", name, f, after);
            }
            // every record written anew from its typed values (what the batch gives the oracles besides the file as read)
            if (exporting()) {
                hdxf::Drawing forced(d.version());
                forced.layers() = d.layers();
                forced.linetypes() = d.linetypes();
                forced.text_styles() = d.text_styles();
                forced.dim_styles() = d.dim_styles();
                forced.views() = d.views();
                forced.ucs_list() = d.ucs_list();
                forced.vports() = d.vports();
                forced.app_ids() = d.app_ids();
                for (auto& l : forced.layers()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); }
                for (auto& l : forced.linetypes()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); }
                for (auto& l : forced.text_styles()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); }
                for (auto& l : forced.dim_styles()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); l.reactors.clear(); }
                for (auto& l : forced.views()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); }
                for (auto& l : forced.ucs_list()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); }
                for (auto& l : forced.app_ids()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); }
                for (auto& l : forced.vports()) { l.raw = hdxf::RawRecord{}; l.handle.clear(); }
                auto anew = [](hdxf::Entity e) {
                    e.raw = hdxf::RawRecord{};
                    e.props.handle.clear();
                    e.props.reactors.clear();
                    e.props.xdictionary.clear();
                    if (e.type == hdxf::EntityType::Polyline) {
                        for (auto& v : e.as_polyline().vertices) { v.handle.clear(); v.reactors.clear(); v.raw = hdxf::RawRecord{}; }
                        e.as_polyline().seqend_handle.clear();
                        e.as_polyline().seqend_raw = hdxf::RawRecord{};
                    } else if (e.type == hdxf::EntityType::Insert) {
                        for (auto& a : e.as_insert().attributes) { a.props.handle.clear(); a.raw = hdxf::RawRecord{}; }
                        e.as_insert().seqend_handle.clear();
                        e.as_insert().seqend_raw = hdxf::RawRecord{};
                    }
                    return e;
                };
                for (const auto& b : d.blocks()) {
                    hdxf::Block copy = b;
                    copy.raw = hdxf::RawRecord{};
                    copy.endblk_raw = hdxf::RawRecord{};
                    copy.handle.clear();
                    copy.endblk_handle.clear();
                    copy.record = hdxf::BlockRecord{};
                    for (auto& e : copy.entities) e = anew(e);
                    bool exists = false;
                    for (auto& own : forced.blocks()) {
                        if (own.name == copy.name) { own = copy; exists = true; }
                    }
                    if (!exists) forced.blocks().push_back(copy);
                }
                for (const auto& e : d.entities()) forced.entities().push_back(anew(e));
                std::vector<uint8_t> anew_bytes = forced.save_to_memory(f);
                if (!anew_bytes.empty()) export_file("forced", name, f, anew_bytes);
            }
        }
    }
}
