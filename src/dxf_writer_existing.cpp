#include "records_existing.hpp"
#include "proxy_graphics.hpp"

// Step 9.2: the writing of WIPEOUT, WIPEOUTVARIABLES and SCALE through RecordWriter (plan
// 2026-10-27-fas-9-typer.md). The groups and their order are those AutoCAD writes, as the writer of 1.8.1 had
// them: ACadSharp's samples and the AutoCAD 2025 files doc/facit/acad_20*_dxf_images.dxf and
// fas9_existing_fields.scr's wipeout, scales.

namespace hdxf {
namespace internal {

// AcDbWipeout is IMAGE's AcDbRasterImage without an image: 340 and 360 are 0 (AutoCAD's own WIPEOUT has no
// IMAGEDEF and no IMAGEDEF_REACTOR), the boundary is in pixel space with the picture one pixel, y down.
// Its proxy graphics (92 before 2010, 160 from 2010) are the boundary as a closed polyline.
void write_wipeout_record(RecordWriter& w, const Entity& e, const std::string& owner) {
    const ImageData& d = e.as_wipeout();
    std::vector<uint8_t> proxy;
    if (w.proxy_graphics_enabled() && !w.older()) {
        ProxyGraphics g;
        g.polyline(image_outline(d));
        proxy = g.bytes();
    }
    w.entity_head("WIPEOUT", e, owner, &proxy);
    w.subclass("AcDbWipeout");
    w.integer(90, d.class_version);
    w.point3(10, d.insertion_point);
    w.point3(11, d.u_vector);
    w.point3(12, d.v_vector);
    w.real(13, d.size.x);
    w.real(23, d.size.y);
    // A pointer that leads to a record that is gone is 0 (B19)
    w.pointer(340, d.image_def_handle.empty() ? std::string("0") : d.image_def_handle, PointerWrite::ZeroDead);
    w.integer(70, d.display_flags);
    w.boolean(280, d.clipping);
    w.integer(281, d.brightness);
    w.integer(282, d.contrast);
    w.integer(283, d.fade);
    // 2010: the clip mode after the fade (ACadSharp's sample_AC1024, B44)
    if (w.version() >= DxfVersion::R2010) w.boolean(290, d.clip_inside);
    w.pointer(360, d.reactor_handle.empty() ? std::string("0") : d.reactor_handle, PointerWrite::ZeroDead);
    w.integer(71, d.clip_boundary_type);
    w.integer(91, static_cast<int64_t>(d.clip_boundary.size()));
    for (const Point2& c : d.clip_boundary) w.point2(14, c);
    w.xdata(e.props.xdata);
}

void write_wipeout_variables_record(RecordWriter& w, const Object& o) {
    w.object_head("WIPEOUTVARIABLES", o);
    w.subclass("AcDbWipeoutVariables");
    w.integer(70, o.as_wipeout_variables().display_frame);
    w.xdata(o.xdata);
}

void write_scale_record(RecordWriter& w, const Object& o) {
    const ScaleData& d = o.as_scale();
    w.object_head("SCALE", o);
    w.subclass("AcDbScale");
    w.integer(70, d.flags);
    w.text(300, d.name);
    w.real(140, d.paper_units);
    w.real(141, d.drawing_units);
    w.boolean(290, d.unit_scale);
    w.xdata(o.xdata);
}

} // namespace internal
} // namespace hdxf
