#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// ---------------------------------------------------------------------------
// MLINE (and MLINESTYLE), IMAGE (and IMAGEDEF, IMAGEDEF_REACTOR, RASTERVARIABLES) and
// 3DSOLID/REGION/BODY in DXF 2000, phase 1 step 1.8 (doc/plans/2026-10-23-fas-1-dxf-2000.md):
// read, built in memory as AutoCAD builds them, written as 2000, and the relations of
// a removed IMAGE (B19).
//
// Expected values are read from ACadSharp's AutoCAD sample, not from HDxf.
// ---------------------------------------------------------------------------

namespace {

using Groups = std::vector<std::pair<int, std::string>>;

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

const hdxf::Entity& entity(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return e;
    }
    hdxf_test::assert_true(false, "entity " + handle + " not found");
    return dwg.entities().front();
}

const hdxf::Object& object(const hdxf::Drawing& dwg, const std::string& handle) {
    const hdxf::Object* o = dwg.find_object(handle);
    hdxf_test::assert_true(o != nullptr, "object " + handle + " not found");
    return *o;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The groups of the record with this handle, from its "0" on (ASCII)
Groups record(const std::vector<uint8_t>& data, const std::string& handle) {
    std::string dxf(data.begin(), data.end());
    size_t pos = 0;
    auto line = [&]() {
        size_t end = dxf.find('\n', pos);
        std::string s = dxf.substr(pos, end - pos);
        pos = end == std::string::npos ? dxf.size() : end + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        return s;
    };
    Groups current;
    bool found = false;
    while (pos < dxf.size()) {
        int code = std::atoi(line().c_str());
        std::string value = line();
        if (code == 0) {
            if (found) break;
            current.clear();
        }
        current.push_back({code, dxf_structure::number_text(code, value)});
        if (code == 5 && value == handle && current.size() == 2) found = true;
    }
    return found ? current : Groups{};
}

std::vector<std::string> values(const Groups& g, int code) {
    std::vector<std::string> out;
    for (const auto& [c, v] : g) if (c == code) out.push_back(v);
    return out;
}

void near(double a, double b, const std::string& what, double tolerance = 1e-12) {
    hdxf_test::assert_true(std::fabs(a - b) < tolerance, what + ": " + std::to_string(a) + " != " +
                                                         std::to_string(b));
}

bool has_sample() {
    if (std::filesystem::exists(external("acadsharp/sample_AC1015_ascii.dxf"))) return true;
    hdxf_test::skip("external test file missing");
    return false;
}

void check_sample(const std::string& file) {
    std::filesystem::path path = external(file);
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("external test file missing");
        return;
    }
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(path), dwg.last_error());

    const hdxf::Entity& mline = entity(dwg, "38B");
    hdxf_test::assert_true(mline.type == hdxf::EntityType::Mline, "MLINE");
    const auto& m = mline.as_mline();
    hdxf_test::assert_eq(m.style, "STANDARD");
    hdxf_test::assert_eq(m.style_handle, "18");
    hdxf_test::assert_eq(m.scale, 1.5);
    hdxf_test::assert_eq(m.justification, 1);
    hdxf_test::assert_eq(m.flags, 1);
    hdxf_test::assert_eq(m.style_elements, 2);
    hdxf_test::assert_eq(static_cast<int>(m.vertices.size()), 8);
    near(m.vertices[1].position.y, 8.931727104928427, "vertex");
    near(m.vertices[1].miter.x, -0.7071067811865474, "miter");
    hdxf_test::assert_eq(static_cast<int>(m.vertices[1].elements.size()), 2);
    near(m.vertices[1].elements[0].params[0], 1.060660171779821, "element");
    hdxf_test::assert_true(m.vertices[1].elements[0].fill_params.empty(), "no fill");
    // A break in an element: four parameters
    hdxf_test::assert_eq(static_cast<int>(entity(dwg, "3A5").as_mline().vertices[0].elements[1].params.size()), 4);

    const hdxf::Object& style = object(dwg, "18");
    hdxf_test::assert_true(style.type == hdxf::ObjectType::MlineStyle, "MLINESTYLE");
    const auto& s = style.as_mline_style();
    hdxf_test::assert_eq(s.name, "STANDARD");
    hdxf_test::assert_eq(s.fill_color.aci, 256);
    hdxf_test::assert_eq(s.start_angle, 90.0);
    hdxf_test::assert_eq(static_cast<int>(s.elements.size()), 2);
    hdxf_test::assert_eq(s.elements[1].offset, -0.5);
    hdxf_test::assert_eq(s.elements[1].linetype, "BYLAYER");

    const hdxf::Entity& image = entity(dwg, "8C6");
    hdxf_test::assert_true(image.type == hdxf::EntityType::Image, "IMAGE");
    const auto& i = image.as_image();
    hdxf_test::assert_eq(i.insertion_point.y, 15.0);
    near(i.u_vector.x, 0.0104166666666667, "u");
    near(i.v_vector.y, 0.0104166666666667, "v");
    hdxf_test::assert_eq(i.size.x, 994.0);
    hdxf_test::assert_eq(i.size.y, 965.0);
    hdxf_test::assert_eq(i.image_def_handle, "8C4");
    hdxf_test::assert_eq(i.reactor_handle, "8C5");
    hdxf_test::assert_eq(i.display_flags, 7);
    hdxf_test::assert_eq(i.brightness, 50);
    hdxf_test::assert_eq(static_cast<int>(i.clip_boundary.size()), 2);
    near(i.clip_boundary[1].y, 964.4999999999999, "clip");

    const hdxf::Object& def = object(dwg, "8C4");
    hdxf_test::assert_true(def.type == hdxf::ObjectType::ImageDef, "IMAGEDEF");
    hdxf_test::assert_eq(def.as_image_def().file_name, ".\\image.JPG");
    hdxf_test::assert_eq(def.as_image_def().size.x, 994.0);
    near(def.as_image_def().pixel_size.x, 0.2645833333333333, "pixel");
    hdxf_test::assert_true(def.as_image_def().loaded, "loaded");
    hdxf_test::assert_eq(def.as_image_def().resolution_units, 5);
    hdxf_test::assert_true(def.reactors == std::vector<std::string>({"8C2", "8C5"}), "reactors");
    const hdxf::Object& reactor = object(dwg, "8C5");
    hdxf_test::assert_true(reactor.type == hdxf::ObjectType::ImageDefReactor, "IMAGEDEF_REACTOR");
    hdxf_test::assert_eq(reactor.owner, "8C6");
    hdxf_test::assert_eq(reactor.as_image_def_reactor().image_handle, "8C6");
    hdxf_test::assert_eq(reactor.as_image_def_reactor().class_version, 2);
    const auto& vars = object(dwg, "8C3").as_raster_variables();
    hdxf_test::assert_true(vars.frame, "frame");
    hdxf_test::assert_eq(vars.quality, 1);
    hdxf_test::assert_eq(vars.units, 5);

    const hdxf::Entity& solid = entity(dwg, "D65");
    hdxf_test::assert_true(solid.type == hdxf::EntityType::Solid3D, "3DSOLID");
    hdxf_test::assert_eq(solid.as_acis().version, 1);
    hdxf_test::assert_eq(static_cast<int>(solid.as_acis().lines.size()), 115);
    hdxf_test::assert_eq(solid.as_acis().lines[0], "koo nnm n o         ");
    hdxf_test::assert_eq(solid.as_acis().lines.back(), "/061+ {rn rmqj mqj rmqj |");
    hdxf_test::assert_true(entity(dwg, "D69").type == hdxf::EntityType::Region, "REGION");
    hdxf_test::assert_eq(static_cast<int>(entity(dwg, "D69").as_acis().lines.size()), 45);
}

} // namespace

