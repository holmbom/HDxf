#include "test_framework.hpp"
#include "hdxf.hpp"
#include "dxf_writer.hpp"
#include "dxf_reader.hpp"
#include <sstream>
#include <fstream>

// Helper: write a Drawing to string, then read it back
static hdxf::Drawing roundtrip(const hdxf::Drawing& dwg) {
    std::ostringstream oss;
    hdxf::internal::DxfWriter writer(oss);
    writer.write(dwg);

    std::string dxf = oss.str();
    std::istringstream iss(dxf);

    hdxf::Drawing result(hdxf::DxfVersion::R12);
    hdxf::internal::DxfReader reader(iss);
    reader.read(result);

    return result;
}

// ===========================================================================
// Reader tests
// ===========================================================================

TEST(reader_empty_drawing) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto result = roundtrip(dwg);
    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 0);
}

TEST(reader_line) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}, "0");
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Line);
    hdxf_test::assert_eq(e.as_line().start.x, 1.0);
    hdxf_test::assert_eq(e.as_line().start.y, 2.0);
    hdxf_test::assert_eq(e.as_line().start.z, 3.0);
    hdxf_test::assert_eq(e.as_line().end.x, 4.0);
    hdxf_test::assert_eq(e.as_line().end.y, 5.0);
    hdxf_test::assert_eq(e.as_line().end.z, 6.0);
}

TEST(reader_line_layer) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Walls", hdxf::Color::red());
    dwg.add_line({0, 0}, {10, 0}, "Walls");
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(result.entities()[0].props.layer, std::string("Walls"));
}

TEST(reader_circle) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_circle({10.0, 20.0, 0.0}, 5.0);
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Circle);
    hdxf_test::assert_eq(e.as_circle().center.x, 10.0);
    hdxf_test::assert_eq(e.as_circle().center.y, 20.0);
    hdxf_test::assert_eq(e.as_circle().radius, 5.0);
}

TEST(reader_arc) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_arc({0, 0}, 10.0, 45.0, 135.0);
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Arc);
    hdxf_test::assert_eq(e.as_arc().radius, 10.0);
    hdxf_test::assert_eq(e.as_arc().start_angle, 45.0);
    hdxf_test::assert_eq(e.as_arc().end_angle, 135.0);
}

TEST(reader_point) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_point({7.0, 8.0, 9.0});
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Point);
    hdxf_test::assert_eq(e.as_point().location.x, 7.0);
    hdxf_test::assert_eq(e.as_point().location.y, 8.0);
    hdxf_test::assert_eq(e.as_point().location.z, 9.0);
}

TEST(reader_text) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_text({10, 20}, "Hello DXF", 5.0);
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Text);
    hdxf_test::assert_eq(e.as_text().content, std::string("Hello DXF"));
    hdxf_test::assert_eq(e.as_text().height, 5.0);
    hdxf_test::assert_eq(e.as_text().position.x, 10.0);
    hdxf_test::assert_eq(e.as_text().position.y, 20.0);
}

TEST(reader_text_rotation) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& ent = dwg.add_text({0, 0}, "Rotated", 3.0);
    ent.as_text().rotation = 45.0;
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(result.entities()[0].as_text().rotation, 45.0);
}

TEST(reader_polyline) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}, {0,10}}, true);
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Polyline);
    const auto& poly = e.as_polyline();
    hdxf_test::assert_eq(static_cast<int>(poly.vertices.size()), 4);
    hdxf_test::assert_true(poly.is_closed());
    hdxf_test::assert_eq(poly.vertices[0].position.x, 0.0);
    hdxf_test::assert_eq(poly.vertices[1].position.x, 10.0);
    hdxf_test::assert_eq(poly.vertices[2].position.y, 10.0);
    hdxf_test::assert_eq(poly.vertices[3].position.x, 0.0);
}

TEST(reader_polyline_open) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}}, false);
    auto result = roundtrip(dwg);

    hdxf_test::assert_false(result.entities()[0].as_polyline().is_closed());
}

