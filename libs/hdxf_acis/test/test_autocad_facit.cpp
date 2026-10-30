#include "test_support.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace acis_test;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

// ---------------------------------------------------------------------------
// Against the bodies AutoCAD itself saved (doc/facit/acad_20xx_dxf_solids.dxf, made by the script
// 10_solids.scr): the same shapes through the builder must have the same extents, the same topology
// and the same geometry. Byte identity is not the aim: the builder numbers the records in its own
// order and makes no history attributes. No file of the builder's is a facit.
// ---------------------------------------------------------------------------

namespace {

hdxf::Point3 pt(double x, double y, double z) { return hdxf::Point3{x, y, z}; }

acis::Frame frame_at(double x, double y, double z) {
    acis::Frame frame;
    frame.origin = {x, y, z};
    return frame;
}

const char* surface_name(hdxf::internal::AcisSurfaceType type) {
    switch (type) {
        case hdxf::internal::AcisSurfaceType::Plane: return "plane";
        case hdxf::internal::AcisSurfaceType::Cone: return "cone";
        case hdxf::internal::AcisSurfaceType::Sphere: return "sphere";
        case hdxf::internal::AcisSurfaceType::Torus: return "torus";
        case hdxf::internal::AcisSurfaceType::Spline: return "spline";
        default: return "other";
    }
}

const char* curve_name(hdxf::internal::AcisCurveType type) {
    switch (type) {
        case hdxf::internal::AcisCurveType::Straight: return "straight";
        case hdxf::internal::AcisCurveType::Ellipse: return "ellipse";
        case hdxf::internal::AcisCurveType::Spline: return "spline";
        default: return "other";
    }
}

// What a body is made of, independent of the order of its records: the faces by surface type and loop
// sizes, the edges by curve type
std::string signature(const hdxf::internal::AcisBody& body) {
    std::vector<std::string> faces;
    for (const auto& face : body.faces) {
        std::vector<int> loops;
        for (const auto& loop : face.loops) loops.push_back(static_cast<int>(loop.coedges.size()));
        std::sort(loops.begin(), loops.end());
        std::ostringstream out;
        out << (face.surface >= 0 ? surface_name(body.surfaces[static_cast<size_t>(face.surface)].type) : "none") << ":";
        for (int n : loops) out << n << ",";
        faces.push_back(out.str());
    }
    std::sort(faces.begin(), faces.end());
    std::vector<std::string> edges;
    for (const auto& edge : body.edges) {
        edges.push_back(edge.curve >= 0 ? curve_name(body.curves[static_cast<size_t>(edge.curve)].type) : "degenerate");
    }
    std::sort(edges.begin(), edges.end());
    std::ostringstream out;
    out << "faces[";
    for (const std::string& f : faces) out << f << ";";
    out << "] edges[";
    for (const std::string& e : edges) out << e << ";";
    out << "]";
    return out.str();
}

ParsedSat parse_entity(const hdxf::Entity& entity) {
    ParsedSat parsed;
    std::string text = hdxf::internal::acis_deciphered(entity.as_acis());
    hdxf_test::assert_true(hdxf::internal::read_sat(text, parsed.file), "HDxf cannot read the facit's SAT");
    parsed.bodies = hdxf::internal::acis_bodies(parsed.file);
    return parsed;
}

// The geometry of a body that does not depend on how it is oriented: the radii, half angles, centers
void require_same_geometry(const hdxf::internal::AcisBody& facit, const hdxf::internal::AcisBody& built, const std::string& what) {
    auto sorted_by_type = [](const hdxf::internal::AcisBody& b) {
        std::vector<hdxf::internal::AcisSurface> surfaces = b.surfaces;
        std::sort(surfaces.begin(), surfaces.end(), [](const auto& x, const auto& y) {
            if (x.type != y.type) return static_cast<int>(x.type) < static_cast<int>(y.type);
            return x.radius + x.minor + x.origin.z < y.radius + y.minor + y.origin.z;
        });
        return surfaces;
    };
    std::vector<hdxf::internal::AcisSurface> a = sorted_by_type(facit);
    std::vector<hdxf::internal::AcisSurface> b = sorted_by_type(built);
    assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + ": surfaces");
    for (size_t i = 0; i < a.size(); ++i) {
        assert_true(a[i].type == b[i].type, what + ": surface types");
        switch (a[i].type) {
            case hdxf::internal::AcisSurfaceType::Cone:
                require_near(std::fabs(a[i].sine), std::fabs(b[i].sine), 1e-15, what + " cone sine");
                require_near(a[i].cosine, b[i].cosine, 1e-15, what + " cone cosine");
                require_near(std::sqrt(a[i].major.x * a[i].major.x + a[i].major.y * a[i].major.y + a[i].major.z * a[i].major.z),
                             std::sqrt(b[i].major.x * b[i].major.x + b[i].major.y * b[i].major.y + b[i].major.z * b[i].major.z),
                             1e-12, what + " cone radius");
                require_near(a[i].u_scale, b[i].u_scale, 1e-12, what + " cone u scale");
                break;
            case hdxf::internal::AcisSurfaceType::Sphere:
                require_near(a[i].radius, b[i].radius, 1e-12, what + " sphere radius");
                break;
            case hdxf::internal::AcisSurfaceType::Torus:
                require_near(a[i].radius, b[i].radius, 1e-12, what + " torus major radius");
                require_near(a[i].minor, b[i].minor, 1e-12, what + " torus minor radius");
                break;
            default:
                break;
        }
    }
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) require_near(facit.transform[r][c], built.transform[r][c], 1e-12, what + " transform");
    }
}

