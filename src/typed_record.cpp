#include "TypedRecordTypes.hpp"
#include "dxf_writer.hpp"
#include "handles.hpp"
#include "proxy_record.hpp"
#include <charconv>
#include <cstdlib>
#include <map>

// The foundation of phase 9, step 9.0 (see TypedRecordTypes.hpp): the record splitter and the group cursor,
// the registry, the writer's face for the codecs, the entity head, the classes and the shared values.

namespace hdxf {
namespace internal {

namespace {

constexpr size_t kNoSubclass = static_cast<size_t>(-1);

std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && s[b] == ' ') ++b;
    while (e > b && s[e - 1] == ' ') --e;
    return s.substr(b, e - b);
}

std::string_view trim_view(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

bool is_open_102(const DxfGroup& g) {
    return g.code == 102 && !g.value.empty() && g.value[0] == '{';
}

} // namespace

// ---------------------------------------------------------------------------
// RecordView
// ---------------------------------------------------------------------------

bool RecordView::parse(std::vector<DxfGroup> groups, RecordView& out, std::string& error) {
    out = RecordView();
    if (groups.empty() || groups[0].code != 0) {
        error = "a record must begin with group 0";
        return false;
    }
    out.groups_ = std::move(groups);
    const std::vector<DxfGroup>& g = out.groups_;
    out.type_name_ = trimmed(g[0].value);

    // The head: 5, the 102 groups {ACAD_REACTORS and {ACAD_XDICTIONARY, the owner (330), as AutoCAD's order
    size_t i = 1;
    bool owner_seen = false;
    while (i < g.size()) {
        const DxfGroup& x = g[i];
        if ((x.code == 5 || (x.code == 105 && out.type_name_ == "DIMSTYLE")) && out.head_.handle.empty()) {
            out.head_.handle = trimmed(x.value);
            ++i;
        } else if (x.code == 102 && (x.value == "{ACAD_REACTORS" || x.value == "{ACAD_XDICTIONARY")) {
            bool reactors = x.value == "{ACAD_REACTORS";
            ++i;
            while (i < g.size() && g[i].code != 102) {
                if (reactors && g[i].code == 330) out.head_.reactors.push_back(trimmed(g[i].value));
                if (!reactors && g[i].code == 360) out.head_.xdictionary = trimmed(g[i].value);
                ++i;
            }
            if (i < g.size()) ++i;                  // 102 }
        } else if (x.code == 330 && !owner_seen) {
            out.head_.owner = trimmed(x.value);
            owner_seen = true;
            ++i;
        } else {
            break;
        }
    }
    const size_t body = i;

    // XDATA is the tail from the first 1001 on in which every group is >= 1000: a group of 1010 … 1071
    // before it is class data of a subclass (BLOCKLINEARPARAMETER's 1010 and 1071), not XDATA
    size_t tail = g.size();
    while (tail > body && g[tail - 1].code >= 1000) --tail;
    while (tail < g.size() && g[tail].code != 1001) ++tail;
    out.body_end_ = tail;
    for (size_t k = tail; k < g.size(); ++k) {
        if (g[k].code == 1001) out.xdata_.push_back({g[k].value, {}});
        else if (!out.xdata_.empty()) out.xdata_.back().items.push_back({g[k].code, g[k].value});
    }

    // The subclasses, 102 blocks and embedded objects of the body
    std::map<std::string, size_t> occurrences;
    auto open = [&](const std::string& name) {
        Subclass s;
        s.name = name;
        s.occurrence = occurrences[name]++;
        out.subclasses_.push_back(std::move(s));
    };
    for (size_t k = body; k < tail; ++k) {
        const DxfGroup& x = g[k];
        if (x.code == 100) {
            open(trimmed(x.value));
            continue;
        }
        if (x.code == 101) {
            size_t end = k + 1;
            while (end < tail && g[end].code != 101) ++end;
            out.embedded_.push_back({k, end});
            k = end - 1;
            continue;
        }
        if (is_open_102(x)) {
            size_t end = k + 1;
            while (end < tail && !(g[end].code == 102 && !is_open_102(g[end]))) ++end;
            if (end >= tail) {
                error = "the 102 group " + x.value + " is not closed";
                return false;
            }
            out.blocks_.push_back({x.value, k, end, out.subclasses_.empty() ? kNoSubclass : out.subclasses_.size() - 1});
            k = end;
            continue;
        }
        if (out.subclasses_.empty()) open("");
        out.subclasses_.back().indices.push_back(k);
    }
    return true;
}

const Subclass* RecordView::find_subclass(std::string_view name, size_t occurrence) const {
    for (const Subclass& s : subclasses_) {
        if (s.name == name && s.occurrence == occurrence) return &s;
    }
    return nullptr;
}

size_t RecordView::subclass_count(std::string_view name) const {
    size_t n = 0;
    for (const Subclass& s : subclasses_) if (s.name == name) ++n;
    return n;
}

std::string RecordView::chain() const {
    std::string out;
    for (const Subclass& s : subclasses_) {
        if (s.name.empty()) continue;
        if (!out.empty()) out += "/";
        out += s.name;
    }
    return out;
}

// ---------------------------------------------------------------------------
// GroupCursor
// ---------------------------------------------------------------------------

GroupCursor::GroupCursor(const RecordView& view, const Subclass* subclass) : view_(view), subclass_(subclass) {}

GroupCursor::GroupCursor(const RecordView& view, std::string_view name, size_t occurrence)
    : view_(view), subclass_(view.find_subclass(name, occurrence)) {}

const std::string& GroupCursor::name() const {
    static const std::string none;
    return subclass_ ? subclass_->name : none;
}

size_t GroupCursor::occurrence() const { return subclass_ ? subclass_->occurrence : 0; }

const DxfGroup* GroupCursor::group_at(size_t pos) const {
    if (!subclass_ || pos >= subclass_->indices.size()) return nullptr;
    return &view_.groups()[subclass_->indices[pos]];
}

const DxfGroup* GroupCursor::peek(size_t ahead) const { return group_at(pos_ + ahead); }

int GroupCursor::code() const {
    const DxfGroup* g = peek();
    return g ? g->code : -1;
}

size_t GroupCursor::ordinal(int code) const {
    auto it = taken_.find(code);
    return it == taken_.end() ? 0 : it->second;
}

void GroupCursor::fail(const std::string& message) {
    if (!error_.empty()) return;
    error_ = view_.type_name() + (subclass_ && !subclass_->name.empty() ? " " + subclass_->name : std::string()) +
             (view_.head().handle.empty() ? std::string() : " " + view_.head().handle) + ": " + message;
}

bool GroupCursor::required(int code, bool present) {
    if (!present && error_.empty()) fail("group " + std::to_string(code) + " is missing");
    return present && error_.empty();
}

bool GroupCursor::string(int code, std::string& out) {
    const DxfGroup* g = peek();
    if (!g || g->code != code) return false;
    out = g->value;
    ++pos_;
    taken(code);
    return true;
}

bool GroupCursor::handle(int code, std::string& out) {
    std::string value;
    if (!string(code, value)) return false;
    out = trimmed(value);
    return true;
}

bool GroupCursor::take_integer(int code, int64_t& out) {
    const DxfGroup* g = peek();
    if (!g || g->code != code) return false;
    std::string_view s = trim_view(g->value);
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    int64_t v = 0;
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || ec != std::errc() || end != s.data() + s.size()) {
        fail("group " + std::to_string(code) + " is \"" + g->value + "\", not an integer");
        return false;
    }
    out = v;
    ++pos_;
    taken(code);
    return true;
}

