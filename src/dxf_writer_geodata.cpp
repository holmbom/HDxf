#include "dxf_writer.hpp"
#include "text_layout.hpp"

// GEODATA as AutoCAD writes it (plan phase 4, B50): version 2 as in AutoCAD's 2010 files
// (LibreDWG's gh209_1), version 3 with its coordinate system in 303 parts of 250 characters and
// the last in 301 (ACadSharp's geoloc.dxf and the other 2018 files), version 1 as AutoCAD writes
// it in 2007 files (ACadSharp's geoloc_AC1021.dxf: the reference point's y, x, z in 40-42, the
// 2010 values in its CIVIL3D_DATA groups).

namespace hdxf {
namespace internal {

namespace {

std::string utf8_of(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        uint32_t cp = static_cast<uint32_t>(c);
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return out;
}

} // namespace

void DxfWriter::write_geodata(const GeoDataData& d) {
    auto xy = [&](int code, const Point2& p) {
        groups_.write_double(code, p.x);
        groups_.write_double(code + 10, p.y);
    };
    std::string host = d.host_block_handle;
    if (host.empty() || (filter_live_ && !is_live(host))) host = record_handle("*Model_Space");
    write_code(100, "AcDbGeoData");
    groups_.write_int(90, d.version);
    if (d.version == 1) {
        groups_.write_int(70, d.coordinate_type);
        write_code(330, host);
        groups_.write_double(40, d.reference_point.y);
        groups_.write_double(41, d.reference_point.x);
        groups_.write_double(42, d.reference_point.z);
        groups_.write_int(91, d.horizontal_units);
        write_point(10, d.design_point);
        write_point(11, d.obsolete_point);
        write_point(210, d.up_direction);
        groups_.write_double(52, d.north_angle);
        groups_.write_double(43, d.obsolete_scale.x);
        groups_.write_double(44, d.obsolete_scale.y);
        groups_.write_double(45, d.obsolete_scale.z);
        write_code(301, d.coordinate_system);
        write_code(302, d.geo_rss);
        groups_.write_double(46, d.horizontal_unit_scale);
        write_code(303, d.datum_name);
        write_code(304, d.coordinate_system_wkt);
    } else {
        write_code(330, host);
        groups_.write_int(70, d.coordinate_type);
        write_point(10, d.design_point);
        write_point(11, d.reference_point);
        groups_.write_double(40, d.horizontal_unit_scale);
        groups_.write_int(91, d.horizontal_units);
        groups_.write_double(41, d.vertical_unit_scale);
        groups_.write_int(92, d.vertical_units);
        write_point(210, d.up_direction);
        xy(12, d.north_direction);
        groups_.write_int(95, d.scale_estimation);
        groups_.write_double(141, d.user_scale);
        groups_.write_int(294, d.sea_level_correction ? 1 : 0);
        groups_.write_double(142, d.sea_level_elevation);
        groups_.write_double(143, d.projection_radius);
        if (d.version >= 3) {
            // In parts of 250 characters (code points), the last in 301
            std::u32string cs = code_points(d.coordinate_system);
            size_t at = 0;
            while (cs.size() - at > 250) {
                write_code(303, utf8_of(cs.substr(at, 250)));
                at += 250;
            }
            write_code(301, utf8_of(cs.substr(at)));
        } else {
            write_code(301, d.coordinate_system);
        }
        write_code(302, d.geo_rss);
    }
    write_code(305, d.observation_from);
    write_code(306, d.observation_to);
    write_code(307, d.observation_coverage);
    groups_.write_int(93, static_cast<int64_t>(d.mesh_points.size()));
    int first = d.version == 1 ? 12 : 13;
    for (const auto& p : d.mesh_points) {
        xy(first, p.source);
        xy(first + 1, p.destination);
    }
    groups_.write_int(96, static_cast<int64_t>(d.mesh_faces.size()));
    for (const auto& f : d.mesh_faces) {
        groups_.write_int(97, f.a);
        groups_.write_int(98, f.b);
        groups_.write_int(99, f.c);
    }
    if (d.version != 1) return;
    // CIVIL3D_DATA: as read, with the 2010 values in their places; one made here as AutoCAD's
    // 2007 file has it (the groups the reference does not describe with that file's values)
    std::vector<DxfGroup> civil = d.civil3d;
    if (civil.empty()) {
        auto f = [](double v) { return format_double(v, NumberStyle::AutoCad); };
        civil = {{3, "CIVIL3D_DATA_BEGIN"}, {292, "0"},
                 {14, f(d.reference_point.x)}, {24, f(d.reference_point.y)},
                 {15, f(d.reference_point.x)}, {25, f(d.reference_point.y)},
                 {93, "0"}, {94, "0"}, {293, "0"}, {16, "0.0"}, {26, "0.0"}, {17, "0.0"}, {27, "0.0"},
                 {54, f(d.north_angle)}, {140, f(d.north_direction.x)},
                 {95, ""}, {141, ""}, {294, ""}, {142, ""}, {143, ""}, {4, "CIVIL3D_DATA_END"}};
    }
    for (const auto& g : civil) {
        switch (g.code) {
            case 95:  groups_.write_int(95, d.scale_estimation); break;
            case 141: groups_.write_double(141, d.user_scale); break;
            case 294: groups_.write_int(294, d.sea_level_correction ? 1 : 0); break;
            case 142: groups_.write_double(142, d.sea_level_elevation); break;
            case 143: groups_.write_double(143, d.projection_radius); break;
            default:  write_code_typed(g.code, g.value); break;
        }
    }
}

} // namespace internal
} // namespace hdxf
