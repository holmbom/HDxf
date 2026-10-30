#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include "extents.hpp"
#include "mesh.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MESH (phase 4, step 4.4 — plan B48): read from AutoCAD's files (ACadSharp's two meshes in
// 2000 … 2018, jscad's 3Dmesh01, LibreDWG's Surface.dxf), written anew with AutoCAD's groups and
// proxy graphics in 2000 … 2010, made with add_mesh() as AutoCAD lists a mesh's edges.
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

bool has_file(const std::string& name) {
    if (std::filesystem::exists(external(name))) return true;
    hdxf_test::skip("external test file missing: " + name);
    return false;
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

// The groups from a subclass on (XDATA excluded), and the proxy graphics groups
Record body_of(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first >= 1000) break;
        if (g.first == 100 && g.second == subclass) in = true;
        if (in) out.push_back(g);
    }
    return out;
}

Record proxy_of(const Record& r) {
    Record out;
    for (const auto& g : r) {
        if (g.first == 92 || g.first == 160 || g.first == 310) out.push_back(g);
    }
    return out;
}

// The same groups; a real number to its 16th digit (AutoCAD's ASCII file has 16, read and
// written again the last may differ)
bool same_groups(const Record& a, const Record& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first) return false;
        if (hdxf::internal::group_value_type(a[i].first) == hdxf::internal::GroupValueType::Double) {
            double x = std::strtod(a[i].second.c_str(), nullptr), y = std::strtod(b[i].second.c_str(), nullptr);
            if (std::fabs(x - y) > 1e-15 * std::max(1.0, std::fabs(x))) return false;
        } else if (a[i].second != b[i].second) {
            return false;
        }
    }
    return true;
}

std::vector<const hdxf::Entity*> meshes(const hdxf::Drawing& dwg) {
    std::vector<const hdxf::Entity*> out;
    for (const auto& e : dwg.entities()) if (e.type == hdxf::EntityType::Mesh) out.push_back(&e);
    return out;
}

const char* const kAcadSharp[] = {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1015_binary.dxf",
                                  "acadsharp/sample_AC1018_ascii.dxf", "acadsharp/sample_AC1018_binary.dxf",
                                  "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1021_binary.dxf",
                                  "acadsharp/sample_AC1024_ascii.dxf", "acadsharp/sample_AC1024_binary.dxf",
                                  "acadsharp/sample_AC1027_ascii.dxf", "acadsharp/sample_AC1032_binary.dxf"};

} // namespace

// AutoCAD's meshes read: ACadSharp's 343 (a sphere: 62 vertices, triangles at its poles) and 380
// (a torus) the same in every version; jscad's box FB with ten creases "always"
TEST(mesh_read_values) {
    for (const char* name : kAcadSharp) {
        if (!has_file(name)) continue;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        std::vector<const hdxf::Entity*> m = meshes(dwg);
        hdxf_test::assert_eq(static_cast<int>(m.size()), 2, name);
        const hdxf::MeshData& a = m[0]->as_mesh();
        hdxf_test::assert_eq(m[0]->props.handle, "343", "first");
        hdxf_test::assert_true(a.version == 2 && !a.blend_crease && a.level == 0, "71, 72, 91");
        hdxf_test::assert_eq(static_cast<int>(a.vertices.size()), 62, "vertices");
        hdxf_test::assert_eq(static_cast<int>(a.faces.size()), 72, "faces");
        hdxf_test::assert_true(a.faces[0] == std::vector<int>({60, 1, 0}), "the first face");
        hdxf_test::assert_eq(static_cast<int>(a.edges.size()), 132, "edges");
        hdxf_test::assert_true(a.edges[0].start == 1 && a.edges[0].end == 60, "the first edge");
        hdxf_test::assert_eq(static_cast<int>(a.creases.size()), 132, "creases");
        hdxf_test::assert_true(a.overrides.empty() && a.overrides_unread.empty(), "no overrides");
        hdxf_test::assert_true(std::fabs(a.vertices[60].z + 5.091688756873281) < 1e-14, "the lowest vertex");
        const hdxf::MeshData& b = m[1]->as_mesh();
        hdxf_test::assert_true(b.vertices.size() == 64 && b.faces.size() == 64 && b.edges.size() == 128, "380");
        hdxf_test::assert_eq(m[1]->props.layer, "Layer_color_80", "its layer");
    }
    if (has_file("jscad/3Dmesh01.dxf")) {
        hdxf::Drawing dwg = load(read_bytes(external("jscad/3Dmesh01.dxf")));
        std::vector<const hdxf::Entity*> m = meshes(dwg);
        hdxf_test::assert_eq(static_cast<int>(m.size()), 2, "3Dmesh01");
        int always = 0;
        for (double c : m[0]->as_mesh().creases) always += c == -1.0;
        hdxf_test::assert_eq(always, 10, "ten creases -1");
        hdxf_test::assert_eq(static_cast<int>(m[0]->as_mesh().faces.size()), 54, "54 quads");
    }
}

