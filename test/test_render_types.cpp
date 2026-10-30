#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include "RenderTypes.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.11 of phase 9 (plan 2026-10-27-fas-9-typer.md): the render objects, LIGHT and SUN (codecs since this step), the
// light list and the sun study.
//
// AutoCAD's originals: doc/facit/acad_<version>_dxf_lights*.dxf (fas9_render.scr, AutoCAD 2025's core: a point, a spot and a
// distant light, one erased), acad_<version>_dxf_objects (LIGHT and SUN, 13_objects.scr), acad_<version>_dxf_modeldoc_views and
// acad_<version>_dxf_render_modeldoc (RAPIDRT presets, the IBL environment and MENTALRAYRENDERSETTINGS, made in the GUI by
// 18_render_modeldoc.scr and 24_rapidrt_preset.scr). They are local files (the facit is not in git): a test whose file is
// missing is skipped. RENDERSETTINGS, RENDERENVIRONMENT, RENDERGLOBAL, LIGHTLIST and SUNSTUDY have no record in any AutoCAD
// file: their tests run on the reference's grammar and are ej testat as AutoCAD output (F9.13 … F9.15).
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf::LightData;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};
const DxfVersion kVersions[] = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007,
                                DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018};

std::filesystem::path facit_path(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

std::vector<uint8_t> facit_bytes(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(facit_path(name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    return in;
}

std::string facit_name(const char* tag, const std::string& theme, bool binary) {
    return std::string("acad_") + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf";
}

Drawing load(const std::vector<uint8_t>& bytes, const std::string& what) {
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), what + ": " + d.last_error());
    return d;
}

Drawing load_text(const std::string& text) {
    Drawing d;
    assert_true(d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), d.last_error());
    return d;
}

std::vector<uint8_t> save(Drawing& d, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = d.save_to_memory(format);
    assert_true(!out.empty(), what + ": " + d.last_error());
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::vector<Rec> ascii_records(const std::vector<uint8_t>& bytes) {
    std::vector<Rec> out;
    std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::string code_line, value_line;
    while (std::getline(in, code_line) && std::getline(in, value_line)) {
        if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();
        int code = std::atoi(code_line.c_str());
        if (code == 0) out.emplace_back();
        if (!out.empty()) out.back().push_back({code, value_line});
    }
    return out;
}

const Rec* record_with_handle(const std::vector<Rec>& all, const std::string& handle) {
    for (const Rec& r : all) {
        for (const Group& g : r) {
            if (g.first == 5) {
                if (trim(g.second) == handle) return &r;
                break;
            }
        }
    }
    return nullptr;
}

std::vector<const Rec*> records_named(const std::vector<Rec>& all, const std::string& name) {
    std::vector<const Rec*> out;
    for (const Rec& r : all) if (!r.empty() && r[0].second == name) out.push_back(&r);
    return out;
}

std::vector<Group> after_marker(const Rec& r, const std::string& subclass) {
    std::vector<Group> out;
    bool in = false;
    for (const Group& g : r) {
        if (g.first == 100) {
            in = trim(g.second) == subclass;
            continue;
        }
        if (g.first >= 1000) break;
        if (in) out.push_back(g);
    }
    return out;
}

bool same_group(const Group& a, const Group& b) {
    if (a.first != b.first) return false;
    std::string x = trim(a.second), y = trim(b.second);
    if (x == y) return true;
    char* ex = nullptr;
    char* ey = nullptr;
    double dx = std::strtod(x.c_str(), &ex);
    double dy = std::strtod(y.c_str(), &ey);
    return !x.empty() && !y.empty() && *ex == '\0' && *ey == '\0' && dx == dy;
}

std::string why_different(const std::vector<Group>& a, const std::vector<Group>& b) {
    if (a.size() != b.size()) return "the number of groups: " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (!same_group(a[i], b[i])) {
            return "group " + std::to_string(i) + ": " + std::to_string(a[i].first) + "=" + a[i].second + " and " +
                   std::to_string(b[i].first) + "=" + b[i].second;
        }
    }
    return {};
}

std::string record_text(const Rec& r) {
    std::ostringstream s;
    for (const Group& g : r) {
        s << (g.first < 10 ? "  " : (g.first < 100 ? " " : "")) << g.first << "\n" << g.second << "\n";
    }
    return s.str();
}

// A drawing as `version` with `records` added to its OBJECTS section, as text
std::string drawing_with_objects(const std::vector<Rec>& records, DxfVersion version = DxfVersion::R2007) {
    Drawing d(version);
    d.add_line({0, 0, 0}, {1, 1, 0});
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!bytes.empty(), d.last_error());
    std::string text = hdxf_test::lf_text(bytes);
    size_t at = text.rfind("  0\nENDSEC\n  0\nEOF");
    assert_true(at != std::string::npos, "the end of the file");
    std::string added;
    for (const Rec& r : records) added += record_text(r);
    return text.substr(0, at) + added + text.substr(at);
}

Object make_object(ObjectType type, const std::string& handle, hdxf::ObjectData data) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = "0";
    o.data = std::move(data);
    return o;
}

std::string first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    size_t n = std::min(a.size(), b.size());
    size_t i = 0;
    while (i < n && a[i] == b[i]) ++i;
    auto around = [&](const std::vector<uint8_t>& v) {
        size_t from = i > 60 ? i - 60 : 0;
        std::string s;
        for (size_t k = from; k < v.size() && k < i + 60; ++k) s += v[k] == '\n' ? '|' : v[k] == '\r' ? '~' : static_cast<char>(v[k]);
        return s;
    };
    return "at byte " + std::to_string(i) + " (" + std::to_string(a.size()) + " and " + std::to_string(b.size()) + " bytes): [" + around(a) + "] against [" + around(b) + "]";
}

