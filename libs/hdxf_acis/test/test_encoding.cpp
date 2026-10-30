#include "test_support.hpp"

#include <algorithm>
#include <cctype>
#include <clocale>
#include <cstdlib>
#include <utility>

using namespace acis_test;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

// ---------------------------------------------------------------------------
// The text: the cipher, the lines, the numbers, and what HDxf does with a line on its way into the DXF
// ---------------------------------------------------------------------------
//
// Found in the facit (a 2010 body's group 1 lines against its SAT, doc/reference/hdxf_acis_sat_21500.md):
// the SAT cipher is 159 - c for each character from "!" to "~" and nothing else; a capital "A" becomes
// "^". The "^ " that stands for it in an ASCII DXF is not part of the cipher but DXF's own caret
// notation, which HDxf's group writer applies and its reader undoes: AcisData::lines hold "^".

namespace {

acis::SatDocument document_of(std::vector<std::string> lines) {
    acis::SatDocument document;
    document.lines = std::move(lines);
    return document;
}

// A document that is not a real SAT: the header lines, and a last line to carry over
acis::SatDocument with_last_line(const std::string& last) {
    return document_of({"21500 1 2 24", "16 Autodesk AutoCAD 20 ASM 230.1.0.65535 NT 0  ", "1 1e-06 1e-10 ", last});
}

// The (code, value) of the groups 1 and 3 of the first body in an ASCII DXF
std::vector<std::pair<int, std::string>> acis_groups(const std::string& dxf) {
    std::vector<std::string> lines;
    size_t at = 0;
    while (at <= dxf.size()) {
        size_t end = dxf.find('\n', at);
        if (end == std::string::npos) end = dxf.size();
        lines.push_back(dxf.substr(at, end - at));
        at = end + 1;
    }
    std::vector<std::pair<int, std::string>> groups;
    size_t i = 0;
    while (i < lines.size() && lines[i] != "AcDbModelerGeometry") ++i;
    for (i += 1; i + 1 < lines.size(); i += 2) {
        int code = std::atoi(lines[i].c_str());
        if (code == 0) break;
        if (code == 1 || code == 3) groups.push_back({code, lines[i + 1]});
    }
    return groups;
}

std::string caret_escaped(const std::string& line) {
    std::string out;
    for (char c : line) {
        if (c == '^') out += "^ ";
        else out += c;
    }
    return out;
}

std::string flat_text(const acis::SatDocument& document) {
    std::string out;
    for (const std::string& line : document.lines) out += line + " ";
    return out;
}

} // namespace

TEST(acis_encoding_cipher_is_159_minus_c) {
    acis::SatDocument document = with_last_line("A@_^ !~ 09");
    hdxf::AcisData data = acis_data(document);
    // A → ^, @ → _, _ → @, ^ → A, the space stays, ! → ~, ~ → !, 0 → o, 9 → f
    assert_eq(data.lines[3], "^_@A ~! of");
    // Every printable character but the space: the cipher is its own inverse and reaches each once
    std::string all;
    for (int c = 33; c <= 126; ++c) all += static_cast<char>(c);
    hdxf::AcisData whole = acis_data(with_last_line(all));
    std::string seen(94, '.');
    for (int i = 0; i < 94; ++i) {
        unsigned char c = static_cast<unsigned char>(whole.lines[3][static_cast<size_t>(i)]);
        assert_eq(static_cast<int>(c), 159 - (33 + i));
        seen[static_cast<size_t>(c - 33)] = 'x';
    }
    assert_eq(seen, std::string(94, 'x'));
    // HDxf's reader of AutoCAD's lines gives the text back
    assert_eq(hdxf::internal::acis_deciphered(whole),
              std::string("21500 1 2 24\n16 Autodesk AutoCAD 20 ASM 230.1.0.65535 NT 0  \n1 1e-06 1e-10 \n") + all + "\n");
}

TEST(acis_encoding_rejects_characters_the_sat_cipher_does_not_hold) {
    for (const std::string& bad : {std::string("a\tb"), std::string("caf\xc3\xa9"), std::string("del\x7f"), std::string("ctl\x1f")}) {
        acis::Result<hdxf::AcisData> result = acis::to_acis_data(with_last_line(bad));
        assert_false(result.ok(), "a line with a character outside printable ASCII");
        assert_true(has_code(result.errors(), acis::ErrorCode::Encoding), errors_text(result.errors()));
    }
}

