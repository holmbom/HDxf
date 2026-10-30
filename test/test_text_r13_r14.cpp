#include "test_framework.hpp"
#include "hdxf.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Text, code pages, names, fonts and paths in DXF R13 and R14 (plan phase 7, step 7.21, B1, B55, B90,
// B98): the text as AutoCAD 2000 saved it as R14 has it (ASCII as it is, everything else \U+XXXX, a
// character above U+FFFF as its two surrogates, ^J for a line feed), a file's own $DWGCODEPAGE (dos932 of
// ezdxf/small_r13.dxf) for the strings it keeps, the symbol-name rule of R13 and R14 ($EXTNAMES 0: 31
// characters, A to Z, 0 to 9, $, _ and -, "*" first for a system name), the multiline style name of 32
// characters, MTEXT in parts of 255 (R13) and 250 (R14) characters, fonts and a Unicode file path. No
// golden files.
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;

const DxfVersion older_versions[] = {DxfVersion::R13, DxfVersion::R14};
const DxfFormat both_formats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfVersion v) { return v == DxfVersion::R13 ? "R13" : "R14"; }
std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

std::filesystem::path external(const std::string& folder, const std::string& file) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / folder / file;
}

// The UTF-8 of some characters, in pieces so that no hex escape runs into the next letter
const std::string swedish = std::string("\xC3\x85") + "ke " + "\xC3\x96" + "berg " + "\xC3\xA5\xC3\xA4\xC3\xB6";
const std::string swedish_escapes = "\\U+00C5ke \\U+00D6berg \\U+00E5\\U+00E4\\U+00F6";
const std::string cjk = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";    // U+65E5 U+672C U+8A9E
const std::string cjk_escapes = "\\U+65E5\\U+672C\\U+8A9E";
const std::string emoji = "\xF0\x9F\x98\x80";                       // U+1F600
const std::string emoji_escapes = "\\U+D83D\\U+DE00";

std::string as_text(const std::vector<uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }

bool contains(const std::vector<uint8_t>& bytes, const std::string& what) {
    return as_text(bytes).find(what) != std::string::npos;
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), what + ": " + dwg.last_error());
    return out;
}

std::string save_error(hdxf::Drawing& dwg, DxfFormat format = DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    return out.empty() ? dwg.last_error() : std::string();
}

hdxf::Drawing read_back(const std::vector<uint8_t>& bytes, DxfVersion expected) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), dwg.last_error());
    hdxf_test::assert_true(dwg.version() == expected, "read as its version");
    return dwg;
}

// The (code, value) pairs of an ASCII file; the values as they are written
std::vector<std::pair<int, std::string>> ascii_groups(const std::vector<uint8_t>& bytes) {
    std::string text = as_text(bytes);
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        start = end + 1;
    }
    std::vector<std::pair<int, std::string>> out;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) out.push_back({std::atoi(lines[i].c_str()), lines[i + 1]});
    return out;
}

const hdxf::Entity* first_of(const hdxf::Drawing& dwg, hdxf::EntityType type) {
    for (const auto& e : dwg.entities()) {
        if (e.type == type) return &e;
    }
    return nullptr;
}

std::filesystem::path temp_dir(const std::string& name) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / hdxf::path_from_utf8(name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

struct FontPaths {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    explicit FontPaths(std::vector<std::filesystem::path> now) { hdxf::set_font_paths(std::move(now)); }
    ~FontPaths() { hdxf::set_font_paths(saved); }
};

} // namespace

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

// Swedish, CJK and a character above U+FFFF: written as AutoCAD 2000 writes R14 (\U+XXXX, the supplementary
// character as two surrogates, in R13 the same), read back as the same UTF-8 — in TEXT, MTEXT and an
// INSERT's attribute, ASCII and binary
TEST(text_r13_r14_unicode_round_trip) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.add_text({0, 0, 0}, swedish + " " + cjk + " " + emoji);
            dwg.add_mtext({0, 5, 0}, swedish + "\\P" + cjk + emoji);
            dwg.add_block("BLK");
            hdxf::Entity& ins = dwg.add_insert("BLK", {1, 1, 0});
            hdxf::InsertAttribute a;
            a.data.tag = "NAME";
            a.data.value = cjk + swedish;
            ins.as_insert().attributes.push_back(a);

            std::vector<uint8_t> bytes = save(dwg, format, what);
            if (format == DxfFormat::Ascii) {
                hdxf_test::assert_true(contains(bytes, swedish_escapes + " " + cjk_escapes + " " + emoji_escapes), what + ": TEXT");
                hdxf_test::assert_true(contains(bytes, cjk_escapes + emoji_escapes), what + ": MTEXT");
                hdxf_test::assert_false(contains(bytes, swedish), what + ": UTF-8 bytes in the file");
            }

            hdxf::Drawing back = read_back(bytes, v);
            const hdxf::Entity* text = first_of(back, hdxf::EntityType::Text);
            const hdxf::Entity* mtext = first_of(back, hdxf::EntityType::MText);
            const hdxf::Entity* insert = first_of(back, hdxf::EntityType::Insert);
            hdxf_test::assert_true(text && mtext && insert, what + ": entities");
            hdxf_test::assert_eq(swedish + " " + cjk + " " + emoji, text->as_text().content, what + ": TEXT");
            hdxf_test::assert_eq(swedish + "\\P" + cjk + emoji, mtext->as_mtext().content, what + ": MTEXT");
            hdxf_test::assert_eq(1, static_cast<int>(insert->as_insert().attributes.size()), what + ": attribute");
            hdxf_test::assert_eq(cjk + swedish, insert->as_insert().attributes[0].data.value, what + ": attribute value");

            // Written again, the text is the same
            hdxf::Drawing twice = read_back(save(back, format, what + " again"), v);
            hdxf_test::assert_eq(swedish + " " + cjk + " " + emoji, first_of(twice, hdxf::EntityType::Text)->as_text().content,
                                 what + ": again");
        }
    }
}

