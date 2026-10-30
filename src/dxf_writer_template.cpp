#include "dxf_writer.hpp"
#include "AssocExtended.hpp"
#include "handles.hpp"
#include "TypedRecordTypes.hpp"
#include "dimension_block.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

// Writing a drawing read from DXF 2000 back as 2000 (plan phase 1, step 1.4, B12/B19).
//
// Every element is written twice into a capture: as it is now (N) and as it was read (O,
// from the original — the preserved records read again). Their groups are compared by
// key — (subclass and its occurrence, group code, occurrence of the code in it) — and the
// preserved record (T) is written with only what differs replaced. Groups HDxf does not
// interpret, 102 groups, embedded objects and XDATA that did not change stay as they were.

namespace hdxf {
namespace internal {

namespace {

uint64_t handle_value(const std::string& h) {
    uint64_t v = 0;
    return parse_handle(h, v) == HandleParse::Ok ? v : 0;
}

bool is_pointer(int code) {
    return (code >= 320 && code <= 369) || (code >= 390 && code <= 399);
}

// The role of each group of a record
enum class Role { Key, Foreign, Embedded, Xdata };

struct Segment {
    Role role = Role::Key;
    std::string key;            // Key: "subclass#n|code|occurrence"
    std::string instance;       // the subclass occurrence the group is in
    size_t block_end = 0;       // Foreign, at a 102 {…: the index of its 102 }
    // Key: the same in parts, for the descriptors of the typed records (TypedRecordTypes.hpp)
    std::string subclass;       // the subclass the group is in
    size_t subclass_occurrence = 0;
    size_t ordinal = 0;         // groups with this code taken before it in the subclass occurrence
};

int code_of(const DxfGroup& g) { return g.code; }
int code_of(const CapturedGroup& g) { return g.code; }
const std::string& text_of(const DxfGroup& g) { return g.value; }
const std::string& text_of(const CapturedGroup& g) { return g.text; }

std::string trimmed(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

// XDATA is the tail from a 1001 on where every group is >= 1000
template <typename G>
size_t xdata_begin(const std::vector<G>& g) {
    size_t x = g.size();
    while (x > 1 && code_of(g[x - 1]) >= 1000) --x;
    while (x < g.size() && code_of(g[x]) != 1001) ++x;
    return x;
}

template <typename G>
std::vector<Segment> segment(const std::vector<G>& g, size_t& xdata) {
    xdata = xdata_begin(g);
    std::vector<Segment> s(g.size());
    std::string instance;
    std::map<std::string, int> instances;
    std::map<std::string, int> occurrences;
    bool embedded = false;
    std::string subclass_name;
    size_t subclass_occurrence = 0;
    for (size_t i = 0; i < g.size(); ++i) {
        if (i >= xdata) { s[i].role = Role::Xdata; continue; }
        int code = code_of(g[i]);
        // A 102 group opens with "{name" and closes at the next 102 — "}" or, in XRECORD
        // data, "name}" ({ATTRRECORD … ATTRRECORD})
        if (code == 102 && !text_of(g[i]).empty() && text_of(g[i])[0] == '{') {
            size_t end = i + 1;
            while (end < xdata && !(code_of(g[end]) == 102 &&
                                    (text_of(g[end]).empty() || text_of(g[end])[0] != '{'))) ++end;
            if (end >= xdata) end = xdata - 1;
            for (size_t k = i; k <= end; ++k) s[k].role = Role::Foreign;
            s[i].block_end = end;
            s[i].instance = instance;
            i = end;
            continue;
        }
        if (embedded || code == 101) {
            embedded = true;
            s[i].role = Role::Embedded;
            continue;
        }
        if (code == 100) {
            std::string name = trimmed(text_of(g[i]));
            subclass_name = name;
            subclass_occurrence = instances[name];
            instance = name + "#" + std::to_string(instances[name]++);
        }
        std::string base = instance + "|" + std::to_string(code);
        s[i].ordinal = occurrences[base]++;
        s[i].key = base + "|" + std::to_string(s[i].ordinal);
        s[i].instance = instance;
        s[i].subclass = subclass_name;
        s[i].subclass_occurrence = subclass_occurrence;
    }
    return s;
}

// The codes a subclass's list is made of: per vertex, element or entry
bool list_code(const std::string& subclass, int code) {
    if (subclass == "AcDbPolyline") {
        return code == 10 || code == 20 || code == 40 || code == 41 || code == 42 || code == 91;
    }
    if (subclass == "AcDbLinetypeTableRecord") {
        return code == 49 || code == 74 || code == 75 || code == 340 || code == 46 ||
               code == 50 || code == 44 || code == 45 || code == 9;
    }
    if (subclass == "AcDbDictionary") return code == 3 || code == 350 || code == 360;
    if (subclass == "AcDbMText") return code == 3 || code == 1;     // the text in parts
    if (subclass == "AcDbLeader") return code == 10 || code == 20 || code == 30;
    if (subclass == "AcDbMline") {
        return (code >= 11 && code <= 13) || (code >= 21 && code <= 23) || (code >= 31 && code <= 33) ||
               code == 74 || code == 41 || code == 75 || code == 42;
    }
    if (subclass == "AcDbRasterImage" || subclass == "AcDbWipeout") return code == 14 || code == 24;
    if (subclass == "AcDbModelerGeometry") return code == 1 || code == 3;
    // Proxy graphics (MULTILEADER's): their size and chunks are one value
    if (subclass == "AcDbEntity") return code == 92 || code == 160 || code == 310;
    if (subclass == "AcDbSpline") {
        return code == 40 || code == 41 || code == 10 || code == 20 || code == 30 || code == 11 ||
               code == 21 || code == 31;
    }
    return false;
}

// The groups of a record that belong to a list (B12): a polyline's, leader's or
// multiline's vertices, a linetype's or multiline style's elements, a dictionary's
// entries, MTEXT's text in its parts, an image's clip boundary, ACIS lines, a hatch's
// paths, pattern and seed points (every group from its 91 on: their codes mean different
// things where they stand), and XRECORD's data (every group after its 280, 102 groups too).
// A list has an id: 1 for these; the lists of a typed record (TypedRecordTypes.hpp, ListSpec) have their own, so
// that a subclass can have several. The groups of one list are one unit; 0 is no list.
template <typename G>
std::vector<int> list_groups(const std::vector<G>& g, const std::vector<Segment>& s, const std::string& record_type) {
    std::vector<int> out(g.size(), 0);
    std::string subclass;
    size_t position = 0;            // groups into the subclass
    size_t subclass_occurrence = 0;
    bool positional = false;        // AcDbHatch from its 91 on, AcDbMlineStyle from its 71
    bool from_started = false;      // a typed list that runs from its first code to the end of the subclass
    const std::vector<ListSpec>* typed_lists = TypedRegistry::instance().lists_for(record_type);
    std::map<std::string, size_t> occurrences;
    for (size_t i = 0; i < g.size(); ++i) {
        if (s[i].role == Role::Xdata || s[i].role == Role::Embedded) continue;
        if (s[i].role == Role::Key && code_of(g[i]) == 100) {
            subclass = trimmed(text_of(g[i]));
            subclass_occurrence = occurrences[subclass]++;
            position = 0;
            positional = false;
            from_started = false;
            continue;
        }
        if (typed_lists && s[i].role == Role::Key) {
            // The lists of a typed record, by its descriptors
            if (int id = find_list_in(typed_lists, subclass, subclass_occurrence, code_of(g[i]), from_started)) {
                out[i] = id;
                ++position;
                continue;
            }
        }
        if (subclass == "AcDbMLeader" || subclass == "AcDbExtrudedSurface" || subclass == "AcDbLoftedSurface" ||
            subclass == "AcDbRevolvedSurface" || subclass == "AcDbSweptSurface" || subclass == "AcDbSubDMesh" ||
            subclass == "AcDbGeoData") {
            // Their codes mean different things in each part (B36, B39, B48, B50)
            out[i] = 1;
        } else if (subclass == "AcDbHatch" || subclass == "AcDbMlineStyle") {
            int start = subclass == "AcDbHatch" ? 91 : 71;
            positional = positional || (s[i].role == Role::Key && code_of(g[i]) == start);
            out[i] = positional ? 1 : 0;
        } else if (subclass == "AcDbXrecord") {
            out[i] = (position > 0 || code_of(g[i]) != 280) ? 1 : 0;
        } else {
            out[i] = (s[i].role == Role::Key && list_code(subclass, code_of(g[i]))) ? 1 : 0;
        }
        ++position;
    }
    return out;
}

// One list of one subclass occurrence: its key
std::string list_key(const std::string& instance, int id) { return instance + "|L" + std::to_string(id); }

template <typename G>
std::vector<std::string> chain(const std::vector<G>& g, const std::vector<Segment>& s) {
    std::vector<std::string> c;
    for (size_t i = 0; i < g.size(); ++i) {
        if (s[i].role == Role::Key && code_of(g[i]) == 100) c.push_back(trimmed(text_of(g[i])));
    }
    return c;
}

// The 102 blocks of a record by "{name#occurrence" (a record can have several with the
// same name)
template <typename G>
std::vector<std::string> block_keys(const std::vector<G>& g, const std::vector<Segment>& s) {
    std::vector<std::string> keys(g.size());
    std::map<std::string, int> seen;
    for (size_t i = 0; i < g.size(); ++i) {
        if (s[i].role == Role::Foreign && code_of(g[i]) == 102 && !text_of(g[i]).empty() &&
            text_of(g[i])[0] == '{') {
            keys[i] = text_of(g[i]) + "#" + std::to_string(seen[text_of(g[i])]++);
            i = s[i].block_end;
        }
    }
    return keys;
}

// The 102 blocks of a captured record: their values, by block key
std::map<std::string, std::vector<std::string>> blocks_of(const std::vector<CapturedGroup>& g,
                                                          const std::vector<Segment>& s,
                                                          std::map<std::string, std::string>* instance = nullptr) {
    std::map<std::string, std::vector<std::string>> out;
    std::vector<std::string> keys = block_keys(g, s);
    for (size_t i = 0; i < g.size(); ++i) {
        if (keys[i].empty()) continue;
        auto& list = out[keys[i]];
        if (instance) (*instance)[keys[i]] = s[i].instance;
        for (size_t k = i + 1; k < s[i].block_end; ++k) list.push_back(g[k].text);
        i = s[i].block_end;
    }
    return out;
}

std::string handle_of(const std::vector<CapturedGroup>& rec) {
    for (const auto& g : rec) {
        if (g.code == 5 || g.code == 105) return g.text;
        if (g.code == 100) break;
    }
    return {};
}

std::string handle_of(const std::vector<DxfGroup>& rec) {
    for (const auto& g : rec) {
        if (g.code == 5 || g.code == 105) return g.value;
        if (g.code == 100) break;
    }
    return {};
}

std::vector<std::vector<CapturedGroup>> split(const std::vector<CapturedGroup>& g) {
    std::vector<std::vector<CapturedGroup>> out;
    for (const auto& x : g) {
        if (x.code == 0 || out.empty()) out.emplace_back();
        out.back().push_back(x);
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Live handles and what goes with a record that is gone (B19)
// ---------------------------------------------------------------------------

// Written, or — for a drawing read from a file — neither a record the file had nor one
// made since it was read (the handles above the file's and below the next free one): a
// pointer that led nowhere already in the file is kept as it was
bool DxfWriter::is_live(const std::string& handle) const {
    if (live_.empty()) return true;
    uint64_t v = handle_value(handle);
    if (v == 0 || live_.count(v) > 0) return true;
    // A drawing built in memory: what the writer makes is there; any other handle the drawing no longer has is gone
    if (!template_mode()) return v >= first_handle_;
    if (orig_handles_.count(v) > 0) return false;
    bool made_since_read = v > orig_max_ && v < next_handle_;
    return !made_since_read;
}

// A drawing built in memory: what was taken out of it takes what hangs on it along, as AutoCAD's ERASE does (Codex 9.S, 3).
// live_ is every handle the drawing has; one that is not there (an entity taken out of entities()) is gone, and so is
// - an object whose owner is gone (a solid's history and its graph, an entity's extension dictionary), and what it owns,
// - a record its codec drops with what it refers to (a DIMASSOC without geometry, step 9.17),
// - an associative action whose geometry is gone, with what it owns (step 9.15),
// - a dimensional constraint's DIMENSION with its dependency.
// Pointers, reactors and dictionary entries to them are then left out as for a drawing read from a file (is_live())
void DxfWriter::prepare_owners_built_in_memory(const Drawing& dwg) {
    auto add = [&](const std::string& h) {
        if (uint64_t v = handle_value(h)) live_.insert(v);
    };
    auto entity = [&](const Entity& e) {
        add(e.props.handle);
        if (e.type == EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) add(v.handle);
            add(e.as_polyline().seqend_handle);
        } else if (e.type == EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) add(a.props.handle);
            add(e.as_insert().seqend_handle);
        }
    };
    auto block = [&](const Block& b) {
        add(b.handle);
        add(b.endblk_handle);
        add(b.record.handle);
        for (const auto& e : b.entities) entity(e);
    };
    auto entries = [&](const auto& list) {
        for (const auto& x : list) add(x.handle);
    };
    auto first_handle = [&](const RawRecord& r) {
        for (const auto& g : r.groups()) {
            if (g.code == 5 || g.code == 105) {
                add(g.value);
                return;
            }
        }
    };
    for (const auto& t : dwg.raw_tables()) {
        first_handle(t.header);
        for (const auto& r : t.entries) first_handle(r);
    }
    entries(dwg.vports());
    entries(dwg.linetypes());
    entries(dwg.layers());
    entries(dwg.text_styles());
    entries(dwg.views());
    entries(dwg.ucs_list());
    entries(dwg.app_ids());
    entries(dwg.dim_styles());
    for (const auto& [name, h] : table_handles_) add(h);
    for (const auto& [name, h] : default_handles_) add(h);
    for (const auto& b : generated_dim_blocks_) block(b);
    for (const auto& b : dwg.blocks()) block(b);
    for (const auto& e : dwg.entities()) entity(e);
    for (const auto& o : dwg.objects()) add(o.handle);
    if (live_.empty()) return;

    auto alive = [&](const std::string& handle) { return is_live(handle); };
    auto gone = [&](const std::string& handle) { return !handle.empty() && handle != "0" && !is_live(handle); };
    auto drop = [&](uint64_t h) {
        dropped_objects_.insert(h);
        live_.erase(h);
    };
    bool more = true;
    while (more) {
        more = false;
        for (uint64_t h : assoc_orphaned_actions(dwg, alive, dropped_objects_)) {
            drop(h);
            more = true;
        }
        for (const auto& o : dwg.objects()) {
            const uint64_t h = handle_value(o.handle);
            if (h == 0 || dropped_objects_.count(h)) continue;
            const ObjectCodec* codec = TypedRegistry::instance().find_object(o.type);
            const bool by_codec = codec && codec->dropped_when && codec->dropped_when(o, gone);
            if (!by_codec && (o.owner.empty() || o.owner == "0" || is_live(o.owner))) continue;
            drop(h);
            more = true;
        }
        if (drop_constraint_dimensions(dwg)) more = true;
    }
    if (!dropped_objects_.empty() || !dropped_entities_.empty()) changed_ = true;
}

// A dimensional constraint's DIMENSION goes with its dimension dependency (an ACDBASSOCDEPENDENCY whose body is an
// ASSOCDIMDEPENDENCYBODY), as AutoCAD's ERASE of the constrained geometry takes it (assoc_extended
// constrained_line_erased: the DIMENSION on *ADSK_CONSTRAINTS is gone, its *D block stays). True when one went
bool DxfWriter::drop_constraint_dimensions(const Drawing& dwg) {
    bool any = false;
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::AssocDependency || !dropped_objects_.count(handle_value(o.handle))) continue;
        const auto* data = o.data.get_if<Boxed<AssocDependencyData>>();
        if (!data) continue;
        const AssocDependencyBase& dependency = (**data).dependency;
        const Object* body = dwg.find_object(dependency.dependency_body);
        if (!body || body->type != ObjectType::AssocDimDependencyBody) continue;
        const Entity* dimension = dwg.find_entity(dependency.dependent_on_object);
        if (!dimension || dimension->type != EntityType::Dimension) continue;
        const uint64_t h = handle_value(dimension->props.handle);
        if (dropped_entities_.insert(h).second) {
            live_.erase(h);
            any = true;
        }
    }
    return any;
}

// A dimension read from the file that changed — a DIMENSION, ARC_DIMENSION or LARGE_RADIAL_DIMENSION, in the entities or in a
// block (9.S, Codex 7) — gets its block's geometry anew (B12): the block AutoCAD builds (dimension_block.cpp, R.7 and 9.17) with
// 10, 11, 16 and 42 as built, as for a dimension whose block is missing (generate_dim_blocks()); a DIMENSION the builder cannot
// draw the simpler geometry it had before R.7. Whether it changed is what its record says, written now and as read, both from
// their values (the old test of a non-empty preserved record is gone; one whose record was emptied: below). Called before the
// extents (Codex 9): they count the geometry written.
// The handles are given in prepare_template().
void DxfWriter::prepare_dimension_regeneration(const Drawing& dwg) {
    regenerated_.clear();
    regenerated_order_.clear();
    regenerated_blocks_.clear();
    regenerated_dimensions_.clear();
    auto key_of = [](std::string name) {
        for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        return name;
    };
    std::unordered_map<uint64_t, const Entity*> as_read;
    for (const auto& e : original_->entities()) {
        if (e.dimension_part()) as_read[handle_value(e.props.handle)] = &e;
    }
    for (const auto& b : original_->blocks()) {
        for (const auto& e : b.entities) {
            if (e.dimension_part()) as_read[handle_value(e.props.handle)] = &e;
        }
    }
    TextLayout layout(dwg);
    std::vector<std::string> arrows;
    const TypedRegistry& registry = TypedRegistry::instance();

    auto regenerate = [&](const Entity& e) {
        const DimensionData* d = e.dimension_part();
        if (!d || d->block_name.empty()) return;
        // The dimension as read. One whose preserved record was emptied is not in the drawing as read (that is made of the
        // preserved records): it is written from its values as a dimension built in memory is, and keeps the block it names
        // (generate_dim_blocks(): "a block of its own stays"); a program that wants it drawn anew clears block_name. The
        // geometry built now is no test of a change: the builder matches AutoCAD's blocks but not every one to the last bit
        // (ACadSharp's sample_AC1021 and AC1032, written anew record by record, would get new blocks)
        auto it = as_read.find(handle_value(e.props.handle));
        if (it == as_read.end() || it->second->type != e.type) return;
        const Block* block = dwg.find_block(d->block_name);
        if (!block) return;                                 // a missing block is built by generate_dim_blocks()
        const std::string key = key_of(block->name);
        if (regenerated_.count(key) > 0) return;            // one dimension per block (bit 32 of 70)

        // The record now and as read
        std::vector<CapturedGroup> now, before;
        const std::string saved = error_;
        if (e.type == EntityType::Dimension) {
            groups_.capture(&now);
            write_dimension_r2000(e, "");
            groups_.capture(&before);
            write_dimension_r2000(*it->second, "");
        } else {
            const EntityCodec* codec = registry.find_entity(e.type);
            if (!codec || !codec->write) return;
            RecordWriter rw(*this);
            groups_.capture(&now);
            codec->write(rw, e, "");
            groups_.capture(&before);
            codec->write(rw, *it->second, "");
        }
        groups_.capture(nullptr);
        error_ = saved;
        if (now == before) return;

        std::vector<Entity> geometry;
        DimensionBuild built;
        std::string why;
        if (build_dimension(dwg, layout, e, version_, built, why)) {
            geometry = std::move(built.entities);
            built_dimensions_[&e] = {built.definition_point, built.text_position, built.measurement, built.arc_point};
            for (const auto& a : built.arrows) arrows.push_back(a);
        } else if (e.type == EntityType::Dimension) {
            Block simple;
            dimension_geometry(*d, find_dim_style(d->dim_style, dwg), simple);
            geometry = std::move(simple.entities);
        } else {
            warnings_.push_back(std::string(e.type == EntityType::ArcDimension ? "ARC_DIMENSION " : "LARGE_RADIAL_DIMENSION ") +
                                e.props.handle + " changed but its block cannot be built anew (" + why +
                                "): the block is written as it was read");
            return;
        }
        Block written;
        written.name = block->name;
        written.base_point = block->base_point;
        written.entities = geometry;
        regenerated_blocks_.push_back(std::move(written));
        regenerated_[key] = std::move(geometry);
        regenerated_order_.push_back(key);
        regenerated_dimensions_.insert(&e);
        // B106 (Codex 9): the proxy graphics read show the dimension as it was; it is written without them, as a new one
        const std::vector<uint8_t>* proxy = e.type == EntityType::ArcDimension ? &e.as_arc_dimension().proxy_graphics
                                          : e.type == EntityType::LargeRadialDimension ? &e.as_large_radial_dimension().proxy_graphics
                                          : nullptr;
        if (proxy && !proxy->empty()) {
            warnings_.push_back(std::string(e.type == EntityType::ArcDimension ? "ARC_DIMENSION " : "LARGE_RADIAL_DIMENSION ") +
                                e.props.handle + " changed: its proxy graphics (the dimension as it was) are not written, as for "
                                "a new one; AutoCAD makes them when it saves the drawing");
        }
    };
    for (const auto& e : dwg.entities()) regenerate(e);
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) regenerate(e);
    }