// Every mesh of AutoCAD's 2000 … 2013 files written anew has AutoCAD's groups and AutoCAD's proxy
// graphics — byte for byte from the binary files (the ASCII files' 16 digits are not the doubles
// AutoCAD drew with); 2013 as 2010 (B65: ACadSharp's two meshes, jscad's 3Dmesh01)
TEST(mesh_written_anew_as_autocad) {
    std::vector<std::string> files(std::begin(kAcadSharp), std::begin(kAcadSharp) + 9);
    files.push_back("acadsharp/sample_AC1027_binary.dxf");
    files.push_back("jscad/3Dmesh01.dxf");
    files.push_back("libre/Surface_2004.dxf");
    int compared = 0;
    for (const auto& name : files) {
        if (!has_file(name)) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        std::vector<std::string> handles;
        for (auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::Mesh) continue;
            e.raw = hdxf::RawRecord();
            handles.push_back(e.props.handle);
        }
        bool binary = name.find("binary") != std::string::npos;
        std::vector<Record> after = records_of(save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii));
        for (const auto& h : handles) {
            Record a = record(before, h), b = record(after, h);
            hdxf_test::assert_true(same_groups(body_of(a, "AcDbSubDMesh"), body_of(b, "AcDbSubDMesh")),
                                   name + " MESH " + h + ": groups");
            if (binary) hdxf_test::assert_true(proxy_of(a) == proxy_of(b), name + " MESH " + h + ": proxy graphics");
            else hdxf_test::assert_eq(static_cast<int>(proxy_of(a).size()), static_cast<int>(proxy_of(b).size()), name + " MESH " + h + ": proxy size");
            ++compared;
        }
    }
    hdxf_test::assert_true(compared > 0, "meshes compared");
}

