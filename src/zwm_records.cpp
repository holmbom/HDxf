#include "ExtensionTypes.hpp"
#include <optional>
#include <string>
#include <vector>

// ZWCAD Mechanical's standard (phase 9, step 9.18; plan 2026-10-27-fas-9-typer.md, "ZWM"; the old plan's B.77 ... B.104): the 28
// ZWM*STD records (application ZWMCUSTCORE), as codecs of the registry. The grammar is what the one file the records were seen
// in shows (a ZWCAD drawing, AC1024: third-party observations, not AutoCAD facit; F9.25, ej testat). What the file does not explain
// keeps its place by code and position (`reserved_70_0`, `text_1_1` ...); a list a count gives is a vector.
//
// A record is the subclasses ZwmObject, ZwmStd, [ZwmSymbolStd], the type's own, [ZwmSymProperties and ZWMATTRIBUTEDEF], the type's
// properties. Three forms:
//   - the symbol types (BALLOON, BREAKSYM, ...): ZwmObject, ZwmStd, ZwmSymbolStd, Zwm<X>Std (70 70 1), ZwmSymProperties with its
//     attribute definitions (counted by its last 70, each one's key a 1 before its subclass), Zwm<X>Properties
//   - the style types COLOR, DIMSTYLE, LAYER, LINETYPE, TEXTSTYLE: ZwmObject (its last 70 counts the definitions), the
//     ZWMATTRIBUTEDEF subclasses, ZwmStd, the type's own subclass ending in the tail of eight handles
//   - BOMSTYLE, TITLESTYLE (their properties count the definitions) and FRAMESTYLE (a list of ZwmFrameProperties)
// The value of an attribute definition follows from its fourth string: VT_R8 40, VT_BOOL 70, else 1. ZWMSTANDARD's ZwmSymProperties
// has a 70 of 483351136, outside the 16 bits of a binary group: kept in ASCII as read, never truncated.

