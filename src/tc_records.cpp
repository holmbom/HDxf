#include "ExtensionTypes.hpp"
#include "proxy_record_graphics.hpp"
#include "text_layout.hpp"
#include <string>
#include <vector>

// TurboCAD's records (phase 9, step 9.18; plan 2026-10-27-fas-9-typer.md, "TCOPTIONALDATA och TCPOINTENTITY"; the old plan's B.74 and
// B.75): TCOPTIONALDATA (an object, application ExTcadCustObjs) and TCPOINTENTITY (an entity), as codecs of the registry. The grammar
// is what TurboCAD's files show (17 forms in AC1014, AC1018 and AC1021 files; third-party observations, not AutoCAD facit: F9.25,
// ej testat).
//
// TCOPTIONALDATA: TcOptionalData 70 (1 or 2), 70 (flags), then by the flags 1 (the text) when 1, 10 20 30 when 2, 90 (count) and the
// properties when 128, 1 and 70 (the font) when 16, 40 when 32. A property is 1 (its name), 70 (its type) and the value: type 5 a
// real (40), type 8 a string (group 1 in a version 1 record; 90 and 310, the bytes, in version 2), type 18 an integer (70). Another
// type, or a flag bit that is none of the five, is not established: the groups after the last known one stay in the preserved record.
//
// TCPOINTENTITY: AcDbEntity with its proxy graphics (92 before 2010, 160 from it, then 310: the point's symbol), AcDbPoint (10 20 30)
// and TcPointEntity 70, 71, 40. What the drawing's extents count is the symbol, as for any proxy: the extents command of the graphics
// when it has one, else its geometry.

