#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Integration of the R13/R14 writers (plan phase 7, wave 2): one name is never written in two forms in a
// file, and everything a drawing can have in the older form together passes the structure check — ASCII
// and binary, written, read and written again. No golden files: the checks are the structure and the
// consistency of the file with itself.
// ---------------------------------------------------------------------------

namespace {

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

std::vector<uint8_t> file_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

using dxf_structure::Symbols;
using dxf_structure::check_references;
using dxf_structure::expect_in;
using dxf_structure::head_value;
using dxf_structure::symbols_of;

// A drawing that refers to its records in other cases than they are written in
hdxf::Drawing named_drawing(DxfVersion version) {
    hdxf::Drawing d(version);
    d.add_layer("MyLayer");
    d.add_linetype("Dashed", "Dashed __ __", {0.5, -0.25});
    d.add_text_style("MyStyle", "txt");
    hdxf::DimStyle& dim_style = d.add_dim_style("MyDim");
    (void)dim_style;
    hdxf::Block& block = d.add_block("MyBlock");
    {
        hdxf::Drawing tmp;
        hdxf::Entity in_block = tmp.add_line({0, 0, 0}, {1, 1, 0}, "mylayer");
        in_block.props.linetype = "DASHED";
        block.entities.push_back(in_block);
    }

    hdxf::Entity& a = d.add_line({0, 0, 0}, {5, 0, 0}, "mylayer");
    a.props.linetype = "dashed";
    hdxf::Entity& b = d.add_line({0, 1, 0}, {5, 1, 0}, "0");
    b.props.linetype = "ByLayer";
    hdxf::Entity& c = d.add_line({0, 2, 0}, {5, 2, 0}, "0");
    c.props.linetype = "Continuous";
    hdxf::Entity& e = d.add_line({0, 3, 0}, {5, 3, 0}, "MYLAYER");
    e.props.linetype = "ByBlock";
    d.add_text({0, 5, 0}, "style", 2.0, "0");
    d.entities().back().as_text().style = "MYSTYLE";
    d.add_text({0, 8, 0}, "standard", 2.0, "0");
    d.entities().back().as_text().style = "STANDARD";
    d.add_text({0, 9, 0}, "standard", 2.0, "0");
    d.entities().back().as_text().style = "standard";
    d.add_mtext({0, 12, 0}, "mtext", 2.0);
    d.entities().back().as_mtext().style = "mystyle";
    d.add_insert("MYBLOCK", {20, 0, 0});
    d.add_insert("myblock", {30, 0, 0});

    // Dimensions: the default style, a style in another case, with and without a block of their own
    for (const char* style : {"Standard", "mydim", "MYDIM", "STANDARD"}) {
        hdxf::Entity& dim = d.add_dimension(hdxf::DimensionType::Aligned, {34, 38, 0}, {17, 19, 0});
        hdxf::DimensionData& dd = dim.as_dimension();
        dd.dim_style = style;
        dd.def_point2 = {0, 0, 0};
        dd.def_point3 = {40, 30, 0};
    }
    d.add_leader({{0, 0, 0}, {2, 2, 0}, {4, 2, 0}});
    d.entities().back().as_leader().dim_style = "mydim";
    d.add_tolerance({9, 9, 0}, "{\\Fgdt;j}%%v1");
    d.entities().back().as_tolerance().dim_style = "standard";

    // The header names the records in other cases too
    auto set = [&](const char* variable, int code, const char* value) {
        hdxf::HeaderVar* v = d.find_header(variable);
        hdxf_test::assert_true(v != nullptr, std::string("the header has ") + variable);
        if (v) v->values = {{code, value}};
    };
    set("$CLAYER", 8, "mylayer");
    set("$CELTYPE", 6, "dashed");
    set("$TEXTSTYLE", 7, "mystyle");
    set("$DIMSTYLE", 2, "mydim");
    return d;
}


} // namespace