void GroupCursor::fail_range(int code, int64_t value, size_t bits) {
    fail("group " + std::to_string(code) + " is " + std::to_string(value) + ", which does not fit " +
         std::to_string(bits) + " bits");
}

bool GroupCursor::boolean(int code, bool& out) {
    int64_t v = 0;
    if (!take_integer(code, v)) return false;
    out = v != 0;
    return true;
}

bool GroupCursor::real(int code, double& out) {
    const DxfGroup* g = peek();
    if (!g || g->code != code) return false;
    std::string_view s = trim_view(g->value);
    if (!s.empty() && s.front() == '+') s.remove_prefix(1);
    double v = 0.0;
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (s.empty() || ec == std::errc::invalid_argument || end != s.data() + s.size()) {
        fail("group " + std::to_string(code) + " is \"" + g->value + "\", not a number");
        return false;
    }
    // A value too large for a double (1E+400) is a double's own limit: infinity, as atof gives
    if (ec == std::errc::result_out_of_range) v = std::strtod(std::string(s).c_str(), nullptr);
    out = v;
    ++pos_;
    taken(code);
    return true;
}

bool GroupCursor::point2(int code, Point2& out) {
    const DxfGroup* a = peek(0);
    const DxfGroup* b = peek(1);
    if (!a || a->code != code || !b || b->code != code + 10) return false;
    Point2 p;
    return real(code, p.x) && real(code + 10, p.y) && ((out = p), true);
}

