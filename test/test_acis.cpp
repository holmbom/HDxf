#include "test_framework.hpp"
#include "hdxf.hpp"
#include "acis.hpp"
#include "extents.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// ACIS (3DSOLID, REGION, BODY) in the extents (rättningsfas R.9): SAT and SAB read as
// AutoCAD writes them, and the bodies' exact boxes — edges, and the faces' interior
// extremes (sphere, torus, cone apex, spline surface and its knot lines, surface of
// revolution, sum of curves) inside their loops. Facit: AutoCAD's own $EXTMIN/$EXTMAX in
// files that hold only solids, the solids' data itself, and the surfaces' own geometry.
// ---------------------------------------------------------------------------

namespace {

using hdxf::internal::AcisBody;
using hdxf::internal::AcisFile;
using hdxf::internal::AcisMap;
using hdxf::internal::Extents;

// AutoCAD's 1/3 lines: every character but the space as 159 - c, a line per SAT line
hdxf::AcisData enciphered(const std::string& sat) {
    hdxf::AcisData d;
    std::string line;
    for (char c : sat) {
        if (c == '\n') {
            d.lines.push_back(line);
            line.clear();
            continue;
        }
        unsigned char u = static_cast<unsigned char>(c);
        line += (u > 32 && u < 159) ? static_cast<char>(159 - u) : c;
    }
    if (!line.empty()) d.lines.push_back(line);
    return d;
}

std::vector<AcisBody> bodies_of(const std::string& sat) {
    AcisFile file;
    hdxf_test::assert_true(hdxf::internal::read_sat(sat, file), "SAT not read");
    return hdxf::internal::acis_bodies(file);
}

Extents box_of(const std::string& sat, const AcisMap& map = AcisMap{}) {
    Extents e;
    hdxf::internal::acis_extents(bodies_of(sat), map, e);
    return e;
}

void near(double a, double b, double tol, const std::string& what) {
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s: %.17g != %.17g", what.c_str(), a, b);
    hdxf_test::assert_true(std::fabs(a - b) <= tol, buf);
}

void box_is(const Extents& e, double x0, double y0, double z0, double x1, double y1, double z1, double tol,
            const std::string& what) {
    near(e.min.x, x0, tol, what + " min.x");
    near(e.min.y, y0, tol, what + " min.y");
    near(e.min.z, z0, tol, what + " min.z");
    near(e.max.x, x1, tol, what + " max.x");
    near(e.max.y, y1, tol, what + " max.y");
    near(e.max.z, z1, tol, what + " max.z");
}

std::string replaced(std::string s, const std::string& from, const std::string& to) {
    size_t at = s.find(from);
    hdxf_test::assert_true(at != std::string::npos, from);
    return s.replace(at, from.size(), to);
}

const double kPi = 3.14159265358979323846;

const char* kHeader700 = "700 0 1 0\n@4 Test @8 ACIS 7.0 @24 Thu Oct 24 12:00:00 2026\n1 9.9999999999999995e-007 1e-010\n";

// A sphere (center 0, radius 1) cut at z = 0.5: the coedge forward is the cap above,
// reversed the rest
std::string sphere_cap(const char* sense) {
    std::string s = std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n"
        "sphere-surface $-1 -1 $-1 0 0 0 1 1 0 0 0 0 1 forward_v I I I I #\n"
        "coedge $-1 -1 $-1 $6 $6 $-1 $7 SENSE $4 $-1 #\n"
        "edge $-1 -1 $-1 $8 0 $8 6.2831853071795862 $6 $9 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $7 $10 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0.5 0 0 1 0.8660254037844386 0 0 1 I I #\n"
        "point $-1 -1 $-1 0.8660254037844386 0 0.5 #\n"
        "End-of-ACIS-data\n";
    return replaced(s, "SENSE", sense);
}

// The torus R 3, r 1 about z, cut at z = 0 by its outer (r 4) and inner (r 2) equator:
// the upper half with the senses forward / reversed, the lower half the other way
std::string torus_half(const char* outer, const char* inner) {
    std::string s = std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $6 forward single #\n"
        "loop $-1 -1 $-1 $5 $7 $3 #\n"
        "loop $-1 -1 $-1 $-1 $8 $3 #\n"
        "torus-surface $-1 -1 $-1 0 0 0 0 0 1 3 1 1 0 0 forward_v I I I I #\n"
        "coedge $-1 -1 $-1 $7 $7 $-1 $9 OUTER $4 $-1 #\n"
        "coedge $-1 -1 $-1 $8 $8 $-1 $10 INNER $5 $-1 #\n"
        "edge $-1 -1 $-1 $11 0 $11 6.2831853071795862 $7 $13 forward @7 unknown #\n"
        "edge $-1 -1 $-1 $12 0 $12 6.2831853071795862 $8 $14 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $9 $15 #\n"
        "vertex $-1 -1 $-1 $10 $16 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0 0 0 1 4 0 0 1 I I #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0 0 0 1 2 0 0 1 I I #\n"
        "point $-1 -1 $-1 4 0 0 #\n"
        "point $-1 -1 $-1 2 0 0 #\n"
        "End-of-ACIS-data\n";
    return replaced(replaced(s, "OUTER", outer), "INNER", inner);
}

// The dome z = 1 - x² - y² over [-1, 1]² as a biquadratic Bézier patch (exactsur), the
// face the cap inside the circle x² + y² = 0.25 (where z = 0.75)
std::string spline_dome() {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n"
        "spline-surface $-1 -1 $-1 forward { exactsur full nubs 2 2 open open none none 2 2\n"
        " 0 2 1 2\n"
        " 0 2 1 2\n"
        " -1 -1 -1\n 0 -1 1\n 1 -1 -1\n"
        " -1 0 1\n 0 0 3\n 1 0 1\n"
        " -1 1 -1\n 0 1 1\n 1 1 -1\n"
        " 0\n"
        " } I I I I #\n"
        "coedge $-1 -1 $-1 $6 $6 $-1 $7 forward $4 $-1 #\n"
        "edge $-1 -1 $-1 $8 0 $8 6.2831853071795862 $6 $9 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $7 $10 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0.75 0 0 1 0.5 0 0 1 I I #\n"
        "point $-1 -1 $-1 0.5 0 0.75 #\n"
        "End-of-ACIS-data\n";
}

// The cylinder x² + y² = 25 cut by the plane z = 10 + x/2: the plane's face, bounded by
// the intersection (an ellipse) as AutoCAD saves it — an int_int_cur whose spline
// approximates the curve within its fit tolerance. Here the spline (the ellipse exactly
// as a rational quadratic, starting at x = -5) is moved 1e-4 up at x = 5, within the
// tolerance 1e-3: the curve itself still reaches z = 12.5 there, not 12.5001.
std::string plane_cut() {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward double out #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n"
        "plane-surface $-1 -1 $-1 0 0 10 -0.44721359549995793 0 0.89442719099991586 "
        "0.89442719099991586 0 0.44721359549995793 forward_v I I I I #\n"
        "coedge $-1 -1 $-1 $6 $6 $-1 $7 forward $4 $-1 #\n"
        "edge $-1 -1 $-1 $8 0 $8 4 $6 $9 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $7 $10 #\n"
        "intcurve-curve $-1 -1 $-1 forward { int_int_cur 22100 full nurbs 2 open 5\n"
        " 0 2 1 2 2 2 3 2 4 2\n"
        " -5 0 7.5 1\n"
        " -5 -5 7.5 0.70710678118654757\n"
        " 0 -5 10 1\n"
        " 5 -5 12.5 0.70710678118654757\n"
        " 5 0 12.5001 1\n"
        " 5 5 12.5 0.70710678118654757\n"
        " 0 5 10 1\n"
        " -5 5 7.5 0.70710678118654757\n"
        " -5 0 7.5 1\n"
        " 0.001\n"
        " cone 0 0 0 0 0 1 5 0 0 1 I I 0 1 5 forward I I I I\n"
        " plane 0 0 10 -0.44721359549995793 0 0.89442719099991586 0.89442719099991586 0 "
        "0.44721359549995793 forward_v I I I I\n"
        " nullbs\n nullbs\n F 0 F 4\n 0\n 0\n 0\n"
        " } I I #\n"
        "point $-1 -1 $-1 -5 0 7.5 #\n"
        "End-of-ACIS-data\n";
}

// A whole sphere (center (1, 0, 0), radius 1) with the body's transform: turned 90° about
// z, scale 2, moved by (10, 0, 0) — so center (10, 2, 0), radius 2
std::string moved_sphere() {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $5 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $-1 $2 $-1 $4 forward single #\n"
        "sphere-surface $-1 -1 $-1 1 0 0 1 1 0 0 0 0 1 forward_v I I I I #\n"
        "transform $-1 -1 0 1 0 -1 0 0 0 0 1 10 0 0 2 rotate no_reflect no_shear #\n"
        "End-of-ACIS-data\n";
}

// A spline face bounded by the square (-1, -1, 0) … (1, 1, 0) (its right side at x =
// right): four edges between those corners, counterclockwise from (-1, -1) — straight, or
// the curve records given (an empty one straight) — or (loops false) a face without loops:
// the whole surface
std::string square_face(const std::string& surface, bool loops = true, const std::vector<std::string>& curves = {},
                        double right = 1.0) {
    if (!loops) {
        return std::string(kHeader700) +
            "body $-1 -1 $-1 $1 $-1 $-1 #\n"
            "lump $-1 -1 $-1 $-1 $2 $0 #\n"
            "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
            "face $-1 -1 $-1 $-1 $-1 $2 $-1 $4 forward single #\n" +
            surface + "\nEnd-of-ACIS-data\n";
    }
    const double corner[4][2] = {{-1, -1}, {right, -1}, {right, 1}, {-1, 1}};
    std::string s = std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n" +
        surface + "\n";
    auto n = [](int i) { return "$" + std::to_string(i); };
    for (int i = 0; i < 4; ++i) {
        s += "coedge $-1 -1 $-1 " + n(6 + (i + 1) % 4) + " " + n(6 + (i + 3) % 4) + " $-1 " + n(10 + i) +
             " forward $4 $-1 #\n";
    }
    for (int i = 0; i < 4; ++i) {
        s += "edge $-1 -1 $-1 " + n(14 + i) + " 0 " + n(14 + (i + 1) % 4) + " 2 " + n(6 + i) + " " + n(18 + i) +
             " forward @7 unknown #\n";
    }
    for (int i = 0; i < 4; ++i) s += "vertex $-1 -1 $-1 " + n(10 + i) + " " + n(22 + i) + " #\n";
    for (int i = 0; i < 4; ++i) {
        if (!curves.empty() && !curves[static_cast<size_t>(i)].empty()) {
            s += curves[static_cast<size_t>(i)] + "\n";
            continue;
        }
        const double* a = corner[i];
        const double* b = corner[(i + 1) % 4];
        char buf[200];
        std::snprintf(buf, sizeof buf, "straight-curve $-1 -1 $-1 %.17g %.17g 0 %.17g %.17g 0 I I #\n", a[0], a[1],
                      (b[0] - a[0]) / 2.0, (b[1] - a[1]) / 2.0);
        s += buf;
    }
    for (int i = 0; i < 4; ++i) {
        char buf[100];
        std::snprintf(buf, sizeof buf, "point $-1 -1 $-1 %.17g %.17g 0 #\n", corner[i][0], corner[i][1]);
        s += buf;
    }
    return s + "End-of-ACIS-data\n";
}

// A sphere (center 0, radius 1) bounded only by its seam, a meridian from pole to pole run
// both ways
std::string sphere_seam() {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n"
        "sphere-surface $-1 -1 $-1 0 0 0 1 1 0 0 0 0 1 forward_v I I I I #\n"
        "coedge $-1 -1 $-1 $7 $7 $7 $8 forward $4 $-1 #\n"
        "coedge $-1 -1 $-1 $6 $6 $6 $8 reversed $4 $-1 #\n"
        "edge $-1 -1 $-1 $9 0 $10 3.1415926535897931 $6 $11 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $8 $12 #\n"
        "vertex $-1 -1 $-1 $8 $13 #\n"
        "ellipse-curve $-1 -1 $-1 0 0 0 0 1 0 0 0 1 1 I I #\n"
        "point $-1 -1 $-1 0 0 1 #\n"
        "point $-1 -1 $-1 0 0 -1 #\n"
        "End-of-ACIS-data\n";
}

// A closed surface bounded by two seams that meet at (3, 0, 1): the tube's circle at
// angle 0 and the top circle (radius 3, z = 1) of the torus R 3, r 1 — each run both ways
std::string two_seams(const std::string& surface) {
    return std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #\n"
        "lump $-1 -1 $-1 $-1 $2 $0 #\n"
        "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
        "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
        "loop $-1 -1 $-1 $-1 $6 $3 #\n" +
        surface + "\n"
        "coedge $-1 -1 $-1 $7 $9 $8 $10 forward $4 $-1 #\n"
        "coedge $-1 -1 $-1 $8 $6 $9 $11 forward $4 $-1 #\n"
        "coedge $-1 -1 $-1 $9 $7 $6 $10 reversed $4 $-1 #\n"
        "coedge $-1 -1 $-1 $6 $8 $7 $11 reversed $4 $-1 #\n"
        "edge $-1 -1 $-1 $12 0 $12 6.2831853071795862 $6 $13 forward @7 unknown #\n"
        "edge $-1 -1 $-1 $12 0 $12 6.2831853071795862 $7 $14 forward @7 unknown #\n"
        "vertex $-1 -1 $-1 $10 $15 #\n"
        "ellipse-curve $-1 -1 $-1 3 0 0 0 1 0 0 0 1 1 I I #\n"
        "ellipse-curve $-1 -1 $-1 0 0 1 0 0 1 3 0 0 1 I I #\n"
        "point $-1 -1 $-1 3 0 1 #\n"
        "End-of-ACIS-data\n";
}

// The torus R 3, r 1 as a spline surface: two rational quadratic circles (nine points,
// double inner knots) times each other, periodic both ways — u around the axis, v around
// the tube
std::string spline_torus() {
    const double h = std::sqrt(0.5);
    const double circle[9][3] = {{1, 0, 1}, {1, 1, h}, {0, 1, 1}, {-1, 1, h}, {-1, 0, 1},
                                 {-1, -1, h}, {0, -1, 1}, {1, -1, h}, {1, 0, 1}};
    std::string s = "spline-surface $-1 -1 $-1 forward { exactsur full nurbs 2 2 both periodic periodic none none 5 5\n"
                    " 0 2 1 2 2 2 3 2 4 2\n 0 2 1 2 2 2 3 2 4 2\n";
    for (int j = 0; j < 9; ++j) {
        for (int i = 0; i < 9; ++i) {
            const double* a = circle[i];
            const double* b = circle[j];
            double rho = 3.0 + b[0];
            char buf[200];
            std::snprintf(buf, sizeof buf, " %.17g %.17g %.17g %.17g\n", rho * a[0], rho * a[1], b[1], a[2] * b[2]);
            s += buf;
        }
    }
    return s + " 0\n } I I I I #";
}

// A surface of revolution about z (rotsur, saved without its approximation): profile is
// the curve's text, u its range
std::string revolution(const std::string& profile, double u0, double u1) {
    char buf[200];
    std::snprintf(buf, sizeof buf, " 0 0 0 0 0 1 none F %.17g F %.17g F 0 F 6.2831853071795862 open periodic none none",
                  u0, u1);
    return "spline-surface $-1 -1 $-1 forward { rotsur " + profile + buf + " 0 0 0 0 0 0 } I I I I #";
}

// An exact spline curve (exactcur) of degree 2 through Bézier points, as an intcurve's text
std::string exact_curve(const std::string& points) {
    return "intcurve forward { exactcur full nubs 2 open 2 0 2 1 2 " + points + " 0 } I I";
}

std::filesystem::path external(const std::string& file) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(file);
}

