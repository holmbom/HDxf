#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "group_codes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// HATCH and IMAGE (with IMAGEDEF, IMAGEDEF_REACTOR and RASTERVARIABLES) in DXF R13 and R14 (plan
// phase 7, steps 7.14 and 7.15, B94, D8 and D9 of doc/reference/hdxf_r13_r14_format.md).
//
// R14 has both, in the form of the R14 DXF Reference (hatch, boundary path data, pattern data,
// image, imagedef, imagedef_reactor, rastervariables) and of AutoCAD's own R14 files: the five
// HATCHes of BL15-1 (AutoCAD 2000 saving as R14) and the image objects of altera_yeager_io.
// R13 has neither: "HATCH requires DXF R14 or later", "IMAGE requires DXF R14 or later".
//
// What AutoCAD's files do not show is said where the test checks it. No golden files: the
// expected groups are written out here from the reference and the files, never from HDxf.
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using Rec = dxf_structure::Rec;
using Group = dxf_structure::Group;

const DxfFormat kFormats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), what + ": " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data, const std::string& what) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), what + ": " + dwg.last_error());
    return dwg;
}

hdxf::Drawing load_file(const std::filesystem::path& path) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(path), path.string() + ": " + dwg.last_error());
    return dwg;
}

// The records of an ASCII file, groups with their numbers without alignment
std::vector<Rec> records(const std::vector<uint8_t>& data) {
    std::vector<Rec> out = dxf_structure::records_of(data);
    for (auto& r : out) {
        for (auto& g : r.groups) g.second = dxf_structure::number_text(g.first, g.second);
    }
    return out;
}

const Rec* by_handle(const std::vector<Rec>& recs, const std::string& handle) {
    for (const auto& r : recs) {
        for (const auto& g : r.groups) {
            if ((g.first == 5 || g.first == 105) && g.second == handle) return &r;
            if (g.first == 100) break;
        }
    }
    return nullptr;
}

std::vector<const Rec*> all_of(const std::vector<Rec>& recs, const std::string& type, const std::string& section) {
    std::vector<const Rec*> out;
    for (const auto& r : recs) {
        if (r.type() == type && r.section == section) out.push_back(&r);
    }
    return out;
}

std::vector<std::string> values(const Rec& r, int code) {
    std::vector<std::string> out;
    for (const auto& g : r.groups) if (g.first == code) out.push_back(g.second);
    return out;
}

// The groups after the subclass marker `100 <subclass>`, up to the next marker or the XDATA
std::vector<Group> after_marker(const Rec& r, const std::string& subclass) {
    std::vector<Group> out;
    bool in = false;
    for (const auto& g : r.groups) {
        if (g.first == 100) {
            if (in) break;
            in = g.second == subclass;
            continue;
        }
        if (in && g.first >= 1000) break;
        if (in) out.push_back(g);
    }
    return out;
}

std::vector<int> codes_of(const std::vector<Group>& groups) {
    std::vector<int> out;
    for (const auto& g : groups) out.push_back(g.first);
    return out;
}

std::string codes_text(const std::vector<int>& codes) {
    std::string s;
    for (int c : codes) s += std::to_string(c) + " ";
    return s;
}

// The 330 handles of the `102 {ACAD_REACTORS` group
std::vector<std::string> reactors_of(const Rec& r) {
    std::vector<std::string> out;
    bool in = false;
    for (const auto& g : r.groups) {
        if (g.first == 102) {
            in = g.second == "{ACAD_REACTORS";
            continue;
        }
        if (in && g.first == 330) out.push_back(g.second);
    }
    return out;
}

bool has_code(const Rec& r, int code) {
    return std::any_of(r.groups.begin(), r.groups.end(), [&](const Group& g) { return g.first == code; });
}

// The `3` names of a dictionary, in order
std::vector<std::string> dictionary_names(const Rec& r) { return values(r, 3); }

// The handle a dictionary has for an entry
std::string dictionary_entry(const Rec& r, const std::string& name) {
    for (size_t i = 0; i + 1 < r.groups.size(); ++i) {
        if (r.groups[i].first == 3 && r.groups[i].second == name) return r.groups[i + 1].second;
    }
    return {};
}

const Rec* root_dictionary(const std::vector<Rec>& recs) {
    auto all = all_of(recs, "DICTIONARY", "OBJECTS");
    return all.empty() ? nullptr : all.front();
}

// The classes of a file: DXF name -> the record
const Rec* class_named(const std::vector<Rec>& recs, const std::string& name) {
    for (const auto& r : recs) {
        if (r.type() == "CLASS" && r.value(1) == name) return &r;
    }
    return nullptr;
}

double number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

// Two groups are the same: a double by its value, anything else by its text
bool same_group(const Group& a, const Group& b, double relative = 0.0) {
    if (a.first != b.first) return false;
    if (hdxf::internal::group_value_type(a.first) == hdxf::internal::GroupValueType::Double) {
        double x = number(a.second);
        double y = number(b.second);
        return x == y || std::fabs(x - y) <= relative * std::max(std::fabs(x), std::fabs(y));
    }
    return a.second == b.second;
}

std::string group_text(const Group& g) { return std::to_string(g.first) + " " + g.second; }

// The groups of a record from its `100 AcDbEntity` on (what an owner and reactors before it
// say is the family's and the file's own)
std::vector<Group> from_entity(const Rec& r) {
    std::vector<Group> out;
    bool in = false;
    for (const auto& g : r.groups) {
        if (g.first == 100) in = true;
        if (in) out.push_back(g);
    }
    return out;
}

void assert_same_groups(const std::vector<Group>& expected, const std::vector<Group>& actual, const std::string& what,
                        double relative = 0.0) {
    size_t n = std::min(expected.size(), actual.size());
    for (size_t i = 0; i < n; ++i) {
        hdxf_test::assert_true(same_group(expected[i], actual[i], relative),
                               what + ": group " + std::to_string(i) + " is \"" + group_text(actual[i]) +
                                   "\", expected \"" + group_text(expected[i]) + "\"");
    }
    hdxf_test::assert_eq(static_cast<int>(expected.size()), static_cast<int>(actual.size()),
                         what + ": the number of groups");
}

hdxf::HatchBoundaryPath polyline_path(int flags, std::vector<hdxf::HatchVertex> vertices, bool closed = true) {
    hdxf::HatchBoundaryPath p;
    p.flags = flags;
    p.closed = closed;
    p.vertices = std::move(vertices);
    return p;
}

hdxf::HatchEdge line_edge(hdxf::Point2 a, hdxf::Point2 b) {
    hdxf::HatchEdge e;
    e.start = a;
    e.end = b;
    return e;
}

hdxf::HatchEdge arc_edge(hdxf::Point2 center, double radius, double from, double to) {
    hdxf::HatchEdge e;
    e.type = hdxf::HatchEdgeType::Arc;
    e.center = center;
    e.radius = radius;
    e.start_angle = from;
    e.end_angle = to;
    return e;
}

const hdxf::Entity& last_hatch(const hdxf::Drawing& dwg) {
    for (auto it = dwg.entities().rbegin(); it != dwg.entities().rend(); ++it) {
        if (it->type == hdxf::EntityType::Hatch) return *it;
    }
    hdxf_test::assert_true(false, "no HATCH");
    return dwg.entities().front();
}

std::vector<const hdxf::Entity*> images_of(const hdxf::Drawing& dwg) {
    std::vector<const hdxf::Entity*> out;
    for (const auto& e : dwg.entities()) if (e.type == hdxf::EntityType::Image) out.push_back(&e);
    return out;
}

// The dictionary that lists a record (an IMAGEDEF's ACAD_IMAGE_DICT)
std::string holder_of(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& o : dwg.objects()) {
        if (o.type != hdxf::ObjectType::Dictionary) continue;
        for (const auto& e : o.as_dictionary().entries) {
            if (e.handle == handle) return o.handle;
        }
    }
    return {};
}

int count_objects(const hdxf::Drawing& dwg, hdxf::ObjectType type) {
    int n = 0;
    for (const auto& o : dwg.objects()) n += o.type == type;
    return n;
}

} // namespace

// ===========================================================================
// HATCH, R14
// ===========================================================================