// ^J for a line feed and "^ " for the caret itself in ASCII; none in binary
TEST(text_r13_r14_ascii_caret) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.add_text({0, 0, 0}, "one\ntwo^three\tfour");
            std::vector<uint8_t> bytes = save(dwg, format, what);
            if (format == DxfFormat::Ascii) {
                hdxf_test::assert_true(contains(bytes, "one^Jtwo^ three^Ifour"), what + ": carets");
            } else {
                hdxf_test::assert_true(contains(bytes, std::string("one\ntwo^three\tfour")), what + ": binary text as it is");
            }
            hdxf::Drawing back = read_back(bytes, v);
            hdxf_test::assert_eq("one\ntwo^three\tfour", first_of(back, hdxf::EntityType::Text)->as_text().content, what);
        }
    }
}

// A long MTEXT: groups 3 of at most 255 characters in R13 and 250 in R14, then the last in 1, split between
// characters — never inside a \U+XXXX or between the two surrogates of one character
TEST(text_r13_r14_long_mtext_is_split_by_the_versions_limit) {
    for (DxfVersion v : older_versions) {
        const size_t limit = v == DxfVersion::R13 ? 255 : 250;
        // Letters of one, two, three and four bytes in UTF-8: written as one letter and \U+XXXX escapes
        std::string content;
        for (int i = 0; i < 40; ++i) content += "abc" + swedish + cjk + emoji + "\\P";
        hdxf::Drawing dwg(v);
        dwg.add_mtext({0, 0, 0}, content);
        std::vector<uint8_t> bytes = save(dwg, DxfFormat::Ascii, name_of(v));

        std::vector<std::string> parts;
        std::string last;
        bool in_mtext = false;
        for (const auto& g : ascii_groups(bytes)) {
            if (g.first == 0) in_mtext = g.second == "MTEXT";
            if (!in_mtext) continue;
            if (g.first == 3) parts.push_back(g.second);
            if (g.first == 1) last = g.second;
        }
        hdxf_test::assert_true(parts.size() >= 2, name_of(v) + ": parts");
        std::string joined;
        for (const auto& p : parts) {
            hdxf_test::assert_true(p.size() <= limit, name_of(v) + ": a part of " + std::to_string(p.size()) + " characters");
            hdxf_test::assert_true(p.size() + 7 > limit, name_of(v) + ": a part that could have held more: " + std::to_string(p.size()));
            const size_t escape = p.rfind("\\U+");
            hdxf_test::assert_true(escape == std::string::npos || escape + 7 <= p.size(), name_of(v) + ": an escape cut in two");
            // not a low surrogate first (U+DC00 to U+DFFF), not a high one (U+D800 to U+DBFF) last
            const bool low_first = p.compare(0, 3, "\\U+") == 0 && p[3] == 'D' && p.size() > 4 &&
                                   std::string("CDEF").find(p[4]) != std::string::npos;
            hdxf_test::assert_false(low_first, name_of(v) + ": a part starts with a low surrogate");
            const bool high_last = p.size() >= 7 && p.compare(p.size() - 7, 3, "\\U+") == 0 && p[p.size() - 4] == 'D' &&
                                   std::string("89AB").find(p[p.size() - 3]) != std::string::npos;
            hdxf_test::assert_false(high_last, name_of(v) + ": a part ends with a high surrogate");
            joined += p;
        }
        hdxf_test::assert_true(last.size() <= limit, name_of(v) + ": the last part");
        joined += last;

        hdxf::Drawing back = read_back(bytes, v);
        hdxf_test::assert_eq(content, first_of(back, hdxf::EntityType::MText)->as_mtext().content, name_of(v) + ": read back");
        // The text as written in one piece is the content with its escapes
        std::string escaped;
        for (int i = 0; i < 40; ++i) escaped += "abc" + swedish_escapes + cjk_escapes + emoji_escapes + "\\P";
        hdxf_test::assert_eq(escaped, joined, name_of(v) + ": the parts joined");
    }
}

