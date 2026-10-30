#include "DynamicBlockTypes.hpp"
#include "object_data_visit.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

// The bases of the dynamic block elements and the elements of step 9.9 (phase 9; plan 2026-10-27-fas-9-typer.md,
// "Evalueringsgraf och dynamiska block", B109; Bilaga B.4, B.5, B.38 … B.43):
//
//   BLOCKGRIPLOCATIONCOMPONENT   AcDbEvalExpr, AcDbBlockGripExpr
//   BLOCKLINEARPARAMETER         AcDbEvalExpr, AcDbBlockElement, AcDbBlockParameter, AcDbBlock2PtParameter, AcDbBlockLinearParameter
//   BLOCKLINEARGRIP              AcDbEvalExpr, AcDbBlockElement, AcDbBlockGrip, AcDbBlockLinearGrip
//   BLOCKVISIBILITYPARAMETER     AcDbEvalExpr, AcDbBlockElement, AcDbBlockParameter, AcDbBlock1PtParameter, AcDbBlockVisibilityParameter
//   BLOCKVISIBILITYGRIP          AcDbEvalExpr, AcDbBlockElement, AcDbBlockGrip, AcDbBlockVisibilityGrip (no group of its own)
//   BLOCKSCALEACTION             AcDbEvalExpr, AcDbBlockElement, AcDbBlockAction, AcDbBlockActionWithBasePt, AcDbBlockScaleAction
//   ACDB_BLOCKREPRESENTATION_DATA, ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION: one subclass each
//
// The groups are what AutoCAD's own files have, 2000 … 2018, ASCII and binary (the dynamic block of doc/facit/
// acad_<version>_dxf_dynamic_blocks.dxf and its saves by script fas9_dynamic_base.scr; ACadSharp's sample_AC1015 … AC1032 for
// BLOCKSCALEACTION). 1010 … 1071 before the XDATA's 1001 are class data of the subclass they stand in (the grip's location, the
// element's 1071), never XDATA: XDATA (ACAUTHENVIRON, the author's environment) is Object::xdata.
//
// The parts that a file shows only in one shape: a property slot is a flag and, when the flag is not 0, an id and a name (every
// file with a flag 1 has both; a flag 0 has neither); the value list of a linear parameter's value set (175) has no group
// AutoCAD's files show (no file has a count above 0), so such a record is not typed — it stays as read, with a warning.

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

// The AcDbEvalExpr a data type holds as `eval` (the convention of the 9.9 and 9.13 types), or null
template <typename T>
EvalExprData* eval_in(T& value) {
    if constexpr (requires { value.eval; }) {
        return &value.eval;
    } else if constexpr (requires { (*value).eval; }) {
        return &(*value).eval;
    } else {
        return nullptr;
    }
}

EvalExprData* eval_of(Object& o) {
    return internal::visit_object_data([](auto& value) { return eval_in(value); }, o.data);
}

Object make_object(ObjectType type, const std::string& handle, const std::string& owner, ObjectData data,
                   bool reactor = true) {
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = owner;
    if (reactor && owner != "0") o.reactors.push_back(owner);
    o.data = std::move(data);
    return o;
}

// An entry in a dictionary at its place in the order AutoCAD keeps (by name, case-sensitive, as its files show:
// "ACAD_ENHANCEDBLOCK" before "AcDbDynamicBlockRoundTripPurgePreventer")
void insert_entry(DictionaryData& d, const std::string& name, const std::string& handle, bool hard) {
    auto at = std::find_if(d.entries.begin(), d.entries.end(), [&](const DictionaryEntry& e) { return e.name > name; });
    d.entries.insert(at, {name, handle, hard});
}

const XdataList* find_xdata(const std::vector<XdataList>& xdata, const char* app) {
    for (const XdataList& x : xdata) {
        if (x.app_name == app) return &x;
    }
    return nullptr;
}

// A GUID in AutoCAD's text form, made from the block's name and handle (FNV-1a over both, twice with another start):
// AutoCAD's own are random, HDxf's are the same every time for the same drawing
std::string guid_of(const std::string& name, const std::string& handle) {
    auto hash = [&](uint64_t h) {
        for (char c : name + "|" + handle) {
            h ^= static_cast<unsigned char>(c);
            h *= 1099511628211ull;
        }
        return h;
    };
    const uint64_t a = hash(1469598103934665603ull);
    const uint64_t b = hash(a ^ 0x9E3779B97F4A7C15ull);
    char text[48];
    std::snprintf(text, sizeof(text), "{%08X-%04X-%04X-%04X-%012llX}", static_cast<unsigned>(a >> 32),
                  static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFFF),
                  static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
    return text;
}

