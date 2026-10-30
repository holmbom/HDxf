#include "test_framework.hpp"
#include "hdxf.hpp"
#include <cmath>
#include <string>

// ---------------------------------------------------------------------------
// $EXTMIN/$EXTMAX and $PEXTMIN/$PEXTMAX as AutoCAD keeps them (an open R12 point from
// phase 0, fixed before phase 1 step 1.4): model space only, INSERT through its block,
// arcs exact, paper space without the viewport with id 1, 1E+20/-1E+20 when empty.
// ---------------------------------------------------------------------------

namespace {

hdxf::Point3 header_point(const hdxf::Drawing& dwg, const std::string& name) {
    const hdxf::HeaderVar* h = dwg.find_header(name);
    hdxf_test::assert_true(h != nullptr, name);
    hdxf::Point3 p;
    for (const auto& [code, value] : h->values) {
        if (code == 10) p.x = std::stod(value);
        if (code == 20) p.y = std::stod(value);
        if (code == 30) p.z = std::stod(value);
    }
    return p;
}

// Saved and read back, so the values are the ones in the file
hdxf::Drawing saved(hdxf::Drawing& dwg) {
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    return back;
}

void near(double a, double b, const std::string& what) {
    hdxf_test::assert_true(std::fabs(a - b) < 1e-9, what + ": " + std::to_string(a) +
                                                    " != " + std::to_string(b));
}

void check(const hdxf::Drawing& dwg, const char* var, double x, double y, double z) {
    hdxf::Point3 p = header_point(dwg, var);
    near(p.x, x, std::string(var) + ".x");
    near(p.y, y, std::string(var) + ".y");
    near(p.z, z, std::string(var) + ".z");
}

} // namespace

TEST(extents_empty_drawing) {
    for (auto version : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000}) {
        hdxf::Drawing dwg(version);
        hdxf::Drawing back = saved(dwg);
        check(back, "$EXTMIN", 1e20, 1e20, 1e20);
        check(back, "$EXTMAX", -1e20, -1e20, -1e20);
        check(back, "$PEXTMIN", 0, 0, 0);   // AutoCAD 2025: an empty paper space is 0,0,0
        check(back, "$PEXTMAX", 0, 0, 0);
    }
}

// A quarter arc reaches only its own extremes; a circle all four
TEST(extents_arc_and_circle) {
    hdxf::Drawing dwg;
    dwg.add_arc({0, 0, 0}, 10, 0, 90);      // (10,0) … (0,10), through no other extreme
    hdxf::Drawing back = saved(dwg);
    check(back, "$EXTMIN", 0, 0, 0);
    check(back, "$EXTMAX", 10, 10, 0);

    hdxf::Drawing c;
    c.add_circle({5, 5, 0}, 2);
    hdxf::Drawing cb = saved(c);
    check(cb, "$EXTMIN", 3, 3, 0);
    check(cb, "$EXTMAX", 7, 7, 0);
}

// A bulge is a half circle here: (0,0) → (10,0) with bulge 1 bulges to y = -5
TEST(extents_polyline_bulge) {
    hdxf::Drawing dwg;
    hdxf::Entity& pl = dwg.add_polyline({{0, 0}, {10, 0}});
    pl.as_polyline().vertices[0].bulge = 1.0;
    hdxf::Drawing back = saved(dwg);
    check(back, "$EXTMIN", 0, -5, 0);
    check(back, "$EXTMAX", 10, 0, 0);
}

// The block's geometry, scaled and rotated by the INSERT — the block definition alone
// does not count
TEST(extents_insert_transformed) {
    hdxf::Drawing dwg;
    hdxf::Block& b = dwg.add_block("SQ", {1, 1, 0});
    hdxf::Entity line;
    line.type = hdxf::EntityType::Line;
    line.data = hdxf::LineData{{1, 1, 0}, {3, 1, 0}};   // 0 … 2 from the base point
    b.entities.push_back(line);
    hdxf::Entity& ins = dwg.add_insert("SQ", {100, 100, 0});
    ins.as_insert().x_scale = 2.0;
    ins.as_insert().rotation = 90.0;
    hdxf::Drawing back = saved(dwg);
    // (0..2, 0) × 2 = (0..4, 0), rotated 90° = (0, 0..4), at 100,100
    check(back, "$EXTMIN", 100, 100, 0);
    check(back, "$EXTMAX", 100, 104, 0);
}

// Paper space has its own extents; the viewport with id 1 is the paper space view itself
TEST(extents_paper_space) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    hdxf::Entity& overall = dwg.add_viewport({500, 500, 0}, 1000, 1000);
    overall.props.paper_space = 1;
    overall.as_viewport().id = 1;
    hdxf::Entity& vp = dwg.add_viewport({20, 30, 0}, 10, 20);
    vp.props.paper_space = 1;
    vp.as_viewport().id = 2;
    hdxf::Drawing back = saved(dwg);
    check(back, "$EXTMIN", 0, 0, 0);
    check(back, "$EXTMAX", 1, 1, 0);
    check(back, "$PEXTMIN", 15, 20, 0);
    check(back, "$PEXTMAX", 25, 40, 0);
}