// add_mesh(): the edges as AutoCAD lists them (face by face, each once, the smaller index first),
// a crease 0 each; written in 2000 … 2013 with its class and proxy graphics, refused as R12
TEST(mesh_add) {
    const std::vector<hdxf::Point3> v = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0.5, 0.5, 1}};
    const std::vector<std::vector<int>> f = {{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
    for (hdxf::DxfVersion version : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007,
                                     hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013}) {
        hdxf::Drawing dwg(version);
        hdxf::Entity& e = dwg.add_mesh(v, f, 1);
        const hdxf::MeshData& d = e.as_mesh();
        std::vector<std::pair<int, int>> edges;
        for (const auto& edge : d.edges) edges.push_back({edge.start, edge.end});
        hdxf_test::assert_true(edges == std::vector<std::pair<int, int>>({{0, 3}, {2, 3}, {1, 2}, {0, 1}, {1, 4}, {0, 4},
                                                                          {2, 4}, {3, 4}}),
                               "edges in AutoCAD's order");
        hdxf_test::assert_true(d.creases == std::vector<double>(8, 0.0), "no creases");
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "mesh");
        std::vector<Record> recs = records_of(out);
        bool cls = false;
        for (const auto& r : recs) {
            if (r[0].second == "CLASS" && r[1].second == "MESH") {
                cls = true;
                hdxf_test::assert_true(r == Record({{0, "CLASS"}, {1, "MESH"}, {2, "AcDbSubDMesh"}, {3, "ObjectDBX Classes"},
                                                    {90, "0"}, {91, "1"}, {280, "0"}, {281, "1"}})
                                           || version == hdxf::DxfVersion::R2000,
                                       "the class as AutoCAD writes it");
            }
            if (r[0].second != "MESH") continue;
            Record body = body_of(r, "AcDbSubDMesh");
            hdxf_test::assert_true(body.at(1) == Record::value_type{71, "2"} && body.at(3) == Record::value_type{91, "1"},
                                   "71 2, 91 1");
            hdxf_test::assert_false(proxy_of(r).empty(), "proxy graphics");
            hdxf_test::assert_eq(proxy_of(r).at(0).first, version >= hdxf::DxfVersion::R2010 ? 160 : 92, "92 / 160");
            hdxf_test::assert_eq(body.back().first, 90, "the override count last");
        }
        hdxf_test::assert_true(cls, "class MESH");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_true(meshes(back).size() == 1 && meshes(back)[0]->as_mesh().faces == f, "read back");
    }
    hdxf::Drawing r12;
    r12.add_mesh(v, f);
    hdxf_test::assert_true(r12.save_to_memory().empty(), "R12");
    hdxf_test::assert_eq(r12.last_error(), "MESH requires DXF 2000 or later");
    hdxf::Drawing bad(hdxf::DxfVersion::R2010);
    bad.add_mesh(v, {{0, 1, 9}});
    hdxf_test::assert_true(bad.save_to_memory().empty(), "a vertex index out of range");
    hdxf_test::assert_true(bad.last_error().find("vertex index 9 of 5 vertices") != std::string::npos, bad.last_error());
}

// The overridden properties: a color (63, 421, 431), a material (330) and a transparency (90) as
// AutoCAD reads them and ODA writes them; a material mapper's data kept as read, a new one refused
TEST(mesh_overrides) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    std::string material;
    for (const auto& o : dwg.objects()) {
        if (o.type == hdxf::ObjectType::Material) material = o.handle;
    }
    hdxf::Entity& e = dwg.add_mesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}, {{0, 1, 2, 3}});
    hdxf::MeshOverride face;
    face.marker = 1;
    hdxf::MeshProperty color;
    color.type = 0;
    color.color = hdxf::Color::from_rgb(255, 0, 0);
    hdxf::MeshProperty mat;
    mat.type = 1;
    mat.material_handle = material;
    face.properties = {color, mat};
    hdxf::MeshOverride edge;
    edge.marker = 10;
    hdxf::MeshProperty transparency;
    transparency.type = 2;
    transparency.transparency = hdxf::Transparency::from_alpha(25);
    edge.properties = {transparency};
    e.as_mesh().overrides = {face, edge};
    std::vector<uint8_t> out = save(dwg);
    for (const auto& r : records_of(out)) {
        if (r[0].second != "MESH") continue;
        Record body = body_of(r, "AcDbSubDMesh");
        Record tail(body.end() - 12, body.end());
        hdxf_test::assert_true(tail == Record({{90, "2"}, {91, "1"}, {92, "2"}, {90, "0"}, {63, "1"}, {421, "16711680"},
                                               {90, "1"}, {330, material}, {91, "10"}, {92, "1"}, {90, "2"},
                                               {90, "33554457"}}),
                               "the override values");
        // The override data in AutoCAD's order
        std::vector<int> codes;
        for (const auto& g : body) codes.push_back(g.first);
        auto at = std::find(codes.rbegin(), codes.rend(), 140);
        std::vector<int> after(at.base(), codes.end());
        hdxf_test::assert_true(after == std::vector<int>({90, 91, 92, 90, 63, 421, 90, 330, 91, 92, 90, 90}), "groups");
    }
    hdxf::Drawing back = load(out);
    const hdxf::MeshData& d = meshes(back).at(0)->as_mesh();
    hdxf_test::assert_eq(static_cast<int>(d.overrides.size()), 2, "read back");
    hdxf_test::assert_eq(d.overrides[0].properties[0].color.rgb, 0xFF0000, "color");
    hdxf_test::assert_eq(d.overrides[0].properties[1].material_handle, material, "material");
    hdxf_test::assert_eq(d.overrides[1].properties[0].transparency.alpha(), 25, "transparency");

    // A mapper (type 3) read: the override data kept as read and written back
    std::vector<Record> recs = records_of(out);
    std::string mapped;
    for (auto& r : recs) {
        if (r[0].second == "MESH") {
            auto last = std::find_if(r.rbegin(), r.rend(), [](const Record::value_type& g) { return g.first == 140; });
            r.erase(last.base(), r.end());
            for (const auto& g : Record({{90, "1"}, {91, "1"}, {92, "1"}, {90, "3"}, {72, "2"}, {46, "1.5"}})) r.push_back(g);
        }
        for (const auto& [code, v] : r) mapped += std::to_string(code) + std::string(1, 10) + v + std::string(1, 10);
    }
    hdxf::Drawing m = load(std::vector<uint8_t>(mapped.begin(), mapped.end()));
    const hdxf::MeshData& md = meshes(m).at(0)->as_mesh();
    hdxf_test::assert_true(md.overrides.empty() && md.overrides_unread.size() == 6, "kept as read");
    for (auto& ent : m.entities()) if (ent.type == hdxf::EntityType::Mesh) ent.raw = hdxf::RawRecord();
    for (const auto& r : records_of(save(m))) {
        if (r[0].second != "MESH") continue;
        Record tail(r.end() - 6, r.end());
        hdxf_test::assert_true(tail == Record({{90, "1"}, {91, "1"}, {92, "1"}, {90, "3"}, {72, "2"}, {46, "1.5"}}),
                               "written as read");
    }
    hdxf::MeshProperty mapper;
    mapper.type = 3;
    e.as_mesh().overrides[0].properties.push_back(mapper);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "a new mapper");
    hdxf_test::assert_true(dwg.last_error().find("material mapper") != std::string::npos, dwg.last_error());
}

