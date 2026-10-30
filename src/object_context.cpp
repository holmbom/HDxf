#include "ContextDimensionTypes.hpp"
#include "DynamicBlockTypes.hpp"
#include "mleader.hpp"
#include "dxf_writer.hpp"
#include "object_data_visit.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// The annotation contexts of step 9.17 (phase 9; plan 2026-10-27-fas-9-typer.md, B100-B107, B112-B116): the 16 context classes
// (ObjectContext/AnnotScale bases and the concrete ones), their codecs, and the Drawing API that keeps the scale list, the
// entity's extension dictionary chain and its contexts together.
//
// Every context is AcDbObjectContextData (70, 290), AcDbAnnotScaleObjectContextData (340 and, for the classes that have no
// subclass of their own, their groups) and the class's own subclasses, as AutoCAD 2025 writes them (doc/facit/fas9_contexts.scr
// and Erik's acad_<version>_dxf_annotative*.dxf: the same form in 2000, 2004, 2007, 2010, 2013 and 2018). Only the 340 (the scale)
// and, in the classes that have them, the style/block pointers are references: the 70 of the base, the 70 of a LEADER's
// vertex count and every other number are numbers.

namespace hdxf {

namespace {

bool same_handle(const std::string& a, const std::string& b) {
    auto strip = [](const std::string& s) {
        size_t i = 0;
        while (i + 1 < s.size() && s[i] == '0') ++i;
        std::string out = s.substr(i);
        for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return out;
    };
    return !a.empty() && !b.empty() && strip(a) == strip(b);
}

// The data is the alternative of the context type (a TEXT context holds a TextContextData)
bool context_data_fits(ObjectType type, const ObjectData& data) {
    switch (type) {
        case ObjectType::TextContext: return data.get_if<Boxed<TextContextData>>() != nullptr;
        case ObjectType::LeaderContext: return data.get_if<Boxed<LeaderContextData>>() != nullptr;
        case ObjectType::MTextContext: return data.get_if<Boxed<MTextContextData>>() != nullptr;
        case ObjectType::MTextAttributeContext: return data.get_if<Boxed<MTextAttributeContextData>>() != nullptr;
        case ObjectType::MLeaderContext: return data.get_if<Boxed<MLeaderContextData>>() != nullptr;
        case ObjectType::BlkRefContext: return data.get_if<Boxed<BlkRefContextData>>() != nullptr;
        case ObjectType::AlDimContext: return data.get_if<Boxed<AlDimContextData>>() != nullptr;
        case ObjectType::AngDimContext: return data.get_if<Boxed<AngDimContextData>>() != nullptr;
        case ObjectType::DmDimContext: return data.get_if<Boxed<DmDimContextData>>() != nullptr;
        case ObjectType::OrdDimContext: return data.get_if<Boxed<OrdDimContextData>>() != nullptr;
        case ObjectType::RadimContext: return data.get_if<Boxed<RadimContextData>>() != nullptr;
        case ObjectType::RadimLgContext: return data.get_if<Boxed<RadimLgContextData>>() != nullptr;
        case ObjectType::FcfContext: return data.get_if<Boxed<FcfContextData>>() != nullptr;
        case ObjectType::AnnotScaleContext: return data.get_if<Boxed<AnnotScaleContextData>>() != nullptr;
        case ObjectType::HatchScaleContext: return data.get_if<Boxed<HatchScaleContextData>>() != nullptr;
        case ObjectType::HatchViewContext: return data.get_if<Boxed<HatchViewContextData>>() != nullptr;
        default: return false;
    }
}

bool is_context_type(ObjectType t) {
    switch (t) {
        case ObjectType::TextContext: case ObjectType::LeaderContext: case ObjectType::MTextContext:
        case ObjectType::MTextAttributeContext: case ObjectType::MLeaderContext: case ObjectType::BlkRefContext:
        case ObjectType::AlDimContext: case ObjectType::AngDimContext: case ObjectType::DmDimContext:
        case ObjectType::OrdDimContext: case ObjectType::RadimContext: case ObjectType::RadimLgContext:
        case ObjectType::FcfContext: case ObjectType::AnnotScaleContext: case ObjectType::HatchScaleContext:
        case ObjectType::HatchViewContext:
            return true;
        default:
            return false;
    }
}

template <typename T>
ObjectContextBase* base_in(T& value) {
    if constexpr (requires { static_cast<ObjectContextBase*>(&(*value).base); }) return &(*value).base;
    else return nullptr;
}

const char* kContextManager = "AcDbContextDataManager";
const char* kAnnotationScales = "ACDB_ANNOTATIONSCALES";

// The entry `name` of a dictionary: the object it points at
Object* entry_object(Drawing& dwg, const std::string& dictionary, const char* name) {
    Object* d = dictionary.empty() ? nullptr : dwg.find_object(dictionary);
    if (!d || d->type != ObjectType::Dictionary) return nullptr;
    for (const DictionaryEntry& e : d->as_dictionary().entries) {
        if (e.name == name) return dwg.find_object(e.handle);
    }
    return nullptr;
}

} // namespace

ObjectContextBase* Object::context_base() {
    return internal::visit_object_data([](auto& value) { return base_in(value); }, data);
}

const ObjectContextBase* Object::context_base() const {
    return const_cast<Object*>(this)->context_base();
}

// ---------------------------------------------------------------------------
// Drawing: the scale list, the entity's context dictionaries and its contexts
// ---------------------------------------------------------------------------

std::string Drawing::entity_handle(Entity& entity) {
    if (entity.props.handle.empty()) {
        scan_handles();
        entity.props.handle = next_handle();
    }
    return entity.props.handle;
}

Object* Drawing::add_annotation_scale(const std::string& name, double paper_units, double drawing_units) {
    if (version_ < DxfVersion::R2000) return nullptr;
    if (const Object* existing = find_scale(name)) return const_cast<Object*>(existing);
    ensure_r2000_structure();
    scan_handles();
    const std::string dictionary = named_dictionary("ACAD_SCALELIST");
    Object* dict = find_object(dictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    Object o;
    o.type = ObjectType::Scale;
    o.handle = next_handle();
    o.owner = dictionary;
    o.reactors = {dictionary};
    ScaleData s;
    s.name = name;
    s.paper_units = paper_units;
    s.drawing_units = drawing_units;
    s.unit_scale = paper_units == drawing_units;
    o.data = s;
    const std::string handle = o.handle;
    objects_.push_back(std::move(o));
    dict = find_object(dictionary);
    // A0, A1 … as AutoCAD names them; a name a removed scale left is not taken twice
    auto& entries = dict->as_dictionary().entries;
    size_t n = entries.size();
    auto taken = [&](const std::string& name) {
        return std::any_of(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return e.name == name; });
    };
    while (taken("A" + std::to_string(n))) ++n;
    entries.push_back({"A" + std::to_string(n), handle, false});
    return find_object(handle);
}

const Object* Drawing::find_scale(const std::string& name_or_handle) const {
    for (const Object& o : objects_) {
        if (o.type != ObjectType::Scale) continue;
        if (o.as_scale().name == name_or_handle || same_handle(o.handle, name_or_handle)) return &o;
    }
    return nullptr;
}

namespace {

// The extension dictionary of the entity or object `owner` (its handle), when it has one
std::string xdictionary_of(const Drawing& dwg, const std::string& owner) {
    if (const Entity* e = dwg.find_entity(owner)) return e->props.xdictionary;
    if (const Object* o = dwg.find_object(owner)) return o->xdictionary;
    return {};
}

} // namespace

std::vector<const Object*> Drawing::object_contexts(const std::string& owner_handle) const {
    std::vector<const Object*> out;
    Drawing& self = const_cast<Drawing&>(*this);
    Object* manager = entry_object(self, xdictionary_of(*this, owner_handle), kContextManager);
    Object* scales = manager ? entry_object(self, manager->handle, kAnnotationScales) : nullptr;
    Object* list = scales ? entry_object(self, scales->handle, kAnnotationScales) : nullptr;
    // AutoCAD: AcDbContextDataManager → ACDB_ANNOTATIONSCALES (a dictionary that has one entry, itself named so) → the
    // *A<n> entries. Both shapes are read: the contexts are the entries of the last dictionary
    Object* holder = list ? list : scales;
    if (!holder || holder->type != ObjectType::Dictionary) return out;
    for (const DictionaryEntry& e : holder->as_dictionary().entries) {
        const Object* o = find_object(e.handle);
        if (o && is_context_type(o->type)) out.push_back(o);
    }
    return out;
}

const Object* Drawing::object_context(const std::string& owner_handle, const std::string& scale_handle) const {
    for (const Object* o : object_contexts(owner_handle)) {
        const ObjectContextBase* b = o->context_base();
        if (b && same_handle(b->scale, scale_handle)) return o;
    }
    return nullptr;
}

Object* Drawing::object_context(const std::string& owner_handle, const std::string& scale_handle) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->object_context(owner_handle, scale_handle));
}

