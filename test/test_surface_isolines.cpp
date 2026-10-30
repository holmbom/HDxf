#include "test_framework.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "acis.hpp"
#include "acis_isolines.hpp"
#include "proxy_graphics.hpp"
#include "surface_graphics.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.16 of phase 9 (plan 2026-10-27-fas-9-typer.md): the isolines of the surfaces in DXF 2000 and 2004.
//
// The AutoCAD originals are doc/facit/acad_<version>_dxf_surfaces_*.dxf (doc/facit/fas9_surfaces.scr: AutoCAD 2025's core draws
// planes, extruded, revolved and swept surfaces, lofts and NURBS, trims, partial faces, moves and rotations) and the older
// doc/facit/acad_2000_dxf_surfaces.dxf, acad_2004_dxf_surfaces.dxf; fas9_surfaces_view.scr (views and VIEWRES),
// fas9_surfaces_iso.scr and fas9_surfaces_iso2.scr (lofts, NURBS, an offset, a network). They are local files (the facit is not
// in git): a test whose file is missing is skipped. The generator (src/acis_isolines.cpp) is compared command by command with
// the stream AutoCAD saved — markers, colors, fills, lines, circles, arcs, ellipses and polylines with the parameters of their
// points — for every surface it draws; the surfaces it does not draw (a loft straight between more than two sections, a loft of
// closed sections, a sweep, a generic SURFACE, a sphere cut off its pole axis: what AutoCAD draws of those is not what the body
// alone says) must be refused, named, before anything is written.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

struct Cmd {
    int type = 0;
    std::vector<uint8_t> data;
};

std::vector<Cmd> parse_stream(const std::vector<uint8_t>& b) {
    std::vector<Cmd> out;
    if (b.size() < 8) return out;
    int32_t count;
    std::memcpy(&count, &b[4], 4);
    size_t p = 8;
    for (int i = 0; i < count && p + 8 <= b.size(); ++i) {
        int32_t size, type;
        std::memcpy(&size, &b[p], 4);
        std::memcpy(&type, &b[p + 4], 4);
        if (size < 8 || p + static_cast<size_t>(size) > b.size()) break;
        out.push_back({type, std::vector<uint8_t>(b.begin() + static_cast<long>(p) + 8, b.begin() + static_cast<long>(p) + size)});
        p += static_cast<size_t>(size);
    }
    return out;
}

double dbl(const std::vector<uint8_t>& d, size_t off) {
    double v;
    std::memcpy(&v, &d[off], 8);
    return v;
}

