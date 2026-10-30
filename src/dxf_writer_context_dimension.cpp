#include "ContextDimensionTypes.hpp"
#include "DynamicBlockTypes.hpp"
#include "dxf_writer.hpp"
#include <array>
#include <string>
#include <vector>

// The writer of the step 9.17 records (phase 9; plan 2026-10-27-fas-9-typer.md, B19, B103, B104, B112): every record is written
// whole from its typed values in the order of AutoCAD's own files (object_context.cpp, dimassoc.cpp, dimension_special.cpp),
// with its head and, last, its XDATA. The writer is also what the B12 merge compares: the record as read against the record
// now, so a record that did not change is written as the file had it. A pointer to a record that is gone is 0; the numbers
// (a context's 70, a vertex count) are written as they are.

namespace hdxf {
namespace internal {

void RecordWriter::dimension_common(const Entity& e, const DimensionData& d) { w_.write_dimension_common(e, d); }

bool RecordWriter::dimension_regenerated(const Entity& e) const { return w_.dimension_regenerated(e); }

void write_dimension_common(RecordWriter& w, const Entity& e, const DimensionData& d) { w.dimension_common(e, d); }

namespace {

void write_scale(RecordWriter& w, const ObjectContextBase& b) {
    w.pointer(340, b.scale.empty() ? "0" : b.scale, PointerWrite::ZeroDead);
}

// The two bases: AcDbObjectContextData and AcDbAnnotScaleObjectContextData up to and including 340
void write_context_head(RecordWriter& w, const char* dxf_name, const Object& o, const ObjectContextBase& b) {
    w.object_head(dxf_name, o);
    w.subclass("AcDbObjectContextData");
    w.integer(70, b.version);
    w.boolean(290, b.is_default);
    w.subclass("AcDbAnnotScaleObjectContextData");
    write_scale(w, b);
}

void write_dim_common(RecordWriter& w, const DimContextCommon& d) {
    w.subclass("AcDbDimensionObjectContextData");
    w.text(2, d.block_name);
    w.boolean(293, d.flag_293);
    w.point2(10, d.text_location);
    w.boolean(294, d.flag_294);
    w.real(140, d.value_140);
    w.boolean(298, d.flag_298);
    w.boolean(291, d.flag_291);
    w.integer(70, d.value_70);
    w.boolean(292, d.flag_292);
    w.integer(71, d.value_71);
    w.integer(280, d.value_280);
    w.boolean(295, d.flag_295);
    w.boolean(296, d.flag_296);
    w.boolean(297, d.flag_297);
}

void write_mtext_own(RecordWriter& w, const MTextContextData& d) {
    w.integer(70, d.flags);
    w.point3(10, d.direction);
    w.point3(11, d.location);
    w.real(40, d.reference_width);
    w.real(41, d.defined_height);
    w.real(42, d.text_width);
    w.real(43, d.text_height);
    w.integer(71, d.value_71);
}

void write_hatch_common(RecordWriter& w, const HatchContextCommon& h) {
    w.subclass("AcDbHatchObjectContextData");
    w.integer(78, static_cast<int64_t>(h.pattern_lines.size()));
    for (const HatchPatternLine& line : h.pattern_lines) {
        w.real(53, line.angle);
        w.real(43, line.base_point.x);
        w.real(44, line.base_point.y);
        w.real(45, line.offset.x);
        w.real(46, line.offset.y);
        w.integer(79, static_cast<int64_t>(line.dashes.size()));
        for (double dash : line.dashes) w.real(49, dash);
    }
    w.real(40, h.pattern_scale);
    w.point3(10, h.base_point);
    w.integer(90, h.value_90_a);
    w.integer(90, h.value_90_b);
    w.boolean(290, h.flag_290);
    if (!h.extension) return;
    w.integer(90, h.extension->value_90);
    w.boolean(290, h.extension->flag_290);
    if (!h.extension->edges) return;
    w.integer(93, static_cast<int64_t>(h.extension->edges->size()));
    for (const HatchEdge& e : *h.extension->edges) {
        w.integer(72, static_cast<int64_t>(HatchEdgeType::Line));
        w.real(10, e.start.x);
        w.real(20, e.start.y);
        w.real(11, e.end.x);
        w.real(21, e.end.y);
    }
}

// A pointer in a MULTILEADER context (the text style, the block): the handle, or 0 when it is gone
void write_mleader_pointer(RecordWriter& w, int code, const std::string& handle) {
    w.pointer(code, handle.empty() ? "0" : handle, PointerWrite::ZeroDead);
}

} // namespace

void write_text_context(RecordWriter& w, const Object& o) {
    const TextContextData& d = o.as_text_context();
    write_context_head(w, "ACDB_TEXTOBJECTCONTEXTDATA_CLASS", o, d.base);
    w.integer(70, d.flags);
    w.real(50, d.rotation);
    w.point2(10, d.insertion);
    w.point2(11, d.alignment);
    w.xdata(o.xdata);
}

void write_leader_context(RecordWriter& w, const Object& o) {
    const LeaderContextData& d = o.as_leader_context();
    write_context_head(w, "ACDB_LEADEROBJECTCONTEXTDATA_CLASS", o, d.base);
    w.integer(70, static_cast<int64_t>(d.points.size()));
    for (const Point3& p : d.points) w.point3(10, p);
    w.point3(11, d.direction);
    w.boolean(290, d.flag_290);
    w.point3(12, d.offset);
    w.point3(13, d.annotation_offset);
    w.xdata(o.xdata);
}

void write_mtext_context(RecordWriter& w, const Object& o) {
    const MTextContextData& d = o.as_mtext_context();
    write_context_head(w, "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS", o, d.base);
    write_mtext_own(w, d);
    w.xdata(o.xdata);
}

void write_mtext_attribute_context(RecordWriter& w, const Object& o) {
    const MTextAttributeContextData& d = o.as_mtext_attribute_context();
    write_context_head(w, "ACDB_MTEXTATTRIBUTEOBJECTCONTEXTDATA_CLASS", o, d.base);
    w.integer(70, d.flags);
    w.real(50, d.rotation);
    w.point2(10, d.insertion);
    w.point2(11, d.alignment);
    w.boolean(290, d.flag_290);
    if (d.embedded) {
        w.text(101, "Embedded Object");
        w.subclass("AcDbObjectContextData");
        w.integer(70, d.embedded->base.version);
        w.boolean(290, d.embedded->base.is_default);
        w.subclass("AcDbAnnotScaleObjectContextData");
        write_scale(w, d.embedded->base);
        write_mtext_own(w, *d.embedded);
    }
    w.xdata(o.xdata);
}

void write_mleader_context(RecordWriter& w, const Object& o) {
    const MLeaderContextData& d = o.as_mleader_context();
    const MLeaderContext& c = d.content;
    write_context_head(w, "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS", o, d.base);
    w.real(40, c.scale);
    w.point3(10, c.base_point);
    w.real(41, c.text_height);
    w.real(140, c.arrow_size);
    w.real(145, c.landing_gap);
    w.integer(174, c.left_attachment);
    w.integer(175, c.right_attachment);
    w.integer(176, c.text_align);
    w.integer(177, c.block_connection);
    w.boolean(290, c.has_text);
    if (c.has_text) {
        w.text(304, c.text);
        w.point3(11, c.text_normal);
        write_mleader_pointer(w, 340, c.text_style);
        w.point3(12, c.text_location);
        w.point3(13, c.text_direction);
        w.real(42, c.text_rotation);
        w.real(43, c.text_width);
        w.real(44, c.text_defined_height);
        w.real(45, c.line_spacing_factor);
        w.integer(170, c.line_spacing_style);
        w.integer(90, c.text_color.raw);
        w.integer(171, c.text_attachment);
        w.integer(172, c.text_flow);
        w.integer(91, c.background_color.raw);
        w.real(141, c.background_scale);
        w.integer(92, c.background_transparency);
        w.boolean(291, c.background_color_on);
        w.boolean(292, c.background_fill_on);
        w.integer(173, c.column_type);
        w.boolean(293, c.auto_height);
        w.real(142, c.column_width);
        w.real(143, c.column_gutter);
        w.boolean(294, c.column_flow_reversed);
        for (double h : c.column_heights) w.real(144, h);
        w.boolean(295, c.word_break);
    }
    w.boolean(296, c.has_block);
    if (c.has_block) {
        write_mleader_pointer(w, 341, c.block);
        w.point3(14, c.block_normal);
        w.point3(15, c.block_position);
        w.point3(16, c.block_scale);
        w.real(46, c.block_rotation);
        w.integer(93, c.block_color.raw);
        for (double m : c.block_matrix) w.real(47, m);
    }
    w.point3(110, c.plane_origin);
    w.point3(111, c.plane_x_axis);
    w.point3(112, c.plane_y_axis);
    w.boolean(297, c.plane_normal_reversed);
    for (const MLeaderLeader& leader : c.leaders) {
        w.text(302, "LEADER{");
        w.boolean(290, leader.has_last_point);
        w.boolean(291, leader.has_dogleg);
        w.point3(10, leader.last_point);
        w.point3(11, leader.dogleg_direction);
        for (const MLeaderGap& gap : leader.breaks) {
            w.point3(12, gap.start);
            w.point3(13, gap.end);
        }
        w.integer(90, leader.branch_index);
        w.real(40, leader.dogleg_length);
        for (const MLeaderLine& line : leader.lines) {
            w.text(304, "LEADER_LINE{");
            for (const Point3& v : line.vertices) w.point3(10, v);
            for (const MLeaderSegmentGaps& segment : line.breaks) {
                w.integer(90, segment.segment);
                for (const MLeaderGap& gap : segment.gaps) {
                    w.point3(11, gap.start);
                    w.point3(12, gap.end);
                }
            }
            w.integer(91, line.index);
            w.text(305, "}");
        }
        w.integer(271, leader.attachment_direction);
        w.text(303, "}");
    }
    w.integer(272, c.bottom_attachment_direction);
    w.integer(273, c.top_attachment_direction);
    w.xdata(o.xdata);
}

void write_blkref_context(RecordWriter& w, const Object& o) {
    const BlkRefContextData& d = o.as_blkref_context();
    write_context_head(w, "ACDB_BLKREFOBJECTCONTEXTDATA_CLASS", o, d.base);
    w.real(50, d.rotation);
    w.point3(10, d.insertion);
    w.real(41, d.scale.x);
    w.real(42, d.scale.y);
    w.real(43, d.scale.z);
    w.xdata(o.xdata);
}

void write_aldim_context(RecordWriter& w, const Object& o) {
    const AlDimContextData& d = o.as_aldim_context();
    write_context_head(w, "ACDB_ALDIMOBJECTCONTEXTDATA_CLASS", o, d.base);
    write_dim_common(w, d.dimension);
    w.subclass("AcDbAlignedDimensionObjectContextData");
    w.point3(11, d.dimension_line_point);
    w.xdata(o.xdata);
}

void write_angdim_context(RecordWriter& w, const Object& o) {
    const AngDimContextData& d = o.as_angdim_context();
    write_context_head(w, "ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS", o, d.base);
    write_dim_common(w, d.dimension);
    w.subclass("AcDbAngularDimensionObjectContextData");
    w.point3(11, d.arc_point);
    w.xdata(o.xdata);
}

void write_dmdim_context(RecordWriter& w, const Object& o) {
    const DmDimContextData& d = o.as_dmdim_context();
    write_context_head(w, "ACDB_DMDIMOBJECTCONTEXTDATA_CLASS", o, d.base);
    write_dim_common(w, d.dimension);
    w.subclass("AcDbDiametricDimensionObjectContextData");
    w.point3(11, d.point_11);
    w.point3(12, d.point_12);
    w.xdata(o.xdata);
}

void write_orddim_context(RecordWriter& w, const Object& o) {
    const OrdDimContextData& d = o.as_orddim_context();
    write_context_head(w, "ACDB_ORDDIMOBJECTCONTEXTDATA_CLASS", o, d.base);
    write_dim_common(w, d.dimension);
    w.subclass("AcDbOrdinateDimensionObjectContextData");
    w.point3(11, d.point_11);
    w.point3(12, d.point_12);
    w.xdata(o.xdata);
}

void write_radim_context(RecordWriter& w, const Object& o) {
    const RadimContextData& d = o.as_radim_context();
    write_context_head(w, "ACDB_RADIMOBJECTCONTEXTDATA_CLASS", o, d.base);
    write_dim_common(w, d.dimension);
    w.subclass("AcDbRadialDimensionObjectContextData");
    w.point3(11, d.point_11);
    w.xdata(o.xdata);
}

void write_radimlg_context(RecordWriter& w, const Object& o) {
    const RadimLgContextData& d = o.as_radimlg_context();
    write_context_head(w, "ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS", o, d.base);
    write_dim_common(w, d.dimension);
    w.subclass("AcDbRadialDimensionObjectContextData");
    w.point3(11, d.point_11);
    w.subclass("AcDbRadialDimensionLargeObjectContextData");
    w.point3(12, d.point_12);
    w.point3(13, d.point_13);
    w.xdata(o.xdata);
}

void write_fcf_context(RecordWriter& w, const Object& o) {
    const FcfContextData& d = o.as_fcf_context();
    write_context_head(w, "ACDB_FCFOBJECTCONTEXTDATA_CLASS", o, d.base);
    w.point3(10, d.location);
    w.point3(11, d.direction);
    w.xdata(o.xdata);
}

void write_annot_scale_context(RecordWriter& w, const Object& o) {
    const AnnotScaleContextData& d = o.as_annot_scale_context();
    write_context_head(w, "ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS", o, d.base);
    w.xdata(o.xdata);
}

void write_hatch_scale_context(RecordWriter& w, const Object& o) {
    const HatchScaleContextData& d = o.as_hatch_scale_context();
    write_context_head(w, "ACDB_HATCHSCALECONTEXTDATA_CLASS", o, d.base);
    write_hatch_common(w, d.hatch);
    w.xdata(o.xdata);
}

void write_hatch_view_context(RecordWriter& w, const Object& o) {
    const HatchViewContextData& d = o.as_hatch_view_context();
    write_context_head(w, "ACDB_HATCHVIEWCONTEXTDATA_CLASS", o, d.base);
    write_hatch_common(w, d.hatch);
    w.subclass("AcDbHatchViewContextData");
    w.pointer(330, d.pointer_330.empty() ? "0" : d.pointer_330, PointerWrite::ZeroDead);
    w.point3(10, d.normal);
    w.real(51, d.value_51);
    w.boolean(290, d.flag_290);
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// DIMASSOC
// ---------------------------------------------------------------------------

void write_dim_assoc(RecordWriter& w, const Object& o) {
    const DimAssocData& d = o.as_dim_assoc();
    w.object_head("DIMASSOC", o);
    w.subclass("AcDbDimAssoc");
    w.pointer(330, d.dimension.empty() ? "0" : d.dimension, PointerWrite::ZeroDead);
    // The mask is what the points say: bit 1, 2, 4, 8 for the slots that have a reference (the other bits as read)
    // A point whose geometry is gone has no reference: its bit is clear and its groups are not written
    std::array<bool, 4> keep{};
    int32_t mask = d.flags & ~15;
    for (size_t i = 0; i < d.points.size(); ++i) {
        if (!d.points[i]) continue;
        keep[i] = true;
        for (const std::string& h : d.points[i]->main_handles) keep[i] = keep[i] && pointer_alive(w, h);
        for (const std::string& h : d.points[i]->intersection_handles) keep[i] = keep[i] && pointer_alive(w, h);
        if (keep[i]) mask |= 1 << i;
    }
    w.integer(90, mask);
    w.integer(70, d.trans_space);
    w.integer(71, d.rotated_type);
    for (size_t slot_index = 0; slot_index < d.points.size(); ++slot_index) {
        const auto& slot = d.points[slot_index];
        if (!slot || !keep[slot_index]) continue;
        const DimAssocPointRef& p = *slot;
        w.text(1, p.class_name);
        w.integer(72, p.osnap_type);
        for (const std::string& h : p.main_handles) w.pointer(331, h.empty() ? "0" : h, PointerWrite::AsIs);
        w.integer(73, p.main_subentity_type);
        w.integer(91, p.main_marker);
        if (!p.xref_handle_string.empty()) w.text(301, p.xref_handle_string);
        w.real(40, p.near_parameter);
        w.point3(10, p.point);
        for (const std::string& h : p.intersection_handles) w.pointer(332, h.empty() ? "0" : h, PointerWrite::AsIs);
        if (p.intersection_subentity_type) w.integer(74, *p.intersection_subentity_type);
        if (p.intersection_marker) w.integer(92, *p.intersection_marker);
        if (!p.intersection_xref.empty()) w.text(302, p.intersection_xref);
        w.integer(75, p.last_point_ref);
    }
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// The special dimensions
// ---------------------------------------------------------------------------

namespace {

// B106: a special dimension read from a file keeps the proxy graphics AutoCAD wrote for it, a new one has none (AutoCAD makes them
// when it saves; no grammar of its dimension stream is derived). One that changed since it was read gets its block anew (9.S,
// Codex 9): the graphics read show the dimension as it was, so it is written as a new one, without (the writer's warning names it)
const std::vector<uint8_t>* special_proxy(const RecordWriter& w, const Entity& e, const std::vector<uint8_t>& read) {
    if (read.empty() || w.dimension_regenerated(e)) return nullptr;
    return &read;
}

} // namespace

void write_arc_dimension(RecordWriter& w, const Entity& e, const std::string& owner) {
    const ArcDimensionData& a = e.as_arc_dimension();
    if (w.older() && e.raw.empty()) {
        w.fail("ARC_DIMENSION requires DXF 2000 or later");
        return;
    }
    w.entity_head("ARC_DIMENSION", e, owner, special_proxy(w, e, a.proxy_graphics));
    w.dimension_common(e, a.dimension);
    w.subclass("AcDbArcDimension");
    w.point3(13, a.dimension.def_point2);
    w.point3(14, a.dimension.def_point3);
    w.point3(15, a.dimension.def_point4);
    w.boolean(70, a.is_partial);
    w.real(40, a.start_parameter);
    w.real(41, a.end_parameter);
    w.boolean(71, a.has_leader);
    w.point3(16, a.dimension.arc_point);
    w.point3(17, a.leader_end);
    w.xdata(e.props.xdata);
}

void write_large_radial_dimension(RecordWriter& w, const Entity& e, const std::string& owner) {
    const LargeRadialDimensionData& l = e.as_large_radial_dimension();
    if (w.older() && e.raw.empty()) {
        w.fail("LARGE_RADIAL_DIMENSION requires DXF 2000 or later");
        return;
    }
    w.entity_head("LARGE_RADIAL_DIMENSION", e, owner, special_proxy(w, e, l.proxy_graphics));
    w.dimension_common(e, l.dimension);
    w.subclass("AcDbRadialDimensionLarge");
    w.point3(13, l.center_override);
    w.point3(14, l.jog_point);
    w.point3(15, l.dimension.def_point4);
    w.real(40, l.leader_length);
    w.xdata(e.props.xdata);
}

} // namespace internal
} // namespace hdxf
