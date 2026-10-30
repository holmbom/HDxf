#include "test_support.hpp"

#include "acis_sab.hpp"

using namespace acis_test;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

// ---------------------------------------------------------------------------
// Each body through HDxf: SAT 21500 into DXF 2010 (the lines), and DXF 2013 and 2018 (the SAB HDxf makes
// from them: 21800 and 22300). Written, read, extents taken, written again, read again.
// ---------------------------------------------------------------------------

namespace {

acis::Frame frame_at(double x, double y, double z) {
    acis::Frame frame;
    frame.origin = {x, y, z};
    return frame;
}

struct Case {
    std::string name;
    acis::SatDocument sat;
    Box box;                            // the extents the body must have
};

Case make_case(const std::string& name, acis::Result<acis::SatDocument> sat, const Box& box) {
    return Case{name, require(sat, name), box};
}

std::vector<Case> all_cases() {
    std::vector<Case> cases;
    acis::BoxParameters box;
    box.frame = frame_at(10, 10, 10);
    box.size_x = 20;
    box.size_y = 15;
    box.size_z = 20;
    cases.push_back(make_case("box", acis::make_box(box), Box{{0, 2.5, 0}, {20, 17.5, 20}}));

    acis::CylinderParameters cylinder;
    cylinder.frame = frame_at(90, 10, 12.5);
    cylinder.radius = 10;
    cylinder.height = 25;
    cases.push_back(make_case("cylinder", acis::make_cylinder(cylinder), Box{{80, 0, 0}, {100, 20, 25}}));

    acis::ConeParameters cone;
    cone.frame = frame_at(130, 10, 15);
    cone.radius = 12;
    cone.height = 30;
    cases.push_back(make_case("cone", acis::make_cone(cone), Box{{118, -2, 0}, {142, 22, 30}}));

    acis::ConeParameters frustum;
    frustum.radius = 12;
    frustum.top_radius = 6;
    frustum.height = 20;
    cases.push_back(make_case("frustum", acis::make_cone(frustum), Box{{-12, -12, -10}, {12, 12, 10}}));

    acis::SphereParameters sphere;
    sphere.frame = frame_at(50, 10, 10);
    sphere.radius = 12;
    cases.push_back(make_case("sphere", acis::make_sphere(sphere), Box{{38, -2, -2}, {62, 22, 22}}));

    acis::TorusParameters torus;
    torus.frame = frame_at(170, 10, 12);
    torus.major_radius = 15;
    torus.minor_radius = 4;
    cases.push_back(make_case("torus", acis::make_torus(torus), Box{{151, -9, 8}, {189, 29, 16}}));

    cases.push_back(make_case("hole block", build_hole_block(), Box{{0, 0, 0}, {30, 30, 24}}));
    cases.push_back(make_case("two lumps", build_two_lumps(), Box{{0, 0, 0}, {55, 20, 20}}));
    cases.push_back(make_case("cube with cavity", build_cavity_cube(), Box{{0, 0, 0}, {40, 40, 40}}));
    cases.push_back(make_case("rectangle region", build_rectangle_region(0, 60, 30, 80), Box{{0, 60, 0}, {30, 80, 0}}));
    cases.push_back(make_case("circle region", build_circle_region({170, 145, 0}, 15), Box{{155, 130, 0}, {185, 160, 0}}));
    cases.push_back(make_case("sheet", build_rectangle_region(0, 0, 10, 5, acis::BodyKind::OpenBody),
                              Box{{0, 0, 0}, {10, 5, 0}}));
    cases.push_back(make_case("NURBS cylinder", build_nurbs_cylinder(10, 25), Box{{-10, -10, 0}, {10, 10, 25}}));
    {
        // BODY with a closed shell, and BODY with an open one (a box without its top)
        acis::BodyBuilder closed;
        BoxPlacement place;
        place.kind = acis::BodyKind::ClosedBody;
        BoxModel closed_box = build_box(closed, 10, 20, 30, BoxFlags{}, place);
        cases.push_back(make_case("closed body", closed.build_sat(closed_box.body), Box{{0, 0, 0}, {10, 20, 30}}));
        acis::BodyBuilder open;
        place.kind = acis::BodyKind::OpenBody;
        BoxFlags no_top;
        no_top.omit_face = 5;
        BoxModel open_box = build_box(open, 10, 20, 30, no_top, place);
        cases.push_back(make_case("open body", open.build_sat(open_box.body), Box{{0, 0, 0}, {10, 20, 30}}));
    }
    cases.push_back(make_case("cylinder with a seam", build_seam_cylinder(10, 25, false), Box{{-10, -10, -12.5}, {10, 10, 12.5}}));
    cases.push_back(make_case("cylinder with a seam and pcurves", build_seam_cylinder(10, 25, true),
                              Box{{-10, -10, -12.5}, {10, 10, 12.5}}));
    return cases;
}

// The topology and the geometry HDxf finds in two readings of a body must be the same
void require_same_bodies(const std::vector<hdxf::internal::AcisBody>& a, const std::vector<hdxf::internal::AcisBody>& b,
                         const std::string& what) {
    assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + ": bodies");
    for (size_t k = 0; k < a.size(); ++k) {
        const hdxf::internal::AcisBody& x = a[k];
        const hdxf::internal::AcisBody& y = b[k];
        assert_eq(static_cast<int>(x.faces.size()), static_cast<int>(y.faces.size()), what + ": faces");
        assert_eq(static_cast<int>(x.edges.size()), static_cast<int>(y.edges.size()), what + ": edges");
        assert_eq(static_cast<int>(x.curves.size()), static_cast<int>(y.curves.size()), what + ": curves");
        assert_eq(static_cast<int>(x.surfaces.size()), static_cast<int>(y.surfaces.size()), what + ": surfaces");
        for (size_t i = 0; i < x.faces.size(); ++i) {
            assert_eq(static_cast<int>(x.faces[i].loops.size()), static_cast<int>(y.faces[i].loops.size()), what + ": loops");
            assert_true(x.faces[i].reversed == y.faces[i].reversed, what + ": face senses");
            for (size_t l = 0; l < x.faces[i].loops.size(); ++l) {
                assert_eq(static_cast<int>(x.faces[i].loops[l].coedges.size()),
                          static_cast<int>(y.faces[i].loops[l].coedges.size()), what + ": coedges");
            }
        }
        for (size_t i = 0; i < x.edges.size(); ++i) {
            assert_true(x.edges[i].has_start == y.edges[i].has_start && x.edges[i].has_end == y.edges[i].has_end, what + ": ends");
            require_near(x.edges[i].start.x, y.edges[i].start.x, 1e-12, what + " edge start x");
            require_near(x.edges[i].start.y, y.edges[i].start.y, 1e-12, what + " edge start y");
            require_near(x.edges[i].start.z, y.edges[i].start.z, 1e-12, what + " edge start z");
            require_near(x.edges[i].end.x, y.edges[i].end.x, 1e-12, what + " edge end x");
        }
        for (size_t i = 0; i < x.surfaces.size(); ++i) {
            assert_true(x.surfaces[i].type == y.surfaces[i].type, what + ": surface types");
            require_near(x.surfaces[i].radius, y.surfaces[i].radius, 1e-12, what + " radius");
            require_near(x.surfaces[i].origin.x, y.surfaces[i].origin.x, 1e-12, what + " surface origin");
        }
        for (size_t i = 0; i < x.curves.size(); ++i) {
            assert_true(x.curves[i].type == y.curves[i].type, what + ": curve types");
        }
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) require_near(x.transform[r][c], y.transform[r][c], 1e-12, what + " transform");
        }
    }
}

