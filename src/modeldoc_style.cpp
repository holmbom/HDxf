#include "TypedRecordTypes.hpp"
#include <string>
#include <vector>

// The ModelDoc view styles (phase 9, step 9.12; plan 2026-10-27-fas-9-typer.md, "ACDBDETAILVIEWSTYLE och
// ACDBSECTIONVIEWSTYLE"; phase 6, B77): ACDBDETAILVIEWSTYLE (AcDbModelDocViewStyle, AcDbDetailViewStyle) and
// ACDBSECTIONVIEWSTYLE (AcDbModelDocViewStyle, AcDbSectionViewStyle) as the codecs of the registry read and write them,
// replacing the reader and writer branches of phase 6 (which kept everything after the base subclass as a list of groups):
// the base keeps the groups and the XRECORD form of 1.8.1 (drawing_object_fields.cpp), the rest is typed.
//
// What AutoCAD's own files show (every drawing AutoCAD 2025 saves has the template's Imperial24 and Metric50 of both kinds:
// doc/facit/acad_<ver>_dxf_basic.dxf and the 800 records of the corpus, F9.26):
//   - AcDbModelDocViewStyle: 70, 3 (the name), 290, and from 2018 on 300 (the display name) and 90 (its flags); before 2018
//     they are the XRECORD of the style's extension dictionary
//   - the derived subclass: 70 (0), then five parts, each begun by 71 with its own number (0 ... 4):
//     detail  0: 90 | 1: 340 62 40, 340 62 40, 300 40 280 | 2: 340 90 62 | 3: 340 62 40 90 40 90 300 | 4: 340 90 62, 340 90 62, 280
//     section 0: 90 | 1: 340 62 40, 340 340 62 40, 300 40 90 40 90 | 2: 340 90 62, 340 90 62, 40 40 40 | 3: as the detail's |
//             4: 62 62 300 40 90 290 290 90 (40 x the count)
//   - DXF 2000's records have none of the 62: the colors are left out there (and empty in a record read from 2000)
//   - 340: a hard pointer to a STYLE (the identifiers and the labels), an LTYPE (the lines) or, for the arrows, a block
//     record (0 in the template: AutoCAD's built-in arrow)