    // The built-in arrowheads the new geometry inserts that the drawing lacks, as generate_dim_blocks() adds them
    for (const auto& name : arrows) {
        Block arrow = builtin_arrow_block(name);
        bool present = dwg.find_block(arrow.name) != nullptr ||
                       std::any_of(generated_dim_blocks_.begin(), generated_dim_blocks_.end(),
                                   [&](const Block& b) { return names_equal(b.name, arrow.name); });
        if (present) continue;
        if (version_ < DxfVersion::R2000) older_arrow_block(arrow);
        generated_dim_blocks_.push_back(std::move(arrow));
    }
    for (const auto& w : layout.warnings()) {
        if (std::find(warnings_.begin(), warnings_.end(), w) == warnings_.end()) warnings_.push_back(w);
    }
    // AutoCAD makes the layer Defpoints for a dimension's definition points
    needs_defpoints_ = !built_dimensions_.empty() && !has_entry_named(dwg.layers(), "Defpoints");
}

void DxfWriter::prepare_template(const Drawing& dwg) {
    std::unordered_set<uint64_t>* target = &live_;
    auto add = [&](const std::string& h) {
        if (uint64_t v = handle_value(h)) target->insert(v);
    };
    auto add_raw = [&](const RawRecord& r) {
        if (r.empty()) return;
        for (const auto& g : r.groups()) {
            if (g.code == 5 || g.code == 105) { add(g.value); break; }
            if (g.code == 100) break;
        }
    };

    // The original's elements by handle
    for (const auto& e : original_->entities()) orig_entities_[handle_value(e.props.handle)] = &e;
    for (const auto& b : original_->blocks()) {
        orig_blocks_[handle_value(b.handle)] = &b;
        orig_records_[handle_value(b.record.handle)] = &b;
        for (const auto& e : b.entities) orig_entities_[handle_value(e.props.handle)] = &e;
    }
    for (const auto& o : original_->objects()) orig_objects_[handle_value(o.handle)] = &o;

    // A dimension that changed has its block's geometry anew (prepare_dimension_regeneration(), before the extents): its
    // entities get new handles here, in the order the dimensions were found (B12)
    for (const std::string& key : regenerated_order_) {
        for (auto& g : regenerated_[key]) g.props.handle = new_handle();
    }

    // Every handle that is written (live_). The handles the file had (orig_handles_, from
    // set_template()) tell which pointers led to a record that is gone — one that led
    // nowhere already in the file stays as it was
    auto entries = [&](const auto& list) { for (const auto& x : list) add(x.handle); };
    auto entity = [&](const Entity& e) {
        add(e.props.handle);
        if (e.type == EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) add(v.handle);
            add(e.as_polyline().seqend_handle);
        } else if (e.type == EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) add(a.props.handle);
            add(e.as_insert().seqend_handle);
        }
    };
    auto block = [&](const Block& b) {
        add(b.handle);
        add(b.endblk_handle);
        add(b.record.handle);
        std::string key = b.name;
        for (auto& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        // A block with new dimension geometry: that is what is written now
        auto regen = regenerated_.find(key);
        if (target == &live_ && regen != regenerated_.end()) {
            for (const auto& e : regen->second) entity(e);
        } else {
            for (const auto& e : b.entities) entity(e);
        }
    };
    auto collect = [&](const Drawing& d) {
        for (const auto& t : d.raw_tables()) {
            add_raw(t.header);
            for (const auto& r : t.entries) add_raw(r);
        }
        entries(d.vports());
        entries(d.linetypes());
        entries(d.layers());
        entries(d.text_styles());
        entries(d.views());
        entries(d.ucs_list());
        entries(d.app_ids());
        entries(d.dim_styles());
        for (const auto& b : d.blocks()) block(b);
        for (const auto& e : d.entities()) entity(e);
        for (const auto& o : d.objects()) add(o.handle);
    };
    for (const auto& [name, h] : table_handles_) add(h);
    for (const auto& [name, h] : default_handles_) add(h);
    for (const auto& b : generated_dim_blocks_) block(b);
    collect(dwg);

    // An object a typed record owns by a hard pointer (a layer index's IDBUFFERs: their own owner group is 0) goes when
    // its owner lets go of it: the owner is gone, or no longer points at it (B19, B104). The file as it was read says
    // what was owned; an owner removed from the objects altogether is not in it, so the hard ownerships taken when the
    // file was read (Drawing::loaded_hard_owned_) count too
    std::unordered_set<uint64_t> owned_in_file;
    std::unordered_map<uint64_t, std::vector<std::string>> owners_now;      // target -> the objects that point at it
    {
        const TypedRegistry& registry = TypedRegistry::instance();
        std::vector<TypedReference> refs;
        auto scan = [&](const Drawing& d, const std::function<void(uint64_t, const std::string&)>& found) {
            for (const auto& o : d.objects()) {
                const ObjectCodec* codec = registry.find_object(o.type);
                if (!codec || !codec->references) continue;
                refs.clear();
                codec->references(o, refs);
                for (const TypedReference& r : refs) {
                    if (r.kind == RefKind::HardOwner) found(handle_value(r.handle), o.handle);
                }
            }
        };
        scan(*original_, [&](uint64_t target, const std::string&) { owned_in_file.insert(target); });
        if (loaded_hard_owned_) {
            for (const auto& [owned, owner] : *loaded_hard_owned_) owned_in_file.insert(owned);
        }
        scan(dwg, [&](uint64_t target, const std::string& owner) { owners_now[target].push_back(owner); });
    }
    auto held_by_owner = [&](uint64_t target) {
        auto it = owners_now.find(target);
        if (it == owners_now.end()) return false;
        return std::any_of(it->second.begin(), it->second.end(), [&](const std::string& owner) {
            return !dropped_objects_.count(handle_value(owner)) && is_live(owner);
        });
    };

    // A record that goes with what it refers to (a DIMASSOC without geometry, step 9.17): the geometry was in the file and is
    // gone now (a link that was broken in the file stays). What the file had is told by its handles: an entity taken out of
    // entities() has lost its preserved record too, so the drawing as read, made from those records, no longer has it
    auto gone = [&](const std::string& handle) {
        if (handle.empty() || handle == "0") return false;
        if (is_live(handle) || dwg.find_entity(handle) || dwg.find_object(handle)) return false;
        const uint64_t value = handle_value(handle);
        if (value != 0 && orig_handles_.count(value) > 0) return true;
        return original_->find_entity(handle) || original_->find_object(handle);
    };

    // An object goes with its owner: an extension dictionary with its entity, its
    // entries with the dictionary …
    bool more = true;
    while (more) {
        more = false;
        // The associative actions whose geometry is gone go too, with what they own (step 9.15, assoc_removal.cpp)
        for (uint64_t h : assoc_orphaned_actions(dwg, [&](const std::string& handle) { return is_live(handle); }, dropped_objects_)) {
            dropped_objects_.insert(h);
            live_.erase(h);
            more = true;
        }
        for (const auto& o : dwg.objects()) {
            uint64_t h = handle_value(o.handle);
            if (dropped_objects_.count(h)) continue;
            if (const ObjectCodec* codec = TypedRegistry::instance().find_object(o.type); codec && codec->dropped_when && codec->dropped_when(o, gone)) {
                dropped_objects_.insert(h);
                live_.erase(h);
                more = true;
                continue;
            }
            bool ownerless = o.owner.empty() || o.owner == "0";
            if (ownerless) {
                if (!owned_in_file.count(h) || held_by_owner(h)) continue;
            } else if (is_live(o.owner)) {
                continue;
            }
            dropped_objects_.insert(h);
            live_.erase(h);
            more = true;
        }
        if (drop_constraint_dimensions(dwg)) more = true;
    }
    if (!dropped_objects_.empty() || !dropped_entities_.empty()) changed_ = true;
}

