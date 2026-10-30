#include "proxy_record_graphics.hpp"
#include "text_encoding.hpp"
#include "text_layout.hpp"
#include "fonts.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace hdxf {
namespace internal {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Reading the bytes
// ---------------------------------------------------------------------------

uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

double le_double(const uint8_t* p) {
    uint64_t u = 0;
    for (int i = 7; i >= 0; --i) u = (u << 8) | p[i];
    double v = 0.0;
    std::memcpy(&v, &u, 8);
    return v;
}

// A reader over one command's data
class Bytes {
public:
    Bytes(const uint8_t* data, size_t size) : p_(data), size_(size) {}
    bool ok() const { return ok_; }
    size_t left() const { return size_ - at_; }
    bool has(size_t n) {
        if (n > left()) ok_ = false;
        return ok_;
    }
    int32_t i32() {
        if (!has(4)) return 0;
        int32_t v = static_cast<int32_t>(le32(p_ + at_));
        at_ += 4;
        return v;
    }
    double dbl() {
        if (!has(8)) return 0.0;
        double v = le_double(p_ + at_);
        at_ += 8;
        return v;
    }
    Point3 point() {
        Point3 q;
        q.x = dbl();
        q.y = dbl();
        q.z = dbl();
        return q;
    }
    // A string that is padded to a multiple of 4 bytes from its start: narrow (bytes up to a NUL) or wide
    // (UTF-16 units up to a NUL, as UTF-8)
    std::string narrow() {
        size_t start = at_;
        size_t end = start;
        while (end < size_ && p_[end] != 0) ++end;
        if (end >= size_) { ok_ = false; return {}; }
        std::string s(reinterpret_cast<const char*>(p_ + start), end - start);
        size_t used = ((end - start + 1) + 3) / 4 * 4;
        if (used > left()) { ok_ = false; return {}; }
        at_ = start + used;
        return s;
    }
    std::string wide() {
        size_t start = at_;
        std::u16string units;
        size_t pos = start;
        while (true) {
            if (pos + 2 > size_) { ok_ = false; return {}; }
            char16_t u = static_cast<char16_t>(p_[pos] | (p_[pos + 1] << 8));
            pos += 2;
            if (u == 0) break;
            units.push_back(u);
        }
        size_t used = ((pos - start) + 3) / 4 * 4;
        if (used > left()) { ok_ = false; return {}; }
        at_ = start + used;
        return utf8_from_utf16(units);
    }
    const uint8_t* current() const { return p_ + at_; }

private:
    static std::string utf8_from_utf16(const std::u16string& units) {
        std::string out;
        for (size_t i = 0; i < units.size(); ++i) {
            uint32_t c = units[i];
            if (c >= 0xD800 && c < 0xDC00 && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (units[i + 1] - 0xDC00);
                ++i;
            }
            if (c < 0x80) {
                out.push_back(static_cast<char>(c));
            } else if (c < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            } else if (c < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (c >> 12)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (c >> 18)));
                out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
            }
        }
        return out;
    }

    const uint8_t* p_;
    size_t size_;
    size_t at_ = 0;
    bool ok_ = true;
};

