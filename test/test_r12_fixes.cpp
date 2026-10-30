#include "test_framework.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>

// ---------------------------------------------------------------------------
// R12 bugs fixed in phase 0, step 0.1 (doc/plans/2026-10-22-fas-0-grund.md)
// ---------------------------------------------------------------------------

namespace {

bool load_text(hdxf::Drawing& dwg, const std::string& dxf) {
    return dwg.load_from_memory(reinterpret_cast<const uint8_t*>(dxf.data()), dxf.size());
}

std::string to_text(const std::vector<uint8_t>& bytes) {
    return hdxf_test::lf_text(bytes);
}

// Number of "  0\n<type>\n  2\n<name>\n" records, name compared case-insensitively
int count_records(const std::string& dxf, const std::string& type, const std::string& name) {
    auto upper = [](std::string s) {
        for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return s;
    };
    int count = 0;
    std::string head = "  0\n" + type + "\n";
    for (size_t pos = dxf.find(head); pos != std::string::npos; pos = dxf.find(head, pos + 1)) {
        size_t name_pos = dxf.find("  2\n", pos + head.size());
        if (name_pos == std::string::npos) break;
        size_t start = name_pos + 4;
        size_t end = dxf.find('\n', start);
        if (upper(dxf.substr(start, end - start)) == upper(name)) count++;
    }
    return count;
}

const hdxf::HeaderVar* header(const hdxf::Drawing& dwg, const std::string& name) {
    const hdxf::HeaderVar* h = dwg.find_header(name);
    hdxf_test::assert_true(h != nullptr && !h->values.empty(), name + " missing");
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// Bug 1: a drawing's own standard entries are not written twice
// ---------------------------------------------------------------------------

TEST(fix1_standard_entries_written_once) {
    hdxf::Drawing dwg;
    dwg.add_text_style("Standard", "romans").width_factor = 0.9;
    dwg.add_dim_style("STANDARD").dimasz = 4.0;
    dwg.add_linetype("Continuous", "Solid", {});
    dwg.add_app_id("ACAD");

    std::string out = to_text(dwg.save_to_memory());
    hdxf_test::assert_eq(count_records(out, "STYLE", "Standard"), 1, "STYLE Standard");
    hdxf_test::assert_eq(count_records(out, "DIMSTYLE", "Standard"), 1, "DIMSTYLE Standard");
    hdxf_test::assert_eq(count_records(out, "LTYPE", "CONTINUOUS"), 1, "LTYPE CONTINUOUS");
    hdxf_test::assert_eq(count_records(out, "APPID", "ACAD"), 1, "APPID ACAD");

    // The drawing's own values win over the defaults
    hdxf::Drawing loaded;
    hdxf_test::assert_true(load_text(loaded, out), loaded.last_error());
    bool found = false;
    for (const auto& s : loaded.text_styles()) {
        if (s.name == "Standard") {
            found = true;
            hdxf_test::assert_eq(s.font_file, std::string("romans"));
            hdxf_test::assert_eq(s.width_factor, 0.9);
        }
    }
    hdxf_test::assert_true(found, "Standard style not read back");
    hdxf_test::assert_eq(loaded.dim_styles().at(0).dimasz, 4.0);
}

TEST(fix1_dimension_uses_own_standard_style_any_case) {
    // The table keeps the drawing's "STANDARD"; dimension geometry must use it too
    hdxf::Drawing dwg;
    auto& ds = dwg.add_dim_style("STANDARD");
    ds.dimtxt = 7.0;
    auto& dim = dwg.add_dimension(hdxf::DimensionType::Rotated, {0, 0, 0}, {50, 10, 0});
    dim.as_dimension().def_point2 = {0, 0, 0};
    dim.as_dimension().def_point3 = {100, 0, 0};
    hdxf_test::assert_eq(dim.as_dimension().dim_style, std::string("Standard"));

    auto bytes = dwg.save_to_memory();
    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load_from_memory(bytes.data(), bytes.size()), loaded.last_error());

    bool found_text = false;
    for (const auto& blk : loaded.blocks()) {
        if (blk.name.rfind("*D", 0) != 0) continue;
        for (const auto& e : blk.entities) {
            if (e.type != hdxf::EntityType::Text) continue;
            found_text = true;
            hdxf_test::assert_eq(e.as_text().height, 7.0, "dimension text height from STANDARD");
        }
    }
    hdxf_test::assert_true(found_text, "no dimension text generated");
}

TEST(fix1_defaults_still_written_when_missing) {
    hdxf::Drawing dwg;
    std::string out = to_text(dwg.save_to_memory());
    hdxf_test::assert_eq(count_records(out, "STYLE", "Standard"), 1);
    hdxf_test::assert_eq(count_records(out, "DIMSTYLE", "Standard"), 1);
    hdxf_test::assert_eq(count_records(out, "LTYPE", "BYBLOCK"), 0);
    hdxf_test::assert_eq(count_records(out, "LTYPE", "BYLAYER"), 0);
    hdxf_test::assert_eq(count_records(out, "LTYPE", "CONTINUOUS"), 1);
    hdxf_test::assert_eq(count_records(out, "APPID", "ACAD"), 1);
}

// ---------------------------------------------------------------------------
// Bug 2: unknown entities keep their value types in binary DXF
// ---------------------------------------------------------------------------

TEST(fix2_unknown_entity_binary_value_types) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Unknown;
    hdxf::UnknownEntityData d;
    d.entity_type = "MYENTITY";
    d.group_codes = {
        {8, "0"},
        {10, "1.5"},
        {20, "-2.25"},
        {40, "1e-3"},
        {70, "3"},
        {90, "70000"},
        {1001, "MYAPP"},
        {1004, "0A0BFF"},
        {1040, "2.5"},
        {1070, "-7"},
        {1071, "123456"},
    };
    e.data = d;

    hdxf::Drawing dwg;
    dwg.add_entity(e);
    auto bytes = dwg.save_to_memory(hdxf::DxfFormat::Binary);

    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load_from_memory(bytes.data(), bytes.size()), loaded.last_error());
    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 1);
    const auto& got = loaded.entities()[0].as_unknown();
    hdxf_test::assert_eq(got.entity_type, std::string("MYENTITY"));

    // Values come back with their binary type, so compare them as numbers
    auto value = [&](int code) {
        for (const auto& [c, v] : got.group_codes) {
            if (c == code) return v;
        }
        hdxf_test::assert_true(false, "group " + std::to_string(code) + " missing");
        return std::string();
    };
    hdxf_test::assert_eq(std::strtod(value(10).c_str(), nullptr), 1.5);
    hdxf_test::assert_eq(std::strtod(value(20).c_str(), nullptr), -2.25);
    hdxf_test::assert_eq(std::strtod(value(40).c_str(), nullptr), 1e-3);
    hdxf_test::assert_eq(value(70), std::string("3"));
    hdxf_test::assert_eq(value(90), std::string("70000"));
    hdxf_test::assert_eq(value(1001), std::string("MYAPP"));
    hdxf_test::assert_eq(value(1004), std::string("0A0BFF"));
    hdxf_test::assert_eq(std::strtod(value(1040).c_str(), nullptr), 2.5);
    hdxf_test::assert_eq(value(1070), std::string("-7"));
    hdxf_test::assert_eq(value(1071), std::string("123456"));
}

// ---------------------------------------------------------------------------
// Bug 3: handles of LAYER, LTYPE, STYLE and APPID entries are kept on read
// ---------------------------------------------------------------------------