Object* Drawing::add_object_context(const std::string& owner_handle, const std::string& scale_handle, ObjectType type,
                                    ObjectData data) {
    // Checked before anything changes: the data must be the context type's own
    if (!is_context_type(type) || !context_data_fits(type, data)) return nullptr;
    Entity* entity = const_cast<Entity*>(find_entity(owner_handle));
    Object* owner_object = entity ? nullptr : find_object(owner_handle);
    if (!entity && !owner_object) return nullptr;
    const Object* scale = find_scale(scale_handle);
    if (!scale) return nullptr;
    const std::string scale_key = scale->handle;
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    scan_handles();
    entity = entity ? const_cast<Entity*>(find_entity(owner_handle)) : nullptr;
    owner_object = entity ? nullptr : find_object(owner_handle);

    // The chain: the extension dictionary → AcDbContextDataManager → ACDB_ANNOTATIONSCALES (one entry, itself named so) → *A<n>
    auto make_dictionary = [&](const std::string& owner, bool hard, bool owner_reactor) {
        Object d;
        d.type = ObjectType::Dictionary;
        d.handle = next_handle();
        d.owner = owner;
        if (owner_reactor) d.reactors = {owner};
        DictionaryData dd;
        dd.hard_owner = hard;
        d.data = dd;
        objects_.push_back(std::move(d));
        return objects_.back().handle;
    };
    auto add_entry = [&](const std::string& dictionary, const std::string& name, const std::string& target, bool hard) {
        Object* d = find_object(dictionary);
        d->as_dictionary().entries.push_back({name, target, hard});
    };
    // (by handle: a new dictionary may move the objects, and the owner's xdictionary with them)
    std::string xdict_handle = entity ? entity->props.xdictionary : owner_object->xdictionary;
    if (xdict_handle.empty() || !find_object(xdict_handle)) {
        xdict_handle = make_dictionary(owner_handle, true, false);
        if (entity) entity->props.xdictionary = xdict_handle;
        else find_object(owner_handle)->xdictionary = xdict_handle;
    }
    Object* manager = entry_object(*this, xdict_handle, kContextManager);
    if (!manager) {
        const std::string h = make_dictionary(xdict_handle, false, true);
        add_entry(xdict_handle, kContextManager, h, true);
        manager = find_object(h);
    }
    const std::string manager_handle = manager->handle;
    Object* scales = entry_object(*this, manager_handle, kAnnotationScales);
    if (!scales) {
        const std::string h = make_dictionary(manager_handle, false, true);
        add_entry(manager_handle, kAnnotationScales, h, false);
        scales = find_object(h);
    }
    const std::string scales_handle = scales->handle;
    // An existing context for the scale is replaced, and stays the default one if it was
    bool replaced_default = false;
    if (Object* existing = object_context(owner_handle, scale_key)) {
        const std::string old = existing->handle;
        replaced_default = existing->context_base() && existing->context_base()->is_default;
        Object* d = find_object(scales_handle);
        auto& entries = d->as_dictionary().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return same_handle(e.handle, old); }),
                      entries.end());
        objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&](const Object& o) { return same_handle(o.handle, old); }),
                       objects_.end());
    }
    const bool first = replaced_default || object_contexts(owner_handle).empty();
    Object o;
    o.type = type;
    o.handle = next_handle();
    o.owner = scales_handle;
    o.reactors = {scales_handle};
    o.data = std::move(data);
    ObjectContextBase* base = o.context_base();
    if (!base) return nullptr;
    base->scale = scale_key;
    if (first) base->is_default = true;
    // The next *A<n>
    int n = 0;
    for (const DictionaryEntry& e : find_object(scales_handle)->as_dictionary().entries) {
        if (e.name.size() > 2 && e.name[0] == '*' && e.name[1] == 'A') n = std::max(n, std::atoi(e.name.c_str() + 2));
    }
    const std::string name = "*A" + std::to_string(n + 1);
    const std::string handle = o.handle;
    objects_.push_back(std::move(o));
    add_entry(scales_handle, name, handle, false);
    // The entity says it is annotative (AutoCAD's XDATA AcadAnnotative: AnnotativeData { version 1, annotative 1 })
    if (entity) {
        auto& xdata = entity->props.xdata;
        if (std::none_of(xdata.begin(), xdata.end(), [](const XdataList& x) { return x.app_name == "AcadAnnotative"; })) {
            xdata.push_back({"AcadAnnotative", {{1000, "AnnotativeData"}, {1002, "{"}, {1070, "1"}, {1070, "1"}, {1002, "}"}}});
        }
    }
    return find_object(handle);
}