// DWG's bit stream (bits from the high one of each byte; a value's bytes little-endian), what LWPLINE (33) is
class BitReader {
public:
    BitReader(const uint8_t* data, size_t size) : p_(data), bits_(size * 8) {}
    bool ok() const { return ok_; }
    int bit() {
        if (at_ >= bits_) { ok_ = false; return 0; }
        int b = (p_[at_ / 8] >> (7 - at_ % 8)) & 1;
        ++at_;
        return b;
    }
    unsigned bits(int n) {
        unsigned v = 0;
        for (int i = 0; i < n; ++i) v = (v << 1) | static_cast<unsigned>(bit());
        return v;
    }
    uint8_t byte() { return static_cast<uint8_t>(bits(8)); }
    double raw_double() {
        uint8_t b[8];
        for (auto& x : b) x = byte();
        return le_double(b);
    }
    int bit_short() {
        switch (bits(2)) {
            case 0: { int lo = byte(); int hi = byte(); return static_cast<int16_t>(lo | (hi << 8)); }
            case 1: return byte();
            case 2: return 0;
            default: return 256;
        }
    }
    int32_t bit_long() {
        switch (bits(2)) {
            case 0: {
                uint32_t u = 0;
                for (int i = 0; i < 4; ++i) u |= static_cast<uint32_t>(byte()) << (8 * i);
                return static_cast<int32_t>(u);
            }
            case 1: return byte();
            case 2: return 0;
            default: ok_ = false; return 0;
        }
    }
    double bit_double() {
        switch (bits(2)) {
            case 0: return raw_double();
            case 1: return 1.0;
            case 2: return 0.0;
            default: ok_ = false; return 0.0;
        }
    }
    double default_double(double d) {
        uint8_t a[8];
        std::memcpy(a, &d, 8);
        switch (bits(2)) {
            case 0: return d;
            case 1:
                for (int i = 0; i < 4; ++i) a[i] = byte();
                break;
            case 2:
                a[4] = byte();
                a[5] = byte();
                for (int i = 0; i < 4; ++i) a[i] = byte();
                break;
            default:
                return raw_double();
        }
        double v = 0.0;
        std::memcpy(&v, a, 8);
        return v;
    }

private:
    const uint8_t* p_;
    size_t bits_;
    size_t at_ = 0;
    bool ok_ = true;
};

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

Point3 sub(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point3 add(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 scale(const Point3& a, double f) { return {a.x * f, a.y * f, a.z * f}; }
double dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Point3& a) { return std::sqrt(dot(a, a)); }
Point3 unit(const Point3& a) {
    double l = length(a);
    return l < 1e-300 ? Point3{0.0, 0.0, 1.0} : scale(a, 1.0 / l);
}

// A transform: the first three rows of AcGeMatrix3d (the translation in the fourth column)
struct Affine {
    double m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};

    Point3 apply(const Point3& p) const {
        return {m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
                m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
                m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]};
    }
    Point3 linear(const Point3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    // this after `inner`: a point goes through `inner` first
    Affine after(const Affine& inner) const {
        Affine out;
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) {
                double s = j == 3 ? m[i][3] : 0.0;
                for (int k = 0; k < 3; ++k) s += m[i][k] * inner.m[k][j];
                out.m[i][j] = s;
            }
        }
        return out;
    }
};

// The collector: points through the current transform
class Drawn {
public:
    explicit Drawn(int code_page) : code_page_(code_page) { stack_.push_back(Affine()); }

    ProxyDrawn out;

    void command(const ProxyCommandRef& c) {
        Bytes b(c.data, c.size);
        switch (c.type) {
            case 1: extents(b); break;
            case 2: circle(b); break;
            case 3: circle_3p(b); break;
            case 4: arc(b); break;
            case 5: arc_3p(b); break;
            case 6:
            case 7: polyline(b, false); break;
            case 8: mesh(b); break;
            case 9: shell(b); break;
            case 11: text(b, false); break;
            case 38: text(b, true); break;
            case 29:
            case 30: push(b); break;
            case 31: if (stack_.size() > 1) stack_.pop_back(); break;
            case 32: polyline(b, true); break;
            case 33: lwpolyline(b); break;
            case 44: ellipse_arc(b); break;
            default: break;
        }
    }

private:
    const Affine& m() const { return stack_.back(); }
    void add_point(const Point3& p) { out.points.push_back(m().apply(p)); }
    void drew() { ++out.commands_drawn; }
    void skipped() { ++out.commands_skipped; }

    void extents(Bytes& b) {
        Point3 lo = b.point();
        Point3 hi = b.point();
        if (!b.ok() || out.has_declared) return;
        out.has_declared = true;
        out.declared_min = lo;
        out.declared_max = hi;
    }