hdxf::Drawing load_or_skip(const std::string& file) {
    std::filesystem::path p = external(file);
    if (!std::filesystem::exists(p)) hdxf_test::skip(file + " missing (downloaded when HDXF_TEST_DATA=ON)");
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(p), file + ": " + dwg.last_error());
    return dwg;
}

const hdxf::Entity& entity_with_handle(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return e;
    }
    throw hdxf_test::AssertFailure{"no entity " + handle};
}

// A solid's handle and its box: min x y z, max x y z
struct SolidBox {
    const char* handle;
    double box[6];
};

void drawing_box(const hdxf::Drawing& dwg, Extents& model) {
    Extents paper;
    std::vector<std::string> warnings;
    hdxf::internal::drawing_extents(dwg, {}, {}, model, paper, warnings);
}

} // namespace

// The 1/3 lines are SAT with each character but the space written as 159 - c
TEST(acis_deciphered_lines) {
    hdxf::AcisData d = enciphered("400 0 1 0\nbody $-1 $1 $-1 $2 #\n");
    hdxf_test::assert_eq(d.lines[0], "koo o n o");
    hdxf_test::assert_eq(hdxf::internal::acis_deciphered(d), "400 0 1 0\nbody $-1 $1 $-1 $2 #\n");
}

// SAT text: "#" ends a record also with the next record's identifier against it (AutoCAD
// writes "lump … #eye_refinement …"), @n strings by their length, "{ ref n }" to the
// subtype numbered n
TEST(acis_sat_records) {
    std::string sat = std::string(kHeader700) +
        "body $-1 -1 $-1 $1 $-1 $-1 #lump $-1 -1 $-1 $-1 $-1 $0 #\n"
        "string_attrib-name_attrib-gen-attrib $-1 -1 $-1 $-1 $0 @7 a # {b} $1 #\n"
        "spline-surface $-1 -1 $-1 forward { exactsur full nubs 1 1 open open none none 2 2 0 1 1 1 0 1 1 1\n"
        " 0 0 0 1 0 0 0 1 0 1 1 0 0 } I I I I #\n"
        "spline-surface $-1 -1 $-1 forward { ref 0 } I I I I #\n"
        "End-of-ACIS-data\n";
    AcisFile f;
    hdxf_test::assert_true(hdxf::internal::read_sat(sat, f));
    hdxf_test::assert_eq(f.version, 700);
    hdxf_test::assert_eq(static_cast<int>(f.records.size()), 5);
    hdxf_test::assert_eq(f.records[1].type, "lump");
    hdxf_test::assert_eq(f.records[2].tokens[5].text, "a # {b}");
    hdxf_test::assert_eq(static_cast<int>(f.subtypes.size()), 1);
    hdxf_test::assert_eq(static_cast<int>(f.subtypes[0].record), 3);
}