bool Drawing::remove_object_context(const std::string& owner_handle, const std::string& scale_handle) {
    const Object* scale = find_scale(scale_handle);
    const std::string key = scale ? scale->handle : scale_handle;
    Object* context = object_context(owner_handle, key);
    if (!context) return false;
    const std::string handle = context->handle;
    if (Object* dict = find_object(context->owner); dict && dict->type == ObjectType::Dictionary) {
        auto& entries = dict->as_dictionary().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return same_handle(e.handle, handle); }),
                      entries.end());
    }
    objects_.erase(std::remove_if(objects_.begin(), objects_.end(), [&](const Object& o) { return same_handle(o.handle, handle); }),
                   objects_.end());
    return true;
}

bool Drawing::remove_annotation_scale(const std::string& scale_handle) {
    const Object* scale = find_scale(scale_handle);
    if (!scale) return false;
    const std::string key = scale->handle;
    // Every context that points at it, wherever its entity is
    std::vector<std::string> doomed;
    for (const Object& o : objects_) {
        const ObjectContextBase* b = o.context_base();
        if (b && same_handle(b->scale, key)) doomed.push_back(o.handle);
    }
    for (const std::string& h : doomed) {
        Object* o = find_object(h);
        if (!o) continue;
        if (Object* dict = find_object(o->owner); dict && dict->type == ObjectType::Dictionary) {
            auto& entries = dict->as_dictionary().entries;
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return same_handle(e.handle, h); }),
                          entries.end());
        }
    }
    doomed.push_back(key);
    // The scale list's own entry
    for (Object& o : objects_) {
        if (o.type != ObjectType::Dictionary) continue;
        auto& entries = o.as_dictionary().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return same_handle(e.handle, key); }),
                      entries.end());
    }
    objects_.erase(std::remove_if(objects_.begin(), objects_.end(),
                                  [&](const Object& o) {
                                      return std::any_of(doomed.begin(), doomed.end(), [&](const std::string& h) { return same_handle(o.handle, h); });
                                  }),
                   objects_.end());
    return true;
}

