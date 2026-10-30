#include "dimension_block.hpp"
#include "dxf_writer.hpp"
#include "dimstyle_legacy.hpp"
#include "header_defaults.hpp"
#include "handles.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hdxf {
namespace internal {

namespace {

// ---------------------------------------------------------------------------
// 2D in the dimension's plane (its OCS)
// ---------------------------------------------------------------------------

struct V2 {
    double x = 0.0, y = 0.0;
};
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator*(V2 a, double s) { return {a.x * s, a.y * s}; }
double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
double cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
double length(V2 a) { return std::sqrt(dot(a, a)); }
V2 unit(V2 a) { double l = length(a); return l > 0.0 ? a * (1.0 / l) : V2{1.0, 0.0}; }
V2 rot90(V2 a) { return {-a.y, a.x}; }
V2 polar(double radians) { return {std::cos(radians), std::sin(radians)}; }
double angle_of(V2 a) { return std::atan2(a.y, a.x); }

// The dimension's plane: OCS ↔ WCS by its extrusion, z its elevation
class Plane {
public:
    explicit Plane(const Point3& extrusion) : n_(extrusion) {
        identity_ = extrusion.x == 0.0 && extrusion.y == 0.0 && extrusion.z == 1.0;
        if (!identity_) {
            ecs::arbitrary_axis(extrusion, ax_, ay_);
            double l = std::sqrt(n_.x * n_.x + n_.y * n_.y + n_.z * n_.z);
            az_ = l > 0.0 ? Point3{n_.x / l, n_.y / l, n_.z / l} : Point3{0.0, 0.0, 1.0};
        }
    }
    // WCS → OCS
    Point3 ocs(const Point3& p) const {
        if (identity_) return p;
        return {p.x * ax_.x + p.y * ax_.y + p.z * ax_.z, p.x * ay_.x + p.y * ay_.y + p.z * ay_.z,
                p.x * az_.x + p.y * az_.y + p.z * az_.z};
    }
    // OCS → WCS
    Point3 wcs(const Point3& p) const {
        if (identity_) return p;
        return {ax_.x * p.x + ay_.x * p.y + az_.x * p.z, ax_.y * p.x + ay_.y * p.y + az_.y * p.z,
                ax_.z * p.x + ay_.z * p.y + az_.z * p.z};
    }
    Point3 wcs_dir(V2 d) const { return wcs({d.x, d.y, 0.0}); }
    bool identity() const { return identity_; }

private:
    Point3 n_;
    bool identity_ = true;
    Point3 ax_, ay_, az_;
};

V2 v2(const Point3& p) { return {p.x, p.y}; }

// An angular dimension's vertex (c), arc point (a), the ends of its lines toward the arc (o1,
// o2), the rays from the vertex that hold the arc between them (r1, r2 at the angles t1, t2)
// and the angle from r1 counterclockwise to r2 through the arc (sweep), in the OCS
struct AngleRays {
    V2 c, a, o1, o2, r1, r2;
    double t1 = 0.0, t2 = 0.0, sweep = 0.0;
};

// Two lines (13→14, 15→10, the arc through 16) or three points (the vertex 15, 13, 14, the
// arc through 10)
bool angle_rays(const Plane& plane, const DimensionData& d, AngleRays& out, std::string& error) {
    V2 c, a, o1, o2, r1, r2;
    if (d.dim_type == DimensionType::Angular3Pt) {
        c = v2(plane.ocs(d.def_point4));
        o1 = v2(plane.ocs(d.def_point2));
        o2 = v2(plane.ocs(d.def_point3));
        a = v2(plane.ocs(d.definition_point));
        r1 = unit(o1 - c);
        r2 = unit(o2 - c);
    } else {
        V2 a0 = v2(plane.ocs(d.def_point2)), a1 = v2(plane.ocs(d.def_point3));
        V2 b0 = v2(plane.ocs(d.def_point4)), b1 = v2(plane.ocs(d.definition_point));
        a = v2(d.arc_point);
        V2 da = a1 - a0, db = b1 - b0;
        double den = cross(da, db);
        if (std::fabs(den) < 1e-15) {
            error = "the lines of an angular dimension are parallel";
            return false;
        }
        c = a0 + da * (cross(b0 - a0, db) / den);
        // The rays from the vertex that hold the arc point between them
        V2 ua = unit(da), ub = unit(db);
        V2 pa = cross(ua, a - c) * cross(ua, ub) >= 0.0 ? ua : ua * -1.0;
        V2 pb = cross(ub, a - c) * cross(ub, ua) >= 0.0 ? ub : ub * -1.0;
        if (dot(a - c, pa) < 0.0 && dot(a - c, pb) < 0.0) { pa = pa * -1.0; pb = pb * -1.0; }
        r1 = pb;
        r2 = pa;
        // The end of each line toward the arc
        o1 = dot(b0 - c, r1) > dot(b1 - c, r1) ? b0 : b1;
        o2 = dot(a0 - c, r2) > dot(a1 - c, r2) ? a0 : a1;
    }
    // Counterclockwise from the first ray through the arc point to the second
    double t1 = angle_of(r1), t2 = angle_of(r2), ta = angle_of(a - c);
    auto ccw = [](double from, double to) {
        double x = std::fmod(to - from, 2.0 * M_PI);
        return x < 0.0 ? x + 2.0 * M_PI : x;
    };
    if (ccw(t1, ta) > ccw(t1, t2)) {
        std::swap(t1, t2);
        std::swap(r1, r2);
        std::swap(o1, o2);
    }
    out.c = c;
    out.a = a;
    out.o1 = o1;
    out.o2 = o2;
    out.r1 = r1;
    out.r2 = r2;
    out.t1 = t1;
    out.t2 = t2;
    out.sweep = ccw(t1, t2);
    return true;
}

// ---------------------------------------------------------------------------
// The dimension style's variables by their group codes (DSTYLE overrides)
// ---------------------------------------------------------------------------

using HandleNames = std::unordered_map<uint64_t, std::string>;

bool name_of(const HandleNames& names, const std::string& handle, std::string& out) {
    uint64_t h = 0;
    if (parse_handle(handle, h) != HandleParse::Ok) return false;
    auto it = names.find(h);
    if (it == names.end()) return false;
    out = it->second;
    return true;
}

// One variable of a DSTYLE override: its DIMSTYLE group code and the value as written
void set_variable(DimStyle& s, int code, const std::string& v, const HandleNames& styles,
                  const HandleNames& blocks, const HandleNames& linetypes) {
    double d = std::atof(v.c_str());
    int i = std::atoi(v.c_str());
    switch (code) {
        case 3: s.dimpost = v; break;
        case 4: s.dimapost = v; break;
        case 5: s.dimblk = v; break;
        case 6: s.dimblk1 = v; break;
        case 7: s.dimblk2 = v; break;
        case 40: s.dimscale = d; break;
        case 41: s.dimasz = d; break;
        case 42: s.dimexo = d; break;
        case 43: s.dimdli = d; break;
        case 44: s.dimexe = d; break;
        case 45: s.dimrnd = d; break;
        case 46: s.dimdle = d; break;
        case 47: s.dimtp = d; break;
        case 48: s.dimtm = d; break;
        case 49: s.dimfxl = d; break;
        case 50: s.dimjogang = d * 180.0 / M_PI; break;     // a dimension's DSTYLE XDATA has it in radians (AutoCAD's LARGE_RADIAL_DIMENSION)
        case 69: s.dimtfill = i; break;
        case 70: s.dimtfillclr = Color::from_aci(i); break;
        case 71: s.dimtol = i; break;
        case 72: s.dimlim = i; break;
        case 73: s.dimtih = i; break;
        case 74: s.dimtoh = i; break;
        case 75: s.dimse1 = i; break;
        case 76: s.dimse2 = i; break;
        case 77: s.dimtad = i; break;
        case 78: s.dimzin = i; break;
        case 79: s.dimazin = i; break;
        case 140: s.dimtxt = d; break;
        case 141: s.dimcen = d; break;
        case 142: s.dimtsz = d; break;
        case 143: s.dimaltf = d; break;
        case 144: s.dimlfac = d; break;
        case 145: s.dimtvp = d; break;
        case 146: s.dimtfac = d; break;
        case 147: s.dimgap = d; break;
        case 148: s.dimaltrnd = d; break;
        case 170: s.dimalt = i; break;
        case 171: s.dimaltd = i; break;
        case 172: s.dimtofl = i; break;
        case 173: s.dimsah = i; break;
        case 174: s.dimtix = i; break;
        case 175: s.dimsoxd = i; break;
        case 176: s.dimclrd = i; break;
        case 177: s.dimclre = i; break;
        case 178: s.dimclrt = i; break;
        case 179: s.dimadec = i; break;
        case 271: s.dimdec = i; break;
        case 272: s.dimtdec = i; break;
        case 273: s.dimaltu = i; break;
        case 274: s.dimalttd = i; break;
        case 270: set_dimunit(s, i); break;     // R13/R14's DIMUNIT and DIMFIT (B91)
        case 287: set_dimfit(s, i); break;
        case 275: s.dimaunit = i; break;
        case 276: s.dimfrac = i; break;
        case 277: s.dimlunit = i; break;
        case 278: s.dimdsep = i; break;
        case 279: s.dimtmove = i; break;
        case 280: s.dimjust = i; break;
        case 281: s.dimsd1 = i; break;
        case 282: s.dimsd2 = i; break;
        case 283: s.dimtolj = i; break;
        case 284: s.dimtzin = i; break;
        case 285: s.dimaltz = i; break;
        case 286: s.dimalttz = i; break;
        case 288: s.dimupt = i; break;
        case 289: s.dimatfit = i; break;
        case 290: s.dimfxlon = i; break;
        case 340: name_of(styles, v, s.dimtxsty); break;
        case 341: name_of(blocks, v, s.dimldrblk); break;
        case 342: name_of(blocks, v, s.dimblk); break;
        case 343: name_of(blocks, v, s.dimblk1); break;
        case 344: name_of(blocks, v, s.dimblk2); break;
        case 345: name_of(linetypes, v, s.dimltype); break;
        case 346: name_of(linetypes, v, s.dimltex1); break;
        case 347: name_of(linetypes, v, s.dimltex2); break;
        case 371: s.dimlwd = i; break;
        case 372: s.dimlwe = i; break;
        default: break;
    }
}

template <typename T>
HandleNames names_of(const std::vector<T>& records) {
    HandleNames m;
    for (const auto& r : records) {
        uint64_t h = 0;
        if (parse_handle(r.handle, h) == HandleParse::Ok) m.emplace(h, r.name);
    }
    return m;
}

// ---------------------------------------------------------------------------
// Numbers as the dimension style writes them
// ---------------------------------------------------------------------------

// A value rounded to a multiple of `step` (DIMRND, DIMALTRND); 0 leaves it
double round_to(double v, double step) {
    if (step <= 0.0) return v;
    return std::floor(v / step + 0.5) * step;
}

// Decimal with `places` digits, the separator, and DIMZIN's 4 (no leading zero) and 8 (no
// trailing zeros)
std::string decimal(double v, int places, char separator, int zin) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", std::max(0, places), v);
    std::string s = buf;
    if (s == "-0" || (s.size() > 1 && s[0] == '-' && s.find_first_not_of("-0.") == std::string::npos)) s.erase(0, 1);
    size_t dot = s.find('.');
    if ((zin & 8) && dot != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        dot = s.find('.');
    }
    if ((zin & 4) && dot != std::string::npos) {
        size_t lead = s[0] == '-' ? 1 : 0;
        if (s.size() > lead + 1 && s[lead] == '0' && s[lead + 1] == '.') s.erase(lead, 1);
    }
    if (dot != std::string::npos) {
        dot = s.find('.');
        if (dot != std::string::npos) s[dot] = separator;
    }
    return s;
}

// A fraction of 1/2^places: its whole part, numerator and denominator (reduced)
void fraction(double v, int places, long& whole, long& num, long& den) {
    den = 1L << std::clamp(places, 0, 8);
    long total = std::lround(std::fabs(v) * static_cast<double>(den));
    whole = total / den;
    num = total % den;
    while (num != 0 && num % 2 == 0 && den > 1) { num /= 2; den /= 2; }
    if (num == 0) den = 1;
}

// A stack at DIMTFAC times the height as AutoCAD writes it: from 2004 always in its own
// height, "{\H1x;\S1#4;}"; in 2000 without one at 1 (ACadSharp's samples of the same
// dimension: 2000 "4'-0\S1/4;\"", 2004 … 2018 "4'-0{\H1x;\S1#4;}\"")
std::string stack_text(const std::string& stack, double tfac, bool r2004) {
    if (!r2004 && tfac == 1.0) return stack;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "{\\H%gx;", tfac);
    return buf + stack + "}";
}