// A sphere cut by a plane: the cap holds the pole, the rest the equator
TEST(acis_sphere_face) {
    box_is(box_of(sphere_cap("forward")), -0.8660254037844386, -0.8660254037844386, 0.5, 0.8660254037844386,
           0.8660254037844386, 1.0, 1e-15, "cap");
    box_is(box_of(sphere_cap("reversed")), -1, -1, -1, 1, 1, 0.5, 1e-15, "rest");
}

// A half torus: its top circle (where the normal is the axis) or its bottom
TEST(acis_torus_face) {
    box_is(box_of(torus_half("forward", "reversed")), -4, -4, 0, 4, 4, 1, 1e-15, "upper half");
    box_is(box_of(torus_half("reversed", "forward")), -4, -4, -1, 4, 4, 0, 1e-15, "lower half");
}

// A spline face: its top inside its loop (the patch's control net reaches z = 3), also
// for a coordinate that is a combination in the body's own coordinates
TEST(acis_spline_face) {
    box_is(box_of(spline_dome()), -0.5, -0.5, 0.75, 0.5, 0.5, 1.0, 1e-14, "dome");
    // Turned 30° about x: z' = y sin 30° + z cos 30°, largest c + s²/(4c) at y = s/(2c)
    double s = 0.5;
    double c = std::sqrt(3.0) / 2.0;
    AcisMap m;
    m.m[1][1] = c;
    m.m[1][2] = -s;
    m.m[2][1] = s;
    m.m[2][2] = c;
    Extents e = box_of(spline_dome(), m);
    near(e.max.z, c + s * s / (4.0 * c), 1e-14, "turned dome");
}