// ---------------------------------------------------------------------------
// The codecs
// ---------------------------------------------------------------------------

namespace internal {

namespace {

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// AcDbObjectContextData (70, 290), then AcDbAnnotScaleObjectContextData: 340 and what `own` reads of the groups after it
bool read_context(const RecordView& view, TypedReadContext& ctx, ObjectContextBase& base,
                  const std::function<void(GroupCursor&)>& own) {
    if (!read_subclass(view, ctx, "AcDbObjectContextData", [&](GroupCursor& c) {
            c.require_integer(70, base.version);
            c.require_boolean(290, base.is_default);
        })) {
        return false;
    }
    return read_subclass(view, ctx, "AcDbAnnotScaleObjectContextData", [&](GroupCursor& c) {
        c.require_handle(340, base.scale);
        if (c.ok() && own) own(c);
    });
}

void read_dim_common(GroupCursor& c, DimContextCommon& d) {
    c.string(2, d.block_name);
    c.boolean(293, d.flag_293);
    c.point2(10, d.text_location);
    c.boolean(294, d.flag_294);
    c.real(140, d.value_140);
    c.boolean(298, d.flag_298);
    c.boolean(291, d.flag_291);
    c.integer(70, d.value_70);
    c.boolean(292, d.flag_292);
    c.integer(71, d.value_71);
    c.integer(280, d.value_280);
    c.boolean(295, d.flag_295);
    c.boolean(296, d.flag_296);
    c.boolean(297, d.flag_297);
}

// A context of the dimension classes: the common subclass (AcDbDimensionObjectContextData), then the class's own
bool read_dim_context(const RecordView& view, TypedReadContext& ctx, ObjectContextBase& base, DimContextCommon& d) {
    if (!read_context(view, ctx, base, nullptr)) return false;
    return read_subclass(view, ctx, "AcDbDimensionObjectContextData", [&](GroupCursor& c) { read_dim_common(c, d); });
}

bool read_text_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    TextContextData d;
    if (!read_context(view, ctx, d.base, [&](GroupCursor& c) {
            c.require_integer(70, d.flags);
            c.require_real(50, d.rotation);
            c.require_real(10, d.insertion.x);
            c.require_real(20, d.insertion.y);
            c.require_real(11, d.alignment.x);
            c.require_real(21, d.alignment.y);
        })) {
        return false;
    }
    o.data = Boxed<TextContextData>(std::move(d));
    return true;
}

bool read_leader_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    LeaderContextData d;
    if (!read_context(view, ctx, d.base, [&](GroupCursor& c) {
            size_t n = 0;
            if (!c.count(70, 3, n)) {
                if (c.ok()) c.fail("group 70 (the vertex count) is missing");
                return;
            }
            for (size_t i = 0; i < n && c.ok(); ++i) {
                Point3 p;
                c.require_point3(10, p);
                d.points.push_back(p);
            }
            c.require_point3(11, d.direction);
            c.require_boolean(290, d.flag_290);
            c.require_point3(12, d.offset);
            c.require_point3(13, d.annotation_offset);
        })) {
        return false;
    }
    o.data = Boxed<LeaderContextData>(std::move(d));
    return true;
}