struct Built {
    std::string name;
    const hdxf::Entity* facit;
    acis::SatDocument sat;
};

std::vector<Built> built_primitives(const FacitPrimitives& f) {
    std::vector<Built> out;
    acis::BoxParameters box;
    box.frame = frame_at(10, 10, 10);
    box.size_x = 20;
    box.size_y = 15;
    box.size_z = 20;
    out.push_back({"box", f.box, require(acis::make_box(box), "box")});
    acis::SphereParameters sphere;
    sphere.frame = frame_at(50, 10, 10);
    sphere.radius = 12;
    out.push_back({"sphere", f.sphere, require(acis::make_sphere(sphere), "sphere")});
    acis::CylinderParameters cylinder;
    cylinder.frame = frame_at(90, 10, 12.5);
    cylinder.radius = 10;
    cylinder.height = 25;
    out.push_back({"cylinder", f.cylinder, require(acis::make_cylinder(cylinder), "cylinder")});
    acis::ConeParameters cone;
    cone.frame = frame_at(130, 10, 15);
    cone.radius = 12;
    cone.height = 30;
    out.push_back({"cone", f.cone, require(acis::make_cone(cone), "cone")});
    acis::TorusParameters torus;
    torus.frame = frame_at(170, 10, 12);
    torus.major_radius = 15;
    torus.minor_radius = 4;
    out.push_back({"torus", f.torus, require(acis::make_torus(torus), "torus")});
    out.push_back({"rectangle region", f.region_rectangle, require(build_rectangle_region(0, 60, 30, 80), "rectangle")});
    out.push_back({"circle region", f.region_circle, require(build_circle_region(pt(170, 145, 0), 15), "circle")});
    return out;
}

FacitPrimitives require_primitives(const hdxf::Drawing& facit) {
    FacitPrimitives f = find_facit_primitives(facit);
    assert_true(f.box && f.sphere && f.cylinder && f.cone && f.torus && f.region_rectangle && f.region_circle,
                "the facit has the box, sphere, cylinder, cone, torus and the two regions of 10_solids.scr");
    return f;
}

} // namespace

