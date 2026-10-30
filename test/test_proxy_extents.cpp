#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include "proxy_graphics.hpp"
#include "proxy_record_graphics.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.3: proxy graphics read back (proxy_record_graphics.cpp) and the extents of what has them — a proxy
// entity and an OLE2FRAME.
//
// The streams AutoCAD wrote are the MULTILEADERs' proxy graphics of its own files (doc/facit): the decoder's
// framing is proved on them. The geometry of the other commands is proved on streams made by ProxyGraphics, the
// writer whose layouts were checked against those of AutoCAD (a command's data, not an AutoCAD file of a proxy
// entity: none exists in the corpus, F9.2 — the extents AutoCAD takes of a custom entity's graphics are ej
// testat).
// ---------------------------------------------------------------------------

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Point3;
using namespace hdxf::internal;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Box {
    Point3 lo = {1e300, 1e300, 1e300};
    Point3 hi = {-1e300, -1e300, -1e300};
    void add(const Point3& p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    bool empty() const { return lo.x > hi.x; }
};

Box box_of(const std::vector<Point3>& points) {
    Box b;
    for (const auto& p : points) b.add(p);
    return b;
}

bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }

std::string describe(const Box& b) {
    return "[" + std::to_string(b.lo.x) + " " + std::to_string(b.lo.y) + " " + std::to_string(b.lo.z) + " / " +
           std::to_string(b.hi.x) + " " + std::to_string(b.hi.y) + " " + std::to_string(b.hi.z) + "]";
}

bool same_box(const Box& a, const Box& b, double tolerance) {
    return near(a.lo.x, b.lo.x, tolerance) && near(a.lo.y, b.lo.y, tolerance) && near(a.lo.z, b.lo.z, tolerance) &&
           near(a.hi.x, b.hi.x, tolerance) && near(a.hi.y, b.hi.y, tolerance) && near(a.hi.z, b.hi.z, tolerance);
}

Box drawn_box(const std::vector<uint8_t>& stream) { return box_of(proxy_graphics_extent_points(stream, 1252)); }

std::array<double, 16> translation(double x, double y, double z) {
    return {1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z, 0, 0, 0, 1};
}

std::array<double, 16> scaling(double sx, double sy, double sz) {
    return {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, sz, 0, 0, 0, 0, 1};
}

std::array<double, 16> rotation_z(double degrees) {
    double c = std::cos(degrees * kPi / 180.0), s = std::sin(degrees * kPi / 180.0);
    return {c, -s, 0, 0, s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) { return hdxf_test::read_file_bytes(path); }

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put_double(std::vector<uint8_t>& out, double v) {
    uint64_t u = 0;
    std::memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(u >> (8 * i)));
}

// One command as the stream has it: its size (the whole command), its type and its data
std::vector<uint8_t> command(uint32_t type, const std::vector<double>& doubles, const std::vector<uint32_t>& before = {}) {
    std::vector<uint8_t> data;
    for (uint32_t v : before) put32(data, v);
    for (double v : doubles) put_double(data, v);
    std::vector<uint8_t> out;
    put32(out, static_cast<uint32_t>(8 + data.size()));
    put32(out, type);
    out.insert(out.end(), data.begin(), data.end());
    return out;
}

