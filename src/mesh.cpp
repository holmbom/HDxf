#include "mesh.hpp"
#include "proxy_graphics.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>

// MESH as AutoCAD draws and measures it (plan phase 4, B48/B49): its edges — at level 0 the
// cage's, the vertices themselves (ACadSharp's MESH 343 gives AutoCAD's $EXTMIN z exactly);
// above level 0 the cage subdivided as AutoCAD smooths it, the rule found with AutoCAD 2027's
// own engine (DWG TrueView's accoreconsole: the extents of 120 meshes, B52):
//
// Catmull-Clark, level by level. A face's point is its centroid. An edge's sharpness s is 1 for
// a boundary edge and for a crease "always" (-1), else min(crease, 1); its point is
// (a + b + w (f1 + f2)) / (2 + 2w), w = 1 - s (the midpoint on the boundary). A vertex with fewer
// than two sharp edges (s > 0, the boundary's counted) moves by Catmull-Clark's rule
// (Q + 2R + (n - 3) v) / n; with two, the sharp rule is (6v + a + b) / 8, with more — or in one
// face only — the vertex stays; the vertex's sharpness is the mean of its sharp edges' s (1 on
// the boundary) and its point that much of the sharp rule and the rest of the smooth one. The
// children of an edge have its crease less one (none at 0), "always" stays. The extents and the
// wire are the edges that come from the cage's edges.

namespace hdxf {
namespace internal {

namespace {

Point3 operator+(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 operator*(const Point3& a, double k) { return {a.x * k, a.y * k, a.z * k}; }

uint64_t edge_key(int a, int b) {
    uint32_t lo = static_cast<uint32_t>(std::min(a, b)), hi = static_cast<uint32_t>(std::max(a, b));
    return (static_cast<uint64_t>(lo) << 32) | hi;
}

// One level of the mesh: its points, faces and the creases of its edges (by key)
struct MeshLevel {
    std::vector<Point3> points;
    std::vector<std::vector<int>> faces;
    std::unordered_map<uint64_t, double> creases;
};

// An edge of a level: its ends and the faces it borders (more than two: as the boundary)
struct LevelEdge {
    int a = 0;
    int b = 0;
    int faces = 0;
    int face[2] = {-1, -1};
};

// AutoCAD's largest number of faces it smooths a mesh to (SMOOTHMESHMAXFACE at most 16 000 000)
const size_t kMaxFaces = 16000000;

// The next level: the new points (the old vertices' points first, by the same index, then the
// edges' points in `edges`' order, then the faces'); `edge_point` gives each old edge's point
MeshLevel subdivide(const MeshLevel& m, std::vector<LevelEdge>& edges, std::unordered_map<uint64_t, int>& index) {
    const size_t nv = m.points.size();
    edges.clear();
    index.clear();
    std::vector<std::vector<int>> vertex_edges(nv), vertex_faces(nv);
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        const auto& f = m.faces[fi];
        for (size_t k = 0; k < f.size(); ++k) {
            int a = f[k], b = f[(k + 1) % f.size()];
            uint64_t key = edge_key(a, b);
            auto it = index.find(key);
            int e;
            if (it == index.end()) {
                e = static_cast<int>(edges.size());
                index.emplace(key, e);
                edges.push_back({a, b, 0, {-1, -1}});
                vertex_edges[static_cast<size_t>(a)].push_back(e);
                vertex_edges[static_cast<size_t>(b)].push_back(e);
            } else {
                e = it->second;
            }
            LevelEdge& le = edges[static_cast<size_t>(e)];
            if (le.faces < 2) le.face[le.faces] = static_cast<int>(fi);
            ++le.faces;
            vertex_faces[static_cast<size_t>(f[k])].push_back(static_cast<int>(fi));
        }
    }
    std::vector<Point3> face_point(m.faces.size());
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        Point3 sum;
        for (int v : m.faces[fi]) sum = sum + m.points[static_cast<size_t>(v)];
        face_point[fi] = sum * (1.0 / static_cast<double>(m.faces[fi].size()));
    }
    auto crease = [&](const LevelEdge& e) {
        auto it = m.creases.find(edge_key(e.a, e.b));
        return it == m.creases.end() ? 0.0 : it->second;
    };
    auto sharpness = [&](const LevelEdge& e) {
        if (e.faces != 2) return 1.0;
        double s = crease(e);
        return s < 0.0 ? 1.0 : std::min(s, 1.0);
    };

