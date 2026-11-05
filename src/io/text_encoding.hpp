#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hdxf {
namespace internal {

// Text in a DXF file is in the code page of $DWGCODEPAGE up to 2004 and UTF-8 from 2007.
// Every string in HDxf's API is UTF-8 (plan B1). A code page is held as its Windows number
// (1252, 437, 932, 28591 for ISO8859-1, 10000 for MACINTOSH, 20127 for ASCII …),
// utf8_code_page for UTF-8, 0 for one HDxf does not know.
constexpr int utf8_code_page = 65001;
constexpr int ascii_code_page = 20127;

// Bytes 0x80-0xFF of a single-byte code page as code points (code_pages.cpp); nullptr for
// any other code page
const char16_t* code_page_table(int code_page);

// A double-byte code page (932, 936, 949, 950, 1361): its bytes and byte pairs as code
// points, sorted by code (a single byte is its value, a pair lead << 8 | trail), and the
// digit AutoCAD's \M+ escape names it by
struct DoubleBytePair {
    uint16_t code;
    char16_t cp;
};
struct DoubleBytePage {
    int number;
    int mif;
    const DoubleBytePair* pairs;
    size_t count;
};
const DoubleBytePage* double_byte_page(int code_page);
int code_page_for_mif(int digit);          // \M+1 … \M+5 → 932, 950, 949, 1361, 936
int code_page_number(const std::string& upper_name);

// AutoCAD's names for them, any case: "ANSI_1252" → 1252, "DOS850" → 850, "ANSI_932" /
// "DOS932" → 932, "ISO8859-1" → 28591, "MACINTOSH" → 10000, "ASCII" … ; any other name → 0
int code_page_from_name(const std::string& name);

// A character's byte (0x80-0xFF) or byte pair (lead << 8 | trail) in a code page — what a
// shape font or big font numbers it by; 0 when the page does not have it
uint16_t code_page_code(int code_page, char32_t cp);

// Whether a value may need any of the conversions below — most values are plain ASCII
bool needs_text_conversion(const std::string& s);

// ASCII DXF writes control characters as ^ + letter (^J = line feed) and ^ itself as
// "^ " (DXF reference, "ASCII control characters in DXF files"). Binary DXF has neither.
// In a double-byte code page the second byte of a pair is left alone (it may be '^').
std::string decode_carets(const std::string& s, int code_page = 0);
std::string encode_carets(const std::string& s, int code_page = 0);

// Read: bytes in code_page → UTF-8, and \U+XXXX → the character. Fails on bytes >= 0x80
// when the code page is not supported, and on invalid UTF-8 (code_page UTF-8); error
// then says why.
bool decode_text(const std::string& in, int code_page, const std::string& code_page_name,
                 std::string& out, std::string& error);

// Write: UTF-8 → bytes in code_page; a character the code page lacks as \U+XXXX, one above
// U+FFFF as its UTF-16 surrogates \U+XXXX\U+XXXX (in UTF-8 too: the form AutoCAD reads).
// Fails on invalid UTF-8.
bool encode_text(const std::string& utf8, int code_page, std::string& out);

// Long text (MTEXT) in parts that are at most limit characters as written — encoded for
// code_page, with carets when carets — split between characters, never inside one (a
// UTF-8 sequence or the \U+XXXX it is written as). With by_character, at most limit
// characters each in UTF-16 units (one above U+FFFF two), however they are written. The parts
// are UTF-8; always one at least.
std::vector<std::string> split_encoded(const std::string& utf8, int code_page, bool carets,
                                       size_t limit, bool by_character = false);

} // namespace internal
} // namespace hdxf
