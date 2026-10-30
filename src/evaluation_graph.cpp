#include "DynamicBlockTypes.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// ACAD_EVALUATION_GRAPH (phase 9, step 9.9; plan 2026-10-27-fas-9-typer.md, "Evalueringsgraf och dynamiska block", B109,
// B19, B104): the dependency graph of a dynamic block, in the block record's extension dictionary under
// ACAD_ENHANCEDBLOCK (the same record, with the same groups, is the history graph of a solid: step 9.14).
//
// What AutoCAD's own files show (doc/facit/acad_<version>_dxf_dynamic_blocks*.dxf, ACadSharp's sample_AC1015 … AC1032 and
// the dynamic block of doc/facit/web: seven different graphs with edges, 650 records in the corpus):
//   AcDbEvalGraph 96 97, then the nodes (each: 91 93 95 360 and four 92) and then the edges (each: 92 93 94 91 91 and five 92).
//   There are no counts: the nodes end where the first 92 after a node's 360 and its four 92 starts an edge. 96 and 97 are
//   equal in every file and are the highest node id (1 for no node and for one node).
//   - a node's 91 is its place in the list (every file: 0, 1, 2 …), 95 its own number (1 … n; it has gaps in the graph of
//     a block that lost elements, which is why the number and the place are two groups), 360 the element the graph owns
//     (the element's owner, 330, is the graph) and the element's AcDbEvalExpr 90 is the node's 95
//   - an edge's two 91 are node places, not ids; its 92, the first, is its own place in the list
//   - the four 92 of a node are the first and last edge that end at it and the first and last that start at it (-1 for
//     none); the five of an edge are the previous and next edge that end at the node it ends at, the previous and next that
//     start at the node it starts at, and its inverse: AutoCAD's bidirectional pairs have flags 4 and name each other
//     (the dynamic block of doc/facit/web, a lookup parameter). In all seven graphs every one of these links equals what
//     the edges' order gives, so EvaluationGraphData::relink() makes them; they are read and written as they are
//   - removing an element (the file after AutoCAD's PURGE has no graph at all) is not shown by any file: the graph here
//     loses the node and its edges, the nodes and edges that stay are numbered again and the ids stay, as the gaps in the
//     lookup block's graph suggest. No AutoCAD file shows it: ej testat (F9.9)

