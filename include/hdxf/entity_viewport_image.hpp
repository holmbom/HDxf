#pragma once

// VIEWPORT and IMAGE.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// VIEWPORT (paper space)
// ---------------------------------------------------------------------------

// In R12 the view is ACAD/MVIEW XDATA on the entity; from 2000 it is these fields. A
// drawing written as 2000 that still has MVIEW XDATA (read from R12) takes the view from
// the XDATA.
struct ViewportData {
    Point3 center;
    double width = 0.0;
    double height = 0.0;
    int status = 0;     // group 68
    int id = 0;         // group 69
    // 2000+
    Point2 view_center;                     // 12, 22 (model space)
    Point2 snap_base;                       // 13, 23
    Point2 snap_spacing = {10.0, 10.0};     // 14, 24
    Point2 grid_spacing = {10.0, 10.0};     // 15, 25
    Point3 view_direction = {0.0, 0.0, 1.0}; // 16, 26, 36
    Point3 view_target;                     // 17, 27, 37
    double lens_length = 50.0;              // 42
    double front_clip = 0.0;                // 43
    double back_clip = 0.0;                 // 44
    double view_height = 0.0;               // 45 (model space); 0 = the viewport's height
    double snap_angle = 0.0;                // 50
    double twist_angle = 0.0;               // 51
    int circle_zoom = 1000;                 // 72
    int status_flags = 819232;              // 90, as AutoCAD writes a new viewport
    int render_mode = 0;                    // 281
    int ucs_per_viewport = 1;               // 71
    int ucs_icon = 0;                       // 74
    int ucs_ortho_type = 0;                 // 79
    Point3 ucs_origin;                      // 110, 120, 130
    Point3 ucs_x_axis = {1.0, 0.0, 0.0};    // 111, 121, 131
    Point3 ucs_y_axis = {0.0, 1.0, 0.0};    // 112, 122, 132
    double elevation = 0.0;                 // 146
    int shade_plot_mode = 0;                // 170 (2004+): 0 as displayed, 1 wireframe,
                                            //     2 hidden, 3 rendered
    // 2007+, as AutoCAD writes a new viewport
    int grid_major = 5;                     // 61: a major grid line every n lines
    std::string visual_style_handle;        // 348: the VISUALSTYLE, empty = 2dWireframe
    // 348 in AcDbEntity instead of AcDbViewport, as some AutoCAD files have it (KioskStation's;
    // in no reference)
    bool visual_style_in_entity = false;
    bool default_lighting = true;           // 292
    int default_lighting_type = 1;          // 282: 0 one distant light, 1 two
    double brightness = 0.0;                // 141
    double contrast = 0.0;                  // 142
    Color ambient_color = Color::from_rgb(51, 51, 51);  // 63, 421, 431 (ACI 250)
    std::string background_handle;          // 332
    std::string shade_plot_handle;          // 333
    std::string sun_handle;                 // 361
    std::string ucs;                        // 345: a named UCS, empty = the one above
    std::string base_ucs;                   // 346: UCS name, empty = WORLD
    std::string plot_style_sheet;           // 1
    std::string clip_boundary_handle;       // 340: the entity that clips the viewport
    std::vector<std::string> frozen_layers; // 331, as layer names
};

// ---------------------------------------------------------------------------
// IMAGE (2000+), in the WCS; its IMAGEDEF and IMAGEDEF_REACTOR are objects
// ---------------------------------------------------------------------------

struct ImageData {
    int class_version = 0;              // 90
    Point3 insertion_point;             // 10, 20, 30: the lower left corner
    Point3 u_vector = {1.0, 0.0, 0.0};  // 11, 21, 31: one pixel along the image's x
    Point3 v_vector = {0.0, 1.0, 0.0};  // 12, 22, 32: one pixel along its y
    Point2 size;                        // 13, 23: in pixels
    std::string image_def_handle;       // 340: the IMAGEDEF
    int display_flags = 7;              // 70: 1 show, 2 show when not aligned with the
                                        //     screen, 4 use the clip boundary, 8 transparency
    bool clipping = false;              // 280
    int brightness = 50;                // 281
    int contrast = 50;                  // 282
    int fade = 0;                       // 283
    std::string reactor_handle;         // 360: its IMAGEDEF_REACTOR (owned by the image)
    int clip_boundary_type = 1;         // 71: 1 rectangle, 2 polygon
    std::vector<Point2> clip_boundary;  // 91 (the count), 14, 24: in pixels
    bool clip_inside = false;           // 290 (2010+): the clip mode, false = outside
};

} // namespace hdxf