// The stacked fraction as AutoCAD writes it in dimension text: DIMFRAC 0 horizontal "/",
// 1 diagonal "#" (2000 has "/" for both), 2 "1/4" not stacked
std::string fraction_text(long num, long den, int dimfrac, double tfac, bool r2004) {
    std::string n = std::to_string(num), d = std::to_string(den);
    if (dimfrac == 2) return n + "/" + d;
    std::string stack = "\\S" + n + (dimfrac == 1 && r2004 ? "#" : "/") + d + ";";
    return stack_text(stack, tfac, r2004);
}

// A length in the style's units (DIMLUNIT): 1 scientific, 2 decimal, 3 engineering, 4
// architectural, 5 fractional, 6 Windows desktop (decimal). zin: DIMZIN (or DIMALTZ,
// DIMTZIN) — bits 0-1 the feet and inches zeros, 4 leading, 8 trailing.
std::string format_length(double v, int unit, int places, char separator, int zin, int dimfrac, double tfac,
                          bool r2004) {
    bool negative = v < 0.0;
    double a = std::fabs(v);
    std::string sign = negative ? "-" : "";
    switch (unit) {
        case 1: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.*E", std::max(0, places), a);
            std::string s = buf;
            size_t dot = s.find('.');
            if (dot != std::string::npos) s[dot] = separator;
            return sign + s;
        }
        case 3: case 4: {
            // Feet and inches (the drawing unit is the inch)
            long feet = static_cast<long>(std::floor(a / 12.0 + 1e-12));
            double inches = a - 12.0 * static_cast<double>(feet);
            std::string inch_text;
            bool zero_inches;
            if (unit == 3) {
                inch_text = decimal(inches, places, separator, zin & 12);
                double rounded = std::atof(decimal(inches, places, '.', 0).c_str());
                if (rounded >= 12.0) { ++feet; inches = 0.0; inch_text = decimal(0.0, places, separator, zin & 12); rounded = 0.0; }
                zero_inches = rounded == 0.0;
            } else {
                long whole, num, den;
                fraction(inches, places, whole, num, den);
                if (whole >= 12) { ++feet; whole -= 12; }
                inch_text = std::to_string(whole);
                // Stacked right after the whole inches, not stacked after a space
                if (num != 0) inch_text += (dimfrac == 2 ? " " : "") + fraction_text(num, den, dimfrac, tfac, r2004);
                zero_inches = whole == 0 && num == 0;
            }
            int mode = zin & 3;     // 0 no 0' nor 0", 1 both, 2 0' only, 3 0" only
            bool show_feet = feet != 0 || mode == 1 || mode == 2;
            bool show_inches = !zero_inches || mode == 1 || mode == 3 || (!show_feet);
            std::string out = sign;
            if (show_feet) out += std::to_string(feet) + "'";
            if (show_feet && show_inches) out += "-";
            if (show_inches) out += inch_text + "\"";
            return out;
        }
        case 5: {
            long whole, num, den;
            fraction(a, places, whole, num, den);
            std::string out = sign;
            if (whole != 0 || num == 0) out += std::to_string(whole);
            if (num != 0) out += (whole != 0 && dimfrac == 2 ? " " : "") + fraction_text(num, den, dimfrac, tfac, r2004);
            return out;
        }
        default:
            return (negative ? "-" : "") + decimal(a, places, separator, zin);
    }
}

// An angle in the style's angular units (DIMAUNIT): 0 decimal degrees, 1 degrees minutes
// seconds, 2 grads, 3 radians; DIMAZIN 1 no leading zeros, 2 no trailing zeros
std::string format_angle(double radians, int unit, int places, char separator, int azin) {
    int zin = ((azin & 1) ? 4 : 0) | ((azin & 2) ? 8 : 0);
    double degrees = radians * 180.0 / M_PI;
    switch (unit) {
        case 1: {
            // d°m's" with as many parts as the precision asks: 0 degrees, 1-2 minutes, 3-4
            // seconds, more decimal seconds
            double total = std::fabs(degrees);
            if (places <= 0) return decimal(total, 0, separator, 0) + "°";
            long d = static_cast<long>(std::floor(total));
            double rest = (total - static_cast<double>(d)) * 60.0;
            if (places <= 2) {
                long m = std::lround(rest);
                if (m == 60) { ++d; m = 0; }
                return std::to_string(d) + "°" + std::to_string(m) + "'";
            }
            long m = static_cast<long>(std::floor(rest));
            double s = (rest - static_cast<double>(m)) * 60.0;
            int sp = places <= 4 ? 0 : places - 4;
            std::string st = decimal(s, sp, separator, zin & 8);
            if (std::atof(decimal(s, sp, '.', 0).c_str()) >= 60.0) { ++m; st = decimal(0.0, sp, separator, zin & 8); }
            if (m >= 60) { ++d; m -= 60; }
            return std::to_string(d) + "°" + std::to_string(m) + "'" + st + "\"";
        }
        case 2: return decimal(degrees * 400.0 / 360.0, places, separator, zin) + "g";
        case 3: return decimal(radians, places, separator, zin) + "r";
        default: return decimal(degrees, places, separator, zin) + "°";
    }
}

// Replaces "<>" in a pattern (DIMPOST, the text override) by the measurement; a pattern
// without "<>" is a suffix (DIMPOST) or the whole text (override)
std::string with_measurement(const std::string& pattern, const std::string& value, bool suffix) {
    size_t at = pattern.find("<>");
    if (at == std::string::npos) return suffix ? value + pattern : pattern;
    return pattern.substr(0, at) + value + pattern.substr(at + 2);
}

// ---------------------------------------------------------------------------
// Built-in arrowheads
// ---------------------------------------------------------------------------

struct ArrowInfo {
    const char* name;
    bool trims;             // the dimension line stops one arrow size short (its body)
};

// AutoCAD's built-in arrowheads; whether the dimension line stops at the arrow's back
// (ODA File Converter's dimension blocks for each, which match AutoCAD's)
const ArrowInfo kArrows[] = {
    {"_ClosedFilled", true}, {"_ClosedBlank", true}, {"_Closed", true}, {"_Dot", true},
    {"_ArchTick", false}, {"_Oblique", false}, {"_Open", true}, {"_Origin", true},
    {"_Origin2", true}, {"_Open90", true}, {"_Open30", true}, {"_DotSmall", false},
    {"_DotBlank", true}, {"_Small", false}, {"_BoxBlank", true}, {"_BoxFilled", true},
    {"_DatumBlank", true}, {"_DatumFilled", true}, {"_Integral", false}, {"_None", false},
};

const ArrowInfo* arrow_info(const std::string& name) {
    for (const auto& a : kArrows) {
        if (names_equal(a.name, name)) return &a;
    }
    return nullptr;
}

} // namespace

// R13 and R14 name the built-in arrowheads in DIMBLK without 2000's underscore (R12 too, in
// AutoCAD's file: DIMBLK "ARCHTICK", the block `_ARCHTICK`): a name that is none of the built-ins,
// is no block of the drawing and is one with the underscore, is that one
std::string builtin_arrow_name(const Drawing& dwg, const std::string& name, DxfVersion version) {
    if ((version != DxfVersion::R13 && version != DxfVersion::R14) || name.empty()) return name;
    if (arrow_info(name) != nullptr || dwg.find_block(name) != nullptr) return name;
    const ArrowInfo* info = arrow_info("_" + name);
    return info ? info->name : name;
}

bool arrow_trims(const std::string& name) {
    if (name.empty()) return true;
    const ArrowInfo* info = arrow_info(name);
    return info ? info->trims : true;
}

bool is_builtin_arrow(const std::string& name) {
    return arrow_info(name) != nullptr && !names_equal(name, "_ClosedFilled");
}

// The blocks AutoCAD makes for its arrowheads, as in its files (ACadSharp's _ArchTick,
// _BoxBlank) and ODA File Converter's for all of them: one unit long, the tip at the
// origin pointing along +X, everything ByBlock
Block builtin_arrow_block(const std::string& name) {
    Block b;
    const ArrowInfo* info = arrow_info(name);
    b.name = info ? info->name : name;
    b.layer = "0";
    auto props = [](Entity& e, bool lineweight) {
        e.props.layer = "0";
        e.props.linetype = "ByBlock";
        e.props.color = Color::by_block();
        e.props.lineweight = lineweight ? -2 : -1;
    };
    auto line = [&](double x0, double y0, double x1, double y1) {
        Entity e;
        e.type = EntityType::Line;
        e.data = LineData{{x0, y0, 0.0}, {x1, y1, 0.0}};
        props(e, true);
        b.entities.push_back(std::move(e));
    };
    auto circle = [&](double r) {
        Entity e;
        e.type = EntityType::Circle;
        CircleData c;
        c.radius = r;
        e.data = c;
        props(e, true);
        b.entities.push_back(std::move(e));
    };
    auto arc = [&](double cx, double cy, double r, double a0, double a1) {
        Entity e;
        e.type = EntityType::Arc;
        ArcData a;
        a.center = {cx, cy, 0.0};
        a.radius = r;
        a.start_angle = a0;
        a.end_angle = a1;
        e.data = a;
        props(e, true);
        b.entities.push_back(std::move(e));
    };
    auto solid = [&](V2 p0, V2 p1, V2 p2, V2 p3) {
        Entity e;
        e.type = EntityType::Solid;
        e.data = SolidData{{p0.x, p0.y, 0.0}, {p1.x, p1.y, 0.0}, {p2.x, p2.y, 0.0}, {p3.x, p3.y, 0.0}};
        props(e, false);
        b.entities.push_back(std::move(e));
    };
    // A wide polyline (the dots, the architectural tick); closed: two half circles
    auto wide = [&](double width, std::vector<LwVertex> vertices, bool closed) {
        Entity e;
        e.type = EntityType::LwPolyline;
        LwPolylineData p;
        p.flags = closed ? 1 : 0;
        for (auto& v : vertices) {
            v.start_width = width;
            v.end_width = width;
        }
        p.vertices = std::move(vertices);
        e.data = std::move(p);
        props(e, false);
        b.entities.push_back(std::move(e));
    };
    auto dot = [&](double r) {
        LwVertex a, c;
        a.position = {-r, 0.0};
        a.bulge = 1.0;
        c.position = {r, 0.0};
        c.bulge = 1.0;
        wide(2.0 * r, {a, c}, true);
    };
    const double third = 0.166667, open30 = 0.267949, datum = 0.57735;
    const std::string n = b.name;
    if (n == "_ClosedBlank" || n == "_Closed") {
        line(-1.0, third, 0.0, 0.0);
        line(0.0, 0.0, -1.0, -third);
        line(-1.0, third, -1.0, -third);
        if (n == "_Closed") line(0.0, 0.0, -1.0, 0.0);
    } else if (n == "_Dot") {
        dot(0.25);
        line(-0.5, 0.0, -1.0, 0.0);
    } else if (n == "_ArchTick") {
        LwVertex a, c;
        a.position = {-0.5, -0.5};
        c.position = {0.5, 0.5};
        wide(0.15, {a, c}, false);
    } else if (n == "_Oblique") {
        line(-0.5, -0.5, 0.5, 0.5);
    } else if (n == "_Open") {
        line(-1.0, third, 0.0, 0.0);
        line(-1.0, -third, 0.0, 0.0);
        line(0.0, 0.0, -1.0, 0.0);
    } else if (n == "_Origin") {
        circle(0.5);
        line(0.0, 0.0, -1.0, 0.0);
    } else if (n == "_Origin2") {
        circle(0.5);
        circle(0.25);
        line(-0.5, 0.0, -1.0, 0.0);
    } else if (n == "_Open90") {
        line(-0.5, 0.5, 0.0, 0.0);
        line(0.0, 0.0, -0.5, -0.5);
        line(0.0, 0.0, -1.0, 0.0);
    } else if (n == "_Open30") {
        line(-1.0, open30, 0.0, 0.0);
        line(0.0, 0.0, -1.0, -open30);
        line(0.0, 0.0, -1.0, 0.0);
    } else if (n == "_DotSmall") {
        dot(0.0625);
    } else if (n == "_DotBlank") {
        circle(0.5);
        line(-0.5, 0.0, -1.0, 0.0);
    } else if (n == "_Small") {
        circle(0.25);
    } else if (n == "_BoxBlank") {
        line(-0.5, -0.5, 0.5, -0.5);
        line(0.5, -0.5, 0.5, 0.5);
        line(0.5, 0.5, -0.5, 0.5);
        line(-0.5, 0.5, -0.5, -0.5);
        line(-0.5, 0.0, -1.0, 0.0);
    } else if (n == "_BoxFilled") {
        solid({-0.5, 0.5}, {0.5, 0.5}, {-0.5, -0.5}, {0.5, -0.5});
        line(-0.5, 0.0, -1.0, 0.0);
    } else if (n == "_DatumBlank") {
        line(0.0, datum, -1.0, 0.0);
        line(-1.0, 0.0, 0.0, -datum);
        line(0.0, -datum, 0.0, datum);
    } else if (n == "_DatumFilled") {
        solid({0.0, datum}, {-1.0, 0.0}, {0.0, -datum}, {0.0, -datum});
    } else if (n == "_Integral") {
        arc(0.444888, -0.091335, 0.45416667, 102.0, 168.0);
        arc(-0.444888, 0.091335, 0.45416667, 282.0, 348.0);
    }
    return b;
}

