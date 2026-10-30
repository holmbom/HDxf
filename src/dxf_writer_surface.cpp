#include "dxf_writer.hpp"
#include "extents.hpp"
#include "handles.hpp"
#include "surface_graphics.hpp"
#include "text_encoding.hpp"
#include <cmath>

// Surfaces and HELIX as AutoCAD writes them (plan phase 3, B39): AcDbModelerGeometry as
// 3DSOLID, AcDbSurface's isolines, the type's subclass in the order of AutoCAD's files
// (LibreDWG's Surface.dxf saved by AutoCAD as 2004 — all five types —, the stl-metrics
// project's 2007 test3D.dxf, jscad's 3Dlines.dxf) and Autodesk's DXF reference. A surface
// written anew has AutoCAD's 2007 proxy graphics, which are empty (92 8 and its 8 bytes);
// HELIX's are its spline as a polyline.

namespace hdxf {
namespace internal {

namespace {

const char* surface_name(EntityType type) {
    switch (type) {
        case EntityType::ExtrudedSurface: return "EXTRUDEDSURFACE";
        case EntityType::LoftedSurface:   return "LOFTEDSURFACE";
        case EntityType::RevolvedSurface: return "REVOLVEDSURFACE";
        case EntityType::SweptSurface:    return "SWEPTSURFACE";
        case EntityType::PlaneSurface:    return "PLANESURFACE";
        default:                          return "SURFACE";
    }
}

} // namespace

// A 16-number matrix, row by row; none as the identity
void DxfWriter::write_matrix(int code, const std::vector<double>& m) {
    for (int i = 0; i < 16; ++i) {
        double v = i < static_cast<int>(m.size()) ? m[i] : (i % 5 == 0 ? 1.0 : 0.0);
        groups_.write_double(code, v);
    }
}

// An entity's data: its class, its size in bits (90), the bytes in 310 groups of 127
void DxfWriter::write_surface_source(int class_code, const SurfaceSource& s) {
    groups_.write_int(class_code, s.class_id);
    groups_.write_int(90, s.bits);
    write_binary_chunks(s.data);
}

void DxfWriter::write_surface_r2000(const Entity& e, const std::string& owner) {
    const SurfaceData& d = e.as_surface();
    const EntityProperties& p = e.props;
    auto flag = [&](int code, bool v) { groups_.write_int(code, v ? 1 : 0); };
    // AutoCAD's 2007 proxy graphics of a surface: no commands. In 2000 and 2004 they are its isolines (plan 9.16,
    // surface_graphics.cpp): made from the body for the view the drawing is saved with. A surface that was read
    // and has not changed keeps the bytes it was read with (written from its record: both writes make the
    // same here); one whose isolines cannot be made fails, named, unless it is the same as it was read
    std::vector<uint8_t> proxy;
    if (version_ >= DxfVersion::R2007) {
        proxy = {8, 0, 0, 0, 0, 0, 0, 0};
    } else if (proxy_graphics_) {
        double view = 0.0;
        int circle_zoom = 1000;
        if (drawing_) {
            for (const auto& vp : drawing_->vports()) {
                if (vp.name == "*Active" || vp.name == "*ACTIVE") {
                    view = vp.view_height;
                    circle_zoom = vp.circle_zoom;
                }
            }
        }
        std::string why;
        if (!surface_isoline_graphics(e, view, proxy, why, circle_zoom)) {
            proxy.clear();
            bool unchanged = false;
            if (!filter_live_) {
                unchanged = true;       // the drawing as read: its record stays
            } else if (template_mode()) {
                uint64_t h = 0;
                if (parse_handle(p.handle, h) == HandleParse::Ok) {
                    auto it = orig_entities_.find(h);
                    if (it != orig_entities_.end() && it->second->type == e.type) {
                        const SurfaceData& was = it->second->as_surface();
                        unchanged = was.acis.lines == d.acis.lines && was.acis.sab == d.acis.sab &&
                                    was.u_isolines == d.u_isolines && was.v_isolines == d.v_isolines;
                        // the same body in another color: AutoCAD's stream as read with the new color in its 22s
                        const Color& before = it->second->props.color;
                        if (unchanged && (before.aci != p.color.aci || before.rgb != p.color.rgb || before.name != p.color.name)) {
                            proxy = surface_recolored_graphics(*it->second, p.color);
                        }
                    }
                }
            }
            if (!unchanged) {
                if (error_.empty()) error_ = std::string(surface_name(e.type)) + ": " + why + " (DXF 2000 and 2004 keep them as proxy graphics)";
                return;
            }
        }
    }
    const char* name = e.type == EntityType::NurbsSurface && version_ >= DxfVersion::R2013 ? "NURBSURFACE" : surface_name(e.type);
    write_entity_head(name, p.handle, owner, p, &proxy);
    write_modeler_geometry(d.acis);
    write_code(100, "AcDbSurface");
    groups_.write_int(71, d.u_isolines);
    groups_.write_int(72, d.v_isolines);
    auto sweep = [&](const SurfaceSweep& w) {
        groups_.write_double(42, w.draft_angle);
        groups_.write_double(43, w.draft_start);
        groups_.write_double(44, w.draft_end);
        groups_.write_double(45, w.twist);
        groups_.write_double(48, w.scale);
        groups_.write_double(49, w.align_angle);
        write_matrix(46, w.sweep_matrix);
        write_matrix(47, w.path_matrix);
        flag(290, w.solid);
        groups_.write_int(70, w.alignment);
        groups_.write_int(71, w.option_71);
        flag(292, w.align_start);
        flag(293, w.bank);
        flag(294, w.base_point_set);
        flag(295, w.sweep_transform_computed);
        flag(296, w.path_transform_computed);
        write_point(11, w.reference_vector);
    };
    switch (e.type) {
        case EntityType::ExtrudedSurface:
            write_code(100, "AcDbExtrudedSurface");
            write_surface_source(90, d.source);
            write_point(10, d.sweep_vector);
            write_matrix(40, d.matrix);
            sweep(d.sweep);
            break;
        case EntityType::LoftedSurface:
            write_code(100, "AcDbLoftedSurface");
            write_matrix(40, d.matrix);
            for (const auto& c : d.curves) write_surface_source(90, c);
            groups_.write_int(70, d.plane_normal_lofting);
            groups_.write_double(41, d.start_draft_angle);
            groups_.write_double(42, d.end_draft_angle);
            groups_.write_double(43, d.start_draft_magnitude);
            groups_.write_double(44, d.end_draft_magnitude);
            flag(290, d.arc_length_parameters);
            flag(291, d.no_twist);
            flag(292, d.align_direction);
            flag(293, d.simple_surfaces);
            flag(294, d.closed);
            flag(295, d.loft_solid);
            flag(296, d.ruled);
            flag(297, d.virtual_guide);
            break;
        case EntityType::RevolvedSurface:
            write_code(100, "AcDbRevolvedSurface");
            write_surface_source(90, d.source);
            write_point(10, d.axis_point);
            write_point(11, d.axis_vector);
            groups_.write_double(40, d.revolve_angle);
            groups_.write_double(41, d.start_angle);
            write_matrix(42, d.matrix);
            groups_.write_double(43, d.revolve_draft_angle);
            groups_.write_double(44, d.start_draft_distance);
            groups_.write_double(45, d.end_draft_distance);
            groups_.write_double(46, d.revolve_twist);
            flag(290, d.revolve_solid);
            flag(291, d.close_to_axis);
            break;
        case EntityType::SweptSurface:
            write_code(100, "AcDbSweptSurface");
            write_surface_source(90, d.source);
            write_surface_source(91, d.path);
            write_matrix(40, d.matrix);
            write_matrix(41, d.path_entity_matrix);
            sweep(d.sweep);
            break;
        case EntityType::NurbsSurface:
            // AutoCAD writes it as NURBSURFACE from 2013; before, as a SURFACE with AcDbSurface only (its files)
            if (version_ >= DxfVersion::R2013) {
                write_code(100, "AcDbNurbSurface");
                groups_.write_int(170, d.nurbs.flag);
                flag(290, d.nurbs.flag_290);
                write_point(10, d.nurbs.point_10);
                write_point(11, d.nurbs.point_11);
                write_point(12, d.nurbs.point_12);
                write_point(13, d.nurbs.point_13);
            }
            break;
        default:
            // PLANESURFACE and SURFACE: AcDbSurface only (LibreDWG's Surface.dxf)
            break;
    }
}

// HELIX's proxy graphics: its spline as one polyline (helix_points)
std::vector<uint8_t> DxfWriter::helix_proxy(const HelixData& d) {
    std::vector<Point3> points = helix_points(d);
    if (points.size() < 2) return {};
    ProxyGraphics g;
    g.polyline(points);
    return g.bytes();
}

void DxfWriter::write_helix_r2000(const Entity& e, const std::string& owner) {
    const HelixData& d = e.as_helix();
    const EntityProperties& p = e.props;
    std::vector<uint8_t> proxy = proxy_graphics_ ? helix_proxy(d) : std::vector<uint8_t>{};
    write_entity_head("HELIX", p.handle, owner, p, &proxy);
    write_spline_body(d.spline, p);
    write_code(100, "AcDbHelix");
    groups_.write_int(90, d.major_version);
    groups_.write_int(91, d.maintenance_version);
    write_point(10, d.axis_base);
    write_point(11, d.start);
    write_point(12, d.axis_vector);
    groups_.write_double(40, d.radius);
    groups_.write_double(41, d.turns);
    groups_.write_double(42, d.turn_height);
    groups_.write_int(290, d.right_handed ? 1 : 0);
    groups_.write_int(280, d.constrain);
}

// R13 and R14 (plan phase 7, B95) write the same: 70 (1 in AutoCAD's R14 REGION, as in 2000's) and the
// SAT lines as read or given — SAT 105/106 at most (acis_target_error(), checked before anything is
// written); AutoCAD's only saved form of a line of 255 characters or more is the 2000 one below, and
// AutoCAD's R14 file has no such line (its longest is 194 characters, ACIS 1.6 wraps records itself).
// Before 2013: 70 and the SAT lines as AutoCAD writes them: a line of 255 characters or more in 3s
// of 255 and the rest, perhaps empty, in the 1 that ends it (Aspose's conic_pyramid, endstop_ztop,
// kolja_oberteil: 3 255 + 1 26, 3 255 + 1 ""; the reference has a 1 and 3s after it, 3DSOLID
// p. 82 of the 2014 reference, BODY p. 95, REGION p. 149 — AutoCAD's core does not read a body
// so split, error 65010). 255 counts the characters as written, without the caret escapes (no
// AutoCAD file has a "^" in a 3; its core reads both forms, plan 5.6).
// 2013 (AutoCAD's 2013 files; in no reference, B58): 290 and the change GUID; the body's ACIS data
// is its SAB in the ACDSDATA section (AcisData::sab, B61, or the SAB made from its SAT lines, B62;
// dxf_writer_acds.cpp)
void DxfWriter::write_modeler_geometry(const AcisData& d) {
    write_code(100, "AcDbModelerGeometry");
    if (version_ >= DxfVersion::R2013) {
        groups_.write_int(290, d.guid.empty() ? 0 : 1);
        write_code(2, d.guid.empty() ? std::string("{00000000-0000-0000-0000-000000000000}") : d.guid);
        return;
    }
    groups_.write_int(70, d.version);
    for (const auto& line : d.lines) {
        std::vector<std::string> parts = split_encoded(line, text_code_page_, false, 255);
        std::string last;
        if (!encode_text(parts.back(), text_code_page_, last)) last = parts.back();
        if (last.size() >= 255) parts.emplace_back();
        for (size_t i = 0; i < parts.size(); ++i) write_code(i + 1 < parts.size() ? 3 : 1, parts[i]);
    }
}

} // namespace internal
} // namespace hdxf
