#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// The fonts a text style names, read as AutoCAD reads them, for the size of text
// (rättningsfas R.8): TrueType/OpenType (.ttf, .ttc, .otf) and AutoCAD's shape fonts (.shx:
// shapes 1.0/1.1, unifont, bigfont). Found in hdxf::font_paths() like AutoCAD's support
// file search path.

namespace hdxf {
namespace internal {

// A glyph in units of the text height (1.0 = the style's height): where the pen goes
// (advance), and the ink's box around the origin on the baseline; empty when it draws
// nothing (a space)
struct Glyph {
    bool found = false;
    double advance = 0.0;
    bool has_ink = false;
    double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
};

// A TrueType face as Windows describes it (LOGFONT): its family (name ID 1, the US English
// name), bold and italic (OS/2 fsSelection) and its pitch and family — pitch 2 variable or
// 1 fixed (post.isFixedPitch), family by the OS/2 class (sFamilyClass: 8 sans serif
// FF_SWISS, 1–7 serif FF_ROMAN, 10 script FF_SCRIPT, 9 and 12 FF_DECORATIVE; a fixed pitch
// font FF_MODERN) — Arial 34 as AutoCAD writes it
struct FontFace {
    std::string family;
    bool bold = false;
    bool italic = false;
    int pitch_and_family = 0;
};

class Font {
public:
    virtual ~Font() = default;
    virtual Glyph glyph(char32_t c) const = 0;
    virtual bool is_shape_font() const = 0;
    // How far below the baseline the font reaches (positive), in text heights: a shape
    // font's "below" over its "above", a TrueType font's Windows descent (usWinDescent,
    // what GDI reports)
    virtual double descent() const = 0;
    // How far above the baseline its cell reaches, in text heights: a TrueType font's
    // Windows ascent (usWinAscent), a shape font's "above" (1)
    virtual double ascent() const = 0;
    // Where an underline goes below the baseline (positive), in text heights: a TrueType
    // font's post.underlinePosition, a shape font's "below" (its descent)
    virtual double underline() const = 0;
    // Whether the font numbers its characters by Unicode (TrueType, unifont); a shape font
    // (shapes 1.0/1.1) numbers them by the drawing's code page, a big font by its double-
    // byte codes
    virtual bool unicode() const = 0;
    virtual bool big_font() const { return false; }
    // A shape in a shape file by its name (SHAPE, complex linetypes): its number, 0 when
    // there is none
    virtual uint32_t shape_number(const std::string& name) const { (void)name; return 0; }
    // A TrueType face as Windows names it, for proxy graphics (FontFace): empty for a shape font
    virtual FontFace face() const { return {}; }
};

// The font named in a style (font file, e.g. "arial.ttf", "txt", "romans.shx"); null when
// it is not found or cannot be read. Cached; safe to call from several threads.
std::shared_ptr<const Font> find_font(const std::string& file_name);

// A TrueType/OpenType font by its family name (the style's XDATA ACAD 1000, e.g. "Arial")
// and bold/italic, as Windows chooses it: the face in font_paths() whose family (name ID 1,
// or 16) is the name, closest in bold and italic (OS/2 fsSelection). Null when no face has
// the family.
std::shared_ptr<const Font> find_font_family(const std::string& family, bool bold, bool italic);

// For tests: read a font from a file (a collection: its face `face`)
std::shared_ptr<const Font> load_font(const std::filesystem::path& path, int face = 0);

} // namespace internal
} // namespace hdxf
