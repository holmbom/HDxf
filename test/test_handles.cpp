#include "test_framework.hpp"
#include "hdxf.hpp"
#include "dxf_reader.hpp"
#include "handles.hpp"
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Handles and owners (phase 0, step 0.5 — doc/plans/2026-10-22-fas-0-grund.md)
// ---------------------------------------------------------------------------

using hdxf::internal::HandleParse;
using hdxf::internal::parse_handle;

namespace {

std::string header_value(const hdxf::Drawing& dwg, const std::string& name) {
    const hdxf::HeaderVar* h = dwg.find_header(name);
    return (h && !h->values.empty()) ? h->values[0].second : std::string();
}

// A DXF 2000 file read with the internal reader (Drawing reads R13+ from step 0.9)
bool load_r2000(hdxf::Drawing& dwg, const std::string& dxf) {
    std::istringstream in(dxf, std::ios::binary);
    hdxf::internal::DxfReader reader(in);
    return reader.read(dwg);
}

} // namespace

TEST(handle_parse) {
    uint64_t v = 0;
    hdxf_test::assert_true(parse_handle("0", v) == HandleParse::Ok && v == 0, "0");
    hdxf_test::assert_true(parse_handle(" 1A ", v) == HandleParse::Ok && v == 0x1A, "spaces");
    hdxf_test::assert_true(parse_handle("ff", v) == HandleParse::Ok && v == 0xFF, "lower case");
    hdxf_test::assert_true(parse_handle("FFFFFFFFFFFFFFFF", v) == HandleParse::Ok &&
                           v == UINT64_MAX, "16 digits");
    hdxf_test::assert_true(parse_handle("0000FFFFFFFFFFFFFFFF", v) == HandleParse::Ok &&
                           v == UINT64_MAX, "leading zeros do not count");
    hdxf_test::assert_true(parse_handle("10000000000000000", v) == HandleParse::TooLong,
                           "17 digits");
    hdxf_test::assert_true(parse_handle("", v) == HandleParse::Empty, "empty");
    hdxf_test::assert_true(parse_handle("XYZ", v) == HandleParse::NotHex, "not hex");
    hdxf_test::assert_eq(hdxf::internal::format_handle(0xFFFFFFFFFFFFFF1CULL),
                         std::string("FFFFFFFFFFFFFF1C"));
}

// Handles above 0x7FFFFFFF used to be read into an int
TEST(handles_above_31_bits) {
    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {1, 1, 0}).props.handle = "80000000";
    dwg.add_line({0, 0, 0}, {2, 2, 0});
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    hdxf_test::assert_eq(dwg.entities()[1].props.handle, std::string("80000001"));
    hdxf_test::assert_eq(header_value(dwg, "$HANDSEED"), std::string("80000002"));
}

TEST(handles_near_16_digits) {
    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {1, 1, 0}).props.handle = "FFFFFFFFFFFFFF00";
    dwg.add_line({0, 0, 0}, {2, 2, 0});
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf_test::assert_eq(dwg.entities()[1].props.handle, std::string("FFFFFFFFFFFFFF01"));
    hdxf_test::assert_eq(header_value(dwg, "$HANDSEED"), std::string("FFFFFFFFFFFFFF02"));

    // And they survive a round trip
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(back.entities()[1].props.handle, std::string("FFFFFFFFFFFFFF01"));
}