// An intersection curve counts at its exact points, not at its approximation's
TEST(acis_intersection_curve) {
    box_is(box_of(plane_cut()), -5, -5, 7.5, 5, 5, 12.5, 1e-12, "plane cut");
}

// The body's transform, and any affine map on top of it (here turned and not uniformly
// scaled — the sphere an ellipsoid): exact, as a world coordinate is a linear function of
// the body's coordinates. An INSERT counts as its block's box transformed (AcDbBlockReference,
// as for every entity in a block).
TEST(acis_transforms) {
    box_is(box_of(moved_sphere()), 8, 0, -2, 12, 4, 2, 1e-14, "transform");

    // turn 30° about z · scale (1, 2, 3), then + (100, 0, 0)
    double a = 30.0 * std::acos(-1.0) / 180.0;
    double L[3][3] = {{std::cos(a) * 1.0, -std::sin(a) * 2.0, 0.0},
                      {std::sin(a) * 1.0, std::cos(a) * 2.0, 0.0},
                      {0.0, 0.0, 3.0}};
    AcisMap m;
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) m.m[i][k] = L[i][k];
    }
    m.m[0][3] = 100.0;
    Extents e = box_of(moved_sphere(), m);
    // The ellipsoid's half extent in a coordinate: the radius (2) times the length of that
    // row of the map
    double center[3] = {10.0, 2.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        double c = m.m[i][3];
        double row = 0.0;
        for (int k = 0; k < 3; ++k) {
            c += L[i][k] * center[k];
            row += L[i][k] * L[i][k];
        }
        double half = 2.0 * std::sqrt(row);
        near(i == 0 ? e.min.x : (i == 1 ? e.min.y : e.min.z), c - half, 1e-12, "map min " + std::to_string(i));
        near(i == 0 ? e.max.x : (i == 1 ? e.max.y : e.max.z), c + half, 1e-12, "map max " + std::to_string(i));
    }

    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::Block& b = dwg.add_block("ball");
    hdxf::Entity solid;
    solid.type = hdxf::EntityType::Solid3D;
    solid.data = enciphered(moved_sphere());
    b.entities.push_back(solid);
    hdxf::Entity& ins = dwg.add_insert("ball", {100.0, 0.0, 0.0});
    ins.as_insert().x_scale = 1.0;
    ins.as_insert().y_scale = 2.0;
    ins.as_insert().z_scale = 3.0;
    ins.as_insert().rotation = 30.0;
    hdxf::Point3 lo, hi;
    hdxf_test::assert_true(dwg.entity_extents(ins, lo, hi));
    // Turned an angle that is not a multiple of 90 degrees (30), an INSERT has the extents of its entities where they
    // are — here the ellipsoid under the map above — not those of the turned box (AutoCAD 2025's extents of an inserted
    // wipeout and of inserted lines, step 9.2: doc/facit/fas9_existing_fields.scr)
    Extents ie;
    ie.min = lo;
    ie.max = hi;
    box_is(ie, e.min.x, e.min.y, e.min.z, e.max.x, e.max.y, e.max.z, 1e-9, "insert");

    // Turned 90 degrees the block's box (8 … 12, 0 … 4, -2 … 2) through the insertion, corner by corner, is the same
    ins.as_insert().rotation = 90.0;
    hdxf_test::assert_true(dwg.entity_extents(ins, lo, hi));
    double L90[3][3] = {{0.0, -2.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 0.0, 3.0}};
    double box_lo[3] = {1e300, 1e300, 1e300};
    double box_hi[3] = {-1e300, -1e300, -1e300};
    for (int corner = 0; corner < 8; ++corner) {
        double p[3] = {(corner & 1) ? 12.0 : 8.0, (corner & 2) ? 4.0 : 0.0, (corner & 4) ? 2.0 : -2.0};
        for (int i = 0; i < 3; ++i) {
            double v = i == 0 ? 100.0 : 0.0;
            for (int k = 0; k < 3; ++k) v += L90[i][k] * p[k];
            box_lo[i] = std::min(box_lo[i], v);
            box_hi[i] = std::max(box_hi[i], v);
        }
    }
    Extents ie90;
    ie90.min = lo;
    ie90.max = hi;
    box_is(ie90, box_lo[0], box_lo[1], box_lo[2], box_hi[0], box_hi[1], box_hi[2], 1e-12, "insert turned 90 degrees");
}

