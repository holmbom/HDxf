#include "text_layout.hpp"
#include "dxf_writer.hpp"
#include "text_encoding.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// MTEXT as AutoCAD lays it out (rättningsfas R.8). Measured in AutoCAD's own files
// (ACadSharp's samples: 2018's MTEXT 42/43, and the R12 file where AutoCAD wrote each line
// as a TEXT):
// - a line is placed by its ink: at the left, the center or the right of its ink box
//   (spaces count as the pen moves over them); 42 is the widest line;
// - the first line's ink top is the top of the text; with "at least" spacing the next
//   baseline is 5/3 of the line's height times the factor below, less the height, plus
//   the next line's ink top (so 5/3 h for ordinary lines); with "exactly" 5/3 h times the
//   factor, and the text ends a fifth of that below the last baseline;
// - the last line's ink bottom is the bottom (at least); 43 is top to bottom.

namespace hdxf {
namespace internal {

namespace {

constexpr double kLinePitch = 5.0 / 3.0;    // baseline to baseline, in text heights
// The space between a stack's parts and its middle: none (AutoCAD's 4'-0\S1/4;" in
// ACadSharp's sample_AC1015 is two heights tall); the width of the diagonal bar, in the
// parts' height, is not in AutoCAD's files nor Autodesk's documentation (a checkpoint)
constexpr double kStackGap = 0.0;
// The diagonal fraction's bar: its angle (ODA File Converter's; not in AutoCAD's files)
constexpr double kDiagonalAngle = 55.0 * 3.14159265358979323846 / 180.0;
// Default tab stops, in text heights: reported ("4/(HT)") but not documented — an AutoCAD
// checkpoint
constexpr double kTabSpacing = 4.0;

// What a character is drawn with
struct RunStyle {
    const StyleFonts* fonts = nullptr;
    double height = 0.0;
    double width_factor = 1.0;
    double oblique = 0.0;                   // degrees
    double tracking = 1.0;
    bool underline = false;
    bool overline = false;
    int align = 0;                          // \A: 0 bottom, 1 center, 2 top
    bool has_color = false;                 // \C (ACI) or \c (RGB) set it
    Color color;
};

// What a paragraph is laid out with (\p): indents, alignment, tab stops
struct ParagraphStyle {
    double first_indent = 0.0;              // the first line, from the left indent
    double left_indent = 0.0;
    double right_indent = 0.0;
    int align = -1;                         // 0 left, 1 center, 2 right, 3 justified,
                                            // 4 distributed; -1 the attachment's
    std::vector<double> tabs;
};

enum class ItemKind { Glyph, Space, Tab, Stack };

struct Item {
    ItemKind kind = ItemKind::Glyph;
    char32_t c = 0;
    bool breakable = true;                  // a space where a line may break (\~ is not)
    RunStyle style;
    std::u32string top, bottom;             // a stack: its two parts
    char stack = 0;                         // '/', '#', '^'
};

struct Paragraph {
    std::vector<Item> items;
    ParagraphStyle style;
    bool column_break = false;              // \N after it
};

struct State {
    RunStyle run;
    ParagraphStyle paragraph;
};

// A number as written (a leading +, then as from_chars: never the locale's decimal comma);
// 0 for anything that is not a finite number
double number(const std::u32string& s) {
    std::string a;
    for (char32_t c : s) a += static_cast<char>(c < 0x80 ? c : '?');
    size_t from = !a.empty() && a[0] == '+' ? 1 : 0;
    double v = 0.0;
    auto r = std::from_chars(a.data() + from, a.data() + a.size(), v);
    return r.ec == std::errc() && std::isfinite(v) ? v : 0.0;
}

std::string ascii(const std::u32string& s) {
    std::string a;
    for (char32_t c : s) {
        if (c < 0x80) {
            a += static_cast<char>(c);
        } else {
            // UTF-8, for a font family's name
            if (c < 0x800) {
                a += static_cast<char>(0xC0 | (c >> 6));
            } else {
                if (c < 0x10000) {
                    a += static_cast<char>(0xE0 | (c >> 12));
                } else {
                    a += static_cast<char>(0xF0 | (c >> 18));
                    a += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
                }
                a += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            }
            a += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return a;
}

// The content's format codes (Autodesk, "Format Codes for Multiline Text") into
// paragraphs of items
class Parser {
public:
    Parser(TextLayout& layout, const StyleFonts& fonts, double height)
        : layout_(layout), initial_height_(height) {
        state_.run.fonts = &fonts;
        state_.run.height = height;
        state_.run.width_factor = fonts.width_factor != 0.0 ? fonts.width_factor : 1.0;
        state_.run.oblique = fonts.oblique_angle;
    }

    std::vector<Paragraph> parse(const std::u32string& s) {
        s_ = s;
        paragraphs_.assign(1, Paragraph{});
        size_t i = 0;
        while (i < s_.size()) {
            char32_t c = s_[i];
            if (c == U'\\' && i + 1 < s_.size()) {
                i = code(i + 1);
                continue;
            }
            if (c == U'{') { stack_.push_back(state_); ++i; continue; }
            if (c == U'}') {
                if (!stack_.empty()) { state_ = stack_.back(); stack_.pop_back(); }
                ++i;
                continue;
            }
            if (c == U'\n') { end_paragraph(false); ++i; continue; }
            if (c == U'%' && i + 2 < s_.size() && s_[i + 1] == U'%') {
                char32_t k = s_[i + 2];
                char32_t sym = 0;
                if (k == U'c' || k == U'C') sym = kDiameter;
                else if (k == U'd' || k == U'D') sym = kDegree;
                else if (k == U'p' || k == U'P') sym = kPlusMinus;
                else if (k == U'%') sym = U'%';
                if (sym != 0) { glyph(sym); i += 3; continue; }
            }
            if (c == U'\t') { add(ItemKind::Tab, 0); ++i; continue; }
            if (c == U' ') { add(ItemKind::Space, U' '); ++i; continue; }
            glyph(c);
            ++i;
        }
        paragraphs_.back().style = state_.paragraph;
        return paragraphs_;
    }

private:
    void add(ItemKind kind, char32_t c, bool breakable = true) {
        Item item;
        item.kind = kind;
        item.c = c;
        item.breakable = breakable;
        item.style = state_.run;
        paragraphs_.back().items.push_back(std::move(item));
    }
    void glyph(char32_t c) { add(ItemKind::Glyph, c); }

    void end_paragraph(bool column_break) {
        paragraphs_.back().style = state_.paragraph;
        paragraphs_.back().column_break = column_break;
        paragraphs_.push_back(Paragraph{});
    }

    // The value of a code up to its ';' (from i); the index after it
    size_t value(size_t i, std::u32string& out) {
        out.clear();
        while (i < s_.size() && s_[i] != U';') out.push_back(s_[i++]);
        return i < s_.size() ? i + 1 : i;
    }

    // A factor: "2" absolute, "2x" times the current value
    static double factor(const std::u32string& v, double current) {
        bool relative = !v.empty() && (v.back() == U'x' || v.back() == U'X');
        double n = number(relative ? v.substr(0, v.size() - 1) : v);
        return relative ? current * n : n;
    }

    // A code after its backslash at i; the index after it
    size_t code(size_t i) {
        char32_t c = s_[i];
        std::u32string v;
        switch (c) {
            case U'P': end_paragraph(false); return i + 1;
            case U'N': end_paragraph(true); return i + 1;
            case U'X': end_paragraph(false); return i + 1;
            case U'~': add(ItemKind::Space, U' ', false); return i + 1;
            case U'\\': case U'{': case U'}': glyph(c); return i + 1;
            case U'L': state_.run.underline = true; return i + 1;
            case U'l': state_.run.underline = false; return i + 1;
            case U'O': state_.run.overline = true; return i + 1;
            case U'o': state_.run.overline = false; return i + 1;
            case U'K': case U'k': return i + 1;         // strike-through: within the text
            case U'C': case U'c': {
                // \C an ACI color, \c a true color as a decimal 0xRRGGBB (the order is not
                // in Autodesk's documentation nor in AutoCAD's files — a checkpoint)
                size_t next = value(i + 1, v);
                int n = static_cast<int>(number(v));
                state_.run.has_color = true;
                state_.run.color = c == U'C' ? Color::from_aci(n)
                                             : Color::from_rgb((n >> 16) & 0xFF, (n >> 8) & 0xFF, n & 0xFF);
                return next;
            }
            case U'A': {
                size_t next = value(i + 1, v);
                double a = number(v);
                state_.run.align = a == 1.0 ? 1 : (a == 2.0 ? 2 : 0);
                return next;
            }
            case U'H': {
                size_t next = value(i + 1, v);
                double h = factor(v, state_.run.height);
                if (h > 0.0) state_.run.height = h;
                return next;
            }
            case U'W': {
                size_t next = value(i + 1, v);
                double w = factor(v, state_.run.width_factor);
                if (w > 0.0) state_.run.width_factor = w;
                return next;
            }
            case U'T': {
                size_t next = value(i + 1, v);
                double t = factor(v, state_.run.tracking);
                if (t > 0.0) state_.run.tracking = t;
                return next;
            }
            case U'Q': {
                size_t next = value(i + 1, v);
                state_.run.oblique = number(v);
                return next;
            }
            case U'f': case U'F': {
                size_t next = value(i + 1, v);
                state_.run.fonts = &layout_.font_override(*state_.run.fonts, ascii(v), c == U'f');
                return next;
            }
            case U'S': return stack(i + 1);
            case U'p': {
                size_t next = value(i + 1, v);
                paragraph_codes(v);
                return next;
            }
            case U'U':
                // \U+XXXX the reader left (an invalid one stays as written)
                if (i + 5 < s_.size() && s_[i + 1] == U'+') {
                    char32_t cp = 0;
                    bool hex = true;
                    for (size_t k = i + 2; k < i + 6; ++k) {
                        char32_t d = s_[k];
                        int digit = d >= U'0' && d <= U'9' ? static_cast<int>(d - U'0')
                                  : d >= U'A' && d <= U'F' ? static_cast<int>(d - U'A' + 10)
                                  : d >= U'a' && d <= U'f' ? static_cast<int>(d - U'a' + 10) : -1;
                        if (digit < 0) { hex = false; break; }
                        cp = cp * 16 + static_cast<char32_t>(digit);
                    }
                    if (hex) { glyph(cp); return i + 6; }
                }
                break;
            default:
                break;
        }
        // Not a code: the backslash is shown
        glyph(U'\\');
        return i;
    }

    // \S top ^ / # bottom ; — \; is a ; in it
    size_t stack(size_t i) {
        std::u32string top, bottom;
        char kind = 0;
        while (i < s_.size() && s_[i] != U';') {
            char32_t c = s_[i];
            if (c == U'\\' && i + 1 < s_.size()) { (kind ? bottom : top).push_back(s_[i + 1]); i += 2; continue; }
            if (!kind && (c == U'^' || c == U'/' || c == U'#')) { kind = static_cast<char>(c); ++i; continue; }
            (kind ? bottom : top).push_back(c);
            ++i;
        }
        Item item;
        item.kind = ItemKind::Stack;
        item.style = state_.run;
        item.top = top;
        item.bottom = bottom;
        item.stack = kind ? kind : '^';
        // "^ " is a space after ^: the bottom without it
        if (item.stack == '^' && !item.bottom.empty() && item.bottom[0] == U' ') item.bottom.erase(0, 1);
        paragraphs_.back().items.push_back(std::move(item));
        return i < s_.size() ? i + 1 : i;
    }

    // \p[x]i<first>,l<left>,r<right>,q<l|c|r|j|d>,t<stop>,<stop>…;
    void paragraph_codes(const std::u32string& v) {
        ParagraphStyle& p = state_.paragraph;
        size_t i = 0;
        // x: the values are times the MTEXT's height, from here on in the whole text (as
        // AutoCAD 2023 writes them, reported in Autodesk's forum); without, drawing units
        if (i < v.size() && v[i] == U'x') { scalable_ = true; ++i; }
        double unit = scalable_ ? initial_height_ : 1.0;
        bool in_tabs = false;
        while (i < v.size()) {
            size_t j = i;
            while (j < v.size() && v[j] != U',') ++j;
            std::u32string part = v.substr(i, j - i);
            i = j + 1;
            if (part.empty()) continue;
            char32_t key = part[0];
            std::u32string rest = part.substr(1);
            bool numeric = (key >= U'0' && key <= U'9') || key == U'-' || key == U'.';
            if (in_tabs && numeric) { p.tabs.push_back(number(part) * unit); continue; }
            in_tabs = false;
            switch (key) {
                case U'i': p.first_indent = number(rest) * unit; break;
                case U'l': p.left_indent = number(rest) * unit; break;
                case U'r': p.right_indent = number(rest) * unit; break;
                case U'q':
                    if (!rest.empty()) {
                        switch (rest[0]) {
                            case U'l': p.align = 0; break;
                            case U'c': p.align = 1; break;
                            case U'r': p.align = 2; break;
                            case U'j': p.align = 3; break;
                            case U'd': p.align = 4; break;
                            default: p.align = -1; break;
                        }
                    }
                    break;
                case U't':
                    p.tabs.clear();
                    if (!rest.empty()) p.tabs.push_back(number(rest) * unit);
                    in_tabs = true;
                    break;
                default: break;
            }
        }
    }

    TextLayout& layout_;
    double initial_height_;
    bool scalable_ = false;
    std::u32string s_;
    State state_;
    std::vector<State> stack_;
    std::vector<Paragraph> paragraphs_;
};

// A line as laid out: its items, and once measured its pen box (ink, and the spaces the
// pen moves over), its ink box and its height
struct Line {
    std::vector<Item> items;
    const ParagraphStyle* style = nullptr;
    bool first = false;                     // the first line of its paragraph
    bool last = false;                      // the last line of its paragraph
    bool column_break = false;
    TextBox ink;
    double left = 0.0, right = 0.0;         // the pen box
    bool has_extent = false;
    double nominal = 0.0;                   // its largest text height
    double baseline = 0.0;
    double x = 0.0;                         // where its pen starts
};

// What placing an item draws, in the line's coordinates (x from its pen's start, y from its
// baseline): a character with its pen there, or a stack's bar
struct Piece {
    bool bar = false;
    char32_t c = 0;
    double x = 0.0, y = 0.0;
    double x2 = 0.0, y2 = 0.0;
    double height = 0.0;
    const RunStyle* style = nullptr;
};
using PieceSink = std::function<void(const Piece&)>;

// Places an item at the pen (x), adding its ink and moving the pen; what it draws to sink
void place(const Item& item, double line_height, double& x, TextBox& ink, double& pen_left,
           double& pen_right, bool& pen_any, const PieceSink* sink = nullptr) {
    const RunStyle& r = item.style;
    const StyleFonts& f = *r.fonts;
    double shear = std::tan(r.oblique * M_PI / 180.0);
    auto glyph_at = [&](char32_t c, double gx, double gy, double h) {
        if (sink) {
            Piece piece;
            piece.c = c;
            piece.x = gx;
            piece.y = gy;
            piece.height = h;
            piece.style = &r;
            (*sink)(piece);
        }
        Glyph g = text_glyph(f, c);
        if (g.has_ink) {
            double x0 = gx + g.min_x * h * r.width_factor, x1 = gx + g.max_x * h * r.width_factor;
            double y0 = gy + g.min_y * h, y1 = gy + g.max_y * h;
            ink.add(x0 + (y0 - gy) * shear, y0);
            ink.add(x1 + (y0 - gy) * shear, y0);
            ink.add(x0 + (y1 - gy) * shear, y1);
            ink.add(x1 + (y1 - gy) * shear, y1);
        }
        return g.advance * h * r.width_factor;
    };
    auto pen = [&](double a, double b) {
        if (!pen_any) { pen_left = a; pen_right = b; pen_any = true; return; }
        pen_left = std::min(pen_left, a);
        pen_right = std::max(pen_right, b);
    };
    double start = x;
    switch (item.kind) {
        case ItemKind::Glyph: {
            double adv = glyph_at(item.c, x, 0.0, r.height);
            x += adv * r.tracking;
            break;
        }
        case ItemKind::Space: {
            Glyph g = text_glyph(f, U' ');
            double adv = g.advance * r.height * r.width_factor * r.tracking;
            if (sink) {
                Piece piece;
                piece.c = U' ';
                piece.x = x;
                piece.height = r.height;
                piece.style = &r;
                (*sink)(piece);
            }
            pen(x, x + adv);
            x += adv;
            break;
        }
        case ItemKind::Tab:
            break;                          // the line moves the pen to the tab stop
        case ItemKind::Stack: {
            // Each part at the height in effect (AutoCAD writes its 70% as \H0.7x before
            // \S). Autodesk's Text Formatting reference: / centers the parts with a bar
            // as long as the longer one, ^ aligns them left without a bar, # puts the top
            // left and the bottom right of a diagonal bar over both; \A 0 puts the stack's
            // bottom on the baseline, 1 its middle at the line's middle, 2 its top at the
            // line's top
            double h = r.height;
            double gap = kStackGap * h;
            auto draw = [&](const std::u32string& s, double sx, double sy) {
                for (char32_t c : s) sx += glyph_at(c, sx, sy, h) * r.tracking;
            };
            // A part's ink (from its pen and baseline) and its advance
            auto part = [&](const std::u32string& s, TextBox& box) {
                double px = 0.0;
                for (char32_t c : s) {
                    Glyph g = text_glyph(f, c);
                    if (g.has_ink) {
                        box.add(px + g.min_x * h * r.width_factor, g.min_y * h);
                        box.add(px + g.max_x * h * r.width_factor, g.max_y * h);
                    }
                    px += g.advance * h * r.width_factor * r.tracking;
                }
                return px;
            };
            TextBox tb, bb;
            double at = part(item.top, tb), ab = part(item.bottom, bb);
            if (item.stack == '#') {
                // Diagonal: the top on the line's top half, a bar twice the height through
                // its baseline an h/6 past it, the bottom on the baseline an h/6 past the bar
                // (ODA File Converter's; its height is AutoCAD's sample_AC1015's)
                double wt = tb.valid ? tb.max_x - tb.min_x : at;
                double half = h + gap;
                double mid = r.align == 1 ? line_height / 2.0 : (r.align == 2 ? line_height - half : half);
                double top_y = mid + gap, bottom_y = mid - gap - h;
                double top_x = x - (tb.valid ? tb.min_x : 0.0);
                draw(item.top, top_x, top_y);
                double bar_x = x + wt + h / 6.0;
                double dx = std::cos(kDiagonalAngle) * h, dy = std::sin(kDiagonalAngle) * h;
                ink.add(bar_x - dx, top_y - dy);
                ink.add(bar_x + dx, top_y + dy);
                if (sink) {
                    Piece piece;
                    piece.bar = true;
                    piece.x = bar_x - dx;
                    piece.y = top_y - dy;
                    piece.x2 = bar_x + dx;
                    piece.y2 = top_y + dy;
                    piece.style = &r;
                    (*sink)(piece);
                }
                double bottom_x = bar_x + h / 6.0 - (bb.valid ? bb.min_x : 0.0);
                draw(item.bottom, bottom_x, bottom_y);
                x = bottom_x + ab;
            } else {
                // Horizontal fraction (/) and tolerance (^), ODA File Converter's exploded
                // dimension text: the top's baseline an h/6 above the middle (\A1; the
                // bottom on the baseline at \A0, the top at the line's top at \A2), the
                // bottom's ink an h/3 below the top's, the bar halfway between; the parts
                // centered (/) or to the left (^) by their ink, the stack as wide as the wider
                double tmin = tb.valid ? tb.min_y : 0.0, tmax = tb.valid ? tb.max_y : h;
                double bmin = bb.valid ? bb.min_y : 0.0, bmax = bb.valid ? bb.max_y : h;
                double top_y = r.align == 1 ? line_height / 2.0 + h / 6.0
                             : r.align == 2 ? line_height - tmax : 0.0;
                double bottom_y = top_y + tmin - h / 3.0 - bmax;
                if (r.align != 1 && r.align != 2) {
                    // Bottom: the bottom's ink on the baseline
                    double shift = -(bottom_y + bmin);
                    top_y += shift;
                    bottom_y += shift;
                }
                double wt = tb.valid ? tb.max_x - tb.min_x : at, wb = bb.valid ? bb.max_x - bb.min_x : ab;
                double w = std::max(wt, wb);
                bool fraction = item.stack == '/';
                double top_x = x + (fraction ? (w - wt) / 2.0 : 0.0) - (tb.valid ? tb.min_x : 0.0);
                double bottom_x = x + (fraction ? (w - wb) / 2.0 : 0.0) - (bb.valid ? bb.min_x : 0.0);
                draw(item.top, top_x, top_y);
                // The bar between the parts, as AutoCAD draws it (its proxy graphics: after
                // the top)
                double bar_y = (top_y + tmin + bottom_y + bmax) / 2.0;
                if (fraction && sink) {
                    Piece piece;
                    piece.bar = true;
                    piece.x = x;
                    piece.y = bar_y;
                    piece.x2 = x + w;
                    piece.y2 = bar_y;
                    piece.style = &r;
                    (*sink)(piece);
                }
                draw(item.bottom, bottom_x, bottom_y);
                if (fraction) {
                    ink.add(x, bar_y);
                    ink.add(x + w, bar_y);
                }
                // The pen goes on from the end of the wider part
                x = wt >= wb ? top_x + at : bottom_x + ab;
            }
            break;
        }
    }
    // The underline and the overline, ink too
    if ((r.underline || r.overline) && x != start) {
        double under = -f.font->underline() * r.height;
        double over = r.height - under;
        if (r.underline) { ink.add(start + under * shear, under); ink.add(x + under * shear, under); }
        if (r.overline) { ink.add(start + over * shear, over); ink.add(x + over * shear, over); }
    }
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

// A character as proxy graphics draw it: the % symbols as the characters the font draws
// (AutoCAD's MULTILEADERs: %%c in Arial as Ø, %%d as °)
void append_drawn(std::string& out, const StyleFonts& fonts, char32_t c) {
    if (c == kDegree) c = U'°';
    else if (c == kPlusMinus) c = U'±';
    else if (c == kDiameter || c == U'∅') {
        bool truetype = fonts.font && !fonts.font->is_shape_font();
        c = truetype && fonts.font->glyph(U'Ø').found ? U'Ø' : U'∅';
    }
    append_utf8(out, c);
}

// A character as TEXT has it: the % symbols as their codes
void append_code(std::string& out, char32_t c) {
    if (c == kDegree || c == U'°') out += "%%d";
    else if (c == kPlusMinus || c == U'±') out += "%%p";
    else if (c == kDiameter || c == U'∅') out += "%%c";
    else append_utf8(out, c);
}

// A line's text as TEXT has it: the % symbols as their codes, a stack as " top#bottom"
// (AutoCAD's R12 file of ACadSharp's sample: 4'-0\S1/4;" became 4'-0 1#4")
std::string line_text(const std::vector<Item>& items) {
    std::string out;
    for (const auto& item : items) {
        switch (item.kind) {
            case ItemKind::Space: case ItemKind::Tab: out += ' '; break;
            case ItemKind::Stack:
                out += ' ';
                for (char32_t c : item.top) append_utf8(out, c);
                out += '#';
                for (char32_t c : item.bottom) append_utf8(out, c);
                break;
            case ItemKind::Glyph:
                if (item.c == kDegree || item.c == U'°') out += "%%d";
                else if (item.c == kPlusMinus || item.c == U'±') out += "%%p";
                else if (item.c == kDiameter || item.c == U'∅') out += "%%c";
                else append_utf8(out, item.c);
                break;
        }
    }
    return out;
}

} // namespace

const StyleFonts& TextLayout::font_override(const StyleFonts& base, const std::string& spec, bool by_family) {
    std::string key = std::string(by_family ? "f" : "F") + spec + "|" +
                      std::to_string(reinterpret_cast<std::uintptr_t>(&base));
    auto it = overrides_.find(key);
    if (it != overrides_.end()) return it->second;
    StyleFonts f = base;
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t i = 0; i <= spec.size(); ++i) {
        if (i == spec.size() || spec[i] == '|') { parts.push_back(spec.substr(start, i - start)); start = i + 1; }
    }
    std::shared_ptr<const Font> font;
    std::string name = parts.empty() ? std::string() : parts[0];
    if (by_family) {
        bool bold = false, italic = false;
        int pitch = 0;
        for (size_t i = 1; i < parts.size(); ++i) {
            if (parts[i].size() >= 2 && parts[i][0] == 'b') bold = parts[i][1] == '1';
            if (parts[i].size() >= 2 && parts[i][0] == 'i') italic = parts[i][1] == '1';
            if (parts[i].size() >= 2 && parts[i][0] == 'p') pitch = std::atoi(parts[i].c_str() + 1);
        }
        if (!name.empty()) font = find_font_family(name, bold, italic);
        f.family = name;
        f.bold = bold;
        f.italic = italic;
        f.pitch_and_family = pitch;
        f.file.clear();
        f.big_font_file.clear();
    } else {
        // \F file[,bigfont]
        std::string big;
        if (size_t comma = name.find(','); comma != std::string::npos) {
            big = name.substr(comma + 1);
            name = name.substr(0, comma);
        }
        if (!name.empty()) font = find_font(name);
        if (!big.empty()) f.big_font = find_font(big);
        f.family.clear();
        f.bold = f.italic = false;
        f.pitch_and_family = 0;
        f.file = name;
        f.big_font_file = big;
    }
    if (font) {
        f.font = font;
    } else if (!name.empty()) {
        warnings_.push_back("MTEXT font \"" + name + "\" not found; measured with the style's font");
    }
    return overrides_[key] = std::move(f);
}

std::vector<std::string> TextLayout::mtext_paragraphs(const std::string& content, const std::string& style) {
    const StyleFonts& base = fonts(style);
    std::u32string chars;
    for (size_t i = 0; i < content.size();) {
        unsigned char b = static_cast<unsigned char>(content[i]);
        char32_t cp = b;
        size_t n = 1;
        if (b >= 0xF0) { cp = b & 0x07; n = 4; }
        else if (b >= 0xE0) { cp = b & 0x0F; n = 3; }
        else if (b >= 0xC0) { cp = b & 0x1F; n = 2; }
        for (size_t k = 1; k < n && i + k < content.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(content[i + k]) & 0x3F);
        chars.push_back(cp);
        i += n;
    }
    Parser parser(*this, base, 1.0);
    std::vector<std::string> out;
    for (const auto& p : parser.parse(chars)) out.push_back(line_text(p.items));
    return out;
}

namespace {

// A line's runs (proxy graphics): its items placed again from its pen's start (x0, y0), the
// characters of one format on one baseline, one after the other, into one run; a stack's
// parts and bars runs of their own, a tab ends a run
void line_runs(const Line& line, double x0, double y0, const std::function<double(double, double)>& tab_stop,
               std::vector<MTextRun>& out) {
    bool open = false;
    double next_x = 0.0, run_y = 0.0;
    const RunStyle* run_style = nullptr;
    auto same = [](const RunStyle& a, const RunStyle& b) {
        return a.fonts == b.fonts && a.height == b.height && a.width_factor == b.width_factor &&
               a.oblique == b.oblique && a.tracking == b.tracking && a.underline == b.underline &&
               a.overline == b.overline && a.has_color == b.has_color &&
               (!a.has_color || (a.color.aci == b.color.aci && a.color.rgb == b.color.rgb));
    };
    PieceSink sink = [&](const Piece& p) {
        if (p.bar) {
            MTextRun bar;
            bar.bar = true;
            bar.x = x0 + p.x;
            bar.y = y0 + p.y;
            bar.x2 = x0 + p.x2;
            bar.y2 = y0 + p.y2;
            bar.fonts = p.style->fonts;
            bar.has_color = p.style->has_color;
            bar.color = p.style->color;
            out.push_back(bar);
            open = false;
            return;
        }
        bool continues = open && run_style && same(*run_style, *p.style) && p.y == run_y &&
                         std::fabs(p.x - next_x) <= 1e-9 * std::max(1.0, std::fabs(p.x));
        if (!continues) {
            MTextRun run;
            run.x = x0 + p.x;
            run.y = y0 + p.y;
            run.height = p.height;
            run.width_factor = p.style->width_factor;
            run.oblique = p.style->oblique;
            run.tracking = p.style->tracking;
            run.fonts = p.style->fonts;
            run.underline = p.style->underline;
            run.overline = p.style->overline;
            run.has_color = p.style->has_color;
            run.color = p.style->color;
            out.push_back(run);
            open = true;
            run_style = p.style;
            run_y = p.y;
        }
        append_drawn(out.back().text, *p.style->fonts, p.c);
        append_code(out.back().code_text, p.c);
        Glyph g = text_glyph(*p.style->fonts, p.c);
        next_x = p.x + g.advance * p.height * p.style->width_factor * p.style->tracking;
        out.back().advance = x0 + next_x - out.back().x;
    };
    double x = 0.0, pl = 0.0, pr = 0.0;
    bool any = false;
    TextBox ink;
    for (const auto& item : line.items) {
        if (item.kind == ItemKind::Tab) {
            open = false;
            x = tab_stop(x, item.style.height);
            continue;
        }
        place(item, line.nominal, x, ink, pl, pr, any, &sink);
    }
}

} // namespace

MTextSize TextLayout::mtext(const MTextData& d, bool runs) {
    MTextSize out;
    const StyleFonts& base = fonts(d.style);
    if (!base.font) return out;
    out.font = true;
    double h = d.height;
    double factor = d.line_spacing_factor > 0.0 ? d.line_spacing_factor : 1.0;
    bool exactly = d.line_spacing_style == 2;

    std::u32string chars;
    {
        // UTF-8 to code points
        const std::string& s = d.content;
        for (size_t i = 0; i < s.size();) {
            unsigned char b = static_cast<unsigned char>(s[i]);
            char32_t cp = b;
            size_t n = 1;
            if (b >= 0xF0) { cp = b & 0x07; n = 4; }
            else if (b >= 0xE0) { cp = b & 0x0F; n = 3; }
            else if (b >= 0xC0) { cp = b & 0x1F; n = 2; }
            for (size_t k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
            chars.push_back(cp);
            i += n;
        }
    }
    Parser parser(*this, base, h);
    std::vector<Paragraph> paragraphs = parser.parse(chars);

    // Columns (B73): type (0 none, 1 static, 2 dynamic), flow reversed, width, gutter, heights
    int column_type = 0;
    bool reversed = false;
    double column_width = d.reference_width, gutter = 0.0;
    std::vector<double> column_heights;
    if (d.columns) {
        const MTextColumns& c = *d.columns;
        column_type = c.type;
        reversed = c.reversed;
        column_width = c.width;
        gutter = c.gutter;
        column_heights = c.heights;
    }
    double wrap_width = column_type != 0 && column_width > 0.0 ? column_width : d.reference_width;

    int column = (d.attachment - 1) % 3;    // 0 left, 1 center, 2 right
    int row = (d.attachment - 1) / 3;       // 0 top, 1 middle, 2 bottom
    if (d.attachment < 1 || d.attachment > 9) { column = 0; row = 0; }

    // Lines: each paragraph broken where its next word would pass the width
    std::vector<Line> lines;
    // The next tab stop after the pen: the paragraph's, then the default ones
    auto tab_stop = [&](const ParagraphStyle& ps, double x, double height) {
        for (double t : ps.tabs) {
            if (t > x + 1e-9) return t;
        }
        double step = kTabSpacing * height;
        return step > 0.0 ? (std::floor(x / step + 1e-9) + 1.0) * step : x;
    };
    // The pen after an item (not a tab)
    auto advance = [&](const Item& item, double x) {
        double pl = 0, pr = 0;
        bool any = false;
        TextBox ink;
        place(item, item.style.height, x, ink, pl, pr, any);
        return x;
    };
    for (const auto& p : paragraphs) {
        double limit = wrap_width > 0.0 ? wrap_width - p.style.left_indent - p.style.right_indent : 0.0;
        Line line;
        line.style = &p.style;
        line.first = true;
        double line_x = 0.0;                // the pen after the line's items
        std::vector<Item> pending_spaces;
        std::vector<Item> word;
        auto line_limit = [&]() { return limit - (line.first ? p.style.first_indent : 0.0); };
        auto flush_word = [&]() {
            if (word.empty()) return;
            double end = line_x;
            for (const auto& item : pending_spaces) end = advance(item, end);
            for (const auto& item : word) end = advance(item, end);
            if (limit > 0.0 && !line.items.empty() && end > line_limit() + 1e-9) {
                lines.push_back(line);
                line = Line{};
                line.style = &p.style;
                pending_spaces.clear();         // the spaces at the break are dropped
                end = 0.0;
                for (const auto& item : word) end = advance(item, end);
            }
            line.items.insert(line.items.end(), pending_spaces.begin(), pending_spaces.end());
            line.items.insert(line.items.end(), word.begin(), word.end());
            line_x = end;
            pending_spaces.clear();
            word.clear();
        };
        for (const auto& item : p.items) {
            if (item.kind == ItemKind::Space && item.breakable) {
                flush_word();
                pending_spaces.push_back(item);
            } else if (item.kind == ItemKind::Tab) {
                flush_word();
                for (const auto& space : pending_spaces) line_x = advance(space, line_x);
                line.items.insert(line.items.end(), pending_spaces.begin(), pending_spaces.end());
                pending_spaces.clear();
                line.items.push_back(item);
                line_x = tab_stop(p.style, line_x, item.style.height);
            } else {
                word.push_back(item);
            }
        }
        flush_word();
        // The spaces at the paragraph's end are dropped: neither drawn nor counted (AutoCAD's
        // MULTILEADERs "Spalt für \P…" in the files of test/data/README.md, right-aligned by
        // the ink of "Spalt für")
        line.last = true;
        line.column_break = p.column_break;
        lines.push_back(line);
    }

    // Each line measured: tabs to their stops, the pen box, the ink, the largest height
    for (auto& line : lines) {
        double x = 0.0, pl = 0.0, pr = 0.0;
        bool any = false;
        line.nominal = 0.0;
        for (const auto& item : line.items) line.nominal = std::max(line.nominal, item.style.height);
        for (const auto& item : line.items) {
            if (item.kind == ItemKind::Tab) {
                double stop = tab_stop(*line.style, x, item.style.height);
                if (!any) { pl = x; pr = stop; any = true; }
                pr = std::max(pr, stop);
                x = stop;
                continue;
            }
            place(item, line.nominal, x, line.ink, pl, pr, any);
        }
        if (line.nominal == 0.0) line.nominal = h;
        line.has_extent = line.ink.valid || any;
        // A line starts no further left than its pen (ODA File Converter: "_" with its ink
        // left of the pen)
        line.left = line.ink.valid ? std::max(line.ink.min_x, 0.0) : pl;
        line.right = line.ink.valid ? line.ink.max_x : pr;
        if (any) { line.left = std::min(line.left, pl); line.right = std::max(line.right, pr); }
    }

    // Lines into columns, top to bottom: the baselines, and the height of each column
    // A line reaches at least its height above the baseline and down to it (ODA File
    // Converter's dimension text "5": its top is the height, not the 5's ink)
    auto top_of = [&](const Line& l) { return l.ink.valid ? std::max(l.ink.max_y, l.nominal) : l.nominal; };
    auto bottom_of = [&](const Line& l) { return l.ink.valid ? std::min(l.ink.min_y, 0.0) : 0.0; };
    std::vector<std::vector<Line*>> columns(1);
    std::vector<double> column_bottoms(1, 0.0);
    double y = 0.0;
    bool column_start = true;
    for (size_t i = 0; i < lines.size(); ++i) {
        Line& l = lines[i];
        double baseline;
        if (column_start) {
            baseline = -top_of(l);
        } else if (exactly) {
            baseline = y - factor * kLinePitch * h;
        } else {
            baseline = y - (factor * kLinePitch * l.nominal - l.nominal + top_of(l));
        }
        double bottom = exactly ? baseline - factor * kLinePitch * h / 5.0 : baseline + bottom_of(l);
        // A column of a set height is full when the line would end below it
        size_t c = columns.size() - 1;
        double limit = c < column_heights.size() ? column_heights[c] : 0.0;
        // Static columns have the one defined height (no 50 heights)
        if (column_type == 1 && limit == 0.0) limit = d.defined_height;
        if (column_type != 0 && !column_start && limit > 0.0 && -bottom > limit + 1e-9) {
            columns.emplace_back();
            column_bottoms.push_back(0.0);
            column_start = true;
            --i;
            continue;
        }
        l.baseline = baseline;
        columns.back().push_back(&l);
        column_bottoms.back() = bottom;
        y = baseline;
        column_start = false;
        if (l.column_break && column_type != 0 && i + 1 < lines.size()) {
            columns.emplace_back();
            column_bottoms.push_back(0.0);
            column_start = true;
        }
    }
    double height = 0.0;
    for (double b : column_bottoms) height = std::max(height, -b);
    double width = 0.0;
    for (const auto& l : lines) {
        if (l.has_extent) width = std::max(width, l.right - l.left);
    }
    out.width = width;
    out.height = height;

    // Placed: the box (the reference width, or the widest line) by the attachment, each
    // line in it by its paragraph's alignment
    double box_width = wrap_width > 0.0 ? wrap_width : width;
    double box_left = column == 0 ? 0.0 : (column == 1 ? -box_width / 2.0 : -box_width);
    double dy = row == 0 ? 0.0 : (row == 1 ? height / 2.0 : height);
    for (size_t c = 0; c < columns.size(); ++c) {
        double offset = static_cast<double>(c) * (column_width + gutter) * (reversed ? -1.0 : 1.0);
        for (Line* l : columns[c]) {
            if (!l->has_extent) continue;
            const ParagraphStyle& ps = *l->style;
            int align = ps.align >= 0 ? ps.align : column;
            double from = box_left + offset + ps.left_indent + (l->first ? ps.first_indent : 0.0);
            double to = box_left + offset + box_width - ps.right_indent;
            double left, right;
            double w = l->right - l->left;
            bool stretch = (align == 3 && !l->last) || align == 4;
            if (stretch && box_width > 0.0) {
                left = from;
                right = std::max(to, from + w);
            } else if (align == 1) {
                left = (from + to) / 2.0 - w / 2.0;
                right = left + w;
            } else if (align == 2) {
                right = to;
                left = right - w;
            } else {
                left = from;
                right = left + w;
            }
            double top = l->baseline + top_of(*l) + dy;
            double bottom = l->baseline + bottom_of(*l) + dy;
            out.lines.add(left, bottom);
            out.lines.add(right, top);
            // As TEXT: the pen starts its ink's left short of the line's left
            MTextLine tl;
            tl.x = left - l->left;
            tl.baseline = l->baseline + dy;
            tl.height = l->nominal;
            tl.text = line_text(l->items);
            if (runs) {
                line_runs(*l, tl.x, tl.baseline,
                          [&](double at, double height) { return tab_stop(*l->style, at, height); }, out.runs);
            }
            out.text_lines.push_back(std::move(tl));
        }
    }
    out.ink = out.lines.valid;
    // The extents: the lines. Not the width the text is set in (R.8 took it from a report in Autodesk's forum): AutoCAD 2025's
    // $EXTMIN/$EXTMAX of an MTEXT "A" set in a width of 100 end at the "A" (x 0.187), and an annotative one at 1:5 at its context's
    // 42 × 43, not its width there (doc/facit/fas9_mtext_width.scr, 9.S Codex 17)
    out.box = out.lines;
    // The background fill (90 bit 1) and the text frame (bit 16) reach beyond the text by the
    // border offset factor (45) less one, times the height (Autodesk: 1.0 fits the text, 1.5 adds
    // half the height; AutoCAD's frame LWPOLYLINEs: 0.5 for 1.5, ezdxf's mtext_framed_columns) —
    // drawn, so in the extents
    if (out.ink && (d.background.flags & 17) && d.background.scale > 1.0) {
        double margin = (d.background.scale - 1.0) * h;
        out.box.add(out.box.min_x - margin, out.box.min_y - margin);
        out.box.add(out.box.max_x + margin, out.box.max_y + margin);
    }
    return out;
}

// TOLERANCE (AcDbFcf): rows of boxes, a line (\n, ^J in the file) per row and %%v
// between the boxes (the first of several counts). A row is DIMTXT + 2 DIMGAP high, a box
// its text's ink and DIMGAP on each side, times DIMSCALE; the insertion point is the
// middle of the first row's left side. The dimension style's values, with the entity's
// own (XDATA ACAD DSTYLE). Autodesk documents what the variables control, not the
// numbers: these are ODA File Converter's frames for ACadSharp's TOLERANCE (its R12
// output), which match the fonts' ink exactly.
MTextSize TextLayout::tolerance(const ToleranceData& d, const std::vector<XdataList>& xdata) {
    MTextSize out;
    DimStyle style;
    for (const auto& ds : dwg_.dim_styles()) {
        if (names_equal(ds.name, d.dim_style)) { style = ds; break; }
    }
    double dimtxt = style.dimtxt, dimgap = style.dimgap, dimscale = style.dimscale;
    std::string text_style = style.dimtxsty.empty() ? std::string("Standard") : style.dimtxsty;
    for (const auto& list : xdata) {
        if (!names_equal(list.app_name, "ACAD")) continue;
        bool in = false;
        for (size_t i = 0; i < list.items.size(); ++i) {
            const XdataItem& it = list.items[i];
            if (it.code == 1000 && it.value == "DSTYLE") { in = true; continue; }
            if (!in) continue;
            if (it.code == 1002 && it.value == "}") break;
            if (it.code != 1070 || i + 1 >= list.items.size()) continue;
            int key = std::atoi(it.value.c_str());
            const XdataItem& v = list.items[++i];
            if (key == 140) dimtxt = std::atof(v.value.c_str());
            else if (key == 147) dimgap = std::atof(v.value.c_str());
            else if (key == 40) dimscale = std::atof(v.value.c_str());
        }
    }
    if (dimscale <= 0.0) dimscale = 1.0;
    double h = dimtxt * dimscale, gap = std::fabs(dimgap) * dimscale;
    double row = h + 2.0 * gap;
    const StyleFonts& f = fonts(text_style);
    if (!f.font) return out;
    out.font = true;

    std::vector<std::string> rows(1);
    for (char c : d.content) {
        if (c == '\n') rows.emplace_back();
        else rows.back() += c;
    }
    for (size_t r = 0; r < rows.size(); ++r) {
        // The boxes: the text between the first %%v of each run
        std::vector<std::string> cells(1);
        const std::string& s = rows[r];
        for (size_t i = 0; i < s.size();) {
            if (s.compare(i, 3, "%%v") == 0 || s.compare(i, 3, "%%V") == 0) {
                if (!cells.back().empty()) cells.emplace_back();
                i += 3;
                continue;
            }
            cells.back() += s[i++];
        }
        double x = 0.0;
        for (const auto& cell : cells) {
            if (cell.empty()) continue;
            MTextData m;
            m.content = cell;
            m.height = h;
            m.style = text_style;
            MTextSize size = mtext(m, {});
            if (!size.ink) continue;
            x += size.width + 2.0 * gap;
        }
        if (x == 0.0) continue;
        double top = row / 2.0 - static_cast<double>(r) * row;
        out.box.add(0.0, top - row);
        out.box.add(x, top);
        out.width = std::max(out.width, x);
    }
    out.ink = out.box.valid;
    out.height = out.ink ? out.box.max_y - out.box.min_y : 0.0;
    return out;
}

} // namespace internal
} // namespace hdxf
