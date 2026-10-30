#include "test_framework.hpp"
#include "hdxf.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The version errors of DXF R13 and R14 (plan phase 7, step 7.20, B94, B98): the full traversal before
// anything is changed — model space, paper space, every block, each INSERT's attributes, the tables, the
// header and the objects — in a drawing built in memory and in one read from an R13 or R14 file (where
// only what the program added or changed is a fault), the first version that has what R12, R13 and R14
// lack, one order of checks, and a failed save that leaves the drawing, its handles, its file name and
// the destination as they were. No golden files.
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;

const DxfVersion older_versions[] = {DxfVersion::R13, DxfVersion::R14};
const DxfFormat both_formats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfVersion v) { return v == DxfVersion::R13 ? "R13" : "R14"; }
std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

std::filesystem::path external(const std::string& folder, const std::string& file) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / folder / file;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::filesystem::path temp_file(const std::string& name) {
    return std::filesystem::temp_directory_path() / name;
}

// The error of a save (as memory, ASCII): empty when it succeeds
std::string save_error(hdxf::Drawing& dwg, DxfFormat format = DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    return out.empty() ? dwg.last_error() : std::string();
}

hdxf::Drawing read_back(const std::vector<uint8_t>& bytes) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), dwg.last_error());
    return dwg;
}

// A drawing made as `version`, saved and read: the way a program gets a drawing from an R13 or R14 file
hdxf::Drawing read_from_file_made_as(DxfVersion version, const std::function<void(hdxf::Drawing&)>& build,
                                     DxfFormat format = DxfFormat::Ascii) {
    hdxf::Drawing made(version);
    build(made);
    std::vector<uint8_t> bytes = made.save_to_memory(format);
    hdxf_test::assert_false(bytes.empty(), made.last_error());
    hdxf::Drawing dwg = read_back(bytes);
    hdxf_test::assert_true(dwg.version() == version, "read as its version");
    return dwg;
}

hdxf::Entity text_entity_with(const std::function<void(hdxf::EntityProperties&)>& set) {
    hdxf::Drawing scratch(DxfVersion::R14);
    hdxf::Entity e = scratch.add_line({0, 0, 0}, {1, 1, 0});
    set(e.props);
    return e;
}

std::string handseed_of(const hdxf::Drawing& dwg) {
    const hdxf::HeaderVar* h = dwg.find_header("$HANDSEED");
    return h && !h->values.empty() ? h->values[0].second : std::string();
}

// Everything a failed save must leave: every handle of the drawing, $HANDSEED and the sizes of its lists
struct Snapshot {
    std::vector<std::string> handles;
    std::string handseed;
    size_t entities = 0, blocks = 0, objects = 0, layers = 0, linetypes = 0, styles = 0, dim_styles = 0;
    bool operator==(const Snapshot& o) const {
        return handles == o.handles && handseed == o.handseed && entities == o.entities && blocks == o.blocks &&
               objects == o.objects && layers == o.layers && linetypes == o.linetypes && styles == o.styles &&
               dim_styles == o.dim_styles;
    }
};

Snapshot snapshot(const hdxf::Drawing& dwg) {
    Snapshot s;
    auto entity = [&](const hdxf::Entity& e) {
        s.handles.push_back(e.props.handle);
        if (e.type == hdxf::EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) s.handles.push_back(a.props.handle);
            s.handles.push_back(e.as_insert().seqend_handle);
        }
        if (e.type == hdxf::EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) s.handles.push_back(v.handle);
        }
    };
    for (const auto& e : dwg.entities()) entity(e);
    for (const auto& b : dwg.blocks()) {
        s.handles.push_back(b.handle);
        s.handles.push_back(b.record.handle);
        s.handles.push_back(b.endblk_handle);
        for (const auto& e : b.entities) entity(e);
    }
    for (const auto& o : dwg.objects()) s.handles.push_back(o.handle);
    for (const auto& l : dwg.layers()) s.handles.push_back(l.handle);
    for (const auto& l : dwg.linetypes()) s.handles.push_back(l.handle);
    for (const auto& t : dwg.text_styles()) s.handles.push_back(t.handle);
    for (const auto& d : dwg.dim_styles()) s.handles.push_back(d.handle);
    s.handseed = handseed_of(dwg);
    s.entities = dwg.entities().size();
    s.blocks = dwg.blocks().size();
    s.objects = dwg.objects().size();
    s.layers = dwg.layers().size();
    s.linetypes = dwg.linetypes().size();
    s.styles = dwg.text_styles().size();
    s.dim_styles = dwg.dim_styles().size();
    return s;
}

