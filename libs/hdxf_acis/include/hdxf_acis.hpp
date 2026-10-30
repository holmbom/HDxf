#pragma once

// ---------------------------------------------------------------------------
// hdxf_acis: ACIS bodies built from a program's exact B-rep
//
// A program describes its B-rep (bodies, lumps, shells, faces, loops, coedges, edges,
// vertices, curves, surfaces, pcurves) to a BodyBuilder; the builder validates the topology
// and the geometry and writes ACIS SAT 21500 (a SatDocument: the plain logical lines), which
// to_acis_data() turns into the hdxf::AcisData of a 3DSOLID, REGION or BODY.
//
//   auto sat = hdxf::acis::make_sphere(parameters);
//   if (!sat) return report(sat.errors());
//   auto acis = hdxf::acis::to_acis_data(sat.value());
//   if (!acis) return report(acis.errors());
//   drawing.add_acis(hdxf::EntityType::Solid3D, std::move(acis.value()));
//
// Conventions (details in doc/reference/hdxf_acis_sat_21500.md):
//   - All lengths are millimetres, all angles radians, all strings UTF-8.
//   - Tolerances (BuildOptions) are used to validate, never to merge vertices, to
//     approximate a curve or to change a control point.
//   - Nothing is tessellated or approximated: a surface or a curve the builder cannot write
//     exactly is an error.
//   - A face's normal (its surface's normal turned by the face's sense) points out of the
//     material; a loop runs counter-clockwise about it (outer) or clockwise (inner).
//   - Handles are indices into one builder; the indices in diagnostics are those handles'.
// ---------------------------------------------------------------------------

#include "hdxf.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hdxf::acis {

using Vector3 = Point3;

// ---------------------------------------------------------------------------
// Errors and results
// ---------------------------------------------------------------------------

enum class ErrorCode {
    InvalidHandle,          // a handle that is not valid, or is from another builder
    InvalidArgument,        // a value outside what the function accepts
    InvalidGeometry,        // a curve, surface or transform that is not valid
    InvalidBSpline,         // degree, knots, control net or weights that do not fit together
    UnsupportedGeometry,    // valid, but the builder cannot write it exactly
    EmptyBody,              // a body without lumps, a lump without shells, a shell without faces
    ShellRole,              // outer shells and void shells that do not fit the body's kind
    LooplessFace,           // a face without loops on a surface that is not closed in both directions
    LoopRole,               // outer and inner loops that do not fit the face
    LoopNotClosed,          // a loop whose coedges do not join, or an edge in two shells
    VertexMismatch,         // the same position in two vertices, or an edge's ends that are not its vertices
    CurveEndpointMismatch,  // an edge's vertex that is not on its curve at the edge's parameter
    SurfaceMismatch,        // an edge's curve that is not on the surface of its face
    EdgeUseCount,           // an edge with a number of coedges the shell does not allow
    PartnerMismatch,        // partner coedges that do not point at each other or use two edges
    OrientationConflict,    // two uses of an edge in the same direction, or a loop against its face
    PcurveRequired,         // a coedge on a B-spline surface without a pcurve
    PcurveMismatch,         // a pcurve that is not the edge's curve on the surface
    Encoding,               // SAT text that cannot be enciphered
};

// The name a diagnostic starts with: "edge_use_count", "partner_mismatch" …
const char* error_code_name(ErrorCode code);

struct Diagnostic {
    ErrorCode code = ErrorCode::InvalidArgument;
    std::string entity;                 // the handle concerned: "edge[12]"; empty when none
    std::string path;                   // "body[0]/lump[0]/shell[0]/edge[12]"; empty when none
    std::string message;                // UTF-8, readable

    // "edge_use_count: body[0]/lump[0]/shell[0]/edge[12]: a closed shell needs exactly two ..."
    std::string text() const;
};

// A value or the diagnostics of why there is none. No exceptions for errors in the input;
// value() of a failure is a programming error and throws std::logic_error.
template <class T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(std::vector<Diagnostic> errors) : errors_(std::move(errors)) {}
    Result(Diagnostic error) { errors_.push_back(std::move(error)); }

    explicit operator bool() const { return value_.has_value(); }
    bool ok() const { return value_.has_value(); }

    T& value() { check(); return *value_; }
    const T& value() const { check(); return *value_; }
    const std::vector<Diagnostic>& errors() const { return errors_; }

private:
    void check() const {
        if (!value_) throw std::logic_error("hdxf::acis::Result::value() of a failure");
    }
    std::optional<T> value_;
    std::vector<Diagnostic> errors_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(std::vector<Diagnostic> errors) : errors_(std::move(errors)) {}
    Result(Diagnostic error) { errors_.push_back(std::move(error)); }

    explicit operator bool() const { return errors_.empty(); }
    bool ok() const { return errors_.empty(); }
    const std::vector<Diagnostic>& errors() const { return errors_; }