    // The curve c + u cos t + v sin t from a0 to a1 (full: all of it) in the current transform: its extremes
    // exactly, and a point every 2 degrees
    void curve(const Point3& center, const Point3& u, const Point3& v, double a0, double a1, bool full) {
        Point3 c = m().apply(center);
        Point3 uu = m().linear(u);
        Point3 vv = m().linear(v);
        // The extreme of each axis lies where the derivative is 0: at atan2(v, u) and half a turn on
        for (int axis = 0; axis < 3; ++axis) {
            double ua = axis == 0 ? uu.x : axis == 1 ? uu.y : uu.z;
            double va = axis == 0 ? vv.x : axis == 1 ? vv.y : vv.z;
            double t = std::atan2(va, ua);
            for (double k : {0.0, kPi}) {
                double a = t + k;
                if (!full) {
                    double rel = std::fmod(a - a0, 2.0 * kPi);
                    if (rel < 0) rel += 2.0 * kPi;
                    if (rel > a1 - a0) continue;
                }
                out.points.push_back(add(c, add(scale(uu, std::cos(a)), scale(vv, std::sin(a)))));
            }
        }
        double span = full ? 2.0 * kPi : a1 - a0;
        int steps = std::max(2, static_cast<int>(std::ceil(std::fabs(span) / (2.0 * kPi / 180.0))));
        for (int i = 0; i <= steps; ++i) {
            double a = a0 + span * i / steps;
            out.points.push_back(add(c, add(scale(uu, std::cos(a)), scale(vv, std::sin(a)))));
        }
        // the end points exactly
        if (!full) {
            out.points.push_back(add(c, add(scale(uu, std::cos(a0)), scale(vv, std::sin(a0)))));
            out.points.push_back(add(c, add(scale(uu, std::cos(a1)), scale(vv, std::sin(a1)))));
        }
    }

    void circle(Bytes& b) {
        Point3 center = b.point();
        double radius = b.dbl();
        Point3 normal = b.point();
        if (!b.ok()) return skipped();
        Point3 ax, ay;
        ecs::arbitrary_axis(unit(normal), ax, ay);
        curve(center, scale(ax, radius), scale(ay, radius), 0.0, 2.0 * kPi, true);
        drew();
    }

    // The circle through three points
    bool circle_through(const Point3& p1, const Point3& p2, const Point3& p3, Point3& center, double& radius,
                        Point3& normal) {
        Point3 a = sub(p2, p1), c = sub(p3, p1);
        Point3 n = cross(a, c);
        double d = 2.0 * dot(n, n);
        if (d < 1e-300) return false;
        Point3 to = add(scale(cross(n, a), dot(c, c)), scale(cross(c, n), dot(a, a)));
        Point3 off = scale(to, 1.0 / d);
        center = add(p1, off);
        radius = length(off);
        normal = unit(n);
        return true;
    }

    void circle_3p(Bytes& b) {
        Point3 p1 = b.point(), p2 = b.point(), p3 = b.point();
        if (!b.ok()) return skipped();
        Point3 center, normal;
        double radius = 0.0;
        if (!circle_through(p1, p2, p3, center, radius, normal)) {
            add_point(p1);
            add_point(p2);
            add_point(p3);
            return drew();
        }
        Point3 ax, ay;
        ecs::arbitrary_axis(normal, ax, ay);
        curve(center, scale(ax, radius), scale(ay, radius), 0.0, 2.0 * kPi, true);
        drew();
    }

    void arc(Bytes& b) {
        Point3 center = b.point();
        double radius = b.dbl();
        Point3 normal = unit(b.point());
        Point3 start = b.point();
        double sweep = b.dbl();
        if (!b.ok()) return skipped();
        Point3 ax, ay;
        ecs::arbitrary_axis(normal, ax, ay);
        double a0 = std::atan2(dot(start, ay), dot(start, ax));
        if (sweep < 0) {
            a0 += sweep;
            sweep = -sweep;
        }
        curve(center, scale(ax, radius), scale(ay, radius), a0, a0 + sweep, false);
        drew();
    }