// A copy of an entity of the original block for the anonymous one: its own handles, reactors and extension dictionary are
// not the copy's
void renew_entity(Entity& e) {
    e.props.handle.clear();
    e.props.reactors.clear();
    e.props.xdictionary.clear();
    e.raw = RawRecord();
    if (e.type == EntityType::Polyline) {
        PolylineData& p = e.as_polyline();
        for (Vertex& v : p.vertices) {
            v.handle.clear();
            v.reactors.clear();
            v.raw = RawRecord();
        }
        p.seqend_handle.clear();
        p.seqend_raw = RawRecord();
    } else if (e.type == EntityType::Insert) {
        InsertData& i = e.as_insert();
        for (InsertAttribute& a : i.attributes) {
            a.props.handle.clear();
            a.props.reactors.clear();
            a.props.xdictionary.clear();
            a.raw = RawRecord();
        }
        i.seqend_handle.clear();
        i.seqend_raw = RawRecord();
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Drawing: the graph, its elements and the representations
// ---------------------------------------------------------------------------

const Object* Drawing::dynamic_block_graph(const std::string& block_name) const {
    const Block* block = find_block(block_name);
    if (!block || block->record.xdictionary.empty()) return nullptr;
    const Object* dict = find_object(block->record.xdictionary);
    if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
    for (const DictionaryEntry& e : dict->as_dictionary().entries) {
        if (e.name != "ACAD_ENHANCEDBLOCK") continue;
        const Object* graph = find_object(e.handle);
        return graph && graph->type == ObjectType::EvaluationGraph ? graph : nullptr;
    }
    return nullptr;
}

Object* Drawing::dynamic_block_graph(const std::string& block_name) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->dynamic_block_graph(block_name));
}

Object& Drawing::add_dynamic_block_graph(const std::string& block_name) {
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    scan_handles();
    if (Object* existing = dynamic_block_graph(block_name)) return *existing;

    Block* block = find_block(block_name);
    if (!block) block = &add_block(block_name);
    if (block->record.handle.empty()) block->record.handle = next_handle();
    const std::string record_handle = block->record.handle;

    // The block record's extension dictionary, hard-owning and owned by the record, with no reactors (AutoCAD's own)
    std::string dict_handle = block->record.xdictionary;
    Object* dict = dict_handle.empty() ? nullptr : find_object(dict_handle);
    if (!dict || dict->type != ObjectType::Dictionary) {
        dict_handle = next_handle();
        DictionaryData d;
        d.hard_owner = true;
        objects_.push_back(make_object(ObjectType::Dictionary, dict_handle, record_handle, std::move(d), false));
        block = find_block(block_name);
        block->record.xdictionary = dict_handle;
        dict = find_object(dict_handle);
    }

    const std::string graph_handle = next_handle();
    const std::string purge_handle = next_handle();
    insert_entry(dict->as_dictionary(), "ACAD_ENHANCEDBLOCK", graph_handle, true);
    insert_entry(dict->as_dictionary(), "AcDbDynamicBlockRoundTripPurgePreventer", purge_handle, true);

    // The empty graph AutoCAD writes for a block with no element: 96 = 97 = 1 and nothing after
    objects_.push_back(make_object(ObjectType::EvaluationGraph, graph_handle, dict_handle, EvaluationGraphData()));
    objects_.push_back(make_object(ObjectType::DynamicBlockPurgePreventer, purge_handle, dict_handle,
                                   DynamicBlockPurgePreventerData()));

    // What AutoCAD 2025 puts in the block record's XDATA of a dynamic block definition
    block = find_block(block_name);
    if (!find_xdata(block->record.xdata, "AcDbDynamicBlockTrueName")) {
        XdataList name;
        name.app_name = "AcDbDynamicBlockTrueName";
        name.items.push_back({1000, block->name});
        XdataList guid;
        guid.app_name = "AcDbDynamicBlockGUID";
        guid.items.push_back({1000, guid_of(block->name, record_handle)});
        block->record.xdata.push_back(std::move(name));
        block->record.xdata.push_back(std::move(guid));
    }
    return *find_object(graph_handle);
}