// A solid fill with a polyline loop with and without bulge, as AutoCAD's BL15-1 writes it:
// no pattern groups, 75 and 76, the pixel size, the seed points, nothing of 2004's gradient
TEST(hatch_r14_solid_polyline_groups) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 solid hatch " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        hdxf::Entity& h = dwg.entities().back();
        h.as_hatch().paths[0].flags = 7;                // external, polyline, derived
        h.as_hatch().paths[0].vertices[1].bulge = 0.5;
        h.as_hatch().pixel_size = 0.33;
        h.as_hatch().seed_points = {{5, 5}};
        h.props.color = hdxf::Color::from_aci(2);
        h.props.layer = "HATCHES";
        dwg.add_hatch({{20, 0}, {30, 0}, {30, 10}});     // no bulge: 72 0, no 42

        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 2, what);
        const auto& a = back.entities()[0];
        hdxf_test::assert_true(a.type == hdxf::EntityType::Hatch, what + ": type");
        const auto& d = a.as_hatch();
        hdxf_test::assert_true(d.solid, what + ": solid");
        hdxf_test::assert_eq(d.pattern_name, "SOLID", what);
        hdxf_test::assert_eq(static_cast<int>(d.paths.size()), 1, what);
        hdxf_test::assert_eq(d.paths[0].flags, 7, what + ": 92");
        hdxf_test::assert_true(d.paths[0].closed, what + ": closed");
        hdxf_test::assert_eq(static_cast<int>(d.paths[0].vertices.size()), 4, what);
        hdxf_test::assert_eq(d.paths[0].vertices[1].bulge, 0.5, what + ": bulge");
        hdxf_test::assert_eq(d.paths[0].vertices[2].position.y, 10.0, what);
        hdxf_test::assert_eq(d.pixel_size, 0.33, what + ": 47");
        hdxf_test::assert_eq(static_cast<int>(d.seed_points.size()), 1, what);
        hdxf_test::assert_eq(d.seed_points[0].x, 5.0, what);
        hdxf_test::assert_eq(a.props.layer, "HATCHES", what);
        hdxf_test::assert_eq(a.props.color.aci, 2, what + ": 62");
        hdxf_test::assert_false(back.entities()[1].as_hatch().paths[0].vertices.empty(), what);
        if (format == DxfFormat::Binary) continue;

        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        auto hatches = all_of(recs, "HATCH", "ENTITIES");
        hdxf_test::assert_eq(static_cast<int>(hatches.size()), 2, what);
        const Rec& r = *hatches[0];
        // AutoCAD's head: AcDbEntity, the layer, the color, then AcDbHatch (BL15-1 handle 62)
        std::vector<Group> head = from_entity(r);
        hdxf_test::assert_eq(head[0].second, "AcDbEntity", what);
        hdxf_test::assert_eq(head[1].second, "HATCHES", what);
        hdxf_test::assert_eq(head[2].first, 62, what);
        hdxf_test::assert_eq(head[3].second, "AcDbHatch", what);
        // 10 20 30 (the elevation) and 210 220 230 (always), 2, 70, 71, 91 and the loop, 75, 76, 47, 98, 10 20
        std::vector<int> expected = {10, 20, 30, 210, 220, 230, 2, 70, 71, 91, 92, 72, 73, 93, 10, 20,
                                     42, 10, 20, 42, 10, 20, 42, 10, 20, 42, 97, 75, 76, 47, 98, 10, 20};
        std::vector<int> actual = codes_of(after_marker(r, "AcDbHatch"));
        hdxf_test::assert_true(actual == expected, what + ": codes " + codes_text(actual));
        hdxf_test::assert_eq(r.value(2), "SOLID", what);
        hdxf_test::assert_eq(r.value(70), "1", what);
        hdxf_test::assert_eq(r.value(71), "0", what + ": not associative");
        hdxf_test::assert_eq(r.value(92), "7", what);
        hdxf_test::assert_eq(r.value(72), "1", what + ": has bulge");
        hdxf_test::assert_eq(r.value(73), "1", what + ": closed");
        hdxf_test::assert_eq(r.value(97), "0", what + ": no sources");
        // No loop without a bulge has 42, and nothing of the 2004 gradient (450 .. 470, 63, 421) is there
        const Rec& plain = *hatches[1];
        hdxf_test::assert_eq(plain.value(72), "0", what + ": no bulge");
        std::vector<int> plain_codes = codes_of(after_marker(plain, "AcDbHatch"));
        hdxf_test::assert_true(std::find(plain_codes.begin(), plain_codes.end(), 42) == plain_codes.end(), what + ": 42");
        for (const Rec* h2 : hatches) {
            for (int code : {450, 451, 452, 453, 460, 461, 462, 463, 470, 63, 421}) {
                hdxf_test::assert_false(has_code(*h2, code), what + ": 2004 group " + std::to_string(code));
            }
        }
        // 98 is the last count, a 2000 file has nothing but the groups of the reference after it
        hdxf_test::assert_eq(codes_of(after_marker(r, "AcDbHatch")).back(), 20, what + ": the last group is a seed point");
    }
}

// A pattern fill: 52, 41, 77, 78 and the lines of 53, 43, 44, 45, 46, 79, 49 (R14 reference, pattern data)
TEST(hatch_r14_pattern_groups) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 pattern hatch " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        auto& d = dwg.entities().back().as_hatch();
        d.solid = false;
        d.pattern_name = "ANSI31";
        d.pattern_type = 1;
        d.pattern_angle = 15.0;
        d.pattern_scale = 2.5;
        d.pattern_double = true;
        d.style = 1;
        d.pattern_lines.push_back({45.0, {0, 0}, {-0.1767766952966369, 0.1767766952966369}, {}});
        d.pattern_lines.push_back({135.0, {1.0, 2.0}, {0.5, 0.5}, {1.5, -0.5, 0.25}});
        d.seed_points = {{5, 5}, {6, 6}};

        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        const auto& b = last_hatch(back).as_hatch();
        hdxf_test::assert_false(b.solid, what);
        hdxf_test::assert_eq(b.pattern_name, "ANSI31", what);
        hdxf_test::assert_eq(b.pattern_type, 1, what);
        hdxf_test::assert_eq(b.pattern_angle, 15.0, what);
        hdxf_test::assert_eq(b.pattern_scale, 2.5, what);
        hdxf_test::assert_true(b.pattern_double, what);
        hdxf_test::assert_eq(b.style, 1, what);
        hdxf_test::assert_eq(static_cast<int>(b.pattern_lines.size()), 2, what);
        hdxf_test::assert_eq(b.pattern_lines[0].offset.x, -0.1767766952966369, what);
        hdxf_test::assert_eq(b.pattern_lines[1].base_point.y, 2.0, what);
        hdxf_test::assert_eq(static_cast<int>(b.pattern_lines[1].dashes.size()), 3, what);
        hdxf_test::assert_eq(b.pattern_lines[1].dashes[1], -0.5, what);
        hdxf_test::assert_eq(static_cast<int>(b.seed_points.size()), 2, what);
        if (format == DxfFormat::Binary) continue;

        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        const Rec& r = *all_of(recs, "HATCH", "ENTITIES")[0];
        std::vector<int> expected = {10, 20, 30, 210, 220, 230, 2, 70, 71, 91, 92, 72, 73, 93, 10, 20, 10, 20, 10, 20,
                                     10, 20, 97, 75, 76, 52, 41, 77, 78, 53, 43, 44, 45, 46, 79, 53, 43, 44, 45, 46,
                                     79, 49, 49, 49, 98, 10, 20, 10, 20};
        std::vector<int> actual = codes_of(after_marker(r, "AcDbHatch"));
        hdxf_test::assert_true(actual == expected, what + ": codes " + codes_text(actual));
        hdxf_test::assert_eq(r.value(70), "0", what + ": pattern fill");
        hdxf_test::assert_eq(r.value(75), "1", what);
        hdxf_test::assert_eq(r.value(77), "1", what);
        hdxf_test::assert_eq(r.value(78), "2", what);
    }
}

// Islands: an outer loop and two inner ones, as BL15-1's hatch 62 has three loops with 92 7
TEST(hatch_r14_islands) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 islands " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({{0, 0}, {100, 0}, {100, 100}, {0, 100}});
        auto& d = dwg.entities().back().as_hatch();
        d.paths[0].flags = 7;
        d.paths.push_back(polyline_path(2, {{10, 10}, {30, 10}, {30, 30}, {10, 30}}));
        d.paths.push_back(polyline_path(18, {{60, 60}, {80, 60}, {80, 80}}));       // polyline, outermost
        d.paths.push_back(polyline_path(2, {{40, 10}, {45, 10}}, false));
        d.style = 0;

        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        const auto& b = last_hatch(back).as_hatch();
        hdxf_test::assert_eq(static_cast<int>(b.paths.size()), 4, what);
        hdxf_test::assert_eq(b.paths[0].flags, 7, what);
        hdxf_test::assert_eq(b.paths[1].flags, 2, what);
        hdxf_test::assert_eq(b.paths[2].flags, 18, what + ": outermost");
        hdxf_test::assert_false(b.paths[3].closed, what + ": an open polyline loop");
        hdxf_test::assert_eq(static_cast<int>(b.paths[2].vertices.size()), 3, what);
        hdxf_test::assert_eq(b.paths[3].vertices[1].position.x, 45.0, what);
        if (format == DxfFormat::Binary) continue;

        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        const Rec& r = *all_of(recs, "HATCH", "ENTITIES")[0];
        hdxf_test::assert_eq(r.value(91), "4", what + ": four loops");
        std::vector<std::string> flags = values(r, 92);
        hdxf_test::assert_true(flags == std::vector<std::string>({"7", "2", "18", "2"}), what + ": 92");
        std::vector<std::string> vertex_counts = values(r, 93);
        hdxf_test::assert_true(vertex_counts == std::vector<std::string>({"4", "4", "3", "2"}), what + ": 93");
        std::vector<std::string> closed = values(r, 73);
        hdxf_test::assert_true(closed == std::vector<std::string>({"1", "1", "1", "0"}), what + ": 73");
    }
}

// The four edge types of the R14 reference: line (10 11), circular arc (10 40 50 51 73), elliptic arc
// (10 11 40 50 51 73) and spline (94 73 74 95 96 40 10 [42]) — no fit points, they are 2010's
TEST(hatch_r14_edge_types) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 edge hatch " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({});
        auto& d = dwg.entities().back().as_hatch();
        d.solid = false;
        d.pattern_name = "ANSI31";
        d.pattern_lines.push_back({45.0, {0, 0}, {-0.1, 0.1}, {}});
        hdxf::HatchBoundaryPath edges;
        edges.flags = 1;
        hdxf::HatchEdge ellipse;
        ellipse.type = hdxf::HatchEdgeType::Ellipse;
        ellipse.center = {10, 40};
        ellipse.major_axis = {10, 0};
        ellipse.ratio = 0.5;
        ellipse.start_angle = 0;
        ellipse.end_angle = 180;
        ellipse.counterclockwise = false;
        hdxf::HatchEdge curve;
        curve.type = hdxf::HatchEdgeType::Spline;
        curve.degree = 2;
        curve.rational = true;
        curve.periodic = false;
        curve.knots = {0, 0, 0, 1, 1, 1};
        curve.control_points = {{0, 40}, {-5, 30}, {0, 20}};
        curve.weights = {1.0, 0.5, 1.0};
        edges.edges = {line_edge({0, 20}, {20, 20}), arc_edge({20, 30}, 10, 270, 90), ellipse, curve};
        d.paths = {edges};
        d.seed_points = {{10, 30}};

        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        const auto& b = last_hatch(back).as_hatch();
        hdxf_test::assert_eq(static_cast<int>(b.paths.size()), 1, what);
        hdxf_test::assert_false(b.paths[0].is_polyline(), what);
        const auto& e = b.paths[0].edges;
        hdxf_test::assert_eq(static_cast<int>(e.size()), 4, what);
        hdxf_test::assert_true(e[0].type == hdxf::HatchEdgeType::Line, what);
        hdxf_test::assert_eq(e[0].end.x, 20.0, what);
        hdxf_test::assert_true(e[1].type == hdxf::HatchEdgeType::Arc, what);
        hdxf_test::assert_eq(e[1].radius, 10.0, what);
        hdxf_test::assert_eq(e[1].start_angle, 270.0, what);
        hdxf_test::assert_eq(e[1].end_angle, 90.0, what);
        hdxf_test::assert_true(e[1].counterclockwise, what);
        hdxf_test::assert_true(e[2].type == hdxf::HatchEdgeType::Ellipse, what);
        hdxf_test::assert_eq(e[2].ratio, 0.5, what);
        hdxf_test::assert_eq(e[2].major_axis.x, 10.0, what);
        hdxf_test::assert_false(e[2].counterclockwise, what + ": 73 0");
        hdxf_test::assert_true(e[3].type == hdxf::HatchEdgeType::Spline, what);
        hdxf_test::assert_eq(e[3].degree, 2, what);
        hdxf_test::assert_true(e[3].rational, what);
        hdxf_test::assert_eq(static_cast<int>(e[3].knots.size()), 6, what);
        hdxf_test::assert_eq(e[3].control_points[1].x, -5.0, what);
        hdxf_test::assert_eq(e[3].weights[1], 0.5, what);
        hdxf_test::assert_true(e[3].fit_points.empty(), what + ": no fit points");
        if (format == DxfFormat::Binary) continue;

        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        const Rec& r = *all_of(recs, "HATCH", "ENTITIES")[0];
        // From 91: the loop (92 93, then per edge 72 and its groups) and 97
        std::vector<int> expected = {10, 20, 30, 210, 220, 230, 2, 70, 71, 91, 92, 93,
                                     72, 10, 20, 11, 21,                                 // line
                                     72, 10, 20, 40, 50, 51, 73,                         // circular arc
                                     72, 10, 20, 11, 21, 40, 50, 51, 73,                 // elliptic arc
                                     72, 94, 73, 74, 95, 96, 40, 40, 40, 40, 40, 40,     // spline: knots
                                     10, 20, 42, 10, 20, 42, 10, 20, 42,                 // control points and weights
                                     97, 75, 76, 52, 41, 77, 78, 53, 43, 44, 45, 46, 79, 98, 10, 20};
        std::vector<int> actual = codes_of(after_marker(r, "AcDbHatch"));
        hdxf_test::assert_true(actual == expected, what + ": codes " + codes_text(actual));
        std::vector<std::string> edge_types = values(r, 72);
        hdxf_test::assert_true(edge_types == std::vector<std::string>({"1", "2", "3", "4"}), what + ": 72");
    }
}