// R13 and R14 are one part longer or shorter than each other: 255 against 250 characters
TEST(text_r13_r14_mtext_limits_differ) {
    std::string content(600, 'x');
    size_t sizes[2] = {0, 0};
    int i = 0;
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg(v);
        dwg.add_mtext({0, 0, 0}, content);
        std::vector<uint8_t> bytes = save(dwg, DxfFormat::Ascii, name_of(v));
        std::vector<size_t> parts;
        bool in_mtext = false;
        for (const auto& g : ascii_groups(bytes)) {
            if (g.first == 0) in_mtext = g.second == "MTEXT";
            if (in_mtext && g.first == 3) parts.push_back(g.second.size());
        }
        hdxf_test::assert_eq(2, static_cast<int>(parts.size()), name_of(v));
        sizes[i++] = parts[0];
    }
    hdxf_test::assert_eq(255, static_cast<int>(sizes[0]), "R13");
    hdxf_test::assert_eq(250, static_cast<int>(sizes[1]), "R14");
}

// $DWGCODEPAGE dos932 (ezdxf/small_r13.dxf, AutoCAD's own R13 family): a Shift-JIS string of the file is
// decoded to UTF-8, kept as its bytes while it is not changed, new text is \U+XXXX; a changed header page
// converts what the file kept
TEST(text_r13_r14_code_page_dos932) {
    const std::string sjis = "\x93\xFA\x96\x7B\x8C\xEA";   // U+65E5 U+672C U+8A9E in code page 932
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            // A file that says dos932 and has Shift-JIS bytes: made from our own, the bytes put in by hand
            hdxf::Drawing made(v);
            made.set_header_string("$DWGCODEPAGE", 3, "dos932");
            made.add_text({0, 0, 0}, cjk);
            std::vector<uint8_t> bytes = save(made, DxfFormat::Ascii, what);
            std::string text = as_text(bytes);
            const size_t at = text.find(cjk_escapes);
            hdxf_test::assert_true(at != std::string::npos, what + ": the escapes");
            text.replace(at, cjk_escapes.size(), sjis);
            if (format == DxfFormat::Binary) {
                // (the binary file has the bytes too: made through the reader and the writer)
                hdxf::Drawing ascii = read_back(std::vector<uint8_t>(text.begin(), text.end()), v);
                std::vector<uint8_t> binary = save(ascii, DxfFormat::Binary, what);
                text = as_text(binary);
            }
            std::vector<uint8_t> file(text.begin(), text.end());

            hdxf::Drawing dwg = read_back(file, v);
            hdxf_test::assert_eq(cjk, first_of(dwg, hdxf::EntityType::Text)->as_text().content, what + ": decoded");

            // Saved unchanged: the bytes are the file's
            std::vector<uint8_t> same = save(dwg, format, what + " unchanged");
            hdxf_test::assert_true(contains(same, sjis), what + ": the string as its bytes");
            // Something added: the old string stays its bytes, the new one is escapes
            dwg.add_text({0, 5, 0}, swedish + cjk);
            std::vector<uint8_t> added = save(dwg, format, what + " added");
            hdxf_test::assert_true(contains(added, sjis), what + ": the old string still its bytes");
            if (format == DxfFormat::Ascii) hdxf_test::assert_true(contains(added, swedish_escapes + cjk_escapes), what + ": the new one");
            hdxf::Drawing back = read_back(added, v);
            hdxf_test::assert_eq(2, static_cast<int>(back.entities().size()), what);
            hdxf_test::assert_eq(cjk, back.entities()[0].as_text().content, what + ": first");
            hdxf_test::assert_eq(swedish + cjk, back.entities()[1].as_text().content, what + ": second");
            const hdxf::HeaderVar* page = back.find_header("$DWGCODEPAGE");
            hdxf_test::assert_true(page && !page->values.empty() && page->values[0].second == "dos932", what + ": $DWGCODEPAGE");
        }
    }
}

// \M+nXXXX (AutoCAD's escape for a character of a double-byte code page: 1 is 932) is read in R13 and R14; what
// is written is \U+XXXX, as AutoCAD 2000 writes R14 (format profile 12: no AutoCAD-origin R13 file has either)
TEST(text_r13_r14_reads_m_plus_escapes) {
    for (DxfVersion v : older_versions) {
        hdxf::Drawing made(v);
        made.set_header_string("$DWGCODEPAGE", 3, "dos932");
        made.add_text({0, 0, 0}, cjk);
        std::string text = as_text(save(made, DxfFormat::Ascii, name_of(v)));
        const size_t at = text.find(cjk_escapes);
        hdxf_test::assert_true(at != std::string::npos, name_of(v));
        text.replace(at, cjk_escapes.size(), "\\M+193FA\\M+1967B\\M+18CEA");
        std::vector<uint8_t> file(text.begin(), text.end());
        hdxf::Drawing dwg = read_back(file, v);
        hdxf_test::assert_eq(cjk, first_of(dwg, hdxf::EntityType::Text)->as_text().content, name_of(v));
        // New text beside it is \U+XXXX, never \M+
        dwg.add_text({0, 5, 0}, cjk);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, name_of(v) + " written");
        hdxf_test::assert_true(contains(out, cjk_escapes), name_of(v) + ": the new text");
        hdxf_test::assert_true(contains(out, "\\M+193FA\\M+1967B\\M+18CEA"), name_of(v) + ": the old text as it was");
    }
}

