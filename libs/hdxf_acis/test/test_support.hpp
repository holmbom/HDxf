#pragma once

// What the hdxf_acis tests share: reading what the builder wrote the way HDxf reads it, bodies to
// build and break, and the AutoCAD facit.

#include "acis.hpp"
#include "hdxf.hpp"
#include "hdxf_acis.hpp"
#include "test_framework.hpp"

#include <cmath>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace acis_test {

namespace acis = hdxf::acis;

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------

std::string errors_text(const std::vector<acis::Diagnostic>& errors);
bool has_code(const std::vector<acis::Diagnostic>& errors, acis::ErrorCode code);

// The value of a result, or a failed test that lists why there is none
template <class T>
T& require(acis::Result<T>& result, const std::string& what = "") {
    if (!result) throw hdxf_test::AssertFailure{what + ": " + errors_text(result.errors())};
    return result.value();
}

// The same for a result that is not kept: the value moves out
template <class T>
T require(acis::Result<T>&& result, const std::string& what = "") {
    if (!result) throw hdxf_test::AssertFailure{what + ": " + errors_text(result.errors())};
    return std::move(result.value());
}

inline void require_ok(const acis::Result<void>& result, const std::string& what = "") {
    if (!result) throw hdxf_test::AssertFailure{what + ": " + errors_text(result.errors())};
}

// A failed result with a diagnostic of the code
void require_failure(const acis::Result<acis::SatDocument>& result, acis::ErrorCode code, const std::string& what);

// ---------------------------------------------------------------------------
// The SAT as HDxf reads it
// ---------------------------------------------------------------------------

struct ParsedSat {
    hdxf::internal::AcisFile file;
    std::vector<hdxf::internal::AcisBody> bodies;
};

// The document's lines enciphered, deciphered and read by HDxf's SAT reader
ParsedSat parse_document(const acis::SatDocument& document);

hdxf::AcisData acis_data(const acis::SatDocument& document);

int count_records(const hdxf::internal::AcisFile& file, const std::string& type);

// The largest distance, over the edge's parameters, between a coedge's pcurve taken on its surface
// and the edge's curve, as HDxf's reader evaluates them (SAT 21500's record layouts); negative when
// the coedge has no pcurve or the reader cannot evaluate what it refers to
double pcurve_deviation(const hdxf::internal::AcisFile& file, size_t coedge_record);
std::vector<std::string> record_types(const hdxf::internal::AcisFile& file);

// ---------------------------------------------------------------------------
// Drawings and extents
// ---------------------------------------------------------------------------

struct Box {
    hdxf::Point3 min;
    hdxf::Point3 max;
};

hdxf::EntityType entity_type_of(acis::BodyKind kind);

// A drawing of the given version holding the body
hdxf::Drawing drawing_of(const acis::SatDocument& document, hdxf::DxfVersion version);

// The drawing written to memory and read again
hdxf::Drawing reloaded(hdxf::Drawing& drawing, hdxf::DxfFormat format);

// The first 3DSOLID, REGION or BODY of a drawing
const hdxf::Entity& first_body(const hdxf::Drawing& drawing);
std::vector<const hdxf::Entity*> bodies_of(const hdxf::Drawing& drawing);

Box extents_of(const hdxf::Drawing& drawing, const hdxf::Entity& entity);
void require_box(const Box& actual, const Box& expected, double tolerance, const std::string& what);
void require_near(double actual, double expected, double tolerance, const std::string& what);

// ---------------------------------------------------------------------------
// Bodies to build, and to break
// ---------------------------------------------------------------------------

// What a box is built with, to break it in one place at a time
struct BoxFlags {
    int omit_face = -1;                 // this face is not built: an open shell
    int flip_face = -1;                 // this face runs the other way round its loop
    bool extra_face = false;            // a seventh face on the box's first edge: a third use
    int wrong_pair_face = -1;           // this face's first coedge is paired with one of another edge
    bool bspline_edge = false;          // the first edge's curve is a B-spline, not a line
    bool skew_vertex = false;           // one corner's vertex is moved off its edges
    bool invert = false;                // normals point in, loops run the other way (a cavity's faces)
};

// Where a box goes: its lower corner, and the body, lump or shell it is built in. Without a body
// a body is made; with a body and no lump, a lump; with a lump, a shell of the given role.
struct BoxPlacement {
    hdxf::Point3 origin;
    acis::BodyId body;
    acis::LumpId lump;
    acis::ShellRole role = acis::ShellRole::Outer;
    acis::BodyKind kind = acis::BodyKind::Solid;
};

