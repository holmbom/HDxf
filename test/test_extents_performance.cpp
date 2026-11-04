#include "test_framework.hpp"
#include "hdxf.hpp"
#include "acis.hpp"
#include "extents.hpp"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// What the extents calculation does for INSERT and for bodies, counted instead of timed: a corner of an
// array is walked once, a turned INSERT does not compute the box of its block, the box of a block made of
// edges only and the box with everything in it are kept apart, and a body is read once.
// ---------------------------------------------------------------------------

namespace {

using hdxf::internal::AcisFile;
using hdxf::internal::AcisMap;
using hdxf::internal::Extents;
using hdxf::internal::ExtentsCounters;

struct Measured {
    Extents model;
    Extents paper;
    ExtentsCounters counters;
};

Measured measure(const hdxf::Drawing& dwg) {
    Measured m;
    std::vector<std::string> warnings;
    hdxf::internal::drawing_extents(dwg, {}, {}, m.model, m.paper, warnings, nullptr, &m.counters);
    return m;
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

hdxf::Entity line_of(const hdxf::Point3& a, const hdxf::Point3& b) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Line;
    e.data = hdxf::LineData{a, b};
    return e;
}

hdxf::Entity insert_of(const std::string& block, const hdxf::Point3& at, double rotation) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Insert;
    hdxf::InsertData d;
    d.block_name = block;
    d.insertion_point = at;
    d.rotation = rotation;
    e.data = d;
    return e;
}

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

// A sphere (center 0, radius 1) cut at z = 0.5: what is below the cut (z from -1 to 0.5), its one edge the circle of radius
// 0.866 at z = 0.5
const char* kRestOfSphere =
    "700 0 1 0\n@4 Test @8 ACIS 7.0 @24 Thu Oct 24 12:00:00 2026\n1 9.9999999999999995e-007 1e-010\n"
    "body $-1 -1 $-1 $1 $-1 $-1 #\n"
    "lump $-1 -1 $-1 $-1 $2 $0 #\n"
    "shell $-1 -1 $-1 $-1 $-1 $3 $-1 $1 #\n"
    "face $-1 -1 $-1 $-1 $4 $2 $-1 $5 forward single #\n"
    "loop $-1 -1 $-1 $-1 $6 $3 #\n"
    "sphere-surface $-1 -1 $-1 0 0 0 1 1 0 0 0 0 1 forward_v I I I I #\n"
    "coedge $-1 -1 $-1 $6 $6 $-1 $7 reversed $4 $-1 #\n"
    "edge $-1 -1 $-1 $8 0 $8 6.2831853071795862 $6 $9 forward @7 unknown #\n"
    "vertex $-1 -1 $-1 $7 $10 #\n"
    "ellipse-curve $-1 -1 $-1 0 0 0.5 0 0 1 0.8660254037844386 0 0 1 I I #\n"
    "point $-1 -1 $-1 0.8660254037844386 0 0.5 #\n"
    "End-of-ACIS-data\n";

// The box of that body alone, measured the way a body is measured (all of it at once)
Extents body_box() {
    AcisFile file;
    hdxf_test::assert_true(hdxf::internal::read_sat(kRestOfSphere, file), "SAT not read");
    Extents box;
    hdxf::internal::acis_extents(hdxf::internal::acis_bodies(file), AcisMap{}, box);
    return box;
}

hdxf::Entity solid_of_sphere() {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Solid3D;
    e.data = enciphered(kRestOfSphere);
    return e;
}

// Blocks "L0" … "L<levels-1>": L0 holds a line, each other one an INSERT of the one before it, turned 30 degrees
void add_turned_chain(hdxf::Drawing& dwg, int levels) {
    for (int i = 0; i < levels; ++i) dwg.add_block("L" + std::to_string(i));
    dwg.find_block("L0")->entities.push_back(line_of({1, 0, 0}, {2, 0, 0}));
    for (int i = 1; i < levels; ++i) {
        dwg.find_block("L" + std::to_string(i))->entities.push_back(insert_of("L" + std::to_string(i - 1), {0, 0, 0}, 30.0));
    }
}

} // namespace

// A chain of 30 turned levels: every level is walked once, one walk for each corner of {0, cols - 1} x {0, rows - 1} that is a corner
// of its own
TEST(insert_counts_each_array_corner_once) {
    const int levels = 30;
    hdxf::Drawing dwg;
    add_turned_chain(dwg, levels);
    dwg.add_insert("L" + std::to_string(levels - 1), {0, 0, 0}).as_insert().rotation = 30.0;
    Measured m = measure(dwg);
    hdxf_test::assert_eq(static_cast<int>(m.counters.insert_walks), levels, "one walk per level");
    hdxf_test::assert_eq(static_cast<int>(m.counters.block_boxes), 0, "no box of a block");
    // 30 turns of 30 degrees are 900 degrees, half a turn: the line (1,0) … (2,0) lies at (-1,0) … (-2,0)
    box_is(m.model, -2, 0, 0, -1, 0, 0, 1e-9, "chain");

    // An array has a walk for each of its corners that exists: one, two (a row or a column) or four
    const int shapes[4][3] = {{1, 1, 1}, {3, 1, 2}, {1, 4, 2}, {2, 5, 4}};      // columns, rows, walks
    for (const auto& shape : shapes) {
        hdxf::Drawing a;
        a.add_block("ONE").entities.push_back(line_of({0, 0, 0}, {1, 0, 0}));
        hdxf::InsertData& d = a.add_insert("ONE", {0, 0, 0}).as_insert();
        d.rotation = 30.0;
        d.column_count = shape[0];
        d.row_count = shape[1];
        d.column_spacing = 10.0;
        d.row_spacing = 20.0;
        Measured r = measure(a);
        hdxf_test::assert_eq(static_cast<int>(r.counters.insert_walks), shape[2],
                             std::to_string(shape[0]) + " x " + std::to_string(shape[1]));
    }
}