// The file of AutoCAD's own with dos932: new text written next to it reads back
TEST(text_r13_r14_small_r13_with_new_text) {
    if (!std::filesystem::exists(external("ezdxf", "small_r13.dxf"))) hdxf_test::skip("ezdxf/small_r13.dxf is not there");
    for (DxfFormat format : both_formats) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(external("ezdxf", "small_r13.dxf")), dwg.last_error());
        const size_t before = dwg.entities().size();
        dwg.add_text({0, 0, 0}, swedish + " " + cjk + emoji);
        std::vector<uint8_t> bytes = save(dwg, format, name_of(format));
        hdxf::Drawing back = read_back(bytes, DxfVersion::R13);
        hdxf_test::assert_eq(static_cast<int>(before + 1), static_cast<int>(back.entities().size()), name_of(format));
        hdxf_test::assert_eq(swedish + " " + cjk + emoji, back.entities().back().as_text().content, name_of(format));
        const hdxf::HeaderVar* page = back.find_header("$DWGCODEPAGE");
        hdxf_test::assert_true(page && page->values[0].second == "dos932", "the code page stays the file's");
    }
}

// The header's code page of a new drawing is ANSI_1252, and text outside ASCII is escapes whatever it is
TEST(text_r13_r14_new_drawing_code_page) {
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg(v);
        dwg.add_text({0, 0, 0}, swedish);
        std::vector<uint8_t> bytes = save(dwg, DxfFormat::Ascii, name_of(v));
        hdxf::Drawing back = read_back(bytes, v);
        const hdxf::HeaderVar* page = back.find_header("$DWGCODEPAGE");
        hdxf_test::assert_true(page && !page->values.empty(), name_of(v) + ": $DWGCODEPAGE");
        hdxf_test::assert_eq("ANSI_1252", page->values[0].second, name_of(v));
        // (no byte above 0x7F in the file: what 1252 has is escaped too, as AutoCAD's 2000 does it)
        for (uint8_t b : bytes) hdxf_test::assert_true(b < 0x80, name_of(v) + ": a byte above 0x7F");
    }
}

// ---------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------

namespace {

// Each kind of table record named `name`
struct Kind {
    const char* what;           // as the error says it
    std::function<void(hdxf::Drawing&, const std::string&)> add;
};

const std::vector<Kind>& kinds() {
    static const std::vector<Kind> list = {
        {"Layer", [](hdxf::Drawing& d, const std::string& n) { d.add_layer(n); }},
        {"Linetype", [](hdxf::Drawing& d, const std::string& n) { d.add_linetype(n, "d", {0.5, -0.25}); }},
        {"Text style", [](hdxf::Drawing& d, const std::string& n) { d.add_text_style(n, "txt"); }},
        {"Dimension style", [](hdxf::Drawing& d, const std::string& n) { d.add_dim_style(n); }},
        {"Block", [](hdxf::Drawing& d, const std::string& n) { d.add_block(n); }},
        {"Application", [](hdxf::Drawing& d, const std::string& n) { d.add_app_id(n); }},
        {"UCS", [](hdxf::Drawing& d, const std::string& n) {
             hdxf::Ucs u;
             u.name = n;
             d.ucs_list().push_back(u);
         }},
        {"View", [](hdxf::Drawing& d, const std::string& n) {
             hdxf::View v;
             v.name = n;
             d.views().push_back(v);
         }},
        {"Viewport configuration", [](hdxf::Drawing& d, const std::string& n) {
             hdxf::Vport vp;
             vp.name = n;
             d.vports().push_back(vp);
         }},
    };
    return list;
}

std::string bad_character(const std::string& what, const std::string& name, const std::string& character) {
    return what + " name \"" + name + "\" has the character \"" + character +
           "\", which DXF R13 and R14 do not allow (a name has at most 31 characters: the letters A to Z, the digits, $, _ and -)";
}

} // namespace

// 31 characters are fine and 32 are an error that names the table and the name — in every table, in R13 and
// R14; the name is not shortened
TEST(text_r13_r14_names_31_and_32_characters) {
    const std::string name31(31, 'N');
    const std::string name32(32, 'N');
    for (DxfVersion v : older_versions) {
        for (const Kind& k : kinds()) {
            const std::string what = name_of(v) + " " + k.what;
            hdxf::Drawing ok(v);
            k.add(ok, name31);
            hdxf_test::assert_eq("", save_error(ok), what + ": 31 characters");

            hdxf::Drawing bad(v);
            k.add(bad, name32);
            hdxf_test::assert_eq(std::string(k.what) + " name \"" + name32 +
                                     "\" is longer than 31 characters, which DXF R13 and R14 do not allow",
                                 save_error(bad), what + ": 32 characters");
            // The same drawing with the name made shorter by the program saves
            for (auto& l : bad.layers()) if (l.name == name32) l.name = name31;
            for (auto& l : bad.linetypes()) if (l.name == name32) l.name = name31;
            for (auto& s : bad.text_styles()) if (s.name == name32) s.name = name31;
            for (auto& s : bad.dim_styles()) if (s.name == name32) s.name = name31;
            for (auto& b : bad.blocks()) if (b.name == name32) b.name = name31;
            for (auto& a : bad.app_ids()) if (a.name == name32) a.name = name31;
            for (auto& u : bad.ucs_list()) if (u.name == name32) u.name = name31;
            for (auto& w : bad.views()) if (w.name == name32) w.name = name31;
            for (auto& p : bad.vports()) if (p.name == name32) p.name = name31;
            hdxf_test::assert_eq("", save_error(bad), what + ": shortened");
        }
    }
}