std::vector<Object*> objects_of(Drawing& d, ObjectType type) {
    std::vector<Object*> out;
    for (Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

std::vector<const Object*> objects_of(const Drawing& d, ObjectType type) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

std::vector<Entity*> lights_of(Drawing& d) {
    std::vector<Entity*> out;
    for (Entity& e : d.entities()) if (e.type == EntityType::Light) out.push_back(&e);
    for (auto& b : d.blocks()) {
        for (Entity& e : b.entities) if (e.type == EntityType::Light) out.push_back(&e);
    }
    return out;
}

Entity* light_named(Drawing& d, const std::string& name) {
    for (Entity* e : lights_of(d)) if (e->as_light().name == name) return e;
    return nullptr;
}

std::string describe(const LightData& d) {
    std::ostringstream s;
    s << d.version << "|" << d.name << "|" << d.light_type << "|" << d.on << "|" << d.color.aci << "/" << d.color.rgb << "|"
      << d.plot_glyph << "|" << d.intensity << "|" << d.position.x << "," << d.position.y << "," << d.position.z << "|"
      << d.target.x << "," << d.target.y << "," << d.target.z << "|" << d.attenuation_type << "|" << d.use_limits << "|"
      << d.start_limit << "|" << d.end_limit << "|" << d.hotspot_angle << "|" << d.falloff_angle << "|" << d.cast_shadows
      << "|" << d.shadow_type << "|" << d.shadow_map_size << "|" << d.shadow_softness;
    return s.str();
}

std::string describe(const hdxf::SunData& d) {
    std::ostringstream s;
    s << d.version << "|" << d.on << "|" << d.color.aci << "/" << d.color.rgb << "|" << d.intensity << "|" << d.shadows << "|"
      << d.julian_day << "|" << d.time << "|" << d.daylight_saving << "|" << d.shadow_type << "|" << d.shadow_map_size << "|"
      << d.shadow_softness;
    return s.str();
}

std::string describe(const hdxf::RapidRtRenderSettingsData& d) {
    std::ostringstream s;
    s << d.base_version << "|" << d.name << "|" << d.materials << d.texture_sampling << d.back_faces << d.shadows << "|"
      << d.preview_file << "|" << d.description << "|" << d.display_index << "|" << d.version << "|"
      << static_cast<int>(d.duration_type) << "|" << d.levels << "|" << d.duration << "|" << static_cast<int>(d.accuracy) << "|"
      << static_cast<int>(d.filter) << "|" << d.filter_width << "|" << d.filter_height << "|" << d.predefined;
    return s.str();
}

std::string describe(const hdxf::RapidRtRenderEnvironmentData& d) {
    std::ostringstream s;
    s << d.version << "|" << d.enabled << "|" << d.ibl_file << "|" << d.rotation << "|" << d.ibl_as_background << "|" << d.background_handle;
    return s.str();
}

std::string describe(const hdxf::RenderSettingsData& d) {
    std::ostringstream s;
    s << d.version << "|" << d.name << "|" << d.materials << d.texture_sampling << d.back_faces << d.shadows << "|"
      << d.preview_file << "|" << d.description << "|" << d.display_index << "|"
      << (d.predefined ? std::to_string(*d.predefined) : std::string("-"));
    return s.str();
}

std::string describe(const hdxf::MentalRayRenderSettingsData& d) {
    std::ostringstream s;
    s << describe(d.base) << "||" << d.version << "|" << d.min_samples << "|" << d.max_samples << "|" << static_cast<int>(d.filter)
      << "|" << d.filter_width << "|" << d.filter_height << "|" << d.contrast[0] << "," << d.contrast[1] << "," << d.contrast[2]
      << "," << d.contrast[3] << "|" << d.shadow_mode << "|" << d.shadow_maps << d.ray_tracing << "|" << d.trace_reflections << ","
      << d.trace_refractions << "," << d.trace_max << "|" << d.global_illumination << "|" << d.photons_per_sample << "|"
      << d.use_radius << "|" << d.radius << "|" << d.photons_per_light << "|" << d.photon_reflections << "," << d.photon_refractions
      << "," << d.photon_max << "|" << d.final_gather << "|" << d.final_gather_rays << "|" << d.use_min_radius << d.use_max_radius
      << d.radius_in_pixels << "|" << d.min_radius << "," << d.max_radius << "|" << d.luminance_scale << "|" << d.diagnostic_mode
      << "," << d.grid_mode << "|" << d.grid_size << "|" << d.photon_mode << "," << d.bsp_mode << "|" << d.export_statistics << "|"
      << d.export_file << "|" << d.tile_size << "|" << d.tile_order << "|" << d.memory_limit << "|" << d.extra_flag << "|"
      << d.extra_factor;
    return s.str();
}

std::string describe(const hdxf::RenderEnvironmentData& d) {
    std::ostringstream s;
    s << d.version << "|" << d.fog_enabled << d.fog_background << "|" << int(d.fog_color[0]) << "," << int(d.fog_color[1]) << ","
      << int(d.fog_color[2]) << "|" << d.near_density << "," << d.far_density << "|" << d.near_distance << "," << d.far_distance
      << "|" << d.use_image << "|" << d.image_file;
    return s.str();
}

std::string describe(const hdxf::RenderGlobalData& d) {
    std::ostringstream s;
    s << d.version << "|" << d.procedure << "|" << d.destination << "|" << d.save_to_file << "|" << d.file_name << "|" << d.width
      << "|" << d.height << "|" << d.predefined_first << d.high_info;
    return s.str();
}

std::string describe(const hdxf::LightListData& d) {
    std::ostringstream s;
    s << d.version;
    for (const auto& e : d.lights) s << "|" << e.light_handle << "=" << e.name;
    return s.str();
}

std::string describe(const hdxf::SunStudyData& d) {
    std::ostringstream s;
    s << d.version << "|" << d.name << "|" << d.description << "|" << d.output_type << "|"
      << (d.sheet_set_name ? *d.sheet_set_name : "-") << "|" << (d.use_subset ? std::to_string(*d.use_subset) : "-") << "|"
      << (d.sheet_subset_name ? *d.sheet_subset_name : "-") << "|" << d.select_dates << "|";
    for (const auto& date : d.dates) s << date.julian_day << "/" << date.seconds << ",";
    s << "|" << d.select_range << "|" << (d.start_time ? std::to_string(*d.start_time) : "-") << "|"
      << (d.end_time ? std::to_string(*d.end_time) : "-") << "|" << (d.interval ? std::to_string(*d.interval) : "-") << "|";
    for (int h : d.hours) s << h << ",";
    s << "|" << d.page_setup_handle << "|" << d.view_handle << "|" << d.visual_style_handle << "|" << d.shade_plot_type << "|"
      << d.viewports_per_page << "|" << d.rows << "|" << d.columns << "|" << d.spacing << "|" << d.lock_viewports
      << d.label_viewports << "|" << (d.text_style_handle ? *d.text_style_handle : "-");
    return s.str();
}

// ---------------------------------------------------------------------------
// The values the AutoCAD files have
// ---------------------------------------------------------------------------

LightData expected_point() {
    LightData d;
    d.name = "HDXF_POINT";
    d.light_type = 2;
    d.intensity = 1.5;
    d.position = {30.0, 30.0, 80.0};
    d.target = {30.0, 30.0, 70.0};
    d.color = hdxf::Color{30, 0xFF8000, {}};
    return d;
}

LightData expected_spot() {
    LightData d;
    d.name = "HDXF_SPOT";
    d.light_type = 3;
    d.position = {100.0, 20.0, 90.0};
    d.target = {100.0, 20.0, 80.0};
    d.hotspot_angle = 30.0;
    d.falloff_angle = 40.0;
    d.shadow_type = 1;
    d.shadow_map_size = 512;
    d.shadow_softness = 4;
    d.color = hdxf::Color::from_aci(1);
    return d;
}

LightData expected_distant() {
    LightData d;
    d.name = "HDXF_DISTANT";
    d.light_type = 1;
    d.on = false;
    d.position = {0.0, 0.0, 100.0};
    d.target = {50.0, 50.0, 0.0};
    d.cast_shadows = false;
    d.color = hdxf::Color{7, 0xFFFFFF, {}};
    return d;
}

bool is_binary_name(const std::string& name) { return name.find("_bin") != std::string::npos; }

} // namespace

// ---------------------------------------------------------------------------
// LIGHT: AutoCAD's three lights in every version and both formats
// ---------------------------------------------------------------------------

TEST(render_lights_read_from_autocad_in_every_version_and_format) {
    for (const char* tag : kTags) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "lights", binary);
            Drawing d = load(facit_bytes(name), name);
            const std::string where = name + ": ";
            assert_eq(3, static_cast<int>(lights_of(d).size()), where + "three lights");
            const std::pair<LightData, const char*> wanted[] = {
                {expected_point(), "the point light"}, {expected_spot(), "the spot light"}, {expected_distant(), "the distant light"}};
            for (const auto& [want, what] : wanted) {
                Entity* e = light_named(d, want.name);
                assert_true(e != nullptr, where + what);
                assert_eq(describe(want), describe(e->as_light()), where + what);
                assert_false(e->raw.empty(), where + what + ": the preserved record");
                assert_false(e->props.xdictionary.empty(), where + what + ": the extension dictionary");
            }
        }
    }
}

TEST(render_lights_unchanged_are_written_back_byte_for_byte) {
    for (const char* tag : kTags) {
        for (const char* theme : {"lights", "lights_light_erased", "objects"}) {
            for (bool binary : {false, true}) {
                const std::string name = facit_name(tag, theme, binary);
                std::vector<uint8_t> in = facit_bytes(name);
                Drawing d = load(in, name);
                std::vector<uint8_t> out = save(d, binary ? DxfFormat::Binary : DxfFormat::Ascii, name);
                assert_true(out == in, name + ": " + first_difference(in, out));
            }
        }
    }
}

// A record written anew from its typed values (its raw emptied) is the record AutoCAD saved, group for group
TEST(render_lights_written_anew_are_autocads_records) {
    for (const char* tag : kTags) {
        const std::string name = facit_name(tag, "lights", false);
        std::vector<uint8_t> in = facit_bytes(name);
        std::vector<Rec> original = ascii_records(in);
        Drawing d = load(in, name);
        for (Entity* e : lights_of(d)) e->raw = hdxf::RawRecord();
        std::vector<Rec> written = ascii_records(save(d, DxfFormat::Ascii, name));
        std::vector<const Rec*> theirs = records_named(original, "LIGHT");
        assert_eq(3, static_cast<int>(theirs.size()), "AutoCAD's lights");
        for (const Rec* r : theirs) {
            std::string handle;
            for (const Group& g : *r) if (g.first == 5) { handle = trim(g.second); break; }
            const Rec* mine = record_with_handle(written, handle);
            assert_true(mine != nullptr, std::string(tag) + ": the light " + handle + " is written");
            std::string why = why_different(*r, *mine);
            assert_true(why.empty(), std::string(tag) + " light " + handle + ": " + why);
        }
    }
}