Object& Drawing::add_dynamic_block_element(const std::string& block_name, ObjectType type, ObjectData data) {
    const std::string graph_handle = add_dynamic_block_graph(block_name).handle;
    const std::string handle = next_handle();
    Object o = make_object(type, handle, graph_handle, std::move(data), false);
    // The node: the next id, which is the element's own number in its AcDbEvalExpr
    Object* graph = find_object(graph_handle);
    if (EvalExprData* eval = eval_of(o)) {
        eval->node_id = graph->as_evaluation_graph().add_node(handle);
        if (eval->version == 0) eval->version = 33;
        if (eval->maintenance == 0) eval->maintenance = 327;
    }
    objects_.push_back(std::move(o));
    return objects_.back();
}

const Object* Drawing::dynamic_block_element(const std::string& block_name, int32_t node_id) const {
    const Object* graph = dynamic_block_graph(block_name);
    if (!graph) return nullptr;
    const EvalGraphNode* node = graph->as_evaluation_graph().find_node_by_id(node_id);
    if (!node || node->object.empty()) return nullptr;
    return find_object(node->object);
}

Object* Drawing::dynamic_block_element(const std::string& block_name, int32_t node_id) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->dynamic_block_element(block_name, node_id));
}

bool Drawing::remove_dynamic_block_element(const std::string& handle) {
    auto it = std::find_if(objects_.begin(), objects_.end(), [&](const Object& o) { return same_handle(o.handle, handle); });
    if (it == objects_.end()) return false;
    Object* graph = find_object(it->owner);
    if (!graph || graph->type != ObjectType::EvaluationGraph) return false;
    graph->as_evaluation_graph().remove_object(it->handle);
    // A visibility state that lists the element no longer does
    const std::string removed = it->handle;
    objects_.erase(it);
    for (Object& o : objects_) {
        if (o.type != ObjectType::BlockVisibilityParameter) continue;
        for (BlockVisibilityState& s : o.as_block_visibility_parameter().states) {
            s.elements.erase(std::remove_if(s.elements.begin(), s.elements.end(),
                                            [&](const std::string& h) { return same_handle(h, removed); }),
                             s.elements.end());
        }
    }
    return true;
}

std::string Drawing::dynamic_block_true_name(const Block& block) const {
    if (const XdataList* x = find_xdata(block.record.xdata, "AcDbDynamicBlockTrueName")) {
        for (const XdataItem& item : x->items) {
            if (item.code == 1000) return item.value;
        }
    }
    return block.name;
}

const Block* Drawing::dynamic_block_original(const Block& anonymous) const {
    const XdataList* tag = find_xdata(anonymous.record.xdata, "AcDbBlockRepBTag");
    if (!tag) return nullptr;
    for (const XdataItem& item : tag->items) {
        if (item.code != 1005) continue;
        for (const Block& b : blocks_) {
            if (same_handle(b.record.handle, item.value)) return &b;
        }
    }
    return nullptr;
}