DimStyle default_standard_dim_style() {
    DimStyle s;
    s.dimtad = 0;       // as the writers write Standard
    return s;
}

DimStyle dimension_style(const Drawing& dwg, const Entity& dimension) {
    const DimensionData& d = *dimension.dimension_part();
    DimStyle s = default_standard_dim_style();
    bool found = false;
    for (const auto& ds : dwg.dim_styles()) {
        if (names_equal(ds.name, d.dim_style)) { s = ds; found = true; break; }
    }
    if (!found) {
        for (const auto& ds : dwg.dim_styles()) {
            if (names_equal(ds.name, "Standard")) { s = ds; break; }
        }
    }
    HandleNames styles = names_of(dwg.text_styles());
    HandleNames linetypes = names_of(dwg.linetypes());
    HandleNames blocks;
    for (const auto& b : dwg.blocks()) {
        uint64_t h = 0;
        if (parse_handle(b.record.handle, h) == HandleParse::Ok) blocks.emplace(h, b.name);
    }
    for (const auto& list : dimension.props.xdata) {
        const auto& x = list.items;
        if (names_equal(list.app_name, "ACAD")) {
            bool in = false;
            for (size_t i = 0; i < x.size(); ++i) {
                if (x[i].code == 1000 && x[i].value == "DSTYLE") { in = true; continue; }
                if (!in) continue;
                if (x[i].code == 1002 && x[i].value == "}") break;
                if (x[i].code != 1070 || i + 1 >= x.size()) continue;
                int code = std::atoi(x[i].value.c_str());
                set_variable(s, code, x[i + 1].value, styles, blocks, linetypes);
                ++i;
            }
            continue;
        }
        // The newer variables as a style keeps them in 2000/2004 (ACAD_DSTYLE_…)
        if (x.size() >= 2 && x[0].code == 1070) {
            int number = std::atoi(x[0].value.c_str());
            const std::string& v = x[1].value;
            switch (number) {
                case 376:
                    s.dimtfill = std::atoi(v.c_str());
                    break;
                case 380: name_of(linetypes, v, s.dimltype); break;
                case 381: name_of(linetypes, v, s.dimltex1); break;
                case 382: name_of(linetypes, v, s.dimltex2); break;
                case 383: s.dimfxlon = std::atoi(v.c_str()); break;
                case 378: s.dimfxl = std::atof(v.c_str()); break;
                case 384: s.dimjogang = std::atof(v.c_str()) * 180.0 / M_PI; break;
                default: break;
            }
        }
    }
    return s;
}

std::string dimension_text(const DimStyle& s, const DimensionData& d, double measurement, bool paper_space,
                           DxfVersion version) {
    bool r2004 = version >= DxfVersion::R2004;
    char sep = static_cast<char>(s.dimdsep != 0 ? s.dimdsep : '.');
    bool angular = d.dim_type == DimensionType::Angular || d.dim_type == DimensionType::Angular3Pt;
    std::string value;
    if (angular) {
        value = format_angle(measurement, s.dimaunit, s.dimadec, sep, s.dimazin);
        // AutoCAD's dimension text has the degree sign as the escape \U+00B0 in it — which
        // 2007 (UTF-8) shows: 94\U+00B0 in ACadSharp's sample_AC1021 … AC1032, while the
        // diameter sign is the character (plan phase 3, B31)
        if (version >= DxfVersion::R2007) {
            std::string escaped;
            for (size_t i = 0; i < value.size(); ++i) {
                if (value.compare(i, 2, "\xC2\xB0") == 0) {
                    escaped += "\\U+00B0";
                    ++i;
                } else {
                    escaped += value[i];
                }
            }
            value = escaped;
        }
    } else {
        // DIMLFAC scales lengths; a negative one only in paper space
        double factor = s.dimlfac;
        if (factor < 0.0) factor = paper_space ? -factor : 1.0;
        double v = round_to(measurement * factor, s.dimrnd);
        value = format_length(v, s.dimlunit, s.dimdec, sep, s.dimzin, s.dimfrac, s.dimtfac, r2004);
    }
    // Prefix: R for a radius, ∅ for a diameter — unless DIMPOST has one
    std::string prefix;
    if (d.dim_type == DimensionType::Radius) prefix = "R";
    if (d.dim_type == DimensionType::Diameter) prefix = "∅";
    std::string primary;
    if (!angular && s.dimpost.find("<>") != std::string::npos && s.dimpost.find("<>") > 0) {
        primary = with_measurement(s.dimpost, value, true);
    } else {
        primary = prefix + with_measurement(s.dimpost, value, true);
    }
    // Tolerances (DIMTOL) or limits (DIMLIM), stacked at DIMTFAC
    if (!angular && (s.dimtol || s.dimlim)) {
        double factor = s.dimlfac < 0.0 ? (paper_space ? -s.dimlfac : 1.0) : s.dimlfac;
        auto tol = [&](double v) {
            return format_length(v, s.dimlunit, s.dimtdec, sep, s.dimtzin, s.dimfrac, 1.0, r2004);
        };
        // Tolerances and limits stacked at DIMTFAC times the height
        auto stacked = [&](const std::string& top, const std::string& bottom) {
            // As a fraction is written (the same \S code; AutoCAD's 2004 tolerances are not in
            // its files — a checkpoint)
            return stack_text("\\S" + top + "^" + bottom + ";", s.dimtfac, r2004);
        };
        if (s.dimlim) {
            double v = measurement * factor;
            primary = stacked(tol(v + s.dimtp), tol(v - s.dimtm));
        } else if (s.dimtp == s.dimtm) {
            primary += "%%p" + tol(s.dimtp);
        } else {
            std::string plus = s.dimtp >= 0.0 ? "+" + tol(s.dimtp) : tol(s.dimtp);
            std::string minus = s.dimtm > 0.0 ? "-" + tol(s.dimtm) : (s.dimtm == 0.0 ? tol(0.0) : "+" + tol(-s.dimtm));
            primary += stacked(plus, minus);
        }
    }
    // Alternate units in brackets (DIMALT)
    if (s.dimalt && !angular) {
        double alt = round_to(measurement * s.dimaltf * (s.dimlfac < 0.0 ? (paper_space ? -s.dimlfac : 1.0) : s.dimlfac),
                              s.dimaltrnd);
        int unit = s.dimaltu;
        if (unit == 6) unit = 4;            // architectural (not stacked)
        else if (unit == 7) unit = 5;       // fractional (not stacked)
        else if (unit == 8) unit = 2;       // Windows desktop
        std::string a = format_length(alt, unit, s.dimaltd, sep, s.dimaltz, s.dimaltu >= 6 ? 2 : s.dimfrac, 1.0, r2004);
        std::string pattern = s.dimapost;
        primary += " [" + with_measurement(pattern, a, true) + "]";
    }
    // The text override: "<>" is the measurement, " " none
    std::string text;
    if (d.text_override.empty()) text = primary;
    else if (d.text_override == " ") text = "";
    else text = with_measurement(d.text_override, primary, false);
    return text;
}

namespace {

// What an entity of the block draws: its colour, lineweight and linetype come from the
// style (DIMCLRD/DIMLWD/DIMLTYPE, DIMCLRE/DIMLWE/DIMLTEX1-2, DIMCLRT)
enum class Part { DimLine, Ext1, Ext2, Arrow, Text, Point };

// Where the text goes and how it is turned
struct TextPlace {
    V2 center;
    double angle = 0.0;     // radians
};

class Builder {
public:
    Builder(const Drawing& dwg, TextLayout& text, const Entity& e, DxfVersion version, DimensionBuild& out)
        : dwg_(dwg), text_(text), e_(e), d_(*e.dimension_part()), version_(version), out_(out),
          plane_(e.props.extrusion) {
        s_ = dimension_style(dwg, e);
        // R13 and R14 (step 7.16): their DIMFIT 0 … 3 are 2000's DIMTMOVE 0 in the style's numbers (B91), but R14 has
        // no dimension line that follows a moved text: AutoCAD's own R14 block of a dimension whose text was
        // put 10 units off its line (BL15-1, `*D10`, DIMFIT 0) keeps the line where 10 is. So the text goes where
        // it was put (DIMTMOVE 2) and 10 stays.
        if ((version == DxfVersion::R13 || version == DxfVersion::R14) && s_.dimtmove == 0) s_.dimtmove = 2;
        double scale = s_.dimscale > 0.0 ? s_.dimscale : 1.0;
        asz_ = s_.dimasz * scale;
        exo_ = s_.dimexo * scale;
        exe_ = s_.dimexe * scale;
        gap_ = std::fabs(s_.dimgap) * scale;
        txt_ = s_.dimtxt * scale;
        dle_ = s_.dimdle * scale;
        tsz_ = s_.dimtsz * scale;
        fxl_ = s_.dimfxl * scale;
        cen_ = s_.dimcen * scale;
        arrow1_ = builtin_arrow_name(dwg, s_.dimsah ? s_.dimblk1 : s_.dimblk, version);
        arrow2_ = builtin_arrow_name(dwg, s_.dimsah ? s_.dimblk2 : s_.dimblk, version);
        elevation_ = plane_.ocs(d_.definition_point).z;
    }

