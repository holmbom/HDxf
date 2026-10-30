#include "TypedRecordTypes.hpp"
#include "proxy_record.hpp"
#include <string>
#include <vector>

// OLEFRAME and OLE2FRAME (phase 9, step 9.3; plan 2026-10-27-fas-9-typer.md, old plan "OLEFRAME och OLE2FRAME").
//
// Both hold an OLE object as the bytes of its compound file: 90 is their length, 310 the bytes (127 to a group)
// and 1 the string "OLE" that ends them (DXF reference, Oleframe / Ole2frame group codes, every edition from 2000).
// HDxf keeps the bytes as they are — the application's content is opaque and no OLE server is started. What
// OLE2FRAME adds (3 the application's name, 10 and 11 the frame's corners, 71 the kind of object, 72 the space
// the object is in) is, according to Autodesk, derived from the OLE content and ignored when AutoCAD opens a
// file; the code 3 is a string although the table of the reference calls it a length (Autodesk's own sample
// says "Paintbrush Picture"; 90 is the length). They are read and written as they are.
//
// Reading is grammar driven (B105): the groups of the reference in their order, the byte count of 90 checked
// against the bytes before anything is kept, the terminator 1 checked to be "OLE". A record that does not fit is
// not typed: it stays as it was read, with a warning (typed_read_entity's contract).

namespace hdxf {
namespace internal {

namespace {

// What follows 70 in both classes: 90, the bytes, and the terminator
bool read_ole_payload(GroupCursor& c, std::vector<uint8_t>& data) {
    int32_t length = 0;
    if (!c.require_integer(90, length)) return false;
    if (length < 0) {
        c.fail("the OLE data's length (90) is negative");
        return false;
    }
    data.clear();
    c.bytes(310, data);
    if (!c.ok()) return false;
    if (data.size() != static_cast<size_t>(length)) {
        c.fail("the OLE data has " + std::to_string(data.size()) + " bytes in its 310 groups, 90 says " +
               std::to_string(length));
        return false;
    }
    std::string end;
    if (!c.require_string(1, end)) return false;
    // (the terminator has the code's own text: trailing blanks of an ASCII line are no part of it)
    while (!end.empty() && end.back() == ' ') end.pop_back();
    if (end != "OLE") {
        c.fail("the OLE data does not end with \"OLE\" (1) but with \"" + end + "\"");
        return false;
    }
    return true;
}

void write_ole_payload(RecordWriter& w, const std::vector<uint8_t>& data) {
    w.integer(90, static_cast<int64_t>(data.size()));
    w.bytes(310, data);
    w.text(1, "OLE");
}

// The entity's own proxy graphics (AcDbEntity 92/160 and 310): only from 2000 on and when the drawing keeps
// proxy graphics ($PROXYGRAPHICS)
const std::vector<uint8_t>* graphics_to_write(const RecordWriter& w, const std::vector<uint8_t>& graphics) {
    if (graphics.empty() || w.older() || !w.proxy_graphics_enabled()) return nullptr;
    return &graphics;
}

// What a group after the terminator would be: a later subversion of the class. It is kept (the preserved
// record), not typed, with a warning (B105)
void note_future_groups(GroupCursor& c, TypedReadContext& context, const char* type) {
    if (c.at_end()) return;
    std::vector<DxfGroup> rest = c.take_rest();
    context.warn(std::string(type) + ": " + std::to_string(rest.size()) +
                 " groups after the OLE data's end are not typed (a later subversion); they stay in the preserved record");
}

bool take_entity_graphics(const RecordView& view, TypedReadContext& context, std::vector<uint8_t>& graphics) {
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &graphics, error)) return context.fail(error);
    return true;
}

// ---------------------------------------------------------------------------
// OLEFRAME
// ---------------------------------------------------------------------------

bool read_oleframe(const RecordView& view, TypedReadContext& context, Entity& e) {
    GroupCursor c(view, "AcDbOleFrame");
    if (!c.valid()) return context.fail("OLEFRAME has no AcDbOleFrame subclass");
    OleFrameData d;
    c.require_integer(70, d.version);
    if (!c.ok() || !read_ole_payload(c, d.data)) return context.fail(c.error());
    note_future_groups(c, context, "OLEFRAME");
    if (!take_entity_graphics(view, context, d.proxy_graphics)) return false;
    e.data = std::move(d);
    return true;
}

void write_oleframe(RecordWriter& w, const Entity& e, const std::string& owner) {
    const OleFrameData& d = e.as_ole_frame();
    w.entity_head("OLEFRAME", e, owner, graphics_to_write(w, d.proxy_graphics));
    w.subclass("AcDbOleFrame");
    w.integer(70, d.version);
    write_ole_payload(w, d.data);
    w.xdata(e.props.xdata);
}

// ---------------------------------------------------------------------------
// OLE2FRAME
// ---------------------------------------------------------------------------

bool read_ole2frame(const RecordView& view, TypedReadContext& context, Entity& e) {
    GroupCursor c(view, "AcDbOle2Frame");
    if (!c.valid()) return context.fail("OLE2FRAME has no AcDbOle2Frame subclass");
    Ole2FrameData d;
    int32_t object_type = 0;
    c.require_integer(70, d.version);
    c.require_string(3, d.application_name);
    c.require_point3(10, d.upper_left);
    c.require_point3(11, d.lower_right);
    c.require_integer(71, object_type);
    c.require_integer(72, d.tile_mode);
    if (!c.ok() || !read_ole_payload(c, d.data)) return context.fail(c.error());
    d.object_type = static_cast<OleObjectType>(object_type);
    note_future_groups(c, context, "OLE2FRAME");
    if (!take_entity_graphics(view, context, d.proxy_graphics)) return false;
    e.data = std::move(d);
    return true;
}

void write_ole2frame(RecordWriter& w, const Entity& e, const std::string& owner) {
    const Ole2FrameData& d = e.as_ole2_frame();
    w.entity_head("OLE2FRAME", e, owner, graphics_to_write(w, d.proxy_graphics));
    w.subclass("AcDbOle2Frame");
    w.integer(70, d.version);
    w.text(3, d.application_name);
    w.point3(10, d.upper_left);
    w.point3(11, d.lower_right);
    w.integer(71, static_cast<int32_t>(d.object_type));
    w.integer(72, d.tile_mode);
    write_ole_payload(w, d.data);
    w.xdata(e.props.xdata);
}

// The frame AutoCAD draws is the rectangle of 10 and 11 (WCS). What AutoCAD recomputes from the object when it
// opens the file is not known to HDxf: the extents follow the corners as they are written (ej testat, F9.1)
void ole2frame_extents(const Entity& e, TypedExtentsSink& sink) {
    const Ole2FrameData& d = e.as_ole2_frame();
    sink.add_wcs(d.upper_left);
    sink.add_wcs(d.lower_right);
}

// A list of the data: its length and bytes are one value (B12)
std::vector<ListSpec> data_lists(const char* subclass) {
    ListSpec list;
    list.subclass = subclass;
    list.id = 1;
    list.shape = ListShape::Codes;
    list.codes = {90, 310};
    return {list};
}

} // namespace