Entity& Drawing::add_dynamic_block_representation(const std::string& block_name, Point3 insertion_point,
                                                  const std::string& layer) {
    add_dynamic_block_graph(block_name);        // the original is a dynamic block definition
    const Block* original = find_block(block_name);
    const std::string original_record = original->record.handle;

    // The next *U number
    size_t number = 0;
    for (const Block& b : blocks_) {
        if (b.name.size() > 2 && b.name[0] == '*' && (b.name[1] == 'U' || b.name[1] == 'u')) {
            number = std::max(number, static_cast<size_t>(std::atoll(b.name.c_str() + 2)));
        }
    }
    Block anonymous;
    anonymous.name = "*U" + std::to_string(number + 1);
    anonymous.base_point = original->base_point;
    anonymous.flags = 1;
    anonymous.layer = original->layer;
    anonymous.entities = original->entities;
    for (Entity& e : anonymous.entities) renew_entity(e);
    anonymous.handle = next_handle();
    anonymous.endblk_handle = next_handle();
    anonymous.record.handle = next_handle();
    anonymous.record.explodable = original->record.explodable;
    anonymous.record.scalable = original->record.scalable;
    XdataList tag;
    tag.app_name = "AcDbBlockRepBTag";
    tag.items.push_back({1070, "1"});
    tag.items.push_back({1005, original_record});
    anonymous.record.xdata.push_back(std::move(tag));

    // The anonymous block record's extension dictionary (empty: AutoCAD's own) and the INSERT's, with the
    // representation: AcDbBlockRepresentation → AcDbRepData → ACDB_BLOCKREPRESENTATION_DATA
    const std::string anonymous_dict = next_handle();
    anonymous.record.xdictionary = anonymous_dict;
    DictionaryData anonymous_dictionary;
    anonymous_dictionary.hard_owner = true;
    objects_.push_back(make_object(ObjectType::Dictionary, anonymous_dict, anonymous.record.handle,
                                   std::move(anonymous_dictionary), false));

    Entity insert;
    insert.type = EntityType::Insert;
    insert.props.layer = layer;
    insert.props.handle = next_handle();
    InsertData data;
    data.block_name = anonymous.name;
    data.insertion_point = insertion_point;
    insert.data = std::move(data);

    const std::string insert_dict = next_handle();
    const std::string representation_dict = next_handle();
    const std::string data_handle = next_handle();
    insert.props.xdictionary = insert_dict;
    DictionaryData insert_entries;
    insert_entries.hard_owner = true;
    insert_entries.entries.push_back({"AcDbBlockRepresentation", representation_dict, true});
    objects_.push_back(make_object(ObjectType::Dictionary, insert_dict, insert.props.handle, std::move(insert_entries), false));
    DictionaryData representation_entries;
    representation_entries.hard_owner = true;
    representation_entries.entries.push_back({"AcDbRepData", data_handle, true});
    objects_.push_back(make_object(ObjectType::Dictionary, representation_dict, insert_dict, std::move(representation_entries)));
    BlockRepresentationData representation;
    representation.original_block_record = original_record;
    objects_.push_back(make_object(ObjectType::BlockRepresentation, data_handle, representation_dict, std::move(representation)));

    anonymous.record.block_refs.push_back(insert.props.handle);
    blocks_.push_back(std::move(anonymous));
    rebuild_block_index();

    // The original's AcDbBlockRepETag: how many representations it has (not established by an AutoCAD file: ej testat)
    int representations = 0;
    for (const Block& b : blocks_) {
        if (const Block* of = dynamic_block_original(b); of && of->record.handle == original_record) ++representations;
    }
    Block* definition = find_block(block_name);
    XdataList* etag = nullptr;
    for (XdataList& x : definition->record.xdata) {
        if (x.app_name == "AcDbBlockRepETag") etag = &x;
    }
    if (!etag) {
        // first of the definition's XDATA, as AutoCAD 2025 writes it
        definition->record.xdata.insert(definition->record.xdata.begin(), XdataList{"AcDbBlockRepETag", {}});
        etag = &definition->record.xdata.front();
    }
    etag->items = {{1070, "1"}, {1071, std::to_string(representations)}};

    entities_.push_back(std::move(insert));
    return entities_.back();
}

// ---------------------------------------------------------------------------
// The codecs
// ---------------------------------------------------------------------------

namespace internal {

std::string describe_record(const RecordView& view) {
    return view.type_name() + " " + view.head().handle;
}

bool read_subclass(const RecordView& view, TypedReadContext& ctx, const char* name,
                   const std::function<void(GroupCursor&)>& read) {
    GroupCursor c(view, name);
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no " + name + " subclass");
    read(c);
    if (!c.ok()) return ctx.fail(c.error());
    if (!c.at_end()) {
        // A later version of the record: the groups stay as the file has them (the preserved record), B105
        const size_t n = c.take_rest().size();
        ctx.warn(describe_record(view) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + name +
                 ") are kept as they were read");
    }
    return true;
}

VersionMatrix dynamic_block_matrix() {
    // AutoCAD's files of 2000, 2004, 2007, 2010, 2013 and 2018 (ASCII and binary) have every one of the types. R12 has no
    // OBJECTS section at all ("<TYPE> requires DXF 2000 or later"); R13 and R14: no AutoCAD file and no grammar (2025 cannot
    // save them), so no claim either way
    VersionMatrix m = VersionMatrix::unestablished();
    m.set(DxfVersion::R12, Generation::None, ErrorBasis::Documented);
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013,
                         DxfVersion::R2018}) {
        m.set(v, Generation::Native);
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

ClassSpec dynamic_block_class(const char* cpp_name) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = 1153;
    c.in_older = false;
    return c;
}

RefDescriptor counted_soft_pointer(const char* subclass, int code, int counter_code) {
    RefDescriptor d;
    d.subclass = subclass;
    d.code = code;
    d.kind = RefKind::SoftPointer;
    d.removal = RefRemoval::DropMember;
    d.counter_code = counter_code;
    return d;
}