    bool build(std::string& error) {
        // Step 9.17: the arc length is the three-point angular dimension's geometry with its own text and symbol; the large
        // radius is a radius with the jog
        if (e_.type == EntityType::ArcDimension) return angular(error);
        if (e_.type == EntityType::LargeRadialDimension) return large_radial(error);
        switch (d_.dim_type) {
            case DimensionType::Rotated:
            case DimensionType::Aligned:
                return linear(error);
            case DimensionType::Angular:
            case DimensionType::Angular3Pt:
                return angular(error);
            case DimensionType::Radius:
            case DimensionType::Diameter:
                return radial(error);
            case DimensionType::Ordinate:
                return ordinate(error);
        }
        error = "unknown dimension type";
        return false;
    }

private:
    // --- Entities -----------------------------------------------------------------

    Point3 wcs(V2 p) const { return plane_.wcs({p.x, p.y, elevation_}); }

    void props(Entity& e, Part part) const {
        EntityProperties& p = e.props;
        p.layer = e_.props.layer;
        p.extrusion = e_.props.extrusion;
        if (version_ >= DxfVersion::R2004) p.transparency = Transparency::by_block();
        switch (part) {
            case Part::DimLine:
            case Part::Arrow:
                p.color = Color::from_aci(s_.dimclrd);
                p.lineweight = s_.dimlwd;
                if (part == Part::DimLine && !s_.dimltype.empty()) p.linetype = s_.dimltype;
                break;
            case Part::Ext1:
            case Part::Ext2:
                p.color = Color::from_aci(s_.dimclre);
                p.lineweight = s_.dimlwe;
                if (part == Part::Ext1 && !s_.dimltex1.empty()) p.linetype = s_.dimltex1;
                if (part == Part::Ext2 && !s_.dimltex2.empty()) p.linetype = s_.dimltex2;
                break;
            case Part::Text:
                p.color = Color::from_aci(s_.dimclrt);
                break;
            case Part::Point:
                p.layer = "Defpoints";
                p.color = Color::by_block();
                break;
        }
        // R13 and R14 (step 7.16): the entities of AutoCAD's own R14 blocks (BL15-1, `*D0` …) are on
        // layer 0 and ByBlock in linetype and, by DIMCLRD, DIMCLRE and DIMCLRT 0, in color — the
        // dimension's layer is the INSERT's; no lineweight (370 is 2000's)
        if (version_ == DxfVersion::R13 || version_ == DxfVersion::R14) {
            if (part != Part::Point) p.layer = "0";
            p.linetype = "BYBLOCK";
            p.lineweight = -1;
        }
    }

    // A line of the block; LINE is in the WCS
    void line(V2 a, V2 b, Part part) {
        Entity e;
        e.type = EntityType::Line;
        e.data = LineData{wcs(a), wcs(b)};
        props(e, part);
        out_.entities.push_back(std::move(e));
    }

    // An arc in the OCS, angles in degrees
    void arc(V2 c, double r, double a0, double a1, Part part) {
        Entity e;
        e.type = EntityType::Arc;
        ArcData a;
        a.center = {c.x, c.y, elevation_};
        a.radius = r;
        a.start_angle = a0;
        a.end_angle = a1;
        e.data = a;
        props(e, part);
        out_.entities.push_back(std::move(e));
    }

    // An arrowhead with its tip at `tip`, pointing along `out` (from its body to its tip).
    // The closed filled one is a SOLID, its first corner on the `side` of `out` (+1 left,
    // −1 right; AutoCAD's order in ACadSharp's samples); the others an INSERT of their
    // block, scaled by DIMASZ.
    void arrow(V2 tip, V2 out, const std::string& name, int side) {
        if (names_equal(name, "_None")) return;
        if (name.empty() || names_equal(name, "_ClosedFilled")) {
            V2 base = tip - out * asz_;
            V2 half = rot90(out) * (asz_ / 6.0 * side);
            Entity e;
            e.type = EntityType::Solid;
            // SOLID's corners are in its OCS, the dimension's
            auto ocs = [&](V2 p) { return Point3{p.x, p.y, elevation_}; };
            e.data = SolidData{ocs(base + half), ocs(base - half), ocs(tip), ocs(tip)};
            props(e, Part::Arrow);
            e.props.lineweight = -1;
            out_.entities.push_back(std::move(e));
            return;
        }
        Entity e;
        e.type = EntityType::Insert;
        InsertData ins;
        ins.block_name = name;
        if (const ArrowInfo* info = arrow_info(name)) {
            ins.block_name = info->name;
            if (std::find(out_.arrows.begin(), out_.arrows.end(), ins.block_name) == out_.arrows.end()) {
                out_.arrows.push_back(ins.block_name);
            }
        }
        ins.insertion_point = {tip.x, tip.y, elevation_};
        ins.x_scale = ins.y_scale = ins.z_scale = asz_;
        double deg = angle_of(out) * 180.0 / M_PI;
        if (deg < 0.0) deg += 360.0;
        ins.rotation = deg;
        e.data = ins;
        props(e, Part::Arrow);
        out_.entities.push_back(std::move(e));
    }

    // Whether an arrow stops the dimension line one arrow size short of its tip
    static bool trims(const std::string& name) { return arrow_trims(name); }

    // The text's MTEXT (no font: its size is 0)
    MTextData text_data(const std::string& content, const TextPlace& place) const {
        MTextData m;
        Point3 c = wcs(place.center);
        m.insertion_point = c;
        m.height = txt_;
        m.reference_width = 0.0;
        m.attachment = 5;
        m.drawing_direction = 1;
        m.content = content;
        m.style = s_.dimtxsty.empty() ? std::string("Standard") : s_.dimtxsty;
        m.x_axis_direction = plane_.wcs_dir(polar(place.angle));
        m.line_spacing_style = 1;
        m.line_spacing_factor = 1.0;
        return m;
    }

    MTextSize measure(const std::string& content) {
        MTextData m;
        m.content = content;
        m.height = txt_;
        m.attachment = 5;
        m.style = s_.dimtxsty.empty() ? std::string("Standard") : s_.dimtxsty;
        return text_.mtext(m);
    }

    void text(const std::string& content, const TextPlace& place) {
        if (content.empty()) return;
        if (version_ == DxfVersion::R12) {
            // R12 has no MTEXT: a TEXT per line, where the MTEXT's line would be (as
            // AutoCAD and ODA File Converter write a dimension's MTEXT in R12)
            MTextSize size = measure(content);
            V2 t = polar(place.angle), up = rot90(t);
            std::string style = s_.dimtxsty.empty() ? std::string("Standard") : s_.dimtxsty;
            if (!size.font) {
                // No font to lay it out (a warning says so): one TEXT, centered on the place
                std::string all;
                for (const auto& p : text_.mtext_paragraphs(content, style)) all += (all.empty() ? "" : " ") + p;
                Entity e;
                e.type = EntityType::Text;
                TextData td;
                td.position = td.alignment_point = {place.center.x, place.center.y, elevation_};
                td.h_justify = 4;
                td.height = txt_;
                td.content = all;
                td.rotation = place.angle * 180.0 / M_PI;
                td.style = style;
                e.data = std::move(td);
                props(e, Part::Text);
                out_.entities.push_back(std::move(e));
                return;
            }
            for (const auto& l : size.text_lines) {
                Entity e;
                e.type = EntityType::Text;
                TextData td;
                V2 p = place.center + t * l.x + up * l.baseline;
                td.position = {p.x, p.y, elevation_};
                td.height = l.height;
                td.content = l.text;
                td.rotation = place.angle * 180.0 / M_PI;
                td.style = s_.dimtxsty.empty() ? std::string("Standard") : s_.dimtxsty;
                e.data = std::move(td);
                props(e, Part::Text);
                out_.entities.push_back(std::move(e));
            }
            return;
        }
        Entity e;
        e.type = EntityType::MText;
        // The text's vertical alignment is DIMTOLJ (\A1 middle, \A2 top; bottom, 0, is not
        // written): ISO-25 (DIMTOLJ 0) has none in ACadSharp's samples, the others \A1
        std::string prefix = s_.dimtolj != 0 ? "\\A" + std::to_string(s_.dimtolj) + ";" : std::string();
        MTextData m = text_data(prefix + content, place);
        // The text's background (DIMTFILL): 1 the drawing's, 2 DIMTFILLCLR, its box 1.25 —
        // MTEXT's own from 2004, only XDATA in 2000
        int fill_flags = s_.dimtfill == 1 ? 3 : 1;
        Color fill = s_.dimtfill == 1 ? Color::by_block() : s_.dimtfillclr;
        if (s_.dimtfill != 0 && version_ >= DxfVersion::R2004) {
            m.background.flags = fill_flags;
            m.background.color = fill;
            m.background.scale = 1.25;
        }
        e.data = m;
        props(e, Part::Text);
        // (R13 and R14: none — family N's MTEXT in the dimension blocks has no XDATA, BL15-1)
        if (version_ >= DxfVersion::R2000 && version_ < DxfVersion::R2004) {
            // AutoCAD 2000 writes the line spacing as XDATA on a dimension's MTEXT
            XdataList x{"ACAD", {{1000, "MTEXTBEGIN"}, {1070, "73"}, {1070, "1"}, {1070, "44"},
                                 {1040, "1.0"}, {1070, "74"}, {1070, "0"}, {1000, "MTEXTEND"}}};
            if (s_.dimtfill != 0) {
                // And its background (DIMTFILL), which 2000 has no group codes for: AutoCAD's
                // custom_dim_style dimensions in ACadSharp's sample_AC1015 — the flags (91),
                // the box (46), the color as AcCmColor's 0xMMRRGGBB (64), its names (65, 66),
                // the transparency (442)
                uint32_t packed = fill.has_rgb() ? 0xC2000000u | static_cast<uint32_t>(fill.rgb)
                                : fill.aci == 0 ? 0xC1000000u
                                : fill.aci == 256 ? 0xC0000000u
                                : 0xC3000000u | static_cast<uint32_t>(fill.aci & 0xFF);
                std::vector<XdataItem> bg = {
                    {1000, "ACAD_MTEXT_BBRT"}, {1070, "91"}, {1071, std::to_string(fill_flags)},
                    {1070, "46"}, {1040, "1.25"}, {1070, "64"}, {1071, std::to_string(static_cast<int32_t>(packed))},
                    {1070, "65"}, {1000, fill.name}, {1070, "66"}, {1000, ""}, {1070, "442"}, {1071, "0"},
                    {1000, "ACAD_MTEXT_BERT"}};
                x.items.insert(x.items.end(), bg.begin(), bg.end());
            }
            e.props.xdata.push_back(std::move(x));
        }
        out_.entities.push_back(std::move(e));
    }

    void point(const Point3& p) {
        Entity e;
        e.type = EntityType::Point;
        PointData d;
        d.location = p;
        e.data = d;
        props(e, Part::Point);
        out_.entities.push_back(std::move(e));
    }

    // --- Text ---------------------------------------------------------------------

    // An angle turned to read left to right (in (−90°, 90°])
    static double readable(double a) {
        while (a > M_PI / 2.0 + 1e-9) a -= M_PI;
        while (a <= -M_PI / 2.0 + 1e-9) a += M_PI;
        return a;
    }