// What a drawing built in memory cannot have in R13 or R14, with the first version that has it
struct Fault {
    const char* what;
    std::function<void(hdxf::Drawing&)> add;
    std::string error;
};

std::vector<Fault> faults_of_a_new_drawing() {
    std::vector<Fault> out;
    out.push_back({"entity lineweight", [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.lineweight = 25; },
                   "Lineweight requires DXF 2000 or later"});
    out.push_back({"entity ByBlock lineweight", [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.lineweight = -2; },
                   "Lineweight requires DXF 2000 or later"});
    out.push_back({"entity plot style", [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.plot_style_type = 1; },
                   "Plot style requires DXF 2000 or later"});
    out.push_back({"entity plot style handle",
                   [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.plot_style_handle = "ABC"; },
                   "Plot style requires DXF 2000 or later"});
    out.push_back({"lineweight in a block",
                   [](hdxf::Drawing& d) {
                       hdxf::Block& b = d.add_block("BLK");
                       b.entities.push_back(text_entity_with([](hdxf::EntityProperties& p) { p.lineweight = 50; }));
                   },
                   "Lineweight requires DXF 2000 or later"});
    out.push_back({"lineweight in a nested block's insert attribute",
                   [](hdxf::Drawing& d) {
                       hdxf::Block& b = d.add_block("NEST");
                       hdxf::Entity ins;
                       ins.type = hdxf::EntityType::Insert;
                       hdxf::InsertData data;
                       data.block_name = "NEST2";
                       hdxf::InsertAttribute a;
                       a.data.tag = "T";
                       a.data.value = "V";
                       a.props.lineweight = 13;
                       data.attributes.push_back(a);
                       ins.data = data;
                       b.entities.push_back(ins);
                       d.add_block("NEST2");
                   },
                   "Lineweight requires DXF 2000 or later"});
    out.push_back({"plot style in an INSERT's attribute",
                   [](hdxf::Drawing& d) {
                       d.add_block("B1");
                       hdxf::Entity& ins = d.add_insert("B1", {0, 0, 0});
                       hdxf::InsertAttribute a;
                       a.data.tag = "T";
                       a.props.plot_style_handle = "1F";
                       ins.as_insert().attributes.push_back(a);
                   },
                   "Plot style requires DXF 2000 or later"});
    out.push_back({"attribute version", [](hdxf::Drawing& d) {
                       d.add_block("B1");
                       hdxf::Entity& ins = d.add_insert("B1", {0, 0, 0});
                       hdxf::InsertAttribute a;
                       a.data.tag = "T";
                       a.data.version = 1;
                       ins.as_insert().attributes.push_back(a);
                   },
                   "An attribute's version requires DXF 2010 or later"});
    out.push_back({"block lineweight", [](hdxf::Drawing& d) { d.add_block("B1").lineweight = 20; },
                   "Lineweight requires DXF 2000 or later"});
    out.push_back({"block description", [](hdxf::Drawing& d) { d.add_block("B1").description = "text"; },
                   "A block's description requires DXF 2000 or later"});
    out.push_back({"transparency", [](hdxf::Drawing& d) {
                       d.add_line({0, 0, 0}, {1, 0, 0}).props.transparency = hdxf::Transparency::from_alpha(100);
                   },
                   "Transparency requires DXF 2004 or later"});
    out.push_back({"true color", [](hdxf::Drawing& d) {
                       d.add_line({0, 0, 0}, {1, 0, 0}).props.color = hdxf::Color::from_rgb(10, 20, 30);
                   },
                   "True color requires DXF 2004 or later"});
    out.push_back({"material", [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.material_handle = "AB"; },
                   "A material requires DXF 2007 or later"});
    out.push_back({"layer lineweight", [](hdxf::Drawing& d) { d.add_layer("A").lineweight = 50; },
                   "Lineweight requires DXF 2000 or later"});
    out.push_back({"layer plot style", [](hdxf::Drawing& d) { d.add_layer("A").plot_style_handle = "ABC"; },
                   "Plot style requires DXF 2000 or later"});
    out.push_back({"layer plot flag", [](hdxf::Drawing& d) { d.add_layer("A").plot = false; },
                   "Layer \"A\": the plot flag requires DXF 2000 or later"});
    out.push_back({"layer transparency", [](hdxf::Drawing& d) {
                       d.add_layer("A").transparency = hdxf::Transparency::from_alpha(50);
                   },
                   "Layer \"A\": transparency requires DXF 2000 or later"});
    out.push_back({"dimension style variable", [](hdxf::Drawing& d) { d.add_dim_style("D").dimaltrnd = 0.5; },
                   "Dimension style \"D\": DIMALTRND requires DXF 2000 or later"});
    out.push_back({"header variable", [](hdxf::Drawing& d) { d.set_header_int("$CELWEIGHT", 370, -1); },
                   "$CELWEIGHT requires DXF 2000 or later"});
    out.push_back({"a layout object", [](hdxf::Drawing& d) {
                       hdxf::Object o;
                       o.type = hdxf::ObjectType::Layout;
                       o.data = hdxf::LayoutData{};
                       d.objects().push_back(o);
                   },
                   "LAYOUT requires DXF 2000 or later"});
    out.push_back({"a page setup object", [](hdxf::Drawing& d) {
                       hdxf::Object o;
                       o.type = hdxf::ObjectType::PlotSettings;
                       o.data = hdxf::PlotSettings{};
                       d.objects().push_back(o);
                   },
                   "PLOTSETTINGS requires DXF 2000 or later"});
    out.push_back({"a placeholder object", [](hdxf::Drawing& d) {
                       hdxf::Object o;
                       o.type = hdxf::ObjectType::Placeholder;
                       d.objects().push_back(o);
                   },
                   "ACDBPLACEHOLDER requires DXF 2000 or later"});
    out.push_back({"a multileader style object", [](hdxf::Drawing& d) { d.add_mleader_style("MLS"); },
                   "MLEADERSTYLE requires DXF 2000 or later"});
    out.push_back({"a color book object", [](hdxf::Drawing& d) {
                       hdxf::Object o;
                       o.type = hdxf::ObjectType::DbColor;
                       o.data = hdxf::DbColorData{};
                       d.objects().push_back(o);
                   },
                   "DBCOLOR requires DXF 2004 or later"});
    out.push_back({"a sun object", [](hdxf::Drawing& d) {
                       hdxf::Object o;
                       o.type = hdxf::ObjectType::Sun;
                       d.objects().push_back(o);
                   },
                   "SUN requires DXF 2007 or later"});
    out.push_back({"a viewport's clip boundary", [](hdxf::Drawing& d) {
                       d.add_viewport({0, 0, 0}, 10, 10).as_viewport().clip_boundary_handle = "AA";
                   },
                   "A viewport's clip boundary requires DXF 2000 or later"});
    out.push_back({"a viewport's own UCS", [](hdxf::Drawing& d) {
                       d.add_viewport({0, 0, 0}, 10, 10).as_viewport().ucs_per_viewport = !hdxf::ViewportData{}.ucs_per_viewport;
                   },
                   "A viewport's own UCS requires DXF 2000 or later"});
    return out;
}

} // namespace