namespace {

// A property slot: a flag (`flag_code`) and, when it is not 0, an id and a name. AutoCAD's files have the flag 0 or 1
void read_slot(GroupCursor& c, int flag_code, int id_code, int name_code, std::optional<BlockPropertyLink>& slot) {
    int16_t flag = 0;
    if (!c.require_integer(flag_code, flag)) return;
    if (flag == 0) {
        slot.reset();
        return;
    }
    if (flag != 1) {
        c.fail("the property slot flag (" + std::to_string(flag_code) + ") is " + std::to_string(flag) +
               ": AutoCAD's files have 0 or 1");
        return;
    }
    BlockPropertyLink link;
    c.require_integer(id_code, link.node_id);
    c.require_string(name_code, link.name);
    if (c.ok()) slot = std::move(link);
}

// A list of handles counted by `count_code` and written with `code`
void read_handles(GroupCursor& c, int count_code, int code, std::vector<std::string>& out) {
    size_t n = 0;
    if (!c.count(count_code, 1, n)) {
        if (c.ok()) c.fail("group " + std::to_string(count_code) + " is missing");
        return;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        std::string handle;
        c.require_handle(code, handle);
        out.push_back(std::move(handle));
    }
}

} // namespace

bool pointer_alive(const RecordWriter& w, const std::string& handle) {
    if (handle.empty() || handle == "0") return true;
    if (!w.live(handle)) return false;
    // A drawing built in memory knows nothing of what was removed from it: the record is looked up (an entity, an object or a
    // block record). In a drawing read from a file `live` has said it all, and a link that was broken in the file stays
    if (!w.filtering() || !w.built_in_memory()) return true;
    const Drawing& d = w.drawing();
    if (d.find_entity(handle) || d.find_object(handle)) return true;
    auto same = [&](const std::string& other) {
        auto strip = [](std::string s) {
            size_t i = 0;
            while (i + 1 < s.size() && s[i] == '0') ++i;
            s = s.substr(i);
            for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            return s;
        };
        return !other.empty() && strip(other) == strip(handle);
    };
    for (const Block& b : d.blocks()) {
        if (same(b.record.handle) || same(b.handle) || same(b.endblk_handle)) return true;
    }
    return false;
}

std::vector<std::string> members_still_there(const RecordWriter& w, const std::vector<std::string>& handles) {
    std::vector<std::string> out;
    for (const std::string& h : handles) {
        if (pointer_alive(w, h)) out.push_back(h);
    }
    return out;
}

void add_action_references(const BlockActionData& d, std::vector<TypedReference>& out) {
    for (const std::string& h : d.selection) {
        if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
    }
}

void read_block_element(GroupCursor& c, BlockElementData& d) {
    c.require_string(300, d.name);
    int32_t version = 0, maintenance = 0;
    c.require_integer(98, version);
    c.require_integer(99, maintenance);
    c.require_integer(1071, d.flags);
    if (c.ok()) {
        d.version = version;
        d.maintenance = maintenance;
    }
}

void read_block_parameter(GroupCursor& c, BlockParameterData& d) {
    c.require_integer(280, d.show_properties);
    c.require_integer(281, d.chain_actions);
}

void read_block_1pt_parameter(GroupCursor& c, Block1PtParameterData& d) {
    c.require_point3(1010, d.position);
    c.require_integer(93, d.grip_node_id);
    read_slot(c, 170, 91, 301, d.links[0]);
    if (c.ok()) read_slot(c, 171, 92, 302, d.links[1]);
}

void read_block_2pt_parameter(GroupCursor& c, Block2PtParameterData& d) {
    c.require_point3(1010, d.first);
    c.require_point3(1011, d.second);
    size_t n = 0;
    if (!c.count(170, 1, n)) {
        if (c.ok()) c.fail("group 170 is missing");
        return;
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        int32_t id = 0;
        c.require_integer(91, id);
        d.grip_node_ids.push_back(id);
    }
    for (int k = 0; k < 4 && c.ok(); ++k) read_slot(c, 171 + k, 92 + k, 301 + k, d.links[static_cast<size_t>(k)]);
    c.require_integer(177, d.flag_177);
}