// A spline edge without weights has no 42; the knots and control points are as given
TEST(hatch_r14_spline_edge_without_weights) {
    hdxf::Drawing dwg(DxfVersion::R14);
    dwg.add_hatch({});
    hdxf::HatchBoundaryPath edges;
    edges.flags = 1;
    hdxf::HatchEdge curve;
    curve.type = hdxf::HatchEdgeType::Spline;
    curve.degree = 3;
    curve.knots = {0, 0, 0, 0, 1, 1, 1, 1};
    curve.control_points = {{0, 0}, {1, 2}, {2, 2}, {3, 0}};
    edges.edges = {curve, line_edge({3, 0}, {0, 0})};
    dwg.entities().back().as_hatch().paths = {edges};
    std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, "spline edge");
    std::vector<Rec> recs = records(out);
    const Rec& r = *all_of(recs, "HATCH", "ENTITIES")[0];
    hdxf_test::assert_false(has_code(r, 42), "no weights, no 42");
    hdxf_test::assert_false(has_code(r, 12), "no tangents (2010)");
    // After the spline's last control point comes the next edge's 72, not a fit point count
    std::vector<int> actual = codes_of(after_marker(r, "AcDbHatch"));
    std::vector<int> expected = {10, 20, 30, 210, 220, 230, 2, 70, 71, 91, 92, 93, 72, 94, 73, 74, 95, 96,
                                 40, 40, 40, 40, 40, 40, 40, 40, 10, 20, 10, 20, 10, 20, 10, 20, 72, 10, 20, 11, 21,
                                 97, 75, 76, 98};   // no derived path: no 47 (D13.16)
    hdxf_test::assert_true(actual == expected, "codes " + codes_text(actual));
    hdxf::Drawing back = load(out, "spline edge");
    hdxf_test::assert_eq(static_cast<int>(last_hatch(back).as_hatch().paths[0].edges.size()), 2, "edges");
}

// A hatch in a block, in paper space and with its own linetype and scale: everything the head has
TEST(hatch_r14_in_block_and_paper_space) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 hatch in a block " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        hdxf::Block& block = dwg.add_block("FILL");
        hdxf::Entity h;
        h.type = hdxf::EntityType::Hatch;
        hdxf::HatchData d;
        d.paths.push_back(polyline_path(3, {{0, 0}, {1, 0}, {1, 1}}));
        h.data = d;
        block.entities.push_back(h);
        dwg.add_insert("FILL", {5, 5, 0});
        dwg.add_hatch({{0, 0}, {2, 0}, {2, 2}});
        dwg.entities().back().props.paper_space = 1;
        dwg.entities().back().props.linetype_scale = 2.0;

        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        const hdxf::Block* fill = back.find_block("FILL");
        hdxf_test::assert_true(fill != nullptr, what);
        hdxf_test::assert_eq(static_cast<int>(fill->entities.size()), 1, what);
        hdxf_test::assert_true(fill->entities[0].type == hdxf::EntityType::Hatch, what);
        hdxf_test::assert_eq(last_hatch(back).props.paper_space, 1, what + ": 67");
        hdxf_test::assert_eq(last_hatch(back).props.linetype_scale, 2.0, what + ": 48");
        if (format == DxfFormat::Binary) continue;
        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        auto in_blocks = all_of(recs, "HATCH", "BLOCKS");
        hdxf_test::assert_eq(static_cast<int>(in_blocks.size()), 1, what);
        auto in_entities = all_of(recs, "HATCH", "ENTITIES");
        hdxf_test::assert_eq(static_cast<int>(in_entities.size()), 1, what);
        std::vector<Group> head = from_entity(*in_entities[0]);
        hdxf_test::assert_eq(head[0].second, "AcDbEntity", what);
        hdxf_test::assert_eq(head[1].first, 67, what);
        hdxf_test::assert_eq(head[1].second, "1", what);
    }
}

// Associative: the loops' source objects are listed (97, 330) and have the hatch as reactor. A source
// that is erased leaves its loop, the hatch stays associative while another loop has its source and is
// not any more when none has (B19; AutoCAD's hatch behaves so when its boundary is erased).
TEST(hatch_r14_associative_and_erased_boundary) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 associative hatch " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_circle({0, 0, 0}, 10);
        dwg.add_circle({0, 0, 0}, 4);
        dwg.add_hatch({});
        auto& d = dwg.entities().back().as_hatch();
        d.associative = true;
        hdxf::HatchBoundaryPath outer;
        outer.flags = 1;
        outer.edges = {arc_edge({0, 0}, 10, 0, 360)};
        hdxf::HatchBoundaryPath inner = outer;
        inner.flags = 0;
        inner.edges = {arc_edge({0, 0}, 4, 0, 360)};
        d.paths = {outer, inner};
        d.seed_points = {{7, 0}};

        // The boundary objects get their handles on the first save; then they are the sources
        save(dwg, format, what + " (first)");
        std::string outer_handle = dwg.entities()[0].props.handle;
        std::string inner_handle = dwg.entities()[1].props.handle;
        std::string hatch_handle = dwg.entities()[2].props.handle;
        hdxf_test::assert_false(outer_handle.empty() || inner_handle.empty(), what);
        dwg.entities()[2].as_hatch().paths[0].source_handles = {outer_handle};
        dwg.entities()[2].as_hatch().paths[1].source_handles = {inner_handle};

        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        const auto& b = last_hatch(back).as_hatch();
        hdxf_test::assert_true(b.associative, what);
        hdxf_test::assert_eq(b.paths[0].source_handles.at(0), outer_handle, what);
        hdxf_test::assert_eq(b.paths[1].source_handles.at(0), inner_handle, what);
        if (format == DxfFormat::Ascii) {
            dxf_structure::check_structure(out, what);
            std::vector<Rec> recs = records(out);
            const Rec& hatch = *by_handle(recs, hatch_handle);
            hdxf_test::assert_eq(hatch.value(71), "1", what);
            std::vector<std::string> sources = values(hatch, 97);
            hdxf_test::assert_true(sources == std::vector<std::string>({"1", "1"}), what + ": 97");
            // The boundary has the hatch as its reactor, in the group the reference gives them
            for (const std::string& h : {outer_handle, inner_handle}) {
                const Rec& circle = *by_handle(recs, h);
                std::vector<std::string> r = reactors_of(circle);
                hdxf_test::assert_true(std::find(r.begin(), r.end(), hatch_handle) != r.end(),
                                       what + ": " + h + " has the hatch as reactor");
            }
            // 330 in the hatch: the owner of the family and the two sources (the sources only after 97)
            const std::vector<Group> tail = after_marker(hatch, "AcDbHatch");
            int sources_330 = 0;
            for (const auto& g : tail) sources_330 += g.first == 330;
            hdxf_test::assert_eq(sources_330, 2, what + ": the two sources are the only 330 of AcDbHatch");
        }

        // The inner boundary erased (a drawing read from the file: its relations follow)
        {
            std::string erased = what + ", the inner boundary erased";
            auto& ents = back.entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(),
                                      [&](const hdxf::Entity& e) { return e.props.handle == inner_handle; }),
                       ents.end());
            std::vector<uint8_t> out2 = save(back, format, erased);
            hdxf::Drawing again = load(out2, erased);
            const auto& h2 = last_hatch(again).as_hatch();
            hdxf_test::assert_true(h2.associative, erased + ": still associative");
            hdxf_test::assert_eq(static_cast<int>(h2.paths.size()), 2, erased + ": both loops stay");
            hdxf_test::assert_eq(static_cast<int>(h2.paths[0].source_handles.size()), 1, erased);
            hdxf_test::assert_eq(static_cast<int>(h2.paths[1].source_handles.size()), 0, erased + ": the erased source left its loop");
            hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(back.warnings()).empty(), erased + ": no pointer kept");
            if (format == DxfFormat::Ascii) {
                dxf_structure::check_structure(out2, erased);
                hdxf_test::assert_true(by_handle(records(out2), inner_handle) == nullptr, erased + ": the circle is gone");
                for (const auto& r : records(out2)) {
                    for (const auto& g : r.groups) {
                        if (g.first == 330 || g.first == 340) hdxf_test::assert_true(g.second != inner_handle, erased + ": a pointer to it");
                    }
                }
            }

            // And the outer one: the hatch is not associative any more
            std::string both = what + ", both boundaries erased";
            auto& ents2 = again.entities();
            ents2.erase(std::remove_if(ents2.begin(), ents2.end(),
                                       [&](const hdxf::Entity& e) { return e.props.handle == outer_handle; }),
                        ents2.end());
            std::vector<uint8_t> out3 = save(again, format, both);
            hdxf::Drawing last = load(out3, both);
            const auto& h3 = last_hatch(last).as_hatch();
            hdxf_test::assert_false(h3.associative, both + ": 71 0");
            hdxf_test::assert_eq(static_cast<int>(h3.paths.size()), 2, both);
            for (const auto& p : h3.paths) hdxf_test::assert_true(p.source_handles.empty(), both);
            if (format == DxfFormat::Ascii) dxf_structure::check_structure(out3, both);
        }
    }
}

