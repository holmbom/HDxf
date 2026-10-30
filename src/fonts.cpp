#include "fonts.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Reading fonts for the size of text (rättningsfas R.8). Written from the TrueType/OpenType
// specification (Apple, Microsoft) and AutoCAD's documentation of shape fonts
// ("Shape Descriptions", "Big Font", "Unicode font" in the AutoCAD Customization Guide).

namespace hdxf {

namespace {

std::mutex& paths_mutex() {
    static std::mutex m;
    return m;
}

std::vector<std::filesystem::path>& paths_storage() {
    static std::vector<std::filesystem::path> paths = [] {
        std::vector<std::filesystem::path> p;
#ifdef _WIN32
        if (const wchar_t* windir = _wgetenv(L"WINDIR")) p.push_back(std::filesystem::path(windir) / "Fonts");
#endif
        return p;
    }();
    return paths;
}

// Counts the changes of the paths: the fonts found before are forgotten
unsigned& paths_generation() {
    static unsigned generation = 0;
    return generation;
}

unsigned current_generation() {
    std::lock_guard<std::mutex> lock(paths_mutex());
    return paths_generation();
}

} // namespace

void set_font_paths(std::vector<std::filesystem::path> paths) {
    std::lock_guard<std::mutex> lock(paths_mutex());
    paths_storage() = std::move(paths);
    ++paths_generation();
}

std::vector<std::filesystem::path> font_paths() {
    std::lock_guard<std::mutex> lock(paths_mutex());
    return paths_storage();
}

namespace internal {

namespace {

void append_utf8(std::string& out, char32_t c);

using Bytes = std::vector<uint8_t>;

bool read_all(const std::filesystem::path& path, Bytes& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

uint16_t be16(const Bytes& b, size_t at) {
    return at + 2 <= b.size() ? static_cast<uint16_t>(b[at] << 8 | b[at + 1]) : 0;
}
int16_t be16s(const Bytes& b, size_t at) { return static_cast<int16_t>(be16(b, at)); }
uint32_t be32(const Bytes& b, size_t at) {
    return at + 4 <= b.size() ? static_cast<uint32_t>(b[at]) << 24 | static_cast<uint32_t>(b[at + 1]) << 16 |
                                    static_cast<uint32_t>(b[at + 2]) << 8 | b[at + 3]
                              : 0;
}
uint16_t le16(const Bytes& b, size_t at) {
    return at + 2 <= b.size() ? static_cast<uint16_t>(b[at] | b[at + 1] << 8) : 0;
}
uint32_t le32(const Bytes& b, size_t at) {
    return at + 4 <= b.size() ? static_cast<uint32_t>(b[at]) | static_cast<uint32_t>(b[at + 1]) << 8 |
                                    static_cast<uint32_t>(b[at + 2]) << 16 | static_cast<uint32_t>(b[at + 3]) << 24
                              : 0;
}

// ---------------------------------------------------------------------------
// TrueType / OpenType
// ---------------------------------------------------------------------------

// A glyph's box in font units
struct UnitBox {
    bool ink = false;
    double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    void add(double x, double y) {
        if (!ink) { min_x = max_x = x; min_y = max_y = y; ink = true; return; }
        min_x = std::min(min_x, x); max_x = std::max(max_x, x);
        min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
};

// A CFF INDEX (Compact Font Format, Adobe TN 5176 §5): where each object starts and ends
struct CffIndex {
    std::vector<std::pair<size_t, size_t>> objects;
    size_t end = 0;                     // the first byte after the INDEX
};

class TrueTypeFont : public Font {
public:
    bool load(Bytes data, int face);
    Glyph glyph(char32_t c) const override;
    bool is_shape_font() const override { return false; }
    double descent() const override { return descent_; }
    double ascent() const override { return ascent_; }
    double underline() const override { return underline_; }
    bool unicode() const override { return true; }
    FontFace face() const override { return face_; }

private:
    void read_face();
    size_t table(const char* tag) const;
    size_t table_size(const char* tag) const;
    uint16_t glyph_index(char32_t c) const;
    bool glyph_box(uint16_t index, UnitBox& box) const;
    bool load_cff();
    bool cff_box(uint16_t index, UnitBox& box) const;
    CffIndex cff_index(size_t at) const;
    // A CFF DICT: its operators (12 x as 1200 + x) and their operands
    std::unordered_map<int, std::vector<double>> cff_dict(size_t at, size_t end) const;

    Bytes data_;
    size_t base_ = 0;                   // the font in a collection
    std::unordered_map<std::string, std::pair<size_t, size_t>> tables_;
    double units_per_em_ = 2048.0;
    double height_em_ = 0.716;
    double descent_ = 0.3;
    double ascent_ = 0.9;
    double underline_ = 0.15;
    FontFace face_;
    int loca_format_ = 0;
    uint16_t glyphs_ = 0;
    uint16_t h_metrics_ = 0;
    size_t cmap4_ = 0, cmap12_ = 0;
    // CFF outlines (OpenType "OTTO"): the charstrings, the global and local subroutines
    // (one set per font DICT in a CID font) and which font DICT each glyph uses
    bool cff_ = false;
    CffIndex charstrings_;
    CffIndex global_subrs_;
    std::vector<CffIndex> local_subrs_;
    std::vector<uint8_t> fd_select_;
};

bool TrueTypeFont::load(Bytes data, int face) {
    data_ = std::move(data);
    if (data_.size() < 12) return false;
    uint32_t tag = be32(data_, 0);
    if (tag == 0x74746366) {                                // "ttcf": a collection
        if (face < 0 || static_cast<uint32_t>(face) >= be32(data_, 8)) return false;
        base_ = be32(data_, 12 + 4 * static_cast<size_t>(face));
    }
    uint32_t version = be32(data_, base_);
    if (version != 0x00010000 && version != 0x74727565 && version != 0x4F54544F) return false;
    cff_ = version == 0x4F54544F;                           // "OTTO"
    uint16_t count = be16(data_, base_ + 4);
    for (uint16_t i = 0; i < count; ++i) {
        size_t rec = base_ + 12 + 16 * static_cast<size_t>(i);
        if (rec + 16 > data_.size()) return false;
        std::string name(reinterpret_cast<const char*>(&data_[rec]), 4);
        tables_[name] = {be32(data_, rec + 8), be32(data_, rec + 12)};
    }
    size_t head = table("head"), hhea = table("hhea"), maxp = table("maxp"), cmap = table("cmap");
    if (!head || !hhea || !maxp || !cmap || !table("hmtx")) return false;
    units_per_em_ = be16(data_, head + 18);
    if (units_per_em_ <= 0) units_per_em_ = 2048.0;
    loca_format_ = be16s(data_, head + 50);
    glyphs_ = be16(data_, maxp + 4);
    h_metrics_ = be16(data_, hhea + 34);
    if (cff_ && !load_cff()) return false;
    // The cap height: OS/2 sCapHeight (version 2+) when the font has no "A"
    double cap = 0.0;
    size_t os2 = table("OS/2");
    if (os2 && be16(data_, os2) >= 2) cap = be16s(data_, os2 + 88);
    // The descent GDI reports: usWinDescent; without OS/2, hhea's descender
    double descent = os2 ? be16(data_, os2 + 76) : -static_cast<double>(be16s(data_, hhea + 6));
    // cmap: Windows Unicode BMP (format 4) and full (format 12)
    uint16_t subtables = be16(data_, cmap + 2);
    for (uint16_t i = 0; i < subtables; ++i) {
        size_t rec = cmap + 4 + 8 * static_cast<size_t>(i);
        uint16_t platform = be16(data_, rec), encoding = be16(data_, rec + 2);
        size_t at = cmap + be32(data_, rec + 4);
        uint16_t format = be16(data_, at);
        if (format == 4 && ((platform == 3 && (encoding == 1 || encoding == 0)) || platform == 0)) {
            if (!cmap4_ || (platform == 3 && encoding == 1)) cmap4_ = at;
        }
        if (format == 12 && ((platform == 3 && encoding == 10) || platform == 0)) cmap12_ = at;
    }
    // AutoCAD's text height is the top of the glyph "A" (Arial: 1466 of 2048, where
    // sCapHeight says 1467 — AutoCAD's MTEXT in ACadSharp's files matches 1466 to 1e-5;
    // SimSun: 179 of 256, where "H" and sCapHeight have 175 — AutoCAD's MULTILEADERs in
    // SimSun, plan phase 3, B38); without an "A", sCapHeight
    UnitBox box;
    uint16_t h = glyph_index('A');
    if (h != 0 && (cff_ ? cff_box(h, box) : glyph_box(h, box)) && box.max_y > 0) cap = box.max_y;
    if (cap > 0.0) height_em_ = cap / units_per_em_;
    descent_ = descent / (units_per_em_ * height_em_);
    // The ascent GDI reports: usWinAscent; without OS/2, hhea's ascender
    double ascent = os2 ? be16(data_, os2 + 74) : static_cast<double>(be16s(data_, hhea + 4));
    ascent_ = ascent / (units_per_em_ * height_em_);
    // post.underlinePosition: the top of the underline, negative below the baseline
    if (size_t post = table("post"); post && table_size("post") >= 10) {
        underline_ = -static_cast<double>(be16s(data_, post + 8)) / (units_per_em_ * height_em_);
    }
    read_face();
    return true;
}

// The face as Windows describes it (FontFace)
void TrueTypeFont::read_face() {
    size_t os2 = table("OS/2");
    if (os2 && table_size("OS/2") >= 64) {
        uint16_t selection = be16(data_, os2 + 62);
        face_.italic = (selection & 1) != 0;
        face_.bold = (selection & 32) != 0;
    }
    bool fixed = false;
    if (size_t post = table("post"); post && table_size("post") >= 16) fixed = be32(data_, post + 12) != 0;
    int family = 0;
    if (fixed) {
        family = 0x30;
    } else if (os2 && table_size("OS/2") >= 32) {
        int cls = data_[os2 + 30];
        if (cls == 8) family = 0x20;
        else if (cls >= 1 && cls <= 7) family = 0x10;
        else if (cls == 10) family = 0x40;
        else if (cls == 9 || cls == 12) family = 0x50;
    }
    face_.pitch_and_family = (fixed ? 1 : 2) | family;
    // name ID 1 from the Windows (Unicode BMP) names, US English first
    size_t names = table("name");
    if (!names || table_size("name") < 6) return;
    uint16_t n = be16(data_, names + 2);
    size_t strings = names + be16(data_, names + 4);
    std::string first;
    for (uint16_t i = 0; i < n; ++i) {
        size_t rec = names + 6 + 12 * static_cast<size_t>(i);
        if (rec + 12 > data_.size()) break;
        uint16_t platform = be16(data_, rec), language = be16(data_, rec + 4), id = be16(data_, rec + 6);
        uint16_t length = be16(data_, rec + 8);
        size_t at = strings + be16(data_, rec + 10);
        if (id != 1 || platform != 3 || at + length > data_.size()) continue;
        std::string name;
        for (size_t k = 0; k + 1 < length; k += 2) {
            char32_t c = be16(data_, at + k);
            if (c >= 0xD800 && c < 0xDC00 && k + 3 < length) {
                char32_t lo = be16(data_, at + k + 2);
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                k += 2;
            }
            append_utf8(name, c);
        }
        if (language == 0x0409) {
            face_.family = name;
            return;
        }
        if (first.empty()) first = name;
    }
    face_.family = first;
}

size_t TrueTypeFont::table(const char* tag) const {
    auto it = tables_.find(tag);
    return it != tables_.end() && it->second.first + it->second.second <= data_.size() ? it->second.first : 0;
}

size_t TrueTypeFont::table_size(const char* tag) const {
    return table(tag) ? tables_.find(tag)->second.second : 0;
}

CffIndex TrueTypeFont::cff_index(size_t at) const {
    CffIndex index;
    uint16_t count = be16(data_, at);
    if (count == 0) {
        index.end = at + 2;
        return index;
    }
    int off_size = at + 2 < data_.size() ? data_[at + 2] : 0;
    if (off_size < 1 || off_size > 4) return index;
    auto offset = [&](size_t i) {
        size_t p = at + 3 + i * static_cast<size_t>(off_size);
        size_t v = 0;
        for (int k = 0; k < off_size; ++k) v = v << 8 | (p + k < data_.size() ? data_[p + k] : 0);
        return v;
    };
    // The offsets count from the byte before the data
    size_t data = at + 3 + (static_cast<size_t>(count) + 1) * off_size - 1;
    for (size_t i = 0; i < count; ++i) {
        size_t a = data + offset(i), b = data + offset(i + 1);
        if (b < a || b > data_.size()) return CffIndex{};
        index.objects.push_back({a, b});
    }
    index.end = data + offset(count);
    return index;
}

std::unordered_map<int, std::vector<double>> TrueTypeFont::cff_dict(size_t at, size_t end) const {
    std::unordered_map<int, std::vector<double>> dict;
    std::vector<double> operands;
    end = std::min(end, data_.size());
    while (at < end) {
        uint8_t b = data_[at++];
        if (b <= 21) {
            int op = b;
            if (b == 12 && at < end) op = 1200 + data_[at++];
            dict[op] = operands;
            operands.clear();
        } else if (b == 28) {
            operands.push_back(be16s(data_, at));
            at += 2;
        } else if (b == 29) {
            operands.push_back(static_cast<int32_t>(be32(data_, at)));
            at += 4;
        } else if (b == 30) {
            // A real: nibbles 0-9, . (a), E (b), E- (c), - (e), end (f)
            std::string s;
            bool done = false;
            while (at < end && !done) {
                uint8_t v = data_[at++];
                for (int n : {v >> 4, v & 15}) {
                    if (n <= 9) s += static_cast<char>('0' + n);
                    else if (n == 10) s += '.';
                    else if (n == 11) s += 'E';
                    else if (n == 12) s += "E-";
                    else if (n == 14) s += '-';
                    else if (n == 15) { done = true; break; }
                }
            }
            operands.push_back(std::atof(s.c_str()));
        } else if (b >= 32 && b <= 246) {
            operands.push_back(b - 139);
        } else if (b >= 247 && b <= 250) {
            operands.push_back((b - 247) * 256 + (at < end ? data_[at] : 0) + 108);
            ++at;
        } else if (b >= 251 && b <= 254) {
            operands.push_back(-(b - 251) * 256 - (at < end ? data_[at] : 0) - 108);
            ++at;
        }
    }
    return dict;
}

// The CFF table: the first font of its Top DICT INDEX, its charstrings, its subroutines
bool TrueTypeFont::load_cff() {
    size_t cff = table("CFF ");
    if (!cff) return false;
    size_t cff_end = cff + table_size("CFF ");
    CffIndex names = cff_index(cff + data_[cff + 2]);
    CffIndex tops = cff_index(names.end);
    CffIndex strings = cff_index(tops.end);
    global_subrs_ = cff_index(strings.end);
    if (tops.objects.empty()) return false;
    auto top = cff_dict(tops.objects[0].first, tops.objects[0].second);
    auto num = [](const std::unordered_map<int, std::vector<double>>& d, int op, size_t i) {
        auto it = d.find(op);
        return it != d.end() && i < it->second.size() ? static_cast<size_t>(it->second[i]) : 0;
    };
    if (!top.count(17)) return false;
    charstrings_ = cff_index(cff + num(top, 17, 0));
    // The local subroutines of a Private DICT (size, offset): Subrs counts from the
    // Private DICT
    auto private_subrs = [&](const std::unordered_map<int, std::vector<double>>& d) {
        size_t size = num(d, 18, 0), offset = num(d, 18, 1);
        if (!d.count(18) || cff + offset + size > cff_end) return CffIndex{};
        auto priv = cff_dict(cff + offset, cff + offset + size);
        if (!priv.count(19)) return CffIndex{};
        return cff_index(cff + offset + num(priv, 19, 0));
    };
    if (top.count(1230)) {
        // CID-keyed (ROS): font DICTs in FDArray, chosen per glyph by FDSelect
        CffIndex fds = cff_index(cff + num(top, 1236, 0));
        for (const auto& fd : fds.objects) local_subrs_.push_back(private_subrs(cff_dict(fd.first, fd.second)));
        size_t sel = cff + num(top, 1237, 0);
        size_t n = charstrings_.objects.size();
        fd_select_.assign(n, 0);
        if (sel < data_.size() && data_[sel] == 0) {
            for (size_t g = 0; g < n; ++g) fd_select_[g] = sel + 1 + g < data_.size() ? data_[sel + 1 + g] : 0;
        } else if (sel < data_.size() && data_[sel] == 3) {
            uint16_t ranges = be16(data_, sel + 1);
            for (uint16_t r = 0; r < ranges; ++r) {
                size_t rec = sel + 3 + 3 * static_cast<size_t>(r);
                uint16_t first = be16(data_, rec), next = be16(data_, rec + 3);
                uint8_t fd = rec + 2 < data_.size() ? data_[rec + 2] : 0;
                for (size_t g = first; g < next && g < n; ++g) fd_select_[g] = fd;
            }
        }
    } else {
        local_subrs_.push_back(private_subrs(top));
    }
    return !charstrings_.objects.empty();
}

// The extremes of a cubic Bézier (one axis) between 0 and 1
void cubic_extremes(double p0, double p1, double p2, double p3, std::vector<double>& t) {
    double a = -p0 + 3 * p1 - 3 * p2 + p3, b = 2 * (p0 - 2 * p1 + p2), c = p1 - p0;
    if (std::fabs(a) < 1e-12) {
        if (std::fabs(b) > 1e-12) t.push_back(-c / b);
        return;
    }
    double disc = b * b - 4 * a * c;
    if (disc < 0) return;
    double r = std::sqrt(disc);
    t.push_back((-b + r) / (2 * a));
    t.push_back((-b - r) / (2 * a));
}

// A glyph's box from its Type 2 charstring (Adobe TN 5177): the outline's points and the
// curves' extremes — the box that glyf's header holds for TrueType outlines
bool TrueTypeFont::cff_box(uint16_t index, UnitBox& box) const {
    if (index >= charstrings_.objects.size()) return false;
    const CffIndex* local = nullptr;
    size_t fd = index < fd_select_.size() ? fd_select_[index] : 0;
    if (fd < local_subrs_.size()) local = &local_subrs_[fd];
    auto bias = [](size_t n) { return n < 1240 ? 107 : (n < 33900 ? 1131 : 32768); };

    std::vector<double> stack;
    double transient[32] = {};
    double x = 0, y = 0;
    bool open = false;
    int stems = 0;
    bool width_done = false;
    bool ended = false;
    auto point = [&](double px, double py) {
        if (!open) { box.add(x, y); open = true; }
        box.add(px, py);
    };
    auto line = [&](double dx, double dy) {
        point(x + dx, y + dy);
        x += dx;
        y += dy;
    };
    auto curve = [&](double dxa, double dya, double dxb, double dyb, double dxc, double dyc) {
        double x1 = x + dxa, y1 = y + dya;
        double x2 = x1 + dxb, y2 = y1 + dyb;
        double x3 = x2 + dxc, y3 = y2 + dyc;
        point(x3, y3);
        std::vector<double> ts;
        cubic_extremes(x, x1, x2, x3, ts);
        cubic_extremes(y, y1, y2, y3, ts);
        for (double t : ts) {
            if (t <= 0.0 || t >= 1.0) continue;
            double s = 1 - t;
            box.add(s * s * s * x + 3 * s * s * t * x1 + 3 * s * t * t * x2 + t * t * t * x3,
                    s * s * s * y + 3 * s * s * t * y1 + 3 * s * t * t * y2 + t * t * t * y3);
        }
        x = x3;
        y = y3;
    };
    // The first stack-clearing operator may carry the width first: an odd one out
    auto width = [&](size_t expected_parity_args) {
        if (!width_done && stack.size() > expected_parity_args) stack.erase(stack.begin());
        width_done = true;
    };
    auto stem_width = [&]() {
        if (!width_done && stack.size() % 2 == 1) stack.erase(stack.begin());
        width_done = true;
    };
    auto moveto = [&](double dx, double dy) {
        open = false;
        x += dx;
        y += dy;
    };

    auto byte = [&](size_t i) { return i < data_.size() ? data_[i] : 0; };
    std::function<bool(size_t, size_t, int)> run = [&](size_t at, size_t end, int depth) -> bool {
        if (depth > 10) return false;
        while (at < end && !ended) {
            uint8_t b = static_cast<uint8_t>(byte(at++));
            if (b == 28) { stack.push_back(be16s(data_, at)); at += 2; continue; }
            if (b >= 32 && b <= 246) { stack.push_back(b - 139); continue; }
            if (b >= 247 && b <= 250) { stack.push_back((b - 247) * 256 + byte(at++) + 108); continue; }
            if (b >= 251 && b <= 254) { stack.push_back(-(b - 251) * 256 - byte(at++) - 108); continue; }
            if (b == 255) { stack.push_back(static_cast<int32_t>(be32(data_, at)) / 65536.0); at += 4; continue; }
            size_t n = stack.size();
            auto arg = [&](size_t i) { return i < stack.size() ? stack[i] : 0.0; };
            switch (b) {
                case 1: case 3: case 18: case 23:           // hstem, vstem, hstemhm, vstemhm
                    stem_width();
                    stems += static_cast<int>(stack.size() / 2);
                    stack.clear();
                    break;
                case 19: case 20:                           // hintmask, cntrmask
                    stem_width();
                    stems += static_cast<int>(stack.size() / 2);
                    stack.clear();
                    at += (stems + 7) / 8;
                    break;
                case 21:                                    // rmoveto
                    width(2);
                    moveto(arg(0), arg(1));
                    stack.clear();
                    break;
                case 22:                                    // hmoveto
                    width(1);
                    moveto(arg(0), 0);
                    stack.clear();
                    break;
                case 4:                                     // vmoveto
                    width(1);
                    moveto(0, arg(0));
                    stack.clear();
                    break;
                case 5:                                     // rlineto
                    for (size_t i = 0; i + 1 < n; i += 2) line(stack[i], stack[i + 1]);
                    stack.clear();
                    break;
                case 6: case 7: {                           // hlineto, vlineto
                    bool horizontal = b == 6;
                    for (size_t i = 0; i < n; ++i, horizontal = !horizontal) {
                        if (horizontal) line(stack[i], 0); else line(0, stack[i]);
                    }
                    stack.clear();
                    break;
                }
                case 8:                                     // rrcurveto
                    for (size_t i = 0; i + 5 < n; i += 6) {
                        curve(stack[i], stack[i + 1], stack[i + 2], stack[i + 3], stack[i + 4], stack[i + 5]);
                    }
                    stack.clear();
                    break;
                case 24: {                                  // rcurveline
                    size_t i = 0;
                    for (; i + 7 < n; i += 6) {
                        curve(stack[i], stack[i + 1], stack[i + 2], stack[i + 3], stack[i + 4], stack[i + 5]);
                    }
                    if (i + 1 < n) line(stack[i], stack[i + 1]);
                    stack.clear();
                    break;
                }
                case 25: {                                  // rlinecurve
                    size_t i = 0;
                    for (; i + 7 < n; i += 2) line(stack[i], stack[i + 1]);
                    if (i + 5 < n) curve(stack[i], stack[i + 1], stack[i + 2], stack[i + 3], stack[i + 4], stack[i + 5]);
                    stack.clear();
                    break;
                }
                case 26: {                                  // vvcurveto
                    size_t i = 0;
                    double dx1 = 0;
                    if (n % 4 == 1) dx1 = stack[i++];
                    for (; i + 3 < n; i += 4) {
                        curve(dx1, stack[i], stack[i + 1], stack[i + 2], 0, stack[i + 3]);
                        dx1 = 0;
                    }
                    stack.clear();
                    break;
                }
                case 27: {                                  // hhcurveto
                    size_t i = 0;
                    double dy1 = 0;
                    if (n % 4 == 1) dy1 = stack[i++];
                    for (; i + 3 < n; i += 4) {
                        curve(stack[i], dy1, stack[i + 1], stack[i + 2], stack[i + 3], 0);
                        dy1 = 0;
                    }
                    stack.clear();
                    break;
                }
                case 30: case 31: {                         // vhcurveto, hvcurveto
                    bool horizontal = b == 31;
                    size_t i = 0;
                    while (i + 3 < n) {
                        double extra = n - i == 5 ? stack[i + 4] : 0.0;
                        if (horizontal) curve(stack[i], 0, stack[i + 1], stack[i + 2], extra, stack[i + 3]);
                        else curve(0, stack[i], stack[i + 1], stack[i + 2], stack[i + 3], extra);
                        horizontal = !horizontal;
                        i += 4;
                    }
                    stack.clear();
                    break;
                }
                case 10: case 29: {                         // callsubr, callgsubr
                    if (stack.empty()) return false;
                    const CffIndex* subrs = b == 10 ? local : &global_subrs_;
                    if (!subrs) return false;
                    long i = static_cast<long>(stack.back()) + bias(subrs->objects.size());
                    stack.pop_back();
                    if (i < 0 || static_cast<size_t>(i) >= subrs->objects.size()) return false;
                    if (!run(subrs->objects[i].first, subrs->objects[i].second, depth + 1)) return false;
                    break;
                }
                case 11:                                    // return
                    return true;
                case 14:                                    // endchar (seac is not in OpenType)
                    width(0);
                    stack.clear();
                    ended = true;
                    return true;
                case 12: {
                    uint8_t op = at < end ? static_cast<uint8_t>(byte(at++)) : 0;
                    // The arithmetic operators take two operands
                    bool binary = op == 3 || op == 4 || op == 10 || op == 11 || op == 12 || op == 15 ||
                                  op == 20 || op == 24;
                    if ((binary && n < 2) || (op == 22 && n < 4)) { stack.clear(); break; }
                    switch (op) {
                        case 35:                            // flex
                            curve(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
                            curve(arg(6), arg(7), arg(8), arg(9), arg(10), arg(11));
                            break;
                        case 34: {                          // hflex
                            double y0 = y;
                            curve(arg(0), 0, arg(1), arg(2), arg(3), 0);
                            curve(arg(4), 0, arg(5), y0 - y, arg(6), 0);
                            break;
                        }
                        case 36: {                          // hflex1
                            double y0 = y;
                            curve(arg(0), arg(1), arg(2), arg(3), arg(4), 0);
                            curve(arg(5), 0, arg(6), arg(7), arg(8), y0 - y - arg(7));
                            break;
                        }
                        case 37: {                          // flex1
                            double dx = arg(0) + arg(2) + arg(4) + arg(6) + arg(8);
                            double dy = arg(1) + arg(3) + arg(5) + arg(7) + arg(9);
                            bool horizontal = std::fabs(dx) > std::fabs(dy);
                            curve(arg(0), arg(1), arg(2), arg(3), arg(4), arg(5));
                            curve(arg(6), arg(7), arg(8), arg(9), horizontal ? arg(10) : -dx,
                                  horizontal ? -dy : arg(10));
                            break;
                        }
                        // Arithmetic and storage (TN 5177 §4.5–4.6)
                        case 3: { double b2 = arg(n - 1), a2 = arg(n - 2); stack.resize(n - 2); stack.push_back(a2 != 0 && b2 != 0); break; }
                        case 4: { double b2 = arg(n - 1), a2 = arg(n - 2); stack.resize(n - 2); stack.push_back(a2 != 0 || b2 != 0); break; }
                        case 5: if (n) stack.back() = stack.back() == 0; break;
                        case 9: if (n) stack.back() = std::fabs(stack.back()); break;
                        case 10: { double b2 = arg(n - 1), a2 = arg(n - 2); stack.resize(n - 2); stack.push_back(a2 + b2); break; }
                        case 11: { double b2 = arg(n - 1), a2 = arg(n - 2); stack.resize(n - 2); stack.push_back(a2 - b2); break; }
                        case 12: { double b2 = arg(n - 1), a2 = arg(n - 2); stack.resize(n - 2); stack.push_back(b2 != 0 ? a2 / b2 : 0); break; }
                        case 14: if (n) stack.back() = -stack.back(); break;
                        case 15: { double b2 = arg(n - 1), a2 = arg(n - 2); stack.resize(n - 2); stack.push_back(a2 == b2); break; }
                        case 18: if (n) stack.pop_back(); break;
                        case 20: {
                            double i = arg(n - 1), v = arg(n - 2);
                            stack.resize(n - 2);
                            if (i >= 0 && i < 32) transient[static_cast<int>(i)] = v;
                            break;
                        }
                        case 21: if (n && stack.back() >= 0 && stack.back() < 32) stack.back() = transient[static_cast<int>(stack.back())]; break;
                        case 22: {
                            double v2 = arg(n - 1), v1 = arg(n - 2), s2 = arg(n - 3), s1 = arg(n - 4);
                            stack.resize(n - 4);
                            stack.push_back(v1 <= v2 ? s1 : s2);
                            break;
                        }
                        case 23: stack.push_back(0.5); break;   // random: any value in (0, 1]
                        case 24: { double b2 = arg(n - 1), a2 = arg(n - 2); stack.resize(n - 2); stack.push_back(a2 * b2); break; }
                        case 26: if (n) stack.back() = std::sqrt(std::fabs(stack.back())); break;
                        case 27: if (n) stack.push_back(stack.back()); break;
                        case 28: if (n >= 2) std::swap(stack[n - 1], stack[n - 2]); break;
                        case 29: {
                            if (!n) break;
                            long i = static_cast<long>(stack.back());
                            stack.pop_back();
                            if (i < 0) i = 0;
                            if (static_cast<size_t>(i) < stack.size()) stack.push_back(stack[stack.size() - 1 - i]);
                            break;
                        }
                        case 30: {
                            if (n < 2) break;
                            long j = static_cast<long>(stack.back());
                            long count = static_cast<long>(stack[n - 2]);
                            stack.resize(n - 2);
                            if (count <= 0 || static_cast<size_t>(count) > stack.size()) break;
                            j = ((j % count) + count) % count;
                            std::rotate(stack.end() - count, stack.end() - j, stack.end());
                            break;
                        }
                        default: break;
                    }
                    if (op == 34 || op == 35 || op == 36 || op == 37) stack.clear();
                    break;
                }
                default:
                    stack.clear();
                    break;
            }
        }
        return true;
    };
    const auto& cs = charstrings_.objects[index];
    return run(cs.first, cs.second, 0);
}

uint16_t TrueTypeFont::glyph_index(char32_t c) const {
    if (cmap12_) {
        uint32_t groups = be32(data_, cmap12_ + 12);
        for (uint32_t g = 0; g < groups; ++g) {
            size_t at = cmap12_ + 16 + 12 * static_cast<size_t>(g);
            uint32_t start = be32(data_, at), end = be32(data_, at + 4);
            if (c >= start && c <= end) return static_cast<uint16_t>(be32(data_, at + 8) + (c - start));
        }
    }
    if (cmap4_ && c <= 0xFFFF) {
        uint16_t segs = be16(data_, cmap4_ + 6) / 2;
        size_t ends = cmap4_ + 14;
        size_t starts = ends + 2 * static_cast<size_t>(segs) + 2;
        size_t deltas = starts + 2 * static_cast<size_t>(segs);
        size_t ranges = deltas + 2 * static_cast<size_t>(segs);
        for (uint16_t s = 0; s < segs; ++s) {
            uint16_t end = be16(data_, ends + 2 * s);
            if (c > end) continue;
            uint16_t start = be16(data_, starts + 2 * s);
            if (c < start) return 0;
            uint16_t delta = be16(data_, deltas + 2 * s);
            uint16_t range = be16(data_, ranges + 2 * s);
            if (range == 0) return static_cast<uint16_t>(c + delta);
            size_t at = ranges + 2 * s + range + 2 * (c - start);
            uint16_t g = be16(data_, at);
            return g == 0 ? 0 : static_cast<uint16_t>(g + delta);
        }
    }
    return 0;
}

// The glyph's box from its glyf header (composite glyphs have theirs too)
bool TrueTypeFont::glyph_box(uint16_t index, UnitBox& box) const {
    size_t loca = table("loca"), glyf = table("glyf");
    if (!loca || !glyf || index >= glyphs_) return false;
    size_t off, next;
    if (loca_format_ == 0) {
        off = 2 * static_cast<size_t>(be16(data_, loca + 2 * index));
        next = 2 * static_cast<size_t>(be16(data_, loca + 2 * (index + 1)));
    } else {
        off = be32(data_, loca + 4 * index);
        next = be32(data_, loca + 4 * (index + 1));
    }
    if (next <= off) return false;                         // no outline (a space)
    size_t at = glyf + off;
    box.add(be16s(data_, at + 2), be16s(data_, at + 4));
    box.add(be16s(data_, at + 6), be16s(data_, at + 8));
    return true;
}

Glyph TrueTypeFont::glyph(char32_t c) const {
    Glyph g;
    uint16_t index = glyph_index(c);
    if (index == 0 && c != 0) return g;
    g.found = true;
    size_t hmtx = table("hmtx");
    uint16_t m = std::min<uint16_t>(index, h_metrics_ > 0 ? h_metrics_ - 1 : 0);
    double scale = 1.0 / (units_per_em_ * height_em_);    // font units → text heights
    g.advance = be16(data_, hmtx + 4 * m) * scale;
    UnitBox box;
    if ((cff_ ? cff_box(index, box) : glyph_box(index, box)) && box.ink) {
        g.has_ink = true;
        g.min_x = box.min_x * scale;
        g.min_y = box.min_y * scale;
        g.max_x = box.max_x * scale;
        g.max_y = box.max_y * scale;
    }
    return g;
}

// ---------------------------------------------------------------------------
// Shape fonts (SHX)
// ---------------------------------------------------------------------------

class ShapeFont : public Font {
public:
    bool load(const Bytes& data);
    Glyph glyph(char32_t c) const override;
    bool is_shape_font() const override { return true; }
    double descent() const override { return below_ / above_; }
    double ascent() const override { return 1.0; }
    double underline() const override { return below_ / above_; }
    bool unicode() const override { return unifont_; }
    bool big_font() const override { return big_; }
    uint32_t shape_number(const std::string& name) const override;

private:
    struct Box {
        bool ink = false;
        double min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        void add(double x, double y) {
            if (!ink) { min_x = max_x = x; min_y = max_y = y; ink = true; return; }
            min_x = std::min(min_x, x); max_x = std::max(max_x, x);
            min_y = std::min(min_y, y); max_y = std::max(max_y, y);
        }
    };
    // Runs a shape's codes: pen position, scale and box; false when the shape is missing
    bool run(uint32_t number, double& x, double& y, double& scale, Box& box, int depth) const;

    // A definition: its name and codes
    void add_shape(uint32_t number, const Bytes& def);

    std::unordered_map<uint32_t, Bytes> shapes_;    // the codes (after the name)
    std::unordered_map<std::string, uint32_t> names_;   // upper case
    double above_ = 1.0;
    double below_ = 0.0;
    bool unifont_ = false;
    bool big_ = false;
};

bool ShapeFont::load(const Bytes& data) {
    std::string head(data.begin(), data.begin() + std::min<size_t>(data.size(), 40));
    auto starts = [&](const char* s) { return head.compare(0, std::strlen(s), s) == 0; };
    auto body = [&](const Bytes& d, size_t at) {
        // A definition: its name up to a 0, then the codes
        size_t i = at;
        while (i < d.size() && d[i] != 0) ++i;
        return i + 1;
    };
    if (starts("AutoCAD-86 shapes 1.0") || starts("AutoCAD-86 shapes 1.1")) {
        size_t at = 0;
        while (at < data.size() && data[at] != 0x1A) ++at;
        ++at;
        uint16_t count = le16(data, at + 4);
        size_t index = at + 6;
        size_t defs = index + 4 * static_cast<size_t>(count);
        for (uint16_t i = 0; i < count; ++i) {
            uint16_t number = le16(data, index + 4 * i);
            uint16_t length = le16(data, index + 4 * i + 2);
            if (defs + length > data.size()) return false;
            Bytes def(data.begin() + static_cast<long>(defs), data.begin() + static_cast<long>(defs + length));
            if (number == 0) {
                // Font info: name, then above, below, modes (, encoding, type), 0
                size_t b = body(def, 0);
                if (b < def.size()) above_ = def[b];
                if (b + 1 < def.size()) below_ = def[b + 1];
            } else {
                add_shape(number, def);
            }
            defs += length;
        }
        return above_ > 0;
    }
    if (starts("AutoCAD-86 unifont 1.0")) {
        unifont_ = true;
        size_t at = 0;
        while (at < data.size() && data[at] != 0x1A) ++at;
        ++at;
        uint32_t count = le32(data, at);
        uint16_t info_length = le16(data, at + 4);
        size_t info = at + 6;
        size_t b = body(data, info);
        if (b < data.size()) above_ = data[b];
        if (b + 1 < data.size()) below_ = data[b + 1];
        size_t p = info + info_length;
        for (uint32_t i = 1; i < count && p + 4 <= data.size(); ++i) {
            uint16_t number = le16(data, p);
            uint16_t length = le16(data, p + 2);
            p += 4;
            if (p + length > data.size()) return false;
            Bytes def(data.begin() + static_cast<long>(p), data.begin() + static_cast<long>(p + length));
            add_shape(number, def);
            p += length;
        }
        return above_ > 0;
    }
    if (starts("AutoCAD-86 bigfont 1.0")) {
        big_ = true;
        size_t at = 0;
        while (at < data.size() && data[at] != 0x1A) ++at;
        ++at;
        uint16_t count = le16(data, at + 2);
        uint16_t ranges = le16(data, at + 4);
        size_t index = at + 6 + 4 * static_cast<size_t>(ranges);
        for (uint16_t i = 0; i < count; ++i) {
            size_t e = index + 8 * static_cast<size_t>(i);
            uint16_t number = le16(data, e);
            uint16_t length = le16(data, e + 2);
            uint32_t offset = le32(data, e + 4);
            if (number == 0 && length == 0) continue;
            if (offset + length > data.size()) continue;
            Bytes def(data.begin() + offset, data.begin() + offset + length);
            if (number == 0) {
                // Font info: name, then the character height, 0, modes, width (vertical),
                // 0 — big font characters stand on the baseline
                size_t b = body(def, 0);
                if (b < def.size()) above_ = def[b];
            } else {
                add_shape(number, def);
            }
        }
        return above_ > 0;
    }
    return false;
}

void ShapeFont::add_shape(uint32_t number, const Bytes& def) {
    size_t end = 0;
    while (end < def.size() && def[end] != 0) ++end;
    std::string name(def.begin(), def.begin() + static_cast<long>(end));
    for (auto& ch : name) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    if (!name.empty()) names_.emplace(name, number);
    shapes_[number] = Bytes(def.begin() + static_cast<long>(std::min(end + 1, def.size())), def.end());
}

uint32_t ShapeFont::shape_number(const std::string& name) const {
    std::string key = name;
    for (auto& ch : key) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    auto it = names_.find(key);
    return it != names_.end() ? it->second : 0;
}

// The direction vectors of the 16 directions of a length/direction code
const double kDir[16][2] = {
    {1, 0}, {1, 0.5}, {1, 1}, {0.5, 1}, {0, 1}, {-0.5, 1}, {-1, 1}, {-1, 0.5},
    {-1, 0}, {-1, -0.5}, {-1, -1}, {-0.5, -1}, {0, -1}, {0.5, -1}, {1, -1}, {1, -0.5},
};

bool ShapeFont::run(uint32_t number, double& x, double& y, double& scale, Box& box, int depth) const {
    auto it = shapes_.find(number);
    if (it == shapes_.end() || depth > 16) return false;
    const Bytes& c = it->second;
    bool pen = true;
    std::vector<std::array<double, 2>> stack;
    auto move = [&](double dx, double dy) {
        double nx = x + dx * scale, ny = y + dy * scale;
        if (pen) { box.add(x, y); box.add(nx, ny); }
        x = nx;
        y = ny;
    };
    // An arc from the pen around a center: its box (the ends and the axis crossings inside)
    auto arc = [&](double cx, double cy, double r, double a0, double sweep) {
        double a1 = a0 + sweep;
        if (pen) {
            box.add(cx + r * std::cos(a0), cy + r * std::sin(a0));
            box.add(cx + r * std::cos(a1), cy + r * std::sin(a1));
            double lo = std::min(a0, a1), hi = std::max(a0, a1);
            for (int k = -8; k <= 8; ++k) {
                double a = k * M_PI / 2.0;
                if (a > lo && a < hi) box.add(cx + r * std::cos(a), cy + r * std::sin(a));
            }
        }
        x = cx + r * std::cos(a1);
        y = cy + r * std::sin(a1);
    };
    auto bulge_arc = [&](double dx, double dy, int bulge) {
        dx *= scale;
        dy *= scale;
        if (bulge == 0) {
            if (pen) { box.add(x, y); box.add(x + dx, y + dy); }
            x += dx;
            y += dy;
            return;
        }
        // Bulge = 2h / chord × 127; the arc's center and angles from it
        double b = bulge / 127.0;
        double chord = std::sqrt(dx * dx + dy * dy);
        double angle = 4.0 * std::atan(b);
        double r = chord / (2.0 * std::sin(std::fabs(angle) / 2.0));
        double mx = x + dx / 2.0, my = y + dy / 2.0;
        double d = r * std::cos(std::fabs(angle) / 2.0);
        double nx = -dy / chord, ny = dx / chord;           // left of the chord
        double s = b > 0 ? 1.0 : -1.0;
        double cx = mx + nx * d * s, cy = my + ny * d * s;
        if (std::fabs(angle) > M_PI) { cx = mx - nx * d * s; cy = my - ny * d * s; }
        double a0 = std::atan2(y - cy, x - cx);
        double sx = x, sy = y;
        arc(cx, cy, r, a0, angle);
        x = sx + dx;                    // exactly at the end
        y = sy + dy;
    };
    auto sbyte = [&](size_t i) { return static_cast<int8_t>(c[i]); };
    size_t i = 0;
    bool vertical_only = false;
    while (i < c.size()) {
        uint8_t code = c[i++];
        bool skip = vertical_only;          // code 14: the next command is for vertical text only
        vertical_only = false;
        if (code >= 0x10) {
            if (skip) continue;
            int length = code >> 4, dir = code & 15;
            move(kDir[dir][0] * length, kDir[dir][1] * length);
            continue;
        }
        switch (code) {
            case 0: return true;
            case 1: if (!skip) pen = true; break;
            case 2: if (!skip) pen = false; break;
            case 3: { uint8_t f = i < c.size() ? c[i++] : 1; if (!skip && f) scale /= f; break; }
            case 4: { uint8_t f = i < c.size() ? c[i++] : 1; if (!skip) scale *= f; break; }
            case 5: if (!skip) stack.push_back({x, y}); break;
            case 6:
                if (!skip && !stack.empty()) { x = stack.back()[0]; y = stack.back()[1]; stack.pop_back(); }
                break;
            case 7: {
                uint32_t sub;
                if (unifont_ || big_) {
                    if (big_ && i < c.size() && c[i] == 0) {
                        // Big font: 0, then the shape (2 bytes), origin, width, height
                        sub = static_cast<uint32_t>(c[i + 1]) << 8 | c[i + 2];
                        i += 7;
                    } else if (big_) {
                        sub = c[i];
                        i += 1;
                    } else {
                        sub = static_cast<uint32_t>(c[i]) << 8 | c[i + 1];
                        i += 2;
                    }
                } else {
                    sub = c[i++];
                }
                if (!skip) {
                    bool saved = pen;
                    run(sub, x, y, scale, box, depth + 1);
                    pen = saved;
                }
                break;
            }
            case 8: {
                double dx = sbyte(i), dy = sbyte(i + 1);
                i += 2;
                if (!skip) move(dx, dy);
                break;
            }
            case 9:
                while (i + 1 < c.size()) {
                    double dx = sbyte(i), dy = sbyte(i + 1);
                    i += 2;
                    if (dx == 0 && dy == 0) break;
                    if (!skip) move(dx, dy);
                }
                break;
            case 10: {
                // Octant arc: radius, then (sign: direction) start octant (high nibble),
                // octants (low nibble, 0 = 8)
                double r = c[i] * scale;
                int8_t s = sbyte(i + 1);
                i += 2;
                if (skip) break;
                int sb = std::abs(s);
                int start = (sb >> 4) & 7, span = sb & 7;
                if (span == 0) span = 8;
                double dirn = s < 0 ? -1.0 : 1.0;
                double a0 = start * M_PI / 4.0;
                double cx = x - r * std::cos(a0), cy = y - r * std::sin(a0);
                arc(cx, cy, r, a0, dirn * span * M_PI / 4.0);
                break;
            }
            case 11: {
                // Fractional arc: start offset, end offset (1/256 of 45°), radius (2 bytes),
                // then as code 10
                int start_off = c[i], end_off = c[i + 1];
                double r = (c[i + 2] * 256 + c[i + 3]) * scale;
                int8_t s = sbyte(i + 4);
                i += 5;
                if (skip) break;
                int sb = std::abs(s);
                int start = (sb >> 4) & 7, span = sb & 7;
                double dirn = s < 0 ? -1.0 : 1.0;
                double a0 = (start * 45.0 + dirn * start_off * 45.0 / 256.0) * M_PI / 180.0;
                double end_oct = start + dirn * (span == 0 ? 8 : span);
                double a1 = ((end_oct - dirn) * 45.0 + dirn * end_off * 45.0 / 256.0) * M_PI / 180.0;
                if (end_off == 0) a1 = end_oct * 45.0 * M_PI / 180.0;
                double cx = x - r * std::cos(a0), cy = y - r * std::sin(a0);
                arc(cx, cy, r, a0, a1 - a0);
                break;
            }
            case 12: {
                double dx = sbyte(i), dy = sbyte(i + 1);
                int b = sbyte(i + 2);
                i += 3;
                if (!skip) bulge_arc(dx, dy, b);
                break;
            }
            case 13:
                while (i + 1 < c.size()) {
                    double dx = sbyte(i), dy = sbyte(i + 1);
                    i += 2;
                    if (dx == 0 && dy == 0) break;
                    int b = sbyte(i++);
                    if (!skip) bulge_arc(dx, dy, b);
                }
                break;
            case 14:
                vertical_only = true;
                break;
            default:
                break;
        }
    }
    return true;
}

Glyph ShapeFont::glyph(char32_t c) const {
    Glyph g;
    double x = 0, y = 0, scale = 1.0 / above_;
    Box box;
    if (!run(static_cast<uint32_t>(c), x, y, scale, box, 0)) return g;
    g.found = true;
    g.advance = x;
    g.has_ink = box.ink;
    g.min_x = box.min_x;
    g.min_y = box.min_y;
    g.max_x = box.max_x;
    g.max_y = box.max_y;
    return g;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A face in the fonts folders, for finding a font by its family
struct FaceInfo {
    std::vector<std::string> families;  // lower case, every language
    std::filesystem::path path;
    int face = 0;
    bool italic = false;
    int weight = 400;
};

bool read_range(std::ifstream& in, uint64_t at, size_t n, Bytes& out) {
    out.assign(n, 0);
    in.clear();
    in.seekg(static_cast<std::streamoff>(at));
    in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
    return static_cast<size_t>(in.gcount()) == n;
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

// The faces of a font file: their family names (name IDs 1 and 16; Windows names as
// UTF-16, Macintosh names as ASCII), italic and weight (OS/2) — read without loading the
// whole file
void read_faces(const std::filesystem::path& path, std::vector<FaceInfo>& out) {
    std::ifstream in(path, std::ios::binary);
    Bytes head;
    if (!in || !read_range(in, 0, 12, head)) return;
    std::vector<uint32_t> bases;
    if (be32(head, 0) == 0x74746366) {
        uint32_t n = std::min<uint32_t>(be32(head, 8), 256);
        Bytes offsets;
        if (!read_range(in, 12, 4 * static_cast<size_t>(n), offsets)) return;
        for (uint32_t i = 0; i < n; ++i) bases.push_back(be32(offsets, 4 * static_cast<size_t>(i)));
    } else {
        bases.push_back(0);
    }
    for (size_t f = 0; f < bases.size(); ++f) {
        Bytes dir;
        if (!read_range(in, bases[f], 12, dir)) continue;
        uint32_t version = be32(dir, 0);
        if (version != 0x00010000 && version != 0x74727565 && version != 0x4F54544F) continue;
        uint16_t count = be16(dir, 4);
        Bytes records;
        if (!read_range(in, bases[f] + 12, 16 * static_cast<size_t>(count), records)) continue;
        uint32_t name_at = 0, name_size = 0, os2_at = 0, os2_size = 0;
        for (uint16_t i = 0; i < count; ++i) {
            size_t rec = 16 * static_cast<size_t>(i);
            std::string tag(reinterpret_cast<const char*>(&records[rec]), 4);
            if (tag == "name") { name_at = be32(records, rec + 8); name_size = be32(records, rec + 12); }
            if (tag == "OS/2") { os2_at = be32(records, rec + 8); os2_size = be32(records, rec + 12); }
        }
        FaceInfo info;
        info.path = path;
        info.face = static_cast<int>(f);
        Bytes os2;
        if (os2_at && os2_size >= 64 && read_range(in, os2_at, 64, os2)) {
            info.weight = be16(os2, 4);
            uint16_t selection = be16(os2, 62);
            info.italic = selection & 1;
            if (info.weight == 0) info.weight = (selection & 32) ? 700 : 400;
        }
        Bytes names;
        if (!name_at || name_size > (1u << 22) || !read_range(in, name_at, name_size, names)) continue;
        uint16_t n = be16(names, 2);
        size_t strings = be16(names, 4);
        for (uint16_t i = 0; i < n; ++i) {
            size_t rec = 6 + 12 * static_cast<size_t>(i);
            uint16_t platform = be16(names, rec), encoding = be16(names, rec + 2);
            uint16_t id = be16(names, rec + 6), length = be16(names, rec + 8);
            size_t at = strings + be16(names, rec + 10);
            if ((id != 1 && id != 16) || at + length > names.size()) continue;
            std::string name;
            if (platform == 3 || platform == 0) {
                for (size_t k = 0; k + 1 < length; k += 2) {
                    char32_t c = be16(names, at + k);
                    if (c >= 0xD800 && c < 0xDC00 && k + 3 < length) {
                        char32_t lo = be16(names, at + k + 2);
                        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                        k += 2;
                    }
                    append_utf8(name, c);
                }
            } else if (platform == 1 && encoding == 0) {
                for (size_t k = 0; k < length; ++k) {
                    if (names[at + k] < 0x80) name += static_cast<char>(names[at + k]);
                }
            }
            if (!name.empty()) info.families.push_back(lower(name));
        }
        if (!info.families.empty()) out.push_back(std::move(info));
    }
}

} // namespace

std::shared_ptr<const Font> load_font(const std::filesystem::path& path, int face) {
    Bytes data;
    if (!read_all(path, data)) return nullptr;
    std::string ext = lower(path.extension().string());
    if (ext == ".shx") {
        auto f = std::make_shared<ShapeFont>();
        return f->load(data) ? f : nullptr;
    }
    auto f = std::make_shared<TrueTypeFont>();
    return f->load(std::move(data), face) ? f : nullptr;
}

std::shared_ptr<const Font> find_font_family(const std::string& family, bool bold, bool italic) {
    static std::mutex m;
    static bool indexed = false;
    static std::vector<FaceInfo> faces;
    static std::unordered_map<std::string, std::shared_ptr<const Font>> cache;
    static unsigned generation = 0;
    std::string want = lower(family);
    std::string key = want + (bold ? "|b" : "|") + (italic ? "i" : "");
    std::lock_guard<std::mutex> lock(m);
    if (unsigned g = current_generation(); g != generation) {
        indexed = false;
        faces.clear();
        cache.clear();
        generation = g;
    }
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    if (!indexed) {
        std::error_code ec;
        for (const auto& dir : font_paths()) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                std::string ext = lower(utf8_from_path(entry.path().extension()));
                if (ext == ".ttf" || ext == ".ttc" || ext == ".otf" || ext == ".otc") read_faces(entry.path(), faces);
            }
        }
        indexed = true;
    }
    // As Windows matches a face: the family, then italic, then the weight nearest
    const FaceInfo* best = nullptr;
    int best_score = 0;
    int want_weight = bold ? 700 : 400;
    for (const auto& f : faces) {
        if (std::find(f.families.begin(), f.families.end(), want) == f.families.end()) continue;
        int score = (f.italic != italic ? 10000 : 0) + std::abs(f.weight - want_weight);
        if (!best || score < best_score) { best = &f; best_score = score; }
    }
    std::shared_ptr<const Font> font = best ? load_font(best->path, best->face) : nullptr;
    cache[key] = font;
    return font;
}

std::shared_ptr<const Font> find_font(const std::string& file_name) {
    static std::mutex m;
    static std::unordered_map<std::string, std::shared_ptr<const Font>> cache;
    static unsigned generation = 0;
    std::string key = lower(file_name);
    {
        std::lock_guard<std::mutex> lock(m);
        if (unsigned g = current_generation(); g != generation) {
            cache.clear();
            generation = g;
        }
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    std::filesystem::path name = path_from_utf8(file_name);
    if (!name.has_extension()) name += ".shx";              // "txt" is txt.shx, as AutoCAD
    std::shared_ptr<const Font> font;
    std::error_code ec;
    if (name.is_absolute() && std::filesystem::exists(name, ec)) font = load_font(name);
    if (!font) {
        std::string want = lower(utf8_from_path(name.filename()));
        for (const auto& dir : font_paths()) {
            for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (lower(utf8_from_path(entry.path().filename())) == want) {
                    font = load_font(entry.path());
                    break;
                }
            }
            if (font) break;
        }
    }
    std::lock_guard<std::mutex> lock(m);
    cache[key] = font;
    return font;
}

} // namespace internal
} // namespace hdxf