void read_block_grip(GroupCursor& c, BlockGripData& d) {
    c.require_integer(91, d.x_node_id);
    c.require_integer(92, d.y_node_id);
    c.require_point3(1010, d.location);
    c.require_integer(280, d.flag_280);
    c.require_integer(93, d.value_93);
}

void read_block_action(GroupCursor& c, BlockActionData& d) {
    // 70 counts the 91 (the graph nodes the action is connected to), as AutoCAD's files of step 9.13 show
    size_t nodes = 0;
    if (!c.count(70, 1, nodes)) {
        if (c.ok()) c.fail("group 70 is missing");
        return;
    }
    for (size_t i = 0; i < nodes && c.ok(); ++i) {
        int32_t id = 0;
        c.require_integer(91, id);
        d.node_ids.push_back(id);
    }
    read_handles(c, 71, 330, d.selection);
    c.require_point3(1010, d.location);
}

void read_block_action_base_point(GroupCursor& c, BlockActionBasePointData& d) {
    c.require_integer(92, d.x_node_id);
    c.require_integer(93, d.y_node_id);
    c.require_string(301, d.x_name);
    c.require_string(302, d.y_name);
    c.require_point3(1011, d.base_point);
    c.require_integer(280, d.flag_280);
    c.require_point3(1012, d.point_1012);
}

namespace {

// ---------------------------------------------------------------------------
// Reading the types
// ---------------------------------------------------------------------------

bool read_eval(const RecordView& view, TypedReadContext& ctx, EvalExprData& eval) {
    return read_subclass(view, ctx, "AcDbEvalExpr", [&](GroupCursor& c) { read_eval_expr(c, eval); });
}

bool read_element(const RecordView& view, TypedReadContext& ctx, BlockElementData& element) {
    return read_subclass(view, ctx, "AcDbBlockElement", [&](GroupCursor& c) { read_block_element(c, element); });
}

bool too_old(const RecordView& view, TypedReadContext& ctx) {
    if (ctx.version >= DxfVersion::R2000) return false;
    ctx.fail(describe_record(view) + " in DXF R13/R14: no AutoCAD file shows the form there");
    return true;
}

bool read_grip_location(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    BlockGripLocationData d;
    if (!read_eval(view, ctx, d.eval)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockGripExpr", [&](GroupCursor& c) {
            c.require_integer(91, d.parameter_node_id);
            c.require_string(300, d.property_name);
        })) {
        return false;
    }
    o.data = Boxed<BlockGripLocationData>(std::move(d));
    return true;
}

bool read_linear_parameter(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    BlockLinearParameterData d;
    if (!read_eval(view, ctx, d.eval) || !read_element(view, ctx, d.element)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockParameter", [&](GroupCursor& c) { read_block_parameter(c, d.parameter); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlock2PtParameter", [&](GroupCursor& c) { read_block_2pt_parameter(c, d.two_point); })) {
        return false;
    }
    if (!read_subclass(view, ctx, "AcDbBlockLinearParameter", [&](GroupCursor& c) {
            c.require_string(305, d.label);
            c.require_string(306, d.description);
            c.require_real(140, d.label_offset);
            c.require_string(307, d.expression);
            c.require_integer(96, d.value_set_flags);
            c.require_real(141, d.constraints[0]);
            c.require_real(142, d.constraints[1]);
            c.require_real(143, d.constraints[2]);
            size_t values = 0;
            if (!c.count(175, 1, values)) {
                if (c.ok()) c.fail("group 175 is missing");
            } else {
                for (size_t i = 0; i < values && c.ok(); ++i) {
                    double value = 0.0;
                    c.require_real(144, value);
                    d.values.push_back(value);
                }
            }
        })) {
        return false;
    }
    o.data = Boxed<BlockLinearParameterData>(std::move(d));
    return true;
}

bool read_linear_grip(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    BlockLinearGripData d;
    if (!read_eval(view, ctx, d.eval) || !read_element(view, ctx, d.element)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockGrip", [&](GroupCursor& c) { read_block_grip(c, d.grip); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlockLinearGrip", [&](GroupCursor& c) {
            c.require_real(140, d.vector.x);
            c.require_real(141, d.vector.y);
            c.require_real(142, d.vector.z);
        })) {
        return false;
    }
    o.data = Boxed<BlockLinearGripData>(std::move(d));
    return true;
}