// ---------------------------------------------------------------------------
// One element
// ---------------------------------------------------------------------------

void DxfWriter::emit(const std::vector<const RawRecord*>& templates, bool entity,
                     const std::function<void()>& now, const std::function<void()>& orig) {
    bool any_template = false;
    for (const RawRecord* t : templates) any_template = any_template || (t && !t->empty());
    if (!template_mode() || !any_template) {
        if (template_mode()) changed_ = true;       // a record that was not in the file
        now();
        return;
    }

    std::vector<CapturedGroup> n, o;
    size_t warnings_before = warnings_.size();
    groups_.capture(&n);
    now();
    groups_.capture(nullptr);
    size_t warnings_now = warnings_.size();
    if (orig) {
        // The original is written for comparison only: what it lacks now is no error, and what
        // it would lose is no warning
        std::string saved = error_;
        filter_live_ = false;
        groups_.capture(&o);
        orig();
        groups_.capture(nullptr);
        filter_live_ = true;
        error_ = saved;
        warnings_.resize(warnings_now);
        // Unchanged: the record as read is written, nothing of it is lost
        if (n == o) warnings_.resize(warnings_before);
    }

    std::vector<std::vector<DxfGroup>> t_records;
    std::unordered_map<uint64_t, size_t> t_by_handle;
    for (const RawRecord* t : templates) {
        if (!t || t->empty()) continue;
        t_records.push_back(t->groups());
        t_by_handle[handle_value(handle_of(t_records.back()))] = t_records.size() - 1;
    }
    std::unordered_map<uint64_t, size_t> o_by_handle;
    std::vector<std::vector<CapturedGroup>> o_records = split(o);
    for (size_t i = 0; i < o_records.size(); ++i) o_by_handle[handle_value(handle_of(o_records[i]))] = i;

    size_t used = 0;
    for (const auto& rec : split(n)) {
        uint64_t h = handle_value(handle_of(rec));
        auto t = t_by_handle.find(h);
        if (h == 0 || t == t_by_handle.end()) {
            changed_ = true;
            for (const auto& g : rec) groups_.write_captured(g);
            continue;
        }
        ++used;
        auto oi = o_by_handle.find(h);
        merge_record(t_records[t->second], oi != o_by_handle.end() ? &o_records[oi->second] : nullptr,
                     rec, entity);
    }
    if (used < t_records.size()) changed_ = true;   // records that are gone
}