TEST(acis_facit_primitives_have_the_extents_of_autocads_in_2010_and_2018) {
    for (const char* name : {"acad_2010_dxf_solids.dxf", "acad_2018_dxf_solids.dxf"}) {
        hdxf::Drawing facit = load_facit(name);
        FacitPrimitives f = require_primitives(facit);
        hdxf::DxfVersion version = std::string(name).find("2018") != std::string::npos ? hdxf::DxfVersion::R2018
                                                                                       : hdxf::DxfVersion::R2010;
        for (const Built& b : built_primitives(f)) {
            Box autocad = extents_of(facit, *b.facit);
            hdxf::Drawing drawing = drawing_of(b.sat, version);
            Box ours = extents_of(drawing, first_body(drawing));
            require_box(ours, autocad, 1e-9, std::string(name) + " " + b.name);
            hdxf::Drawing again = reloaded(drawing, hdxf::DxfFormat::Ascii);
            require_box(extents_of(again, first_body(again)), autocad, 1e-9, std::string(name) + " " + b.name + " after reading");
        }
    }
}

TEST(acis_facit_primitives_have_the_topology_and_geometry_of_autocads) {
    hdxf::Drawing facit = load_facit("acad_2010_dxf_solids.dxf");
    FacitPrimitives f = require_primitives(facit);
    for (const Built& b : built_primitives(f)) {
        ParsedSat autocad = parse_entity(*b.facit);
        ParsedSat ours = parse_document(b.sat);
        assert_eq(static_cast<int>(autocad.bodies.size()), 1, b.name);
        assert_eq(static_cast<int>(ours.bodies.size()), 1, b.name);
        assert_eq(signature(ours.bodies[0]), signature(autocad.bodies[0]), b.name + ": faces and edges");
        require_same_geometry(autocad.bodies[0], ours.bodies[0], b.name);
        // The same records but for AutoCAD's history attributes
        for (const char* type : {"face", "loop", "coedge", "edge", "vertex", "point", "plane-surface", "cone-surface",
                                 "sphere-surface", "torus-surface", "straight-curve", "ellipse-curve", "transform"}) {
            assert_eq(count_records(ours.file, type), count_records(autocad.file, type), b.name + ": " + type);
        }
    }
}

TEST(acis_facit_header_is_in_autocads_form) {
    hdxf::Drawing facit = load_facit("acad_2010_dxf_solids.dxf");
    FacitPrimitives f = require_primitives(facit);
    std::string autocad = hdxf::internal::acis_deciphered(f.sphere->as_acis());
    std::vector<std::string> theirs;
    std::istringstream in(autocad);
    for (std::string line; std::getline(in, line);) theirs.push_back(line);

    acis::SphereParameters p;
    p.frame = frame_at(50, 10, 10);
    p.radius = 12;
    acis::Result<acis::SatDocument> sat = acis::make_sphere(p);
    const std::vector<std::string>& ours = require(sat, "sphere").lines;

    // Line 1: the SAT version, the record count, the entity count (the asmheader and the body: 2) and
    // the flags (24), the whole line 23 characters
    assert_eq(static_cast<int>(theirs[0].size()), 23);
    assert_eq(static_cast<int>(ours[0].size()), 23);
    std::istringstream a(theirs[0]);
    std::istringstream b(ours[0]);
    int version_a, records_a, entities_a, flags_a, version_b, records_b, entities_b, flags_b;
    a >> version_a >> records_a >> entities_a >> flags_a;
    b >> version_b >> records_b >> entities_b >> flags_b;
    assert_eq(version_a, version_b);
    assert_eq(entities_a, entities_b);
    assert_eq(flags_a, flags_b);
    // AutoCAD's count includes the face's history attribute, which the builder does not make
    assert_eq(records_a, 8);
    assert_eq(records_b, 7);
    // Line 2: the producer, the same string
    assert_eq(ours[1], theirs[1]);
    // Line 3: the unit, resabs and resnor. AutoCAD's 25.4 is the imperial template's; the builder's
    // coordinates are millimetres.
    std::istringstream c(theirs[2]);
    double unit_a, resabs_a, resnor_a;
    c >> unit_a >> resabs_a >> resnor_a;
    require_near(unit_a, 25.4, 1e-12, "AutoCAD's unit in the facit");
    require_near(resabs_a, 1e-6, 1e-20, "AutoCAD's resabs");
    require_near(resnor_a, 1e-10, 1e-24, "AutoCAD's resnor");
    std::istringstream d(ours[2]);
    double unit_b, resabs_b, resnor_b;
    d >> unit_b >> resabs_b >> resnor_b;
    require_near(unit_b, 1.0, 0.0, "the builder's unit");
    require_near(resabs_b, resabs_a, 0.0, "resabs");
    require_near(resnor_b, resnor_a, 0.0, "resnor");
    // The asmheader record
    assert_eq(ours[3], "asmheader $-1 -1 @13 230.1.0.65535 #");
    assert_true(theirs[3] == ours[3], theirs[3]);
    // Neither has an end marker
    assert_true(autocad.find("End-of") == std::string::npos);
}

