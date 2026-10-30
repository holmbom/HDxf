#include "sat_writer.hpp"

#include "format.hpp"
#include "geometry.hpp"
#include "validation.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace hdxf::acis::detail {

namespace {

// Puts an index in the order the first time it comes (a number is assigned afterwards)
void note_once(std::vector<int>& marks, std::vector<std::uint32_t>& order, std::uint32_t index) {
    if (marks[index] == kNoRecord) {
        marks[index] = 0;
        order.push_back(index);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// The numbering
// ---------------------------------------------------------------------------

SatPlan make_plan(const BuilderData& d, BodyId body) {
    SatPlan plan;
    plan.data = &d;
    plan.body = body;
    plan.body_record = &d.bodies[body.index];
    plan.kind = plan.body_record->kind;

    plan.lump_record.assign(d.lumps.size(), kNoRecord);
    plan.shell_record.assign(d.shells.size(), kNoRecord);
    plan.face_record.assign(d.faces.size(), kNoRecord);
    plan.loop_record.assign(d.loops.size(), kNoRecord);
    plan.coedge_record.assign(d.coedges.size(), kNoRecord);
    plan.edge_record.assign(d.edges.size(), kNoRecord);
    plan.vertex_record.assign(d.vertices.size(), kNoRecord);
    plan.point_record.assign(d.vertices.size(), kNoRecord);
    plan.surface_record.assign(d.surfaces.size(), kNoRecord);
    plan.curve_record.assign(d.curves.size(), kNoRecord);
    plan.pcurve_record.assign(d.pcurves.size(), kNoRecord);
    plan.surface_subtype.assign(d.surfaces.size(), -1);
    plan.edge_start_vertex.assign(d.edges.size(), 0);
    plan.edge_end_vertex.assign(d.edges.size(), 0);
    plan.edge_first_coedge.assign(d.edges.size(), 0);
    plan.coedge_partner.assign(d.coedges.size(), kNoRecord);
    plan.vertex_edge.assign(d.vertices.size(), kNoRecord);
    plan.vertex_end_flag.assign(d.vertices.size(), 0);

    // The order: the body's parts as the builder holds them
    for (const LumpId& lump_id : plan.body_record->lumps) {
        plan.lumps.push_back(lump_id.index);
        for (const ShellId& shell_id : d.lumps[lump_id.index].shells) {
            plan.shells.push_back(shell_id.index);
            for (const FaceId& face_id : d.shells[shell_id.index].faces) {
                plan.faces.push_back(face_id.index);
                for (const LoopId& loop_id : d.faces[face_id.index].loops) {
                    plan.loops.push_back(loop_id.index);
                    for (const CoedgeId& coedge_id : d.loops[loop_id.index].coedges) {
                        plan.coedges.push_back(coedge_id.index);
                    }
                }
            }
        }
    }

    // Edges in the order their first coedge comes; each edge as written, forward and from the vertex
    // at the lower parameter (a degenerate edge: from its one vertex)
    for (std::uint32_t ci : plan.coedges) {
        std::uint32_t ei = d.coedges[ci].edge.index;
        if (plan.edge_record[ei] != kNoRecord) continue;
        plan.edge_record[ei] = 0;
        plan.edges.push_back(ei);
        plan.edge_first_coedge[ei] = ci;
        const detail::EdgeRecord& e = d.edges[ei];
        plan.edge_start_vertex[ei] = e.against_curve ? e.end.index : e.start.index;
        plan.edge_end_vertex[ei] = e.against_curve ? e.start.index : e.end.index;
    }
    // Vertices in the order the edges come, the start before the end; each points at the first edge
    // that has it, and says which end it is
    for (std::uint32_t ei : plan.edges) {
        const std::uint32_t ends[2] = {plan.edge_start_vertex[ei], plan.edge_end_vertex[ei]};
        for (int end = 0; end < 2; ++end) {
            std::uint32_t vi = ends[end];
            if (plan.vertex_record[vi] != kNoRecord) continue;
            plan.vertex_record[vi] = 0;
            plan.vertices.push_back(vi);
            plan.vertex_edge[vi] = static_cast<int>(ei);
            plan.vertex_end_flag[vi] = (end == 1 && ends[0] != ends[1]) ? 1 : 0;
        }
    }
    // Surfaces in the order of the faces, curves in the order of the edges
    for (std::uint32_t fi : plan.faces) note_once(plan.surface_record, plan.surfaces, d.faces[fi].surface.index);
    for (std::uint32_t ei : plan.edges) {
        if (d.edges[ei].curve.valid()) note_once(plan.curve_record, plan.curves, d.edges[ei].curve.index);
    }
    for (std::uint32_t ci : plan.coedges) {
        if (d.coedges[ci].pcurve.valid()) plan.pcurves.push_back(ci);       // numbered by its coedge
    }

    // The record numbers: the asmheader, the body, the lumps, the transform, then the kinds in turn
    int next = 2;
    for (std::uint32_t i : plan.lumps) plan.lump_record[i] = next++;
    if (plan.body_record->has_transform) plan.transform_record = next++;
    for (std::uint32_t i : plan.shells) plan.shell_record[i] = next++;
    for (std::uint32_t i : plan.faces) plan.face_record[i] = next++;
    for (std::uint32_t i : plan.loops) plan.loop_record[i] = next++;
    for (std::uint32_t i : plan.coedges) plan.coedge_record[i] = next++;
    for (std::uint32_t i : plan.edges) plan.edge_record[i] = next++;
    for (std::uint32_t i : plan.vertices) plan.vertex_record[i] = next++;
    for (std::uint32_t i : plan.vertices) plan.point_record[i] = next++;
    for (std::uint32_t i : plan.surfaces) plan.surface_record[i] = next++;
    for (std::uint32_t i : plan.curves) plan.curve_record[i] = next++;
    // A pcurve record is numbered under its coedge's builder index
    for (std::uint32_t ci : plan.pcurves) plan.pcurve_record[d.coedges[ci].pcurve.index] = next++;
    plan.record_count = next;

    // Partners: the two uses of an edge point at each other
    std::map<std::uint32_t, std::vector<std::uint32_t>> uses;
    for (std::uint32_t ci : plan.coedges) uses[d.coedges[ci].edge.index].push_back(ci);
    for (const auto& [edge, coedges] : uses) {
        (void)edge;
        if (coedges.size() == 2) {
            plan.coedge_partner[coedges[0]] = plan.coedge_record[coedges[1]];
            plan.coedge_partner[coedges[1]] = plan.coedge_record[coedges[0]];
        }
    }
    return plan;
}

// ---------------------------------------------------------------------------
// The header and the body's own records
// ---------------------------------------------------------------------------

namespace {

bool is_identity(const double r[3][3]) {
    for (int i = 0; i < 3; ++i) {
        for (int k = 0; k < 3; ++k) {
            if (std::fabs(r[i][k] - (i == k ? 1.0 : 0.0)) > 1e-12) return false;
        }
    }
    return true;
}

} // namespace

void write_body_records(const SatPlan& plan, std::vector<Record>& records) {
    const BuilderData& d = *plan.data;
    const detail::BodyRecord& body = *plan.body_record;

    // asmheader: attribute, tag, the kernel's version as a string
    records.push_back(RecordWriter("asmheader").pointer(-1).integer(-1).string(kKernelVersion).finish());

    // body: attribute, tag, pattern, the first lump, the wire, the transform
    {
        RecordWriter out("body");
        out.pointer(-1).integer(-1).pointer(-1);
        out.pointer(plan.lumps.empty() ? -1 : plan.lump_record[plan.lumps.front()]);
        out.pointer(-1);
        out.pointer(plan.transform_record);
        records.push_back(out.finish());
    }

    // lump: ..., the next lump, its first shell, the body
    for (size_t i = 0; i < plan.lumps.size(); ++i) {
        const detail::LumpRecord& lump = d.lumps[plan.lumps[i]];
        RecordWriter out("lump");
        out.pointer(-1).integer(-1).pointer(-1);
        out.pointer(i + 1 < plan.lumps.size() ? plan.lump_record[plan.lumps[i + 1]] : -1);
        out.pointer(lump.shells.empty() ? -1 : plan.shell_record[lump.shells.front().index]);
        out.pointer(plan.body_number);
        records.push_back(out.finish());
    }

    // transform: attribute, tag, the rotation's rows (the images of the local axes), the translation,
    // the scale, and what kind of transform it is
    if (plan.transform_record != kNoRecord) {
        const Transform& t = body.transform;
        RecordWriter out("transform");
        out.pointer(-1).integer(-1);
        for (int k = 0; k < 3; ++k) out.triple({t.rotation[0][k], t.rotation[1][k], t.rotation[2][k]});
        out.triple(t.translation);
        out.real(t.scale);
        out.word(is_identity(t.rotation) ? "no_rotate" : "rotate").word("no_reflect").word("no_shear");
        records.push_back(out.finish());
    }
}

// ---------------------------------------------------------------------------
// The whole SAT
// ---------------------------------------------------------------------------

Result<SatDocument> write_sat(const BuilderData& data, BodyId body) {
    SatPlan plan = make_plan(data, body);
    std::vector<Record> records;
    write_body_records(plan, records);
    write_topology_records(plan, records);
    write_geometry_records(plan, records);

    SatDocument document;
    document.version = 21500;
    document.kind = plan.kind;
    document.record_count = static_cast<int>(records.size());
    document.lines = header_lines(document.record_count, data.options);
    std::vector<std::string> body_lines = layout_lines(records);
    document.lines.insert(document.lines.end(), body_lines.begin(), body_lines.end());
    return document;
}

} // namespace hdxf::acis::detail