// A stream: its total size, the number of commands, the commands
std::vector<uint8_t> stream_of(const std::vector<std::vector<uint8_t>>& commands) {
    std::vector<uint8_t> body;
    for (const auto& c : commands) body.insert(body.end(), c.begin(), c.end());
    std::vector<uint8_t> out;
    put32(out, static_cast<uint32_t>(8 + body.size()));
    put32(out, static_cast<uint32_t>(commands.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool header_point(const hdxf::Drawing& d, const std::string& name, Point3& out) {
    for (const auto& v : d.header_vars()) {
        if (v.name != name) continue;
        for (const auto& [code, value] : v.values) {
            if (code == 10) out.x = std::atof(value.c_str());
            if (code == 20) out.y = std::atof(value.c_str());
            if (code == 30) out.z = std::atof(value.c_str());
        }
        return true;
    }
    return false;
}

hdxf::Entity proxy_entity_with(const std::vector<uint8_t>& graphics, bool class_graphics = false) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::ProxyEntity;
    e.props.layer = "0";
    hdxf::ProxyEntityData d;
    d.proxy.application_class.dxf_name = "GFXENT";
    d.proxy.application_class.cpp_name = "AcGfxEnt";
    d.proxy.application_class.application = "Gfx";
    d.proxy.application_class.entity = true;
    d.proxy.data = {1, 2, 3};
    d.proxy.data_bits = 24;
    if (class_graphics) {
        d.proxy.graphics = graphics;
        d.proxy.graphics_bits = static_cast<uint32_t>(graphics.size() * 8);
    } else {
        d.entity_graphics = graphics;
    }
    e.data = d;
    return e;
}

} // namespace

// ---------------------------------------------------------------------------
// The stream
// ---------------------------------------------------------------------------

TEST(proxy_extents_a_stream_is_cut_into_its_commands_and_a_wrong_one_says_why) {
    ProxyGraphics g;
    g.color(3);
    g.circle({0, 0, 0}, 1, {0, 0, 1});
    g.polyline({{0, 0, 0}, {1, 1, 0}});
    std::vector<uint8_t> bytes = g.bytes();
    ProxyStreamView v = decode_proxy_stream(bytes);
    assert_true(v.complete, v.problem);
    assert_eq(3, static_cast<int>(v.commands.size()), "commands");
    assert_eq(14, static_cast<int>(v.commands[0].type), "the color");
    assert_eq(2, static_cast<int>(v.commands[1].type), "the circle");
    assert_eq(56, static_cast<int>(v.commands[1].size), "a circle's data: center, radius, normal");

    // truncated, a size that runs past the end, a count that is not the commands, a total that is not the bytes
    std::vector<uint8_t> cut(bytes.begin(), bytes.end() - 5);
    assert_false(decode_proxy_stream(cut).complete, "a cut stream is not complete");
    std::vector<uint8_t> wrong_size = bytes;
    wrong_size[8] = 0xFF;                                   // the first command claims 255 bytes
    assert_false(decode_proxy_stream(wrong_size).complete, "a command's size past the end");
    std::vector<uint8_t> wrong_count = bytes;
    wrong_count[4] = 9;
    ProxyStreamView counted = decode_proxy_stream(wrong_count);
    assert_false(counted.complete, "a count that is not the commands");
    assert_false(counted.problem.empty(), "with a reason");
    assert_false(decode_proxy_stream({}).complete, "an empty stream");
    assert_false(decode_proxy_stream({1, 2, 3}).complete, "a stream shorter than its header");
    // nothing of a broken stream makes a crash or a huge allocation
    std::vector<uint8_t> huge = {20, 0, 0, 0, 1, 0, 0, 0, 12, 0, 0, 0, 6, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x7F};      // a polyline of 2^31 points
    assert_true(proxy_graphics_extent_points(huge, 1252).empty(), "a polyline whose count the bytes cannot hold draws nothing");
}

TEST(proxy_extents_autocads_own_proxy_graphics_are_read_to_the_end_and_their_commands_kept) {
    // The MULTILEADERs of AutoCAD 2025's files have proxy graphics (92 or 160 and 310 of AcDbEntity)
    const char* files[] = {"acad_2000_dxf_annotative.dxf", "acad_2004_dxf_annotative.dxf", "acad_2007_dxf_annotative.dxf",
                           "acad_2010_dxf_annotative.dxf", "acad_2013_dxf_annotative.dxf", "acad_2018_dxf_annotative.dxf",
                           "acad_2010_dxf_dimensions.dxf", "acad_2013_dxf_dimensions.dxf", "acad_2018_dxf_dimensions.dxf"};
    int streams = 0, commands = 0, compared = 0;
    std::set<uint32_t> types;
    for (const char* name : files) {
        std::vector<uint8_t> in = read_bytes(hdxf_test::source_path(std::string("doc/facit/") + name));
        if (in.empty()) continue;
        hdxf::Drawing d;
        assert_true(d.load_from_memory(in.data(), in.size()), std::string(name) + ": " + d.last_error());
        auto scan = [&](const std::vector<hdxf::Entity>& list) {
            for (const hdxf::Entity& e : list) {
                if (e.type != hdxf::EntityType::MLeader || e.raw.empty()) continue;
                std::vector<uint8_t> stream;
                bool in_stream = false;
                for (const auto& g : e.raw.groups()) {
                    if (g.code == 92 || g.code == 160) { in_stream = true; continue; }
                    if (in_stream && g.code == 310) {
                        std::string h = g.value;
                        while (!h.empty() && (h.back() == ' ' || h.back() == '\r')) h.pop_back();
                        for (size_t i = 0; i + 1 < h.size(); i += 2) stream.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
                    } else if (in_stream) {
                        break;
                    }
                }
                if (stream.empty()) continue;
                ++streams;
                ProxyStreamView v = decode_proxy_stream(stream);
                assert_true(v.complete, std::string(name) + " " + e.props.handle + ": " + v.problem);
                commands += static_cast<int>(v.commands.size());
                for (const auto& c : v.commands) types.insert(c.type);
                // the writer's own framing gives the same bytes back
                ProxyGraphics again;
                again.append(stream);
                assert_true(again.bytes() == stream, std::string(name) + " " + e.props.handle + ": the commands are kept as they are");
                // geometry is drawn
                ProxyDrawn drawn = proxy_graphics_drawn(stream, 1252);
                assert_true(drawn.commands_drawn > 0 && !drawn.points.empty(), std::string(name) + ": the stream draws");
                assert_eq(0, drawn.commands_skipped, std::string(name) + " " + e.props.handle + ": every drawing command is read");
                // The extents of the MULTILEADER are AutoCAD's (HDxf's, mleader_graphics.cpp): everything its stream draws
                // but text lies inside them (a text command's box needs the font of the command: one that is not found
                // is the pen's position)
                std::vector<std::vector<uint8_t>> without_text;
                for (const auto& c : v.commands) {
                    if (c.type == 11 || c.type == 38) continue;
                    std::vector<uint8_t> copy;
                    put32(copy, static_cast<uint32_t>(8 + c.size));
                    put32(copy, c.type);
                    copy.insert(copy.end(), c.data, c.data + c.size);
                    without_text.push_back(copy);
                }
                Point3 lo, hi;
                if (d.entity_extents(e, lo, hi)) {
                    Box own;
                    own.add(lo);
                    own.add(hi);
                    Box geometry = box_of(proxy_graphics_drawn(stream_of(without_text), 1252).points);
                    if (!geometry.empty()) {
                        assert_true(geometry.lo.x >= own.lo.x - 1e-6 && geometry.lo.y >= own.lo.y - 1e-6 &&
                                        geometry.hi.x <= own.hi.x + 1e-6 && geometry.hi.y <= own.hi.y + 1e-6,
                                    std::string(name) + " " + e.props.handle + ": the stream's geometry " + describe(geometry) +
                                        " lies inside the entity's extents " + describe(own));
                        ++compared;
                    }
                }
            }
        };
        scan(d.entities());
        for (const auto& b : d.blocks()) scan(b.entities);
    }
    if (streams == 0) hdxf_test::skip("no AutoCAD file with a MULTILEADER's proxy graphics here (doc/facit is gitignored)");
    assert_true(commands > streams, "commands were read");
    assert_true(compared > 0, "extents were compared");
    assert_true(types.count(2) || types.count(6) || types.count(32), "geometry commands among them");
}

// ---------------------------------------------------------------------------
// What the commands draw
// ---------------------------------------------------------------------------

TEST(proxy_extents_circles_arcs_and_ellipses_are_exact_in_their_plane) {
    // A circle in the plane x = 5: normal (1, 0, 0)
    {
        ProxyGraphics g;
        g.circle({5, 10, 20}, 3, {1, 0, 0});
        Box b = drawn_box(g.bytes());
        assert_true(same_box(b, box_of({{5, 7, 17}, {5, 13, 23}}), 1e-9), "a circle in a plane of x: " + describe(b));
    }
    // A circle in the XY plane
    {
        ProxyGraphics g;
        g.circle({1, 2, 3}, 4, {0, 0, 1});
        Box b = drawn_box(g.bytes());
        assert_true(same_box(b, box_of({{-3, -2, 3}, {5, 6, 3}}), 1e-9), "a circle: " + describe(b));
    }
    // A circle in a tilted plane: the extent of an axis is r * sqrt(1 - n²)
    {
        Point3 n = {0.6, 0.0, 0.8};
        ProxyGraphics g;
        g.circle({0, 0, 0}, 10, n);
        Box b = drawn_box(g.bytes());
        assert_true(near(b.hi.x, 10 * std::sqrt(1 - 0.36), 1e-9) && near(b.hi.y, 10, 1e-9) && near(b.hi.z, 10 * std::sqrt(1 - 0.64), 1e-9),
                    "a tilted circle: " + describe(b));
    }
    // A quarter arc from (r, 0) counterclockwise
    {
        ProxyGraphics g;
        g.arc({0, 0, 0}, 2, {0, 0, 1}, {2, 0, 0}, kPi / 2, 0);
        Box b = drawn_box(g.bytes());
        assert_true(same_box(b, box_of({{0, 0, 0}, {2, 2, 0}}), 1e-9), "a quarter arc: " + describe(b));
    }
    // An arc of 270 degrees starting at 45 degrees: its extremes are on the axes it passes
    {
        ProxyGraphics g;
        g.arc({0, 0, 0}, 1, {0, 0, 1}, {std::cos(kPi / 4), std::sin(kPi / 4), 0}, 1.5 * kPi, 0);
        Box b = drawn_box(g.bytes());
        // from 45 to 315 degrees: it passes 90, 180, 270 and 0 is outside: x from -1 to cos(45), y from -1 to 1
        assert_true(near(b.lo.x, -1, 1e-9) && near(b.hi.x, std::cos(kPi / 4), 1e-9) && near(b.lo.y, -1, 1e-9) && near(b.hi.y, 1, 1e-9),
                    "a three quarter arc: " + describe(b));
    }
    // An elliptical arc: center, normal, radii, the major axis turned 90 degrees: the major radius is along y
    {
        ProxyGraphics g;
        g.ellipse_arc({1, 1, 0}, {0, 0, 1}, 4, 2, 0.0, 2 * kPi, kPi / 2, 0);
        Box b = drawn_box(g.bytes());
        assert_true(same_box(b, box_of({{-1, -3, 0}, {3, 5, 0}}), 1e-9), "an ellipse: " + describe(b));
    }
    // A circle through three points: (1,0), (0,1), (-1,0): the unit circle (type 3: ProxyGraphics does not write it)
    {
        std::vector<uint8_t> stream = stream_of({command(3, {1, 0, 0, 0, 1, 0, -1, 0, 0})});
        Box b = drawn_box(stream);
        assert_true(same_box(b, box_of({{-1, -1, 0}, {1, 1, 0}}), 1e-9), "a circle through three points: " + describe(b));
    }
    // An arc through three points (type 5): (1,0), (0,1), (-1,0) is the upper half of the unit circle
    {
        std::vector<uint8_t> stream = stream_of({command(5, {1, 0, 0, 0, 1, 0, -1, 0, 0}, {})});
        Box b = drawn_box(stream);
        assert_true(near(b.lo.y, 0, 1e-9) && near(b.hi.y, 1, 1e-9) && near(b.lo.x, -1, 1e-9) && near(b.hi.x, 1, 1e-9),
                    "an arc through three points: " + describe(b));
    }
}

TEST(proxy_extents_lines_polygons_meshes_and_shells_are_their_vertices) {
    ProxyGraphics g;
    g.polyline({{0, 0, 0}, {10, 5, 2}});
    g.polygon({{-3, 1, 0}, {4, 8, 0}, {0, -6, 1}});
    g.polyline_normal({{20, 0, 0}, {21, 1, 0}}, {0, 0, 1});
    g.shell({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 7}}, {4, 0, 1, 2, 3}, {0, 0, 0});
    g.mesh(2, 2, {{0, 0, 0}, {30, 0, 0}, {0, 30, 0}, {30, 30, 4}}, {0, 0});
    Box b = drawn_box(g.bytes());
    assert_true(same_box(b, box_of({{-3, -6, 0}, {30, 30, 7}}), 1e-9), "the vertices: " + describe(b));
    // an xline and a ray are infinite: they add nothing
    ProxyGraphics lines;
    lines.xline({0, 0, 0}, {1, 1, 0});
    lines.ray({5, 5, 5}, {6, 6, 6});
    assert_true(drawn_box(lines.bytes()).empty(), "an xline and a ray draw no extents");
}

