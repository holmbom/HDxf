#include "text_layout.hpp"
#include "dxf_writer.hpp"
#include "text_encoding.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hdxf {
namespace internal {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A byte of the code page as its code point (%%nnn)
char32_t code_page_character(int code_page, int byte) {
    if (byte < 0x80) return static_cast<char32_t>(byte);
    std::string in(1, static_cast<char>(byte)), out, error;
    if (!decode_text(in, code_page, "", out, error) || out.empty()) return U'?';
    std::u32string cps = code_points(out);
    return cps.empty() ? U'?' : cps[0];
}

} // namespace

std::u32string code_points(const std::string& utf8) {
    std::u32string cps;
    for (size_t i = 0; i < utf8.size();) {
        unsigned char b = static_cast<unsigned char>(utf8[i]);
        char32_t cp = b;
        size_t n = 1;
        if (b >= 0xF0) { cp = b & 0x07; n = 4; }
        else if (b >= 0xE0) { cp = b & 0x0F; n = 3; }
        else if (b >= 0xC0) { cp = b & 0x1F; n = 2; }
        for (size_t k = 1; k < n && i + k < utf8.size(); ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        }
        cps.push_back(cp);
        i += n;
    }
    return cps;
}

int drawing_code_page(const Drawing& dwg) {
    const HeaderVar* page = dwg.find_header("$DWGCODEPAGE");
    int number = page && !page->values.empty() ? code_page_from_name(page->values[0].second) : 0;
    return number != 0 ? number : 1252;
}

StyleFonts style_fonts(const Drawing& dwg, const std::string& style) {
    StyleFonts out;
    out.code_page = drawing_code_page(dwg);
    const TextStyle* s = nullptr;
    for (const auto& ts : dwg.text_styles()) {
        if (names_equal(ts.name, style)) { s = &ts; break; }
    }
    // A style that is not in the drawing: AutoCAD's Standard (txt)
    std::string file = s ? s->font_file : std::string("txt");
    if (s) {
        out.width_factor = s->width_factor;
        out.oblique_angle = s->oblique_angle;
        out.family = s->font_family;
        out.bold = (s->font_flags & 0x02000000) != 0;
        out.italic = (s->font_flags & 0x01000000) != 0;
        out.pitch_and_family = s->font_flags & 0xFF;
        out.big_font_file = s->big_font;
    }
    out.file = file;
    // AutoCAD finds a TrueType font by its family (and bold, italic) first
    if (s && !s->font_family.empty()) {
        out.font = find_font_family(s->font_family, s->font_flags & 0x02000000, s->font_flags & 0x01000000);
    }
    if (!out.font && !file.empty()) out.font = find_font(file);
    if (!out.font) {
        out.missing = file.empty() && s ? s->font_family : file;
        out.font = find_font("simplex.shx");            // AutoCAD's FONTALT
    }
    if (s && !s->big_font.empty()) {
        out.big_font = find_font(s->big_font);
        if (!out.big_font) out.missing_big_font = s->big_font;
    }
    return out;
}

std::u32string text_characters(const std::string& utf8, int code_page) {
    std::u32string cps = code_points(utf8);
    std::u32string out;
    for (size_t i = 0; i < cps.size(); ++i) {
        if (cps[i] == U'%' && i + 2 < cps.size() && cps[i + 1] == U'%') {
            char32_t c = cps[i + 2];
            switch (c) {
                case U'c': case U'C': out.push_back(kDiameter); i += 2; continue;
                case U'd': case U'D': out.push_back(kDegree); i += 2; continue;
                case U'p': case U'P': out.push_back(kPlusMinus); i += 2; continue;
                case U'u': case U'U': out.push_back(kUnderline); i += 2; continue;
                case U'o': case U'O': out.push_back(kOverline); i += 2; continue;
                case U'%': out.push_back(U'%'); i += 2; continue;
                default:
                    if (c >= U'0' && c <= U'9') {
                        // Up to three digits: the character of that code in the code page
                        size_t j = i + 2;
                        int v = 0;
                        while (j < cps.size() && j < i + 5 && cps[j] >= U'0' && cps[j] <= U'9') {
                            v = v * 10 + static_cast<int>(cps[j] - U'0');
                            ++j;
                        }
                        out.push_back(v < 256 ? code_page_character(code_page, v) : static_cast<char32_t>(v));
                        i = j - 1;
                        continue;
                    }
                    break;
            }
        }
        out.push_back(cps[i]);
    }
    return out;
}

