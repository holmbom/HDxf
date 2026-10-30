#include "test_framework.hpp"
#include "hdxf.hpp"

TEST(drawing_construct_with_version) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R12);
}

TEST(drawing_set_version) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.set_version(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R12);
}

TEST(drawing_empty) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 0);
    hdxf_test::assert_eq(static_cast<int>(dwg.layers().size()), 0);
}

TEST(drawing_add_line) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10});
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
    hdxf_test::assert_true(dwg.entities()[0].type == hdxf::EntityType::Line);
}

TEST(drawing_add_circle) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_circle({50, 25}, 10.0);
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
    hdxf_test::assert_true(dwg.entities()[0].type == hdxf::EntityType::Circle);
    hdxf_test::assert_eq(dwg.entities()[0].as_circle().radius, 10.0);
}

TEST(drawing_add_arc) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_arc({0, 0}, 5.0, 0.0, 90.0);
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
    hdxf_test::assert_true(dwg.entities()[0].type == hdxf::EntityType::Arc);
    hdxf_test::assert_eq(dwg.entities()[0].as_arc().end_angle, 90.0);
}

TEST(drawing_add_multiple_entities) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 0});
    dwg.add_circle({5, 5}, 3.0);
    dwg.add_arc({0, 0}, 2.0, 0.0, 180.0);
    dwg.add_point({7, 7});
    dwg.add_text({0, 0}, "Hello", 3.0);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}}, true);
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 6);
}

TEST(drawing_default_layer) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10});
    hdxf_test::assert_eq(dwg.entities()[0].props.layer, std::string("0"));
}

TEST(drawing_custom_layer) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10}, "Walls");
    hdxf_test::assert_eq(dwg.entities()[0].props.layer, std::string("Walls"));
}

TEST(drawing_add_layer) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Walls", hdxf::Color::red());
    hdxf_test::assert_eq(static_cast<int>(dwg.layers().size()), 1);
    hdxf_test::assert_eq(dwg.layers()[0].name, std::string("Walls"));
    hdxf_test::assert_eq(dwg.layers()[0].color.aci, 1);
}

TEST(drawing_duplicate_layer_updates) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("X", hdxf::Color::red());
    dwg.add_layer("X", hdxf::Color::blue());
    hdxf_test::assert_eq(static_cast<int>(dwg.layers().size()), 1);
    hdxf_test::assert_eq(dwg.layers()[0].color.aci, 5);
}

TEST(drawing_entity_ref_modification) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& ent = dwg.add_line({0, 0}, {10, 10});
    ent.props.color = hdxf::Color::red();
    hdxf_test::assert_eq(dwg.entities()[0].props.color.aci, 1);
}

TEST(drawing_add_polyline) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}, {0,10}}, true, "Frame");
    const auto& poly = dwg.entities()[0].as_polyline();
    hdxf_test::assert_eq(static_cast<int>(poly.vertices.size()), 4);
    hdxf_test::assert_true(poly.is_closed());
    hdxf_test::assert_eq(dwg.entities()[0].props.layer, std::string("Frame"));
}

TEST(drawing_add_entity_manual) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    hdxf::Entity e;
    e.type = hdxf::EntityType::Circle;
    e.data = hdxf::CircleData{{0, 0, 0}, 7.0};
    dwg.add_entity(std::move(e));
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
    hdxf_test::assert_eq(dwg.entities()[0].as_circle().radius, 7.0);
}

TEST(drawing_layer_properties) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& layer = dwg.add_layer("Test", hdxf::Color::green());
    layer.frozen = true;
    layer.locked = true;
    layer.on = false;
    hdxf_test::assert_true(dwg.layers()[0].frozen);
    hdxf_test::assert_true(dwg.layers()[0].locked);
    hdxf_test::assert_false(dwg.layers()[0].on);
}
