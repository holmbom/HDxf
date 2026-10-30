#include "test_framework.hpp"
#include "hdxf.hpp"
#include "dxf_writer.hpp"
#include <sstream>
#include <fstream>

static std::string generate_dxf(const hdxf::Drawing& dwg) {
    std::ostringstream oss;
    hdxf::internal::DxfWriter writer(oss);
    writer.write(dwg);
    return oss.str();
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// ===========================================================================
// R12 (AC1009) format tests
// ===========================================================================

TEST(writer_has_correct_sections) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10});
    std::string dxf = generate_dxf(dwg);

    hdxf_test::assert_true(contains(dxf, "HEADER"), "Missing HEADER");
    hdxf_test::assert_true(contains(dxf, "TABLES"), "Missing TABLES");
    hdxf_test::assert_true(contains(dxf, "BLOCKS"), "Missing BLOCKS");
    hdxf_test::assert_true(contains(dxf, "ENTITIES"), "Missing ENTITIES");
    hdxf_test::assert_true(contains(dxf, "EOF"), "Missing EOF");

    // R12 does NOT have CLASSES or OBJECTS
    hdxf_test::assert_true(!contains(dxf, "CLASSES"), "Should not have CLASSES");
    hdxf_test::assert_true(!contains(dxf, "OBJECTS"), "Should not have OBJECTS");
}

TEST(writer_has_acadver) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "AC1009"), "Missing AC1009");
}

TEST(writer_r12_header_vars) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10});
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "$HANDLING"), "Should have $HANDLING");
    hdxf_test::assert_true(contains(dxf, "$HANDSEED"), "Should have $HANDSEED");
    hdxf_test::assert_true(!contains(dxf, "AcDbEntity"), "Should not have R13+ subclass markers");
}

TEST(writer_no_subclass_markers) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10});
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(!contains(dxf, "AcDb"), "Should not have AcDb subclass markers");
}

TEST(writer_has_eight_tables) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_dxf(dwg);

    hdxf_test::assert_true(contains(dxf, "\nVPORT\n"), "Missing VPORT table");
    hdxf_test::assert_true(contains(dxf, "\nLTYPE\n"), "Missing LTYPE table");
    hdxf_test::assert_true(contains(dxf, "\nLAYER\n"), "Missing LAYER table");
    hdxf_test::assert_true(contains(dxf, "\nSTYLE\n"), "Missing STYLE table");
    hdxf_test::assert_true(contains(dxf, "\nVIEW\n"), "Missing VIEW table");
    hdxf_test::assert_true(contains(dxf, "\nUCS\n"), "Missing UCS table");
    hdxf_test::assert_true(contains(dxf, "\nAPPID\n"), "Missing APPID table");
    hdxf_test::assert_true(contains(dxf, "\nDIMSTYLE\n"), "Missing DIMSTYLE table");

    hdxf_test::assert_true(!contains(dxf, "BLOCK_RECORD"), "Should not have BLOCK_RECORD");
}

TEST(writer_has_required_linetypes) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_dxf(dwg);
    // R12 as AutoCAD 2025 writes it: CONTINUOUS only (BYBLOCK and BYLAYER are no records)
    hdxf_test::assert_false(contains(dxf, "LTYPE\n  2\nBYBLOCK"), "BYBLOCK is no R12 linetype");
    hdxf_test::assert_false(contains(dxf, "LTYPE\n  2\nBYLAYER"), "BYLAYER is no R12 linetype");
    hdxf_test::assert_true(contains(dxf, "CONTINUOUS"), "Missing CONTINUOUS");
}

TEST(writer_has_layer_0) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_dxf(dwg);
    size_t layer_section = dxf.find("\nLAYER\n");
    hdxf_test::assert_true(layer_section != std::string::npos, "Missing LAYER section");
}

TEST(writer_line_entity) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {100, 50});
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "\nLINE\n"), "Missing LINE");
}

TEST(writer_circle_entity) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_circle({50, 25}, 10.0);
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "\nCIRCLE\n"), "Missing CIRCLE");
}

TEST(writer_arc_entity) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_arc({0, 0}, 5.0, 0.0, 90.0);
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "\nARC\n"), "Missing ARC");
}

TEST(writer_text_entity) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_text({10, 20}, "Hello DXF", 5.0);
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "\nTEXT\n"), "Missing TEXT");
    hdxf_test::assert_true(contains(dxf, "Hello DXF"), "Missing text content");
}

TEST(writer_polyline_entity) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_polyline({{0,0}, {10,0}, {10,10}, {0,10}}, true);
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "\nPOLYLINE\n"), "Missing POLYLINE");
    hdxf_test::assert_true(contains(dxf, "\nVERTEX\n"), "Missing VERTEX");
    hdxf_test::assert_true(contains(dxf, "\nSEQEND\n"), "Missing SEQEND");
    hdxf_test::assert_true(!contains(dxf, "LWPOLYLINE"), "Should not have LWPOLYLINE");
}

