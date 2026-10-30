#include "hdxf.hpp"
#include "handles.hpp"
#include "roundtrip.hpp"
#include "mtext_data.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <unordered_set>

// What 2013 has in its records and 2000 … 2010 keeps elsewhere, as AutoCAD writes it (plan
// phase 5, B60): a linetype's upright elements (74 bit 8), a MULTILEADER's 295 and a
// MLEADERSTYLE's 298 in round-trip XRECORDs, and a body's change GUID (AcDbModelerGeometry 290/2)
// as XDATA ACAD_OBJECT_CHANGE_GUID.
//
// The XRECORD is in the record's extension dictionary under ACAD_XREC_ROUNDTRIP: the dictionary
// hard-owned by the record (280 1, 281 1, no reactors), the XRECORD with the dictionary as owner
// and reactor, 280 1, then 102 and its name and the value (ACadSharp's 2000 … 2010 samples for the
// linetype, LibreDWG's example_2007/2010 for the multileader and its style, jscad's
// empty2007/2010 for the style):
//   ACAD_ROUNDTRIP_2011_LINETYPE         310: int32 n + 1, then per element its 2013 value of 74
//   ACAD_ROUNDTRIP_2012_MLEADER          295 1
//   ACAD_ROUNDTRIP_2012_MLEADER_STYLE    298 1

namespace hdxf {

namespace internal {

const AcisData* acis_of(const Entity& e) {
    switch (e.type) {
        case EntityType::Solid3D:
        case EntityType::Region:
        case EntityType::Body:
            return &e.as_acis();
        case EntityType::Surface:
        case EntityType::ExtrudedSurface:
        case EntityType::LoftedSurface:
        case EntityType::RevolvedSurface:
        case EntityType::SweptSurface:
        case EntityType::PlaneSurface:
        case EntityType::NurbsSurface:
            return &e.as_surface().acis;
        default:
            return nullptr;
    }
}

AcisData* acis_of(Entity& e) {
    return const_cast<AcisData*>(acis_of(static_cast<const Entity&>(e)));
}

namespace {

bool hex_value(const std::string& s, size_t from, size_t count, uint64_t& out) {
    out = 0;
    if (from + count > s.size()) return false;
    for (size_t i = from; i < from + count; ++i) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0'
              : c >= 'a' && c <= 'f' ? c - 'a' + 10
              : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        out = out * 16 + static_cast<uint64_t>(d);
    }
    return true;
}

std::string hex_digits(uint64_t v, int count) {
    static const char digits[] = "0123456789abcdef";
    std::string out(static_cast<size_t>(count), '0');
    for (int i = count - 1; i >= 0; --i) {
        out[static_cast<size_t>(i)] = digits[v & 15];
        v >>= 4;
    }
    return out;
}

bool integer_of(const std::string& text, long long& out) {
    std::string t = text;
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    while (!t.empty() && t.back() == ' ') t.pop_back();
    if (t.empty()) return false;
    char* end = nullptr;
    out = std::strtoll(t.c_str(), &end, 10);
    return end && *end == '\0';
}

} // namespace

bool change_guid_xdata(const std::string& guid, XdataList& out) {
    // {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
    if (guid.size() != 38 || guid.front() != '{' || guid.back() != '}' || guid[9] != '-' ||
        guid[14] != '-' || guid[19] != '-' || guid[24] != '-') {
        return false;
    }
    uint64_t data1 = 0, data2 = 0, data3 = 0;
    if (!hex_value(guid, 1, 8, data1) || !hex_value(guid, 10, 4, data2) || !hex_value(guid, 15, 4, data3)) return false;
    std::vector<uint64_t> data4;
    for (size_t at : {20, 22, 25, 27, 29, 31, 33, 35}) {
        uint64_t b = 0;
        if (!hex_value(guid, at, 2, b)) return false;
        data4.push_back(b);
    }
    out = XdataList{};
    out.app_name = "ACAD_OBJECT_CHANGE_GUID";
    out.items.push_back({1070, "1"});
    out.items.push_back({1071, std::to_string(static_cast<int32_t>(static_cast<uint32_t>(data1)))});
    out.items.push_back({1070, std::to_string(static_cast<int16_t>(static_cast<uint16_t>(data2)))});
    out.items.push_back({1070, std::to_string(static_cast<int16_t>(static_cast<uint16_t>(data3)))});
    for (uint64_t b : data4) out.items.push_back({1070, std::to_string(b)});
    return true;
}

bool change_guid_of(const XdataList& x, std::string& guid) {
    if (x.app_name != "ACAD_OBJECT_CHANGE_GUID" || x.items.size() != 12) return false;
    std::vector<long long> v;
    for (size_t i = 0; i < x.items.size(); ++i) {
        int want = i == 1 ? 1071 : 1070;
        long long n = 0;
        if (x.items[i].code != want || !integer_of(x.items[i].value, n)) return false;
        v.push_back(n);
    }
    if (v[0] != 1 || v[1] < INT32_MIN || v[1] > INT32_MAX) return false;
    for (size_t i = 2; i < 4; ++i) if (v[i] < INT16_MIN || v[i] > INT16_MAX) return false;
    for (size_t i = 4; i < 12; ++i) if (v[i] < 0 || v[i] > 255) return false;
    guid = "{" + hex_digits(static_cast<uint32_t>(static_cast<int32_t>(v[1])), 8) + "-" +
           hex_digits(static_cast<uint16_t>(static_cast<int16_t>(v[2])), 4) + "-" +
           hex_digits(static_cast<uint16_t>(static_cast<int16_t>(v[3])), 4) + "-" +
           hex_digits(static_cast<uint64_t>(v[4]), 2) + hex_digits(static_cast<uint64_t>(v[5]), 2) + "-";
    for (size_t i = 6; i < 12; ++i) guid += hex_digits(static_cast<uint64_t>(v[i]), 2);
    guid += "}";
    return true;
}

} // namespace internal