TEST(mline_image_2000_read_ascii)  { check_sample("acadsharp/sample_AC1015_ascii.dxf"); }
TEST(mline_image_2000_read_binary) { check_sample("acadsharp/sample_AC1015_binary.dxf"); }

// add_mline() computes what AutoCAD stored for MLINE 38B: directions, miters, element
// parameters
TEST(mline_image_2000_mline_geometry_as_autocad) {
    if (!has_sample()) return;
    hdxf::Drawing autocad = load(read_bytes(external("acadsharp/sample_AC1015_ascii.dxf")));
    const auto& expected = entity(autocad, "38B").as_mline();
    std::vector<hdxf::Point3> points;
    for (const auto& v : expected.vertices) points.push_back(v.position);

    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    const auto& m = dwg.add_mline(points, false, 1.5, 1).as_mline();
    hdxf_test::assert_eq(m.style_elements, 2);
    hdxf_test::assert_false(m.style_handle.empty(), "the Standard style");
    for (size_t i = 0; i < points.size(); ++i) {
        std::string at = "vertex " + std::to_string(i);
        near(m.vertices[i].direction.x, expected.vertices[i].direction.x, at + " direction", 1e-15);
        near(m.vertices[i].direction.y, expected.vertices[i].direction.y, at + " direction", 1e-15);
        near(m.vertices[i].miter.x, expected.vertices[i].miter.x, at + " miter", 1e-15);
        near(m.vertices[i].miter.y, expected.vertices[i].miter.y, at + " miter", 1e-15);
        for (size_t k = 0; k < 2; ++k) {
            near(m.vertices[i].elements[k].params[0], expected.vertices[i].elements[k].params[0],
                 at + " element", 1e-14);
            near(m.vertices[i].elements[k].params[1], 0.0, at + " no break");
        }
    }
    // Justification top: the top element on the vertices (MLINE 3A5: 0 and -scale)
    const auto& top = dwg.add_mline({{0, 0, 0}, {0, 10, 0}}, false, 2.0, 0).as_mline();
    hdxf_test::assert_eq(top.vertices[0].elements[0].params[0], 0.0);
    hdxf_test::assert_eq(top.vertices[0].elements[1].params[0], -2.0);
}