// An unassociated hatch has 97 0 on every loop and nobody has it as a reactor; two hatches over the
// same boundary are both its reactors
TEST(hatch_r14_unassociated_and_two_hatches_one_boundary) {
    hdxf::Drawing dwg(DxfVersion::R14);
    dwg.add_circle({0, 0, 0}, 10);
    dwg.add_hatch({{0, 0}, {5, 0}, {5, 5}});
    save(dwg, DxfFormat::Ascii, "first");
    std::string circle = dwg.entities()[0].props.handle;
    std::vector<Rec> plain = records(save(dwg, DxfFormat::Ascii, "unassociated"));
    hdxf_test::assert_true(reactors_of(*by_handle(plain, circle)).empty(), "no reactor");
    hdxf_test::assert_eq(all_of(plain, "HATCH", "ENTITIES")[0]->value(97), "0", "97 0");

    for (int i = 0; i < 2; ++i) {
        hdxf::Entity& h = dwg.add_hatch({});
        h.as_hatch().associative = true;
        hdxf::HatchBoundaryPath p;
        p.flags = 1;
        p.edges = {arc_edge({0, 0}, 10, 0, 360)};
        p.source_handles = {circle};
        h.as_hatch().paths = {p};
    }
    std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, "two hatches");
    dxf_structure::check_structure(out, "two hatches");
    std::vector<Rec> recs = records(out);
    std::vector<std::string> r = reactors_of(*by_handle(recs, circle));
    hdxf_test::assert_eq(static_cast<int>(r.size()), 2, "two reactors");
    hdxf_test::assert_true(r[0] != r[1], "different hatches");
}

// BL15-1 is AutoCAD 2000's own R14 file with five HATCHes (solid, associative, polyline and edge
// loops, seed points, 47). A hatch of it written anew by HDxf has the groups AutoCAD wrote, from
// AcDbEntity to the last seed point, one by one.
TEST(hatch_r14_written_anew_as_autocad_bl15) {
    std::filesystem::path path = external("local-r13r14/BL15-1_davidsmarx_r14_dimension_hatch.dxf");
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("local test file missing: " + path.string());
        return;
    }
    hdxf::Drawing file = load_file(path);
    hdxf_test::assert_true(file.version() == DxfVersion::R14, "R14");
    std::vector<uint8_t> original_bytes;
    {
        std::ifstream in(path, std::ios::binary);
        original_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<Rec> original = records(original_bytes);
    auto original_hatches = all_of(original, "HATCH", "ENTITIES");
    hdxf_test::assert_eq(static_cast<int>(original_hatches.size()), 5, "five HATCHes in the file");

    // 1. The file written again as it is: each HATCH record keeps its groups
    for (DxfFormat format : {DxfFormat::Ascii}) {
        std::vector<uint8_t> again = save(file, format, "BL15-1 as read");
        std::vector<Rec> recs = records(again);
        auto hatches = all_of(recs, "HATCH", "ENTITIES");
        hdxf_test::assert_eq(static_cast<int>(hatches.size()), 5, "HATCHes written again");
        for (size_t i = 0; i < hatches.size(); ++i) {
            assert_same_groups(from_entity(*original_hatches[i]), from_entity(*hatches[i]),
                               "HATCH " + original_hatches[i]->value(5) + " as read");
        }
    }

    // 2. Each HATCH of the file as a typed entity of a new R14 drawing: written anew from its values
    std::vector<const hdxf::Entity*> typed;
    for (const auto& e : file.entities()) if (e.type == hdxf::EntityType::Hatch) typed.push_back(&e);
    hdxf_test::assert_eq(static_cast<int>(typed.size()), 5, "typed HATCHes");
    for (DxfFormat format : kFormats) {
        hdxf::Drawing dwg(DxfVersion::R14);
        // The boundary objects come along with their handles: a hatch of a drawing made here keeps only
        // the boundary objects the drawing has (B97)
        std::set<std::string> sources;
        for (const hdxf::Entity* h : typed) {
            for (const auto& path : h->as_hatch().paths) sources.insert(path.source_handles.begin(), path.source_handles.end());
        }
        for (const auto& e : file.entities()) {
            if (e.type == hdxf::EntityType::Hatch || !sources.count(e.props.handle)) continue;
            hdxf::Entity copy = e;
            copy.raw = hdxf::RawRecord{};
            copy.props.reactors.clear();
            dwg.add_entity(copy);
        }
        for (const hdxf::Entity* h : typed) {
            hdxf::Entity copy = *h;
            copy.raw = hdxf::RawRecord{};
            copy.props.handle.clear();
            copy.props.reactors.clear();
            dwg.add_entity(copy);
        }
        std::vector<uint8_t> out = save(dwg, format, "BL15-1's HATCHes anew " + name_of(format));
        if (format == DxfFormat::Binary) {
            hdxf::Drawing back = load(out, "binary");
            int n = 0;
            for (const auto& e : back.entities()) n += e.type == hdxf::EntityType::Hatch;
            hdxf_test::assert_eq(n, 5, "binary: five HATCHes");
            continue;
        }
        std::vector<Rec> recs = records(out);
        auto hatches = all_of(recs, "HATCH", "ENTITIES");
        hdxf_test::assert_eq(static_cast<int>(hatches.size()), 5, "HATCHes anew");
        for (size_t i = 0; i < hatches.size(); ++i) {
            assert_same_groups(from_entity(*original_hatches[i]), from_entity(*hatches[i]),
                               "HATCH " + original_hatches[i]->value(5) + " written anew");
        }
    }
}

// The same for the HATCHes of the other R14 files of the local set — the template family's (a program
// that loads AutoCAD's R14 template and saves; not facit): solid and pattern fills, polyline and edge loops
TEST(hatch_r14_written_anew_as_the_template_family) {
    const char* files[] = {"local-r13r14/BenF18_ms2_r14_template1997.dxf",
                           "local-r13r14/bne_JetRanger_Altimeter_r14_template1997.dxf",
                           "local-r13r14/FYP_10Marla_010_r14_template1997.dxf",
                           "local-r13r14/Zurn_59251_r14_acad2000family.dxf",
                           "local-r13r14/Test3_Flash_Tank_I_r14.dxf"};
    int compared = 0;
    for (const char* name : files) {
        std::filesystem::path path = external(name);
        if (!std::filesystem::exists(path)) continue;
        std::string what = name;
        hdxf::Drawing file = load_file(path);
        std::vector<uint8_t> original_bytes;
        {
            std::ifstream in(path, std::ios::binary);
            original_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::vector<Rec> original = records(original_bytes);
        auto original_hatches = all_of(original, "HATCH", "ENTITIES");
        std::vector<const hdxf::Entity*> typed;
        for (const auto& e : file.entities()) if (e.type == hdxf::EntityType::Hatch) typed.push_back(&e);
        hdxf_test::assert_eq(static_cast<int>(typed.size()), static_cast<int>(original_hatches.size()), what);
        if (typed.empty()) continue;
        hdxf::Drawing dwg(DxfVersion::R14);
        // The boundary objects come along with their handles: a hatch of a drawing made here keeps only
        // the boundary objects the drawing has (B97)
        std::set<std::string> sources;
        for (const hdxf::Entity* h : typed) {
            for (const auto& path : h->as_hatch().paths) sources.insert(path.source_handles.begin(), path.source_handles.end());
        }
        for (const auto& e : file.entities()) {
            if (e.type == hdxf::EntityType::Hatch || !sources.count(e.props.handle)) continue;
            hdxf::Entity copy = e;
            copy.raw = hdxf::RawRecord{};
            copy.props.reactors.clear();
            dwg.add_entity(copy);
        }
        for (const hdxf::Entity* h : typed) {
            hdxf::Entity copy = *h;
            copy.raw = hdxf::RawRecord{};
            copy.props.handle.clear();
            copy.props.reactors.clear();
            dwg.add_entity(copy);
        }
        std::vector<Rec> recs = records(save(dwg, DxfFormat::Ascii, what));
        auto hatches = all_of(recs, "HATCH", "ENTITIES");
        hdxf_test::assert_eq(static_cast<int>(hatches.size()), static_cast<int>(original_hatches.size()), what);
        for (size_t i = 0; i < hatches.size(); ++i) {
            // (the head: a hatch of a program's file may have an xdata or linetype of its own; compared from AcDbHatch)
            std::vector<Group> expected = after_marker(*original_hatches[i], "AcDbHatch");
            std::vector<Group> actual = after_marker(*hatches[i], "AcDbHatch");
            // (a program's numbers have up to 19 digits, which a double does not hold: 1e-15 of the value)
            assert_same_groups(expected, actual, what + " HATCH " + original_hatches[i]->value(5), 1e-15);
            ++compared;
        }
    }
    if (compared == 0) hdxf_test::skip("local test files missing");
}

// What R14's HATCH has no group for is a version error naming the first version that has it, before
// anything is written; the drawing is as it was and saves when the property is taken off
TEST(hatch_r14_newer_properties_are_version_errors) {
    // A gradient fill (2004)
    {
        hdxf::Drawing dwg(DxfVersion::R14);
        hdxf::HatchGradient g;
        g.colors = {{0.0, hdxf::Color::from_aci(1)}, {1.0, hdxf::Color::from_aci(5)}};
        dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}}, g);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "gradient saved");
        hdxf_test::assert_eq(dwg.last_error(), "A gradient fill requires DXF 2004 or later");
        hdxf_test::assert_true(dwg.entities()[0].props.handle.empty(), "handles taken back");
        dwg.entities()[0].as_hatch().gradient.reset();
        hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    }
    // A true color (2004)
    {
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}});
        dwg.entities()[0].props.color = hdxf::Color::from_rgb(10, 20, 30);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "true color saved");
        hdxf_test::assert_eq(dwg.last_error(), "True color requires DXF 2004 or later");
    }
    // A spline edge's fit points (2010)
    {
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({});
        hdxf::HatchBoundaryPath edges;
        edges.flags = 1;
        hdxf::HatchEdge curve;
        curve.type = hdxf::HatchEdgeType::Spline;
        curve.knots = {0, 0, 0, 0, 1, 1, 1, 1};
        curve.control_points = {{0, 0}, {1, 2}, {2, 2}, {3, 0}};
        curve.fit_points = {{0, 0}, {3, 0}};
        edges.edges = {curve};
        dwg.entities()[0].as_hatch().paths = {edges};
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "fit points saved");
        hdxf_test::assert_eq(dwg.last_error(), "A hatch spline edge's fit points requires DXF 2010 or later");
        dwg.entities()[0].as_hatch().paths[0].edges[0].fit_points.clear();
        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        // (a 2010 hatch writes 97 after a spline edge; R14's has no such group: the list ends with 96's points)
        std::vector<Rec> recs = records(out);
        const Rec& r = *all_of(recs, "HATCH", "ENTITIES")[0];
        std::vector<int> codes = codes_of(after_marker(r, "AcDbHatch"));
        hdxf_test::assert_eq(static_cast<int>(std::count(codes.begin(), codes.end(), 97)), 1, "one 97: the loop's source count");
    }
    // A hatch with 2000's lineweight
    // (the entity head's 370 is step 7.11's: "Lineweight requires DXF 2000 or later")
}

