#include "StyleTypes.hpp"
#include <algorithm>
#include <set>

// AutoCAD's standard materials and visual styles as typed values (phase 9, step 9.7): the generated tables of a new drawing
// (drawing_r2007.cpp, drawing_r2010.cpp, drawing_r2013.cpp, jscad's empty2007/2010/2013 from AutoCAD 2017) are groups; the
// drawing's standard objects are made from them through the same readers a file's records go through, so what a new drawing
// has and what a read file has are the same typed values (avtalets punkt 4: the same bytes as before).

namespace hdxf {
namespace internal {

namespace {

// The record `type` with the groups of one table record after its subclass marker, split as a read record
bool split_record(const char* type, const char* subclass, const DataRecord& r, RecordView& view) {
    std::vector<DxfGroup> groups;
    groups.push_back({0, type, 0});
    groups.push_back({5, "0", 0});
    groups.push_back({100, subclass, 0});
    for (size_t i = 0; i < r.count; ++i) groups.push_back({r.groups[i].code, r.groups[i].value, 0});
    std::string error;
    return RecordView::parse(std::move(groups), view, error);
}

} // namespace

MaterialData typed_material(const DataRecord& record) {
    MaterialData d;
    RecordView view;
    TypedReadContext ctx;
    if (split_record("MATERIAL", "AcDbMaterial", record, view)) read_material_subclass(view, ctx, d, "a standard material");
    return d;
}

VisualStyleData typed_visual_style(const DataRecord& record) {
    VisualStyleData d;
    RecordView view;
    TypedReadContext ctx;
    if (!split_record("VISUALSTYLE", "AcDbVisualStyle", record, view) ||
        !read_visual_style_subclass(view, ctx, d, "a standard visual style")) {
        return VisualStyleData();
    }
    // The older forms keep the properties 28 … 57 in the XRECORD of the style
    if (record.roundtrip_count != 0) {
        std::vector<DxfGroup> groups;
        for (size_t i = 0; i < record.roundtrip_count; ++i) groups.push_back({record.roundtrip[i].code, record.roundtrip[i].value, 0});
        decode_visual_style_roundtrip(groups, d);
    }
    return d;
}

const std::vector<MaterialData>& standard_typed_materials() {
    static const std::vector<MaterialData> materials = [] {
        size_t n = 0;
        const DataRecord* r = standard_materials(n);
        std::vector<MaterialData> v;
        for (size_t i = 0; i < n; ++i) v.push_back(typed_material(r[i]));
        return v;
    }();
    return materials;
}

const std::vector<VisualStyleData>& standard_typed_styles(int form) {
    auto make = [](const DataRecord* (*table)(size_t&)) {
        size_t n = 0;
        const DataRecord* r = table(n);
        std::vector<VisualStyleData> v;
        for (size_t i = 0; i < n; ++i) v.push_back(typed_visual_style(r[i]));
        return v;
    };
    static const std::vector<VisualStyleData> forms[3] = {make(visual_styles_2007), make(visual_styles_2010), make(visual_styles_2013)};
    return forms[form];
}

void drop_standard_style_objects(Drawing& dwg) {
    std::set<std::string> gone;
    auto take_extension = [&](const Object& o) {
        gone.insert(o.handle);
        if (o.xdictionary.empty()) return;
        const Object* dict = dwg.find_object(o.xdictionary);
        if (!dict || dict->type != ObjectType::Dictionary) return;
        gone.insert(dict->handle);
        for (const DictionaryEntry& e : dict->as_dictionary().entries) {
            const Object* x = dwg.find_object(e.handle);
            if (x && x->type == ObjectType::XRecord) gone.insert(x->handle);
        }
    };
    const std::vector<MaterialData>& materials = standard_typed_materials();
    for (const Object& o : dwg.objects()) {
        if (o.type == ObjectType::Material) {
            for (const MaterialData& m : materials) {
                if (material_equal(o.as_material(), m)) {
                    take_extension(o);
                    break;
                }
            }
        } else if (o.type == ObjectType::VisualStyle) {
            for (int form = 0; form < 3; ++form) {
                bool same = false;
                for (const VisualStyleData& s : standard_typed_styles(form)) {
                    if (visual_style_equal(o.as_visual_style(), s)) {
                        same = true;
                        break;
                    }
                }
                if (same) {
                    take_extension(o);
                    break;
                }
            }
        }
    }
    if (gone.empty()) return;
    // Their dictionaries (ACAD_MATERIAL, ACAD_VISUALSTYLE) lose the entries, and go with their entry in the root dictionary
    // when nothing else is in them
    Object* root = nullptr;
    for (Object& o : dwg.objects()) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") {
            root = &o;
            break;
        }
    }
    if (root) {
        auto& root_entries = root->as_dictionary().entries;
        for (const char* name : {"ACAD_MATERIAL", "ACAD_VISUALSTYLE"}) {
            auto it = std::find_if(root_entries.begin(), root_entries.end(), [&](const DictionaryEntry& e) { return e.name == name; });
            if (it == root_entries.end()) continue;
            Object* dict = dwg.find_object(it->handle);
            if (!dict || dict->type != ObjectType::Dictionary) continue;
            auto& entries = dict->as_dictionary().entries;
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return gone.count(e.handle) > 0; }),
                          entries.end());
            if (entries.empty()) {
                gone.insert(dict->handle);
                root_entries.erase(it);
            }
        }
    }
    auto& objects = dwg.objects();
    objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const Object& o) { return gone.count(o.handle) > 0; }), objects.end());
}

void register_style_codecs(TypedRegistry& registry) {
    register_material_codec(registry);
    register_visual_style_codec(registry);
}

} // namespace internal
} // namespace hdxf
