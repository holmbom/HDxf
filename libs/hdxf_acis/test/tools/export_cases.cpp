// hdxf_acis_export_cases <output directory>
//
// Writes the bodies the builder makes, and AutoCAD's own bodies of the same shapes (cut out of the facit,
// one body to a drawing), as DXF 2010 and 2018 files with one body each, and a manifest (cases.tsv)
// with the values the shapes must have: volume (area of a region), surface area, centroid. The files
// are for AutoCAD's core (tools/acad_measure.py runs MASSPROP and AREA on them): its measurement of the
// builder's body next to its measurement of AutoCAD's own is the check that the builder wrote the same
// solid. The analytic values are the third reference.

#include "test_support.hpp"

#include <fstream>
#include <iostream>

using namespace acis_test;

namespace {

const double kPiValue = 3.14159265358979323846;

struct Expected {
    double volume = 0.0;                // a region: its area
    double area = 0.0;                  // a solid's surface area; a region: its perimeter
    hdxf::Point3 centroid;
};

struct Row {
    std::string name;
    std::string source;
    std::string kind;
    std::string file_2010;
    std::string file_2018;
    Expected expected;
};

bool write_one(const hdxf::AcisData& data, hdxf::EntityType type, hdxf::DxfVersion version, const std::filesystem::path& path) {
    hdxf::Drawing drawing(version);
    drawing.add_acis(type, data);
    if (!drawing.save_as(path)) {
        std::cerr << path.string() << ": " << drawing.last_error() << "\n";
        return false;
    }
    return true;
}

acis::Frame frame_at(double x, double y, double z) {
    acis::Frame frame;
    frame.origin = {x, y, z};
    return frame;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: hdxf_acis_export_cases <output directory>\n";
        return 2;
    }
    std::filesystem::path out = argv[1];
    std::filesystem::create_directories(out);
    std::vector<Row> rows;
    int failures = 0;

    auto add_built = [&](const std::string& name, acis::Result<acis::SatDocument> sat, const Expected& expected) {
        if (!sat) {
            std::cerr << name << ": " << errors_text(sat.errors()) << "\n";
            ++failures;
            return;
        }
        Row row;
        row.name = name;
        row.source = "built";
        switch (sat.value().kind) {
            case acis::BodyKind::Solid: row.kind = "solid"; break;
            case acis::BodyKind::Region: row.kind = "region"; break;
            default: row.kind = "body"; break;
        }
        row.file_2010 = "built_" + name + "_2010.dxf";
        row.file_2018 = "built_" + name + "_2018.dxf";
        row.expected = expected;
        hdxf::AcisData data = acis_data(sat.value());
        hdxf::EntityType type = entity_type_of(sat.value().kind);
        if (!write_one(data, type, hdxf::DxfVersion::R2010, out / row.file_2010) ||
            !write_one(data, type, hdxf::DxfVersion::R2018, out / row.file_2018)) {
            ++failures;
        }
        rows.push_back(row);
    };

    // The shapes of 10_solids.scr, as AutoCAD's own are
    acis::BoxParameters box;
    box.frame = frame_at(10, 10, 10);
    box.size_x = 20;
    box.size_y = 15;
    box.size_z = 20;
    add_built("box", acis::make_box(box), {6000.0, 2000.0, {10, 10, 10}});

    acis::SphereParameters sphere;
    sphere.frame = frame_at(50, 10, 10);
    sphere.radius = 12;
    add_built("sphere", acis::make_sphere(sphere), {2304.0 * kPiValue, 576.0 * kPiValue, {50, 10, 10}});

    acis::CylinderParameters cylinder;
    cylinder.frame = frame_at(90, 10, 12.5);
    cylinder.radius = 10;
    cylinder.height = 25;
    add_built("cylinder", acis::make_cylinder(cylinder), {2500.0 * kPiValue, 700.0 * kPiValue, {90, 10, 12.5}});

    acis::ConeParameters cone;
    cone.frame = frame_at(130, 10, 15);
    cone.radius = 12;
    cone.height = 30;
    add_built("cone", acis::make_cone(cone),
              {1440.0 * kPiValue, 12.0 * kPiValue * (12.0 + std::sqrt(1044.0)), {130, 10, 7.5}});

    acis::TorusParameters torus;
    torus.frame = frame_at(170, 10, 12);
    torus.major_radius = 15;
    torus.minor_radius = 4;
    add_built("torus", acis::make_torus(torus), {480.0 * kPiValue * kPiValue, 240.0 * kPiValue * kPiValue, {170, 10, 12}});

    add_built("rectangle_region", build_rectangle_region(0, 60, 30, 80), {600.0, 100.0, {15, 70, 0}});
    add_built("circle_region", build_circle_region({170, 145, 0}, 15), {225.0 * kPiValue, 30.0 * kPiValue, {170, 145, 0}});

