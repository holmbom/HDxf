#include "SectionTypes.hpp"
#include "proxy_graphics.hpp"
#include <cctype>
#include <cstring>

// The indicator of a section plane (phase 9, step 9.12; plan 2026-10-27-fas-9-typer.md, "SECTIONOBJECT ..."): the proxy
// graphics AutoCAD 2025 writes into a SECTIONOBJECT, and so what the section adds to the drawing's extents.
//
// What AutoCAD's own sections show (doc/facit/acad_<ver>_dxf_objects.dxf, the sections of doc/facit/fas9_sections.scr; the
// commands are AcGi's, numbered as in ODA's specification and proxy_graphics.hpp):
//   19 marker 1, 22 true color (the indicator's color 62 as ACI: 0xC3000000 | 62; 9 by default), 20 fill 1,
//   16 layer (the entity's, by its place in the layer table), 22 true color (the entity's own: ByLayer 0xC0000000, ACI 1
//   0xC3000001 after a CHPROP), 18 linetype (the entity's: 0x7FFF ByLayer),
//   then the lines, each 19 marker n (from 2) and 6 polyline of its two points,
//   16 layer (Defpoints), 22 true color and 18 linetype as before — after the first lines of a slice, a boundary or a volume
//   (the outline is on Defpoints), at the end for a plane — then the rest of the lines, and 20 fill 1.
//   - a plane: one line per segment of the vertices
//   - a slice and a boundary (state 2): and the rest of the outline from the last vertex through the back vertices to the first
//   - a volume (state 4): the same at the top and bottom of the section (the vertical direction times the heights), and a
//     vertical edge at each back vertex; the lines of the vertex segments at the top and the bottom come first, then the
//     outline (middle, top, bottom), then the edges (observed with two vertices and two back vertices: the order for more
//     vertices is the same grouping, ej testat)
// The section's extents are the points of its lines (a plane: its vertices; a volume: also the top and the bottom).