    // The text box (its MTEXT, attachment middle center) widened by the gap: the parts of
    // the segment a → b outside it
    std::vector<std::pair<V2, V2>> outside_box(V2 a, V2 b, const TextPlace& place, double w, double h) const {
        V2 t = polar(place.angle), u = rot90(t);
        double hw = w / 2.0 + gap_, hh = h / 2.0 + gap_;
        // The segment in the box's frame: a + (b − a)·s, s in [0, 1]
        V2 pa = a - place.center, pb = b - place.center;
        V2 la{dot(pa, t), dot(pa, u)}, lb{dot(pb, t), dot(pb, u)};
        double s0 = 0.0, s1 = 1.0;
        V2 dl = lb - la;
        auto clip = [&](double p, double q) {
            // p·s <= q
            if (p == 0.0) return q >= 0.0;
            double r = q / p;
            if (p < 0.0) { if (r > s1) return false; if (r > s0) s0 = r; }
            else { if (r < s0) return false; if (r < s1) s1 = r; }
            return true;
        };
        bool hit = clip(-dl.x, la.x + hw) && clip(dl.x, hw - la.x) && clip(-dl.y, la.y + hh) && clip(dl.y, hh - la.y);
        if (!hit || s0 >= s1) return {{a, b}};
        std::vector<std::pair<V2, V2>> out;
        V2 d = b - a;
        if (s0 > 1e-12) out.push_back({a, a + d * s0});
        if (s1 < 1.0 - 1e-12) out.push_back({b, a + d * s1});
        return out;
    }

    // The content as it is drawn: a dimension's fraction is written "\S1/4;" but drawn as
    // DIMFRAC says — diagonal (1) as "#" (ODA File Converter, matching AutoCAD's sample_AC1015)
    std::string drawn_as(std::string content) const {
        if (s_.dimfrac != 1 || version_ >= DxfVersion::R2004) return content;
        for (size_t at = content.find("\\S"); at != std::string::npos; at = content.find("\\S", at + 2)) {
            size_t end = content.find(';', at);
            size_t slash = content.find('/', at);
            if (slash != std::string::npos && (end == std::string::npos || slash < end)) content[slash] = '#';
        }
        return content;
    }

    // The frame of a basic dimension's text (negative DIMGAP): a gap out from the text,
    // bottom, right, top, left (ODA File Converter's order)
    void frame(const TextPlace& place, double w, double h) {
        V2 t = polar(place.angle), up = rot90(t);
        double hw = w / 2.0 + gap_, hh = h / 2.0 + gap_;
        V2 c = place.center;
        V2 bl = c - t * hw - up * hh, br = c + t * hw - up * hh, tr = c + t * hw + up * hh, tl = c - t * hw + up * hh;
        line(bl, br, Part::DimLine);
        line(br, tr, Part::DimLine);
        line(tr, tl, Part::DimLine);
        line(tl, bl, Part::DimLine);
    }

    // --- Angular ------------------------------------------------------------------

    // Two lines (13→14, 15→10, the arc through 16) or three points (the vertex 15, 13, 14,
    // the arc through 10). AutoCAD's blocks in ACadSharp's samples: extension lines from
    // the points out to the arc and DIMEXE past it, the arc between the arrows (each
    // DIMASZ along the arc short of its end, the arrow along the chord), the text along
    // the arc's middle a gap and half its height beyond it
    bool angular(std::string& error) {
        AngleRays rays;
        if (!angle_rays(plane_, d_, rays, error)) return false;
        bool three = d_.dim_type == DimensionType::Angular3Pt;
        V2 c = rays.c, a = rays.a, o1 = rays.o1, o2 = rays.o2, r1 = rays.r1, r2 = rays.r2;
        double t1 = rays.t1, t2 = rays.t2, sweep = rays.sweep;
        double radius = length(a - c);
        // An arc length (step 9.17) is measured along the arc, in the dimension's linear units
        const bool arc_length = e_.type == EntityType::ArcDimension;
        // ... along the arc that is measured (13 about the center 15), not along the dimension's arc through 10 (AutoCAD's
        // ARC_DIMENSION 306 of fas9_special_dimensions: 31.4159 = 20 x pi/2, the dimension arc has the radius 31.65)
        out_.measurement = arc_length ? length(o1 - c) * sweep : sweep;
        // AutoCAD keeps the arc's point a third of the way along it (ACadSharp's
        // sample_AC1015: 13.03° of −18.43° … 76.0°, in both angular dimensions)
        V2 kept = c + polar(t1 + sweep / 3.0) * radius;
        if (three) out_.definition_point = wcs(kept);
        else out_.arc_point = {kept.x, kept.y, elevation_};

        DimensionData linear = d_;
        linear.dim_type = DimensionType::Aligned;
        std::string content = dimension_text(s_, arc_length ? linear : d_, out_.measurement, e_.props.paper_space != 0, version_);
        MTextSize size = measure(drawn_as(content));
        double w = size.width, h = size.height;
        double clearance = s_.dimgap < 0.0 ? 2.0 * gap_ : gap_;

        // The text along the arc's middle, beside it by DIMTAD
        double mid_angle = t1 + sweep / 2.0;
        V2 radial_dir = polar(mid_angle);
        TextPlace place;
        place.angle = s_.dimtih ? 0.0 : readable(mid_angle - M_PI / 2.0);
        if (d_.text_rotation != 0.0) place.angle = d_.text_rotation * M_PI / 180.0;
        V2 t = polar(place.angle);
        double across = std::fabs(dot(t, radial_dir)) * w / 2.0 + std::fabs(cross(t, radial_dir)) * h / 2.0;
        double tw = std::fabs(cross(t, radial_dir)) * w + std::fabs(dot(t, radial_dir)) * h;
        bool on_arc = s_.dimtad == 0 || std::fabs(dot(t, radial_dir)) > 1e-9;
        V2 on = c + radial_dir * radius;
        // Above is the text's up: outward, or inward where the text is turned to read
        V2 above = dot(rot90(t), radial_dir) >= 0.0 ? radial_dir : radial_dir * -1.0;
        if (d_.flags & 128) {
            place.center = v2(d_.text_midpoint);
        } else if (on_arc) {
            place.center = on + above * (s_.dimtvp * txt_);
        } else if (s_.dimtad == 4) {
            place.center = on - above * (clearance + across);
        } else {
            place.center = on + above * (clearance + across);
        }
        out_.text_position = {place.center.x, place.center.y, elevation_};

        // What fits on the arc between the extension lines, as for a linear dimension
        double len = radius * sweep;
        bool text_alone = len > tw + 2.0 * gap_;
        bool arrows_alone = len > 2.0 * asz_ + 2.0 * gap_;
        bool both = on_arc ? len > tw + 2.0 * asz_ + 4.0 * gap_ : text_alone && arrows_alone;
        bool arrows_in = both || content.empty() ? arrows_alone || both : false;
        if (!both && !content.empty()) {
            switch (s_.dimatfit) {
                case 0: case 1: arrows_in = false; break;
                case 2: arrows_in = arrows_alone; break;
                default:
                    if (text_alone && arrows_alone) arrows_in = !(tw > 2.0 * asz_);
                    else arrows_in = !text_alone && arrows_alone;
                    break;
            }
            if (s_.dimtix) arrows_in = on_arc ? both : arrows_alone;
        }
        bool arrows_shown = arrows_in || !(s_.dimtix && s_.dimsoxd);

        // Extension lines: from the point to the arc and past it
        auto extension = [&](V2 o, V2 r, Part part) {
            double dist = dot(o - c, r);
            if (std::fabs(radius - dist) < 1e-12) return;
            double sign = radius > dist ? 1.0 : -1.0;
            line(o + r * (exo_ * sign), c + r * (radius + exe_ * sign), part);
        };
        if (!s_.dimse1) extension(o1, r1, Part::Ext1);
        if (!s_.dimse2) extension(o2, r2, Part::Ext2);

        auto deg = [](double rad) {
            double d = std::fmod(rad * 180.0 / M_PI, 360.0);
            return d < 0.0 ? d + 360.0 : d;
        };
        // An arrow's back is on the arc where the chord from its tip is DIMASZ long
        double chord = radius > 0.0 ? 2.0 * std::asin(std::min(1.0, asz_ / (2.0 * radius))) : 0.0;
        // Arrows outside: their tails, a chord further along the arc
        if (!arrows_in && arrows_shown) {
            if (!s_.dimsd1) arc(c, radius, deg(t1 - 2.0 * chord), deg(t1 - chord), Part::DimLine);
            if (!s_.dimsd2) arc(c, radius, deg(t2 + chord), deg(t2 + 2.0 * chord), Part::DimLine);
        }
        // The arc between the arrows (past ticks by DIMDLE), or between the extension lines
        // (DIMTOFL), broken around text standing on it
        if (arrows_in || s_.dimtofl) {
            double delta1 = arrows_in ? ((tsz_ > 0.0 || !trims(arrow1_)) ? -dle_ / radius : chord) : 0.0;
            double delta2 = arrows_in ? ((tsz_ > 0.0 || !trims(arrow2_)) ? -dle_ / radius : chord) : 0.0;
            double a0 = t1 + delta1, a1 = t2 - delta2;
            auto inside = [&](double ang) {
                V2 pt = c + polar(ang) * radius - place.center;
                return std::fabs(dot(pt, t)) <= w / 2.0 + gap_ && std::fabs(cross(t, pt)) <= h / 2.0 + gap_;
            };
            auto edge = [&](double lo, double hi) {
                for (int i = 0; i < 100; ++i) {
                    double m = (lo + hi) / 2.0;
                    if (inside(m)) hi = m; else lo = m;
                }
                return (lo + hi) / 2.0;
            };
            if (on_arc && !content.empty() && inside(mid_angle)) {
                double e1 = edge(a0, mid_angle), e2 = edge(a1, mid_angle);
                if (!s_.dimsd1 && e1 > a0) arc(c, radius, deg(a0), deg(e1), Part::DimLine);
                if (!s_.dimsd2 && e2 < a1) arc(c, radius, deg(e2), deg(a1), Part::DimLine);
            } else if (!s_.dimsd1 || !s_.dimsd2) {
                arc(c, radius, deg(s_.dimsd1 ? mid_angle : a0), deg(s_.dimsd2 ? mid_angle : a1), Part::DimLine);
            }
        }
        // Arrows (or ticks) at the arc's ends, along the chord to the arc an arrow size on
        V2 tip1 = c + r1 * radius, tip2 = c + r2 * radius;
        if (arrows_shown && tsz_ > 0.0) {
            V2 k1 = (rot90(r1) + r1) * tsz_, k2 = (rot90(r2) * -1.0 + r2) * tsz_;
            if (!s_.dimsd1) line(tip1 + k1, tip1 - k1, Part::DimLine);
            if (!s_.dimsd2) line(tip2 - k2, tip2 + k2, Part::DimLine);
        } else if (arrows_shown) {
            double s1 = arrows_in ? chord : -chord;
            V2 base1 = c + polar(t1 + s1) * radius, base2 = c + polar(t2 - s1) * radius;
            if (!s_.dimsd1) arrow(tip1, unit(tip1 - base1), arrow1_, 1);
            if (!s_.dimsd2) arrow(tip2, unit(tip2 - base2), arrow2_, 1);
        }
        text(content, place);
        if (arc_length && !content.empty()) arc_symbol(place, w, h);
        if (s_.dimgap < 0.0 && !content.empty()) frame(place, w, h);
        // The definition points (ODA File Converter's order: the rays counterclockwise; for
        // two lines 16, 15, 13, 14, 10)
        if (three) {
            point(d_.def_point4);
            point(wcs(o1));
            point(wcs(o2));
            point(out_.definition_point);
        } else {
            point(plane_.wcs(out_.arc_point));
            point(d_.def_point4);
            point(d_.def_point2);
            point(d_.def_point3);
            point(d_.definition_point);
        }
        return true;
    }

