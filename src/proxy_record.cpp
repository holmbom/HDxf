#include "proxy_record.hpp"
#include "dxf_writer.hpp"
#include "handles.hpp"
#include "group_io.hpp"
#include "proxy_record_graphics.hpp"
#include "text_layout.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

// ACAD_PROXY_ENTITY and ACAD_PROXY_OBJECT (phase 9, step 9.3; plan 2026-10-27-fas-9-typer.md, old plan
// "ACAD_PROXY_ENTITY och ACAD_PROXY_OBJECT" and appendix B.2).
//
// A proxy stands for a custom entity or object whose application is not loaded. Its class data is the
// application's own stream — opaque, belonging to the application — described by lengths in bits, the ids of
// the objects it refers to and the class's place in CLASSES. HDxf types exactly that header and keeps the
// bytes as they are; it never creates a representation of an application's class.
//
// The forms (B100: AutoCAD's own files decide; the DXF reference where they show nothing):
//
//   AutoCAD 2025 saved the three ACAD_PROXY_OBJECT of LibreDWG's gh209_1 (AutoCAD 2010, test/data) as 2000, 2004,
//   2007, 2010, 2013 and 2018, ASCII and binary (doc/facit/fas9_ole_proxy.scr): after the subclass marker
//     2000, 2004   90 91 95 70 93 310 refs 94              the graphics are inside the data (93)
//     2007         90 91 95 70 96 [311] 93 [310] refs 94   96 and 93 are lengths in bits
//     2010, 2013   90 91 95 70 162 [311] 161 [310] refs 94
//     2018         90 91 71 97 70 162 [311] 161 [310] refs 94   95 is split: 71 the drawing's version, 97 its
//                                                              maintenance release
//   2000 also has the XDATA ACAD / AcRTDxfName / <the DXF name of the class> (2004 and later do not).
//   R13 and R14 (the R14 reference's ACAD_PROXY_ENTITY page; the proxies of other producers' files):
//     90 91 [92 310] 93 310 refs 94, no 95 and 70, graphics in bytes (92)
//   The R14 form is the reference's, not an AutoCAD file's (AutoCAD 2025 cannot save R13 or R14).
//   A file written by another producer may leave out 94 when there are no references: read as it is.
//   The proxy entity's own graphics (AcDbEntity 92/160 and 310) are the entity's, apart from these.
//
// The class id 91 is 500 plus the position of the application's CLASS record. A file read keeps its CLASSES as
// they were (the position cannot move); a drawing built in memory gets its classes in the order the writer
// makes them, and 91 is written from the class's name (ProxyClassData::application_class) at the position it has.
// The pointers (330 soft pointer, 340 hard pointer, 350 soft owner, 360 hard owner) are the object ids the data
// refers to by place: a target that is gone becomes 0 in place (AutoCAD's own null id), never a removed member,
// and the writer says so in warnings().
//
// A proxy of DXF data (70 = 1; step 9.S, Codex's review of 9.3/9.18): AutoCAD 2025 saved the third-party drawings of F9.24 and
// F9.25 (doc/facit/acad_<version>_dxf_ext_*.dxf, doc/facit/fas9_extensions.scr) with the class's own data as DXF groups after
// the proxy's subclass - no 93/161 stream, no id list, no 94:
//     2000, 2004            90 91 95 70 [92 <bytes> 310]                then the class's subclasses
//     2010, 2013            90 91 95 70 [160 <bytes> 310]               (AcDbPoint and TcPointEntity of TurboCAD's point;
//     2018                  90 91 71 97 70 [160 <bytes> 310]             TcOptionalData, AcMapDM..., Zwm... of the objects)
//   the graphics in bytes (92 before 2010, 160 from: AcDbEntity's codes), the same in every file as the entity's own; an object
//   has none. In the version the object was made in (95's low word: 25 2004, 27 2007, 29 2010, 33 2018) AutoCAD writes the class's
//   own record instead (TCPOINTENTITY and TCOPTIONALDATA in acad_2007_dxf_ext_tc_echocal_blockdiag, the ZWM records in 2010, Map's
//   in 2004 and 2018). The class's subclasses are read and written through the codec HDxf has for the class (ProxyEntityData::
//   class_data, ProxyObjectData::class_data): a proxy whose class has none, whose class data that codec cannot hold whole (a group
//   it does not know), or whose form is not one of these stays untyped, preserved, with a warning - never typed with a part only
//   its preserved record holds. 2007's proxy of DXF data is not in AutoCAD's files (2007 was the version the TurboCAD records were
//   made in): written in 2000's form with 92 (AcDbEntity's code in 2007), ej testat.