    // Frustum around the origin: r 12 at z = -10, 6 at z = +10
    acis::ConeParameters frustum;
    frustum.radius = 12;
    frustum.top_radius = 6;
    frustum.height = 20;
    {
        double r = 12, t = 6, h = 20;
        double volume = kPiValue * h / 3.0 * (r * r + r * t + t * t);
        double slant = std::sqrt(h * h + (r - t) * (r - t));
        double area = kPiValue * (r * r + t * t) + kPiValue * (r + t) * slant;
        double z_from_base = h * (r * r + 2 * r * t + 3 * t * t) / (4.0 * (r * r + r * t + t * t));
        add_built("frustum", acis::make_cone(frustum), {volume, area, {0, 0, -10 + z_from_base}});
    }

    add_built("hole_block", build_hole_block(),
              {21600.0 - 864.0 * kPiValue, 4680.0 + 216.0 * kPiValue, {15, 15, 12}});
    add_built("two_lumps", build_two_lumps(), {16000.0, 4800.0, {27.5, 10, 10}});
    add_built("cavity_cube", build_cavity_cube(), {63000.0, 10200.0, {20, 20, 20}});
    // The bodies of 26_acis_referens.scr, built as AutoCAD saves them (the geometry around the center)
    add_built("hole_block_local", build_hole_block(true, true),
              {21600.0 - 864.0 * kPiValue, 4680.0 + 216.0 * kPiValue, {15, 15, 12}});
    add_built("cavity_solid", build_cavity_cube(true, {120, 20, 20}), {63000.0, 10200.0, {120, 20, 20}});
    add_built("hemisphere", build_hemisphere(12, {200, 20, 20}),
              {1152.0 * kPiValue, 432.0 * kPiValue, {200, 20, 20 + 4.5}});
    add_built("nurbs_cylinder", build_nurbs_cylinder(10, 25), {2500.0 * kPiValue, 700.0 * kPiValue, {0, 0, 12.5}});
    add_built("seam_cylinder", build_seam_cylinder(10, 25, false), {2500.0 * kPiValue, 700.0 * kPiValue, {0, 0, 0}});
    add_built("seam_cylinder_pcurves", build_seam_cylinder(10, 25, true), {2500.0 * kPiValue, 700.0 * kPiValue, {0, 0, 0}});

    // BODY entities: MASSPROP does not cover them; the core must read them as a BODY
    {
        acis::BodyBuilder closed;
        BoxPlacement place;
        place.kind = acis::BodyKind::ClosedBody;
        BoxModel closed_box = build_box(closed, 10, 20, 30, BoxFlags{}, place);
        add_built("closed_body", closed.build_sat(closed_box.body), {0.0, 0.0, {0, 0, 0}});
        acis::BodyBuilder open;
        place.kind = acis::BodyKind::OpenBody;
        BoxFlags no_top;
        no_top.omit_face = 5;
        BoxModel open_box = build_box(open, 10, 20, 30, no_top, place);
        add_built("open_body", open.build_sat(open_box.body), {0.0, 0.0, {0, 0, 0}});
    }

    // AutoCAD's own bodies of the same shapes, from the facit, one to a drawing
    for (const char* name : {"acad_2010_dxf_solids.dxf", "acad_2018_dxf_solids.dxf"}) {
        std::filesystem::path path = facit_path(name);
        if (!std::filesystem::exists(path)) {
            std::cerr << "no facit " << path.string() << " (the AutoCAD bodies are left out)\n";
            continue;
        }
        hdxf::Drawing facit;
        if (!facit.load(path)) {
            std::cerr << name << ": " << facit.last_error() << "\n";
            ++failures;
            continue;
        }
        FacitPrimitives f = find_facit_primitives(facit);
        const std::pair<const char*, const hdxf::Entity*> found[] = {
            {"box", f.box},           {"sphere", f.sphere},   {"cylinder", f.cylinder},
            {"cone", f.cone},         {"torus", f.torus},     {"rectangle_region", f.region_rectangle},
            {"circle_region", f.region_circle}};
        bool is_2018 = std::string(name).find("2018") != std::string::npos;
        for (const auto& [shape, entity] : found) {
            if (!entity) {
                std::cerr << name << ": no " << shape << "\n";
                ++failures;
                continue;
            }
            std::string file = std::string("facit_") + shape + (is_2018 ? "_2018.dxf" : "_2010.dxf");
            hdxf::DxfVersion version = is_2018 ? hdxf::DxfVersion::R2018 : hdxf::DxfVersion::R2010;
            if (!write_one(entity->as_acis(), entity->type, version, out / file)) ++failures;
            for (Row& row : rows) {
                if (row.name == shape && row.source == "built") {
                    Row copy = row;
                    copy.source = "facit";
                    copy.file_2010 = std::string("facit_") + shape + "_2010.dxf";
                    copy.file_2018 = std::string("facit_") + shape + "_2018.dxf";
                    bool known = false;
                    for (const Row& r : rows) known = known || (r.name == shape && r.source == "facit");
                    if (!known) {
                        rows.push_back(copy);
                    }
                    break;
                }
            }
        }
    }

