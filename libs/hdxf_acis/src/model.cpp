#include "model.hpp"

#include "format.hpp"
#include "geometry.hpp"
#include "sat_writer.hpp"
#include "validation.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace hdxf::acis {

// ---------------------------------------------------------------------------
// Diagnostics, handles and documents
// ---------------------------------------------------------------------------

const char* error_code_name(ErrorCode code) {
    switch (code) {
        case ErrorCode::InvalidHandle: return "invalid_handle";
        case ErrorCode::InvalidArgument: return "invalid_argument";
        case ErrorCode::InvalidGeometry: return "invalid_geometry";
        case ErrorCode::InvalidBSpline: return "invalid_bspline";
        case ErrorCode::UnsupportedGeometry: return "unsupported_geometry";
        case ErrorCode::EmptyBody: return "empty_body";
        case ErrorCode::ShellRole: return "shell_role";
        case ErrorCode::LooplessFace: return "loopless_face";
        case ErrorCode::LoopRole: return "loop_role";
        case ErrorCode::LoopNotClosed: return "loop_not_closed";
        case ErrorCode::VertexMismatch: return "vertex_mismatch";
        case ErrorCode::CurveEndpointMismatch: return "curve_endpoint_mismatch";
        case ErrorCode::SurfaceMismatch: return "surface_mismatch";
        case ErrorCode::EdgeUseCount: return "edge_use_count";
        case ErrorCode::PartnerMismatch: return "partner_mismatch";
        case ErrorCode::OrientationConflict: return "orientation_conflict";
        case ErrorCode::PcurveRequired: return "pcurve_required";
        case ErrorCode::PcurveMismatch: return "pcurve_mismatch";
        case ErrorCode::Encoding: return "encoding";
    }
    return "unknown";
}

std::string Diagnostic::text() const {
    std::string out = error_code_name(code);
    out += ": ";
    if (!path.empty()) out += path + ": ";
    else if (!entity.empty()) out += entity + ": ";
    out += message;
    return out;
}

std::string handle_text(HandleKind kind, std::uint32_t index) {
    const char* name = "handle";
    switch (kind) {
        case HandleKind::Body: name = "body"; break;
        case HandleKind::Lump: name = "lump"; break;
        case HandleKind::Shell: name = "shell"; break;
        case HandleKind::Face: name = "face"; break;
        case HandleKind::Loop: name = "loop"; break;
        case HandleKind::Coedge: name = "coedge"; break;
        case HandleKind::Edge: name = "edge"; break;
        case HandleKind::Vertex: name = "vertex"; break;
        case HandleKind::Curve: name = "curve"; break;
        case HandleKind::Surface: name = "surface"; break;
        case HandleKind::Pcurve: name = "pcurve"; break;
    }
    if (index == Handle<HandleKind::Body>::none) return std::string(name) + "[none]";
    return std::string(name) + "[" + detail::integer_text(index) + "]";
}

Diagnostic make_diagnostic(ErrorCode code, std::string entity, std::string path, std::string message) {
    Diagnostic d;
    d.code = code;
    d.entity = std::move(entity);
    d.path = std::move(path);
    d.message = std::move(message);
    return d;
}

