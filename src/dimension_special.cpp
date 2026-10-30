#include "ContextDimensionTypes.hpp"
#include "DynamicBlockTypes.hpp"
#include "dxf_writer.hpp"
#include <cmath>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ARC_DIMENSION and LARGE_RADIAL_DIMENSION of step 9.17 (phase 9; plan 2026-10-27-fas-9-typer.md, B100-B107, B113-B116): an
// AcDbDimension (the groups DIMENSION has) and the type's own subclass, as AutoCAD 2025 writes them (doc/facit/
// fas9_special_dimensions.scr and Erik's acad_<version>_dxf_dimensions*.dxf, 2000 … 2018, ASCII and binary):
//   ARC_DIMENSION             S:AcDbDimension S:AcDbArcDimension 13 14 15 (the arc's ends and center) 70 (partial) 40 41 71
//                             (leader) 16 17
//   LARGE_RADIAL_DIMENSION    S:AcDbDimension S:AcDbRadialDimensionLarge 13 (the overridden center) 14 (the jog) 15 (the point on
//                             the circle) 40
// Their blocks (*D<n>) are built by dimension_block.cpp as for any dimension (the arc symbol and the jog included), their extents
// are their block's and 10 (extents.cpp, as a DIMENSION's).

namespace hdxf {

Entity& Drawing::add_arc_dimension(const Point3& center, const Point3& start, const Point3& end, const Point3& arc_point,
                                   const std::string& layer) {
    Entity e;
    e.type = EntityType::ArcDimension;
    e.props.layer = layer;
    ArcDimensionData a;
    a.dimension.dim_type = DimensionType::Angular3Pt;       // low nibble 5
    a.dimension.definition_point = arc_point;               // 10: a point on the dimension arc
    a.dimension.text_midpoint = arc_point;
    a.dimension.def_point2 = start;                         // 13
    a.dimension.def_point3 = end;                           // 14
    a.dimension.def_point4 = center;                        // 15
    a.dimension.arc_point = start;                          // 16 and 17 are the ends when there is no leader (AutoCAD's DIMARC)
    a.leader_end = end;
    double sweep = std::atan2(end.y - center.y, end.x - center.x) - std::atan2(start.y - center.y, start.x - center.x);
    while (sweep < 0.0) sweep += 2.0 * M_PI;
    a.end_parameter = sweep;
    e.data = Boxed<ArcDimensionData>(std::move(a));
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_large_radial_dimension(const Point3& center, const Point3& circle_point, const Point3& center_override,
                                            const Point3& text_point, const std::string& layer) {
    Entity e;
    e.type = EntityType::LargeRadialDimension;
    e.props.layer = layer;
    LargeRadialDimensionData l;
    l.dimension.dim_type = static_cast<DimensionType>(9);   // low nibble 9: AutoCAD's type of the jogged radius
    l.dimension.flags = 128;                                // the text is where the program put it
    l.dimension.definition_point = center;                  // 10
    l.dimension.text_midpoint = text_point;                 // 11
    l.dimension.def_point4 = circle_point;                  // 15
    l.center_override = center_override;                    // 13
    l.jog_point = {(center_override.x + circle_point.x) / 2.0, (center_override.y + circle_point.y) / 2.0,
                   (center_override.z + circle_point.z) / 2.0};     // 14: half way, as AutoCAD's DIMJOGGED
    e.data = Boxed<LargeRadialDimensionData>(std::move(l));
    entities_.push_back(std::move(e));
    return entities_.back();
}

namespace internal {

namespace {

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

} // namespace

bool read_dimension_common(const RecordView& view, TypedReadContext& ctx, Entity& e, DimensionData& d,
                           std::optional<int16_t>* spacing_style, std::optional<double>* spacing_factor) {
    GroupCursor c(view, "AcDbDimension");
    if (!c.valid()) return ctx.fail(view.type_name() + " " + view.head().handle + " has no AcDbDimension subclass");
    c.integer(280, d.version);
    c.require_string(2, d.block_name);
    c.require_point3(10, d.definition_point);
    c.require_point3(11, d.text_midpoint);
    c.point3(12, d.block_translation);
    c.require_integer(70, d.flags);
    c.string(1, d.text_override);
    // 71, 72, 41, 73 … 75 as read, and written from there (9.S, Codex 10; DimensionData, the spacing the type's own)
    c.integer(71, d.attachment_point);
    int16_t style = 0;
    if (c.integer(72, style) && spacing_style) *spacing_style = style;
    double factor = 0.0;
    if (c.real(41, factor) && spacing_factor) *spacing_factor = factor;
    double measurement = 0.0;
    c.real(42, measurement);            // the measurement: derived when written
    c.integer(73, d.value_73);
    c.integer(74, d.value_74);
    c.integer(75, d.value_75);
    std::string text;
    if (c.string(1, text)) d.text_override = text;
    c.real(53, d.text_rotation);
    c.real(51, d.horizontal_dir);
    Point3 extrusion;
    if (c.point3(210, extrusion)) e.props.extrusion = extrusion;
    c.require_string(3, d.dim_style);
    if (!c.ok()) return ctx.fail(view.type_name() + " " + view.head().handle + ": " + c.error());
    if (!c.at_end()) {
        const size_t n = c.take_rest().size();
        ctx.warn(view.type_name() + " " + view.head().handle + ": " + std::to_string(n) +
                 " group(s) after the ones HDxf knows (subclass AcDbDimension) are kept as they were read");
    }
    d.dim_type = static_cast<DimensionType>(d.flags & 15);
    return true;
}

namespace {

bool read_arc_dimension(const RecordView& view, TypedReadContext& ctx, Entity& e) {
    ArcDimensionData a;
    if (!read_dimension_common(view, ctx, e, a.dimension, &a.line_spacing_style, &a.line_spacing_factor)) return false;
    GroupCursor c(view, "AcDbArcDimension");
    if (!c.valid()) return ctx.fail(view.type_name() + " " + view.head().handle + " has no AcDbArcDimension subclass");
    c.require_point3(13, a.dimension.def_point2);
    c.require_point3(14, a.dimension.def_point3);
    c.require_point3(15, a.dimension.def_point4);
    c.require_boolean(70, a.is_partial);
    c.require_real(40, a.start_parameter);
    c.require_real(41, a.end_parameter);
    c.require_boolean(71, a.has_leader);
    c.require_point3(16, a.dimension.arc_point);
    c.require_point3(17, a.leader_end);
    if (!c.ok()) return ctx.fail(view.type_name() + " " + view.head().handle + ": " + c.error());
    if (!c.at_end()) {
        const size_t n = c.take_rest().size();
        ctx.warn(view.type_name() + " " + view.head().handle + ": " + std::to_string(n) +
                 " group(s) after the ones HDxf knows (subclass AcDbArcDimension) are kept as they were read");
    }
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &a.proxy_graphics, error)) return ctx.fail(error);
    e.data = Boxed<ArcDimensionData>(std::move(a));
    return true;
}

bool read_large_radial_dimension(const RecordView& view, TypedReadContext& ctx, Entity& e) {
    LargeRadialDimensionData l;
    if (!read_dimension_common(view, ctx, e, l.dimension, &l.line_spacing_style, &l.line_spacing_factor)) return false;
    GroupCursor c(view, "AcDbRadialDimensionLarge");
    if (!c.valid()) return ctx.fail(view.type_name() + " " + view.head().handle + " has no AcDbRadialDimensionLarge subclass");
    c.require_point3(13, l.center_override);
    c.require_point3(14, l.jog_point);
    c.require_point3(15, l.dimension.def_point4);
    c.require_real(40, l.leader_length);
    if (!c.ok()) return ctx.fail(view.type_name() + " " + view.head().handle + ": " + c.error());
    if (!c.at_end()) {
        const size_t n = c.take_rest().size();
        ctx.warn(view.type_name() + " " + view.head().handle + ": " + std::to_string(n) +
                 " group(s) after the ones HDxf knows (subclass AcDbRadialDimensionLarge) are kept as they were read");
    }
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &l.proxy_graphics, error)) return ctx.fail(error);
    e.data = Boxed<LargeRadialDimensionData>(std::move(l));
    return true;
}

