#pragma once

#include "hdxf.hpp"
#include "fonts.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The size of text as AutoCAD lays it out (rättningsfas R.8): a line of TEXT, ATTRIB or
// ATTDEF in its style's font and the justification that places it, and SHAPE.

namespace hdxf {
namespace internal {

// What the %% codes stand for, outside Unicode so each font is asked for its own: a
// Unicode font (TrueType, unifont) has °, ± and ∅ (Ø when it lacks ∅), a shape font
// numbered by the code page has them as 127, 128 and 129 (AutoCAD's special characters).
// %%u and %%o turn the underline and the overline on or off.
constexpr char32_t kDegree = 0x110000;
constexpr char32_t kPlusMinus = 0x110001;
constexpr char32_t kDiameter = 0x110002;
constexpr char32_t kUnderline = 0x110003;
constexpr char32_t kOverline = 0x110004;

// A box in the text's own coordinates (x along the baseline from the insertion point, y up)
struct TextBox {
    bool valid = false;
    double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;

    void add(double x, double y) {
        if (!valid) { min_x = max_x = x; min_y = max_y = y; valid = true; return; }
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
    void add(const TextBox& b) {
        if (b.valid) { add(b.min_x, b.min_y); add(b.max_x, b.max_y); }
    }
};

// The fonts of a text style: its font and big font, found in font_paths(); AutoCAD's
// alternate font (simplex.shx, FONTALT) when the style's is not found. font is null when
// none is. code_page is the drawing's ($DWGCODEPAGE), which a shape font and a big font
// number their characters by.
struct StyleFonts {
    std::shared_ptr<const Font> font;
    std::shared_ptr<const Font> big_font;
    std::string missing;                // the font that was not found, if any
    std::string missing_big_font;       // the big font that was not found, if any
    double width_factor = 1.0;
    double oblique_angle = 0.0;         // degrees
    int code_page = 1252;
    // What names the font, as the style (or MTEXT's \f, \F) has it — for proxy graphics:
    // a TrueType family with its bold, italic and pitch and family (the style's XDATA, \f's
    // p), or a font file and big font file
    std::string family;
    bool bold = false;
    bool italic = false;
    int pitch_and_family = 0;
    std::string file;
    std::string big_font_file;
};
StyleFonts style_fonts(const Drawing& dwg, const std::string& style);

// The drawing's code page ($DWGCODEPAGE, ANSI_1252 when it has none)
int drawing_code_page(const Drawing& dwg);

// UTF-8 to code points, as they are
std::u32string code_points(const std::string& utf8);

// TEXT's characters: UTF-8 to code points, and the % codes: %%c, %%d, %%p, %%u, %%o as
// above, %%% a %, %%nnn the character nnn of the code page
std::u32string text_characters(const std::string& utf8, int code_page);

// A character in the style's fonts: a big font first for a character of the code page's
// double-byte set, then the font; a character neither has as "?", as AutoCAD shows it
Glyph text_glyph(const StyleFonts& fonts, char32_t c);

// One line of text at a height: the ink box (the underline and the overline are ink too)
// and where the pen ends (the advance), with the width factor and the oblique angle
struct LineMeasure {
    TextBox ink;
    double advance = 0.0;
};
LineMeasure measure_line(const StyleFonts& fonts, const std::u32string& text, double height,
                         double width_factor, double oblique_degrees);

// What places a line of TEXT, ATTRIB or ATTDEF
struct TextLine {
    std::string style;
    std::string content;
    double height = 0.0;
    double width_factor = 1.0;
    double oblique_angle = 0.0;         // degrees
    double rotation = 0.0;              // degrees
    int generation_flags = 0;           // 2 backward, 4 upside down
    int h_justify = 0;
    int v_justify = 0;
    Point3 position;                    // 10
    Point3 alignment_point;             // 11
};

// A text's box placed in its OCS: the four corners (z of the insertion point). font is
// false when neither the style's font nor simplex.shx is found; ink is false when the
// text draws nothing (empty, only spaces).
struct TextCorners {
    bool font = false;
    bool ink = false;
    std::array<Point3, 4> corners;
};

// A line of laid out MTEXT as TEXT would draw it: where its pen starts and its baseline
// (from the insertion point, x along the MTEXT's direction), its text with TEXT's %%
// codes, and its height
struct MTextLine {
    double x = 0.0;
    double baseline = 0.0;
    double height = 0.0;
    std::string text;
};

// A run of laid out MTEXT as AutoCAD draws it in proxy graphics (plan phase 3, B38): text
// of one format from where its pen starts on its baseline, or a stack's bar — in the
// MTEXT's plane (x along its direction, y up) from its insertion point
struct MTextRun {
    bool bar = false;                   // a stack's bar from (x, y) to (x2, y2)
    double x = 0.0, y = 0.0;
    double x2 = 0.0, y2 = 0.0;
    double height = 0.0;
    double width_factor = 1.0;
    double oblique = 0.0;               // degrees
    double tracking = 1.0;
    double advance = 0.0;               // where the pen ends, from x
    const StyleFonts* fonts = nullptr;
    std::string text;                   // the characters drawn (%%c as the font has it)
    std::string code_text;              // the same as TEXT writes it (%%c, %%d, %%p; a multiline attribute's fragments)
    bool underline = false;
    bool overline = false;
    bool has_color = false;             // \C or \c set the color
    Color color;
};

// MTEXT laid out (mtext_layout.cpp): the width of its widest line (42) and its height (43)
// as AutoCAD computes them, and in its own plane — x along its direction (11), y up, from
// the insertion point — the box of its lines and its box in the extents (the lines and
// the width it is set in)
struct MTextSize {
    bool font = false;
    bool ink = false;
    double width = 0.0;
    double height = 0.0;
    TextBox lines;
    TextBox box;
    std::vector<MTextLine> text_lines;
    std::vector<MTextRun> runs;         // when asked for (TextLayout::mtext's runs)
};

// Where TEXT's pen starts (its baseline's start, before backward/upside down mirror it)
// and the height, width factor and rotation it is drawn with (Aligned and Fit change
// them); font is false when no font is found
struct TextPen {
    bool font = false;
    Point3 pen;
    double height = 0.0;
    double width_factor = 1.0;
    double rotation = 0.0;              // degrees
};

// The text of a drawing, the fonts of each style looked up once; the fonts that were not
// found, each once, for the save's warnings
class TextLayout {
public:
    explicit TextLayout(const Drawing& dwg) : dwg_(dwg), code_page_(drawing_code_page(dwg)) {}