    // The three bodies of 26_acis_referens.scr, in the order the script made them
    for (const char* version : {"2010", "2018"}) {
        std::filesystem::path path = facit_path(std::string("acad_") + version + "_dxf_acis_reference.dxf");
        if (!std::filesystem::exists(path)) {
            std::cerr << "no facit " << path.string() << "\n";
            continue;
        }
        hdxf::Drawing facit;
        if (!facit.load(path)) {
            std::cerr << path.string() << ": " << facit.last_error() << "\n";
            ++failures;
            continue;
        }
        std::vector<const hdxf::Entity*> found = bodies_of(facit);
        const char* names[3] = {"hole_block_local", "cavity_solid", "hemisphere"};
        bool is_2018 = std::string(version) == "2018";
        hdxf::DxfVersion v = is_2018 ? hdxf::DxfVersion::R2018 : hdxf::DxfVersion::R2010;
        for (size_t i = 0; i < 3 && i < found.size(); ++i) {
            std::string file = std::string("facit_") + names[i] + "_" + version + ".dxf";
            if (!write_one(found[i]->as_acis(), found[i]->type, v, out / file)) ++failures;
            if (is_2018) continue;
            for (Row& row : rows) {
                if (row.name == names[i] && row.source == "built") {
                    Row copy = row;
                    copy.source = "facit";
                    copy.file_2010 = std::string("facit_") + names[i] + "_2010.dxf";
                    copy.file_2018 = std::string("facit_") + names[i] + "_2018.dxf";
                    rows.push_back(copy);
                    break;
                }
            }
        }
    }

    // The box AutoCAD rotated (the second facit of 10_solids.scr): its transform is the frame of ours
    for (const char* version : {"2010", "2018"}) {
        std::filesystem::path plain_path = facit_path(std::string("acad_") + version + "_dxf_solids.dxf");
        std::filesystem::path rotated_path = facit_path(std::string("acad_") + version + "_dxf_solids_solid_rotated.dxf");
        if (!std::filesystem::exists(plain_path) || !std::filesystem::exists(rotated_path)) continue;
        hdxf::Drawing plain;
        hdxf::Drawing rotated;
        if (!plain.load(plain_path) || !rotated.load(rotated_path)) {
            ++failures;
            continue;
        }
        FacitPrimitives f = find_facit_primitives(plain);
        std::vector<const hdxf::Entity*> before = bodies_of(plain);
        std::vector<const hdxf::Entity*> after = bodies_of(rotated);
        size_t index = 0;
        while (index < before.size() && before[index] != f.box) ++index;
        if (index >= before.size() || index >= after.size()) {
            ++failures;
            continue;
        }
        const hdxf::Entity& box_entity = *after[index];
        bool is_2018 = std::string(version) == "2018";
        hdxf::DxfVersion v = is_2018 ? hdxf::DxfVersion::R2018 : hdxf::DxfVersion::R2010;
        std::string file = std::string("facit_rotated_box_") + version + ".dxf";
        if (!write_one(box_entity.as_acis(), box_entity.type, v, out / file)) ++failures;
        // The transform of AutoCAD's body is read from its SAT (2010) or its SAB (2018) by HDxf
        hdxf::internal::AcisFile file_data;
        bool read = false;
        if (!box_entity.as_acis().lines.empty()) {
            read = hdxf::internal::read_sat(hdxf::internal::acis_deciphered(box_entity.as_acis()), file_data);
        } else {
            read = hdxf::internal::read_sab(box_entity.as_acis().sab, file_data);
        }
        std::vector<hdxf::internal::AcisBody> bodies = read ? hdxf::internal::acis_bodies(file_data) : std::vector<hdxf::internal::AcisBody>{};
        if (bodies.empty()) {
            ++failures;
            continue;
        }
        const auto& m = bodies[0].transform;
        acis::BoxParameters p;
        p.size_x = 20;
        p.size_y = 15;
        p.size_z = 20;
        p.frame.origin = {m[0][3], m[1][3], m[2][3]};
        p.frame.x_axis = {m[0][0], m[1][0], m[2][0]};
        p.frame.z_axis = {m[0][2], m[1][2], m[2][2]};
        if (!is_2018) {
            add_built("rotated_box", acis::make_box(p), {6000.0, 2000.0, p.frame.origin});
            for (Row& row : rows) {
                if (row.name == "rotated_box" && row.source == "built") {
                    Row copy = row;
                    copy.source = "facit";
                    copy.file_2010 = "facit_rotated_box_2010.dxf";
                    copy.file_2018 = "facit_rotated_box_2018.dxf";
                    rows.push_back(copy);
                    break;
                }
            }
        }
    }

    std::ofstream manifest(out / "cases.tsv");
    manifest.precision(17);
    manifest << "name\tsource\tkind\tfile_2010\tfile_2018\tvolume\tarea\tcx\tcy\tcz\n";
    for (const Row& row : rows) {
        manifest << row.name << "\t" << row.source << "\t" << row.kind << "\t" << row.file_2010 << "\t" << row.file_2018 << "\t"
                 << row.expected.volume << "\t" << row.expected.area << "\t" << row.expected.centroid.x << "\t"
                 << row.expected.centroid.y << "\t" << row.expected.centroid.z << "\n";
    }
    std::cout << rows.size() << " cases in " << out.string() << (failures ? " (with failures)" : "") << "\n";
    return failures ? 1 : 0;
}