TEST(writer_point_entity) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_point({5, 5});
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "\nPOINT\n"), "Missing POINT");
}

TEST(writer_user_layer) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Walls", hdxf::Color::red());
    dwg.add_line({0, 0}, {10, 0}, "Walls");
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "Walls"), "Missing user layer Walls");
}

TEST(writer_entity_color) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    auto& ent = dwg.add_line({0, 0}, {10, 10});
    ent.props.color = hdxf::Color::red();
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(contains(dxf, "\nLINE\n"), "Missing LINE");
}

TEST(writer_layer_no_lineweight) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_layer("Test", hdxf::Color::red());
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(!contains(dxf, "370\n"), "Should not have code 370");
    hdxf_test::assert_true(!contains(dxf, "390\n"), "Should not have code 390");
}

TEST(writer_empty_blocks_section) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(!contains(dxf, "*Model_Space"), "Should not have *Model_Space");
    hdxf_test::assert_true(!contains(dxf, "*Paper_Space"), "Should not have *Paper_Space");
}

TEST(writer_section_order) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10});
    std::string dxf = generate_dxf(dwg);

    auto header_pos = dxf.find("HEADER");
    auto tables_pos = dxf.find("TABLES");
    auto blocks_pos = dxf.find("BLOCKS");
    auto entities_pos = dxf.find("ENTITIES");
    auto eof_pos = dxf.find("EOF");

    hdxf_test::assert_true(header_pos < tables_pos, "HEADER before TABLES");
    hdxf_test::assert_true(tables_pos < blocks_pos, "TABLES before BLOCKS");
    hdxf_test::assert_true(blocks_pos < entities_pos, "BLOCKS before ENTITIES");
    hdxf_test::assert_true(entities_pos < eof_pos, "ENTITIES before EOF");
}

TEST(writer_empty_drawing_valid) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_dxf(dwg);
    hdxf_test::assert_true(dxf.size() > 100, "DXF output too small");
}

TEST(writer_save_to_file) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {100, 50});
    dwg.add_circle({50, 25}, 10.0);

    std::string filename = "test_output_r12.dxf";
    bool result = dwg.save_as(filename);
    hdxf_test::assert_true(result, "save() returned false");

    std::ifstream f(filename);
    hdxf_test::assert_true(f.good(), "File not readable");
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    hdxf_test::assert_true(size > 100, "File too small");
    f.close();

    std::remove(filename.c_str());
}

// ===========================================================================
// Binary DXF writer tests
// ===========================================================================

static std::string generate_binary_dxf(const hdxf::Drawing& dwg) {
    std::ostringstream oss(std::ios::binary);
    hdxf::internal::DxfWriter writer(oss, hdxf::DxfVersion::R12, hdxf::DxfFormat::Binary);
    writer.write(dwg);
    return oss.str();
}

TEST(writer_binary_has_sentinel) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_binary_dxf(dwg);

    hdxf_test::assert_true(dxf.size() >= 22, "Binary DXF too small");

    std::string sentinel = "AutoCAD Binary DXF\r\n\x1a";
    hdxf_test::assert_true(dxf.substr(0, 21) == sentinel, "Missing sentinel");
    hdxf_test::assert_eq(static_cast<int>(static_cast<unsigned char>(dxf[21])), 0);
}

TEST(writer_binary_contains_eof) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    std::string dxf = generate_binary_dxf(dwg);

    // Last group should be code 0 + "EOF\0"
    // Code 0 = 0x00 0x00, then "EOF\0" = 0x45 0x4F 0x46 0x00
    std::string eof_marker = {'\x00', '\x00', 'E', 'O', 'F', '\x00'};
    auto pos = dxf.rfind(eof_marker);
    hdxf_test::assert_true(pos != std::string::npos, "Missing binary EOF marker");
}

TEST(writer_binary_no_text_formatting) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {10, 10});
    std::string dxf = generate_binary_dxf(dwg);

    // Binary should not contain ASCII formatted codes like "  0\n" or " 10\n"
    hdxf_test::assert_true(!contains(dxf, "  0\n"), "Should not have ASCII code formatting");
    hdxf_test::assert_true(!contains(dxf, " 10\n"), "Should not have ASCII code formatting");
}

TEST(writer_binary_save_to_file) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);
    dwg.add_line({0, 0}, {100, 50});
    dwg.add_circle({50, 25}, 10.0);

    std::string filename = "test_output_binary.dxf";
    bool result = dwg.save_as(filename, hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(result, "save(Binary) returned false");

    std::ifstream f(filename, std::ios::binary);
    hdxf_test::assert_true(f.good(), "File not readable");
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    hdxf_test::assert_true(size > 22, "Binary file too small");
    f.close();

    std::remove(filename.c_str());
}