bool read_visibility_parameter(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    BlockVisibilityParameterData d;
    if (!read_eval(view, ctx, d.eval) || !read_element(view, ctx, d.element)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockParameter", [&](GroupCursor& c) { read_block_parameter(c, d.parameter); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlock1PtParameter", [&](GroupCursor& c) { read_block_1pt_parameter(c, d.one_point); })) {
        return false;
    }
    if (!read_subclass(view, ctx, "AcDbBlockVisibilityParameter", [&](GroupCursor& c) {
            c.require_integer(281, d.flag_281);
            c.require_string(301, d.name);
            c.require_string(302, d.description);
            c.require_integer(91, d.value_91);
            read_handles(c, 93, 331, d.entities);
            size_t states = 0;
            if (!c.count(92, 3, states)) {
                if (c.ok()) c.fail("group 92 is missing");
                return;
            }
            for (size_t i = 0; i < states && c.ok(); ++i) {
                BlockVisibilityState s;
                c.require_string(303, s.name);
                read_handles(c, 94, 332, s.entities);
                if (c.ok()) read_handles(c, 95, 333, s.elements);
                if (c.ok()) d.states.push_back(std::move(s));
            }
        })) {
        return false;
    }
    o.data = Boxed<BlockVisibilityParameterData>(std::move(d));
    return true;
}

bool read_visibility_grip(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    BlockVisibilityGripData d;
    if (!read_eval(view, ctx, d.eval) || !read_element(view, ctx, d.element)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockGrip", [&](GroupCursor& c) { read_block_grip(c, d.grip); })) return false;
    // AcDbBlockVisibilityGrip has no group of its own, but its marker must be there
    if (!read_subclass(view, ctx, "AcDbBlockVisibilityGrip", [](GroupCursor&) {})) return false;
    o.data = Boxed<BlockVisibilityGripData>(std::move(d));
    return true;
}

bool read_scale_action(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    BlockScaleActionData d;
    if (!read_eval(view, ctx, d.eval) || !read_element(view, ctx, d.element)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockAction", [&](GroupCursor& c) { read_block_action(c, d.action); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlockActionWithBasePt", [&](GroupCursor& c) {
            read_block_action_base_point(c, d.base_point_action);
        })) {
        return false;
    }
    if (!read_subclass(view, ctx, "AcDbBlockScaleAction", [&](GroupCursor& c) {
            // The ids of the three properties (94 Scale, 95 XScale, 96 YScale), then their names (303, 304, 305)
            for (int k = 0; k < 3; ++k) c.require_integer(94 + k, d.properties[static_cast<size_t>(k)].node_id);
            for (int k = 0; k < 3; ++k) c.require_string(303 + k, d.properties[static_cast<size_t>(k)].name);
        })) {
        return false;
    }
    o.data = Boxed<BlockScaleActionData>(std::move(d));
    return true;
}

bool read_representation(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    BlockRepresentationData d;
    if (!read_subclass(view, ctx, "AcDbBlockRepresentationData", [&](GroupCursor& c) {
            c.require_integer(70, d.version);
            c.require_handle(340, d.original_block_record);
        })) {
        return false;
    }
    o.data = std::move(d);
    return true;
}

bool read_purge_preventer(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (too_old(view, ctx)) return false;
    DynamicBlockPurgePreventerData d;
    if (!read_subclass(view, ctx, "AcDbDynamicBlockPurgePreventer", [&](GroupCursor& c) { c.require_integer(70, d.version); })) {
        return false;
    }
    o.data = std::move(d);
    return true;
}

// ---------------------------------------------------------------------------
// References
// ---------------------------------------------------------------------------

void visibility_references(const Object& o, std::vector<TypedReference>& out) {
    const BlockVisibilityParameterData& d = o.as_block_visibility_parameter();
    auto add = [&](const std::string& h) {
        if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
    };
    for (const std::string& h : d.entities) add(h);
    for (const BlockVisibilityState& s : d.states) {
        for (const std::string& h : s.entities) add(h);
        for (const std::string& h : s.elements) add(h);
    }
}

void scale_action_references(const Object& o, std::vector<TypedReference>& out) {
    add_action_references(o.as_block_scale_action().action, out);
}

void representation_references(const Object& o, std::vector<TypedReference>& out) {
    const std::string& h = o.as_block_representation().original_block_record;
    if (!h.empty() && h != "0") out.push_back({h, RefKind::HardPointer, false});
}

ListSpec codes_list(const char* subclass, std::vector<int> codes) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::Codes;
    s.codes = std::move(codes);
    return s;
}