// A turned INSERT uses the entities of its block where they are, never the box of the block: the box of the block is not computed
TEST(rotated_insert_computes_no_block_box) {
    hdxf::Drawing dwg;
    dwg.add_block("T").entities.push_back(line_of({0, 0, 0}, {10, 0, 0}));
    dwg.add_insert("T", {0, 0, 0}).as_insert().rotation = 30.0;
    Measured m = measure(dwg);
    hdxf_test::assert_eq(static_cast<int>(m.counters.block_boxes), 0, "no box of a block");
    hdxf_test::assert_eq(static_cast<int>(m.counters.insert_walks), 1, "the block walked");
    box_is(m.model, 0, 0, 0, 10 * std::cos(30.0 * 3.14159265358979323846 / 180.0), 5, 0, 1e-9, "turned line");

    // Turned a multiple of 90 degrees the box of the block is what is used, and the block is not walked
    hdxf::Drawing aligned;
    aligned.add_block("T").entities.push_back(line_of({0, 0, 0}, {10, 0, 0}));
    aligned.add_insert("T", {0, 0, 0}).as_insert().rotation = 90.0;
    Measured a = measure(aligned);
    hdxf_test::assert_eq(static_cast<int>(a.counters.block_boxes), 1, "aligned: the box of the block");
    hdxf_test::assert_eq(static_cast<int>(a.counters.insert_walks), 0, "aligned: no walk");
    box_is(a.model, 0, 0, 0, 0, 10, 0, 1e-9, "aligned line");

    // A turned INSERT inside the block of an aligned one: the box of the outer block only
    hdxf::Drawing nested;
    nested.add_block("INNER").entities.push_back(line_of({0, 0, 0}, {10, 0, 0}));
    nested.add_block("OUTER").entities.push_back(insert_of("INNER", {0, 0, 0}, 30.0));
    nested.add_insert("OUTER", {0, 0, 0});
    Measured n = measure(nested);
    hdxf_test::assert_eq(static_cast<int>(n.counters.block_boxes), 1, "nested: the box of OUTER only");
    hdxf_test::assert_eq(static_cast<int>(n.counters.insert_walks), 1, "nested: INNER walked");
}

// The first pass keeps the box of a block made of the edges of its bodies, the second pass the box with everything in the block.
// With one cache for both the line in the block would be lost to the second
TEST(edge_and_full_block_boxes_are_separate) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::Block& block = dwg.add_block("H");
    block.entities.push_back(solid_of_sphere());
    block.entities.push_back(line_of({200, 0, 0}, {300, 0, 0}));
    dwg.add_insert("H", {0, 0, 0});
    Measured m = measure(dwg);
    // The box of H once with only the edges of the body (first pass) and once with everything in it
    hdxf_test::assert_eq(static_cast<int>(m.counters.block_boxes), 2, "an edge box and a full box");
    hdxf_test::assert_eq(static_cast<int>(m.counters.bodies_read), 1, "the body read once for both");
    const Extents body = body_box();
    box_is(m.model, body.min.x, body.min.y, body.min.z, 300.0, body.max.y, body.max.z, 1e-12, "block with body and line");

    // A drawing without a body goes through the second pass only
    hdxf::Drawing plain;
    plain.add_block("P").entities.push_back(line_of({0, 0, 0}, {5, 5, 0}));
    plain.add_insert("P", {0, 0, 0});
    hdxf_test::assert_eq(static_cast<int>(measure(plain).counters.block_boxes), 1, "no body: one box");
}

// A body of an entity that lives on is read once, whichever corner or pass asks; one that is a copy for the moment (the content of a
// MULTILEADER's block) is read where it is met and has no place in the cache
TEST(body_cache_skips_temporaries) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_block("B").entities.push_back(solid_of_sphere());
    hdxf::InsertData& d = dwg.add_insert("B", {0, 0, 0}).as_insert();
    d.rotation = 30.0;
    d.column_count = 2;
    d.row_count = 2;
    d.column_spacing = 10.0;
    d.row_spacing = 10.0;
    Measured m = measure(dwg);
    // Four corners, two passes: the body is read once
    hdxf_test::assert_eq(static_cast<int>(m.counters.bodies_read), 1, "four corners and two passes");
    hdxf_test::assert_eq(static_cast<int>(m.counters.insert_walks), 8, "the block walked for each corner and pass");

    // The same body as the content of the block of a MULTILEADER: each MULTILEADER reads it for itself
    auto reads = [](int leaders) {
        hdxf::Drawing d2(hdxf::DxfVersion::R2007);
        d2.add_block("SB").entities.push_back(solid_of_sphere());
        for (int i = 0; i < leaders; ++i) d2.add_mleader_block({{0, 0, 0}, {4, 3, 0}}, "SB");
        return static_cast<int>(measure(d2).counters.bodies_read);
    };
    const int one = reads(1);
    hdxf_test::assert_true(one >= 1, "the body of the block is read for one MULTILEADER");
    hdxf_test::assert_eq(reads(2), 2 * one, "each MULTILEADER reads the body again");
}