namespace {

const char* const kRoundTrip = "ACAD_XREC_ROUNDTRIP";
const char* const kLinetype = "ACAD_ROUNDTRIP_2011_LINETYPE";
const char* const kMLeader = "ACAD_ROUNDTRIP_2012_MLEADER";
const char* const kMLeaderStyle = "ACAD_ROUNDTRIP_2012_MLEADER_STYLE";

bool is_roundtrip_name(const std::string& name) {
    return name == kLinetype || name == kMLeader || name == kMLeaderStyle || name == "DISPLAYNAME";
}

// The XRECORD `name` in the extension dictionary `xdictionary` under ACAD_XREC_ROUNDTRIP
const Object* roundtrip_record(const Drawing& dwg, const std::string& xdictionary, const char* name) {
    if (xdictionary.empty()) return nullptr;
    const Object* dict = dwg.find_object(xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const auto& e : dict->as_dictionary().entries) {
        if (e.name != kRoundTrip) continue;
        const Object* x = dwg.find_object(e.handle);
        if (x && x->type == ObjectType::XRecord && !x->as_xrecord().data.empty() &&
            x->as_xrecord().data[0].code == 102 && x->as_xrecord().data[0].value == name) {
            return x;
        }
    }
    return nullptr;
}

// The first value of `code` in an XRECORD's data, as an integer (0 when there is none)
long long roundtrip_value(const XRecordData& x, int code) {
    for (const auto& g : x.data) {
        long long n = 0;
        if (g.code == code && internal::integer_of(g.value, n)) return n;
    }
    return 0;
}

// The linetype XRECORD's elements: the int32 values after the first (n + 1, as AutoCAD writes
// it; ODA's reading takes n too, B66), from its 310 bytes
std::vector<int32_t> linetype_values(const XRecordData& x) {
    std::string hex;
    for (const auto& g : x.data) if (g.code == 310) hex += g.value;
    std::vector<int32_t> words;
    for (size_t at = 0; at + 8 <= hex.size(); at += 8) {
        uint32_t w = 0;
        for (size_t k = 0; k < 4; ++k) {
            uint64_t b = 0;
            if (!internal::hex_value(hex, at + 2 * k, 2, b)) return {};
            w |= static_cast<uint32_t>(b) << (8 * k);
        }
        words.push_back(static_cast<int32_t>(w));
    }
    if (words.empty()) return {};
    return std::vector<int32_t>(words.begin() + 1, words.end());
}

// Which elements are upright (bit 8), by the linetype's dashes
std::vector<bool> upright_of(const Linetype& lt) {
    std::vector<bool> out(lt.pattern.size(), false);
    for (size_t i = 0; i < out.size() && i < lt.elements.size(); ++i) out[i] = (lt.elements[i].type & 8) != 0;
    return out;
}

std::vector<bool> upright_of(const XRecordData& x, size_t count) {
    std::vector<int32_t> values = linetype_values(x);
    std::vector<bool> out(count, false);
    for (size_t i = 0; i < count && i < values.size(); ++i) out[i] = (values[i] & 8) != 0;
    return out;
}

std::string hex_of_words(const std::vector<int32_t>& words) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (int32_t w : words) {
        uint32_t u = static_cast<uint32_t>(w);
        for (int k = 0; k < 4; ++k) {
            uint8_t b = static_cast<uint8_t>(u >> (8 * k));
            out += digits[b >> 4];
            out += digits[b & 15];
        }
    }
    return out;
}