TEST(proxy_extents_an_lwpolyline_follows_its_bulges_and_its_normal) {
    // A square with the right side a half circle bulging out (bulge 1 between (2,0) and (2,2): center (2,1), radius 1)
    hdxf::LwPolylineData lw;
    lw.elevation = 3.0;
    for (auto p : std::vector<std::array<double, 3>>{{0, 0, 0}, {2, 0, 1.0}, {2, 2, 0}, {0, 2, 0}}) {
        hdxf::LwVertex v;
        v.position = {p[0], p[1]};
        v.bulge = p[2];
        lw.vertices.push_back(v);
    }
    lw.flags = 1;
    ProxyGraphics g;
    g.lwpolyline(lwpline_data(lw, 0.0, {0, 0, 1}));
    Box b = drawn_box(g.bytes());
    // the bulge is counterclockwise from (2,0) to (2,2): the half circle is on the right, x to 3
    assert_true(near(b.lo.x, 0, 1e-9) && near(b.hi.x, 3, 1e-9) && near(b.lo.y, 0, 1e-9) && near(b.hi.y, 2, 1e-9) && near(b.lo.z, 3, 1e-9),
                "an lwpolyline with a bulge: " + describe(b));
    // a clockwise bulge (negative) goes the other way: x to 1... the half circle from (2,0) to (2,2) on the left
    hdxf::LwPolylineData lw2 = lw;
    lw2.vertices[1].bulge = -1.0;
    ProxyGraphics g2;
    g2.lwpolyline(lwpline_data(lw2, 0.0, {0, 0, 1}));
    Box b2 = drawn_box(g2.bytes());
    assert_true(near(b2.lo.x, 0, 1e-9) && near(b2.hi.x, 2, 1e-9), "a negative bulge: " + describe(b2));
    // in a plane of normal (1, 0, 0): the OCS axes are the arbitrary axes of that normal
    hdxf::LwPolylineData flat;
    flat.elevation = 7.0;
    for (auto p : std::vector<std::array<double, 2>>{{0, 0}, {4, 0}, {4, 3}}) {
        hdxf::LwVertex v;
        v.position = {p[0], p[1]};
        flat.vertices.push_back(v);
    }
    ProxyGraphics g3;
    g3.lwpolyline(lwpline_data(flat, 0.0, {1, 0, 0}));
    Box b3 = drawn_box(g3.bytes());
    assert_true(near(b3.lo.x, 7, 1e-9) && near(b3.hi.x, 7, 1e-9), "its elevation is along the normal: " + describe(b3));
}

