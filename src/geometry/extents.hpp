#pragma once

#include "hdxf/core.hpp"
#include "hdxf/fwd.hpp"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hdxf {
namespace internal {

// A bounding box in WCS. Empty: min 1e20 and max -1e20, the values AutoCAD writes for a
// drawing without extents (AcDbExtents' "invalid state").
struct Extents {
    Point3 min = {1e20, 1e20, 1e20};
    Point3 max = {-1e20, -1e20, -1e20};

    bool empty() const { return min.x > max.x; }
    void add(const Point3& p);
};

// How much work a calculation did: what the tests look at instead of a clock. Null where nobody asks.
struct ExtentsCounters {
    size_t bodies_read = 0;             // ACIS data read into bodies
    size_t block_boxes = 0;             // blocks whose own box was computed
    size_t insert_walks = 0;            // blocks walked for an INSERT that is turned
    size_t faces_in_box = 0;            // faces not measured: their surface lies in the box known
    size_t faces_measured = 0;          // faces measured exactly
    size_t deviations = 0;              // edge deviations computed in (u, v), the costly step of a face's loops
};

// How many whole-drawing calculations this thread has started (drawing_extents)
size_t drawing_extents_started();
// How many calculations of any kind this thread has started: one a drawing_extents or entity_extents, however much it walks
size_t extents_builders_started();

// $EXTMIN/$EXTMAX (model space) and $PEXTMIN/$PEXTMAX (paper space) as AutoCAD keeps
// them: model space entities, an INSERT as its block's extents transformed by the
// insertion (AcDbBlockReference), a DIMENSION by its block, arcs and circles exact in
// their OCS. Paper space: the entities with paper_space set, without the viewport with
// id 1 (the paper space view itself). An entity with group 60 set is left out, with all it inserts; so is an ATTRIB or a
// constant ATTDEF with the invisible flag of group 70. 3DSOLID, REGION and BODY by their B-rep, exactly
// (acis.hpp). Text by its box in its style's font (text_layout);
// the fonts that are not found are in `warnings`. `generated` are blocks the writer made
// (dimension blocks), named for their DIMENSION in `dim_block_names`. `replaced`: blocks of the drawing the writer writes with
// other entities (the dimension blocks it regenerates, 9.S): by their name, these are counted instead of the drawing's.
void drawing_extents(const Drawing& dwg, const std::vector<Block>& generated,
                     const std::unordered_map<const Entity*, std::string>& dim_block_names,
                     Extents& model, Extents& paper, std::vector<std::string>& warnings,
                     const std::vector<Block>* replaced = nullptr, ExtentsCounters* counters = nullptr);

// One entity's extents, as it stands in model or paper space
void entity_extents(const Drawing& dwg, const Entity& e, Extents& out, ExtentsCounters* counters = nullptr);

// The curve c + u cos a + v sin a (WCS) from a0 to a1 radians counterclockwise, or all of
// it: its ends and its extremes, exactly
void ellipse_extents(Extents& out, const Point3& c, const Point3& u, const Point3& v, double a0,
                     double a1, bool full);

// A NURBS curve from parameter lo to hi, mapped by an affine map: exactly (rättningsfas
// R.6). The parameters of the points it took (the ends of each span, and where a
// coordinate has an extreme) are added to params.
void nurbs_extents(Extents& out, const std::function<Point3(const Point3&)>& map, int degree,
                   const std::vector<double>& knots, const std::vector<Point3>& points,
                   const std::vector<double>& weights, double lo, double hi,
                   std::vector<double>* params = nullptr);

// A point of a NURBS curve at parameter u (de Boor's algorithm)
Point3 nurbs_point(const SplineData& s, double u);

// A SPLINE's curve: its control points, knots and weights, or — with fit points only — the
// cubic AutoCAD makes through them (as in the extents)
SplineData spline_curve(const SplineData& d);

// HELIX as AutoCAD draws it — its proxy graphics and its extents: its spline at equal steps
// of the parameter, 64 points a turn (LibreDWG's Helix.dxf: 192 points for 3 turns, each on
// the spline, and AutoCAD's $EXTMIN/$EXTMAX theirs exactly; the number for other turns is a
// checkpoint)
std::vector<Point3> helix_points(const HelixData& d);

// IMAGE as AutoCAD draws it — its proxy graphics (2000 … 2010) and in a block AutoCAD's proxy
// graphics draws: its clip boundary closed, in the WCS (a pixel's corner at -0.5, y from the
// top); not clipped, its frame. A rectangle from (a.x, b.y): ACadSharp's sample_AC1015 … AC1024,
// a polygon in its order (LibreDWG's Leader_2010 and Leader_2018)
std::vector<Point3> image_outline(const ImageData& d);

// The real roots in (lo, hi) of the polynomial a[0] + a[1] t + a[2] t² …, each found to the
// last bit where it changes sign (R.9)
void polynomial_roots(const std::vector<double>& a, double lo, double hi, std::vector<double>& out);

// Bernstein coefficients q0 … qp on [0, 1] → the same polynomial's power coefficients
std::vector<double> bernstein_power(const std::vector<double>& q);

} // namespace internal
} // namespace hdxf