// Every fault a new drawing can have, in R13 and R14, ASCII and binary: the exact error, nothing written,
// no handle given, $HANDSEED, the lists and the destination file as they were
TEST(versions_r13_r14_a_new_drawing_names_the_first_version) {
    for (const Fault& f : faults_of_a_new_drawing()) {
        for (DxfVersion v : older_versions) {
            for (DxfFormat format : both_formats) {
                const std::string what = std::string(f.what) + " as " + name_of(v) + " " + name_of(format);
                hdxf::Drawing dwg(v);
                dwg.add_line({0, 0, 0}, {1, 1, 0});
                f.add(dwg);
                const Snapshot before = snapshot(dwg);
                const std::filesystem::path path = temp_file("hdxf_j_fault.dxf");
                std::filesystem::remove(path);

                hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": saved");
                hdxf_test::assert_eq(f.error, dwg.last_error(), what);
                hdxf_test::assert_false(dwg.save_as(path, format), what + ": save_as");
                hdxf_test::assert_eq(f.error, dwg.last_error(), what + ": save_as error");
                hdxf_test::assert_false(std::filesystem::exists(path), what + ": a file was made");
                hdxf_test::assert_true(dwg.warnings().empty(), what + ": warnings");
                hdxf_test::assert_true(before == snapshot(dwg), what + ": the drawing changed");
                hdxf_test::assert_eq("No filename set", [&] { dwg.save(); return dwg.last_error(); }(),
                                     what + ": the file name changed");
            }
        }
    }
}

