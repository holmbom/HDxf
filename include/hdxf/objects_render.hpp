#pragma once

// The render presets, the render environment, the light list and the sun study.

#include "core.hpp"
#include "objects_base.hpp"

namespace hdxf {

// --- 9.11 render, lights and sun (plan 2026-10-27-fas-9-typer.md, step 9.11) ---

// AcDbRenderSettings: the base of every render preset (the RENDERSETTINGS record by itself, the base of the MentalRay
// preset). AutoCAD's files write the four flags as 290 (the reference's older table has the second as 90) and the
// version 2 (the reference says 1).
struct RenderSettingsData {
    int32_t version = 2;                // 90
    std::string name;                   // 1: the preset's name
    bool materials = true;              // 290
    bool texture_sampling = true;       // 290
    bool back_faces = true;             // 290
    bool shadows = true;                // 290
    std::string preview_file;           // 1 (can be blank)
    std::string description;            // 1
    int32_t display_index = 0;          // 90
    // The reference puts the predefined flag here (290 after the display index); AutoCAD's own files have it at the end of
    // the RapidRT subclass and none in the MentalRay record. Present only where the record has it
    std::optional<bool> predefined;
};

// MENTALRAYRENDERSETTINGS: AcDbRenderSettings then AcDbMentalRayRenderSettings (from AutoCAD 2007; the feature set is gone
// from AutoCAD 2016 but the data stays in the file). The groups are by position; the reference's names are the comments.
struct MentalRayRenderSettingsData {
    RenderSettingsData base;
    int32_t version = 2;                // 90
    int32_t min_samples = 0;            // 90: sampling rate (minimum)
    int32_t max_samples = 1;            // 90: sampling rate (maximum)
    SamplingFilter filter = SamplingFilter::Gauss;   // 70
    double filter_width = 3.0;          // 40
    double filter_height = 3.0;         // 40
    std::array<double, 4> contrast = {0.05, 0.05, 0.05, 0.05};   // 40 x 4: the sampling contrast color's red, green, blue, alpha
    int16_t shadow_mode = 0;            // 70: 0 simple, 1 sort, 2 segment
    bool shadow_maps = false;           // 290
    bool ray_tracing = true;            // 290
    int32_t trace_reflections = 5;      // 90
    int32_t trace_refractions = 5;      // 90
    int32_t trace_max = 5;              // 90
    bool global_illumination = false;   // 290
    int32_t photons_per_sample = 500;   // 90
    bool use_radius = false;            // 290: the global illumination radius flag
    double radius = 1.0;                // 40
    int32_t photons_per_light = 10000;  // 90
    int32_t photon_reflections = 5;     // 90
    int32_t photon_refractions = 5;     // 90
    int32_t photon_max = 5;             // 90
    bool final_gather = false;          // 290
    int32_t final_gather_rays = 200;    // 90
    bool use_min_radius = false;        // 290
    bool use_max_radius = false;        // 290
    bool radius_in_pixels = false;      // 290
    double min_radius = 1.0;            // 40
    double max_radius = 1.0;            // 40
    double luminance_scale = 1500.0;    // 40: the energy multiplier
    int16_t diagnostic_mode = 0;        // 70: 0 off, 1 grid, 2 photon, 4 BSP
    int16_t grid_mode = 0;              // 70: 0 object, 1 world, 2 camera
    double grid_size = 10.0;            // 40
    int16_t photon_mode = 0;            // 70: 0 density, 1 irradiance
    int16_t bsp_mode = 0;               // 70: 0 depth, 1 size
    bool export_statistics = false;     // 290
    std::string export_file;            // 1 (can be blank)
    int32_t tile_size = 32;             // 90
    int16_t tile_order = 0;             // 70: 0 Hilbert, 1 spiral, 2 left to right, 3 right to left, 4 top to bottom, 5 bottom to top
    int32_t memory_limit = 1048;        // 90
    // The last two groups of AutoCAD's own files (290 and 40, after the memory limit) that the reference's table lacks:
    // their meaning is not documented, so they are named by their place
    bool extra_flag = false;            // 290
    double extra_factor = 1.0;          // 40
};

// RENDERENVIRONMENT (AcDbRenderEnvironment; the root dictionary's ACAD_RENDER_ENVIRONMENT)
struct RenderEnvironmentData {
    int32_t version = 1;                // 90
    bool fog_enabled = false;           // 290
    bool fog_background = false;        // 290
    std::array<uint8_t, 3> fog_color = {0, 0, 0};   // 280 x 3: red, green, blue
    double near_density = 0.0;          // 40 (a percentage)
    double far_density = 100.0;         // 40
    double near_distance = 0.0;         // 40 (a percentage of the distance to the far clipping plane)
    double far_distance = 100.0;        // 40
    bool use_image = false;             // 290: the environment image flag
    std::string image_file;             // 1 (blank when the flag is 0)
};

// RENDERGLOBAL (AcDbRenderGlobal; the root dictionary's ACAD_RENDER_GLOBAL)
struct RenderGlobalData {
    int32_t version = 2;                // 90
    int32_t procedure = 0;              // 90: 0 view, 1 crop, 2 selection
    int32_t destination = 0;            // 90: 0 render window, 1 viewport
    bool save_to_file = false;          // 290
    std::string file_name;              // 1: the rendered image's file
    int32_t width = 640;                // 90
    int32_t height = 480;               // 90
    bool predefined_first = false;      // 290
    bool high_info = false;             // 290
};

// LIGHTLIST (AcDbLightList; the root dictionary's ACAD_LIGHT): the drawing's lights. Each member's 5 is the LIGHT entity's
// handle (a pointer: the list's own handle is the record's 5), the 1 its name.
struct LightListEntry {
    std::string light_handle;           // 5
    std::string name;                   // 1
};

struct LightListData {
    int32_t version = 1;                // 90
    std::vector<LightListEntry> lights; // 90 (the count) then 5 and 1 for each
};

// SUNSTUDY (AcDbSunStudy): the Sun Study wizard's setup. Groups as the reference has them; no AutoCAD file has one
// (the core has no command), so the whole record is ej testat. The hour is an integer: the reference gives it the code 290
// (a flag in the binary format), and HDxf writes and reads the number as it is, never as a flag.
struct SunStudyDate {
    int32_t julian_day = 0;             // 90
    int32_t seconds = 0;                // 90: seconds past midnight
};

struct SunStudyData {
    int32_t version = 1;                // 90
    std::string name;                   // 1: the sun setup's name
    std::string description;            // 2
    int16_t output_type = 0;            // 70
    // Included only when the output type is a sheet set
    std::optional<std::string> sheet_set_name;      // 3
    std::optional<bool> use_subset;                 // 290
    std::optional<std::string> sheet_subset_name;   // 4
    bool select_dates = false;          // 291: select dates from the calendar
    std::vector<SunStudyDate> dates;    // 91 (the count) then 90, 90 for each date picked
    bool select_range = false;          // 292: select a range of dates
    // Included only when the range flag is set
    std::optional<int32_t> start_time;  // 93
    std::optional<int32_t> end_time;    // 94
    std::optional<int32_t> interval;    // 95: seconds
    std::vector<int32_t> hours;         // 73 (the count) then 290 for each
    std::string page_setup_handle;      // 340: hard pointer; empty = none (written as 0)
    std::string view_handle;            // 341
    std::string visual_style_handle;    // 342
    int16_t shade_plot_type = 0;        // 74
    int16_t viewports_per_page = 1;     // 75
    int16_t rows = 1;                   // 76
    int16_t columns = 1;                // 77
    double spacing = 0.0;               // 40
    bool lock_viewports = false;        // 293
    bool label_viewports = false;       // 294
    std::optional<std::string> text_style_handle;   // 343 (2013's reference; not in 2007's)
};

} // namespace hdxf
