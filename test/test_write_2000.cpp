#include "test_framework.hpp"
#include "test_drawing_builder.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "handles.hpp"
#include "dxf_structure.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Writing DXF 2000 (phase 1, step 1.2 — doc/plans/2026-10-23-fas-1-dxf-2000.md)
//
// ODA File Converter accepts a file whose owners and pointers lead nowhere, so the
// structure is checked here: unique handles, every reference to a record that exists,
// $HANDSEED above all handles, owners before what they own, LAYOUT ↔ BLOCK_RECORD.
// ---------------------------------------------------------------------------

namespace {

using dxf_structure::check_structure;


std::vector<uint8_t> save_2000(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save as 2000: " + dwg.last_error());
    return out;
}

std::string text_of(const std::vector<uint8_t>& data) {
    return hdxf_test::lf_text(data);
}

} // namespace

TEST(write_2000_structure_full) {
    hdxf::Drawing dwg = build_test_drawing(7);
    dwg.set_version(hdxf::DxfVersion::R2000);
    check_structure(save_2000(dwg), "full");
}

TEST(write_2000_structure_empty) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    check_structure(save_2000(dwg), "empty");
}

TEST(write_2000_structure_layouts_and_group) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_line({0, 0, 0}, {1, 0, 0});
    dwg.add_circle({0, 0, 0}, 1);
    // Pointers taken after the last add_…(), which may move the entities
    std::vector<hdxf::Entity*> members = {&dwg.entities()[0], &dwg.entities()[1]};
    std::string g = dwg.add_group("G", members).handle;   // objects() may grow below
    dwg.add_layout("Sheet");
    hdxf_test::assert_true(&dwg.add_layout("SHEET") == dwg.layouts().back(), "same layout");
    std::vector<uint8_t> out = save_2000(dwg);
    check_structure(out, "layouts");

    // The members have the group as reactor
    std::string s = text_of(out);
    hdxf_test::assert_true(s.find("102\n{ACAD_REACTORS\n330\n" + g + "\n102\n}\n330\n") != std::string::npos,
                           "member reactor");
    // After AutoCAD's Layout1 and Layout2 (*Paper_Space, *Paper_Space0)
    hdxf_test::assert_eq(dwg.layouts().back()->as_layout().tab_order, 3);
    hdxf_test::assert_true(dwg.find_block("*Paper_Space1") != nullptr, "block");
}

// What is written is read back the same
TEST(write_2000_read_back) {
    for (auto format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing src = build_test_drawing(7);
        src.set_version(hdxf::DxfVersion::R2000);
        std::vector<uint8_t> out = save_2000(src, format);

        hdxf::Drawing back;
        hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R2000, "version");
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()),
                             static_cast<int>(src.entities().size()), "entities");
        for (size_t i = 0; i < src.entities().size(); ++i) {
            const auto& a = src.entities()[i];
            const auto& b = back.entities()[i];
            hdxf_test::assert_true(a.type == b.type, "type of entity " + std::to_string(i));
            hdxf_test::assert_eq(b.props.layer, a.props.layer, "layer");
            hdxf_test::assert_eq(b.props.handle, a.props.handle, "handle");
            hdxf_test::assert_eq(b.props.color.aci, a.props.color.aci, "color");
            if (a.type == hdxf::EntityType::Line) {
                hdxf_test::assert_eq(b.as_line().end.x, a.as_line().end.x, "line end");
            }
            if (a.type == hdxf::EntityType::Text) {
                hdxf_test::assert_eq(b.as_text().content, a.as_text().content, "text");
            }
            if (a.type == hdxf::EntityType::Polyline) {
                hdxf_test::assert_eq(static_cast<int>(b.as_polyline().vertices.size()),
                                     static_cast<int>(a.as_polyline().vertices.size()), "vertices");
            }
        }
        for (const auto& l : src.layers()) {
            bool found = false;
            for (const auto& bl : back.layers()) found = found || bl.name == l.name;
            hdxf_test::assert_true(found, "layer " + l.name);
        }
        hdxf_test::assert_eq(static_cast<int>(back.layouts().size()), 3, "Model, Layout1, Layout2");
        hdxf_test::assert_true(back.find_block("*Model_Space") != nullptr, "*Model_Space");
    }
}

// B16: outside ASCII as \U+XXXX, also what ANSI_1252 has; read back as the same text
TEST(write_2000_text_as_unicode_escapes) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_text({0, 0, 0}, "°åäö € ∅", 2.5);
    std::vector<uint8_t> out = save_2000(dwg);
    std::string s = text_of(out);
    hdxf_test::assert_true(s.find("\\U+00B0\\U+00E5\\U+00E4\\U+00F6 \\U+20AC \\U+2205") != std::string::npos,
                           "escapes");
    for (uint8_t c : out) hdxf_test::assert_true(c < 0x80, "byte >= 0x80 in the file");

    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(back.entities()[0].as_text().content, "°åäö € ∅");
}