// Two commands the same: the same type and bytes, the doubles within 1e-9 (AutoCAD's own are to 16 digits in DXF)
bool same_command(const Cmd& a, const Cmd& b) {
    if (a.type != b.type || a.data.size() != b.data.size()) return false;
    if (a.type == 19 || a.type == 20 || a.type == 22) return a.data == b.data;
    size_t off = a.type == 6 ? 4 : 0;
    size_t count = a.type == 6 ? (a.data.size() - 4) / 8 : a.type == 5 ? 9 : a.data.size() / 8;
    if (a.type == 6 && a.data.size() >= 4 && std::memcmp(a.data.data(), b.data.data(), 4) != 0) return false;
    for (size_t i = 0; i < count; ++i) {
        double x = dbl(a.data, off + 8 * i), y = dbl(b.data, off + 8 * i);
        if (std::fabs(x - y) > 1e-9 * (1.0 + std::fabs(x) + std::fabs(y))) return false;
    }
    return true;
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// The proxy graphics a record has (AcDbEntity: 92 before 2010, then the 310 groups)
std::vector<uint8_t> saved_proxy(const Entity& e) {
    std::vector<uint8_t> out;
    int size = -1;
    bool in_entity = false;
    for (const hdxf::DxfGroup& g : e.raw.groups()) {
        if (g.code == 100) {
            if (in_entity && size >= 0) break;
            in_entity = g.value == "AcDbEntity";
        }
        if (in_entity && (g.code == 92 || g.code == 160) && size < 0) size = std::atoi(g.value.c_str());
        if (in_entity && g.code == 310 && size >= 0) {
            for (size_t i = 0; i + 1 < g.value.size(); i += 2) {
                out.push_back(static_cast<uint8_t>(hex_digit(g.value[i]) * 16 + hex_digit(g.value[i + 1])));
            }
        }
    }
    if (size >= 0 && out.size() > static_cast<size_t>(size)) out.resize(static_cast<size_t>(size));
    return out;
}

bool is_surface(const Entity& e) {
    return e.type == EntityType::Surface || e.type == EntityType::ExtrudedSurface || e.type == EntityType::LoftedSurface ||
           e.type == EntityType::RevolvedSurface || e.type == EntityType::SweptSurface || e.type == EntityType::PlaneSurface ||
           e.type == EntityType::NurbsSurface;
}

Drawing facit(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/" + name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    Drawing d;
    assert_true(d.load_from_memory(in.data(), in.size()), name + ": " + d.last_error());
    return d;
}

double active_view(const Drawing& d) {
    for (const auto& vp : d.vports()) {
        if (vp.name == "*Active") return vp.view_height;
    }
    return 0.0;
}

// VIEWRES: the active VPORT's 72
int active_circle_zoom(const Drawing& d) {
    for (const auto& vp : d.vports()) {
        if (vp.name == "*Active") return vp.circle_zoom;
    }
    return 1000;
}

struct Tally {
    int compared = 0;
    int refused = 0;
};

// Every surface of a file the generator draws, against AutoCAD's stream
Tally compare_file(const std::string& name) {
    Drawing d = facit(name);
    Tally t;
    double view = active_view(d);
    int circle_zoom = active_circle_zoom(d);
    for (const Entity& e : d.entities()) {
        if (!is_surface(e) || e.raw.empty()) continue;
        if (!surface_isoline_error(e).empty()) {
            ++t.refused;
            continue;
        }
        std::vector<uint8_t> made;
        std::string why;
        assert_true(surface_isoline_graphics(e, view, made, why, circle_zoom), name + " " + e.props.handle + ": " + why);
        std::vector<Cmd> got = parse_stream(made);
        std::vector<Cmd> want = parse_stream(saved_proxy(e));
        assert_true(got.size() == want.size(), name + " " + e.props.handle + ": the number of commands");
        for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
            assert_true(same_command(got[i], want[i]),
                        name + " " + e.props.handle + ": command " + std::to_string(i) + " (type " + std::to_string(want[i].type) + ")");
        }
        ++t.compared;
    }
    return t;
}

} // namespace

TEST(surface_isolines_analytic_faces_are_autocads_in_2000_and_2004) {
    int compared = 0;
    for (const char* tag : {"2000", "2004"}) {
        for (const char* theme : {"surfaces_analytic", "surfaces_trim", "surfaces_trim_trimmed", "surfaces_counts", "surfaces_props",
                                  "surfaces_transform", "surfaces_transform_moved", "surfaces_transform_rotated",
                                  "surfaces_transform_scaled", "surfaces_transform_mirrored", "surfaces_transform_rotated3d"}) {
            // (2000 has no true color: a 255,128,0 entity reads as ACI 30 while its proxy keeps the RGB)
            if (std::string(tag) == "2000" && std::string(theme) == "surfaces_props") continue;
            compared += compare_file(std::string("acad_") + tag + "_dxf_" + theme + ".dxf").compared;
        }
    }
    assert_true(compared > 100, "AutoCAD's surfaces compared: " + std::to_string(compared));
}

// F9.18, third round (Opus): splines, lofts, sums and revolutions — every surface the generator draws in the files of
// fas9_surfaces.scr (spline, nurbs), fas9_surfaces_iso.scr and fas9_surfaces_iso2.scr (extrusions of splines in 2000's sums,
// lofts of two sections, ruled also between different splines, a line, a spline and an arc revolved part of the way round,
// a sphere cut along half its equator), command by command
TEST(surface_isolines_splines_lofts_nurbs_and_revolutions_are_autocads_in_2000_and_2004) {
    int compared = 0, refused = 0;
    for (const char* tag : {"2000", "2004"}) {
        for (const char* theme : {"surfaces_spline", "surfaces_nurbs", "surfaces_nurbs_converted", "surfaces_nurbs_made", "surfaces_iso",
                                  "surfaces_iso2"}) {
            Tally t = compare_file(std::string("acad_") + tag + "_dxf_" + theme + ".dxf");
            compared += t.compared;
            refused += t.refused;
        }
    }
    assert_eq(compared, 56, "AutoCAD's surfaces compared");
    assert_true(refused > 0, "and some refused");
}

