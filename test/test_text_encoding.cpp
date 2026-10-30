#include "test_framework.hpp"
#include "hdxf.hpp"
#include "dxf_reader.hpp"
#include "text_encoding.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// Text encoding (phase 0, step 0.7 — plan B1): every string in the API is UTF-8
// ---------------------------------------------------------------------------

using hdxf::internal::decode_carets;
using hdxf::internal::decode_text;
using hdxf::internal::encode_carets;
using hdxf::internal::encode_text;
using hdxf::internal::utf8_code_page;

namespace {

std::string decoded(const std::string& in, int code_page, const std::string& name = "ANSI_X") {
    std::string out;
    std::string error;
    hdxf_test::assert_true(decode_text(in, code_page, name, out, error), error);
    return out;
}

std::string encoded(const std::string& utf8, int code_page) {
    std::string out;
    hdxf_test::assert_true(encode_text(utf8, code_page, out), "invalid UTF-8");
    return out;
}

bool load_text(hdxf::Drawing& dwg, const std::string& dxf) {
    return dwg.load_from_memory(reinterpret_cast<const uint8_t*>(dxf.data()), dxf.size());
}

bool read_internal(hdxf::Drawing& dwg, const std::string& dxf, std::string& error) {
    std::istringstream in(dxf, std::ios::binary);
    hdxf::internal::DxfReader reader(in);
    bool ok = reader.read(dwg);
    error = reader.error();
    return ok;
}

bool read_internal(hdxf::Drawing& dwg, const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return read_internal(dwg, ss.str(), error);
}

std::string first_text(const hdxf::Drawing& dwg, size_t index = 0) {
    size_t n = 0;
    for (const auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::Text && n++ == index) return e.as_text().content;
    }
    return {};
}

bool has_layer(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& l : dwg.layers()) {
        if (l.name == name) return true;
    }
    return false;
}

std::string as_text(const std::vector<uint8_t>& bytes) {
    return hdxf_test::lf_text(bytes);
}

const std::string aao = "\xC3\xA5\xC3\xA4\xC3\xB6";                 // åäö
const std::string aao_upper = "\xC3\x85\xC3\x84\xC3\x96";           // ÅÄÖ
const std::string euro = "\xE2\x82\xAC";                            // €
const std::string empty_set = "\xE2\x88\x85";                       // ∅

std::filesystem::path synthetic(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / hdxf::path_from_utf8(name);
}

} // namespace

TEST(encoding_decode_code_pages) {
    hdxf_test::assert_eq(decoded("\xE5\xE4\xF6 \x80", 1252), aao + " " + euro, "ANSI_1252");
    hdxf_test::assert_eq(decoded("\x93x\x94", 1252), "\xE2\x80\x9Cx\xE2\x80\x9D", "quotes");
    hdxf_test::assert_eq(decoded("\xB3", 1250), "\xC5\x82", "ANSI_1250 l with stroke");
    hdxf_test::assert_eq(decoded("\x81", 1252), "\xC2\x81", "undefined byte keeps its number");

    // An unsupported code page is fine as long as the text is ASCII
    hdxf_test::assert_eq(decoded("plain", 0, "dos932"), "plain");
    std::string out, error;
    hdxf_test::assert_false(decode_text("\xE5", 0, "dos932", out, error), "byte >= 0x80");
    hdxf_test::assert_eq(error, std::string("Unsupported code page: dos932"));
}

TEST(encoding_decode_utf8_and_escapes) {
    hdxf_test::assert_eq(decoded(aao + " " + euro, utf8_code_page), aao + " " + euro);
    hdxf_test::assert_eq(decoded("\\U+00E5\\U+00E4\\U+00F6", 1252), aao, "\\U+ in a code page");
    hdxf_test::assert_eq(decoded("Tsch\xC3\xBCss mit \\U+00FC", utf8_code_page),
                         "Tsch\xC3\xBCss mit \xC3\xBC", "UTF-8 mixed with \\U+");
    hdxf_test::assert_eq(decoded("\\U+D83D\\U+DE00", 1252), "\xF0\x9F\x98\x80", "surrogate pair");
    hdxf_test::assert_eq(decoded("\\U+D83D!", 1252), "\\U+D83D!", "lone surrogate kept");
    hdxf_test::assert_eq(decoded("\\U+12", 1252), "\\U+12", "too short kept");

    std::string out, error;
    hdxf_test::assert_false(decode_text("ok \xE5 bad", utf8_code_page, "", out, error), "invalid");
    hdxf_test::assert_true(error.find("Invalid UTF-8 at byte 4") != std::string::npos, error);
    hdxf_test::assert_false(decode_text("\xC0\xAF", utf8_code_page, "", out, error), "overlong");
    hdxf_test::assert_false(decode_text("\xED\xA0\x80", utf8_code_page, "", out, error),
                            "surrogate in UTF-8");
}