void read_mtext_own(GroupCursor& c, MTextContextData& d) {
    c.require_integer(70, d.flags);
    c.require_point3(10, d.direction);
    c.require_point3(11, d.location);
    c.require_real(40, d.reference_width);
    c.require_real(41, d.defined_height);
    c.require_real(42, d.text_width);
    c.require_real(43, d.text_height);
    c.require_integer(71, d.value_71);
}

bool read_mtext_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MTextContextData d;
    if (!read_context(view, ctx, d.base, [&](GroupCursor& c) { read_mtext_own(c, d); })) return false;
    o.data = Boxed<MTextContextData>(std::move(d));
    return true;
}

bool read_mtext_attribute_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MTextAttributeContextData d;
    if (!read_context(view, ctx, d.base, [&](GroupCursor& c) {
            c.require_integer(70, d.flags);
            c.require_real(50, d.rotation);
            c.require_real(10, d.insertion.x);
            c.require_real(20, d.insertion.y);
            c.require_real(11, d.alignment.x);
            c.require_real(21, d.alignment.y);
            c.require_boolean(290, d.flag_290);
        })) {
        return false;
    }
    // 101 Embedded Object: the MTEXT context, whose own groups are read as a record's
    if (!view.embedded().empty()) {
        const EmbeddedPart& part = view.embedded().front();
        std::vector<DxfGroup> groups;
        groups.push_back({0, "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS", 0});
        for (size_t i = part.first + 1; i < part.last; ++i) groups.push_back(view.groups()[i]);
        RecordView inner;
        std::string error;
        if (!RecordView::parse(std::move(groups), inner, error)) return ctx.fail(describe_record(view) + ": " + error);
        MTextContextData m;
        if (!read_context(inner, ctx, m.base, [&](GroupCursor& c) { read_mtext_own(c, m); })) return false;
        d.embedded = std::move(m);
        if (view.embedded().size() > 1) {
            ctx.warn(describe_record(view) + ": " + std::to_string(view.embedded().size() - 1) +
                     " embedded object(s) after the first are kept as they were read");
        }
    }
    o.data = Boxed<MTextAttributeContextData>(std::move(d));
    return true;
}

bool read_mleader_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MLeaderContextData d;
    std::vector<DxfGroup> rest;
    if (!read_context(view, ctx, d.base, [&](GroupCursor& c) { rest = c.take_rest(); })) return false;
    // What follows is the groups of an MULTILEADER's CONTEXT_DATA (40 … 273), without its markers: read as the entity's are
    std::vector<DxfGroup> groups;
    groups.push_back({300, "CONTEXT_DATA{", 0});
    for (DxfGroup& g : rest) groups.push_back(std::move(g));
    groups.push_back({301, "}", 0});
    MLeaderData parsed;
    parse_mleader(groups, parsed);
    d.content = std::move(parsed.context);
    o.data = Boxed<MLeaderContextData>(std::move(d));
    return true;
}

bool read_blkref_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    BlkRefContextData d;
    if (!read_context(view, ctx, d.base, [&](GroupCursor& c) {
            c.require_real(50, d.rotation);
            c.require_point3(10, d.insertion);
            c.require_real(41, d.scale.x);
            c.require_real(42, d.scale.y);
            c.require_real(43, d.scale.z);
        })) {
        return false;
    }
    o.data = Boxed<BlkRefContextData>(std::move(d));
    return true;
}

bool read_aldim_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AlDimContextData d;
    if (!read_dim_context(view, ctx, d.base, d.dimension)) return false;
    if (!read_subclass(view, ctx, "AcDbAlignedDimensionObjectContextData", [&](GroupCursor& c) { c.require_point3(11, d.dimension_line_point); })) return false;
    o.data = Boxed<AlDimContextData>(std::move(d));
    return true;
}

bool read_angdim_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AngDimContextData d;
    if (!read_dim_context(view, ctx, d.base, d.dimension)) return false;
    if (!read_subclass(view, ctx, "AcDbAngularDimensionObjectContextData", [&](GroupCursor& c) { c.require_point3(11, d.arc_point); })) return false;
    o.data = Boxed<AngDimContextData>(std::move(d));
    return true;
}

bool read_dmdim_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    DmDimContextData d;
    if (!read_dim_context(view, ctx, d.base, d.dimension)) return false;
    if (!read_subclass(view, ctx, "AcDbDiametricDimensionObjectContextData", [&](GroupCursor& c) {
            c.require_point3(11, d.point_11);
            c.require_point3(12, d.point_12);
        })) {
        return false;
    }
    o.data = Boxed<DmDimContextData>(std::move(d));
    return true;
}

bool read_orddim_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    OrdDimContextData d;
    if (!read_dim_context(view, ctx, d.base, d.dimension)) return false;
    if (!read_subclass(view, ctx, "AcDbOrdinateDimensionObjectContextData", [&](GroupCursor& c) {
            c.require_point3(11, d.point_11);
            c.require_point3(12, d.point_12);
        })) {
        return false;
    }
    o.data = Boxed<OrdDimContextData>(std::move(d));
    return true;
}