TEST(proxy_extents_transforms_apply_to_what_is_drawn_inside_them_and_pop) {
    ProxyGraphics g;
    g.push_transform(translation(100, 200, 0));
    g.push_transform(scaling(2, 3, 1));
    g.polyline({{1, 1, 0}, {2, 2, 0}});          // (102, 203) and (104, 206)
    g.pop_transform();
    g.polyline({{0, 0, 0}, {1, 1, 0}});          // (100, 200) and (101, 201)
    g.pop_transform();
    g.polyline({{-5, -5, 0}, {-4, -4, 0}});      // not transformed
    Box b = drawn_box(g.bytes());
    assert_true(same_box(b, box_of({{-5, -5, 0}, {104, 206, 0}}), 1e-9), "nested transforms: " + describe(b));

    // a circle under a non-uniform scale is an ellipse: exact
    ProxyGraphics c;
    c.push_transform(scaling(4, 1, 1));
    c.circle({0, 0, 0}, 1, {0, 0, 1});
    c.pop_transform();
    Box cb = drawn_box(c.bytes());
    assert_true(same_box(cb, box_of({{-4, -1, 0}, {4, 1, 0}}), 1e-9), "a circle under a scale: " + describe(cb));

    // a rotation of 90 degrees turns an arc
    ProxyGraphics r;
    r.push_transform(rotation_z(90));
    r.arc({0, 0, 0}, 1, {0, 0, 1}, {1, 0, 0}, kPi / 2, 0);           // from (1,0) to (0,1): after the turn (0,1) to (-1,0)
    r.pop_transform();
    Box rb = drawn_box(r.bytes());
    assert_true(same_box(rb, box_of({{-1, 0, 0}, {0, 1, 0}}), 1e-9), "an arc under a rotation: " + describe(rb));

    // a pop without a push is nothing
    ProxyGraphics p;
    p.pop_transform();
    p.polyline({{1, 1, 0}, {2, 2, 0}});
    assert_true(same_box(drawn_box(p.bytes()), box_of({{1, 1, 0}, {2, 2, 0}}), 1e-9), "a pop without a push");
}