    // The arc symbol of an arc length (DIMARCSYM 0: before the text, 1: above it, 2: none): a half circle, AutoCAD's blocks
    // (fas9_special_dimensions.scr, *D8 and *D9, and Erik's dimensions): its radius 0.8 of the text's height as laid out (h: the
    // MTEXT's 43, 0.183929 of the text height 0.18 in txt.shx; 0.183806 for "0.0000": the radius follows it), its middle a text
    // height before the text's start (w/2 + h from the text's middle) and 0.4 of it under the text's middle line (half a radius).
    // AutoCAD gives its angles as 359.9999999999961 and 180.0000000000029 (its own rounding of 0 and pi), the linetype
    // Continuous, no lineweight
    void arc_symbol(const TextPlace& place, double w, double h) {
        if (s_.dimarcsym == 2) return;
        const double r = 0.8 * h;
        V2 t = polar(place.angle), up = rot90(t);
        V2 center = s_.dimarcsym == 1 ? place.center + up * (h / 2.0 + gap_ + r * 0.5)
                                      : place.center - t * (w / 2.0 + h) - up * (r / 2.0);
        double rotation = place.angle * 180.0 / M_PI;
        Entity e;
        e.type = EntityType::Arc;
        ArcData a;
        a.center = {center.x, center.y, elevation_};
        a.radius = r;
        a.start_angle = 359.9999999999961 + rotation;
        a.end_angle = 180.0000000000029 + rotation;
        e.data = a;
        props(e, Part::Text);
        e.props.linetype = "Continuous";
        out_.entities.push_back(std::move(e));
    }

    // --- Large radius (DIMJOGGED) ---------------------------------------------------

    // The center (10), the point on the circle (15), the center's overridden place (13) and where the jog crosses (14). AutoCAD's
    // blocks (*D10, *D11 of fas9_special_dimensions.scr): the arrow outside the circle, its line away from the circle along the
    // radius, broken around the text; the line jumps by a jog (DIMJOGANG to the line, centered on 14) to the line that is
    // parallel to it through the overridden center, and goes on to that. Order: that last line, the jog, the line past the
    // text, the line at the arrow, the arrowhead, the text, the points 15, 13, 14
    bool large_radial(std::string& error) {
        (void)error;
        const LargeRadialDimensionData& lr = e_.as_large_radial_dimension();
        V2 c = v2(plane_.ocs(d_.definition_point));
        V2 p = v2(plane_.ocs(d_.def_point4));
        V2 o = v2(plane_.ocs(lr.center_override));
        V2 j = v2(plane_.ocs(lr.jog_point));
        double r = length(p - c);
        out_.measurement = r;
        DimensionData radius = d_;
        radius.dim_type = DimensionType::Radius;
        std::string content = dimension_text(s_, radius, r, e_.props.paper_space != 0, version_);
        MTextSize size = measure(drawn_as(content));
        double tw = size.width, th = size.height;
        V2 w = unit(c - p);                         // from the circle toward the center

        TextPlace place;
        place.angle = s_.dimtoh ? 0.0 : readable(angle_of(w));
        if (d_.text_rotation != 0.0) place.angle = d_.text_rotation * M_PI / 180.0;
        // Text the user moved (128): the arrow outside the circle pointing in, the text where it was put (the blocks of
        // fas9_special_dimensions.scr). Text where AutoCAD left it (Erik's acad_<version>_dxf_dimensions: flags 41): the arrow inside
        // the circle pointing out from its tip on the circle, the text on the line half way between the jog point and the arrow's base
        const bool moved = (d_.flags & 128) != 0;
        V2 near_arrow = moved ? p - w * asz_ : p + w * asz_;
        const V2 arrow_out = moved ? w : w * -1.0;
        if (moved) place.center = v2(d_.text_midpoint);

        // The two parallel lines: through the circle's point and through the overridden center; the jog joins them
        V2 q = o - p;
        double along_o = dot(q, w);
        V2 across = q - w * along_o;
        double distance = length(across);
        double jog = s_.dimjogang > 0.0 && s_.dimjogang < 180.0 ? s_.dimjogang * M_PI / 180.0 : M_PI / 4.0;
        // The centers on one line (no jog): the line from the overridden center still ends at the jog point, where the line from the
        // arrow, broken around the text, begins (AutoCAD's *D10 of acad_2010_dxf_dimensions: 235 to 249.14, then 249.14 to the text)
        V2 b = p + w * dot(j - p, w);
        if (distance <= 1e-9) {
            if (length(b - o) > 1e-9) line(o, b, Part::DimLine);
        } else {
            V2 normal = across * (1.0 / distance);
            double along_j = dot(j - p, w);
            double half = distance / (2.0 * std::tan(jog));
            b = p + w * (along_j - half);
            V2 a = p + w * (along_j + half) + normal * distance;
            line(o, a, Part::DimLine);
            line(a, b, Part::DimLine);
        }
        if (!moved) place.center = (b + near_arrow) * 0.5;
        out_.text_position = {place.center.x, place.center.y, elevation_};
        auto pieces = outside_box(b, near_arrow, place, tw, th);
        if (content.empty()) pieces = {{b, near_arrow}};
        if (pieces.size() == 2) {
            line(pieces[0].second, pieces[0].first, Part::DimLine);
            line(pieces[1].first, pieces[1].second, Part::DimLine);
        } else if (pieces.size() == 1) {
            line(pieces[0].first, pieces[0].second, Part::DimLine);
        }
        // The arrowhead's corners are on the left of the line as it reads (AutoCAD's blocks: the line's direction turned into
        // (-90, 90] degrees, as the text's angle is, decides which corner comes first: *D10 at 27 degrees, *D11 at -103.5, whose
        // solid has its corners the other way)
        const int side = dot(polar(readable(angle_of(arrow_out))), arrow_out) >= 0.0 ? 1 : -1;
        arrow(p, arrow_out, arrow2_, side);
        text(content, place);
        if (s_.dimgap < 0.0 && !content.empty()) frame(place, tw, th);
        point(d_.def_point4);
        point(lr.center_override);
        point(lr.jog_point);
        return true;
    }

    // --- Radius and diameter --------------------------------------------------------

    // Radius: the center (10) and a point on the circle (15); diameter: two points across
    // it (15 and 10). AutoCAD's blocks in ACadSharp's samples: text outside the circle
    // after its arrow, an arrow size and a gap, the line from the arrow to past the text;
    // the line inside from the center (DIMTOFL)
    bool radial(std::string& error) {
        (void)error;
        bool diameter = d_.dim_type == DimensionType::Diameter;
        V2 p = v2(plane_.ocs(d_.def_point4));
        V2 q = v2(plane_.ocs(d_.definition_point));
        V2 c = diameter ? (p + q) * 0.5 : q;
        double r = length(p - c);
        V2 dir = unit(p - c);
        double measured = diameter ? 2.0 * r : r;
        out_.measurement = measured;
        std::string content = dimension_text(s_, d_, measured, e_.props.paper_space != 0, version_);
        MTextSize size = measure(drawn_as(content));
        double w = size.width, h = size.height;
        double clearance = s_.dimgap < 0.0 ? 2.0 * gap_ : gap_;

        // Inside the circle only where the user put the text inside it
        bool user = (d_.flags & 128) != 0;
        V2 at = user ? v2(d_.text_midpoint) : V2{};
        bool text_inside = user && length(at - c) < r;
        TextPlace place;
        place.angle = (text_inside ? s_.dimtih : s_.dimtoh) ? 0.0 : readable(angle_of(dir));
        if (d_.text_rotation != 0.0) place.angle = d_.text_rotation * M_PI / 180.0;
        V2 t = polar(place.angle), up = rot90(t);
        bool along_line = std::fabs(cross(t, dir)) < 1e-9;
        bool on_line = s_.dimtad == 0;
        auto offset = [&](V2 base_dir) {
            // From a point on the leader to the text's middle, by DIMTAD (above: a gap
            // and half the box across the leader)
            V2 side = rot90(base_dir);
            if (dot(side, up) < 0.0) side = side * -1.0;
            double across = std::fabs(dot(t, side)) * w / 2.0 + std::fabs(dot(up, side)) * h / 2.0;
            if (on_line) return V2{};
            return s_.dimtad == 4 ? side * -(clearance + across) : side * (clearance + across);
        };
        double tl_dir = std::fabs(dot(t, dir)) * w + std::fabs(cross(t, dir)) * h;

        // The arrow at 15: outside the circle pointing in, or inside pointing out for text
        // inside
        V2 base = text_inside ? p - dir * asz_ : p + dir * asz_;
        bool landing = !text_inside && !along_line;    // horizontal text: a jog and a landing
        V2 elbow = base + dir * asz_;
        V2 hdir = t;
        if (dot(hdir, dir) < 0.0) hdir = hdir * -1.0;
        // Where the user put it: on the leader there, no nearer the arrow than an arrow size
        // and a gap (ODA File Converter moves it so)
        V2 user_on = at;
        if (user && !landing) {
            V2 out = text_inside ? dir * -1.0 : dir;
            double s = std::max(dot(at - base, out), asz_ + gap_ + tl_dir / 2.0);
            user_on = base + out * s;
        }
        if (user) {
            place.center = user_on + offset(landing ? hdir : dir);
        } else if (landing) {
            // An arrow size out from the arrow, then along the text, which starts a gap on
            double tl = std::fabs(dot(t, hdir)) * w + std::fabs(cross(t, hdir)) * h;
            place.center = elbow + hdir * (gap_ + tl / 2.0) + offset(hdir);
        } else {
            // On along the leader: an arrow size and a gap past the arrow
            place.center = base + dir * (asz_ + gap_ + tl_dir / 2.0) + offset(dir);
        }
        out_.text_position = user ? Point3{user_on.x, user_on.y, elevation_} : Point3{place.center.x, place.center.y, elevation_};

        // The leader, from the text's side to the arrow: past the text and a gap (to a gap
        // before it when it stands on the line)
        bool passes = false;
        if (!content.empty()) {
            if (landing) {
                double tl = std::fabs(dot(t, hdir)) * w + std::fabs(cross(t, hdir)) * h;
                double ca = dot(place.center - elbow, hdir);
                // A landing ends at the text (ODA File Converter's)
                double reach = on_line ? ca - tl / 2.0 - gap_ : ca + tl / 2.0;
                line(elbow + hdir * reach, elbow, Part::DimLine);
                line(base, elbow, Part::DimLine);
            } else {
                V2 out = text_inside ? dir * -1.0 : dir;
                double ca = dot(place.center - base, out);
                double reach = on_line ? ca - tl_dir / 2.0 - gap_ : ca + tl_dir / 2.0 + gap_;
                // Inside text that reaches out of the circle past its center: from 15
                V2 from = base;
                passes = text_inside && dot(base + out * reach - c, dir) < -r;
                if (passes) from = p;
                line(base + out * reach, from, Part::DimLine);
            }
        }
        // A diameter's other arrow is outside too, with its tail (DIMTOFL)
        bool both = diameter && s_.dimtofl && !text_inside;
        if (both) line(q - dir * asz_, q - dir * (2.0 * asz_), Part::DimLine);
        text(content, place);
        if (s_.dimgap < 0.0 && !content.empty()) frame(place, w, h);
        // Inside: the line from the center or across (DIMTOFL)
        if (s_.dimtofl && (!text_inside || (passes && diameter))) line(diameter ? q : c, p, Part::DimLine);
        // The arrows' first corner on the left of the line from the center (AutoCAD's *D10, *D11)
        if (both) arrow(q, dir, arrow1_, 1);
        arrow(p, text_inside ? dir : dir * -1.0, arrow2_, text_inside ? 1 : -1);
        // Without the line inside: the center mark (DIMCEN; negative with center lines)
        if (!s_.dimtofl && !text_inside && cen_ != 0.0) {
            double m = std::fabs(cen_);
            auto mark = [&](V2 a, V2 b) {
                line(a, b, Part::DimLine);
                out_.entities.back().props.lineweight = -1;
            };
            mark(c - V2{m, 0.0}, c + V2{m, 0.0});
            mark(c - V2{0.0, m}, c + V2{0.0, m});
            if (cen_ < 0.0) {
                mark(c - V2{2.0 * m, 0.0}, c - V2{r + m, 0.0});
                mark(c + V2{2.0 * m, 0.0}, c + V2{r + m, 0.0});
                mark(c - V2{0.0, 2.0 * m}, c - V2{0.0, r + m});
                mark(c + V2{0.0, 2.0 * m}, c + V2{0.0, r + m});
            }
        }
        point(d_.def_point4);
        point(d_.definition_point);
        return true;
    }