// … and in binary the model that comes back from a record written anew is the same
TEST(render_lights_written_anew_in_binary_give_the_same_model) {
    for (const char* tag : kTags) {
        const std::string name = facit_name(tag, "lights", true);
        std::vector<uint8_t> in = facit_bytes(name);
        Drawing d = load(in, name);
        std::vector<std::string> before;
        for (Entity* e : lights_of(d)) before.push_back(describe(e->as_light()));
        for (Entity* e : lights_of(d)) e->raw = hdxf::RawRecord();
        std::vector<uint8_t> out = save(d, DxfFormat::Binary, name);
        Drawing back = load(out, name + " written anew");
        std::vector<Entity*> lights = lights_of(back);
        assert_eq(static_cast<int>(before.size()), static_cast<int>(lights.size()), name);
        for (size_t i = 0; i < lights.size(); ++i) assert_eq(before[i], describe(lights[i]->as_light()), name + ": light " + std::to_string(i));
    }
}

// The 2000 color is one group 90, an AcCmEntityColor: an index color 0xC3 and the index, a true color 0xC2 and its three bytes
TEST(render_light_color_in_2000_is_the_one_group_autocad_writes) {
    assert_eq(static_cast<int>(0xC3000001u), static_cast<int>(light_color_to_raw(hdxf::Color::from_aci(1))), "index 1");
    assert_eq(-1023410175, light_color_to_raw(hdxf::Color::from_aci(1)), "as AutoCAD 2025 wrote it (0xC3000001)");
    assert_eq(-1023442944, light_color_to_raw(hdxf::Color{30, 0xFF8000, {}}), "255,128,0 (0xC2FF8000)");
    assert_eq(-1023410177, light_color_to_raw(hdxf::Color{7, 0xFFFFFF, {}}), "white (0xC2FFFFFF)");
    hdxf::Color c;
    assert_true(light_color_from_raw(-1023442944, c) && c.rgb == 0xFF8000 && c.aci == 30, "a true color comes back with AutoCAD's nearest index");
    assert_true(light_color_from_raw(-1023410175, c) && c.aci == 1 && !c.has_rgb(), "an index color has no true color");
    assert_true(light_color_from_raw(-1023410177, c) && c.rgb == 0xFFFFFF && c.aci == 7, "white");
    assert_false(light_color_from_raw(0x01020304, c), "no AcCmEntityColor of a known method");
    assert_false(light_color_from_raw(static_cast<int32_t>(0xC0000000u), c), "ByLayer is no light color");

    for (DxfVersion version : kVersions) {
        Drawing d(version);
        Entity e;
        e.type = EntityType::Light;
        LightData light = expected_spot();
        e.data = hdxf::Boxed<LightData>(light);
        d.add_entity(e);
        std::vector<Rec> recs = ascii_records(save(d, DxfFormat::Ascii, "light"));
        std::vector<const Rec*> lights = records_named(recs, "LIGHT");
        assert_eq(1, static_cast<int>(lights.size()), "the light");
        std::vector<Group> body = after_marker(*lights[0], "AcDbLight");
        assert_eq(4, static_cast<int>(body.size() > 4 ? 4 : body.size()), "the head of the subclass");
        if (version == DxfVersion::R2000) {
            assert_eq(290, body[3].first, "the status");
            assert_eq(90, body[4].first, "2000: the color is group 90");
            assert_eq(std::string("-1023410175"), trim(body[4].second), "the index color 1");
        } else {
            assert_eq(63, body[4].first, "2004 and later: 63");
            assert_eq(std::string("1"), trim(body[4].second), "the index");
            assert_eq(291, body[5].first, "no true color for an index color");
        }
    }
}

// ---------------------------------------------------------------------------
// SUN
// ---------------------------------------------------------------------------