namespace hdxf {
namespace internal {

std::string ext_describe(const RecordView& view) { return view.type_name() + " " + view.head().handle; }

bool ext_missing(const RecordView& view, TypedReadContext& ctx, const char* name, size_t occurrence) {
    return ctx.fail(ext_describe(view) + " has no " + name + " subclass" + (occurrence ? " number " + std::to_string(occurrence + 1) : std::string()));
}

bool ext_done(GroupCursor& c, TypedReadContext& ctx, const RecordView& view) {
    if (!c.ok()) return ctx.fail(c.error());
    if (!c.at_end()) {
        size_t n = c.take_rest().size();
        ctx.warn(ext_describe(view) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
                 ") are kept as they were read");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

bool zwm_read_tail(GroupCursor& c, ZwmStdTail& t) {
    rdh(c, 340, t.handle_340);
    rd(c, 70, t.reserved_70);
    rd(c, 90, t.reserved_90);
    rds(c, 1, t.text_1);
    return c.ok();
}

bool zwm_read_object(GroupCursor& c, ZwmObjectHeader& h, size_t& count) {
    rd(c, 70, h.version);
    rds(c, 1, h.text_1);
    rd(c, 70, h.reserved_70_0);
    if (c.ok() && h.reserved_70_0 < 0) c.fail("the count in ZwmObject's last 70 is negative");
    count = c.ok() ? static_cast<size_t>(h.reserved_70_0) : 0;
    return c.ok();
}

bool zwm_read_std(GroupCursor& c, ZwmStd& s) {
    rd(c, 70, s.version);
    rd(c, 70, s.reserved_70_0);
    rd(c, 70, s.reserved_70_1);
    rds(c, 1, s.text_1);
    return zwm_read_tail(c, s.tail);
}

bool zwm_read_symbol_std(GroupCursor& c, ZwmSymbolStd& s) {
    rd(c, 70, s.version);
    return zwm_read_tail(c, s.tail);
}

bool zwm_read_type_std(GroupCursor& c, ZwmTypeStd& s) {
    rd(c, 70, s.version);
    rd(c, 70, s.reserved_70_0);
    rds(c, 1, s.text_1_0);
    return c.ok();
}

bool zwm_read_definition(GroupCursor& c, ZwmAttributeDefinition& d) {
    rd(c, 70, d.version);
    rd(c, 70, d.reserved_70_0);
    rds(c, 1, d.text_1);
    rd(c, 70, d.reserved_70_1);
    if (!c.ok()) return false;
    switch (zwm_value_kind(d)) {
        case ZwmValueKind::Text:    rds(c, 1, d.value.text); break;
        case ZwmValueKind::Integer: rd(c, 70, d.value.integer); break;
        case ZwmValueKind::Real:    rd(c, 40, d.value.real); break;
        case ZwmValueKind::Unsupported:
            c.fail("the attribute definition \"" + d.text_1[0] + "\" has the value type \"" + d.text_1[3] + "\" (variant " +
                   std::to_string(d.reserved_70_1[5]) + "), whose value is not established");
            break;
    }
    return c.ok();
}

bool zwm_read_attributes(const RecordView& view, TypedReadContext& ctx, GroupCursor& c, size_t count, bool keyed,
                         std::vector<ZwmAttributeEntry>& out, std::optional<GroupCursor>& last) {
    out.clear();
    last.reset();
    if (view.subclass_count("ZWMATTRIBUTEDEF") != count) {
        return ctx.fail(ext_describe(view) + ": " + std::to_string(count) + " attribute definition(s) are counted, the record has " +
                        std::to_string(view.subclass_count("ZWMATTRIBUTEDEF")) + " ZWMATTRIBUTEDEF subclass(es)");
    }
    for (size_t i = 0; i < count; ++i) {
        ZwmAttributeEntry e;
        if (keyed) {
            GroupCursor& before = last ? *last : c;
            rds(before, 1, e.key);
            if (!before.ok()) return ctx.fail(before.error());
        }
        last.emplace(view, "ZWMATTRIBUTEDEF", i);
        if (!last->valid()) return ext_missing(view, ctx, "ZWMATTRIBUTEDEF", i);
        if (!zwm_read_definition(*last, e.definition)) return ctx.fail(last->error());
        out.push_back(std::move(e));
    }
    return true;
}

bool zwm_read_symbol_properties(const RecordView& view, TypedReadContext& ctx, ZwmSymProperties& s) {
    GroupCursor c(view, "ZwmSymProperties");
    if (!c.valid()) return ext_missing(view, ctx, "ZwmSymProperties");
    rd(c, 70, s.version);
    rds(c, 1, s.text_1);
    rd(c, 70, s.reserved_70_0);
    rd(c, 70, s.reserved_70_1);
    rd(c, 40, s.reserved_40_0);
    rd(c, 40, s.reserved_40_1);
    rd(c, 70, s.reserved_70_2);
    rd(c, 40, s.reserved_40_2);
    rd(c, 70, s.reserved_70_3);
    rd(c, 70, s.reserved_70_4);
    rd_list(c, 70, 70, s.list_70_a);
    rd_list(c, 70, 70, s.list_70_b);
    rd_list(c, 70, 1, s.list_1_a);
    rd_list(c, 70, 1, s.list_1_b);
    rdh(c, 340, s.handle_340_0);
    rd(c, 40, s.reserved_40_3);
    rd(c, 70, s.reserved_70_5);
    rdh(c, 340, s.handle_340_1);
    rd(c, 70, s.reserved_70_6);
    rd(c, 90, s.reserved_90_0);
    rds(c, 1, s.text_1_1);
    int16_t count = 0;
    rd(c, 70, count);
    if (c.ok() && count < 0) c.fail("the attribute count is negative");
    if (!c.ok()) return ctx.fail(c.error());
    std::optional<GroupCursor> last;
    if (!zwm_read_attributes(view, ctx, c, static_cast<size_t>(count), true, s.attributes, last)) return false;
    if (!ext_done(c, ctx, view)) return false;
    return !last || ext_done(*last, ctx, view);
}

// ---------------------------------------------------------------------------
// The properties of each symbol type
// ---------------------------------------------------------------------------

namespace {

void read_props(GroupCursor& c, ZwmBalloonProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70_0);
    rd_list(c, 70, 70, p.list_70_a);
    p.shapes.clear();
    rd_counted(c, 70, 4, [&] {
        ZwmBalloonShape s;
        rds(c, 1, s.text_1_0);
        rds(c, 1, s.text_1_1);
        rds(c, 1, s.text_1_2);
        rd(c, 70, s.reserved_70_0);
        if (c.ok()) p.shapes.push_back(s);
    });
    rds(c, 1, p.text_1);
    rd(c, 70, p.reserved_70_1);
    rd(c, 40, p.reserved_40);
    rdh(c, 340, p.handle_340_0);
    rds(c, 1, p.text_1_2);
    rd(c, 70, p.reserved_70_2);
}
void write_props(RecordWriter& w, const ZwmBalloonProperties& p, const std::string& who) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70_0);
    wr_list(w, 70, 70, p.list_70_a);
    wr_count(w, 70, p.shapes.size());
    for (const ZwmBalloonShape& s : p.shapes) {
        wrs(w, 1, s.text_1_0);
        wrs(w, 1, s.text_1_1);
        wrs(w, 1, s.text_1_2);
        wr16(w, 70, s.reserved_70_0);
    }
    wrs(w, 1, p.text_1);
    wr16(w, 70, p.reserved_70_1);
    wrd(w, 40, p.reserved_40);
    wrp(w, 340, p.handle_340_0, who);
    wrs(w, 1, p.text_1_2);
    wr16(w, 70, p.reserved_70_2);
}

void read_props(GroupCursor& c, ZwmBreakSymProperties& p) {
    rd(c, 70, p.version);
    rd(c, 40, p.reserved_40);
}
void write_props(RecordWriter& w, const ZwmBreakSymProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wrd(w, 40, p.reserved_40);
}

void read_props(GroupCursor& c, ZwmCenterHoleProperties& p) {
    rd(c, 70, p.version);
    rd_list(c, 70, 1, p.list_1_a);
    rd(c, 70, p.reserved_70_0);
}
void write_props(RecordWriter& w, const ZwmCenterHoleProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr_list(w, 70, 1, p.list_1_a);
    wr16(w, 70, p.reserved_70_0);
}

void read_props(GroupCursor& c, ZwmChamferProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70);
}
void write_props(RecordWriter& w, const ZwmChamferProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70);
}

void read_props(GroupCursor& c, ZwmCircleMarkProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70_0);
}
void write_props(RecordWriter& w, const ZwmCircleMarkProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70_0);
}

void read_props(GroupCursor& c, ZwmDatumIdProperties& p) {
    rd(c, 70, p.version);
    rd(c, 40, p.reserved_40);
}
void write_props(RecordWriter& w, const ZwmDatumIdProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wrd(w, 40, p.reserved_40);
}

void read_props(GroupCursor& c, ZwmDatumTargetProperties& p) {
    rd(c, 70, p.version);
    rd(c, 40, p.reserved_40);
    rdh(c, 340, p.handle_340);
    rd(c, 70, p.reserved_70);
}
void write_props(RecordWriter& w, const ZwmDatumTargetProperties& p, const std::string& who) {
    wr16(w, 70, p.version);
    wrd(w, 40, p.reserved_40);
    wrp(w, 340, p.handle_340, who);
    wr16(w, 70, p.reserved_70);
}

void read_props(GroupCursor& c, ZwmDetailProperties& p) {
    rd(c, 70, p.version);
    rdh(c, 340, p.handle_340_0);
    rd(c, 70, p.reserved_70_0);
    rds(c, 1, p.text_1_0);
    rd(c, 70, p.reserved_70_1);
    rd_list(c, 70, 1, p.list_1_a);
    rd_list(c, 70, 1, p.list_1_b);
}
void write_props(RecordWriter& w, const ZwmDetailProperties& p, const std::string& who) {
    wr16(w, 70, p.version);
    wrp(w, 340, p.handle_340_0, who);
    wr16(w, 70, p.reserved_70_0);
    wrs(w, 1, p.text_1_0);
    wr16(w, 70, p.reserved_70_1);
    wr_list(w, 70, 1, p.list_1_a);
    wr_list(w, 70, 1, p.list_1_b);
}