// T: the preserved record; O: the original as written now; N: the drawing as written now
void DxfWriter::merge_record(const std::vector<DxfGroup>& t, const std::vector<CapturedGroup>* o,
                             const std::vector<CapturedGroup>& n, bool entity) {
    size_t tx = 0, nx = 0, ox = 0;
    std::vector<Segment> ts = segment(t, tx);
    std::vector<Segment> ns = segment(n, nx);
    std::vector<Segment> os;
    if (o) os = segment(*o, ox);

    // The relation descriptors of a typed record (TypedRecordTypes.hpp, B104): what each handle-like group is,
    // by its subclass occurrence, code and ordinal; a type without descriptors has the code ranges' rule alone
    const std::string record_type = trimmed(t.empty() ? std::string() : t[0].value);
    const std::vector<RefDescriptor>* type_refs = TypedRegistry::instance().refs_for(record_type);
    const bool has_refs = type_refs != nullptr;
    auto ref_of_group = [&](const Segment& s, int code) -> const RefDescriptor* {
        if (!has_refs || s.role != Role::Key) return nullptr;
        return find_reference_in(type_refs, s.subclass, s.subclass_occurrence, code, s.ordinal);
    };

    // A pointer to a record that is gone becomes 0 (none), as AutoCAD writes a null
    // pointer; the record keeps its shape (counts, pairs) (B19)
    auto raw = [&](const DxfGroup& g) {
        if (is_pointer(g.code) && !is_live(g.value)) {
            ++dangling_;
            changed_ = true;
            groups_.write_raw(g.code, "0", source_binary_);
            return;
        }
        groups_.write_raw(g.code, g.value, source_binary_, g.code_columns);
    };
    // `d`: the group's descriptor when it has one - a sort key, an internal id or a handle held as text is
    // never zeroed, a pointer is, whatever its code
    auto put = [&](const CapturedGroup& g, const RefDescriptor* d = nullptr) {
        changed_ = true;
        bool pointer = d ? is_pointer_kind(d->kind) && d->removal != RefRemoval::Keep : is_pointer(g.code);
        if (pointer && g.kind == CapturedGroup::Kind::String && !is_live(g.text)) {
            ++dangling_;            // e.g. in an unknown type's groups, changed
            groups_.write_raw(g.code, "0", source_binary_);
            return;
        }
        groups_.write_captured(g);
    };
    auto put_n = [&](size_t k) { put(n[k], ref_of_group(ns[k], n[k].code)); };

    std::unordered_map<std::string, size_t> tk, nk, ok;
    for (size_t i = 0; i < t.size(); ++i) if (ts[i].role == Role::Key) tk[ts[i].key] = i;
    for (size_t i = 0; i < n.size(); ++i) if (ns[i].role == Role::Key) nk[ns[i].key] = i;
    if (o) for (size_t i = 0; i < o->size(); ++i) if (os[i].role == Role::Key) ok[os[i].key] = i;
    auto unchanged = [&](const std::string& k) {
        return o && ok.count(k) && nk.count(k) && (*o)[ok[k]] == n[nk[k]];
    };

    std::map<std::string, std::string> n_block_instance;
    auto n_blocks = blocks_of(n, ns, &n_block_instance);
    std::map<std::string, std::vector<std::string>> o_blocks;
    if (o) o_blocks = blocks_of(*o, os);
    std::vector<std::string> t_keys = block_keys(t, ts);
    std::set<std::string> t_block_keys(t_keys.begin(), t_keys.end());

    // The 102 blocks HDxf knows: reactors, extension dictionary, block references. Their
    // pointers to records that are gone are left out, and the ones the drawing has now
    // are added. Any other block (an application's, XRECORD data) is written as it was.
    auto known = [](const std::string& key) {
        return key.rfind("{ACAD_REACTORS#", 0) == 0 || key.rfind("{ACAD_XDICTIONARY#", 0) == 0 ||
               key.rfind("{BLKREFS#", 0) == 0;
    };
    // The relations (block references, reactors) of N against O: a record that was read keeps its
    // list as AutoCAD wrote it — AutoCAD leaves a relation out of some lists — and gets only what
    // is new in N added and what is gone from N taken out. A record without a counterpart in O
    // has N's whole list.
    auto added_since = [&](const std::string& key) {
        std::vector<std::string> list;
        auto now = n_blocks.find(key);
        if (now != n_blocks.end()) list = now->second;
        auto was = o_blocks.find(key);
        if (!o || was == o_blocks.end()) return list;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string& h) {
                       return std::find(was->second.begin(), was->second.end(), h) != was->second.end();
                   }), list.end());
        return list;
    };
    auto removed_since = [&](const std::string& key) {
        std::vector<std::string> list;
        auto was = o_blocks.find(key);
        if (!o || was == o_blocks.end()) return list;
        list = was->second;
        const std::vector<std::string>& now = n_blocks[key];
        list.erase(std::remove_if(list.begin(), list.end(), [&](const std::string& h) {
                       return std::find(now.begin(), now.end(), h) != now.end();
                   }), list.end());
        return list;
    };
    auto write_block = [&](const std::string& key, const std::vector<std::string>& kept,
                           const std::vector<DxfGroup>* preserved, size_t from, size_t to,
                           const std::vector<std::string>& removed = {}) {
        std::string name = key.substr(0, key.rfind('#'));
        if (preserved && !known(key)) {
            for (size_t k = from - 1; k <= to; ++k) raw((*preserved)[k]);
            return;
        }
        std::vector<std::pair<int, std::string>> items;
        if (preserved) {
            for (size_t k = from; k < to; ++k) {
                const DxfGroup& g = (*preserved)[k];
                if (is_pointer(g.code) && !is_live(g.value)) { changed_ = true; continue; }
                if (std::find(removed.begin(), removed.end(), g.value) != removed.end()) { changed_ = true; continue; }
                items.push_back({g.code, g.value});
            }
        }
        int pointer_code = name == "{ACAD_REACTORS" ? 330 : (name == "{ACAD_XDICTIONARY" ? 360 : 331);
        for (const auto& h : kept) {
            if (!is_live(h)) continue;
            bool present = std::any_of(items.begin(), items.end(),
                                       [&](const auto& it) { return it.second == h; });
            if (!present) {
                items.push_back({pointer_code, h});
                changed_ = true;
            }
        }
        if (items.empty()) {                            // nothing left to point to
            if (preserved) changed_ = true;
            return;
        }
        groups_.write_raw(102, preserved ? (*preserved)[from - 1].value : name, source_binary_);
        for (const auto& [code, value] : items) groups_.write_raw(code, value, source_binary_);
        groups_.write_raw(102, preserved ? (*preserved)[to].value : "}", source_binary_);
    };
    auto block_from_now = [&](const std::string& key) { return added_since(key); };

    // Regenerate when the subclasses changed (e.g. another dimension type): the head as
    // it was, the rest as it is now
    bool regen = o && chain(*o, os) != chain(n, ns);

    // Lists (B12): one that changed is written whole from N where its first group stood
    // (or last in its subclass, when the record had none); one that did not, as it was
    // (a subclass can have several lists, a typed record's: a unit is a subclass occurrence and a list id)
    std::vector<int> tl = list_groups(t, ts, record_type);
    std::vector<int> nl = list_groups(n, ns, record_type);
    std::map<std::string, std::vector<size_t>> n_list;     // unit → its list in N
    std::map<std::string, std::vector<CapturedGroup>> o_list;
    std::map<std::string, std::string> unit_instance;      // unit → its subclass occurrence
    std::string list_instance;
    for (size_t i = 0; i < n.size(); ++i) {
        if (ns[i].role == Role::Key && n[i].code == 100) list_instance = ns[i].instance;
        if (nl[i]) {
            std::string unit = list_key(list_instance, nl[i]);
            n_list[unit].push_back(i);
            unit_instance[unit] = list_instance;
        }
    }
    if (o) {
        std::vector<int> ol = list_groups(*o, os, record_type);
        list_instance.clear();
        for (size_t i = 0; i < o->size(); ++i) {
            if (os[i].role == Role::Key && (*o)[i].code == 100) list_instance = os[i].instance;
            if (ol[i]) {
                std::string unit = list_key(list_instance, ol[i]);
                o_list[unit].push_back((*o)[i]);
                unit_instance[unit] = list_instance;
            }
        }
    }
    std::set<std::string> list_changed;
    auto compare_list = [&](const std::string& unit) {
        std::vector<CapturedGroup> now;
        for (size_t i : n_list[unit]) now.push_back(n[i]);
        if (!o || now != o_list[unit]) list_changed.insert(unit);
    };
    for (const auto& [unit, list] : n_list) compare_list(unit);
    for (const auto& [unit, list] : o_list) compare_list(unit);
    std::set<std::string> list_written;
    auto write_list = [&](const std::string& unit) {
        if (!list_changed.count(unit) || !list_written.insert(unit).second) return;
        for (size_t i : n_list[unit]) put_n(i);
    };
    // Every list of a subclass occurrence that changed
    auto write_lists_of = [&](const std::string& instance) {
        for (const std::string& unit : std::vector<std::string>(list_changed.begin(), list_changed.end())) {
            if (unit_instance[unit] == instance) write_list(unit);
        }
    };

    // A group the record lacks goes where N has it: before the next group of N in its
    // subclass that the record has (a list: where the list starts); with none, last in
    // its subclass
    std::map<std::string, size_t> t_list_start;            // unit → its list in T
    for (size_t i = 0; i < tx; ++i) {
        if (tl[i] && !t_list_start.count(list_key(ts[i].instance, tl[i]))) t_list_start[list_key(ts[i].instance, tl[i])] = i;
    }
    auto anchor_of = [&](size_t i) -> size_t {
        for (size_t j = i + 1; j < nx; ++j) {
            if (ns[j].role == Role::Key && n[j].code == 100) break;
            if (nl[j]) {
                auto start = t_list_start.find(list_key(ns[i].instance, nl[j]));
                if (start != t_list_start.end()) return start->second;
                continue;
            }
            if (ns[j].role == Role::Key && tk.count(ns[j].key)) return tk[ns[j].key];
        }
        return t.size();
    };
    std::map<size_t, std::vector<size_t>> before;          // T index → N groups before it
    std::map<std::string, std::vector<size_t>> pending;    // instance → N groups to add last
    for (size_t i = 0; i < n.size(); ++i) {
        if (nl[i]) continue;                                // with its list
        if (ns[i].role != Role::Key || tk.count(ns[i].key) || unchanged(ns[i].key)) continue;
        if (o && ok.count(ns[i].key) && (*o)[ok[ns[i].key]] == n[i]) continue;
        size_t anchor = anchor_of(i);
        if (anchor < tx) before[anchor].push_back(i);
        else pending[ns[i].instance].push_back(i);
    }
    std::set<std::string> flushed;
    auto flush = [&](const std::string& instance) {
        if (!flushed.insert(instance).second) return;
        for (size_t i : pending[instance]) put_n(i);
        write_lists_of(instance);       // a list the record did not have
        // 102 blocks the drawing has now but the record did not (BLKREFS, reactors)
        for (const auto& [key, inst] : n_block_instance) {
            if (inst == instance && known(key) && !t_block_keys.count(key)) {
                write_block(key, block_from_now(key), nullptr, 0, 0);
            }
        }
    };

    // The members of a list whose target is gone go with it, as AutoCAD removes them (B19): a draw order
    // (SORTENTSTABLE) pairs an entity (331) with its sort handle (5), and the pair of an entity that is gone
    // goes too. The descriptors say which groups form the member (first_offset ... last_offset around the
    // pointer) and which count follows the list (counter_code); the groups of a kind that is not a pointer
    // stay (B104)
    std::vector<bool> dropped(t.size(), false);
    std::vector<const RefDescriptor*> t_ref(t.size(), nullptr);
    std::map<size_t, int64_t> counter_value;             // T index of a count -> its value now
    if (has_refs) {
        for (size_t i = 0; i < tx; ++i) t_ref[i] = ref_of_group(ts[i], t[i].code);
        std::map<size_t, int> counter_drop;
        for (size_t i = 0; i < tx; ++i) {
            const RefDescriptor* d = t_ref[i];
            if (!d || !is_pointer_kind(d->kind) || is_live(t[i].value)) continue;
            if (d->removal != RefRemoval::DropMember && d->removal != RefRemoval::DropPair) continue;
            long first = static_cast<long>(i) + d->first_offset;
            long last = static_cast<long>(i) + d->last_offset;
            if (first < 0 || last >= static_cast<long>(tx)) continue;
            long far_end = d->last_offset > 0 ? last : (d->first_offset < 0 ? first : static_cast<long>(i));
            if (d->partner_code != 0 && t[static_cast<size_t>(far_end)].code != d->partner_code) continue;
            for (long k = first; k <= last; ++k) dropped[static_cast<size_t>(k)] = true;
            if (d->counter_code != 0) {
                for (long j = first - 1; j >= 0; --j) {
                    if (ts[static_cast<size_t>(j)].role == Role::Key && t[static_cast<size_t>(j)].code == d->counter_code &&
                        ts[static_cast<size_t>(j)].instance == ts[i].instance) {
                        ++counter_drop[static_cast<size_t>(j)];
                        break;
                    }
                }
            }
        }
        for (const auto& [j, dropped_members] : counter_drop) {
            counter_value[j] = std::max<int64_t>(0, std::atoll(t[j].value.c_str()) - dropped_members);
        }
    }
    // A group of the record's own, as it was: a pointer by its descriptor (or the code ranges' rule), a count
    // that lost members with its new value
    auto raw_key = [&](size_t i) {
        const DxfGroup& g = t[i];
        auto count = counter_value.find(i);
        if (count != counter_value.end()) {
            changed_ = true;
            groups_.write_raw(g.code, std::to_string(count->second), source_binary_, g.code_columns);
            return;
        }
        const RefDescriptor* d = t_ref[i];
        if (!d) {
            raw(g);
        } else if (is_pointer_kind(d->kind) && d->removal != RefRemoval::Keep && !is_live(g.value)) {
            ++dangling_;
            changed_ = true;
            groups_.write_raw(g.code, "0", source_binary_);
        } else {
            groups_.write_raw(g.code, g.value, source_binary_, g.code_columns);
        }
    };

    // Embedded objects (101, plan phase 6, B72): the ones HDxf types are in N and O. They stay as
    // they were while they say the same; a changed (or new, or removed) one is written from N in
    // place of the record's first so many, the record's others stay.
    std::vector<size_t> n_embedded, o_embedded;
    for (size_t i = 0; i < nx; ++i) if (ns[i].role == Role::Embedded) n_embedded.push_back(i);
    if (o) for (size_t i = 0; i < ox; ++i) if (os[i].role == Role::Embedded) o_embedded.push_back(i);
    bool embedded_changed = false;
    if (n_embedded.size() != o_embedded.size() || !o) {
        embedded_changed = !n_embedded.empty() || !o_embedded.empty();
    } else {
        for (size_t k = 0; k < n_embedded.size(); ++k) {
            embedded_changed = embedded_changed || !(n[n_embedded[k]] == (*o)[o_embedded[k]]);
        }
    }
    // The parts HDxf typed in the record as it was read: those are replaced by N's (none when the
    // columns were removed); the record's others are not HDxf's and stay
    size_t typed_parts = 0;
    for (size_t i : (o ? o_embedded : n_embedded)) {
        if ((o ? (*o)[i] : n[i]).code == 101) ++typed_parts;
    }
    bool t_has_embedded = false;
    for (size_t i = 0; i < tx; ++i) t_has_embedded = t_has_embedded || ts[i].role == Role::Embedded;
    bool embedded_done = false;

    std::string instance;
    bool in_body = false;
    for (size_t i = 0; i < tx; ++i) {
        const Segment& s = ts[i];
        if (dropped[i]) {
            changed_ = true;
            continue;
        }
        auto ahead = before.find(i);
        if (ahead != before.end()) {
            for (size_t k : ahead->second) put_n(k);
        }
        if (tl[i] && list_changed.count(list_key(s.instance, tl[i]))) {
            write_list(list_key(s.instance, tl[i]));
            if (s.role == Role::Foreign) i = s.block_end;
            continue;
        }
        if (s.role == Role::Foreign) {
            const std::string& key = t_keys[i];
            // An extension dictionary the record did not have goes before its reactors, as
            // AutoCAD writes them
            const std::string xdict_key = "{ACAD_XDICTIONARY#0";
            if (!in_body && key == "{ACAD_REACTORS#0" && n_blocks.count(xdict_key) &&
                !t_block_keys.count(xdict_key)) {
                write_block(xdict_key, block_from_now(xdict_key), nullptr, 0, 0);
                t_block_keys.insert(xdict_key);
            }
            std::vector<std::string> kept;
            if (key == "{ACAD_REACTORS#0") {
                bool objects_changed = !entity && o && o_blocks[key] != n_blocks[key];
                if (objects_changed) {
                    // The object's reactors (typed) changed: now's list instead
                    changed_ = true;
                    write_block(key, n_blocks[key], nullptr, 0, 0);
                    i = s.block_end;
                    continue;
                }
                if (entity) kept = added_since(key);   // e.g. a GROUP it was added to
            } else if (key == xdict_key && o && o_blocks[key] != n_blocks[key]) {
                // The extension dictionary (typed, one pointer) changed or was cleared: now's
                // pointer instead of the one read, none when it was cleared
                changed_ = true;
                write_block(key, n_blocks[key], nullptr, 0, 0);
                i = s.block_end;
                continue;
            } else {
                kept = added_since(key);
            }
            write_block(key, kept, &t, i + 1, s.block_end, removed_since(key));
            i = s.block_end;
            continue;
        }
        if (s.role == Role::Embedded) {
            flush(instance);
            if (!embedded_changed) {
                raw(t[i]);
                continue;
            }
            // The record's parts, each from its 101: the typed ones are replaced by N's
            if (!embedded_done) {
                embedded_done = true;
                changed_ = true;
                for (size_t k : n_embedded) put(n[k]);
            }
            size_t part = 0;
            for (size_t k = 0; k <= i; ++k) if (t[k].code == 101 && ts[k].role == Role::Embedded) ++part;
            if (part > typed_parts) raw(t[i]);
            continue;
        }
        // Key
        if (t[i].code == 100) {
            if (regen) break;           // the rest comes from N
            flush(instance);
            instance = s.instance;
            in_body = true;
        } else if (!in_body && t[i].code == 330) {
            flush("");                  // head blocks the record lacked go before the owner
        }
        const std::string& k = s.key;
        if (nk.count(k)) {
            if (unchanged(k)) raw_key(i);
            else if (o && !ok.count(k) && trimmed(t[i].value) == n[nk[k]].text &&
                     n[nk[k]].kind == CapturedGroup::Kind::String) raw_key(i);
            else put_n(nk[k]);
        } else if (o && ok.count(k)) {
            changed_ = true;            // the drawing no longer has it
        } else {
            raw_key(i);                 // not something HDxf writes: kept
        }
    }

    if (regen) {
        changed_ = true;
        bool body = false;
        for (size_t i = 0; i < nx; ++i) {
            if (n[i].code == 100) body = true;
            if (body) groups_.write_captured(n[i]);
        }
    } else {
        flush(instance);
        for (const auto& [inst, list] : pending) flush(inst);
        for (const auto& [key, inst] : n_block_instance) flush(inst);
        for (const auto& unit : std::vector<std::string>(list_changed.begin(), list_changed.end())) {
            flush(unit_instance[unit]);
        }
        // An embedded object the record did not have: after everything but XDATA
        if (!t_has_embedded && !n_embedded.empty()) {
            changed_ = true;
            for (size_t k : n_embedded) put(n[k]);
        }
    }

    // XDATA: as it was, unless it changed
    bool xdata_same = o && std::equal(o->begin() + static_cast<long>(ox), o->end(),
                                      n.begin() + static_cast<long>(nx), n.end());
    if (xdata_same) {
        for (size_t i = tx; i < t.size(); ++i) raw(t[i]);
    } else {
        for (size_t i = nx; i < n.size(); ++i) put(n[i]);
    }
}