TEST(render_sun_from_autocad_is_written_anew_as_autocad_has_it) {
    for (const char* tag : {"2007", "2010", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "objects", binary);
            std::vector<uint8_t> in = facit_bytes(name);
            Drawing d = load(in, name);
            std::vector<Object*> suns = objects_of(d, ObjectType::Sun);
            assert_true(!suns.empty(), name + ": AutoCAD's file has suns");
            std::vector<std::string> before;
            for (Object* o : suns) before.push_back(describe(o->as_sun()));
            for (Object* o : suns) o->raw = hdxf::RawRecord();
            DxfFormat format = binary ? DxfFormat::Binary : DxfFormat::Ascii;
            std::vector<uint8_t> out = save(d, format, name);
            Drawing back = load(out, name + " written anew");
            std::vector<Object*> again = objects_of(back, ObjectType::Sun);
            assert_eq(static_cast<int>(before.size()), static_cast<int>(again.size()), name + ": suns");
            for (size_t i = 0; i < again.size(); ++i) assert_eq(before[i], describe(again[i]->as_sun()), name + ": sun " + std::to_string(i));
            if (!binary) {
                std::vector<Rec> original = ascii_records(in);
                std::vector<Rec> written = ascii_records(out);
                for (const Rec* r : records_named(original, "SUN")) {
                    std::string handle;
                    for (const Group& g : *r) if (g.first == 5) { handle = trim(g.second); break; }
                    const Rec* mine = record_with_handle(written, handle);
                    assert_true(mine != nullptr, name + ": the sun " + handle);
                    std::string why = why_different(*r, *mine);
                    assert_true(why.empty(), name + " sun " + handle + ": " + why);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// RAPIDRT presets, the IBL environment and MENTALRAYRENDERSETTINGS, from AutoCAD's files
// ---------------------------------------------------------------------------

TEST(render_rapidrt_presets_and_environment_from_autocad) {
    struct Source { const char* theme; };
    for (const char* tag : kTags) {
        for (bool binary : {false, true}) {
            for (const char* theme : {"modeldoc_views", "render_modeldoc"}) {
                const std::string name = facit_name(tag, theme, binary);
                std::vector<uint8_t> in = facit_bytes(name);
                Drawing d = load(in, name);
                const DxfFormat format = binary ? DxfFormat::Binary : DxfFormat::Ascii;
                std::vector<Object*> presets = objects_of(d, ObjectType::RapidRtRenderSettings);
                std::vector<Object*> environments = objects_of(d, ObjectType::RapidRtRenderEnvironment);
                assert_true(!presets.empty(), name + ": presets");
                // the presets: AutoCAD's standard ones by name (the file of the active preset alone has "Medium")
                std::set<std::string> names;
                for (Object* o : presets) {
                    names.insert(o->as_rapidrt_settings().name);
                    assert_false(o->raw.empty(), name + ": the preserved record");
                    assert_true(!o->as_rapidrt_settings().name.empty(), name + ": a name");
                }
                // the file of the render script has the preset it made active (High), the other one Medium
                assert_true(names.count(std::string(theme) == "render_modeldoc" ? "High" : "Medium") > 0, name + ": the active preset");
                // unchanged: the file again
                std::vector<uint8_t> same = save(d, format, name);
                assert_true(same == in, name + ": unchanged, " + first_difference(in, same));
                // written anew: the same model, and in ASCII the same groups
                std::vector<std::string> before;
                for (Object* o : presets) before.push_back(describe(o->as_rapidrt_settings()));
                std::vector<std::string> env_before;
                for (Object* o : environments) env_before.push_back(describe(o->as_rapidrt_environment()));
                for (Object* o : presets) o->raw = hdxf::RawRecord();
                for (Object* o : environments) o->raw = hdxf::RawRecord();
                std::vector<uint8_t> out = save(d, format, name);
                Drawing back = load(out, name + " written anew");
                std::vector<Object*> again = objects_of(back, ObjectType::RapidRtRenderSettings);
                assert_eq(static_cast<int>(before.size()), static_cast<int>(again.size()), name + ": presets again");
                for (size_t i = 0; i < again.size(); ++i) assert_eq(before[i], describe(again[i]->as_rapidrt_settings()), name + ": preset " + std::to_string(i));
                std::vector<Object*> envs_again = objects_of(back, ObjectType::RapidRtRenderEnvironment);
                assert_eq(static_cast<int>(env_before.size()), static_cast<int>(envs_again.size()), name + ": environments again");
                for (size_t i = 0; i < envs_again.size(); ++i) assert_eq(env_before[i], describe(envs_again[i]->as_rapidrt_environment()), name + ": environment " + std::to_string(i));
                if (!binary) {
                    std::vector<Rec> original = ascii_records(in);
                    std::vector<Rec> written = ascii_records(out);
                    for (const char* type : {"RAPIDRTRENDERSETTINGS", "RAPIDRTRENDERENVIRONMENT"}) {
                        for (const Rec* r : records_named(original, type)) {
                            std::string handle;
                            for (const Group& g : *r) if (g.first == 5) { handle = trim(g.second); break; }
                            const Rec* mine = record_with_handle(written, handle);
                            assert_true(mine != nullptr, name + ": " + type + " " + handle);
                            std::string why = why_different(*r, *mine);
                            assert_true(why.empty(), name + " " + type + " " + handle + ": " + why);
                        }
                    }
                }
            }
        }
    }
}

TEST(render_rapidrt_environment_has_autocads_ibl_values) {
    const std::string name = facit_name("2018", "render_modeldoc", false);
    Drawing d = load(facit_bytes(name), name);
    std::vector<Object*> envs = objects_of(d, ObjectType::RapidRtRenderEnvironment);
    assert_true(!envs.empty(), "environments");
    const hdxf::RapidRtRenderEnvironmentData& e = envs[0]->as_rapidrt_environment();
    assert_eq(2, e.version, "version");
    assert_true(e.enabled, "enabled");
    assert_eq(27.5, e.rotation, "rotation");
    assert_true(e.ibl_as_background, "the IBL map is the background");
    assert_true(e.ibl_file.find("environment.hdr") != std::string::npos, "the file: " + e.ibl_file);
    assert_eq(std::string("0"), e.background_handle, "no custom background");
}

TEST(render_mentalray_settings_from_autocad_in_every_version_and_format) {
    for (const char* tag : {"2000", "2004", "2007", "2010", "2013"}) {
        for (bool binary : {false, true}) {
            const std::string name = facit_name(tag, "modeldoc_views", binary);
            std::vector<uint8_t> in = facit_bytes(name);
            Drawing d = load(in, name);
            const DxfFormat format = binary ? DxfFormat::Binary : DxfFormat::Ascii;
            std::vector<Object*> presets = objects_of(d, ObjectType::MentalRayRenderSettings);
            assert_eq(1, static_cast<int>(presets.size()), name + ": the active MentalRay preset");
            for (Object* o : presets) {
                const hdxf::MentalRayRenderSettingsData& m = o->as_mentalray_settings();
                assert_false(o->raw.empty(), name + ": the preserved record");
                assert_eq(2, m.version, name + ": version");
                assert_eq(2, static_cast<int>(m.filter), name + ": Gauss");
                assert_eq(3.0, m.filter_width, name + ": filter width");
                assert_true(m.ray_tracing && !m.shadow_maps && !m.global_illumination, name + ": flags");
                assert_eq(5, m.trace_max, name + ": trace depth");
                assert_eq(10000, m.photons_per_light, name + ": photons per light");
                assert_eq(200, m.final_gather_rays, name + ": final gather rays");
                assert_eq(1500.0, m.luminance_scale, name + ": luminance");
                assert_eq(32, m.tile_size, name + ": tile size");
                assert_eq(1048, m.memory_limit, name + ": memory limit");
                assert_eq(1.0, m.extra_factor, name + ": the last 40");
                assert_true(m.base.materials && m.base.texture_sampling && m.base.back_faces && m.base.shadows, name + ": the base's four flags");
            }
            assert_eq(std::string("Medium"), presets[0]->as_mentalray_settings().base.name, name + ": the name");
            std::vector<uint8_t> same = save(d, format, name);
            assert_true(same == in, name + ": unchanged, " + first_difference(in, same));
            std::vector<std::string> before;
            for (Object* o : presets) before.push_back(describe(o->as_mentalray_settings()));
            for (Object* o : presets) o->raw = hdxf::RawRecord();
            std::vector<uint8_t> out = save(d, format, name);
            Drawing back = load(out, name + " written anew");
            std::vector<Object*> again = objects_of(back, ObjectType::MentalRayRenderSettings);
            assert_eq(1, static_cast<int>(again.size()), name + ": again");
            for (size_t i = 0; i < again.size(); ++i) assert_eq(before[i], describe(again[i]->as_mentalray_settings()), name + ": preset " + std::to_string(i));
            if (!binary) {
                std::vector<Rec> original = ascii_records(in);
                std::vector<Rec> written = ascii_records(out);
                for (const Rec* r : records_named(original, "MENTALRAYRENDERSETTINGS")) {
                    std::string handle;
                    for (const Group& g : *r) if (g.first == 5) { handle = trim(g.second); break; }
                    const Rec* mine = record_with_handle(written, handle);
                    assert_true(mine != nullptr, name + ": preset " + handle);
                    std::string why = why_different(*r, *mine);
                    assert_true(why.empty(), name + " preset " + handle + ": " + why);
                }
            }
        }
    }
}

// Every MentalRay scalar and ordinal is the one the position says: each is changed and found again in its own group
TEST(render_mentalray_every_scalar_is_written_to_its_own_group) {
    hdxf::MentalRayRenderSettingsData m;
    m.base.name = "Every field";
    m.base.preview_file = "preview.png";
    m.base.description = "A description with \xC3\xA5\xC3\xA4\xC3\xB6";
    m.base.display_index = 7;
    m.base.materials = false;
    m.base.texture_sampling = true;
    m.base.back_faces = false;
    m.base.shadows = true;
    m.min_samples = 2;
    m.max_samples = 9;
    m.filter = hdxf::SamplingFilter::Lanczos;
    m.filter_width = 4.5;
    m.filter_height = 5.5;
    m.contrast = {0.1, 0.2, 0.3, 0.4};
    m.shadow_mode = 2;
    m.shadow_maps = true;
    m.ray_tracing = false;
    m.trace_reflections = 11;
    m.trace_refractions = 12;
    m.trace_max = 13;
    m.global_illumination = true;
    m.photons_per_sample = 777;
    m.use_radius = true;
    m.radius = 2.25;
    m.photons_per_light = 4242;
    m.photon_reflections = 3;
    m.photon_refractions = 4;
    m.photon_max = 6;
    m.final_gather = true;
    m.final_gather_rays = 321;
    m.use_min_radius = true;
    m.use_max_radius = false;
    m.radius_in_pixels = true;
    m.min_radius = 0.5;
    m.max_radius = 8.0;
    m.luminance_scale = 99.5;
    m.diagnostic_mode = 4;
    m.grid_mode = 2;
    m.grid_size = 17.5;
    m.photon_mode = 1;
    m.bsp_mode = 1;
    m.export_statistics = true;
    m.export_file = "stats.mi";
    m.tile_size = 64;
    m.tile_order = 5;
    m.memory_limit = 2048;
    m.extra_flag = true;
    m.extra_factor = 3.5;
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2013);
        d.set_mentalray_render_settings(m);
        std::vector<uint8_t> bytes = save(d, format, "mentalray");
        Drawing back = load(bytes, "mentalray");
        std::vector<Object*> found = objects_of(back, ObjectType::MentalRayRenderSettings);
        assert_eq(1, static_cast<int>(found.size()), "one preset");
        assert_eq(describe(m), describe(found[0]->as_mentalray_settings()), "every field comes back");
    }
    // the order of the groups: the codes of AutoCAD's own record
    Drawing d(DxfVersion::R2013);
    d.set_mentalray_render_settings(m);
    std::vector<Rec> recs = ascii_records(save(d, DxfFormat::Ascii, "mentalray"));
    std::vector<const Rec*> found = records_named(recs, "MENTALRAYRENDERSETTINGS");
    assert_eq(1, static_cast<int>(found.size()), "the record");
    std::vector<Group> base = after_marker(*found[0], "AcDbRenderSettings");
    std::vector<int> base_codes;
    for (const Group& g : base) base_codes.push_back(g.first);
    assert_true(base_codes == std::vector<int>({90, 1, 290, 290, 290, 290, 1, 1, 90}), "the base subclass: AutoCAD's order");
    std::vector<Group> mental = after_marker(*found[0], "AcDbMentalRayRenderSettings");
    std::vector<int> codes;
    for (const Group& g : mental) codes.push_back(g.first);
    const std::vector<int> expected = {90, 90, 90, 70, 40, 40, 40, 40, 40, 40, 70, 290, 290, 90, 90, 90, 290, 90, 290, 40, 90, 90, 90,
                                       90, 290, 90, 290, 290, 290, 40, 40, 40, 70, 70, 40, 70, 70, 290, 1, 90, 70, 90, 290, 40};
    assert_true(codes == expected, "the MentalRay subclass: AutoCAD's order");
}

// ---------------------------------------------------------------------------
// Building the types anew: every version and both formats (the matrix of B101)
// ---------------------------------------------------------------------------

namespace {

hdxf::RenderSettingsData a_render_settings() {
    hdxf::RenderSettingsData d;
    d.name = "Standalone";
    d.description = "The base by itself";
    d.display_index = 3;
    d.materials = false;
    d.shadows = false;
    return d;
}

hdxf::MentalRayRenderSettingsData a_mentalray() {
    hdxf::MentalRayRenderSettingsData m;
    m.base.name = "Matrix";
    m.min_samples = 1;
    m.max_samples = 4;
    m.photons_per_light = 5000;
    m.export_file = "x.mi";
    m.extra_flag = true;
    return m;
}

hdxf::RenderEnvironmentData an_environment() {
    hdxf::RenderEnvironmentData e;
    e.fog_enabled = true;
    e.fog_background = true;
    e.fog_color = {10, 200, 255};
    e.near_density = 5.0;
    e.far_density = 90.0;
    e.near_distance = 1.5;
    e.far_distance = 80.0;
    e.use_image = true;
    e.image_file = "sky.png";
    return e;
}

hdxf::RenderGlobalData a_global() {
    hdxf::RenderGlobalData g;
    g.procedure = 2;
    g.destination = 1;
    g.save_to_file = true;
    g.file_name = "out.png";
    g.width = 1920;
    g.height = 1080;
    g.predefined_first = true;
    g.high_info = true;
    return g;
}

hdxf::RapidRtRenderSettingsData a_preset() {
    hdxf::RapidRtRenderSettingsData d;
    d.name = "Matrix preset";
    d.description = "A custom preset";
    d.levels = 7;
    d.duration = 33;
    d.duration_type = hdxf::RenderDurationType::ByTime;
    d.accuracy = hdxf::RenderAccuracy::High;
    d.filter = hdxf::SamplingFilter::Lanczos;
    d.filter_width = 2.0;
    d.filter_height = 2.5;
    d.predefined = true;
    return d;
}

hdxf::RapidRtRenderEnvironmentData an_ibl() {
    hdxf::RapidRtRenderEnvironmentData e;
    e.enabled = true;
    e.ibl_file = "C:/maps/sky.hdr";
    e.rotation = -45.5;
    e.ibl_as_background = true;
    e.background_handle = "0";          // none, as it is read
    return e;
}

hdxf::SunData a_sun() {
    hdxf::SunData s;
    s.on = true;
    s.color = hdxf::Color{5, 0x0000FF, {}};
    s.intensity = 2.5;
    s.shadows = false;
    s.julian_day = 2461000;
    s.time = 43200000;
    s.daylight_saving = true;
    s.shadow_type = 1;
    s.shadow_map_size = 1024;
    s.shadow_softness = 3;
    return s;
}

LightData a_light() {
    LightData l = expected_point();
    l.name = "BUILT";
    l.light_type = 3;
    l.use_limits = true;
    l.attenuation_type = 2;
    l.start_limit = 2.0;
    l.end_limit = 40.0;
    l.cast_shadows = false;
    return l;
}

hdxf::SunStudyData a_sun_study() {
    hdxf::SunStudyData s;
    s.name = "Study";
    s.description = "Summer";
    s.output_type = 2;
    s.sheet_set_name = "Sheets";
    s.use_subset = true;
    s.sheet_subset_name = "Subset";
    s.select_dates = true;
    s.dates = {{2460000, 36000}, {2460100, 43200}};
    s.select_range = true;
    s.start_time = 21600;
    s.end_time = 64800;
    s.interval = 3600;
    s.hours = {0, 7, 12, 23};
    s.shade_plot_type = 2;
    s.viewports_per_page = 4;
    s.rows = 2;
    s.columns = 2;
    s.spacing = 1.25;
    s.lock_viewports = true;
    s.label_viewports = true;
    s.page_setup_handle = s.view_handle = s.visual_style_handle = "0";      // none, as they are read
    return s;
}

const Object* the_object(const Drawing& d, ObjectType type) {
    const Object* found = nullptr;
    for (const Object& o : d.objects()) if (o.type == type) found = &o;
    return found;
}

void expect_matrix(const hdxf_test::MatrixReport& report, int generated, int named, int verified, int not_tested, const std::string& what) {
    assert_eq(generated, report.generated, what + ": cells generated, read back and written again");
    assert_eq(named, report.named_errors, what + ": named version errors");
    assert_eq(verified, report.autocad_verified, what + ": cells AutoCAD has verified");
    assert_eq(4, report.autocad_not_savable, what + ": R13 and R14");
    assert_eq(not_tested, report.autocad_not_tested, what + ": cells ej testat");
}

} // namespace

TEST(render_version_matrix_light) {
    const EntityCodec* codec = TypedRegistry::instance().find_entity("LIGHT");
    assert_true(codec != nullptr, "a codec");
    const LightData want = a_light();
    auto add = [&](Drawing& d) {
        Entity e;
        e.type = EntityType::Light;
        e.data = hdxf::Boxed<LightData>(want);
        d.add_entity(e);
    };
    auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
        Drawing& m = const_cast<Drawing&>(r);
        std::vector<Entity*> lights = lights_of(m);
        if (lights.size() != 1) return "the light is not there";
        if (describe(lights[0]->as_light()) != describe(want)) return "values: " + describe(lights[0]->as_light());
        return {};
    };
    // 2000 … 2018 (AutoCAD writes lights in 2000 and 2004 too), R12 none, R13/R14 not saved by AutoCAD 2025
    expect_matrix(hdxf_test::run_version_matrix("LIGHT", codec->versions, add, verify), 12, 6, 14, 0, "LIGHT");
}

TEST(render_version_matrix_sun) {
    const ObjectCodec* codec = TypedRegistry::instance().find_object("SUN");
    assert_true(codec != nullptr, "a codec");
    const hdxf::SunData want = a_sun();
    auto add = [&](Drawing& d) { d.objects().push_back(make_object(ObjectType::Sun, "7E10", want)); };
    auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
        const Object* o = the_object(r, ObjectType::Sun);
        if (!o) return "no sun";
        if (describe(o->as_sun()) != describe(want)) return "values: " + describe(o->as_sun());
        return {};
    };
    // 2007 … 2018; before, the error "SUN requires DXF 2007 or later" (R12, R13, R14, 2000, 2004)
    expect_matrix(hdxf_test::run_version_matrix("SUN", codec->versions, add, verify), 8, 10, 14, 0, "SUN");
}

