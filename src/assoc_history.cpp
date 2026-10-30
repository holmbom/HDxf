#include "AssocExtended.hpp"
#include <map>

// The history entries of the persistent subentity managers (phase 9, step 9.15; plan 2026-10-27-fas-9-typer.md, F9.11). 9.10
// typed the managers without history and left the 59 of 306 in the corpus that have entries as they were read; the grammar is
// derived here from AutoCAD's own files: doc/facit/fas9_assoc_manager.scr (a box, cylinder, sphere, cone, wedge, torus and
// pyramid, FILLETEDGE of one and of two edges, CHAMFEREDGE, UNION, SUBTRACT, INTERSECT, EXTRUDE and REVOLVE, one operation
// at a time, saved after each, in 2000 … 2018, both formats), fas9_assoc_extended.scr (surface operations, dimensions on solids),
// fas9_assoc_core.scr, the solids and surfaces files of 9.10 and the corpus' Surface_2004 and example_*.
//
// Every entry is `kind, 0, n, the n ints of its payload, S, the S ints of its block` and, in the assoc manager's form, `count
// + that many doubles`. The payload is a step id and groups of ids that AutoCAD separates with 0 (the ids of the steps that made
// the subentities the entry names). The block, by kind:
//   7  S zeros (6 in the assoc manager's form, 4 in the base manager's): the step of a surface made from curves
//   3, 4, 5  name records: an optional id (1, id, parameter, or 0), 0, kind (0 edge, 1 vertex), a count and that many faces
//            (1, id, parameter), then the slots. A box's edge entry is an edge record and the two vertex records at its ends;
//            a vertex entry is one vertex record. The assoc manager's form has two zeros before every record. How many slots a
//            record has is not written: the records are cut where the next one can start (the shortest slots that let the
//            rest of the block parse)
//   9  [zeros], two counted lists of ids (1, id, parameter)
//   6  a container: its payload is its step and the payloads of its items, its block zeros (3, or 1 in the base form), the
//            number of items and the items as `kind, 0, n, S, block` (and a count of doubles in the assoc form) without payloads
//   8  SURFTRIM (one entry per trimmed surface and cutting edge): a container whose every item is followed by a counted list
//            of paths; its payload is the step and, for every item, the item's payload and one step per id of its paths
//   10 SURFFILLET: 3 zeros, a count of surfaces and for each a counted list of paths (its faces), then a count of groups, each a
//            count of lists of ints (for each surface the indices of the paths the fillet stands on); its payload is the step and
//            one step per id of the paths
// A path is `count, then id, parameter` for each id (the name records' `1, id, parameter` is a path of one). The kinds 8 and 10
// are derived from doc/facit/fas9_assoc_history_surf.scr (step 9.15, Opus: SURFTRIM of two surfaces by one circle, by a line,
// by a vertical surface, by two circles, two crossing walls; SURFFILLET of two walls with and without trimming, of a plane and a
// wall, of a plane and a wall of two faces, of two oblique walls), fas9_assoc_extended.scr and fas9_surfaces.scr (a rectangular
// hole: four items; a hole in a revolved surface: two paths of two ids, four steps). They occur in the assoc manager only: in
// the base manager's form they are not established.

namespace hdxf {
namespace internal {

namespace {

constexpr int32_t kNoId = 101000;

// ---------------------------------------------------------------------------
// The blocks of ints
// ---------------------------------------------------------------------------

bool take_ints(GroupCursor& c, size_t n, std::vector<int32_t>& out) {
    out.clear();
    out.reserve(n);
    for (size_t i = 0; i < n && c.ok(); ++i) {
        int32_t v = 0;
        c.require_integer(90, v);
        out.push_back(v);
    }
    return c.ok();
}

PersHistoryId make_id(int32_t id, int32_t parameter) {
    PersHistoryId out;
    out.id = id;
    out.parameter = parameter;
    return out;
}

// The name records of a block: cut where the next record can start. `memo[pos]`: 0 not tried, 1 fails, 2 a way exists
struct RecordParser {
    const std::vector<int32_t>& b;
    bool new_form;
    std::vector<unsigned char> memo;
    std::vector<size_t> tail_of;                // the slots the record at pos has
    explicit RecordParser(const std::vector<int32_t>& block, bool nf) : b(block), new_form(nf), memo(block.size() + 1, 0), tail_of(block.size() + 1, 0) {}