uint64_t value_of(const std::string& h) {
    uint64_t v = 0;
    return internal::parse_handle(h, v) == internal::HandleParse::Ok ? v : 0;
}

} // namespace

// A multiline attribute up to 2013 (plan phase 6, B76): the XRECORD ACAD_MLATT of the attribute's
// extension dictionary — 70 (2 attribute, 4 definition), 70 (really locked), 70 (the number of
// secondary attributes) and their 340, 10 (the alignment point, when it has one), 40 (the annotation
// scale), 2 (the tag), 1 "Embedded Object" and the text's AcDbMText groups (ACadSharp's
// sample_AC1027, 79D and 796). False when the XRECORD does not have that form.
static bool multiline_from_xrecord(const XRecordData& x, MultilineAttribute& ml) {
    const std::vector<DxfGroup>& g = x.data;
    size_t i = 0;
    auto number = [&](int code, int& out) {
        if (i >= g.size() || g[i].code != code) return false;
        out = std::atoi(g[i++].value.c_str());
        return true;
    };
    int locked = 0, secondary = 0;
    if (!number(70, ml.type) || !number(70, locked) || !number(70, secondary)) return false;
    ml.really_locked = locked != 0;
    ml.secondary.clear();
    for (int k = 0; k < secondary; ++k) {
        if (i >= g.size() || g[i].code != 340) return false;
        ml.secondary.push_back(g[i++].value);
    }
    ml.alignment_point = {};
    if (i + 2 < g.size() && g[i].code == 10 && g[i + 1].code == 20 && g[i + 2].code == 30) {
        ml.alignment_point = {std::atof(g[i].value.c_str()), std::atof(g[i + 1].value.c_str()), std::atof(g[i + 2].value.c_str())};
        i += 3;
    }
    if (i >= g.size() || g[i].code != 40) return false;
    ml.annotation_scale = std::atof(g[i++].value.c_str());
    if (i >= g.size() || g[i].code != 2) return false;
    ml.tag = g[i++].value;
    if (i >= g.size() || g[i].code != 1 || g[i].value != "Embedded Object") return false;
    ++i;
    internal::MTextParseState state;
    MTextData text;
    int embedded = 1;
    for (; i < g.size(); ++i) {
        if (g[i].code == 101) { ++embedded; continue; }
        if (embedded == 1) internal::mtext_apply_group(text, state, g[i].code, g[i].value);
        else if (embedded == 2) internal::mtext_apply_embedded_group(text, state, g[i].code, g[i].value);
    }
    internal::mtext_finish(text, state, {0.0, 0.0, 1.0});
    ml.text = std::move(text);
    return true;
}

