#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "number_parse.hpp"
#include "dxf_reader.hpp"
#include "handles.hpp"
#include "template_2000.hpp"
#include <algorithm>
#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Group numbers and group I/O: a group's number is read as atof reads it, a binary double keeps the text "%.17g" gives,
// the width of a group code line is kept, a binary file cut short is not read, a record's handle is found in its head,
// and a file's line endings and what follows its EOF are kept.
// ---------------------------------------------------------------------------

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::internal::BinaryCodeWidth;
using hdxf::internal::DxfReader;
using hdxf::internal::GroupCode;
using hdxf::internal::GroupReader;
using hdxf::internal::GroupWriter;
using hdxf::internal::parse_double_compat;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

bool same_bits(double a, double b) {
    if (std::isnan(a) && std::isnan(b)) return true;
    return std::memcmp(&a, &b, sizeof(double)) == 0;
}

std::string printf_width(int width, long long value) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%*lld", width, value);
    return buf;
}

std::string binary_stream(const std::function<void(GroupWriter&)>& fill) {
    std::ostringstream out(std::ios::binary);
    GroupWriter writer(out, DxfFormat::Binary, BinaryCodeWidth::TwoBytes);
    writer.write_sentinel();
    writer.write_string(0, "SECTION");
    fill(writer);
    return out.str();
}

// A reader on `bytes`, past the group 0 / SECTION every stream starts with (the code width is taken from it)
struct Opened {
    explicit Opened(const std::string& bytes) : in(bytes, std::ios::binary), reader(in) {
        reader.detect_format();
        GroupCode gc;
        started = reader.read(gc) && gc.code == 0 && gc.value == "SECTION";
    }
    std::istringstream in;
    GroupReader reader;
    bool started = false;
};