// The characters of a name: A to Z, 0 to 9, $, _ and -; anything else is an error naming it, never a
// renamed name (R13 and R14, every table)
TEST(text_r13_r14_name_characters) {
    struct Case {
        std::string name;
        std::string character;      // the first character that is not allowed; empty when the name is fine
    };
    const std::vector<Case> cases = {
        {"ABC", ""},
        {"abc", ""},
        {"Wall-1_$x9", ""},
        {"A B", " "},
        {"A.B", "."},
        {"A/B", "/"},
        {"A\\B", "\\"},
        {"A\"B", "\""},
        {"A:B", ":"},
        {"A;B", ";"},
        {"A<B", "<"},
        {"A?B", "?"},
        {"A*B", "*"},
        {"A,B", ","},
        {"A=B", "="},
        {"A|B", "|"},
        {"\xC3\x85rhus", "\xC3\x85"},                                      // Å
        {"Lager_" + cjk, "\xE6\x97\xA5"},                                 // a CJK letter
        {"emoji" + emoji, emoji},
    };
    for (DxfVersion v : older_versions) {
        for (const Kind& k : kinds()) {
            for (const Case& c : cases) {
                const std::string what = name_of(v) + " " + k.what + " \"" + c.name + "\"";
                hdxf::Drawing dwg(v);
                k.add(dwg, c.name);
                if (c.character.empty()) {
                    hdxf_test::assert_eq("", save_error(dwg), what);
                } else {
                    hdxf_test::assert_eq(bad_character(k.what, c.name, c.character), save_error(dwg), what);
                }
            }
        }
    }
}

// What is not a name of a table but is spelled like one in the drawing: the system and anonymous names, the
// names AutoCAD's own R13 and R14 files have
TEST(text_r13_r14_system_names_are_valid) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            hdxf::Drawing dwg(v);
            dwg.add_block("*U1");                       // an anonymous block
            dwg.add_block("*D7");                       // a dimension's
            dwg.add_block("_ARCHTICK");
            dwg.add_layer("DEFPOINTS");
            dwg.add_linetype("ACAD_ISO02W100", "ISO dash", {12, -3});
            dwg.add_dim_style("ISO-25");
            dwg.add_app_id("ACAD");
            dwg.add_app_id("AcDbAttr");
            hdxf_test::assert_eq("", save_error(dwg, format), name_of(v) + " " + name_of(format));
            // The standard records the writer makes are written in capitals and are valid
            hdxf::Drawing plain(v);
            plain.add_line({0, 0, 0}, {1, 1, 0});
            hdxf_test::assert_eq("", save_error(plain, format), name_of(v) + " plain");
        }
    }
    // A name that depends on an xref (70 bit 16) has "|"; one that does not has not
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg(v);
        hdxf::Layer& l = dwg.add_layer("XREF|WALLS");
        hdxf_test::assert_eq(bad_character("Layer", "XREF|WALLS", "|"), save_error(dwg), name_of(v) + ": without the bit");
        l.flags = 16;
        hdxf_test::assert_eq("", save_error(dwg), name_of(v) + ": xref dependent");
    }
}

