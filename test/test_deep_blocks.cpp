#include "test_framework.hpp"
#include "hdxf.hpp"
#include "geometry/extents.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Blocks inside blocks, as deep as a file can make them: no ceiling and no call stack that runs out. A synthetic R12 file of 100 000
// blocks, each inserting the one before it, aligned and turned, is read, measured and saved; and blocks that insert themselves, or
// each other, stop at the cycle only, with the geometry beside the cycle counted.
// ---------------------------------------------------------------------------

namespace {

const int kLevels = 100000;

void group(std::string& text, int code, const std::string& value) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%3d\n", code);
    text += buf;
    text += value;
    text += '\n';
}

// An R12 file with the blocks B0 … B<levels-1>: B0 holds a line from (0,0) to (1,0), each other block one INSERT of the block
// before it at `step` turned `rotation` degrees; the one INSERT of the last block is in the model
std::string chain_file(int levels, double step, double rotation) {
    std::string t;
    t.reserve(static_cast<size_t>(levels) * 140 + 1024);
    group(t, 0, "SECTION");
    group(t, 2, "HEADER");
    group(t, 9, "$ACADVER");
    group(t, 1, "AC1009");
    group(t, 0, "ENDSEC");
    group(t, 0, "SECTION");
    group(t, 2, "BLOCKS");
    auto insert = [&](int block) {
        group(t, 0, "INSERT");
        group(t, 8, "0");
        group(t, 2, "B" + std::to_string(block));
        group(t, 10, std::to_string(step));
        group(t, 20, "0.0");
        group(t, 30, "0.0");
        if (rotation != 0.0) group(t, 50, std::to_string(rotation));
    };
    for (int k = 0; k < levels; ++k) {
        group(t, 0, "BLOCK");
        group(t, 8, "0");
        group(t, 2, "B" + std::to_string(k));
        group(t, 70, "0");
        group(t, 10, "0.0");
        group(t, 20, "0.0");
        group(t, 30, "0.0");
        if (k == 0) {
            group(t, 0, "LINE");
            group(t, 8, "0");
            group(t, 10, "0.0");
            group(t, 20, "0.0");
            group(t, 30, "0.0");
            group(t, 11, "1.0");
            group(t, 21, "0.0");
            group(t, 31, "0.0");
        } else {
            insert(k - 1);
        }
        group(t, 0, "ENDBLK");
        group(t, 8, "0");
    }
    group(t, 0, "ENDSEC");
    group(t, 0, "SECTION");
    group(t, 2, "ENTITIES");
    insert(levels - 1);
    group(t, 0, "ENDSEC");
    group(t, 0, "EOF");
    return t;
}

int chain_blocks(const hdxf::Drawing& dwg) {
    return static_cast<int>(std::count_if(dwg.blocks().begin(), dwg.blocks().end(), [](const hdxf::Block& b) {
        return b.name.size() > 1 && b.name[0] == 'B' && std::isdigit(static_cast<unsigned char>(b.name[1]));
    }));
}

void near(double a, double b, double tol, const std::string& what) {
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s: %.17g != %.17g", what.c_str(), a, b);
    hdxf_test::assert_true(std::fabs(a - b) <= tol, buf);
}

// Reads the chain, checks that all of its blocks are there and what the INSERT in the model measures, saves it, reads it back
// and checks both again
void load_measure_save(const std::string& text, double x0, double y0, double x1, double y1, double tol) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
                           dwg.last_error());
    hdxf_test::assert_eq(chain_blocks(dwg), kLevels, "blocks read");
    hdxf_test::assert_true(dwg.entities().size() == 1, "one INSERT in the model");

    auto check = [&](const hdxf::Drawing& d, const std::string& when) {
        hdxf_test::assert_eq(chain_blocks(d), kLevels, when + ": blocks");
        hdxf::Point3 lo, hi;
        hdxf_test::assert_true(d.entity_extents(d.entities().front(), lo, hi), when + ": extents of the INSERT");
        near(lo.x, x0, tol, when + " min.x");
        near(lo.y, y0, tol, when + " min.y");
        near(hi.x, x1, tol, when + " max.x");
        near(hi.y, y1, tol, when + " max.y");
        // The whole drawing's extents are the INSERT's
        hdxf::internal::Extents model, paper;
        std::vector<std::string> warnings;
        hdxf::internal::drawing_extents(d, {}, {}, model, paper, warnings);
        near(model.min.x, x0, tol, when + " drawing min.x");
        near(model.max.x, x1, tol, when + " drawing max.x");
        near(model.min.y, y0, tol, when + " drawing min.y");
        near(model.max.y, y1, tol, when + " drawing max.y");
    };
    check(dwg, "read");

    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), "saved: " + dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    check(back, "saved and read back");
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

const double kPi = 3.14159265358979323846;

// A point of a block turned 30 degrees about its insertion point (x, y)
hdxf::Point3 turned(double x, double y, double at_x, double at_y) {
    const double c = std::cos(30.0 * kPi / 180.0);
    const double s = std::sin(30.0 * kPi / 180.0);
    return {at_x + x * c - y * s, at_y + x * s + y * c, 0.0};
}

