#include "template_2000.hpp"
#include "group_codes.hpp"
#include "text_encoding.hpp"
#include "handles.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>

namespace hdxf {
namespace internal {

namespace {

class TemplateText {
public:
    TemplateText(bool source_binary, int code_page)
        : source_binary_(source_binary), code_page_(code_page) {}

    void group(int code, const std::string& value) {
        out_ += std::to_string(code);
        out_ += '\n';
        GroupValueType type = group_value_type(code);
        bool text = type == GroupValueType::String || type == GroupValueType::Unknown;
        out_ += (source_binary_ && text) ? encode_carets(value, code_page_) : value;
        out_ += '\n';
    }

    void record(const RawRecord& r) {
        for (const auto& g : r.groups()) group(g.code, g.value);
    }

    void entity(const Entity& e) {
        if (e.raw.empty()) return;
        record(e.raw);
        if (e.type == EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) {
                if (!v.raw.empty()) record(v.raw);
            }
            if (!e.as_polyline().seqend_raw.empty()) record(e.as_polyline().seqend_raw);
        } else if (e.type == EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) {
                if (!a.raw.empty()) record(a.raw);
            }
            if (!e.as_insert().seqend_raw.empty()) record(e.as_insert().seqend_raw);
        }
    }

    template <typename T>
    void entries(const std::vector<T>& list) {
        for (const auto& x : list) {
            if (!x.raw.empty()) record(x.raw);
        }
    }

    void marker(const char* type) { group(0, type); }
    void section(const char* name) {
        group(0, "SECTION");
        group(2, name);
    }

    std::string& text() { return out_; }

private:
    bool source_binary_;
    int code_page_;
    std::string out_;
};

} // namespace

std::vector<uint64_t> preserved_handles(const Drawing& dwg) {
    std::vector<uint64_t> out;
    auto add = [&](const RawRecord& r) {
        if (r.empty()) return;
        for (const auto& g : r.groups()) {
            if (g.code == 5 || g.code == 105) {
                uint64_t v = 0;
                if (parse_handle(g.value, v) == HandleParse::Ok) out.push_back(v);
                return;
            }
            if (g.code == 100) return;
        }
    };
    auto entity = [&](const Entity& e) {
        add(e.raw);
        if (e.type == EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) add(v.raw);
            add(e.as_polyline().seqend_raw);
        } else if (e.type == EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) add(a.raw);
            add(e.as_insert().seqend_raw);
        }
    };
    auto entries = [&](const auto& list) { for (const auto& x : list) add(x.raw); };
    for (const auto& t : dwg.raw_tables()) {
        add(t.header);
        for (const auto& r : t.entries) add(r);
    }
    entries(dwg.vports());
    entries(dwg.linetypes());
    entries(dwg.layers());
    entries(dwg.text_styles());
    entries(dwg.views());
    entries(dwg.ucs_list());
    entries(dwg.app_ids());
    entries(dwg.dim_styles());
    for (const auto& b : dwg.blocks()) {
        add(b.raw);
        add(b.endblk_raw);
        add(b.record.raw);
        for (const auto& e : b.entities) entity(e);
    }
    for (const auto& e : dwg.entities()) entity(e);
    for (const auto& o : dwg.objects()) add(o.raw);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::pair<uint64_t, uint64_t>> preserved_block_entities(const Drawing& dwg) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    for (const auto& b : dwg.blocks()) {
        uint64_t record = 0;
        if (b.record.raw.empty() || parse_handle(b.record.handle, record) != HandleParse::Ok) continue;
        for (const auto& e : b.entities) {
            uint64_t v = 0;
            if (!e.raw.empty() && parse_handle(e.props.handle, v) == HandleParse::Ok) out.push_back({record, v});
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::pair<uint64_t, uint64_t>> preserved_hard_owned(const Drawing& dwg) {
    std::vector<std::pair<uint64_t, uint64_t>> out;
    const TypedRegistry& registry = TypedRegistry::instance();
    std::vector<TypedReference> refs;
    for (const auto& o : dwg.objects()) {
        const ObjectCodec* codec = registry.find_object(o.type);
        uint64_t owner = 0;
        if (!codec || !codec->references || o.raw.empty() || parse_handle(o.handle, owner) != HandleParse::Ok) continue;
        refs.clear();
        codec->references(o, refs);
        for (const TypedReference& r : refs) {
            uint64_t target = 0;
            if (r.kind == RefKind::HardOwner && parse_handle(r.handle, target) == HandleParse::Ok && target != 0) {
                out.push_back({target, owner});
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string template_text(const Drawing& dwg, bool source_binary) {
    int code_page = 0;
    if (const HeaderVar* page = dwg.find_header("$DWGCODEPAGE"); page && !page->values.empty()) {
        code_page = code_page_from_name(page->values[0].second);
    }
    TemplateText t(source_binary, code_page);

    for (const auto& s : dwg.raw_sections()) {
        if (s.name != "HEADER") continue;
        t.record(s.head);           // the whole section from 0 SECTION on
        t.marker("ENDSEC");
    }

    t.section("TABLES");
    for (const auto& table : dwg.raw_tables()) {
        t.record(table.header);
        if (table.name == "VPORT") t.entries(dwg.vports());
        if (table.name == "LTYPE") t.entries(dwg.linetypes());
        if (table.name == "LAYER") t.entries(dwg.layers());
        if (table.name == "STYLE") t.entries(dwg.text_styles());
        if (table.name == "VIEW") t.entries(dwg.views());
        if (table.name == "UCS") t.entries(dwg.ucs_list());
        if (table.name == "APPID") t.entries(dwg.app_ids());
        if (table.name == "DIMSTYLE") t.entries(dwg.dim_styles());
        if (table.name == "BLOCK_RECORD") {
            for (const auto& b : dwg.blocks()) {
                if (!b.record.raw.empty()) t.record(b.record.raw);
            }
        }
        for (const auto& r : table.entries) t.record(r);
        t.marker("ENDTAB");
    }
    t.marker("ENDSEC");

    t.section("BLOCKS");
    for (const auto& b : dwg.blocks()) {
        if (b.raw.empty()) continue;
        t.record(b.raw);
        for (const auto& e : b.entities) t.entity(e);
        if (!b.endblk_raw.empty()) t.record(b.endblk_raw);
    }
    t.marker("ENDSEC");

    t.section("ENTITIES");
    for (const auto& e : dwg.entities()) t.entity(e);
    t.marker("ENDSEC");

    // R12 has no OBJECTS section, thumbnail or ACDSDATA
    if (dwg.loaded_r12_) {
        t.marker("EOF");
        return std::move(t.text());
    }

    t.section("OBJECTS");
    for (const auto& o : dwg.objects()) {
        if (!o.raw.empty()) t.record(o.raw);
    }
    t.marker("ENDSEC");
    if (!dwg.thumbnail_raw().empty()) {
        t.record(dwg.thumbnail_raw());  // the whole section from 0 SECTION on
        t.marker("ENDSEC");
    }
    if (!dwg.acds_head_.empty()) {
        t.record(dwg.acds_head_);      // ACDSDATA (2013+) as read
        for (const auto& r : dwg.acds_schemas_) t.record(r);
        for (const auto& r : dwg.acds_records_) t.record(r);
        t.marker("ENDSEC");
    }
    t.marker("EOF");
    return std::move(t.text());
}

} // namespace internal
} // namespace hdxf