    // The header of the record at `pos`: fills `r` (without the slots) and returns the position after its faces, or npos
    size_t head(size_t pos, PersHistoryRecord& r) const {
        size_t p = pos;
        auto need = [&](size_t k) { return p + k <= b.size(); };
        if (new_form) {
            if (!need(2) || b[p] != 0 || b[p + 1] != 0) return npos;
            p += 2;
        }
        if (!need(1)) return npos;
        if (b[p] == 1) {
            if (!need(3)) return npos;
            r.id = make_id(b[p + 1], b[p + 2]);
            p += 3;
        } else if (b[p] == 0) {
            r.id.reset();
            p += 1;
        } else {
            return npos;
        }
        if (!need(3) || b[p] != 0) return npos;
        r.reserved = b[p];
        r.kind = b[p + 1];
        const int64_t n = b[p + 2];
        p += 3;
        if (n < 0 || static_cast<uint64_t>(n) > (b.size() - p) / 3) return npos;
        r.faces.clear();
        // a face is a path: `count, then id, parameter` for each id (1 in most files; 2 for a face of a wall of two segments)
        for (int64_t i = 0; i < n; ++i) {
            if (!need(1)) return npos;
            const int64_t k = b[p];
            if (k < 1 || static_cast<uint64_t>(k) > (b.size() - p - 1) / 2) return npos;
            PersHistoryId face = make_id(b[p + 1], b[p + 2]);
            for (int64_t j = 1; j < k; ++j) face.more.push_back(make_id(b[p + 1 + 2 * j], b[p + 2 + 2 * j]));
            r.faces.push_back(std::move(face));
            p += 1 + 2 * static_cast<size_t>(k);
        }
        return p;
    }

    bool fits(size_t pos) {
        if (pos == b.size()) return true;
        if (memo[pos]) return memo[pos] == 2;
        PersHistoryRecord r;
        size_t p = head(pos, r);
        bool ok = false;
        if (p != npos) {
            for (size_t slots = 0; p + slots <= b.size() && slots < 10; ++slots) {
                if (fits(p + slots)) {
                    tail_of[pos] = slots;
                    ok = true;
                    break;
                }
            }
        }
        memo[pos] = ok ? 2 : 1;
        return ok;
    }

    bool run(std::vector<PersHistoryRecord>& out) {
        if (!fits(0)) return false;
        size_t pos = 0;
        while (pos < b.size()) {
            PersHistoryRecord r;
            size_t p = head(pos, r);
            for (size_t i = 0; i < tail_of[pos]; ++i) r.slots.push_back(b[p + i]);
            out.push_back(std::move(r));
            pos = p + tail_of[pos];
        }
        return true;
    }