uint32_t sab_format(const std::vector<uint8_t>& sab) {
    // "ACIS BinaryFile" or "ASM BinaryFile4", then the format version as 32 bits
    return static_cast<uint32_t>(sab[15]) | (static_cast<uint32_t>(sab[16]) << 8) | (static_cast<uint32_t>(sab[17]) << 16) |
           (static_cast<uint32_t>(sab[18]) << 24);
}

} // namespace

TEST(acis_roundtrip_dxf_2010_keeps_the_lines_and_the_extents) {
    for (const Case& c : all_cases()) {
        hdxf::Drawing drawing = drawing_of(c.sat, hdxf::DxfVersion::R2010);
        hdxf::EntityType type = entity_type_of(c.sat.kind);
        assert_true(first_body(drawing).type == type, c.name + ": entity type");
        Box built = extents_of(drawing, first_body(drawing));
        require_box(built, c.box, 1e-6, c.name + " extents");
        hdxf::AcisData original = acis_data(c.sat);
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            std::string how = c.name + (format == hdxf::DxfFormat::Ascii ? " (ASCII)" : " (binary)");
            hdxf::Drawing again = reloaded(drawing, format);
            const hdxf::Entity& body = first_body(again);
            assert_true(body.type == type, how + ": entity type after reading");
            assert_true(body.as_acis().lines == original.lines, how + ": the lines");
            require_box(extents_of(again, body), built, 1e-9, how + " extents after reading");
            // Written again from what was read, read again
            hdxf::Drawing twice = reloaded(again, format);
            assert_true(first_body(twice).as_acis().lines == original.lines, how + ": the lines the second time");
            require_box(extents_of(twice, first_body(twice)), built, 1e-9, how + " extents the second time");
        }
    }
}