void read_props(GroupCursor& c, ZwmElevSymProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70_0);
    rd(c, 40, p.reserved_40);
}
void write_props(RecordWriter& w, const ZwmElevSymProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70_0);
    wrd(w, 40, p.reserved_40);
}

void read_props(GroupCursor& c, ZwmFCFrameProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70_0);
    rd(c, 40, p.reserved_40_0);
    rd(c, 70, p.reserved_70_1);
    rd_list(c, 70, p.rows_a);
    rd_list(c, 70, p.rows_b);
    rd(c, 40, p.reserved_40_1);
}
void write_props(RecordWriter& w, const ZwmFCFrameProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70_0);
    wrd(w, 40, p.reserved_40_0);
    wr16(w, 70, p.reserved_70_1);
    wr_list(w, 70, p.rows_a);
    wr_list(w, 70, p.rows_b);
    wrd(w, 40, p.reserved_40_1);
}

void read_props(GroupCursor& c, ZwmFeatureIdProperties& p) {
    rd(c, 70, p.version);
    rd(c, 40, p.reserved_40);
}
void write_props(RecordWriter& w, const ZwmFeatureIdProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wrd(w, 40, p.reserved_40);
}

void read_props(GroupCursor& c, ZwmHoleChartProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70_0);
    for (ZwmRealInt& pair : p.pairs) {
        rd(c, 40, pair.reserved_40_0);
        rd(c, 70, pair.reserved_70_0);
    }
    rd(c, 70, p.reserved_70_1);
    rd(c, 40, p.reserved_40_0);
    rd(c, 70, p.reserved_70_2);
    rd_list(c, 70, 1, p.list_1_a);
    rd_list(c, 70, 1, p.list_1_b);
    rd_list(c, 70, 1, p.list_1_c);
    for (std::vector<ZwmTextPair>& rows : p.rows) rd_list(c, 70, rows);
    rd(c, 70, p.reserved_70_3);
    rd(c, 40, p.reserved_40_1);
}
void write_props(RecordWriter& w, const ZwmHoleChartProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70_0);
    for (const ZwmRealInt& pair : p.pairs) {
        wrd(w, 40, pair.reserved_40_0);
        wr16(w, 70, pair.reserved_70_0);
    }
    wr16(w, 70, p.reserved_70_1);
    wrd(w, 40, p.reserved_40_0);
    wr16(w, 70, p.reserved_70_2);
    wr_list(w, 70, 1, p.list_1_a);
    wr_list(w, 70, 1, p.list_1_b);
    wr_list(w, 70, 1, p.list_1_c);
    for (const std::vector<ZwmTextPair>& rows : p.rows) wr_list(w, 70, rows);
    wr16(w, 70, p.reserved_70_3);
    wrd(w, 40, p.reserved_40_1);
}

void read_props(GroupCursor& c, ZwmNoteProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70);
    rd(c, 40, p.reserved_40);
}
void write_props(RecordWriter& w, const ZwmNoteProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70);
    wrd(w, 40, p.reserved_40);
}

void read_props(GroupCursor& c, ZwmPartRefProperties& p) {
    rd(c, 70, p.version);
    rd(c, 40, p.reserved_40_0);
}
void write_props(RecordWriter& w, const ZwmPartRefProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wrd(w, 40, p.reserved_40_0);
}

void read_props(GroupCursor& c, ZwmSectionSymProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70_0);
    rds(c, 1, p.text_1_0);
    rd(c, 70, p.reserved_70_1);
    rdh(c, 340, p.handle_340_0);
    rd(c, 70, p.reserved_70_2);
    rd(c, 40, p.reserved_40);
    rd_list(c, 70, 1, p.list_1_a);
    rd_list(c, 70, 1, p.list_1_b);
}
void write_props(RecordWriter& w, const ZwmSectionSymProperties& p, const std::string& who) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70_0);
    wrs(w, 1, p.text_1_0);
    wr16(w, 70, p.reserved_70_1);
    wrp(w, 340, p.handle_340_0, who);
    wr16(w, 70, p.reserved_70_2);
    wrd(w, 40, p.reserved_40);
    wr_list(w, 70, 1, p.list_1_a);
    wr_list(w, 70, 1, p.list_1_b);
}

void read_props(GroupCursor& c, ZwmSurfSymProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70_0);
    rd(c, 40, p.reserved_40_0);
    rd(c, 70, p.reserved_70_1);
    rd_list(c, 70, 1, p.list_1_a);
    rd_list(c, 70, 1, p.list_1_b);
    rd(c, 70, p.reserved_70_2);
    rd(c, 40, p.reserved_40_1);
}
void write_props(RecordWriter& w, const ZwmSurfSymProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70_0);
    wrd(w, 40, p.reserved_40_0);
    wr16(w, 70, p.reserved_70_1);
    wr_list(w, 70, 1, p.list_1_a);
    wr_list(w, 70, 1, p.list_1_b);
    wr16(w, 70, p.reserved_70_2);
    wrd(w, 40, p.reserved_40_1);
}

void read_props(GroupCursor& c, ZwmTaperSlopeProperties& p) {
    rd(c, 70, p.version);
    rd(c, 70, p.reserved_70);
}
void write_props(RecordWriter& w, const ZwmTaperSlopeProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wr16(w, 70, p.reserved_70);
}

void read_props(GroupCursor& c, ZwmViewDirectionProperties& p) {
    rd(c, 70, p.version);
    rds(c, 1, p.text_1_0);
    rd(c, 40, p.reserved_40_0);
    rd(c, 70, p.reserved_70_0);
    rd_list(c, 70, 1, p.list_1_a);
    rd_list(c, 70, 1, p.list_1_b);
}
void write_props(RecordWriter& w, const ZwmViewDirectionProperties& p, const std::string&) {
    wr16(w, 70, p.version);
    wrs(w, 1, p.text_1_0);
    wrd(w, 40, p.reserved_40_0);
    wr16(w, 70, p.reserved_70_0);
    wr_list(w, 70, 1, p.list_1_a);
    wr_list(w, 70, 1, p.list_1_b);
}

