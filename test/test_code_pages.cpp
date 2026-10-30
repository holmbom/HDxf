#include "test_framework.hpp"
#include "hdxf.hpp"
#include "text_encoding.hpp"
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Every code page AutoCAD names in $DWGCODEPAGE (rättningsfas R.5): DOS, ISO 8859,
// Macintosh, ANSI_874 and the double-byte ones with AutoCAD's \M+ escape. Expected values
// from the Unicode consortium's mappings (as in Python's codecs).
// ---------------------------------------------------------------------------

using hdxf::internal::code_page_from_name;
using hdxf::internal::decode_carets;
using hdxf::internal::decode_text;
using hdxf::internal::encode_text;

namespace {

std::string decoded(const std::string& bytes, const std::string& page) {
    std::string out, error;
    hdxf_test::assert_true(decode_text(bytes, code_page_from_name(page), page, out, error), page + ": " + error);
    return out;
}

std::string encoded(const std::string& utf8, const std::string& page) {
    std::string out;
    hdxf_test::assert_true(encode_text(utf8, code_page_from_name(page), out), page);
    return out;
}

} // namespace

TEST(code_pages_names) {
    hdxf_test::assert_eq(code_page_from_name("ANSI_1252"), 1252);
    hdxf_test::assert_eq(code_page_from_name("ansi_1252"), 1252);
    hdxf_test::assert_eq(code_page_from_name("DOS850"), 850);
    hdxf_test::assert_eq(code_page_from_name("CP850"), 850);
    hdxf_test::assert_eq(code_page_from_name("DOS932"), 932);
    hdxf_test::assert_eq(code_page_from_name("ANSI_932"), 932);
    hdxf_test::assert_eq(code_page_from_name("BIG5"), 950);
    hdxf_test::assert_eq(code_page_from_name("KSC5601"), 949);
    hdxf_test::assert_eq(code_page_from_name("GB2312"), 936);
    hdxf_test::assert_eq(code_page_from_name("JOHAB"), 1361);
    hdxf_test::assert_eq(code_page_from_name("ISO8859-1"), 28591);
    hdxf_test::assert_eq(code_page_from_name("ISO8859-10"), 28600);
    hdxf_test::assert_eq(code_page_from_name("MACINTOSH"), 10000);
    hdxf_test::assert_eq(code_page_from_name("ANSI_874"), 874);
    hdxf_test::assert_eq(code_page_from_name("ASCII"), hdxf::internal::ascii_code_page);
    hdxf_test::assert_eq(code_page_from_name("KLINGON"), 0);
}

TEST(code_pages_single_byte) {
    struct Case { const char* page; unsigned char byte; const char* utf8; };
    const Case cases[] = {
        {"DOS437", 0x81, "ü"}, {"DOS850", 0x9B, "ø"}, {"DOS852", 0xA5, "ą"},
        {"DOS866", 0x80, "А"}, {"DOS869", 0xA4, "Α"}, {"ISO8859-1", 0xE5, "å"},
        {"ISO8859-7", 0xC1, "Α"}, {"ISO8859-10", 0xA1, "Ą"}, {"MACINTOSH", 0x8A, "ä"},
        {"ANSI_874", 0xA1, "ก"},
    };
    for (const auto& c : cases) {
        std::string bytes = "a" + std::string(1, static_cast<char>(c.byte)) + "b";
        hdxf_test::assert_eq(decoded(bytes, c.page), std::string("a") + c.utf8 + "b", c.page);
        hdxf_test::assert_eq(encoded(std::string("a") + c.utf8 + "b", c.page), bytes, std::string(c.page) + " back");
    }
}