// An older error names R13 or R14, the version that has the type, for R12; the types of 2000 and later
// keep 2000. R13 has what R14 adds (format profile 12)
TEST(versions_r13_r14_types_name_the_first_version) {
    struct Type {
        const char* name;
        const char* first;   // R13, R14 or 2000
        std::function<void(hdxf::Drawing&)> add;
    };
    const Type types[] = {
        {"ELLIPSE", "R13", [](hdxf::Drawing& d) { d.add_ellipse({0, 0, 0}, {1, 0, 0}, 0.5); }},
        {"XLINE", "R13", [](hdxf::Drawing& d) { d.add_xline({0, 0, 0}, {1, 0, 0}); }},
        {"RAY", "R13", [](hdxf::Drawing& d) { d.add_ray({0, 0, 0}, {1, 0, 0}); }},
        {"MTEXT", "R13", [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "M"); }},
        {"TOLERANCE", "R13", [](hdxf::Drawing& d) { d.add_tolerance({0, 0, 0}, "{\\Fgdt;j}"); }},
        {"LEADER", "R13", [](hdxf::Drawing& d) { d.add_leader({{0, 0, 0}, {1, 1, 0}}); }},
        {"SPLINE", "R13", [](hdxf::Drawing& d) { d.add_spline({{0, 0, 0}, {1, 1, 0}, {2, 0, 0}, {3, 1, 0}}); }},
        {"MLINE", "R13", [](hdxf::Drawing& d) { d.add_mline({{0, 0, 0}, {1, 0, 0}}); }},
        {"3DSOLID", "R13", [](hdxf::Drawing& d) { d.add_acis(hdxf::EntityType::Solid3D, {}); }},
        {"REGION", "R13", [](hdxf::Drawing& d) { d.add_acis(hdxf::EntityType::Region, {}); }},
        {"BODY", "R13", [](hdxf::Drawing& d) { d.add_acis(hdxf::EntityType::Body, {}); }},
        {"LWPOLYLINE", "R14", [](hdxf::Drawing& d) { d.add_lwpolyline({{0, 0}, {1, 1}}); }},
        {"HATCH", "R14", [](hdxf::Drawing& d) { d.add_hatch({{0, 0}, {1, 0}, {1, 1}}); }},
        {"IMAGE", "R14", [](hdxf::Drawing& d) { d.add_image("a.png", {0, 0, 0}, {10, 10}, 1.0); }},
        {"WIPEOUT", "2000", [](hdxf::Drawing& d) { d.add_wipeout({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}}); }},
        {"MESH", "2000", [](hdxf::Drawing& d) { d.add_mesh({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}, {{0, 1, 2}}, 0); }},
    };
    for (const Type& t : types) {
        const std::string first = t.first;
        for (bool in_block : {false, true}) {
            auto build = [&](hdxf::Drawing& d) {
                if (!in_block) {
                    t.add(d);
                    return;
                }
                hdxf::Drawing source(DxfVersion::R2000);
                t.add(source);
                d.add_block("HOLDER").entities.push_back(source.entities().front());
            };
            const std::string expected = std::string(t.name) + " requires DXF " + first + " or later";
            const std::string where = std::string(t.name) + (in_block ? " in a block" : "");

            hdxf::Drawing r12;
            build(r12);
            hdxf_test::assert_eq(expected, save_error(r12), where + " as R12");

            hdxf::Drawing r13(DxfVersion::R13);
            build(r13);
            std::string e13 = save_error(r13);
            if (first == "R13") {
                // R13 has it: the error here is not the type's (an empty ACIS body has no SAT, for one)
                hdxf_test::assert_true(e13.find("requires DXF R13") == std::string::npos, where + " as R13: " + e13);
            } else {
                hdxf_test::assert_eq(expected, e13, where + " as R13");
            }

            hdxf::Drawing r14(DxfVersion::R14);
            build(r14);
            std::string e14 = save_error(r14);
            if (first == "2000") {
                hdxf_test::assert_eq(expected, e14, where + " as R14");
            } else {
                hdxf_test::assert_true(e14.find(std::string(t.name) + " requires") == std::string::npos,
                                       where + " as R14: " + e14);
            }
        }
    }
}