// SAB (2013+): the same records with tagged binary values
TEST(acis_sab) {
    std::vector<uint8_t> b;
    auto bytes = [&](const void* p, size_t n) {
        const uint8_t* q = static_cast<const uint8_t*>(p);
        b.insert(b.end(), q, q + n);
    };
    auto i32 = [&](int32_t v) {
        for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>((static_cast<uint32_t>(v) >> (8 * i)) & 0xff));
    };
    auto f64 = [&](double v) {
        uint64_t u;
        std::memcpy(&u, &v, sizeof u);
        for (int i = 0; i < 8; ++i) b.push_back(static_cast<uint8_t>((u >> (8 * i)) & 0xff));
    };
    auto ident = [&](uint8_t tag, const std::string& s) {
        b.push_back(tag);
        b.push_back(static_cast<uint8_t>(s.size()));
        bytes(s.data(), s.size());
    };
    auto ptr = [&](int32_t v) { b.push_back(12); i32(v); };
    auto num = [&](int32_t v) { b.push_back(4); i32(v); };
    auto real = [&](double v) { b.push_back(6); f64(v); };
    auto vec = [&](uint8_t tag, double x, double y, double z) { b.push_back(tag); f64(x); f64(y); f64(z); };
    bytes("ACIS BinaryFile", 15);
    i32(21800); i32(0); i32(2); i32(12);
    ident(7, "Test"); ident(7, "ASM 1.0"); ident(7, "");
    real(25.4); real(1e-6); real(1e-10);
    ident(13, "asmheader"); ptr(-1); num(-1); ident(7, "1.0"); b.push_back(17);
    ident(13, "body"); ptr(-1); num(-1); ptr(-1); ptr(2); ptr(-1); ptr(6); b.push_back(17);
    ident(13, "lump"); ptr(-1); num(-1); ptr(-1); ptr(-1); ptr(3); ptr(1); b.push_back(17);
    ident(13, "shell"); ptr(-1); num(-1); ptr(-1); ptr(-1); ptr(-1); ptr(4); ptr(-1); ptr(2); b.push_back(17);
    ident(13, "face"); ptr(-1); num(-1); ptr(-1); ptr(-1); ptr(-1); ptr(3); ptr(-1); ptr(5);
    b.push_back(11); b.push_back(11); b.push_back(17);
    ident(14, "sphere"); ident(13, "surface"); ptr(-1); num(-1); ptr(-1);
    vec(19, 1, 0, 0); real(1.0); vec(20, 1, 0, 0); vec(20, 0, 0, 1);
    for (int i = 0; i < 5; ++i) b.push_back(11);
    b.push_back(17);
    ident(13, "transform"); ptr(-1); num(-1);
    vec(20, 0, 1, 0); vec(20, -1, 0, 0); vec(20, 0, 0, 1); vec(20, 10, 0, 0); real(2.0);
    b.push_back(10); b.push_back(11); b.push_back(11); b.push_back(17);
    ident(14, "End"); ident(14, "of"); ident(14, "ASM"); ident(13, "data");

    AcisFile f;
    hdxf_test::assert_true(hdxf::internal::read_sab(b, f), "SAB not read");
    hdxf_test::assert_eq(f.version, 21800);
    Extents e;
    hdxf::internal::acis_extents(hdxf::internal::acis_bodies(f), AcisMap{}, e);
    box_is(e, 8, 0, -2, 12, 4, 2, 1e-14, "SAB sphere");
}

// SAT 1.6 (R13/R14): no product line, sequence numbers, integer senses
TEST(acis_sat_106) {
    std::string sat =
        "106 10 1 0\n"
        "-0 body $-1 $1 $-1 $-1 #\n"
        "-1 lump $-1 $-1 $2 $0 #\n"
        "-2 shell $-1 $-1 $-1 $3 $1 #\n"
        "-3 face $-1 $-1 $4 $2 $-1 $5 reversed double out #\n"
        "-4 loop $-1 $-1 $6 $3 #\n"
        "-5 plane-surface $-1 0 0 2 0 0 1 1 0 0 0 I I I I #\n"
        "-6 coedge $-1 $6 $6 $-1 $7 1 $4 $-1 #\n"
        "-7 edge $-1 $8 $8 $6 $9 forward #\n"
        "-8 vertex $-1 $7 $10 #\n"
        "-9 ellipse-curve $-1 3 4 2 0 0 1 2 0 0 0.5 I I #\n"
        "-10 point $-1 5 4 2 #\n"
        "End-of-ACIS-data\n";
    box_is(box_of(sat), 1, 3, 2, 5, 5, 2, 1e-15, "region");
}