TEST(encoding_carets) {
    hdxf_test::assert_eq(decode_carets("a^Jb^Ic^ d"), std::string("a\nb\tc^d"));
    hdxf_test::assert_eq(decode_carets("^x ^"), std::string("^x ^"), "not caret notation");
    hdxf_test::assert_eq(encode_carets("a\nb\tc^d"), std::string("a^Jb^Ic^ d"));
}

TEST(encoding_encode_for_writing) {
    hdxf_test::assert_eq(encoded(aao + " " + euro, 1252), "\xE5\xE4\xF6 \x80");
    hdxf_test::assert_eq(encoded("\xE2\x80\x9Cx\xE2\x80\x9D", 1252), "\x93x\x94", "quotes");
    hdxf_test::assert_eq(encoded("\xC5\x82", 1252), "\\U+0142", "not in ANSI_1252");
    hdxf_test::assert_eq(encoded("\xC5\x82", 1250), "\xB3", "in ANSI_1250");
    hdxf_test::assert_eq(encoded(aao, 0), "\\U+00E5\\U+00E4\\U+00F6", "unsupported code page");
    // Above U+FFFF: its surrogates as two escapes, as AutoCAD reads it (B55) — in UTF-8 too
    hdxf_test::assert_eq(encoded("a\xF0\x9F\x98\x80" "b", 1252), "a\\U+D83D\\U+DE00b", "above U+FFFF");
    hdxf_test::assert_eq(encoded("a\xF0\x9F\x98\x80" "b", hdxf::internal::utf8_code_page), "a\\U+D83D\\U+DE00b",
                         "above U+FFFF in UTF-8");
    hdxf_test::assert_eq(encoded("\xF4\x8F\xBF\xBF", 0), "\\U+DBFF\\U+DFFF", "U+10FFFF");

    std::string out;
    hdxf_test::assert_false(encode_text("\xE5", 1252, out), "ANSI byte is not UTF-8");
}

TEST(encoding_read_r12) {
    // ANSI_1252 bytes and \U+ in one R12 file
    std::string dxf =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1009\n"
        "  9\n$DWGCODEPAGE\n  3\nANSI_1252\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nTEXT\n  8\n0\n 10\n0\n 20\n0\n 40\n1\n  1\n\xE5\xE4\xF6 \\U+20AC \\U+0142\n"
        "  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, dxf), dwg.last_error());
    hdxf_test::assert_eq(first_text(dwg), aao + " " + euro + " \xC5\x82");
}

TEST(encoding_read_2004_and_2007_files) {
    // The same strings, as \U+ and ANSI_1252 bytes in 2004 and as UTF-8 in 2007
    hdxf::Drawing r2004;
    std::string error;
    hdxf_test::assert_true(read_internal(r2004, synthetic("r2004_unicode_escapes.dxf"), error),
                           error);
    hdxf_test::assert_true(has_layer(r2004, "Lager_" + aao), "2004 layer from \\U+");
    hdxf_test::assert_eq(first_text(r2004, 0), aao + " " + aao_upper + " " + euro, "2004 \\U+");
    hdxf_test::assert_eq(first_text(r2004, 1), aao + " " + aao_upper + " " + euro, "2004 bytes");
    hdxf_test::assert_eq(first_text(r2004, 2), empty_set + "45,6", "2004 \\U+2205");

    hdxf::Drawing r2007;
    hdxf_test::assert_true(read_internal(r2007, synthetic("r2007_utf8.dxf"), error), error);
    hdxf_test::assert_true(has_layer(r2007, "Lager_" + aao), "2007 layer");
    hdxf_test::assert_eq(first_text(r2007, 0), aao + " " + aao_upper + " " + euro, "2007");
    hdxf_test::assert_eq(first_text(r2007, 1), empty_set + "45,6", "2007 UTF-8");

    // The preserved record keeps the file's bytes
    const hdxf::Entity& text = r2004.entities()[0];
    bool escapes_kept = false;
    for (const auto& g : text.raw.groups()) {
        if (g.code == 1 && g.value.find("\\U+00E5") != std::string::npos) escapes_kept = true;
    }
    hdxf_test::assert_true(escapes_kept, "raw record as written");
}

TEST(encoding_read_invalid_utf8_in_2007) {
    std::string dxf =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1021\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nTEXT\n  8\n0\n 10\n0\n 20\n0\n 40\n1\n  1\nANSI byte \xE5 in UTF-8\n"
        "  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing dwg;
    std::string error;
    hdxf_test::assert_false(read_internal(dwg, dxf, error), "loaded");
    hdxf_test::assert_true(error.find("Invalid UTF-8") != std::string::npos &&
                           error.find("group code 1") != std::string::npos &&
                           error.find("group 13 of the file") != std::string::npos, error);
}