// A record written as it was read, except pointers to records that are gone: left out of
// reactors and block references, 0 (none) elsewhere
void DxfWriter::write_raw_record(const RawRecord& record, int added_to_count) {
    std::vector<DxfGroup> g = record.groups();
    bool in_known = false, counted = added_to_count == 0;
    for (const auto& x : g) {
        if (!counted && x.code == 70) {
            counted = true;
            groups_.write_int(70, std::atoi(x.value.c_str()) + added_to_count);
            continue;
        }
        if (x.code == 102) {
            in_known = x.value == "{ACAD_REACTORS" || x.value == "{BLKREFS";
        } else if (is_pointer(x.code) && !is_live(x.value)) {
            if (in_known) { changed_ = true; continue; }
            ++dangling_;
            changed_ = true;
            groups_.write_raw(x.code, "0", source_binary_);
            continue;
        }
        groups_.write_raw(x.code, x.value, source_binary_);
    }
}

// A TABLE record as read, with the count (70) of the file raised by one for each record the drawing has
// added: AutoCAD does not lower it for a record that is removed (plan F10, doc/facit/14_annotative.scr,
// the _added and _removed files of 2000 ... 2018). DIMSTYLE's list of styles that have been current loses
// the styles that are gone, and its count (71) follows
void DxfWriter::write_table_head_template(const RawTable& table, int added) {
    std::vector<DxfGroup> g = table.header.groups();
    if (table.name != "DIMSTYLE") {
        write_raw_record(table.header, added);
        return;
    }
    std::string subclass;
    std::vector<DxfGroup> out;
    int listed = 0;
    for (const auto& x : g) {
        if (x.code == 100) subclass = trimmed(x.value);
        if (subclass == "AcDbDimStyleTable" && x.code == 340) {
            if (!is_live(x.value)) { changed_ = true; continue; }
            ++listed;
        }
        out.push_back(x);
    }
    subclass.clear();
    bool counted = added == 0;
    for (const auto& x : out) {
        if (!counted && x.code == 70) {
            counted = true;
            groups_.write_int(70, std::atoi(x.value.c_str()) + added);
            continue;
        }
        if (x.code == 100) subclass = trimmed(x.value);
        if (subclass == "AcDbDimStyleTable" && x.code == 71 && std::atoi(x.value.c_str()) != listed) {
            groups_.write_int(71, listed);
            continue;
        }
        groups_.write_raw(x.code, x.value, source_binary_);
    }
}

