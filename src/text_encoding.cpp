#include "text_encoding.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

static void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// The code point starting at s[pos]; advances pos. False for invalid UTF-8 (bad lead or
// continuation byte, truncated, overlong, surrogate, above U+10FFFF).
static bool next_utf8(const std::string& s, size_t& pos, uint32_t& cp) {
    uint8_t b = static_cast<uint8_t>(s[pos]);
    int extra;
    uint32_t min;
    if (b < 0x80)      { cp = b; ++pos; return true; }
    else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; min = 0x80; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; min = 0x800; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; min = 0x10000; }
    else return false;

    if (pos + extra >= s.size()) return false;    // truncated
    for (int i = 1; i <= extra; ++i) {
        uint8_t c = static_cast<uint8_t>(s[pos + i]);
        if ((c & 0xC0) != 0x80) return false;
        cp = (cp << 6) | (c & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    pos += extra + 1;
    return true;
}

// ---------------------------------------------------------------------------
// Code page names
// ---------------------------------------------------------------------------

int code_page_from_name(const std::string& name) {
    std::string upper;
    for (char c : name) {
        if (c != ' ') upper += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return code_page_number(upper);
}

// ---------------------------------------------------------------------------
// Double-byte code pages
// ---------------------------------------------------------------------------

// The code point of a byte (code < 0x100) or byte pair; 0 when the page has none
static char16_t double_byte_lookup(const DoubleBytePage& page, uint16_t code) {
    const DoubleBytePair* end = page.pairs + page.count;
    const DoubleBytePair* it = std::lower_bound(page.pairs, end, code,
        [](const DoubleBytePair& p, uint16_t c) { return p.code < c; });
    return it != end && it->code == code ? it->cp : 0;
}

// Whether a byte starts a pair in the page
static bool is_lead_byte(const DoubleBytePage& page, uint8_t b) {
    const DoubleBytePair* end = page.pairs + page.count;
    const DoubleBytePair* it = std::lower_bound(page.pairs, end, static_cast<uint16_t>(b << 8),
        [](const DoubleBytePair& p, uint16_t c) { return p.code < c; });
    return it != end && (it->code >> 8) == b;
}

// The byte or byte pair of a code point; 0 when the page has none (the first one when the
// page has several, as Windows encodes)
static uint16_t double_byte_code(const DoubleBytePage& page, char16_t cp) {
    static const int numbers[] = {932, 936, 949, 950, 1361};
    // Every page sorted by code point, made once (a function-local static is initialized
    // once even when several threads get here first) and read-only after
    static const std::vector<std::vector<DoubleBytePair>> by_cp = [] {
        std::vector<std::vector<DoubleBytePair>> all;
        for (int number : numbers) {
            const DoubleBytePage* p = double_byte_page(number);
            std::vector<DoubleBytePair> list(p->pairs, p->pairs + p->count);
            std::stable_sort(list.begin(), list.end(),
                             [](const DoubleBytePair& a, const DoubleBytePair& b) { return a.cp < b.cp; });
            all.push_back(std::move(list));
        }
        return all;
    }();
    size_t slot = 0;
    while (slot < 4 && numbers[slot] != page.number) ++slot;
    const std::vector<DoubleBytePair>& list = by_cp[slot];
    auto it = std::lower_bound(list.begin(), list.end(), cp,
        [](const DoubleBytePair& p, char16_t c) { return p.cp < c; });
    return it != list.end() && it->cp == cp ? it->code : 0;
}

uint16_t code_page_code(int code_page, char32_t cp) {
    if (cp < 0x80 || cp > 0xFFFF) return cp < 0x80 ? static_cast<uint16_t>(cp) : 0;
    if (const DoubleBytePage* page = double_byte_page(code_page)) {
        return double_byte_code(*page, static_cast<char16_t>(cp));
    }
    if (const char16_t* table = code_page_table(code_page)) {
        for (int i = 0; i < 128; ++i) {
            if (table[i] == cp) return static_cast<uint16_t>(0x80 + i);
        }
    }
    return 0;
}

bool needs_text_conversion(const std::string& s) {
    for (char c : s) {
        uint8_t b = static_cast<uint8_t>(c);
        if (b >= 0x80 || b < 0x20 || c == '^' || c == '\\') return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Caret notation (ASCII DXF only)
// ---------------------------------------------------------------------------

std::string decode_carets(const std::string& s, int code_page) {
    if (s.find('^') == std::string::npos) return s;
    const DoubleBytePage* page = double_byte_page(code_page);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (page && i + 1 < s.size() && is_lead_byte(*page, static_cast<uint8_t>(s[i]))) {
            out += s[i];
            out += s[++i];              // the second byte of a pair, whatever it is
            continue;
        }
        if (s[i] == '^' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == ' ') { out += '^'; ++i; continue; }
            if (next >= '@' && next <= '_') { out += static_cast<char>(next - '@'); ++i; continue; }
        }
        out += s[i];
    }
    return out;
}

std::string encode_carets(const std::string& s, int code_page) {
    const DoubleBytePage* page = double_byte_page(code_page);
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        uint8_t b = static_cast<uint8_t>(c);
        if (page && i + 1 < s.size() && is_lead_byte(*page, b)) {
            out += c;
            out += s[++i];              // the second byte of a pair, as it is
            continue;
        }
        if (c == '^') {
            out += "^ ";
        } else if (b < 0x20) {
            out += '^';
            out += static_cast<char>(b + '@');
        } else {
            out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// \U+XXXX
// ---------------------------------------------------------------------------

static bool hex4(const std::string& s, size_t pos, uint32_t& value) {
    if (pos + 4 > s.size()) return false;
    value = 0;
    for (size_t i = pos; i < pos + 4; ++i) {
        char c = s[i];
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
        value = (value << 4) | static_cast<uint32_t>(std::isdigit(static_cast<unsigned char>(c))
                                                     ? c - '0'
                                                     : std::toupper(static_cast<unsigned char>(c)) - 'A' + 10);
    }
    return true;
}

// \U+XXXX → the character (UTF-8). A surrogate pair written as two escapes is joined;
// a lone surrogate is left as written. \M+nXXXX — AutoCAD's escape for a character of a
// double-byte code page (n: 1 Shift-JIS, 2 Big5, 3 Wansung, 4 Johab, 5 GB) — likewise.
static std::string decode_unicode_escapes(const std::string& s) {
    if (s.find("\\U+") == std::string::npos && s.find("\\u+") == std::string::npos &&
        s.find("\\M+") == std::string::npos && s.find("\\m+") == std::string::npos) return s;
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        if (s[i] == '\\' && i + 7 < s.size() && (s[i + 1] == 'M' || s[i + 1] == 'm') &&
            s[i + 2] == '+' && s[i + 3] >= '1' && s[i + 3] <= '5' && hex4(s, i + 4, cp)) {
            const DoubleBytePage* page = double_byte_page(code_page_for_mif(s[i + 3] - '0'));
            char16_t ch = page ? double_byte_lookup(*page, static_cast<uint16_t>(cp)) : 0;
            if (ch != 0) {
                append_utf8(out, ch);
                i += 8;
                continue;
            }
        }
        if (s[i] == '\\' && i + 2 < s.size() && (s[i + 1] == 'U' || s[i + 1] == 'u') &&
            s[i + 2] == '+' && hex4(s, i + 3, cp)) {
            if (cp >= 0xD800 && cp <= 0xDBFF) {
                uint32_t low = 0;
                size_t j = i + 7;
                if (j + 2 < s.size() && s[j] == '\\' && (s[j + 1] == 'U' || s[j + 1] == 'u') &&
                    s[j + 2] == '+' && hex4(s, j + 3, low) && low >= 0xDC00 && low <= 0xDFFF) {
                    append_utf8(out, 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00));
                    i = j + 7;
                    continue;
                }
            } else if (cp < 0xDC00 || cp > 0xDFFF) {
                append_utf8(out, cp);
                i += 7;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// ---------------------------------------------------------------------------
// Decode (read) and encode (write)
// ---------------------------------------------------------------------------

bool decode_text(const std::string& in, int code_page, const std::string& code_page_name,
                 std::string& out, std::string& error) {
    std::string utf8;
    if (code_page == utf8_code_page) {
        size_t pos = 0;
        while (pos < in.size()) {
            uint32_t cp;
            size_t start = pos;
            if (!next_utf8(in, pos, cp)) {
                error = "Invalid UTF-8 at byte " + std::to_string(start + 1) + " of the value";
                return false;
            }
        }
        utf8 = in;
    } else if (const DoubleBytePage* page = double_byte_page(code_page)) {
        // A byte >= 0x80 is a character of its own or starts a pair; one the code page
        // does not define is U+FFFD (the Unicode replacement character)
        utf8.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            uint8_t b = static_cast<uint8_t>(in[i]);
            if (b < 0x80) {
                utf8 += in[i];
                continue;
            }
            char16_t ch = 0;
            if (i + 1 < in.size() && is_lead_byte(*page, b)) {
                ch = double_byte_lookup(*page, static_cast<uint16_t>(b << 8 | static_cast<uint8_t>(in[i + 1])));
                if (ch != 0) ++i;
            }
            if (ch == 0) ch = double_byte_lookup(*page, b);
            append_utf8(utf8, ch != 0 ? ch : 0xFFFD);
        }
    } else {
        const char16_t* table = code_page_table(code_page);
        utf8.reserve(in.size());
        for (char c : in) {
            uint8_t b = static_cast<uint8_t>(c);
            if (b < 0x80) {
                utf8 += c;
            } else if (table == nullptr) {
                error = "Unsupported code page: " + code_page_name;
                return false;
            } else {
                append_utf8(utf8, table[b - 0x80]);
            }
        }
    }
    out = decode_unicode_escapes(utf8);
    return true;
}

bool encode_text(const std::string& utf8, int code_page, std::string& out) {
    const char16_t* table = code_page_table(code_page);
    out.clear();
    out.reserve(utf8.size());
    size_t pos = 0;
    while (pos < utf8.size()) {
        uint32_t cp;
        if (!next_utf8(utf8, pos, cp)) return false;
        if (cp < 0x80) {
            out += static_cast<char>(cp);
            continue;
        }
        // Above U+FFFF: its UTF-16 surrogates as two escapes, in every version — the only form
        // AutoCAD reads (AutoCAD 2027's DXFIN refuses the character as UTF-8 in a 2007/2010
        // file, as CESU-8 too, and reads \U+D83D\U+DE00 as U+1F600 in 2004 and 2010; plan
        // phase 4, B55)
        if (cp > 0xFFFF) {
            char buf[16];
            uint32_t v = cp - 0x10000;
            std::snprintf(buf, sizeof(buf), "\\U+%04X\\U+%04X", static_cast<unsigned>(0xD800 + (v >> 10)),
                          static_cast<unsigned>(0xDC00 + (v & 0x3FF)));
            out += buf;
            continue;
        }
        if (code_page == utf8_code_page) {
            append_utf8(out, cp);
            continue;
        }
        bool found = false;
        // A double-byte code page's character as AutoCAD writes it in DXF: \M+nXXXX, so
        // the file stays ASCII (a pair's second byte could be ^ or \)
        if (const DoubleBytePage* page = double_byte_page(code_page); page && cp <= 0xFFFF) {
            if (uint16_t code = double_byte_code(*page, static_cast<char16_t>(cp))) {
                char buf[12];
                if (code < 0x100) {
                    out += static_cast<char>(code);          // a single-byte character
                } else {
                    std::snprintf(buf, sizeof(buf), "\\M+%d%04X", page->mif, static_cast<unsigned>(code));
                    out += buf;
                }
                found = true;
            }
        }
        if (!found && table != nullptr && cp <= 0xFFFF) {
            for (int i = 0; i < 128; ++i) {
                if (table[i] == cp) {
                    out += static_cast<char>(0x80 + i);
                    found = true;
                    break;
                }
            }
        }
        if (found) continue;
        char buf[8];
        std::snprintf(buf, sizeof(buf), "\\U+%04X", static_cast<unsigned>(cp));
        out += buf;
    }
    return true;
}


// ---------------------------------------------------------------------------
// Long text in parts (MTEXT, B16)
// ---------------------------------------------------------------------------

std::vector<std::string> split_encoded(const std::string& utf8, int code_page, bool carets,
                                       size_t limit, bool by_character) {
    std::vector<std::string> parts(1);
    size_t written = 0;                 // the current part's length as written
    size_t pos = 0;
    while (pos < utf8.size()) {
        // One character: a UTF-8 sequence, or one byte where the UTF-8 is broken
        uint8_t lead = static_cast<uint8_t>(utf8[pos]);
        size_t n = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3
                                   : (lead >> 3) == 0x1E ? 4 : 1;
        if (pos + n > utf8.size()) n = 1;
        for (size_t k = 1; k < n; ++k) {
            if ((static_cast<uint8_t>(utf8[pos + k]) & 0xC0) != 0x80) { n = 1; break; }
        }
        std::string ch = utf8.substr(pos, n);
        std::string encoded;
        if (!encode_text(ch, code_page, encoded)) encoded = ch;
        if (carets) encoded = encode_carets(encoded);
        // By character: in UTF-16 units, as AutoCAD holds text (one above U+FFFF counts two —
        // ODA File Converter's 2010 parts of 250 such units)
        size_t size = by_character ? (n == 4 ? 2 : 1) : encoded.size();
        if (written + size > limit && !parts.back().empty()) {
            parts.emplace_back();
            written = 0;
        }
        parts.back() += ch;
        written += size;
        pos += n;
    }
    return parts;
}

} // namespace internal
} // namespace hdxf