// ---------------------------------------------------------------------------
// R13
// ---------------------------------------------------------------------------

// R13 has no HATCH (D8): refused with the entity's name, in model space, paper space and a block,
// as ASCII, binary, memory and file — and nothing of the drawing or the file is changed
TEST(hatch_r13_is_a_version_error) {
    const std::string expected = "HATCH requires DXF R14 or later";
    auto add_hatch_in = [](hdxf::Drawing& dwg, int where) {
        if (where == 0) {
            dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}});
        } else if (where == 1) {
            dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}});
            dwg.entities().back().props.paper_space = 1;
        } else {
            hdxf::Block& b = dwg.add_block("FILL");
            hdxf::Entity h;
            h.type = hdxf::EntityType::Hatch;
            h.data = hdxf::HatchData{};
            b.entities.push_back(h);
        }
    };
    for (int where = 0; where < 3; ++where) {
        for (DxfFormat format : kFormats) {
            std::string what = "R13 hatch (" + std::to_string(where) + ") " + name_of(format);
            hdxf::Drawing dwg(DxfVersion::R13);
            dwg.add_line({0, 0, 0}, {1, 1, 0});
            add_hatch_in(dwg, where);
            std::filesystem::path path = std::filesystem::temp_directory_path() / "hdxf_r13_hatch.dxf";
            std::filesystem::remove(path);
            hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": saved");
            hdxf_test::assert_eq(dwg.last_error(), expected, what);
            hdxf_test::assert_false(dwg.save_as(path, format), what + ": save_as");
            hdxf_test::assert_eq(dwg.last_error(), expected, what);
            hdxf_test::assert_false(std::filesystem::exists(path), what + ": a file was created");
            hdxf_test::assert_true(dwg.entities().at(0).props.handle.empty(), what + ": a handle was kept");
        }
    }
    // The same drawing as R14 (set_version) saves; as R13 again it does not
    hdxf::Drawing dwg(DxfVersion::R14);
    dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}});
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    dwg.set_version(DxfVersion::R13);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "R14 hatch as R13");
    hdxf_test::assert_eq(dwg.last_error(), expected);
    // Without the hatch it is an R13 drawing
    dwg.entities().clear();
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
}

// ---------------------------------------------------------------------------
// CLASS (7.10's register: HATCH `AcDbHatch` "ACAD_SEDONA", 90 0, 280 0, 281 1; no 91)
// ---------------------------------------------------------------------------

TEST(hatch_r14_class) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 class " + name_of(format);
        hdxf::Drawing none(DxfVersion::R14);
        none.add_line({0, 0, 0}, {1, 1, 0});
        std::vector<uint8_t> plain = save(none, DxfFormat::Ascii, what);
        hdxf_test::assert_true(class_named(records(plain), "HATCH") == nullptr, what + ": a class without a hatch");

        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}});
        std::vector<uint8_t> out = save(dwg, format, what);
        if (format == DxfFormat::Binary) {
            hdxf::Drawing back = load(out, what);
            continue;
        }
        std::vector<Rec> recs = records(out);
        const Rec* c = class_named(recs, "HATCH");
        hdxf_test::assert_true(c != nullptr, what + ": CLASS HATCH");
        hdxf_test::assert_eq(c->value(2), "AcDbHatch", what);
        hdxf_test::assert_eq(c->value(3), "\"ACAD_SEDONA\"", what + ": the application, quoted as AutoCAD writes it");
        hdxf_test::assert_eq(c->value(90), "0", what);
        hdxf_test::assert_eq(c->value(280), "0", what);
        hdxf_test::assert_eq(c->value(281), "1", what + ": an entity");
        hdxf_test::assert_false(has_code(*c, 91), what + ": no instance count");
        // Once: two hatches have one class
        dwg.add_hatch({{0, 0}, {1, 0}, {1, 1}});
        std::vector<Rec> two = records(save(dwg, DxfFormat::Ascii, what));
        int n = 0;
        for (const auto& r : two) n += r.type() == "CLASS" && r.value(1) == "HATCH";
        hdxf_test::assert_eq(n, 1, what + ": one class record");
    }
}

// ===========================================================================
// IMAGE, R14
// ===========================================================================

namespace {

// A drawing with one image of "C:\bilder\karta.png" (200 x 100 pixels, 40 units wide)
hdxf::Drawing one_image() {
    hdxf::Drawing dwg(DxfVersion::R14);
    dwg.add_image("C:\\bilder\\karta.png", {20, 0, 0}, {200, 100}, 40.0);
    return dwg;
}

} // namespace

// The IMAGE record: the groups of the R14 reference in its order, with no proxy graphics; its
// objects: the IMAGEDEF with the reactors of AutoCAD's R14 files, the IMAGEDEF_REACTOR, the
// RASTERVARIABLES and the dictionaries, in the places AutoCAD's R14 files have them
TEST(image_r14_groups_and_objects) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 image " + name_of(format);
        hdxf::Drawing dwg = one_image();
        std::string image_handle;
        std::vector<uint8_t> out = save(dwg, format, what);
        image_handle = dwg.entities()[0].props.handle;
        hdxf::Drawing back = load(out, what);
        const auto images = images_of(back);
        hdxf_test::assert_eq(static_cast<int>(images.size()), 1, what);
        const auto& i = images[0]->as_image();
        hdxf_test::assert_eq(i.size.x, 200.0, what);
        hdxf_test::assert_eq(i.size.y, 100.0, what);
        hdxf_test::assert_eq(i.u_vector.x, 0.2, what);
        hdxf_test::assert_eq(i.insertion_point.x, 20.0, what);
        hdxf_test::assert_eq(i.display_flags, 7, what);
        hdxf_test::assert_eq(i.brightness, 50, what);
        hdxf_test::assert_eq(i.contrast, 50, what);
        hdxf_test::assert_eq(i.fade, 0, what);
        hdxf_test::assert_false(i.clip_inside, what);
        const hdxf::Object* def = back.find_object(i.image_def_handle);
        hdxf_test::assert_true(def != nullptr && def->type == hdxf::ObjectType::ImageDef, what + ": IMAGEDEF");
        hdxf_test::assert_eq(def->as_image_def().file_name, "C:\\bilder\\karta.png", what);
        hdxf_test::assert_eq(def->as_image_def().size.x, 200.0, what);
        const hdxf::Object* reactor = back.find_object(i.reactor_handle);
        hdxf_test::assert_true(reactor != nullptr && reactor->type == hdxf::ObjectType::ImageDefReactor, what + ": reactor");
        hdxf_test::assert_eq(reactor->as_image_def_reactor().image_handle, images[0]->props.handle, what);
        hdxf_test::assert_eq(reactor->as_image_def_reactor().class_version, 2, what);
        hdxf_test::assert_eq(count_objects(back, hdxf::ObjectType::RasterVariables), 1, what);
        if (format == DxfFormat::Binary) continue;

        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        const Rec& image = *by_handle(recs, image_handle);
        hdxf_test::assert_eq(image.type(), "IMAGE", what);
        // R14 reference, IMAGE: 90, 10, 11, 12, 13, 340, 70, 280, 281, 282, 283, 360, 71, 91, 14 (a rectangle: two corners)
        std::vector<int> expected = {90, 10, 20, 30, 11, 21, 31, 12, 22, 32, 13, 23, 340, 70, 280, 281, 282, 283,
                                     360, 71, 91, 14, 24, 14, 24};
        std::vector<int> actual = codes_of(after_marker(image, "AcDbRasterImage"));
        hdxf_test::assert_true(actual == expected, what + ": codes " + codes_text(actual));
        for (int code : {92, 160, 310, 290}) hdxf_test::assert_false(has_code(image, code), what + ": group " + std::to_string(code));
        hdxf_test::assert_eq(image.value(90), "0", what + ": class version");
        hdxf_test::assert_eq(image.value(71), "1", what + ": rectangular");
        hdxf_test::assert_eq(image.value(91), "2", what);
        hdxf_test::assert_eq(image.value(280), "0", what + ": clipping off");

        // The objects, as AutoCAD's R14 file with images has them (altera_yeager_io)
        const Rec& def_rec = *by_handle(recs, image.value(340));
        hdxf_test::assert_eq(def_rec.type(), "IMAGEDEF", what);
        std::vector<int> def_codes = codes_of(after_marker(def_rec, "AcDbRasterImageDef"));
        hdxf_test::assert_true(def_codes == std::vector<int>({90, 1, 10, 20, 11, 21, 280, 281}),
                               what + ": IMAGEDEF codes " + codes_text(def_codes));
        hdxf_test::assert_eq(def_rec.value(1), "C:\\bilder\\karta.png", what);
        hdxf_test::assert_eq(def_rec.value(280), "1", what + ": loaded");
        hdxf_test::assert_eq(def_rec.value(281), "0", what);
        // Its reactors: the IMAGEDEF_REACTOR of the image, then ACAD_IMAGE_DICT (both of AutoCAD's R14 files)
        std::vector<std::string> def_reactors = reactors_of(def_rec);
        hdxf_test::assert_eq(static_cast<int>(def_reactors.size()), 2, what);
        hdxf_test::assert_eq(def_reactors[0], image.value(360), what + ": the image's reactor first");
        const Rec& dict = *by_handle(recs, def_reactors[1]);
        hdxf_test::assert_eq(dict.type(), "DICTIONARY", what);
        hdxf_test::assert_true(dictionary_names(dict) == std::vector<std::string>({"KARTA"}),
                               what + ": ACAD_IMAGE_DICT holds the definition under the file's name, upper case");
        hdxf_test::assert_eq(dictionary_entry(dict, "KARTA"), def_rec.value(5), what);
        for (int code : {280, 281}) hdxf_test::assert_false(has_code(dict, code), what + ": dictionary group " + std::to_string(code));
        const Rec& reactor_rec = *by_handle(recs, image.value(360));
        hdxf_test::assert_eq(reactor_rec.type(), "IMAGEDEF_REACTOR", what);
        std::vector<int> reactor_codes = codes_of(after_marker(reactor_rec, "AcDbRasterImageDefReactor"));
        hdxf_test::assert_true(reactor_codes == std::vector<int>({90, 330}), what + ": reactor codes " + codes_text(reactor_codes));
        hdxf_test::assert_eq(reactor_rec.value(90), "2", what + ": R14's class version");
        hdxf_test::assert_eq(values(reactor_rec, 330).back(), image_handle, what + ": the image");
        hdxf_test::assert_true(reactors_of(reactor_rec).empty(), what + ": the reactor has none of its own");

        // The root dictionary lists them after ACAD_GROUP and ACAD_MLINESTYLE, ACAD_IMAGE_DICT first
        const Rec& root = *root_dictionary(recs);
        std::vector<std::string> names = dictionary_names(root);
        hdxf_test::assert_true(names == std::vector<std::string>({"ACAD_GROUP", "ACAD_MLINESTYLE", "ACAD_IMAGE_DICT", "ACAD_IMAGE_VARS"}),
                               what + ": the root dictionary");
        hdxf_test::assert_eq(dictionary_entry(root, "ACAD_IMAGE_DICT"), dict.value(5), what);
        const Rec& vars = *by_handle(recs, dictionary_entry(root, "ACAD_IMAGE_VARS"));
        hdxf_test::assert_eq(vars.type(), "RASTERVARIABLES", what);
        std::vector<int> var_codes = codes_of(after_marker(vars, "AcDbRasterVariables"));
        hdxf_test::assert_true(var_codes == std::vector<int>({90, 70, 71, 72}), what + ": RASTERVARIABLES codes " + codes_text(var_codes));
        hdxf_test::assert_eq(vars.value(70), "1", what + ": frame");
        hdxf_test::assert_eq(vars.value(71), "1", what + ": high quality");
        hdxf_test::assert_eq(vars.value(72), "0", what + ": no units");
        // Its owner: the root dictionary (R14 reference, RASTERVARIABLES), as a reactor
        hdxf_test::assert_true(reactors_of(vars) == std::vector<std::string>({root.value(5)}), what + ": reactor of RASTERVARIABLES");
        hdxf_test::assert_true(reactors_of(dict) == std::vector<std::string>({root.value(5)}), what + ": reactor of ACAD_IMAGE_DICT");
    }
}