// ---------------------------------------------------------------------------
// HEADER: each variable as it was, unless it changed
// ---------------------------------------------------------------------------

void DxfWriter::write_header_template(const Drawing& dwg) {
    const RawSection* raw = nullptr;
    for (const auto& s : dwg.raw_sections()) if (s.name == "HEADER") raw = &s;
    std::vector<DxfGroup> t = raw->head.groups();

    // The drawing's values now; $HANDSEED above every handle, $VERSIONGUID new when the
    // drawing changed
    std::vector<HeaderVar> now = dwg.header_vars();
    auto find = [](std::vector<HeaderVar>& vars, const std::string& name) -> HeaderVar* {
        for (auto& v : vars) if (v.name == name) return &v;
        return nullptr;
    };
    std::vector<HeaderVar> before = original_->header_vars();
    // $XCLIPFRAME read with its variable (B43): written as the version has it
    xclipframe_as_written(now, version_);
    xclipframe_as_written(before, version_);
    maintenance_as_written(now, version_);
    maintenance_as_written(before, version_);
    for (const auto& v : now) {
        if (v.name == "$HANDSEED" || v.name == "$VERSIONGUID") continue;
        const HeaderVar* b = find(before, v.name);
        if (!b || b->values != v.values) changed_ = true;
        // A variable that points at a record that is gone ($CMATERIAL, $INTERFEREOBJVS …) is written 0 (B19, step 9.7)
        for (const auto& [code, value] : v.values) {
            if (is_pointer(code) && !is_live(value)) changed_ = true;
        }
    }
    if (now.size() != before.size()) changed_ = true;
    if (version_ == DxfVersion::R12) {
        // R12 (plan F9, F19): the extents as the file has them while the drawing's geometry is as it
        // was read, new ones (the same calculation as for a new drawing) for the space that has
        // changed — unless the program set the variable itself
        auto point = [](const Point3& p) -> std::vector<std::pair<int, std::string>> {
            char x[64], y[64], z[64];
            std::snprintf(x, sizeof(x), "%.17g", p.x);
            std::snprintf(y, sizeof(y), "%.17g", p.y);
            std::snprintf(z, sizeof(z), "%.17g", p.z);
            return {{10, x}, {20, y}, {30, z}};
        };
        auto extents = [&](const char* name, const Point3& p, bool geometry_changed) {
            HeaderVar* v = find(now, name);
            const HeaderVar* b = find(before, name);
            if (!geometry_changed || !v || (b && b->values != v->values)) return;
            v->values = point(p);
        };
        extents("$EXTMIN", ext_min_, model_extents_changed_);
        extents("$EXTMAX", ext_max_, model_extents_changed_);
        extents("$PEXTMIN", pext_min_, paper_extents_changed_);
        extents("$PEXTMAX", pext_max_, paper_extents_changed_);
    }
    // An R12 file read without $HANDSEED has records without handles (5): written without it too, as
    // AutoCAD discards a drawing with $HANDSEED whose records have none
    if (version_ == DxfVersion::R12 && !find(before, "$HANDSEED")) {
        now.erase(std::remove_if(now.begin(), now.end(), [](const HeaderVar& v) { return v.name == "$HANDSEED"; }), now.end());
    }
    if (HeaderVar* seed = find(now, "$HANDSEED")) {
        // Above every handle in use; an unchanged file (no record new, none given a handle) keeps the
        // $HANDSEED it has, also when that is below a handle some writer left in it (B12)
        const HeaderVar* seed_before = find(before, "$HANDSEED");
        if (!changed_ && handles_given_ == 0 && next_handle_ == first_handle_ && seed_before) {
            seed->values = seed_before->values;
        } else {
            seed->values = {{5, format_handle(next_handle_)}};
        }
    }
    if (changed_) {
        if (HeaderVar* guid = find(now, "$VERSIONGUID")) guid->values = {{2, new_guid()}};
        // Saved changed: by whoever saves it now, as AutoCAD writes it — unless the
        // program set another name
        HeaderVar* by = find(now, "$LASTSAVEDBY");
        const HeaderVar* by_before = find(before, "$LASTSAVEDBY");
        if (by && by_before && by->values == by_before->values) by->values = {{1, login_name()}};
    }

    auto write_values = [&](const HeaderVar& v) {
        write_code(9, v.name);
        for (const auto& [code, value] : v.values) {
            if (is_pointer(code) && !is_live(value)) {
                ++dangling_;
                write_code(code, "0");
                continue;
            }
            switch (group_value_type(code)) {
                case GroupValueType::Double:
                    groups_.write_double(code, std::strtod(value.c_str(), nullptr));
                    break;
                case GroupValueType::Int16:
                case GroupValueType::Int32:
                case GroupValueType::Int64:
                case GroupValueType::Bool:
                    groups_.write_int(code, std::strtoll(value.c_str(), nullptr, 10));
                    break;
                default:
                    write_code(code, value);
                    break;
            }
        }
    };

    std::set<std::string> written;
    size_t i = 0;
    while (i < t.size() && t[i].code != 9) groups_.write_raw(t[i].code, t[i].value, source_binary_), ++i;
    while (i < t.size()) {
        size_t end = i + 1;
        while (end < t.size() && t[end].code != 9) ++end;
        std::string name = trimmed(t[i].value);
        HeaderVar* cur = find(now, name);
        const HeaderVar* was = find(before, name);
        if (cur) {
            written.insert(name);
            bool points_nowhere = false;
            for (const auto& [code, value] : cur->values) points_nowhere = points_nowhere || (is_pointer(code) && !is_live(value));
            if (was && was->values == cur->values && !points_nowhere) {
                for (size_t k = i; k < end; ++k) groups_.write_raw(t[k].code, t[k].value, source_binary_);
            } else {
                write_values(*cur);
            }
        }
        i = end;
    }
    for (const auto& v : now) {
        if (!written.count(v.name)) write_values(v);
    }
    marker_section_ = "HEADER";
    write_code(0, "ENDSEC");
}