TEST(proxy_extents_state_commands_draw_nothing_and_an_extents_command_is_the_extents) {
    ProxyGraphics state;
    state.color(1);
    state.layer(2);
    state.linetype(3);
    state.marker(4);
    state.fill(true);
    state.true_color(0x00FF00);
    state.lineweight(25);
    state.thickness(5);
    state.command_51();
    assert_true(drawn_box(state.bytes()).empty(), "colors, layers, fill and the rest draw nothing");
    // an extents command (1): two corners; the stream's extents are those the application recorded
    std::vector<uint8_t> stream = stream_of({command(1, {-1, -2, -3, 4, 5, 6}), command(6, {100, 100, 0, 200, 200, 0}, {2})});
    Box b = drawn_box(stream);
    assert_true(same_box(b, box_of({{-1, -2, -3}, {4, 5, 6}}), 1e-12), "the extents command wins: " + describe(b));
    ProxyDrawn drawn = proxy_graphics_drawn(stream, 1252);
    assert_true(drawn.has_declared && !drawn.points.empty(), "and the geometry is still read");
}

// ---------------------------------------------------------------------------
// The entities' extents in a drawing
// ---------------------------------------------------------------------------

TEST(proxy_extents_a_proxy_entity_adds_its_graphics_to_the_drawings_extents_in_every_version) {
    for (DxfVersion version : {DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string where = std::string(hdxf_test::version_label(version)) + " " + hdxf_test::format_label(format);
            ProxyGraphics g;
            g.circle({50, 60, 0}, 10, {0, 0, 1});
            g.polyline({{0, 0, 0}, {5, 5, 0}});
            hdxf::Drawing d(version);
            // R14 keeps the entity's own proxy graphics only from 2000 on: the class's graphics are what R14 has
            d.add_entity(proxy_entity_with(g.bytes(), version < DxfVersion::R2000));
            Point3 lo, hi;
            assert_true(d.entity_extents(d.entities().back(), lo, hi), where + ": extents of the entity");
            assert_true(near(lo.x, 0, 1e-9) && near(hi.x, 60, 1e-9) && near(lo.y, 0, 1e-9) && near(hi.y, 70, 1e-9), where + ": entity extents");
            std::vector<uint8_t> out = d.save_to_memory(format);
            assert_true(!out.empty(), where + ": " + d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
            Point3 min, max;
            assert_true(header_point(r, "$EXTMIN", min) && header_point(r, "$EXTMAX", max), where + ": $EXTMIN and $EXTMAX");
            assert_true(near(min.x, 0, 1e-9) && near(max.x, 60, 1e-9) && near(min.y, 0, 1e-9) && near(max.y, 70, 1e-9),
                        where + ": the drawing's extents are the graphics' (" + std::to_string(min.x) + ", " + std::to_string(max.x) + ")");
            // read back from the file the extents are the same
            for (const auto& e : r.entities()) {
                if (e.type != hdxf::EntityType::ProxyEntity) continue;
                Point3 a, b;
                assert_true(r.entity_extents(e, a, b), where + ": extents of the entity read back");
                assert_true(near(a.x, 0, 1e-9) && near(b.y, 70, 1e-9), where + ": read back");
            }
        }
    }
}

TEST(proxy_extents_a_proxy_entity_without_graphics_adds_nothing_and_an_unreadable_stream_too) {
    hdxf::Drawing d(DxfVersion::R2010);
    d.add_entity(proxy_entity_with({}));
    Point3 lo, hi;
    assert_false(d.entity_extents(d.entities().back(), lo, hi), "no graphics, no extents");
    // bytes that are not a stream
    hdxf::Drawing e(DxfVersion::R2010);
    e.add_entity(proxy_entity_with({9, 9, 9, 9, 9, 9, 9, 9, 9, 9}));
    assert_false(e.entity_extents(e.entities().back(), lo, hi), "a stream that is no stream draws nothing");
}

TEST(proxy_extents_a_proxy_entity_in_a_rotated_scaled_block_reference_and_in_paper_space) {
    ProxyGraphics g;
    g.circle({0, 0, 0}, 5, {0, 0, 1});
    hdxf::Drawing d(DxfVersion::R2018);
    hdxf::Block& block = d.add_block("PROXIES");
    block.entities.push_back(proxy_entity_with(g.bytes()));
    hdxf::Entity& insert = d.add_insert("PROXIES", {100, 100, 0});
    insert.as_insert().x_scale = 2.0;
    insert.as_insert().y_scale = 2.0;
    insert.as_insert().rotation = 30.0;
    Point3 lo, hi;
    assert_true(d.entity_extents(insert, lo, hi), "the insert's extents");
    // An INSERT turned by an angle that is not a multiple of 90 degrees has the exact extents of its entities, as
    // AutoCAD measures them (step 9.2, facit acad_*_dxf_insert_rotated): the circle scaled by 2 is one of radius 10
    // about (100, 100), whatever the turn
    const double reach = 10.0;
    assert_true(near(lo.x, 100 - reach, 0.01) && near(hi.x, 100 + reach, 0.01) && near(lo.y, 100 - reach, 0.01) &&
                    near(hi.y, 100 + reach, 0.01),
                "the circle through the INSERT: " + std::to_string(lo.x) + " " + std::to_string(hi.x));
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Binary);
    assert_true(!out.empty(), d.last_error());
    hdxf::Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
    Point3 min, max;
    assert_true(header_point(r, "$EXTMIN", min) && header_point(r, "$EXTMAX", max), "header");
    assert_true(near(min.x, 100 - reach, 0.01) && near(max.y, 100 + reach, 0.01), "the drawing's extents through the INSERT");
    // paper space has its own extents ($PEXTMIN)
    hdxf::Drawing p(DxfVersion::R2018);
    hdxf::Entity e = proxy_entity_with(g.bytes());
    e.props.paper_space = 1;
    p.add_entity(e);
    std::vector<uint8_t> paper = p.save_to_memory(DxfFormat::Ascii);
    assert_true(!paper.empty(), p.last_error());
    hdxf::Drawing pr;
    assert_true(pr.load_from_memory(paper.data(), paper.size()), pr.last_error());
    Point3 pmin, pmax;
    assert_true(header_point(pr, "$PEXTMIN", pmin) && header_point(pr, "$PEXTMAX", pmax), "paper header");
    assert_true(near(pmin.x, -5, 0.01) && near(pmax.x, 5, 0.01), "the paper space extents: " + std::to_string(pmin.x));
}