TEST(render_version_matrix_rapidrt_presets_and_environment) {
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("RAPIDRTRENDERSETTINGS");
        assert_true(codec != nullptr, "a codec");
        const hdxf::RapidRtRenderSettingsData want = a_preset();
        auto add = [&](Drawing& d) { d.add_rapidrt_render_settings(want); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::RapidRtRenderSettings);
            if (!o) return "no preset";
            if (describe(o->as_rapidrt_settings()) != describe(want)) return "values: " + describe(o->as_rapidrt_settings());
            return {};
        };
        expect_matrix(hdxf_test::run_version_matrix("RAPIDRTRENDERSETTINGS", codec->versions, add, verify), 12, 6, 14, 0, "RAPIDRTRENDERSETTINGS");
        assert_eq(std::string("A RAPIDRT render preset requires DXF 2013 or later"), version_error_text("RAPIDRTRENDERSETTINGS", codec->versions, DxfVersion::R12), "the text as it was");
    }
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("RAPIDRTRENDERENVIRONMENT");
        assert_true(codec != nullptr, "a codec");
        const hdxf::RapidRtRenderEnvironmentData want = an_ibl();
        auto add = [&](Drawing& d) { d.set_rapidrt_render_environment(want); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::RapidRtRenderEnvironment);
            if (!o) return "no environment";
            if (describe(o->as_rapidrt_environment()) != describe(want)) return "values: " + describe(o->as_rapidrt_environment());
            return {};
        };
        expect_matrix(hdxf_test::run_version_matrix("RAPIDRTRENDERENVIRONMENT", codec->versions, add, verify), 12, 6, 14, 0, "RAPIDRTRENDERENVIRONMENT");
    }
}

TEST(render_version_matrix_mentalray_and_render_settings) {
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("MENTALRAYRENDERSETTINGS");
        assert_true(codec != nullptr, "a codec");
        const hdxf::MentalRayRenderSettingsData want = a_mentalray();
        auto add = [&](Drawing& d) { d.set_mentalray_render_settings(want); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::MentalRayRenderSettings);
            if (!o) return "no preset";
            if (describe(o->as_mentalray_settings()) != describe(want)) return "values: " + describe(o->as_mentalray_settings());
            return {};
        };
        // AutoCAD 2025 has the record in 2000 … 2013 (12 cells) and in no 2018 file: the 2018 cells are ej testat (2)
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("MENTALRAYRENDERSETTINGS", codec->versions, add, verify);
        assert_eq(12, report.generated, "2000 … 2018");
        assert_eq(6, report.named_errors, "R12, R13, R14");
        assert_eq(12, report.autocad_verified, "R12 (none) and 2000 … 2013");
        assert_eq(2, report.autocad_not_tested, "2018 ASCII and binary: ej testat (no MentalRay record in AutoCAD 2025's 2018 files)");
    }
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("RENDERSETTINGS");
        assert_true(codec != nullptr, "a codec");
        const hdxf::RenderSettingsData want = a_render_settings();
        auto add = [&](Drawing& d) { d.objects().push_back(make_object(ObjectType::RenderSettings, "7E11", hdxf::Boxed<hdxf::RenderSettingsData>(want))); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::RenderSettings);
            if (!o) return "no settings";
            if (describe(o->as_render_settings()) != describe(want)) return "values: " + describe(o->as_render_settings());
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("RENDERSETTINGS", codec->versions, add, verify);
        assert_eq(12, report.generated, "2000 … 2018");
        assert_eq(6, report.named_errors, "R12, R13, R14");
        assert_eq(0, report.autocad_verified, "no AutoCAD file has a record: ej testat");
        assert_eq(14, report.autocad_not_tested, "ej testat in every version that has the type, and in R12");
    }
}