// Written to a 2000 file: the solid is in $EXTMIN/$EXTMAX
TEST(acis_in_saved_extents) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_acis(hdxf::EntityType::Solid3D, enciphered(sphere_cap("reversed")));
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    const hdxf::HeaderVar* lo = back.find_header("$EXTMIN");
    const hdxf::HeaderVar* hi = back.find_header("$EXTMAX");
    hdxf_test::assert_true(lo && hi);
    double v[2][3] = {};
    for (int k = 0; k < 2; ++k) {
        for (const auto& [code, value] : (k == 0 ? lo : hi)->values) {
            if (code == 10) v[k][0] = std::stod(value);
            if (code == 20) v[k][1] = std::stod(value);
            if (code == 30) v[k][2] = std::stod(value);
        }
    }
    near(v[0][0], -1, 1e-12, "x");
    near(v[0][2], -1, 1e-12, "z");
    near(v[1][1], 1, 1e-12, "y");
    near(v[1][2], 0.5, 1e-12, "z");
}

// ACadSharp's drawing, saved by AutoCAD as 2000 … 2018, ASCII and binary: SAT in the 1/3
// lines up to 2010 (400, 20800, 21200, 21500), SAB in ACDSDATA from 2013 ("ACIS
// BinaryFile" 21800, "ASM BinaryFile4" 22300). The boxes are the solids' own data: the box
// solid D65 (±2.5 about its transform's translation), the region D69 and the prism D6A
// (their vertices, moved by the translation).
TEST(acis_acadsharp_solids) {
    for (const char* version : {"AC1015", "AC1018", "AC1021", "AC1024", "AC1027", "AC1032"}) {
        for (const char* form : {"ascii", "binary"}) {
            std::string file = std::string("acadsharp/sample_") + version + "_" + form + ".dxf";
            hdxf::Drawing dwg = load_or_skip(file);
            const SolidBox expected[] = {
                {"D65", {15.27672546982375934, -223.3501226607159253, 0.0, 20.27672546982375934,
                         -218.3501226607159253, 5.0}},
                {"D69", {21.77206982639515331, -223.79081030579777684, 0.0, 28.19943872902541671,
                         -217.00922537771160132, 0.0}},
                {"D6A", {29.66767876841747764, -222.09968099858145636, 0.0, 33.70384675508884342,
                         -218.11496059988495228, 4.380487155114503928}},
            };
            for (const auto& x : expected) {
                hdxf::Point3 lo, hi;
                hdxf_test::assert_true(dwg.entity_extents(entity_with_handle(dwg, x.handle), lo, hi),
                                       file + " " + x.handle);
                Extents e;
                e.min = lo;
                e.max = hi;
                box_is(e, x.box[0], x.box[1], x.box[2], x.box[3], x.box[4], x.box[5], 1e-12,
                       file + " " + x.handle);
            }
        }
    }
}

// jscad's 3Ddraw.dxf (AutoCAD 2017, SAB): box, wedge, cone (its apex), sphere, cylinder,
// torus, pyramid — the drawing holds only them; AutoCAD's $EXTMIN/$EXTMAX
TEST(acis_autocad_3ddraw) {
    hdxf::Drawing dwg = load_or_skip("jscad/3Ddraw.dxf");
    Extents e;
    drawing_box(dwg, e);
    box_is(e, 4.950490243203606, 5.999999999999996, -2.5, 29.00000000000001, 20.0, 9.0, 1e-12, "3Ddraw");
}

// Aspose's conic_pyramid.dxf (AutoCAD, 2010 format, SAT 21500): eight tori and a cone
// (its apex the top); AutoCAD's $EXTMIN/$EXTMAX
TEST(acis_autocad_conic_pyramid) {
    hdxf::Drawing dwg = load_or_skip("aspose/conic_pyramid.dxf");
    Extents e;
    drawing_box(dwg, e);
    box_is(e, 418.6360698312054, 674.3860089634267, -85.00000000000055, 1258.636069831206, 1514.386008963427,
           1118.593630366642, 1e-9, "conic pyramid");
}

// ezdxf's 3dsolids.dxf (ODA's ACIS writer, SAT 700): boxes, a cylinder, a cone, a torus
TEST(acis_ezdxf_3dsolids) {
    hdxf::Drawing dwg = load_or_skip("ezdxf/3dsolids.dxf");
    Extents e;
    drawing_box(dwg, e);
    box_is(e, 0, 0, 0, 70, 10, 10, 1e-12, "3dsolids");
}

// A spline face's extreme on a knot line, where the surface is only continuous: a tent of
// degree 1 × 1 (knots 0, 0.5, 1 both ways) whose top (z = 2) is where the knot lines
// cross, and a ridge of degree 1 × 2 whose top (z = 1) is in the middle of the knot line
// u = 0.5. The patches have no critical point there; the edges are at z = 0.
TEST(acis_spline_knot_lines) {
    const char* grid = " -1 -1 0\n 0 -1 0\n 1 -1 0\n -1 0 0\n 0 0 2\n 1 0 0\n -1 1 0\n 0 1 0\n 1 1 0\n";
    std::string tent = std::string("spline-surface $-1 -1 $-1 forward { exactsur full nubs 1 1 open open none none 3 3\n"
                                   " 0 1 0.5 1 1 1\n 0 1 0.5 1 1 1\n") + grid + " 0\n } I I I I #";
    std::string ridge = std::string("spline-surface $-1 -1 $-1 forward { exactsur full nubs 1 2 open open none none 3 2\n"
                                    " 0 1 0.5 1 1 1\n 0 2 1 2\n") + grid + " 0\n } I I I I #";
    box_is(box_of(square_face(tent)), -1, -1, 0, 1, 1, 2, 1e-14, "tent");
    box_is(box_of(square_face(ridge)), -1, -1, 0, 1, 1, 1, 1e-14, "ridge");
    // Without loops: the whole surface, its ends and its knot lines
    box_is(box_of(square_face(tent, false)), -1, -1, 0, 1, 1, 2, 1e-14, "tent without loops");
}

