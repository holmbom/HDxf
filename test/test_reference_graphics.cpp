#include "test_framework.hpp"
#include "typed_corpus.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.6 of phase 9 (plan 2026-10-27-fas-9-typer.md): the graphics and the extents of the underlays and the coordination
// model.
//
// An underlay's drawing is the page of a PDF, a sheet of a DWF or a model of a DGN: HDxf has no reader of any of them (no PDF,
// DWF or DGN engine, B107: a file name alone is no geometry source), so
//   - the proxy graphics of an underlay are AutoCAD's own bytes when a file has them and, for a new one, AutoCAD's stream of
//     an underlay whose source it cannot read (the 8 bytes of an empty stream: doc/facit/acad_*_dxf_underlay_pdf*.dxf);
//   - an underlay adds nothing to $EXTMIN/$EXTMAX (AutoCAD's frame comes from the source's page: ej testat, F9.3).
// The coordination model has its definition's box (NAVISWORKSMODELDEF's two corners) and a transform: its extents are the
// eight corners of the box through the transform, mapped by the INSERT it is in (B78, B107); AutoCAD's own are ej testat
// (no real model could be attached, F9.23).
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf::Point3;
using hdxf::UnderlayKind;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tolerance = 1e-9) { return std::fabs(a - b) <= tolerance * std::max(1.0, std::fabs(a)); }

bool header_point(const Drawing& d, const std::string& name, Point3& out) {
    for (const auto& v : d.header_vars()) {
        if (v.name != name) continue;
        for (const auto& [code, value] : v.values) {
            if (code == 10) out.x = std::atof(value.c_str());
            if (code == 20) out.y = std::atof(value.c_str());
            if (code == 30) out.z = std::atof(value.c_str());
        }
        return true;
    }
    return false;
}

Drawing reload(Drawing& d, DxfFormat format) {
    std::vector<uint8_t> out = d.save_to_memory(format);
    assert_true(!out.empty(), d.last_error());
    Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
    return r;
}

struct Box {
    Point3 lo = {1e300, 1e300, 1e300};
    Point3 hi = {-1e300, -1e300, -1e300};
    void add(const Point3& p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
};

// The eight corners of (min, max) through a row-major 4x4 matrix (the translation in the fourth column)
Box model_box(const Point3& min, const Point3& max, const std::array<double, 16>& m) {
    Box b;
    for (int i = 0; i < 8; ++i) {
        const double x = (i & 1) ? max.x : min.x, y = (i & 2) ? max.y : min.y, z = (i & 4) ? max.z : min.z;
        b.add({m[0] * x + m[1] * y + m[2] * z + m[3], m[4] * x + m[5] * y + m[6] * z + m[7], m[8] * x + m[9] * y + m[10] * z + m[11]});
    }
    return b;
}

std::array<double, 16> rotation_z(double degrees, double tx, double ty, double tz, double sx = 1.0) {
    const double c = std::cos(degrees * kPi / 180.0), s = std::sin(degrees * kPi / 180.0);
    return {sx * c, -s, 0, tx, sx * s, c, 0, ty, 0, 0, 1, tz, 0, 0, 0, 1};
}

std::vector<uint8_t> pattern(size_t n) {
    std::vector<uint8_t> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = static_cast<uint8_t>((i * 37 + 11) & 0xFF);
    out[0] = static_cast<uint8_t>(n & 0xFF);
    out[1] = static_cast<uint8_t>((n >> 8) & 0xFF);
    out[2] = out[3] = 0;
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Underlays: proxy graphics
// ---------------------------------------------------------------------------

TEST(reference_graphics_an_underlays_proxy_graphics_follow_proxygraphics_and_the_version) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (bool enabled : {true, false}) {
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                Drawing d(v);
                d.set_header_int("$PROXYGRAPHICS", 70, enabled ? 1 : 0);
                d.add_underlay(UnderlayKind::Pdf, "a.pdf", "1", {0, 0, 0});
                Drawing r = reload(d, f);
                const Entity* e = nullptr;
                for (const Entity& x : r.entities()) if (x.type == EntityType::PdfUnderlay) e = &x;
                assert_true(e != nullptr, "the underlay");
                const std::vector<uint8_t>& g = e->as_underlay().proxy_graphics;
                if (enabled) {
                    assert_eq(8, static_cast<int>(g.size()), std::string("AutoCAD's empty stream, ") + hdxf_test::version_label(v));
                    assert_true(g[0] == 8 && g[1] == 0 && g[2] == 0 && g[3] == 0 && g[4] == 0 && g[7] == 0, "its size and no commands");
                } else {
                    assert_true(g.empty(), "$PROXYGRAPHICS 0: AutoCAD writes no proxy graphics");
                }
            }
        }
    }
}