void read_props(GroupCursor& c, ZwmWeldingProperties& p) {
    rd(c, 70, p.version);
    rdh(c, 340, p.handle_340_0);
    rd(c, 40, p.reserved_40_0);
    rd_list(c, 70, p.rows_a);
    rd_list(c, 70, p.rows_b);
    rd_list(c, 70, p.rows_c);
}
void write_props(RecordWriter& w, const ZwmWeldingProperties& p, const std::string& who) {
    wr16(w, 70, p.version);
    wrp(w, 340, p.handle_340_0, who);
    wrd(w, 40, p.reserved_40_0);
    wr_list(w, 70, p.rows_a);
    wr_list(w, 70, p.rows_b);
    wr_list(w, 70, p.rows_c);
}

// ---------------------------------------------------------------------------
// The style types' own subclasses
// ---------------------------------------------------------------------------

void read_body(GroupCursor& c, ZwmColorStdBody& b) {
    rd(c, 70, b.version);
    rd_list(c, 70, b.colors);
    zwm_read_tail(c, b.tail);
}
void write_body(RecordWriter& w, const ZwmColorStdBody& b, const std::string& who) {
    wr16(w, 70, b.version);
    wr_list(w, 70, b.colors);
    zwm_write_tail(w, b.tail, who);
}
void read_body(GroupCursor& c, ZwmDimStyleStdBody& b) {
    rd(c, 70, b.version);
    rd(c, 70, b.reserved_70_0);
    rd(c, 40, b.reserved_40);
    rd(c, 70, b.reserved_70_1);
    rd_list(c, 70, b.styles);
    zwm_read_tail(c, b.tail);
}
void write_body(RecordWriter& w, const ZwmDimStyleStdBody& b, const std::string& who) {
    wr16(w, 70, b.version);
    wr16(w, 70, b.reserved_70_0);
    wrd(w, 40, b.reserved_40);
    wr16(w, 70, b.reserved_70_1);
    wr_list(w, 70, b.styles);
    zwm_write_tail(w, b.tail, who);
}
void read_body(GroupCursor& c, ZwmLayerStdBody& b) {
    rd(c, 70, b.version);
    rd_list(c, 70, b.layers);
    zwm_read_tail(c, b.tail);
}
void write_body(RecordWriter& w, const ZwmLayerStdBody& b, const std::string& who) {
    wr16(w, 70, b.version);
    wr_list(w, 70, b.layers);
    zwm_write_tail(w, b.tail, who);
}
void read_body(GroupCursor& c, ZwmLineTypeStdBody& b) {
    rd(c, 70, b.version);
    rd_list(c, 70, b.linetypes);
    zwm_read_tail(c, b.tail);
}
void write_body(RecordWriter& w, const ZwmLineTypeStdBody& b, const std::string& who) {
    wr16(w, 70, b.version);
    wr_list(w, 70, b.linetypes);
    zwm_write_tail(w, b.tail, who);
}
void read_body(GroupCursor& c, ZwmTextStyleStdBody& b) {
    rd(c, 70, b.version);
    rd_list(c, 70, b.styles);
    zwm_read_tail(c, b.tail);
}
void write_body(RecordWriter& w, const ZwmTextStyleStdBody& b, const std::string& who) {
    wr16(w, 70, b.version);
    wr_list(w, 70, b.styles);
    zwm_write_tail(w, b.tail, who);
}

// ---------------------------------------------------------------------------
// Handles
// ---------------------------------------------------------------------------

void tail_refs(const ZwmStdTail& t, std::vector<TypedReference>& out) {
    for (const std::string& h : t.handle_340) ext_add_ref(out, h, RefKind::HardPointer);
}
void symbol_refs(const ZwmSymProperties& s, std::vector<TypedReference>& out) {
    ext_add_ref(out, s.handle_340_0, RefKind::HardPointer);
    for (const std::string& h : s.handle_340_1) ext_add_ref(out, h, RefKind::HardPointer);
}
template <typename P>
void props_refs(const P&, std::vector<TypedReference>&) {}
void props_refs(const ZwmBalloonProperties& p, std::vector<TypedReference>& out) { ext_add_ref(out, p.handle_340_0, RefKind::HardPointer); }
void props_refs(const ZwmDatumTargetProperties& p, std::vector<TypedReference>& out) {
    for (const std::string& h : p.handle_340) ext_add_ref(out, h, RefKind::HardPointer);
}
void props_refs(const ZwmDetailProperties& p, std::vector<TypedReference>& out) { ext_add_ref(out, p.handle_340_0, RefKind::HardPointer); }
void props_refs(const ZwmSectionSymProperties& p, std::vector<TypedReference>& out) { ext_add_ref(out, p.handle_340_0, RefKind::HardPointer); }
void props_refs(const ZwmWeldingProperties& p, std::vector<TypedReference>& out) { ext_add_ref(out, p.handle_340_0, RefKind::HardPointer); }

// ---------------------------------------------------------------------------
// The common parts of a codec
// ---------------------------------------------------------------------------

bool refuse_older(RecordWriter& w, const char* name, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail(std::string(name) + " in DXF R13 or R14 is not established: no file of the application has it (a record read from a file is kept as it was)");
        return true;
    }
    return false;
}

ClassSpec zwm_class(const char* cpp_name) { return ext_class(cpp_name, "ZWMCUSTCORE", 1); }

ListSpec whole(const char* subclass) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

template <typename D>
D& data_of(Object& o) { return *o.data.get<Boxed<D>>(); }
template <typename D>
const D& data_of(const Object& o) { return *o.data.get<Boxed<D>>(); }

