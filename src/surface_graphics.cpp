#include "acis_isolines.hpp"
#include "surface_graphics.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>

// The proxy graphics of the surfaces in DXF 2000 and 2004 (plan phase 9, step 9.16): AutoCAD's isolines, from the
// surface's body (acis_isolines.cpp). Which bodies are drawn is what AutoCAD's own files agree with
// (doc/facit/acad_2000_dxf_surfaces*.dxf, acad_2004_dxf_surfaces*.dxf: fas9_surfaces.scr, fas9_surfaces_view.scr,
// fas9_surfaces_iso.scr, fas9_surfaces_iso2.scr; the notes in doc/facit/fas9_notes/F9.18.md): planes, cones and
// cylinders (also mirrored), spheres and tori with any loops (holes, trims, seams, slits, poles, caps), surfaces of
// revolution and sums of curves, extrusions of splines, NURBS surfaces and lofts of two sections. The rest — a loft
// straight between more than two sections (AutoCAD draws through the sections, which the body does not give), one
// curved between them (AutoCAD draws its own loft, not always the body), a loft of closed sections, a sweep, a
// generic SURFACE (AutoCAD's offset or network cannot be told from a NURBS), a sphere cut off its pole axis — is named.

namespace hdxf {
namespace internal {

namespace {

bool body_of(const Entity& e, AcisFile& file, std::string& why) {
    const AcisData& d = e.as_surface().acis;
    if (!d.lines.empty()) {
        if (!read_sat(acis_deciphered(d), file)) {
            why = "its ACIS data is not SAT that can be read";
            return false;
        }
        return true;
    }
    if (!d.sab.empty() && read_sab(d.sab, file)) return true;
    why = "it has no ACIS data";
    return false;
}

int32_t raw_color(const Color& c) {
    if (c.rgb >= 0) return static_cast<int32_t>(0xC2000000u | static_cast<uint32_t>(c.rgb & 0xFFFFFF));
    if (c.aci == 0) return static_cast<int32_t>(0xC1000000u);
    if (c.aci >= 1 && c.aci <= 255) return static_cast<int32_t>(0xC3000000u | static_cast<uint32_t>(c.aci));
    return static_cast<int32_t>(0xC0000000u);
}

} // namespace

std::string surface_isoline_error(const Entity& e) {
    AcisFile file;
    std::string why;
    if (!body_of(e, file, why)) return why;
    for (const AcisBody& b : acis_bodies(file)) {
        // a body turned inside out by a mirror (MIRROR of a surface): AutoCAD's curves run the other way (acad_*_surfaces_transform_mirrored)
        const double (*m)[4] = b.transform;
        double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                     m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
        if (det < 0.0) return "its isolines for a body with a reflecting transform are not made";
        for (const AcisSurface& s : b.surfaces) {
            switch (s.type) {
                case AcisSurfaceType::Plane:
                case AcisSurfaceType::Cone:
                    // (a cone with its sense of u turned, MIRROR of an extruded circle: AutoCAD's u is ACIS's own)
                    break;
                case AcisSurfaceType::Sphere:
                case AcisSurfaceType::Torus:
                    break;
                case AcisSurfaceType::Spline: {
                    const SurfaceData& d = e.as_surface();
                    // an extrusion of a spline: straight along v (ACIS's cylsur, approximated linearly in v)
                    if (s.ruled || (s.spline.degree_v == 1 && s.spline.count_v == 2)) break;
                    // a NURBS surface: AutoCAD draws the surface its body has (CONVTONURBS and NURBS mode,
                    // acad_*_surfaces_nurbs_converted, _nurbs_made, _iso)
                    if (e.type == EntityType::NurbsSurface) break;
                    // (a loft of closed or rational sections, circles: AutoCAD draws its own surface, which the body
                    // approximates, acad_*_surfaces_spline 771)
                    bool rational = false;
                    for (double w : s.spline.weights) rational = rational || w != s.spline.weights.front();
                    if (e.type == EntityType::LoftedSurface && !d.closed && !s.spline.periodic_u && !s.spline.periodic_v && !rational) {
                        // two sections: ruled between them, drawn from its body (acad_*_surfaces_spline 768, _iso 26D)
                        if (d.curves.size() == 2) break;
                        // straight between more sections: AutoCAD draws its curves along v through the sections (where
                        // they are, acad_*_surfaces_iso 25D, 26A), which neither the body nor the record gives
                        if (acis_straight_in_v(s)) return "its isolines along a loft straight between more than two sections are not made";
                        // curved between them: AutoCAD draws its own loft, which the body is in the core's files
                        // (_iso 263, _iso2 25D, 275) but not in Erik's (acad_*_surfaces 27C, 291: the side edge 0.01 off it)
                        return "its isolines along a loft of more than two sections are not made";
                    }
                    // a closed loft (tolerant edges), a sweep, a SURFACE that may be AutoCAD's own procedure (an
                    // offset, a network: acad_*_surfaces_iso2 261, 270, which the record does not tell from a NURBS)
                    return "its isolines along a spline surface of this kind are not made";
                }
                case AcisSurfaceType::Sum:
                    // a sum of two curves (AutoCAD 2000's SAT of an extrusion)
                case AcisSurfaceType::Revolution:
                    // a curve revolved (a line, an arc, a spline): the generator names a curve it does not draw
                    break;
                default:
                    return "its isolines along a surface of this kind are not made";
            }
        }
    }
    // A sphere cut by an edge and trimmed by another loop (SURFTRIM of a REVOLVE'd sphere) is not what AutoCAD's file shows
    // the same stream for: named, not drawn (F9.18 ej testat)
    for (const AcisBody& b : acis_bodies(file)) {
        for (const AcisFace& f : b.faces) {
            // a sphere of more than one loop is drawn when its edges are circles of latitude (caps and a seam on the
            // equator: SURFTRIM of a REVOLVE'd sphere, acad_*_surfaces_trim_trimmed BCF); other trims are not made
            if (f.surface >= 0 && b.surfaces[static_cast<size_t>(f.surface)].type == AcisSurfaceType::Sphere && f.loops.size() > 1) {
                const AcisSurface& sp = b.surfaces[static_cast<size_t>(f.surface)];
                for (const AcisLoop& loop : f.loops) {
                    for (const AcisCoedge& ce : loop.coedges) {
                        const AcisEdge& edge = b.edges[static_cast<size_t>(ce.edge)];
                        const AcisCurve* c = edge.curve >= 0 ? &b.curves[static_cast<size_t>(edge.curve)] : nullptr;
                        bool latitude = c && c->type == AcisCurveType::Ellipse && std::fabs(c->ratio - 1.0) < 1e-9;
                        if (latitude) {
                            const Point3& a = c->axis;
                            const Point3& p = sp.axis;
                            Point3 q{c->origin.x - sp.origin.x, c->origin.y - sp.origin.y, c->origin.z - sp.origin.z};
                            double s1 = std::sqrt(std::pow(a.y * p.z - a.z * p.y, 2) + std::pow(a.z * p.x - a.x * p.z, 2) +
                                                  std::pow(a.x * p.y - a.y * p.x, 2));
                            double s2 = std::sqrt(std::pow(q.y * p.z - q.z * p.y, 2) + std::pow(q.z * p.x - q.x * p.z, 2) +
                                                  std::pow(q.x * p.y - q.y * p.x, 2));
                            latitude = s1 < 1e-9 && s2 < 1e-9 * (1.0 + std::fabs(sp.radius));
                        }
                        if (!latitude) return "its isolines along a sphere trimmed by other curves than circles of latitude are not made";
                    }
                }
            }
            // a lune whose two edges do not meet on the sphere's own pole axis: AutoCAD draws it about the axis it was
            // revolved on, which the SAT does not give (45 degree lune of REVOLVE, F9.18 ej testat)
            if (f.surface >= 0 && b.surfaces[static_cast<size_t>(f.surface)].type == AcisSurfaceType::Sphere && f.loops.size() == 1 &&
                f.loops[0].coedges.size() == 2 && f.loops[0].coedges[0].edge != f.loops[0].coedges[1].edge) {
                const AcisSurface& sp = b.surfaces[static_cast<size_t>(f.surface)];
                for (const AcisCoedge& ce : f.loops[0].coedges) {
                    const AcisEdge& edge = b.edges[static_cast<size_t>(ce.edge)];
                    for (const Point3* p : {&edge.start, &edge.end}) {
                        Point3 q{p->x - sp.origin.x, p->y - sp.origin.y, p->z - sp.origin.z};
                        Point3 c{q.y * sp.axis.z - q.z * sp.axis.y, q.z * sp.axis.x - q.x * sp.axis.z, q.x * sp.axis.y - q.y * sp.axis.x};
                        if (std::sqrt(c.x * c.x + c.y * c.y + c.z * c.z) > 1e-6 * (1.0 + std::fabs(sp.radius))) {
                            return "its isolines along a sphere cut by edges that do not meet on its pole axis are not made";
                        }
                    }
                }
            }
        }
    }
    ProxyGraphics g;
    IsolineOptions o;
    if (!acis_isoline_graphics(file, o, g, why)) return why;
    return std::string();
}

bool surface_isoline_graphics(const Entity& e, double view_height, std::vector<uint8_t>& out, std::string& why, int circle_zoom) {
    why = surface_isoline_error(e);
    if (!why.empty()) return false;
    AcisFile file;
    if (!body_of(e, file, why)) return false;
    const SurfaceData& d = e.as_surface();
    IsolineOptions o;
    o.u_isolines = d.u_isolines;
    o.v_isolines = d.v_isolines;
    o.view_height = view_height > 0.0 ? view_height : 100.0;
    o.circle_zoom = circle_zoom;
    o.color = raw_color(e.props.color);
    ProxyGraphics g;
    if (!acis_isoline_graphics(file, o, g, why)) return false;
    out = g.bytes();
    return true;
}

std::vector<uint8_t> surface_recolored_graphics(const Entity& was, const Color& c) {
    // the record's stream: AcDbEntity's 92 (160 from 2010) and the 310 groups after it
    std::vector<uint8_t> bytes;
    long size = -1;
    bool in_entity = false;
    for (const DxfGroup& g : was.raw.groups()) {
        if (g.code == 100) {
            if (in_entity && size >= 0) break;
            in_entity = g.value == "AcDbEntity";
        }
        if (in_entity && (g.code == 92 || g.code == 160) && size < 0) size = std::atol(g.value.c_str());
        if (in_entity && g.code == 310 && size >= 0) {
            for (size_t i = 0; i + 1 < g.value.size(); i += 2) {
                auto digit = [](char x) { return x <= '9' ? x - '0' : (x | 0x20) - 'a' + 10; };
                bytes.push_back(static_cast<uint8_t>(digit(g.value[i]) * 16 + digit(g.value[i + 1])));
            }
        }
    }
    if (size < 8 || bytes.size() < static_cast<size_t>(size)) return {};
    bytes.resize(static_cast<size_t>(size));
    int32_t count;
    std::memcpy(&count, &bytes[4], 4);
    int32_t color = raw_color(c);
    size_t p = 8;
    for (int32_t i = 0; i < count && p + 8 <= bytes.size(); ++i) {
        int32_t length, type;
        std::memcpy(&length, &bytes[p], 4);
        std::memcpy(&type, &bytes[p + 4], 4);
        if (length < 8 || p + static_cast<size_t>(length) > bytes.size()) return {};
        if (type == 22 && length >= 12) std::memcpy(&bytes[p + 8], &color, 4);
        p += static_cast<size_t>(length);
    }
    return bytes;
}

} // namespace internal
} // namespace hdxf
