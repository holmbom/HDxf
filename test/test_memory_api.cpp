#include "test_framework.hpp"
#include "hdxf.hpp"

// ---------------------------------------------------------------------------
// save_to_memory / load_from_memory round-trip (ASCII)
// ---------------------------------------------------------------------------

TEST(memory_roundtrip_ascii) {
    hdxf::Drawing dwg;
    dwg.add_layer("TestLayer", hdxf::Color::red());
    dwg.add_line({0, 0, 0}, {10, 10, 0}, "TestLayer");
    dwg.add_circle({5, 5, 0}, 3.0);

    auto bytes = dwg.save_to_memory(hdxf::DxfFormat::Ascii);
    hdxf_test::assert_true(bytes.size() > 0, "save_to_memory should produce non-empty output");

    hdxf::Drawing dwg2;
    bool ok = dwg2.load_from_memory(bytes.data(), bytes.size());
    hdxf_test::assert_true(ok, "load_from_memory should succeed");

    hdxf_test::assert_eq(static_cast<int>(dwg2.entities().size()), 2);
    hdxf_test::assert_true(dwg2.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_true(dwg2.entities()[1].type == hdxf::EntityType::Circle);
    hdxf_test::assert_eq(dwg2.entities()[0].props.layer, std::string("TestLayer"));
}

// ---------------------------------------------------------------------------
// save_to_memory / load_from_memory round-trip (Binary)
// ---------------------------------------------------------------------------

TEST(memory_roundtrip_binary) {
    hdxf::Drawing dwg;
    dwg.add_line({1, 2, 3}, {4, 5, 6});
    dwg.add_text({0, 0, 0}, "Hello", 2.5);

    auto bytes = dwg.save_to_memory(hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(bytes.size() > 0, "Binary save_to_memory should produce non-empty output");

    hdxf::Drawing dwg2;
    bool ok = dwg2.load_from_memory(bytes.data(), bytes.size());
    hdxf_test::assert_true(ok, "load_from_memory (binary) should succeed");

    hdxf_test::assert_eq(static_cast<int>(dwg2.entities().size()), 2);
    hdxf_test::assert_true(dwg2.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_true(dwg2.entities()[1].type == hdxf::EntityType::Text);
}

// ---------------------------------------------------------------------------
// load_from_memory does not set filename (save() should fail after)
// ---------------------------------------------------------------------------

TEST(memory_load_no_filename) {
    hdxf::Drawing dwg;
    dwg.add_line({0, 0}, {1, 1});
    auto bytes = dwg.save_to_memory();

    hdxf::Drawing dwg2;
    dwg2.load_from_memory(bytes.data(), bytes.size());
    hdxf_test::assert_false(dwg2.save(), "save() should fail when no filename is set");
}

// ---------------------------------------------------------------------------
// load_from_memory with invalid data
// ---------------------------------------------------------------------------

TEST(memory_load_invalid_data) {
    hdxf::Drawing dwg;
    const uint8_t garbage[] = { 0x00, 0xFF, 0xAB, 0xCD };
    bool ok = dwg.load_from_memory(garbage, sizeof(garbage));
    hdxf_test::assert_false(ok, "load_from_memory with garbage should fail");
    hdxf_test::assert_true(!dwg.last_error().empty(), "Should have error message");
}

// ---------------------------------------------------------------------------
// remove_layer — basic
// ---------------------------------------------------------------------------

TEST(remove_layer_basic) {
    hdxf::Drawing dwg;
    dwg.add_layer("A", hdxf::Color::red());
    dwg.add_layer("B", hdxf::Color::blue());
    dwg.add_line({0, 0}, {1, 1}, "A");
    dwg.add_circle({0, 0}, 5.0, "B");

    bool ok = dwg.remove_layer("A");
    hdxf_test::assert_true(ok, "remove_layer should succeed");
    hdxf_test::assert_eq(static_cast<int>(dwg.layers().size()), 1);
    hdxf_test::assert_eq(dwg.layers()[0].name, std::string("B"));
    // Entity on removed layer should move to "0"
    hdxf_test::assert_eq(dwg.entities()[0].props.layer, std::string("0"));
    // Entity on layer B unchanged
    hdxf_test::assert_eq(dwg.entities()[1].props.layer, std::string("B"));
}

// ---------------------------------------------------------------------------
// remove_layer — cannot remove layer "0"
// ---------------------------------------------------------------------------

TEST(remove_layer_zero_protected) {
    hdxf::Drawing dwg;
    dwg.add_layer("0");
    bool ok = dwg.remove_layer("0");
    hdxf_test::assert_false(ok, "Should not be able to remove layer 0");
}

// ---------------------------------------------------------------------------
// remove_layer — non-existent layer
// ---------------------------------------------------------------------------

TEST(remove_layer_nonexistent) {
    hdxf::Drawing dwg;
    bool ok = dwg.remove_layer("DoesNotExist");
    hdxf_test::assert_false(ok, "Should return false for non-existent layer");
}

// ---------------------------------------------------------------------------
// remove_layer — a polyline's vertices and SEQEND have layers of their own
// ---------------------------------------------------------------------------

TEST(remove_layer_moves_vertices_and_seqend) {
    hdxf::Drawing dwg;
    dwg.add_layer("A", hdxf::Color::red());
    dwg.add_layer("B", hdxf::Color::blue());
    auto make = [](hdxf::Entity& e) {
        auto& polyline = e.as_polyline();
        polyline.vertices[0].layer = "A";
        polyline.vertices[1].layer = "B";
        polyline.seqend_layer = "A";
    };
    make(dwg.add_polyline({{0, 0}, {1, 1}}, false, "B"));
    hdxf::Block& block = dwg.add_block("BLK");
    block.entities.push_back(dwg.entities().back());

    hdxf_test::assert_true(dwg.remove_layer("A"), "remove_layer should succeed");
    for (const hdxf::Entity* e : {&dwg.entities()[0], &dwg.blocks()[0].entities[0]}) {
        const auto& polyline = e->as_polyline();
        hdxf_test::assert_eq(polyline.vertices[0].layer, std::string("0"), "vertex on the removed layer");
        hdxf_test::assert_eq(polyline.vertices[1].layer, std::string("B"), "vertex on another layer");
        hdxf_test::assert_eq(polyline.seqend_layer, std::string("0"), "SEQEND on the removed layer");
    }
}