TEST(acis_facit_the_header_unit_does_not_scale_the_extents) {
    // AutoCAD's core reads the coordinates as the drawing's units whatever the header says (measured in
    // doc/reference/hdxf_acis_sat_21500.md), and so does HDxf's reader: the unit is only written
    hdxf::Drawing facit = load_facit("acad_2010_dxf_solids.dxf");
    FacitPrimitives f = require_primitives(facit);
    Box original = extents_of(facit, *f.sphere);
    hdxf::AcisData data = f.sphere->as_acis();
    // Line 3 enciphered, with the unit replaced
    std::string plain = "1 9.999999999999999547e-07 1.000000000000000036e-10 ";
    for (char& ch : plain) {
        unsigned char u = static_cast<unsigned char>(ch);
        if (u > 32 && u < 159) ch = static_cast<char>(159 - u);
    }
    data.lines[2] = plain;
    hdxf::Drawing drawing(hdxf::DxfVersion::R2010);
    drawing.add_acis(hdxf::EntityType::Solid3D, data);
    require_box(extents_of(drawing, first_body(drawing)), original, 1e-9, "sphere with the unit 1");
}

TEST(acis_facit_rotated_box_has_the_transform_of_autocads) {
    hdxf::Drawing plain = load_facit("acad_2010_dxf_solids.dxf");
    hdxf::Drawing rotated = load_facit("acad_2010_dxf_solids_solid_rotated.dxf");
    FacitPrimitives f = require_primitives(plain);
    std::vector<const hdxf::Entity*> before = bodies_of(plain);
    std::vector<const hdxf::Entity*> after = bodies_of(rotated);
    assert_eq(static_cast<int>(before.size()), static_cast<int>(after.size()));
    size_t index = 0;
    while (index < before.size() && before[index] != f.box) ++index;
    assert_true(index < before.size());
    const hdxf::Entity& box = *after[index];

    // The frame is AutoCAD's transform: the images of the local axes and the translation
    ParsedSat autocad = parse_entity(box);
    const auto& m = autocad.bodies[0].transform;
    acis::BoxParameters p;
    p.size_x = 20;
    p.size_y = 15;
    p.size_z = 20;
    p.frame.origin = {m[0][3], m[1][3], m[2][3]};
    p.frame.x_axis = {m[0][0], m[1][0], m[2][0]};
    p.frame.z_axis = {m[0][2], m[1][2], m[2][2]};
    acis::Result<acis::SatDocument> sat = acis::make_box(p);
    acis::SatDocument& document = require(sat, "rotated box");
    ParsedSat ours = parse_document(document);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 4; ++c) {
            require_near(ours.bodies[0].transform[r][c], m[r][c], 1e-15, "transform element");
        }
    }
    hdxf::Drawing drawing = drawing_of(document, hdxf::DxfVersion::R2010);
    require_box(extents_of(drawing, first_body(drawing)), extents_of(rotated, box), 1e-9, "rotated box extents");
    // AutoCAD says "rotate", and so do we
    assert_true(hdxf::internal::acis_deciphered(box.as_acis()).find(" rotate no_reflect no_shear #") != std::string::npos);
    assert_true(document.text().find(" rotate no_reflect no_shear #") != std::string::npos);
}