    static constexpr size_t npos = static_cast<size_t>(-1);
};

// The three groups' worth of a payload: step first, then groups separated by 0
void split_payload(const std::vector<int32_t>& payload, PersHistoryItem& item) {
    item.step = payload.empty() ? 0 : payload[0];
    item.groups.clear();
    if (payload.size() > 1) {
        item.groups.emplace_back();
        for (size_t i = 1; i < payload.size(); ++i) {
            if (payload[i] == 0) item.groups.emplace_back();
            else item.groups.back().push_back(payload[i]);
        }
    }
}

void join_payload(const PersHistoryItem& item, std::vector<int32_t>& payload) {
    payload.push_back(item.step);
    for (size_t g = 0; g < item.groups.size(); ++g) {
        if (g > 0) payload.push_back(0);
        for (int32_t v : item.groups[g]) payload.push_back(v);
    }
}

// A counted list of paths at `p` (kinds 8 and 10): `count, then for each path: count, then id, parameter`. The ids' steps are
// set by the caller from the payload
bool take_paths(const std::vector<int32_t>& b, size_t& p, std::vector<PersHistoryPath>& out, std::string& error) {
    out.clear();
    if (p >= b.size()) { error = "a list of paths is missing"; return false; }
    const int64_t n = b[p++];
    if (n < 0 || static_cast<uint64_t>(n) > b.size() - p) { error = "a list of paths longer than its block"; return false; }
    for (int64_t i = 0; i < n; ++i) {
        if (p >= b.size()) { error = "a path is missing"; return false; }
        const int64_t k = b[p++];
        if (k < 0 || static_cast<uint64_t>(k) > (b.size() - p) / 2) { error = "a path longer than its block"; return false; }
        PersHistoryPath path;
        for (int64_t j = 0; j < k; ++j) {
            path.ids.push_back({b[p], b[p + 1], 0});
            p += 2;
        }
        out.push_back(std::move(path));
    }
    return true;
}

void put_paths(const std::vector<PersHistoryPath>& paths, std::vector<int32_t>& block, std::vector<int32_t>& steps) {
    block.push_back(static_cast<int32_t>(paths.size()));
    for (const PersHistoryPath& path : paths) {
        block.push_back(static_cast<int32_t>(path.ids.size()));
        for (const PersHistoryPathId& id : path.ids) {
            block.push_back(id.id);
            block.push_back(id.parameter);
            steps.push_back(id.step);
        }
    }
}

size_t path_id_count(const std::vector<PersHistoryPath>& paths) {
    size_t n = 0;
    for (const PersHistoryPath& path : paths) n += path.ids.size();
    return n;
}

// The steps of the paths' ids, in order, from the payload at `at`
void set_path_steps(std::vector<PersHistoryPath>& paths, const std::vector<int32_t>& payload, size_t& at) {
    for (PersHistoryPath& path : paths) {
        for (PersHistoryPathId& id : path.ids) id.step = payload[at++];
    }
}

// An item's block as the grammar of its kind reads it
bool read_item_block(PersHistoryItem& item, const std::vector<int32_t>& block, bool new_form, std::string& error) {
    switch (item.kind) {
        case 7:
            for (int32_t v : block) {
                if (v != 0) { error = "a kind 7 entry whose block is not zeros"; return false; }
            }
            item.zero_count = static_cast<int32_t>(block.size());
            return true;
        case 3:
        case 4:
        case 5: {
            RecordParser parser(block, new_form);
            if (!parser.run(item.records)) {
                error = "the name records of a kind " + std::to_string(item.kind) + " entry do not fit the grammar derived from AutoCAD's files";
                return false;
            }
            return true;
        }
        case 9: {
            size_t p = 0;
            const size_t lead = new_form ? 3 : 1;
            if (block.size() < lead) { error = "a kind 9 entry's block is too short"; return false; }
            for (size_t i = 0; i < lead; ++i) {
                if (block[i] != 0) { error = "a kind 9 entry's block does not start with zeros"; return false; }
            }
            p = lead;
            item.lists.clear();
            for (int list = 0; list < 2; ++list) {
                if (p >= block.size()) { error = "a kind 9 entry's block has fewer than two lists"; return false; }
                const int64_t n = block[p++];
                if (n < 0 || static_cast<uint64_t>(n) > (block.size() - p) / 3) { error = "a kind 9 list longer than its block"; return false; }
                PersHistoryList l;
                for (int64_t i = 0; i < n; ++i) {
                    if (p >= block.size()) { error = "a kind 9 list member is missing"; return false; }
                    const int64_t k = block[p];
                    if (k < 1 || static_cast<uint64_t>(k) > (block.size() - p - 1) / 2) {
                        error = "a kind 9 list member that is not count, then id, parameter for each";
                        return false;
                    }
                    PersHistoryId id = make_id(block[p + 1], block[p + 2]);
                    for (int64_t j = 1; j < k; ++j) id.more.push_back(make_id(block[p + 1 + 2 * j], block[p + 2 + 2 * j]));
                    l.ids.push_back(std::move(id));
                    p += 1 + 2 * static_cast<size_t>(k);
                }
                item.lists.push_back(std::move(l));
            }
            if (p != block.size()) { error = "a kind 9 entry's block has more than two lists"; return false; }
            return true;
        }
        default:
            error = "the entry kind " + std::to_string(item.kind) + " is not established as an item (AutoCAD's files have 3, 4, 5, 7 and 9 as items; "
                    "6, 8 and 10 as entries of their own)";
            return false;
    }
}

bool build_item_block(const PersHistoryItem& item, bool new_form, std::vector<int32_t>& block, std::string& error) {
    switch (item.kind) {
        case 7:
            block.assign(static_cast<size_t>(std::max(0, item.zero_count)), 0);
            return true;
        case 3:
        case 4:
        case 5:
            for (const PersHistoryRecord& r : item.records) {
                if (new_form) { block.push_back(0); block.push_back(0); }
                if (r.id && !r.id->more.empty()) { error = "a name record's own id of more than one id is not established"; return false; }
                if (r.id) { block.push_back(1); block.push_back(r.id->id); block.push_back(r.id->parameter); }
                else block.push_back(0);
                block.push_back(r.reserved);
                block.push_back(r.kind);
                block.push_back(static_cast<int32_t>(r.faces.size()));
                for (const PersHistoryId& f : r.faces) {
                    block.push_back(static_cast<int32_t>(1 + f.more.size()));
                    block.push_back(f.id);
                    block.push_back(f.parameter);
                    for (const PersHistoryId& m : f.more) {
                        if (!m.more.empty()) { error = "an id of a face's path with ids of its own"; return false; }
                        block.push_back(m.id);
                        block.push_back(m.parameter);
                    }
                }
                for (int32_t s : r.slots) block.push_back(s);
            }
            return true;
        case 9: {
            if (!new_form) { error = "a kind 9 entry in the base manager's form is not established"; return false; }
            if (item.lists.size() != 2) { error = "a kind 9 entry has two lists"; return false; }
            block.assign(3, 0);
            for (const PersHistoryList& l : item.lists) {
                block.push_back(static_cast<int32_t>(l.ids.size()));
                for (const PersHistoryId& id : l.ids) {
                    block.push_back(static_cast<int32_t>(1 + id.more.size()));
                    block.push_back(id.id);
                    block.push_back(id.parameter);
                    for (const PersHistoryId& m : id.more) {
                        if (!m.more.empty()) { error = "an id of a kind 9 list member's path with ids of its own"; return false; }
                        block.push_back(m.id);
                        block.push_back(m.parameter);
                    }
                }
            }
            return true;
        }
        default:
            error = "the entry kind " + std::to_string(item.kind) + " is not established";
            return false;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

namespace {

struct Envelope {
    int32_t kind = 0;
    int32_t reserved = 0;
    std::vector<int32_t> payload;
    std::vector<int32_t> block;
    std::vector<double> doubles;
};

bool read_envelope(GroupCursor& c, bool new_form, Envelope& e, std::string& error) {
    size_t np = 0, sb = 0;
    c.require_integer(90, e.kind);
    c.require_integer(90, e.reserved);
    if (c.ok() && !c.count(90, 1, np)) { if (c.ok()) c.fail("the length of an entry's payload (90) is missing"); }
    if (c.ok()) take_ints(c, np, e.payload);
    if (c.ok() && !c.count(90, 1, sb)) { if (c.ok()) c.fail("the size of an entry's block (90) is missing"); }
    if (c.ok()) take_ints(c, sb, e.block);
    if (c.ok() && new_form) {
        size_t nd = 0;
        if (!c.count(90, 1, nd)) { if (c.ok()) c.fail("the number of an entry's doubles (90) is missing"); }
        for (size_t i = 0; i < nd && c.ok(); ++i) {
            double v = 0.0;
            c.require_real(40, v);
            e.doubles.push_back(v);
        }
    }
    if (!c.ok()) { error = c.error(); return false; }
    return true;
}

bool read_container(Envelope& e, bool new_form, PersHistoryContainer& out, std::string& error) {
    if (e.payload.empty()) { error = "a container without a step"; return false; }
    out.step = e.payload[0];
    out.reserved = e.reserved;
    out.parameters = e.doubles;
    const size_t lead = new_form ? 3 : 1;
    const std::vector<int32_t>& b = e.block;
    if (b.size() < lead + 1) { error = "a container's block is too short"; return false; }
    for (size_t i = 0; i < lead; ++i) {
        if (b[i] != 0) { error = "a container's block does not start with zeros"; return false; }
    }
    const int64_t count = b[lead];
    size_t p = lead + 1;
    size_t payload_at = 1;
    if (count < 0 || static_cast<uint64_t>(count) > b.size()) { error = "a container with more items than its block"; return false; }
    for (int64_t i = 0; i < count; ++i) {
        if (p + 4 > b.size()) { error = "a container's block ends inside an item"; return false; }
        PersHistoryItem item;
        item.kind = b[p];
        item.reserved = b[p + 1];
        const int64_t n = b[p + 2];
        const int64_t size = b[p + 3];
        p += 4;
        if (n < 0 || size < 0 || static_cast<uint64_t>(size) > b.size() - p) { error = "an item of a container is longer than the container"; return false; }
        if (payload_at + static_cast<uint64_t>(n) > e.payload.size()) { error = "a container's items have more payload than the container"; return false; }
        std::vector<int32_t> payload(e.payload.begin() + static_cast<long>(payload_at), e.payload.begin() + static_cast<long>(payload_at + static_cast<size_t>(n)));
        payload_at += static_cast<size_t>(n);
        std::vector<int32_t> block(b.begin() + static_cast<long>(p), b.begin() + static_cast<long>(p + static_cast<size_t>(size)));
        p += static_cast<size_t>(size);
        if (new_form) {
            if (p >= b.size()) { error = "a container's item has no count of doubles"; return false; }
            if (b[p] != 0) { error = "an item of a container with doubles is not established"; return false; }
            ++p;
        }
        split_payload(payload, item);
        if (!read_item_block(item, block, new_form, error)) return false;
        out.items.push_back(std::move(item));
    }
    if (p != b.size() || payload_at != e.payload.size()) { error = "a container's block or payload is not consumed by its items"; return false; }
    return true;
}

bool leading_zeros(const std::vector<int32_t>& b, const char* what, std::string& error) {
    if (b.size() < 4 || b[0] != 0 || b[1] != 0 || b[2] != 0) {
        error = std::string("a ") + what + " entry's block does not start with three zeros and a count";
        return false;
    }
    return true;
}

// Kind 8: 3 zeros, the number of items and each item as `kind, 0, n, S, block, 0` followed by its paths; the payload is the step
// and, for each item, its n ints and one step per id of its paths
bool read_trim(const Envelope& e, bool new_form, PersHistoryTrim& out, std::string& error) {
    if (!new_form) { error = "a kind 8 entry in the base manager's form is not established (AutoCAD's files have it in the assoc manager's)"; return false; }
    if (e.payload.empty()) { error = "a kind 8 entry without a step"; return false; }
    const std::vector<int32_t>& b = e.block;
    if (!leading_zeros(b, "kind 8", error)) return false;
    out.step = e.payload[0];
    out.reserved = e.reserved;
    out.parameters = e.doubles;
    const int64_t count = b[3];
    if (count < 0 || static_cast<uint64_t>(count) > b.size()) { error = "a kind 8 entry with more items than its block"; return false; }
    size_t p = 4;
    size_t payload_at = 1;
    for (int64_t i = 0; i < count; ++i) {
        if (p + 4 > b.size()) { error = "a kind 8 entry's block ends inside an item"; return false; }
        PersHistoryTrimItem trim_item;
        PersHistoryItem& item = trim_item.item;
        item.kind = b[p];
        item.reserved = b[p + 1];
        const int64_t n = b[p + 2];
        const int64_t size = b[p + 3];
        p += 4;
        if (n < 1 || size < 0 || static_cast<uint64_t>(size) >= b.size() - p) { error = "an item of a kind 8 entry is longer than the entry"; return false; }
        if (payload_at + static_cast<uint64_t>(n) > e.payload.size()) { error = "a kind 8 entry's items have more payload than the entry"; return false; }
        std::vector<int32_t> payload(e.payload.begin() + static_cast<long>(payload_at), e.payload.begin() + static_cast<long>(payload_at + static_cast<size_t>(n)));
        payload_at += static_cast<size_t>(n);
        std::vector<int32_t> block(b.begin() + static_cast<long>(p), b.begin() + static_cast<long>(p + static_cast<size_t>(size)));
        p += static_cast<size_t>(size);
        if (b[p] != 0) { error = "an item of a kind 8 entry with doubles is not established"; return false; }
        ++p;
        if (!take_paths(b, p, trim_item.paths, error)) return false;
        const size_t ids = path_id_count(trim_item.paths);
        if (payload_at + ids > e.payload.size()) { error = "a kind 8 entry's paths have more ids than its payload has steps"; return false; }
        set_path_steps(trim_item.paths, e.payload, payload_at);
        split_payload(payload, item);
        if (!read_item_block(item, block, new_form, error)) return false;
        out.items.push_back(std::move(trim_item));
    }
    if (p != b.size() || payload_at != e.payload.size()) { error = "a kind 8 entry's block or payload is not consumed by its items"; return false; }
    return true;
}

// Kind 10: 3 zeros, the surfaces (a count and for each its paths), the groups (a count, each a count of lists of ints); the payload
// is the step and one step per id of the surfaces' paths
bool read_fillet(const Envelope& e, bool new_form, PersHistoryFillet& out, std::string& error) {
    if (!new_form) { error = "a kind 10 entry in the base manager's form is not established (AutoCAD's files have it in the assoc manager's)"; return false; }
    if (e.payload.empty()) { error = "a kind 10 entry without a step"; return false; }
    const std::vector<int32_t>& b = e.block;
    if (!leading_zeros(b, "kind 10", error)) return false;
    out.step = e.payload[0];
    out.reserved = e.reserved;
    out.parameters = e.doubles;
    const int64_t surfaces = b[3];
    if (surfaces < 0 || static_cast<uint64_t>(surfaces) > b.size()) { error = "a kind 10 entry with more surfaces than its block"; return false; }
    size_t p = 4;
    size_t payload_at = 1;
    for (int64_t i = 0; i < surfaces; ++i) {
        std::vector<PersHistoryPath> paths;
        if (!take_paths(b, p, paths, error)) return false;
        if (payload_at + path_id_count(paths) > e.payload.size()) { error = "a kind 10 entry's paths have more ids than its payload has steps"; return false; }
        set_path_steps(paths, e.payload, payload_at);
        out.surfaces.push_back(std::move(paths));
    }
    if (p >= b.size()) { error = "a kind 10 entry has no count of groups"; return false; }
    const int64_t groups = b[p++];
    if (groups < 0 || static_cast<uint64_t>(groups) > b.size() - p) { error = "a kind 10 entry with more groups than its block"; return false; }
    for (int64_t g = 0; g < groups; ++g) {
        if (p >= b.size()) { error = "a kind 10 group is missing"; return false; }
        const int64_t lists = b[p++];
        if (lists < 0 || static_cast<uint64_t>(lists) > b.size() - p) { error = "a kind 10 group longer than its block"; return false; }
        std::vector<std::vector<int32_t>> group;
        for (int64_t l = 0; l < lists; ++l) {
            if (p >= b.size()) { error = "a kind 10 list is missing"; return false; }
            const int64_t k = b[p++];
            if (k < 0 || static_cast<uint64_t>(k) > b.size() - p) { error = "a kind 10 list longer than its block"; return false; }
            group.emplace_back(b.begin() + static_cast<long>(p), b.begin() + static_cast<long>(p + static_cast<size_t>(k)));
            p += static_cast<size_t>(k);
        }
        out.groups.push_back(std::move(group));
    }
    if (p != b.size() || payload_at != e.payload.size()) { error = "a kind 10 entry's block or payload is not consumed by its surfaces and groups"; return false; }
    return true;
}

} // namespace

bool read_persistent_history(GroupCursor& c, size_t count, bool new_form, std::vector<PersHistoryEntry>& out, std::string& error) {
    out.clear();
    for (size_t i = 0; i < count; ++i) {
        Envelope e;
        if (!read_envelope(c, new_form, e, error)) return false;
        if (e.kind == 6) {
            PersHistoryContainer container;
            if (!read_container(e, new_form, container, error)) return false;
            out.push_back(std::move(container));
        } else if (e.kind == 8) {
            PersHistoryTrim trim;
            if (!read_trim(e, new_form, trim, error)) return false;
            out.push_back(std::move(trim));
        } else if (e.kind == 10) {
            PersHistoryFillet fillet;
            if (!read_fillet(e, new_form, fillet, error)) return false;
            out.push_back(std::move(fillet));
        } else {
            PersHistoryItem item;
            item.kind = e.kind;
            item.reserved = e.reserved;
            item.parameters = e.doubles;
            if (e.payload.empty()) { error = "an entry without a payload"; return false; }
            split_payload(e.payload, item);
            if (!read_item_block(item, e.block, new_form, error)) return false;
            out.push_back(std::move(item));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

namespace {

void write_envelope(RecordWriter& w, bool new_form, int32_t kind, int32_t reserved, const std::vector<int32_t>& payload,
                    const std::vector<int32_t>& block, const std::vector<double>& doubles) {
    w.integer(90, kind);
    w.integer(90, reserved);
    w.integer(90, static_cast<int64_t>(payload.size()));
    for (int32_t v : payload) w.integer(90, v);
    w.integer(90, static_cast<int64_t>(block.size()));
    for (int32_t v : block) w.integer(90, v);
    if (new_form) {
        w.integer(90, static_cast<int64_t>(doubles.size()));
        for (double d : doubles) w.real(40, d);
    }
}

bool write_item(RecordWriter& w, bool new_form, const PersHistoryItem& item, std::string& error) {
    std::vector<int32_t> payload, block;
    join_payload(item, payload);
    if (!build_item_block(item, new_form, block, error)) return false;
    write_envelope(w, new_form, item.kind, item.reserved, payload, block, item.parameters);
    return true;
}

bool write_container(RecordWriter& w, bool new_form, const PersHistoryContainer& container, std::string& error) {
    std::vector<int32_t> payload{container.step};
    std::vector<int32_t> block(new_form ? 3 : 1, 0);
    block.push_back(static_cast<int32_t>(container.items.size()));
    for (const PersHistoryItem& item : container.items) {
        std::vector<int32_t> item_payload, item_block;
        join_payload(item, item_payload);
        if (!build_item_block(item, new_form, item_block, error)) return false;
        payload.insert(payload.end(), item_payload.begin(), item_payload.end());
        block.push_back(item.kind);
        block.push_back(item.reserved);
        block.push_back(static_cast<int32_t>(item_payload.size()));
        block.push_back(static_cast<int32_t>(item_block.size()));
        block.insert(block.end(), item_block.begin(), item_block.end());
        if (new_form) {
            if (!item.parameters.empty()) { error = "an item of a container with doubles is not established"; return false; }
            block.push_back(0);
        }
    }
    write_envelope(w, new_form, 6, container.reserved, payload, block, container.parameters);
    return true;
}

bool write_trim(RecordWriter& w, bool new_form, const PersHistoryTrim& trim, std::string& error) {
    if (!new_form) { error = "a kind 8 entry in the base manager's form is not established"; return false; }
    std::vector<int32_t> payload{trim.step};
    std::vector<int32_t> block{0, 0, 0, static_cast<int32_t>(trim.items.size())};
    for (const PersHistoryTrimItem& trim_item : trim.items) {
        const PersHistoryItem& item = trim_item.item;
        if (!item.parameters.empty()) { error = "an item of a kind 8 entry with doubles is not established"; return false; }
        std::vector<int32_t> item_payload, item_block, steps;
        join_payload(item, item_payload);
        if (!build_item_block(item, new_form, item_block, error)) return false;
        block.push_back(item.kind);
        block.push_back(item.reserved);
        block.push_back(static_cast<int32_t>(item_payload.size()));
        block.push_back(static_cast<int32_t>(item_block.size()));
        block.insert(block.end(), item_block.begin(), item_block.end());
        block.push_back(0);
        put_paths(trim_item.paths, block, steps);
        payload.insert(payload.end(), item_payload.begin(), item_payload.end());
        payload.insert(payload.end(), steps.begin(), steps.end());
    }
    write_envelope(w, new_form, 8, trim.reserved, payload, block, trim.parameters);
    return true;
}

bool write_fillet(RecordWriter& w, bool new_form, const PersHistoryFillet& fillet, std::string& error) {
    if (!new_form) { error = "a kind 10 entry in the base manager's form is not established"; return false; }
    std::vector<int32_t> payload{fillet.step};
    std::vector<int32_t> block{0, 0, 0, static_cast<int32_t>(fillet.surfaces.size())};
    for (const std::vector<PersHistoryPath>& paths : fillet.surfaces) put_paths(paths, block, payload);
    block.push_back(static_cast<int32_t>(fillet.groups.size()));
    for (const std::vector<std::vector<int32_t>>& group : fillet.groups) {
        block.push_back(static_cast<int32_t>(group.size()));
        for (const std::vector<int32_t>& list : group) {
            block.push_back(static_cast<int32_t>(list.size()));
            block.insert(block.end(), list.begin(), list.end());
        }
    }
    write_envelope(w, new_form, 10, fillet.reserved, payload, block, fillet.parameters);
    return true;
}

} // namespace

bool write_persistent_history(RecordWriter& w, bool new_form, const std::vector<PersHistoryEntry>& entries) {
    for (const PersHistoryEntry& entry : entries) {
        std::string error;
        bool ok = false;
        if (const auto* item = std::get_if<PersHistoryItem>(&entry)) ok = write_item(w, new_form, *item, error);
        else if (const auto* container = std::get_if<PersHistoryContainer>(&entry)) ok = write_container(w, new_form, *container, error);
        else if (const auto* trim = std::get_if<PersHistoryTrim>(&entry)) ok = write_trim(w, new_form, *trim, error);
        else if (const auto* fillet = std::get_if<PersHistoryFillet>(&entry)) ok = write_fillet(w, new_form, *fillet, error);
        if (!ok) {
            w.fail("a persistent subentity history entry: " + error);
            return false;
        }
    }
    return true;
}

} // namespace internal
} // namespace hdxf