TEST(render_version_matrix_environment_global_light_list_and_sun_study) {
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("RENDERENVIRONMENT");
        assert_true(codec != nullptr, "a codec");
        const hdxf::RenderEnvironmentData want = an_environment();
        auto add = [&](Drawing& d) { d.set_render_environment(want); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::RenderEnvironment);
            if (!o) return "no environment";
            if (describe(o->as_render_environment()) != describe(want)) return "values: " + describe(o->as_render_environment());
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("RENDERENVIRONMENT", codec->versions, add, verify);
        assert_eq(8, report.generated, "2007 … 2018");
        assert_eq(10, report.named_errors, "before 2007");
        assert_eq(18 - 4, report.autocad_not_tested, "ej testat (F9.13)");
    }
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("RENDERGLOBAL");
        assert_true(codec != nullptr, "a codec");
        const hdxf::RenderGlobalData want = a_global();
        auto add = [&](Drawing& d) { d.set_render_global(want); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::RenderGlobal);
            if (!o) return "no global settings";
            if (describe(o->as_render_global()) != describe(want)) return "values: " + describe(o->as_render_global());
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("RENDERGLOBAL", codec->versions, add, verify);
        assert_eq(8, report.generated, "2007 … 2018");
        assert_eq(10, report.named_errors, "before 2007");
    }
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("LIGHTLIST");
        assert_true(codec != nullptr, "a codec");
        // (without lights: a drawing with a LIGHT would name the LIGHT's version first; the members are in the relations test)
        hdxf::LightListData want;
        auto add = [&](Drawing& d) { d.set_light_list(want); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::LightList);
            if (!o) return "no light list";
            if (describe(o->as_light_list()) != describe(want)) return "values: " + describe(o->as_light_list());
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("LIGHTLIST", codec->versions, add, verify);
        assert_eq(8, report.generated, "2007 … 2018");
        assert_eq(10, report.named_errors, "before 2007");
    }
    {
        const ObjectCodec* codec = TypedRegistry::instance().find_object("SUNSTUDY");
        assert_true(codec != nullptr, "a codec");
        const hdxf::SunStudyData want = a_sun_study();
        auto add = [&](Drawing& d) { d.objects().push_back(make_object(ObjectType::SunStudy, "7E12", hdxf::Boxed<hdxf::SunStudyData>(want))); };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = the_object(r, ObjectType::SunStudy);
            if (!o) return "no sun study";
            if (describe(o->as_sun_study()) != describe(want)) return "values: " + describe(o->as_sun_study());
            return {};
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("SUNSTUDY", codec->versions, add, verify);
        assert_eq(8, report.generated, "2007 … 2018: the hours 0, 7, 12 and 23 in ASCII and binary alike");
        assert_eq(10, report.named_errors, "before 2007");
        assert_eq(0, report.autocad_verified, "no AutoCAD file: ej testat (F9.15)");
    }
}

// ---------------------------------------------------------------------------
// The sun study: the hours are numbers, never flags
// ---------------------------------------------------------------------------

TEST(render_sun_study_hours_are_numbers_in_both_formats) {
    hdxf::SunStudyData s = a_sun_study();
    s.hours = {0, 1, 7, 12, 13, 23, 255};
    for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2013);
        d.objects().push_back(make_object(ObjectType::SunStudy, "7E20", hdxf::Boxed<hdxf::SunStudyData>(s)));
        std::vector<uint8_t> bytes = save(d, format, "sun study");
        Drawing back = load(bytes, "sun study");
        const Object* o = the_object(back, ObjectType::SunStudy);
        assert_true(o != nullptr, "the study");
        assert_true(o->as_sun_study().hours == s.hours, "the hours as they were given");
        // unchanged, then the same bytes
        std::vector<uint8_t> again = save(back, format, "sun study again");
        assert_true(again == bytes, "written again: " + first_difference(bytes, again));
        // and as the other format: still the numbers
        DxfFormat other = format == DxfFormat::Ascii ? DxfFormat::Binary : DxfFormat::Ascii;
        Drawing other_back = load(save(back, other, "converted"), "converted");
        assert_true(the_object(other_back, ObjectType::SunStudy)->as_sun_study().hours == s.hours, "the other format: the hours");
    }
    // in the ASCII file the groups are 290 with the number
    Drawing d(DxfVersion::R2013);
    d.objects().push_back(make_object(ObjectType::SunStudy, "7E20", hdxf::Boxed<hdxf::SunStudyData>(s)));
    std::vector<Rec> recs = ascii_records(save(d, DxfFormat::Ascii, "sun study"));
    std::vector<const Rec*> found = records_named(recs, "SUNSTUDY");
    assert_eq(1, static_cast<int>(found.size()), "the record");
    std::vector<std::string> hours;
    for (const Group& g : after_marker(*found[0], "AcDbSunStudy")) {
        // after the optional sheet-set 290 and before the pointers: the hours
        if (g.first == 290) hours.push_back(trim(g.second));
    }
    assert_true(hours.size() >= s.hours.size(), "the hours are group 290");
    std::vector<std::string> tail(hours.end() - static_cast<long>(s.hours.size()), hours.end());
    assert_true(tail == std::vector<std::string>({"0", "1", "7", "12", "13", "23", "255"}), "the numbers, not 0 and 1");
}

TEST(render_sun_study_hour_that_is_no_byte_is_refused_before_anything_is_written) {
    for (int bad : {-1, 256, 100000}) {
        hdxf::SunStudyData s = a_sun_study();
        s.hours = {12, bad};
        Drawing d(DxfVersion::R2013);
        d.objects().push_back(make_object(ObjectType::SunStudy, "7E21", hdxf::Boxed<hdxf::SunStudyData>(s)));
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "no file for the hour " + std::to_string(bad));
        assert_true(d.last_error().find("hour") != std::string::npos, "the error names the hour: " + d.last_error());
    }
}

// What the sheet-set and range branches leave out is left out
TEST(render_sun_study_optional_groups_are_present_only_when_set) {
    hdxf::SunStudyData s = a_sun_study();
    s.sheet_set_name.reset();
    s.use_subset.reset();
    s.sheet_subset_name.reset();
    s.select_range = false;
    s.start_time.reset();
    s.end_time.reset();
    s.interval.reset();
    s.text_style_handle.reset();
    Drawing d(DxfVersion::R2013);
    d.objects().push_back(make_object(ObjectType::SunStudy, "7E22", hdxf::Boxed<hdxf::SunStudyData>(s)));
    std::vector<Rec> recs = ascii_records(save(d, DxfFormat::Ascii, "sun study"));
    std::vector<int> codes;
    for (const Group& g : after_marker(*records_named(recs, "SUNSTUDY")[0], "AcDbSunStudy")) codes.push_back(g.first);
    const std::vector<int> expected = {90, 1, 2, 70, 291, 91, 90, 90, 90, 90, 292, 73, 290, 290, 290, 290, 340, 341, 342, 74, 75, 76, 77, 40, 293, 294};
    assert_true(codes == expected, "the groups of a study without the sheet set, the range and the text style");
    Drawing back = load(save(d, DxfFormat::Ascii, "sun study"), "sun study");
    assert_eq(describe(s), describe(the_object(back, ObjectType::SunStudy)->as_sun_study()), "read back");
    // with all of them: the order of the reference
    Drawing full(DxfVersion::R2013);
    hdxf::SunStudyData f = a_sun_study();
    f.text_style_handle = "0";
    full.objects().push_back(make_object(ObjectType::SunStudy, "7E23", hdxf::Boxed<hdxf::SunStudyData>(f)));
    std::vector<Rec> frecs = ascii_records(save(full, DxfFormat::Ascii, "full study"));
    std::vector<int> fcodes;
    for (const Group& g : after_marker(*records_named(frecs, "SUNSTUDY")[0], "AcDbSunStudy")) fcodes.push_back(g.first);
    const std::vector<int> fexpected = {90, 1, 2, 70, 3, 290, 4, 291, 91, 90, 90, 90, 90, 292, 93, 94, 95, 73, 290, 290, 290, 290,
                                        340, 341, 342, 74, 75, 76, 77, 40, 293, 294, 343};
    assert_true(fcodes == fexpected, "the reference's order");
}