    void arc_3p(Bytes& b) {
        Point3 p1 = b.point(), p2 = b.point(), p3 = b.point();
        if (!b.ok()) return skipped();
        Point3 center, normal;
        double radius = 0.0;
        if (!circle_through(p1, p2, p3, center, radius, normal)) {
            add_point(p1);
            add_point(p2);
            add_point(p3);
            return drew();
        }
        Point3 ax = unit(sub(p1, center));
        Point3 ay = cross(normal, ax);
        auto angle = [&](const Point3& p) {
            double a = std::atan2(dot(sub(p, center), ay), dot(sub(p, center), ax));
            return a < 0 ? a + 2.0 * kPi : a;
        };
        // The normal is that of p1, p2, p3 in this order: the arc runs counterclockwise about it from p1 to p3,
        // through p2
        curve(center, scale(ax, radius), scale(ay, radius), 0.0, angle(p3), false);
        drew();
    }

    void polyline(Bytes& b, bool with_normal) {
        int32_t n = b.i32();
        // each point is 24 bytes: a count that the data cannot hold is no count
        if (!b.ok() || n < 0 || static_cast<size_t>(n) > b.left() / 24) return skipped();
        std::vector<Point3> points;
        points.reserve(static_cast<size_t>(n));
        for (int32_t i = 0; i < n; ++i) points.push_back(b.point());
        if (!b.ok()) return skipped();
        for (const auto& p : points) add_point(p);
        (void)with_normal;
        drew();
    }

    void mesh(Bytes& b) {
        int32_t rows = b.i32();
        int32_t columns = b.i32();
        if (!b.ok() || rows < 0 || columns < 0) return skipped();
        uint64_t count = static_cast<uint64_t>(rows) * static_cast<uint64_t>(columns);
        if (count > b.left() / 24) return skipped();
        for (uint64_t i = 0; i < count; ++i) add_point(b.point());
        drew();
    }

    void shell(Bytes& b) {
        int32_t n = b.i32();
        if (!b.ok() || n < 0 || static_cast<size_t>(n) > b.left() / 24) return skipped();
        for (int32_t i = 0; i < n; ++i) add_point(b.point());
        drew();
    }