TEST(code_pages_double_byte) {
    struct Case { const char* page; std::string bytes; const char* utf8; const char* escaped; };
    const Case cases[] = {
        {"ANSI_932", "\x93\xFA\x96\x7B\x95\x5C\x83\x5C", "日本表ソ",
         "\\M+193FA\\M+1967B\\M+1955C\\M+1835C"},
        {"ANSI_936", "\xD6\xD0\xCE\xC4", "中文", "\\M+5D6D0\\M+5CEC4"},
        {"ANSI_949", "\xC7\xD1\xB1\xB9", "한국", "\\M+3C7D1\\M+3B1B9"},
        {"ANSI_950", "\xA4\xA4\xA4\xE5", "中文", "\\M+2A4A4\\M+2A4E5"},
        {"ANSI_1361", "\xD0\x65\x8A\x82", "한국", "\\M+4D065\\M+48A82"},
    };
    for (const auto& c : cases) {
        // Raw bytes, as another program may write them; a pair's second byte may be '\'
        hdxf_test::assert_eq(decoded(c.bytes, c.page), c.utf8, std::string(c.page) + " bytes");
        // AutoCAD's \M+ escape, in any code page
        hdxf_test::assert_eq(decoded(c.escaped, "ANSI_1252"), c.utf8, std::string(c.page) + " \\M+");
        // Written as AutoCAD writes them: \M+, the file stays ASCII
        hdxf_test::assert_eq(encoded(c.utf8, c.page), c.escaped, std::string(c.page) + " written");
    }
    // Half-width katakana is one byte in Shift-JIS
    hdxf_test::assert_eq(decoded("\xB1", "ANSI_932"), "ｱ");
    hdxf_test::assert_eq(encoded("ｱ", "ANSI_932"), "\xB1");
    // A character the code page lacks: \U+XXXX
    hdxf_test::assert_eq(encoded("å", "ANSI_932"), "\\U+00E5");
}

// ASCII DXF: a caret in the second byte of a pair is that byte, not a control character —
// reading and writing (a preserved record from binary written as ASCII)
TEST(code_pages_carets_in_pairs) {
    std::string pair = "\x81\x5E";            // U+FF0F in Shift-JIS
    hdxf_test::assert_eq(decode_carets(pair + "^J", 932), pair + "\n");
    hdxf_test::assert_eq(decoded(decode_carets(pair, 932), "ANSI_932"), "／");
    hdxf_test::assert_eq(decode_carets("^J", 1252), "\n");
    hdxf_test::assert_eq(hdxf::internal::encode_carets(pair + "\n", 932), pair + "^J", "written");
    hdxf_test::assert_eq(hdxf::internal::encode_carets("^", 1252), "^ ");
}

// Windows' own mappings, which AutoCAD uses — also where they go beyond the Unicode
// consortium's tables (Codex review of R.5)
TEST(code_pages_windows_extensions) {
    hdxf_test::assert_eq(decoded("\x80", "ANSI_936"), "€", "CP936 0x80: the euro");
    hdxf_test::assert_eq(decoded("\xAA\xA1", "ANSI_936"), "", "CP936 user-defined area");
    hdxf_test::assert_eq(encoded("€", "ANSI_936"), "\x80");
}

// A whole R12 file in Shift-JIS, raw bytes and AutoCAD's escapes, read and written back
TEST(code_pages_r12_file) {
    std::string dxf =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1009\n  9\n$DWGCODEPAGE\n  3\nANSI_932\n"
        "  0\nENDSEC\n  0\nSECTION\n  2\nENTITIES\n"
        "  0\nTEXT\n  8\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n 40\n1.0\n  1\n\x93\xFA\x96\x7B\n"
        "  0\nTEXT\n  8\n0\n 10\n0.0\n 20\n5.0\n 30\n0.0\n 40\n1.0\n  1\n\\M+1955C\n"
        "  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(reinterpret_cast<const uint8_t*>(dxf.data()), dxf.size()),
                           dwg.last_error());
    hdxf_test::assert_eq(dwg.entities()[0].as_text().content, "日本");
    hdxf_test::assert_eq(dwg.entities()[1].as_text().content, "表");
    // Unchanged, a text is as the file had it (the file's records are written back as read)
    std::vector<uint8_t> kept = dwg.save_to_memory();
    std::string k(kept.begin(), kept.end());
    hdxf_test::assert_true(k.find("\x93\xFA\x96\x7B") != std::string::npos, "the bytes as read");
    hdxf_test::assert_true(k.find("\\M+1955C") != std::string::npos, "the escape as read");
    // A text that changed is written as AutoCAD writes it
    dwg.entities()[0].as_text().content = "日本 ";
    std::vector<uint8_t> out = dwg.save_to_memory();
    std::string s(out.begin(), out.end());
    hdxf_test::assert_true(s.find("\\M+193FA\\M+1967B") != std::string::npos, "written as AutoCAD: \\M+");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    hdxf_test::assert_eq(back.entities()[0].as_text().content, "日本 ");
}