// Built in memory: MLINE, IMAGE with its objects and classes, an ACIS solid copied from
// AutoCAD's file, as ASCII and binary
TEST(mline_image_2000_built) {
    hdxf::AcisData acis;
    acis.lines = {"koo nnm n o          ", "ni ^ *+0;:,4 ^ *+0\\^ [ mo", std::string(300, 'x') + "end"};
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_mline({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}}, true, 1.0, 1, "Standard");
        hdxf::Entity& image = dwg.add_image("C:\\bilder\\karta.png", {20, 0, 0}, {200, 100}, 40.0);
        std::string image_handle = image.props.handle;
        dwg.add_image("C:\\bilder\\karta.png", {20, 30, 0}, {200, 100}, 20.0);
        dwg.add_acis(hdxf::EntityType::Region, acis);

        std::vector<uint8_t> out = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "mline and image");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 4);
        const auto& m = back.entities()[0].as_mline();
        hdxf_test::assert_true(m.is_closed(), "closed");
        hdxf_test::assert_eq(static_cast<int>(m.vertices.size()), 3);
        near(m.vertices[1].miter.x, -0.7071067811865475, "miter", 1e-15);

        // One IMAGEDEF for the file, in ACAD_IMAGE_DICT as "karta"; both images react to it
        const auto& i = back.entities()[1].as_image();
        hdxf_test::assert_eq(i.size.x, 200.0);
        hdxf_test::assert_eq(i.u_vector.x, 0.2);
        hdxf_test::assert_eq(i.image_def_handle, back.entities()[2].as_image().image_def_handle);
        const hdxf::Object& def = object(back, i.image_def_handle);
        hdxf_test::assert_eq(def.as_image_def().file_name, "C:\\bilder\\karta.png");
        hdxf_test::assert_eq(static_cast<int>(def.reactors.size()), 3, "the dictionary and two reactors");
        const hdxf::Object& dict = object(back, def.owner);
        hdxf_test::assert_eq(dict.as_dictionary().entries.at(0).name, "karta");
        const hdxf::Object& reactor = object(back, i.reactor_handle);
        hdxf_test::assert_eq(reactor.owner, image_handle);
        hdxf_test::assert_eq(reactor.as_image_def_reactor().image_handle, image_handle);
        hdxf_test::assert_eq(def.reactors[1], i.reactor_handle);
        bool variables = false;
        for (const auto& o : back.objects()) variables = variables || o.type == hdxf::ObjectType::RasterVariables;
        hdxf_test::assert_true(variables, "RASTERVARIABLES");

        const auto& a = back.entities()[3];
        hdxf_test::assert_true(a.type == hdxf::EntityType::Region, "REGION");
        hdxf_test::assert_true(a.as_acis().lines == acis.lines, "ACIS lines as they were");
        if (format == hdxf::DxfFormat::Binary) continue;

        std::string dxf = hdxf_test::lf_text(out);
        for (const char* name : {"RASTERVARIABLES", "IMAGEDEF", "IMAGEDEF_REACTOR", "IMAGE"}) {
            hdxf_test::assert_true(dxf.find("  0\nCLASS\n  1\n" + std::string(name) + "\n") != std::string::npos,
                                   std::string("class ") + name);
        }
        Groups region = record(out, back.entities()[3].props.handle);
        std::vector<std::string> threes = values(region, 3);
        // AutoCAD's form: the long line's 255 first characters in 3, the rest in the 1 that ends it
        hdxf_test::assert_eq(static_cast<int>(threes.size()), 1, "the long line starts in 3");
        hdxf_test::assert_eq(static_cast<int>(threes[0].size()), 255);
        hdxf_test::assert_eq(threes[0] + values(region, 1).at(2), acis.lines[2]);
        hdxf_test::assert_eq(values(region, 1).at(1), "ni ^  *+0;:,4 ^  *+0\\^  [ mo", "^ is \"^ \"");
        Groups mline = record(out, back.entities()[0].props.handle);
        hdxf_test::assert_eq(values(mline, 340).at(0), object(back, values(mline, 340).at(0)).handle);
        hdxf_test::assert_true(object(back, values(mline, 340).at(0)).type == hdxf::ObjectType::MlineStyle,
                               "340: the style");
    }
    // A style that is not there
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_mline({{0, 0, 0}, {1, 0, 0}}, false, 1.0, 0, "NONE");
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "missing style");
    hdxf_test::assert_eq(dwg.last_error(), "MLINE style \"NONE\" is not in ACAD_MLINESTYLE");
}