void Drawing::read_multiline_attributes() {
    auto record = [&](const std::string& xdictionary) -> const XRecordData* {
        if (xdictionary.empty()) return nullptr;
        const Object* dict = find_object(xdictionary);
        if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
        for (const auto& e : dict->as_dictionary().entries) {
            if (e.name != "ACAD_MLATT") continue;
            const Object* x = find_object(e.handle);
            if (x && x->type == ObjectType::XRecord) return &x->as_xrecord();
        }
        return nullptr;
    };
    // The attribute's tag is the XRECORD's (the ATTRIB's own 2 has the "_001" of the first line)
    auto read = [&](const std::string& xdictionary, OptionalBoxed<MultilineAttribute>& out, std::string& tag) {
        const XRecordData* x = record(xdictionary);
        if (!x || out) return;
        MultilineAttribute ml;
        if (!multiline_from_xrecord(*x, ml)) return;
        if (!ml.tag.empty()) {
            if (tag == ml.tag + "_001") tag = ml.tag;
            if (tag == ml.tag) ml.tag.clear();
        }
        out = std::move(ml);
    };
    auto entities = [&](std::vector<Entity>& list) {
        for (auto& e : list) {
            if (e.type == EntityType::Attdef) read(e.props.xdictionary, e.as_attdef().multiline, e.as_attdef().tag);
            if (e.type == EntityType::Attrib) read(e.props.xdictionary, e.as_attrib().multiline, e.as_attrib().tag);
            if (e.type != EntityType::Insert) continue;
            for (auto& a : e.as_insert().attributes) read(a.props.xdictionary, a.data.multiline, a.data.tag);
        }
    };
    entities(entities_);
    for (auto& b : blocks_) entities(b.entities);
}

void Drawing::read_roundtrip_records(DxfVersion version) {
    if (version < DxfVersion::R2000) return;
    read_visual_style_records();            // (step 9.7: the older forms' properties 28 … 57 in the style's XRECORD)
    link_mtext_columns();
    if (version >= DxfVersion::R2018) return;
    read_multiline_attributes();
    link_multiline_attributes();
    read_view_style_records();
    if (version >= DxfVersion::R2013) return;
    for (auto& lt : linetypes_) {
        const Object* x = roundtrip_record(*this, lt.xdictionary, kLinetype);
        if (!x) continue;
        std::vector<bool> upright = upright_of(x->as_xrecord(), lt.pattern.size());
        bool any = std::find(upright.begin(), upright.end(), true) != upright.end();
        if (any && lt.elements.size() < lt.pattern.size()) lt.elements.resize(lt.pattern.size());
        for (size_t i = 0; i < upright.size(); ++i) if (upright[i]) lt.elements[i].type |= 8;
    }
    auto entities = [&](std::vector<Entity>& list) {
        for (auto& e : list) {
            if (e.type == EntityType::MLeader) {
                if (const Object* x = roundtrip_record(*this, e.props.xdictionary, kMLeader)) {
                    e.as_mleader().text_extended = roundtrip_value(x->as_xrecord(), 295) != 0;
                }
            }
            AcisData* acis = internal::acis_of(e);
            if (!acis || !acis->guid.empty()) continue;
            // Exactly AutoCAD's form, the only list of its application: the GUID, out of the XDATA
            int found = 0;
            std::string guid;
            for (const auto& xd : e.props.xdata) {
                if (xd.app_name != "ACAD_OBJECT_CHANGE_GUID") continue;
                ++found;
                if (!internal::change_guid_of(xd, guid)) found = 2;
            }
            if (found != 1) continue;
            acis->guid = guid;
            auto& xdata = e.props.xdata;
            xdata.erase(std::remove_if(xdata.begin(), xdata.end(),
                                       [](const XdataList& xd) { return xd.app_name == "ACAD_OBJECT_CHANGE_GUID"; }),
                        xdata.end());
        }
    };
    entities(entities_);
    for (auto& b : blocks_) entities(b.entities);
    for (auto& o : objects_) {
        if (o.type != ObjectType::MLeaderStyle) continue;
        if (const Object* x = roundtrip_record(*this, o.xdictionary, kMLeaderStyle)) {
            o.as_mleader_style().text_extended = roundtrip_value(x->as_xrecord(), 298) != 0;
        }
    }
}