// A name an entity, the header or a style refers to follows the rule too: the layer, linetype and text style
// of a record, an INSERT's block, a dimension style, $CLAYER and the like, $UCSNAME and $PUCSNAME, a
// dimension style's arrow blocks, an override in a dimension's XDATA
TEST(text_r13_r14_names_that_are_referred_to) {
    struct Case {
        const char* what;
        std::function<void(hdxf::Drawing&)> add;
        std::string expected;
    };
    const std::vector<Case> cases = {
        {"an entity's layer", [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 1, 0}, "Lager A"); },
         bad_character("Layer", "Lager A", " ")},
        {"an entity's linetype", [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 1, 0}).props.linetype = "Dash dot"; },
         bad_character("Linetype", "Dash dot", " ")},
        {"a text's style", [](hdxf::Drawing& d) { d.add_text({0, 0, 0}, "t").as_text().style = "Big text"; },
         bad_character("Text style", "Big text", " ")},
        {"an MTEXT's style", [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "t").as_mtext().style = "Big text"; },
         bad_character("Text style", "Big text", " ")},
        {"an INSERT's block", [](hdxf::Drawing& d) { d.add_insert("My block", {0, 0, 0}); },
         bad_character("Block", "My block", " ")},
        {"a dimension style of a tolerance", [](hdxf::Drawing& d) { d.add_tolerance({0, 0, 0}, "t").as_tolerance().dim_style = "My dims"; },
         bad_character("Dimension style", "My dims", " ")},
        {"$CLAYER", [](hdxf::Drawing& d) { d.set_header_string("$CLAYER", 8, "My layer"); },
         bad_character("Layer", "My layer", " ")},
        {"$CELTYPE", [](hdxf::Drawing& d) { d.set_header_string("$CELTYPE", 6, "My type"); },
         bad_character("Linetype", "My type", " ")},
        {"$TEXTSTYLE", [](hdxf::Drawing& d) { d.set_header_string("$TEXTSTYLE", 7, "My style"); },
         bad_character("Text style", "My style", " ")},
        {"$DIMSTYLE", [](hdxf::Drawing& d) { d.set_header_string("$DIMSTYLE", 2, "My dims"); },
         bad_character("Dimension style", "My dims", " ")},
        {"$UCSNAME", [](hdxf::Drawing& d) { d.set_header_string("$UCSNAME", 2, "My UCS"); },
         bad_character("UCS", "My UCS", " ")},
        {"$PUCSNAME", [](hdxf::Drawing& d) { d.set_header_string("$PUCSNAME", 2, "My UCS"); },
         bad_character("UCS", "My UCS", " ")},
        {"$DIMBLK", [](hdxf::Drawing& d) { d.set_header_string("$DIMBLK", 1, "My arrow"); },
         bad_character("Block", "My arrow", " ")},
        {"a dimension style's DIMBLK", [](hdxf::Drawing& d) { d.add_dim_style("D").dimblk = "My arrow"; },
         bad_character("Block", "My arrow", " ")},
        {"a dimension style's DIMBLK2", [](hdxf::Drawing& d) { d.add_dim_style("D").dimblk2 = "My arrow"; },
         bad_character("Block", "My arrow", " ")},
        {"a dimension style's text style", [](hdxf::Drawing& d) { d.add_dim_style("D").dimtxsty = "My style"; },
         bad_character("Text style", "My style", " ")},
        {"an override's arrow block", [](hdxf::Drawing& d) {
             d.add_line({0, 0, 0}, {1, 1, 0}).props.xdata.push_back(
                 {"ACAD", {{1000, "DSTYLE"}, {1002, "{"}, {1070, "5"}, {1000, "My arrow"}, {1002, "}"}}});
         },
         bad_character("Block", "My arrow", " ")},
    };
    for (DxfVersion v : older_versions) {
        for (const Case& c : cases) {
            hdxf::Drawing dwg(v);
            c.add(dwg);
            hdxf_test::assert_eq(c.expected, save_error(dwg), name_of(v) + ": " + c.what);
        }
    }
    // The names a style may have: arrow blocks of the built-in kind, a UCS in the header
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg(v);
        dwg.add_dim_style("D").dimblk = "_DOT";
        dwg.set_header_string("$UCSNAME", 2, "MY_UCS");
        dwg.set_header_string("$PUCSNAME", 2, "");
        dwg.add_line({0, 0, 0}, {1, 1, 0}).props.xdata.push_back(
            {"ACAD", {{1000, "DSTYLE"}, {1002, "{"}, {1070, "5"}, {1000, "_DOT"}, {1002, "}"}}});
        hdxf_test::assert_eq("", save_error(dwg), name_of(v));
    }
}

// The multiline style's name: 32 characters (R13 reference: MLINE, $CMLSTYLE), no character rule
TEST(text_r13_r14_mline_style_name_32_characters) {
    const std::string name32(32, 'M');
    const std::string name33(33, 'M');
    for (DxfVersion v : older_versions) {
        const std::string what = name_of(v);
        const std::string error33 = "MLINE style name \"" + name33 + "\" is longer than 32 characters, which DXF R13 and R14 do not allow";
        {
            hdxf::Drawing dwg(v);
            dwg.set_header_string("$CMLSTYLE", 2, name32);
            hdxf_test::assert_eq("", save_error(dwg), what + ": $CMLSTYLE of 32");
            dwg.set_header_string("$CMLSTYLE", 2, name33);
            hdxf_test::assert_eq(error33, save_error(dwg), what + ": $CMLSTYLE of 33");
        }
        {
            hdxf::Drawing dwg(v);
            hdxf::Object o;
            o.type = hdxf::ObjectType::MlineStyle;
            hdxf::MlineStyleData style;
            style.name = name33;
            o.data = style;
            dwg.objects().push_back(o);
            hdxf_test::assert_eq(error33, save_error(dwg), what + ": MLINESTYLE of 33");
        }
        {
            hdxf::Drawing dwg(v);
            dwg.add_mline({{0, 0, 0}, {1, 0, 0}}).as_mline().style = name33;
            hdxf_test::assert_eq(error33, save_error(dwg), what + ": an MLINE of a style of 33");
            // 32 characters are not this error (the style is not in ACAD_MLINESTYLE: the MLINE says so)
            dwg.entities().back().as_mline().style = name32;
            hdxf_test::assert_true(save_error(dwg).find("longer than") == std::string::npos, what + ": 32");
        }
        {
            // A space is fine in a multiline style's name: the reference has no rule for the characters
            hdxf::Drawing dwg(v);
            dwg.set_header_string("$CMLSTYLE", 2, "My style");
            hdxf_test::assert_eq("", save_error(dwg), what + ": characters");
        }
    }
}

// R12 has no such rule: a name with a space is written
TEST(text_r13_r14_r12_names_are_not_checked) {
    hdxf::Drawing dwg;
    dwg.add_layer("A layer with a space and more than thirty-one characters");
    hdxf_test::assert_eq("", save_error(dwg));
}