std::string ascii_text(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

std::vector<uint8_t> bytes_of(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

bool load(hdxf::Drawing& d, const std::vector<uint8_t>& bytes) { return d.load_from_memory(bytes.data(), bytes.size()); }

} // namespace

TEST(matches_atof_forms) {
    const char* forms[] = {
        "0", "1", "-1", "+1", "1.5", "-1.5", "+1.5", ".5", "-.5", "5.", "  2.5", "\t7", "\r\n9", "1e5", "1E5", "1e-5",
        "-2.5e+3", "1.5abc", "12 34", "abc", "", " ", "+", "-", ".", "--5", "+-5", "-+5", "1,5", "0x10", "-0x1p3",
        "0X1F", "inf", "-inf", "infinity", "nan", "nan(0x7ff)", "1e999", "-1e999", "1e-999", "12345678901234567890",
        "0.1", "0.30000000000000004", "123456789.123456789", "-0", "-0.0", "4.9e-324", "1.7976931348623157e308",
        "3.141592653589793238462643383279", "000123", "1e", "1e+", "2.5E", "7.0000000000000000000000001"};
    for (const char* form : forms) {
        const std::string text = form;
        assert_true(same_bits(parse_double_compat(text), std::atof(form)), "\"" + text + "\" reads as atof reads it");
    }
    assert_eq(1.5, parse_double_compat("1.5"));
    assert_eq(-2.25, parse_double_compat("  -2.25"));
    assert_eq(3.0, parse_double_compat("+3"));
    assert_eq(1.0, parse_double_compat("1.0xyz"));
    assert_eq(0.0, parse_double_compat("not a number"));
    // always a decimal point, whatever the C locale says (when the machine has a locale with a decimal comma)
    const std::string saved = std::setlocale(LC_NUMERIC, nullptr) ? std::setlocale(LC_NUMERIC, nullptr) : "C";
    for (const char* name : {"de_DE.UTF-8", "de_DE", "sv_SE.UTF-8", "sv_SE", "German_Germany", "Swedish_Sweden", "fr_FR.UTF-8"}) {
        if (!std::setlocale(LC_NUMERIC, name)) continue;
        const double got = parse_double_compat("12.75");
        const double below_range = parse_double_compat("1.5e-999");
        const double hexadecimal = parse_double_compat("0x1.8p+1");
        std::setlocale(LC_NUMERIC, saved.c_str());
        assert_eq(12.75, got, std::string("a decimal point in the locale ") + name);
        assert_eq(0.0, below_range, std::string("a number out of range reads its decimals in the locale ") + name);
        assert_eq(3.0, hexadecimal, std::string("a hexadecimal number reads its decimals in the locale ") + name);
        break;
    }
    std::setlocale(LC_NUMERIC, saved.c_str());
}

TEST(general17_keeps_text) {
    const double values[] = {0.0, -0.0, 1.0, -1.0, 0.1, 0.30000000000000004, 1e20, 1e-5, 123456789.123456789, 5e-324,
                             1.7976931348623157e308, 1e15, 1e16, 1e17, 1e21, 12345678901234567890.0, -2.5e-7,
                             100.0, 3.141592653589793, 0.0001, 0.00001, 2.2250738585072014e-308, 1.0 / 3.0};
    std::string bytes = binary_stream([&](GroupWriter& w) {
        for (double v : values) w.write_double(10, v);
        w.write_typed(20, "nan(0x7FF8000000001234)");
        w.write_typed(30, "nan(0xFFF8000000000001)");
        w.write_double(40, std::numeric_limits<double>::infinity());
        w.write_double(41, -std::numeric_limits<double>::infinity());
    });
    Opened open(bytes);
    assert_true(open.started, "the stream starts with 0 / SECTION");
    GroupCode gc;
    for (double v : values) {
        assert_true(open.reader.read(gc), "a double: " + open.reader.error());
        char expected[64];
        std::snprintf(expected, sizeof(expected), "%.17g", v);
        assert_eq(std::string(expected), gc.value, "the text of a double");
        assert_true(same_bits(std::strtod(gc.value.c_str(), nullptr), v), "and it reads back to the same bits: " + gc.value);
    }
    assert_true(open.reader.read(gc), "a NaN");
    assert_eq(std::string("nan(0x7FF8000000001234)"), gc.value, "a NaN keeps its bits");
    assert_true(open.reader.read(gc), "a second NaN");
    assert_eq(std::string("nan(0xFFF8000000000001)"), gc.value, "a negative NaN keeps its bits");
    assert_true(open.reader.read(gc), "an infinity");
    assert_eq(std::string("inf"), gc.value, "infinity");
    assert_true(open.reader.read(gc), "a negative infinity");
    assert_eq(std::string("-inf"), gc.value, "negative infinity");
    assert_false(open.reader.read(gc), "nothing more");
}

TEST(group_code_alignment_kept) {
    // Read: the width of a code line that is not three columns, and written back as it was
    struct Line {
        const char* text;
        int code;
        int columns;            // GroupCode::code_columns
        const char* written;    // the code line written back from that width
    };
    const Line lines[] = {
        {"  0", 0, 0, "  0"},       {"0", 0, 1, "0"},           {" 0", 0, 2, " 0"},         {" 70", 70, 0, " 70"},
        {"70", 70, 2, "70"},        {"  70", 70, 4, "  70"},    {"    70", 70, 6, "    70"}, {"     70", 70, 7, "     70"},
        {"      70", 70, 0, " 70"}, {"070", 70, 0, " 70"},      {"1071", 1071, 4, "1071"},   {"999", 999, 0, "999"},
        {" 999", 999, 4, " 999"},   {"-5", -5, 2, "-5"},        {" -5", -5, 0, " -5"},
    };
    for (const Line& line : lines) {
        std::istringstream in(std::string(line.text) + "\nvalue\n", std::ios::binary);
        GroupReader reader(in);
        reader.detect_format();
        GroupCode gc;
        assert_true(reader.read(gc), std::string("\"") + line.text + "\" is a group");
        assert_eq(line.code, gc.code, std::string("code of \"") + line.text + "\"");
        assert_eq(line.columns, gc.code_columns, std::string("columns of \"") + line.text + "\"");
        assert_eq(std::string("value"), gc.value, "its value");
        std::ostringstream out(std::ios::binary);
        GroupWriter writer(out, DxfFormat::Ascii, BinaryCodeWidth::OneByte);
        writer.write_raw(gc.code, gc.value, false, gc.code_columns);
        assert_eq(std::string(line.written) + "\nvalue\n", out.str(), std::string("written back from \"") + line.text + "\"");
    }
    // Written: the code right-aligned in its columns (three unless the record had others, one in plain mode), wider when
    // the code needs more
    for (int code : {0, 5, 70, 999, 1071, -5, 12345}) {
        for (int columns : {0, 1, 2, 3, 4, 7}) {
            std::ostringstream out(std::ios::binary);
            GroupWriter writer(out, DxfFormat::Ascii, BinaryCodeWidth::OneByte);
            writer.write_raw(code, "v", false, columns);
            assert_eq(printf_width(columns > 0 ? columns : 3, code) + "\nv\n", out.str(),
                      "code " + std::to_string(code) + " in " + std::to_string(columns) + " columns");
        }
        std::ostringstream plain(std::ios::binary);
        GroupWriter plain_writer(plain, DxfFormat::Ascii, BinaryCodeWidth::OneByte);
        plain_writer.set_plain_ascii(true);
        plain_writer.write_string(code, "v");
        plain_writer.write_raw(code, "w", false, 0);
        assert_eq(std::to_string(code) + "\nv\n" + std::to_string(code) + "\nw\n", plain.str(), "plain mode, code " + std::to_string(code));
        std::ostringstream normal(std::ios::binary);
        GroupWriter normal_writer(normal, DxfFormat::Ascii, BinaryCodeWidth::OneByte);
        normal_writer.write_string(code, "v");
        assert_eq(printf_width(3, code) + "\nv\n", normal.str(), "three columns, code " + std::to_string(code));
    }
    // Written: integers right-aligned in 6, 9 and 18 columns, as AutoCAD writes them; not in plain mode
    for (bool plain : {false, true}) {
        std::ostringstream out(std::ios::binary);
        GroupWriter writer(out, DxfFormat::Ascii, BinaryCodeWidth::OneByte);
        writer.set_plain_ascii(plain);
        writer.write_int16(70, 5);
        writer.write_int16(70, -25536);
        writer.write_int16(70, -32768);
        writer.write_int32(90, 7);
        writer.write_int32(90, -123456789);
        writer.write_int(160, 5);                // 64 bits
        writer.write_int(290, 1);                // a flag
        if (plain) {
            assert_eq(std::string("70\n5\n70\n-25536\n70\n-32768\n90\n7\n90\n-123456789\n160\n5\n290\n1\n"), out.str(), "plain integers");
        } else {
            assert_eq(" 70\n" + printf_width(6, 5) + "\n 70\n" + printf_width(6, -25536) + "\n 70\n" + printf_width(6, -32768) + "\n" +
                          " 90\n" + printf_width(9, 7) + "\n 90\n" + printf_width(9, -123456789) + "\n160\n" + printf_width(18, 5) +
                          "\n290\n" + printf_width(6, 1) + "\n",
                      out.str(), "aligned integers");
        }
    }
    // Binary files have no code lines: the same calls give the same groups there
    {
        std::string bytes = binary_stream([](GroupWriter& w) { w.write_raw(70, "5", false, 7); });
        Opened open(bytes);
        GroupCode gc;
        assert_true(open.started && open.reader.read(gc), "a binary group");
        assert_eq(70, gc.code);
        assert_eq(5, std::atoi(gc.value.c_str()));
    }
}

TEST(truncated_binary_rejected) {
    struct Kind {
        const char* name;
        std::function<void(GroupWriter&)> write;
        const char* value;      // what the whole group reads as
        int code;
    };
    const Kind kinds[] = {
        {"a double", [](GroupWriter& w) { w.write_double(10, 1.5); }, "1.5", 10},
        {"a 16-bit integer", [](GroupWriter& w) { w.write_int16(70, 1234); }, "1234", 70},
        {"a 32-bit integer", [](GroupWriter& w) { w.write_int32(90, 123456); }, "123456", 90},
        {"a 64-bit integer", [](GroupWriter& w) { w.write_typed(160, "1234567890123"); }, "1234567890123", 160},
        {"a flag", [](GroupWriter& w) { w.write_int(290, 1); }, "1", 290},
        {"a chunk", [](GroupWriter& w) { w.write_typed(310, "AABBCCDD"); }, "AABBCCDD", 310},
    };
    for (const Kind& kind : kinds) {
        const std::string base = binary_stream([](GroupWriter&) {});
        const std::string full = binary_stream(kind.write);
        const size_t group_size = full.size() - base.size();
        assert_true(group_size > 2, std::string(kind.name) + ": a group of its own");
        {
            Opened open(full);
            GroupCode gc;
            assert_true(open.started && open.reader.read(gc), std::string(kind.name) + ": the whole group reads");
            assert_eq(kind.code, gc.code);
            assert_eq(std::string(kind.value), gc.value, std::string(kind.name) + ": its value");
            assert_false(open.reader.read(gc), std::string(kind.name) + ": and nothing follows it");
        }
        for (size_t cut = 1; cut <= group_size; ++cut) {
            const std::string where = std::string(kind.name) + " cut by " + std::to_string(cut) + " of " + std::to_string(group_size);
            Opened open(full.substr(0, full.size() - cut));
            assert_true(open.started, where + ": the group before it reads");
            GroupCode gc;
            assert_false(open.reader.read(gc), where + ": the cut group is not read");
            assert_false(open.reader.read(gc), where + ": and nothing is read after it");
        }
    }
    // A string without its terminator is the file's last: it is read as it stands, and nothing follows
    {
        const std::string full = binary_stream([](GroupWriter& w) { w.write_string(0, "EOF"); });
        Opened open(full.substr(0, full.size() - 1));
        GroupCode gc;
        assert_true(open.started && open.reader.read(gc), "a last string without its terminator is read");
        assert_eq(std::string("EOF"), gc.value);
        assert_false(open.reader.read(gc), "and nothing follows it");
    }
}

namespace {

// The handles of the records a drawing read keeps, as the groups of each record give them
std::vector<uint64_t> handles_by_groups(const hdxf::Drawing& dwg) {
    std::vector<uint64_t> out;
    auto add = [&](const hdxf::RawRecord& r) {
        if (r.empty()) return;
        for (const auto& g : r.groups()) {
            if (g.code == 5 || g.code == 105) {
                uint64_t v = 0;
                if (hdxf::internal::parse_handle(g.value, v) == hdxf::internal::HandleParse::Ok) out.push_back(v);
                return;
            }
            if (g.code == 100) return;
        }
    };
    auto entity = [&](const hdxf::Entity& e) {
        add(e.raw);
        if (e.type == hdxf::EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) add(v.raw);
            add(e.as_polyline().seqend_raw);
        } else if (e.type == hdxf::EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) add(a.raw);
            add(e.as_insert().seqend_raw);
        }
    };
    auto entries = [&](const auto& list) { for (const auto& x : list) add(x.raw); };
    for (const auto& t : dwg.raw_tables()) {
        add(t.header);
        for (const auto& r : t.entries) add(r);
    }
    entries(dwg.vports());
    entries(dwg.linetypes());
    entries(dwg.layers());
    entries(dwg.text_styles());
    entries(dwg.views());
    entries(dwg.ucs_list());
    entries(dwg.app_ids());
    entries(dwg.dim_styles());
    for (const auto& b : dwg.blocks()) {
        add(b.raw);
        add(b.endblk_raw);
        add(b.record.raw);
        for (const auto& e : b.entities) entity(e);
    }
    for (const auto& e : dwg.entities()) entity(e);
    for (const auto& o : dwg.objects()) add(o.raw);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST(handles_scan_heads_only) {
    // The head of a record: its handle before the first subclass, as written; one after a subclass marker is not the head
    const std::string text =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLINE\n  5\n2A\n102\n{ACAD_REACTORS\n330\n1F\n102\n}\n100\nAcDbEntity\n  8\n0\n100\nAcDbLine\n 10\n1.0\n 20\n2.0\n 30\n0.0\n"
        " 11\n3.0\n 21\n4.0\n 31\n0.0\n"
        "  0\nLINE\n100\nAcDbEntity\n  5\n2B\n  8\n0\n100\nAcDbLine\n 10\n1.0\n 20\n2.0\n 30\n0.0\n 11\n3.0\n 21\n4.0\n 31\n0.0\n"
        "  0\nLINE\n  8\n0\n 10\n1.0\n 20\n2.0\n 30\n0.0\n 11\n3.0\n 21\n4.0\n 31\n0.0\n"
        "  0\nLINE\n330\n5\n  5\n 2C \n  8\n0\n 10\n1.0\n 20\n2.0\n 30\n0.0\n 11\n3.0\n 21\n4.0\n 31\n0.0\n"
        "  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing dwg;
    assert_true(load(dwg, bytes_of(text)), dwg.last_error());
    assert_eq(4, static_cast<int>(dwg.entities().size()), "four lines");
    assert_eq(std::string("2A"), DxfReader::head_handle(dwg.entities()[0].raw), "the handle of the head");
    assert_eq(std::string(""), DxfReader::head_handle(dwg.entities()[1].raw), "a 5 after a subclass marker is not the head's");
    assert_eq(std::string(""), DxfReader::head_handle(dwg.entities()[2].raw), "no handle");
    assert_eq(std::string(" 2C "), DxfReader::head_handle(dwg.entities()[3].raw), "the value as written");
    assert_eq(std::string(""), DxfReader::head_handle(hdxf::RawRecord()), "an empty record");
    // The handles of a whole drawing come from the heads alone: no record's groups are decoded for them
    const size_t decoded = hdxf::internal::raw_groups_decoded();
    assert_false(hdxf::internal::preserved_handles(dwg).empty(), "the drawing has handles");
    assert_eq(0, static_cast<int>(hdxf::internal::raw_groups_decoded() - decoded), "records decoded for the handles");
    uint64_t value = 0;
    assert_true(hdxf::internal::parse_handle(DxfReader::head_handle(dwg.entities()[3].raw), value) == hdxf::internal::HandleParse::Ok &&
                    value == 0x2C,
                "and parses");

    // A drawing with the records of every kind: the handles found are the ones the groups give
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
            const std::string where = std::string(hdxf_test::version_label(version)) + " " + hdxf_test::format_label(format);
            hdxf::Drawing d(version);
            d.add_layer("Walls");
            for (int i = 0; i < 3; ++i) {
                hdxf::Entity e;
                e.type = hdxf::EntityType::Line;
                e.props.layer = "Walls";
                hdxf::LineData line;
                line.end = {1.0 + i, 2.0, 0.0};
                e.data = line;
                d.add_entity(e);
            }
            const std::vector<uint8_t> bytes = d.save_to_memory(format);
            assert_true(!bytes.empty(), where + ": " + d.last_error());
            hdxf::Drawing r;
            assert_true(load(r, bytes), where + ": " + r.last_error());
            const std::vector<uint64_t> expected = handles_by_groups(r);
            assert_true(version == DxfVersion::R12 || !expected.empty(), where + ": the records have handles");
            assert_true(hdxf::internal::preserved_handles(r) == expected, where + ": the handles of the preserved records");
            if (version >= DxfVersion::R2000) {
                // a table record's handle is its 105
                bool found = false;
                for (const hdxf::Layer& layer : r.layers()) {
                    if (layer.name != "Walls") continue;
                    found = true;
                    std::string by_groups;
                    for (const auto& g : layer.raw.groups()) {
                        if (g.code == 105 || g.code == 5) { by_groups = g.value; break; }
                        if (g.code == 100) break;
                    }
                    assert_true(!by_groups.empty(), where + ": the layer record has a handle");
                    assert_eq(by_groups, DxfReader::head_handle(layer.raw), where + ": a table record's handle");
                }
                assert_true(found, where + ": the layer is there");
            }
        }
    }
}