TEST(fix3_table_entry_handles_kept) {
    const std::string dxf =
        "  0\nSECTION\n  2\nTABLES\n"
        "  0\nTABLE\n  2\nLTYPE\n 70\n1\n"
        "  0\nLTYPE\n  5\nA1\n  2\nDASHED\n 70\n0\n  3\nDashed\n 72\n65\n 73\n2\n 40\n1.0\n 49\n0.5\n 49\n-0.5\n"
        "  0\nENDTAB\n"
        "  0\nTABLE\n  2\nLAYER\n 70\n1\n"
        "  0\nLAYER\n  5\nA2\n  2\nWALLS\n 70\n0\n 62\n1\n  6\nDASHED\n"
        "  0\nENDTAB\n"
        "  0\nTABLE\n  2\nSTYLE\n 70\n1\n"
        "  0\nSTYLE\n  5\nA3\n  2\nNOTES\n 70\n0\n 40\n0.0\n 41\n1.0\n 50\n0.0\n 71\n0\n 42\n2.5\n  3\ntxt\n  4\n\n"
        "  0\nENDTAB\n"
        "  0\nTABLE\n  2\nAPPID\n 70\n1\n"
        "  0\nAPPID\n  5\nA4\n  2\nMYAPP\n 70\n0\n"
        "  0\nENDTAB\n"
        "  0\nENDSEC\n  0\nEOF\n";

    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, dxf), dwg.last_error());
    hdxf_test::assert_eq(dwg.linetypes().at(0).handle, std::string("A1"), "LTYPE handle");
    hdxf_test::assert_eq(dwg.layers().at(0).handle, std::string("A2"), "LAYER handle");
    hdxf_test::assert_eq(dwg.text_styles().at(0).handle, std::string("A3"), "STYLE handle");
    hdxf_test::assert_eq(dwg.app_ids().at(0).handle, std::string("A4"), "APPID handle");
}

// ---------------------------------------------------------------------------
// Bug 4: $HANDSEED is above every handle written
// ---------------------------------------------------------------------------

TEST(fix4_handseed_above_all_handles) {
    hdxf::Drawing dwg;
    dwg.add_layer("A", hdxf::Color::red());
    for (int i = 0; i < 40; ++i) dwg.add_line({0, 0, 0}, {1.0 * i, 1, 0});
    auto bytes = dwg.save_to_memory();

    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load_from_memory(bytes.data(), bytes.size()), loaded.last_error());
    unsigned long seed = std::strtoul(header(loaded, "$HANDSEED")->values[0].second.c_str(), nullptr, 16);

    unsigned long max_handle = 0;
    auto take = [&](const std::string& h) {
        if (!h.empty()) max_handle = std::max(max_handle, std::strtoul(h.c_str(), nullptr, 16));
    };
    for (const auto& e : loaded.entities()) take(e.props.handle);
    for (const auto& l : loaded.layers()) take(l.handle);
    hdxf_test::assert_true(max_handle > 0, "no handles written");
    hdxf_test::assert_true(seed > max_handle, "$HANDSEED must be above the largest handle");
}

// ---------------------------------------------------------------------------
// Bug 5: trailing spaces in string values are kept
// ---------------------------------------------------------------------------

TEST(fix5_trailing_spaces_in_text_kept) {
    hdxf::Drawing dwg;
    dwg.add_text({0, 0, 0}, "Text with spaces  ", 2.5);
    auto bytes = dwg.save_to_memory();

    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load_from_memory(bytes.data(), bytes.size()), loaded.last_error());
    hdxf_test::assert_eq(loaded.entities().at(0).as_text().content, std::string("Text with spaces  "));
}

TEST(fix5_padded_keywords_still_recognised) {
    // Record types, section names and header variable names padded with spaces
    const std::string dxf =
        "  0\nSECTION  \n  2\nHEADER \n  9\n$ACADVER \n  1\nAC1009 \n  0\nENDSEC \n"
        "  0\nSECTION\n  2\nENTITIES  \n"
        "  0\nLINE \n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n 11\n1.0\n 21\n1.0\n 31\n0.0\n"
        "  0\nENDSEC\n  0\nEOF \n";

    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, dxf), dwg.last_error());
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
    hdxf_test::assert_true(dwg.entities()[0].type == hdxf::EntityType::Line);
}

// ---------------------------------------------------------------------------
// Bug 6: a file without HEADER / $ACADVER is read as R12
// ---------------------------------------------------------------------------

TEST(fix6_entities_only_file_read_as_r12) {
    const std::string dxf =
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nCIRCLE\n  8\nCIRCLES\n 10\n1.0\n 20\n2.0\n 30\n0.0\n 40\n1.5\n"
        "  0\nENDSEC\n  0\nEOF\n";

    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, dxf), dwg.last_error());
    hdxf_test::assert_true(dwg.last_error().empty());
    hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R12);
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
    hdxf_test::assert_eq(dwg.entities()[0].as_circle().radius, 1.5);
}

TEST(fix6_data_after_eof_ignored) {
    const std::string dxf =
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nPOINT\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
        "  0\nENDSEC\n  0\nEOF\nnot\nDXF\nat all";

    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, dxf), dwg.last_error());
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);
}

TEST(fix6_non_dxf_rejected) {
    hdxf::Drawing dwg;
    hdxf_test::assert_false(load_text(dwg, "This is not a DXF file.\nJust text.\n"));
    hdxf_test::assert_true(dwg.last_error().find("Not a DXF file") != std::string::npos,
                           dwg.last_error());
}

TEST(fix6_incomplete_or_malformed_rejected) {
    hdxf::Drawing a;
    hdxf_test::assert_false(load_text(a, "  0\nSECTION\n"), "SECTION without a name");

    hdxf::Drawing b;
    hdxf_test::assert_false(load_text(b, "garbage\nSECTION\n  2\nENTITIES\n  0\nENDSEC\n"),
                            "non-numeric group code");
    hdxf_test::assert_true(b.last_error().find("Invalid group code") != std::string::npos,
                           b.last_error());
}

TEST(fix6_binary_r13_read_with_two_byte_codes) {
    // Binary R13+ has two-byte group codes (step 0.3); $ACADVER is read correctly
    std::string dxf("AutoCAD Binary DXF\r\n\x1a", 21);
    dxf += '\0';
    dxf += std::string("\0\0SECTION\0", 10);
    dxf += std::string("\x02\0HEADER\0", 9);
    dxf += std::string("\x09\0$ACADVER\0", 11);
    dxf += std::string("\x01\0AC1015\0", 9);
    dxf += std::string("\0\0ENDSEC\0", 9);
    dxf += std::string("\0\0EOF\0", 6);

    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, dxf), dwg.last_error());
    hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R2000, "version");
}

// ---------------------------------------------------------------------------
// The three open R12 points from phase 0, fixed before phase 1 step 1.4 (as AutoCAD
// writes R12, ACadSharp's sample_AC1009_ascii.dxf): handles, numbers, extents
// (test_extents.cpp)
// ---------------------------------------------------------------------------

namespace {

std::string r12_text(hdxf::Drawing& dwg) {
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    return hdxf_test::lf_text(out);
}

} // namespace

// $HANDLING 1; handles right after 0 on entities, VERTEX, SEQEND, ATTRIB and ENDBLK;
// none on BLOCK or table entries
TEST(r12_handles_as_autocad) {
    hdxf::Drawing dwg;
    dwg.add_layer("L");
    dwg.add_block("B").entities.push_back(dwg.add_line({0, 0, 0}, {1, 0, 0}));
    dwg.entities().clear();
    dwg.add_polyline({{0, 0}, {1, 1}});
    std::string s = r12_text(dwg);

    hdxf_test::assert_true(s.find("$HANDLING\n 70\n     1\n") != std::string::npos, "$HANDLING 1");
    const hdxf::Entity& pl = dwg.entities()[0];
    const hdxf::PolylineData& d = pl.as_polyline();
    hdxf_test::assert_true(s.find("  0\nPOLYLINE\n  5\n" + pl.props.handle + "\n  8\n") != std::string::npos,
                           "5 after 0");
    hdxf_test::assert_false(d.vertices[0].handle.empty(), "VERTEX handle");
    hdxf_test::assert_true(s.find("  0\nVERTEX\n  5\n" + d.vertices[0].handle + "\n") != std::string::npos,
                           "VERTEX 5");
    hdxf_test::assert_true(s.find("  0\nSEQEND\n  5\n" + d.seqend_handle + "\n") != std::string::npos,
                           "SEQEND 5");
    hdxf_test::assert_true(s.find("  0\nENDBLK\n  5\n") != std::string::npos, "ENDBLK 5");
    hdxf_test::assert_true(s.find("  0\nBLOCK\n  5\n") == std::string::npos, "no BLOCK 5");
    hdxf_test::assert_true(s.find("  0\nLAYER\n  5\n") == std::string::npos, "no LAYER 5");
}