Glyph text_glyph(const StyleFonts& fonts, char32_t c) {
    Glyph g;
    if (!fonts.font) return g;
    if (fonts.big_font && c >= 0x80) {
        if (uint16_t code = code_page_code(fonts.code_page, c)) g = fonts.big_font->glyph(code);
        if (g.found) return g;
    }
    const Font& font = *fonts.font;
    if (font.unicode()) {
        // A TrueType font draws the diameter sign as Ø, and the degree sign no higher than
        // the text (ODA File Converter's dimension text, matching AutoCAD's in ACadSharp's
        // sample_AC1015: ∅45,6 and 94° — and AutoCAD writes ∅ as Ø in R12)
        bool truetype = !font.is_shape_font();
        if (c == U'∅' && truetype) c = kDiameter;
        if (c == U'°') c = kDegree;
        switch (c) {
            case kDegree:
                g = font.glyph(U'°');
                if (truetype && g.has_ink) g.max_y = std::min(g.max_y, 1.0);
                break;
            case kPlusMinus: g = font.glyph(U'±'); break;
            case kDiameter:
                if (truetype) g = font.glyph(U'Ø');
                if (!g.found) g = font.glyph(U'∅');
                if (!g.found) g = font.glyph(U'Ø');
                break;
            default: g = font.glyph(c); break;
        }
    } else {
        uint32_t code = 0;
        switch (c) {
            case kDegree: code = 127; break;
            case kPlusMinus: code = 128; break;
            case kDiameter: code = 129; break;
            default: code = c < 0x80 ? c : code_page_code(fonts.code_page, c); break;
        }
        if (code != 0) g = font.glyph(code);
    }
    if (!g.found) g = font.glyph(U'?');
    return g;
}

LineMeasure measure_line(const StyleFonts& fonts, const std::u32string& text, double height,
                         double width_factor, double oblique_degrees) {
    LineMeasure m;
    if (!fonts.font) return m;
    double shear = std::tan(oblique_degrees * M_PI / 180.0);
    auto add = [&](double x, double y) { m.ink.add(x + y * shear, y); };
    // The underline below the baseline, the overline as far above the text height
    double under_y = -fonts.font->underline() * height;
    double over_y = height - under_y;
    bool under = false, over = false;
    double under_from = 0.0, over_from = 0.0;
    double x = 0.0;
    for (char32_t c : text) {
        if (c == kUnderline || c == kOverline) {
            bool& on = c == kUnderline ? under : over;
            double& from = c == kUnderline ? under_from : over_from;
            double y = c == kUnderline ? under_y : over_y;
            if (on && x != from) { add(from, y); add(x, y); }
            on = !on;
            from = x;
            continue;
        }
        Glyph g = text_glyph(fonts, c);
        if (g.has_ink) {
            // The glyph box, scaled and slanted: its corners
            double x0 = x + g.min_x * height * width_factor, x1 = x + g.max_x * height * width_factor;
            double y0 = g.min_y * height, y1 = g.max_y * height;
            add(x0, y0);
            add(x1, y0);
            add(x0, y1);
            add(x1, y1);
        }
        x += g.advance * height * width_factor;
    }
    if (under && x != under_from) { add(under_from, under_y); add(x, under_y); }
    if (over && x != over_from) { add(over_from, over_y); add(x, over_y); }
    m.advance = x;
    return m;
}