// The same objects as AutoCAD's own R14 file with an image definition (altera_yeager_io: its IMAGE was
// erased, the objects stay): the codes of each object and the order of the root dictionary are the same
TEST(image_r14_objects_as_autocad_files) {
    std::filesystem::path path = external("local-r13r14/altera_yeager_io_r14_imagedef.dxf");
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("local test file missing: " + path.string());
        return;
    }
    std::vector<uint8_t> file_bytes;
    {
        std::ifstream in(path, std::ios::binary);
        file_bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<Rec> file = records(file_bytes);
    hdxf::Drawing built = one_image();
    std::vector<Rec> mine = records(save(built, DxfFormat::Ascii, "image"));

    struct Part {
        const char* type;
        const char* subclass;
    };
    for (const Part& part : {Part{"IMAGEDEF", "AcDbRasterImageDef"}, Part{"IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor"},
                             Part{"RASTERVARIABLES", "AcDbRasterVariables"}}) {
        auto theirs = all_of(file, part.type, "OBJECTS");
        auto ours = all_of(mine, part.type, "OBJECTS");
        hdxf_test::assert_false(theirs.empty() || ours.empty(), std::string(part.type));
        std::vector<int> a = codes_of(after_marker(*theirs[0], part.subclass));
        std::vector<int> b = codes_of(after_marker(*ours[0], part.subclass));
        hdxf_test::assert_true(a == b, std::string(part.type) + ": AutoCAD " + codes_text(a) + "/ HDxf " + codes_text(b));
        // The groups before the subclass marker: the reactors, in the order of the file
        hdxf_test::assert_eq(static_cast<int>(reactors_of(*theirs[0]).size()), static_cast<int>(reactors_of(*ours[0]).size()),
                             std::string(part.type) + ": reactors");
    }
    // The numbers: RASTERVARIABLES 90 0, 70 1, 71 1, 72 0; IMAGEDEF 90 0, 280 1, 281 0; the reactor 90 2
    const Rec& vars_theirs = *all_of(file, "RASTERVARIABLES", "OBJECTS")[0];
    const Rec& vars_ours = *all_of(mine, "RASTERVARIABLES", "OBJECTS")[0];
    assert_same_groups(after_marker(vars_theirs, "AcDbRasterVariables"), after_marker(vars_ours, "AcDbRasterVariables"), "RASTERVARIABLES");
    const Rec& def_theirs = *all_of(file, "IMAGEDEF", "OBJECTS")[0];
    const Rec& def_ours = *all_of(mine, "IMAGEDEF", "OBJECTS")[0];
    hdxf_test::assert_eq(def_theirs.value(90), def_ours.value(90), "IMAGEDEF 90");
    hdxf_test::assert_eq(def_theirs.value(280), def_ours.value(280), "IMAGEDEF 280");
    hdxf_test::assert_eq(def_theirs.value(281), def_ours.value(281), "IMAGEDEF 281");
    hdxf_test::assert_eq(all_of(file, "IMAGEDEF_REACTOR", "OBJECTS")[0]->value(90), all_of(mine, "IMAGEDEF_REACTOR", "OBJECTS")[0]->value(90),
                         "IMAGEDEF_REACTOR 90");
    // The root dictionary: ACAD_GROUP, ACAD_MLINESTYLE, ACAD_IMAGE_DICT, ACAD_IMAGE_VARS
    hdxf_test::assert_true(dictionary_names(*root_dictionary(file)) == dictionary_names(*root_dictionary(mine)), "root dictionary");
    // The IMAGEDEF's name in ACAD_IMAGE_DICT: the file's name, upper case, [ ] as _
    const Rec& dict_theirs = *by_handle(file, reactors_of(def_theirs).back());
    hdxf_test::assert_true(dictionary_names(dict_theirs) == std::vector<std::string>({"SC_3_"}), "AutoCAD's name");
    hdxf::Drawing named(DxfVersion::R14);
    named.add_image("C:\\Documents and Settings\\jmkuo\\Temporary Internet Files\\sc[3].png", {0, 0, 0}, {850, 668}, 1.0);
    std::vector<Rec> named_recs = records(save(named, DxfFormat::Ascii, "sc[3]"));
    const Rec& named_def = *all_of(named_recs, "IMAGEDEF", "OBJECTS")[0];
    const Rec& named_dict = *by_handle(named_recs, reactors_of(named_def).back());
    hdxf_test::assert_true(dictionary_names(named_dict) == std::vector<std::string>({"SC_3_"}), "HDxf's name");

    // The file's objects read and written again as they are
    hdxf::Drawing read = load_file(path);
    hdxf_test::assert_eq(count_objects(read, hdxf::ObjectType::ImageDef), 1, "IMAGEDEF read");
    for (const auto& o : read.objects()) {
        if (o.type != hdxf::ObjectType::ImageDef) continue;
        hdxf_test::assert_eq(o.as_image_def().size.x, 850.0, "size");
        hdxf_test::assert_eq(o.as_image_def().size.y, 668.0, "size");
        hdxf_test::assert_true(o.as_image_def().loaded, "loaded");
        hdxf_test::assert_eq(static_cast<int>(o.reactors.size()), 2, "reactors");
    }
    std::vector<Rec> again = records(save(read, DxfFormat::Ascii, "yeager"));
    assert_same_groups(from_entity(*all_of(file, "IMAGEDEF", "OBJECTS")[0]), from_entity(*all_of(again, "IMAGEDEF", "OBJECTS")[0]), "IMAGEDEF as read");
}

// Two instances of one file share the IMAGEDEF: one reactor each, and the definition lists both
TEST(image_r14_two_instances_share_a_definition) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 shared definition " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_image("C:\\bilder\\karta.png", {20, 0, 0}, {200, 100}, 40.0);
        dwg.add_image("C:\\bilder\\karta.png", {20, 30, 0}, {200, 100}, 20.0);
        dwg.add_image("C:\\bilder\\annan.png", {0, 60, 0}, {50, 50}, 10.0);
        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        auto images = images_of(back);
        hdxf_test::assert_eq(static_cast<int>(images.size()), 3, what);
        hdxf_test::assert_eq(images[0]->as_image().image_def_handle, images[1]->as_image().image_def_handle, what + ": shared");
        hdxf_test::assert_true(images[0]->as_image().image_def_handle != images[2]->as_image().image_def_handle, what + ": another file");
        hdxf_test::assert_true(images[0]->as_image().reactor_handle != images[1]->as_image().reactor_handle, what + ": one reactor each");
        hdxf_test::assert_eq(count_objects(back, hdxf::ObjectType::ImageDef), 2, what);
        hdxf_test::assert_eq(count_objects(back, hdxf::ObjectType::ImageDefReactor), 3, what);
        hdxf_test::assert_eq(count_objects(back, hdxf::ObjectType::RasterVariables), 1, what + ": one set of variables");
        const hdxf::Object* shared = back.find_object(images[0]->as_image().image_def_handle);
        hdxf_test::assert_eq(static_cast<int>(shared->reactors.size()), 3, what + ": two reactors and the dictionary");
        hdxf_test::assert_eq(shared->reactors[0], images[0]->as_image().reactor_handle, what);
        hdxf_test::assert_eq(shared->reactors[1], images[1]->as_image().reactor_handle, what);
        hdxf_test::assert_eq(shared->reactors[2], holder_of(back, shared->handle), what + ": ACAD_IMAGE_DICT last");
        if (format == DxfFormat::Binary) continue;
        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        const Rec& dict = *by_handle(recs, holder_of(back, shared->handle));
        hdxf_test::assert_true(dictionary_names(dict) == std::vector<std::string>({"KARTA", "ANNAN"}), what + ": entries in the order they were added");
        hdxf_test::assert_eq(static_cast<int>(all_of(recs, "IMAGEDEF", "OBJECTS").size()), 2, what);
        // 1 IMAGEDEF_REACTOR record of each image, 90 2, 330 its image
        auto reactors = all_of(recs, "IMAGEDEF_REACTOR", "OBJECTS");
        hdxf_test::assert_eq(static_cast<int>(reactors.size()), 3, what);
        for (const Rec* r : reactors) hdxf_test::assert_eq(r->value(90), "2", what);
    }
}