bool GroupCursor::point3(int code, Point3& out) {
    const DxfGroup* a = peek(0);
    const DxfGroup* b = peek(1);
    const DxfGroup* c = peek(2);
    if (!a || a->code != code || !b || b->code != code + 10 || !c || c->code != code + 20) return false;
    Point3 p;
    return real(code, p.x) && real(code + 10, p.y) && real(code + 20, p.z) && ((out = p), true);
}

bool GroupCursor::bytes(int code, std::vector<uint8_t>& out) {
    const DxfGroup* g = peek();
    if (!g || g->code != code) return false;
    auto digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    std::vector<uint8_t> data;
    while ((g = peek()) && g->code == code) {
        std::string_view hex = trim_view(g->value);
        if (hex.size() % 2 != 0) {
            fail("group " + std::to_string(code) + " holds an odd number of hex digits");
            return false;
        }
        for (size_t i = 0; i < hex.size(); i += 2) {
            int hi = digit(hex[i]), lo = digit(hex[i + 1]);
            if (hi < 0 || lo < 0) {
                fail("group " + std::to_string(code) + " holds a character that is not a hex digit");
                return false;
            }
            data.push_back(static_cast<uint8_t>(hi * 16 + lo));
        }
        ++pos_;
        taken(code);
    }
    out.insert(out.end(), data.begin(), data.end());
    return true;
}

bool GroupCursor::count(int code, size_t min_groups_per_item, size_t& n) {
    int64_t v = 0;
    if (!take_integer(code, v)) return false;
    if (v < 0) {
        fail("count " + std::to_string(v) + " in group " + std::to_string(code) + " is negative");
        return false;
    }
    // The groups that remain are all the items can have: a corrupt count cannot cause an allocation
    size_t per = min_groups_per_item == 0 ? 1 : min_groups_per_item;
    if (static_cast<uint64_t>(v) > remaining() / per) {
        fail("count " + std::to_string(v) + " in group " + std::to_string(code) + " is more than the " +
             std::to_string(remaining()) + " groups that remain can hold");
        return false;
    }
    n = static_cast<size_t>(v);
    return true;
}

std::vector<DxfGroup> GroupCursor::take_rest() {
    std::vector<DxfGroup> out;
    while (const DxfGroup* g = peek()) {
        out.push_back(*g);
        ++pos_;
    }
    return out;
}

// ---------------------------------------------------------------------------
// The registry
// ---------------------------------------------------------------------------

namespace {

constexpr TypedKind E = TypedKind::Entity;
constexpr TypedKind O = TypedKind::Object;
constexpr TypedKind S = TypedKind::Structural;
constexpr InventoryStatus Typed = InventoryStatus::AlreadyTyped;
constexpr InventoryStatus Structural = InventoryStatus::Structural;
constexpr InventoryStatus Pending = InventoryStatus::Pending;
constexpr InventoryStatus Zero = InventoryStatus::ZeroSamples;
constexpr InventoryStatus Alias = InventoryStatus::DerivedAlias;
constexpr InventoryStatus External = InventoryStatus::ExternalClass;

// Generated by tools/gen_type_registry.py from the old plan's appendices A, B and C (doc/plans/
// 2026-10-25-fas-9-typer.md) and the dispatch of v1.8.1; checked against both by test_all_typed_records
const TypeRegistryEntry kRegistry[] = {
#include "typed_registry_data.inc"
};

} // namespace

TypedRegistry::TypedRegistry() {
    entries_.assign(std::begin(kRegistry), std::end(kRegistry));
    for (size_t i = 0; i < entries_.size(); ++i) entry_by_name_[std::string(entries_[i].dxf_name)] = i;

    // (SORTENTSTABLE's pair rule is the codec's since step 9.4: objects_order_index.cpp)
    register_typed_families(*this);
}

TypedRegistry& TypedRegistry::instance() {
    static TypedRegistry registry;
    return registry;
}

const TypeRegistryEntry* TypedRegistry::find_entry(std::string_view dxf_name) const {
    auto it = entry_by_name_.find(dxf_name);
    return it == entry_by_name_.end() ? nullptr : &entries_[it->second];
}