TEST(handles_overflow_is_an_error) {
    // The largest handle leaves nothing free above it
    hdxf::Drawing full;
    full.add_line({0, 0, 0}, {1, 1, 0}).props.handle = "FFFFFFFFFFFFFFFF";
    full.add_line({0, 0, 0}, {2, 2, 0});
    hdxf_test::assert_true(full.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(full.last_error().find("Handle out of range") != std::string::npos,
                           full.last_error());
    hdxf_test::assert_true(full.entities()[1].props.handle.empty(), "drawing changed");

    // More than 16 hex digits
    hdxf::Drawing too_long;
    too_long.add_line({0, 0, 0}, {1, 1, 0}).props.handle = "10000000000000000";
    hdxf_test::assert_true(too_long.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(too_long.last_error().find("10000000000000000") != std::string::npos,
                           too_long.last_error());

    // Free handles above the largest, but too few for what needs one
    hdxf::Drawing almost;
    almost.add_line({0, 0, 0}, {1, 1, 0}).props.handle = "FFFFFFFFFFFFFFFE";
    almost.add_line({0, 0, 0}, {2, 2, 0});
    almost.add_line({0, 0, 0}, {3, 3, 0});
    hdxf_test::assert_true(almost.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(almost.last_error().find("Out of handles") != std::string::npos,
                           almost.last_error());
    hdxf_test::assert_true(almost.entities()[1].props.handle.empty(), "drawing changed");
}

TEST(handles_of_attributes_count) {
    hdxf::Drawing dwg;
    dwg.add_block("B");
    hdxf::Entity& insert = dwg.add_insert("B", {0, 0, 0});
    hdxf::InsertAttribute attr;
    attr.data.tag = "T";
    attr.props.handle = "5000";
    insert.as_insert().attributes.push_back(attr);
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());

    // Every handle given out is above the attribute's
    auto above = [](const std::string& h) {
        uint64_t v = 0;
        return parse_handle(h, v) == HandleParse::Ok && v > 0x5000;
    };
    hdxf_test::assert_true(above(dwg.entities()[0].props.handle), "INSERT");
    hdxf_test::assert_true(above(dwg.entities()[1].props.handle), "LINE");
    // R12 as AutoCAD writes it: ENDBLK has a handle, BLOCK and table entries none
    for (const auto& b : dwg.blocks()) {
        hdxf_test::assert_true(above(b.endblk_handle), "ENDBLK");
        hdxf_test::assert_true(b.handle.empty(), "BLOCK");
    }
    for (const auto& l : dwg.layers()) hdxf_test::assert_true(l.handle.empty(), "LAYER");
}

// Handles that are only in preserved records (here an OBJECTS dictionary and a
// BLOCK_RECORD) are in use too
TEST(handles_in_preserved_records_count) {
    std::string dxf =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nTABLES\n"
        "  0\nTABLE\n  2\nBLOCK_RECORD\n  5\n1\n 70\n1\n"
        "  0\nBLOCK_RECORD\n  5\nAB000000\n  2\n*Model_Space\n  0\nENDTAB\n"
        "  0\nTABLE\n  2\nDIMSTYLE\n  5\n2\n 70\n1\n"
        "  0\nDIMSTYLE\n105\n77\n  2\nStandard\n 70\n0\n  0\nENDTAB\n"
        "  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n  0\nLINE\n  5\n20\n  8\n0\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nOBJECTS\n"
        "  0\nDICTIONARY\n  5\nABCDEF0000\n330\n0\n  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_r2000(dwg, dxf), "load");

    // DIMSTYLE: 105 is the handle in R13+ (5 is the DIMBLK name in R12)
    bool found = false;
    for (const auto& ds : dwg.dim_styles()) {
        if (ds.name == "Standard") {
            found = true;
            hdxf_test::assert_eq(ds.handle, std::string("77"), "DIMSTYLE handle");
            hdxf_test::assert_eq(ds.dimblk, std::string(""), "DIMBLK");
        }
    }
    hdxf_test::assert_true(found, "DIMSTYLE Standard");

    dwg.add_line({0, 0, 0}, {1, 1, 0});
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    hdxf_test::assert_eq(dwg.entities().back().props.handle, std::string("ABCDEF0001"));
}

// A new R12 drawing: the ENDBLK of $MODEL_SPACE and $PAPER_SPACE have handles below $HANDSEED,
// and reading and writing the file again leaves the header as it was
TEST(handseed_is_above_space_endblk_handles) {
    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    std::string file = hdxf_test::lf_text(out);

    uint64_t seed = 0;
    size_t seed_at = file.find("$HANDSEED\n  5\n");
    hdxf_test::assert_true(seed_at != std::string::npos, "$HANDSEED");
    std::string seed_text = file.substr(seed_at + 14, file.find('\n', seed_at + 14) - seed_at - 14);
    hdxf_test::assert_true(parse_handle(seed_text, seed) == HandleParse::Ok, "seed handle");
    int endblks = 0;
    for (size_t at = file.find("ENDBLK\n  5\n"); at != std::string::npos; at = file.find("ENDBLK\n  5\n", at + 1)) {
        std::string text = file.substr(at + 11, file.find('\n', at + 11) - at - 11);
        uint64_t handle = 0;
        hdxf_test::assert_true(parse_handle(text, handle) == HandleParse::Ok, "ENDBLK handle");
        hdxf_test::assert_true(handle < seed, "ENDBLK " + text + " below $HANDSEED " + seed_text);
        ++endblks;
    }
    hdxf_test::assert_eq(endblks, 2, "$MODEL_SPACE and $PAPER_SPACE");

    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    std::vector<uint8_t> again = back.save_to_memory();
    hdxf_test::assert_false(again.empty(), back.last_error());
    std::string second = hdxf_test::lf_text(again);
    hdxf_test::assert_eq(second.substr(0, second.find("ENDSEC")), file.substr(0, file.find("ENDSEC")),
                         "the header after reading and writing");
}