private:
    std::vector<Diagnostic> errors_;
};

// ---------------------------------------------------------------------------
// Handles: typed indices into one builder
// ---------------------------------------------------------------------------

enum class HandleKind { Body, Lump, Shell, Face, Loop, Coedge, Edge, Vertex, Curve, Surface, Pcurve };

template <HandleKind Kind>
struct Handle {
    static constexpr std::uint32_t none = 0xFFFFFFFFu;

    std::uint32_t builder = 0;          // the builder that made it
    std::uint32_t index = none;         // creation order among the builder's handles of this kind

    bool valid() const { return index != none; }
    friend bool operator==(const Handle&, const Handle&) = default;
};

using BodyId = Handle<HandleKind::Body>;
using LumpId = Handle<HandleKind::Lump>;
using ShellId = Handle<HandleKind::Shell>;
using FaceId = Handle<HandleKind::Face>;
using LoopId = Handle<HandleKind::Loop>;
using CoedgeId = Handle<HandleKind::Coedge>;
using EdgeId = Handle<HandleKind::Edge>;
using VertexId = Handle<HandleKind::Vertex>;
using CurveId = Handle<HandleKind::Curve>;
using SurfaceId = Handle<HandleKind::Surface>;
using PcurveId = Handle<HandleKind::Pcurve>;

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

struct Tolerances {
    double distance = 1e-6;             // mm: points on curves and surfaces, vertices that meet
    double parameter = 1e-9;            // parameters that are the same
    double direction = 1e-9;            // unit vectors, perpendicular axes, rotation matrices
};

struct BuildOptions {
    Tolerances tolerance;
    double resabs = 1e-6;               // the SAT header's resabs
    double resnor = 1e-10;              // the SAT header's resnor
    // The SAT header's unit: millimetres per coordinate unit. The coordinates are millimetres,
    // so 1. (AutoCAD's own files say 25.4 when they were made from the imperial template; its
    // core reads a DXF's ACIS data in the drawing's units whatever the header says.)
    double header_unit = 1.0;
};

// What the body is meant to be: the entity it becomes decides which checks must pass
enum class BodyKind {
    Solid,                              // 3DSOLID: closed shells enclosing material
    Region,                             // REGION: planar, an outer boundary and perhaps holes
    ClosedBody,                         // BODY with closed shells
    OpenBody,                           // BODY with open shells (sheets): free edges are allowed
};

enum class ShellRole { Outer, Void };       // a void shell is a cavity inside the lump's outer shell

enum class LoopRole {
    Outer,                              // on a planar face: counter-clockwise about the face's normal
    Inner,                              // on a planar face: a hole, clockwise
    Boundary,                           // on any other face: a closed boundary, no inside or outside
};

// ---------------------------------------------------------------------------
// Geometry. Surfaces and curves are exactly what the SAT stores.
// ---------------------------------------------------------------------------

// A coordinate system: the origin and two axes (the third is z × x)
struct Frame {
    Point3 origin;
    Vector3 z_axis{0.0, 0.0, 1.0};
    Vector3 x_axis{1.0, 0.0, 0.0};
};

// WCS = scale · R · local + translation, R's columns the images of the local axes. A body's
// transform: rotation and a positive uniform scale (no reflection, no shear).
struct Transform {
    double rotation[3][3] = {{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}};
    Point3 translation;
    double scale = 1.0;
};

// The transform that puts a body made around the origin at `frame`
Transform transform_from_frame(const Frame& frame);

enum class Closure { Open, Closed, Periodic };
enum class Singularity { None, Low, High, Both };

// S(u, v) = origin + u · u_direction + v · (normal × u_direction)
struct PlaneGeometry {
    Point3 origin;
    Vector3 normal{0.0, 0.0, 1.0};      // unit
    Vector3 u_direction{1.0, 0.0, 0.0}; // unit, perpendicular to the normal
};

// A right circular cone or cylinder. The reference circle (center, major axis' length as the
// radius, normal `axis`) is at u = 0; the radius changes by `radius_gradient` per unit along
// the axis (0: a cylinder; negative: narrowing along the axis; the apex is where it reaches
// 0). The surface's normal points away from the axis. S(u, v) = the circle at the angle v
// (counter-clockwise about the axis from the major axis) moved u · |major_axis| along the line
// through it.
struct ConeGeometry {
    Point3 center;
    Vector3 axis{0.0, 0.0, 1.0};        // unit
    Vector3 major_axis{1.0, 0.0, 0.0};  // perpendicular to the axis; its length is the reference radius
    double radius_gradient = 0.0;       // tan of the half angle, signed
};