// XDATA of POLYLINE and INSERT was written after SEQEND, on the SEQEND record, and lost
// on reading
TEST(r12_xdata_of_polyline_and_insert_kept) {
    hdxf::Drawing dwg;
    dwg.add_app_id("APP");
    dwg.add_block("B");
    hdxf::Entity& pl = dwg.add_polyline({{0, 0}, {1, 1}});
    pl.props.xdata.push_back({"APP", {{1000, "on the polyline"}}});
    hdxf::Entity& ins = dwg.add_insert("B", {0, 0, 0});
    hdxf::InsertAttribute a;
    a.data.tag = "T";
    ins.as_insert().attributes.push_back(a);
    ins.props.xdata.push_back({"APP", {{1000, "on the insert"}}});

    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(static_cast<int>(back.entities()[0].props.xdata.size()), 1, "polyline");
    hdxf_test::assert_eq(back.entities()[0].props.xdata[0].items[0].value, "on the polyline");
    hdxf_test::assert_eq(static_cast<int>(back.entities()[1].props.xdata.size()), 1, "insert");
    hdxf_test::assert_eq(back.entities()[1].props.xdata[0].items[0].value, "on the insert");
}

// 16 decimal places, as DXFOUT writes them by default
TEST(r12_numbers_16_decimals) {
    hdxf::Drawing dwg;
    dwg.add_line({1.0 / 3.0, 0, 0}, {25.4, 0, 0});
    std::string s = r12_text(dwg);
    hdxf_test::assert_true(s.find("\n0.3333333333333333\n") != std::string::npos, "1/3");
    hdxf_test::assert_true(s.find("\n25.3999999999999986\n") != std::string::npos, "25.4");
}

// ---------------------------------------------------------------------------
// R12 read from a file is written back from its records (plan F9 to F22,
// doc/plans/2026-10-25-facit-rattningar.md): a record that did not change is written as it was
// read, one that did has only what changed written anew. The file below is a technical stimulus
// with the forms AutoCAD 2025's R12 files have; the facit itself is the files in doc/facit
// (test_autocad_facit.cpp).
// ---------------------------------------------------------------------------

namespace {

std::string g(int code, const std::string& value) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%3d", code);
    return std::string(buf) + "\n" + value + "\n";
}

std::string header_var(const std::string& name, const std::string& groups) {
    return g(9, name) + groups;
}

