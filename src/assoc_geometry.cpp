#include "AssocTypes.hpp"
#include <algorithm>

// The curve of an ACDBASSOCEDGEACTIONPARAM (phase 9, step 9.10; plan 2026-10-27-fas-9-typer.md, appendix B.11): the
// edge a surface action took its profile or path from, as AutoCAD stores the AcGe curve. The groups are AutoCAD's own
// (doc/facit/acad_*_dxf_associative.dxf and _surfaces.dxf in all versions, the corpus' Surface_2004, 3Dlines):
//
//   90 kind, then
//   23 (a line)             10 20 30 start, 10 20 30 the vector to its end
//   11 (a circle arc)       10 20 30 center, normal, reference vector, 40 radius, 40 start angle, 40 end angle (radians)
//                           and, in the 2013 form (an action parameter of version 1), one more 40 (0.0 in every file)
//   17 (an ellipse arc)     10 20 30 center, major axis, minor axis, 40 major radius, 40 minor radius, 40 start angle,
//                           40 end angle (radians)
//   42 (a spline)           70 70 (0 0), 90 degree, 40 knot tolerance (1e-9), then three AcArrays: the knots (90 count,
//                           90 physical length, 90 growth length, 40 each), the weights (the same; none: not rational)
//                           and the control points (10 20 30 each)
//   47 (a composite curve)  90 count, then every curve with its own 90 kind
//
// An angle is in radians here, as the files have it (not the 50/51 degrees of DXF's own entities). A curve kind
// that no file has makes the record stay as it was read.

namespace hdxf {

AssocCurveKind AssocCurve::kind() const {
    if (std::holds_alternative<AssocLineCurve>(shape)) return AssocCurveKind::Line;
    if (std::holds_alternative<AssocCircleArcCurve>(shape)) return AssocCurveKind::CircleArc;
    if (std::holds_alternative<AssocEllipseArcCurve>(shape)) return AssocCurveKind::EllipseArc;
    if (std::holds_alternative<AssocSplineCurve>(shape)) return AssocCurveKind::Spline;
    return AssocCurveKind::Composite;
}

namespace internal {

namespace {

constexpr int kMaxCompositeDepth = 16;

bool read_curve_at(GroupCursor& c, bool trailing, int depth, AssocCurve& out);

bool read_doubles(GroupCursor& c, int n, double* out) {
    for (int i = 0; i < n; ++i) {
        if (!c.require_real(40, out[i])) return false;
    }
    return true;
}

bool read_array_header(GroupCursor& c, size_t min_groups_per_item, size_t& n, AssocArrayLengths& lengths) {
    if (!c.count(90, min_groups_per_item, n)) {
        if (c.ok()) c.fail("the length (90) of an array of a spline is missing");
        return false;
    }
    c.require_integer(90, lengths.physical);
    c.require_integer(90, lengths.growth);
    return c.ok();
}

bool read_spline(GroupCursor& c, AssocSplineCurve& s) {
    c.require_integer(70, s.first_flag);
    c.require_integer(70, s.second_flag);
    c.require_integer(90, s.degree);
    c.require_real(40, s.knot_tolerance);
    size_t n = 0;
    if (!c.ok() || !read_array_header(c, 1, n, s.knots_lengths)) return false;
    s.knots.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (!c.require_real(40, s.knots[i])) return false;
    }
    if (!read_array_header(c, 1, n, s.weights_lengths)) return false;
    s.weights.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (!c.require_real(40, s.weights[i])) return false;
    }
    if (!read_array_header(c, 3, n, s.control_points_lengths)) return false;
    s.control_points.resize(n);
    for (size_t i = 0; i < n; ++i) {
        if (!c.require_point3(10, s.control_points[i])) return false;
    }
    return c.ok();
}

bool read_curve_at(GroupCursor& c, bool trailing, int depth, AssocCurve& out) {
    int32_t kind = 0;
    if (!c.require_integer(90, kind)) return false;
    switch (kind) {
        case 23: {
            AssocLineCurve line;
            c.require_point3(10, line.start);
            c.require_point3(10, line.offset);
            out.shape = line;
            break;
        }
        case 11: {
            AssocCircleArcCurve arc;
            c.require_point3(10, arc.center);
            c.require_point3(10, arc.normal);
            c.require_point3(10, arc.reference);
            double v[3] = {0.0, 0.0, 0.0};
            if (read_doubles(c, 3, v)) {
                arc.radius = v[0];
                arc.start_angle = v[1];
                arc.end_angle = v[2];
                if (trailing) {
                    double extra = 0.0;
                    if (c.require_real(40, extra)) arc.trailing = extra;
                }
            }
            out.shape = arc;
            break;
        }
        case 17: {
            AssocEllipseArcCurve ellipse;
            c.require_point3(10, ellipse.center);
            c.require_point3(10, ellipse.major_axis);
            c.require_point3(10, ellipse.minor_axis);
            double v[4] = {0.0, 0.0, 0.0, 0.0};
            if (read_doubles(c, 4, v)) {
                ellipse.major_radius = v[0];
                ellipse.minor_radius = v[1];
                ellipse.start_angle = v[2];
                ellipse.end_angle = v[3];
            }
            out.shape = ellipse;
            break;
        }
        case 42: {
            AssocSplineCurve spline;
            read_spline(c, spline);
            out.shape = std::move(spline);
            break;
        }
        case 47: {
            if (depth >= kMaxCompositeDepth) {
                c.fail("composite curves are nested more than " + std::to_string(kMaxCompositeDepth) + " deep");
                return false;
            }
            size_t n = 0;
            if (!c.count(90, 2, n)) {
                if (c.ok()) c.fail("the number of curves (90) of a composite curve is missing");
                return false;
            }
            AssocCompositeCurve composite;
            composite.curves.resize(n);
            for (size_t i = 0; i < n && c.ok(); ++i) {
                if (!read_curve_at(c, trailing, depth + 1, composite.curves[i])) return false;
            }
            out.shape = std::move(composite);
            break;
        }
        default:
            c.fail("the curve kind " + std::to_string(kind) + " of an edge parameter is not established (AutoCAD's files have 11, 17, 23, 42 and 47)");
            return false;
    }
    return c.ok();
}

void write_array_header(RecordWriter& w, size_t count, const AssocArrayLengths& lengths) {
    w.integer(90, static_cast<int64_t>(count));
    w.integer(90, std::max<int64_t>(lengths.physical, static_cast<int64_t>(count)));
    w.integer(90, lengths.growth);
}

} // namespace

