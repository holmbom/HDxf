#include "records_existing.hpp"

// Step 9.2 of phase 9 (plan 2026-10-27-fas-9-typer.md): codecs for the record types HDxf typed before phase 9
// and that the plan completes — WIPEOUT (the entity), WIPEOUTVARIABLES and SCALE (objects). Reading is here, the
// writing in dxf_writer_existing.cpp.
//
// Every group is read by code, in any order, as the reader of 1.8.1 read them (AutoCAD's order is the one the
// writer gives); what the codec adds is that a value that is not a number, a count that the record cannot hold
// and a subclass the record lacks are errors — the record then stays as it was read, with a warning (B105).

namespace hdxf {
namespace internal {

namespace {

// ---------------------------------------------------------------------------
// WIPEOUT
// ---------------------------------------------------------------------------

// AutoCAD's WIPEOUT has AcDbWipeout; a file from another producer may have AcDbRasterImage there, which has
// the same groups
bool read_image_groups(GroupCursor& c, ImageData& d) {
    while (c.ok() && !c.at_end()) {
        const size_t before = c.remaining();
        switch (c.code()) {
            case 90:  c.integer(90, d.class_version); break;
            case 10:  c.point3(10, d.insertion_point); break;
            case 11:  c.point3(11, d.u_vector); break;
            case 12:  c.point3(12, d.v_vector); break;
            case 13:  c.point2(13, d.size); break;
            case 340: c.handle(340, d.image_def_handle); break;
            case 70:  c.integer(70, d.display_flags); break;
            case 280: c.boolean(280, d.clipping); break;
            case 281: c.integer(281, d.brightness); break;
            case 282: c.integer(282, d.contrast); break;
            case 283: c.integer(283, d.fade); break;
            case 290: c.boolean(290, d.clip_inside); break;
            case 360: c.handle(360, d.reactor_handle); break;
            case 71:  c.integer(71, d.clip_boundary_type); break;
            case 91: {
                // The number of boundary vertices, each a 14 and a 24: checked against the groups there are
                size_t n = 0;
                c.count(91, 2, n);
                break;
            }
            case 14: {
                Point2 p;
                if (c.point2(14, p)) d.clip_boundary.push_back(p);
                break;
            }
            default: break;
        }
        if (c.remaining() == before) c.skip(1);     // a group the type does not know
    }
    return c.ok();
}

bool read_wipeout(const RecordView& view, TypedReadContext& context, Entity& e) {
    const char* subclass = view.find_subclass("AcDbWipeout") ? "AcDbWipeout" : "AcDbRasterImage";
    GroupCursor c(view, subclass);
    // A record without the subclass has the defaults, as the reader of 1.8.1 gave it
    ImageData d;
    if (c.valid() && !read_image_groups(c, d)) return context.fail(c.error());
    e.data = Boxed<ImageData>(std::move(d));
    return true;
}

void wipeout_references(const Entity& e, std::vector<TypedReference>& out) {
    const ImageData& d = e.as_wipeout();
    // AutoCAD's WIPEOUT has neither (340 and 360 are 0); a file from elsewhere may point to an IMAGEDEF
    if (!d.image_def_handle.empty() && d.image_def_handle != "0") out.push_back({d.image_def_handle, RefKind::HardPointer, false});
    if (!d.reactor_handle.empty() && d.reactor_handle != "0") out.push_back({d.reactor_handle, RefKind::HardOwner, false});
}

void wipeout_extents(const Entity& e, TypedExtentsSink& sink) {
    for (const Point3& p : wipeout_extent_points(e.as_wipeout())) sink.add_wcs(p);
}

// ---------------------------------------------------------------------------
// WIPEOUTVARIABLES and SCALE
// ---------------------------------------------------------------------------

bool read_wipeout_variables(const RecordView& view, TypedReadContext& context, Object& o) {
    GroupCursor c(view, "AcDbWipeoutVariables");
    WipeoutVariablesData d;
    int16_t frame = 0;
    if (c.integer(70, frame)) d.display_frame = frame;
    if (!c.ok()) return context.fail(c.error());
    if (!c.at_end()) {
        // A later AutoCAD's groups: kept in the preserved record
        c.take_rest();
        context.warn("WIPEOUTVARIABLES has groups this version does not know (kept as they were read)");
    }
    o.data = d;
    return true;
}

bool read_scale(const RecordView& view, TypedReadContext& context, Object& o) {
    GroupCursor c(view, "AcDbScale");
    ScaleData d;
    int16_t flags = 0;
    if (c.integer(70, flags)) d.flags = flags;
    c.string(300, d.name);
    c.real(140, d.paper_units);
    c.real(141, d.drawing_units);
    c.boolean(290, d.unit_scale);
    if (!c.ok()) return context.fail(c.error());
    if (!c.at_end()) {
        c.take_rest();
        context.warn("SCALE has groups this version does not know (kept as they were read)");
    }
    o.data = d;
    return true;
}

// What AutoCAD 2025 saved of each (doc/facit/fas9_existing_fields.scr): the type in 2000 … 2018, ASCII and binary;
// R12 has neither OBJECTS nor a WIPEOUT. R13 and R14 AutoCAD 2025 cannot save
VersionMatrix native_from_2000() {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2000);
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

} // namespace

// The boundary of a WIPEOUT in the WCS: the same polygon its proxy graphics draw. AutoCAD's own extents of a
// wipeout are this polygon's box, not the picture's four corners (AutoCAD 2025: a polygon of 50 x 40 in a picture of
// 50 x 50 has the extents 50 x 40; rotated and mirrored the same)
std::vector<Point3> wipeout_extent_points(const ImageData& d) { return image_outline(d); }

void register_existing_codecs(TypedRegistry& registry) {
    {
        EntityCodec c;
        c.dxf_name = "WIPEOUT";
        c.type = EntityType::Wipeout;
        c.read = read_wipeout;
        c.write = write_wipeout_record;
        c.references = wipeout_references;
        c.extents = wipeout_extents;
        c.versions = native_from_2000();
        c.step = 2;
        RefDescriptor image;
        image.subclass = "AcDbWipeout";
        image.code = 340;
        image.kind = RefKind::HardPointer;
        image.removal = RefRemoval::ZeroInPlace;
        c.refs.push_back(image);
        RefDescriptor reactor;
        reactor.subclass = "AcDbWipeout";
        reactor.code = 360;
        reactor.kind = RefKind::HardOwner;
        reactor.removal = RefRemoval::ZeroInPlace;
        c.refs.push_back(reactor);
        // The boundary: its count (91) and the vertices (14, 24) are one list
        ListSpec boundary;
        boundary.subclass = "AcDbWipeout";
        boundary.codes = {91, 14, 24};
        c.lists.push_back(boundary);
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "WIPEOUTVARIABLES";
        c.type = ObjectType::WipeoutVariables;
        c.read = read_wipeout_variables;
        c.write = write_wipeout_variables_record;
        c.versions = native_from_2000();
        c.step = 2;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "SCALE";
        c.type = ObjectType::Scale;
        c.read = read_scale;
        c.write = write_scale_record;
        c.versions = native_from_2000();
        c.step = 2;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