    void push(Bytes& b) {
        if (b.left() < 128) return;
        Affine a;
        double v[16];
        for (double& x : v) x = b.dbl();
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 4; ++j) a.m[i][j] = v[4 * i + j];
        }
        stack_.push_back(m().after(a));
    }

    void ellipse_arc(Bytes& b) {
        Point3 center = b.point();
        Point3 normal = unit(b.point());
        double major = b.dbl();
        double minor = b.dbl();
        double start = b.dbl();
        double end = b.dbl();
        double angle = b.dbl();
        if (!b.ok()) return skipped();
        Point3 ax, ay;
        ecs::arbitrary_axis(normal, ax, ay);
        Point3 u = add(scale(ax, std::cos(angle)), scale(ay, std::sin(angle)));
        Point3 v = add(scale(ax, -std::sin(angle)), scale(ay, std::cos(angle)));
        bool full = std::fabs((end - start) - 2.0 * kPi) < 1e-9;
        curve(center, scale(u, major), scale(v, minor), start, end, full);
        drew();
    }

    // LWPLINE: flags, constant width, elevation, thickness, normal, the counts, the points (the first raw, the
    // others against the one before) and the bulges; a bulge is an arc between two points
    void lwpolyline(Bytes& b) {
        int32_t size = b.i32();
        if (!b.ok() || size < 0 || static_cast<size_t>(size) > b.left()) return skipped();
        BitReader r(b.current(), static_cast<size_t>(size));
        int flags = r.bit_short();
        double elevation = 0.0;
        Point3 normal = {0.0, 0.0, 1.0};
        if (flags & 4) r.bit_double();
        if (flags & 8) elevation = r.bit_double();
        if (flags & 2) r.bit_double();
        if (flags & 1) {
            normal.x = r.bit_double();
            normal.y = r.bit_double();
            normal.z = r.bit_double();
        }
        int32_t n = r.bit_long();
        int32_t nb = (flags & 16) ? r.bit_long() : 0;
        int32_t nw = (flags & 32) ? r.bit_long() : 0;
        // a point takes at least 4 bits against the data's bits: a count the data cannot hold is no count
        if (!r.ok() || n < 0 || nb < 0 || nw < 0 || static_cast<uint64_t>(n) > static_cast<uint64_t>(size) * 8 / 4) {
            return skipped();
        }
        std::vector<std::pair<double, double>> pts;
        for (int32_t i = 0; i < n && r.ok(); ++i) {
            if (i == 0) {
                double x = r.raw_double();
                double y = r.raw_double();
                pts.push_back({x, y});
            } else {
                double x = r.default_double(pts.back().first);
                double y = r.default_double(pts.back().second);
                pts.push_back({x, y});
            }
        }
        std::vector<double> bulges;
        for (int32_t i = 0; i < nb && r.ok(); ++i) bulges.push_back(r.bit_double());
        if (!r.ok()) return skipped();
        Point3 ax, ay;
        ecs::arbitrary_axis(normal, ax, ay);
        auto world = [&](double x, double y) { return add(add(scale(ax, x), scale(ay, y)), scale(normal, elevation)); };
        const size_t segments = (flags & 512) ? pts.size() : (pts.empty() ? 0 : pts.size() - 1);
        for (size_t i = 0; i < pts.size(); ++i) add_point(world(pts[i].first, pts[i].second));
        for (size_t i = 0; i < segments; ++i) {
            double bulge = i < bulges.size() ? bulges[i] : 0.0;
            if (bulge == 0.0) continue;
            const auto& p0 = pts[i];
            const auto& p1 = pts[(i + 1) % pts.size()];
            double dx = p1.first - p0.first, dy = p1.second - p0.second;
            double chord = std::sqrt(dx * dx + dy * dy);
            if (chord < 1e-300) continue;
            double theta = 4.0 * std::atan(bulge);              // the arc's sweep (negative: clockwise)
            double radius = chord / (2.0 * std::sin(std::fabs(theta) / 2.0));
            double mx = (p0.first + p1.first) / 2.0, my = (p0.second + p1.second) / 2.0;
            // The center is on the left of the chord for a counterclockwise arc of less than half a turn, on the
            // right for a clockwise one; cos(theta / 2) is negative past half a turn and turns it round
            double h = radius * std::cos(theta / 2.0) * (theta > 0 ? 1.0 : -1.0);
            double cx = mx - dy / chord * h;
            double cy = my + dx / chord * h;
            double a0 = std::atan2(p0.second - cy, p0.first - cx);
            double a1 = a0 + theta;
            if (theta < 0) std::swap(a0, a1);
            curve(world(cx, cy), scale(ax, radius), scale(ay, radius), a0, a1, false);
        }
        drew();
    }

    // TEXT2 (11) and its Unicode form (38): its box in its font, from where the pen starts along its direction
    void text(Bytes& b, bool unicode) {
        Point3 position = b.point();
        Point3 normal = unit(b.point());
        Point3 direction = unit(b.point());
        std::string content = unicode ? b.wide() : b.narrow();
        b.i32();                                // the length
        int32_t raw = b.i32();
        double height = b.dbl();
        double width_factor = b.dbl();
        double oblique = b.dbl();
        b.dbl();                                // tracking
        int backward = b.i32();
        int upside_down = b.i32();
        b.i32();                                // vertical
        b.i32();                                // underline
        b.i32();                                // overline
        std::string face, font, big;
        bool bold = false, italic = false;
        if (unicode) {
            bold = b.i32() != 0;
            italic = b.i32() != 0;
            b.i32();                            // charset
            b.i32();                            // pitch and family
            face = b.wide();
            font = b.wide();
            big = b.wide();
        } else {
            font = b.narrow();
            big = b.narrow();
        }
        if (!b.ok()) {
            // Not all of it: the position is what is known
            add_point(position);
            return skipped();
        }
        std::string utf8 = content;
        if (!unicode) {
            std::string decoded, error;
            if (decode_text(content, code_page_, "", decoded, error)) utf8 = decoded;
        }
        StyleFonts fonts;
        fonts.code_page = code_page_;
        if (!face.empty()) fonts.font = find_font_family(face, bold, italic);
        if (!fonts.font && !font.empty()) fonts.font = find_font(font);
        if (!fonts.font) fonts.font = find_font("simplex.shx");         // AutoCAD's FONTALT
        if (!big.empty()) fonts.big_font = find_font(big);
        if (!fonts.font || height <= 0.0) {
            add_point(position);
            return drew();
        }
        std::u32string chars = raw ? code_points(utf8) : text_characters(utf8, code_page_);
        LineMeasure line = measure_line(fonts, chars, height, width_factor, oblique * 180.0 / kPi);
        if (!line.ink.valid) {
            add_point(position);
            return drew();
        }
        Point3 up = unit(cross(normal, direction));
        double sx = backward ? -1.0 : 1.0, sy = upside_down ? -1.0 : 1.0;
        for (double x : {line.ink.min_x, line.ink.max_x}) {
            for (double y : {line.ink.min_y, line.ink.max_y}) {
                add_point(add(position, add(scale(direction, sx * x), scale(up, sy * y))));
            }
        }
        drew();
    }

    int code_page_;
    std::vector<Affine> stack_;
};

} // namespace