TEST(line_endings_and_eof_kept) {
    for (DxfVersion version : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2018}) {
        const std::string where = hdxf_test::version_label(version);
        hdxf::Drawing d(version);
        hdxf::Entity e;
        e.type = hdxf::EntityType::Line;
        e.props.layer = "0";
        hdxf::LineData line;
        line.end = {1.0, 2.0, 0.0};
        e.data = line;
        d.add_entity(e);
        const std::vector<uint8_t> built = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!built.empty(), where + ": " + d.last_error());
        // a drawing made in memory has AutoCAD's CRLF
        const std::string crlf = ascii_text(built);
        for (size_t i = 0; i < crlf.size(); ++i) {
            if (crlf[i] == '\n') assert_true(i > 0 && crlf[i - 1] == '\r', where + ": every line ends in CRLF");
        }
        std::string lf = crlf;
        lf.erase(std::remove(lf.begin(), lf.end(), '\r'), lf.end());
        assert_true(lf.size() > 3 && lf.compare(lf.size() - 4, 4, "EOF\n") == 0, where + ": the file ends with EOF");
        const std::string no_final_line_end = lf.substr(0, lf.size() - 1);
        const std::string blank_after_eof = lf + "\n";
        const std::pair<const char*, const std::string*> variants[] = {
            {"CRLF", &crlf}, {"LF", &lf}, {"LF, no line end after EOF", &no_final_line_end}, {"LF, a blank line after EOF", &blank_after_eof}};
        for (const auto& variant : variants) {
            const std::string name = where + " " + variant.first;
            hdxf::Drawing r;
            assert_true(load(r, bytes_of(*variant.second)), name + ": " + r.last_error());
            const std::vector<uint8_t> back = r.save_to_memory(DxfFormat::Ascii);
            assert_true(back == bytes_of(*variant.second), name + ": written back as it was read");
        }
        // binary is never converted
        const std::vector<uint8_t> binary = d.save_to_memory(DxfFormat::Binary);
        assert_true(!binary.empty(), where + ": " + d.last_error());
        hdxf::Drawing r;
        assert_true(load(r, binary), where + ": " + r.last_error());
        assert_true(r.save_to_memory(DxfFormat::Binary) == binary, where + " binary: written back as it was read");
    }
}