// The clip boundary: a rectangle (71 1, two corners in pixels, default (-0.5, -0.5) to (size - 0.5)) and a
// polygon (71 2, three or more vertices in order), clipping on (280 1) with 70 bit 4 — as the R14 reference says
TEST(image_r14_clipping) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 image clip " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_image("a.png", {0, 0, 0}, {100, 50}, 10.0);
        dwg.add_image("a.png", {20, 0, 0}, {100, 50}, 10.0);
        dwg.add_image("a.png", {40, 0, 0}, {100, 50}, 10.0);
        auto& rect = dwg.entities()[1].as_image();
        rect.clipping = true;
        rect.display_flags = 7;
        rect.clip_boundary = {{10, 5}, {60, 30}};
        auto& poly = dwg.entities()[2].as_image();
        poly.clipping = true;
        poly.clip_boundary_type = 2;
        poly.clip_boundary = {{0, 0}, {50, 0}, {99, 25}, {50, 49}, {0, 40}};
        poly.brightness = 70;
        poly.contrast = 30;
        poly.fade = 15;
        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        auto images = images_of(back);
        hdxf_test::assert_eq(static_cast<int>(images.size()), 3, what);
        const auto& a = images[0]->as_image();
        hdxf_test::assert_false(a.clipping, what);
        hdxf_test::assert_eq(a.clip_boundary_type, 1, what);
        hdxf_test::assert_eq(static_cast<int>(a.clip_boundary.size()), 2, what);
        hdxf_test::assert_eq(a.clip_boundary[0].x, -0.5, what + ": the default boundary");
        hdxf_test::assert_eq(a.clip_boundary[1].x, 99.5, what);
        hdxf_test::assert_eq(a.clip_boundary[1].y, 49.5, what);
        const auto& b = images[1]->as_image();
        hdxf_test::assert_true(b.clipping, what);
        hdxf_test::assert_eq(b.clip_boundary_type, 1, what);
        hdxf_test::assert_eq(static_cast<int>(b.clip_boundary.size()), 2, what);
        hdxf_test::assert_eq(b.clip_boundary[1].x, 60.0, what);
        const auto& c = images[2]->as_image();
        hdxf_test::assert_true(c.clipping, what);
        hdxf_test::assert_eq(c.clip_boundary_type, 2, what);
        hdxf_test::assert_eq(static_cast<int>(c.clip_boundary.size()), 6, what);   // written closed in R13/R14 (7.24)
        hdxf_test::assert_eq(c.clip_boundary[2].x, 99.0, what);
        hdxf_test::assert_eq(c.clip_boundary[4].y, 40.0, what);
        hdxf_test::assert_eq(c.brightness, 70, what);
        hdxf_test::assert_eq(c.contrast, 30, what);
        hdxf_test::assert_eq(c.fade, 15, what);
        hdxf_test::assert_false(c.clip_inside, what);
        if (format == DxfFormat::Binary) continue;
        dxf_structure::check_structure(out, what);
        std::vector<Rec> recs = records(out);
        auto written = all_of(recs, "IMAGE", "ENTITIES");
        hdxf_test::assert_eq(static_cast<int>(written.size()), 3, what);
        hdxf_test::assert_eq(written[2]->value(71), "2", what);
        hdxf_test::assert_eq(written[2]->value(91), "6", what);   // closed in R13/R14 (7.24)
        hdxf_test::assert_eq(static_cast<int>(values(*written[2], 14).size()), 6, what + ": five 14 and the first again");
        hdxf_test::assert_eq(number(values(*written[2], 24)[4]), 40.0, what);
        hdxf_test::assert_eq(values(*written[2], 24).back(), values(*written[2], 24).front(), what + ": closed");
        hdxf_test::assert_eq(written[1]->value(280), "1", what);
    }
}

// Erasing images: the IMAGEDEF_REACTOR goes with its image and the IMAGEDEF no longer lists it; the IMAGEDEF stays
// while an image uses it and after the last one (the image is still attached, as in AutoCAD)
TEST(image_r14_erased_instance) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 erased instance " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_image("C:\\bilder\\karta.png", {20, 0, 0}, {200, 100}, 40.0);
        dwg.add_image("C:\\bilder\\karta.png", {20, 30, 0}, {200, 100}, 20.0);
        hdxf::Drawing back = load(save(dwg, format, what), what);
        auto images = images_of(back);
        std::string first = images[0]->props.handle;
        std::string second = images[1]->props.handle;
        std::string first_reactor = images[0]->as_image().reactor_handle;
        std::string second_reactor = images[1]->as_image().reactor_handle;
        std::string def_handle = images[0]->as_image().image_def_handle;
        std::string dict_handle = holder_of(back, def_handle);
        auto erase = [&](hdxf::Drawing& d, const std::string& handle) {
            auto& ents = d.entities();
            ents.erase(std::remove_if(ents.begin(), ents.end(), [&](const hdxf::Entity& e) { return e.props.handle == handle; }), ents.end());
        };

        erase(back, first);
        std::vector<uint8_t> out = save(back, format, what + " (one)");
        hdxf::Drawing one = load(out, what + " (one)");
        hdxf_test::assert_eq(static_cast<int>(images_of(one).size()), 1, what);
        hdxf_test::assert_true(one.find_object(first_reactor) == nullptr, what + ": the reactor of the erased image is gone");
        hdxf_test::assert_true(one.find_object(second_reactor) != nullptr, what + ": the other's stays");
        const hdxf::Object* def = one.find_object(def_handle);
        hdxf_test::assert_true(def != nullptr, what + ": IMAGEDEF stays");
        hdxf_test::assert_true(def->reactors == std::vector<std::string>({second_reactor, dict_handle}), what + ": reactors of the IMAGEDEF");
        hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(back.warnings()).empty(), what + ": a pointer set to none");
        if (format == DxfFormat::Ascii) dxf_structure::check_structure(out, what + " (one)");

        erase(one, second);
        std::vector<uint8_t> out2 = save(one, format, what + " (none)");
        hdxf::Drawing none = load(out2, what + " (none)");
        hdxf_test::assert_eq(static_cast<int>(images_of(none).size()), 0, what);
        hdxf_test::assert_eq(count_objects(none, hdxf::ObjectType::ImageDefReactor), 0, what);
        hdxf_test::assert_eq(count_objects(none, hdxf::ObjectType::ImageDef), 1, what + ": the IMAGEDEF stays");
        hdxf_test::assert_eq(count_objects(none, hdxf::ObjectType::RasterVariables), 1, what);
        hdxf_test::assert_true(none.find_object(def_handle)->reactors == std::vector<std::string>({dict_handle}), what);
        if (format == DxfFormat::Ascii) dxf_structure::check_structure(out2, what + " (none)");
    }
}

// A definition that is removed: the IMAGE that used it has no definition (340 0) and the dictionary no
// longer lists it; the file keeps its structure
TEST(image_r14_removed_definition) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 removed definition " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_image("C:\\bilder\\karta.png", {20, 0, 0}, {200, 100}, 40.0);
        dwg.add_image("C:\\bilder\\annan.png", {0, 60, 0}, {50, 50}, 10.0);
        hdxf::Drawing back = load(save(dwg, format, what), what);
        auto images = images_of(back);
        std::string def_handle = images[0]->as_image().image_def_handle;
        std::string other_def = images[1]->as_image().image_def_handle;
        auto& objects = back.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const hdxf::Object& o) { return o.handle == def_handle; }),
                      objects.end());
        std::vector<uint8_t> out = save(back, format, what + " (saved)");
        hdxf::Drawing again = load(out, what + " (again)");
        hdxf_test::assert_true(again.find_object(def_handle) == nullptr, what + ": gone");
        auto now = images_of(again);
        hdxf_test::assert_eq(static_cast<int>(now.size()), 2, what);
        hdxf_test::assert_eq(now[0]->as_image().image_def_handle, "0", what + ": 340 0");
        hdxf_test::assert_eq(now[1]->as_image().image_def_handle, other_def, what + ": the other's definition stays");
        hdxf_test::assert_eq(count_objects(again, hdxf::ObjectType::ImageDef), 1, what);
        bool named = false;
        for (const auto& o : again.objects()) {
            if (o.type != hdxf::ObjectType::Dictionary) continue;
            for (const auto& e : o.as_dictionary().entries) named = named || e.handle == def_handle;
        }
        hdxf_test::assert_false(named, what + ": the dictionary still lists it");
        bool warned = false;
        for (const auto& w : back.warnings()) warned = warned || w.find("set to none") != std::string::npos;
        hdxf_test::assert_true(warned, what + ": a warning that a pointer was set to none");
        if (format == DxfFormat::Ascii) dxf_structure::check_structure(out, what);
    }
}

// Paths: relative, with spaces, UTF-8 outside the code page (written as \U+XXXX), within it (one byte); the
// IMAGEDEF's name is made from the file's name as R14 spells a name
TEST(image_r14_paths_and_names) {
    const std::string utf8_ae = "Bilder\\r\xC3\xA4v \xC3\xB6.png";                 // "räv ö" (Latin-1)
    const std::string cjk = "C:\\\xE7\x94\xBB\xE5\x83\x8F\\\xE5\x9C\xB0\xE5\x9B\xB3.png";    // 画像\地図
    const std::vector<std::string> paths = {".\\bilder\\karta.png", "..\\up\\two dots.tmp.jpg", "karta", utf8_ae, cjk,
                                            "//server/share/a.png", "sc[3].png"};
    for (DxfFormat format : kFormats) {
        std::string what = "R14 image paths " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        double y = 0;
        for (const auto& p : paths) dwg.add_image(p, {0, y += 10, 0}, {10, 10}, 5.0);
        std::vector<uint8_t> out = save(dwg, format, what);
        hdxf::Drawing back = load(out, what);
        auto images = images_of(back);
        hdxf_test::assert_eq(static_cast<int>(images.size()), static_cast<int>(paths.size()), what);
        for (size_t i = 0; i < paths.size(); ++i) {
            const hdxf::Object* def = back.find_object(images[i]->as_image().image_def_handle);
            hdxf_test::assert_true(def != nullptr, what + ": IMAGEDEF of " + paths[i]);
            hdxf_test::assert_eq(def->as_image_def().file_name, paths[i], what + ": file name");
        }
        // The names (in the order of the root dictionary's ACAD_IMAGE_DICT)
        std::vector<std::string> names;
        for (const auto& o : back.objects()) {
            if (o.type != hdxf::ObjectType::Dictionary || o.owner == "0") continue;
            for (const auto& e : o.as_dictionary().entries) {
                const hdxf::Object* target = back.find_object(e.handle);
                if (target && target->type == hdxf::ObjectType::ImageDef) names.push_back(e.name);
            }
        }
        // (a UTF-8 character is one underscore: "räv ö" is R, _, V, _, _ and the two characters of the CJK name two)
        std::vector<std::string> expected = {"KARTA", "TWO_DOTS_TMP", "KARTA_1", "R_V__", "__", "A", "SC_3_"};
        hdxf_test::assert_true(names == expected, what + ": names");
        if (format == DxfFormat::Binary) continue;
        std::string text(out.begin(), out.end());
        // Within the code page: the one byte, not UTF-8; outside it: \U+XXXX (the only form AutoCAD reads)
        // (which of the two forms a character of the code page has is step 7.21's: the common writer's \U+00E4
        // as for 2000 and 2004, or the byte of ANSI_1252; never UTF-8 before 2007)
        hdxf_test::assert_true(text.find("Bilder\\r\xE4v \xF6.png") != std::string::npos ||
                                   text.find("Bilder\\r\\U+00E4v \\U+00F6.png") != std::string::npos,
                               what + ": the characters of the code page as a byte or as \\U+");
        hdxf_test::assert_true(text.find("r\xC3\xA4v") == std::string::npos, what + ": no UTF-8 in an R14 file");
        hdxf_test::assert_true(text.find("C:\\\\U+753B\\U+50CF\\\\U+5730\\U+56F3.png") != std::string::npos, what + ": \\U+");
    }
}