TEST(reader_layers) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Red", hdxf::Color::red());
    dwg.add_layer("Blue", hdxf::Color::blue());
    dwg.add_line({0,0}, {10,0}, "Red");
    auto result = roundtrip(dwg);

    hdxf_test::assert_true(static_cast<int>(result.layers().size()) >= 2,
                           "Expected at least 2 layers");

    bool found_red = false, found_blue = false;
    for (const auto& l : result.layers()) {
        if (l.name == "Red") { found_red = true; hdxf_test::assert_eq(l.color.aci, 1); }
        if (l.name == "Blue") { found_blue = true; hdxf_test::assert_eq(l.color.aci, 5); }
    }
    hdxf_test::assert_true(found_red, "Missing layer Red");
    hdxf_test::assert_true(found_blue, "Missing layer Blue");
}

TEST(reader_layer_frozen) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& layer = dwg.add_layer("Frozen", hdxf::Color::red());
    layer.frozen = true;
    auto result = roundtrip(dwg);

    bool found = false;
    for (const auto& l : result.layers()) {
        if (l.name == "Frozen") { found = true; hdxf_test::assert_true(l.frozen); }
    }
    hdxf_test::assert_true(found, "Missing layer Frozen");
}

TEST(reader_layer_locked) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& layer = dwg.add_layer("Locked", hdxf::Color::green());
    layer.locked = true;
    auto result = roundtrip(dwg);

    bool found = false;
    for (const auto& l : result.layers()) {
        if (l.name == "Locked") { found = true; hdxf_test::assert_true(l.locked); }
    }
    hdxf_test::assert_true(found, "Missing layer Locked");
}

TEST(reader_layer_off) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& layer = dwg.add_layer("Off", hdxf::Color::cyan());
    layer.on = false;
    auto result = roundtrip(dwg);

    bool found = false;
    for (const auto& l : result.layers()) {
        if (l.name == "Off") { found = true; hdxf_test::assert_false(l.on); }
    }
    hdxf_test::assert_true(found, "Missing layer Off");
}

TEST(reader_entity_color) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& ent = dwg.add_line({0,0}, {10,10});
    ent.props.color = hdxf::Color::red();
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(result.entities()[0].props.color.aci, 1);
}

TEST(reader_multiple_entities) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0,0}, {10,0});
    dwg.add_circle({5,5}, 3.0);
    dwg.add_arc({0,0}, 2.0, 0.0, 180.0);
    dwg.add_point({7,7});
    dwg.add_text({0,0}, "Test", 3.0);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}}, true);
    auto result = roundtrip(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 6);
    hdxf_test::assert_true(result.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_true(result.entities()[1].type == hdxf::EntityType::Circle);
    hdxf_test::assert_true(result.entities()[2].type == hdxf::EntityType::Arc);
    hdxf_test::assert_true(result.entities()[3].type == hdxf::EntityType::Point);
    hdxf_test::assert_true(result.entities()[4].type == hdxf::EntityType::Text);
    hdxf_test::assert_true(result.entities()[5].type == hdxf::EntityType::Polyline);
}

TEST(reader_roundtrip_save_load) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Test", hdxf::Color::green());
    dwg.add_line({1,2}, {3,4}, "Test");
    dwg.add_circle({10,20}, 5.0);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}}, true);

    std::string filename = "test_roundtrip.dxf";
    hdxf_test::assert_true(dwg.save_as(filename), "save() failed");

    hdxf::Drawing loaded(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(loaded.load(filename), "load() failed");

    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 3);
    hdxf_test::assert_true(loaded.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_eq(loaded.entities()[0].as_line().start.x, 1.0);
    hdxf_test::assert_true(loaded.entities()[1].type == hdxf::EntityType::Circle);
    hdxf_test::assert_eq(loaded.entities()[1].as_circle().radius, 5.0);
    hdxf_test::assert_true(loaded.entities()[2].type == hdxf::EntityType::Polyline);
    hdxf_test::assert_eq(static_cast<int>(loaded.entities()[2].as_polyline().vertices.size()), 3);

    std::remove(filename.c_str());
}