// The symbol types: ZwmObject, ZwmStd, ZwmSymbolStd, the type's own, ZwmSymProperties, the type's properties
template <typename Tag>
bool read_symbol_type(const RecordView& view, TypedReadContext& ctx, Object& o) {
    typename Tag::Data d;
    {
        GroupCursor c(view, "ZwmObject");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmObject");
        size_t count = 0;
        zwm_read_object(c, d.header, count);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStd");
        zwm_read_std(c, d.standard);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmSymbolStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmSymbolStd");
        zwm_read_symbol_std(c, d.symbol_std);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, Tag::kStd);
        if (!c.valid()) return ext_missing(view, ctx, Tag::kStd);
        zwm_read_type_std(c, d.type_std);
        if (!ext_done(c, ctx, view)) return false;
    }
    if (!zwm_read_symbol_properties(view, ctx, d.symbol)) return false;
    {
        GroupCursor c(view, Tag::kProperties);
        if (!c.valid()) return ext_missing(view, ctx, Tag::kProperties);
        read_props(c, d.properties);
        if (!ext_done(c, ctx, view)) return false;
    }
    o.data = Boxed<typename Tag::Data>(std::move(d));
    return true;
}

template <typename Tag>
void write_symbol_type(RecordWriter& w, const Object& o) {
    if (refuse_older(w, Tag::kDxf, o)) return;
    const typename Tag::Data& d = data_of<typename Tag::Data>(o);
    const std::string who = std::string(Tag::kDxf) + " " + o.handle;
    w.object_head(Tag::kDxf, o);
    zwm_write_object(w, d.header, static_cast<size_t>(d.header.reserved_70_0));
    zwm_write_std(w, d.standard, who);
    zwm_write_symbol_std(w, d.symbol_std, who);
    zwm_write_type_std(w, Tag::kStd, d.type_std);
    zwm_write_symbol_properties(w, o, d.symbol, who);
    w.subclass(Tag::kProperties);
    write_props(w, d.properties, who);
    w.xdata(o.xdata);
}

template <typename Tag>
void symbol_type_references(const Object& o, std::vector<TypedReference>& out) {
    const typename Tag::Data& d = data_of<typename Tag::Data>(o);
    tail_refs(d.standard.tail, out);
    tail_refs(d.symbol_std.tail, out);
    symbol_refs(d.symbol, out);
    props_refs(d.properties, out);
}

// The style types that count their definitions in ZwmObject
template <typename Tag>
bool read_style_type(const RecordView& view, TypedReadContext& ctx, Object& o) {
    typename Tag::Data d;
    size_t count = 0;
    std::vector<ZwmAttributeEntry> entries;
    {
        GroupCursor c(view, "ZwmObject");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmObject");
        zwm_read_object(c, d.header, count);
        if (!ext_done(c, ctx, view)) return false;
        std::optional<GroupCursor> last;
        if (!zwm_read_attributes(view, ctx, c, count, false, entries, last)) return false;
        if (last && !ext_done(*last, ctx, view)) return false;
    }
    for (ZwmAttributeEntry& e : entries) d.attributes.push_back(std::move(e.definition));
    {
        GroupCursor c(view, "ZwmStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStd");
        zwm_read_std(c, d.standard);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, Tag::kBody);
        if (!c.valid()) return ext_missing(view, ctx, Tag::kBody);
        read_body(c, Tag::body(d));
        if (!ext_done(c, ctx, view)) return false;
    }
    o.data = Boxed<typename Tag::Data>(std::move(d));
    return true;
}

template <typename Tag>
void write_style_type(RecordWriter& w, const Object& o) {
    if (refuse_older(w, Tag::kDxf, o)) return;
    const typename Tag::Data& d = data_of<typename Tag::Data>(o);
    const std::string who = std::string(Tag::kDxf) + " " + o.handle;
    w.object_head(Tag::kDxf, o);
    zwm_write_object(w, d.header, d.attributes.size());
    for (const ZwmAttributeDefinition& a : d.attributes) zwm_write_definition(w, a);
    zwm_write_std(w, d.standard, who);
    w.subclass(Tag::kBody);
    write_body(w, Tag::body(d), who);
    w.xdata(o.xdata);
}

template <typename Tag>
void style_type_references(const Object& o, std::vector<TypedReference>& out) {
    const typename Tag::Data& d = data_of<typename Tag::Data>(o);
    tail_refs(d.standard.tail, out);
    tail_refs(Tag::body(d).tail, out);
}

#define ZWM_SYMBOL_TAG(Name, DXF)                                                                                  \
    struct Name##Tag {                                                                                             \
        using Data = Zwm##Name##StdData;                                                                           \
        static constexpr const char* kDxf = DXF;                                                                   \
        static constexpr const char* kStd = "Zwm" #Name "Std";                                                     \
        static constexpr const char* kProperties = "Zwm" #Name "Properties";                                       \
        static constexpr ObjectType kType = ObjectType::Zwm##Name##Std;                                            \
    }

ZWM_SYMBOL_TAG(Balloon, "ZWMBALLOONSTD");
ZWM_SYMBOL_TAG(BreakSym, "ZWMBREAKSYMSTD");
ZWM_SYMBOL_TAG(CenterHole, "ZWMCENTERHOLESTD");
ZWM_SYMBOL_TAG(Chamfer, "ZWMCHAMFERSTD");
ZWM_SYMBOL_TAG(CircleMark, "ZWMCIRCLEMARKSTD");
ZWM_SYMBOL_TAG(DatumId, "ZWMDATUMIDSTD");
ZWM_SYMBOL_TAG(DatumTarget, "ZWMDATUMTARGETSTD");
ZWM_SYMBOL_TAG(Detail, "ZWMDETAILSTD");
ZWM_SYMBOL_TAG(ElevSym, "ZWMELEVSYMSTD");
ZWM_SYMBOL_TAG(FCFrame, "ZWMFCFRAMESTD");
ZWM_SYMBOL_TAG(FeatureId, "ZWMFEATUREIDSTD");
ZWM_SYMBOL_TAG(HoleChart, "ZWMHOLECHARTSTD");
ZWM_SYMBOL_TAG(Note, "ZWMNOTESTD");
ZWM_SYMBOL_TAG(PartRef, "ZWMPARTREFSTD");
ZWM_SYMBOL_TAG(SectionSym, "ZWMSECTIONSYMSTD");
ZWM_SYMBOL_TAG(SurfSym, "ZWMSURFSYMSTD");
ZWM_SYMBOL_TAG(TaperSlope, "ZWMTAPERSLOPESTD");
ZWM_SYMBOL_TAG(ViewDirection, "ZWMVIEWDIRECTIONSTD");
ZWM_SYMBOL_TAG(Welding, "ZWMWELDINGSTD");