// R12's other diagnostics: a layer's XDATA is R13's, its transparency 2000's; a text style's XDATA R13's
TEST(versions_r13_r14_r12_diagnostics) {
    {
        hdxf::Drawing r12;
        r12.add_layer("A").xdata.push_back({"HDXF_TEST", {{1000, "x"}}});
        hdxf_test::assert_eq("Layer \"A\": layer XDATA requires DXF R13 or later", save_error(r12));
        for (DxfVersion v : older_versions) {
            r12.set_version(v);
            hdxf_test::assert_eq("", save_error(r12), "the XDATA of a layer in " + name_of(v));
            r12.set_version(DxfVersion::R12);
        }
    }
    {
        hdxf::Drawing r12;
        r12.add_text_style("S").xdata.push_back({"HDXF_TEST", {{1000, "x"}}});
        hdxf_test::assert_eq("Text style \"S\": text style XDATA requires DXF R13 or later", save_error(r12));
        r12.set_version(DxfVersion::R14);
        hdxf_test::assert_eq("", save_error(r12));
    }
    {
        // R13 and R14 have no place for a layer's transparency: an error, not a lost value
        for (DxfVersion v : older_versions) {
            hdxf::Drawing d(v);
            d.add_layer("Glass").transparency = hdxf::Transparency::from_alpha(127);
            hdxf_test::assert_eq("Layer \"Glass\": transparency requires DXF 2000 or later", save_error(d), name_of(v));
        }
    }
}

// What a drawing read from R13 or R14 has is written from its preserved record and is no fault; what the
// program adds or changes is one
TEST(versions_r13_r14_a_read_file_only_the_added_is_a_fault) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            auto build = [](hdxf::Drawing& d) {
                d.add_layer("WALLS");
                hdxf::Block& b = d.add_block("BLK");
                b.entities.push_back(text_entity_with([](hdxf::EntityProperties&) {}));
                d.add_line({0, 0, 0}, {5, 5, 0}, "WALLS");
                hdxf::Entity& ins = d.add_insert("BLK", {1, 1, 0});
                hdxf::InsertAttribute a;
                a.data.tag = "T";
                a.data.value = "v";
                ins.as_insert().attributes.push_back(a);
            };

            // The unchanged file saves
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                hdxf_test::assert_eq("", save_error(dwg, format), what + ": unchanged");
            }
            // An added entity with a lineweight
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                dwg.add_circle({0, 0, 0}, 3).props.lineweight = 35;
                const Snapshot before = snapshot(dwg);
                hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg, format), what + ": added");
                hdxf_test::assert_true(before == snapshot(dwg), what + ": changed by a failed save");
                dwg.entities().back().props.lineweight = -1;
                hdxf_test::assert_eq("", save_error(dwg, format), what + ": after the fix");
            }
            // A lineweight put on an entity of the file
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                dwg.entities().front().props.lineweight = 25;
                hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg, format), what + ": changed");
            }
            // A plot style on an attribute of the file's INSERT
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                hdxf::Entity* insert = nullptr;
                for (auto& e : dwg.entities()) if (e.type == hdxf::EntityType::Insert) insert = &e;
                hdxf_test::assert_true(insert && !insert->as_insert().attributes.empty(), "the INSERT has its attribute");
                insert->as_insert().attributes[0].props.plot_style_handle = "ABC";
                hdxf_test::assert_eq("Plot style requires DXF 2000 or later", save_error(dwg, format), what + ": attribute");
            }
            // An entity added to a block of the file
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                hdxf::Block* block = dwg.find_block("BLK");
                hdxf_test::assert_true(block != nullptr, "the block");
                block->entities.push_back(text_entity_with([](hdxf::EntityProperties& p) { p.lineweight = 30; }));
                hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg, format), what + ": in a block");
            }
            // A layer with a lineweight, a layer with a space in its name, a layout object
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                dwg.add_layer("NEW").lineweight = 40;
                hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg, format), what + ": layer");
                dwg.layers().back().lineweight = -3;
                dwg.layers().back().name = "NEW LAYER";
                hdxf_test::assert_true(save_error(dwg, format).find("Layer name \"NEW LAYER\" has the character \" \"") == 0,
                                       what + ": " + dwg.last_error());
                dwg.layers().back().name = "NEW_LAYER";
                hdxf_test::assert_eq("", save_error(dwg, format), what + ": the layer, fixed");
            }
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                hdxf::Object o;
                o.type = hdxf::ObjectType::Layout;
                o.data = hdxf::LayoutData{};
                dwg.objects().push_back(o);
                hdxf_test::assert_eq("LAYOUT requires DXF 2000 or later", save_error(dwg, format), what + ": layout");
            }
            // A newer type added to a read R13 file, and to a read R14 file (the R14 one has LWPOLYLINE)
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                dwg.add_lwpolyline({{0, 0}, {1, 1}});
                if (v == DxfVersion::R13) {
                    hdxf_test::assert_eq("LWPOLYLINE requires DXF R14 or later", save_error(dwg, format), what + ": LWPOLYLINE");
                } else {
                    hdxf_test::assert_eq("", save_error(dwg, format), what + ": LWPOLYLINE");
                }
            }
            // A conversion is still the conversion error, before anything
            {
                hdxf::Drawing dwg = read_from_file_made_as(v, build, format);
                dwg.set_version(DxfVersion::R2000);
                hdxf_test::assert_true(save_error(dwg, format).find("is not supported yet") != std::string::npos, what);
            }
        }
    }
}