// ===========================================================================
// Binary DXF reader tests
// ===========================================================================

// Helper: binary roundtrip via ostringstream/istringstream
static hdxf::Drawing roundtrip_binary(const hdxf::Drawing& dwg) {
    std::ostringstream oss(std::ios::binary);
    hdxf::internal::DxfWriter writer(oss, hdxf::DxfVersion::R12, hdxf::DxfFormat::Binary);
    writer.write(dwg);

    std::string data = oss.str();
    std::istringstream iss(data, std::ios::binary);

    hdxf::Drawing result(hdxf::DxfVersion::R12);
    hdxf::internal::DxfReader reader(iss);
    reader.read(result);

    return result;
}

TEST(reader_binary_line) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({1.5, 2.5, 3.5}, {4.5, 5.5, 6.5});
    auto result = roundtrip_binary(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Line);
    hdxf_test::assert_eq(e.as_line().start.x, 1.5);
    hdxf_test::assert_eq(e.as_line().start.y, 2.5);
    hdxf_test::assert_eq(e.as_line().end.z, 6.5);
}

TEST(reader_binary_circle) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_circle({10.0, 20.0}, 7.5);
    auto result = roundtrip_binary(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& e = result.entities()[0];
    hdxf_test::assert_true(e.type == hdxf::EntityType::Circle);
    hdxf_test::assert_eq(e.as_circle().center.x, 10.0);
    hdxf_test::assert_eq(e.as_circle().radius, 7.5);
}

TEST(reader_binary_polyline) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}, {0,10}}, true);
    auto result = roundtrip_binary(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& poly = result.entities()[0].as_polyline();
    hdxf_test::assert_eq(static_cast<int>(poly.vertices.size()), 4);
    hdxf_test::assert_true(poly.is_closed());
    hdxf_test::assert_eq(poly.vertices[2].position.x, 10.0);
    hdxf_test::assert_eq(poly.vertices[2].position.y, 10.0);
}

TEST(reader_binary_layers) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Red", hdxf::Color::red());
    auto& frozen = dwg.add_layer("Frozen", hdxf::Color::blue());
    frozen.frozen = true;
    dwg.add_line({0,0}, {10,0}, "Red");
    auto result = roundtrip_binary(dwg);

    bool found_red = false, found_frozen = false;
    for (const auto& l : result.layers()) {
        if (l.name == "Red") { found_red = true; hdxf_test::assert_eq(l.color.aci, 1); }
        if (l.name == "Frozen") { found_frozen = true; hdxf_test::assert_true(l.frozen); }
    }
    hdxf_test::assert_true(found_red, "Missing layer Red");
    hdxf_test::assert_true(found_frozen, "Missing layer Frozen");
}

TEST(reader_binary_multiple_entities) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0,0}, {10,0});
    dwg.add_circle({5,5}, 3.0);
    dwg.add_arc({0,0}, 2.0, 0.0, 180.0);
    dwg.add_point({7,7});
    dwg.add_text({0,0}, "Test", 3.0);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}}, true);
    auto result = roundtrip_binary(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 6);
    hdxf_test::assert_true(result.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_true(result.entities()[1].type == hdxf::EntityType::Circle);
    hdxf_test::assert_true(result.entities()[2].type == hdxf::EntityType::Arc);
    hdxf_test::assert_true(result.entities()[3].type == hdxf::EntityType::Point);
    hdxf_test::assert_true(result.entities()[4].type == hdxf::EntityType::Text);
    hdxf_test::assert_true(result.entities()[5].type == hdxf::EntityType::Polyline);
}