// Names a read file has are its own: they are written from the file, whatever they are; the program's
// additions and renames follow the rule
TEST(text_r13_r14_a_read_files_names) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing made(v);
            made.add_layer("ABC");
            made.add_layer("KEEP");
            hdxf::Drawing scratch(DxfVersion::R14);
            hdxf::Entity on_abc = scratch.add_line({0, 0, 0}, {1, 1, 0}, "ABC");
            made.add_block("BLOCKA").entities.push_back(on_abc);
            made.add_line({0, 0, 0}, {1, 1, 0}, "ABC");
            std::vector<uint8_t> bytes = save(made, DxfFormat::Ascii, what);

            // The layer ABC called "A C" and the block BLOCKA a name of forty characters, as a file from
            // a program that has other rules would have it
            std::string text = as_text(bytes);
            const std::string long_name = "BLOCK_OF_FORTY_CHARACTERS_0123456789ABCD";
            hdxf_test::assert_eq(40, static_cast<int>(long_name.size()), "forty");
            auto replace_all = [&](const std::string& from, const std::string& to) {
                for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
                    text.replace(at, from.size(), to);
                }
            };
            replace_all("\nABC\r", "\nA C\r");
            replace_all("\nABC\n", "\nA C\n");
            replace_all("\nBLOCKA\r", "\n" + long_name + "\r");
            replace_all("\nBLOCKA\n", "\n" + long_name + "\n");
            std::vector<uint8_t> edited(text.begin(), text.end());
            hdxf::Drawing dwg = read_back(edited, v);
            bool found_layer = false;
            for (const auto& l : dwg.layers()) found_layer = found_layer || l.name == "A C";
            hdxf_test::assert_true(found_layer, what + ": the layer read as \"A C\"");
            hdxf_test::assert_true(dwg.find_block(long_name) != nullptr, what + ": the long block name");

            hdxf_test::assert_eq("", save_error(dwg, format), what + ": the file as it is");
            // The program adds: the rule
            hdxf::Drawing added = read_back(edited, v);
            added.add_layer("NEW LAYER");
            hdxf_test::assert_eq(bad_character("Layer", "NEW LAYER", " "), save_error(added, format), what + ": added");
            // The program renames one of the file's
            hdxf::Drawing renamed = read_back(edited, v);
            for (auto& l : renamed.layers()) if (l.name == "KEEP") l.name = "KEEP IT";
            hdxf_test::assert_eq(bad_character("Layer", "KEEP IT", " "), save_error(renamed, format), what + ": renamed");
            // The program puts an entity on a layer that has a bad name and is not the file's
            hdxf::Drawing moved = read_back(edited, v);
            moved.entities().front().props.layer = "A C B";
            hdxf_test::assert_eq(bad_character("Layer", "A C B", " "), save_error(moved, format), what + ": moved");
            // ... and on the file's own layer
            hdxf::Drawing fine = read_back(edited, v);
            fine.entities().front().props.layer = "A C";
            hdxf_test::assert_eq("", save_error(fine, format), what + ": the file's layer");
        }
    }
}

// ---------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------

// A font that cannot be found is a warning, not an error; the style as TrueType (family XDATA) or SHX is
// written and read back (R14: AutoCAD 2000 as R14 has the TrueType form; R13 has no sample)
TEST(text_r13_r14_fonts_missing_font_is_a_warning) {
    FontPaths none({});
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.add_text_style("SHXSTYLE", "no_such_font_hdxf.shx");
            hdxf::TextStyle& tt = dwg.add_text_style("TTSTYLE", "no_such_font_hdxf.ttf");
            tt.font_family = "No Such Family";
            tt.font_flags = 0x2000000;
            dwg.add_text({0, 0, 0}, "Hello").as_text().style = "SHXSTYLE";
            dwg.add_text({0, 5, 0}, "World").as_text().style = "TTSTYLE";
            std::vector<uint8_t> bytes = save(dwg, format, what);
            bool shx = false, ttf = false;
            for (const auto& w : dwg.warnings()) {
                if (w.find("Text style \"SHXSTYLE\": font \"no_such_font_hdxf.shx\" not found") == 0) shx = true;
                if (w.find("Text style \"TTSTYLE\": font \"no_such_font_hdxf.ttf\" not found") == 0) ttf = true;
            }
            hdxf_test::assert_true(shx, what + ": SHX warning");
            hdxf_test::assert_true(ttf, what + ": TrueType warning");

            hdxf::Drawing back = read_back(bytes, v);
            const hdxf::TextStyle* shx_style = nullptr;
            const hdxf::TextStyle* tt_style = nullptr;
            for (const auto& s : back.text_styles()) {
                if (s.name == "SHXSTYLE") shx_style = &s;
                if (s.name == "TTSTYLE") tt_style = &s;
            }
            hdxf_test::assert_true(shx_style && tt_style, what + ": styles");
            hdxf_test::assert_eq("no_such_font_hdxf.shx", shx_style->font_file, what);
            hdxf_test::assert_eq("no_such_font_hdxf.ttf", tt_style->font_file, what);
            hdxf_test::assert_eq("No Such Family", tt_style->font_family, what + ": the family");
            hdxf_test::assert_eq(0x2000000, tt_style->font_flags, what + ": the family's flags");
            // Read and written again: the style is still as it was
            hdxf::Drawing twice = read_back(save(back, format, what + " again"), v);
            bool family = false;
            for (const auto& st : twice.text_styles()) family = family || st.font_family == "No Such Family";
            hdxf_test::assert_true(family, what + ": the family after a second round");
        }
    }
}