bool read_radim_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    RadimContextData d;
    if (!read_dim_context(view, ctx, d.base, d.dimension)) return false;
    if (!read_subclass(view, ctx, "AcDbRadialDimensionObjectContextData", [&](GroupCursor& c) { c.require_point3(11, d.point_11); })) return false;
    o.data = Boxed<RadimContextData>(std::move(d));
    return true;
}

bool read_radimlg_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    RadimLgContextData d;
    if (!read_dim_context(view, ctx, d.base, d.dimension)) return false;
    if (!read_subclass(view, ctx, "AcDbRadialDimensionObjectContextData", [&](GroupCursor& c) { c.require_point3(11, d.point_11); })) return false;
    if (!read_subclass(view, ctx, "AcDbRadialDimensionLargeObjectContextData", [&](GroupCursor& c) {
            c.require_point3(12, d.point_12);
            c.require_point3(13, d.point_13);
        })) {
        return false;
    }
    o.data = Boxed<RadimLgContextData>(std::move(d));
    return true;
}

bool read_fcf_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    FcfContextData d;
    if (!read_context(view, ctx, d.base, [&](GroupCursor& c) {
            c.require_point3(10, d.location);
            c.require_point3(11, d.direction);
        })) {
        return false;
    }
    o.data = Boxed<FcfContextData>(std::move(d));
    return true;
}

bool read_annot_scale_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AnnotScaleContextData d;
    if (!read_context(view, ctx, d.base, nullptr)) return false;
    o.data = Boxed<AnnotScaleContextData>(std::move(d));
    return true;
}

bool read_hatch_common(const RecordView& view, TypedReadContext& ctx, HatchContextCommon& h) {
    return read_subclass(view, ctx, "AcDbHatchObjectContextData", [&](GroupCursor& c) {
        size_t n = 0;
        if (!c.count(78, 6, n)) {
            if (c.ok()) c.fail("group 78 (the number of pattern lines) is missing");
            return;
        }
        for (size_t i = 0; i < n && c.ok(); ++i) {
            HatchPatternLine line;
            c.require_real(53, line.angle);
            c.require_real(43, line.base_point.x);
            c.require_real(44, line.base_point.y);
            c.require_real(45, line.offset.x);
            c.require_real(46, line.offset.y);
            size_t dashes = 0;
            if (!c.count(79, 1, dashes)) {
                if (c.ok()) c.fail("group 79 (the number of dashes) is missing");
                return;
            }
            for (size_t k = 0; k < dashes && c.ok(); ++k) {
                double v = 0.0;
                c.require_real(49, v);
                line.dashes.push_back(v);
            }
            h.pattern_lines.push_back(std::move(line));
        }
        c.require_real(40, h.pattern_scale);
        c.require_point3(10, h.base_point);
        c.require_integer(90, h.value_90_a);
        c.require_integer(90, h.value_90_b);
        c.require_boolean(290, h.flag_290);
        // A hatch with a dashed pattern goes on: a 90, a 290 and, in a scale context, the pattern's boundary as line edges
        if (c.ok() && c.code() == 90) {
            HatchContextExtension x;
            c.require_integer(90, x.value_90);
            c.require_boolean(290, x.flag_290);
            if (c.ok() && c.code() == 93) {
                size_t edges = 0;
                c.count(93, 5, edges);
                std::vector<HatchEdge> list;
                for (size_t i = 0; i < edges && c.ok(); ++i) {
                    HatchEdge e;
                    int32_t type = 0;
                    c.require_integer(72, type);
                    if (!c.ok()) break;
                    if (type != static_cast<int32_t>(HatchEdgeType::Line)) {
                        c.fail("a boundary edge of type " + std::to_string(type) + " (72): only line edges are in a file of AutoCAD's");
                        break;
                    }
                    e.type = HatchEdgeType::Line;
                    c.require_real(10, e.start.x);
                    c.require_real(20, e.start.y);
                    c.require_real(11, e.end.x);
                    c.require_real(21, e.end.y);
                    list.push_back(e);
                }
                x.edges = std::move(list);
            }
            h.extension = std::move(x);
        }
    });
}

bool read_hatch_scale_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    HatchScaleContextData d;
    if (!read_context(view, ctx, d.base, nullptr)) return false;
    if (!read_hatch_common(view, ctx, d.hatch)) return false;
    o.data = Boxed<HatchScaleContextData>(std::move(d));
    return true;
}