TEST(integration_r13_r14_every_name_is_written_in_one_form) {
    for (DxfVersion version : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(version) + " " + name_of(format);
            hdxf::Drawing d = named_drawing(version);
            std::vector<uint8_t> out = save(d, format, what);
            std::vector<Rec> recs = dxf_structure::records_of_file(out);
            check_references(recs, what);

            // What the program named is written as it named it, the standard records in capitals
            Symbols s = symbols_of(recs);
            expect_in(s.layers, "MyLayer", what);
            expect_in(s.layers, "0", what);
            expect_in(s.layers, "DEFPOINTS", what);
            expect_in(s.linetypes, "Dashed", what);
            expect_in(s.linetypes, "BYBLOCK", what);
            expect_in(s.linetypes, "BYLAYER", what);
            expect_in(s.linetypes, "CONTINUOUS", what);
            expect_in(s.styles, "MyStyle", what);
            expect_in(s.styles, "STANDARD", what);
            expect_in(s.dim_styles, "MyDim", what);
            expect_in(s.dim_styles, "STANDARD", what);
            expect_in(s.blocks, "MyBlock", what);
            expect_in(s.blocks, "*MODEL_SPACE", what);
            expect_in(s.blocks, "*PAPER_SPACE", what);
            hdxf_test::assert_eq(static_cast<int>(s.layers.size()), 3, what + ": layers");

            // The file read again names its records as they are, and written again keeps one spelling
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), what + ": " + back.last_error());
            for (const auto& e : back.entities()) {
                const bool layer_found = std::any_of(back.layers().begin(), back.layers().end(), [&](const hdxf::Layer& l) {
                    return upper(l.name) == upper(e.props.layer);
                });
                hdxf_test::assert_true(layer_found, what + ": read back: layer " + e.props.layer);
                if (e.type == hdxf::EntityType::Insert) {
                    hdxf_test::assert_true(back.find_block(e.as_insert().block_name) != nullptr, what + ": read back: block " + e.as_insert().block_name);
                }
            }
            hdxf::Drawing again = back;
            check_references(dxf_structure::records_of_file(save(again, format, what + " again")), what + " again");
        }
    }
}

// ---------------------------------------------------------------------------
// Structure
// ---------------------------------------------------------------------------