namespace hdxf {
namespace internal {

namespace {

std::string describe_record(const RecordView& view) {
    return view.type_name() + " " + view.head().handle;
}

void note_rest(GroupCursor& c, TypedReadContext& ctx, const RecordView& view) {
    if (!c.ok() || c.at_end()) return;
    size_t n = c.take_rest().size();
    ctx.warn(describe_record(view) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
             ") are kept as they were read");
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// A part of the derived subclass begins with 71 and its number
bool part(GroupCursor& c, int16_t number) {
    int16_t n = -1;
    if (!c.integer(71, n)) {
        if (c.ok()) c.fail("part " + std::to_string(number) + " of the style does not begin (group 71 is missing)");
        return false;
    }
    if (n != number) {
        c.fail("part " + std::to_string(number) + " of the style begins with 71 " + std::to_string(n));
        return false;
    }
    return true;
}

// The groups of 70 ... 79 and 280 ... 289 are 16-bit integers: a value that does not fit is an error, not truncated
void get16(GroupCursor& c, int code, int& out) {
    int16_t v = static_cast<int16_t>(out);
    if (c.integer(code, v)) out = v;
}

void need16(GroupCursor& c, int code, int& out) {
    int16_t v = static_cast<int16_t>(out);
    if (c.require_integer(code, v)) out = v;
}

void read_color(GroupCursor& c, ViewStyleColor& out) {
    int16_t v = 0;
    if (c.integer(62, v)) out = v;
}

void read_line(GroupCursor& c, ViewStyleLineFormat& f) {
    c.handle(340, f.linetype_handle);
    c.integer(90, f.lineweight);
    read_color(c, f.color);
}

void read_label(GroupCursor& c, ViewLabelFormat& f) {
    c.handle(340, f.style_handle);
    read_color(c, f.color);
    c.real(40, f.height);
    c.integer(90, f.position_flag);
    c.real(40, f.offset);
    c.integer(90, f.alignment_flag);
    c.string(300, f.pattern);
}

bool read_base(const RecordView& view, TypedReadContext& ctx, ModelDocViewStyleData& b) {
    GroupCursor c(view, "AcDbModelDocViewStyle");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbModelDocViewStyle subclass");
    need16(c, 70, b.version);
    c.require_string(3, b.name);
    c.require_boolean(290, b.flag);
    c.string(300, b.display_name);
    c.integer(90, b.display_flags);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    return true;
}

bool read_detail(const RecordView& view, TypedReadContext& ctx, Object& o) {
    DetailViewStyleData d;
    if (!read_base(view, ctx, d.base)) return false;
    GroupCursor c(view, "AcDbDetailViewStyle");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbDetailViewStyle subclass");
    need16(c, 70, d.version);
    if (part(c, 0)) c.require_integer(90, d.flags);
    if (c.ok() && part(c, 1)) {
        DetailIdentifierFormat& f = d.identifier;
        c.handle(340, f.style_handle);
        read_color(c, f.color);
        c.require_real(40, f.height);
        c.handle(340, f.arrow_handle);
        read_color(c, f.arrow_color);
        c.require_real(40, f.arrow_size);
        c.string(300, f.text);
        c.require_real(40, f.offset);
        get16(c, 280, f.flags);
    }
    if (c.ok() && part(c, 2)) read_line(c, d.boundary_line);
    if (c.ok() && part(c, 3)) read_label(c, d.label);
    if (c.ok() && part(c, 4)) {
        read_line(c, d.lines[0]);
        read_line(c, d.lines[1]);
        get16(c, 280, d.end_flags);
    }
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = Boxed<DetailViewStyleData>(std::move(d));
    return true;
}

bool read_section(const RecordView& view, TypedReadContext& ctx, Object& o) {
    SectionViewStyleData d;
    if (!read_base(view, ctx, d.base)) return false;
    GroupCursor c(view, "AcDbSectionViewStyle");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbSectionViewStyle subclass");
    need16(c, 70, d.version);
    if (part(c, 0)) c.require_integer(90, d.flags);
    if (c.ok() && part(c, 1)) {
        SectionIdentifierFormat& f = d.identifier;
        c.handle(340, f.style_handle);
        read_color(c, f.color);
        c.require_real(40, f.height);
        c.handle(340, f.start_arrow_handle);
        c.handle(340, f.end_arrow_handle);
        read_color(c, f.arrow_color);
        c.require_real(40, f.arrow_size);
        c.string(300, f.excluded_characters);
        c.require_real(40, f.offset);
        c.integer(90, f.flags_a);
        c.require_real(40, f.distance);
        c.integer(90, f.flags_b);
    }
    if (c.ok() && part(c, 2)) {
        read_line(c, d.lines[0]);
        read_line(c, d.lines[1]);
        for (double& m : d.measures) c.require_real(40, m);
    }
    if (c.ok() && part(c, 3)) read_label(c, d.label);
    if (c.ok() && part(c, 4)) {
        SectionHatchFormat& h = d.hatch;
        read_color(c, h.background_color);
        read_color(c, h.foreground_color);
        c.string(300, h.pattern);
        c.require_real(40, h.scale);
        c.integer(90, h.flags);
        c.boolean(290, h.flag_a);
        c.boolean(290, h.flag_b);
        size_t n = 0;
        if (c.count(90, 1, n)) {
            h.angles.reserve(n);
            for (size_t i = 0; i < n && c.ok(); ++i) {
                double angle = 0.0;
                c.require_real(40, angle);
                h.angles.push_back(angle);
            }
        }
    }
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = Boxed<SectionViewStyleData>(std::move(d));
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

// The 62 of a part: AutoCAD 2004 and later have it (ByLayer when the style has none), 2000's record does not
void write_color(RecordWriter& w, const ViewStyleColor& color) {
    if (w.version() >= DxfVersion::R2004) w.integer(62, color.value_or(256));
}

// A pointer to a table record or a block: the handle, 0 for none, 0 (with a warning) for a record that is gone (B19)
void write_ref(RecordWriter& w, int code, const std::string& handle, const std::string& who) {
    if (handle.empty() || handle == "0") {
        w.handle(code, "0");
        return;
    }
    if (!w.live(handle)) {
        if (w.filtering()) w.warn(who + ": the record " + handle + " (group " + std::to_string(code) + ") no longer exists, set to none (0)");
        w.handle(code, "0");
        return;
    }
    w.handle(code, handle);
}

void write_line(RecordWriter& w, const ViewStyleLineFormat& f, const std::string& who) {
    write_ref(w, 340, f.linetype_handle, who);
    w.integer(90, f.lineweight);
    write_color(w, f.color);
}

void write_label(RecordWriter& w, const ViewLabelFormat& f, const std::string& who) {
    write_ref(w, 340, f.style_handle, who);
    write_color(w, f.color);
    w.real(40, f.height);
    w.integer(90, f.position_flag);
    w.real(40, f.offset);
    w.integer(90, f.alignment_flag);
    w.text(300, f.pattern);
}

void write_base(RecordWriter& w, const char* name, const Object& o, const ModelDocViewStyleData& b) {
    w.object_head(name, o);
    w.subclass("AcDbModelDocViewStyle");
    w.integer(70, b.version);
    w.text(3, b.name);
    w.boolean(290, b.flag);
    // 2018 has the display name and its flags itself; before they are the XRECORD of the extension dictionary (B77)
    if (w.version() >= DxfVersion::R2018) {
        w.text(300, b.display_name);
        w.integer(90, b.display_flags);
    }
}

bool refuse_older(RecordWriter& w, const char* name, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail(std::string(name) + " in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return true;
    }
    return false;
}

void write_detail(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "ACDBDETAILVIEWSTYLE", o)) return;
    const DetailViewStyleData& d = o.as_detail_view_style();
    const std::string who = "ACDBDETAILVIEWSTYLE " + o.handle;
    write_base(w, "ACDBDETAILVIEWSTYLE", o, d.base);
    w.subclass("AcDbDetailViewStyle");
    w.integer(70, d.version);
    w.integer(71, 0);
    w.integer(90, d.flags);
    w.integer(71, 1);
    const DetailIdentifierFormat& f = d.identifier;
    write_ref(w, 340, f.style_handle, who);
    write_color(w, f.color);
    w.real(40, f.height);
    write_ref(w, 340, f.arrow_handle, who);
    write_color(w, f.arrow_color);
    w.real(40, f.arrow_size);
    w.text(300, f.text);
    w.real(40, f.offset);
    w.integer(280, f.flags);
    w.integer(71, 2);
    write_line(w, d.boundary_line, who);
    w.integer(71, 3);
    write_label(w, d.label, who);
    w.integer(71, 4);
    write_line(w, d.lines[0], who);
    write_line(w, d.lines[1], who);
    w.integer(280, d.end_flags);
    w.xdata(o.xdata);
}

void write_section(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "ACDBSECTIONVIEWSTYLE", o)) return;
    const SectionViewStyleData& d = o.as_section_view_style();
    const std::string who = "ACDBSECTIONVIEWSTYLE " + o.handle;
    write_base(w, "ACDBSECTIONVIEWSTYLE", o, d.base);
    w.subclass("AcDbSectionViewStyle");
    w.integer(70, d.version);
    w.integer(71, 0);
    w.integer(90, d.flags);
    w.integer(71, 1);
    const SectionIdentifierFormat& f = d.identifier;
    write_ref(w, 340, f.style_handle, who);
    write_color(w, f.color);
    w.real(40, f.height);
    write_ref(w, 340, f.start_arrow_handle, who);
    write_ref(w, 340, f.end_arrow_handle, who);
    write_color(w, f.arrow_color);
    w.real(40, f.arrow_size);
    w.text(300, f.excluded_characters);
    w.real(40, f.offset);
    w.integer(90, f.flags_a);
    w.real(40, f.distance);
    w.integer(90, f.flags_b);
    w.integer(71, 2);
    write_line(w, d.lines[0], who);
    write_line(w, d.lines[1], who);
    for (double m : d.measures) w.real(40, m);
    w.integer(71, 3);
    write_label(w, d.label, who);
    w.integer(71, 4);
    const SectionHatchFormat& h = d.hatch;
    write_color(w, h.background_color);
    write_color(w, h.foreground_color);
    w.text(300, h.pattern);
    w.real(40, h.scale);
    w.integer(90, h.flags);
    w.boolean(290, h.flag_a);
    w.boolean(290, h.flag_b);
    w.integer(90, static_cast<int64_t>(h.angles.size()));
    for (double angle : h.angles) w.real(40, angle);
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// Relations (B19, B104), lists (B12), class and versions
// ---------------------------------------------------------------------------

void add_pointer(const std::string& handle, std::vector<TypedReference>& out) {
    if (!handle.empty() && handle != "0") out.push_back({handle, RefKind::HardPointer, false});
}

void detail_references(const Object& o, std::vector<TypedReference>& out) {
    const DetailViewStyleData& d = o.as_detail_view_style();
    add_pointer(d.identifier.style_handle, out);
    add_pointer(d.identifier.arrow_handle, out);
    add_pointer(d.boundary_line.linetype_handle, out);
    add_pointer(d.label.style_handle, out);
    add_pointer(d.lines[0].linetype_handle, out);
    add_pointer(d.lines[1].linetype_handle, out);
}

void section_references(const Object& o, std::vector<TypedReference>& out) {
    const SectionViewStyleData& d = o.as_section_view_style();
    add_pointer(d.identifier.style_handle, out);
    add_pointer(d.identifier.start_arrow_handle, out);
    add_pointer(d.identifier.end_arrow_handle, out);
    add_pointer(d.lines[0].linetype_handle, out);
    add_pointer(d.lines[1].linetype_handle, out);
    add_pointer(d.label.style_handle, out);
}

// Every 340 of the derived subclass is a hard pointer to a table record or a block; a record that is gone gives 0 in its place
std::vector<RefDescriptor> view_style_refs(const char* subclass) {
    RefDescriptor pointer;
    pointer.subclass = subclass;
    pointer.code = 340;
    pointer.kind = RefKind::HardPointer;
    pointer.removal = RefRemoval::ZeroInPlace;
    return {pointer};
}

// The parts are written back whole when any of them changed: the 71 dividers and the repeated 40/90/340/62 mean what their
// place says
std::vector<ListSpec> view_style_lists(const char* subclass) {
    ListSpec parts;
    parts.subclass = subclass;
    parts.id = 1;
    parts.shape = ListShape::WholeSubclass;
    return {parts};
}

// 2000 on: AutoCAD's own files of 2000 ... 2018 have both; AutoCAD's R12 files have neither. R13 and R14 cannot be saved
// by AutoCAD 2025 (NotSavable): ej testat
VersionMatrix view_style_matrix() {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2000);
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

ClassSpec view_style_class(const char* cpp_name) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 1025;
    c.in_older = false;
    return c;
}

} // namespace

void register_modeldoc_style_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "ACDBDETAILVIEWSTYLE";
        c.type = ObjectType::DetailViewStyle;
        c.read = read_detail;
        c.write = write_detail;
        c.references = detail_references;
        c.cls = view_style_class("AcDbDetailViewStyle");
        c.versions = view_style_matrix();
        c.refs = view_style_refs("AcDbDetailViewStyle");
        c.lists = view_style_lists("AcDbDetailViewStyle");
        c.step = 12;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBSECTIONVIEWSTYLE";
        c.type = ObjectType::SectionViewStyle;
        c.read = read_section;
        c.write = write_section;
        c.references = section_references;
        c.cls = view_style_class("AcDbSectionViewStyle");
        c.versions = view_style_matrix();
        c.refs = view_style_refs("AcDbSectionViewStyle");
        c.lists = view_style_lists("AcDbSectionViewStyle");
        c.step = 12;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