// ---------------------------------------------------------------------------
// MESH smoothing and extents (phase 4, step 4.5 — plan B49)
// ---------------------------------------------------------------------------

namespace {

struct FacitCrease {
    int a;
    int b;
    double value;
};

struct SmoothingCase {
    const char* name;
    int level;
    int blend;
    std::vector<hdxf::Point3> vertices;
    std::vector<std::vector<int>> faces;
    std::vector<FacitCrease> creases;
    std::vector<double> extents;        // AutoCAD's $EXTMIN, $EXTMAX
};

const std::vector<SmoothingCase>& smoothing_cases() {
    static const std::vector<SmoothingCase> cases = {
#include "mesh_smoothing_facit.inc"
    };
    return cases;
}

} // namespace

// AutoCAD's extents of 120 smoothed meshes (closed and open, triangles, quads and pentagons,
// creases -1, 0.25 … 2 alone, in loops, at corners and from the boundary, blend crease on and off,
// levels 1-4), computed by AutoCAD 2027's own engine: HDxf's smoothing gives them to AutoCAD's
// noise (about 5e-8 in these, B52)
TEST(mesh_smoothing_as_autocad) {
    int cases = 0;
    double worst = 0.0;
    for (const auto& c : smoothing_cases()) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
        hdxf::Entity& e = dwg.add_mesh(c.vertices, c.faces, c.level);
        hdxf::MeshData& d = e.as_mesh();
        d.blend_crease = c.blend != 0;
        for (const auto& cr : c.creases) {
            for (size_t i = 0; i < d.edges.size(); ++i) {
                bool same = (d.edges[i].start == cr.a && d.edges[i].end == cr.b) ||
                            (d.edges[i].start == cr.b && d.edges[i].end == cr.a);
                if (same) d.creases[i] = cr.value;
            }
        }
        hdxf::Point3 lo, hi;
        hdxf_test::assert_true(dwg.entity_extents(e, lo, hi), c.name);
        const double ours[] = {lo.x, lo.y, lo.z, hi.x, hi.y, hi.z};
        for (int k = 0; k < 6; ++k) {
            double dev = std::fabs(ours[k] - c.extents[static_cast<size_t>(k)]);
            worst = std::max(worst, dev);
            hdxf_test::assert_true(dev < 1e-7, std::string(c.name) + ": " + std::to_string(ours[k]) + " / " +
                                                   std::to_string(c.extents[static_cast<size_t>(k)]));
        }
        ++cases;
    }
    hdxf_test::assert_eq(cases, 120, "cases");
    hdxf_test::assert_true(worst < 6e-8, "AutoCAD's noise: " + std::to_string(worst));
}