TEST(acis_encoding_adapter_sets_the_acis_data_fields) {
    acis::SphereParameters p;
    acis::Result<acis::SatDocument> sat = acis::make_sphere(p);
    hdxf::AcisData data = acis_data(require(sat, "sphere"));
    assert_eq(data.version, 1, "DXF group 70, not the SAT version");
    assert_true(data.history_handle.empty() && data.guid.empty() && data.sab.empty());
    assert_eq(static_cast<int>(data.lines.size()), static_cast<int>(sat.value().lines.size()));
}

TEST(acis_encoding_adapter_writes_only_sat_21500_with_records) {
    acis::SatDocument other = with_last_line("body $-1 -1 $-1 $-1 $-1 $-1 #");
    other.version = 21800;
    acis::Result<hdxf::AcisData> result = acis::to_acis_data(other);
    assert_false(result.ok());
    assert_true(has_code(result.errors(), acis::ErrorCode::InvalidArgument));
    assert_false(acis::to_acis_data(document_of({"21500 0 2 24"})).ok(), "a document with no records");
}

TEST(acis_encoding_capital_a_is_a_caret_and_the_dxf_text_escapes_it) {
    acis::SphereParameters p;
    acis::Result<acis::SatDocument> sat = acis::make_sphere(p);
    hdxf::AcisData data = acis_data(require(sat, "sphere"));
    // The product line has capital A: its cipher is a caret
    assert_true(data.lines[1].find('^') != std::string::npos, data.lines[1]);
    hdxf::Drawing drawing(hdxf::DxfVersion::R2010);
    drawing.add_acis(hdxf::EntityType::Solid3D, data);
    std::vector<uint8_t> bytes = drawing.save_to_memory(hdxf::DxfFormat::Ascii);
    std::string text = hdxf_test::lf_text(bytes);
    std::vector<std::pair<int, std::string>> groups = acis_groups(text);
    assert_eq(static_cast<int>(groups.size()), static_cast<int>(data.lines.size()));
    // In the file the caret is "^ " (DXF's notation), the rest of the line as it is
    assert_eq(groups[1].second, caret_escaped(data.lines[1]));
    assert_true(groups[1].second.find("^ ") != std::string::npos);
    // ... and HDxf reads the file back to the lines as they were, with no extra space
    hdxf::Drawing again = reloaded(drawing, hdxf::DxfFormat::Ascii);
    assert_true(first_body(again).as_acis().lines == data.lines, "lines after an ASCII round trip");
    hdxf::Drawing binary = reloaded(drawing, hdxf::DxfFormat::Binary);
    assert_true(first_body(binary).as_acis().lines == data.lines, "lines after a binary round trip");
    // The text a body is read as is the SAT that was written
    assert_eq(hdxf::internal::acis_deciphered(first_body(again).as_acis()), sat.value().text());
}

TEST(acis_encoding_dxf_line_lengths_254_255_256_510_511) {
    // HDxf writes a line of 255 characters or more as groups 3 of 255 and the rest in a group 1 (empty
    // when the line is a multiple of 255), as AutoCAD does; a line shorter than that is one group 1
    struct Case {
        size_t length;
        std::vector<size_t> groups_3;
        size_t group_1;
    };
    const Case cases[] = {{253, {}, 253},        {254, {}, 254},         {255, {255}, 0},        {256, {255}, 1},
                          {509, {255}, 254},     {510, {255, 255}, 0},   {511, {255, 255}, 1}};
    for (const Case& c : cases) {
        std::string what = "a line of " + std::to_string(c.length) + " characters";
        acis::SatDocument document = with_last_line(std::string(c.length, 'x'));
        hdxf::AcisData data = acis_data(document);
        hdxf::Drawing drawing(hdxf::DxfVersion::R2010);
        drawing.add_acis(hdxf::EntityType::Solid3D, data);
        std::string text = hdxf_test::lf_text(drawing.save_to_memory(hdxf::DxfFormat::Ascii));
        std::vector<std::pair<int, std::string>> groups = acis_groups(text);
        // Three header lines, then this line's groups
        assert_eq(static_cast<int>(groups.size()), 3 + static_cast<int>(c.groups_3.size()) + 1, what);
        for (size_t k = 0; k < c.groups_3.size(); ++k) {
            assert_eq(groups[3 + k].first, 3, what);
            assert_eq(static_cast<int>(groups[3 + k].second.size()), static_cast<int>(c.groups_3[k]), what);
        }
        assert_eq(groups.back().first, 1, what);
        assert_eq(static_cast<int>(groups.back().second.size()), static_cast<int>(c.group_1), what);
        // Both formats read the line back whole, with no group left over
        hdxf::Drawing ascii = reloaded(drawing, hdxf::DxfFormat::Ascii);
        assert_true(first_body(ascii).as_acis().lines == data.lines, what + " (ASCII)");
        hdxf::Drawing binary = reloaded(drawing, hdxf::DxfFormat::Binary);
        assert_true(first_body(binary).as_acis().lines == data.lines, what + " (binary)");
    }
}