// An R12 file: the tables without handles, VIEW with a count of 10 and no records, STYLE and DIMSTYLE
// with counts above their records, and entities in the forms AutoCAD's own exports have
std::string r12_stimulus() {
    std::string s;
    s += g(0, "SECTION") + g(2, "HEADER");
    s += header_var("$ACADVER", g(1, "AC1009"));
    s += header_var("$EXTMIN", g(10, "-1.0") + g(20, "0.5") + g(30, "-0.00000001"));
    s += header_var("$EXTMAX", g(10, "170.0") + g(20, "73.0") + g(30, "0.00000001"));
    s += header_var("$PEXTMIN", g(10, "1.1692913385826771") + g(20, "0.8267716535433073") + g(30, "0.0"));
    s += header_var("$PEXTMAX", g(10, "6.0") + g(20, "4.0") + g(30, "0.0"));
    s += header_var("$HANDLING", g(70, "     1"));
    s += header_var("$HANDSEED", g(5, "100"));
    s += header_var("$DWGCODEPAGE", g(3, "ANSI_1252"));
    s += g(0, "ENDSEC");

    s += g(0, "SECTION") + g(2, "TABLES");
    s += g(0, "TABLE") + g(2, "VPORT") + g(70, "     1");
    s += g(0, "VPORT") + g(2, "*ACTIVE") + g(70, "     0") + g(10, "0.0") + g(20, "0.0") + g(11, "1.0") + g(21, "1.0") +
         g(12, "5.0") + g(22, "5.0") + g(13, "0.0") + g(23, "0.0") + g(14, "0.5") + g(24, "0.5") + g(15, "0.5") +
         g(25, "0.5") + g(16, "0.0") + g(26, "0.0") + g(36, "1.0") + g(17, "0.0") + g(27, "0.0") + g(37, "0.0") +
         g(40, "10.0") + g(41, "1.0") + g(42, "50.0") + g(43, "0.0") + g(44, "0.0") + g(50, "0.0") + g(51, "0.0") +
         g(71, "     0") + g(72, "  1000") + g(73, "     1") + g(74, "     3") + g(75, "     0") + g(76, "     1") +
         g(77, "     0") + g(78, "     0");
    s += g(0, "ENDTAB");
    s += g(0, "TABLE") + g(2, "LTYPE") + g(70, "     1");
    s += g(0, "LTYPE") + g(2, "CONTINUOUS") + g(70, "     0") + g(3, "Solid line") + g(72, "    65") + g(73, "     0") +
         g(40, "0.0");
    s += g(0, "ENDTAB");
    s += g(0, "TABLE") + g(2, "LAYER") + g(70, "     2");
    s += g(0, "LAYER") + g(2, "0") + g(70, "     0") + g(62, "     7") + g(6, "CONTINUOUS");
    s += g(0, "LAYER") + g(2, "WALLS") + g(70, "     0") + g(62, "     3") + g(6, "CONTINUOUS");
    s += g(0, "ENDTAB");
    s += g(0, "TABLE") + g(2, "STYLE") + g(70, "     3");
    s += g(0, "STYLE") + g(2, "STANDARD") + g(70, "     0") + g(40, "0.0") + g(41, "1.0") + g(50, "0.0") + g(71, "     0") +
         g(42, "2.5") + g(3, "txt") + g(4, "");
    s += g(0, "ENDTAB");
    s += g(0, "TABLE") + g(2, "VIEW") + g(70, "    10");
    s += g(0, "ENDTAB");
    s += g(0, "TABLE") + g(2, "UCS") + g(70, "     0");
    s += g(0, "ENDTAB");
    s += g(0, "TABLE") + g(2, "APPID") + g(70, "     2");
    s += g(0, "APPID") + g(2, "ACAD") + g(70, "     0");
    s += g(0, "APPID") + g(2, "HDXF_FACIT") + g(70, "     0");
    s += g(0, "ENDTAB");
    s += g(0, "TABLE") + g(2, "DIMSTYLE") + g(70, "     2");
    s += g(0, "DIMSTYLE") + g(2, "STANDARD") + g(70, "     0") + g(3, "") + g(4, "") + g(5, "") + g(6, "") + g(7, "") +
         g(40, "1.0") + g(41, "2.5") + g(42, "0.625") + g(43, "3.75") + g(44, "0.625") + g(45, "0.0") + g(46, "0.0") +
         g(47, "0.0") + g(48, "0.0") + g(140, "2.5") + g(141, "2.5") + g(142, "0.0") + g(143, "25.4") + g(144, "1.0") +
         g(145, "0.0") + g(146, "1.0") + g(147, "0.625") + g(71, "     0") + g(72, "     0") + g(73, "     0") +
         g(74, "     0") + g(75, "     0") + g(76, "     0") + g(77, "     0") + g(78, "     0") + g(170, "     0") +
         g(171, "     2") + g(172, "     0") + g(173, "     0") + g(174, "     0") + g(175, "     0") +
         g(176, "     0") + g(177, "     0") + g(178, "     0");
    s += g(0, "ENDTAB");
    s += g(0, "ENDSEC");

    s += g(0, "SECTION") + g(2, "BLOCKS");
    s += g(0, "BLOCK") + g(8, "0") + g(2, "$MODEL_SPACE") + g(70, "     0") + g(10, "0.0") + g(20, "0.0") + g(30, "0.0") +
         g(3, "$MODEL_SPACE") + g(1, "");
    s += g(0, "ENDBLK") + g(5, "11") + g(8, "0");
    s += g(0, "BLOCK") + g(67, "     1") + g(8, "0") + g(2, "$PAPER_SPACE") + g(70, "     0") + g(10, "0.0") + g(20, "0.0") +
         g(30, "0.0") + g(3, "$PAPER_SPACE") + g(1, "");
    s += g(0, "ENDBLK") + g(5, "13") + g(67, "     1") + g(8, "0");
    s += g(0, "BLOCK") + g(8, "0") + g(2, "BLK") + g(70, "     0") + g(10, "0.0") + g(20, "0.0") + g(30, "0.0") +
         g(3, "BLK") + g(1, "");
    s += g(0, "LINE") + g(5, "15") + g(8, "0") + g(10, "0.0") + g(20, "0.0") + g(30, "0.0") + g(11, "1.0") + g(21, "1.0") +
         g(31, "0.0");
    s += g(0, "ENDBLK") + g(5, "16") + g(8, "0");
    s += g(0, "BLOCK") + g(8, "0") + g(2, "*D1") + g(70, "     1") + g(10, "0.0") + g(20, "0.0") + g(30, "0.0") +
         g(3, "*D1") + g(1, "");
    s += g(0, "LINE") + g(5, "81") + g(8, "0") + g(10, "0.0") + g(20, "0.0") + g(30, "0.0") + g(11, "10.0") + g(21, "0.0") +
         g(31, "0.0");
    s += g(0, "ENDBLK") + g(5, "82") + g(8, "0");
    s += g(0, "ENDSEC");

    s += g(0, "SECTION") + g(2, "ENTITIES");
    // The extrusion before the geometry, and six-digit numbers
    s += g(0, "LINE") + g(5, "20") + g(8, "0") + g(210, "0.0") + g(220, "0.0") + g(230, "-1.0") + g(10, "0.0") +
         g(20, "0.0") + g(30, "0.0") + g(11, "169.813") + g(21, "0.00000") + g(31, "0.0");
    // A polyline AutoCAD exports: the layer before the handle, its 10 and 20 not 0, six-digit vertices
    s += g(0, "POLYLINE") + g(8, "0") + g(5, "30") + g(66, "     1") + g(10, "5.5") + g(20, "7.25") + g(30, "0.0") +
         g(70, "     8");
    s += g(0, "VERTEX") + g(8, "0") + g(5, "31") + g(10, "170.000") + g(20, "73.4810") + g(30, "0.00000") +
         g(70, "    32") + g(40, "0.0") + g(41, "0.0");
    s += g(0, "VERTEX") + g(5, "32") + g(8, "0") + g(6, "BYBLOCK") + g(62, "     0") + g(10, "1.0") + g(20, "2.0") +
         g(30, "0.0") + g(70, "    32") + g(1001, "HDXF_FACIT") + g(1000, "vertex") + g(1070, "     7");
    s += g(0, "SEQEND") + g(8, "0") + g(5, "33") + g(6, "BYBLOCK") + g(62, "     0");
    // An ordinary polyline
    s += g(0, "POLYLINE") + g(5, "60") + g(8, "0") + g(66, "     1") + g(10, "0.0") + g(20, "0.0") + g(30, "0.0") +
         g(70, "     0");
    s += g(0, "VERTEX") + g(5, "61") + g(8, "0") + g(10, "1.0") + g(20, "1.0") + g(30, "0.0");
    s += g(0, "VERTEX") + g(5, "62") + g(8, "0") + g(10, "9.0") + g(20, "1.0") + g(30, "0.0");
    s += g(0, "SEQEND") + g(5, "63") + g(8, "0");
    // TEXT as AutoCAD exports it in a dimension block: its own order and explicit zeros
    s += g(0, "TEXT") + g(8, "0") + g(40, "0.18") + g(5, "40") + g(62, "     0") + g(72, "     0") + g(71, "     0") +
         g(10, "134.719") + g(20, "19.5427") + g(30, "0.00000") + g(1, "hello");
    // An INSERT with its scales written out
    s += g(0, "INSERT") + g(5, "50") + g(8, "0") + g(2, "BLK") + g(10, "0.0") + g(20, "0.0") + g(30, "0.0") +
         g(41, "1.0") + g(42, "1.0") + g(43, "1.0") + g(50, "74.9999999999999716");
    s += g(0, "CIRCLE") + g(5, "71") + g(8, "WALLS") + g(10, "50.0") + g(20, "20.0") + g(30, "0.0") + g(40, "5.0");
    s += g(0, "LINE") + g(5, "70") + g(67, "     1") + g(8, "0") + g(10, "0.5") + g(20, "0.5") + g(30, "0.0") +
         g(11, "2.0") + g(21, "2.0") + g(31, "0.0");
    // A DIMENSION without 70 and with its style written out (AutoCAD's annotative file)
    s += g(0, "DIMENSION") + g(5, "80") + g(8, "0") + g(2, "*D1") + g(10, "5.0") + g(20, "5.0") + g(30, "0.0") +
         g(11, "5.0") + g(21, "5.0") + g(31, "0.0") + g(3, "STANDARD") + g(13, "0.0") + g(23, "0.0") + g(33, "0.0") +
         g(14, "10.0") + g(24, "0.0") + g(34, "0.0");
    s += g(0, "ENDSEC");
    s += g(0, "EOF");
    return s;
}

// A file's records, each as its type and its groups
using Group = std::pair<int, std::string>;
struct Rec {
    std::string type;
    std::vector<Group> groups;
};

std::vector<Rec> records_of(const std::string& lf) {
    std::vector<Rec> out;
    size_t pos = 0;
    auto next_line = [&]() {
        size_t end = lf.find('\n', pos);
        std::string line = lf.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? lf.size() : end + 1;
        return line;
    };
    while (pos < lf.size()) {
        std::string code_line = next_line();
        std::string value = next_line();
        int code = std::atoi(code_line.c_str());
        while (!value.empty() && value.back() == ' ') value.pop_back();
        size_t first = value.find_first_not_of(' ');
        if (code == 0) {
            out.push_back({first == std::string::npos ? std::string() : value.substr(first), {}});
        } else if (!out.empty()) {
            out.back().groups.push_back({code, first == std::string::npos ? std::string() : value.substr(first)});
        }
    }
    return out;
}

// The nth record of a type
const Rec& nth(const std::vector<Rec>& recs, const std::string& type, int n = 0) {
    for (const auto& r : recs) {
        if (r.type == type && n-- == 0) return r;
    }
    throw hdxf_test::AssertFailure{"no " + type};
}

std::vector<int> codes_of(const Rec& r) {
    std::vector<int> out;
    for (const auto& gp : r.groups) out.push_back(gp.first);
    return out;
}

std::string value_of(const Rec& r, int code, int occurrence = 0) {
    for (const auto& gp : r.groups) {
        if (gp.first == code && occurrence-- == 0) return gp.second;
    }
    return "<none>";
}