ProxyStreamView decode_proxy_stream(const std::vector<uint8_t>& bytes) {
    ProxyStreamView view;
    if (bytes.size() < 8) {
        view.problem = bytes.empty() ? "the stream is empty" : "the stream is shorter than its header";
        return view;
    }
    const uint32_t total = le32(bytes.data());
    const uint32_t count = le32(bytes.data() + 4);
    size_t at = 8;
    while (at + 8 <= bytes.size() && view.commands.size() < count) {
        uint32_t size = le32(bytes.data() + at);
        if (size < 8 || size > bytes.size() - at) {
            view.problem = "a command's size (" + std::to_string(size) + ") does not fit the stream";
            return view;
        }
        ProxyCommandRef c;
        c.type = le32(bytes.data() + at + 4);
        c.data = bytes.data() + at + 8;
        c.size = size - 8;
        view.commands.push_back(c);
        at += size;
    }
    if (view.commands.size() != count) {
        view.problem = "the stream has " + std::to_string(view.commands.size()) + " commands, its header says " +
                       std::to_string(count);
        return view;
    }
    if (total != bytes.size()) {
        view.problem = "the stream's size (" + std::to_string(total) + ") is not the " + std::to_string(bytes.size()) +
                       " bytes there are";
        return view;
    }
    view.complete = true;
    return view;
}

ProxyDrawn proxy_graphics_drawn(const std::vector<uint8_t>& bytes, int code_page) {
    ProxyStreamView view = decode_proxy_stream(bytes);
    Drawn drawn(code_page);
    for (const ProxyCommandRef& c : view.commands) drawn.command(c);
    return std::move(drawn.out);
}

std::vector<Point3> proxy_graphics_extent_points(const std::vector<uint8_t>& bytes, int code_page) {
    ProxyDrawn drawn = proxy_graphics_drawn(bytes, code_page);
    if (drawn.has_declared) return {drawn.declared_min, drawn.declared_max};
    return std::move(drawn.points);
}

} // namespace internal
} // namespace hdxf
