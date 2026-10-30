#include "RenderTypes.hpp"
#include <algorithm>

// The writers of step 9.11 (plan 2026-10-27-fas-9-typer.md): the render objects, the light list, the sun study, SUN and
// LIGHT. Every writer writes the whole record in the order AutoCAD's files have (the groups' meaning is by position
// where a code repeats); the readers are in objects_render.cpp and sun_study.cpp.
//
// The RAPIDRT presets and the IBL environment, the SUN and the LIGHT were written by DxfWriter's own functions until
// this step (dxf_writer_reference.cpp, dxf_writer_2000.cpp, dxf_writer_surface.cpp): the same groups in the same order
// (the existing tests and AutoCAD's files check the bytes), moved here so that there is one writer per type.

namespace hdxf {
namespace internal {

namespace {

// 2018 has no ACAD XDATA that holds only AcDbSavedByObjectVersion on a SUN or a LIGHT (AutoCAD 2025's files)
std::vector<XdataList> without_saved_by_version(const std::vector<XdataList>& xdata) {
    std::vector<XdataList> out;
    for (const XdataList& x : xdata) {
        bool saved_by = x.app_name == "ACAD" && x.items.size() == 2 && x.items[0].code == 1000 &&
                        x.items[0].value == "AcDbSavedByObjectVersion" && x.items[1].code == 1070;
        if (!saved_by) out.push_back(x);
    }
    return out;
}

bool record_exists(const Drawing& d, const std::string& handle) {
    if (d.find_object(handle) || d.find_entity(handle)) return true;
    for (const View& v : d.views()) if (v.handle == handle) return true;
    for (const TextStyle& s : d.text_styles()) if (s.handle == handle) return true;
    return false;
}

void write_render_base(RecordWriter& w, const RenderSettingsData& d) {
    w.subclass("AcDbRenderSettings");
    w.integer(90, d.version);
    w.text(1, d.name);
    w.boolean(290, d.materials);
    w.boolean(290, d.texture_sampling);
    w.boolean(290, d.back_faces);
    w.boolean(290, d.shadows);
    w.text(1, d.preview_file);
    w.text(1, d.description);
    w.integer(90, d.display_index);
    if (d.predefined) w.boolean(290, *d.predefined);
}

} // namespace

std::string existing_pointer(const RecordWriter& w, const std::string& handle) {
    if (handle.empty() || handle == "0") return "0";
    if (!w.filtering()) return handle;
    if (!w.live(handle)) return "0";
    // live(): a handle the drawing writes, or one that led nowhere already in the file it was read from (kept, B19).
    // A drawing built in memory has no file: there a handle no record has is one that was removed
    return w.read_from_file() || record_exists(w.drawing(), handle) ? handle : "0";
}

// The color of a LIGHT in DXF 2000 (AutoCAD 2025's files, fas9_render.scr): an AcCmEntityColor, a true color as 0xC2 and its
// three bytes (255, 128, 0: 0xC2FF8000; white 0xC2FFFFFF), an index color as 0xC3 and the index (1: 0xC3000001)
int32_t light_color_to_raw(const Color& color) {
    uint32_t raw = color.has_rgb() ? 0xC2000000u | (static_cast<uint32_t>(color.rgb) & 0xFFFFFFu)
                                   : 0xC3000000u | (static_cast<uint32_t>(color.aci) & 0xFFu);
    return static_cast<int32_t>(raw);
}

bool light_color_from_raw(int32_t raw, Color& color) {
    const uint32_t u = static_cast<uint32_t>(raw);
    const uint32_t method = u >> 24;
    if (method == 0xC3) {
        color = Color::from_aci(static_cast<int>(u & 0xFFu));
        return true;
    }
    if (method != 0xC2) return false;
    const int rgb = static_cast<int>(u & 0xFFFFFFu);
    color = Color{nearest_aci(rgb), rgb, {}};
    return true;
}

void write_render_settings_record(RecordWriter& w, const Object& o) {
    w.object_head("RENDERSETTINGS", o);
    write_render_base(w, o.as_render_settings());
    w.xdata(o.xdata);
}

void write_mentalray_settings_record(RecordWriter& w, const Object& o) {
    const MentalRayRenderSettingsData& d = o.as_mentalray_settings();
    w.object_head("MENTALRAYRENDERSETTINGS", o);
    write_render_base(w, d.base);
    w.subclass("AcDbMentalRayRenderSettings");
    w.integer(90, d.version);
    w.integer(90, d.min_samples);
    w.integer(90, d.max_samples);
    w.integer(70, static_cast<int>(d.filter));
    w.real(40, d.filter_width);
    w.real(40, d.filter_height);
    for (double v : d.contrast) w.real(40, v);
    w.integer(70, d.shadow_mode);
    w.boolean(290, d.shadow_maps);
    w.boolean(290, d.ray_tracing);
    w.integer(90, d.trace_reflections);
    w.integer(90, d.trace_refractions);
    w.integer(90, d.trace_max);
    w.boolean(290, d.global_illumination);
    w.integer(90, d.photons_per_sample);
    w.boolean(290, d.use_radius);
    w.real(40, d.radius);
    w.integer(90, d.photons_per_light);
    w.integer(90, d.photon_reflections);
    w.integer(90, d.photon_refractions);
    w.integer(90, d.photon_max);
    w.boolean(290, d.final_gather);
    w.integer(90, d.final_gather_rays);
    w.boolean(290, d.use_min_radius);
    w.boolean(290, d.use_max_radius);
    w.boolean(290, d.radius_in_pixels);
    w.real(40, d.min_radius);
    w.real(40, d.max_radius);
    w.real(40, d.luminance_scale);
    w.integer(70, d.diagnostic_mode);
    w.integer(70, d.grid_mode);
    w.real(40, d.grid_size);
    w.integer(70, d.photon_mode);
    w.integer(70, d.bsp_mode);
    w.boolean(290, d.export_statistics);
    w.text(1, d.export_file);
    w.integer(90, d.tile_size);
    w.integer(70, d.tile_order);
    w.integer(90, d.memory_limit);
    w.boolean(290, d.extra_flag);
    w.real(40, d.extra_factor);
    w.xdata(o.xdata);
}

// The RAPIDRT preset: the predefined flag last, after the filter size (AutoCAD's own file; the reference puts it after
// the display index, which the reader accepts too)
void write_rapidrt_settings_record(RecordWriter& w, const Object& o) {
    const RapidRtRenderSettingsData& d = o.as_rapidrt_settings();
    w.object_head("RAPIDRTRENDERSETTINGS", o);
    w.subclass("AcDbRenderSettings");
    w.integer(90, d.base_version);
    w.text(1, d.name);
    w.boolean(290, d.materials);
    w.boolean(290, d.texture_sampling);
    w.boolean(290, d.back_faces);
    w.boolean(290, d.shadows);
    w.text(1, d.preview_file);
    w.text(1, d.description);
    w.integer(90, d.display_index);
    w.subclass("AcDbRapidRTRenderSettings");
    w.integer(90, d.version);
    w.integer(70, static_cast<int>(d.duration_type));
    w.integer(90, d.levels);
    w.integer(90, d.duration);
    w.integer(70, static_cast<int>(d.accuracy));
    w.integer(70, static_cast<int>(d.filter));
    w.real(40, d.filter_width);
    w.real(40, d.filter_height);
    w.boolean(290, d.predefined);
    w.xdata(o.xdata);
}

void write_render_environment_record(RecordWriter& w, const Object& o) {
    const RenderEnvironmentData& d = o.as_render_environment();
    w.object_head("RENDERENVIRONMENT", o);
    w.subclass("AcDbRenderEnvironment");
    w.integer(90, d.version);
    w.boolean(290, d.fog_enabled);
    w.boolean(290, d.fog_background);
    for (uint8_t channel : d.fog_color) w.integer(280, channel);
    w.real(40, d.near_density);
    w.real(40, d.far_density);
    w.real(40, d.near_distance);
    w.real(40, d.far_distance);
    w.boolean(290, d.use_image);
    w.text(1, d.image_file);
    w.xdata(o.xdata);
}

void write_render_global_record(RecordWriter& w, const Object& o) {
    const RenderGlobalData& d = o.as_render_global();
    w.object_head("RENDERGLOBAL", o);
    w.subclass("AcDbRenderGlobal");
    w.integer(90, d.version);
    w.integer(90, d.procedure);
    w.integer(90, d.destination);
    w.boolean(290, d.save_to_file);
    w.text(1, d.file_name);
    w.integer(90, d.width);
    w.integer(90, d.height);
    w.boolean(290, d.predefined_first);
    w.boolean(290, d.high_info);
    w.xdata(o.xdata);
}

void write_rapidrt_environment_record(RecordWriter& w, const Object& o) {
    const RapidRtRenderEnvironmentData& d = o.as_rapidrt_environment();
    w.object_head("RAPIDRTRENDERENVIRONMENT", o);
    w.subclass("AcDbIBLBackground");
    w.integer(90, d.version);
    w.boolean(290, d.enabled);
    w.text(1, d.ibl_file);
    w.real(40, d.rotation);
    w.boolean(290, d.ibl_as_background);
    const std::string background = existing_pointer(w, d.background_handle);
    if (background == "0" && !d.background_handle.empty() && d.background_handle != "0") {
        w.warn("RAPIDRTRENDERENVIRONMENT " + o.handle + ": the custom background " + d.background_handle +
               " no longer exists, set to none (0)");
    }
    w.handle(340, background);
    w.xdata(o.xdata);
}

// The members whose light is gone are left out and the count follows (B19)
void write_light_list_record(RecordWriter& w, const Object& o) {
    const LightListData& d = o.as_light_list();
    w.object_head("LIGHTLIST", o);
    w.subclass("AcDbLightList");
    w.integer(90, d.version);
    std::vector<const LightListEntry*> kept;
    for (const LightListEntry& e : d.lights) {
        if (existing_pointer(w, e.light_handle) != "0") {
            kept.push_back(&e);
        } else {
            w.warn("LIGHTLIST " + o.handle + ": the light " + e.light_handle + " (" + e.name + ") no longer exists, left out");
        }
    }
    w.integer(90, static_cast<int64_t>(kept.size()));
    for (const LightListEntry* e : kept) {
        w.handle(5, e->light_handle);
        w.text(1, e->name);
    }
    w.xdata(o.xdata);
}

void write_sun_study_record(RecordWriter& w, const Object& o) {
    const SunStudyData& d = o.as_sun_study();
    for (int32_t hour : d.hours) {
        if (hour < 0 || hour > 255) {
            w.fail("SUNSTUDY " + o.handle + ": the hour " + std::to_string(hour) + " does not fit the one byte the hour (group 290) has in a binary file");
            return;
        }
    }
    w.object_head("SUNSTUDY", o);
    w.subclass("AcDbSunStudy");
    w.integer(90, d.version);
    w.text(1, d.name);
    w.text(2, d.description);
    w.integer(70, d.output_type);
    if (d.sheet_set_name) w.text(3, *d.sheet_set_name);
    if (d.use_subset) w.boolean(290, *d.use_subset);
    if (d.sheet_subset_name) w.text(4, *d.sheet_subset_name);
    w.boolean(291, d.select_dates);
    w.integer(91, static_cast<int64_t>(d.dates.size()));
    for (const SunStudyDate& date : d.dates) {
        w.integer(90, date.julian_day);
        w.integer(90, date.seconds);
    }
    w.boolean(292, d.select_range);
    if (d.start_time) w.integer(93, *d.start_time);
    if (d.end_time) w.integer(94, *d.end_time);
    if (d.interval) w.integer(95, *d.interval);
    w.integer(73, static_cast<int64_t>(d.hours.size()));
    // The hour is the number itself (group 290 as AutoCAD's reference has it): not a flag
    for (int32_t hour : d.hours) w.integer(290, hour);
    auto pointer = [&](int code, const std::string& handle, const char* what) {
        const std::string h = existing_pointer(w, handle);
        if (h == "0" && !handle.empty() && handle != "0") {
            w.warn("SUNSTUDY " + o.handle + ": the " + what + " " + handle + " no longer exists, set to none (0)");
        }
        w.handle(code, h);
    };
    pointer(340, d.page_setup_handle, "page setup");
    pointer(341, d.view_handle, "view");
    pointer(342, d.visual_style_handle, "visual style");
    w.integer(74, d.shade_plot_type);
    w.integer(75, d.viewports_per_page);
    w.integer(76, d.rows);
    w.integer(77, d.columns);
    w.real(40, d.spacing);
    w.boolean(293, d.lock_viewports);
    w.boolean(294, d.label_viewports);
    if (d.text_style_handle) pointer(343, *d.text_style_handle, "text style");
    w.xdata(o.xdata);
}

void write_sun_record(RecordWriter& w, const Object& o) {
    const SunData& d = o.as_sun();
    w.object_head("SUN", o);
    w.subclass("AcDbSun");
    w.integer(90, d.version);
    w.boolean(290, d.on);
    w.integer(63, d.color.aci);
    if (d.color.has_rgb()) w.integer(421, d.color.rgb);
    w.real(40, d.intensity);
    w.boolean(291, d.shadows);
    w.integer(91, d.julian_day);
    w.integer(92, d.time);
    w.boolean(292, d.daylight_saving);
    w.integer(70, d.shadow_type);
    w.integer(71, d.shadow_map_size);
    w.integer(280, d.shadow_softness);
    w.xdata(w.version() >= DxfVersion::R2018 ? without_saved_by_version(o.xdata) : o.xdata);
}

// LIGHT: AcDbLight's groups in AutoCAD's order; its proxy graphics AutoCAD's empty ones (8 bytes, no commands). The color is
// 63 with 421 from 2004, one 90 in 2000 (AutoCAD 2025's files)
void write_light_record(RecordWriter& w, const Entity& e, const std::string& owner) {
    const LightData& d = e.as_light();
    std::vector<uint8_t> proxy;
    if (w.proxy_graphics_enabled()) proxy = {8, 0, 0, 0, 0, 0, 0, 0};
    w.entity_head("LIGHT", e, owner, &proxy);
    w.subclass("AcDbLight");
    w.integer(90, d.version);
    w.text(1, d.name);
    w.integer(70, d.light_type);
    w.boolean(290, d.on);
    if (w.version() == DxfVersion::R2000) {
        w.integer(90, light_color_to_raw(d.color));
    } else {
        w.integer(63, d.color.aci);
        if (d.color.has_rgb()) w.integer(421, d.color.rgb);
    }
    w.boolean(291, d.plot_glyph);
    w.real(40, d.intensity);
    w.point3(10, d.position);
    w.point3(11, d.target);
    w.integer(72, d.attenuation_type);
    w.boolean(292, d.use_limits);
    w.real(41, d.start_limit);
    w.real(42, d.end_limit);
    w.real(50, d.hotspot_angle);
    w.real(51, d.falloff_angle);
    w.boolean(293, d.cast_shadows);
    w.integer(73, d.shadow_type);
    w.integer(91, d.shadow_map_size);
    w.integer(280, d.shadow_softness);
    w.xdata(w.version() >= DxfVersion::R2018 ? without_saved_by_version(e.props.xdata) : e.props.xdata);
}

} // namespace internal
} // namespace hdxf