namespace hdxf {
namespace internal {

namespace {

bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == ' ') ++b;
    while (e > b && s[e - 1] == ' ') --e;
    return s.substr(b, e - b);
}

constexpr const char* kRoundTripApp = "ACAD";
constexpr const char* kRoundTripName = "AcRTDxfName";

// 2000's XDATA ACAD / AcRTDxfName / <DXF name>: the class a proxy of 2000 names
std::string round_trip_name(const std::vector<XdataList>& xdata) {
    for (const XdataList& x : xdata) {
        if (x.app_name != kRoundTripApp) continue;
        for (size_t i = 0; i + 1 < x.items.size(); ++i) {
            if (x.items[i].code == 1000 && x.items[i].value == kRoundTripName && x.items[i + 1].code == 1000) {
                return x.items[i + 1].value;
            }
        }
    }
    return {};
}

bool has_round_trip_name(const std::vector<XdataList>& xdata) { return !round_trip_name(xdata).empty(); }

// The XDATA a record of version 2000 is written with: the list ACAD gets AcRTDxfName first when it has none
std::vector<XdataList> xdata_with_class_name(const std::vector<XdataList>& xdata, const std::string& class_name) {
    if (class_name.empty() || has_round_trip_name(xdata)) return xdata;
    std::vector<XdataList> out = xdata;
    XdataList list;
    list.app_name = kRoundTripApp;
    list.items.push_back({1000, kRoundTripName});
    list.items.push_back({1000, class_name});
    auto acad = std::find_if(out.begin(), out.end(), [](const XdataList& x) { return x.app_name == kRoundTripApp; });
    if (acad == out.end()) out.push_back(std::move(list));
    else acad->items.insert(acad->items.begin(), list.items.begin(), list.items.end());
    return out;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// Bits → bytes
size_t bytes_for(uint64_t bits) { return static_cast<size_t>((bits + 7) / 8); }

// The class data of a proxy: the groups of the subclass (AcDbProxyEntity or AcDbProxyObject, or the
// AcDbZombie… name R13 files of other producers use), in the order AutoCAD writes them and in the order of the
// reference — the reader takes each group by its code, so both are read. `bytes` is where a stream of 310/311
// groups goes: the graphics after 92/96/162, the data after 93/161
bool read_proxy_class(const RecordView& view, TypedReadContext& context, const char* proxy_name, const char* zombie_name,
                      ProxyClassData& out, const Subclass*& found, bool& id_list) {
    bool zombie = false;
    const Subclass* sub = view.find_subclass(proxy_name);
    if (!sub) {
        sub = view.find_subclass(zombie_name);
        zombie = sub != nullptr;
    }
    if (!sub) return context.fail(std::string(view.type_name()) + " has no " + proxy_name + " subclass");
    found = sub;
    id_list = false;
    GroupCursor c(view, sub);
    out = ProxyClassData();
    out.zombie_marker = zombie;

    c.require_integer(90, out.class_id);
    c.require_integer(91, out.application_class_id);
    if (!c.ok()) return context.fail(c.error());
    // 91 is 500 + the position of the class in CLASSES: below 500 it names no class
    if (out.application_class_id < 500) {
        return context.fail(std::string(view.type_name()) + " " + view.head().handle + ": the class id (91) is " +
                            std::to_string(out.application_class_id) + ", not 500 or more");
    }

    bool graphics_seen = false, data_seen = false, ended = false;
    ProxyDrawingFormat format;
    bool has_format = false, has_71 = false, has_97 = false;
    while (!c.at_end() && c.ok() && !ended) {
        const int code = c.code();
        switch (code) {
            case 95: {
                uint32_t v = 0;
                if (c.integer(95, v)) {
                    format.dwg_version = static_cast<uint16_t>(v & 0xFFFF);
                    format.maintenance_version = static_cast<uint16_t>(v >> 16);
                    has_format = true;
                }
                break;
            }
            case 71: {
                uint16_t v = 0;
                if (c.integer(71, v)) { format.dwg_version = v; has_71 = true; has_format = true; }
                break;
            }
            case 97: {
                uint16_t v = 0;
                if (c.integer(97, v)) { format.maintenance_version = v; has_97 = true; has_format = true; }
                break;
            }
            case 70:
                c.integer(70, out.original_format);
                break;
            case 92:
            case 160:
            case 96:
            case 162: {
                // The graphics: 92 and 160 count bytes (R14's and the reference's form, and the proxy of DXF data:
                // AutoCAD's TurboCAD point, 92 before 2010 and 160 from), 96 and 162 bits; their bytes follow in 310
                // (92, 160) or 311
                if (graphics_seen) { c.fail("the proxy has its graphics twice"); break; }
                graphics_seen = true;
                const bool in_bytes = code == 92 || code == 160;
                uint64_t count = 0;
                int64_t raw = 0;
                if (!c.integer(code, raw)) break;
                if (raw < 0) { c.fail("the graphics' length (" + std::to_string(code) + ") is negative"); break; }
                count = static_cast<uint64_t>(raw);
                if (count > 0xFFFFFFFFull) { c.fail("the graphics' length does not fit 32 bits"); break; }
                const uint64_t bits = in_bytes ? count * 8 : count;
                if (bits > 0xFFFFFFFFull) { c.fail("the graphics' length does not fit 32 bits"); break; }
                c.bytes(in_bytes ? 310 : 311, out.graphics);
                if (!c.ok()) break;
                if (out.graphics.size() != bytes_for(bits)) {
                    c.fail("the graphics have " + std::to_string(out.graphics.size()) + " bytes, " + std::to_string(code) +
                           " says " + std::to_string(count) + (in_bytes ? " bytes" : " bits"));
                    break;
                }
                out.graphics_bits = static_cast<uint32_t>(bits);
                break;
            }
            case 93:
            case 161: {
                if (data_seen) { c.fail("the proxy has its data twice"); break; }
                data_seen = true;
                int64_t raw = 0;
                if (!c.integer(code, raw)) break;
                if (raw < 0 || static_cast<uint64_t>(raw) > 0xFFFFFFFFull) {
                    c.fail("the data's length (" + std::to_string(code) + ") is " + std::to_string(raw) + " bits");
                    break;
                }
                c.bytes(310, out.data);
                if (!c.ok()) break;
                if (out.data.size() != bytes_for(static_cast<uint64_t>(raw))) {
                    c.fail("the data have " + std::to_string(out.data.size()) + " bytes, " + std::to_string(code) +
                           " says " + std::to_string(raw) + " bits");
                    break;
                }
                out.data_bits = static_cast<uint32_t>(raw);
                break;
            }
            case 330:
            case 340:
            case 350:
            case 360: {
                ProxyReference r;
                r.kind = static_cast<ProxyPointerKind>(code);
                c.handle(code, r.handle);
                out.references.push_back(std::move(r));
                break;
            }
            case 94: {
                int32_t v = 0;
                if (c.integer(94, v) && v != 0) c.fail("the object id list's end (94) is " + std::to_string(v) + ", not 0");
                ended = true;
                id_list = true;
                break;
            }
            default:
                // A group none of the forms has (a later subversion?): the typed values could not hold it, so the record
                // stays untyped and is written as it was read (a change of the typed part would lose it)
                c.fail("a group " + std::to_string(code) + " that the proxy's forms do not have");
                break;
        }
    }
    if (!c.ok()) {
        return context.fail(std::string(view.type_name()) + " " + view.head().handle + ": " + c.error());
    }
    if (ended && !c.at_end()) {
        return context.fail(std::string(view.type_name()) + " " + view.head().handle + ": " + std::to_string(c.remaining()) +
                            " group(s) after the object id list's end (94), which the proxy's forms do not have");
    }
    // (71 without 97, or 97 without 71, is read as it stands: the other half is 0)
    (void)has_71;
    (void)has_97;
    if (has_format) out.drawing_format = format;
    return true;
}

// The class named by 2000's XDATA, when the record has no more: the name alone (CLASSES gives the rest)
void note_round_trip_name(const std::vector<XdataList>& xdata, ProxyClassData& p) {
    if (p.application_class.dxf_name.empty()) p.application_class.dxf_name = round_trip_name(xdata);
}

// The DXF name of the class a proxy stands for when it is read: the CLASS record its 91 names, else 2000's XDATA name
std::string class_name_when_read(const ProxyClassData& p, const TypedReadContext& context, const std::vector<XdataList>& xdata) {
    const int64_t index = static_cast<int64_t>(p.application_class_id) - 500;
    if (context.class_names && index >= 0 && static_cast<uint64_t>(index) < context.class_names->size()) {
        return (*context.class_names)[static_cast<size_t>(index)];
    }
    return round_trip_name(xdata);
}

// What a proxy's form says of its class data: the subclasses after the proxy's are the class's own (70 = 1, no stream, no id
// list), or there are none (a DWG stream). Anything else is no form AutoCAD writes: false with the reason
bool class_data_form(const RecordView& view, TypedReadContext& context, const Subclass* proxy, const ProxyClassData& p,
                     bool id_list, bool& dxf_data) {
    const std::vector<Subclass>& subclasses = view.subclasses();
    const size_t at = static_cast<size_t>(proxy - subclasses.data());
    const bool after = at + 1 < subclasses.size();
    const std::string what = std::string(view.type_name()) + " " + view.head().handle;
    dxf_data = false;
    if (p.original_format == 1 && after) {
        if (!p.data.empty() || p.data_bits != 0 || !p.references.empty() || id_list) {
            return context.fail(what + ": a proxy of DXF data (70 = 1) with a DWG data stream or an id list is not a form AutoCAD writes");
        }
        if (context.version < DxfVersion::R2000) {
            return context.fail(what + ": a proxy of DXF data (70 = 1) in DXF R13 or R14 is not a form AutoCAD's files show");
        }
        dxf_data = true;
        return true;
    }
    if (after) {
        return context.fail(what + ": the subclass " + subclasses[at + 1].name + " after " + proxy->name + " in a proxy of " +
                            (p.original_format == 1 ? "DXF" : "DWG") + " data (70 = " + std::to_string(p.original_format) +
                            ") is not a form AutoCAD writes");
    }
    if (p.original_format == 1 && p.data.empty() && p.data_bits == 0 && !id_list && context.version >= DxfVersion::R2000) {
        return context.fail(what + ": a proxy of DXF data (70 = 1) without its class's subclasses is not a form AutoCAD writes");
    }
    return true;
}

// The class's subclasses read by the codec HDxf has for the class. The codec reads the proxy's record (its subclasses by name);
// a warning it gives means a group it keeps only in the preserved record: then the proxy stays untyped
template <typename Codec, typename Record>
bool read_class_through(const Codec* codec, const std::string& class_name, const RecordView& view, TypedReadContext& context,
                        Record& made) {
    const std::string what = std::string(view.type_name()) + " " + view.head().handle;
    if (!codec || !codec->read) {
        return context.fail(what + ": its class " + (class_name.empty() ? std::string("(not named)") : class_name) +
                            " is not a type HDxf has, so its class data (70 = 1) cannot be typed");
    }
    TypedReadContext local;
    local.version = context.version;
    local.binary = context.binary;
    local.class_names = context.class_names;
    std::vector<std::string> notes;
    local.warnings = &notes;
    if (!codec->read(view, local, made)) {
        return context.fail(what + ": its class " + class_name + "'s data: " + local.error);
    }
    if (!notes.empty()) {
        return context.fail(what + ": its class " + class_name + "'s data is not held whole by the typed values: " + notes.front());
    }
    return true;
}

bool is_proxy_name(const std::string& name) {
    return iequals(name, "ACAD_PROXY_ENTITY") || iequals(name, "ACAD_PROXY_OBJECT");
}

bool read_proxy_object(const RecordView& view, TypedReadContext& context, Object& o) {
    ProxyObjectData d;
    const Subclass* sub = nullptr;
    bool id_list = false, dxf_data = false;
    if (!read_proxy_class(view, context, "AcDbProxyObject", "AcDbZombieObject", d.proxy, sub, id_list)) return false;
    if (!class_data_form(view, context, sub, d.proxy, id_list, dxf_data)) return false;
    if (dxf_data) {
        const std::string name = class_name_when_read(d.proxy, context, o.xdata);
        const ObjectCodec* codec = is_proxy_name(name) ? nullptr : TypedRegistry::instance().find_object(name);
        Object made;
        made.type = codec ? codec->type : ObjectType::Unknown;
        made.handle = o.handle;
        made.owner = o.owner;
        made.reactors = o.reactors;
        made.xdictionary = o.xdictionary;
        made.xdata = o.xdata;
        if (!read_class_through(codec, name, view, context, made)) return false;
        d.class_data = ProxyObjectClass{codec->type, std::move(made.data)};
    }
    note_round_trip_name(o.xdata, d.proxy);
    o.data = std::move(d);
    return true;
}

bool read_proxy_entity(const RecordView& view, TypedReadContext& context, Entity& e) {
    ProxyEntityData d;
    const Subclass* sub = nullptr;
    bool id_list = false, dxf_data = false;
    if (!read_proxy_class(view, context, "AcDbProxyEntity", "AcDbZombieEntity", d.proxy, sub, id_list)) return false;
    if (!class_data_form(view, context, sub, d.proxy, id_list, dxf_data)) return false;
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &d.entity_graphics, error)) return context.fail(error);
    if (dxf_data) {
        const std::string name = class_name_when_read(d.proxy, context, e.props.xdata);
        const EntityCodec* codec = is_proxy_name(name) ? nullptr : TypedRegistry::instance().find_entity(name);
        Entity made;
        made.type = codec ? codec->type : EntityType::Unknown;
        made.props = e.props;
        if (!read_class_through(codec, name, view, context, made)) return false;
        // The class's own subclass may hold common values (AcDbPoint's thickness and normal are the entity's)
        e.props = made.props;
        d.class_data = ProxyEntityClass{codec->type, std::move(made.data)};
    }
    note_round_trip_name(e.props.xdata, d.proxy);
    e.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

const char* subclass_name(const ProxyClassData& p, bool entity) {
    if (entity) return p.zombie_marker ? "AcDbZombieEntity" : "AcDbProxyEntity";
    return p.zombie_marker ? "AcDbZombieObject" : "AcDbProxyObject";
}

// The class's id: its place in the CLASSES section that is written, else the id as it was read or set
int32_t class_id_to_write(RecordWriter& w, const ProxyClassData& p) {
    if (!p.application_class.dxf_name.empty()) {
        int id = w.class_id(p.application_class.dxf_name);
        if (id > 0) return id;
    }
    return p.application_class_id;
}

bool lengths_agree(RecordWriter& w, const std::string& what, const std::vector<uint8_t>& bytes, uint32_t bits) {
    if (bytes.size() == bytes_for(bits)) return true;
    w.fail(what + ": " + std::to_string(bytes.size()) + " bytes do not hold " + std::to_string(bits) + " bits");
    return false;
}

void write_references(RecordWriter& w, const ProxyClassData& p, const std::string& what) {
    for (const ProxyReference& r : p.references) {
        const int code = static_cast<int>(r.kind);
        if (r.handle.empty()) {
            w.text(code, "0");
        } else if (r.handle != "0" && !w.live(r.handle)) {
            // The target is gone: AutoCAD's null id in its place (the data refers to the ids by place)
            w.warn(what + ": " + std::to_string(code) + " " + r.handle + " points to a record that is gone and is set to 0");
            w.text(code, "0");
        } else {
            w.text(code, r.handle);
        }
    }
    w.integer(94, 0);
}

// The class data in the form the version has (see the top of the file). `dxf_data`: the proxy has its class's data typed
// (70 = 1): the graphics in bytes and no stream, no id list; the class's subclasses follow (write_class_subclasses)
void write_proxy_class(RecordWriter& w, const ProxyClassData& p, bool entity, const std::string& what, bool dxf_data = false) {
    const DxfVersion v = w.version();
    w.subclass(subclass_name(p, entity));
    w.integer(90, p.class_id);
    w.integer(91, class_id_to_write(w, p));
    if (dxf_data) {
        // AutoCAD's proxy of DXF data: 95 (71 and 97 in 2018), 70 = 1, the graphics as AcDbEntity counts them (92 before 2010,
        // 160 from) and nothing of a DWG stream
        if (p.original_format != 1) {
            w.fail(what + ": the class's data typed (class_data) is the proxy of DXF data, whose 70 is 1, not " +
                   std::to_string(p.original_format));
            return;
        }
        if (!p.data.empty() || p.data_bits != 0 || !p.references.empty()) {
            w.fail(what + ": a proxy of DXF data (70 = 1) has its class's subclasses and no DWG data stream or id list (93/161, "
                          "330 … 94)");
            return;
        }
        const ProxyDrawingFormat format = p.drawing_format.value_or(ProxyDrawingFormat());
        if (v >= DxfVersion::R2018) {
            w.integer(71, format.dwg_version);
            w.integer(97, format.maintenance_version);
        } else {
            w.integer(95, static_cast<int64_t>(format.combined()));
        }
        w.integer(70, p.original_format);
        if (!p.graphics.empty()) {
            if (p.graphics_bits != p.graphics.size() * 8) {
                w.fail(what + ": the graphics of a proxy of DXF data are counted in bytes (92/160): " +
                       std::to_string(p.graphics_bits) + " bits are not their " + std::to_string(p.graphics.size()) + " bytes");
                return;
            }
            if (!entity) {
                w.fail(what + ": graphics of a proxy object of DXF data are in none of AutoCAD's files");
                return;
            }
            w.integer(v >= DxfVersion::R2010 ? 160 : 92, static_cast<int64_t>(p.graphics.size()));
            w.bytes(310, p.graphics);
        }
        return;
    }
    if (w.older()) {
        // R13, R14: the reference's order; the graphics in bytes
        if (!p.graphics.empty()) {
            w.integer(92, static_cast<int64_t>(p.graphics.size()));
            w.bytes(310, p.graphics);
        }
        if (lengths_agree(w, what + " data", p.data, p.data_bits)) {
            w.integer(93, p.data_bits);
            w.bytes(310, p.data);
        }
        write_references(w, p, what);
        return;
    }
    const ProxyDrawingFormat format = p.drawing_format.value_or(ProxyDrawingFormat());
    if (v >= DxfVersion::R2018) {
        w.integer(71, format.dwg_version);
        w.integer(97, format.maintenance_version);
    } else {
        w.integer(95, static_cast<int64_t>(format.combined()));
    }
    w.integer(70, p.original_format);
    const bool data_ok = lengths_agree(w, what + " data", p.data, p.data_bits);
    if (v <= DxfVersion::R2004) {
        // 2000 and 2004: graphics apart from the data are counted in bytes (92) with their bytes in 310, as the R14
        // reference has them: AutoCAD 2025 writes a proxy entity of TurboCAD's TCPOINTENTITY so, 92 and its bytes
        // straight after 70 and the class's own subclasses (AcDbPoint ...) after them (acad_2000_dxf_ext_tc_echocal_blockdiag)
        if (!p.graphics.empty()) {
            if (entity) {
                w.integer(92, static_cast<int64_t>(p.graphics.size()));
                w.bytes(310, p.graphics);
            } else {
                // No AutoCAD file shows it for a proxy object
                w.fail(what + ": graphics apart from the object data cannot be written as DXF 2000 or 2004 "
                              "(AutoCAD keeps them inside the data, 93)");
            }
        }
        if (data_ok) {
            w.integer(93, p.data_bits);
            w.bytes(310, p.data);
        }
    } else {
        const bool graphics_ok = lengths_agree(w, what + " graphics", p.graphics, p.graphics_bits);
        w.integer(v == DxfVersion::R2007 ? 96 : 162, graphics_ok ? p.graphics_bits : 0);
        if (graphics_ok) w.bytes(311, p.graphics);
        if (data_ok) {
            w.integer(v == DxfVersion::R2007 ? 93 : 161, p.data_bits);
            w.bytes(310, p.data);
        }
    }
    write_references(w, p, what);
}

// A record built for a version whose form of the proxy is not established cannot be made (no AutoCAD file shows
// it); a record read from a file is written as it was
bool form_established(RecordWriter& w, const std::string& what, bool has_raw) {
    if (has_raw || w.version() != DxfVersion::R13) return true;
    w.fail(what + " cannot be built for DXF R13: the form AutoCAD's R13 writes (ACAD_ZOMBIE_ENTITY) is not established");
    return false;
}

// AcDbDwgVersion of a DXF version (the low word of 95, 71 in 2018): AutoCAD's files have 21 (R14), 25 (2004), 27 (2007),
// 29 (2010) and 33 (2018) there; 23 and 31 are 2000's and 2013's in the same series
int dwg_version_of(DxfVersion v) {
    switch (v) {
        case DxfVersion::R13:   return 19;
        case DxfVersion::R14:   return 21;
        case DxfVersion::R2000: return 23;
        case DxfVersion::R2004: return 25;
        case DxfVersion::R2007: return 27;
        case DxfVersion::R2010: return 29;
        case DxfVersion::R2013: return 31;
        case DxfVersion::R2018: return 33;
        default:                return -1;
    }
}

// A proxy of DXF data built in memory and written in the version its object was made in: AutoCAD writes the class's own
// record there (see the top of the file). A proxy read from a file keeps its record (it was a proxy in that version)
bool written_as_its_class(RecordWriter& w, const ProxyClassData& p, bool has_class_data, bool has_raw) {
    return has_class_data && !has_raw && !w.older() && p.original_format == 1 && p.drawing_format &&
           p.drawing_format->dwg_version == dwg_version_of(w.version());
}

// The class's subclasses as the class's codec writes them, after the proxy's subclass: the codec writes the class's whole
// record into a capture, and what comes after its head (AcDbEntity for an entity) up to the XDATA is written here
void write_class_subclasses(RecordWriter& w, const std::vector<CapturedGroup>& record, bool entity) {
    size_t at = 0;
    int markers = 0;
    for (; at < record.size(); ++at) {
        if (record[at].code == 100 && ++markers == (entity ? 2 : 1)) break;
    }
    for (; at < record.size() && record[at].code != 1001; ++at) w.captured(record[at]);
}

const ObjectCodec* class_codec(const ProxyObjectClass& c) { return TypedRegistry::instance().find_object(c.type); }
const EntityCodec* class_codec(const ProxyEntityClass& c) { return TypedRegistry::instance().find_entity(c.type); }

// The class's record (the head and data of the proxy, the class's type and data)
Object class_record(const Object& o, const ProxyObjectClass& c, bool with_xdata) {
    Object made;
    made.type = c.type;
    made.handle = o.handle;
    made.owner = o.owner;
    made.reactors = o.reactors;
    made.xdictionary = o.xdictionary;
    if (with_xdata) made.xdata = o.xdata;
    made.data = c.data;
    return made;
}

Entity class_record(const Entity& e, const ProxyEntityClass& c, bool with_xdata) {
    Entity made;
    made.type = c.type;
    made.props = e.props;
    if (!with_xdata) made.props.xdata.clear();
    made.data = c.data;
    return made;
}

void write_proxy_object(RecordWriter& w, const Object& o) {
    const ProxyObjectData& d = o.as_proxy_object();
    const std::string what = "ACAD_PROXY_OBJECT " + o.handle;
    if (!form_established(w, "ACAD_PROXY_OBJECT", !o.raw.empty())) return;
    const ObjectCodec* codec = d.class_data ? class_codec(*d.class_data) : nullptr;
    if (d.class_data && (!codec || !codec->write)) {
        w.fail(what + ": its class data is of a type HDxf has no writer for");
        return;
    }
    if (d.class_data && w.older()) {
        w.fail(what + ": a proxy of DXF data (70 = 1) in DXF R13 or R14 is not established (no file shows it)");
        return;
    }
    if (written_as_its_class(w, d.proxy, static_cast<bool>(d.class_data), !o.raw.empty())) {
        codec->write(w, class_record(o, *d.class_data, true));
        return;
    }
    w.object_head("ACAD_PROXY_OBJECT", o);
    write_proxy_class(w, d.proxy, false, what, static_cast<bool>(d.class_data));
    if (d.class_data) {
        std::vector<CapturedGroup> record;
        std::vector<CapturedGroup>* before = w.begin_capture(record);
        codec->write(w, class_record(o, *d.class_data, false));
        w.end_capture(before);
        write_class_subclasses(w, record, false);
    }
    w.xdata(w.version() == DxfVersion::R2000 ? xdata_with_class_name(o.xdata, d.proxy.application_class.dxf_name)
                                             : o.xdata);
}

void write_proxy_entity(RecordWriter& w, const Entity& e, const std::string& owner) {
    const ProxyEntityData& d = e.as_proxy_entity();
    const std::string what = "ACAD_PROXY_ENTITY " + e.props.handle;
    if (!form_established(w, "ACAD_PROXY_ENTITY", !e.raw.empty())) return;
    const EntityCodec* codec = d.class_data ? class_codec(*d.class_data) : nullptr;
    if (d.class_data && (!codec || !codec->write)) {
        w.fail(what + ": its class data is of a type HDxf has no writer for");
        return;
    }
    if (d.class_data && w.older()) {
        w.fail(what + ": a proxy of DXF data (70 = 1) in DXF R13 or R14 is not established (no file shows it)");
        return;
    }
    if (written_as_its_class(w, d.proxy, static_cast<bool>(d.class_data), !e.raw.empty())) {
        codec->write(w, class_record(e, *d.class_data, true), owner);
        return;
    }
    const bool graphics = !d.entity_graphics.empty() && !w.older() && w.proxy_graphics_enabled();
    w.entity_head("ACAD_PROXY_ENTITY", e, owner, graphics ? &d.entity_graphics : nullptr);
    write_proxy_class(w, d.proxy, true, what, static_cast<bool>(d.class_data));
    if (d.class_data) {
        std::vector<CapturedGroup> record;
        std::vector<CapturedGroup>* before = w.begin_capture(record);
        codec->write(w, class_record(e, *d.class_data, false), owner);
        w.end_capture(before);
        write_class_subclasses(w, record, true);
    }
    w.xdata(w.version() == DxfVersion::R2000 ? xdata_with_class_name(e.props.xdata, d.proxy.application_class.dxf_name)
                                             : e.props.xdata);
}

// ---------------------------------------------------------------------------
// Relations, lists, extents
// ---------------------------------------------------------------------------

void add_references(const ProxyClassData& p, std::vector<TypedReference>& out) {
    for (const ProxyReference& r : p.references) {
        if (r.handle.empty() || r.handle == "0") continue;
        TypedReference t;
        t.handle = r.handle;
        switch (r.kind) {
            case ProxyPointerKind::SoftPointer: t.kind = RefKind::SoftPointer; break;
            case ProxyPointerKind::HardPointer: t.kind = RefKind::HardPointer; break;
            case ProxyPointerKind::SoftOwner:   t.kind = RefKind::SoftOwner; break;
            case ProxyPointerKind::HardOwner:   t.kind = RefKind::HardOwner; break;
        }
        out.push_back(std::move(t));
    }
}

// The ids of the stream, then the handles the class's data holds (its codec says what they are)
void proxy_object_references(const Object& o, std::vector<TypedReference>& out) {
    const ProxyObjectData& d = o.as_proxy_object();
    add_references(d.proxy, out);
    if (!d.class_data) return;
    const ObjectCodec* codec = class_codec(*d.class_data);
    if (codec && codec->references) codec->references(class_record(o, *d.class_data, false), out);
}

void proxy_entity_references(const Entity& e, std::vector<TypedReference>& out) {
    const ProxyEntityData& d = e.as_proxy_entity();
    add_references(d.proxy, out);
    if (!d.class_data) return;
    const EntityCodec* codec = class_codec(*d.class_data);
    if (codec && codec->references) codec->references(class_record(e, *d.class_data, false), out);
}

// The ids are pointers whatever their code says: a target that is gone becomes 0 in place (the data refers to
// the ids by place), never a removed member
std::vector<RefDescriptor> pointer_descriptors(std::initializer_list<const char*> subclasses) {
    std::vector<RefDescriptor> out;
    for (const char* subclass : subclasses) {
        const struct {
            int code;
            RefKind kind;
        } kinds[] = {{330, RefKind::SoftPointer}, {340, RefKind::HardPointer}, {350, RefKind::SoftOwner},
                     {360, RefKind::HardOwner}};
        for (const auto& k : kinds) {
            RefDescriptor d;
            d.subclass = subclass;
            d.code = k.code;
            d.kind = k.kind;
            d.removal = RefRemoval::ZeroInPlace;
            out.push_back(d);
        }
    }
    return out;
}

// The class data is written as one unit: a change of the data, the graphics or a pointer rewrites the subclass
// from the typed values (their lengths, the stream and the id list belong together)
std::vector<ListSpec> whole_subclass_lists(std::initializer_list<const char*> subclasses) {
    std::vector<ListSpec> out;
    for (const char* subclass : subclasses) {
        ListSpec list;
        list.subclass = subclass;
        list.id = 1;
        list.shape = ListShape::WholeSubclass;
        out.push_back(list);
    }
    return out;
}

// What the entity draws: its own proxy graphics, else the class's, as AutoCAD's extents take it (the
// extents command of the stream, else the geometry). A stream that is not a proxy graphics stream draws nothing
void proxy_entity_extents(const Entity& e, TypedExtentsSink& sink) {
    const ProxyEntityData& d = e.as_proxy_entity();
    const std::vector<uint8_t>& stream = !d.entity_graphics.empty() ? d.entity_graphics : d.proxy.graphics;
    if (stream.empty()) return;
    const int code_page = sink.drawing ? drawing_code_page(*sink.drawing) : 1252;
    for (const Point3& p : proxy_graphics_extent_points(stream, code_page)) sink.add_wcs(p);
}

// The relations and lists of the classes' subclasses, added to the proxy's own (a proxy of DXF data has its class's subclasses
// in its record, and the merge of a changed record looks them up by the proxy's name): the descriptors of every codec with a
// class (only a class can be proxied) that name their subclass, not AcDbEntity's
template <typename Codec>
void add_class_descriptors(const Codec& c, std::vector<RefDescriptor>& refs, std::vector<ListSpec>& lists) {
    if (!c.cls.cpp_name) return;
    auto own = [](const char* subclass) {
        return subclass && !std::string(subclass).empty() && std::string(subclass) != "AcDbEntity" &&
               std::string(subclass).rfind("AcDbProxy", 0) != 0 && std::string(subclass).rfind("AcDbZombie", 0) != 0;
    };
    for (const RefDescriptor& d : c.refs) if (own(d.subclass)) refs.push_back(d);
    for (const ListSpec& l : c.lists) if (own(l.subclass)) lists.push_back(l);
}

} // namespace