const ObjectCodec* TypedRegistry::find_object(std::string_view dxf_name) const {
    auto it = object_by_name_.find(dxf_name);
    if (it != object_by_name_.end()) return &objects_[it->second];
    auto alias = aliases_.find(dxf_name);
    if (alias == aliases_.end()) return nullptr;
    auto target = object_by_name_.find(alias->second);
    return target == object_by_name_.end() ? nullptr : &objects_[target->second];
}

const ObjectCodec* TypedRegistry::find_object(ObjectType type) const {
    auto it = object_by_type_.find(static_cast<int>(type));
    return it == object_by_type_.end() ? nullptr : &objects_[it->second];
}

const EntityCodec* TypedRegistry::find_entity(std::string_view dxf_name) const {
    auto it = entity_by_name_.find(dxf_name);
    if (it != entity_by_name_.end()) return &entities_[it->second];
    auto alias = aliases_.find(dxf_name);
    if (alias == aliases_.end()) return nullptr;
    auto target = entity_by_name_.find(alias->second);
    return target == entity_by_name_.end() ? nullptr : &entities_[target->second];
}

const EntityCodec* TypedRegistry::find_entity(EntityType type) const {
    auto it = entity_by_type_.find(static_cast<int>(type));
    return it == entity_by_type_.end() ? nullptr : &entities_[it->second];
}

const std::vector<RefDescriptor>* TypedRegistry::refs_for(std::string_view dxf_name) const {
    if (const ObjectCodec* o = find_object(dxf_name); o && !o->refs.empty()) return &o->refs;
    if (const EntityCodec* e = find_entity(dxf_name); e && !e->refs.empty()) return &e->refs;
    auto it = relations_.find(dxf_name);
    return it == relations_.end() ? nullptr : &it->second;
}

const std::vector<ListSpec>* TypedRegistry::lists_for(std::string_view dxf_name) const {
    if (const ObjectCodec* o = find_object(dxf_name); o && !o->lists.empty()) return &o->lists;
    if (const EntityCodec* e = find_entity(dxf_name); e && !e->lists.empty()) return &e->lists;
    auto it = lists_.find(dxf_name);
    return it == lists_.end() ? nullptr : &it->second;
}

bool TypedRegistry::implemented(std::string_view dxf_name) const {
    return find_object(dxf_name) || find_entity(dxf_name);
}

void TypedRegistry::reindex() {
    object_by_name_.clear();
    entity_by_name_.clear();
    object_by_type_.clear();
    entity_by_type_.clear();
    for (size_t i = 0; i < objects_.size(); ++i) {
        object_by_name_[std::string(objects_[i].dxf_name)] = i;
        if (objects_[i].type != ObjectType::Unknown) object_by_type_[static_cast<int>(objects_[i].type)] = i;
    }
    for (size_t i = 0; i < entities_.size(); ++i) {
        entity_by_name_[std::string(entities_[i].dxf_name)] = i;
        if (entities_[i].type != EntityType::Unknown) entity_by_type_[static_cast<int>(entities_[i].type)] = i;
    }
}

void TypedRegistry::add(ObjectCodec codec) {
    remove_object(codec.dxf_name);
    if (codec.type != ObjectType::Unknown) {
        for (size_t i = 0; i < objects_.size(); ++i) {
            if (objects_[i].type == codec.type) { objects_.erase(objects_.begin() + static_cast<long>(i)); break; }
        }
    }
    objects_.push_back(std::move(codec));
    reindex();
}

void TypedRegistry::add(EntityCodec codec) {
    remove_entity(codec.dxf_name);
    if (codec.type != EntityType::Unknown) {
        for (size_t i = 0; i < entities_.size(); ++i) {
            if (entities_[i].type == codec.type) { entities_.erase(entities_.begin() + static_cast<long>(i)); break; }
        }
    }
    entities_.push_back(std::move(codec));
    reindex();
}

bool TypedRegistry::remove_object(std::string_view dxf_name) {
    for (size_t i = 0; i < objects_.size(); ++i) {
        if (objects_[i].dxf_name == dxf_name) {
            objects_.erase(objects_.begin() + static_cast<long>(i));
            reindex();
            return true;
        }
    }
    return false;
}