namespace hdxf {
namespace internal {

namespace {

constexpr int kKnownFlags = 1 | 2 | 16 | 32 | 128;

template <typename D>
const D& data_of(const Object& o) { return *o.data.get<Boxed<D>>(); }

bool read_property(GroupCursor& c, int version, TcProperty& p) {
    rds(c, 1, p.name);
    rd(c, 70, p.type);
    if (!c.ok()) return false;
    switch (p.type) {
        case 5:
            rd(c, 40, p.real);
            break;
        case 8:
            if (version >= 2) {
                int32_t length = 0;
                rd(c, 90, length);
                if (c.ok()) {
                    c.bytes(310, p.blob);
                    if (c.ok() && (length < 0 || static_cast<size_t>(length) != p.blob.size())) {
                        c.fail("the property \"" + p.name + "\" says " + std::to_string(length) + " byte(s) and has " + std::to_string(p.blob.size()));
                    }
                }
            } else {
                rds(c, 1, p.text);
            }
            break;
        case 18:
            rd(c, 70, p.integer);
            break;
        default:
            c.fail("the property \"" + p.name + "\" has the type " + std::to_string(p.type) + ", whose value is not established");
            break;
    }
    return c.ok();
}

bool read_optional_data(const RecordView& view, TypedReadContext& ctx, Object& o) {
    TcOptionalDataData d;
    GroupCursor c(view, "TcOptionalData");
    if (!c.valid()) return ext_missing(view, ctx, "TcOptionalData");
    int32_t flags = 0;
    rd(c, 70, d.version);
    rd(c, 70, flags);
    if (c.ok() && (flags < 0 || flags > 0xFFFF)) c.fail("the flags " + std::to_string(flags) + " are not a 16 bit set");
    if (!c.ok()) return ctx.fail(c.error());
    d.other_flags = static_cast<uint16_t>(flags & ~kKnownFlags);
    // Groups after a flag this reader does not know are not read: they stay in the preserved record
    if (flags & 1) {
        std::string text;
        rds(c, 1, text);
        d.text = text;
    }
    if (flags & 2) {
        Point3 p;
        if (c.ok() && c.require_point3(10, p)) d.point = p;
    }
    if (flags & 128) {
        std::vector<TcProperty> list;
        rd_counted(c, 90, 3, [&] {
            TcProperty p;
            if (read_property(c, d.version, p)) list.push_back(std::move(p));
        });
        d.properties = std::move(list);
    }
    if (flags & 16) {
        TcFont f;
        rds(c, 1, f.name);
        rd(c, 70, f.reserved_70_0);
        d.font = f;
    }
    if (flags & 32) {
        double v = 0.0;
        rd(c, 40, v);
        d.real = v;
    }
    if (!ext_done(c, ctx, view)) return false;
    o.data = Boxed<TcOptionalDataData>(std::move(d));
    return true;
}

void write_optional_data(RecordWriter& w, const Object& o) {
    const TcOptionalDataData& d = data_of<TcOptionalDataData>(o);
    if (w.version() == DxfVersion::R13 && o.raw.empty()) {
        w.fail("TCOPTIONALDATA in DXF R13 is not established: no TurboCAD file of that version was seen (a record read from a file is kept as it was)");
        return;
    }
    int flags = d.other_flags;
    if (d.text) flags |= 1;
    if (d.point) flags |= 2;
    if (d.font) flags |= 16;
    if (d.real) flags |= 32;
    if (d.properties) flags |= 128;
    w.object_head("TCOPTIONALDATA", o);
    w.subclass("TcOptionalData");
    wr16(w, 70, d.version);
    wr16(w, 70, flags > 32767 ? flags - 65536 : flags);
    if (d.text) wrs(w, 1, *d.text);
    if (d.point) w.point3(10, *d.point);
    if (d.properties) {
        wr_count(w, 90, d.properties->size());
        for (const TcProperty& p : *d.properties) {
            wrs(w, 1, p.name);
            wr16(w, 70, p.type);
            switch (p.type) {
                case 5:
                    wrd(w, 40, p.real);
                    break;
                case 8:
                    if (d.version >= 2) {
                        w.integer(90, static_cast<int64_t>(p.blob.size()));
                        w.bytes(310, p.blob);
                    } else {
                        wrs(w, 1, p.text);
                    }
                    break;
                case 18:
                    wr16(w, 70, p.integer);
                    break;
                default:
                    w.fail("TCOPTIONALDATA's property \"" + p.name + "\" has the type " + std::to_string(p.type) + ", whose value is not established");
                    break;
            }
        }
    }
    if (d.font) {
        wrs(w, 1, d.font->name);
        wr16(w, 70, d.font->reserved_70_0);
    }
    if (d.real) wrd(w, 40, *d.real);
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// TCPOINTENTITY
// ---------------------------------------------------------------------------

bool read_point(const RecordView& view, TypedReadContext& ctx, Entity& e) {
    TcPointData d;
    {
        GroupCursor c(view, "AcDbPoint");
        if (!c.valid()) return ext_missing(view, ctx, "AcDbPoint");
        if (c.ok()) c.require_point3(10, d.point.location);
        double thickness = 0.0;
        if (c.real(39, thickness)) e.props.thickness = thickness;
        Point3 normal;
        if (c.point3(210, normal)) e.props.extrusion = normal;
        c.real(50, d.point.x_axis_angle);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "TcPointEntity");
        if (!c.valid()) return ext_missing(view, ctx, "TcPointEntity");
        rd(c, 70, d.version);
        rd(c, 71, d.flags);
        rd(c, 40, d.size);
        if (!ext_done(c, ctx, view)) return false;
    }
    // The symbol: the entity's own proxy graphics (AcDbEntity 92/160 + 310)
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &d.proxy_graphics, error)) return ctx.fail(error);
    e.data = Boxed<TcPointData>(std::move(d));
    return true;
}

void write_point(RecordWriter& w, const Entity& e, const std::string& owner) {
    const TcPointData& d = *std::get<Boxed<TcPointData>>(e.data);
    if (w.older() && e.raw.empty()) {
        w.fail("TCPOINTENTITY in DXF R13 or R14 is not established: no TurboCAD file of those versions has it (a record read from a file is kept as it was)");
        return;
    }
    const bool graphics = !w.older() && w.proxy_graphics_enabled() && !d.proxy_graphics.empty();
    w.entity_head("TCPOINTENTITY", e, owner, graphics ? &d.proxy_graphics : nullptr);
    w.subclass("AcDbPoint");
    w.point3(10, d.point.location);
    if (e.props.thickness != 0.0) wrd(w, 39, e.props.thickness);
    const Point3& n = e.props.extrusion;
    if (n.x != 0.0 || n.y != 0.0 || n.z != 1.0) w.point3(210, n);
    if (d.point.x_axis_angle != 0.0) wrd(w, 50, d.point.x_axis_angle);
    w.subclass("TcPointEntity");
    wr16(w, 70, d.version);
    wr16(w, 71, d.flags);
    wrd(w, 40, d.size);
    w.xdata(e.props.xdata);
}

void point_extents(const Entity& e, TypedExtentsSink& sink) {
    const TcPointData& d = *std::get<Boxed<TcPointData>>(e.data);
    if (d.proxy_graphics.empty()) return;
    const int code_page = sink.drawing ? drawing_code_page(*sink.drawing) : 1252;
    for (const Point3& p : proxy_graphics_extent_points(d.proxy_graphics, code_page)) sink.add_wcs(p);
}

} // namespace

void register_tc_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "TCOPTIONALDATA";
        c.type = ObjectType::TcOptionalData;
        c.read = read_optional_data;
        c.write = write_optional_data;
        c.cls = ext_class("TcOptionalData", "ExTcadCustObjs|Description: TurboCAD extension data object", 4095, true);
        // TurboCAD's files of AC1014 (R14) and AC1018 ... AC1021 have it; no file of R13
        c.versions = ext_object_matrix();
        c.versions.set(DxfVersion::R14, Generation::Native, ErrorBasis::NotApplicable);
        // The properties and the flags that say which groups follow are one unit
        ListSpec all;
        all.subclass = "TcOptionalData";
        all.id = 1;
        all.shape = ListShape::WholeSubclass;
        c.lists = {all};
        c.step = 18;
        registry.add(std::move(c));
    }
    {
        EntityCodec c;
        c.dxf_name = "TCPOINTENTITY";
        c.type = EntityType::TcPoint;
        c.read = read_point;
        c.write = write_point;
        c.extents = point_extents;
        c.cls = ext_class("TcPointEntity", "ExTcadCustObjs|Description: TurboCAD point", 4095);
        c.versions = ext_object_matrix();
        c.step = 18;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