// The NURBS AutoCAD makes (CONVTONURBS of a cylinder, cone, sphere, torus, loft, extrusion; NURBS mode) are its streams from the
// body: periodic directions, poles, slits and seams (177B … 1781, 1C81 … 1C8C, iso 265, iso2 260). Written as SURFACE in 2000
// and 2004 they are refused all the same (a generic SURFACE may be AutoCAD's offset or network, which the record does not tell
// apart); here the generator is compared on their bodies
TEST(surface_isolines_nurbs_bodies_are_autocads) {
    struct Case {
        const char* theme;
        const char* handles[8];
    };
    const Case cases[] = {
        {"surfaces_nurbs_converted", {"177A", "177B", "177C", "177D", "177E", "177F", "1780", "1781"}},
        {"surfaces_nurbs_made", {"1C81", "1C84", "1C89", "1C8C", nullptr, nullptr, nullptr, nullptr}},
        {"surfaces_iso", {"265", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}},
        {"surfaces_iso2", {"260", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}},
    };
    int compared = 0;
    for (const char* tag : {"2000", "2004"}) {
        for (const Case& c : cases) {
            const std::string name = std::string("acad_") + tag + "_dxf_" + c.theme + ".dxf";
            Drawing d = facit(name);
            for (const char* handle : c.handles) {
                if (!handle) continue;
                const Entity* found = nullptr;
                for (const Entity& e : d.entities()) {
                    if (e.props.handle == handle) found = &e;
                }
                assert_true(found != nullptr && is_surface(*found), name + " has " + handle);
                assert_true(found->props.color.aci == 256 && found->props.color.rgb < 0, name + " " + handle + ": by layer");
                const hdxf::AcisData& acis = found->as_surface().acis;
                hdxf::internal::AcisFile file;
                assert_true(read_sat(acis_deciphered(acis), file), name + " " + handle + ": its SAT");
                IsolineOptions o;
                o.u_isolines = found->as_surface().u_isolines;
                o.v_isolines = found->as_surface().v_isolines;
                o.view_height = active_view(d);
                o.circle_zoom = active_circle_zoom(d);
                hdxf::internal::ProxyGraphics g;
                std::string why;
                assert_true(acis_isoline_graphics(file, o, g, why), name + " " + handle + ": " + why);
                std::vector<Cmd> got = parse_stream(g.bytes());
                std::vector<Cmd> want = parse_stream(saved_proxy(*found));
                assert_true(got.size() == want.size(), name + " " + handle + ": the number of commands");
                for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
                    assert_true(same_command(got[i], want[i]), name + " " + handle + ": command " + std::to_string(i));
                }
                ++compared;
            }
        }
    }
    assert_eq(compared, 28, "the NURBS of the four files, 2000 and 2004");
}