TEST(encoding_write_and_read_back) {
    hdxf::Drawing dwg;
    std::string text = aao + " " + euro + " \xE2\x80\x9Cq\xE2\x80\x9D \xC5\x82 \xCE\xA9";
    dwg.add_text({0, 0, 0}, text);
    dwg.add_layer("Lager_" + aao);

    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    std::string file = as_text(out);
    hdxf_test::assert_true(file.find("\xE5\xE4\xF6 \x80 \x93q\x94 \\U+0142 \\U+03A9") !=
                           std::string::npos, "ANSI_1252 bytes and \\U+ in the file");
    hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), "no warnings");

    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(first_text(back), text, "read back");
    hdxf_test::assert_true(has_layer(back, "Lager_" + aao), "layer read back");
}

// A character above U+FFFF as its surrogates in two escapes (B55), read back as the character
TEST(encoding_write_above_bmp) {
    hdxf::Drawing dwg;
    dwg.add_text({0, 0, 0}, "smile \xF0\x9F\x98\x80");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf_test::assert_true(as_text(out).find("smile \\U+D83D\\U+DE00") != std::string::npos, "the surrogates");
    hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), "no warning");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(first_text(back), "smile \xF0\x9F\x98\x80", "read back");
}

TEST(encoding_write_invalid_utf8_fails) {
    hdxf::Drawing dwg;
    dwg.add_text({0, 0, 0}, "ANSI byte \xE5 is not UTF-8");
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved");
    hdxf_test::assert_true(dwg.last_error().find("Invalid UTF-8") != std::string::npos,
                           dwg.last_error());

    // Nothing on disk is touched
    std::filesystem::path path = std::filesystem::temp_directory_path() / "hdxf_invalid_utf8.dxf";
    std::filesystem::remove(path);
    hdxf_test::assert_false(dwg.save_as(path), "saved to file");
    hdxf_test::assert_false(std::filesystem::exists(path), "file created");
}

TEST(encoding_control_characters_round_trip) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg;
        std::string text = "line 1\nline 2\ttab ^ caret ^J literal";
        dwg.add_text({0, 0, 0}, text);
        std::vector<uint8_t> out = dwg.save_to_memory(format);
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        if (format == hdxf::DxfFormat::Ascii) {
            hdxf_test::assert_true(as_text(out).find("line 1^Jline 2^Itab ^  caret ^ J literal\n") !=
                                   std::string::npos, "caret notation in ASCII");
        }
        hdxf::Drawing back;
        hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        hdxf_test::assert_eq(first_text(back), text, "read back");
    }
}

// $DWGCODEPAGE changed after loading: the preserved strings were read in the old code page and
// are written in the new one (Cyrillic Zhe under ANSI_1251 is the byte 0xC6, AE under ANSI_1252)
TEST(encoding_changed_code_page_r12) {
    const std::string zhe = "\xD0\x96";
    std::string dxf =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1009\n"
        "  9\n$DWGCODEPAGE\n  3\nANSI_1251\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nTEXT\n  8\n0\n 10\n0\n 20\n0\n 40\n1\n  1\n\xC6\n"
        "  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing dwg;
    hdxf_test::assert_true(load_text(dwg, dxf), dwg.last_error());
    hdxf_test::assert_eq(first_text(dwg), zhe, "read as ANSI_1251");

    dwg.set_header_string("$DWGCODEPAGE", 3, "ANSI_1252");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    std::string file = as_text(out);
    hdxf_test::assert_true(file.find("\xC6\n") == std::string::npos, "not the old byte under the new page");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(first_text(back), zhe, "read back under ANSI_1252");

    // Binary in, binary out
    hdxf::Drawing binary;
    hdxf_test::assert_true(load_text(binary, dxf), binary.last_error());
    binary.set_header_string("$DWGCODEPAGE", 3, "ANSI_1252");
    std::vector<uint8_t> bin = binary.save_to_memory(hdxf::DxfFormat::Binary);
    hdxf_test::assert_false(bin.empty(), binary.last_error());
    hdxf::Drawing binary_back;
    hdxf_test::assert_true(binary_back.load_from_memory(bin.data(), bin.size()), binary_back.last_error());
    hdxf_test::assert_eq(first_text(binary_back), zhe, "binary read back under ANSI_1252");
}

// The same for a 2004 file: its text in ANSI_1252 bytes written under ANSI_1251
TEST(encoding_changed_code_page_2004) {
    hdxf::Drawing dwg;
    std::string error;
    hdxf_test::assert_true(read_internal(dwg, synthetic("r2004_unicode_escapes.dxf"), error), error);
    std::string expected = first_text(dwg, 1);
    hdxf_test::assert_eq(expected, aao + " " + aao_upper + " " + euro, "2004 bytes");

    std::ifstream in(synthetic("r2004_unicode_escapes.dxf"), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    hdxf::Drawing loaded;
    hdxf_test::assert_true(load_text(loaded, ss.str()), loaded.last_error());
    loaded.set_header_string("$DWGCODEPAGE", 3, "ANSI_1251");
    std::vector<uint8_t> out = loaded.save_to_memory();
    hdxf_test::assert_false(out.empty(), loaded.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(first_text(back, 1), expected, "2004 text under ANSI_1251");
}