// AutoCAD's own R13/R14 files that are of the AutoCAD 2000 family have 2000 things in them: a LAYOUT
// object (small_r13.dxf), a lineweight (Zurn_59251). Written unchanged they are no fault; what is
// added or changed is.
TEST(versions_r13_r14_a_read_file_keeps_its_2000_things) {
    if (!std::filesystem::exists(external("ezdxf", "small_r13.dxf"))) hdxf_test::skip("ezdxf/small_r13.dxf is not there");
    {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(external("ezdxf", "small_r13.dxf")), dwg.last_error());
        hdxf_test::assert_eq("", save_error(dwg), "small_r13 unchanged");
        hdxf::Object o;
        o.type = hdxf::ObjectType::Layout;
        o.data = hdxf::LayoutData{};
        dwg.objects().push_back(o);
        hdxf_test::assert_eq("LAYOUT requires DXF 2000 or later", save_error(dwg), "a LAYOUT added to small_r13");
        dwg.objects().pop_back();
        dwg.add_line({0, 0, 0}, {1, 1, 0}).props.lineweight = 25;
        hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg), "a lineweight added to small_r13");
    }
}

// A file of the AutoCAD 2000 family (AutoCAD 2002 … 2005 saving as R14) has a lineweight (370) on an entity: the
// entity is written from its record and is no fault; changing the lineweight, or another entity with one, is
TEST(versions_r13_r14_a_read_entity_with_a_lineweight) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing made(v);
            made.add_line({0, 0, 0}, {1, 1, 0});
            made.add_circle({0, 0, 0}, 2);
            std::string text = std::string(
                [&] { std::vector<uint8_t> b = made.save_to_memory(); return std::string(b.begin(), b.end()); }());
            const std::string after = "AcDbEntity\r\n  8\r\n0\r\n100\r\nAcDbLine";
            const size_t at = text.find(after);
            hdxf_test::assert_true(at != std::string::npos, what + ": the LINE");
            text.insert(at + std::string("AcDbEntity\r\n  8\r\n0\r\n").size(), "370\r\n    25\r\n");
            std::vector<uint8_t> file(text.begin(), text.end());
            hdxf::Drawing ascii = read_back(file);
            hdxf_test::assert_eq(25, ascii.entities()[0].props.lineweight, what + ": read");
            // (the same file as binary)
            std::vector<uint8_t> binary_bytes = ascii.save_to_memory(DxfFormat::Binary);
            hdxf_test::assert_false(binary_bytes.empty(), what + ": " + ascii.last_error());
            hdxf::Drawing dwg = format == DxfFormat::Ascii ? read_back(file) : read_back(binary_bytes);

            hdxf_test::assert_eq(25, dwg.entities()[0].props.lineweight, what + ": read");
            hdxf_test::assert_eq("", save_error(dwg, format), what + ": unchanged");
            dwg.entities()[0].props.lineweight = 30;
            hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg, format), what + ": changed");
            dwg.entities()[0].props.lineweight = 25;
            hdxf_test::assert_eq("", save_error(dwg, format), what + ": back as it was");
            dwg.entities()[1].props.lineweight = 25;
            hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg, format), what + ": the other entity");
            dwg.entities()[1].props.lineweight = -1;
            dwg.add_line({0, 0, 0}, {2, 2, 0}).props.lineweight = 25;
            hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", save_error(dwg, format), what + ": an added one with the same");
        }
    }
}