const StyleFonts& TextLayout::fonts(const std::string& style) {
    std::string key = upper(style);
    auto it = styles_.find(key);
    if (it != styles_.end()) return it->second;
    StyleFonts f = style_fonts(dwg_, style);
    f.code_page = code_page_;
    if (!f.missing.empty()) {
        warnings_.push_back("Text style \"" + style + "\": font \"" + f.missing + "\" not found; " +
                            (f.font ? "text measured with simplex.shx"
                                    : "nor simplex.shx, text counts by its insertion point"));
    }
    if (!f.missing_big_font.empty()) {
        warnings_.push_back("Text style \"" + style + "\": big font \"" + f.missing_big_font +
                            "\" not found; text measured without it");
    }
    return styles_[key] = std::move(f);
}

// TEXT as AutoCAD places it (Autodesk's TEXT and DXF references, 72/73): left from the
// pen's start, center and right by the pen's advance; baseline, bottom by the font's
// descent (even without descenders, as reported in Autodesk's forum), middle at half the
// height, top at the height; Middle (72 4) at the middle of the characters themselves,
// descenders too (MC is half the height); Aligned (3) and Fit (5) fill 10 to 11 by
// scaling the height and the width (Aligned) or the width (Fit). Backward (71 2) and
// upside down (4) mirror the text about its insertion point. Autodesk does not say
// whether center and right count the advance or the ink — an AutoCAD checkpoint.
TextCorners TextLayout::text(const TextLine& t) {
    TextCorners out;
    const StyleFonts& f = fonts(t.style);
    if (!f.font) return out;
    out.font = true;
    std::u32string chars = text_characters(t.content, code_page_);
    double h = t.height;
    double wf = t.width_factor != 0.0 ? t.width_factor : 1.0;
    double rotation = t.rotation;
    Point3 anchor = t.position;
    LineMeasure m = measure_line(f, chars, h, wf, t.oblique_angle);
    if (!m.ink.valid) return out;
    double dx = 0.0, dy = 0.0;
    if (t.h_justify == 3 || t.h_justify == 5) {
        double ux = t.alignment_point.x - t.position.x;
        double uy = t.alignment_point.y - t.position.y;
        double length = std::sqrt(ux * ux + uy * uy);
        if (length > 0.0 && m.advance > 0.0) {
            double scale = length / m.advance;
            if (t.h_justify == 3) h *= scale;
            else wf *= scale;
            rotation = std::atan2(uy, ux) * 180.0 / M_PI;
            m = measure_line(f, chars, h, wf, t.oblique_angle);
        }
    } else {
        if (t.h_justify != 0 || t.v_justify != 0) anchor = t.alignment_point;
        if (t.h_justify == 1 || t.h_justify == 4) dx = m.advance / 2.0;
        else if (t.h_justify == 2) dx = m.advance;
        double descent = f.font->descent() * h;
        if (t.h_justify == 4 && t.v_justify == 0) {
            dy = (m.ink.min_y + m.ink.max_y) / 2.0;
        } else {
            switch (t.v_justify) {
                case 1: dy = -descent; break;
                case 2: dy = h / 2.0; break;
                case 3: dy = h; break;
                default: break;
            }
        }
    }
    out.ink = true;
    double sx = (t.generation_flags & 2) ? -1.0 : 1.0;
    double sy = (t.generation_flags & 4) ? -1.0 : 1.0;
    double a = rotation * M_PI / 180.0;
    double c = std::cos(a), s = std::sin(a);
    const double xs[4] = {m.ink.min_x, m.ink.max_x, m.ink.max_x, m.ink.min_x};
    const double ys[4] = {m.ink.min_y, m.ink.min_y, m.ink.max_y, m.ink.max_y};
    for (int i = 0; i < 4; ++i) {
        double x = (xs[i] - dx) * sx, y = (ys[i] - dy) * sy;
        out.corners[i] = {anchor.x + c * x - s * y, anchor.y + s * x + c * y, anchor.z};
    }
    return out;
}