bool read_assoc_curve(GroupCursor& c, bool trailing_circle_value, AssocCurve& out) {
    return read_curve_at(c, trailing_circle_value, 0, out);
}

void write_assoc_curve(RecordWriter& w, const AssocCurve& v) {
    w.integer(90, static_cast<int64_t>(v.kind()));
    if (const auto* line = std::get_if<AssocLineCurve>(&v.shape)) {
        w.point3(10, line->start);
        w.point3(10, line->offset);
    } else if (const auto* arc = std::get_if<AssocCircleArcCurve>(&v.shape)) {
        w.point3(10, arc->center);
        w.point3(10, arc->normal);
        w.point3(10, arc->reference);
        w.real(40, arc->radius);
        w.real(40, arc->start_angle);
        w.real(40, arc->end_angle);
        if (arc->trailing) w.real(40, *arc->trailing);
    } else if (const auto* ellipse = std::get_if<AssocEllipseArcCurve>(&v.shape)) {
        w.point3(10, ellipse->center);
        w.point3(10, ellipse->major_axis);
        w.point3(10, ellipse->minor_axis);
        w.real(40, ellipse->major_radius);
        w.real(40, ellipse->minor_radius);
        w.real(40, ellipse->start_angle);
        w.real(40, ellipse->end_angle);
    } else if (const auto* spline = std::get_if<AssocSplineCurve>(&v.shape)) {
        w.integer(70, spline->first_flag);
        w.integer(70, spline->second_flag);
        w.integer(90, spline->degree);
        w.real(40, spline->knot_tolerance);
        write_array_header(w, spline->knots.size(), spline->knots_lengths);
        for (double k : spline->knots) w.real(40, k);
        write_array_header(w, spline->weights.size(), spline->weights_lengths);
        for (double k : spline->weights) w.real(40, k);
        write_array_header(w, spline->control_points.size(), spline->control_points_lengths);
        for (const Point3& p : spline->control_points) w.point3(10, p);
    } else if (const auto* composite = std::get_if<AssocCompositeCurve>(&v.shape)) {
        w.integer(90, static_cast<int64_t>(composite->curves.size()));
        for (const AssocCurve& sub : composite->curves) write_assoc_curve(w, sub);
    }
}

} // namespace internal
} // namespace hdxf