// B16: numbers as AutoCAD writes them
TEST(write_2000_number_format) {
    using hdxf::internal::format_double;
    using hdxf::internal::NumberStyle;
    auto f = [](double v) { return format_double(v, NumberStyle::AutoCad); };
    hdxf_test::assert_eq(f(0.0), "0.0");
    hdxf_test::assert_eq(f(1.0), "1.0");
    hdxf_test::assert_eq(f(0.5), "0.5");
    // 16 significant digits, trailing zeros dropped: 566.16858220088704456 → …887(0)
    hdxf_test::assert_eq(f(-566.16858220088704456), "-566.168582200887");
    hdxf_test::assert_eq(f(1852.185633549592), "1852.185633549592");
    hdxf_test::assert_eq(f(494617382.0), "494617382.0");
    hdxf_test::assert_eq(f(-0.0000000114653187), "-0.0000000114653187");
    // Below 1e-15 still 16 decimals (ACadSharp's HATCH 45 -0.0000000000000004; -0.0 for less),
    // with an exponent only for an MLINE's directions (its 12/13 1.224646799147353E-16)
    hdxf_test::assert_eq(f(1.224646799147353e-16), "0.0000000000000001");
    hdxf_test::assert_eq(f(-4.440892098500626e-16), "-0.0000000000000004");
    hdxf_test::assert_eq(f(-1e-20), "-0.0");
    hdxf_test::assert_eq(format_double(1.224646799147353e-16, NumberStyle::AutoCadExponent), "1.224646799147353E-16");
    hdxf_test::assert_eq(format_double(0.5, NumberStyle::AutoCadExponent), "0.5");
    hdxf_test::assert_eq(f(1e20), "1.000000000000000E+20");
    hdxf_test::assert_eq(f(0.1), "0.1");
    // R12 as AutoCAD writes it: 16 decimal places (values from sample_AC1009_ascii.dxf)
    hdxf_test::assert_eq(format_double(0.5, NumberStyle::R12), "0.5");
    hdxf_test::assert_eq(format_double(0.18, NumberStyle::R12), "0.18");
    hdxf_test::assert_eq(format_double(25.4, NumberStyle::R12), "25.3999999999999986");
    hdxf_test::assert_eq(format_double(1.0 / 3.0, NumberStyle::R12), "0.3333333333333333");
    hdxf_test::assert_eq(format_double(-1.2e-16, NumberStyle::R12), "-0.0000000000000001");
    // The sentinel with 16 decimals in R12 (AutoCAD 2025's R12 files), 15 in 2000 and later (plan F9)
    hdxf_test::assert_eq(format_double(1e20, NumberStyle::R12), "1.0000000000000000E+20");
    hdxf_test::assert_eq(format_double(-1e20, NumberStyle::R12), "-1.0000000000000000E+20");
}

// A save that fails takes back the handles it gave: the drawing is as it was
TEST(write_2000_failed_save_changes_nothing) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_line({0, 0, 0}, {1, 0, 0});
    dwg.add_text({0, 0, 0}, std::string("bad \xFF utf-8"), 2.5);
    const hdxf::HeaderVar* seed = dwg.find_header("$HANDSEED");
    std::string before = seed ? seed->values[0].second : "";

    hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(dwg.last_error().find("UTF-8") != std::string::npos, dwg.last_error());
    for (const auto& e : dwg.entities()) hdxf_test::assert_true(e.props.handle.empty(), "handle kept");
    for (const auto& b : dwg.blocks()) hdxf_test::assert_true(b.handle.empty(), "block handle kept");
    seed = dwg.find_header("$HANDSEED");
    hdxf_test::assert_eq(seed ? seed->values[0].second : "", before, "$HANDSEED");
}

// The arrow blocks of a dimension style are BLOCK_RECORD handles in 2000 (342-344)
TEST(write_2000_dimstyle_arrow_blocks) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_block("ARROW1").entities.push_back(dwg.add_line({0, 0, 0}, {1, 0, 0}));
    dwg.entities().clear();
    dwg.add_dim_style("Arrows").dimblk = "ARROW1";
    std::vector<uint8_t> out = save_2000(dwg);
    check_structure(out, "arrows");
    std::string record = dwg.find_block("ARROW1")->record.handle;
    hdxf_test::assert_true(text_of(out).find("342\n" + record + "\n") != std::string::npos, "342");

    dwg.add_dim_style("Missing").dimblk1 = "NO_SUCH_BLOCK";
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(dwg.last_error().find("NO_SUCH_BLOCK") != std::string::npos, dwg.last_error());
}