std::string SatDocument::text() const {
    std::string out;
    for (const std::string& line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

Transform transform_from_frame(const Frame& frame) {
    Vector3 z = detail::normalized(frame.z_axis);
    Vector3 x = detail::normalized(frame.x_axis);
    Vector3 y = detail::cross(z, x);
    Transform t;
    const Vector3 columns[3] = {x, y, z};
    for (int k = 0; k < 3; ++k) {
        t.rotation[0][k] = columns[k].x;
        t.rotation[1][k] = columns[k].y;
        t.rotation[2][k] = columns[k].z;
    }
    t.translation = frame.origin;
    t.scale = 1.0;
    return t;
}

// ---------------------------------------------------------------------------
// The builder
// ---------------------------------------------------------------------------

namespace {

std::atomic<std::uint32_t> next_builder_id{1};

} // namespace

BodyBuilder::BodyBuilder(BuildOptions options) : data_(std::make_unique<BuilderData>()) {
    data_->options = options;
    data_->id = next_builder_id.fetch_add(1);
}

BodyBuilder::~BodyBuilder() = default;
BodyBuilder::BodyBuilder(BodyBuilder&&) noexcept = default;
BodyBuilder& BodyBuilder::operator=(BodyBuilder&&) noexcept = default;

namespace {

// A call's argument that is not a handle of this builder: noted for build_sat()
template <HandleKind Kind>
bool check_handle(BuilderData& d, const Handle<Kind>& h, const char* function, const char* argument) {
    if (d.owns(h)) return true;
    std::string why = !h.valid() ? "is not valid" : (h.builder != d.id ? "belongs to another builder" : "is out of range");
    d.pending.push_back(make_diagnostic(ErrorCode::InvalidHandle, handle_text(h), {},
                                        std::string(function) + ": the " + argument + " " + why));
    return false;
}

} // namespace

BodyId BodyBuilder::create_body(BodyKind kind) {
    BuilderData& d = *data_;
    detail::BodyRecord record;
    record.kind = kind;
    d.bodies.push_back(record);
    return d.make_handle<HandleKind::Body>(d.bodies.size() - 1);
}

LumpId BodyBuilder::add_lump(BodyId body) {
    BuilderData& d = *data_;
    if (!check_handle(d, body, "add_lump", "body")) return {};
    detail::LumpRecord record;
    record.body = body;
    d.lumps.push_back(record);
    LumpId id = d.make_handle<HandleKind::Lump>(d.lumps.size() - 1);
    d.bodies[body.index].lumps.push_back(id);
    return id;
}

ShellId BodyBuilder::add_shell(LumpId lump, ShellOptions options) {
    BuilderData& d = *data_;
    if (!check_handle(d, lump, "add_shell", "lump")) return {};
    detail::ShellRecord record;
    record.lump = lump;
    record.role = options.role;
    d.shells.push_back(record);
    ShellId id = d.make_handle<HandleKind::Shell>(d.shells.size() - 1);
    d.lumps[lump.index].shells.push_back(id);
    return id;
}

SurfaceId BodyBuilder::add_surface(SurfaceGeometry geometry) {
    BuilderData& d = *data_;
    detail::SurfaceRecord record;
    record.geometry = std::move(geometry);
    d.surfaces.push_back(std::move(record));
    return d.make_handle<HandleKind::Surface>(d.surfaces.size() - 1);
}

CurveId BodyBuilder::add_curve(CurveGeometry geometry) {
    BuilderData& d = *data_;
    detail::CurveRecord record;
    record.geometry = std::move(geometry);
    d.curves.push_back(std::move(record));
    return d.make_handle<HandleKind::Curve>(d.curves.size() - 1);
}

PcurveId BodyBuilder::add_pcurve(PcurveGeometry geometry) {
    BuilderData& d = *data_;
    detail::PcurveRecord record;
    record.geometry = std::move(geometry);
    d.pcurves.push_back(std::move(record));
    return d.make_handle<HandleKind::Pcurve>(d.pcurves.size() - 1);
}

VertexId BodyBuilder::add_vertex(Point3 position) {
    BuilderData& d = *data_;
    detail::VertexRecord record;
    record.position = position;
    d.vertices.push_back(record);
    return d.make_handle<HandleKind::Vertex>(d.vertices.size() - 1);
}

EdgeId BodyBuilder::add_edge(EdgeDefinition definition) {
    BuilderData& d = *data_;
    bool good = check_handle(d, definition.start, "add_edge", "start vertex");
    good = check_handle(d, definition.end, "add_edge", "end vertex") && good;
    if (definition.curve.valid()) good = check_handle(d, definition.curve, "add_edge", "curve") && good;
    if (!good) return {};
    detail::EdgeRecord record;
    record.start = definition.start;
    record.end = definition.end;
    record.curve = definition.curve;
    record.start_parameter = definition.start_parameter;
    record.end_parameter = definition.end_parameter;
    record.against_curve = definition.against_curve;
    d.edges.push_back(record);
    return d.make_handle<HandleKind::Edge>(d.edges.size() - 1);
}

FaceId BodyBuilder::add_face(ShellId shell, FaceDefinition definition) {
    BuilderData& d = *data_;
    bool good = check_handle(d, shell, "add_face", "shell");
    good = check_handle(d, definition.surface, "add_face", "surface") && good;
    if (!good) return {};
    detail::FaceRecord record;
    record.shell = shell;
    record.surface = definition.surface;
    record.reversed = definition.reversed;
    d.faces.push_back(record);
    FaceId id = d.make_handle<HandleKind::Face>(d.faces.size() - 1);
    d.shells[shell.index].faces.push_back(id);
    return id;
}

LoopId BodyBuilder::add_loop(FaceId face, LoopRole role) {
    BuilderData& d = *data_;
    if (!check_handle(d, face, "add_loop", "face")) return {};
    detail::LoopRecord record;
    record.face = face;
    record.role = role;
    d.loops.push_back(record);
    LoopId id = d.make_handle<HandleKind::Loop>(d.loops.size() - 1);
    d.faces[face.index].loops.push_back(id);
    return id;
}

CoedgeId BodyBuilder::add_coedge(LoopId loop, CoedgeDefinition definition) {
    BuilderData& d = *data_;
    bool good = check_handle(d, loop, "add_coedge", "loop");
    good = check_handle(d, definition.edge, "add_coedge", "edge") && good;
    if (definition.pcurve.valid()) good = check_handle(d, definition.pcurve, "add_coedge", "pcurve") && good;
    if (!good) return {};
    detail::CoedgeRecord record;
    record.loop = loop;
    record.edge = definition.edge;
    record.reversed = definition.reversed;
    record.pcurve = definition.pcurve;
    d.coedges.push_back(record);
    CoedgeId id = d.make_handle<HandleKind::Coedge>(d.coedges.size() - 1);
    d.loops[loop.index].coedges.push_back(id);
    return id;
}

Result<void> BodyBuilder::set_transform(BodyId body, const Transform& transform) {
    BuilderData& d = *data_;
    if (!d.owns(body)) {
        return make_diagnostic(ErrorCode::InvalidHandle, handle_text(body), {}, "set_transform: the body is not one of this builder's");
    }
    std::string why = detail::check_transform(transform, d.options.tolerance);
    if (!why.empty()) {
        return make_diagnostic(ErrorCode::InvalidGeometry, handle_text(body), handle_text(body), why);
    }
    d.bodies[body.index].has_transform = true;
    d.bodies[body.index].transform = transform;
    return {};
}

Result<void> BodyBuilder::set_loop_order(LoopId loop, std::span<const CoedgeId> coedges) {
    BuilderData& d = *data_;
    if (!d.owns(loop)) {
        return make_diagnostic(ErrorCode::InvalidHandle, handle_text(loop), {}, "set_loop_order: the loop is not one of this builder's");
    }
    std::vector<CoedgeId>& current = d.loops[loop.index].coedges;
    bool same = coedges.size() == current.size();
    for (size_t i = 0; same && i < coedges.size(); ++i) {
        const CoedgeId& id = coedges[i];
        bool found = std::find(current.begin(), current.end(), id) != current.end();
        bool repeated = std::find(coedges.begin(), coedges.begin() + static_cast<std::ptrdiff_t>(i), id) !=
                        coedges.begin() + static_cast<std::ptrdiff_t>(i);
        same = found && !repeated;
    }
    if (!same) {
        return make_diagnostic(ErrorCode::InvalidArgument, handle_text(loop), handle_text(loop),
                               "set_loop_order: the order must hold the loop's coedges, each once");
    }
    current.assign(coedges.begin(), coedges.end());
    return {};
}

Result<void> BodyBuilder::pair_coedges(CoedgeId first, CoedgeId second) {
    BuilderData& d = *data_;
    if (!d.owns(first) || !d.owns(second)) {
        return make_diagnostic(ErrorCode::InvalidHandle, handle_text(first), {}, "pair_coedges: a coedge is not one of this builder's");
    }
    if (first == second) {
        return make_diagnostic(ErrorCode::InvalidArgument, handle_text(first), {}, "pair_coedges: a coedge cannot be its own partner");
    }
    d.coedges[first.index].partner = second;
    d.coedges[second.index].partner = first;
    return {};
}

Result<SatDocument> BodyBuilder::build_sat(BodyId body) const {
    const BuilderData& d = *data_;
    if (!d.owns(body)) {
        return make_diagnostic(ErrorCode::InvalidHandle, handle_text(body), {}, "build_sat: the body is not one of this builder's");
    }
    std::vector<Diagnostic> errors = d.pending;
    const BuildOptions& o = d.options;
    auto positive = [](double v) { return std::isfinite(v) && v > 0.0; };
    if (!positive(o.header_unit) || !positive(o.resabs) || !positive(o.resnor) || !positive(o.tolerance.distance) ||
        !positive(o.tolerance.parameter) || !positive(o.tolerance.direction)) {
        errors.push_back(make_diagnostic(ErrorCode::InvalidArgument, {}, {},
                                         "build_sat: the unit, resabs, resnor and the tolerances must be positive numbers"));
    }
    std::vector<Diagnostic> found = detail::validate_body(d, body);
    errors.insert(errors.end(), found.begin(), found.end());
    if (!errors.empty()) return errors;
    return detail::write_sat(d, body);
}

} // namespace hdxf::acis
