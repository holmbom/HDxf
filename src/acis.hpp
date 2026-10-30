#pragma once

#include "extents.hpp"
#include "hdxf.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// ACIS data: the B-rep of 3DSOLID, REGION and BODY (rättningsfas R.9)
//
// The format is Spatial's "SAT Save File Format" (ACIS 7.0 documentation, with
// versions.hxx): a header and entity records of pointers ($n), numbers, identifiers,
// strings (@n) and subtypes ({ … }, referenced as "ref n"). SAB is the same records with
// tagged binary values. AutoCAD writes SAT in the entity's 1/3 lines (R13–2010, each
// character but the space as 159 - c) and SAB in the ACDSDATA section (2013+).
// ---------------------------------------------------------------------------

namespace hdxf {
namespace internal {

enum class AcisTokenType {
    Pointer,            // $n (SAB tag 12)
    Number,             // an integer or a real (SAB tags 2–6, and 19/20/22 as numbers)
    Word,               // an identifier: forward, I, nubs, exactsur … (SAB tags 13, 14)
    Logical,            // SAB tags 10 (true) and 11 (false)
    Enum,               // SAB tag 21
    String,             // @n text (SAB tags 7, 8, 9)
    SubtypeStart,       // {
    SubtypeEnd,         // }
};

struct AcisToken {
    AcisTokenType type = AcisTokenType::Number;
    double number = 0.0;                // Pointer, Number, Enum; Logical: 1 true, 0 false
    std::string text;                   // Word, String; Number: as SAT wrote it (SAB: empty)
    size_t begin = 0;                   // SAT: where it stands in the text (a string: its "@"),
    size_t end = 0;                     //     and the position after it (SAB: 0)
};

// An entity record: its save identifier ("plane-surface") and its data, without the
// terminator
struct AcisRecord {
    std::string type;
    std::vector<AcisToken> tokens;
    size_t end = 0;                     // SAT: the position of its "#" in the text (SAB: 0)
};

// Where a subtype definition starts: its record and the index of its "{"
struct AcisSubtype {
    size_t record = 0;
    size_t token = 0;
};

struct AcisFile {
    int version = 0;                    // 100 × major + minor: 106, 400, 700, 21800 …
    std::vector<AcisRecord> records;    // $n is records[n]
    std::vector<AcisSubtype> subtypes;  // the definitions, numbered as "ref n" counts them
};

// SAT text → records. False when it is not SAT (or is broken).
bool read_sat(const std::string& text, AcisFile& out);

// SAB data ("ACIS BinaryFile" or "ASM BinaryFile4") → records
bool read_sab(const std::vector<uint8_t>& data, AcisFile& out);

// The SAT text in AutoCAD's 1/3 lines: each character but the space was written as
// 159 - c (2000–2010, verified against AutoCAD's files; R13/R14 the same)
std::string acis_deciphered(const AcisData& d);

// ---------------------------------------------------------------------------
// ACIS in DXF R13 and R14 (plan phase 7, 7.17, B95)
//
// The modeler's group 70 (AcDbModelerGeometry, 1 in every AutoCAD file) is not the SAT version:
// that is the first number of the SAT's first line (106 in AutoCAD's R14 REGION, 400 in AutoCAD
// 2000's). SAT text is the 1/3 lines of R13 … 2010 alike; R13/R14 have no ACDSDATA and no SAB.
// ---------------------------------------------------------------------------

// The SAT version of a body's SAT lines (the first number of the first line, 100 × major +
// minor). False when the body has no SAT lines or its first line starts with no number.
bool acis_sat_version(const AcisData& d, int& version);

// The newest SAT version a DXF version is written with: ACIS reads the save files of its own
// version and older ones. R13 (ACIS 1.5, SAT 105) and R14 (ACIS 1.6, SAT 106) — sources in
// doc/plans/2026-10-25-fas-7-dxf-r13-r14.md, "Utfall 7.17". 0 for a version that has no such
// limit here (R12 has no ACIS; 2000 … 2018 take what AutoCAD wrote there)
int acis_newest_sat_version(DxfVersion version);

// Why the first 3DSOLID, REGION or BODY (drawing or block) cannot be written as `target` (R13 or
// R14), a message naming it; empty when each can (or `target` is another version). A body of the
// SAT version the target holds, or one that is the same as in `original` (the drawing as it was
// read from a file of this version, matched by handle — its stream is kept as it is), is
// written. A newer SAT, a body with only SAB (ACDSDATA, 2013+), data that is no SAT and a
// body's change GUID (2013's property, XDATA only in AutoCAD's 2000 … 2010 files) are errors:
// converting SAT or SAB is not done (phase 8), and the SAT header's number is never changed to
// make a body fit.
std::string acis_target_error(const Drawing& dwg, DxfVersion target, const Drawing* original);

// ---------------------------------------------------------------------------
// The geometry of a save file
// ---------------------------------------------------------------------------

// A B-spline curve (bs3_curve) with its knot vector made whole (ACIS leaves out the
// first and the last knot)
struct AcisSplineCurve {
    int degree = 0;
    std::vector<double> knots;          // points.size() + degree + 1
    std::vector<Point3> points;
    std::vector<double> weights;        // empty: polynomial
    bool periodic = false;
};

// A B-spline surface (bs3_surface): points[v * count_u + u] — u varies fastest in the
// file
struct AcisSplineSurface {
    int degree_u = 0;
    int degree_v = 0;
    int count_u = 0;
    int count_v = 0;
    std::vector<double> knots_u;
    std::vector<double> knots_v;
    std::vector<Point3> points;
    std::vector<double> weights;        // empty: polynomial
    bool periodic_u = false;
    bool periodic_v = false;
};

enum class AcisSurfaceType { None, Plane, Cone, Sphere, Torus, Spline, Revolution, Sum };

struct AcisCurve;

// A surface. Plane: origin, axis (normal). Cone: the base ellipse (origin, axis = unit
// normal, major, ratio), the half angle's sine and cosine (cosine < 0: the normal points
// to the axis). Sphere: origin, radius (< 0: the normal points in), axis the pole, major
// the u direction. Torus: origin, axis, radius (major), minor (< 0: the normal points
// to the spine), major the u direction. Spline: its approximation (exact when
// fit_tolerance is 0), reversed when the normal is -Su × Sv; ruled when it is straight in
// one direction (an extrusion), then perhaps without an approximation.
// Revolution (rot_spl_sur): curves[0] turned about the axis through origin, S(u, v) =
// origin + R(v)(C(u) - origin), v the angle counterclockwise about axis. Sum (sum_spl_sur):
// S(u, v) = C1(u) + C2(v) - origin, curves[0] and curves[1]. Both by their own definition
// (exact), reversed as a spline; the range the parameters' (has_range).
struct AcisSurface {
    AcisSurfaceType type = AcisSurfaceType::None;
    Point3 origin;
    Point3 axis = {0.0, 0.0, 1.0};
    Point3 major = {1.0, 0.0, 0.0};
    double ratio = 1.0;
    double sine = 0.0;
    double cosine = 1.0;
    double radius = 0.0;
    double minor = 0.0;
    bool reversed = false;
    bool ruled = false;
    AcisSplineSurface spline;
    double fit_tolerance = 0.0;
    std::vector<AcisCurve> curves;      // Revolution: the profile; Sum: the two curves
    bool has_range = false;
    double u0 = 0.0, u1 = 0.0, v0 = 0.0, v1 = 0.0;
    // ACIS's parameters (acis_surface_point()): a plane's u direction, a cone's u scale (0: the
    // major axis' length), the sense of v (reverse_v) or of a cone's u (reversed)
    Point3 u_direction = {1.0, 0.0, 0.0};
    double u_scale = 0.0;
    bool reverse_v = false;
    bool reverse_u = false;
};

enum class AcisCurveType { None, Straight, Ellipse, Spline };

// A curve. Straight: origin, direction. Ellipse: origin (center), axis (unit normal),
// major, ratio. Spline (intcurve): its approximation, reversed when the curve runs
// against it; an intersection curve (fit_tolerance > 0) with its two surfaces.
struct AcisCurve {
    AcisCurveType type = AcisCurveType::None;
    Point3 origin;
    Point3 direction = {1.0, 0.0, 0.0};
    Point3 axis = {0.0, 0.0, 1.0};
    Point3 major = {1.0, 0.0, 0.0};
    double ratio = 1.0;
    bool reversed = false;
    AcisSplineCurve spline;
    double fit_tolerance = 0.0;
    bool exact = false;                 // exactcur: the spline is the curve
    AcisSurface surface1;
    AcisSurface surface2;
    bool bounded = false;               // a curve inside a surface: its range, when saved
    double t0 = 0.0, t1 = 0.0;
};

// An edge: its vertices' points (missing: none), its curve, its direction on it
struct AcisEdge {
    bool has_start = false;
    bool has_end = false;
    bool closed = false;                // the same vertex at both ends
    Point3 start;
    Point3 end;
    int curve = -1;                     // index into AcisBody::curves
    bool reversed = false;
};

// A coedge of a loop: its edge (index into AcisBody::edges), the direction it runs
struct AcisCoedge {
    int edge = -1;
    bool reversed = false;
};

struct AcisLoop {
    std::vector<AcisCoedge> coedges;
};

struct AcisFace {
    int surface = -1;                   // index into AcisBody::surfaces
    bool reversed = false;
    std::vector<AcisLoop> loops;
};

// A body: its faces, its edges (each once: those of the faces and of the wires), and the
// transform to the entity's coordinates (WCS = transform · (x, y, z, 1))
struct AcisBody {
    std::vector<AcisFace> faces;
    std::vector<AcisEdge> edges;
    std::vector<AcisCurve> curves;
    std::vector<AcisSurface> surfaces;
    double transform[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
};

// The bodies of a save file
std::vector<AcisBody> acis_bodies(const AcisFile& file);

// A record's curve or surface (a curve-, surface- or pcurve record: the curve its 3D one),
// and a vertex record's point — the SAB encoder's computed fields (plan phase 5, B63)
bool acis_record_curve(const AcisFile& file, size_t record, AcisCurve& c);
bool acis_record_surface(const AcisFile& file, size_t record, AcisSurface& s);
bool acis_vertex_point(const AcisFile& file, size_t vertex, Point3& p);

// A curve's point and derivatives at its parameter t, and the parameter of its point
// nearest to p (src/acis_extents.cpp): a straight's root + t · direction, an ellipse's
// angle from its major axis, an intcurve's spline parameter (negated when it is reversed)
void acis_curve_eval(const AcisCurve& c, double t, Point3& p, Point3* d1 = nullptr, Point3* d2 = nullptr);
double acis_curve_parameter(const AcisCurve& c, const Point3& p);

// A pcurve record (an exp_par_cur): its bs2 curve (the points (u, v, 0)), the surface, its sense
// and its offset in the surface's parameters — a tolerant coedge's 3D curve (plan phase 5, B63)
bool acis_pcurve(const AcisFile& file, size_t record, AcisSplineCurve& uv, AcisSurface& surface, bool& reversed,
                 double offset[2]);

// A spline's point (and first derivative) at t, and a surface's point at (u, v) in ACIS's
// parameters (src/acis_extents.cpp): a plane root + u·U + v·(N × U); a cone the base ellipse at
// the angle v moved u·scale along its line; a sphere and a torus by latitude u and longitude v;
// a spline surface its bs3 surface; a surface of revolution its profile at u turned by v; a sum
// C1(u) + C2(v) - origin; with its derivatives in u and v. False when the surface has no such
// parameters.
void acis_spline_point(const AcisSplineCurve& c, double t, Point3& p, Point3* d1 = nullptr);
bool acis_surface_point(const AcisSurface& s, double u, double v, Point3& p, Point3* du = nullptr, Point3* dv = nullptr);

// An affine map: WCS = m · (x, y, z, 1)
struct AcisMap {
    double m[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
};

// The exact box of the bodies mapped by `map` (src/acis_extents.cpp): edges exactly
// (lines, ellipses, splines — an intersection curve on its surfaces), and the faces'
// interior extremes where a surface reaches beyond its edges (sphere, torus, cone apex,
// spline surface with its knot lines, surface of revolution, sum of curves), each within
// the face (its loops in the surface's parameters; seams only, or no loops: all of it)
void acis_extents(const std::vector<AcisBody>& bodies, const AcisMap& map, Extents& out);

// The edges of the bodies as polylines in the entity's coordinates (each edge once; a
// MULTILEADER's block drawn in proxy graphics, B38): a straight edge its two ends, an
// elliptical one 64 segments a turn, a spline one 8 segments a knot span
std::vector<std::vector<Point3>> acis_edge_polylines(const std::vector<AcisBody>& bodies);

// The ACIS data of 3DSOLID, REGION, BODY and the surfaces: their SAT lines (R13–2010), or
// (2013+) their SAB (AcisData::sab, the ACDSDATA section's record, plan phase 5, B61)
class AcisSource {
public:
    explicit AcisSource(const Drawing& dwg) : dwg_(dwg) {}

    // The bodies of e; empty when it has no ACIS data that can be read
    std::vector<AcisBody> bodies(const Entity& e);

private:
    const Drawing& dwg_;
};

} // namespace internal
} // namespace hdxf