// B19: IMAGE removed — its IMAGEDEF_REACTOR goes with it and the IMAGEDEF no longer has it
// as reactor; the IMAGEDEF stays (the image is still attached, as in AutoCAD)
TEST(mline_image_2000_image_removed) {
    if (!has_sample()) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_ascii.dxf"));
    hdxf::Drawing dwg = load(in);
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(),
                              [](const hdxf::Entity& e) { return e.props.handle == "8C6"; }),
               ents.end());
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "image removed");
    hdxf_test::assert_true(record(out, "8C5").empty(), "IMAGEDEF_REACTOR gone");
    Groups def = record(out, "8C4");
    hdxf_test::assert_false(def.empty(), "IMAGEDEF kept");
    for (const auto& [code, value] : def) hdxf_test::assert_true(value != "8C5", "no reactor 8C5");
    hdxf_test::assert_eq(values(def, 330).at(0), "8C2", "the dictionary still a reactor");
    hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), "no pointer set to none");
}

// A drawing read from a file without raster classes gets them with an image (B19)
TEST(mline_image_2000_classes_added) {
    std::filesystem::path path =
        hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / "r2000_large_handles.dxf";
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(path), dwg.last_error());
    dwg.add_image("photo.jpg", {0, 0, 0}, {10, 10}, 10.0);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "image added");
    std::string dxf = hdxf_test::lf_text(out);
    hdxf_test::assert_true(dxf.find("  0\nCLASS\n  1\nIMAGE\n") != std::string::npos, "IMAGE class");
    hdxf_test::assert_true(dxf.find("  0\nCLASS\n  1\nIMAGEDEF_REACTOR\n") != std::string::npos, "reactor class");
    hdxf::Drawing back = load(out);
    hdxf_test::assert_true(back.entities().back().type == hdxf::EntityType::Image, "the image");
}

// Extents: an image by its corners, a multiline by its elements
TEST(mline_image_2000_extents) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_image("a.png", {5, 5, 0}, {100, 50}, 20.0);
    dwg.add_mline({{0, 0, 0}, {0, 10, 0}}, false, 4.0, 1);      // elements at x = ±2
    hdxf::Drawing back = load(save(dwg));
    auto at = [&](const char* name, int code) {
        for (const auto& [c, v] : back.find_header(name)->values) if (c == code) return std::stod(v);
        return 0.0;
    };
    near(at("$EXTMIN", 10), -2.0, "mline element");
    near(at("$EXTMIN", 20), 0.0, "y");
    near(at("$EXTMAX", 10), 25.0, "image corner");
    near(at("$EXTMAX", 20), 15.0, "image corner");
}

// An MLINE with a style name but no style handle gets the handle of the MLINESTYLE of that name
TEST(mline_image_2000_style_by_name) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::Entity& mline = dwg.add_mline({{0, 0, 0}, {10, 0, 0}}, false, 1.0, 1, "Standard");
    mline.as_mline().style_handle.clear();
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf::Drawing back = load(out);
    const hdxf::Object& style = object(back, back.entities()[0].as_mline().style_handle);
    hdxf_test::assert_true(style.type == hdxf::ObjectType::MlineStyle, "MLINESTYLE");
    hdxf_test::assert_eq(style.as_mline_style().name, std::string("STANDARD"));
}