// A failed save of a drawing that was saved before (its handles are given): everything stays, the file
// that was there stays, and after the fix the same drawing saves
TEST(versions_r13_r14_a_failed_save_after_a_good_one) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            const std::filesystem::path path = temp_file("hdxf_j_after_good.dxf");
            hdxf::Drawing dwg(v);
            dwg.add_layer("WALLS");
            dwg.add_block("BLK").entities.push_back(text_entity_with([](hdxf::EntityProperties&) {}));
            dwg.add_insert("BLK", {1, 2, 0});
            dwg.add_line({0, 0, 0}, {5, 5, 0}, "WALLS");
            hdxf_test::assert_true(dwg.save_as(path, format), what + ": " + dwg.last_error());
            const std::vector<uint8_t> file = read_bytes(path);
            const Snapshot before = snapshot(dwg);
            hdxf_test::assert_false(before.handles.empty(), "handles");

            hdxf::Entity& bad = dwg.add_line({1, 1, 0}, {2, 2, 0});
            bad.props.lineweight = 70;
            hdxf_test::assert_false(dwg.save(format), what + ": save()");
            hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", dwg.last_error(), what);
            hdxf_test::assert_true(read_bytes(path) == file, what + ": the file");
            dwg.entities().pop_back();
            hdxf_test::assert_true(before == snapshot(dwg), what + ": the drawing");
            hdxf_test::assert_true(dwg.save(format), what + ": after the fix: " + dwg.last_error());
            hdxf_test::assert_true(read_bytes(path) == file, what + ": the same file again");
            std::filesystem::remove(path);
        }
    }
}

// A failure found by the writer itself (a reference to a record that is not there), after the checks: the
// handles it gave are taken back and the drawing is as it was
TEST(versions_r13_r14_a_failure_inside_the_writer_changes_nothing) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.add_line({0, 0, 0}, {1, 1, 0});
            hdxf::MlineData& mline = dwg.add_mline({{0, 0, 0}, {1, 0, 0}}).as_mline();
            mline.style = "NOSUCH";
            mline.style_handle.clear();
            const Snapshot before = snapshot(dwg);
            hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": saved");
            hdxf_test::assert_true(dwg.last_error() == "MLINE style \"NOSUCH\" is not in ACAD_MLINESTYLE", what + ": " + dwg.last_error());
            hdxf_test::assert_true(before == snapshot(dwg), what + ": the drawing changed");
            dwg.entities().back().as_mline().style = "STANDARD";
            hdxf_test::assert_eq("", save_error(dwg, format), what + ": fixed");
        }
    }
}

// A failed save_as leaves the file name: save() writes where the drawing was loaded from
TEST(versions_r13_r14_a_failed_save_as_keeps_the_file_name) {
    for (DxfVersion v : older_versions) {
        const std::filesystem::path original = temp_file("hdxf_j_name_original.dxf");
        const std::filesystem::path other = temp_file("hdxf_j_name_other.dxf");
        std::filesystem::remove(other);
        {
            hdxf::Drawing made(v);
            made.add_line({0, 0, 0}, {1, 1, 0});
            hdxf_test::assert_true(made.save_as(original), made.last_error());
        }
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(original), dwg.last_error());
        dwg.add_line({0, 0, 0}, {2, 2, 0}).props.lineweight = 20;
        hdxf_test::assert_false(dwg.save_as(other), name_of(v));
        hdxf_test::assert_false(std::filesystem::exists(other), "no file at the new name");
        dwg.entities().back().props.lineweight = -1;
        hdxf_test::assert_true(dwg.save(), dwg.last_error());
        hdxf_test::assert_false(std::filesystem::exists(other), "save() writes the loaded file's name");
        hdxf::Drawing back;
        hdxf_test::assert_true(back.load(original), back.last_error());
        hdxf_test::assert_eq(2, static_cast<int>(back.entities().size()), "the added entity is in the original file");
        std::filesystem::remove(original);
    }
}

// warnings() and last_error() are cleared by every call: a failed save has the error and no warnings, the
// one after it has neither
TEST(versions_r13_r14_errors_and_warnings_are_cleared_by_each_call) {
    hdxf::Drawing dwg(DxfVersion::R14);
    dwg.add_text_style("NOFONT", "no_such_font_hdxf.shx");
    dwg.add_text({0, 0, 0}, "Hello").as_text().style = "NOFONT";
    const std::vector<std::filesystem::path> paths = hdxf::font_paths();
    hdxf::set_font_paths({});
    struct Restore {
        std::vector<std::filesystem::path> paths;
        ~Restore() { hdxf::set_font_paths(paths); }
    } restore{paths};
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    hdxf_test::assert_false(dwg.warnings().empty(), "a font that was not found is a warning");
    dwg.add_line({0, 0, 0}, {1, 1, 0}).props.lineweight = 5;
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "fails");
    hdxf_test::assert_true(dwg.warnings().empty(), "the warnings of the call before are gone");
    hdxf_test::assert_false(dwg.last_error().empty(), "the error");
    dwg.entities().back().props.lineweight = -1;
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    hdxf_test::assert_eq("", dwg.last_error());
}

