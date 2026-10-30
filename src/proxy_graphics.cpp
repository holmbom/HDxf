#include "proxy_graphics.hpp"
#include "text_encoding.hpp"
#include "text_layout.hpp"
#include <cmath>
#include <cstring>

namespace hdxf {
namespace internal {

void ProxyGraphics::begin(int type) {
    open_ = data_.size();
    put_int(0);
    put_int(type);
}

void ProxyGraphics::end() {
    auto size = static_cast<int32_t>(data_.size() - open_);
    std::memcpy(&data_[open_], &size, 4);
    count_++;
}

void ProxyGraphics::put_int(int32_t v) {
    uint32_t u = static_cast<uint32_t>(v);
    for (int i = 0; i < 4; ++i) data_.push_back(static_cast<uint8_t>(u >> (8 * i)));
}

void ProxyGraphics::put_double(double v) {
    uint64_t u = 0;
    std::memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i) data_.push_back(static_cast<uint8_t>(u >> (8 * i)));
}

void ProxyGraphics::put_point(const Point3& p) {
    put_double(p.x);
    put_double(p.y);
    put_double(p.z);
}

void ProxyGraphics::put_string(const std::string& s) {
    size_t start = data_.size();
    data_.insert(data_.end(), s.begin(), s.end());
    data_.push_back(0);
    while ((data_.size() - start) % 4) data_.push_back(0);
}

void ProxyGraphics::put_wide_string(const std::string& utf8) {
    size_t start = data_.size();
    auto unit = [&](uint32_t u) {
        data_.push_back(static_cast<uint8_t>(u & 0xFF));
        data_.push_back(static_cast<uint8_t>(u >> 8));
    };
    for (char32_t c : code_points(utf8)) {
        if (c >= 0x10000) {
            uint32_t v = static_cast<uint32_t>(c) - 0x10000;
            unit(0xD800 + (v >> 10));
            unit(0xDC00 + (v & 0x3FF));
        } else {
            unit(static_cast<uint32_t>(c));
        }
    }
    unit(0);
    while ((data_.size() - start) % 4) data_.push_back(0);
}

void ProxyGraphics::circle(const Point3& center, double radius, const Point3& normal) {
    begin(2);
    put_point(center);
    put_double(radius);
    put_point(normal);
    end();
}

void ProxyGraphics::arc(const Point3& center, double radius, const Point3& normal, const Point3& start,
                        double sweep, int arc_type) {
    begin(4);
    put_point(center);
    put_double(radius);
    put_point(normal);
    put_point(start);
    put_double(sweep);
    put_int(arc_type);
    end();
}

void ProxyGraphics::circle3(const Point3& a, const Point3& b, const Point3& c) {
    begin(3);
    put_point(a);
    put_point(b);
    put_point(c);
    end();
}

void ProxyGraphics::arc3(const Point3& a, const Point3& b, const Point3& c) {
    begin(5);
    put_point(a);
    put_point(b);
    put_point(c);
    put_int(0);
    end();
}

void ProxyGraphics::polyline(const std::vector<Point3>& points) {
    begin(6);
    put_int(static_cast<int32_t>(points.size()));
    for (const auto& p : points) put_point(p);
    end();
}

void ProxyGraphics::polygon(const std::vector<Point3>& points) {
    begin(7);
    put_int(static_cast<int32_t>(points.size()));
    for (const auto& p : points) put_point(p);
    end();
}

void ProxyGraphics::mesh(int rows, int columns, const std::vector<Point3>& vertices,
                         const std::vector<int32_t>& traits) {
    begin(8);
    put_int(rows);
    put_int(columns);
    for (const auto& p : vertices) put_point(p);
    for (int32_t v : traits) put_int(v);
    end();
}

void ProxyGraphics::shell(const std::vector<Point3>& vertices, const std::vector<int32_t>& faces,
                          const std::vector<int32_t>& traits) {
    begin(9);
    put_int(static_cast<int32_t>(vertices.size()));
    for (const auto& p : vertices) put_point(p);
    put_int(static_cast<int32_t>(faces.size()));
    for (int32_t v : faces) put_int(v);
    for (int32_t v : traits) put_int(v);
    end();
}

void ProxyGraphics::xline(const Point3& base, const Point3& through) {
    begin(12);
    put_point(base);
    put_point(through);
    end();
}

void ProxyGraphics::ray(const Point3& base, const Point3& through) {
    begin(13);
    put_point(base);
    put_point(through);
    end();
}

void ProxyGraphics::thickness(double thickness) {
    begin(25);
    put_double(thickness);
    end();
}

void ProxyGraphics::polyline_normal(const std::vector<Point3>& points, const Point3& normal) {
    begin(32);
    put_int(static_cast<int32_t>(points.size()));
    for (const auto& p : points) put_point(p);
    put_point(normal);
    end();
}

void ProxyGraphics::lwpolyline(const std::vector<uint8_t>& data) {
    begin(33);
    put_int(static_cast<int32_t>(data.size()));
    data_.insert(data_.end(), data.begin(), data.end());
    for (int i = 0; i < 3; ++i) data_.push_back(0);
    end();
}

void ProxyGraphics::ellipse_arc(const Point3& center, const Point3& normal, double major_radius,
                                double minor_radius, double start, double end_angle, double angle, int arc_type) {
    begin(44);
    put_point(center);
    put_point(normal);
    put_double(major_radius);
    put_double(minor_radius);
    put_double(start);
    put_double(end_angle);
    put_double(angle);
    put_int(arc_type);
    end();
}

// ---------------------------------------------------------------------------
// The DWG bit stream (ODA's specification, "Bit codes"): bits from the high one of each
// byte; a value's bytes little-endian
// ---------------------------------------------------------------------------

namespace {

class BitWriter {
public:
    void bit(int b) {
        if (bits_ % 8 == 0) out_.push_back(0);
        if (b) out_.back() = static_cast<uint8_t>(out_.back() | (0x80 >> (bits_ % 8)));
        ++bits_;
    }
    void bits(unsigned v, int n) {
        for (int i = n - 1; i >= 0; --i) bit((v >> i) & 1);
    }
    void byte(uint8_t b) { bits(b, 8); }
    void raw_double(double v) {
        uint8_t b[8];
        std::memcpy(b, &v, 8);
        for (uint8_t x : b) byte(x);
    }
    // BS: 10 zero, 01 a byte, 11 256, 00 two bytes
    void bit_short(int v) {
        if (v == 0) { bits(2, 2); return; }
        if (v == 256) { bits(3, 2); return; }
        if (v > 0 && v < 256) { bits(1, 2); byte(static_cast<uint8_t>(v)); return; }
        bits(0, 2);
        uint16_t u = static_cast<uint16_t>(v);
        byte(static_cast<uint8_t>(u & 0xFF));
        byte(static_cast<uint8_t>(u >> 8));
    }
    // BL: 10 zero, 01 a byte, 00 four bytes
    void bit_long(int32_t v) {
        if (v == 0) { bits(2, 2); return; }
        if (v > 0 && v < 256) { bits(1, 2); byte(static_cast<uint8_t>(v)); return; }
        bits(0, 2);
        uint32_t u = static_cast<uint32_t>(v);
        for (int i = 0; i < 4; ++i) byte(static_cast<uint8_t>(u >> (8 * i)));
    }
    // BD: 10 zero, 01 one, 00 eight bytes
    void bit_double(double v) {
        if (v == 0.0 && !std::signbit(v)) { bits(2, 2); return; }
        if (v == 1.0) { bits(1, 2); return; }
        bits(0, 2);
        raw_double(v);
    }
    // DD, a double against a default: 00 the default, 01 its bytes 0-3 changed, 10 bytes 4-5
    // then 0-3 changed, 11 eight bytes
    void default_double(double v, double d) {
        uint8_t a[8], b[8];
        std::memcpy(a, &v, 8);
        std::memcpy(b, &d, 8);
        if (std::memcmp(a, b, 8) == 0) { bits(0, 2); return; }
        if (std::memcmp(a + 4, b + 4, 4) == 0) {
            bits(1, 2);
            for (int i = 0; i < 4; ++i) byte(a[i]);
            return;
        }
        if (std::memcmp(a + 6, b + 6, 2) == 0) {
            bits(2, 2);
            byte(a[4]);
            byte(a[5]);
            for (int i = 0; i < 4; ++i) byte(a[i]);
            return;
        }
        bits(3, 2);
        raw_double(v);
    }
    std::vector<uint8_t> bytes() const { return out_; }

private:
    std::vector<uint8_t> out_;
    size_t bits_ = 0;
};

} // namespace

// LWPLINE: flags BS (1 a normal, 2 thickness, 4 a constant width, 8 elevation, 16 bulges,
// 32 widths, 256 linetype generation, 512 closed), constant width BD, elevation BD,
// thickness BD, normal 3BD, the number of points BL, of bulges BL, of widths BL; the first
// point RD RD, the others DD DD against the one before; the bulges BD; the widths BD BD
std::vector<uint8_t> lwpline_data(const LwPolylineData& d, double thickness, const Point3& normal) {
    const auto& v = d.vertices;
    bool constant = !v.empty();
    for (const auto& x : v) {
        constant = constant && x.start_width == v.front().start_width && x.end_width == v.front().start_width;
    }
    double width = constant ? v.front().start_width : 0.0;
    bool widths = false, bulges = false;
    for (const auto& x : v) {
        widths = widths || x.start_width != 0.0 || x.end_width != 0.0;
        bulges = bulges || x.bulge != 0.0;
    }
    widths = widths && !constant;
    bool has_normal = !(normal.x == 0.0 && normal.y == 0.0 && normal.z == 1.0);
    int flags = (has_normal ? 1 : 0) | (thickness != 0.0 ? 2 : 0) | (width != 0.0 ? 4 : 0) |
                (d.elevation != 0.0 ? 8 : 0) | (bulges ? 16 : 0) | (widths ? 32 : 0) |
                ((d.flags & 128) ? 256 : 0) | (d.is_closed() ? 512 : 0);
    BitWriter w;
    w.bit_short(flags);
    if (flags & 4) w.bit_double(width);
    if (flags & 8) w.bit_double(d.elevation);
    if (flags & 2) w.bit_double(thickness);
    if (flags & 1) {
        w.bit_double(normal.x);
        w.bit_double(normal.y);
        w.bit_double(normal.z);
    }
    w.bit_long(static_cast<int32_t>(v.size()));
    if (bulges) w.bit_long(static_cast<int32_t>(v.size()));
    if (widths) w.bit_long(static_cast<int32_t>(v.size()));
    for (size_t i = 0; i < v.size(); ++i) {
        if (i == 0) {
            w.raw_double(v[i].position.x);
            w.raw_double(v[i].position.y);
        } else {
            w.default_double(v[i].position.x, v[i - 1].position.x);
            w.default_double(v[i].position.y, v[i - 1].position.y);
        }
    }
    if (bulges) for (const auto& x : v) w.bit_double(x.bulge);
    if (widths) {
        for (const auto& x : v) {
            w.bit_double(x.start_width);
            w.bit_double(x.end_width);
        }
    }
    return w.bytes();
}

// The text, its length (characters: UTF-16 units, or bytes in the code page), "raw" (1: the
// characters as they are, no %% codes), the format, the font
void ProxyGraphics::text(const ProxyText& t, bool unicode, int code_page) {
    begin(unicode ? 38 : 11);
    put_point(t.position);
    put_point(t.normal);
    put_point(t.direction);
    int32_t length = 0;
    if (unicode) {
        put_wide_string(t.text);
        for (char32_t c : code_points(t.text)) length += c >= 0x10000 ? 2 : 1;
    } else {
        std::string bytes;
        if (!encode_text(t.text, code_page, bytes)) bytes = t.text;
        put_string(bytes);
        length = static_cast<int32_t>(bytes.size());
    }
    put_int(length);
    put_int(1);
    put_double(t.height);
    put_double(t.width_factor);
    put_double(t.oblique);
    put_double(t.tracking);
    put_int(t.backward ? 1 : 0);
    put_int(t.upside_down ? 1 : 0);
    put_int(t.vertical ? 1 : 0);
    put_int(t.underline ? 1 : 0);
    put_int(t.overline ? 1 : 0);
    if (unicode) {
        put_int(t.bold ? 1 : 0);
        put_int(t.italic ? 1 : 0);
        put_int(t.charset);
        put_int(t.pitch_and_family);
        put_wide_string(t.face);
        put_wide_string(t.font);
        put_wide_string(t.big_font);
    } else {
        // The narrow form has no face: AutoCAD writes a TrueType style's family where the
        // font file goes
        std::string font, big;
        const std::string& name = t.font.empty() ? t.face : t.font;
        if (!encode_text(name, code_page, font)) font = name;
        if (!encode_text(t.big_font, code_page, big)) big = t.big_font;
        put_string(font);
        put_string(big);
    }
    end();
}

void ProxyGraphics::color(int aci) {
    begin(14);
    put_int(aci);
    end();
}

void ProxyGraphics::layer(int index) {
    begin(16);
    put_int(index);
    end();
}

void ProxyGraphics::linetype(int index) {
    begin(18);
    put_int(index);
    end();
}

void ProxyGraphics::marker(int id) {
    begin(19);
    put_int(id);
    end();
}

// AcGiFillType: 1 always, 2 never
void ProxyGraphics::fill(bool on) {
    begin(20);
    put_int(on ? 1 : 2);
    end();
}

void ProxyGraphics::true_color(int32_t raw) {
    begin(22);
    put_int(raw);
    end();
}

void ProxyGraphics::lineweight(int32_t lineweight) {
    begin(23);
    put_int(lineweight);
    end();
}

// The matrix row by row
void ProxyGraphics::push_transform(const std::array<double, 16>& matrix) {
    begin(29);
    for (double m : matrix) put_double(m);
    end();
}

void ProxyGraphics::pop_transform() {
    begin(31);
    end();
}

void ProxyGraphics::command_51() {
    begin(51);
    put_int(0);
    end();
}

void ProxyGraphics::linetype_scale(double scale) {
    begin(24);
    put_double(scale);
    end();
}

void ProxyGraphics::command_26(double value) {
    begin(26);
    put_double(value);
    end();
}

void ProxyGraphics::command_34(int64_t value) {
    begin(34);
    put_int(static_cast<int32_t>(value & 0xFFFFFFFF));
    put_int(static_cast<int32_t>((static_cast<uint64_t>(value) >> 32) & 0xFFFFFFFF));
    end();
}

// The value is the last of its four bytes (AutoCAD's 00 00 00 01)
void ProxyGraphics::command_51(int value) {
    begin(51);
    data_.push_back(0);
    data_.push_back(0);
    data_.push_back(0);
    data_.push_back(static_cast<uint8_t>(value));
    end();
}

void ProxyGraphics::append(const std::vector<uint8_t>& stream) {
    if (stream.size() < 8) return;
    auto at = [&](size_t o) {
        uint32_t u = 0;
        for (int i = 0; i < 4; ++i) u |= static_cast<uint32_t>(stream[o + i]) << (8 * i);
        return u;
    };
    uint32_t count = at(4);
    size_t o = 8;
    for (uint32_t i = 0; i < count && o + 8 <= stream.size(); ++i) {
        uint32_t size = at(o);
        if (size < 8 || o + size > stream.size()) break;
        data_.insert(data_.end(), stream.begin() + static_cast<long>(o), stream.begin() + static_cast<long>(o + size));
        ++count_;
        o += size;
    }
}

std::vector<uint8_t> ProxyGraphics::bytes() const {
    std::vector<uint8_t> out;
    out.reserve(data_.size() + 8);
    auto put = [&](uint32_t u) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(u >> (8 * i)));
    };
    put(static_cast<uint32_t>(data_.size() + 8));
    put(static_cast<uint32_t>(count_));
    out.insert(out.end(), data_.begin(), data_.end());
    return out;
}

} // namespace internal
} // namespace hdxf