    // --- Ordinate -----------------------------------------------------------------

    // The feature (13), the leader's end (14), the origin (10); X (bit 64) measures along
    // X with a leader along Y, else the other way. AutoCAD's blocks in ACadSharp's samples:
    // the leader from the feature (DIMEXO from it) to four arrow sizes short of its end,
    // a jog over two arrow sizes when the end is off the feature's line, then on to the
    // end of the text, which starts a gap past the leader's end
    bool ordinate(std::string& error) {
        (void)error;
        V2 f = v2(plane_.ocs(d_.def_point2));
        V2 e = v2(plane_.ocs(d_.def_point3));
        V2 o = v2(plane_.ocs(d_.definition_point));
        bool x_type = (d_.flags & 64) != 0;
        double measured = x_type ? std::fabs(f.x - o.x) : std::fabs(f.y - o.y);
        out_.measurement = measured;
        std::string content = dimension_text(s_, d_, measured, e_.props.paper_space != 0, version_);
        MTextSize size = measure(drawn_as(content));
        double w = size.width, h = size.height;
        double gap = s_.dimgap < 0.0 ? 2.0 * gap_ : gap_;
        // The leader's direction and the perpendicular offset of its end
        V2 axis = x_type ? V2{0.0, 1.0} : V2{1.0, 0.0};
        V2 perp = x_type ? V2{1.0, 0.0} : V2{0.0, 1.0};
        double run = dot(e - f, axis);
        V2 d = run >= 0.0 ? axis : axis * -1.0;
        double offset = dot(e - f, perp);
        V2 start = f + d * exo_;
        TextPlace place;
        place.angle = d_.text_rotation != 0.0 ? d_.text_rotation * M_PI / 180.0 : readable(angle_of(d));
        if (s_.dimtoh && d_.text_rotation == 0.0) place.angle = 0.0;
        V2 t = polar(place.angle), up = rot90(t);
        double tl = std::fabs(dot(t, d)) * w + std::fabs(cross(t, d)) * h;
        double across = std::fabs(dot(t, rot90(d))) * w / 2.0 + std::fabs(dot(up, rot90(d))) * h / 2.0;
        bool on_line = s_.dimtad == 0 || std::fabs(cross(t, d)) > 1e-9;
        V2 side = rot90(d);
        if (dot(side, up) < 0.0) side = side * -1.0;
        V2 text_start = e + d * gap_;
        V2 along = text_start + d * (tl / 2.0);
        if (d_.flags & 128) {
            place.center = v2(d_.text_midpoint);
        } else {
            place.center = on_line ? along : (s_.dimtad == 4 ? along - side * (gap + across) : along + side * (gap + across));
        }
        out_.text_position = {place.center.x, place.center.y, elevation_};
        V2 end = on_line ? e : text_start + d * tl;
        if (std::fabs(offset) > 1e-12) {
            double jog_at = std::max(std::fabs(run) - 4.0 * asz_, 2.0 * asz_);
            V2 jog_from = f + d * jog_at;
            V2 jog_to = e - d * (2.0 * asz_);
            line(start, jog_from, Part::DimLine);
            line(jog_from, jog_to, Part::DimLine);
            line(jog_to, end, Part::DimLine);
        } else {
            line(start, end, Part::DimLine);
        }
        text(content, place);
        if (s_.dimgap < 0.0 && !content.empty()) frame(place, w, h);
        point(d_.def_point2);
        point(d_.def_point3);
        point(d_.definition_point);
        return true;
    }

    // --- Linear -------------------------------------------------------------------

    bool linear(std::string& error) {
        (void)error;
        V2 p1 = v2(plane_.ocs(d_.def_point2)), p2 = v2(plane_.ocs(d_.def_point3));
        V2 dp = v2(plane_.ocs(d_.definition_point));
        // Text the user placed takes the dimension line with it (DIMTMOVE 0)
        bool user = (d_.flags & 128) != 0;
        if (user && s_.dimtmove == 0) dp = v2(d_.text_midpoint);
        double theta = d_.dim_type == DimensionType::Aligned ? angle_of(p2 - p1) : d_.rotation * M_PI / 180.0;
        V2 u = polar(theta), n = rot90(u);
        // The feet: where the extension lines meet the dimension line (oblique 52: along
        // that angle)
        V2 e1 = n, e2 = n;
        bool oblique = d_.oblique_angle != 0.0;
        if (oblique) e1 = e2 = polar(d_.oblique_angle * M_PI / 180.0);
        auto foot = [&](V2 p, V2 e) {
            double den = dot(e, n);
            if (std::fabs(den) < 1e-15) return dp + u * dot(p - dp, u);
            return p + e * (dot(dp - p, n) / den);
        };
        V2 f1 = foot(p1, e1), f2 = foot(p2, e2);
        out_.definition_point = wcs(f2);
        // The measurement: along the dimension line
        double measured = std::fabs(dot(p2 - p1, u));
        out_.measurement = measured;
        // Extension lines from the points toward the dimension line
        auto direction = [&](V2 p, V2 f, V2 other) {
            V2 d = f - p;
            if (length(d) > 1e-12) return unit(d);
            return other;
        };
        V2 v1 = direction(p1, f1, length(f2 - p2) > 1e-12 ? unit(f2 - p2) : n);
        V2 v2d = direction(p2, f2, v1);
        // DIMEXO and DIMEXE are distances across the dimension line, also for extension
        // lines at an oblique angle
        double ext_scale1 = std::fabs(dot(v1, n)) > 1e-12 ? 1.0 / std::fabs(dot(v1, n)) : 1.0;
        double ext_scale2 = std::fabs(dot(v2d, n)) > 1e-12 ? 1.0 / std::fabs(dot(v2d, n)) : 1.0;
        std::string content = dimension_text(s_, d_, measured, e_.props.paper_space != 0, version_);
        MTextSize size = measure(drawn_as(content));
        double w = size.width, h = size.height;
        // A negative DIMGAP frames the text (a basic dimension): the frame is a gap out from
        // the text, and the text a gap further from the line
        bool framed = s_.dimgap < 0.0 && !content.empty();
        double clearance = framed ? 2.0 * gap_ : gap_;

        V2 along = length(f2 - f1) > 1e-12 ? unit(f2 - f1) : u;
        double len = length(f2 - f1);
        V2 mid = (f1 + f2) * 0.5;
        // Text orientation: along the line (readable), or horizontal inside (DIMTIH) and
        // outside (DIMTOH); a rotation of its own (53) for both
        double inside_angle = s_.dimtih ? 0.0 : readable(angle_of(along));
        double outside_angle = s_.dimtoh ? 0.0 : readable(angle_of(along));
        if (d_.text_rotation != 0.0) inside_angle = outside_angle = d_.text_rotation * M_PI / 180.0;
        // The text's length along the line and how far it reaches across it from its middle
        auto extent_along = [&](double a) {
            V2 t = polar(a);
            return std::fabs(dot(t, along)) * w + std::fabs(cross(t, along)) * h;
        };
        auto side_of = [&](double a) {
            V2 sd = rot90(along);
            if (dot(sd, rot90(polar(a))) < 0.0) sd = sd * -1.0;
            return sd;
        };
        auto across = [&](double a, V2 sd) {
            V2 t = polar(a);
            return std::fabs(dot(t, sd)) * w / 2.0 + std::fabs(dot(rot90(t), sd)) * h / 2.0;
        };

        // What fits between the extension lines (DIMATFIT 0 both out, 1 the arrows first,
        // 2 the text first, 3 the one that fits best; DIMTIX the text always in, DIMSOXD
        // no arrows then outside) — as ODA File Converter's blocks, which match AutoCAD's:
        // the text alone needs its length and a gap each side, the arrows theirs and a gap
        // each side, the text in the line both and two gaps more
        double tw = extent_along(inside_angle);
        bool text_alone = len > tw + 2.0 * gap_;
        bool arrows_alone = len > 2.0 * asz_ + 2.0 * gap_;
        bool both = s_.dimtad == 0 ? len > tw + 2.0 * asz_ + 4.0 * gap_ : text_alone && arrows_alone;
        if (content.empty()) both = arrows_alone;
        bool text_in = true, arrows_in = true;
        if (!both) {
            switch (s_.dimatfit) {
                case 0: text_in = arrows_in = false; break;
                case 1: arrows_in = false; text_in = text_alone; break;
                case 2: text_in = false; arrows_in = arrows_alone; break;
                default:
                    if (text_alone && arrows_alone) {
                        text_in = tw > 2.0 * asz_;
                        arrows_in = !text_in;
                    } else {
                        text_in = text_alone;
                        arrows_in = !text_alone && arrows_alone;
                    }
                    break;
            }
            if (content.empty()) { text_in = true; arrows_in = arrows_alone; }
            if (s_.dimtix) {
                text_in = true;
                arrows_in = s_.dimtad == 0 ? len > tw + 2.0 * asz_ + 4.0 * gap_ : arrows_alone;
            }
        }
        bool arrows_shown = arrows_in || !(s_.dimtix && s_.dimsoxd);
        // Text not along the line (DIMTIH on a sloping line) stands in it, whatever DIMTAD
        bool in_line = s_.dimtad == 0 || std::fabs(cross(polar(inside_angle), along)) > 1e-9;
        double ext_reach = 0.0;             // an extension line out to the text over it
        int ext_text = 0;

        // The text beside the line at `at`, by DIMTAD: on it (0), above (1, 3), away from the
        // definition points (2), below (4)
        auto beside = [&](V2 at, double angle, bool on_line) {
            V2 sd = side_of(angle);
            double off = clearance + across(angle, sd);
            switch (on_line ? 0 : s_.dimtad) {
                case 0: return at + sd * (s_.dimtvp * txt_);
                case 2: return dot(v1, sd) < 0.0 ? at - sd * off : at + sd * off;
                case 4: return at - sd * off;
                default: return at + sd * off;
            }
        };

        TextPlace place;
        V2 outer2 = f2 + along * (arrows_in ? 0.0 : asz_);     // where the line beyond ext 2 starts
        double user_at = 0.0;                                   // the user's point along the line
        if (user) {
            // Where the user put it: on the dimension line there (11 stays that point), the
            // text beside it as anywhere else
            V2 at = v2(d_.text_midpoint);
            user_at = dot(at - f1, along);
            bool inside = user_at > 0.0 && user_at < len;
            text_in = inside;
            place.angle = inside ? inside_angle : outside_angle;
            bool on_line = s_.dimtad == 0 || std::fabs(cross(polar(place.angle), along)) > 1e-9;
            place.center = s_.dimtmove == 0 ? beside(at, place.angle, on_line) : at;
            out_.text_position = {at.x, at.y, elevation_};
        } else if (text_in && (s_.dimjust == 3 || s_.dimjust == 4)) {
            // Over extension line 1 or 2, along it: past the dimension line by DIMEXE and
            // a gap, beside it (DIMTAD) or on it (0)
            bool first = s_.dimjust == 3;
            V2 f = first ? f1 : f2, v = first ? v1 : v2d;
            place.angle = d_.text_rotation != 0.0 ? inside_angle : readable(angle_of(v));
            V2 t = polar(place.angle), up = rot90(t);
            double tl = std::fabs(dot(t, v)) * w + std::fabs(cross(t, v)) * h;
            V2 c = f + v * (exe_ + gap_ + tl / 2.0);
            double off = clearance + std::fabs(dot(up, rot90(v))) * h / 2.0 + std::fabs(dot(t, rot90(v))) * w / 2.0;
            switch (s_.dimtad) {
                case 0: place.center = c; break;
                case 4: place.center = c - up * off; break;
                default: place.center = c + up * off; break;
            }
            ext_reach = s_.dimtad == 0 ? 0.0 : exe_ + gap_ + tl + gap_;
            ext_text = first ? 1 : 2;
        } else if (text_in) {
            place.angle = inside_angle;
            // Next to extension line 1 or 2 (DIMJUST 1, 2): an arrow and two gaps from it
            V2 at = mid;
            if (s_.dimjust == 1) at = f1 + along * (asz_ + 2.0 * gap_ + tw / 2.0);
            if (s_.dimjust == 2) at = f2 - along * (asz_ + 2.0 * gap_ + tw / 2.0);
            place.center = beside(at, place.angle, in_line);
        } else {
            // Beside the line beyond extension line 2: an arrow size of line, a gap, the text
            place.angle = outside_angle;
            double ow = extent_along(place.angle);
            bool on_line = s_.dimtad == 0 || std::fabs(cross(polar(place.angle), along)) > 1e-9;
            place.center = beside(outer2 + along * (asz_ + gap_ + ow / 2.0), place.angle, on_line);
        }
        if (!user) out_.text_position = {place.center.x, place.center.y, elevation_};     // 11 is in the OCS

        // Extension lines; DIMEXO and DIMEXE across the dimension line
        auto extension = [&](V2 p, V2 f, V2 v, double scale, Part part) {
            V2 start = p + v * (exo_ * scale);
            if (s_.dimfxlon) start = f - v * fxl_;
            bool to_text = (part == Part::Ext1 && ext_text == 1) || (part == Part::Ext2 && ext_text == 2);
            line(start, f + v * std::max(exe_ * scale, to_text ? ext_reach : 0.0), part);
        };
        if (!s_.dimse1) extension(p1, f1, v1, ext_scale1, Part::Ext1);
        if (!s_.dimse2) extension(p2, f2, v2d, ext_scale2, Part::Ext2);

        // Text outside: the line out to it (under it to its end and a gap, or to a gap
        // before it when it stands in the line); beyond extension line 1 for text the user
        // put there
        double ow = extent_along(place.angle);
        bool out_in_line = s_.dimtad == 0 || std::fabs(cross(polar(place.angle), along)) > 1e-9;
        if (!text_in && !content.empty() && (!user || s_.dimtmove == 0)) {
            if (user && user_at <= 0.0) {
                V2 outer1 = f1 - along * (arrows_in ? 0.0 : asz_);
                double reach = -user_at + (out_in_line ? -ow / 2.0 - gap_ : ow / 2.0 + gap_);
                line(outer1, f1 - along * reach, Part::DimLine);
            } else if (user) {
                double reach = user_at - len + (out_in_line ? -ow / 2.0 - gap_ : ow / 2.0 + gap_);
                line(f2, f2 + along * reach, Part::DimLine);
            } else {
                double reach = out_in_line ? asz_ : asz_ + gap_ + ow + gap_;
                line(outer2, outer2 + along * reach, Part::DimLine);
            }
        }
        // The dimension line: between the arrows (or past the ticks by DIMDLE) when they
        // are in, between the extension lines when they are out (DIMTOFL); around the text
        // standing in it; one half suppressed (DIMSD1, DIMSD2): the other to past the text
        bool tick1 = tsz_ > 0.0 || !trims(arrow1_), tick2 = tsz_ > 0.0 || !trims(arrow2_);
        double t1 = tick1 ? -dle_ : asz_, t2 = tick2 ? -dle_ : asz_;
        if (!arrows_in) t1 = t2 = 0.0;
        if (len <= 0.0) t1 = t2 = 0.0;
        if (arrows_in || s_.dimtofl) {
            V2 s1 = f1 + along * t1, s2 = f2 - along * t2;
            bool broken = in_line && (text_in || user) && ext_text == 0 && !content.empty();
            if (broken) {
                for (const auto& [a, b] : outside_box(s1, s2, place, w, h)) {
                    bool first = dot(a - mid, along) < 0.0;
                    if ((first && s_.dimsd1) || (!first && s_.dimsd2)) continue;
                    line(a, b, Part::DimLine);
                }
            } else if (s_.dimsd1 != s_.dimsd2 && !content.empty()) {
                double c = dot(place.center - f1, along);
                if (s_.dimsd2) line(s1, f1 + along * (c + tw / 2.0 + gap_), Part::DimLine);
                else line(s2, f1 + along * (c - tw / 2.0 - gap_), Part::DimLine);
            } else if (!s_.dimsd1 && !s_.dimsd2) {
                line(s1, s2, Part::DimLine);
            }
        }
        // Arrows outside: their tails, an arrow size long (not where the text's line is)
        if (!arrows_in && arrows_shown) {
            bool text_beyond2 = !text_in && !(user && user_at <= 0.0);
            bool text_beyond1 = !text_in && user && user_at <= 0.0;
            if (!s_.dimsd1 && !text_beyond1) line(f1 - along * asz_, f1 - along * (2.0 * asz_), Part::DimLine);
            if (!s_.dimsd2 && !text_beyond2) line(f2 + along * asz_, f2 + along * (2.0 * asz_), Part::DimLine);
        }
        if (arrows_shown) {
            V2 o = arrows_in ? along : along * -1.0;
            if (tsz_ > 0.0) {
                // Oblique strokes (DIMTSZ) instead of arrows: across the feet at 45°
                V2 k = (along + rot90(along)) * tsz_;
                if (!s_.dimsd1) line(f1 + k, f1 - k, Part::DimLine);
                if (!s_.dimsd2) line(f2 - k, f2 + k, Part::DimLine);
            } else {
                if (!s_.dimsd1) arrow(f1, o * -1.0, arrow1_, -1);
                if (!s_.dimsd2) arrow(f2, o, arrow2_, 1);
            }
        }
        text(content, place);
        if (framed) frame(place, w, h);
        point(d_.def_point2);
        point(d_.def_point3);
        point(out_.definition_point);
        return true;
    }