bool read_hatch_view_context(const RecordView& view, TypedReadContext& ctx, Object& o) {
    HatchViewContextData d;
    if (!read_context(view, ctx, d.base, nullptr)) return false;
    if (!read_hatch_common(view, ctx, d.hatch)) return false;
    if (!read_subclass(view, ctx, "AcDbHatchViewContextData", [&](GroupCursor& c) {
            c.require_handle(330, d.pointer_330);
            c.require_point3(10, d.normal);
            c.require_real(51, d.value_51);
            c.require_boolean(290, d.flag_290);
        })) {
        return false;
    }
    o.data = Boxed<HatchViewContextData>(std::move(d));
    return true;
}

// ---------------------------------------------------------------------------
// References (B19, B104): the scale (340) is a hard pointer; a MULTILEADER context's style and block, a hatch view's 330 are
// pointers. The 70 of the base, a LEADER's vertex count and every other number are never handles
// ---------------------------------------------------------------------------

template <typename Data>
void context_references(const Object& o, std::vector<TypedReference>& out) {
    const std::string& scale = (*o.data.get<Boxed<Data>>()).base.scale;
    if (!scale.empty() && scale != "0") out.push_back({scale, RefKind::HardPointer, false});
    if constexpr (std::is_same_v<Data, MLeaderContextData>) {
        const MLeaderContext& c = (*o.data.get<Boxed<Data>>()).content;
        if (c.has_text && !c.text_style.empty() && c.text_style != "0") out.push_back({c.text_style, RefKind::SoftPointer, false});
        if (c.has_block && !c.block.empty() && c.block != "0") out.push_back({c.block, RefKind::SoftPointer, false});
    }
    if constexpr (std::is_same_v<Data, HatchViewContextData>) {
        const std::string& p = (*o.data.get<Boxed<Data>>()).pointer_330;
        if (!p.empty() && p != "0") out.push_back({p, RefKind::SoftPointer, false});
    }
}

RefDescriptor scale_ref() {
    RefDescriptor d;
    d.subclass = "AcDbAnnotScaleObjectContextData";
    d.occurrence = 0;
    d.code = 340;
    d.ordinal = 0;
    d.kind = RefKind::HardPointer;
    d.removal = RefRemoval::ZeroInPlace;
    return d;
}

RefDescriptor soft_ref(const char* subclass, int code, int ordinal) {
    RefDescriptor d;
    d.subclass = subclass;
    d.occurrence = 0;
    d.code = code;
    d.ordinal = ordinal;
    d.kind = RefKind::SoftPointer;
    d.removal = RefRemoval::ZeroInPlace;
    return d;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

ClassSpec context_class(const char* cpp_name, int proxy_flags) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = proxy_flags;
    c.in_older = false;
    return c;
}

// AutoCAD's files of 2000, 2004, 2007, 2010, 2013 and 2018 (ASCII and binary: Erik's acad_<version>_dxf_annotative*.dxf and
// fas9_contexts.scr) have the contexts with one grammar. R12 has no OBJECTS: the named error; R13 and R14: no AutoCAD file and
// no grammar (2025 cannot save them), so no claim either way. ANNOTSCALE alone has no record in any file: not tested
VersionMatrix context_matrix(bool tested) {
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.set(v, Generation::Native);
        if (tested) m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

ListSpec whole_subclass(const char* subclass, int occurrence) {
    ListSpec s;
    s.subclass = subclass;
    s.occurrence = occurrence;
    s.id = 1;
    s.shape = ListShape::WholeSubclass;
    return s;
}

template <typename Data>
void add_context(TypedRegistry& registry, const char* dxf_name, ObjectType type, ObjectReadFn read, ObjectWriteFn write,
                 const char* cpp_name, int proxy_flags, std::vector<RefDescriptor> extra_refs = {},
                 std::vector<ListSpec> lists = {}, bool tested = true) {
    ObjectCodec c;
    c.dxf_name = dxf_name;
    c.type = type;
    c.read = read;
    c.write = write;
    c.references = context_references<Data>;
    c.cls = context_class(cpp_name, proxy_flags);
    c.versions = context_matrix(tested);
    c.refs = {scale_ref()};
    for (auto& r : extra_refs) c.refs.push_back(r);
    c.lists = std::move(lists);
    c.step = 17;
    registry.add(std::move(c));
}

} // namespace