namespace {

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

// Every type that has an older form, with its tables, blocks and paper space
hdxf::Drawing everything(DxfVersion version) {
    hdxf::Drawing d(version);
    // Tables with records of the program's own
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

    // Blocks, INSERT and ATTRIB
    hdxf::Block& tag = d.add_block("TAG");
    {
        hdxf::Drawing tmp;
        tag.entities.push_back(tmp.add_line({0, 0, 0}, {4, 0, 0}));
    }
    tag.entities.push_back(attdef_of("NAME"));
    tag.entities.push_back(attdef_of("NUMBER"));
    hdxf::Block& outer = d.add_block("OUTER");
    {
        hdxf::Entity nested;
        nested.type = hdxf::EntityType::Insert;
        hdxf::InsertData ins;
        ins.block_name = "TAG";
        ins.insertion_point = {2, 2, 0};
        ins.attributes.push_back(attribute_of("NAME", "inner"));
        nested.data = ins;
        outer.entities.push_back(nested);
    }
    hdxf::Entity& first = d.add_insert("TAG", {10, 0, 0}, "MyLayer");
    first.as_insert().attributes.push_back(attribute_of("NAME", "Anna"));
    first.as_insert().attributes.push_back(attribute_of("NUMBER", "17"));
    d.add_insert("OUTER", {20, 0, 0});

    // The R12 types
    d.add_line({0, 0, 0}, {3, 4, 5}, "MyLayer");
    d.add_circle({1, 2, 3}, 4.5);
    d.add_arc({1, 2, 3}, 4.5, 10, 80);
    d.add_point({7, 8, 9});
    d.add_text({1, 2, 0}, "Hello", 3.0);
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
    // POLYLINE: 2D (with bulge), 3D, polygon mesh and polyface
    d.add_polyline({{0, 0}, {5, 0}, {5, 5}}, true);
    d.add_entity(polyline_of(8, {{0, 0, 0}, {1, 1, 1}, {2, 0, 3}}, 32));
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
    // The R13 types
    d.add_ellipse({1, 2, 0}, {5, 0, 0}, 0.4, 0.5, 4.0);
    d.add_spline({{0, 0, 0}, {1, 2, 0}, {3, 3, 0}, {5, 1, 0}, {6, 0, 0}}, 3);
    hdxf::Entity& mtext = d.add_mtext({1, 2, 0}, "Hello\\Pworld", 3.5);
    hdxf::Entity& tolerance = d.add_tolerance({4, 4, 0}, "{\\Fgdt;j}%%v0.5%%v");
    d.add_leader({{0, 0, 0}, {2, 2, 0}, {4, 2, 0}}, &mtext);
    d.add_leader({{7, 7, 0}, {8, 8, 0}}, &tolerance);
    d.add_mline({{0, 0, 0}, {5, 0, 0}, {5, 5, 0}}, false, 2.0, 1);
    d.add_xline({1, 1, 0}, {1, 1, 0});
    d.add_ray({2, 2, 0}, {0, 1, 0});
    if (version == DxfVersion::R14) {
        // R14's own: LWPOLYLINE, HATCH (with its source, a polyline of the drawing) and IMAGE (IMAGEDEF, IMAGEDEF_REACTOR,
        // RASTERVARIABLES and ACAD_IMAGE_DICT of the root); R13 refuses the last two (see the next test)
        d.add_lwpolyline({{0, 0}, {5, 0}, {5, 5}, {0, 5}}, true);
        d.add_hatch({{0, 0}, {4, 0}, {4, 4}, {0, 4}}, "MyLayer");
        d.add_image("pictures/map.png", {50, 50, 0}, {200, 100}, 100.0);
    }
    // Dimensions: every type
    const hdxf::DimensionType types[] = {hdxf::DimensionType::Rotated,    hdxf::DimensionType::Aligned,
                                         hdxf::DimensionType::Angular,    hdxf::DimensionType::Angular3Pt,
                                         hdxf::DimensionType::Diameter,   hdxf::DimensionType::Radius,
                                         hdxf::DimensionType::Ordinate};
    for (auto type : types) {
        hdxf::Entity& dim = d.add_dimension(type, {34, 38, 0}, {17, 19, 0});
        hdxf::DimensionData& dd = dim.as_dimension();
        dd.def_point2 = {0, 0, 0};
        dd.def_point3 = {40, 30, 0};
        dd.def_point4 = {0, 0, 0};
        dd.arc_point = {25, 25, 0};
        if (type == hdxf::DimensionType::Ordinate) dd.flags = 64;
    }
    // Groups and the multiline styles of the root
    std::vector<hdxf::Entity*> members;
    for (auto& e : d.entities()) {
        if (e.type == hdxf::EntityType::Circle || e.type == hdxf::EntityType::Arc) members.push_back(&e);
    }
    d.add_group("MYGROUP", members, "two curves");
    // An ACIS body, SAT 105 in R13 and 106 in R14
    hdxf::AcisData body;
    body.lines = enciphered_lines(small_sat(version == DxfVersion::R13 ? 105 : 106));
    d.add_acis(hdxf::EntityType::Solid3D, body, "MyLayer");
    // Paper space with its VIEWPORTs
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

} // namespace

// R13 has neither HATCH nor IMAGE: the error names the type, before anything is written
TEST(integration_r13_r14_hatch_and_image_are_r14s) {
    for (DxfFormat format : both_formats) {
        hdxf::Drawing hatch(DxfVersion::R13);
        hatch.add_hatch({{0, 0}, {4, 0}, {4, 4}});
        hdxf_test::assert_true(hatch.save_to_memory(format).empty(), "R13 HATCH");
        hdxf_test::assert_eq(hatch.last_error(), std::string("HATCH requires DXF R14 or later"), "R13 HATCH");
        hdxf::Drawing image(DxfVersion::R13);
        image.add_image("pictures/map.png", {0, 0, 0}, {200, 100}, 100.0);
        hdxf_test::assert_true(image.save_to_memory(format).empty(), "R13 IMAGE");
        hdxf_test::assert_eq(image.last_error(), std::string("IMAGE requires DXF R14 or later"), "R13 IMAGE");
    }
}

TEST(integration_r13_r14_a_drawing_with_every_older_type_passes_the_structure_check) {
    for (DxfVersion version : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(version) + " " + name_of(format);
            hdxf::Drawing d = everything(version);
            std::vector<uint8_t> out = save(d, format, what);
            std::vector<Rec> recs = dxf_structure::records_of_file(out);
            dxf_structure::check_older_structure(recs, {version, false, true}, what);

            // every type of the drawing is in the file
            std::set<std::string> types;
            for (const auto& r : recs) if (r.section == "ENTITIES") types.insert(r.type());
            for (const char* t : {"LINE", "CIRCLE", "ARC", "POINT", "TEXT", "TRACE", "SOLID", "3DFACE", "SHAPE", "POLYLINE", "VERTEX",
                                  "SEQEND", "INSERT", "ATTRIB", "ELLIPSE", "SPLINE", "MTEXT", "TOLERANCE", "LEADER", "MLINE", "XLINE",
                                  "RAY", "DIMENSION", "VIEWPORT", "3DSOLID"}) {
                hdxf_test::assert_true(types.count(t) > 0, what + ": no " + t + " in ENTITIES");
            }
            hdxf_test::assert_eq(types.count("LWPOLYLINE") > 0, version == DxfVersion::R14, what + ": LWPOLYLINE only in R14");
            hdxf_test::assert_eq(types.count("HATCH") > 0, version == DxfVersion::R14, what + ": HATCH only in R14");
            hdxf_test::assert_eq(types.count("IMAGE") > 0, version == DxfVersion::R14, what + ": IMAGE only in R14");
            std::set<std::string> objects;
            for (const auto& r : recs) if (r.section == "OBJECTS") objects.insert(r.type());
            for (const char* t : {"DICTIONARY", "GROUP", "MLINESTYLE"}) {
                hdxf_test::assert_true(objects.count(t) > 0, what + ": no " + t + " in OBJECTS");
            }
            for (const char* t : {"IMAGEDEF", "IMAGEDEF_REACTOR", "RASTERVARIABLES"}) {
                hdxf_test::assert_eq(objects.count(t) > 0, version == DxfVersion::R14, what + ": " + t + " only in R14");
            }
            check_references(recs, what);

            // The file read and written again: the same structure, the same family
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), what + ": " + back.last_error());
            std::vector<uint8_t> second = save(back, format, what + " second");
            dxf_structure::check_older_structure(dxf_structure::records_of_file(second), {version, false, true}, what + " second");
            hdxf_test::assert_true(second == out, what + ": written again without a change it is the same file");
        }
    }
}

