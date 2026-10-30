#include "StyleTypes.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

// VISUALSTYLE (phase 9, step 9.7): the properties, reading the three forms, the XRECORD of the older forms, the codec.
// The groups are AutoCAD's own (doc/facit/acad_*_dxf_visual_styles*.dxf from AutoCAD 2025's core, ACadSharp's samples of
// 2000 … 2018, jscad's empty2007/2010/2013); the reference (VISUALSTYLE group codes) still has the 2007 form for every
// version and names none of the properties after 27 (plan 9.7, "VISUALSTYLE — tre former").

namespace hdxf {

namespace {

struct PropertyInfo {
    const char* name;
    VisualStyleValueKind kind;
    int code;                   // the value's group in the two older forms; 0: none (positional or in the XRECORD)
    int true_color_code;        // 421, 424, 425
};

using K = VisualStyleValueKind;

// By id. The kinds of 28 … 57 are what AutoCAD's files show (the newest form's 58 values and the XRECORD's groups agree)
const PropertyInfo kProperties[kVisualStylePropertyCount] = {
    {"FaceLightingModel", K::Integer, 71, 0},
    {"FaceLightingQuality", K::Integer, 72, 0},
    {"FaceColorMode", K::Integer, 73, 0},
    {"FaceModifiers", K::Integer, 90, 0},
    {"FaceOpacity", K::Real, 40, 0},
    {"FaceSpecular", K::Real, 41, 0},
    {"FaceMonoColor", K::Color, 63, 421},
    {"EdgeModel", K::Integer, 74, 0},
    {"EdgeStyles", K::Integer, 91, 0},
    {"EdgeIntersectionColor", K::Color, 64, 0},
    {"EdgeObscuredColor", K::Color, 65, 0},
    {"EdgeObscuredLinetype", K::Integer, 75, 0},
    {"EdgeIntersectionLinetype", K::Integer, 175, 0},
    {"EdgeCreaseAngle", K::Real, 42, 0},
    {"EdgeModifiers", K::Integer, 92, 0},
    {"EdgeColor", K::Color, 66, 424},
    {"EdgeOpacity", K::Real, 43, 0},
    {"EdgeWidth", K::Integer, 76, 0},
    {"EdgeOverhang", K::Integer, 77, 0},
    {"EdgeJitter", K::Integer, 78, 0},
    {"EdgeSilhouetteColor", K::Color, 67, 425},
    {"EdgeSilhouetteWidth", K::Integer, 79, 0},
    {"EdgeHaloGap", K::Integer, 170, 0},
    {"EdgeIsolines", K::Integer, 171, 0},
    {"EdgeHidePrecision", K::Boolean, 290, 0},
    {"DisplaySettings", K::Integer, 93, 0},
    {"DisplayBrightness", K::Real, 44, 0},
    {"DisplayShadowType", K::Integer, 173, 0},
    {"Post2010Prop28", K::Boolean, 0, 0}, {"Post2010Prop29", K::Boolean, 0, 0}, {"Post2010Prop30", K::Boolean, 0, 0},
    {"Post2010Prop31", K::Boolean, 0, 0}, {"Post2010Prop32", K::Boolean, 0, 0}, {"Post2010Prop33", K::Boolean, 0, 0},
    {"Post2010Prop34", K::Boolean, 0, 0}, {"Post2010Prop35", K::Boolean, 0, 0}, {"Post2010Prop36", K::Boolean, 0, 0},
    {"Post2010Prop37", K::Integer, 0, 0}, {"Post2010Prop38", K::Real, 0, 0},    {"Post2010Prop39", K::Real, 0, 0},
    {"Post2010Prop40", K::Integer, 0, 0}, {"Post2010Prop41", K::Color, 0, 0},   {"Post2010Prop42", K::Integer, 0, 0},
    {"Post2010Prop43", K::Integer, 0, 0}, {"Post2010Prop44", K::Color, 0, 0},   {"Post2010Prop45", K::Boolean, 0, 0},
    {"Post2010Prop46", K::Integer, 0, 0}, {"Post2010Prop47", K::Integer, 0, 0}, {"Post2010Prop48", K::Integer, 0, 0},
    {"Post2010Prop49", K::Boolean, 0, 0}, {"Post2010Prop50", K::Integer, 0, 0}, {"Post2010Prop51", K::Color, 0, 0},
    {"Post2010Prop52", K::Real, 0, 0},    {"Post2010Prop53", K::Integer, 0, 0}, {"Post2010Prop54", K::Text, 0, 0},
    {"Post2010Prop55", K::Boolean, 0, 0}, {"Post2010Prop56", K::Real, 0, 0},    {"Post2010Prop57", K::Real, 0, 0},
};

const PropertyInfo& info(VisualStyleProperty p) { return kProperties[static_cast<size_t>(p)]; }

} // namespace

VisualStyleValueKind visual_style_property_kind(VisualStyleProperty property) { return info(property).kind; }
const char* visual_style_property_name(VisualStyleProperty property) { return info(property).name; }
int visual_style_property_code(VisualStyleProperty property) { return info(property).code; }

namespace internal {

const char* const kVisualStyleRoundTripFirst = "RTVSPost2010Prop28";

int visual_style_true_color_code(VisualStyleProperty property) { return info(property).true_color_code; }

VisualStyleValue default_visual_style_value(VisualStyleProperty property) {
    VisualStyleValue v;
    v.kind = info(property).kind;
    if (v.kind == VisualStyleValueKind::Color) v.color = Color::from_aci(7);
    return v;
}

namespace {

bool same_double(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }

bool same_value(const VisualStyleValue& a, const VisualStyleValue& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case VisualStyleValueKind::Integer: return a.integer == b.integer;
        case VisualStyleValueKind::Real:    return same_double(a.real, b.real);
        case VisualStyleValueKind::Boolean: return a.boolean == b.boolean;
        case VisualStyleValueKind::Text:    return a.text == b.text;
        case VisualStyleValueKind::Color:
            return a.color.aci == b.color.aci && a.color.rgb == b.color.rgb && a.color.name == b.color.name &&
                   a.color_tag == b.color_tag;
    }
    return false;
}

bool same_groups(const std::vector<DxfGroup>& a, const std::vector<DxfGroup>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].code != b[i].code || a[i].value != b[i].value) return false;
    }
    return true;
}

} // namespace