// What R14's IMAGE has no group for: the clip mode inside is 2010's (290)
TEST(image_r14_newer_properties_are_version_errors) {
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14}) {
        hdxf::Drawing dwg(version);
        dwg.add_image("a.png", {0, 0, 0}, {10, 10}, 5.0);
        dwg.entities()[0].as_image().clip_inside = true;
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "clip inside saved");
        std::string error = dwg.last_error();
        if (version == DxfVersion::R14) {
            hdxf_test::assert_eq(error, "An image's clip mode inside requires DXF 2010 or later");
            dwg.entities()[0].as_image().clip_inside = false;
            hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
        } else {
            hdxf_test::assert_eq(error, "IMAGE requires DXF R14 or later");
        }
    }
    // A WIPEOUT is 2000's
    hdxf::Drawing wipeout(DxfVersion::R14);
    wipeout.add_wipeout({{0, 0, 0}, {1, 0, 0}, {1, 1, 0}});
    hdxf_test::assert_true(wipeout.save_to_memory().empty(), "WIPEOUT as R14");
    hdxf_test::assert_eq(wipeout.last_error(), "WIPEOUT requires DXF 2000 or later");
}

// R13 has no IMAGE (D9) and none of its objects: the entity first, then the objects of a drawing built here
TEST(image_r13_is_a_version_error) {
    for (DxfFormat format : kFormats) {
        std::string what = "R13 image " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R13);
        dwg.add_line({0, 0, 0}, {1, 1, 0});
        dwg.add_image("picture.png", {0, 0, 0}, {10, 10}, 5.0);
        std::filesystem::path path = std::filesystem::temp_directory_path() / "hdxf_r13_image.dxf";
        std::filesystem::remove(path);
        hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": saved");
        hdxf_test::assert_eq(dwg.last_error(), "IMAGE requires DXF R14 or later", what);
        hdxf_test::assert_false(dwg.save_as(path, format), what + ": save_as");
        hdxf_test::assert_false(std::filesystem::exists(path), what + ": a file was created");
        hdxf_test::assert_true(dwg.entities().at(0).props.handle.empty(), what + ": a handle was kept");

        // The IMAGE erased, its objects stay in the drawing: the first of them is named
        auto& ents = dwg.entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) { return e.type == hdxf::EntityType::Image; }), ents.end());
        hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": objects saved");
        hdxf_test::assert_eq(dwg.last_error(), "RASTERVARIABLES requires DXF R14 or later", what + ": the objects");
        auto& objects = dwg.objects();
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                                     [](const hdxf::Object& o) { return o.type == hdxf::ObjectType::RasterVariables; }), objects.end());
        hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": IMAGEDEF saved");
        hdxf_test::assert_eq(dwg.last_error(), "IMAGEDEF requires DXF R14 or later", what);
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                                     [](const hdxf::Object& o) { return o.type == hdxf::ObjectType::ImageDef; }), objects.end());
        hdxf_test::assert_true(dwg.save_to_memory(format).empty(), what + ": IMAGEDEF_REACTOR saved");
        hdxf_test::assert_eq(dwg.last_error(), "IMAGEDEF_REACTOR requires DXF R14 or later", what);
        objects.erase(std::remove_if(objects.begin(), objects.end(),
                                     [](const hdxf::Object& o) { return o.type == hdxf::ObjectType::ImageDefReactor; }), objects.end());
        hdxf_test::assert_false(dwg.save_to_memory(format).empty(), what + ": " + dwg.last_error());
    }
    // The same drawing as R14 saves
    hdxf::Drawing r14(DxfVersion::R14);
    r14.add_image("picture.png", {0, 0, 0}, {10, 10}, 5.0);
    hdxf_test::assert_false(r14.save_to_memory().empty(), r14.last_error());
    r14.set_version(DxfVersion::R13);
    hdxf_test::assert_true(r14.save_to_memory().empty(), "R14 image as R13");
    hdxf_test::assert_eq(r14.last_error(), "IMAGE requires DXF R14 or later");
}

// AutoCAD's R13 file with images (altera_piranha_io: its export keeps the image objects and loses the
// IMAGE) is written back as R13 with its objects (B12): a read file keeps what it has
TEST(image_r13_read_file_keeps_its_image_objects) {
    std::filesystem::path path = external("local-r13r14/altera_piranha_io_group_r13_imagedef.dxf");
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("local test file missing: " + path.string());
        return;
    }
    hdxf::Drawing dwg = load_file(path);
    hdxf_test::assert_true(dwg.version() == DxfVersion::R13, "R13");
    int defs = count_objects(dwg, hdxf::ObjectType::ImageDef);
    int reactors = count_objects(dwg, hdxf::ObjectType::ImageDefReactor);
    hdxf_test::assert_true(defs >= 1 && reactors >= 1, "the objects are read");
    for (DxfFormat format : kFormats) {
        std::vector<uint8_t> out = save(dwg, format, "piranha " + name_of(format));
        hdxf::Drawing back = load(out, "piranha again");
        hdxf_test::assert_true(back.version() == DxfVersion::R13, "R13 again");
        hdxf_test::assert_eq(count_objects(back, hdxf::ObjectType::ImageDef), defs, "IMAGEDEF");
        hdxf_test::assert_eq(count_objects(back, hdxf::ObjectType::ImageDefReactor), reactors, "IMAGEDEF_REACTOR");
        hdxf_test::assert_eq(count_objects(back, hdxf::ObjectType::RasterVariables), 1, "RASTERVARIABLES");
    }
}

// CLASS (7.10's register): IMAGE `AcDbRasterImage` ISM 127 0 1, RASTERVARIABLES, IMAGEDEF and IMAGEDEF_REACTOR (ISM;
// 90 0, 0, 1); each once; none without the objects
TEST(image_r14_classes) {
    hdxf::Drawing none(DxfVersion::R14);
    none.add_hatch({{0, 0}, {1, 0}, {1, 1}});
    std::vector<Rec> plain = records(save(none, DxfFormat::Ascii, "no image"));
    for (const char* name : {"IMAGE", "IMAGEDEF", "IMAGEDEF_REACTOR", "RASTERVARIABLES"}) {
        hdxf_test::assert_true(class_named(plain, name) == nullptr, std::string("a class ") + name + " without an image");
    }

    hdxf::Drawing dwg(DxfVersion::R14);
    dwg.add_image("a.png", {0, 0, 0}, {10, 10}, 5.0);
    dwg.add_image("a.png", {5, 0, 0}, {10, 10}, 5.0);
    dwg.add_image("b.png", {9, 0, 0}, {10, 10}, 5.0);
    std::vector<Rec> recs = records(save(dwg, DxfFormat::Ascii, "images"));
    struct Expected {
        const char* name;
        const char* cpp;
        int flags;
        int entity;         // 281
    };
    for (const Expected& e : {Expected{"IMAGE", "AcDbRasterImage", 127, 1}, Expected{"RASTERVARIABLES", "AcDbRasterVariables", 0, 0},
                              Expected{"IMAGEDEF", "AcDbRasterImageDef", 0, 0}, Expected{"IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor", 1, 0}}) {
        const Rec* c = class_named(recs, e.name);
        hdxf_test::assert_true(c != nullptr, std::string("CLASS ") + e.name);
        hdxf_test::assert_eq(c->value(2), e.cpp, e.name);
        hdxf_test::assert_eq(c->value(3), "ISM", std::string(e.name) + ": the application");
        hdxf_test::assert_eq(c->value(90), std::to_string(e.flags), e.name);
        hdxf_test::assert_eq(c->value(280), "0", e.name);
        hdxf_test::assert_eq(c->value(281), std::to_string(e.entity), e.name);
        hdxf_test::assert_false(has_code(*c, 91), std::string(e.name) + ": no instance count");
        int n = 0;
        for (const auto& r : recs) n += r.type() == "CLASS" && r.value(1) == e.name;
        hdxf_test::assert_eq(n, 1, std::string(e.name) + ": once");
    }
}

// Both together, in every I/O call and both formats: a drawing with a hatch and an image over it is one file
TEST(hatch_image_r14_in_one_drawing) {
    for (DxfFormat format : kFormats) {
        std::string what = "R14 hatch and image " + name_of(format);
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_circle({5, 5, 0}, 5);
        dwg.add_hatch({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        dwg.add_image("a.png", {0, 0, 0}, {10, 10}, 10.0);
        dwg.add_lwpolyline({{0, 0}, {10, 0}, {10, 10}}, true);
        std::filesystem::path path = std::filesystem::temp_directory_path() / "hdxf_r14_hatch_image.dxf";
        std::filesystem::remove(path);
        hdxf_test::assert_true(dwg.save_as(path, format), what + ": " + dwg.last_error());
        hdxf::Drawing back;
        hdxf_test::assert_true(back.load(path), what + ": " + back.last_error());
        hdxf_test::assert_true(back.version() == DxfVersion::R14, what);
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 4, what);
        // Written back as it was read (B12): the same entities
        std::vector<uint8_t> again = save(back, format, what + " again");
        hdxf::Drawing third = load(again, what + " third");
        hdxf_test::assert_eq(static_cast<int>(third.entities().size()), 4, what);
        hdxf_test::assert_eq(count_objects(third, hdxf::ObjectType::ImageDef), 1, what);
        std::filesystem::remove(path);
    }
}
