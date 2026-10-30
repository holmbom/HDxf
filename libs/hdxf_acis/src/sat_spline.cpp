#include "sat_writer.hpp"

#include "geometry.hpp"

namespace hdxf::acis::detail {

// B-splines and pcurves in the SAT, with the fields AutoCAD 2025 writes for them (the facit's
// 2010 loft: exact_int_cur, a pcurve with its spline surface referred to as "{ ref n }"; the 2007
// loft: exact_spl_sur, whose 21500 form has the logical "F" the field list gives it).
//
//   bs3 curve:   nubs | nurbs, degree, closure, the number of distinct knots, the knots as (value,
//                multiplicity) without the first and the last of the whole vector, the control
//                points (x y z, and the weight for nurbs)
//   bs2 curve:   the same with (u v) points
//   bs3 surface: nubs | nurbs, the two degrees, (nurbs: "both"), the closures, the singularities,
//                the numbers of knots, the u knots, the v knots, the control points with u fastest
//   discontinuities: three lists (derivative orders 1, 2, 3), each a count and the knots
//   exact_int_cur:  22800 full <bs3 curve> 0 null_surface null_surface nullbs nullbs I I <discontinuities>
//                   0 F 1 F 0 UNEXTENDED UNEXTENDED
//   exact_spl_sur:  22800 full <bs3 surface> 0 <u discontinuities> <v discontinuities> F
//                   F 1 F 0 F 1 F 0 UNEXTENDED
//   pcurve:         0 forward { exp_par_cur <bs2 curve> 0 <the surface> } 0 0

namespace {

const char* closure_word(Closure closure) {
    switch (closure) {
        case Closure::Open: return "open";
        case Closure::Closed: return "closed";
        case Closure::Periodic: return "periodic";
    }
    return "open";
}

const char* singularity_word(Singularity singularity) {
    switch (singularity) {
        case Singularity::None: return "none";
        case Singularity::Low: return "low";
        case Singularity::High: return "high";
        case Singularity::Both: return "both";
    }
    return "none";
}

void write_knots(RecordWriter& out, const std::vector<double>& knots) {
    std::vector<KnotRun> runs = knot_runs(knots);
    out.integer(static_cast<long long>(runs.size()));
    for (const KnotRun& run : runs) out.real(run.value).integer(run.multiplicity);
}

void write_bs3_curve(RecordWriter& out, const BSplineCurveGeometry& curve) {
    bool rational = !curve.weights.empty();
    out.word(rational ? "nurbs" : "nubs").integer(curve.degree).word(closure_word(curve.closure));
    write_knots(out, curve.knots);
    for (size_t i = 0; i < curve.points.size(); ++i) {
        out.triple(curve.points[i]);
        if (rational) out.real(curve.weights[i]);
    }
}

void write_bs2_curve(RecordWriter& out, const BSplineCurve2D& curve) {
    bool rational = !curve.weights.empty();
    out.word(rational ? "nurbs" : "nubs").integer(curve.degree).word(closure_word(curve.closure));
    write_knots(out, curve.knots);
    for (size_t i = 0; i < curve.points.size(); ++i) {
        out.real(curve.points[i].x).real(curve.points[i].y);
        if (rational) out.real(curve.weights[i]);
    }
}

void write_bs3_surface(RecordWriter& out, const BSplineSurfaceGeometry& surface) {
    bool rational = !surface.weights.empty();
    out.word(rational ? "nurbs" : "nubs").integer(surface.degree_u).integer(surface.degree_v);
    if (rational) out.word("both");
    out.word(closure_word(surface.closure_u)).word(closure_word(surface.closure_v));
    out.word(singularity_word(surface.singular_u)).word(singularity_word(surface.singular_v));
    out.integer(static_cast<long long>(knot_runs(surface.knots_u).size()));
    out.integer(static_cast<long long>(knot_runs(surface.knots_v).size()));
    // The counts are written with the knots, so the two knot lists follow without them
    for (const std::vector<double>* knots : {&surface.knots_u, &surface.knots_v}) {
        for (const KnotRun& run : knot_runs(*knots)) out.real(run.value).integer(run.multiplicity);
    }
    for (size_t i = 0; i < surface.points.size(); ++i) {
        out.triple(surface.points[i]);
        if (rational) out.real(surface.weights[i]);
    }
}

void write_discontinuities(RecordWriter& out, int degree, const std::vector<double>& knots, int count, bool periodic) {
    std::vector<double> lists[3];
    discontinuity_lists(degree, knots, count, periodic, lists);
    for (const std::vector<double>& list : lists) {
        out.integer(static_cast<long long>(list.size()));
        for (double value : list) out.real(value);
    }
}

// "F 1 F 0": the finite interval [1, 0] AutoCAD writes for the safe range of its subtypes
void write_safe_range(RecordWriter& out) {
    out.word("F").integer(1).word("F").integer(0);
}

} // namespace

void write_spline_surface_record(SatPlan& plan, std::uint32_t surface_index, std::vector<Record>& records) {
    const auto& surface = std::get<BSplineSurfaceGeometry>(plan.data->surfaces[surface_index].geometry);
    plan.surface_subtype[surface_index] = plan.subtype_count++;
    RecordWriter out = entity_record("spline-surface");
    out.word("forward").open().word("exact_spl_sur").integer(kSplineDataVersion).word("full");
    write_bs3_surface(out, surface);
    out.real(0.0);
    write_discontinuities(out, surface.degree_u, surface.knots_u, surface.count_u, surface.closure_u == Closure::Periodic);
    write_discontinuities(out, surface.degree_v, surface.knots_v, surface.count_v, surface.closure_v == Closure::Periodic);
    out.word("F");
    write_safe_range(out);
    write_safe_range(out);
    out.word("UNEXTENDED");
    out.close().infinite_interval(4);
    records.push_back(out.finish());
}

void write_intcurve_record(SatPlan& plan, std::uint32_t curve_index, std::vector<Record>& records) {
    const auto& curve = std::get<BSplineCurveGeometry>(plan.data->curves[curve_index].geometry);
    ++plan.subtype_count;
    RecordWriter out = entity_record("intcurve-curve");
    out.word("forward").open().word("exact_int_cur").integer(kSplineDataVersion).word("full");
    write_bs3_curve(out, curve);
    out.real(0.0);
    out.word("null_surface").word("null_surface").word("nullbs").word("nullbs").infinite_interval(2);
    write_discontinuities(out, curve.degree, curve.knots, static_cast<int>(curve.points.size()),
                          curve.closure == Closure::Periodic);
    out.integer(0);
    write_safe_range(out);
    out.word("UNEXTENDED").word("UNEXTENDED");
    out.close().infinite_interval(2);
    records.push_back(out.finish());
}

// The same curve on the negated parameter: the knots turned about zero, the points and weights in the
// opposite order
static BSplineCurve2D negated_parameter(const BSplineCurve2D& curve) {
    BSplineCurve2D out;
    out.degree = curve.degree;
    out.closure = curve.closure;
    out.knots.assign(curve.knots.rbegin(), curve.knots.rend());
    for (double& knot : out.knots) knot = knot == 0.0 ? 0.0 : -knot;
    out.points.assign(curve.points.rbegin(), curve.points.rend());
    out.weights.assign(curve.weights.rbegin(), curve.weights.rend());
    return out;
}

// A pcurve runs in the direction of its coedge. The builder's pcurve is in the edge's curve parameter t;
// on a coedge that is written reversed the SAT's pcurve is on -t, as AutoCAD's loft has it (the reversed
// coedges' pcurves of the facit, the seam's from -1 to 0).
void write_pcurve_record(SatPlan& plan, std::uint32_t coedge_index, std::vector<Record>& records) {
    const BuilderData& d = *plan.data;
    const detail::CoedgeRecord& coedge = d.coedges[coedge_index];
    const detail::EdgeRecord& edge = d.edges[coedge.edge.index];
    bool written_reversed = edge.curve.valid() ? coedge.reversed != edge.against_curve : true;
    BSplineCurve2D pcurve = d.pcurves[coedge.pcurve.index].geometry;
    if (written_reversed) pcurve = negated_parameter(pcurve);
    std::uint32_t surface_index = d.faces[d.loops[coedge.loop.index].face.index].surface.index;
    const SurfaceGeometry& surface = d.surfaces[surface_index].geometry;

    RecordWriter out = entity_record("pcurve");
    out.integer(0).word("forward").open().word("exp_par_cur");
    write_bs2_curve(out, pcurve);
    out.real(0.0);
    if (std::holds_alternative<BSplineSurfaceGeometry>(surface)) {
        out.word("spline").word("forward").open().word("ref").integer(plan.surface_subtype[surface_index]).close();
        out.infinite_interval(4);
    } else if (std::holds_alternative<PlaneGeometry>(surface)) {
        out.word("plane");
        write_analytic_surface_fields(out, surface);
    } else if (std::holds_alternative<ConeGeometry>(surface)) {
        out.word("cone");
        write_analytic_surface_fields(out, surface);
    } else if (std::holds_alternative<SphereGeometry>(surface)) {
        out.word("sphere");
        write_analytic_surface_fields(out, surface);
    } else {
        out.word("torus");
        write_analytic_surface_fields(out, surface);
    }
    out.close().real(0.0).real(0.0);
    records.push_back(out.finish());
}

} // namespace hdxf::acis::detail