// A dimension block in R13/R14: MTEXT as AutoCAD R14 files have it (71 5, 72 1, A1 before the text, no
// style, no 73/44, the direction 11 only when the text is turned), the points on DEFPOINTS, BYBLOCK throughout
TEST(integration_r13_r14_dimension_block_mtext_and_symbols) {
    for (DxfVersion version : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(version) + " " + name_of(format);
            hdxf::Drawing d(version);
            hdxf::Entity& dim = d.add_dimension(hdxf::DimensionType::Aligned, {34, 38, 0}, {17, 19, 0});
            dim.as_dimension().def_point2 = {0, 0, 0};
            dim.as_dimension().def_point3 = {40, 30, 0};
            hdxf::Entity& flat = d.add_dimension(hdxf::DimensionType::Rotated, {0, 20, 0}, {20, 20, 0});
            flat.as_dimension().def_point2 = {0, 0, 0};
            flat.as_dimension().def_point3 = {40, 0, 0};
            std::vector<Rec> recs = dxf_structure::records_of_file(save(d, format, what));
            dxf_structure::check_older_structure(recs, {version, false, true}, what);
            check_references(recs, what);
            Symbols s = symbols_of(recs);
            expect_in(s.layers, "DEFPOINTS", what);
            expect_in(s.linetypes, "BYBLOCK", what);
            expect_in(s.linetypes, "BYLAYER", what);

            int mtexts = 0;
            int turned = 0;
            bool on_defpoints = false;
            for (const auto& r : recs) {
                if (r.section != "BLOCKS") continue;
                if (r.type() == "POINT" && r.value(8) == "DEFPOINTS") on_defpoints = true;
                if (r.type() != "MTEXT") continue;
                ++mtexts;
                std::vector<int> codes;
                for (const auto& g : r.groups) if (g.first != 5) codes.push_back(g.first);
                std::vector<int> want = {0, 100, 8, 6, 62, 100, 10, 20, 30, 40, 41, 71, 72, 1};
                if (!r.value(11).empty()) {
                    ++turned;
                    want.insert(want.end(), {11, 21, 31});
                }
                hdxf_test::assert_true(codes == want, what + ": the groups of a dimension's MTEXT");
                hdxf_test::assert_eq(r.value(6), std::string("BYBLOCK"), what);
                hdxf_test::assert_eq(r.value(8), std::string("0"), what);
                hdxf_test::assert_eq(std::atoi(r.value(71).c_str()), 5, what + ": 71");
                hdxf_test::assert_eq(std::atoi(r.value(72).c_str()), 1, what + ": 72");
                hdxf_test::assert_true(r.value(1).compare(0, 4, "\\A1;") == 0, what + ": text starts with \\A1;");
            }
            hdxf_test::assert_eq(mtexts, 2, what + ": one MTEXT per dimension");
            hdxf_test::assert_eq(turned, 1, what + ": the direction only for the turned text");
            hdxf_test::assert_true(on_defpoints, what + ": the definition points are on DEFPOINTS");
        }
    }
}