TEST(acis_facit_two_lumps_body) {
    hdxf::Drawing facit = load_facit("acad_2010_dxf_solids.dxf");
    const hdxf::Entity* two = nullptr;
    for (const hdxf::Entity* e : bodies_of(facit)) {
        if (count_records(parse_entity(*e).file, "lump") == 2) two = e;
    }
    assert_true(two != nullptr, "the facit has one solid of two lumps");
    // The script: BOX (80,130,0)-(100,150,20) and (115,130,0)-(135,150,20), united
    acis::Result<acis::SatDocument> sat = build_two_lumps(pt(80, 130, 0));
    acis::SatDocument& document = require(sat, "two lumps");
    ParsedSat autocad = parse_entity(*two);
    ParsedSat ours = parse_document(document);
    assert_eq(count_records(ours.file, "lump"), 2);
    assert_eq(count_records(ours.file, "shell"), count_records(autocad.file, "shell"));
    assert_eq(count_records(ours.file, "face"), count_records(autocad.file, "face"));
    assert_eq(count_records(ours.file, "coedge"), count_records(autocad.file, "coedge"));
    hdxf::Drawing drawing = drawing_of(document, hdxf::DxfVersion::R2010);
    require_box(extents_of(drawing, first_body(drawing)), extents_of(facit, *two), 1e-9, "two lumps extents");
}

TEST(acis_facit_body_in_a_block_inserted_scaled_and_rotated) {
    hdxf::Drawing facit = load_facit("acad_2010_dxf_solids.dxf");
    const hdxf::Entity* facit_insert = nullptr;
    for (const hdxf::Entity& e : facit.entities()) {
        if (e.type == hdxf::EntityType::Insert && e.as_insert().block_name == "HDXF_ACIS") facit_insert = &e;
    }
    assert_true(facit_insert != nullptr, "the facit's block insert");

    acis::SphereParameters sphere;
    sphere.frame = frame_at(0, 0, 8);
    sphere.radius = 8;
    acis::Result<acis::SatDocument> ball = acis::make_sphere(sphere);
    acis::Result<acis::SatDocument> disc = build_circle_region(pt(20, 0, 0), 5);
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
    hdxf::Entity& insert = drawing.add_insert("HDXF_ACIS", facit_insert->as_insert().insertion_point);
    hdxf::InsertData& data = insert.as_insert();
    data.x_scale = facit_insert->as_insert().x_scale;
    data.y_scale = facit_insert->as_insert().y_scale;
    data.z_scale = facit_insert->as_insert().z_scale;
    data.rotation = facit_insert->as_insert().rotation;
    Box autocad = extents_of(facit, *facit_insert);
    Box ours = extents_of(drawing, drawing.entities().back());
    require_box(ours, autocad, 1e-9, "the transformed block");
}

TEST(acis_facit_pcurves_have_the_parameter_of_the_edges_curve) {
    // The builder's pcurves are taken in the edge's own curve parameter, a reversed coedge's too. AutoCAD's
    // loft in the facit is a body that has pcurves; the reader's evaluation of its pcurves on its surface
    // must meet the edges' curves with that convention.
    hdxf::Drawing facit = load_facit("acad_2010_dxf_solids.dxf");
    int bodies_with_pcurves = 0;
    int coedges = 0;
    int reversed = 0;
    for (const hdxf::Entity* e : bodies_of(facit)) {
        ParsedSat parsed = parse_entity(*e);
        if (count_records(parsed.file, "pcurve") == 0) continue;
        ++bodies_with_pcurves;
        for (size_t i = 0; i < parsed.file.records.size(); ++i) {
            const auto& r = parsed.file.records[i];
            if (r.type != "coedge") continue;
            double deviation = pcurve_deviation(parsed.file, i);
            if (deviation < 0.0) continue;
            ++coedges;
            if (r.tokens[7].type == hdxf::internal::AcisTokenType::Word && r.tokens[7].text == "reversed") ++reversed;
            // The loft's spline surface is AutoCAD's approximation of a procedural surface (a fit tolerance
            // in the file), the pcurves are exact on the surface itself
            assert_true(deviation < 1e-3, "the facit's pcurve of record " + std::to_string(i) + " is " +
                                              std::to_string(deviation) + " from the edge's curve");
        }
    }
    assert_true(bodies_with_pcurves >= 1, "a facit body with pcurves");
    assert_true(coedges >= 4, "the loft's coedges with pcurves");
    assert_true(reversed >= 1, "and a reversed coedge among them");
}

