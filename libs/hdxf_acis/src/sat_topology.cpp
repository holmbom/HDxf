#include "sat_writer.hpp"

#include "validation.hpp"

namespace hdxf::acis::detail {

namespace {

// Topology and geometry records start with the attribute pointer, the tag and the pattern pointer:
// no attributes, no tag, no pattern
RecordWriter topology_record(const char* type) {
    RecordWriter out(type);
    out.pointer(-1).integer(-1).pointer(-1);
    return out;
}

// The record of the next element in a list (or -1 at its end)
template <class Id>
int following(const std::vector<Id>& list, size_t position, const std::vector<int>& record_of) {
    return position + 1 < list.size() ? record_of[list[position + 1].index] : -1;
}

bool double_sided(BodyKind kind) { return kind == BodyKind::Region || kind == BodyKind::OpenBody; }

} // namespace

// shell: the next shell of the lump, a subshell (none), its first face, a wire (none), the lump
// face: the next face, its first loop, the shell, a subshell (none), the surface, the face's sense,
//       single or double sided (and, double, "out")
// loop: the next loop, its first coedge, the face
// coedge: next, previous (round the loop), the partner, the edge, its sense on it, the loop, the pcurve
// edge: start vertex and parameter, end vertex and parameter, one of its coedges, the curve, the
//       sense on it (always forward), the convexity (unknown)
// vertex: one edge that has it, which end of it (0 start, 1 end), its point
void write_topology_records(const SatPlan& plan, std::vector<Record>& records) {
    const BuilderData& d = *plan.data;

    for (std::uint32_t si : plan.shells) {
        const detail::ShellRecord& shell = d.shells[si];
        const detail::LumpRecord& lump = d.lumps[shell.lump.index];
        size_t position = 0;
        while (lump.shells[position].index != si) ++position;
        RecordWriter out = topology_record("shell");
        out.pointer(following(lump.shells, position, plan.shell_record));
        out.pointer(-1);
        out.pointer(shell.faces.empty() ? -1 : plan.face_record[shell.faces.front().index]);
        out.pointer(-1);
        out.pointer(plan.lump_record[shell.lump.index]);
        records.push_back(out.finish());
    }

    for (std::uint32_t fi : plan.faces) {
        const detail::FaceRecord& face = d.faces[fi];
        const detail::ShellRecord& shell = d.shells[face.shell.index];
        size_t position = 0;
        while (shell.faces[position].index != fi) ++position;
        RecordWriter out = topology_record("face");
        out.pointer(following(shell.faces, position, plan.face_record));
        out.pointer(face.loops.empty() ? -1 : plan.loop_record[face.loops.front().index]);
        out.pointer(plan.shell_record[face.shell.index]);
        out.pointer(-1);
        out.pointer(plan.surface_record[face.surface.index]);
        out.word(face.reversed ? "reversed" : "forward");
        if (double_sided(plan.kind)) out.word("double").word("out");
        else out.word("single");
        records.push_back(out.finish());
    }

    for (std::uint32_t li : plan.loops) {
        const detail::LoopRecord& loop = d.loops[li];
        const detail::FaceRecord& face = d.faces[loop.face.index];
        size_t position = 0;
        while (face.loops[position].index != li) ++position;
        RecordWriter out = topology_record("loop");
        out.pointer(following(face.loops, position, plan.loop_record));
        out.pointer(plan.coedge_record[loop.coedges.front().index]);
        out.pointer(plan.face_record[loop.face.index]);
        records.push_back(out.finish());
    }

    for (std::uint32_t ci : plan.coedges) {
        const detail::CoedgeRecord& coedge = d.coedges[ci];
        const detail::EdgeRecord& edge = d.edges[coedge.edge.index];
        const detail::LoopRecord& loop = d.loops[coedge.loop.index];
        size_t n = loop.coedges.size();
        size_t position = 0;
        while (loop.coedges[position].index != ci) ++position;
        int next = plan.coedge_record[loop.coedges[(position + 1) % n].index];
        int previous = plan.coedge_record[loop.coedges[(position + n - 1) % n].index];
        // The sense on the edge as written. A degenerate edge is traversed "reversed", as AutoCAD
        // writes a cone's apex.
        bool reversed = edge.curve.valid() ? coedge.reversed != edge.against_curve : true;
        RecordWriter out = topology_record("coedge");
        out.pointer(next).pointer(previous).pointer(plan.coedge_partner[ci]);
        out.pointer(plan.edge_record[coedge.edge.index]);
        out.word(reversed ? "reversed" : "forward");
        out.pointer(plan.loop_record[coedge.loop.index]);
        out.pointer(coedge.pcurve.valid() ? plan.pcurve_record[coedge.pcurve.index] : -1);
        records.push_back(out.finish());
    }

    for (std::uint32_t ei : plan.edges) {
        const detail::EdgeRecord& edge = d.edges[ei];
        RecordWriter out = topology_record("edge");
        out.pointer(plan.vertex_record[plan.edge_start_vertex[ei]]);
        // A degenerate edge has the parameters 1 and 0 in AutoCAD's cone
        out.real(edge.curve.valid() ? edge.start_parameter : 1.0);
        out.pointer(plan.vertex_record[plan.edge_end_vertex[ei]]);
        out.real(edge.curve.valid() ? edge.end_parameter : 0.0);
        out.pointer(plan.coedge_record[plan.edge_first_coedge[ei]]);
        out.pointer(edge.curve.valid() ? plan.curve_record[edge.curve.index] : -1);
        out.word("forward");
        out.string("unknown");
        records.push_back(out.finish());
    }

    for (std::uint32_t vi : plan.vertices) {
        RecordWriter out = topology_record("vertex");
        out.pointer(plan.edge_record[static_cast<std::uint32_t>(plan.vertex_edge[vi])]);
        out.integer(plan.vertex_end_flag[vi]);
        out.pointer(plan.point_record[vi]);
        records.push_back(out.finish());
    }

    for (std::uint32_t vi : plan.vertices) {
        RecordWriter out = topology_record("point");
        out.triple(d.vertices[vi].position);
        records.push_back(out.finish());
    }
}

} // namespace hdxf::acis::detail