// Files AutoCAD wrote, with things added in the older form: the structure check passes in the file's family
// (owner groups only in family A2000), DEFPOINTS, BYBLOCK and the standard names exist where the new
// records need them, and what was added refers to records that are there
TEST(integration_r13_r14_read_files_with_added_records) {
    struct Case { const char* dir; const char* file; bool owners; };
    const Case cases[] = {{"libre", "r13_v.dxf", false},
                          {"spatialauxetic", "HEAD.DXF", false},
                          {"gvsig", "dxfprueba.dxf", false},
                          {"ezdxf", "small_r13.dxf", true},
                          {"ezdxf", "small_r14.dxf", true},
                          {"local-r13r14", "BL15-1_davidsmarx_r14_dimension_hatch.dxf", false},
                          {"local-r13r14", "puppenhaus_4mm_r14_viewport_view.dxf", false},
                          {"local-r13r14", "Test3_Flash_Tank_I_r14.dxf", false}};
    int checked = 0;
    for (const Case& c : cases) {
        std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / c.dir / c.file;
        if (!std::filesystem::exists(path)) continue;
        ++checked;
        const std::vector<Rec> original = dxf_structure::records_of_file(file_bytes(path));
        for (DxfFormat format : both_formats) {
            const std::string what = std::string(c.file) + " " + name_of(format);
            hdxf::Drawing d;
            hdxf_test::assert_true(d.load(path), what + ": " + d.last_error());
            d.add_layer("HDXF_ADDED");
            d.add_line({0, 0, 0}, {3, 4, 0}, "HDXF_ADDED");
            // The new dimension and leader use the style the file has (a file may have no STANDARD: small_r14's
            // only style is ISO-25; HDxf's default name "Standard" would name no record there)
            std::string style = "Standard";
            bool has_standard = false;
            for (const auto& s : d.dim_styles()) has_standard = has_standard || upper(s.name) == "STANDARD";
            if (!has_standard && !d.dim_styles().empty()) style = d.dim_styles().front().name;
            hdxf::Entity& dim = d.add_dimension(hdxf::DimensionType::Aligned, {34, 38, 0}, {17, 19, 0});
            dim.as_dimension().def_point2 = {0, 0, 0};
            dim.as_dimension().def_point3 = {40, 30, 0};
            dim.as_dimension().dim_style = style;
            d.add_text({1, 2, 0}, "added", 2.0);
            hdxf::Entity& mtext = d.add_mtext({5, 5, 0}, "note", 2.5);
            d.add_leader({{0, 0, 0}, {2, 2, 0}}, &mtext);
            d.entities().back().as_leader().dim_style = style;
            std::vector<Rec> recs = dxf_structure::records_of_file(save(d, format, what));
            dxf_structure::OlderCheck check;
            check.version = d.version();
            check.owner_groups = c.owners;
            check.new_drawing = false;
            check.original = &original;
            dxf_structure::check_older_structure(recs, check, what);

            // The new records are there, and the names they use are records of the file
            Symbols s = symbols_of(recs);
            expect_in(s.layers, "HDXF_ADDED", what);
            expect_in(s.layers, "DEFPOINTS", what);
            expect_in(s.linetypes, "BYBLOCK", what);
            bool added_dimension_block = false;
            for (const auto& r : recs) {
                if (r.section == "BLOCKS" && r.type() == "BLOCK" && r.value(2).compare(0, 2, "*D") == 0) added_dimension_block = true;
            }
            hdxf_test::assert_true(added_dimension_block, what + ": the dimension's block");
        }
    }
    if (checked == 0) hdxf_test::skip("no R13/R14 test file");
}