// ---------------------------------------------------------------------------
// The bodies of 26_acis_referens.scr (doc/facit/acad_20xx_dxf_acis_reference.dxf, saved by AutoCAD 2025):
// a block with a hole through it (SUBTRACT), a solid with a cavity (SUBTRACT of an inner box: AutoCAD's
// void shell) and a sphere cut through its center (SLICE: a sphere face with one boundary loop and no
// seam or poles). The script made no cylinder cut through its axis and no loft: a seam on a cylinder and
// a NURBS solid in SAT 21500 are still without a facit.
// ---------------------------------------------------------------------------

namespace {

ParsedSat parse_entity_any(const hdxf::Entity& entity) {
    if (!entity.as_acis().lines.empty()) return parse_entity(entity);
    ParsedSat parsed;
    hdxf_test::assert_true(hdxf::internal::read_sab(entity.as_acis().sab, parsed.file), "HDxf cannot read the facit's SAB");
    parsed.bodies = hdxf::internal::acis_bodies(parsed.file);
    return parsed;
}

struct ReferenceBodies {
    const hdxf::Entity* hole = nullptr;
    const hdxf::Entity* cavity = nullptr;
    const hdxf::Entity* hemisphere = nullptr;
};

ReferenceBodies find_reference_bodies(const hdxf::Drawing& facit) {
    ReferenceBodies found;
    int count = 0;
    for (const hdxf::Entity* e : bodies_of(facit)) {
        ++count;
        ParsedSat parsed = parse_entity_any(*e);
        if (count_records(parsed.file, "cone-surface") == 1) found.hole = e;
        else if (count_records(parsed.file, "sphere-surface") == 1) found.hemisphere = e;
        else if (count_records(parsed.file, "shell") == 2) found.cavity = e;
    }
    assert_eq(count, 3, "26_acis_referens.scr makes three solids");
    assert_true(found.hole && found.cavity && found.hemisphere, "the hole block, the cavity solid and the hemisphere");
    return found;
}

// The records of a body but AutoCAD's history attributes
void require_same_records(const ParsedSat& autocad, const ParsedSat& ours, const std::string& what) {
    for (const char* type : {"body", "lump", "shell", "face", "loop", "coedge", "edge", "vertex", "point", "plane-surface",
                             "cone-surface", "sphere-surface", "torus-surface", "straight-curve", "ellipse-curve", "transform"}) {
        assert_eq(count_records(ours.file, type), count_records(autocad.file, type), what + ": " + type);
    }
}

} // namespace