bool TypedRegistry::remove_entity(std::string_view dxf_name) {
    for (size_t i = 0; i < entities_.size(); ++i) {
        if (entities_[i].dxf_name == dxf_name) {
            entities_.erase(entities_.begin() + static_cast<long>(i));
            reindex();
            return true;
        }
    }
    return false;
}

void TypedRegistry::add_relations(TypeRelations relations) {
    relations_[relations.dxf_name] = std::move(relations.refs);
}

void TypedRegistry::remove_relations(const std::string& dxf_name) { relations_.erase(dxf_name); }

void TypedRegistry::add_alias(std::string alias, std::string dxf_name) { aliases_[std::move(alias)] = std::move(dxf_name); }

void TypedRegistry::add_lists(const std::string& dxf_name, std::vector<ListSpec> lists) {
    lists_[dxf_name] = std::move(lists);
}

void TypedRegistry::remove_lists(const std::string& dxf_name) { lists_.erase(dxf_name); }

// ---------------------------------------------------------------------------
// Reading a typed record
// ---------------------------------------------------------------------------

bool read_entity_properties(const RecordView& view, EntityProperties& props, std::vector<uint8_t>* proxy,
                            std::string& error) {
    props = EntityProperties();
    props.handle = view.head().handle;
    props.reactors = view.head().reactors;
    props.xdictionary = view.head().xdictionary;
    props.xdata = view.xdata();

    GroupCursor c(view, "AcDbEntity");
    if (!c.valid()) return true;            // a record without AcDbEntity has the defaults
    while (!c.at_end() && c.ok()) {
        int32_t n = 0;
        switch (c.code()) {
            case 8:   c.string(8, props.layer); break;
            case 6:   c.string(6, props.linetype); break;
            case 62:  c.integer(62, props.color.aci); break;
            case 420: c.integer(420, props.color.rgb); break;
            case 430: c.string(430, props.color.name); break;
            case 440: c.integer(440, props.transparency.raw); break;
            case 48:  c.real(48, props.linetype_scale); break;
            case 60:  if (c.integer(60, n)) props.visible = n == 0; break;
            case 67:  c.integer(67, props.paper_space); break;
            case 370: c.integer(370, props.lineweight); break;
            case 380: c.integer(380, props.plot_style_type); break;
            case 390: c.handle(390, props.plot_style_handle); break;
            case 347: c.handle(347, props.material_handle); break;
            case 284: c.integer(284, props.shadow_mode); break;
            case 92:
            case 160: {
                // The proxy graphics' size, then the bytes; the size is the bytes' length
                int64_t size = 0;
                c.integer(c.code(), size);
                break;
            }
            case 310: {
                std::vector<uint8_t> bytes;
                c.bytes(310, bytes);
                if (proxy) proxy->insert(proxy->end(), bytes.begin(), bytes.end());
                break;
            }
            default:  c.skip(1); break;
        }
    }
    if (!c.ok()) {
        error = c.error();
        return false;
    }
    return true;
}

namespace {

std::string failure_text(const std::string& type, const std::string& handle, const std::string& reason) {
    return type + (handle.empty() ? std::string() : " " + handle) + ": " + reason;
}

} // namespace

bool typed_read_object(const ObjectCodec& codec, std::vector<DxfGroup> groups, TypedReadContext& context,
                       Object& out) {
    RecordView view;
    std::string error;
    if (!RecordView::parse(std::move(groups), view, error)) {
        return context.fail(failure_text(std::string(codec.dxf_name), out.handle, error));
    }
    if (!codec.read) return context.fail(failure_text(std::string(codec.dxf_name), out.handle, "no read function"));
    Object typed = out;
    typed.type = codec.type;
    if (!codec.read(view, context, typed)) {
        if (context.error.empty()) context.fail(failure_text(std::string(codec.dxf_name), out.handle, "the record was not read"));
        return false;
    }
    out = std::move(typed);
    return true;
}

bool typed_read_entity(const EntityCodec& codec, std::vector<DxfGroup> groups, TypedReadContext& context,
                       Entity& out) {
    RecordView view;
    std::string error;
    if (!RecordView::parse(std::move(groups), view, error)) {
        return context.fail(failure_text(std::string(codec.dxf_name), out.props.handle, error));
    }
    if (!codec.read) return context.fail(failure_text(std::string(codec.dxf_name), out.props.handle, "no read function"));
    Entity typed;
    typed.raw = out.raw;
    typed.type = codec.type;
    if (!read_entity_properties(view, typed.props, nullptr, error)) return context.fail(error);
    if (!codec.read(view, context, typed)) {
        if (context.error.empty()) {
            context.fail(failure_text(std::string(codec.dxf_name), out.props.handle, "the record was not read"));
        }
        return false;
    }
    out = std::move(typed);
    return true;
}