#define ZWM_STYLE_TAG(Name, DXF, BODY, MEMBER)                                                                     \
    struct Name##Tag {                                                                                             \
        using Data = Zwm##Name##StdData;                                                                           \
        static constexpr const char* kDxf = DXF;                                                                   \
        static constexpr const char* kBody = BODY;                                                                 \
        static constexpr ObjectType kType = ObjectType::Zwm##Name##Std;                                            \
        static auto& body(Data& d) { return d.MEMBER; }                                                            \
        static const auto& body(const Data& d) { return d.MEMBER; }                                                \
    }

ZWM_STYLE_TAG(Layer, "ZWMLAYERSTD", "ZwmLayerStd", layer);
ZWM_STYLE_TAG(TextStyle, "ZWMTEXTSTYLESTD", "ZwmTextStyleStd", text_style);
ZWM_STYLE_TAG(LineType, "ZWMLINETYPESTD", "ZwmLineStd", linetype);
ZWM_STYLE_TAG(DimStyle, "ZWMDIMSTYLESTD", "ZwmDimStyleStd", dim_style);
ZWM_STYLE_TAG(Color, "ZWMCOLORSTD", "ZwmColorStd", color);

template <typename Tag>
void add_symbol_type(TypedRegistry& registry, const char* cpp_name) {
    ObjectCodec c;
    c.dxf_name = Tag::kDxf;
    c.type = Tag::kType;
    c.read = read_symbol_type<Tag>;
    c.write = write_symbol_type<Tag>;
    c.references = symbol_type_references<Tag>;
    c.cls = zwm_class(cpp_name);
    c.versions = ext_object_matrix();
    c.refs = ext_pointer_refs(false);
    c.lists = {whole("ZwmSymProperties"), whole("ZWMATTRIBUTEDEF"), whole(Tag::kProperties)};
    c.step = 18;
    registry.add(std::move(c));
}

template <typename Tag>
void add_style_type(TypedRegistry& registry, const char* cpp_name) {
    ObjectCodec c;
    c.dxf_name = Tag::kDxf;
    c.type = Tag::kType;
    c.read = read_style_type<Tag>;
    c.write = write_style_type<Tag>;
    c.references = style_type_references<Tag>;
    c.cls = zwm_class(cpp_name);
    c.versions = ext_object_matrix();
    c.refs = ext_pointer_refs(false);
    c.lists = {whole("ZWMATTRIBUTEDEF"), whole(Tag::kBody)};
    c.step = 18;
    registry.add(std::move(c));
}

// ---------------------------------------------------------------------------
// ZWMSTANDARD, ZWMBOMSTYLESTD, ZWMTITLESTYLESTD, ZWMFRAMESTYLESTD
// ---------------------------------------------------------------------------

bool read_standard(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ZwmStandardData d;
    {
        GroupCursor c(view, "ZwmObject");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmObject");
        size_t count = 0;
        zwm_read_object(c, d.header, count);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStd");
        zwm_read_std(c, d.standard);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmStandard");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStandard");
        rd(c, 70, d.version);
        if (!ext_done(c, ctx, view)) return false;
    }
    if (!zwm_read_symbol_properties(view, ctx, d.symbol)) return false;
    {
        GroupCursor c(view, "ZwmStandardProperties");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStandardProperties");
        ZwmStandardProperties& p = d.properties;
        rd(c, 70, p.version);
        rd(c, 40, p.reserved_40_0);
        rdh(c, 340, p.handle_340);
        rd(c, 70, p.reserved_70_0);
        rd(c, 40, p.reserved_40_1);
        rd_list(c, 70, 360, p.owned);
        p.names.clear();
        rd_counted(c, 70, 2, [&] {
            ZwmNameValue v;
            rds(c, 1, v.text_1_0);
            rd(c, 70, v.reserved_70_0);
            if (c.ok()) p.names.push_back(v);
        });
        if (!ext_done(c, ctx, view)) return false;
    }
    o.data = Boxed<ZwmStandardData>(std::move(d));
    return true;
}

void write_standard(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "ZWMSTANDARD", o)) return;
    const ZwmStandardData& d = data_of<ZwmStandardData>(o);
    const std::string who = "ZWMSTANDARD " + o.handle;
    w.object_head("ZWMSTANDARD", o);
    zwm_write_object(w, d.header, static_cast<size_t>(d.header.reserved_70_0));
    zwm_write_std(w, d.standard, who);
    w.subclass("ZwmStandard");
    wr16(w, 70, d.version);
    zwm_write_symbol_properties(w, o, d.symbol, who);
    w.subclass("ZwmStandardProperties");
    const ZwmStandardProperties& p = d.properties;
    wr16(w, 70, p.version);
    wrd(w, 40, p.reserved_40_0);
    wrp(w, 340, p.handle_340, who);
    wr16(w, 70, p.reserved_70_0);
    wrd(w, 40, p.reserved_40_1);
    wr_members(w, 70, 360, p.owned, who);
    wr_count(w, 70, p.names.size());
    for (const ZwmNameValue& v : p.names) {
        wrs(w, 1, v.text_1_0);
        wr16(w, 70, v.reserved_70_0);
    }
    w.xdata(o.xdata);
}

void standard_references(const Object& o, std::vector<TypedReference>& out) {
    const ZwmStandardData& d = data_of<ZwmStandardData>(o);
    tail_refs(d.standard.tail, out);
    symbol_refs(d.symbol, out);
    for (const std::string& h : d.properties.handle_340) ext_add_ref(out, h, RefKind::HardPointer);
    for (const std::string& h : d.properties.owned) ext_add_ref(out, h, RefKind::HardOwner);
}

