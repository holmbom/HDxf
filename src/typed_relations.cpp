#include "TypedRecordTypes.hpp"
#include <algorithm>

// The relations and lists of the typed records (phase 9, step 9.0; B12, B19, B104). The writer's merge of a
// record with its preserved record (dxf_writer_template.cpp) asks here what a handle-like group is and which
// groups are one list; the relation collector (dxf_writer_2000.cpp) asks which records are reactors of
// which.

namespace hdxf {
namespace internal {

const RefDescriptor* find_reference(std::string_view dxf_name, std::string_view subclass, size_t occurrence,
                                    int code, size_t ordinal) {
    return find_reference_in(TypedRegistry::instance().refs_for(dxf_name), subclass, occurrence, code, ordinal);
}

const RefDescriptor* find_reference_in(const std::vector<RefDescriptor>* refs, std::string_view subclass,
                                       size_t occurrence, int code, size_t ordinal) {
    if (!refs) return nullptr;
    for (const RefDescriptor& d : *refs) {
        if (d.code != code) continue;
        if (d.subclass && d.subclass[0] != '\0' && subclass != d.subclass) continue;
        if (d.occurrence >= 0 && static_cast<size_t>(d.occurrence) != occurrence) continue;
        if (d.ordinal >= 0 && static_cast<size_t>(d.ordinal) != ordinal) continue;
        return &d;
    }
    return nullptr;
}

int find_list(std::string_view dxf_name, std::string_view subclass, size_t occurrence, int code,
              bool& from_started) {
    return find_list_in(TypedRegistry::instance().lists_for(dxf_name), subclass, occurrence, code, from_started);
}

int find_list_in(const std::vector<ListSpec>* lists, std::string_view subclass, size_t occurrence, int code,
                 bool& from_started) {
    if (!lists) return 0;
    for (const ListSpec& s : *lists) {
        if (s.subclass && s.subclass[0] != '\0' && subclass != s.subclass) continue;
        if (s.occurrence >= 0 && static_cast<size_t>(s.occurrence) != occurrence) continue;
        switch (s.shape) {
            case ListShape::Codes:
                if (std::find(s.codes.begin(), s.codes.end(), code) != s.codes.end()) return s.id;
                break;
            case ListShape::WholeSubclass:
                return s.id;
            case ListShape::FromCode:
                if (!s.codes.empty() && (from_started || code == s.codes[0])) {
                    from_started = true;
                    return s.id;
                }
                break;
        }
    }
    return 0;
}

namespace {

std::string upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

void collect_typed_reactors(const Drawing& dwg, std::unordered_map<std::string, std::vector<std::string>>& reactors) {
    const TypedRegistry& registry = TypedRegistry::instance();
    std::vector<TypedReference> refs;
    auto add = [&](const std::string& owner_handle) {
        for (const TypedReference& r : refs) {
            if (r.back_reactor && !r.handle.empty() && !owner_handle.empty()) {
                reactors[upper(r.handle)].push_back(owner_handle);
            }
        }
        refs.clear();
    };
    for (const Object& o : dwg.objects()) {
        const ObjectCodec* codec = registry.find_object(o.type);
        if (!codec || !codec->references) continue;
        codec->references(o, refs);
        add(o.handle);
    }
    auto entities = [&](const std::vector<Entity>& list) {
        for (const Entity& e : list) {
            const EntityCodec* codec = registry.find_entity(e.type);
            if (!codec || !codec->references) continue;
            codec->references(e, refs);
            add(e.props.handle);
        }
    };
    entities(dwg.entities());
    for (const Block& b : dwg.blocks()) entities(b.entities);
}

} // namespace internal
} // namespace hdxf