// Where TEXT's pen starts: as text() places the text — the justification's offset from its
// anchor, turned by the rotation (Aligned and Fit by 10 to 11), mirrored as the text is
TextPen TextLayout::text_pen(const TextLine& t) {
    TextPen out;
    const StyleFonts& f = fonts(t.style);
    if (!f.font) return out;
    out.font = true;
    std::u32string chars = text_characters(t.content, code_page_);
    double h = t.height;
    double wf = t.width_factor != 0.0 ? t.width_factor : 1.0;
    double rotation = t.rotation;
    Point3 anchor = t.position;
    LineMeasure m = measure_line(f, chars, h, wf, t.oblique_angle);
    double dx = 0.0, dy = 0.0;
    if (t.h_justify == 3 || t.h_justify == 5) {
        double ux = t.alignment_point.x - t.position.x;
        double uy = t.alignment_point.y - t.position.y;
        double length = std::sqrt(ux * ux + uy * uy);
        if (length > 0.0 && m.advance > 0.0) {
            double scale = length / m.advance;
            if (t.h_justify == 3) h *= scale;
            else wf *= scale;
            rotation = std::atan2(uy, ux) * 180.0 / M_PI;
        }
    } else {
        if (t.h_justify != 0 || t.v_justify != 0) anchor = t.alignment_point;
        if (t.h_justify == 1 || t.h_justify == 4) dx = m.advance / 2.0;
        else if (t.h_justify == 2) dx = m.advance;
        double descent = f.font->descent() * h;
        if (t.h_justify == 4 && t.v_justify == 0) {
            dy = m.ink.valid ? (m.ink.min_y + m.ink.max_y) / 2.0 : 0.0;
        } else {
            switch (t.v_justify) {
                case 1: dy = -descent; break;
                case 2: dy = h / 2.0; break;
                case 3: dy = h; break;
                default: break;
            }
        }
    }
    double sx = (t.generation_flags & 2) ? -1.0 : 1.0;
    double sy = (t.generation_flags & 4) ? -1.0 : 1.0;
    double a = rotation * M_PI / 180.0;
    double c = std::cos(a), s = std::sin(a);
    double x = -dx * sx, y = -dy * sy;
    out.pen = {anchor.x + c * x - s * y, anchor.y + s * x + c * y, anchor.z};
    out.height = h;
    out.width_factor = wf;
    out.rotation = rotation;
    return out;
}

// A shape from the shape files the drawing's styles name (STYLE 70 bit 1), by its name:
// its definition scaled by the size, the width by 41, slanted by 51, turned by 50
TextCorners TextLayout::shape(const ShapeData& d) {
    TextCorners out;
    std::shared_ptr<const Font> file;
    uint32_t number = 0;
    for (const auto& ts : dwg_.text_styles()) {
        if (!(ts.flags & 1)) continue;
        std::shared_ptr<const Font> f = find_font(ts.font_file);
        if (!f) {
            if (std::find(reported_.begin(), reported_.end(), upper(ts.font_file)) == reported_.end()) {
                reported_.push_back(upper(ts.font_file));
                warnings_.push_back("Shape file \"" + ts.font_file + "\" not found");
            }
            continue;
        }
        if (uint32_t n = f->shape_number(d.name)) { file = f; number = n; break; }
    }
    if (!file) return out;
    out.font = true;
    Glyph g = file->glyph(number);
    if (!g.has_ink) return out;
    out.ink = true;
    double wf = d.x_scale != 0.0 ? d.x_scale : 1.0;
    double shear = std::tan(d.oblique_angle * M_PI / 180.0);
    double a = d.rotation * M_PI / 180.0;
    double c = std::cos(a), s = std::sin(a);
    const double xs[4] = {g.min_x, g.max_x, g.max_x, g.min_x};
    const double ys[4] = {g.min_y, g.min_y, g.max_y, g.max_y};
    for (int i = 0; i < 4; ++i) {
        double y = ys[i] * d.size;
        double x = xs[i] * d.size * wf + y * shear;
        out.corners[i] = {d.insertion_point.x + c * x - s * y, d.insertion_point.y + s * x + c * y,
                          d.insertion_point.z};
    }
    return out;
}

} // namespace internal
} // namespace hdxf