    MeshLevel out;
    out.points.resize(nv + edges.size() + m.faces.size());
    for (size_t i = 0; i < edges.size(); ++i) {
        const LevelEdge& e = edges[i];
        const Point3& a = m.points[static_cast<size_t>(e.a)];
        const Point3& b = m.points[static_cast<size_t>(e.b)];
        Point3 p;
        if (e.faces != 2) {
            p = (a + b) * 0.5;
        } else {
            double w = 1.0 - sharpness(e);
            Point3 f = face_point[static_cast<size_t>(e.face[0])] + face_point[static_cast<size_t>(e.face[1])];
            p = (a + b + f * w) * (1.0 / (2.0 + 2.0 * w));
        }
        out.points[nv + i] = p;
    }
    for (size_t v = 0; v < nv; ++v) {
        const Point3& p = m.points[v];
        const auto& ve = vertex_edges[v];
        if (ve.empty()) {
            out.points[v] = p;
            continue;
        }
        double n = static_cast<double>(ve.size());
        Point3 q, r;
        for (int f : vertex_faces[v]) q = q + face_point[static_cast<size_t>(f)];
        q = q * (1.0 / static_cast<double>(vertex_faces[v].size()));
        for (int e : ve) {
            const LevelEdge& le = edges[static_cast<size_t>(e)];
            r = r + (m.points[static_cast<size_t>(le.a)] + m.points[static_cast<size_t>(le.b)]) * 0.5;
        }
        r = r * (1.0 / n);
        Point3 smooth = (q + r * 2.0 + p * (n - 3.0)) * (1.0 / n);
        std::vector<int> sharp;
        bool boundary = false;
        double sum = 0.0;
        for (int e : ve) {
            const LevelEdge& le = edges[static_cast<size_t>(e)];
            boundary = boundary || le.faces != 2;
            double s = sharpness(le);
            if (s > 0.0) {
                sharp.push_back(e);
                sum += s;
            }
        }
        if (sharp.size() < 2) {
            out.points[v] = smooth;
            continue;
        }
        double vs = boundary ? 1.0 : sum / static_cast<double>(sharp.size());
        Point3 hard = p;
        if (sharp.size() == 2 && vertex_faces[v].size() != 1) {
            Point3 other;
            for (int e : sharp) {
                const LevelEdge& le = edges[static_cast<size_t>(e)];
                other = other + m.points[static_cast<size_t>(le.a == static_cast<int>(v) ? le.b : le.a)];
            }
            hard = (p * 6.0 + other) * (1.0 / 8.0);
        }
        out.points[v] = hard * vs + smooth * (1.0 - vs);
    }
    const int base_face = static_cast<int>(nv + edges.size());
    for (size_t fi = 0; fi < m.faces.size(); ++fi) {
        out.points[static_cast<size_t>(base_face) + fi] = face_point[fi];
        const auto& f = m.faces[fi];
        size_t k = f.size();
        for (size_t j = 0; j < k; ++j) {
            int a = f[(j + k - 1) % k], b = f[j], c = f[(j + 1) % k];
            int bc = static_cast<int>(nv) + index[edge_key(b, c)];
            int ab = static_cast<int>(nv) + index[edge_key(a, b)];
            out.faces.push_back({b, bc, base_face + static_cast<int>(fi), ab});
        }
    }
    for (size_t i = 0; i < edges.size(); ++i) {
        double s = crease(edges[i]);
        double child = s < 0.0 ? s : std::max(0.0, s - 1.0);
        if (child == 0.0) continue;
        int mid = static_cast<int>(nv + i);
        out.creases[edge_key(edges[i].a, mid)] = child;
        out.creases[edge_key(edges[i].b, mid)] = child;
    }
    return out;
}

} // namespace

std::vector<std::vector<Point3>> mesh_wire(const MeshData& d) {
    const int n = static_cast<int>(d.vertices.size());
    // Each cage edge as the chain of a level's points along it, from its first vertex
    std::vector<std::vector<int>> chains;
    for (const auto& e : d.edges) {
        if (e.start < 0 || e.start >= n || e.end < 0 || e.end >= n) continue;
        chains.push_back({e.start, e.end});
    }
    MeshLevel m;
    m.points = d.vertices;
    for (const auto& f : d.faces) {
        bool valid = f.size() >= 3 && std::all_of(f.begin(), f.end(), [&](int v) { return v >= 0 && v < n; });
        if (valid) m.faces.push_back(f);
    }
    for (size_t i = 0; i < d.edges.size() && i < d.creases.size(); ++i) {
        if (d.creases[i] != 0.0) m.creases[edge_key(d.edges[i].start, d.edges[i].end)] = d.creases[i];
    }
    for (int level = 0; level < d.level && !m.faces.empty(); ++level) {
        size_t faces = 0;
        for (const auto& f : m.faces) faces += f.size();
        if (faces > kMaxFaces) break;
        std::vector<LevelEdge> edges;
        std::unordered_map<uint64_t, int> index;
        MeshLevel next = subdivide(m, edges, index);
        const int nv = static_cast<int>(m.points.size());
        for (auto& chain : chains) {
            std::vector<int> longer = {chain.front()};
            for (size_t k = 0; k + 1 < chain.size(); ++k) {
                auto it = index.find(edge_key(chain[k], chain[k + 1]));
                if (it != index.end()) longer.push_back(nv + it->second);
                longer.push_back(chain[k + 1]);
            }
            chain = std::move(longer);
        }
        m = std::move(next);
    }
    std::vector<std::vector<Point3>> out;
    for (const auto& chain : chains) {
        std::vector<Point3> points;
        for (int v : chain) points.push_back(m.points[static_cast<size_t>(v)]);
        out.push_back(std::move(points));
    }
    return out;
}

std::vector<uint8_t> mesh_proxy(const MeshData& d) {
    ProxyGraphics g;
    std::vector<std::vector<Point3>> wire = mesh_wire(d);
    for (size_t i = 0; i < wire.size(); ++i) {
        g.marker(static_cast<int>(8 * i + 2));
        g.polyline(wire[i]);
    }
    return g.bytes();
}

} // namespace internal
} // namespace hdxf
