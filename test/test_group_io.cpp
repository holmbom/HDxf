#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Group code layer (phase 0, step 0.3 — doc/plans/2026-10-22-fas-0-grund.md)
// ---------------------------------------------------------------------------

using hdxf::internal::BinaryCodeWidth;
using hdxf::internal::GroupCode;
using hdxf::internal::GroupReader;
using hdxf::internal::GroupValueType;
using hdxf::internal::GroupWriter;
using hdxf::internal::group_value_type;

namespace {

// One group of every value type, with values that survive the text conversion exactly
const std::vector<GroupCode> every_type = {
    {0, "SECTION"},
    {2, "HEADER"},
    {9, "$ACADVER"},
    {1, "AC1015"},
    {10, "1.5"},                        // Double
    {70, "-12"},                        // Int16
    {90, "123456789"},                  // Int32
    {160, "-1234567890123"},            // Int64
    {280, "300"},                       // Int16 (not 8-bit)
    {290, "1"},                         // Bool
    {310, "00FF10A0"},                  // binary chunk
    {330, "FFFFFFFFFFFFFF1C"},          // handle (string)
    {450, "-7"},                        // Int32
    {1000, "xdata string with spaces "},
    {1004, "DEADBEEF"},                 // binary chunk, code above 255
    {1040, "-0.25"},                    // Double
    {1060, "42"},                       // Int16
    {1071, "2000000000"},               // Int32
    {0, "ENDSEC"},
    {0, "EOF"},
};

std::string write_groups(hdxf::DxfFormat format, BinaryCodeWidth width,
                         const std::vector<GroupCode>& groups) {
    std::ostringstream out(std::ios::binary);
    GroupWriter writer(out, format, width);
    if (format == hdxf::DxfFormat::Binary) writer.write_sentinel();
    for (const auto& g : groups) writer.write_typed(g.code, g.value);
    return out.str();
}

void check_round_trip(hdxf::DxfFormat format, BinaryCodeWidth width) {
    std::string data = write_groups(format, width, every_type);

    std::istringstream in(data, std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    hdxf_test::assert_true(reader.binary() == (format == hdxf::DxfFormat::Binary),
                           "binary detected");
    if (reader.binary()) {
        hdxf_test::assert_true(reader.code_width() == width, "code width detected");
    }

    GroupCode gc;
    for (const auto& expected : every_type) {
        hdxf_test::assert_true(reader.read(gc), "group " + std::to_string(expected.code) +
                                                ": " + reader.error());
        hdxf_test::assert_eq(gc.code, expected.code);
        // ASCII integers come back right-aligned, as AutoCAD writes them
        std::string value = dxf_structure::number_text(gc.code, gc.value);
        hdxf_test::assert_eq(value, expected.value,
                             "group " + std::to_string(expected.code) + ": \"" + gc.value + "\"");
    }
    hdxf_test::assert_false(reader.read(gc), "nothing after EOF");
}

std::string bytes(std::initializer_list<int> list) {
    std::string s;
    for (int b : list) s += static_cast<char>(b);
    return s;
}

} // namespace

TEST(group_value_types) {
    hdxf_test::assert_true(group_value_type(1) == GroupValueType::String, "1");
    hdxf_test::assert_true(group_value_type(10) == GroupValueType::Double, "10");
    hdxf_test::assert_true(group_value_type(70) == GroupValueType::Int16, "70");
    hdxf_test::assert_true(group_value_type(90) == GroupValueType::Int32, "90");
    hdxf_test::assert_true(group_value_type(101) == GroupValueType::String, "101");
    hdxf_test::assert_true(group_value_type(160) == GroupValueType::Int64, "160");
    hdxf_test::assert_true(group_value_type(280) == GroupValueType::Int16, "280");
    hdxf_test::assert_true(group_value_type(290) == GroupValueType::Bool, "290");
    hdxf_test::assert_true(group_value_type(310) == GroupValueType::BinaryChunk, "310");
    hdxf_test::assert_true(group_value_type(320) == GroupValueType::String, "320");
    hdxf_test::assert_true(group_value_type(450) == GroupValueType::Int32, "450");
    hdxf_test::assert_true(group_value_type(1004) == GroupValueType::BinaryChunk, "1004");
    hdxf_test::assert_true(group_value_type(1009) == GroupValueType::String, "1009");
    hdxf_test::assert_true(group_value_type(1060) == GroupValueType::Int16, "1060");
    hdxf_test::assert_true(group_value_type(1071) == GroupValueType::Int32, "1071");
    hdxf_test::assert_true(group_value_type(80) == GroupValueType::Unknown, "80");
    hdxf_test::assert_true(group_value_type(1072) == GroupValueType::Unknown, "1072");
}

TEST(group_io_round_trip_ascii) {
    check_round_trip(hdxf::DxfFormat::Ascii, BinaryCodeWidth::OneByte);
}

TEST(group_io_round_trip_binary_r12) {
    check_round_trip(hdxf::DxfFormat::Binary, BinaryCodeWidth::OneByte);
}

// The R13+ path is not used for writing drawings until R13+ output is turned on,
// so it is tested here: write → read raw groups → compare
TEST(group_io_round_trip_binary_r13) {
    check_round_trip(hdxf::DxfFormat::Binary, BinaryCodeWidth::TwoBytes);
}

TEST(group_io_binary_r12_bytes) {
    std::string data = write_groups(hdxf::DxfFormat::Binary, BinaryCodeWidth::OneByte,
                                    {{0, "SECTION"}, {1071, "1"}});
    std::string body = data.substr(22);
    // 0 as one byte; 1071 escaped: 255 + int16; Int32 value
    std::string expected = bytes({0}) + std::string("SECTION", 7) + bytes({0}) +
                           bytes({255, 0x2F, 0x04, 1, 0, 0, 0});
    hdxf_test::assert_true(body == expected, "R12 binary bytes");
}

TEST(group_io_binary_r13_bytes) {
    std::string data = write_groups(hdxf::DxfFormat::Binary, BinaryCodeWidth::TwoBytes,
                                    {{0, "SECTION"}, {290, "1"}, {310, "0A0B"}});
    std::string body = data.substr(22);
    // Every code as two bytes; Bool as one byte; chunk as length byte + raw bytes
    std::string expected = bytes({0, 0}) + std::string("SECTION", 7) + bytes({0}) +
                           bytes({0x22, 0x01, 1}) +
                           bytes({0x36, 0x01, 2, 0x0A, 0x0B});
    hdxf_test::assert_true(body == expected, "R13+ binary bytes");
}

TEST(group_io_unknown_code_in_binary) {
    // Code 80 is not in the DXF reference: its size is unknown, so reading must stop
    std::string data("AutoCAD Binary DXF\r\n\x1a", 21);
    data += '\0';
    data += std::string("\0\0SECTION\0", 10);
    data += bytes({80, 0, 1, 2, 3});

    std::istringstream in(data, std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    GroupCode gc;
    hdxf_test::assert_true(reader.read(gc), reader.error());
    hdxf_test::assert_false(reader.read(gc), "code 80 read");
    hdxf_test::assert_true(reader.error().find("80") != std::string::npos, reader.error());
}

// Every binary file in the external test data is read to 0/EOF without an error
TEST(group_io_reads_external_binary_files) {
    const char* files[] = {
        "acadsharp/sample_AC1009_binary.dxf", "acadsharp/sample_AC1015_binary.dxf",
        "acadsharp/sample_AC1018_binary.dxf", "acadsharp/sample_AC1021_binary.dxf",
        "acadsharp/sample_AC1024_binary.dxf", "acadsharp/sample_AC1027_binary.dxf",
        "acadsharp/sample_AC1032_binary.dxf", "ezdxf/bin_dxf_r12.dxf",
        "ezdxf/bin_dxf_r13.dxf", "ezdxf/bin_dxf_r14.dxf", "ezdxf/bin_dxf_r2000.dxf",
    };
    int found = 0;
    for (const char* name : files) {
        std::filesystem::path path =
            hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        found++;

        GroupReader reader(in);
        reader.detect_format();
        hdxf_test::assert_true(reader.binary(), std::string(name) + ": not binary");
        bool r12 = std::string(name).find("AC1009") != std::string::npos ||
                   std::string(name).find("r12") != std::string::npos;
        hdxf_test::assert_true(reader.code_width() == (r12 ? BinaryCodeWidth::OneByte
                                                            : BinaryCodeWidth::TwoBytes),
                               std::string(name) + ": code width");

        GroupCode gc;
        bool eof = false;
        while (reader.read(gc)) {
            if (gc.code == 0 && gc.value == "EOF") { eof = true; break; }
        }
        hdxf_test::assert_true(eof, std::string(name) + ": no EOF - " + reader.error());
    }
    if (found == 0) hdxf_test::skip("external test files missing");
}

// ---------------------------------------------------------------------------
// Group 71 of DATATABLE (ACDBDATATABLE in 2000) is an int32 in binary DXF — a Bool cell's value —
// and an int16 in every other record (faciträttning F5)
// ---------------------------------------------------------------------------

namespace {

// The groups the F5 tests write, with the record each is in
const std::vector<GroupCode> datatable_groups = {
    {0, "DATATABLE"},
    {92, "1"},
    {71, "1"},
    {71, "70000"},          // above the int16 range
    {71, "0"},
    {0, "ACDBDATATABLE"},
    {71, "-5"},
    {0, "LAYER"},
    {71, "7"},              // an int16 again
    {0, "EOF"},
};

} // namespace

TEST(group_io_datatable_71_is_int32_in_binary) {
    // Written typed (text given), by value (write_int) and captured then replayed: the same bytes
    std::string typed = write_groups(hdxf::DxfFormat::Binary, BinaryCodeWidth::TwoBytes, datatable_groups);

    std::ostringstream by_value_out(std::ios::binary);
    GroupWriter by_value(by_value_out, hdxf::DxfFormat::Binary, BinaryCodeWidth::TwoBytes);
    by_value.write_sentinel();
    for (const auto& g : datatable_groups) {
        if (g.code == 71) by_value.write_int(71, std::stoll(g.value));
        else by_value.write_typed(g.code, g.value);
    }
    hdxf_test::assert_true(by_value_out.str() == typed, "write_int writes the same bytes as write_typed");

    // A captured group is written after its record, as the writers have it
    std::ostringstream replay2_out(std::ios::binary);
    GroupWriter replay2(replay2_out, hdxf::DxfFormat::Binary, BinaryCodeWidth::TwoBytes);
    replay2.write_sentinel();
    replay2.write_typed(0, "DATATABLE");
    replay2.write_captured({71, hdxf::internal::CapturedGroup::Kind::Int, "", 1, 0.0});
    std::string expected_prefix = bytes({0, 0, 'D', 'A', 'T', 'A', 'T', 'A', 'B', 'L', 'E', 0, 71, 0, 1, 0, 0, 0});
    hdxf_test::assert_true(replay2_out.str().substr(22) == expected_prefix, "a captured 71 is an int32 in DATATABLE");

    // The bytes of the first cell: 71 as two bytes, then the int32 — and the layer's an int16
    std::string at = typed.substr(22);
    size_t first = at.find(bytes({71, 0}));
    hdxf_test::assert_true(first != std::string::npos, "group 71 written");
    hdxf_test::assert_true(at.compare(first, 6, bytes({71, 0, 1, 0, 0, 0})) == 0, "71=1 as an int32");
    size_t last = at.rfind(bytes({71, 0}));
    hdxf_test::assert_true(at.compare(last, 4, bytes({71, 0, 7, 0})) == 0, "LAYER's 71 is an int16");
    hdxf_test::assert_eq(static_cast<int>(at.size() - last), 4 + 6, "an int16 and EOF follow");

    // Read back: every group, to EOF
    std::istringstream in(typed, std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    GroupCode gc;
    for (const auto& expected : datatable_groups) {
        hdxf_test::assert_true(reader.read(gc), "group " + std::to_string(expected.code) + ": " + reader.error());
        hdxf_test::assert_eq(gc.code, expected.code);
        hdxf_test::assert_eq(gc.value, expected.value);
    }
    hdxf_test::assert_false(reader.read(gc), "nothing after EOF");
}

// ASCII keeps its format: the int32 is right-aligned in nine characters, the int16 in six
TEST(group_io_datatable_71_ascii_alignment) {
    std::string text = write_groups(hdxf::DxfFormat::Ascii, BinaryCodeWidth::TwoBytes, datatable_groups);
    hdxf_test::assert_true(text.find(" 71\n        1\n") != std::string::npos, "DATATABLE's 71 in nine characters");
    hdxf_test::assert_true(text.find(" 71\n     7\n") != std::string::npos, "LAYER's 71 in six");
}