namespace hdxf {

namespace {

std::string upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

bool same_handle(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;
    // Leading zeros and case do not matter in a handle
    auto strip = [](const std::string& s) {
        size_t i = 0;
        while (i + 1 < s.size() && s[i] == '0') ++i;
        return upper(s.substr(i));
    };
    return strip(a) == strip(b);
}

} // namespace

const EvalGraphNode* EvaluationGraphData::find_node_by_id(int32_t id) const {
    for (const EvalGraphNode& n : nodes) {
        if (n.id == id) return &n;
    }
    return nullptr;
}

const EvalGraphNode* EvaluationGraphData::find_node_by_object(const std::string& handle) const {
    for (const EvalGraphNode& n : nodes) {
        if (same_handle(n.object, handle)) return &n;
    }
    return nullptr;
}

int32_t EvaluationGraphData::add_node(const std::string& object) {
    // The first node of a graph with none has id 1 (a graph with no node has 96 = 97 = 1: AutoCAD's files); after that
    // every node takes the next id and 96/97 follow
    int32_t id = nodes.empty() && last_node_id <= 1 ? 1 : last_node_id + 1;
    for (const EvalGraphNode& n : nodes) id = std::max(id, n.id + 1);
    EvalGraphNode node;
    node.index = static_cast<int32_t>(nodes.size());
    node.id = id;
    node.object = object;
    nodes.push_back(std::move(node));
    last_node_id = std::max(last_node_id, id);
    last_node_id_2 = last_node_id;
    return id;
}

int32_t EvaluationGraphData::add_edge(int32_t from, int32_t to, int32_t ref_count, bool bidirectional) {
    const int32_t count = static_cast<int32_t>(nodes.size());
    if (from < 0 || to < 0 || from >= count || to >= count) return -1;
    EvalGraphEdge edge;
    edge.index = static_cast<int32_t>(edges.size());
    edge.from = from;
    edge.to = to;
    edge.ref_count = ref_count;
    edge.flags = bidirectional ? 4 : 0;
    const int32_t first = edge.index;
    edges.push_back(edge);
    if (bidirectional) {
        EvalGraphEdge back = edge;
        back.index = first + 1;
        back.from = to;
        back.to = from;
        back.inverse = first;
        edges.push_back(back);
        edges[static_cast<size_t>(first)].inverse = first + 1;
    }
    relink();
    return first;
}

void EvaluationGraphData::relink() {
    const int32_t node_count = static_cast<int32_t>(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        EvalGraphNode& n = nodes[i];
        n.index = static_cast<int32_t>(i);
        n.incoming_first = n.incoming_last = n.outgoing_first = n.outgoing_last = -1;
    }
    for (size_t i = 0; i < edges.size(); ++i) {
        EvalGraphEdge& e = edges[i];
        e.index = static_cast<int32_t>(i);
        e.previous_incoming = e.next_incoming = e.previous_outgoing = e.next_outgoing = -1;
    }
    for (EvalGraphEdge& e : edges) {
        if (e.to >= 0 && e.to < node_count) {
            EvalGraphNode& target = nodes[static_cast<size_t>(e.to)];
            if (target.incoming_last >= 0) edges[static_cast<size_t>(target.incoming_last)].next_incoming = e.index;
            e.previous_incoming = target.incoming_last;
            if (target.incoming_first < 0) target.incoming_first = e.index;
            target.incoming_last = e.index;
        }
        if (e.from >= 0 && e.from < node_count) {
            EvalGraphNode& source = nodes[static_cast<size_t>(e.from)];
            if (source.outgoing_last >= 0) edges[static_cast<size_t>(source.outgoing_last)].next_outgoing = e.index;
            e.previous_outgoing = source.outgoing_last;
            if (source.outgoing_first < 0) source.outgoing_first = e.index;
            source.outgoing_last = e.index;
        }
    }
}

bool EvaluationGraphData::links_consistent() const {
    EvaluationGraphData made = *this;
    made.relink();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const EvalGraphNode& a = nodes[i];
        const EvalGraphNode& b = made.nodes[i];
        if (a.index != b.index || a.incoming_first != b.incoming_first || a.incoming_last != b.incoming_last ||
            a.outgoing_first != b.outgoing_first || a.outgoing_last != b.outgoing_last) {
            return false;
        }
    }
    for (size_t i = 0; i < edges.size(); ++i) {
        const EvalGraphEdge& a = edges[i];
        const EvalGraphEdge& b = made.edges[i];
        if (a.index != b.index || a.previous_incoming != b.previous_incoming || a.next_incoming != b.next_incoming ||
            a.previous_outgoing != b.previous_outgoing || a.next_outgoing != b.next_outgoing) {
            return false;
        }
    }
    return true;
}

namespace {

// Takes the edges that are marked out of the list and numbers the rest again: the places of the edges and the
// inverse links follow; the nodes' places are the caller's
void erase_edges(EvaluationGraphData& g, const std::vector<bool>& drop) {
    std::vector<int32_t> renumber(g.edges.size(), -1);
    std::vector<EvalGraphEdge> kept;
    for (size_t i = 0; i < g.edges.size(); ++i) {
        if (drop[i]) continue;
        renumber[i] = static_cast<int32_t>(kept.size());
        kept.push_back(g.edges[i]);
    }
    for (EvalGraphEdge& e : kept) {
        if (e.inverse >= 0 && static_cast<size_t>(e.inverse) < renumber.size()) e.inverse = renumber[static_cast<size_t>(e.inverse)];
        else e.inverse = -1;
    }
    g.edges = std::move(kept);
}

} // namespace

bool EvaluationGraphData::remove_node(int32_t index) {
    if (index < 0 || static_cast<size_t>(index) >= nodes.size()) return false;
    std::vector<bool> drop(edges.size(), false);
    for (size_t i = 0; i < edges.size(); ++i) drop[i] = edges[i].from == index || edges[i].to == index;
    erase_edges(*this, drop);
    nodes.erase(nodes.begin() + index);
    for (EvalGraphEdge& e : edges) {
        if (e.from > index) --e.from;
        if (e.to > index) --e.to;
    }
    relink();
    return true;
}