struct BoxModel {
    acis::BodyId body;
    acis::LumpId lump;
    acis::ShellId shell;
    std::vector<acis::VertexId> corners;
    std::vector<acis::EdgeId> edges;
    std::vector<acis::FaceId> faces;
    std::vector<acis::LoopId> loops;
    std::vector<std::vector<acis::CoedgeId>> coedges;     // by face
};

// An axis-parallel box (corners at the origin and origin + sizes) in the builder: six faces, twelve edges
BoxModel build_box(acis::BodyBuilder& builder, double sx, double sy, double sz, const BoxFlags& flags = BoxFlags{},
                   const BoxPlacement& placement = BoxPlacement{});

// 30 × 30 × 24 with a cylindrical hole of radius 6 through its middle, at (15, 15): inner loops and a
// wall that is a cone-surface turned inside out
// (the hole's inner loops run the wrong way when `correct` is false). `autocad_frame`: the geometry around the
// block's center and a body transform to (15, 15, 12), as AutoCAD saves the block of 26_acis_referens.scr
acis::Result<acis::SatDocument> build_hole_block(bool correct = true, bool autocad_frame = false);

// Two 20 mm cubes in one body, in two lumps, 35 mm apart along x, the first with its lower corner at origin
acis::Result<acis::SatDocument> build_two_lumps(hdxf::Point3 origin = hdxf::Point3{});

// A solid with a void shell: a 40 mm cube with a 10 mm cube cavity. `autocad_frame`: the geometry around the
// center and a body transform to `center`, as AutoCAD saves the solid of 26_acis_referens.scr
acis::Result<acis::SatDocument> build_cavity_cube(bool autocad_frame = false, hdxf::Point3 center = hdxf::Point3{20.0, 20.0, 20.0});

// A sphere cut through its center, the upper half kept (the sphere face has one boundary loop, the circle the cut
// leaves, and the plane face the same circle): the form of AutoCAD's SLICE, in which the sphere has no seam or poles
acis::Result<acis::SatDocument> build_hemisphere(double radius, hdxf::Point3 center);

acis::Result<acis::SatDocument> build_rectangle_region(double x0, double y0, double x1, double y1,
                                                       acis::BodyKind kind = acis::BodyKind::Region);
acis::Result<acis::SatDocument> build_circle_region(hdxf::Point3 center, double radius);

// A cylinder (radius, height; around the origin, z from -height / 2) the way a boundary representation with a
// seam has it: the wall is one loop of four coedges, the circles' and the seam's twice, with the pcurves of
// the cylinder's parameters (u the height over the radius, v the angle) or without them
acis::Result<acis::SatDocument> build_seam_cylinder(double radius, double height, bool pcurves);

// What a NURBS cylinder is built with
struct NurbsFlags {
    bool pcurves = true;                // false: the wall's coedges have no pcurves
    double bottom_pcurve_v = 0.0;       // the bottom circle's pcurve at this v (not 0: it is off the circle)
    bool share_pcurve = false;          // the top circle's coedge uses the bottom circle's pcurve
};

// A cylinder (radius, height; around the origin from z = 0 up) whose wall is an exact NURBS
// surface (the circle as four rational quadratic arcs, a straight line along v), the circle edges
// B-spline curves, a seam, and the pcurves of all four coedges of the wall
acis::Result<acis::SatDocument> build_nurbs_cylinder(double radius, double height, const NurbsFlags& flags = NurbsFlags{});

// ---------------------------------------------------------------------------
// The AutoCAD facit (doc/facit, made by Erik's scripts; absent files skip the test)
// ---------------------------------------------------------------------------

std::filesystem::path facit_path(const std::string& name);

// Loads a facit file, or skips the test when it is not there
hdxf::Drawing load_facit(const std::string& name);

// The model-space bodies of a facit drawing, in file order
std::vector<const hdxf::Entity*> facit_bodies(const hdxf::Drawing& drawing);

// The facit's box, sphere, cylinder, cone, torus (script 10_solids.scr) in the 2010 drawing
struct FacitPrimitives {
    const hdxf::Entity* box = nullptr;
    const hdxf::Entity* sphere = nullptr;
    const hdxf::Entity* cylinder = nullptr;
    const hdxf::Entity* cone = nullptr;
    const hdxf::Entity* torus = nullptr;
    const hdxf::Entity* region_rectangle = nullptr;
    const hdxf::Entity* region_circle = nullptr;
};
FacitPrimitives find_facit_primitives(const hdxf::Drawing& drawing);

// The numbers of AutoCAD's primitives in 10_solids.scr
extern const double kPi;

} // namespace acis_test