// A closed surface bounded only by seams (an edge its face runs along both ways) is the
// whole surface: a sphere with a pole-to-pole seam, a torus and a periodic spline torus
// with two seams. A face without loops is the whole surface too.
TEST(acis_seam_faces) {
    box_is(box_of(sphere_seam()), -1, -1, -1, 1, 1, 1, 1e-15, "sphere seam");
    std::string torus = "torus-surface $-1 -1 $-1 0 0 0 0 0 1 3 1 1 0 0 forward_v I I I I #";
    box_is(box_of(two_seams(torus)), -4, -4, -1, 4, 4, 1, 1e-15, "torus seams");
    box_is(box_of(two_seams(spline_torus())), -4, -4, -1, 4, 4, 1, 1e-12, "spline torus seams");
    box_is(box_of(square_face(torus, false)), -4, -4, -1, 4, 4, 1, 1e-15, "torus without loops");
}

// A surface of revolution (rotsur) by its definition: the sphere and the torus as a profile
// turned about z — an ellipse, or an exact spline (two rational quarter circles) — give the
// boxes of the analytic surfaces with the same loops, also through a general affine map. The
// cap's top is the profile's pole on the axis.
TEST(acis_revolution_face) {
    std::string half_circle = "ellipse 0 0 0 0 1 0 0 0 1 1 I I";
    std::string half_spline = "intcurve forward { exactcur full nurbs 2 open 3 0 2 1 2 2 2 0 0 1 1 1 0 1 "
                              "0.70710678118654757 1 0 0 1 1 0 -1 0.70710678118654757 0 0 -1 1 0 } I I";
    std::string tube = "ellipse 3 0 0 0 1 0 1 0 0 1 I I";
    std::string sphere = "sphere-surface $-1 -1 $-1 0 0 0 1 1 0 0 0 0 1 forward_v I I I I #";
    std::string torus = "torus-surface $-1 -1 $-1 0 0 0 0 0 1 3 1 1 0 0 forward_v I I I I #";
    double a = 30.0 * std::acos(-1.0) / 180.0;
    AcisMap m;
    m.m[0][0] = std::cos(a);
    m.m[0][1] = -std::sin(a) * 2.0;
    m.m[0][2] = 0.5;
    m.m[1][0] = std::sin(a);
    m.m[1][1] = std::cos(a) * 2.0;
    m.m[1][2] = -0.25;
    m.m[2][0] = 0.3;
    m.m[2][1] = 0.2;
    m.m[2][2] = 3.0;
    m.m[0][3] = 100.0;
    auto same = [&](const std::string& analytic, const std::string& turned, double tol, const std::string& what) {
        for (int mapped = 0; mapped < 2; ++mapped) {
            AcisMap map = mapped ? m : AcisMap{};
            Extents e = box_of(analytic, map);
            // The map moves the box by 100
            box_is(box_of(turned, map), e.min.x, e.min.y, e.min.z, e.max.x, e.max.y, e.max.z, mapped ? 100.0 * tol : tol,
                   what + (mapped ? " mapped" : ""));
        }
    };
    for (const char* sense : {"forward", "reversed"}) {
        std::string cap = sphere_cap(sense);
        same(cap, replaced(cap, sphere, revolution(half_circle, 0.0, kPi)), 1e-14, std::string("cap ") + sense);
        same(cap, replaced(cap, sphere, revolution(half_spline, 0.0, 2.0)), 1e-14, std::string("spline cap ") + sense);
    }
    box_is(box_of(replaced(sphere_cap("forward"), sphere, revolution(half_circle, 0.0, kPi))), -0.8660254037844386,
           -0.8660254037844386, 0.5, 0.8660254037844386, 0.8660254037844386, 1.0, 1e-15, "cap");
    std::string half = torus_half("forward", "reversed");
    same(half, replaced(half, torus, revolution(tube, 0.0, 2.0 * kPi)), 1e-14, "half torus");
    half = torus_half("reversed", "forward");
    same(half, replaced(half, torus, revolution(tube, 0.0, 2.0 * kPi)), 1e-14, "lower half torus");
    // Without loops: the range — half a turn of the torus (its end circles in the planes
    // y = 0) and the whole sphere
    std::string part = "spline-surface $-1 -1 $-1 forward { rotsur " + tube +
                       " 0 0 0 0 0 1 none F 0 F 6.2831853071795862 F 0 F 3.1415926535897931 periodic open none none "
                       "0 0 0 0 0 0 } I I I I #";
    box_is(box_of(square_face(part, false)), -4, 0, -1, 4, 4, 1, 1e-15, "half turn");
    box_is(box_of(square_face(revolution(half_spline, 0.0, 2.0), false)), -1, -1, -1, 1, 1, 1, 1e-15, "whole");
    // A cubic profile bulging out and in, three quarters of a turn, turned 30° about x and
    // 20° about z and moved by (5, -2, 1): the box of the surface sampled densely and refined
    // by a local search (a check outside HDxf)
    std::string wavy = "intcurve forward { exactcur full nubs 3 open 2 0 3 1 3 1 0 -1 3 0 -0.3 0.2 0 0.4 1.5 0 1 0 } I I";
    std::string three_quarters = "spline-surface $-1 -1 $-1 forward { rotsur " + wavy +
                                 " 0 0 0 0 0 1 none F 0 F 1 F 0 F 4.7123889803846897 open open none none "
                                 "0 0 0 0 0 0 } I I I I #";
    double x = 30.0 * kPi / 180.0, z = 20.0 * kPi / 180.0;
    double rx[3][3] = {{1, 0, 0}, {0, std::cos(x), -std::sin(x)}, {0, std::sin(x), std::cos(x)}};
    double rz[3][3] = {{std::cos(z), -std::sin(z), 0}, {std::sin(z), std::cos(z), 0}, {0, 0, 1}};
    AcisMap turn;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            turn.m[i][j] = 0.0;
            for (int k = 0; k < 3; ++k) turn.m[i][j] += rz[i][k] * rx[k][j];
        }
    }
    turn.m[0][3] = 5.0;
    turn.m[1][3] = -2.0;
    turn.m[2][3] = 1.0;
    box_is(box_of(square_face(three_quarters, false), turn), 3.204685925444769, -3.7939686613759696,
           -0.4173346006468708, 6.580549002841697, -0.2297226543353592, 2.6160254037844384, 1e-13, "three quarters");
}