// ACadSharp's whole drawing, every version 2000 … 2018, ASCII and binary: its $EXTMIN/$EXTMAX as
// AutoCAD wrote them — all six, $EXTMIN z −5.091688756873281 from MESH 343's lowest vertex. In
// the ASCII files to the 16 digits AutoCAD wrote (the last may round), in the binary ones to
// the bit
TEST(mesh_acadsharp_extents_as_autocad) {
    for (const char* name : kAcadSharp) {
        if (!has_file(name)) continue;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        hdxf::internal::Extents model, paper;
        std::vector<std::string> warnings;
        hdxf::internal::drawing_extents(dwg, {}, {}, model, paper, warnings);
        bool binary = std::string(name).find("binary") != std::string::npos;
        for (const char* var : {"$EXTMIN", "$EXTMAX"}) {
            const hdxf::HeaderVar* h = dwg.find_header(var);
            hdxf_test::assert_true(h && h->values.size() == 3, var);
            const hdxf::Point3& p = std::string(var) == "$EXTMIN" ? model.min : model.max;
            const double ours[] = {p.x, p.y, p.z};
            for (int k = 0; k < 3; ++k) {
                double theirs = std::strtod(h->values[static_cast<size_t>(k)].second.c_str(), nullptr);
                double tol = binary ? 0.0 : 2e-16 * std::max(1.0, std::fabs(theirs)) * 8;
                hdxf_test::assert_true(std::fabs(ours[k] - theirs) <= tol,
                                       std::string(name) + " " + var + " " + std::to_string(k) + ": " +
                                           std::to_string(ours[k]) + " / " + h->values[static_cast<size_t>(k)].second);
            }
        }
    }
}

// A MESH in a MULTILEADER's block is drawn in its proxy graphics as the MESH's own (its edges after
// their markers, smoothed to its level), in the block's transform
TEST(mesh_in_mleader_block) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    hdxf::Block& b = dwg.add_block("NAT");
    hdxf::Entity mesh;
    mesh.type = hdxf::EntityType::Mesh;
    hdxf::Drawing scratch(hdxf::DxfVersion::R2010);
    mesh.data = scratch.add_mesh({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0.5, 0.5, 1}},
                                 {{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}, 1).as_mesh();
    b.entities.push_back(mesh);
    dwg.add_mleader_block({{0, 0, 0}, {3, 2, 0}}, "NAT");
    std::vector<uint8_t> own = hdxf::internal::mesh_proxy(mesh.as_mesh());
    std::vector<uint8_t> commands(own.begin() + 8, own.end());       // without the size and count
    for (const auto& r : records_of(save(dwg))) {
        if (r[0].second != "MULTILEADER") continue;
        std::vector<uint8_t> bytes;
        auto digit = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        for (const auto& [code, v] : r) {
            if (code != 310) continue;
            for (size_t i = 0; i + 1 < v.size(); i += 2) bytes.push_back(static_cast<uint8_t>(digit(v[i]) * 16 + digit(v[i + 1])));
        }
        hdxf_test::assert_true(std::search(bytes.begin(), bytes.end(), commands.begin(), commands.end()) != bytes.end(),
                               "the MESH's commands in the MULTILEADER's graphics");
    }
}