// S(u, v) = center + radius · (cos u · (cos v · x + sin v · y) + sin u · pole), x = u_direction,
// y = pole × x: u the latitude, v the longitude. The normal points out.
struct SphereGeometry {
    Point3 center;
    double radius = 1.0;
    Vector3 u_direction{1.0, 0.0, 0.0}; // unit, perpendicular to the pole
    Vector3 pole{0.0, 0.0, 1.0};        // unit
};

// A ring torus (major radius > minor radius > 0): S(u, v) = center + (R + r · cos u) ·
// (cos v · x + sin v · y) + r · sin u · axis, x = u_direction, y = axis × x. The normal points out.
struct TorusGeometry {
    Point3 center;
    Vector3 axis{0.0, 0.0, 1.0};        // unit
    double major_radius = 1.0;
    double minor_radius = 0.25;
    Vector3 u_direction{1.0, 0.0, 0.0}; // unit, perpendicular to the axis
};

// A B-spline or NURBS surface: the whole knot vectors (count + degree + 1 values), the control
// net with u varying fastest. For a periodic direction the net repeats its first `degree`
// points at the end, as ACIS holds it.
struct BSplineSurfaceGeometry {
    int degree_u = 3;
    int degree_v = 3;
    int count_u = 0;
    int count_v = 0;
    std::vector<double> knots_u;
    std::vector<double> knots_v;
    std::vector<Point3> points;         // count_u · count_v
    std::vector<double> weights;        // empty: polynomial; else one per control point
    Closure closure_u = Closure::Open;
    Closure closure_v = Closure::Open;
    Singularity singular_u = Singularity::None;
    Singularity singular_v = Singularity::None;
};

using SurfaceGeometry = std::variant<PlaneGeometry, ConeGeometry, SphereGeometry, TorusGeometry,
                                     BSplineSurfaceGeometry>;

// A straight curve: C(t) = origin + t · direction, t the distance
struct LineGeometry {
    Point3 origin;
    Vector3 direction{1.0, 0.0, 0.0};   // unit
};

// C(t) = center + cos t · major_axis + sin t · ratio · (normal × major_axis): t the angle
struct EllipseGeometry {
    Point3 center;
    Vector3 normal{0.0, 0.0, 1.0};      // unit
    Vector3 major_axis{1.0, 0.0, 0.0};  // perpendicular to the normal; a circle's radius is its length
    double ratio = 1.0;                 // minor / major, in (0, 1]
};

struct BSplineCurveGeometry {
    int degree = 3;
    std::vector<double> knots;          // points.size() + degree + 1 values
    std::vector<Point3> points;
    std::vector<double> weights;        // empty: polynomial; else one per control point
    Closure closure = Closure::Open;
};

using CurveGeometry = std::variant<LineGeometry, EllipseGeometry, BSplineCurveGeometry>;

// A coedge's curve in the surface's parameters (u, v): t is the edge's curve parameter, so
// S(P(t)) = C(t). The surface's parameters are those of the geometry types above.
struct BSplineCurve2D {
    int degree = 1;
    std::vector<double> knots;          // points.size() + degree + 1 values
    std::vector<hdxf::Point2> points;   // (u, v)
    std::vector<double> weights;        // empty: polynomial
    Closure closure = Closure::Open;
};

using PcurveGeometry = BSplineCurve2D;

// ---------------------------------------------------------------------------
// Definitions
// ---------------------------------------------------------------------------

// An edge runs from `start` to `end`. Its curve is traversed over [start_parameter,
// end_parameter] (the first less than the second): the edge's start is at start_parameter
// (end at end_parameter) unless `against_curve`, which puts the start at end_parameter. A closed
// edge has the same vertex at both ends. No curve: a degenerate edge (a cone's apex), a single
// vertex that a face's loop must run through.
struct EdgeDefinition {
    VertexId start;
    VertexId end;
    CurveId curve;
    double start_parameter = 0.0;
    double end_parameter = 0.0;
    bool against_curve = false;
};

struct FaceDefinition {
    SurfaceId surface;
    bool reversed = false;              // the face's normal is the surface's turned round
};

struct ShellOptions {
    ShellRole role = ShellRole::Outer;
};

// A use of an edge in a loop. `reversed`: it runs from the edge's end to its start. The pcurve is
// required on a B-spline surface and may be given on any other.
struct CoedgeDefinition {
    EdgeId edge;
    bool reversed = false;
    PcurveId pcurve;
};

struct BuilderData;

// ---------------------------------------------------------------------------
// The output
// ---------------------------------------------------------------------------