bool visual_style_equal(const VisualStyleData& a, const VisualStyleData& b) {
    if (a.form != b.form || a.description != b.description || a.style_type != b.style_type ||
        a.format_version != b.format_version || a.internal_use != b.internal_use ||
        a.edge_style_apply != b.edge_style_apply || a.extra_operations != b.extra_operations ||
        a.properties.size() != b.properties.size() || !same_groups(a.trailing, b.trailing)) {
        return false;
    }
    if (a.trailing_real.has_value() != b.trailing_real.has_value() ||
        (a.trailing_real && !same_double(*a.trailing_real, *b.trailing_real))) {
        return false;
    }
    for (size_t i = 0; i < a.properties.size(); ++i) {
        const VisualStyleEntry& x = a.properties[i];
        const VisualStyleEntry& y = b.properties[i];
        if (x.property != y.property || x.operation != y.operation || !same_value(x.value, y.value)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

namespace {

// A property of the two older forms: the value's group(s), when the next group is its. False when it is not there
bool read_old_value(GroupCursor& c, VisualStyleProperty id, VisualStyleValue& v) {
    const PropertyInfo& p = info(id);
    v = default_visual_style_value(id);
    switch (p.kind) {
        case K::Integer: return c.integer(p.code, v.integer);
        case K::Real:    return c.real(p.code, v.real);
        case K::Boolean: return c.boolean(p.code, v.boolean);
        case K::Text:    return c.string(p.code, v.text);
        case K::Color: {
            // 62 first in AutoCAD's older files, for the mono color only
            const DxfGroup* first = c.peek(0);
            const DxfGroup* second = c.peek(1);
            bool tagged = id == VisualStyleProperty::FaceMonoColor && first && first->code == 62 && second &&
                          second->code == p.code;
            if (!tagged && c.code() != p.code) return false;
            if (tagged) {
                int16_t tag = 0;
                c.integer(62, tag);
                v.color_tag = tag;
            }
            int aci = 0, rgb = 0;
            c.integer(p.code, aci);
            v.color = Color::from_aci(aci);
            if (p.true_color_code != 0 && c.integer(p.true_color_code, rgb)) v.color.rgb = rgb;
            return c.ok();
        }
    }
    return false;
}

// A value of the newest form, which is positional: its group is the kind's
bool read_new_value(GroupCursor& c, VisualStyleProperty id, VisualStyleValue& v, const std::string& what) {
    const PropertyInfo& p = info(id);
    v = default_visual_style_value(id);
    bool got = false;
    switch (p.kind) {
        case K::Integer: got = c.integer(90, v.integer); break;
        case K::Real:    got = c.real(40, v.real); break;
        case K::Boolean: got = c.boolean(290, v.boolean); break;
        case K::Text:    got = c.string(1, v.text); break;
        case K::Color: {
            int aci = 0, rgb = 0;
            got = c.integer(62, aci);
            v.color = Color::from_aci(aci);
            if (got && c.integer(420, rgb)) v.color.rgb = rgb;
            break;
        }
    }
    if (!got && c.ok()) {
        const DxfGroup* g = c.peek();
        c.fail(what + ": the value of property " + std::to_string(static_cast<size_t>(id)) + " (" + p.name + ") is " +
               (g ? "group " + std::to_string(g->code) : std::string("missing")) + ", not the " +
               (p.kind == K::Integer ? "90" : p.kind == K::Real ? "40" : p.kind == K::Boolean ? "290" : p.kind == K::Text ? "1" : "62") +
               " its kind has");
    }
    return got && c.ok();
}

} // namespace

bool read_visual_style_subclass(const RecordView& view, TypedReadContext& ctx, VisualStyleData& d, const std::string& what) {
    GroupCursor c(view, "AcDbVisualStyle");
    if (!c.valid()) return ctx.fail(what + " has no AcDbVisualStyle subclass");
    d = VisualStyleData();
    c.require_string(2, d.description);
    c.require_integer(70, d.style_type);
    int16_t version = 0;
    if (c.integer(177, version)) {
        d.format_version = version;
        if (version == 2) d.form = VisualStyleForm::Form2010;
        else if (version == 3) d.form = VisualStyleForm::Form2013;
        else return ctx.fail(what + ": the form " + std::to_string(version) + " (177) is not one of the two HDxf knows (2, 3)");
        c.require_boolean(291, d.internal_use);
    } else {
        d.form = VisualStyleForm::Form2007;
    }
    if (!c.ok()) return ctx.fail(c.error());

    if (d.form == VisualStyleForm::Form2013) {
        size_t n = 0;
        if (!c.count(70, 1, n)) return ctx.fail(c.ok() ? what + ": the number of properties (70) is missing" : c.error());
        if (n > kVisualStylePropertyCount) {
            return ctx.fail(what + ": " + std::to_string(n) + " properties (70), more than the " +
                            std::to_string(kVisualStylePropertyCount) + " HDxf knows");
        }
        for (size_t id = 0; id < n; ++id) {
            VisualStyleEntry e;
            e.property = static_cast<VisualStyleProperty>(id);
            if (!read_new_value(c, e.property, e.value, what)) return ctx.fail(c.ok() ? what : c.error());
            int16_t op = 0;
            if (c.integer(176, op)) e.operation = op;
            d.properties.push_back(std::move(e));
        }
    } else {
        for (size_t id = 0; id < kVisualStyleOldPropertyCount; ++id) {
            VisualStyleEntry e;
            e.property = static_cast<VisualStyleProperty>(id);
            if (read_old_value(c, e.property, e.value)) {
                if (d.form == VisualStyleForm::Form2010) {
                    int16_t op = 0;
                    if (c.integer(176, op)) e.operation = op;
                }
                d.properties.push_back(std::move(e));
            }
            if (!c.ok()) break;
            if (d.form == VisualStyleForm::Form2007 && id == static_cast<size_t>(VisualStyleProperty::EdgeHidePrecision)) {
                int16_t apply = 0;
                if (c.integer(174, apply)) d.edge_style_apply = apply;
            }
        }
        if (d.form == VisualStyleForm::Form2007) {
            bool internal_use = false;
            if (c.boolean(291, internal_use)) d.internal_use = internal_use;
            double v = 0.0;
            if (c.real(45, v)) d.trailing_real = v;
        }
    }
    if (!c.ok()) return ctx.fail(c.error());
    if (!c.at_end()) {
        d.trailing = c.take_rest();
        ctx.warn(what + ": " + std::to_string(d.trailing.size()) +
                 " group(s) after the ones HDxf knows (subclass AcDbVisualStyle) are kept as they were read");
    }
    return true;
}

// ---------------------------------------------------------------------------
// The XRECORD of the older forms
// ---------------------------------------------------------------------------

namespace {

bool parse_int(const std::string& s, int64_t& out) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == ' ') ++b;
    while (e > b && s[e - 1] == ' ') --e;
    if (b == e) return false;
    char* end = nullptr;
    std::string t = s.substr(b, e - b);
    out = std::strtoll(t.c_str(), &end, 10);
    return end && *end == '\0';
}

bool parse_real(const std::string& s, double& out) {
    std::string t = s;
    char* end = nullptr;
    out = std::strtod(t.c_str(), &end);
    return end && end != t.c_str() && *end == '\0';
}

constexpr int64_t kNoRgb = -16777216;       // 0xFF000000: the XRECORD's "no true color"

std::string prop_name(size_t id) { return "RTVSPost2010Prop" + std::to_string(id); }

} // namespace

bool decode_visual_style_roundtrip(const std::vector<DxfGroup>& g, VisualStyleData& style) {
    std::vector<VisualStyleEntry> post;
    std::vector<std::optional<int16_t>> legacy(kVisualStylePropertyCount);
    std::vector<int16_t> extra;
    size_t i = 0;
    auto marker = [&](const std::string& name) {
        return i < g.size() && g[i].code == 102 && g[i].value == name;
    };
    // One value group of `code`; the groups after the marker
    auto take_int = [&](int code, int64_t& out) {
        if (i >= g.size() || g[i].code != code || !parse_int(g[i].value, out)) return false;
        ++i;
        return true;
    };
    for (size_t id = kVisualStyleOldPropertyCount; id < kVisualStylePropertyCount; ++id) {
        VisualStyleProperty p = static_cast<VisualStyleProperty>(id);
        VisualStyleEntry e;
        e.property = p;
        e.value = default_visual_style_value(p);
        int64_t n = 0;
        if (info(p).kind == K::Color) {
            if (!marker(prop_name(id) + "ColorIndex")) break;
            ++i;
            int64_t aci = 0, rgb = 0;
            if (!take_int(90, aci)) return false;
            if (!marker(prop_name(id) + "ColorRGB")) return false;
            ++i;
            if (!take_int(90, rgb)) return false;
            e.value.color = Color::from_aci(static_cast<int>(aci));
            e.value.color.rgb = rgb == kNoRgb ? -1 : static_cast<int>(rgb);
        } else {
            if (!marker(prop_name(id))) break;
            ++i;
            switch (info(p).kind) {
                case K::Boolean:
                    if (!take_int(280, n)) return false;
                    e.value.boolean = n != 0;
                    break;
                case K::Integer:
                    if (!take_int(90, n)) return false;
                    e.value.integer = static_cast<int32_t>(n);
                    break;
                case K::Real:
                    if (i >= g.size() || g[i].code != 140 || !parse_real(g[i].value, e.value.real)) return false;
                    ++i;
                    break;
                case K::Text:
                    if (i >= g.size() || g[i].code != 1) return false;
                    e.value.text = g[i++].value;
                    break;
                case K::Color:
                    return false;
            }
        }
        if (!marker("RTVSPost2010PropOp" + std::to_string(id))) return false;
        ++i;
        if (!take_int(70, n)) return false;
        e.operation = static_cast<int16_t>(n);
        post.push_back(std::move(e));
    }
    // The operations of every property (the 2007 form), the first 58 are the properties', the rest have no value
    for (size_t id = 0; marker("RTVSPropertyOp" + std::to_string(id)); ++id) {
        ++i;
        int64_t n = 0;
        if (!take_int(70, n)) return false;
        if (id < kVisualStylePropertyCount) legacy[id] = static_cast<int16_t>(n);
        else extra.push_back(static_cast<int16_t>(n));
    }
    if (i != g.size()) return false;

    // Commit: the properties 28 … 57 as the XRECORD has them, the operations of 0 … 27 where it has them
    style.properties.erase(std::remove_if(style.properties.begin(), style.properties.end(), [](const VisualStyleEntry& e) {
        return static_cast<size_t>(e.property) >= kVisualStyleOldPropertyCount;
    }), style.properties.end());
    for (VisualStyleEntry& e : post) style.properties.push_back(std::move(e));
    for (VisualStyleEntry& e : style.properties) {
        size_t id = static_cast<size_t>(e.property);
        if (id < kVisualStyleOldPropertyCount && legacy[id]) e.operation = legacy[id];
    }
    style.extra_operations = std::move(extra);
    return true;
}

std::vector<DxfGroup> encode_visual_style_roundtrip(const VisualStyleData& style) {
    std::vector<DxfGroup> out;
    auto put = [&](int code, const std::string& value) { out.push_back({code, value, 0}); };
    auto number = [](double v) { return format_double(v, NumberStyle::AutoCad); };
    bool any = false;
    for (const VisualStyleEntry& e : style.properties) {
        size_t id = static_cast<size_t>(e.property);
        if (id < kVisualStyleOldPropertyCount) continue;
        any = true;
        const VisualStyleValue& v = e.value;
        switch (info(e.property).kind) {
            case K::Boolean:
                put(102, prop_name(id));
                put(280, v.boolean ? "1" : "0");
                break;
            case K::Integer:
                put(102, prop_name(id));
                put(90, std::to_string(v.integer));
                break;
            case K::Real:
                put(102, prop_name(id));
                put(140, number(v.real));
                break;
            case K::Text:
                put(102, prop_name(id));
                put(1, v.text);
                break;
            case K::Color:
                put(102, prop_name(id) + "ColorIndex");
                put(90, std::to_string(v.color.aci));
                put(102, prop_name(id) + "ColorRGB");
                put(90, std::to_string(v.color.has_rgb() ? v.color.rgb : kNoRgb));
                break;
        }
        put(102, "RTVSPost2010PropOp" + std::to_string(id));
        put(70, std::to_string(e.operation.value_or(kDefaultVisualStyleOperation)));
    }
    if (!any) return {};
    if (style.form == VisualStyleForm::Form2007) {
        // The operation of every property, the extra ones last
        for (size_t id = 0; id < kVisualStylePropertyCount; ++id) {
            const VisualStyleEntry* e = style.find(static_cast<VisualStyleProperty>(id));
            if (!e) continue;
            put(102, "RTVSPropertyOp" + std::to_string(id));
            put(70, std::to_string(e->operation.value_or(kDefaultVisualStyleOperation)));
        }
        for (size_t k = 0; k < style.extra_operations.size(); ++k) {
            put(102, "RTVSPropertyOp" + std::to_string(kVisualStylePropertyCount + k));
            put(70, std::to_string(style.extra_operations[k]));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// The codec
// ---------------------------------------------------------------------------

namespace {

bool read_visual_style(const RecordView& view, TypedReadContext& ctx, Object& o) {
    VisualStyleData d;
    if (!read_visual_style_subclass(view, ctx, d, "VISUALSTYLE " + o.handle)) return false;
    o.data = Boxed<VisualStyleData>(std::move(d));
    return true;
}

} // namespace

void register_visual_style_codec(TypedRegistry& registry) {
    ObjectCodec c;
    c.dxf_name = "VISUALSTYLE";
    c.type = ObjectType::VisualStyle;
    c.read = read_visual_style;
    c.write = write_visual_style;
    c.cls.cpp_name = "AcDbVisualStyle";
    c.cls.application = "ObjectDBX Classes";
    c.cls.proxy_flags = 4095;
    // The groups mean different things where they stand (a property's code is the next one's), so a changed style has
    // its subclass written whole
    ListSpec whole;
    whole.subclass = "AcDbVisualStyle";
    whole.shape = ListShape::WholeSubclass;
    c.lists = {whole};
    c.versions = VersionMatrix::native_from(DxfVersion::R2000);
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        c.versions.autocad(v, AutoCadStatus::Verified);
    }
    c.step = 7;
    registry.add(std::move(c));
}

} // namespace internal

// ---------------------------------------------------------------------------
// The XRECORD in the drawing
// ---------------------------------------------------------------------------

namespace {

// The XRECORD of a style's extension dictionary that has the form of AutoCAD's visual style round trip
Object* style_record(Drawing& dwg, const Object& style, std::string* dictionary_handle = nullptr) {
    if (style.xdictionary.empty()) return nullptr;
    Object* dict = dwg.find_object(style.xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    if (dictionary_handle) *dictionary_handle = dict->handle;
    for (const auto& e : dict->as_dictionary().entries) {
        if (e.name != "ACAD_XREC_ROUNDTRIP") continue;
        Object* x = dwg.find_object(e.handle);
        if (x && x->type == ObjectType::XRecord && !x->as_xrecord().data.empty() &&
            x->as_xrecord().data[0].code == 102 && x->as_xrecord().data[0].value == internal::kVisualStyleRoundTripFirst) {
            return x;
        }
    }
    return nullptr;
}

} // namespace

// What the older forms keep in the XRECORD, read into the style: the properties 28 … 57 and their operations, and in the
// 2007 form the operations of the others. An XRECORD that does not have exactly that form is left as it is
void Drawing::read_visual_style_records() {
    for (Object& o : objects_) {
        if (o.type != ObjectType::VisualStyle) continue;
        VisualStyleData& d = o.as_visual_style();
        if (d.form == VisualStyleForm::Form2013) continue;
        const Object* x = style_record(*this, o);
        if (x) internal::decode_visual_style_roundtrip(x->as_xrecord().data, d);
    }
}

// The XRECORD says what the style has (B12): made for a style that needs one and has none, brought up to date when the
// style's properties changed (the same XRECORD, never a second), removed when the style no longer has a property to keep
// in it; one that says the same stays as it was read. A style of the newest form has none.
void Drawing::sync_visual_style_records() {
    std::vector<std::string> styles;
    for (const Object& o : objects_) {
        if (o.type == ObjectType::VisualStyle && o.as_visual_style().form != VisualStyleForm::Form2013 && !o.handle.empty()) {
            styles.push_back(o.handle);
        }
    }
    bool scanned = false;
    auto remove_object = [&](const std::string& handle) {
        objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&](const Object& o) { return o.handle == handle; }),
                       objects_.end());
    };
    for (const std::string& handle : styles) {
        Object* o = find_object(handle);
        if (!o) continue;
        const VisualStyleData& d = o->as_visual_style();
        std::vector<DxfGroup> desired = internal::encode_visual_style_roundtrip(d);
        Object* record = style_record(*this, *o);
        if (record) {
            // One that is not in AutoCAD's form is left alone (not ours to change)
            VisualStyleData now = d;
            if (!internal::decode_visual_style_roundtrip(record->as_xrecord().data, now)) continue;
            if (internal::visual_style_equal(now, d)) continue;
            if (!desired.empty()) {
                record->as_xrecord().data = desired;
                continue;
            }
            std::string record_handle = record->handle;
            std::string xdictionary = o->xdictionary;
            remove_object(record_handle);
            Object* dict = find_object(xdictionary);
            if (dict && dict->type == ObjectType::Dictionary) {
                auto& entries = dict->as_dictionary().entries;
                entries.erase(std::remove_if(entries.begin(), entries.end(),
                                             [&](const DictionaryEntry& e) { return e.handle == record_handle; }),
                              entries.end());
                if (entries.empty() && dict->xdata.empty() && dict->xdictionary.empty()) {
                    remove_object(xdictionary);
                    find_object(handle)->xdictionary.clear();
                }
            }
            continue;
        }
        if (desired.empty()) continue;
        if (!scanned) {
            scan_handles();
            scanned = true;
        }
        // The extension dictionary (hard-owned by the style, no reactors) and its XRECORD (the dictionary as owner and
        // reactor), as the standard styles have them
        std::string xdictionary = o->xdictionary;
        Object* dict = xdictionary.empty() ? nullptr : find_object(xdictionary);
        if (dict && dict->type != ObjectType::Dictionary) continue;     // not a dictionary: left alone
        if (!dict) {
            Object dictionary;
            dictionary.type = ObjectType::Dictionary;
            dictionary.handle = next_handle();
            dictionary.owner = handle;
            DictionaryData dd;
            dd.hard_owner = true;
            dictionary.data = dd;
            xdictionary = dictionary.handle;
            objects_.push_back(std::move(dictionary));
        }
        Object roundtrip;
        roundtrip.type = ObjectType::XRecord;
        roundtrip.handle = next_handle();
        roundtrip.owner = xdictionary;
        roundtrip.reactors = {xdictionary};
        XRecordData x;
        x.data = desired;
        roundtrip.data = x;
        std::string record_handle = roundtrip.handle;
        objects_.push_back(std::move(roundtrip));
        find_object(xdictionary)->as_dictionary().entries.push_back({"ACAD_XREC_ROUNDTRIP", record_handle, true});
        find_object(handle)->xdictionary = xdictionary;
    }
}

} // namespace hdxf