    const StyleFonts& fonts(const std::string& style);
    TextCorners text(const TextLine& t);
    TextCorners shape(const ShapeData& s);
    // The text laid out, with its columns (MTextData::columns); runs: also the runs AutoCAD's
    // proxy graphics draws (MTextSize::runs)
    MTextSize mtext(const MTextData& d, bool runs = false);
    // Where TEXT's pen starts (TextPen)
    TextPen text_pen(const TextLine& t);
    // MTEXT's paragraphs as TEXT would have them (see MTextLine), without measuring
    std::vector<std::string> mtext_paragraphs(const std::string& content, const std::string& style);
    // TOLERANCE: its frame in its plane (x along 11), from the insertion point
    MTextSize tolerance(const ToleranceData& d, const std::vector<XdataList>& xdata);
    // A font a format code names (MTEXT \f family|b|i, \F file), over the style's fonts
    const StyleFonts& font_override(const StyleFonts& base, const std::string& spec, bool by_family);

    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    const Drawing& dwg_;
    int code_page_;
    std::map<std::string, StyleFonts> styles_;      // by upper-case name
    std::map<std::string, StyleFonts> overrides_;   // by the format code and the base font
    std::vector<std::string> warnings_;
    std::vector<std::string> reported_;             // shape files not found (upper case)
};

// A text MULTILEADER's base point and text location from its leader, text and context
// (drawing_mleader.cpp)
void place_mleader_text(const Drawing& dwg, MLeaderData& d);

} // namespace internal
} // namespace hdxf