ListSpec from_code_list(const char* subclass, int code) {
    ListSpec s;
    s.subclass = subclass;
    s.id = 1;
    s.shape = ListShape::FromCode;
    s.codes = {code};
    return s;
}

} // namespace

void register_dynamic_block_base_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "BLOCKGRIPLOCATIONCOMPONENT";
        c.type = ObjectType::BlockGripLocation;
        c.read = read_grip_location;
        c.write = write_block_grip_location;
        c.cls = dynamic_block_class("AcDbBlockGripExpr");
        c.versions = dynamic_block_matrix();
        c.step = 9;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "BLOCKLINEARPARAMETER";
        c.type = ObjectType::BlockLinearParameter;
        c.read = read_linear_parameter;
        c.write = write_block_linear_parameter;
        c.cls = dynamic_block_class("AcDbBlockLinearParameter");
        c.versions = dynamic_block_matrix();
        // 170 counts the 91 (the grip node ids): one list
        c.lists = {codes_list("AcDbBlock2PtParameter", {170, 91}), codes_list("AcDbBlockLinearParameter", {175, 144})};
        c.step = 9;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "BLOCKLINEARGRIP";
        c.type = ObjectType::BlockLinearGrip;
        c.read = read_linear_grip;
        c.write = write_block_linear_grip;
        c.cls = dynamic_block_class("AcDbBlockLinearGrip");
        c.versions = dynamic_block_matrix();
        c.step = 9;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "BLOCKVISIBILITYPARAMETER";
        c.type = ObjectType::BlockVisibilityParameter;
        c.read = read_visibility_parameter;
        c.write = write_block_visibility_parameter;
        c.references = visibility_references;
        c.cls = dynamic_block_class("AcDbBlockVisibilityParameter");
        c.versions = dynamic_block_matrix();
        // The entities (93, 331) and the states (92, then 303 94 332 95 333 each) are one list from the first count: a state
        // taken out or a member added renumbers the counts
        c.lists = {from_code_list("AcDbBlockVisibilityParameter", 93)};
        // A member of a list whose target is gone goes, and its count follows (B19)
        c.refs = {counted_soft_pointer("AcDbBlockVisibilityParameter", 331, 93),
                  counted_soft_pointer("AcDbBlockVisibilityParameter", 332, 94),
                  counted_soft_pointer("AcDbBlockVisibilityParameter", 333, 95)};
        c.step = 9;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "BLOCKVISIBILITYGRIP";
        c.type = ObjectType::BlockVisibilityGrip;
        c.read = read_visibility_grip;
        c.write = write_block_visibility_grip;
        c.cls = dynamic_block_class("AcDbBlockVisibilityGrip");
        c.versions = dynamic_block_matrix();
        c.step = 9;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "BLOCKSCALEACTION";
        c.type = ObjectType::BlockScaleAction;
        c.read = read_scale_action;
        c.write = write_block_scale_action;
        c.references = scale_action_references;
        c.cls = dynamic_block_class("AcDbBlockScaleAction");
        c.versions = dynamic_block_matrix();
        c.lists = block_action_lists();
        c.refs = {counted_soft_pointer("AcDbBlockAction", 330, 71)};
        c.step = 9;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDB_BLOCKREPRESENTATION_DATA";
        c.type = ObjectType::BlockRepresentation;
        c.read = read_representation;
        c.write = write_block_representation;
        c.references = representation_references;
        c.cls = dynamic_block_class("AcDbBlockRepresentationData");
        c.versions = dynamic_block_matrix();
        // The hard pointer to the original block's BLOCK_RECORD: 0 when that record is gone
        RefDescriptor original;
        original.subclass = "AcDbBlockRepresentationData";
        original.code = 340;
        original.kind = RefKind::HardPointer;
        original.removal = RefRemoval::ZeroInPlace;
        c.refs = {original};
        c.step = 9;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION";
        c.type = ObjectType::DynamicBlockPurgePreventer;
        c.read = read_purge_preventer;
        c.write = write_dynamic_block_purge_preventer;
        c.cls = dynamic_block_class("AcDbDynamicBlockPurgePreventer");
        c.versions = dynamic_block_matrix();
        c.step = 9;
        registry.add(std::move(c));
    }
}

void register_graph_block_codecs(TypedRegistry& registry) {
    register_evaluation_graph_codec(registry);
    register_dynamic_block_base_codecs(registry);
}

} // namespace internal
} // namespace hdxf