// The block is named (2), not pointed at: no reference. The extents are those of a DIMENSION (extents.cpp)
void no_references(const Entity&, std::vector<TypedReference>&) {}

ClassSpec special_class(const char* cpp_name) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 1025;
    c.in_older = false;
    return c;
}

// AutoCAD's files of 2000 … 2018 (ASCII and binary) have both types; before 2000 they are the named error (R13 and R14 as
// 1.8.1 gave it)
VersionMatrix special_matrix() {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2000);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

} // namespace

void register_special_dimension_codecs(TypedRegistry& registry) {
    {
        EntityCodec c;
        c.dxf_name = "LARGE_RADIAL_DIMENSION";
        c.type = EntityType::LargeRadialDimension;
        c.read = read_large_radial_dimension;
        c.write = write_large_radial_dimension;
        c.references = no_references;
        c.cls = special_class("AcDbRadialDimensionLarge");
        c.versions = special_matrix();
        c.step = 17;
        registry.add(std::move(c));
    }
    {
        EntityCodec c;
        c.dxf_name = "ARC_DIMENSION";
        c.type = EntityType::ArcDimension;
        c.read = read_arc_dimension;
        c.write = write_arc_dimension;
        c.references = no_references;
        c.cls = special_class("AcDbArcDimension");
        c.versions = special_matrix();
        c.step = 17;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