bool EvaluationGraphData::remove_object(const std::string& handle) {
    const EvalGraphNode* node = find_node_by_object(handle);
    return node && remove_node(node->index);
}

bool EvaluationGraphData::remove_edge(int32_t index) {
    if (index < 0 || static_cast<size_t>(index) >= edges.size()) return false;
    std::vector<bool> drop(edges.size(), false);
    drop[static_cast<size_t>(index)] = true;
    const int32_t inverse = edges[static_cast<size_t>(index)].inverse;
    if (inverse >= 0 && static_cast<size_t>(inverse) < edges.size()) drop[static_cast<size_t>(inverse)] = true;
    erase_edges(*this, drop);
    relink();
    return true;
}

namespace internal {

namespace {

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

bool read_evaluation_graph(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (ctx.version < DxfVersion::R2000) {
        return ctx.fail(describe_record(view) + " in DXF R13/R14: no AutoCAD file shows the form there");
    }
    EvaluationGraphData d;
    const bool ok = read_subclass(view, ctx, "AcDbEvalGraph", [&](GroupCursor& c) {
        c.require_integer(96, d.last_node_id);
        c.require_integer(97, d.last_node_id_2);
        // The nodes: 91 93 95 360 and four 92. Each takes at least eight groups, so the list cannot be longer than the
        // groups that remain allow (nothing is allocated for a count: there is none)
        while (c.ok() && c.code() == 91) {
            EvalGraphNode n;
            c.require_integer(91, n.index);
            c.require_integer(93, n.flags);
            c.require_integer(95, n.id);
            c.require_handle(360, n.object);
            c.require_integer(92, n.incoming_first);
            c.require_integer(92, n.incoming_last);
            c.require_integer(92, n.outgoing_first);
            c.require_integer(92, n.outgoing_last);
            if (c.ok()) d.nodes.push_back(std::move(n));
        }
        // The edges: 92 93 94 91 91 and five 92
        while (c.ok() && c.code() == 92) {
            EvalGraphEdge e;
            c.require_integer(92, e.index);
            c.require_integer(93, e.flags);
            c.require_integer(94, e.ref_count);
            c.require_integer(91, e.from);
            c.require_integer(91, e.to);
            c.require_integer(92, e.previous_incoming);
            c.require_integer(92, e.next_incoming);
            c.require_integer(92, e.previous_outgoing);
            c.require_integer(92, e.next_outgoing);
            c.require_integer(92, e.inverse);
            if (c.ok()) d.edges.push_back(std::move(e));
        }
    });
    if (!ok) return false;
    o.data = std::move(d);
    return true;
}

void evaluation_graph_references(const Object& o, std::vector<TypedReference>& out) {
    // The graph owns the elements its nodes name (their owner, 330, is the graph)
    for (const EvalGraphNode& n : o.as_evaluation_graph().nodes) {
        if (!n.object.empty() && n.object != "0") out.push_back({n.object, RefKind::HardOwner, false});
    }
}

} // namespace

void register_evaluation_graph_codec(TypedRegistry& registry) {
    ObjectCodec c;
    c.dxf_name = "ACAD_EVALUATION_GRAPH";
    c.type = ObjectType::EvaluationGraph;
    c.read = read_evaluation_graph;
    c.write = write_evaluation_graph;
    c.references = evaluation_graph_references;
    c.cls = dynamic_block_class("AcDbEvalGraph");
    c.versions = dynamic_block_matrix();

    // The nodes and edges are one list: a node taken out renumbers the others and every chain (B12: the subclass is
    // written whole where it stood when anything in it changed)
    ListSpec all;
    all.subclass = "AcDbEvalGraph";
    all.id = 1;
    all.shape = ListShape::WholeSubclass;
    c.lists = {all};

    // A node's 360 is a hard pointer to its element; a node whose element is gone is written out with its edges (the
    // codec does it from the typed graph), and from a preserved record the node's groups go
    RefDescriptor element;
    element.subclass = "AcDbEvalGraph";
    element.code = 360;
    element.kind = RefKind::HardOwner;
    element.removal = RefRemoval::DropMember;
    element.first_offset = -3;
    element.last_offset = 4;
    c.refs = {element};
    c.step = 9;
    registry.add(std::move(c));
}

} // namespace internal
} // namespace hdxf