TEST(reference_graphics_an_underlays_graphics_as_read_are_kept_and_other_bytes_are_written_in_pieces_of_127) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2018}) {
        Drawing d(v);
        Entity& e = d.add_underlay(UnderlayKind::Dwf, "plan.dwf", "Sheet", {1, 2, 3});
        e.as_underlay().proxy_graphics = pattern(300);        // three 310 groups: 127, 127, 46
        std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!bytes.empty(), d.last_error());
        // the size group and the three 310 groups: 92 before 2010, 160 from it
        std::istringstream in(std::string(bytes.begin(), bytes.end()));
        std::string code_line, value_line;
        bool size_group = false;
        int groups = 0;
        const int size_code = v >= DxfVersion::R2010 ? 160 : 92;
        std::vector<size_t> lengths;
        while (std::getline(in, code_line) && std::getline(in, value_line)) {
            const int code = std::atoi(code_line.c_str());
            if (code == size_code && std::atoi(value_line.c_str()) == 300) size_group = true;
            if (code == 310) {
                ++groups;
                while (!value_line.empty() && (value_line.back() == '\r' || value_line.back() == ' ')) value_line.pop_back();
                lengths.push_back(value_line.size() / 2);
            }
        }
        assert_true(size_group, "the size of the stream (300)");
        assert_eq(3, groups, "three 310 groups");
        assert_true(lengths == std::vector<size_t>({127, 127, 46}), "127, 127 and 46 bytes");
        Drawing r;
        assert_true(r.load_from_memory(bytes.data(), bytes.size()), r.last_error());
        const Entity* back = nullptr;
        for (const Entity& x : r.entities()) if (x.type == EntityType::DwfUnderlay) back = &x;
        assert_true(back && back->as_underlay().proxy_graphics == pattern(300), "the bytes as they were");
        // A change of the placement does not touch them: they are AutoCAD's, regenerated by AutoCAD when it draws the underlay
        // (HDxf cannot draw the source: ej testat, F9.3)
        for (Entity& x : r.entities()) if (x.type == EntityType::DwfUnderlay) x.as_underlay().insertion.x = 99.0;
        Drawing again = reload(r, DxfFormat::Binary);
        for (const Entity& x : again.entities()) {
            if (x.type != EntityType::DwfUnderlay) continue;
            assert_true(x.as_underlay().proxy_graphics == pattern(300), "unchanged by a move");
            assert_eq(99.0, x.as_underlay().insertion.x, "the move");
        }
    }
}

TEST(reference_graphics_underlays_add_no_extents_but_the_coordination_model_adds_its_box) {
    Drawing d(DxfVersion::R2018);
    d.add_line({0, 0, 0}, {10, 10, 0});
    Entity& u = d.add_underlay(UnderlayKind::Pdf, "a.pdf", "1", {1000, 1000, 0});
    u.as_underlay().scale = {5, 5, 5};
    Point3 lo, hi;
    assert_false(d.entity_extents(u, lo, hi), "an underlay has no extents of its own (no source reader)");
    Drawing r = reload(d, DxfFormat::Ascii);
    Point3 min, max;
    assert_true(header_point(r, "$EXTMIN", min) && header_point(r, "$EXTMAX", max), "header");
    assert_true(near(max.x, 10) && near(max.y, 10) && near(min.x, 0) && near(min.y, 0), "the line alone: " + std::to_string(max.x));
}

// ---------------------------------------------------------------------------
// The coordination model: the definition's box through the transform
// ---------------------------------------------------------------------------