void complete_proxy_codecs(TypedRegistry& registry) {
    const ObjectCodec* object = registry.find_object("ACAD_PROXY_OBJECT");
    const EntityCodec* entity = registry.find_entity("ACAD_PROXY_ENTITY");
    if (object) {
        ObjectCodec c = *object;
        for (const ObjectCodec& other : registry.object_codecs()) {
            if (other.type != ObjectType::ProxyObject) add_class_descriptors(other, c.refs, c.lists);
        }
        registry.add(std::move(c));
    }
    if (entity) {
        EntityCodec c = *entity;
        for (const EntityCodec& other : registry.entity_codecs()) {
            if (other.type != EntityType::ProxyEntity) add_class_descriptors(other, c.refs, c.lists);
        }
        registry.add(std::move(c));
    }
}

void register_proxy_codecs(TypedRegistry& registry) {
    // ACAD_PROXY_OBJECT: AutoCAD 2025 saved it in 2000 … 2018, ASCII and binary (the facit); R12 has no OBJECTS
    // section and AutoCAD's R12 file has none. R14: the reference's form, not AutoCAD's (not savable); R13: no form
    // is established
    ObjectCodec object;
    object.dxf_name = "ACAD_PROXY_OBJECT";
    object.type = ObjectType::ProxyObject;
    object.read = read_proxy_object;
    object.write = write_proxy_object;
    object.references = proxy_object_references;
    object.versions = VersionMatrix::unestablished();
    object.versions.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    object.versions.set(DxfVersion::R13, Generation::None, ErrorBasis::NotEstablished);
    for (DxfVersion v : {DxfVersion::R14, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        object.versions.set(v, Generation::Native, ErrorBasis::NotApplicable);
    }
    object.versions.autocad(DxfVersion::R12, AutoCadStatus::Verified);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        object.versions.autocad(v, AutoCadStatus::Verified);
    }
    object.refs = pointer_descriptors({"AcDbProxyObject", "AcDbZombieObject"});
    object.lists = whole_subclass_lists({"AcDbProxyObject", "AcDbZombieObject"});
    object.step = 3;
    registry.add(std::move(object));

    // ACAD_PROXY_ENTITY: the R14 reference's page and every later one. AutoCAD's files have the proxy of DXF data (TurboCAD's
    // point, F9.25) in 2000, 2004, 2010, 2013 and 2018 and none in R12; 2007 has the point's own record there (its version), and
    // the proxy of DWG data is in none of AutoCAD's files (F9.2)
    EntityCodec entity;
    entity.dxf_name = "ACAD_PROXY_ENTITY";
    entity.type = EntityType::ProxyEntity;
    entity.read = read_proxy_entity;
    entity.write = write_proxy_entity;
    entity.references = proxy_entity_references;
    entity.extents = proxy_entity_extents;
    entity.versions = VersionMatrix::native_from(DxfVersion::R14);
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        entity.versions.autocad(v, AutoCadStatus::Verified);
    }
    entity.refs = pointer_descriptors({"AcDbProxyEntity", "AcDbZombieEntity"});
    entity.lists = whole_subclass_lists({"AcDbProxyEntity", "AcDbZombieEntity"});
    entity.step = 3;
    registry.add(std::move(entity));
}

