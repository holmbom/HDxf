#include "sat_writer.hpp"

#include "geometry.hpp"

#include <cmath>

namespace hdxf::acis::detail {

// The fields of an analytic surface after the record's "p t n", as AutoCAD's primitives have them
// (the facit's plane-, cone-, sphere- and torus-surface):
//   plane:  root point, normal, u direction, forward_v, I I I I
//   cone:   center of the reference ellipse, its normal, its major axis, ratio, I I, sine and cosine of
//           the half angle, the u scale (the major axis' length), forward, I I I I
//   sphere: center, radius, u direction, pole, forward_v, I I I I
//   torus:  center, axis, major radius, minor radius, u direction, forward_v, I I I I
// The two intervals after the cone's ratio and the u scale are what AutoCAD writes for its
// cylinders and cones; they are not given other values here.
void write_analytic_surface_fields(RecordWriter& out, const SurfaceGeometry& surface) {
    if (const auto* plane = std::get_if<PlaneGeometry>(&surface)) {
        out.triple(plane->origin).triple(plane->normal).triple(plane->u_direction);
        out.word("forward_v").infinite_interval(4);
    } else if (const auto* cone = std::get_if<ConeGeometry>(&surface)) {
        double cosine = 1.0 / std::sqrt(1.0 + cone->radius_gradient * cone->radius_gradient);
        double sine = cone->radius_gradient * cosine;
        out.triple(cone->center).triple(cone->axis).triple(cone->major_axis);
        out.real(1.0).infinite_interval(2);
        out.real(sine).real(cosine).real(length(cone->major_axis));
        out.word("forward").infinite_interval(4);
    } else if (const auto* sphere = std::get_if<SphereGeometry>(&surface)) {
        out.triple(sphere->center).real(sphere->radius).triple(sphere->u_direction).triple(sphere->pole);
        out.word("forward_v").infinite_interval(4);
    } else if (const auto* torus = std::get_if<TorusGeometry>(&surface)) {
        out.triple(torus->center).triple(torus->axis).real(torus->major_radius).real(torus->minor_radius);
        out.triple(torus->u_direction);
        out.word("forward_v").infinite_interval(4);
    }
}

namespace {

const char* surface_type(const SurfaceGeometry& surface) {
    if (std::holds_alternative<PlaneGeometry>(surface)) return "plane-surface";
    if (std::holds_alternative<ConeGeometry>(surface)) return "cone-surface";
    if (std::holds_alternative<SphereGeometry>(surface)) return "sphere-surface";
    if (std::holds_alternative<TorusGeometry>(surface)) return "torus-surface";
    return "spline-surface";
}

// straight-curve: root point, direction, I I.  ellipse-curve: center, normal, major axis, ratio, I I.
void write_analytic_curve_record(const CurveGeometry& curve, std::vector<Record>& records) {
    if (const auto* line = std::get_if<LineGeometry>(&curve)) {
        RecordWriter out = entity_record("straight-curve");
        out.triple(line->origin).triple(line->direction).infinite_interval(2);
        records.push_back(out.finish());
    } else if (const auto* ellipse = std::get_if<EllipseGeometry>(&curve)) {
        RecordWriter out = entity_record("ellipse-curve");
        out.triple(ellipse->center).triple(ellipse->normal).triple(ellipse->major_axis).real(ellipse->ratio);
        out.infinite_interval(2);
        records.push_back(out.finish());
    }
}

} // namespace

// The surfaces, the curves, then the pcurves, in the order the plan numbered them
void write_geometry_records(SatPlan& plan, std::vector<Record>& records) {
    const BuilderData& d = *plan.data;
    for (std::uint32_t si : plan.surfaces) {
        const SurfaceGeometry& surface = d.surfaces[si].geometry;
        if (std::holds_alternative<BSplineSurfaceGeometry>(surface)) {
            write_spline_surface_record(plan, si, records);
            continue;
        }
        RecordWriter out = entity_record(surface_type(surface));
        write_analytic_surface_fields(out, surface);
        records.push_back(out.finish());
    }
    for (std::uint32_t ci : plan.curves) {
        const CurveGeometry& curve = d.curves[ci].geometry;
        if (std::holds_alternative<BSplineCurveGeometry>(curve)) write_intcurve_record(plan, ci, records);
        else write_analytic_curve_record(curve, records);
    }
    for (std::uint32_t coedge : plan.pcurves) write_pcurve_record(plan, coedge, records);
}

} // namespace hdxf::acis::detail
