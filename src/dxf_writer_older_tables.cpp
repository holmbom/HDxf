#include "dxf_writer.hpp"

// The table records of DXF R13 and R14 (plan phase 7, step 7.6, B90): VPORT, VIEW, UCS and LAYER as
// their R13/R14 form has them, without the groups the 2000 writer adds. The groups and their order are
// those of Autodesk's R13 and R14 DXF references and of the AutoCAD-origin files in
// doc/reference/hdxf_r13_r14_format.md, section 5.2 (libre/r13_v.dxf, gvsig/dxfprueba.dxf,
// spatialauxetic/HEAD.DXF, the local az2150_waterloo.dxf and Test3_* files).
//   VPORT: 10 20, 11 21, 12 22, 13 23, 14 24, 15 25, 16 26 36, 17 27 37, 40 … 44, 50, 51, 71 … 78
//   VIEW:  40, 10 20, 41, 11 21 31, 12 22 32, 42, 43, 44, 50, 71
//   UCS:   10 20 30, 11 21 31, 12 22 32
//   LAYER: 70, 62, 6
// No 281, 65, 110-112, 79, 146 or 345/346 (VPORT, VIEW, UCS), no 290, 370, 390 or the 2004+ transparency
// (LAYER); a value of the drawing that has no such group is refused before anything is written
// (older_tables_error()). The record's XDATA is written as 2000 does: R13 and R14 records have it.

namespace hdxf {
namespace internal {

void DxfWriter::write_vport_older(const Vport& vp, const std::string& handle) {
    write_table_entry_head("VPORT", handle, table_handles_["VPORT"], "AcDbViewportTableRecord", vp.xdictionary);
    write_code(2, vp.name);
    groups_.write_int(70, vp.flags);
    groups_.write_double(10, vp.lower_left.x);
    groups_.write_double(20, vp.lower_left.y);
    groups_.write_double(11, vp.upper_right.x);
    groups_.write_double(21, vp.upper_right.y);
    groups_.write_double(12, vp.center.x);
    groups_.write_double(22, vp.center.y);
    groups_.write_double(13, vp.snap_base.x);
    groups_.write_double(23, vp.snap_base.y);
    groups_.write_double(14, vp.snap_spacing.x);
    groups_.write_double(24, vp.snap_spacing.y);
    groups_.write_double(15, vp.grid_spacing.x);
    groups_.write_double(25, vp.grid_spacing.y);
    write_point(16, vp.view_dir);
    write_point(17, vp.target);
    groups_.write_double(40, vp.view_height);
    groups_.write_double(41, vp.aspect_ratio);
    groups_.write_double(42, vp.lens_length);
    groups_.write_double(43, vp.front_clip);
    groups_.write_double(44, vp.back_clip);
    groups_.write_double(50, vp.snap_angle);
    groups_.write_double(51, vp.twist_angle);
    groups_.write_int(71, vp.view_mode);
    groups_.write_int(72, vp.circle_zoom);
    groups_.write_int(73, vp.fast_zoom);
    groups_.write_int(74, vp.ucsicon);
    groups_.write_int(75, vp.snap_on);
    groups_.write_int(76, vp.grid_on);
    groups_.write_int(77, vp.snap_style);
    groups_.write_int(78, vp.snap_isopair);
    write_xdata(vp.xdata);
}

void DxfWriter::write_view_older(const View& v) {
    write_table_entry_head("VIEW", v.handle, table_handles_["VIEW"], "AcDbViewTableRecord", v.xdictionary);
    write_code(2, v.name);
    groups_.write_int(70, v.flags);
    groups_.write_double(40, v.height);
    groups_.write_double(10, v.center.x);
    groups_.write_double(20, v.center.y);
    groups_.write_double(41, v.width);
    write_point(11, v.direction);
    write_point(12, v.target);
    groups_.write_double(42, v.lens_length);
    groups_.write_double(43, v.front_clip);
    groups_.write_double(44, v.back_clip);
    groups_.write_double(50, v.twist);
    groups_.write_int(71, v.view_mode);
}

void DxfWriter::write_ucs_older(const Ucs& u) {
    write_table_entry_head("UCS", u.handle, table_handles_["UCS"], "AcDbUCSTableRecord");
    write_code(2, u.name);
    groups_.write_int(70, u.flags);
    write_point(10, u.origin);
    write_point(11, u.x_dir);
    write_point(12, u.y_dir);
}

void DxfWriter::write_layer_older(const Layer& l, const std::string& handle) {
    write_table_entry_head("LAYER", handle, table_handles_["LAYER"], "AcDbLayerTableRecord", l.xdictionary);
    write_code(2, l.name);
    // The bits HDxf does not interpret (16, 32, 64 …) as they were read
    int flags = l.flags & ~7;
    if (l.frozen) flags |= 1;
    if (l.frozen_in_new_vp) flags |= 2;
    if (l.locked) flags |= 4;
    groups_.write_int(70, flags);
    groups_.write_int(62, l.on ? l.color.aci : -l.color.aci);
    write_code(6, symbol_ref(SymbolKind::Linetype, l.linetype));
    write_xdata(l.xdata);
}

} // namespace internal
} // namespace hdxf