// What is not derived stays refused, named (F9.18): a loft straight between more than two sections (AutoCAD draws through the
// sections, which the body does not give: acad_*_surfaces_iso 25D, 26A unevenly spaced), one curved between more than two
// sections (the core's 263 is its body's stream, Erik's 27C is not: AutoCAD draws its own loft), a loft of circles (closed sections: its
// curves are AutoCAD's own surface's, 771), a sweep (774), a SURFACE that is AutoCAD's offset or network (iso2 261, 270, which
// the record does not tell from a NURBS), a sphere cut by edges off its pole axis (counts 3843: AutoCAD's parameter box of it is
// not the edges')
TEST(surface_isolines_what_is_not_derived_is_refused) {
    struct Case {
        const char* file;
        const char* handle;
        const char* why;
    };
    const Case cases[] = {
        {"acad_2004_dxf_surfaces_spline.dxf", "761", "its isolines along a loft straight between more than two sections are not made"},
        {"acad_2004_dxf_surfaces_iso.dxf", "25D", "its isolines along a loft straight between more than two sections are not made"},
        {"acad_2004_dxf_surfaces_iso.dxf", "26A", "its isolines along a loft straight between more than two sections are not made"},
        {"acad_2004_dxf_surfaces_iso.dxf", "263", "its isolines along a loft of more than two sections are not made"},
        {"acad_2004_dxf_surfaces.dxf", "27C", "its isolines along a loft of more than two sections are not made"},
        {"acad_2004_dxf_surfaces_spline.dxf", "771", "its isolines along a spline surface of this kind are not made"},
        {"acad_2004_dxf_surfaces_spline.dxf", "774", "its isolines along a spline surface of this kind are not made"},
        {"acad_2004_dxf_surfaces_iso2.dxf", "261", "its isolines along a spline surface of this kind are not made"},
        {"acad_2004_dxf_surfaces_iso2.dxf", "270", "its isolines along a spline surface of this kind are not made"},
        {"acad_2004_dxf_surfaces_counts.dxf", "3843", "its isolines along a sphere cut by edges that do not meet on its pole axis are not made"},
    };
    for (const Case& c : cases) {
        Drawing d = facit(c.file);
        bool found = false;
        for (const Entity& e : d.entities()) {
            if (e.props.handle != c.handle) continue;
            found = true;
            assert_eq(surface_isoline_error(e), std::string(c.why), std::string(c.file) + " " + c.handle);
        }
        assert_true(found, std::string(c.file) + " has " + c.handle);
    }
    // a refused surface whose isolines changed (71) is not written as 2004: the named error (a straight loft of three sections)
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/acad_2004_dxf_surfaces_spline.dxf"));
    Drawing dwg;
    assert_true(dwg.load_from_memory(in.data(), in.size()), dwg.last_error());
    for (Entity& e : dwg.entities()) {
        if (e.props.handle == "761") e.as_surface().u_isolines = 3;
    }
    assert_true(dwg.save_to_memory().empty(), "the changed loft is not written");
    assert_eq(dwg.last_error(), std::string("LOFTEDSURFACE: its isolines along a loft straight between more than two sections are not made "
                                            "(DXF 2000 and 2004 keep them as proxy graphics)"));
    // in another color only: AutoCAD's stream as read, the 22 of every curve the new color (ACI 5: 0xC3000005)
    for (const char* version : {"2000", "2004"}) {
        const std::string name = std::string("acad_") + version + "_dxf_surfaces_spline.dxf";
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/" + name));
        Drawing colored;
        assert_true(colored.load_from_memory(bytes.data(), bytes.size()), colored.last_error());
        std::vector<Cmd> before;
        for (Entity& e : colored.entities()) {
            if (e.props.handle != "761") continue;
            before = parse_stream(saved_proxy(e));
            e.props.color = hdxf::Color::from_aci(5);
        }
        std::vector<uint8_t> out = colored.save_to_memory();
        assert_true(!out.empty(), name + ": " + colored.last_error());
        Drawing back;
        assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        for (const Entity& e : back.entities()) {
            if (e.props.handle != "761") continue;
            std::vector<Cmd> after = parse_stream(saved_proxy(e));
            assert_true(!before.empty() && after.size() == before.size(), name + ": the stream as read");
            for (size_t i = 0; i < std::min(after.size(), before.size()); ++i) {
                if (before[i].type == 22) {
                    assert_true(after[i].type == 22 && after[i].data.size() == 4 && after[i].data[0] == 5 && after[i].data[3] == 0xC3,
                                name + ": the new color in command " + std::to_string(i));
                } else {
                    assert_true(same_command(after[i], before[i]), name + ": command " + std::to_string(i) + " as read");
                }
            }
        }
    }
}

TEST(surface_isolines_binary_files_give_the_same_streams) {
    Tally a = compare_file("acad_2004_dxf_surfaces_analytic.dxf");
    Tally b = compare_file("acad_2004_dxf_surfaces_analytic_bin.dxf");
    assert_eq(a.compared, b.compared, "the same surfaces");
    assert_true(a.compared > 20, "surfaces compared");
}