TEST(reference_graphics_coordination_model_extents_follow_rotation_mirror_and_move) {
    const Point3 bmin = {-5, -2, 0}, bmax = {20, 8, 12};
    struct Case { const char* name; std::array<double, 16> m; };
    const Case cases[] = {
        {"identity", {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}},
        {"rotated 30 and moved", rotation_z(30, 100, 200, 5)},
        {"rotated 90", rotation_z(90, 0, 0, 0)},
        {"mirrored in X", rotation_z(0, 50, 0, 0, -1.0)},
        {"mirrored and rotated 35", rotation_z(35, -10, 7, 3, -1.0)},
    };
    for (const Case& c : cases) {
        for (DxfVersion v : {DxfVersion::R2013, DxfVersion::R2018}) {
            Drawing d(v);
            Entity& e = d.add_coordination_model("C:\\m\\plant.nwd", bmin, bmax, c.m, 1.0);
            Box want = model_box(bmin, bmax, c.m);
            Point3 lo, hi;
            assert_true(d.entity_extents(e, lo, hi), std::string(c.name) + ": extents");
            assert_true(near(lo.x, want.lo.x) && near(lo.y, want.lo.y) && near(lo.z, want.lo.z) && near(hi.x, want.hi.x) &&
                            near(hi.y, want.hi.y) && near(hi.z, want.hi.z),
                        std::string(c.name) + ": the box's corners through the transform");
            // the drawing's extents, as saved and read back, both formats
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                Drawing r = reload(d, f);
                Point3 min, max;
                assert_true(header_point(r, "$EXTMIN", min) && header_point(r, "$EXTMAX", max), "header");
                assert_true(near(min.x, want.lo.x, 1e-7) && near(max.x, want.hi.x, 1e-7) && near(min.y, want.lo.y, 1e-7) &&
                                near(max.y, want.hi.y, 1e-7) && near(min.z, want.lo.z, 1e-7) && near(max.z, want.hi.z, 1e-7),
                            std::string(c.name) + ": $EXTMIN/$EXTMAX");
            }
        }
    }
}

TEST(reference_graphics_coordination_model_in_paper_space_in_a_block_and_with_its_definition_gone) {
    const Point3 bmin = {0, 0, 0}, bmax = {4, 6, 8};
    const std::array<double, 16> m = rotation_z(30, 10, 20, 0);
    // paper space has its own extents
    {
        Drawing d(DxfVersion::R2018);
        Entity& e = d.add_coordination_model("C:\\m\\plant.nwd", bmin, bmax, m);
        e.props.paper_space = 1;
        Drawing r = reload(d, DxfFormat::Ascii);
        Box want = model_box(bmin, bmax, m);
        Point3 pmin, pmax;
        assert_true(header_point(r, "$PEXTMIN", pmin) && header_point(r, "$PEXTMAX", pmax), "paper header");
        assert_true(near(pmin.x, want.lo.x, 1e-7) && near(pmax.y, want.hi.y, 1e-7), "the paper space extents");
    }
    // in a block, through an INSERT turned 90 degrees and scaled by 2 (a multiple of 90: the box's corners)
    {
        Drawing d(DxfVersion::R2018);
        Entity& e = d.add_coordination_model("C:\\m\\plant.nwd", bmin, bmax, m);
        Entity model = e;
        d.entities().clear();
        hdxf::Block& block = d.add_block("MODELS");
        block.entities.push_back(model);
        Entity& insert = d.add_insert("MODELS", {1000, 2000, 0});
        insert.as_insert().x_scale = 2.0;
        insert.as_insert().y_scale = 2.0;
        insert.as_insert().z_scale = 2.0;
        insert.as_insert().rotation = 90.0;
        Box inner = model_box(bmin, bmax, m);
        Box want;
        // (x, y) -> (-2y + 1000, 2x + 2000), z -> 2z
        for (int i = 0; i < 8; ++i) {
            const double x = (i & 1) ? inner.hi.x : inner.lo.x, y = (i & 2) ? inner.hi.y : inner.lo.y, z = (i & 4) ? inner.hi.z : inner.lo.z;
            // (the inner box is axis aligned only as a bound: map its corners, which is what an INSERT of 90 degrees does)
            want.add({-2 * y + 1000, 2 * x + 2000, 2 * z});
        }
        Point3 lo, hi;
        assert_true(d.entity_extents(insert, lo, hi), "the insert's extents");
        assert_true(near(lo.x, want.lo.x, 1e-6) && near(hi.x, want.hi.x, 1e-6) && near(lo.y, want.lo.y, 1e-6) && near(hi.y, want.hi.y, 1e-6),
                    "the model through the INSERT: " + std::to_string(lo.x) + " " + std::to_string(hi.x));
    }
    // the definition removed: the model has no box, so no extents (its pointer is 0)
    {
        Drawing d(DxfVersion::R2018);
        Entity& e = d.add_coordination_model("C:\\m\\plant.nwd", bmin, bmax, m);
        d.objects().erase(std::remove_if(d.objects().begin(), d.objects().end(),
                                         [&](const hdxf::Object& o) { return o.type == hdxf::ObjectType::NavisworksModelDef; }),
                          d.objects().end());
        Point3 lo, hi;
        assert_false(d.entity_extents(e, lo, hi), "without its definition the model adds nothing");
    }
}
