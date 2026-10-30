#include "test_framework.hpp"
#include "hdxf.hpp"
#include "dxf_reader.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Reading DXF 2000+ with the existing parsers (phase 0, step 0.6)
//
// Files are read with the internal reader, which accepts R13+ before Drawing does
// (step 0.9).
// ---------------------------------------------------------------------------

namespace {

bool read_internal(hdxf::Drawing& dwg, std::istream& in, std::string& error) {
    hdxf::internal::DxfReader reader(in);
    bool ok = reader.read(dwg);
    error = reader.error();
    return ok;
}

bool read_file(hdxf::Drawing& dwg, const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    return read_internal(dwg, in, error);
}

bool near(double a, double b) {
    return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(a));
}

bool near(const hdxf::Point3& a, const hdxf::Point3& b) {
    return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z);
}

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

const hdxf::Block* block_named(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& b : dwg.blocks()) {
        if (b.name == name) return &b;
    }
    return nullptr;
}

} // namespace

// Groups inside 102 {…} belong to an application or to reactors, never to the entity
TEST(read2000_102_groups_isolated) {
    std::string dxf =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLINE\n  5\n20\n102\n{MYAPP\n 10\n99.0\n 20\n99.0\n  8\nWRONG\n102\n}\n"
        "102\n{ACAD_REACTORS\n330\n1F\n102\n}\n330\n1F\n100\nAcDbEntity\n  8\nL1\n"
        "100\nAcDbLine\n 10\n1.0\n 20\n2.0\n 30\n0.0\n 11\n3.0\n 21\n4.0\n 31\n0.0\n"
        "  0\nENDSEC\n  0\nEOF\n";
    std::istringstream in(dxf, std::ios::binary);
    hdxf::Drawing dwg;
    std::string error;
    hdxf_test::assert_true(read_internal(dwg, in, error), error);
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
    const hdxf::Entity& line = dwg.entities()[0];
    hdxf_test::assert_eq(line.props.layer, std::string("L1"), "layer from inside 102");
    hdxf_test::assert_true(near(line.as_line().start, {1.0, 2.0, 0.0}), "start from inside 102");
    hdxf_test::assert_true(near(line.as_line().end, {3.0, 4.0, 0.0}), "end");
    hdxf_test::assert_eq(line.props.handle, std::string("20"));
}

// ATTRIB/ATTDEF in 2018 carry an embedded MTEXT after 101 whose 10/40/1 must not
// overwrite the attribute's own; 71/72 in AcDbAttribute are not AcDbText's 71/72
TEST(read2000_embedded_object_does_not_overwrite) {
    std::filesystem::path path =
        hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / "r2018_embedded_object.dxf";
    hdxf::Drawing dwg;
    std::string error;
    hdxf_test::assert_true(read_file(dwg, path, error), error);

    const hdxf::Entity* insert = nullptr;
    for (const auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::Insert) insert = &e;
    }
    hdxf_test::assert_true(insert != nullptr, "INSERT");
    hdxf_test::assert_eq(static_cast<int>(insert->as_insert().attributes.size()), 1);
    const hdxf::AttribData& attrib = insert->as_insert().attributes[0].data;
    hdxf_test::assert_true(near(attrib.start_point, {5.0, 6.0, 0.0}), "ATTRIB 10");
    hdxf_test::assert_true(near(attrib.height, 2.5), "ATTRIB 40");
    hdxf_test::assert_eq(attrib.value, std::string(""), "ATTRIB 1");
    hdxf_test::assert_eq(attrib.tag, std::string("MULTI"));
    hdxf_test::assert_eq(attrib.generation_flags, 0, "ATTRIB 71 from AcDbAttribute");
    hdxf_test::assert_eq(attrib.h_justify, 0, "ATTRIB 72");
    hdxf_test::assert_eq(attrib.v_justify, 3, "ATTRIB 74");

    const hdxf::Block* block = block_named(dwg, "EMBEDDED_ATT");
    hdxf_test::assert_true(block != nullptr && block->entities.size() == 1, "block EMBEDDED_ATT");
    const hdxf::AttdefData& attdef = block->entities[0].as_attdef();
    hdxf_test::assert_true(near(attdef.start_point, {5.0, 6.0, 0.0}), "ATTDEF 10");
    hdxf_test::assert_true(near(attdef.height, 2.5), "ATTDEF 40");
    hdxf_test::assert_eq(attdef.default_value, std::string(""), "ATTDEF 1");
    hdxf_test::assert_eq(attdef.prompt, std::string("Prompt"));
    hdxf_test::assert_eq(attdef.generation_flags, 0, "ATTDEF 71 from AcDbAttributeDefinition");
}