bool read_bom(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ZwmBomStyleStdData d;
    {
        GroupCursor c(view, "ZwmObject");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmObject");
        size_t count = 0;
        zwm_read_object(c, d.header, count);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStd");
        zwm_read_std(c, d.standard);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmBomStyleStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmBomStyleStd");
        zwm_read_type_std(c, d.type_std);
        if (!ext_done(c, ctx, view)) return false;
    }
    GroupCursor c(view, "ZwmBomProperties");
    if (!c.valid()) return ext_missing(view, ctx, "ZwmBomProperties");
    ZwmBomProperties& p = d.properties;
    rd(c, 70, p.version);
    rds(c, 1, p.text_1);
    rd(c, 70, p.reserved_70);
    int16_t count = 0;
    rd(c, 70, count);
    if (c.ok() && count < 0) c.fail("the attribute count is negative");
    if (!c.ok()) return ctx.fail(c.error());
    std::optional<GroupCursor> last;
    if (!zwm_read_attributes(view, ctx, c, static_cast<size_t>(count), false, p.attributes, last)) return false;
    if (!ext_done(c, ctx, view)) return false;
    if (last && !ext_done(*last, ctx, view)) return false;
    o.data = Boxed<ZwmBomStyleStdData>(std::move(d));
    return true;
}

void write_bom(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "ZWMBOMSTYLESTD", o)) return;
    const ZwmBomStyleStdData& d = data_of<ZwmBomStyleStdData>(o);
    const std::string who = "ZWMBOMSTYLESTD " + o.handle;
    w.object_head("ZWMBOMSTYLESTD", o);
    zwm_write_object(w, d.header, static_cast<size_t>(d.header.reserved_70_0));
    zwm_write_std(w, d.standard, who);
    zwm_write_type_std(w, "ZwmBomStyleStd", d.type_std);
    w.subclass("ZwmBomProperties");
    const ZwmBomProperties& p = d.properties;
    wr16(w, 70, p.version);
    wrs(w, 1, p.text_1);
    wr16(w, 70, p.reserved_70);
    wr_count(w, 70, p.attributes.size());
    zwm_write_attributes(w, p.attributes, false);
    w.xdata(o.xdata);
}

void bom_references(const Object& o, std::vector<TypedReference>& out) { tail_refs(data_of<ZwmBomStyleStdData>(o).standard.tail, out); }

bool read_title(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ZwmTitleStyleStdData d;
    {
        GroupCursor c(view, "ZwmObject");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmObject");
        size_t count = 0;
        zwm_read_object(c, d.header, count);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStd");
        zwm_read_std(c, d.standard);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmTitleStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmTitleStd");
        zwm_read_type_std(c, d.type_std);
        if (!ext_done(c, ctx, view)) return false;
    }
    GroupCursor c(view, "ZwmTitleProperties");
    if (!c.valid()) return ext_missing(view, ctx, "ZwmTitleProperties");
    ZwmTitleProperties& p = d.properties;
    rd(c, 70, p.version);
    rds(c, 1, p.text_1);
    rd(c, 70, p.reserved_70);
    int16_t count = 0;
    rd(c, 70, count);
    if (c.ok() && count < 0) c.fail("the attribute count is negative");
    if (!c.ok()) return ctx.fail(c.error());
    std::optional<GroupCursor> last;
    if (!zwm_read_attributes(view, ctx, c, static_cast<size_t>(count), false, p.attributes, last)) return false;
    // The tail of eight handles ends the last definition's groups (the subclass itself when there is none)
    GroupCursor& end = last ? *last : c;
    zwm_read_tail(end, p.tail);
    if (!ext_done(end, ctx, view)) return false;
    if (last && !ext_done(c, ctx, view)) return false;
    o.data = Boxed<ZwmTitleStyleStdData>(std::move(d));
    return true;
}

void write_title(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "ZWMTITLESTYLESTD", o)) return;
    const ZwmTitleStyleStdData& d = data_of<ZwmTitleStyleStdData>(o);
    const std::string who = "ZWMTITLESTYLESTD " + o.handle;
    w.object_head("ZWMTITLESTYLESTD", o);
    zwm_write_object(w, d.header, static_cast<size_t>(d.header.reserved_70_0));
    zwm_write_std(w, d.standard, who);
    zwm_write_type_std(w, "ZwmTitleStd", d.type_std);
    w.subclass("ZwmTitleProperties");
    const ZwmTitleProperties& p = d.properties;
    wr16(w, 70, p.version);
    wrs(w, 1, p.text_1);
    wr16(w, 70, p.reserved_70);
    wr_count(w, 70, p.attributes.size());
    zwm_write_attributes(w, p.attributes, false);
    zwm_write_tail(w, p.tail, who);
    w.xdata(o.xdata);
}

void title_references(const Object& o, std::vector<TypedReference>& out) {
    const ZwmTitleStyleStdData& d = data_of<ZwmTitleStyleStdData>(o);
    tail_refs(d.standard.tail, out);
    tail_refs(d.properties.tail, out);
}

bool read_frame(const RecordView& view, TypedReadContext& ctx, Object& o) {
    ZwmFrameStyleStdData d;
    {
        GroupCursor c(view, "ZwmObject");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmObject");
        size_t count = 0;
        zwm_read_object(c, d.header, count);
        if (!ext_done(c, ctx, view)) return false;
    }
    {
        GroupCursor c(view, "ZwmStd");
        if (!c.valid()) return ext_missing(view, ctx, "ZwmStd");
        zwm_read_std(c, d.standard);
        if (!ext_done(c, ctx, view)) return false;
    }
    GroupCursor c(view, "ZwmFrameStyleStd");
    if (!c.valid()) return ext_missing(view, ctx, "ZwmFrameStyleStd");
    rd(c, 70, d.style.version);
    int16_t count = 0;
    rd(c, 70, count);
    if (c.ok() && (count < 0 || view.subclass_count("ZwmFrameProperties") != static_cast<size_t>(count))) {
        c.fail("the frame count " + std::to_string(count) + " is not the " + std::to_string(view.subclass_count("ZwmFrameProperties")) +
               " ZwmFrameProperties subclass(es)");
    }
    if (!c.ok()) return ctx.fail(c.error());
    std::optional<GroupCursor> last;
    for (int16_t i = 0; i < count; ++i) {
        ZwmFrameEntry e;
        GroupCursor& before = last ? *last : c;
        rds(before, 1, e.name);
        if (!before.ok()) return ctx.fail(before.error());
        last.emplace(view, "ZwmFrameProperties", static_cast<size_t>(i));
        GroupCursor& f = *last;
        ZwmFrameProperties& p = e.properties;
        rd(f, 70, p.version);
        rds(f, 1, p.text_1_0);
        rd(f, 40, p.reserved_40);
        rd(f, 70, p.reserved_70_0);
        rds(f, 1, p.text_1);
        rd(f, 90, p.reserved_90_0);
        rd(f, 70, e.reserved_70_0);
        if (!f.ok()) return ctx.fail(f.error());
        d.style.frames.push_back(std::move(e));
    }
    if (!ext_done(c, ctx, view)) return false;
    if (last && !ext_done(*last, ctx, view)) return false;
    o.data = Boxed<ZwmFrameStyleStdData>(std::move(d));
    return true;
}