// The record with this handle (5)
const Rec& by_handle(const std::vector<Rec>& recs, const std::string& handle) {
    for (const auto& r : recs) {
        for (const auto& gp : r.groups) {
            if (gp.first == 5) {
                if (gp.second == handle) return r;
                break;
            }
        }
    }
    throw hdxf_test::AssertFailure{"no record " + handle};
}

std::string first_difference(const std::string& a, const std::string& b) {
    size_t pos = 0;
    while (pos < a.size() && pos < b.size() && a[pos] == b[pos]) ++pos;
    size_t line = 1;
    for (size_t i = 0; i < pos; ++i) line += a[i] == '\n';
    return "first difference at line " + std::to_string(line) + ": " + a.substr(pos, 30) + " / " + b.substr(pos, 30);
}

hdxf::Drawing load_stimulus(const std::string& text) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, text), dwg.last_error());
    return dwg;
}

std::string save_text(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_true(!out.empty(), dwg.last_error());
    return to_text(out);
}

} // namespace

// A file read and written without a change comes back as it was: its forms, its counts, its numbers
TEST(r12_template_unchanged_comes_back_as_read) {
    std::string in = r12_stimulus();
    hdxf::Drawing dwg = load_stimulus(in);
    std::string out = save_text(dwg);
    hdxf_test::assert_true(out == in, "unchanged: " + first_difference(in, out));
    // The mutable accessors are no change
    dwg.layers();
    dwg.entities();
    dwg.views();
    out = save_text(dwg);
    hdxf_test::assert_true(out == in, "after the accessors: " + first_difference(in, out));
    // Saved twice, the same: nothing drifts
    out = save_text(dwg);
    hdxf_test::assert_true(out == in, "saved again: " + first_difference(in, out));
    hdxf_test::assert_true(dwg.warnings().empty(), "no warnings");
}