// The classes of objects and entities added since the file was read, that the file's register does not
// have. R13 and R14: a file's register is its own (some of AutoCAD's have none for the XRECORDs and
// dictionaries they hold): a class is added only when the drawing has more objects or entities of its
// type than the file had, not for the ones the file already had without a class
std::vector<ClassEntry> DxfWriter::classes_added_since_read(const Drawing& dwg, const std::set<std::string>& have,
                                                            const std::set<std::string>& have_cpp) {
    std::map<std::string, int> had_instances;
    if (version_ < DxfVersion::R2000 && original_) {
        for (const auto& c : classes_used(*original_, version_)) had_instances[c.name] = c.count;
    }
    std::vector<ClassEntry> added;
    for (const auto& c : classes_used(dwg, version_, &dropped_objects_, &generated_dim_blocks_)) {
        // (a class under another DXF name, as the reference calls the coordination model, is there)
        if (have.count(c.name) || have_cpp.count(c.cpp_name)) continue;
        if (auto had = had_instances.find(c.name); had != had_instances.end() && c.count <= had->second) continue;
        added.push_back(c);
    }
    return added;
}

// CLASSES as read, and the classes of objects and entities added since that the file did
// not have
void DxfWriter::write_classes_template(const Drawing& dwg, const RawSection& classes) {
    write_raw_record(classes.head);
    std::set<std::string> have, have_cpp;
    class_register_.clear();
    for (const auto& r : classes.records) {
        write_raw_record(r);
        bool is_class = false;
        for (const auto& g : r.groups()) {
            if (g.code == 0) is_class = trimmed(g.value) == "CLASS";
            if (g.code == 1) {
                have.insert(trimmed(g.value));
                if (is_class) class_register_.push_back(trimmed(g.value));
            }
            if (g.code == 2) have_cpp.insert(trimmed(g.value));
        }
    }
    for (const auto& c : classes_added_since_read(dwg, have, have_cpp)) {
        changed_ = true;
        write_class(c);
    }
    marker_section_ = "CLASSES";
    write_code(0, "ENDSEC");
}

// A file with no CLASSES section (R13 and R14 have files like that): none is written unless an object
// or entity made since needs a class
void DxfWriter::write_classes_template_without_section(const Drawing& dwg) {
    std::vector<ClassEntry> added = classes_added_since_read(dwg, {}, {});
    if (added.empty()) return;
    changed_ = true;
    write_code(0, "SECTION");
    write_code(2, "CLASSES");
    for (const auto& c : added) write_class(c);
    write_code(0, "ENDSEC");
}