void register_ole_codecs(TypedRegistry& registry) {
    // OLEFRAME: R13's entity (R13:OLEFRAME) and a page of every reference from 2000 on; the R14 reference has
    // OLE2FRAME instead and no page for OLEFRAME: R14 claims nothing. No AutoCAD file has one (F9.1)
    EntityCodec frame;
    frame.dxf_name = "OLEFRAME";
    frame.type = EntityType::OleFrame;
    frame.read = read_oleframe;
    frame.write = write_oleframe;
    frame.versions = VersionMatrix::native_from(DxfVersion::R13);
    frame.versions.set(DxfVersion::R14, Generation::None, ErrorBasis::NotEstablished);
    frame.lists = data_lists("AcDbOleFrame");
    frame.step = 3;
    registry.add(std::move(frame));

    // OLE2FRAME: R14's (R14:ole2frame) and every later reference; CLASSES has it as AcDbOle2Frame, an entity
    // class (Autodesk's table of default classes: "OLE2FRAME AcDbOle2Frame 0 0 1")
    EntityCodec frame2;
    frame2.dxf_name = "OLE2FRAME";
    frame2.type = EntityType::Ole2Frame;
    frame2.read = read_ole2frame;
    frame2.write = write_ole2frame;
    frame2.extents = ole2frame_extents;
    frame2.cls.cpp_name = "AcDbOle2Frame";
    frame2.cls.application = "ObjectDBX Classes";
    frame2.cls.proxy_flags = 0;
    frame2.cls.in_older = true;
    frame2.cls.older_application = "\"\"";
    frame2.cls.older_flags = 0;
    frame2.versions = VersionMatrix::native_from(DxfVersion::R14);
    frame2.lists = data_lists("AcDbOle2Frame");
    frame2.step = 3;
    registry.add(std::move(frame2));
}

} // namespace internal
} // namespace hdxf