// A font that is found: no warning (a TrueType font of Windows when the machine has one)
TEST(text_r13_r14_fonts_found_font_is_no_warning) {
    const char* windir = std::getenv("WINDIR");
    if (!windir) hdxf_test::skip("no WINDIR");
    const std::filesystem::path fonts = hdxf::path_from_utf8(windir) / "Fonts";
    if (!std::filesystem::exists(fonts / "arial.ttf")) hdxf_test::skip("arial.ttf is not there");
    FontPaths paths({fonts});
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg(v);
        hdxf::TextStyle& style = dwg.add_text_style("ARIALSTYLE", "arial.ttf");
        style.font_family = "Arial";
        dwg.add_text({0, 0, 0}, "Hello").as_text().style = "ARIALSTYLE";
        save(dwg, DxfFormat::Ascii, name_of(v));
        hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), name_of(v) + ": warnings");
        for (const auto& w : dwg.warnings()) hdxf_test::assert_true(w.find("ARIALSTYLE") == std::string::npos, name_of(v) + ": " + w);
    }
}

// ---------------------------------------------------------------------------
// Paths
// ---------------------------------------------------------------------------

// A file path with Swedish, CJK and an emoji in it: written, loaded and saved again, R13 and R14, ASCII and
// binary; the path is std::filesystem::path and not narrowed
TEST(text_r13_r14_unicode_file_path) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            const std::filesystem::path dir = temp_dir(std::string("hdxf_j_") + swedish + "_" + cjk + "_" + emoji);
            const std::filesystem::path path = dir / hdxf::path_from_utf8(swedish + cjk + ".dxf");
            hdxf::Drawing dwg(v);
            dwg.add_text({0, 0, 0}, swedish);
            hdxf_test::assert_true(dwg.save_as(path, format), what + ": " + dwg.last_error());
            hdxf_test::assert_true(std::filesystem::exists(path), what + ": the file");

            hdxf::Drawing loaded;
            hdxf_test::assert_true(loaded.load(path), what + ": " + loaded.last_error());
            hdxf_test::assert_true(loaded.version() == v, what);
            hdxf_test::assert_eq(swedish, loaded.entities()[0].as_text().content, what);
            loaded.add_line({0, 0, 0}, {1, 1, 0});
            hdxf_test::assert_true(loaded.save(format), what + ": save(): " + loaded.last_error());
            hdxf::Drawing again;
            hdxf_test::assert_true(again.load(path), again.last_error());
            hdxf_test::assert_eq(2, static_cast<int>(again.entities().size()), what);

            // A failed save leaves the file
            hdxf::Drawing bad;
            hdxf_test::assert_true(bad.load(path), bad.last_error());
            bad.add_line({0, 0, 0}, {1, 1, 0}).props.lineweight = 25;
            hdxf_test::assert_false(bad.save(format), what);
            hdxf::Drawing still;
            hdxf_test::assert_true(still.load(path), still.last_error());
            hdxf_test::assert_eq(2, static_cast<int>(still.entities().size()), what + ": the file as it was");
            std::filesystem::remove_all(dir);
        }
    }
}

// The path of an IMAGEDEF (R14): UTF-8 in the API, \U+XXXX in the file (the form of the text of AutoCAD 2000
// as R14), the same UTF-8 back; relative, absolute and with backslashes
TEST(text_r13_r14_image_path) {
    const std::vector<std::string> paths = {
        "bilder/B\xC3\xA5t_" + cjk + ".png",
        "C:\\Bilder\\" + std::string("\xC3\x96") + "stra " + emoji + ".png",
        "plain.png",
    };
    for (DxfFormat format : both_formats) {
        for (const std::string& path : paths) {
            const std::string what = name_of(format) + " " + path;
            hdxf::Drawing dwg(DxfVersion::R14);
            dwg.add_image(path, {0, 0, 0}, {10, 10}, 5);
            std::vector<uint8_t> bytes = save(dwg, format, what);
            if (format == DxfFormat::Ascii && path.find("\xC3") != std::string::npos) {
                hdxf_test::assert_false(contains(bytes, "\xC3"), what + ": a UTF-8 byte in the file");
                hdxf_test::assert_true(contains(bytes, "\\U+"), what + ": escapes");
            }
            hdxf::Drawing back = read_back(bytes, DxfVersion::R14);
            std::string found;
            for (const auto& o : back.objects()) {
                if (o.type == hdxf::ObjectType::ImageDef) found = o.as_image_def().file_name;
            }
            hdxf_test::assert_eq(path, found, what + ": the path read back");
            hdxf_test::assert_eq("", save_error(back, format), what + ": written again");
        }
    }
}