// The box of points
struct Points {
    hdxf::Point3 lo{1e300, 1e300, 0.0};
    hdxf::Point3 hi{-1e300, -1e300, 0.0};

    void add(const hdxf::Point3& p) {
        lo.x = std::min(lo.x, p.x);
        lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x);
        hi.y = std::max(hi.y, p.y);
    }
};

void box_has(const hdxf::Drawing& dwg, const hdxf::Entity& e, const Points& expected, const std::string& what) {
    hdxf::Point3 lo, hi;
    hdxf_test::assert_true(dwg.entity_extents(e, lo, hi), what + ": no extents");
    near(lo.x, expected.lo.x, 1e-9, what + " min.x");
    near(lo.y, expected.lo.y, 1e-9, what + " min.y");
    near(hi.x, expected.hi.x, 1e-9, what + " max.x");
    near(hi.y, expected.hi.y, 1e-9, what + " max.y");
}

// The drawing of cycles_keep_sibling_geometry: its INSERTs in the model, and for each what its extents are (a name for the message
// and the points that bound it)
hdxf::Drawing cycle_drawing(std::vector<std::pair<std::string, Points>>& parts) {
    hdxf::Drawing dwg;
    for (const char* name : {"SELF", "SELFR", "MA", "MB", "LEAF", "TWICE"}) dwg.add_block(name);
    dwg.find_block("SELF")->entities = {line_of({0, 0, 0}, {10, 0, 0}), insert_of("SELF", {0, 100, 0}, 0.0)};
    dwg.find_block("SELFR")->entities = {line_of({0, 0, 0}, {10, 0, 0}), insert_of("SELFR", {0, 100, 0}, 30.0)};
    dwg.find_block("MA")->entities = {line_of({0, 0, 0}, {0, 20, 0}), insert_of("MB", {50, 0, 0}, 0.0)};
    dwg.find_block("MB")->entities = {line_of({0, 0, 0}, {0, -30, 0}), insert_of("MA", {60, 0, 0}, 0.0)};
    dwg.find_block("LEAF")->entities = {line_of({0, 0, 0}, {1, 1, 0})};
    dwg.find_block("TWICE")->entities = {insert_of("LEAF", {0, 200, 0}, 0.0), insert_of("LEAF", {0, 300, 0}, 0.0)};

    auto place = [&](const std::string& what, const std::string& block, double x, double y, double rotation,
                     const std::vector<hdxf::Point3>& points) {
        dwg.entities().push_back(insert_of(block, {x, y, 0}, rotation));
        Points p;
        for (const auto& q : points) p.add(q);
        parts.push_back({what, p});
    };
    // A block that inserts itself, aligned or turned: the line is all there is
    place("SELF", "SELF", -500, 0, 0.0, {{-500, 0, 0}, {-490, 0, 0}});
    place("SELFR aligned", "SELFR", -1000, 0, 0.0, {{-1000, 0, 0}, {-990, 0, 0}});
    // The same blocks turned, so that they are walked
    place("SELFR turned", "SELFR", -2000, 0, 30.0, {turned(0, 0, -2000, 0), turned(10, 0, -2000, 0)});
    place("SELF turned", "SELF", -3000, 0, 30.0, {turned(0, 0, -3000, 0), turned(10, 0, -3000, 0)});
    // Two that insert each other: MA reaches MB and stops at the INSERT of MA in MB; MB the same the other way round
    place("MA", "MA", 0, 0, 0.0, {{0, 0, 0}, {0, 20, 0}, {50, 0, 0}, {50, -30, 0}});
    place("MB", "MB", 0, 1000, 0.0, {{0, 1000, 0}, {0, 970, 0}, {60, 1000, 0}, {60, 1020, 0}});
    place("MA turned", "MA", 5000, 0, 30.0,
          {turned(0, 0, 5000, 0), turned(0, 20, 5000, 0), turned(50, 0, 5000, 0), turned(50, -30, 5000, 0)});
    // One block twice in another: both lines. Inside the turned block the box of the block LEAF is turned with it, corner by corner
    place("TWICE", "TWICE", 0, -1000, 0.0, {{0, -800, 0}, {1, -799, 0}, {0, -700, 0}, {1, -699, 0}});
    std::vector<hdxf::Point3> corners;
    for (double y : {200.0, 300.0}) {
        for (double dx : {0.0, 1.0}) {
            for (double dy : {0.0, 1.0}) corners.push_back(turned(dx, y + dy, 7000, 0));
        }
    }
    place("TWICE turned", "TWICE", 7000, 0, 30.0, corners);
    return dwg;
}

} // namespace

// 100 000 nested blocks, each INSERT at a multiple of 90 degrees: every level shifts the line by 1 in x
TEST(aligned_100000_load_save) {
    load_measure_save(chain_file(kLevels, 1.0, 0.0), kLevels, 0.0, kLevels + 1.0, 0.0, 1e-6);
}