TEST(proxy_extents_an_ole2frame_adds_its_frame_and_an_oleframe_none) {
    for (DxfVersion version : {DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2013}) {
        hdxf::Drawing d(version);
        hdxf::Entity e;
        e.type = hdxf::EntityType::Ole2Frame;
        e.props.layer = "0";
        hdxf::Ole2FrameData f;
        f.application_name = "Frame";
        f.upper_left = {10, 40, 0};
        f.lower_right = {30, 5, 0};
        f.data = {1, 2, 3, 4};
        e.data = f;
        d.add_entity(e);
        Point3 lo, hi;
        assert_true(d.entity_extents(d.entities().back(), lo, hi), "extents of the frame");
        assert_true(near(lo.x, 10, 1e-12) && near(hi.x, 30, 1e-12) && near(lo.y, 5, 1e-12) && near(hi.y, 40, 1e-12), "the frame's corners");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        hdxf::Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
        Point3 min, max;
        assert_true(header_point(r, "$EXTMIN", min) && header_point(r, "$EXTMAX", max), "header");
        assert_true(near(min.x, 10, 1e-9) && near(max.x, 30, 1e-9) && near(min.y, 5, 1e-9) && near(max.y, 40, 1e-9),
                    std::string(hdxf_test::version_label(version)) + ": the drawing's extents are the frame");
    }
    // OLEFRAME has no corners: nothing is added (its geometry source is not known, F9.1)
    hdxf::Drawing d(DxfVersion::R2000);
    hdxf::Entity e;
    e.type = hdxf::EntityType::OleFrame;
    e.props.layer = "0";
    e.data = hdxf::OleFrameData{};
    d.add_entity(e);
    Point3 lo, hi;
    assert_false(d.entity_extents(d.entities().back(), lo, hi), "an OLEFRAME adds nothing");
}

