#pragma once

// MESH.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// MESH (plan phase 4, B48/B49): AutoCAD 2010's subdivision mesh (AcDbSubDMesh) — its level-0
// cage of vertices, faces, edges and their creases, and the smoothing level AutoCAD subdivides
// it to (Catmull-Clark, B49). AutoCAD writes it in 2000 … 2010 with the same groups, its edges
// as proxy graphics.
// ---------------------------------------------------------------------------

// An edge of the cage: two vertex indices (90, 90), the smaller first as AutoCAD writes them
struct MeshEdge {
    int start = 0;
    int end = 0;
};

// A property a face, edge or vertex has of its own — as AutoCAD reads it and ODA writes it (no
// AutoCAD file has one): its color, material or transparency
struct MeshProperty {
    int type = 0;                       // 90: 0 color, 1 material, 2 transparency (3 a material
                                        //     mapper: see MeshData::overrides_unread)
    Color color;                        // type 0: 63, 421, 431
    std::string material_handle;        // type 1: 330, the MATERIAL
    Transparency transparency;          // type 2: 90
};

struct MeshOverride {
    int marker = 0;                     // 91: the sub-entity's marker
    std::vector<MeshProperty> properties;   // 92 (the count)
};

struct MeshData {
    int version = 2;                    // 71
    bool blend_crease = false;          // 72 ("blend crease"; changes nothing AutoCAD smooths)
    int level = 0;                      // 91: the smoothing level, 0 = the cage itself
    std::vector<Point3> vertices;       // 92 (the count), 10, 20, 30
    std::vector<std::vector<int>> faces;    // 93 (the list's size), 90: each face's vertex
                                            // count and its vertex indices
    std::vector<MeshEdge> edges;        // 94 (the count), 90, 90
    std::vector<double> creases;        // 95 (the count), 140: one per edge — 0 none, -1 always,
                                        //     n the levels it stays sharp (fractions blend)
    std::vector<MeshOverride> overrides;    // 90 (the count)
    // A MESH read with a material mapper (property type 3) among its overrides: the override
    // data from its count (90) on as read, `overrides` then empty — the mapper's DXF form is not
    // known (no AutoCAD file has one, AutoCAD's reader refuses every form tried) — written back
    // as it was
    std::vector<DxfGroup> overrides_unread;
};

} // namespace hdxf