// 100 000 nested blocks, each INSERT turned 30 degrees: 3 000 000 degrees in all, 120 after whole turns, so the line (0,0) … (1,0)
// lies at (0,0) … (-0.5, 0.866)
TEST(rotated_100000_load_save) {
    load_measure_save(chain_file(kLevels, 0.0, 30.0), -0.5, 0.0, 0.0, std::sin(120.0 * kPi / 180.0), 1e-6);
}

// A block that inserts itself, two that insert each other, and a block that inserts one block twice: the cycle stops its own
// branch, and the other geometry stays. The box of a block that a cycle touched is not kept as final, so that the extents do not
// depend on which block is asked for first
// More INSERTs of a block in a cycle than a box on a path is computed for: an INSERT of the other block, whichever comes last, still has
// its whole box
TEST(cycles_with_many_instances_stay_whole) {
    for (bool a_first : {true, false}) {
        hdxf::Drawing dwg;
        dwg.add_block("A");
        dwg.add_block("B");
        dwg.find_block("A")->entities = {line_of({100, 0, 0}, {101, 0, 0}), insert_of("B", {0, 0, 0}, 0.0)};
        dwg.find_block("B")->entities = {line_of({0, 0, 0}, {1, 0, 0}), insert_of("A", {0, 0, 0}, 0.0)};
        std::vector<hdxf::Entity> many_a(12, insert_of("A", {0, 0, 0}, 0.0));
        if (!a_first) dwg.entities().push_back(insert_of("B", {1000, 0, 0}, 0.0));
        dwg.entities().insert(dwg.entities().end(), many_a.begin(), many_a.end());
        if (a_first) dwg.entities().push_back(insert_of("B", {1000, 0, 0}, 0.0));
        hdxf::internal::Extents model, paper;
        std::vector<std::string> warnings;
        hdxf::internal::drawing_extents(dwg, {}, {}, model, paper, warnings);
        near(model.min.x, 0.0, 1e-9, "min.x");
        // B at 1000: its own line, and A's line through the INSERT of A in it
        near(model.max.x, 1101.0, 1e-9, a_first ? "max.x, A first" : "max.x, B first");
    }
}

// Blocks that all insert each other have more paths than can be walked: the calculation ends, and each block's own line is there
TEST(cycles_of_many_blocks_end) {
    for (double rotation : {0.0, 30.0}) {
        hdxf::Drawing dwg;
        const int count = 24;
        for (int i = 0; i < count; ++i) dwg.add_block("N" + std::to_string(i));
        for (int i = 0; i < count; ++i) {
            std::vector<hdxf::Entity> list = {line_of({0, static_cast<double>(i), 0}, {1, static_cast<double>(i), 0})};
            for (int k = 0; k < count; ++k) {
                if (k != i) list.push_back(insert_of("N" + std::to_string(k), {0, 0, 0}, rotation));
            }
            dwg.find_block("N" + std::to_string(i))->entities = std::move(list);
        }
        dwg.entities().push_back(insert_of("N0", {0, 0, 0}, 0.0));
        hdxf::internal::Extents model, paper;
        std::vector<std::string> warnings;
        hdxf::internal::drawing_extents(dwg, {}, {}, model, paper, warnings);
        hdxf_test::assert_true(!model.empty(), "the extents of the cycle");
        hdxf_test::assert_true(model.min.x <= 0.0 && model.max.x >= 1.0, "the first block's line");
    }
}

TEST(cycles_keep_sibling_geometry) {
    std::vector<std::pair<std::string, Points>> parts;
    hdxf::Drawing dwg = cycle_drawing(parts);
    Points all;
    for (size_t i = 0; i < parts.size(); ++i) {
        box_has(dwg, dwg.entities()[i], parts[i].second, parts[i].first);
        all.add(parts[i].second.lo);
        all.add(parts[i].second.hi);
    }
    // All of them in one calculation: MA then MB, the second not using what the first found while MA was being walked
    hdxf::internal::Extents model, paper;
    std::vector<std::string> warnings;
    hdxf::internal::drawing_extents(dwg, {}, {}, model, paper, warnings);
    near(model.min.x, all.lo.x, 1e-9, "drawing min.x");
    near(model.min.y, all.lo.y, 1e-9, "drawing min.y");
    near(model.max.x, all.hi.x, 1e-9, "drawing max.x");
    near(model.max.y, all.hi.y, 1e-9, "drawing max.y");

    // The same INSERTs in the opposite order
    std::vector<std::pair<std::string, Points>> unused;
    hdxf::Drawing reversed = cycle_drawing(unused);
    std::reverse(reversed.entities().begin(), reversed.entities().end());
    hdxf::internal::Extents model2, paper2;
    hdxf::internal::drawing_extents(reversed, {}, {}, model2, paper2, warnings);
    near(model2.min.x, all.lo.x, 1e-9, "reversed min.x");
    near(model2.min.y, all.lo.y, 1e-9, "reversed min.y");
    near(model2.max.x, all.hi.x, 1e-9, "reversed max.x");
    near(model2.max.y, all.hi.y, 1e-9, "reversed max.y");
}