// The same drawing saved by AutoCAD as 2018 and as R12: the coordinates of what both
// have must agree
TEST(read2000_same_drawing_as_r12) {
    if (!std::filesystem::exists(external("acadsharp/sample_AC1032_ascii.dxf")) ||
        !std::filesystem::exists(external("acadsharp/sample_AC1009_ascii.dxf"))) {
        hdxf_test::skip("external test files missing");
    }
    hdxf::Drawing r2018;
    hdxf::Drawing r12;
    std::string error;
    hdxf_test::assert_true(read_file(r2018, external("acadsharp/sample_AC1032_ascii.dxf"), error),
                           error);
    hdxf_test::assert_true(r12.load(external("acadsharp/sample_AC1009_ascii.dxf")),
                           r12.last_error());

    auto has_match = [&](const hdxf::Entity& e) {
        for (const auto& o : r12.entities()) {
            if (o.type != e.type) continue;
            switch (e.type) {
                case hdxf::EntityType::Line:
                    if (near(e.as_line().start, o.as_line().start) &&
                        near(e.as_line().end, o.as_line().end)) return true;
                    break;
                case hdxf::EntityType::Circle:
                    if (near(e.as_circle().center, o.as_circle().center) &&
                        near(e.as_circle().radius, o.as_circle().radius)) return true;
                    break;
                case hdxf::EntityType::Text:
                    if (near(e.as_text().position, o.as_text().position) &&
                        e.as_text().content == o.as_text().content) return true;
                    break;
                case hdxf::EntityType::Insert:
                    if (near(e.as_insert().insertion_point, o.as_insert().insertion_point))
                        return true;
                    break;
                default:
                    return true;
            }
        }
        return false;
    };

    int compared = 0;
    for (const auto& e : r2018.entities()) {
        if (e.type != hdxf::EntityType::Line && e.type != hdxf::EntityType::Circle &&
            e.type != hdxf::EntityType::Text && e.type != hdxf::EntityType::Insert) continue;
        compared++;
        hdxf_test::assert_true(has_match(e), "no R12 match for 2018 entity " + e.props.handle);
    }
    hdxf_test::assert_eq(compared, 26 + 8 + 29 + 6, "LINE + CIRCLE + TEXT + INSERT");
}

// *Model_Space / *Paper_Space are read as ordinary blocks; counts agree with the facit
// in test/data/README.md
TEST(read2000_acadsharp_2018_counts) {
    if (!std::filesystem::exists(external("acadsharp/sample_AC1032_binary.dxf"))) {
        hdxf_test::skip("external test files missing");
    }
    hdxf::Drawing dwg;
    std::string error;
    hdxf_test::assert_true(read_file(dwg, external("acadsharp/sample_AC1032_binary.dxf"), error),
                           error);
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 165, "entities");
    hdxf_test::assert_eq(static_cast<int>(dwg.layers().size()), 19, "layers");
    hdxf_test::assert_eq(static_cast<int>(dwg.blocks().size()), 27, "blocks");
    hdxf_test::assert_true(block_named(dwg, "*Model_Space") != nullptr, "*Model_Space");
    hdxf_test::assert_true(block_named(dwg, "*Paper_Space") != nullptr, "*Paper_Space");
}