// ---------------------------------------------------------------------------
// CLASSES
// ---------------------------------------------------------------------------

void append_proxy_classes(const Drawing& dwg, DxfVersion version, const std::unordered_set<uint64_t>* dropped,
                          std::vector<ClassEntry>& out) {
    const bool older = version < DxfVersion::R2000;
    struct Use {
        const ProxyApplicationClass* cls = nullptr;
        int count = 0;
    };
    std::vector<Use> uses;
    auto note = [&](const ProxyClassData& p) {
        const ProxyApplicationClass& c = p.application_class;
        // A class needs its DXF name and C++ name: the name alone (2000's XDATA) is no CLASS record
        if (c.dxf_name.empty() || c.cpp_name.empty()) return;
        for (Use& u : uses) {
            if (iequals(u.cls->dxf_name, c.dxf_name)) { ++u.count; return; }
        }
        uses.push_back({&c, 1});
    };
    for (const Object& o : dwg.objects()) {
        uint64_t h = 0;
        if (o.type != ObjectType::ProxyObject) continue;
        if (dropped && parse_handle(o.handle, h) == HandleParse::Ok && dropped->count(h)) continue;
        note(o.as_proxy_object().proxy);
    }
    auto entities = [&](const std::vector<Entity>& list) {
        for (const Entity& e : list) if (e.type == EntityType::ProxyEntity) note(e.as_proxy_entity().proxy);
    };
    entities(dwg.entities());
    for (const Block& b : dwg.blocks()) entities(b.entities);

    for (const Use& u : uses) {
        bool have = false;
        for (const ClassEntry& c : out) have = have || iequals(c.name, u.cls->dxf_name);
        if (have) continue;
        ClassEntry e{u.cls->dxf_name.c_str(), u.cls->cpp_name.c_str(), u.cls->application.c_str(),
                     older && version == DxfVersion::R13 ? 32768 : u.cls->proxy_flags, u.cls->entity, u.count};
        out.push_back(e);
    }
}