// ACIS SAT as plain logical lines: the header, then the records. A record is on one or more lines
// (a continuation line starts with a space); no line is longer than 254 characters.
struct SatDocument {
    int version = 21500;
    BodyKind kind = BodyKind::Solid;
    int record_count = 0;               // the SAT header's record count (the asmheader included)
    std::vector<std::string> lines;

    std::string text() const;           // the lines joined with "\n", a "\n" after the last
};

// ---------------------------------------------------------------------------
// The builder
// ---------------------------------------------------------------------------

class BodyBuilder {
public:
    explicit BodyBuilder(BuildOptions options = BuildOptions{});
    ~BodyBuilder();
    BodyBuilder(BodyBuilder&&) noexcept;
    BodyBuilder& operator=(BodyBuilder&&) noexcept;
    BodyBuilder(const BodyBuilder&) = delete;
    BodyBuilder& operator=(const BodyBuilder&) = delete;

    // Topology. An invalid argument gives an invalid handle and a diagnostic that build_sat()
    // reports, so a build is written without a check after each call.
    BodyId create_body(BodyKind kind);
    LumpId add_lump(BodyId body);
    ShellId add_shell(LumpId lump, ShellOptions options = ShellOptions{});

    SurfaceId add_surface(SurfaceGeometry geometry);
    CurveId add_curve(CurveGeometry geometry);
    PcurveId add_pcurve(PcurveGeometry geometry);
    VertexId add_vertex(Point3 position);

    EdgeId add_edge(EdgeDefinition definition);
    FaceId add_face(ShellId shell, FaceDefinition definition);
    LoopId add_loop(FaceId face, LoopRole role);
    CoedgeId add_coedge(LoopId loop, CoedgeDefinition definition);

    // The body's transform to the WCS (default: none)
    Result<void> set_transform(BodyId body, const Transform& transform);

    // The order of a loop's coedges (the order they were added in by default): the same
    // coedges, each once
    Result<void> set_loop_order(LoopId loop, std::span<const CoedgeId> coedges);

    // Partner coedges: the two uses of one edge. Without it the two coedges of an edge are partners.
    Result<void> pair_coedges(CoedgeId first, CoedgeId second);

    // Validates the body and writes it as SAT 21500. A failure has no partial result.
    Result<SatDocument> build_sat(BodyId body) const;

private:
    std::unique_ptr<BuilderData> data_;
};

// The ACIS data of a 3DSOLID, REGION or BODY entity: the lines enciphered as AutoCAD saves them
// (HDxf writes the DXF groups, and in 2013 and 2018 the SAB made from the lines). The data is SAT
// 21500 for DXF 2000 … 2018; DXF R13 and R14 hold SAT 105 and 106 at most (ACIS 1.5 and 1.6, plan phase 7,
// 7.17), so HDxf refuses a drawing with such a body as R13/R14 ("ACIS SAT version 21500 cannot be
// written as DXF R14"): the builder writes no older SAT, and HDxf does not convert it
Result<hdxf::AcisData> to_acis_data(const SatDocument& document);

// ---------------------------------------------------------------------------
// Primitives, built through the same model and checks. Each is made around the frame's origin
// (a box, cylinder, cone and torus centered on it, the cone's apex on +z), so the SAT has the
// geometry at the origin and a body transform to the frame, as AutoCAD's primitives.
// ---------------------------------------------------------------------------

struct BoxParameters {
    Frame frame;
    double size_x = 1.0;
    double size_y = 1.0;
    double size_z = 1.0;
    BuildOptions options;
};

struct CylinderParameters {
    Frame frame;                        // the middle of the axis
    double radius = 1.0;
    double height = 1.0;
    BuildOptions options;
};

// radius at -z, top_radius at +z (0: the cone ends in its apex)
struct ConeParameters {
    Frame frame;                        // the middle of the axis
    double radius = 1.0;
    double top_radius = 0.0;
    double height = 1.0;
    BuildOptions options;
};

struct SphereParameters {
    Frame frame;                        // the center
    double radius = 1.0;
    BuildOptions options;
};

// A ring torus: major_radius > minor_radius > 0
struct TorusParameters {
    Frame frame;                        // the center, z the axis
    double major_radius = 1.0;
    double minor_radius = 0.25;
    BuildOptions options;
};

Result<SatDocument> make_box(BoxParameters parameters);
Result<SatDocument> make_cylinder(CylinderParameters parameters);
Result<SatDocument> make_cone(ConeParameters parameters);
Result<SatDocument> make_sphere(SphereParameters parameters);
Result<SatDocument> make_torus(TorusParameters parameters);

} // namespace hdxf::acis