    const Drawing& dwg_;
    TextLayout& text_;
    const Entity& e_;
    const DimensionData& d_;
    DxfVersion version_;
    DimensionBuild& out_;
    Plane plane_;
    DimStyle s_;
    double asz_ = 0, exo_ = 0, exe_ = 0, gap_ = 0, txt_ = 0, dle_ = 0, tsz_ = 0, fxl_ = 0, cen_ = 0;
    double elevation_ = 0.0;
    std::string arrow1_, arrow2_;
};

} // namespace

bool build_dimension(const Drawing& dwg, TextLayout& text, const Entity& dimension, DxfVersion version,
                     DimensionBuild& out, std::string& error) {
    out = DimensionBuild{};
    const DimensionData& part = *dimension.dimension_part();
    out.definition_point = part.definition_point;
    out.text_position = part.text_midpoint;
    out.arc_point = part.arc_point;
    Builder b(dwg, text, dimension, version, out);
    if (!b.build(error)) return false;
    // 12, the clone point of a baseline or continued dimension: the block is where the dimension would be
    // without the offset, the INSERT of the display moves it by 12 (AutoCAD's own R14 block of a clone,
    // BL15-1 `*D11`: its definition points are the dimension's, less 12). 10, 11 and 16 stay the dimension's.
    const Point3& shift = part.block_translation;
    const bool older = version == DxfVersion::R13 || version == DxfVersion::R14;   // no facit of 2000 or later for a clone
    if (older && (shift.x != 0.0 || shift.y != 0.0 || shift.z != 0.0)) {
        Plane plane(dimension.props.extrusion);
        Point3 wcs_shift = plane.wcs_dir(V2{shift.x, shift.y});
        auto minus = [](Point3& p, const Point3& s) { p = {p.x - s.x, p.y - s.y, p.z - s.z}; };
        for (auto& e : out.entities) {
            switch (e.type) {
                case EntityType::Line:
                    minus(std::get<LineData>(e.data).start, wcs_shift);
                    minus(std::get<LineData>(e.data).end, wcs_shift);
                    break;
                case EntityType::Point: minus(std::get<PointData>(e.data).location, wcs_shift); break;
                case EntityType::MText: minus(std::get<MTextData>(e.data).insertion_point, wcs_shift); break;
                case EntityType::Arc: minus(std::get<ArcData>(e.data).center, shift); break;
                case EntityType::Insert: minus(std::get<InsertData>(e.data).insertion_point, shift); break;
                case EntityType::Text:
                    minus(std::get<TextData>(e.data).position, shift);
                    minus(std::get<TextData>(e.data).alignment_point, shift);
                    break;
                case EntityType::Solid: {
                    auto& s = std::get<SolidData>(e.data);
                    minus(s.p0, shift);
                    minus(s.p1, shift);
                    minus(s.p2, shift);
                    minus(s.p3, shift);
                    break;
                }
                default: break;
            }
        }
    }
    return true;
}

// An ARC_DIMENSION's measurement is its arc length, a LARGE_RADIAL_DIMENSION's its radius (step 9.17); any other as
// dimension_measurement()
double dimension_entity_measurement(const Entity& e, const DimensionData& d) {
    Plane plane(e.props.extrusion);
    if (e.type == EntityType::ArcDimension) {
        V2 c = v2(plane.ocs(d.def_point4));
        DimensionData three = d;
        three.dim_type = DimensionType::Angular3Pt;
        V2 start = v2(plane.ocs(d.def_point2));
        return length(start - c) * dimension_measurement(three, e.props.extrusion);
    }
    if (e.type == EntityType::LargeRadialDimension) {
        return length(v2(plane.ocs(d.def_point4)) - v2(plane.ocs(d.definition_point)));
    }
    return dimension_measurement(d, e.props.extrusion);
}

// As the builder measures, in the dimension's plane (its OCS): a point's z — an extension
// line's origin off the plane — does not count (LibreDWG's example_2010 37E: 13 at z 1, AutoCAD's
// 42 the length in the plane), and an angle is the one through the arc's point (43B: 108°, not
// the 72° between the lines' directions)
double dimension_measurement(const DimensionData& d, const Point3& extrusion) {
    Plane plane(extrusion);
    switch (d.dim_type) {
        case DimensionType::Rotated:
        case DimensionType::Aligned: {
            V2 p1 = v2(plane.ocs(d.def_point2)), p2 = v2(plane.ocs(d.def_point3));
            double theta = d.dim_type == DimensionType::Aligned ? angle_of(p2 - p1) : d.rotation * M_PI / 180.0;
            return std::fabs(dot(p2 - p1, polar(theta)));
        }
        case DimensionType::Angular:
        case DimensionType::Angular3Pt: {
            AngleRays rays;
            std::string error;
            return angle_rays(plane, d, rays, error) ? rays.sweep : 0.0;
        }
        case DimensionType::Diameter:
        case DimensionType::Radius:
            return length(v2(plane.ocs(d.def_point4)) - v2(plane.ocs(d.definition_point)));
        case DimensionType::Ordinate: {
            V2 f = v2(plane.ocs(d.def_point2)), o = v2(plane.ocs(d.definition_point));
            return (d.flags & 64) ? std::fabs(f.x - o.x) : std::fabs(f.y - o.y);
        }
    }
    return 0.0;
}

} // namespace internal
} // namespace hdxf