// Before 2013 the XRECORD is there exactly when the value needs it and says the value: made,
// changed or removed (a dictionary left empty goes with its pointer); one read that says the same
// stays as it is, so the file's records are written as read. In 2013 there is none — except in a
// drawing read from a 2013 file, whose records stay as read. A round-trip XRECORD whose record is
// gone goes with its dictionary (B19).
void Drawing::sync_roundtrip_records() {
    if (version_ < DxfVersion::R2000) return;
    sync_visual_style_records();            // (step 9.7: by the style's form, not the version written)
    if (source_version_ == DxfVersion::R2013 && version_ == DxfVersion::R2013) return;
    bool records = version_ < DxfVersion::R2013;
    bool scanned = false;

    auto remove_object = [&](const std::string& handle) {
        uint64_t v = value_of(handle);
        objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                      [&](const Object& o) { return value_of(o.handle) == v; }),
                       objects_.end());
    };
    // The XRECORD `name` of the record `owner` (its extension dictionary `xdictionary`, which
    // may change): kept when `same` says it agrees, otherwise given `data`, made, or removed
    // (`data` empty: the value needs none)
    auto sync = [&](std::string& owner, std::string& xdictionary, const char* name,
                    const std::vector<DxfGroup>& data, const std::function<bool(const XRecordData&)>& same) {
        const Object* found = roundtrip_record(*this, xdictionary, name);
        std::string record = found ? found->handle : std::string();
        if (!record.empty() && records && same(found->as_xrecord())) return;
        if (!record.empty() && records && !data.empty()) {
            XRecordData& x = find_object(record)->as_xrecord();
            x.data = data;
            return;
        }
        if (!record.empty()) {
            remove_object(record);
            Object* dict = find_object(xdictionary);
            auto& entries = dict->as_dictionary().entries;
            entries.erase(std::remove_if(entries.begin(), entries.end(),
                                         [&](const DictionaryEntry& e) { return e.handle == record; }),
                          entries.end());
            if (entries.empty() && dict->xdata.empty() && dict->xdictionary.empty()) {
                remove_object(xdictionary);
                xdictionary.clear();
            }
            return;
        }
        if (!records || data.empty()) return;
        if (!scanned) {
            scan_handles();
            scanned = true;
        }
        if (owner.empty()) owner = next_handle();
        Object* dict = xdictionary.empty() ? nullptr : find_object(xdictionary);
        if (dict && dict->type != ObjectType::Dictionary) return;     // not a dictionary: left alone
        if (!dict) {
            Object dictionary;
            dictionary.type = ObjectType::Dictionary;
            dictionary.handle = next_handle();
            dictionary.owner = owner;
            DictionaryData d;
            d.hard_owner = true;
            dictionary.data = d;
            xdictionary = dictionary.handle;
            objects_.push_back(std::move(dictionary));
        }
        Object roundtrip;
        roundtrip.type = ObjectType::XRecord;
        roundtrip.handle = next_handle();
        roundtrip.owner = xdictionary;
        roundtrip.reactors = {xdictionary};
        XRecordData x;
        x.data = data;
        roundtrip.data = x;
        std::string handle = roundtrip.handle;
        objects_.push_back(std::move(roundtrip));
        find_object(xdictionary)->as_dictionary().entries.push_back({kRoundTrip, handle, true});
    };

    for (auto& lt : linetypes_) {
        std::vector<bool> upright = upright_of(lt);
        std::vector<DxfGroup> data;
        if (std::find(upright.begin(), upright.end(), true) != upright.end()) {
            std::vector<int32_t> words = {static_cast<int32_t>(lt.pattern.size() + 1)};
            for (size_t i = 0; i < lt.pattern.size(); ++i) {
                words.push_back(i < lt.elements.size() ? lt.elements[i].type : 0);
            }
            data = {{102, kLinetype}, {310, hex_of_words(words)}};
        }
        sync(lt.handle, lt.xdictionary, kLinetype, data, [&](const XRecordData& x) {
            return upright_of(x, lt.pattern.size()) == upright;
        });
    }
    auto entities = [&](std::vector<Entity>& list) {
        for (auto& e : list) {
            if (e.type != EntityType::MLeader) continue;
            bool extended = e.as_mleader().text_extended;
            std::vector<DxfGroup> data;
            if (extended) data = {{102, kMLeader}, {295, "1"}};
            sync(e.props.handle, e.props.xdictionary, kMLeader, data, [&](const XRecordData& x) {
                return (roundtrip_value(x, 295) != 0) == extended;
            });
        }
    };
    entities(entities_);
    for (auto& b : blocks_) entities(b.entities);
    // (a style added without a handle gets one first when it needs the record — it is found by
    // its handle, since the objects move as records are made; one that needs none is left alone)
    std::vector<std::string> styles;
    for (auto& o : objects_) {
        if (o.type != ObjectType::MLeaderStyle) continue;
        if (o.handle.empty()) {
            if (!records || !o.as_mleader_style().text_extended) continue;
            if (!scanned) {
                scan_handles();
                scanned = true;
            }
            o.handle = next_handle();
        }
        styles.push_back(o.handle);
    }
    for (const auto& h : styles) {
        Object* o = find_object(h);
        std::string handle = o->handle, xdictionary = o->xdictionary;
        bool extended = o->as_mleader_style().text_extended;
        std::vector<DxfGroup> data;
        if (extended) data = {{102, kMLeaderStyle}, {298, "1"}};
        sync(handle, xdictionary, kMLeaderStyle, data, [&](const XRecordData& x) {
            return (roundtrip_value(x, 298) != 0) == extended;
        });
        find_object(h)->xdictionary = xdictionary;
    }

    // A round-trip XRECORD whose record is gone (an entity or linetype removed since): with its
    // entry, and the dictionary when it is left empty
    std::unordered_set<uint64_t> owners;
    for (const auto& lt : linetypes_) if (!lt.xdictionary.empty()) owners.insert(value_of(lt.xdictionary));
    auto owners_of = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (!e.props.xdictionary.empty()) owners.insert(value_of(e.props.xdictionary));
            if (e.type != EntityType::Insert) continue;
            for (const auto& a : e.as_insert().attributes) {
                if (!a.props.xdictionary.empty()) owners.insert(value_of(a.props.xdictionary));
            }
        }
    };
    owners_of(entities_);
    for (const auto& b : blocks_) {
        owners_of(b.entities);
        if (!b.record.xdictionary.empty()) owners.insert(value_of(b.record.xdictionary));
    }
    for (const auto& o : objects_) if (!o.xdictionary.empty()) owners.insert(value_of(o.xdictionary));
    // (the three XRECORDs are only in the extension dictionaries of those records: a dictionary
    // with one that no record has as its extension dictionary belongs to a record that is gone)
    std::vector<std::string> orphans;
    for (const auto& o : objects_) {
        if (o.type != ObjectType::Dictionary || owners.count(value_of(o.handle))) continue;
        for (const auto& e : o.as_dictionary().entries) {
            if (e.name != kRoundTrip) continue;
            const Object* x = find_object(e.handle);
            if (x && x->type == ObjectType::XRecord && !x->as_xrecord().data.empty() &&
                is_roundtrip_name(x->as_xrecord().data[0].value) && x->owner == o.handle) {
                orphans.push_back(o.handle);
            }
        }
    }
    for (const auto& dict_handle : orphans) {
        Object* dict = find_object(dict_handle);
        if (!dict) continue;
        std::vector<std::string> gone;
        auto& entries = dict->as_dictionary().entries;
        for (const auto& e : entries) {
            const Object* x = find_object(e.handle);
            if (e.name == kRoundTrip && x && x->type == ObjectType::XRecord && !x->as_xrecord().data.empty() &&
                is_roundtrip_name(x->as_xrecord().data[0].value)) {
                gone.push_back(e.handle);
            }
        }
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) {
            return std::find(gone.begin(), gone.end(), e.handle) != gone.end();
        }), entries.end());
        bool empty = entries.empty();
        for (const auto& h : gone) remove_object(h);
        if (empty) remove_object(dict_handle);
    }
}

} // namespace hdxf