void register_context_codecs(TypedRegistry& registry) {
    // The order of AutoCAD's own CLASSES (the 2018 annotative file's)
    add_context<HatchScaleContextData>(registry, "ACDB_HATCHSCALECONTEXTDATA_CLASS", ObjectType::HatchScaleContext,
                                       read_hatch_scale_context, write_hatch_scale_context, "AcDbHatchScaleContextData", 1153, {},
                                       {whole_subclass("AcDbHatchObjectContextData", 0)});
    add_context<HatchViewContextData>(registry, "ACDB_HATCHVIEWCONTEXTDATA_CLASS", ObjectType::HatchViewContext,
                                      read_hatch_view_context, write_hatch_view_context, "AcDbHatchViewContextData", 1153,
                                      {soft_ref("AcDbHatchViewContextData", 330, 0)}, {whole_subclass("AcDbHatchObjectContextData", 0)});
    add_context<BlkRefContextData>(registry, "ACDB_BLKREFOBJECTCONTEXTDATA_CLASS", ObjectType::BlkRefContext,
                                   read_blkref_context, write_blkref_context, "AcDbBlkRefObjectContextData", 1153);
    add_context<MTextAttributeContextData>(registry, "ACDB_MTEXTATTRIBUTEOBJECTCONTEXTDATA_CLASS", ObjectType::MTextAttributeContext,
                                           read_mtext_attribute_context, write_mtext_attribute_context,
                                           "AcDbMTextAttributeObjectContextData", 1153);
    add_context<MLeaderContextData>(registry, "ACDB_MLEADEROBJECTCONTEXTDATA_CLASS", ObjectType::MLeaderContext,
                                    read_mleader_context, write_mleader_context, "AcDbMLeaderObjectContextData", 1153,
                                    {soft_ref("AcDbAnnotScaleObjectContextData", 340, 1), soft_ref("AcDbAnnotScaleObjectContextData", 341, 0)},
                                    {whole_subclass("AcDbAnnotScaleObjectContextData", 0)});
    add_context<LeaderContextData>(registry, "ACDB_LEADEROBJECTCONTEXTDATA_CLASS", ObjectType::LeaderContext,
                                   read_leader_context, write_leader_context, "AcDbLeaderObjectContextData", 1153, {},
                                   {whole_subclass("AcDbAnnotScaleObjectContextData", 0)});
    add_context<MTextContextData>(registry, "ACDB_MTEXTOBJECTCONTEXTDATA_CLASS", ObjectType::MTextContext,
                                  read_mtext_context, write_mtext_context, "AcDbMTextObjectContextData", 1153);
    add_context<FcfContextData>(registry, "ACDB_FCFOBJECTCONTEXTDATA_CLASS", ObjectType::FcfContext,
                                read_fcf_context, write_fcf_context, "AcDbFcfObjectContextData", 1153);
    add_context<RadimLgContextData>(registry, "ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS", ObjectType::RadimLgContext,
                                    read_radimlg_context, write_radimlg_context, "AcDbRadialDimensionLargeObjectContextData", 1153);
    add_context<OrdDimContextData>(registry, "ACDB_ORDDIMOBJECTCONTEXTDATA_CLASS", ObjectType::OrdDimContext,
                                   read_orddim_context, write_orddim_context, "AcDbOrdinateDimensionObjectContextData", 1153);
    add_context<DmDimContextData>(registry, "ACDB_DMDIMOBJECTCONTEXTDATA_CLASS", ObjectType::DmDimContext,
                                  read_dmdim_context, write_dmdim_context, "AcDbDiametricDimensionObjectContextData", 1153);
    add_context<RadimContextData>(registry, "ACDB_RADIMOBJECTCONTEXTDATA_CLASS", ObjectType::RadimContext,
                                  read_radim_context, write_radim_context, "AcDbRadialDimensionObjectContextData", 1153);
    add_context<AngDimContextData>(registry, "ACDB_ANGDIMOBJECTCONTEXTDATA_CLASS", ObjectType::AngDimContext,
                                   read_angdim_context, write_angdim_context, "AcDbAngularDimensionObjectContextData", 1153);
    add_context<AlDimContextData>(registry, "ACDB_ALDIMOBJECTCONTEXTDATA_CLASS", ObjectType::AlDimContext,
                                  read_aldim_context, write_aldim_context, "AcDbAlignedDimensionObjectContextData", 1153);
    add_context<TextContextData>(registry, "ACDB_TEXTOBJECTCONTEXTDATA_CLASS", ObjectType::TextContext,
                                 read_text_context, write_text_context, "AcDbTextObjectContextData", 1153);
    // The base alone: AutoCAD writes it only inside the concrete classes (no file has it as a record), so it is not tested
    add_context<AnnotScaleContextData>(registry, "ACDB_ANNOTSCALEOBJECTCONTEXTDATA_CLASS", ObjectType::AnnotScaleContext,
                                       read_annot_scale_context, write_annot_scale_context,
                                       "AcDbAnnotScaleObjectContextData", 1153, {}, {}, false);
    // The plan's name for the large radial context is the same class as AutoCAD's ..._RADIMLRG...
    registry.add_alias("ACDB_RADIMLGOBJECTCONTEXTDATA_CLASS", "ACDB_RADIMLRGOBJECTCONTEXTDATA_CLASS");
}

} // namespace internal
} // namespace hdxf