TEST(surface_isolines_extruded_splines_and_the_older_facit_files) {
    int compared = 0;
    for (const char* name : {"acad_2000_dxf_surfaces.dxf", "acad_2004_dxf_surfaces.dxf", "acad_2000_dxf_surfaces_spline.dxf",
                             "acad_2004_dxf_surfaces_spline.dxf"}) {
        Drawing d = facit(name);
        for (const Entity& e : d.entities()) {
            // the planar ones and cylinders: independent of the view AutoCAD drew with; the extruded splines depend on it
            if (e.type != EntityType::PlaneSurface && e.type != EntityType::Surface) continue;
            if (!surface_isoline_error(e).empty()) continue;
            std::vector<uint8_t> made;
            std::string why;
            assert_true(surface_isoline_graphics(e, active_view(d), made, why), why);
            std::vector<Cmd> got = parse_stream(made), want = parse_stream(saved_proxy(e));
            if (want.empty()) continue;
            assert_true(got.size() == want.size(), std::string(name) + " " + e.props.handle);
            for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
                assert_true(same_command(got[i], want[i]), std::string(name) + " " + e.props.handle + " command " + std::to_string(i));
            }
            ++compared;
        }
    }
    assert_true(compared > 4, "compared " + std::to_string(compared));
}

TEST(surface_isolines_the_view_sets_the_tolerance_of_a_spline_isoline) {
    // The extruded splines of AutoCAD's: their curves are polylines whose points follow T = view height * 1.4055e-4
    Drawing d = facit("acad_2004_dxf_surfaces_spline.dxf");
    bool changed = false;
    for (const Entity& e : d.entities()) {
        if (e.type != EntityType::ExtrudedSurface || !surface_isoline_error(e).empty()) continue;
        std::vector<uint8_t> a, b;
        std::string why;
        assert_true(surface_isoline_graphics(e, 100.0, a, why), why);
        assert_true(surface_isoline_graphics(e, 400.0, b, why), why);
        assert_true(a.size() >= b.size(), "a finer view has at least as many points");
        changed = changed || a != b;
    }
    assert_true(changed, "the view changes the polylines of an extruded spline");
}

TEST(surface_isolines_new_surface_written_as_2000_and_2004_has_the_streams_it_makes) {
    Drawing src = facit("acad_2004_dxf_surfaces_trim_trimmed.dxf");
    const Entity* plane = nullptr;
    for (const Entity& e : src.entities()) {
        if (e.type == EntityType::PlaneSurface && surface_isoline_error(e).empty()) plane = &e;
    }
    assert_true(plane != nullptr, "a plane in the file");
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004}) {
        Drawing dwg(v);
        Entity& e = dwg.add_surface(EntityType::PlaneSurface, plane->as_surface());
        e.props.color = hdxf::Color::from_aci(3);
        std::vector<uint8_t> bytes = dwg.save_to_memory();
        assert_true(!bytes.empty(), dwg.last_error());
        Drawing back;
        assert_true(back.load_from_memory(bytes.data(), bytes.size()), back.last_error());
        const Entity* found = nullptr;
        for (const Entity& x : back.entities()) {
            if (x.type == EntityType::PlaneSurface) found = &x;
        }
        assert_true(found != nullptr, "the plane is back");
        std::vector<Cmd> have = parse_stream(saved_proxy(*found));
        std::vector<uint8_t> made;
        std::string why;
        assert_true(surface_isoline_graphics(*found, active_view(back), made, why), why);
        std::vector<Cmd> want = parse_stream(made);
        assert_true(!have.empty(), "isolines are written");
        assert_true(have.size() == want.size(), "commands");
        // AutoCAD's color for the entity: ACI 3 in the 22 of each curve
        bool green = false;
        for (const Cmd& c : have) green = green || (c.type == 22 && c.data.size() == 4 && c.data[0] == 3 && c.data[3] == 0xC3);
        assert_true(green, "the entity's color in the graphics");
    }
}