TEST(acis_roundtrip_dxf_2010_is_read_by_hdxf_as_the_same_bodies) {
    for (const Case& c : all_cases()) {
        ParsedSat built = parse_document(c.sat);
        hdxf::Drawing drawing = drawing_of(c.sat, hdxf::DxfVersion::R2010);
        hdxf::Drawing again = reloaded(drawing, hdxf::DxfFormat::Ascii);
        hdxf::internal::AcisFile file;
        assert_true(hdxf::internal::read_sat(hdxf::internal::acis_deciphered(first_body(again).as_acis()), file), c.name);
        require_same_bodies(built.bodies, hdxf::internal::acis_bodies(file), c.name);
        assert_eq(static_cast<int>(file.records.size()), c.sat.record_count, c.name + ": the header's record count");
    }
}

TEST(acis_roundtrip_dxf_2013_and_2018_make_the_sab) {
    struct Target {
        hdxf::DxfVersion version;
        uint32_t format;
        const char* magic;
    };
    const Target targets[] = {{hdxf::DxfVersion::R2013, 21800, "ACIS BinaryFile"},
                              {hdxf::DxfVersion::R2018, 22300, "ASM BinaryFile4"}};
    for (const Target& target : targets) {
        for (const Case& c : all_cases()) {
            std::string how = c.name + " in DXF " + (target.format == 21800 ? "2013" : "2018");
            ParsedSat built = parse_document(c.sat);
            hdxf::Drawing drawing = drawing_of(c.sat, target.version);
            Box in_memory = extents_of(drawing, first_body(drawing));
            require_box(in_memory, c.box, 1e-6, how + " extents");
            for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
                std::string where = how + (format == hdxf::DxfFormat::Ascii ? " (ASCII)" : " (binary)");
                hdxf::Drawing again = reloaded(drawing, format);
                const hdxf::Entity& body = first_body(again);
                const std::vector<uint8_t>& sab = body.as_acis().sab;
                assert_true(sab.size() > 20, where + ": the ACIS data is SAB");
                assert_true(std::string(sab.begin(), sab.begin() + 15) == target.magic, where + ": the SAB's signature");
                assert_eq(static_cast<int>(sab_format(sab)), static_cast<int>(target.format), where + ": the SAB's format");
                assert_true(body.as_acis().lines.empty(), where + ": and the lines are gone");
                require_box(extents_of(again, body), in_memory, 1e-9, where + " extents after reading");

                // The SAB holds the body the SAT did
                hdxf::internal::AcisFile file;
                assert_true(hdxf::internal::read_sab(sab, file), where + ": HDxf reads the SAB");
                require_same_bodies(built.bodies, hdxf::internal::acis_bodies(file), where);

                // Written again from the SAB as it is, read again
                hdxf::Drawing twice = reloaded(again, format);
                assert_true(first_body(twice).as_acis().sab == sab, where + ": the same SAB the second time");
                require_box(extents_of(twice, first_body(twice)), in_memory, 1e-9, where + " extents the second time");
            }
        }
    }
}