TEST(reader_binary_save_load_file) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Test", hdxf::Color::green());
    dwg.add_line({1,2}, {3,4}, "Test");
    dwg.add_circle({10,20}, 5.0);

    std::string filename = "test_roundtrip_bin.dxf";
    hdxf_test::assert_true(dwg.save_as(filename, hdxf::DxfFormat::Binary), "save(Binary) failed");

    hdxf::Drawing loaded(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(loaded.load(filename), "load() failed");

    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 2);
    hdxf_test::assert_true(loaded.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_eq(loaded.entities()[0].as_line().start.x, 1.0);
    hdxf_test::assert_true(loaded.entities()[1].type == hdxf::EntityType::Circle);
    hdxf_test::assert_eq(loaded.entities()[1].as_circle().radius, 5.0);

    std::remove(filename.c_str());
}

TEST(reader_auto_detect_ascii) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0,0}, {10,10});

    std::string filename = "test_autodetect_ascii.dxf";
    dwg.save_as(filename, hdxf::DxfFormat::Ascii);

    hdxf::Drawing loaded(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(loaded.load(filename), "load() ascii failed");
    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 1);

    std::remove(filename.c_str());
}

TEST(reader_auto_detect_binary) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0,0}, {10,10});

    std::string filename = "test_autodetect_binary.dxf";
    dwg.save_as(filename, hdxf::DxfFormat::Binary);

    hdxf::Drawing loaded(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(loaded.load(filename), "load() binary failed");
    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 1);

    std::remove(filename.c_str());
}

// ===========================================================================
// Unsupported version handling
// ===========================================================================

// Since step 0.9 the reader takes R12 … 2018
TEST(reader_reads_r2000) {
    std::string fake_dxf =
        "  0\nSECTION\n  2\nHEADER\n"
        "  9\n$ACADVER\n  1\nAC1015\n"
        "  0\nENDSEC\n"
        "  0\nEOF\n";

    std::istringstream iss(fake_dxf);
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    hdxf::internal::DxfReader reader(iss);
    bool ok = reader.read(dwg);

    hdxf_test::assert_true(ok, reader.error());
    hdxf_test::assert_true(reader.detected_version() == hdxf::DxfVersion::R2000, "R2000");
}

TEST(reader_reads_r2007) {
    std::string fake_dxf =
        "  0\nSECTION\n  2\nHEADER\n"
        "  9\n$ACADVER\n  1\nAC1021\n"
        "  0\nENDSEC\n"
        "  0\nEOF\n";

    std::istringstream iss(fake_dxf);
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    hdxf::internal::DxfReader reader(iss);
    bool ok = reader.read(dwg);

    hdxf_test::assert_true(ok, reader.error());
    hdxf_test::assert_true(reader.detected_version() == hdxf::DxfVersion::R2007, "R2007");
}

// load() reads an R14 file; save() writes it as R14 (plan phase 7, B86) — another version is a
// conversion and fails, with the file left alone
TEST(reader_drawing_load_r14_saves_as_r14_only) {
    std::string fake_dxf =
        "  0\nSECTION\n  2\nHEADER\n"
        "  9\n$ACADVER\n  1\nAC1014\n"
        "  0\nENDSEC\n"
        "  0\nEOF\n";

    std::string filename = "test_r14_version.dxf";
    {
        std::ofstream f(filename);
        f << fake_dxf;
    }

    hdxf::Drawing loaded(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(loaded.load(filename), loaded.last_error());
    hdxf_test::assert_true(loaded.version() == hdxf::DxfVersion::R14, "version");

    loaded.set_version(hdxf::DxfVersion::R2000);
    hdxf_test::assert_false(loaded.save(), "save() of an R14 drawing as 2000");
    hdxf_test::assert_true(loaded.last_error().find("Writing DXF R14 as 2000 is not supported yet") !=
                           std::string::npos, loaded.last_error());
    std::ifstream in(filename);
    std::stringstream content;
    content << in.rdbuf();
    in.close();
    hdxf_test::assert_eq(content.str(), fake_dxf, "file unchanged");

    loaded.set_version(hdxf::DxfVersion::R14);
    hdxf_test::assert_true(loaded.save(), loaded.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load(filename), back.last_error());
    hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R14, "version read back");

    std::remove(filename.c_str());
}

