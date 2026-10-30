#pragma once

// The builder's storage: what the BodyBuilder holds between its calls (internal)

#include "hdxf_acis.hpp"

#include <string>
#include <vector>

namespace hdxf::acis::detail {

struct BodyRecord {
    BodyKind kind = BodyKind::Solid;
    bool has_transform = false;
    Transform transform;
    std::vector<LumpId> lumps;
};

struct LumpRecord {
    BodyId body;
    std::vector<ShellId> shells;
};

struct ShellRecord {
    LumpId lump;
    ShellRole role = ShellRole::Outer;
    std::vector<FaceId> faces;
};

struct FaceRecord {
    ShellId shell;
    SurfaceId surface;
    bool reversed = false;
    std::vector<LoopId> loops;
};

struct LoopRecord {
    FaceId face;
    LoopRole role = LoopRole::Outer;
    std::vector<CoedgeId> coedges;
};

struct CoedgeRecord {
    LoopId loop;
    EdgeId edge;
    bool reversed = false;
    PcurveId pcurve;
    CoedgeId partner;                   // set by pair_coedges()
};

struct EdgeRecord {
    VertexId start;
    VertexId end;
    CurveId curve;
    double start_parameter = 0.0;
    double end_parameter = 0.0;
    bool against_curve = false;
};

struct VertexRecord {
    Point3 position;
};

struct CurveRecord {
    CurveGeometry geometry;
};

struct SurfaceRecord {
    SurfaceGeometry geometry;
};

struct PcurveRecord {
    PcurveGeometry geometry;
};

} // namespace hdxf::acis::detail

namespace hdxf::acis {

// Everything a builder holds. The handles' indices are positions in these lists.
struct BuilderData {
    BuildOptions options;
    std::uint32_t id = 0;               // makes a handle of another builder recognizable

    std::vector<detail::BodyRecord> bodies;
    std::vector<detail::LumpRecord> lumps;
    std::vector<detail::ShellRecord> shells;
    std::vector<detail::FaceRecord> faces;
    std::vector<detail::LoopRecord> loops;
    std::vector<detail::CoedgeRecord> coedges;
    std::vector<detail::EdgeRecord> edges;
    std::vector<detail::VertexRecord> vertices;
    std::vector<detail::CurveRecord> curves;
    std::vector<detail::SurfaceRecord> surfaces;
    std::vector<detail::PcurveRecord> pcurves;

    // What the calls found wrong; build_sat() reports it
    std::vector<Diagnostic> pending;

    // How many handles of a kind there are
    size_t count_of(HandleKind kind) const {
        switch (kind) {
            case HandleKind::Body: return bodies.size();
            case HandleKind::Lump: return lumps.size();
            case HandleKind::Shell: return shells.size();
            case HandleKind::Face: return faces.size();
            case HandleKind::Loop: return loops.size();
            case HandleKind::Coedge: return coedges.size();
            case HandleKind::Edge: return edges.size();
            case HandleKind::Vertex: return vertices.size();
            case HandleKind::Curve: return curves.size();
            case HandleKind::Surface: return surfaces.size();
            case HandleKind::Pcurve: return pcurves.size();
        }
        return 0;
    }

    // Whether a handle is one of this builder's and in range
    template <HandleKind Kind>
    bool owns(const Handle<Kind>& h) const {
        return h.valid() && h.builder == id && h.index < count_of(Kind);
    }

    template <HandleKind Kind>
    Handle<Kind> make_handle(size_t index) const {
        Handle<Kind> h;
        h.builder = id;
        h.index = static_cast<std::uint32_t>(index);
        return h;
    }
};

// "edge[12]"
std::string handle_text(HandleKind kind, std::uint32_t index);

template <HandleKind Kind>
std::string handle_text(const Handle<Kind>& h) {
    return handle_text(Kind, h.index);
}

Diagnostic make_diagnostic(ErrorCode code, std::string entity, std::string path, std::string message);

} // namespace hdxf::acis