// ---------------------------------------------------------------------------
// AcDbEvalExpr and AcDbEvalVariant
// ---------------------------------------------------------------------------

bool read_eval_variant(GroupCursor& c, int tag_code, int real_code, int integer_code, EvalVariant& out) {
    int32_t tag = 0;
    if (!c.integer(tag_code, tag)) return false;
    out = EvalVariant();
    out.tag = tag;
    switch (tag) {
        case 40:
            return c.require_real(real_code, out.real);
        case 70:
            return c.require_integer(integer_code, out.integer);
        default:
            // No AutoCAD file has another tag: its value's group is not known, so the record is not typed
            c.fail("the value type " + std::to_string(tag) + " of an evaluation expression is not established");
            return false;
    }
}

void write_eval_variant(RecordWriter& w, int tag_code, int real_code, int integer_code, const EvalVariant& v) {
    w.integer(tag_code, v.tag);
    if (v.tag == 70) w.integer(integer_code, v.integer);
    else w.real(real_code, v.real);
}

bool read_eval_expr(GroupCursor& c, EvalExprData& out) {
    out = EvalExprData();
    if (!c.require_integer(90, out.node_id) || !c.require_integer(98, out.version) ||
        !c.require_integer(99, out.maintenance)) {
        return false;
    }
    if (c.code() == 1) {
        EvalExprValue value;
        c.string(1, value.expression);
        if (!read_eval_variant(c, 70, 140, 70, value.value)) {
            if (c.ok()) c.fail("the expression's value type (70) is missing");
            return false;
        }
        out.expression = std::move(value);
    }
    return c.ok();
}

void write_eval_expr(RecordWriter& w, const EvalExprData& v) {
    w.integer(90, v.node_id);
    w.integer(98, v.version);
    w.integer(99, v.maintenance);
    if (v.expression) {
        w.text(1, v.expression->expression);
        write_eval_variant(w, 70, 140, 70, v.expression->value);
    }
}

// ---------------------------------------------------------------------------
// CLASSES
// ---------------------------------------------------------------------------

void append_typed_classes(const Drawing& dwg, DxfVersion version, const std::unordered_set<uint64_t>* dropped,
                          std::vector<ClassEntry>& out) {
    const TypedRegistry& registry = TypedRegistry::instance();
    const bool older = version < DxfVersion::R2000;
    auto add = [&](std::string_view name, const ClassSpec& cls, bool entity, int count) {
        if (count == 0 || !cls.cpp_name) return;
        if (older && !cls.in_older) return;
        for (const ClassEntry& have : out) {
            if (name == have.name) return;      // a class the built-in lists already have
        }
        ClassEntry e{name.data(), cls.cpp_name, older && cls.older_application ? cls.older_application : cls.application,
                     older ? (version == DxfVersion::R13 ? 32768 : cls.older_flags) : cls.proxy_flags, entity, count};
        out.push_back(e);
        // The classes of what is embedded in the record (an array's parameters): in the CLASSES, no record of their own
        for (size_t i = 0; i < cls.companion_count && cls.companions; ++i) {
            const ClassCompanion& comp = cls.companions[i];
            bool have = false;
            for (const ClassEntry& x : out) have = have || std::string_view(comp.dxf_name) == x.name;
            if (have) continue;
            out.push_back({comp.dxf_name, comp.cpp_name, e.application, e.flags, entity, 0});
        }
    };
    for (const ObjectCodec& c : registry.object_codecs()) {
        int count = 0;
        for (const Object& o : dwg.objects()) {
            uint64_t h = 0;
            if (o.type != c.type) continue;
            if (dropped && parse_handle(o.handle, h) == HandleParse::Ok && dropped->count(h)) continue;
            ++count;
        }
        add(c.dxf_name, c.cls, false, count);
    }
    for (const EntityCodec& c : registry.entity_codecs()) {
        int count = 0;
        auto in = [&](const std::vector<Entity>& list) {
            for (const Entity& e : list) if (e.type == c.type) ++count;
        };
        in(dwg.entities());
        for (const Block& b : dwg.blocks()) in(b.entities);
        add(c.dxf_name, c.cls, true, count);
    }
    // The classes of the applications the proxies stand for (9.3, proxy_record.cpp)
    append_proxy_classes(dwg, version, dropped, out);
}