// A 2000 drawing set back to R12 writes R12 as before: no *Model_Space/*Paper_Space
TEST(write_2000_structure_not_in_r12) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_line({0, 0, 0}, {1, 0, 0});
    dwg.set_version(hdxf::DxfVersion::R12);
    std::string s = text_of(dwg.save_to_memory());
    hdxf_test::assert_true(s.find("AC1009") != std::string::npos, "R12");
    hdxf_test::assert_true(s.find("*Model_Space") == std::string::npos, "*Model_Space");
    hdxf_test::assert_true(s.find("OBJECTS") == std::string::npos, "OBJECTS");
    hdxf_test::assert_true(s.find("$FINGERPRINTGUID") == std::string::npos, "2000-only variable");
}

// The header has AutoCAD's 199 variables for 2000, empty strings and GUIDs included
TEST(write_2000_header_as_autocad) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    std::string s = text_of(save_2000(dwg));
    int vars = 0;
    for (size_t pos = s.find("\n  9\n$"); pos != std::string::npos; pos = s.find("\n  9\n$", pos + 1)) ++vars;
    hdxf_test::assert_eq(vars, 199, "header variables");
    hdxf_test::assert_true(s.find("  9\n$UCSNAME\n  2\n\n") != std::string::npos, "empty $UCSNAME");
    hdxf_test::assert_true(s.find("  9\n$DIMPOST\n  1\n\n") != std::string::npos, "empty $DIMPOST");

    // Two GUIDs, {8-4-4-4-12}, different, kept by the drawing
    const hdxf::HeaderVar* fp = dwg.find_header("$FINGERPRINTGUID");
    const hdxf::HeaderVar* ver = dwg.find_header("$VERSIONGUID");
    hdxf_test::assert_true(fp && ver, "GUIDs");
    const std::string& g = fp->values[0].second;
    hdxf_test::assert_eq(static_cast<int>(g.size()), 38, g);
    hdxf_test::assert_true(g[0] == '{' && g[9] == '-' && g[14] == '-' && g[19] == '-' &&
                           g[24] == '-' && g[37] == '}', g);
    hdxf_test::assert_true(g != ver->values[0].second, "different");
    hdxf_test::assert_true(s.find(g) != std::string::npos, "written");
    hdxf::Drawing other(hdxf::DxfVersion::R2000);
    hdxf_test::assert_true(other.find_header("$FINGERPRINTGUID")->values[0].second != g,
                           "every drawing its own");
}

// A BLOCK_RECORD lists the INSERTs of its block
TEST(write_2000_block_refs) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_block("B");
    dwg.add_insert("B", {0, 0, 0});
    dwg.add_insert("B", {5, 0, 0});
    std::vector<uint8_t> out = save_2000(dwg);
    check_structure(out, "blkrefs");
    std::string s = text_of(out);
    std::string refs = "102\n{BLKREFS\n331\n" + dwg.entities()[0].props.handle + "\n331\n" +
                       dwg.entities()[1].props.handle + "\n102\n}\n";
    hdxf_test::assert_true(s.find(refs) != std::string::npos, "BLKREFS");
}

// Group 42, the measurement, is what AutoCAD wrote for the same dimension: every
// DIMENSION of the AutoCAD sample, copied into a new drawing
TEST(write_2000_dimension_measurement_as_autocad) {
    std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) /
                                 "acadsharp" / "sample_AC1015_ascii.dxf";
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("external test file missing");
        return;
    }
    hdxf::Drawing sample;
    hdxf_test::assert_true(sample.load(path), sample.last_error());

    auto group_42 = [](const hdxf::RawRecord& raw) {
        for (const auto& g : raw.groups()) {
            if (g.code == 42) return std::stod(g.value);
        }
        return -1.0;
    };
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    std::vector<double> expected;
    for (const auto& e : sample.entities()) {
        if (e.type != hdxf::EntityType::Dimension) continue;
        expected.push_back(group_42(e.raw));
        hdxf::Entity copy;
        copy.type = e.type;
        copy.data = e.data;
        copy.as_dimension().block_name.clear();
        copy.as_dimension().dim_style = "Standard";
        dwg.add_entity(copy);
    }
    hdxf_test::assert_eq(static_cast<int>(expected.size()), 11, "dimensions");

    std::vector<uint8_t> out = save_2000(dwg);
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    for (size_t i = 0; i < expected.size(); ++i) {
        double got = group_42(back.entities()[i].raw);
        hdxf_test::assert_true(std::fabs(got - expected[i]) < 1e-9,
                               "dimension " + std::to_string(i) + ": " + std::to_string(got) +
                               " != " + std::to_string(expected[i]));
    }
}