// The one order of the checks: the first fault wins, whatever else the drawing has
TEST(versions_r13_r14_the_order_of_the_checks) {
    for (DxfVersion v : older_versions) {
        const std::string what = name_of(v);
        auto layout = [](hdxf::Drawing& d) {
            hdxf::Object o;
            o.type = hdxf::ObjectType::Layout;
            o.data = hdxf::LayoutData{};
            d.objects().push_back(o);
        };
        auto entity = [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 1, 0}).props.lineweight = 5; };
        auto header = [](hdxf::Drawing& d) { d.set_header_int("$CELWEIGHT", 370, -1); };
        auto object = [](hdxf::Drawing& d) {
            hdxf::Object o;
            o.type = hdxf::ObjectType::Placeholder;
            d.objects().push_back(o);
        };
        auto dim_style = [](hdxf::Drawing& d) { d.add_dim_style("D").dimaltrnd = 0.5; };
        auto table = [](hdxf::Drawing& d) { d.add_layer("A B"); };
        auto acis = [](hdxf::Drawing& d) {
            hdxf::AcisData data;
            data.lines = {"2000 0 1 0"};
            d.add_acis(hdxf::EntityType::Solid3D, data);
        };
        struct Step { const char* name; std::function<void(hdxf::Drawing&)> add; std::string error; };
        const std::vector<Step> steps = {
            {"layout", layout, "LAYOUT requires DXF 2000 or later"},
            {"entity", entity, "Lineweight requires DXF 2000 or later"},
            {"header", header, "$CELWEIGHT requires DXF 2000 or later"},
            {"object", object, "ACDBPLACEHOLDER requires DXF 2000 or later"},
            {"dimension style", dim_style, "Dimension style \"D\": DIMALTRND requires DXF 2000 or later"},
            {"table", table, ""},
            {"acis", acis, ""},
        };
        // Each step with all the later ones is its own error; "table" and "acis" are checked by their text
        for (size_t first = 0; first < steps.size(); ++first) {
            hdxf::Drawing dwg(v);
            for (size_t i = first; i < steps.size(); ++i) steps[i].add(dwg);
            const std::string error = save_error(dwg);
            const std::string label = what + ": the first fault is " + steps[first].name;
            if (!steps[first].error.empty()) hdxf_test::assert_eq(steps[first].error, error, label);
            else if (std::string(steps[first].name) == "table") hdxf_test::assert_true(error.find("Layer name \"A B\"") == 0, label + ": " + error);
            else hdxf_test::assert_true(error.find("SAT") != std::string::npos, label + ": " + error);
        }
    }
}

// An R14 drawing the program built without anything R13 and R14 lack is written; the checks leave the
// types R13 and R14 have alone (nothing is refused that the version has)
TEST(versions_r13_r14_what_the_version_has_is_not_a_fault) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            hdxf::Drawing dwg(v);
            dwg.add_layer("WALLS", hdxf::Color::from_aci(1));
            dwg.add_linetype("DASHED", "Dashed", {0.5, -0.25});
            dwg.add_text_style("ROMAN", "romans");
            dwg.add_line({0, 0, 0}, {1, 1, 0}, "WALLS");
            dwg.add_ellipse({0, 0, 0}, {2, 0, 0}, 0.5);
            dwg.add_xline({0, 0, 0}, {1, 1, 0});
            dwg.add_ray({0, 0, 0}, {1, 0, 0});
            dwg.add_mtext({0, 0, 0}, "M");
            dwg.add_spline({{0, 0, 0}, {1, 1, 0}, {2, 0, 0}, {3, 1, 0}});
            dwg.add_mline({{0, 0, 0}, {1, 0, 0}});
            dwg.add_tolerance({0, 0, 0}, "{\\Fgdt;j}");
            dwg.add_leader({{0, 0, 0}, {1, 1, 0}});
            if (v == DxfVersion::R14) {
                dwg.add_lwpolyline({{0, 0}, {1, 1}});
                dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}});
                dwg.add_image("picture.png", {0, 0, 0}, {10, 10}, 5);
            }
            hdxf_test::assert_eq("", save_error(dwg, format), name_of(v) + " " + name_of(format));
        }
    }
}