TEST(acis_facit_reference_bodies_have_the_topology_and_geometry_of_autocads) {
    for (const char* name : {"acad_2010_dxf_acis_reference.dxf", "acad_2018_dxf_acis_reference.dxf"}) {
        hdxf::Drawing facit = load_facit(name);
        hdxf::DxfVersion version = std::string(name).find("2018") != std::string::npos ? hdxf::DxfVersion::R2018
                                                                                       : hdxf::DxfVersion::R2010;
        ReferenceBodies found = find_reference_bodies(facit);
        // The script: BOX 0,0,0 30,30 24 minus a cylinder r 6 at (15,15); BOX 100,0,0 140,40 40 minus BOX
        // 115,15,15 125,25 10; SPHERE 200,20,20 r 12 cut by SLICE through its center, the upper half kept
        struct Pair {
            std::string name;
            const hdxf::Entity* autocad;
            acis::Result<acis::SatDocument> sat;
        };
        std::vector<Pair> pairs;
        pairs.push_back({"hole block", found.hole, build_hole_block(true, true)});
        pairs.push_back({"cavity solid", found.cavity, build_cavity_cube(true, pt(120, 20, 20))});
        pairs.push_back({"hemisphere", found.hemisphere, build_hemisphere(12, pt(200, 20, 20))});
        for (Pair& pair : pairs) {
            std::string what = std::string(name) + " " + pair.name;
            acis::SatDocument& document = require(pair.sat, what);
            ParsedSat autocad = parse_entity_any(*pair.autocad);
            ParsedSat ours = parse_document(document);
            assert_eq(static_cast<int>(autocad.bodies.size()), 1, what);
            assert_eq(static_cast<int>(ours.bodies.size()), 1, what);
            assert_eq(signature(ours.bodies[0]), signature(autocad.bodies[0]), what + ": faces and edges");
            require_same_geometry(autocad.bodies[0], ours.bodies[0], what);
            require_same_records(autocad, ours, what);
            hdxf::Drawing drawing = drawing_of(document, version);
            require_box(extents_of(drawing, first_body(drawing)), extents_of(facit, *pair.autocad), 1e-9, what + " extents");
        }
    }
}

TEST(acis_facit_void_shell_is_a_second_shell_of_the_lump_with_faces_turned_into_the_cavity) {
    hdxf::Drawing facit = load_facit("acad_2010_dxf_acis_reference.dxf");
    ReferenceBodies found = find_reference_bodies(facit);
    acis::Result<acis::SatDocument> sat = build_cavity_cube(true, pt(120, 20, 20));
    for (const ParsedSat& parsed : {parse_entity_any(*found.cavity), parse_document(require(sat, "cavity"))}) {
        assert_eq(count_records(parsed.file, "lump"), 1);
        assert_eq(count_records(parsed.file, "shell"), 2);
        const auto& body = parsed.bodies[0];
        // Six faces in each shell; a face's normal (the plane's turned by the face's sense) points out of the
        // material, so the cavity's face on the plane at +5 looks down and the outer at +20 up
        int outer_up = 0;
        int void_down = 0;
        for (const auto& face : body.faces) {
            const auto& plane = body.surfaces[static_cast<size_t>(face.surface)];
            if (plane.type != hdxf::internal::AcisSurfaceType::Plane) continue;
            double normal_z = plane.axis.z * (face.reversed ? -1.0 : 1.0);
            double height = plane.origin.z;
            if (std::fabs(height - 20.0) < 1e-9 && normal_z > 0.5) ++outer_up;
            if (std::fabs(height - 5.0) < 1e-9 && normal_z < -0.5) ++void_down;
        }
        assert_eq(outer_up, 1);
        assert_eq(void_down, 1);
    }
}

TEST(acis_facit_hemisphere_has_no_seam_and_no_pole) {
    hdxf::Drawing facit = load_facit("acad_2010_dxf_acis_reference.dxf");
    ReferenceBodies found = find_reference_bodies(facit);
    acis::Result<acis::SatDocument> sat = build_hemisphere(12, pt(200, 20, 20));
    for (const ParsedSat& parsed : {parse_entity_any(*found.hemisphere), parse_document(require(sat, "hemisphere"))}) {
        // Two faces, two loops of one coedge each, one edge (a closed circle) and one vertex: the sphere's seam and
        // pole are not edges or vertices
        assert_eq(count_records(parsed.file, "face"), 2);
        assert_eq(count_records(parsed.file, "loop"), 2);
        assert_eq(count_records(parsed.file, "coedge"), 2);
        assert_eq(count_records(parsed.file, "edge"), 1);
        assert_eq(count_records(parsed.file, "vertex"), 1);
        assert_eq(count_records(parsed.file, "ellipse-curve"), 1);
    }
}