void resolve_proxy_classes(Drawing& dwg, std::vector<std::string>& warnings) {
    // Most drawings have no proxy: nothing to look up
    auto has_proxy_entity = [](const std::vector<Entity>& list) {
        return std::any_of(list.begin(), list.end(), [](const Entity& e) { return e.type == EntityType::ProxyEntity; });
    };
    bool any = std::any_of(dwg.objects().begin(), dwg.objects().end(),
                           [](const Object& o) { return o.type == ObjectType::ProxyObject; }) ||
               has_proxy_entity(dwg.entities());
    for (const Block& b : dwg.blocks()) any = any || has_proxy_entity(b.entities);
    if (!any) return;

    // The CLASS records in the order of the file
    std::vector<ProxyApplicationClass> classes;
    bool have_section = false;
    for (const RawSection& s : dwg.raw_sections()) {
        if (s.name != "CLASSES") continue;
        have_section = true;
        for (const RawRecord& r : s.records) {
            ProxyApplicationClass c;
            bool is_class = false;
            for (const DxfGroup& g : r.groups()) {
                switch (g.code) {
                    case 0: is_class = trimmed(g.value) == "CLASS"; break;
                    case 1: c.dxf_name = trimmed(g.value); break;
                    case 2: c.cpp_name = trimmed(g.value); break;
                    case 3: c.application = trimmed(g.value); break;
                    case 90: c.proxy_flags = std::atoi(g.value.c_str()); break;
                    case 281: c.entity = std::atoi(g.value.c_str()) != 0; break;
                    default: break;
                }
            }
            if (is_class) classes.push_back(std::move(c));
        }
    }
    auto resolve = [&](ProxyClassData& p, const std::string& what, const std::string& handle) {
        // In 64 bits: 91 is any int32 a file has (the reader leaves one below 500 untyped; Codex 15)
        const int64_t index = static_cast<int64_t>(p.application_class_id) - 500;
        if (index >= 0 && static_cast<uint64_t>(index) < classes.size()) {
            const ProxyApplicationClass& c = classes[static_cast<size_t>(index)];
            p.application_class = c;
            return;
        }
        if (!have_section) return;          // a file without CLASSES (R13 and R14 have some): the id stays as read
        warnings.push_back(what + " " + handle + ": the class id " + std::to_string(p.application_class_id) +
                           " is not in CLASSES (" + std::to_string(classes.size()) + " classes); kept as it was read");
    };
    for (Object& o : dwg.objects()) {
        if (o.type == ObjectType::ProxyObject) resolve(o.as_proxy_object().proxy, "ACAD_PROXY_OBJECT", o.handle);
    }
    auto entities = [&](std::vector<Entity>& list) {
        for (Entity& e : list) {
            if (e.type == EntityType::ProxyEntity) resolve(e.as_proxy_entity().proxy, "ACAD_PROXY_ENTITY", e.props.handle);
        }
    };
    entities(dwg.entities());
    for (Block& b : dwg.blocks()) entities(b.entities);
}

} // namespace internal
} // namespace hdxf