TEST(acis_roundtrip_sab_is_made_from_the_sat_without_errors) {
    // The SAT → SAB step that DXF 2013 and 2018 do on their own, run directly: it must not fail on any record
    for (const Case& c : all_cases()) {
        hdxf::AcisData data = acis_data(c.sat);
        std::string sat = hdxf::internal::acis_deciphered(data);
        for (int format : {hdxf::internal::sab_format_acis, hdxf::internal::sab_format_asm}) {
            std::vector<uint8_t> sab;
            std::string error;
            assert_true(hdxf::internal::sab_from_sat(sat, sab, error, format), c.name + ": " + error);
            assert_true(error.empty());
        }
    }
}

TEST(acis_roundtrip_a_drawing_with_several_bodies) {
    hdxf::Drawing drawing(hdxf::DxfVersion::R2010);
    std::vector<Case> cases = all_cases();
    for (const Case& c : cases) drawing.add_acis(entity_type_of(c.sat.kind), acis_data(c.sat));
    hdxf::Drawing again = reloaded(drawing, hdxf::DxfFormat::Ascii);
    std::vector<const hdxf::Entity*> bodies = bodies_of(again);
    assert_eq(static_cast<int>(bodies.size()), static_cast<int>(cases.size()));
    for (size_t i = 0; i < cases.size(); ++i) {
        require_box(extents_of(again, *bodies[i]), cases[i].box, 1e-6, cases[i].name + " in a drawing of many");
    }
}

TEST(acis_roundtrip_body_in_a_block_with_a_scaled_rotated_insert) {
    // The facit's block (script 10_solids.scr): a sphere r = 8 at (0, 0, 8) and a REGION circle r = 5 at
    // (20, 0, 0), inserted at (220, 145, 0) with the scale 1.2 and the rotation 0.3 rad
    acis::SphereParameters sphere;
    sphere.frame = frame_at(0, 0, 8);
    sphere.radius = 8;
    acis::Result<acis::SatDocument> ball = acis::make_sphere(sphere);
    acis::Result<acis::SatDocument> disc = build_circle_region({20, 0, 0}, 5);
    hdxf::Drawing drawing(hdxf::DxfVersion::R2010);
    hdxf::Block& block = drawing.add_block("HDXF_ACIS");
    hdxf::Entity solid;
    solid.type = hdxf::EntityType::Solid3D;
    solid.data = acis_data(require(ball, "sphere"));
    block.entities.push_back(solid);
    hdxf::Entity region;
    region.type = hdxf::EntityType::Region;
    region.data = acis_data(require(disc, "disc"));
    block.entities.push_back(region);
    hdxf::Entity& insert = drawing.add_insert("HDXF_ACIS", {220, 145, 0});
    auto& data = std::get<hdxf::InsertData>(insert.data);
    data.x_scale = data.y_scale = data.z_scale = 1.2;
    data.rotation = 0.3 * 180.0 / 3.14159265358979323846;       // degrees in DXF
    hdxf::Point3 min, max;
    assert_true(drawing.entity_extents(drawing.entities().back(), min, max), "the insert's extents");
    hdxf::Drawing again = reloaded(drawing, hdxf::DxfFormat::Ascii);
    hdxf::Point3 min2, max2;
    assert_true(again.entity_extents(again.entities().back(), min2, max2));
    require_near(min2.x, min.x, 1e-9, "insert min.x");
    require_near(max2.y, max.y, 1e-9, "insert max.y");
    require_near(max2.z, max.z, 1e-9, "insert max.z");
    // The sphere reaches z = 16 in the block, 19.2 scaled
    require_near(max.z, 19.2, 1e-9, "scaled top of the sphere");
}