// An entity with its VERTEX/ATTRIB/SEQEND, from its template when it has one
void DxfWriter::write_entity_template(const Entity& e, const std::string& owner) {
    if (dropped_entities_.count(handle_value(e.props.handle))) return;     // a dimensional constraint whose dependency is gone
    std::vector<const RawRecord*> templates = {&e.raw};
    if (e.type == EntityType::Polyline) {
        for (const auto& v : e.as_polyline().vertices) templates.push_back(&v.raw);
        templates.push_back(&e.as_polyline().seqend_raw);
    } else if (e.type == EntityType::Insert) {
        for (const auto& a : e.as_insert().attributes) templates.push_back(&a.raw);
        templates.push_back(&e.as_insert().seqend_raw);
    }
    const Entity* orig = nullptr;
    if (template_mode()) {
        auto it = orig_entities_.find(handle_value(e.props.handle));
        if (it != orig_entities_.end()) orig = it->second;
    }
    emit(templates, true, [&] { write_entity_r2000(e, owner); },
         orig ? std::function<void()>([&] { write_entity_r2000(*orig, owner); }) : nullptr);
}

// ---------------------------------------------------------------------------
// R12 read from a file (plan F9–F22)
// ---------------------------------------------------------------------------

// The record written from its template: T the record as read, N the drawing's now, O the original's
// as written now (null: the original has no such record)
void DxfWriter::emit_r12(const RawRecord& t, const std::function<void()>& now, const std::function<void()>& orig) {
    if (!r12_template() || t.empty()) {
        if (r12_template()) changed_ = true;        // a record that was not in the file
        now();
        return;
    }
    std::vector<CapturedGroup> n, o;
    size_t warnings_before = warnings_.size();
    groups_.capture(&n);
    now();
    groups_.capture(nullptr);
    size_t warnings_now = warnings_.size();
    if (orig) {
        // The original is written for comparison only: what it lacks now is no error, and what
        // it would lose is no warning
        std::string saved = error_;
        groups_.capture(&o);
        orig();
        groups_.capture(nullptr);
        error_ = saved;
        warnings_.resize(warnings_now);
        if (n == o) warnings_.resize(warnings_before);      // unchanged: the record as read, nothing is lost
    }
    if (n.empty()) {                                        // the drawing no longer writes it
        changed_ = true;
        return;
    }
    merge_record(t.groups(), orig ? &o : nullptr, n, true);
}

bool DxfWriter::table_in_file(const std::string& name) const {
    if (!r12_template() || !drawing_) return false;
    for (const auto& t : drawing_->raw_tables()) {
        if (t.name == name && !t.header.empty()) return true;
    }
    return false;
}

// A table the file lacks and the drawing has nothing in is not made
bool DxfWriter::table_wanted(const std::string& name, bool empty) const {
    return !r12_template() || table_in_file(name) || !empty;
}

// How many of the table's records the drawing has that were read from the file
size_t DxfWriter::records_read(const std::string& name) const {
    if (!drawing_) return 0;
    auto with_raw = [](const auto& records) {
        size_t n = 0;
        for (const auto& r : records) if (!r.raw.empty()) ++n;
        return n;
    };
    if (name == "VPORT") return with_raw(drawing_->vports());
    if (name == "LTYPE") return with_raw(drawing_->linetypes());
    if (name == "LAYER") return with_raw(drawing_->layers());
    if (name == "STYLE") return with_raw(drawing_->text_styles());
    if (name == "VIEW") return with_raw(drawing_->views());
    if (name == "UCS") return with_raw(drawing_->ucs_list());
    if (name == "APPID") return with_raw(drawing_->app_ids());
    if (name == "DIMSTYLE") return with_raw(drawing_->dim_styles());
    return 0;
}

// TABLE and its count (70). A drawing read from a file keeps the count the file had (AutoCAD counts
// records R12 does not export, a VIEW table that is empty has 10) while the table has the records it
// had. With other records it is AutoCAD's rule (plan F10, doc/facit/14_annotative.scr, the _added and
// _removed files): the count of the file is not lowered by a record that is removed, and each record
// that is added raises it by one (STYLE 2 to 3, LTYPE 1 to 2; the records, not the count, are what
// AutoCAD exports).
void DxfWriter::write_table_head_r12(const std::string& name, int count) {
    auto head = [&](int n) {
        write_code(0, "TABLE");
        write_code(2, name);
        write_code_int(70, n);
    };
    const RawTable* raw = nullptr;
    if (r12_template() && drawing_) {
        for (const auto& t : drawing_->raw_tables()) {
            if (t.name == name && !t.header.empty()) raw = &t;
        }
    }
    if (!raw) {
        head(count);
        return;
    }
    int loaded = count;
    if (loaded_table_sizes_) {
        for (const auto& [table, size] : *loaded_table_sizes_) {
            if (table == name) loaded = static_cast<int>(size);
        }
    }
    int in_file = loaded;
    for (const auto& g : raw->header.groups()) {
        if (g.code == 70) in_file = std::atoi(g.value.c_str());
    }
    int added = std::max(0, count - static_cast<int>(records_read(name)));
    int shown = (count == loaded && added == 0) ? loaded : in_file + added;
    emit_r12(raw->header, [&] { head(shown); }, [&] { head(loaded); });
}

// An entity with its VERTEX/ATTRIB/SEQEND, each record from its template when it has one
void DxfWriter::write_entity_r12(const Entity& e) {
    if (!r12_template()) {
        write_single_entity(e);
        return;
    }
    const Entity* was = original_of(e);
    if (was && was->type != e.type) was = nullptr;
    auto against = [&](const std::function<void()>& f) { return was ? f : std::function<void()>(); };

    if (e.type == EntityType::Polyline) {
        const auto& d = e.as_polyline();
        emit_r12(e.raw, [&] { write_polyline_head(e); }, against([&] { write_polyline_head(*was); }));
        for (const auto& v : d.vertices) {
            const Vertex* wv = original_of(v);
            emit_r12(v.raw, [&] { write_vertex(e, v); },
                     wv ? against([&] { write_vertex(*was, *wv); }) : std::function<void()>());
        }
        emit_r12(d.seqend_raw, [&] { write_polyline_seqend(e); }, against([&] { write_polyline_seqend(*was); }));
        return;
    }
    if (e.type == EntityType::Insert) {
        const auto& d = e.as_insert();
        emit_r12(e.raw, [&] { write_insert_head(e); }, against([&] { write_insert_head(*was); }));
        for (const auto& a : d.attributes) {
            const InsertAttribute* wa = original_of(a);
            emit_r12(a.raw, [&] { write_attrib(a, e.props.layer); },
                     wa ? against([&] { write_attrib(*wa, was->props.layer); }) : std::function<void()>());
        }
        emit_r12(d.seqend_raw, [&] { write_insert_seqend(e); }, against([&] { write_insert_seqend(*was); }));
        return;
    }
    emit_r12(e.raw, [&] { write_single_entity(e); }, against([&] { write_single_entity(*was); }));
}

void DxfWriter::pair_entities(const std::vector<Entity>& now, const std::vector<Entity>& was) {
    pair_originals(now, was);
    for (const auto& e : now) {
        const Entity* w = original_of(e);
        if (!w) continue;
        if (w->type != e.type) {
            r12_original_.erase(&e);
        } else if (e.type == EntityType::Polyline) {
            pair_originals(e.as_polyline().vertices, w->as_polyline().vertices);
        } else if (e.type == EntityType::Insert) {
            pair_originals(e.as_insert().attributes, w->as_insert().attributes);
        }
    }
}

// What each record of the drawing is compared with: the one in the drawing as read
void DxfWriter::prepare_r12_template(const Drawing& dwg) {
    r12_original_.clear();
    const Drawing& was = *original_;
    pair_originals(dwg.vports(), was.vports());
    pair_originals(dwg.linetypes(), was.linetypes());
    pair_originals(dwg.layers(), was.layers());
    pair_originals(dwg.text_styles(), was.text_styles());
    pair_originals(dwg.views(), was.views());
    pair_originals(dwg.ucs_list(), was.ucs_list());
    pair_originals(dwg.app_ids(), was.app_ids());
    pair_originals(dwg.dim_styles(), was.dim_styles());
    pair_originals(dwg.blocks(), was.blocks());
    for (const auto& b : dwg.blocks()) {
        if (const Block* wb = original_of(b)) pair_entities(b.entities, wb->entities);
    }
    pair_entities(dwg.entities(), was.entities());
}

// Sections HDxf does not know, as read (THUMBNAILIMAGE and ACDSDATA are typed)
void DxfWriter::write_raw_sections_after_objects(const Drawing& dwg) {
    for (const auto& s : dwg.raw_sections()) {
        if (s.name == "HEADER" || s.name == "CLASSES" || s.name == "TABLES" ||
            s.name == "BLOCKS" || s.name == "ENTITIES" || s.name.empty()) continue;
        write_raw_record(s.head);
        for (const auto& r : s.records) write_raw_record(r);
        marker_section_ = s.name;
        write_code(0, "ENDSEC");
    }
}

} // namespace internal
} // namespace hdxf