// The default view of a drawing whose extents are one point (a single text) or flat (a
// single horizontal line) has a height — a view of height 0 does not open in TrueView
// (found in phase 2). A point gets the limits' size around it.
TEST(extents_default_view_never_flat) {
    auto view_height = [](hdxf::Drawing& dwg) {
        std::vector<uint8_t> out = dwg.save_to_memory();
        std::string s = hdxf_test::lf_text(out);
        for (auto& c : s) if (c == '\r') c = ' ';
        size_t vport = s.find("\nVPORT\n", s.find("\nVPORT\n") + 1);    // the entry after the TABLE
        size_t at = s.find("\n 40\n", vport);
        return std::strtod(s.c_str() + at + 5, nullptr);
    };
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004}) {
        hdxf::Drawing text(v);
        text.add_text({5, 5, 0}, "x", 2.5);
        hdxf_test::assert_eq(view_height(text), 9.0 * 1.1, "one point: the limits' height");
        hdxf::Drawing line(v);
        line.add_line({0, 3, 0}, {20, 3, 0});
        hdxf_test::assert_eq(view_height(line), 20.0 * 1.1, "flat: square");
    }
}

// Rättningsfas R.6: a spline's extents are exact, not sampled
TEST(extents_spline_exact) {
    auto exact = [](const hdxf::Drawing& d, const char* var, double x, double y) {
        hdxf::Point3 p = header_point(d, var);
        hdxf_test::assert_true(std::fabs(p.x - x) < 1e-14 && std::fabs(p.y - y) < 1e-14,
                               std::string(var) + ": " + std::to_string(p.x) + ", " + std::to_string(p.y));
    };
    // A cubic Bézier y = 6t(1-t)(1-2t): its extremes are at t = (3 ± sqrt 3)/6, y = ±1/sqrt 3
    hdxf::Drawing bezier(hdxf::DxfVersion::R2000);
    hdxf::Entity& b = bezier.add_spline({{0, 0, 0}, {1, 2, 0}, {2, -2, 0}, {3, 0, 0}});
    b.as_spline().knots = {0, 0, 0, 0, 1, 1, 1, 1};
    hdxf::Drawing back = saved(bezier);
    exact(back, "$EXTMIN", 0.0, -1.0 / std::sqrt(3.0));
    exact(back, "$EXTMAX", 3.0, 1.0 / std::sqrt(3.0));

    // A full circle of radius 2 as a rational quadratic NURBS (nine control points)
    hdxf::Drawing circle(hdxf::DxfVersion::R2000);
    const double w = std::sqrt(0.5);
    hdxf::Entity& c = circle.add_spline({{2, 0, 0}, {2, 2, 0}, {0, 2, 0}, {-2, 2, 0}, {-2, 0, 0},
                                         {-2, -2, 0}, {0, -2, 0}, {2, -2, 0}, {2, 0, 0}}, 2);
    c.as_spline().knots = {0, 0, 0, 0.25, 0.25, 0.5, 0.5, 0.75, 0.75, 1, 1, 1};
    c.as_spline().weights = {1, w, 1, w, 1, w, 1, w, 1};
    c.as_spline().flags |= 4;
    back = saved(circle);
    exact(back, "$EXTMIN", -2.0, -2.0);
    exact(back, "$EXTMAX", 2.0, 2.0);

    // Fit points only, on a straight line: the curve is the line between the ends
    hdxf::Drawing fit(hdxf::DxfVersion::R2000);
    hdxf::Entity& f = fit.add_spline({{0, 0, 0}, {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {5, 5, 0}});
    f.as_spline().fit_points = f.as_spline().control_points;
    f.as_spline().control_points.clear();
    f.as_spline().knots.clear();
    back = saved(fit);
    exact(back, "$EXTMIN", 0.0, 0.0);
    exact(back, "$EXTMAX", 5.0, 5.0);
}

// Codex review of R.6: natural ends without tangents (the curve through (0,0), (1,0.2),
// (2,0.8), (3,1) stays within y 0 … 1), a weight of 1e-16 keeps the end exact, and 20 000
// fit points are solved in linear memory
TEST(extents_spline_review_cases) {
    hdxf::Drawing fit(hdxf::DxfVersion::R2000);
    hdxf::Entity& f = fit.add_spline({{0, 0, 0}, {1, 0.2, 0}, {2, 0.8, 0}, {3, 1, 0}});
    f.as_spline().fit_points = f.as_spline().control_points;
    f.as_spline().control_points.clear();
    f.as_spline().knots.clear();
    hdxf::Drawing back = saved(fit);
    hdxf::Point3 lo = header_point(back, "$EXTMIN"), hi = header_point(back, "$EXTMAX");
    hdxf_test::assert_true(std::fabs(lo.y) < 1e-12 && std::fabs(hi.y - 1.0) < 1e-12,
                           "natural ends: " + std::to_string(lo.y) + " … " + std::to_string(hi.y));

    hdxf::Drawing weights(hdxf::DxfVersion::R2000);
    hdxf::Entity& w = weights.add_spline({{0, 0, 0}, {0, 1, 0}, {0, 2, 0}, {1, 3, 0}});
    w.as_spline().knots = {0, 0, 0, 0, 1, 1, 1, 1};
    w.as_spline().weights = {1, 1, 1, 1e-16};
    w.as_spline().flags |= 4;
    back = saved(weights);
    hdxf_test::assert_eq(header_point(back, "$EXTMAX").x, 1.0, "the end, exactly");

    hdxf::Drawing many(hdxf::DxfVersion::R2000);
    std::vector<hdxf::Point3> points;
    for (int i = 0; i < 20000; ++i) points.push_back({static_cast<double>(i), std::sin(i * 0.01), 0.0});
    hdxf::Entity& m = many.add_spline(points);
    m.as_spline().fit_points = points;
    m.as_spline().control_points.clear();
    m.as_spline().knots.clear();
    back = saved(many);
    hdxf_test::assert_true(std::fabs(header_point(back, "$EXTMAX").x - 19999.0) < 1e-9, "20000 fit points");
}