// ---------------------------------------------------------------------------
// RecordWriter
// ---------------------------------------------------------------------------

DxfVersion RecordWriter::version() const { return w_.version_; }
DxfFormat RecordWriter::format() const { return w_.format_; }
bool RecordWriter::filtering() const { return w_.filter_live_; }
bool RecordWriter::built_in_memory() const { return !w_.template_mode(); }
bool RecordWriter::live(const std::string& handle) const { return !w_.filter_live_ || w_.is_live(handle); }
bool RecordWriter::read_from_file() const { return w_.template_mode(); }
const Drawing& RecordWriter::drawing() const { return *w_.drawing_; }
std::string RecordWriter::new_handle() { return w_.new_handle(); }

void RecordWriter::object_head(const char* dxf_name, const Object& object, const std::vector<std::string>* reactors) {
    w_.write_object_head(dxf_name, object, reactors);
}

void RecordWriter::entity_head(const char* dxf_name, const Entity& entity, const std::string& owner,
                               const std::vector<uint8_t>* proxy) {
    w_.write_entity_head(dxf_name, entity.props.handle, owner, entity.props, proxy);
}

void RecordWriter::xdata(const std::vector<XdataList>& list) { w_.write_xdata(list); }
void RecordWriter::subclass(const char* name) { w_.write_code(100, name); }
void RecordWriter::text(int code, const std::string& value) { w_.write_code(code, value); }
void RecordWriter::integer(int code, int64_t value) { w_.groups_.write_int(code, value); }
void RecordWriter::boolean(int code, bool value) { w_.groups_.write_int(code, value ? 1 : 0); }
void RecordWriter::real(int code, double value) { w_.groups_.write_double(code, value); }

void RecordWriter::point2(int code, const Point2& p) {
    w_.groups_.write_double(code, p.x);
    w_.groups_.write_double(code + 10, p.y);
}

void RecordWriter::point3(int code, const Point3& p) { w_.write_point(code, p); }

void RecordWriter::bytes(int code, const std::vector<uint8_t>& data) {
    static const char digits[] = "0123456789ABCDEF";
    const size_t chunk = 127;
    for (size_t at = 0; at < data.size(); at += chunk) {
        std::string hex;
        for (size_t i = at; i < data.size() && i < at + chunk; ++i) {
            hex += digits[data[i] >> 4];
            hex += digits[data[i] & 15];
        }
        w_.write_code_typed(code, hex);
    }
}

void RecordWriter::handle(int code, const std::string& handle) {
    if (!handle.empty()) w_.write_code(code, handle);
}

void RecordWriter::pointer(int code, const std::string& handle, PointerWrite how) {
    if (handle.empty()) return;
    if (how == PointerWrite::AsIs || live(handle)) {
        w_.write_code(code, handle);
    } else if (how == PointerWrite::ZeroDead) {
        w_.write_code(code, "0");
    }
}

void RecordWriter::raw(int code, const std::string& value) { w_.write_code_typed(code, value); }

void RecordWriter::surface(const Entity& e, const std::string& owner) { w_.write_surface_r2000(e, owner); }

std::vector<CapturedGroup>* RecordWriter::begin_capture(std::vector<CapturedGroup>& sink) {
    std::vector<CapturedGroup>* before = w_.groups_.capturing();
    w_.groups_.capture(&sink);
    return before;
}

void RecordWriter::end_capture(std::vector<CapturedGroup>* before) { w_.groups_.capture(before); }
void RecordWriter::captured(const CapturedGroup& group) { w_.groups_.write_captured(group); }
void RecordWriter::warn(const std::string& message) { w_.warnings_.push_back(message); }

void RecordWriter::fail(const std::string& message) {
    if (w_.error_.empty()) w_.error_ = message;
}

bool RecordWriter::proxy_graphics_enabled() const {
    return w_.filter_live_ ? w_.proxy_graphics_ : w_.original_proxy_graphics_;
}

const ProxyTables& RecordWriter::proxy_tables() const {
    return w_.filter_live_ ? w_.proxy_tables_ : w_.original_proxy_tables_;
}

} // namespace internal
} // namespace hdxf