TEST(acis_encoding_library_lines_are_at_most_254_characters) {
    acis::Result<acis::SatDocument> sat = build_nurbs_cylinder(10, 25);
    const acis::SatDocument& document = require(sat, "NURBS cylinder");
    size_t longest = 0;
    int continuation = 0;
    for (size_t i = 3; i < document.lines.size(); ++i) {
        const std::string& line = document.lines[i];
        longest = std::max(longest, line.size());
        if (line[0] == ' ') {
            ++continuation;
        } else {
            // A record starts with its type, which is a letter
            assert_true(std::isalpha(static_cast<unsigned char>(line[0])) != 0, line.substr(0, 20));
        }
    }
    assert_true(longest <= 254, "the longest line is " + std::to_string(longest));
    assert_true(longest > 200, "the NURBS surface's record is spread over lines that are nearly full");
    assert_true(continuation > 0, "and has continuation lines");
    // The reader gets the same records however the lines break
    ParsedSat parsed = parse_document(document);
    assert_eq(static_cast<int>(parsed.file.records.size()), document.record_count);
}

TEST(acis_encoding_numbers_are_exact_and_independent_of_the_locale) {
    acis::SphereParameters p;
    p.radius = 0.1;
    std::string small = flat_text(require(acis::make_sphere(p), "r = 0.1"));
    assert_true(small.find(" 0.1 ") != std::string::npos, small);
    p.radius = 1e-7;
    std::string tiny = flat_text(require(acis::make_sphere(p), "r = 1e-7"));
    assert_true(tiny.find(" 1e-07 ") != std::string::npos, tiny);

    // A radius with every digit: read back as the same double
    p.radius = 12345678.123456789;
    acis::Result<acis::SatDocument> sat = acis::make_sphere(p);
    ParsedSat parsed = parse_document(require(sat, "r = 12345678.123456789"));
    assert_eq(parsed.bodies[0].surfaces[0].radius, 12345678.123456789, "radius round trip");

    // Another locale (a decimal comma, where the system has one) changes nothing
    p.radius = 2.5;
    std::string before = require(acis::make_sphere(p), "C locale").text();
    std::string old = std::setlocale(LC_ALL, nullptr);
    const char* names[] = {"sv_SE.UTF-8", "sv-SE", "Swedish_Sweden.1252", "de_DE.UTF-8", "de-DE", "fr_FR.UTF-8"};
    for (const char* name : names) {
        if (!std::setlocale(LC_ALL, name)) continue;
        std::string after = require(acis::make_sphere(p), name).text();
        std::setlocale(LC_ALL, old.c_str());
        assert_eq(after, before, std::string("under locale ") + name);
        break;
    }
    std::setlocale(LC_ALL, old.c_str());
    assert_true(before.find(" 2.5 ") != std::string::npos, before);
}

TEST(acis_encoding_negative_zero_is_written_as_zero) {
    acis::Result<acis::SatDocument> sat = build_rectangle_region(-0.0, -0.0, 4.0, 3.0);
    std::string text = flat_text(require(sat, "region from -0"));
    assert_true(text.find(" -0 ") == std::string::npos, text);
}

TEST(acis_encoding_sat_has_no_end_marker_like_autocads_dxf) {
    acis::BoxParameters p;
    acis::Result<acis::SatDocument> sat = acis::make_box(p);
    const acis::SatDocument& document = require(sat, "box");
    assert_true(document.text().find("End-of") == std::string::npos);
    const std::string& last = document.lines.back();
    assert_true(last.size() >= 2 && last.compare(last.size() - 2, 2, " #") == 0, last);
}