TEST(proxy_extents_text_of_a_proxy_graphics_stream_is_a_box_of_its_font_or_its_position) {
    ProxyText t;
    t.position = {10, 20, 0};
    t.text = "Hello";
    t.height = 2.5;
    t.font = "txt";
    for (bool unicode : {false, true}) {
        ProxyGraphics g;
        g.text(t, unicode, 1252);
        std::vector<uint8_t> stream = g.bytes();
        ProxyStreamView v = decode_proxy_stream(stream);
        assert_true(v.complete, v.problem);
        assert_eq(unicode ? 38 : 11, static_cast<int>(v.commands[0].type), "the command");
        Box b = drawn_box(stream);
        assert_false(b.empty(), "the text draws");
        // the pen starts at the position: the box starts there (or at the font's left bearing) and is as high as the font's ink
        assert_true(b.lo.x >= 10 - 0.5 && b.lo.x <= 10 + 0.5, "the box starts at the pen: " + describe(b));
        assert_true(b.hi.y >= 20 && b.hi.y <= 20 + 2.5 * 1.5, "the box is about a text height high: " + describe(b));
        // where no font is found the position is what is known; where one is, the box is wider than a point
        if (b.hi.x - b.lo.x > 0) assert_true(b.hi.x - b.lo.x > 2.5, "five letters are wider than their height: " + describe(b));
    }
}