// A sum of two curves (sumsur): S = C1(u) + C2(v) - A, here the parabolas z = 1 - x² (in
// x) and z = 1 - y² (in y) with A = 0, the face bounded by the four boundary curves (each
// up to z = 1): the top, z = 2, is inside; turned 30° about x, z' = y sin 30° + z cos 30°
TEST(acis_sum_face) {
    std::string sum = "spline-surface $-1 -1 $-1 forward { sumsur " + exact_curve("-1 0 0 0 0 2 1 0 0") + " " +
                      exact_curve("0 -1 0 0 0 2 0 1 0") +
                      " 0 0 0 none F 0 F 1 F 0 F 1 open open none none 0 0 0 0 0 0 } I I I I #";
    auto edge = [](const char* points) {
        return std::string("intcurve-curve $-1 -1 $-1 forward { exactcur full nubs 2 open 2 0 2 1 2 ") + points +
               " 0 } I I #";
    };
    std::vector<std::string> curves = {edge("-1 -1 0 0 -1 2 1 -1 0"), edge("1 -1 0 1 0 2 1 1 0"),
                                       edge("1 1 0 0 1 2 -1 1 0"), edge("-1 1 0 -1 0 2 -1 -1 0")};
    std::string face = square_face(sum, true, curves);
    box_is(box_of(face), -1, -1, 0, 1, 1, 2, 1e-14, "sum");
    double s = 0.5;
    double c = std::sqrt(3.0) / 2.0;
    AcisMap m;
    m.m[1][1] = c;
    m.m[1][2] = -s;
    m.m[2][1] = s;
    m.m[2][2] = c;
    Extents e = box_of(face, m);
    // z' = s y + c (2 - x² - y²): largest at x = 0, y = s / (2c)
    near(e.max.z, 2.0 * c + s * s / (4.0 * c), 1e-14, "turned sum");
    box_is(box_of(square_face(sum, false)), -1, -1, 0, 1, 1, 2, 1e-14, "sum without loops");
}

// Whether a point is in the face is decided exactly, not by the loops' sampled polylines:
// the ridge z = (1 - |x|)(1 - y²) (its crease, the knot line x = 0, highest at (0, 0, 1))
// cut by the curve x = c + a y² with c = -1e-7 — the face is x ≤ c + a y², so the crease's
// top lies 1e-7 outside it, where a chord of the sampled curve passes outside the curve.
// The curve is an exact spline on the ridge: quartic Bézier pieces (z = (1 ± x)(1 - y²)),
// joined where it crosses the crease (y = ±√(-c/a)), its last piece parameterized slower
// (t to 1.3) so that no sample falls on y = 0. The face's top is the curve's tip, (c, 0, 1 + c).
TEST(acis_inside_exact) {
    const double c = -1e-7, a = 0.1;
    const double y0 = std::sqrt(-c / a);
    // A piece from y = ya to yb as t runs from ta to tb: its Bézier points, from x(y), y and
    // z(y) as polynomials in the piece's own parameter s (y = ya + (yb - ya) s)
    auto piece = [&](double ya, double yb, double sign, std::vector<std::array<double, 3>>& out) {
        double h = yb - ya;
        // y, y² in s
        double Y[5] = {ya, h, 0, 0, 0};
        double Y2[5] = {ya * ya, 2 * ya * h, h * h, 0, 0};
        double X[5], Z[5] = {0, 0, 0, 0, 0};
        for (int i = 0; i < 5; ++i) X[i] = a * Y2[i] + (i == 0 ? c : 0.0);
        // z = (1 + sign x)(1 - y²)
        double f[5], g[5];
        for (int i = 0; i < 5; ++i) {
            f[i] = sign * X[i] + (i == 0 ? 1.0 : 0.0);
            g[i] = -Y2[i] + (i == 0 ? 1.0 : 0.0);
        }
        for (int i = 0; i < 5; ++i) {
            for (int j = 0; i + j < 5; ++j) Z[i + j] += f[i] * g[j];
        }
        // power → Bernstein of degree 4: b_i = Σ_j≤i C(i, j) / C(4, j) a_j
        const double C4[5] = {1, 4, 6, 4, 1};
        auto binomial = [](int n, int k) {
            double r = 1.0;
            for (int i = 1; i <= k; ++i) r = r * (n - k + i) / i;
            return r;
        };
        for (int i = (out.empty() ? 0 : 1); i <= 4; ++i) {
            std::array<double, 3> p = {0, 0, 0};
            for (int j = 0; j <= i; ++j) {
                double k = binomial(i, j) / C4[j];
                p[0] += k * X[j];
                p[1] += k * Y[j];
                p[2] += k * Z[j];
            }
            out.push_back(p);
        }
    };
    std::vector<std::array<double, 3>> points;
    piece(-1.0, -y0, -1.0, points);
    piece(-y0, y0, 1.0, points);
    piece(y0, 1.0, -1.0, points);
    char buf[200];
    std::snprintf(buf, sizeof buf, "intcurve-curve $-1 -1 $-1 forward { exactcur full nubs 4 open 4 -1 4 %.17g 4 %.17g 4 1.3 4",
                  -y0, y0);
    std::string curve = buf;
    for (const auto& p : points) {
        std::snprintf(buf, sizeof buf, " %.17g %.17g %.17g", p[0], p[1], p[2]);
        curve += buf;
    }
    curve += " 0 } I I #";
    std::string ridge = "spline-surface $-1 -1 $-1 forward { exactsur full nubs 1 2 open open none none 3 2\n"
                        " 0 1 0.5 1 1 1\n 0 2 1 2\n -1 -1 0\n 0 -1 0\n 1 -1 0\n -1 0 0\n 0 0 2\n 1 0 0\n"
                        " -1 1 0\n 0 1 0\n 1 1 0\n 0\n } I I I I #";
    box_is(box_of(square_face(ridge, true, {"", curve, "", ""}, c + a)), -1, -1, 0, c + a, 1, 1 + c, 1e-15,
           "cut ridge");
}