// ---------------------------------------------------------------------------
// Changing what AutoCAD saved: a scalar, an optional group, a list
// ---------------------------------------------------------------------------

TEST(render_changes_to_autocads_records_touch_only_what_changed) {
    for (const char* tag : {"2000", "2007", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            const DxfFormat format = binary ? DxfFormat::Binary : DxfFormat::Ascii;
            // a light: a scalar, the true color made, an index color made, a longer name
            {
                const std::string name = facit_name(tag, "lights", binary);
                std::vector<uint8_t> in = facit_bytes(name);
                Drawing d = load(in, name);
                light_named(d, "HDXF_POINT")->as_light().intensity = 4.75;
                light_named(d, "HDXF_SPOT")->as_light().color = hdxf::Color{23, 0x112233, {}};
                light_named(d, "HDXF_DISTANT")->as_light().name += std::string(300, 'n') + "\xE2\x88\x85";
                std::vector<uint8_t> out = save(d, format, name);
                Drawing back = load(out, name + " changed");
                assert_eq(4.75, light_named(back, "HDXF_POINT")->as_light().intensity, name + ": scalar");
                const hdxf::Color c = light_named(back, "HDXF_SPOT")->as_light().color;
                assert_eq(0x112233, c.rgb, name + ": the true color made");
                assert_true(light_named(back, "HDXF_DISTANT" + std::string(300, 'n') + "\xE2\x88\x85") != nullptr, name + ": a long UTF-8 name");
                // the point light is as it was, but for the intensity
                LightData want = expected_point();
                want.intensity = 4.75;
                assert_eq(describe(want), describe(light_named(back, "HDXF_POINT")->as_light()), name + ": the rest of the light is as it was");
            }
            // a MentalRay preset and a RapidRT preset, the IBL environment, the sun
            if (std::string(tag) != "2018") {
                const std::string name = facit_name(tag, "modeldoc_views", binary);
                std::vector<uint8_t> in = facit_bytes(name);
                Drawing d = load(in, name);
                Object* m = objects_of(d, ObjectType::MentalRayRenderSettings)[0];
                const std::string before = describe(m->as_mentalray_settings());
                m->as_mentalray_settings().photons_per_light = 12345;
                m->as_mentalray_settings().export_file = "changed.mi";
                m->as_mentalray_settings().base.description = "changed";
                Object* r = objects_of(d, ObjectType::RapidRtRenderSettings)[0];
                r->as_rapidrt_settings().levels = 99;
                std::vector<uint8_t> out = save(d, format, name);
                Drawing back = load(out, name + " changed");
                const hdxf::MentalRayRenderSettingsData& got = objects_of(back, ObjectType::MentalRayRenderSettings)[0]->as_mentalray_settings();
                assert_eq(12345, got.photons_per_light, name + ": photons per light");
                assert_eq(std::string("changed.mi"), got.export_file, name + ": file name");
                assert_eq(std::string("changed"), got.base.description, name + ": description");
                assert_eq(99, objects_of(back, ObjectType::RapidRtRenderSettings)[0]->as_rapidrt_settings().levels, name + ": levels");
                hdxf::MentalRayRenderSettingsData expect = objects_of(d, ObjectType::MentalRayRenderSettings)[0]->as_mentalray_settings();
                assert_eq(describe(expect), describe(got), name + ": nothing else changed");
                (void)before;
            }
            {
                const std::string name = facit_name(tag, "render_modeldoc", binary);
                std::vector<uint8_t> in = facit_bytes(name);
                Drawing d = load(in, name);
                std::vector<Object*> envs = objects_of(d, ObjectType::RapidRtRenderEnvironment);
                assert_true(!envs.empty(), name + ": environments");
                envs[0]->as_rapidrt_environment().rotation = -120.5;
                envs[0]->as_rapidrt_environment().ibl_as_background = false;
                std::vector<uint8_t> out = save(d, format, name);
                Drawing back = load(out, name + " changed");
                const hdxf::RapidRtRenderEnvironmentData& e = objects_of(back, ObjectType::RapidRtRenderEnvironment)[0]->as_rapidrt_environment();
                assert_eq(-120.5, e.rotation, name + ": rotation");
                assert_false(e.ibl_as_background, name + ": the IBL map no longer the background");
            }
            if (std::string(tag) != "2000") {
                const std::string name = facit_name(tag, "objects", binary);
                std::vector<uint8_t> in = facit_bytes(name);
                Drawing d = load(in, name);
                std::vector<Object*> suns = objects_of(d, ObjectType::Sun);
                if (tag[3] == '7' || tag[2] == '1') {
                    assert_true(!suns.empty(), name + ": suns");
                    suns[0]->as_sun().intensity = 3.25;
                    suns[0]->as_sun().julian_day += 10;
                    suns[0]->as_sun().color.rgb = 0x00FF00;
                    std::vector<uint8_t> out = save(d, format, name);
                    Drawing back = load(out, name + " changed");
                    const hdxf::SunData& s = objects_of(back, ObjectType::Sun)[0]->as_sun();
                    assert_eq(3.25, s.intensity, name + ": intensity");
                    assert_eq(0x00FF00, s.color.rgb, name + ": color");
                    assert_eq(describe(suns[0]->as_sun()), describe(s), name + ": the rest");
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Malformed and future records stay untyped, kept as read, with a warning that names the type (B105)
// ---------------------------------------------------------------------------

namespace {

struct Malformed {
    const char* what;
    Rec record;
};

Rec head_of(const char* type, const char* handle) {
    return {{0, type}, {5, handle}, {330, "0"}};
}

Rec with(Rec r, const std::vector<Group>& more) {
    r.insert(r.end(), more.begin(), more.end());
    return r;
}

std::vector<Group> light_groups(bool with_color_90, const std::string& color_90 = "-1023410177") {
    std::vector<Group> g = {{100, "AcDbEntity"}, {8, "0"}, {100, "AcDbLight"}, {90, "1"}, {1, "L"}, {70, "2"}, {290, "1"}};
    if (with_color_90) g.push_back({90, color_90});
    else {
        g.push_back({63, "7"});
    }
    const std::vector<Group> rest = {{291, "0"}, {40, "1.0"}, {10, "0"}, {20, "0"}, {30, "0"}, {11, "0"}, {21, "0"}, {31, "-1"},
                                     {72, "0"}, {292, "0"}, {41, "1.0"}, {42, "10.0"}, {50, "45.0"}, {51, "50.0"}, {293, "1"},
                                     {73, "0"}, {91, "256"}, {280, "1"}};
    g.insert(g.end(), rest.begin(), rest.end());
    return g;
}

} // namespace

TEST(render_malformed_objects_are_kept_untyped_with_a_warning) {
    const std::vector<Group> base_ok = {{100, "AcDbRenderSettings"}, {90, "2"}, {1, "n"}, {290, "1"}, {290, "1"}, {290, "1"}, {290, "1"},
                                        {1, ""}, {1, ""}, {90, "0"}};
    std::vector<Group> base_reference_form = base_ok;
    base_reference_form[4] = {90, "2"};          // the older reference's texture sampling as a 90
    std::vector<Group> mental_short = base_ok;
    mental_short.insert(mental_short.end(), {{100, "AcDbMentalRayRenderSettings"}, {90, "2"}, {90, "0"}, {90, "1"}});
    std::vector<Group> mental_bad_number = base_ok;
    mental_bad_number.insert(mental_bad_number.end(), {{100, "AcDbMentalRayRenderSettings"}, {90, "2"}, {90, "zero"}});
    const std::vector<Malformed> cases = {
        {"RENDERSETTINGS with the reference's 90 for the texture sampling", with(head_of("RENDERSETTINGS", "7D01"), base_reference_form)},
        {"RENDERSETTINGS without its subclass", with(head_of("RENDERSETTINGS", "7D02"), {{100, "AcDbSomethingElse"}, {90, "1"}})},
        {"MENTALRAYRENDERSETTINGS cut off after the sampling rates", with(head_of("MENTALRAYRENDERSETTINGS", "7D03"), mental_short)},
        {"MENTALRAYRENDERSETTINGS with a number that is none", with(head_of("MENTALRAYRENDERSETTINGS", "7D04"), mental_bad_number)},
        {"RAPIDRTRENDERSETTINGS without its own subclass", with(head_of("RAPIDRTRENDERSETTINGS", "7D05"), base_ok)},
        {"RAPIDRTRENDERENVIRONMENT cut off", with(head_of("RAPIDRTRENDERENVIRONMENT", "7D06"), {{100, "AcDbIBLBackground"}, {90, "2"}, {290, "1"}})},
        {"RENDERENVIRONMENT with an RGB channel above 255", with(head_of("RENDERENVIRONMENT", "7D07"),
             {{100, "AcDbRenderEnvironment"}, {90, "1"}, {290, "0"}, {290, "0"}, {280, "0"}, {280, "300"}, {280, "0"}, {40, "0"}, {40, "1"}, {40, "0"}, {40, "1"}, {290, "0"}, {1, ""}})},
        {"RENDERGLOBAL cut off", with(head_of("RENDERGLOBAL", "7D08"), {{100, "AcDbRenderGlobal"}, {90, "2"}, {90, "0"}})},
        {"LIGHTLIST with a count the groups cannot hold", with(head_of("LIGHTLIST", "7D09"), {{100, "AcDbLightList"}, {90, "1"}, {90, "5000000"}, {5, "AA"}, {1, "x"}})},
        {"LIGHTLIST with a member without its name", with(head_of("LIGHTLIST", "7D0A"), {{100, "AcDbLightList"}, {90, "1"}, {90, "1"}, {5, "AA"}})},
        {"SUNSTUDY with a count the groups cannot hold", with(head_of("SUNSTUDY", "7D0B"),
             {{100, "AcDbSunStudy"}, {90, "1"}, {1, "s"}, {2, ""}, {70, "0"}, {291, "1"}, {91, "9999999"}, {90, "1"}, {90, "2"}})},
        {"SUNSTUDY with an hour that is no number", with(head_of("SUNSTUDY", "7D0C"),
             {{100, "AcDbSunStudy"}, {90, "1"}, {1, "s"}, {2, ""}, {70, "0"}, {291, "0"}, {91, "0"}, {292, "0"}, {73, "1"}, {290, "noon"}})},
        {"SUNSTUDY without its subclass", with(head_of("SUNSTUDY", "7D0D"), {{100, "AcDbOther"}, {90, "1"}})},
        {"SUN cut off", with(head_of("SUN", "7D0E"), {{100, "AcDbSun"}, {90, "1"}, {290, "1"}})},
    };
    for (const Malformed& c : cases) {
        const std::string type = c.record[0].second;
        std::string text = drawing_with_objects({c.record});
        Drawing d = load_text(text);
        const Object* o = d.find_object(c.record[1].second);
        assert_true(o != nullptr, std::string(c.what) + ": the record");
        assert_true(o->type == ObjectType::Unknown, std::string(c.what) + ": left untyped");
        bool warned = false;
        for (const std::string& w : d.warnings()) warned = warned || w.find(type) != std::string::npos;
        assert_true(warned, std::string(c.what) + ": a warning that names the type");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, c.what);
        assert_true(hdxf_test::lf_text(out).find(record_text(c.record)) != std::string::npos, std::string(c.what) + ": kept as read");
    }
}

TEST(render_malformed_light_is_kept_untyped_and_a_later_version_keeps_its_groups) {
    // a LIGHT in ENTITIES with a color 90 of 0x01020304 stays untyped; with a 0xC3 color it is typed
    for (const char* color : {"16909060", "-1023410177"}) {
        Drawing d(DxfVersion::R2000);
        d.add_line({0, 0, 0}, {1, 1, 0});
        std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
        std::string text = hdxf_test::lf_text(bytes);
        size_t at = text.find("  0\nENDSEC\n  0\nSECTION\n  2\nOBJECTS");
        assert_true(at != std::string::npos, "the end of ENTITIES");
        Rec r = with({{0, "LIGHT"}, {5, "7D21"}, {330, "1F"}}, light_groups(true, color));
        text = text.substr(0, at) + record_text(r) + text.substr(at);
        Drawing back = load_text(text);
        const Entity* light = nullptr;
        for (const Entity& e : back.entities()) if (e.props.handle == "7D21") light = &e;
        assert_true(light != nullptr, "the record");
        const bool good = std::string(color) == "-1023410177";
        assert_true((light->type == EntityType::Light) == good, std::string("a color 90 of ") + color + (good ? " is typed" : " leaves the record untyped"));
        if (!good) {
            bool warned = false;
            for (const std::string& w : back.warnings()) warned = warned || w.find("LIGHT") != std::string::npos;
            assert_true(warned, "a warning names the LIGHT");
        }
    }
}

// ---------------------------------------------------------------------------
// Classes: once each, as AutoCAD writes them
// ---------------------------------------------------------------------------

namespace {
Entity a_light_entity_for_structure() {
    Entity e;
    e.type = EntityType::Light;
    e.props.handle = "7E40";
    e.data = hdxf::Boxed<LightData>(a_light());
    return e;
}
} // namespace

TEST(render_classes_are_written_once_as_autocad_has_them) {
    Drawing d(DxfVersion::R2013);
    d.add_rapidrt_render_settings(a_preset());
    d.set_rapidrt_render_environment(an_ibl());
    d.set_mentalray_render_settings(a_mentalray());
    d.set_render_environment(an_environment());
    d.set_render_global(a_global());
    d.objects().push_back(make_object(ObjectType::Sun, "7E30", a_sun()));
    d.objects().push_back(make_object(ObjectType::SunStudy, "7E31", hdxf::Boxed<hdxf::SunStudyData>(a_sun_study())));
    Entity e;
    e.type = EntityType::Light;
    e.props.handle = "7E32";
    e.data = hdxf::Boxed<LightData>(a_light());
    d.add_entity(e);
    d.set_light_list(hdxf::LightListData{1, {{"7E32", "BUILT"}}});
    std::vector<Rec> recs = ascii_records(save(d, DxfFormat::Ascii, "classes"));
    std::map<std::string, std::vector<Rec>> classes;
    for (const Rec& r : recs) if (!r.empty() && r[0].second == "CLASS") classes[trim(r[1].second)].push_back(r);
    struct Want { const char* name; const char* cpp; const char* app; const char* flags; const char* entity; };
    const Want wanted[] = {
        {"RAPIDRTRENDERSETTINGS", "AcDbRapidRTRenderSettings", "ObjectDBX Classes", "1024", "0"},
        {"RAPIDRTRENDERENVIRONMENT", "AcDbIBLBackground", "SCENEOE", "1024", "0"},
        {"MENTALRAYRENDERSETTINGS", "AcDbMentalRayRenderSettings", "SCENEOE", "1024", "0"},
        {"RENDERENVIRONMENT", "AcDbRenderEnvironment", "SCENEOE", "1024", "0"},
        {"RENDERGLOBAL", "AcDbRenderGlobal", "SCENEOE", "1024", "0"},
        {"SUN", "AcDbSun", "SCENEOE", "1153", "0"},
        {"SUNSTUDY", "AcDbSunStudy", "SCENEOE", "1024", "0"},
        {"LIGHT", "AcDbLight", "SCENEOE", "1025", "1"},
        {"LIGHTLIST", "AcDbLightList", "SCENEOE", "1024", "0"},
    };
    for (const Want& w : wanted) {
        assert_eq(1, static_cast<int>(classes[w.name].size()), std::string(w.name) + ": one CLASS");
        const Rec& r = classes[w.name][0];
        assert_eq(std::string(w.cpp), trim(r[2].second), std::string(w.name) + ": the C++ name");
        assert_eq(std::string(w.app), trim(r[3].second), std::string(w.name) + ": the application");
        assert_eq(std::string(w.flags), trim(r[4].second), std::string(w.name) + ": the proxy flags");
        assert_eq(std::string(w.entity), trim(r.back().second), std::string(w.name) + ": the entity flag");
    }
    Drawing plain(DxfVersion::R2013);
    plain.add_entity(a_light_entity_for_structure());
    plain.set_mentalray_render_settings(a_mentalray());
    plain.set_render_environment(an_environment());
    plain.set_render_global(a_global());
    plain.add_rapidrt_render_settings(a_preset());
    plain.set_rapidrt_render_environment(an_ibl());
    plain.set_light_list(hdxf::LightListData{1, {{"7E40", "BUILT"}}});
    dxf_structure::check_structure(save(plain, DxfFormat::Ascii, "structure"), "render objects");
}