TEST(reader_rejects_unknown_version) {
    std::string fake_dxf =
        "  0\nSECTION\n  2\nHEADER\n"
        "  9\n$ACADVER\n  1\nAC1006\n"
        "  0\nENDSEC\n"
        "  0\nEOF\n";

    hdxf::Drawing dwg;
    hdxf_test::assert_false(dwg.load_from_memory(reinterpret_cast<const uint8_t*>(fake_dxf.data()),
                                                 fake_dxf.size()), "AC1006 loaded");
    hdxf_test::assert_true(dwg.last_error().find("Unsupported DXF version: AC1006") !=
                           std::string::npos, dwg.last_error());
}

TEST(reader_drawing_load_file_not_found) {
    hdxf::Drawing loaded(hdxf::DxfVersion::R12);
    bool ok = loaded.load("nonexistent_file_12345.dxf");

    hdxf_test::assert_false(ok, "load() should return false for missing file");
    hdxf_test::assert_true(!loaded.last_error().empty(),
                           "last_error() should be set for missing file");
}

TEST(reader_binary_xdata_roundtrip) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& e = dwg.add_line({1, 2}, {3, 4});

    // Add XDATA with various types: string, doubles, int16, int32, binary chunk
    hdxf::XdataList xd;
    xd.app_name = "TESTAPP";
    xd.items.push_back({1000, "hello"});          // string
    xd.items.push_back({1040, "3.14"});            // double (1010-1059)
    xd.items.push_back({1010, "1.0"});             // double
    xd.items.push_back({1020, "2.0"});             // double
    xd.items.push_back({1030, "3.0"});             // double
    xd.items.push_back({1070, "42"});              // int16
    xd.items.push_back({1071, "100000"});          // int32
    xd.items.push_back({1004, "4F3C2B"});          // binary chunk (3 bytes)
    e.props.xdata.push_back(xd);

    auto result = roundtrip_binary(dwg);

    hdxf_test::assert_eq(static_cast<int>(result.entities().size()), 1);
    const auto& re = result.entities()[0];
    hdxf_test::assert_eq(static_cast<int>(re.props.xdata.size()), 1);

    const auto& rxd = re.props.xdata[0];
    hdxf_test::assert_eq(rxd.app_name, std::string("TESTAPP"));
    hdxf_test::assert_eq(static_cast<int>(rxd.items.size()), 8);

    // String
    hdxf_test::assert_eq(rxd.items[0].code, 1000);
    hdxf_test::assert_eq(rxd.items[0].value, std::string("hello"));

    // Doubles (compare as double values for precision safety)
    hdxf_test::assert_eq(rxd.items[1].code, 1040);
    hdxf_test::assert_eq(std::stod(rxd.items[1].value), 3.14);
    hdxf_test::assert_eq(rxd.items[2].code, 1010);
    hdxf_test::assert_eq(std::stod(rxd.items[2].value), 1.0);
    hdxf_test::assert_eq(rxd.items[3].code, 1020);
    hdxf_test::assert_eq(std::stod(rxd.items[3].value), 2.0);
    hdxf_test::assert_eq(rxd.items[4].code, 1030);
    hdxf_test::assert_eq(std::stod(rxd.items[4].value), 3.0);

    // Int16
    hdxf_test::assert_eq(rxd.items[5].code, 1070);
    hdxf_test::assert_eq(std::stoi(rxd.items[5].value), 42);

    // Int32
    hdxf_test::assert_eq(rxd.items[6].code, 1071);
    hdxf_test::assert_eq(static_cast<int>(std::stol(rxd.items[6].value)), 100000);

    // Binary chunk
    hdxf_test::assert_eq(rxd.items[7].code, 1004);
    hdxf_test::assert_eq(rxd.items[7].value, std::string("4F3C2B"));
}