// Binary: a binary file written from the records comes back byte for byte
TEST(r12_template_binary_round_trip) {
    hdxf::Drawing ascii = load_stimulus(r12_stimulus());
    std::vector<uint8_t> binary = ascii.save_to_memory(hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(!binary.empty(), ascii.last_error());
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(binary.data(), binary.size()), dwg.last_error());
    std::vector<uint8_t> again = dwg.save_to_memory(hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(again == binary, "binary, unchanged");
}

// The state is a read that worked: a drawing built in memory has none, a failed or a repeated read
// leaves nothing of the earlier file
TEST(r12_template_state_follows_the_reading) {
    std::string in = r12_stimulus();
    hdxf::Drawing fresh;
    fresh.add_line({0, 0, 0}, {1, 1, 0});
    std::string built = save_text(fresh);
    hdxf_test::assert_true(built.find("*ACTIVE") != std::string::npos, "a built drawing is written in AutoCAD's form");

    hdxf::Drawing dwg = load_stimulus(in);
    std::string garbage = "not a dxf file";
    hdxf_test::assert_false(load_text(dwg, garbage), "garbage cannot be read");
    dwg.add_line({0, 0, 0}, {2, 2, 0});
    std::string after_failure = save_text(dwg);
    hdxf_test::assert_true(after_failure.find("169.813") == std::string::npos, "nothing of the earlier file");

    // Read again: as read
    hdxf_test::assert_true(load_text(dwg, in), dwg.last_error());
    hdxf_test::assert_true(save_text(dwg) == in, "read again, unchanged");
}

// Only the record that changed is written anew, the others as they were — also those after it
TEST(r12_template_only_the_changed_record) {
    std::string in = r12_stimulus();
    hdxf::Drawing dwg = load_stimulus(in);
    dwg.entities()[0].as_line().end.x = 5.0;
    std::string out = save_text(dwg);
    std::vector<Rec> recs = records_of(out);
    const Rec& line = nth(recs, "LINE", 2);              // (0 is the block's)
    hdxf_test::assert_eq(value_of(line, 11), "5.0", "the changed end");
    hdxf_test::assert_eq(value_of(line, 21), "0.00000", "the end's y, as it was");
    hdxf_test::assert_eq(codes_of(line)[2], 210, "its extrusion stays before the geometry (F11)");
    // The records around it, as read
    hdxf_test::assert_true(out.find("170.000") != std::string::npos, "the vertices' six digits");
    hdxf_test::assert_true(out.find(g(0, "TEXT") + g(8, "0") + g(40, "0.18") + g(5, "40")) != std::string::npos,
                           "TEXT in its order");
    hdxf_test::assert_true(out.find(g(0, "INSERT") + g(5, "50") + g(8, "0") + g(2, "BLK")) != std::string::npos,
                           "INSERT as read");
}

// F9: the extents are the file's while the geometry is as it was read; a space whose geometry
// changed gets new ones, and the other space keeps its own (F19)
TEST(r12_template_extents_follow_the_geometry) {
    std::string in = r12_stimulus();
    // Nothing of the geometry changes: a color, a header text
    hdxf::Drawing dwg = load_stimulus(in);
    dwg.entities()[1].props.color = hdxf::Color::from_aci(1);
    std::string out = save_text(dwg);
    std::vector<Rec> recs = records_of(out);
    hdxf_test::assert_eq(value_of(nth(recs, "POLYLINE"), 62), "1", "the color is written");
    const std::string extents = g(9, "$EXTMIN") + g(10, "-1.0") + g(20, "0.5") + g(30, "-0.00000001") + g(9, "$EXTMAX") +
                                g(10, "170.0") + g(20, "73.0") + g(30, "0.00000001");
    hdxf_test::assert_true(out.find(extents) != std::string::npos, "EXTMIN and EXTMAX as the file has them");
    const std::string paper = g(9, "$PEXTMIN") + g(10, "1.1692913385826771") + g(20, "0.8267716535433073") + g(30, "0.0");
    hdxf_test::assert_true(out.find(paper) != std::string::npos, "PEXTMIN as the file has it");

    // The model's geometry changes: its extents are made new, the paper's are the file's
    hdxf::Drawing moved = load_stimulus(in);
    moved.entities()[0].as_line().end = {300.0, 0.0, 0.0};
    out = save_text(moved);
    hdxf_test::assert_true(out.find(extents) == std::string::npos, "the model's extents are new");
    hdxf_test::assert_true(out.find(paper) != std::string::npos, "the paper's are the file's");
    hdxf::Drawing back = load_stimulus(out);
    hdxf_test::assert_true(back.find_header("$EXTMAX")->values[0].second.find("300") == 0, "EXTMAX reaches the line");

    // The paper's geometry changes: its extents are made new, the model's are the file's
    hdxf::Drawing sheet = load_stimulus(in);
    sheet.entities()[6].as_line().end = {20.0, 20.0, 0.0};
    out = save_text(sheet);
    hdxf_test::assert_true(out.find(extents) != std::string::npos, "the model's are the file's");
    hdxf_test::assert_true(out.find(paper) == std::string::npos, "the paper's are new");

    // A header the program set itself is what is written
    hdxf::Drawing set = load_stimulus(in);
    set.entities()[0].as_line().end = {300.0, 0.0, 0.0};
    set.set_header_point("$EXTMIN", {-5.0, -6.0, 0.0});
    out = save_text(set);
    hdxf::Drawing back_set = load_stimulus(out);
    hdxf_test::assert_eq(back_set.find_header("$EXTMIN")->values[0].second, "-5.0", "the program's EXTMIN");
}

// F10: the count of a table is the file's while the table has the records it had; a table made now
// has the number of its records
TEST(r12_template_table_counts) {
    std::string in = r12_stimulus();
    hdxf::Drawing dwg = load_stimulus(in);
    dwg.layers()[1].color = hdxf::Color::from_aci(5);       // a record changes, not the table
    dwg.dim_styles()[0].dimasz = 3.0;
    std::string out = save_text(dwg);
    hdxf_test::assert_true(out.find(g(0, "TABLE") + g(2, "VIEW") + g(70, "    10")) != std::string::npos, "VIEW stays 10");
    hdxf_test::assert_true(out.find(g(0, "TABLE") + g(2, "STYLE") + g(70, "     3")) != std::string::npos, "STYLE stays 3");
    hdxf_test::assert_true(out.find(g(0, "TABLE") + g(2, "DIMSTYLE") + g(70, "     2")) != std::string::npos, "DIMSTYLE stays 2");
    std::vector<Rec> recs = records_of(out);
    hdxf_test::assert_eq(value_of(nth(recs, "LAYER", 1), 62), "5", "the layer's color");
    hdxf_test::assert_eq(value_of(nth(recs, "LAYER", 0), 62), "7", "the other layer as it was");
    hdxf_test::assert_eq(value_of(nth(recs, "DIMSTYLE"), 41), "3.0", "the dimension style's arrow size");

    // Another record in the table: the file's count plus the record (AutoCAD, the facit view_added/style_added/
    // ltype_added); a record taken away does not lower it (the _removed files)
    hdxf::View v;
    v.name = "HDXF_VIEW";
    v.height = 100.0;
    v.width = 200.0;
    dwg.views().push_back(v);
    out = save_text(dwg);
    hdxf_test::assert_true(out.find(g(0, "TABLE") + g(2, "VIEW") + g(70, "    11")) != std::string::npos, "VIEW with a view");
    // F18: the width after the center
    std::vector<int> view_codes = codes_of(nth(records_of(out), "VIEW"));
    std::vector<int> expected = {2, 70, 40, 10, 20, 41, 11, 21, 31, 12, 22, 32, 42, 43, 44, 50, 71};
    hdxf_test::assert_true(view_codes == expected, "VIEW as AutoCAD 2025 writes it");

    dwg.views().pop_back();
    out = save_text(dwg);
    hdxf_test::assert_true(out.find(g(0, "TABLE") + g(2, "VIEW") + g(70, "    10")) != std::string::npos, "VIEW without it");

    // A new drawing counts its records
    hdxf::Drawing fresh;
    fresh.views().push_back(v);
    std::string built = save_text(fresh);
    hdxf_test::assert_true(built.find(g(0, "TABLE") + g(2, "VIEW") + g(70, "     1")) != std::string::npos, "built");
}

// F11: the extrusion follows the geometry of LINE, CIRCLE, ARC and SOLID, in a new drawing too; the
// order of a record that was read stays
TEST(r12_extrusion_after_the_geometry) {
    hdxf::Drawing dwg;
    hdxf::Point3 n = {0.0, 0.0, -1.0};
    dwg.add_line({0, 0, 0}, {1, 2, 3}).props.extrusion = n;
    dwg.add_circle({0, 0, 0}, 2.0).props.extrusion = n;
    dwg.add_arc({0, 0, 0}, 2.0, 10.0, 80.0).props.extrusion = n;
    dwg.add_solid({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}).props.extrusion = n;
    std::vector<Rec> recs = records_of(save_text(dwg));
    hdxf_test::assert_true(codes_of(nth(recs, "LINE")) == (std::vector<int>{5, 8, 10, 20, 30, 11, 21, 31, 210, 220, 230}), "LINE");
    hdxf_test::assert_true(codes_of(nth(recs, "CIRCLE")) == (std::vector<int>{5, 8, 10, 20, 30, 40, 210, 220, 230}), "CIRCLE");
    hdxf_test::assert_true(codes_of(nth(recs, "ARC")) == (std::vector<int>{5, 8, 10, 20, 30, 40, 210, 220, 230, 50, 51}), "ARC");
    hdxf_test::assert_true(codes_of(nth(recs, "SOLID")) ==
                               (std::vector<int>{5, 8, 10, 20, 30, 11, 21, 31, 12, 22, 32, 13, 23, 33, 210, 220, 230}),
                           "SOLID");
    // Read back, the normal stays; a changed line keeps the order it was read in
    hdxf::Drawing back = load_stimulus(save_text(dwg));
    hdxf_test::assert_eq(back.entities()[0].props.extrusion.z, -1.0, "the normal");

    hdxf::Drawing read = load_stimulus(r12_stimulus());
    read.entities()[0].as_line().start.y = 4.0;
    std::vector<Rec> out = records_of(save_text(read));
    hdxf_test::assert_eq(value_of(nth(out, "LINE", 2), 230), "-1.0", "the normal as read");
    hdxf_test::assert_eq(codes_of(nth(out, "LINE", 2))[2], 210, "before the geometry, as read");
    hdxf_test::assert_eq(value_of(nth(out, "LINE", 2), 20), "4.0", "the changed start");
}

// F12, F13: the order of 8 and 5 and the six-digit numbers stay with a record that was read; a
// changed number is written as a number; a new polyline has the ordinary order
TEST(r12_polyline_order_and_digits_of_a_read_file) {
    std::string in = r12_stimulus();
    hdxf::Drawing dwg = load_stimulus(in);
    auto& pl = dwg.entities()[1].as_polyline();
    pl.vertices[0].flags = 48;                    // another field of the vertex
    pl.vertices[1].position.x = 1.5;              // a coordinate of another
    hdxf::Vertex added;
    added.position = {3.0, 4.0, 0.0};
    pl.vertices.push_back(added);
    std::vector<Rec> recs = records_of(save_text(dwg));
    const Rec& poly = by_handle(recs, "30");
    hdxf_test::assert_true(codes_of(poly).size() > 2 && codes_of(poly)[0] == 8 && codes_of(poly)[1] == 5, "POLYLINE: 8 before 5");
    hdxf_test::assert_eq(value_of(poly, 10), "5.5", "its 10, as read (F14)");
    const Rec& v0 = by_handle(recs, "31");
    hdxf_test::assert_true(codes_of(v0)[0] == 8 && codes_of(v0)[1] == 5, "VERTEX: 8 before 5");
    hdxf_test::assert_eq(value_of(v0, 70), "48", "the changed flags");
    hdxf_test::assert_eq(value_of(v0, 10), "170.000", "the six digits stay");
    hdxf_test::assert_eq(value_of(v0, 20), "73.4810", "the six digits stay");
    hdxf_test::assert_eq(value_of(v0, 41), "0.0", "the explicit zero width (F16)");
    const Rec& v1 = by_handle(recs, "32");
    hdxf_test::assert_eq(value_of(v1, 10), "1.5", "the new coordinate");
    hdxf_test::assert_eq(value_of(v1, 20), "2.0", "the other as read");
    const Rec& seqend = by_handle(recs, "33");
    hdxf_test::assert_true(codes_of(seqend)[0] == 8 && codes_of(seqend)[1] == 5, "SEQEND: 8 before 5");
    // The ordinary polyline and the vertex made now: 5 before 8
    const Rec& ordinary = by_handle(recs, "60");
    hdxf_test::assert_true(codes_of(ordinary)[0] == 5 && codes_of(ordinary)[1] == 8, "an ordinary polyline: 5 before 8");
    hdxf::Drawing plain;
    plain.add_polyline({{0, 0}, {1, 1}});
    std::vector<Rec> built = records_of(save_text(plain));
    const Rec& vertex = nth(built, "VERTEX");
    hdxf_test::assert_true(codes_of(vertex)[0] == 5 && codes_of(vertex)[1] == 8, "a new VERTEX: 5 before 8");
    hdxf_test::assert_true(codes_of(nth(built, "POLYLINE")) == (std::vector<int>{5, 8, 66, 10, 20, 30}), "a new POLYLINE");
}

// F14: the POLYLINE record's 10 and 20 are typed, apart from the vertices and the extents
TEST(r12_polyline_header_point) {
    hdxf::Drawing dwg = load_stimulus(r12_stimulus());
    auto& pl = dwg.entities()[1].as_polyline();
    hdxf_test::assert_eq(pl.header_x, 5.5, "10");
    hdxf_test::assert_eq(pl.header_y, 7.25, "20");
    hdxf_test::assert_eq(pl.elevation, 0.0, "30");
    pl.header_x = 6.5;
    pl.elevation = 2.0;
    std::vector<Rec> recs = records_of(save_text(dwg));
    const Rec& poly = by_handle(recs, "30");
    hdxf_test::assert_eq(value_of(poly, 10), "6.5", "the changed 10");
    hdxf_test::assert_eq(value_of(poly, 20), "7.25", "20 as read");
    hdxf_test::assert_eq(value_of(poly, 30), "2.0", "the changed 30");
    hdxf::Drawing back = load_stimulus(save_text(dwg));
    hdxf_test::assert_eq(back.entities()[1].as_polyline().header_x, 6.5, "read back");
    hdxf_test::assert_eq(back.entities()[1].as_polyline().vertices[0].position.x, 170.0, "the vertices are apart");

    // A new polyline has 0 and 0
    hdxf::Drawing built;
    built.add_polyline({{0, 0}, {1, 1}});
    hdxf_test::assert_eq(built.entities()[0].as_polyline().header_x, 0.0);
    hdxf_test::assert_eq(value_of(nth(records_of(save_text(built)), "POLYLINE"), 10), "0.0");
}

// F15, F16: a vertex and a SEQEND have their own linetype, color and XDATA
TEST(r12_vertex_own_properties) {
    std::string in = r12_stimulus();
    hdxf::Drawing dwg = load_stimulus(in);
    auto& pl = dwg.entities()[1].as_polyline();
    hdxf_test::assert_eq(pl.vertices[1].linetype, "BYBLOCK", "the vertex's linetype");
    hdxf_test::assert_eq(pl.vertices[1].color.aci, 0, "its color is BYBLOCK, not BYLAYER");
    hdxf_test::assert_eq(pl.seqend_linetype, "BYBLOCK", "SEQEND's linetype");
    hdxf_test::assert_eq(pl.seqend_color.aci, 0, "SEQEND's color");
    hdxf_test::assert_eq(static_cast<int>(pl.vertices[1].xdata.size()), 1, "the vertex's XDATA (F16)");
    hdxf_test::assert_eq(pl.vertices[1].xdata[0].app_name, "HDXF_FACIT");
    hdxf_test::assert_true(pl.vertices[0].layer.empty(), "a vertex on its polyline's layer has none of its own");

    // One vertex's color changes and the others are as they were
    pl.vertices[0].color = hdxf::Color::from_aci(1);
    pl.vertices[1].position.y = 2.5;                  // the position of the one with XDATA and widths
    std::vector<Rec> recs = records_of(save_text(dwg));
    const Rec& v0 = by_handle(recs, "31");
    hdxf_test::assert_eq(value_of(v0, 62), "1", "the color");
    const Rec& v1 = by_handle(recs, "32");
    hdxf_test::assert_eq(value_of(v1, 6), "BYBLOCK", "BYBLOCK stays");
    hdxf_test::assert_eq(value_of(v1, 62), "0", "62 = 0 stays");
    hdxf_test::assert_eq(value_of(v1, 20), "2.5", "the new y");
    hdxf_test::assert_eq(value_of(v1, 1001), "HDXF_FACIT", "the XDATA once");
    hdxf_test::assert_eq(value_of(v1, 1001, 1), "<none>", "only once");
    hdxf_test::assert_eq(value_of(by_handle(recs, "31"), 40), "0.0", "the zero width stays");

    // XDATA changed as a unit, and removed
    pl.vertices[1].xdata[0].items[1].value = "9";
    std::vector<Rec> changed = records_of(save_text(dwg));
    hdxf_test::assert_eq(value_of(by_handle(changed, "32"), 1070), "9", "the changed XDATA");
    pl.vertices[1].xdata.clear();
    std::vector<Rec> gone = records_of(save_text(dwg));
    hdxf_test::assert_eq(value_of(by_handle(gone, "32"), 1001), "<none>", "no XDATA");

    // New vertices: their own properties, and the polyline's layer unless they have one
    hdxf::Drawing built;
    hdxf::Entity& e = built.add_polyline({{0, 0}, {1, 1}}, false, "WALLS");
    auto& made = e.as_polyline();
    made.vertices[0].layer = "OTHER";
    made.vertices[1].linetype = "BYBLOCK";
    made.vertices[1].color = hdxf::Color::by_block();
    made.vertices[1].xdata.push_back({"APP", {{1000, "text"}}});
    made.seqend_color = hdxf::Color::by_block();
    built.add_app_id("APP");
    std::vector<Rec> out = records_of(save_text(built));
    hdxf_test::assert_eq(value_of(nth(out, "VERTEX", 0), 8), "OTHER", "its own layer");
    hdxf_test::assert_eq(value_of(nth(out, "VERTEX", 1), 8), "WALLS", "the polyline's layer");
    hdxf_test::assert_eq(value_of(nth(out, "VERTEX", 1), 6), "BYBLOCK");
    hdxf_test::assert_eq(value_of(nth(out, "VERTEX", 1), 62), "0");
    hdxf_test::assert_eq(value_of(nth(out, "VERTEX", 1), 1001), "APP");
    hdxf_test::assert_eq(value_of(nth(out, "SEQEND"), 62), "0");
    hdxf::Drawing again = load_stimulus(save_text(built));
    hdxf_test::assert_eq(again.entities()[0].as_polyline().vertices[1].xdata[0].app_name, "APP", "read back");
}

// F17: DIMENSION as AutoCAD 2025 writes R12: the style after 70 and 1 and not at all for the
// standard one, 70 only when it is not 0, 16 for the 2-line angular only, 40 for radius and diameter
TEST(r12_dimension_groups_as_autocad) {
    hdxf::Drawing dwg;
    dwg.add_dim_style("Other");
    auto& linear = dwg.add_dimension(hdxf::DimensionType::Rotated, {5, 5, 0}, {5, 5, 0});
    linear.as_dimension().def_point2 = {0, 0, 0};
    linear.as_dimension().def_point3 = {10, 0, 0};
    auto& styled = dwg.add_dimension(hdxf::DimensionType::Aligned, {5, 5, 0}, {5, 5, 0});
    styled.as_dimension().dim_style = "Other";
    styled.as_dimension().text_override = "<>";
    auto& radius = dwg.add_dimension(hdxf::DimensionType::Radius, {0, 0, 0}, {5, 5, 0});
    radius.as_dimension().def_point4 = {5, 0, 0};
    auto& three = dwg.add_dimension(hdxf::DimensionType::Angular3Pt, {5, 5, 0}, {5, 5, 0});
    auto& four = dwg.add_dimension(hdxf::DimensionType::Angular, {5, 5, 0}, {5, 5, 0});
    (void)three;
    (void)four;
    std::vector<Rec> recs = records_of(save_text(dwg));
    auto without_xdata = [](const Rec& r) {
        std::vector<int> c;
        for (int code : codes_of(r)) if (code < 1000) c.push_back(code);
        return c;
    };
    // The first DIMENSION is the linear one: the standard style is left out
    std::vector<int> c0 = without_xdata(nth(recs, "DIMENSION", 0));
    hdxf_test::assert_true(std::find(c0.begin(), c0.end(), 3) == c0.end(), "no 3 for the standard style");
    // The other style comes after 70 and 1, before 13
    std::vector<int> c1 = without_xdata(nth(recs, "DIMENSION", 1));
    auto at = [&](const std::vector<int>& c, int code) { return std::find(c.begin(), c.end(), code) - c.begin(); };
    hdxf_test::assert_true(at(c1, 3) > at(c1, 1) && at(c1, 1) > at(c1, 70) && at(c1, 3) < at(c1, 13), "3 after 70 and 1, before 13");
    // Radius: 70, no 3, 15 and 40 (also at 0)
    std::vector<int> c2 = without_xdata(nth(recs, "DIMENSION", 2));
    hdxf_test::assert_true(at(c2, 40) < static_cast<long>(c2.size()), "40 for a radius dimension");
    hdxf_test::assert_true(at(c2, 3) == static_cast<long>(c2.size()), "no style");
    // Angular, 3 points: no 16; 2 lines: 16
    std::vector<int> c3 = without_xdata(nth(recs, "DIMENSION", 3));
    std::vector<int> c4 = without_xdata(nth(recs, "DIMENSION", 4));
    hdxf_test::assert_true(at(c3, 16) == static_cast<long>(c3.size()), "no 16 for the 3-point angular dimension");
    hdxf_test::assert_true(at(c4, 16) < static_cast<long>(c4.size()), "16 for the 2-line one");

    // A DIMENSION as read: without 70 and with its style where it stood, also when a point changes
    hdxf::Drawing read = load_stimulus(r12_stimulus());
    read.entities()[7].as_dimension().def_point3.x = 12.0;
    std::vector<Rec> out = records_of(save_text(read));
    const Rec& d = by_handle(out, "80");
    hdxf_test::assert_true(codes_of(d) == (std::vector<int>{5, 8, 2, 10, 20, 30, 11, 21, 31, 3, 13, 23, 33, 14, 24, 34}),
                           "no 70, the style after 11");
    hdxf_test::assert_eq(value_of(d, 3), "STANDARD", "the style as it was written");
    hdxf_test::assert_eq(value_of(d, 14), "12.0", "the changed point");
}

// F20: an ATTRIB's XDATA once, on the ATTRIB
TEST(r12_attrib_xdata_once) {
    hdxf::Drawing dwg;
    dwg.add_app_id("APPX");
    dwg.add_block("B");
    hdxf::Entity& ins = dwg.add_insert("B", {0, 0, 0});
    hdxf::InsertAttribute a;
    a.data.tag = "T";
    a.data.value = "v";
    a.props.xdata.push_back({"APPX", {{1000, "once"}}});
    ins.as_insert().attributes.push_back(a);
    std::string out = save_text(dwg);
    size_t first = out.find("APPX\n1000");
    hdxf_test::assert_true(first != std::string::npos, "the XDATA is there");
    hdxf_test::assert_true(out.find("APPX\n1000", first + 1) == std::string::npos, "and once");
    std::vector<Rec> recs = records_of(out);
    hdxf_test::assert_eq(value_of(nth(recs, "ATTRIB"), 1001), "APPX", "on the ATTRIB");
    hdxf_test::assert_eq(value_of(nth(recs, "INSERT"), 1001), "<none>", "not on the INSERT");
    hdxf_test::assert_eq(value_of(nth(recs, "SEQEND"), 1001), "<none>", "not on SEQEND");
    hdxf::Drawing back = load_stimulus(out);
    hdxf_test::assert_eq(static_cast<int>(back.entities()[0].as_insert().attributes[0].props.xdata.size()), 1, "read back once");
    std::string again = save_text(back);
    hdxf_test::assert_true(again.find("APPX\n1000", again.find("APPX\n1000") + 1) == std::string::npos, "and written back once");
}

// F21: an INSERT's scales that are written as 1 stay while another field changes, and a changed one
// is written with the others
TEST(r12_insert_explicit_scales) {
    hdxf::Drawing dwg = load_stimulus(r12_stimulus());
    auto& ins = dwg.entities()[4].as_insert();
    ins.rotation = 30.0;
    std::vector<Rec> recs = records_of(save_text(dwg));
    const Rec& r = by_handle(recs, "50");
    hdxf_test::assert_eq(value_of(r, 41), "1.0", "41 stays");
    hdxf_test::assert_eq(value_of(r, 42), "1.0", "42 stays");
    hdxf_test::assert_eq(value_of(r, 43), "1.0", "43 stays");
    hdxf_test::assert_eq(value_of(r, 50), "30.0", "the new rotation");
    ins.x_scale = 2.0;
    recs = records_of(save_text(dwg));
    hdxf_test::assert_eq(value_of(by_handle(recs, "50"), 41), "2.0", "the new scale");
    hdxf_test::assert_eq(value_of(by_handle(recs, "50"), 42), "1.0", "the others stay");
    // A new INSERT has no scale groups at 1
    hdxf::Drawing built;
    built.add_block("B");
    built.add_insert("B", {1, 2, 3});
    std::vector<Rec> out = records_of(save_text(built));
    hdxf_test::assert_eq(value_of(nth(out, "INSERT"), 41), "<none>", "no 41 for a new one");
}

// F22: a TEXT as it was read keeps its order, its zero groups and its numbers when its text changes
TEST(r12_text_exported_form_stays) {
    hdxf::Drawing dwg = load_stimulus(r12_stimulus());
    dwg.entities()[3].as_text().content = "changed";
    std::vector<Rec> recs = records_of(save_text(dwg));
    const Rec& t = by_handle(recs, "40");
    hdxf_test::assert_true(codes_of(t) == (std::vector<int>{8, 40, 5, 62, 72, 71, 10, 20, 30, 1}), "the order and the zeros");
    hdxf_test::assert_eq(value_of(t, 1), "changed", "the new text");
    hdxf_test::assert_eq(value_of(t, 30), "0.00000", "the six digits");
    hdxf_test::assert_eq(value_of(t, 10), "134.719");
    // A new TEXT has the ordinary order
    hdxf::Drawing built;
    built.add_text({1, 2, 0}, "new", 2.5);
    hdxf_test::assert_true(codes_of(nth(records_of(save_text(built)), "TEXT")) == (std::vector<int>{5, 8, 10, 20, 30, 40, 1}), "ordinary");
}

// The records without a handle (the tables) are found again: one is changed, the others stay;
// records removed and added; a block
TEST(r12_template_records_without_handles) {
    std::string in = r12_stimulus();
    hdxf::Drawing dwg = load_stimulus(in);
    dwg.entities().erase(dwg.entities().begin() + 5);        // the CIRCLE
    std::string out = save_text(dwg);
    hdxf_test::assert_true(out.size() < in.size(), "an entity is gone");
    hdxf_test::assert_true(out.find(g(0, "CIRCLE")) == std::string::npos, "the circle");
    hdxf_test::assert_true(out.find(g(0, "TEXT") + g(8, "0") + g(40, "0.18")) != std::string::npos, "the others as they were");
    // A new layer, a renamed one
    hdxf::Drawing layers = load_stimulus(in);
    layers.layers()[0].name = "ZERO";
    layers.add_layer("NEWLAYER").color = hdxf::Color::from_aci(2);
    std::vector<Rec> recs = records_of(save_text(layers));
    hdxf_test::assert_eq(value_of(nth(recs, "LAYER", 0), 2), "ZERO", "the renamed layer");
    hdxf_test::assert_eq(value_of(nth(recs, "LAYER", 1), 2), "WALLS", "the other as it was");
    hdxf_test::assert_eq(value_of(nth(recs, "LAYER", 2), 2), "NEWLAYER", "the new one last");
    // A block changed
    hdxf::Drawing blocks = load_stimulus(in);
    blocks.find_block("BLK")->base_point = {1, 2, 0};
    recs = records_of(save_text(blocks));
    const Rec* blk = nullptr;
    for (const auto& r : recs) if (r.type == "BLOCK" && value_of(r, 2) == "BLK") blk = &r;
    hdxf_test::assert_true(blk != nullptr, "the block");
    hdxf_test::assert_eq(value_of(*blk, 10), "1.0", "its base point");
    hdxf_test::assert_eq(value_of(*blk, 1), "", "its empty xref path as it was");
}