// A read R13/R14 IMAGEDEF has no owner group: AutoCAD lists its IMAGEDEF_REACTORs first and ACAD_IMAGE_DICT
// last, and the owner is the dictionary that holds it, not the first reactor
TEST(integration_r13_r14_read_imagedef_is_owned_by_its_dictionary) {
    const char* files[] = {"altera_yeager_io_r14_imagedef.dxf", "altera_piranha_io_group_r13_imagedef.dxf"};
    int checked = 0;
    int with_several_reactors = 0;
    for (const char* file : files) {
        std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "local-r13r14" / file;
        if (!std::filesystem::exists(path)) continue;
        ++checked;
        hdxf::Drawing d;
        hdxf_test::assert_true(d.load(path), std::string(file) + ": " + d.last_error());
        int defs = 0;
        for (const auto& o : d.objects()) {
            if (o.type == hdxf::ObjectType::ImageDefReactor) {
                hdxf_test::assert_eq(o.owner, o.as_image_def_reactor().image_handle, std::string(file) + ": a reactor is owned by its image");
            }
            if (o.type != hdxf::ObjectType::ImageDef) continue;
            ++defs;
            const hdxf::Object* owner = d.find_object(o.owner);
            hdxf_test::assert_true(owner != nullptr && owner->type == hdxf::ObjectType::Dictionary, std::string(file) + ": the owner of an IMAGEDEF is a dictionary");
            bool holds = false;
            if (owner) for (const auto& e : owner->as_dictionary().entries) holds = holds || e.handle == o.handle;
            hdxf_test::assert_true(holds, std::string(file) + ": the dictionary holds the IMAGEDEF");
            if (o.reactors.size() > 1) {
                ++with_several_reactors;
                hdxf_test::assert_eq(o.owner, o.reactors.back(), std::string(file) + ": ACAD_IMAGE_DICT is the last reactor and the owner");
            }
        }
        hdxf_test::assert_true(defs > 0, std::string(file) + ": has an IMAGEDEF");
        const std::vector<Rec> original = dxf_structure::records_of_file(file_bytes(path));
        // written again unchanged: the same
        for (DxfFormat format : both_formats) {
            hdxf::Drawing again;
            hdxf_test::assert_true(again.load(path), file);
            std::vector<uint8_t> first = save(again, format, file);
            hdxf::Drawing second;
            hdxf_test::assert_true(second.load_from_memory(first.data(), first.size()), second.last_error());
            std::vector<uint8_t> twice = save(second, format, file);
            hdxf_test::assert_true(first == twice, std::string(file) + ": written twice is the same");
            dxf_structure::OlderCheck check;
            check.version = again.version();
            check.new_drawing = false;
            check.original = &original;
            dxf_structure::check_older_structure(dxf_structure::records_of_file(first), check, std::string(file) + " " + name_of(format));
        }
    }
    if (checked == 0) hdxf_test::skip("no R13/R14 file with images");
    else hdxf_test::assert_true(with_several_reactors > 0, "a file has an IMAGEDEF with the reactors before the dictionary");
}