namespace hdxf {
namespace internal {

namespace {

constexpr int kLinetypeByLayer = 0x7FFF;
constexpr int kLinetypeByBlock = 0x7FFE;

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

Point3 plus(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 scaled(const Point3& a, double f) { return {a.x * f, a.y * f, a.z * f}; }

using Line = std::vector<Point3>;

void add_segments(std::vector<Line>& out, const std::vector<Point3>& points, bool closed) {
    if (points.size() < 2) return;
    for (size_t i = 0; i + 1 < points.size(); ++i) out.push_back({points[i], points[i + 1]});
    if (closed) out.push_back({points.back(), points.front()});
}

int layer_index(const ProxyTables& tables, const std::string& name, bool* found) {
    for (size_t i = 0; i < tables.layers.size(); ++i) {
        if (same_name(tables.layers[i], name)) {
            if (found) *found = true;
            return static_cast<int>(i);
        }
    }
    if (found) *found = false;
    return 0;
}

// A linetype among the ones other than ByBlock and ByLayer, as the proxy graphics number them
int linetype_index(const ProxyTables& tables, const std::string& name) {
    if (name.empty() || same_name(name, "ByLayer")) return kLinetypeByLayer;
    if (same_name(name, "ByBlock")) return kLinetypeByBlock;
    int i = 0;
    for (const auto& lt : tables.linetypes) {
        if (same_name(lt, "ByLayer") || same_name(lt, "ByBlock")) continue;
        if (same_name(lt, name)) return i;
        ++i;
    }
    return kLinetypeByLayer;
}

} // namespace

SectionLines section_lines(const SectionObjectData& d) {
    SectionLines out;
    const std::vector<Point3>& v = d.vertices;
    if (v.size() < 2) return out;
    add_segments(out.primary, v, false);
    if (d.state != 2 && d.state != 4) return out;
    if (d.back_vertices.empty()) return out;

    // The outline: from the last vertex through the back vertices to the first
    std::vector<Point3> ring;
    ring.push_back(v.back());
    for (const Point3& b : d.back_vertices) ring.push_back(b);
    ring.push_back(v.front());
    std::vector<Line> outline;
    add_segments(outline, ring, false);
    if (d.state == 2) {
        out.secondary = outline;
        return out;
    }

    const Point3 up = scaled(d.vertical_direction, d.top_height);
    const Point3 down = scaled(d.vertical_direction, -d.bottom_height);
    auto moved = [](const std::vector<Line>& lines, const Point3& by) {
        std::vector<Line> r;
        for (const Line& l : lines) r.push_back({plus(l[0], by), plus(l[1], by)});
        return r;
    };
    auto append = [&](const std::vector<Line>& lines) { out.secondary.insert(out.secondary.end(), lines.begin(), lines.end()); };
    append(moved(out.primary, up));
    append(moved(out.primary, down));
    append(outline);
    append(moved(outline, up));
    append(moved(outline, down));
    for (const Point3& b : d.back_vertices) out.secondary.push_back({plus(b, up), plus(b, down)});
    return out;
}

std::vector<uint8_t> section_indicator_graphics(const SectionObjectData& d, const EntityProperties& props,
                                                const ProxyTables& tables, bool* defpoints_found) {
    if (defpoints_found) *defpoints_found = true;
    const SectionLines lines = section_lines(d);
    if (lines.primary.empty()) return {};
    ProxyGraphics g;
    const int32_t indicator = static_cast<int32_t>(0xC3000000u | static_cast<uint32_t>(d.indicator_color.value_or(9) & 0xFF));
    const int32_t color = CmColor::from_color(props.color).raw;
    const int linetype = linetype_index(tables, props.linetype);
    int marker = 1;
    g.marker(marker++);
    g.true_color(indicator);
    g.fill(true);
    g.layer(layer_index(tables, props.layer, nullptr));
    g.true_color(color);
    g.linetype(linetype);
    for (const Line& l : lines.primary) {
        g.marker(marker++);
        g.polyline(l);
    }
    g.layer(layer_index(tables, "Defpoints", defpoints_found));
    g.true_color(color);
    g.linetype(linetype);
    for (const Line& l : lines.secondary) {
        g.marker(marker++);
        g.polyline(l);
    }
    g.fill(true);
    return g.bytes();
}

bool section_graphics_standard(const std::vector<uint8_t>& stream) {
    auto i32 = [&](size_t at) {
        int32_t v = 0;
        std::memcpy(&v, &stream[at], 4);
        return v;
    };
    if (stream.size() < 8 || i32(0) != static_cast<int32_t>(stream.size())) return false;
    std::vector<int> types;
    std::vector<int32_t> points;
    size_t at = 8;
    while (at < stream.size()) {
        if (at + 8 > stream.size()) return false;
        int32_t size = i32(at);
        if (size < 8 || at + static_cast<size_t>(size) > stream.size()) return false;
        int type = i32(at + 4);
        types.push_back(type);
        if (type == 6) {
            if (size < 12) return false;
            points.push_back(i32(at + 8));
        }
        at += static_cast<size_t>(size);
    }
    if (static_cast<int32_t>(types.size()) != i32(4)) return false;
    // 19 22 20 16 22 18, (19 6)*, 16 22 18, (19 6)*, 20
    const int head[] = {19, 22, 20, 16, 22, 18};
    size_t k = 0;
    for (int t : head) {
        if (k >= types.size() || types[k++] != t) return false;
    }
    auto lines = [&]() {
        while (k + 1 < types.size() && types[k] == 19 && types[k + 1] == 6) k += 2;
    };
    lines();
    const int middle[] = {16, 22, 18};
    for (int t : middle) {
        if (k >= types.size() || types[k++] != t) return false;
    }
    lines();
    if (k + 1 != types.size() || types[k] != 20) return false;
    for (int32_t n : points) {
        if (n != 2) return false;
    }
    return true;
}

} // namespace internal
} // namespace hdxf