TEST(surface_isolines_a_changed_body_gets_new_isolines_and_a_surface_of_another_kind_is_refused) {
    Drawing src = facit("acad_2004_dxf_surfaces_analytic.dxf");
    std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/acad_2004_dxf_surfaces_analytic.dxf"));
    Drawing dwg;
    assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), dwg.last_error());
    // SURFU 3: its isolines are those of 3 curves in one direction
    for (Entity& e : dwg.entities()) {
        if (e.type == EntityType::PlaneSurface) {
            e.as_surface().u_isolines = 2;
            break;
        }
    }
    std::vector<uint8_t> out = dwg.save_to_memory();
    assert_true(!out.empty(), dwg.last_error());
    Drawing back;
    assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    for (const Entity& e : back.entities()) {
        if (e.type != EntityType::PlaneSurface) continue;
        assert_eq(e.as_surface().u_isolines, 2, "71 as changed");
        std::vector<Cmd> have = parse_stream(saved_proxy(e));
        std::vector<uint8_t> made;
        std::string why;
        assert_true(surface_isoline_graphics(e, active_view(back), made, why), why);
        assert_true(have.size() == parse_stream(made).size(), "the isolines follow 71");
        break;
    }
    // a loft of splines: not drawn, named, as 2000 and 2004
    Drawing loft(DxfVersion::R2004);
    hdxf::SurfaceData none;
    loft.add_surface(EntityType::LoftedSurface, none);
    assert_true(loft.save_to_memory().empty(), "a loft without a body");
    assert_true(loft.last_error().rfind("LOFTEDSURFACE ", 0) == 0 && loft.last_error().find("HDxf does not make its isolines") != std::string::npos,
                loft.last_error());
    // 2007 writes AutoCAD's empty stream
    Drawing r2007(DxfVersion::R2007);
    r2007.add_surface(EntityType::PlaneSurface, hdxf::SurfaceData{});
    assert_true(!r2007.save_to_memory().empty(), r2007.last_error());
}

// F9.18, second and third round: AutoCAD's files of the same drawing saved in each view and with each VIEWRES
// (doc/facit/fas9_surfaces_view.scr). Both extruded splines — 25B of four spans and 25F of eight — are the generator's exactly in
// every view and with every VIEWRES, in 2000 and 2004: T = height * 1.4055e-4 * 1000 / VIEWRES (VPORT 40 and 72; an edge two
// thirds of it), each span halved once unless its middle and quarter points are within T of the chord, then each half while its
// middle is not (F9.18 (g))
TEST(surface_isolines_the_view_and_viewres_set_the_tolerance) {
    int compared = 0;
    for (const char* version : {"2000", "2004"}) {
        for (const char* tag : {"", "_viewres100", "_viewres300", "_viewres3000", "_viewres10000", "_zoom_a", "_zoom_b", "_zoom_c", "_zoom_d"}) {
            const std::string name = std::string("acad_") + version + "_dxf_surfaces_view" + tag + ".dxf";
            Drawing d = facit(name);
            double view = active_view(d);
            int circle_zoom = active_circle_zoom(d);
            for (const Entity& e : d.entities()) {
                if (!is_surface(e) || e.raw.empty() || (e.props.handle != "25B" && e.props.handle != "25F")) continue;
                assert_eq(surface_isoline_error(e), std::string(), name + " " + e.props.handle + ": drawn");
                std::vector<uint8_t> made;
                std::string why;
                assert_true(surface_isoline_graphics(e, view, made, why, circle_zoom), name + ": " + why);
                std::vector<Cmd> got = parse_stream(made);
                std::vector<Cmd> want = parse_stream(saved_proxy(e));
                assert_true(got.size() == want.size(), name + " " + e.props.handle + ": the number of commands");
                for (size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
                    assert_true(same_command(got[i], want[i]), name + " " + e.props.handle + ": command " + std::to_string(i) + " (type " +
                                                                   std::to_string(got[i].type) + ", " + std::to_string(got[i].data.size()) +
                                                                   " bytes against " + std::to_string(want[i].data.size()) + ")");
                }
                ++compared;
            }
        }
    }
    assert_true(compared == 36, "the eighteen files have both surfaces: " + std::to_string(compared));
}