void write_frame(RecordWriter& w, const Object& o) {
    if (refuse_older(w, "ZWMFRAMESTYLESTD", o)) return;
    const ZwmFrameStyleStdData& d = data_of<ZwmFrameStyleStdData>(o);
    const std::string who = "ZWMFRAMESTYLESTD " + o.handle;
    w.object_head("ZWMFRAMESTYLESTD", o);
    zwm_write_object(w, d.header, static_cast<size_t>(d.header.reserved_70_0));
    zwm_write_std(w, d.standard, who);
    w.subclass("ZwmFrameStyleStd");
    wr16(w, 70, d.style.version);
    wr_count(w, 70, d.style.frames.size());
    for (const ZwmFrameEntry& e : d.style.frames) {
        wrs(w, 1, e.name);
        w.subclass("ZwmFrameProperties");
        const ZwmFrameProperties& p = e.properties;
        wr16(w, 70, p.version);
        wrs(w, 1, p.text_1_0);
        wrd(w, 40, p.reserved_40);
        wr16(w, 70, p.reserved_70_0);
        wrs(w, 1, p.text_1);
        wr32(w, 90, p.reserved_90_0);
        wr16(w, 70, e.reserved_70_0);
    }
    w.xdata(o.xdata);
}

void frame_references(const Object& o, std::vector<TypedReference>& out) {
    tail_refs(data_of<ZwmFrameStyleStdData>(o).standard.tail, out);
}

} // namespace

// The order of the application's own CLASSES (the file the records were seen in): ZWMSTANDARD first
void register_zwm_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "ZWMSTANDARD";
        c.type = ObjectType::ZwmStandard;
        c.read = read_standard;
        c.write = write_standard;
        c.references = standard_references;
        c.cls = zwm_class("ZwmStandard");
        c.versions = ext_object_matrix();
        c.refs = ext_pointer_refs(true);
        c.lists = {whole("ZwmSymProperties"), whole("ZwmStandardProperties")};
        c.step = 18;
        registry.add(std::move(c));
    }
    add_style_type<LayerTag>(registry, "ZwmLayerStd");
    add_style_type<TextStyleTag>(registry, "ZwmTextStyleStd");
    add_style_type<LineTypeTag>(registry, "ZwmLineTypeStd");
    add_style_type<DimStyleTag>(registry, "ZwmDimStyleStd");
    add_style_type<ColorTag>(registry, "ZwmColorStd");
    add_symbol_type<PartRefTag>(registry, "ZwmPartRefStd");
    add_symbol_type<BalloonTag>(registry, "ZwmBalloonStd");
    add_symbol_type<SurfSymTag>(registry, "ZwmSurfSymStd");
    add_symbol_type<NoteTag>(registry, "ZwmNoteStd");
    add_symbol_type<FCFrameTag>(registry, "ZwmFCFrameStd");
    add_symbol_type<DatumIdTag>(registry, "ZwmDatumIdStd");
    add_symbol_type<FeatureIdTag>(registry, "ZwmFeatureIdStd");
    add_symbol_type<DatumTargetTag>(registry, "ZwmDatumTargetStd");
    add_symbol_type<TaperSlopeTag>(registry, "ZwmTaperSlopeStd");
    add_symbol_type<CenterHoleTag>(registry, "ZwmCenterHoleStd");
    add_symbol_type<ChamferTag>(registry, "ZwmChamferStd");
    add_symbol_type<CircleMarkTag>(registry, "ZwmCircleMarkStd");
    add_symbol_type<WeldingTag>(registry, "ZwmWeldingStd");
    add_symbol_type<DetailTag>(registry, "ZwmDetailStd");
    add_symbol_type<SectionSymTag>(registry, "ZwmSectionSymStd");
    add_symbol_type<ViewDirectionTag>(registry, "ZwmViewDirectionStd");
    add_symbol_type<ElevSymTag>(registry, "ZwmElevSymStd");
    add_symbol_type<BreakSymTag>(registry, "ZwmBreakSymStd");
    add_symbol_type<HoleChartTag>(registry, "ZwmHoleChartStd");
    {
        ObjectCodec c;
        c.dxf_name = "ZWMBOMSTYLESTD";
        c.type = ObjectType::ZwmBomStyleStd;
        c.read = read_bom;
        c.write = write_bom;
        c.references = bom_references;
        c.cls = zwm_class("ZwmBomStyleStd");
        c.versions = ext_object_matrix();
        c.refs = ext_pointer_refs(false);
        c.lists = {whole("ZWMATTRIBUTEDEF"), whole("ZwmBomProperties")};
        c.step = 18;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ZWMTITLESTYLESTD";
        c.type = ObjectType::ZwmTitleStyleStd;
        c.read = read_title;
        c.write = write_title;
        c.references = title_references;
        c.cls = zwm_class("ZwmTitleStyleStd");
        c.versions = ext_object_matrix();
        c.refs = ext_pointer_refs(false);
        c.lists = {whole("ZWMATTRIBUTEDEF"), whole("ZwmTitleProperties")};
        c.step = 18;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ZWMFRAMESTYLESTD";
        c.type = ObjectType::ZwmFrameStyleStd;
        c.read = read_frame;
        c.write = write_frame;
        c.references = frame_references;
        c.cls = zwm_class("ZwmFrameStyleStd");
        c.versions = ext_object_matrix();
        c.refs = ext_pointer_refs(false);
        c.lists = {whole("ZwmFrameStyleStd"), whole("ZwmFrameProperties")};
        c.step = 18;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
