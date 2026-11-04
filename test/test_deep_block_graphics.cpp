#include "test_framework.hpp"
#include "hdxf.hpp"
#include "TableLayout.hpp"
#include "extents.hpp"
#include "mleader_graphics.hpp"
#include "text_layout.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// Blocks inside blocks, drawn into proxy graphics and measured, to a depth only memory limits: a MULTILEADER's block
// (through INSERT, DIMENSION and a MULTILEADER inside the block, and through an arrowhead's block), a table's *T block,
// the extents of tables whose blocks hold other tables, and the stamps of such tables when a file is read. Every test
// compares what was drawn with what the chain holds, so a walk that stops short, or draws a level twice, fails.

using hdxf::Block;
using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf_test::assert_true;
using hdxf::internal::Extents;
using hdxf::internal::ProxyTables;
using hdxf::internal::TextLayout;

namespace {

constexpr size_t kDepth = 100000;
constexpr size_t kTableDepth = 3000;       // tables in the chain that is saved and read
constexpr size_t kExtentsTables = 5000;     // tables in the chain that is only measured
constexpr double kDeepestY = 7.0;       // the top of the line in the deepest table's block

// ---- entities

Entity make_line(double x, double y0, double y1) {
    Entity e;
    e.type = EntityType::Line;
    hdxf::LineData d;
    d.start = {x, y0, 0.0};
    d.end = {x, y1, 0.0};
    e.data = d;
    return e;
}

Entity make_insert(const std::string& block, double x) {
    Entity e;
    e.type = EntityType::Insert;
    hdxf::InsertData d;
    d.block_name = block;
    d.insertion_point = {x, 0.0, 0.0};
    e.data = std::move(d);
    return e;
}

Entity make_dimension(const std::string& block, double x) {
    Entity e;
    e.type = EntityType::Dimension;
    hdxf::DimensionData d;
    d.block_name = block;
    d.block_translation = {x, 0.0, 0.0};
    e.data = std::move(d);
    return e;
}

hdxf::MLeaderData block_mleader_data(const std::string& block, double x) {
    hdxf::MLeaderData d;
    d.content_type = 1;
    d.context.has_text = false;
    d.context.has_block = true;
    d.context.block = block;
    d.context.block_position = {x, 0.0, 0.0};
    return d;
}

Entity make_mleader(const std::string& block, double x) {
    Entity e;
    e.type = EntityType::MLeader;
    e.data = hdxf::Boxed<hdxf::MLeaderData>(block_mleader_data(block, x));
    return e;
}

// One leader with one line from (0,0,0) to (10,0,0), straight, the landing off
hdxf::MLeaderLeader straight_leader() {
    hdxf::MLeaderLine line;
    line.vertices = {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}};
    line.index = 0;
    hdxf::MLeaderLeader leader;
    leader.has_last_point = true;
    leader.has_dogleg = false;
    leader.last_point = {10.0, 0.0, 0.0};
    leader.dogleg_length = 0.0;
    leader.lines.push_back(line);
    return leader;
}

Entity make_table_entity(const std::string& block, double x) {
    Entity e;
    e.type = EntityType::Table;
    hdxf::TableData t;
    t.block_name = block;
    t.insertion = {x, 0.0, 0.0};
    e.data = hdxf::Boxed<hdxf::TableData>(std::move(t));
    return e;
}

std::string name_of(const char* prefix, size_t k) {
    return std::string(prefix) + std::to_string(k);
}

// ---- proxy graphics streams

struct Command {
    uint32_t type = 0;
    size_t data = 0;                    // where the command's data starts in the stream
};

uint32_t read_u32(const std::vector<uint8_t>& s, size_t at) {
    uint32_t u = 0;
    for (size_t i = 0; i < 4; ++i) u |= static_cast<uint32_t>(s[at + i]) << (8 * i);
    return u;
}

int32_t read_i32(const std::vector<uint8_t>& s, size_t at) {
    return static_cast<int32_t>(read_u32(s, at));
}

double read_f64(const std::vector<uint8_t>& s, size_t at) {
    double d = 0.0;
    std::memcpy(&d, &s[at], 8);
    return d;
}

// The commands of a stream (total size, count, then size, type and data of each); the stream must be whole
std::vector<Command> commands_of(const std::vector<uint8_t>& s) {
    std::vector<Command> out;
    assert_true(s.size() >= 8 && read_u32(s, 0) == s.size(), "the stream has its size");
    const uint32_t count = read_u32(s, 4);
    size_t at = 8;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        assert_true(at + 8 <= s.size(), "a command fits in the stream");
        const uint32_t size = read_u32(s, at);
        assert_true(size >= 8 && at + size <= s.size(), "a command has its size");
        out.push_back({read_u32(s, at + 4), at + 8});
        at += size;
    }
    assert_true(at == s.size(), "the commands are all of the stream");
    return out;
}

// What a chain of blocks left in a stream: the translation of every transform pushed, and of every LINE (polyline with a
// normal, command 32) where its first point is and how many transforms were pushed and not popped when it was drawn
struct ChainTrace {
    std::vector<double> pushed_x;
    std::vector<double> line_x;
    std::vector<int64_t> line_depth;
    size_t pops = 0;
    int64_t depth = 0;
    size_t last_pop = 0;                // the index of the last command 31
};

ChainTrace trace_of(const std::vector<uint8_t>& s, const std::vector<Command>& commands) {
    ChainTrace t;
    for (size_t i = 0; i < commands.size(); ++i) {
        const Command& c = commands[i];
        if (c.type == 29) {
            t.pushed_x.push_back(read_f64(s, c.data + 3 * 8));      // the matrix row by row: the x translation is the 4th
            ++t.depth;
        } else if (c.type == 31) {
            ++t.pops;
            --t.depth;
            t.last_pop = i;
        } else if (c.type == 32) {
            t.line_x.push_back(read_f64(s, c.data + 4));
            t.line_depth.push_back(t.depth);
        }
    }
    return t;
}

// The chain left what it holds: these transforms in this order, one LINE per level at the x of its level, each drawn
// `base + level + 1` transforms deep, and every transform popped
void expect_chain(const ChainTrace& t, const std::vector<double>& pushed, size_t levels, int64_t base) {
    assert_true(t.pushed_x.size() == pushed.size(),
                "transforms pushed: " + std::to_string(t.pushed_x.size()) + " of " + std::to_string(pushed.size()));
    for (size_t i = 0; i < pushed.size(); ++i) {
        assert_true(std::fabs(t.pushed_x[i] - pushed[i]) < 1e-9,
                    "transform " + std::to_string(i) + " moves by " + std::to_string(t.pushed_x[i]) + ", not " +
                        std::to_string(pushed[i]));
    }
    assert_true(t.pops == pushed.size(), "transforms popped: " + std::to_string(t.pops));
    assert_true(t.depth == 0, "every transform is popped");
    assert_true(t.line_x.size() == levels, "lines drawn: " + std::to_string(t.line_x.size()) + " of " + std::to_string(levels));
    for (size_t k = 0; k < levels; ++k) {
        assert_true(std::fabs(t.line_x[k] - static_cast<double>(k)) < 1e-9,
                    "line " + std::to_string(k) + " is at x " + std::to_string(t.line_x[k]));
        assert_true(t.line_depth[k] == base + static_cast<int64_t>(k) + 1,
                    "line " + std::to_string(k) + " is drawn " + std::to_string(t.line_depth[k]) + " transforms deep");
    }
}

void expect_lines(const ChainTrace& t, const std::vector<double>& line_x, size_t pushes) {
    assert_true(t.line_x.size() == line_x.size(), "lines drawn: " + std::to_string(t.line_x.size()));
    for (size_t i = 0; i < line_x.size(); ++i) {
        assert_true(std::fabs(t.line_x[i] - line_x[i]) < 1e-9,
                    "line " + std::to_string(i) + " is at x " + std::to_string(t.line_x[i]));
    }
    assert_true(t.pushed_x.size() == pushes, "transforms pushed: " + std::to_string(t.pushed_x.size()));
    assert_true(t.pops == pushes && t.depth == 0, "every transform is popped");
}

// The value of the marker command (19) at index i
int32_t marker_at(const std::vector<uint8_t>& s, const std::vector<Command>& commands, size_t i) {
    assert_true(i < commands.size() && commands[i].type == 19, "a marker");
    return read_i32(s, commands[i].data);
}

// ---- chains of blocks

// Block "L<k>" holds a LINE at x = k, and then the way into "L<k+1>": in turn an INSERT (moving by 1), a DIMENSION (by 2)
// and a MULTILEADER whose content is the block (by 3)
void build_graphics_chain(Drawing& d, size_t depth) {
    d.blocks().reserve(depth + 4);
    for (size_t k = 0; k < depth; ++k) {
        Block b;
        b.name = name_of("L", k);
        b.entities.push_back(make_line(static_cast<double>(k), 0.0, 1.0));
        if (k + 1 < depth) {
            const std::string next = name_of("L", k + 1);
            switch (k % 3) {
                case 0: b.entities.push_back(make_insert(next, 1.0)); break;
                case 1: b.entities.push_back(make_dimension(next, 2.0)); break;
                default: b.entities.push_back(make_mleader(next, 3.0)); break;
            }
        }
        d.blocks().push_back(std::move(b));
    }
}

// The translations of the transforms that entering "L0" and then each next block pushes
std::vector<double> graphics_chain_pushes(double first, size_t depth) {
    std::vector<double> pushed{first};
    for (size_t k = 0; k + 1 < depth; ++k) pushed.push_back(static_cast<double>(k % 3) + 1.0);
    return pushed;
}

// Block "T<k>" holds a LINE at x = k and an INSERT of "T<k+1>" moving by 1
void build_insert_chain(Drawing& d, size_t depth) {
    d.blocks().reserve(depth + 4);
    for (size_t k = 0; k < depth; ++k) {
        Block b;
        b.name = name_of("T", k);
        b.entities.push_back(make_line(static_cast<double>(k), 0.0, 1.0));
        if (k + 1 < depth) b.entities.push_back(make_insert(name_of("T", k + 1), 1.0));
        d.blocks().push_back(std::move(b));
    }
}

// `count` tables, table k (k > 0) in block "B<k>" and table 0 in model space. Table k's anonymous block holds a LINE
// from (0,0) up one, and the INSERT of "B<k+1>"; the last holds a LINE up kDeepestY and nothing else. Table 0 is at
// (0,0), the others at (1,0) in their blocks, so the anonymous block of table k lies k to the right. Made by the drawing
// (add_table) or by hand (just the entity and its block).
void build_table_chain(Drawing& d, size_t count, bool made_by_drawing) {
    std::vector<Entity> tables;
    tables.reserve(count);
    if (made_by_drawing) {
        for (size_t k = 0; k < count; ++k) d.add_table({0.0, 0.0, 0.0}, 1, 1, 1.0, 1.0);
        tables.swap(d.entities());
    } else {
        d.blocks().reserve(2 * count + 8);
        for (size_t k = 0; k < count; ++k) {
            Block anonymous;
            anonymous.name = name_of("*T", k + 1);
            anonymous.flags = 1;
            d.blocks().push_back(std::move(anonymous));
            tables.push_back(make_table_entity(name_of("*T", k + 1), 0.0));
        }
    }
    for (size_t k = 0; k < count; ++k) {
        tables[k].as_table().insertion = {k == 0 ? 0.0 : 1.0, 0.0, 0.0};
        Block* anonymous = d.find_block(tables[k].as_table().block_name);
        assert_true(anonymous != nullptr, "the anonymous block of table " + std::to_string(k));
        anonymous->entities.clear();
        const bool last = k + 1 == count;
        anonymous->entities.push_back(make_line(0.0, 0.0, last ? kDeepestY : 1.0));
        if (!last) anonymous->entities.push_back(make_insert(name_of("B", k + 1), 0.0));
    }
    for (size_t k = 1; k < count; ++k) {
        Block b;
        b.name = name_of("B", k);
        b.entities.push_back(std::move(tables[k]));
        d.blocks().push_back(std::move(b));
    }
    d.entities().push_back(std::move(tables[0]));
}

// The table whose *T block the proxy graphics draw
Entity proxy_table() {
    return make_table_entity("*T1", 5.0);
}

std::vector<uint8_t> table_stream(const Drawing& d, const std::vector<Entity>& block) {
    TextLayout layout(d);
    const Entity table = proxy_table();
    return hdxf::internal::table_proxy_graphics(d, table, block, {}, {}, layout, true);
}

} // namespace

// A MULTILEADER whose block holds a hundred thousand blocks inside each other, entered through an INSERT, a DIMENSION
// and a MULTILEADER in turn: every level is drawn, inside the transforms of the levels above it
TEST(mleader_graphics_iterative) {
    Drawing d;
    build_graphics_chain(d, kDepth);
    TextLayout layout(d);
    ProxyTables tables;
    const Entity top = make_mleader("L0", 7.0);
    const std::vector<uint8_t> bytes = hdxf::internal::mleader_graphics(d, top, tables, layout, true);
    const std::vector<Command> commands = commands_of(bytes);
    const ChainTrace t = trace_of(bytes, commands);
    expect_chain(t, graphics_chain_pushes(7.0, kDepth), kDepth, 0);
    // the leaders and the rest of the drawing come after the content
    assert_true(commands.back().type == 51, "the drawing ends as a MULTILEADER's does");
    bool after_leaders = false;
    for (size_t i = t.last_pop + 1; i < commands.size(); ++i) {
        if (commands[i].type == 19 && marker_at(bytes, commands, i) == 15002) after_leaders = true;
    }
    assert_true(after_leaders, "the marker after the leaders comes after the last block");
}

// The block an arrowhead is drawn with holds the same chain: the arrowhead is drawn whole, and the line goes on after it
TEST(mleader_arrowhead_chain_iterative) {
    Drawing d;
    build_graphics_chain(d, kDepth);
    Block z;
    z.name = "Z";
    d.blocks().push_back(std::move(z));
    hdxf::MLeaderData data = block_mleader_data("Z", 7.0);
    data.arrow_block = "L0";
    data.context.leaders.push_back(straight_leader());
    Entity top;
    top.type = EntityType::MLeader;
    top.data = hdxf::Boxed<hdxf::MLeaderData>(std::move(data));
    TextLayout layout(d);
    ProxyTables tables;
    const std::vector<uint8_t> bytes = hdxf::internal::mleader_graphics(d, top, tables, layout, true);
    const std::vector<Command> commands = commands_of(bytes);
    const ChainTrace t = trace_of(bytes, commands);
    // Z's transform, then the arrowhead's, then the chain (the arrowhead's block is "L0")
    std::vector<double> pushed{7.0, 0.0};
    for (size_t k = 0; k + 1 < kDepth; ++k) pushed.push_back(static_cast<double>(k % 3) + 1.0);
    expect_chain(t, pushed, kDepth, 0);
    // after the arrowhead: the line's own traits again (color 22, linetype 18, lineweight 23), the line's marker and the line
    const size_t after = t.last_pop + 1;
    assert_true(after + 4 < commands.size(), "the line follows the arrowhead");
    assert_true(commands[after].type == 22 && commands[after + 1].type == 18 && commands[after + 2].type == 23,
                "the line's traits are set again after the arrowhead");
    assert_true(marker_at(bytes, commands, after + 3) == 5001, "the line's marker");
    assert_true(commands[after + 4].type == 6, "the line is drawn");
}

// Each INSERT's attributes come after its block, one level at a time from the inside out
TEST(mleader_insert_attributes_follow_their_block) {
    const size_t depth = 200;
    Drawing d;
    d.blocks().reserve(depth + 2);
    for (size_t k = 0; k < depth; ++k) {
        Block b;
        b.name = name_of("T", k);
        b.entities.push_back(make_line(static_cast<double>(k), 0.0, 1.0));
        if (k + 1 < depth) {
            Entity insert = make_insert(name_of("T", k + 1), 1.0);
            hdxf::InsertAttribute a;
            a.data.value = "A";
            a.data.tag = "TAG";
            insert.as_insert().attributes.push_back(a);
            b.entities.push_back(std::move(insert));
        }
        d.blocks().push_back(std::move(b));
    }
    TextLayout layout(d);
    ProxyTables tables;
    const Entity top = make_mleader("T0", 7.0);
    const std::vector<uint8_t> bytes = hdxf::internal::mleader_graphics(d, top, tables, layout, true);
    const std::vector<Command> commands = commands_of(bytes);
    const ChainTrace t = trace_of(bytes, commands);
    std::vector<double> pushed{7.0};
    for (size_t k = 0; k + 1 < depth; ++k) pushed.push_back(1.0);
    expect_chain(t, pushed, depth, 0);
    // after the pop of each INSERT's block, before the next pop: its attribute's color and lineweight (22, 23)
    size_t pops = 0;
    for (size_t i = 0; i < commands.size(); ++i) {
        if (commands[i].type != 31) continue;
        ++pops;
        if (pops == pushed.size()) continue;            // the MULTILEADER's own block: no attribute follows
        assert_true(i + 2 < commands.size() && commands[i + 1].type == 22 && commands[i + 2].type == 23,
                    "the attribute of the INSERT whose block ended as pop " + std::to_string(pops) + " follows it");
    }
}

// A block that holds itself, or a block above it, is left out of its own branch only: the siblings are drawn, and a block
// two branches both hold is drawn in both
TEST(mleader_graphics_cycles_keep_sibling_geometry) {
    Drawing d;
    Block c0;
    c0.name = "C0";
    c0.entities.push_back(make_insert("C1", 1.0));
    c0.entities.push_back(make_line(100.0, 0.0, 1.0));
    Block c1;
    c1.name = "C1";
    c1.entities.push_back(make_insert("C0", 1.0));              // above it
    c1.entities.push_back(make_mleader("C0", 3.0));             // above it, as a MULTILEADER's content
    c1.entities.push_back(make_dimension("C0", 2.0));           // above it, as a DIMENSION's block
    c1.entities.push_back(make_insert("C2", 1.0));
    c1.entities.push_back(make_insert("C2", 1.0));              // the same block in a second branch
    c1.entities.push_back(make_line(200.0, 0.0, 1.0));
    Block c2;
    c2.name = "C2";
    c2.entities.push_back(make_line(300.0, 0.0, 1.0));
    c2.entities.push_back(make_insert("C2", 1.0));              // itself
    d.blocks().push_back(std::move(c0));
    d.blocks().push_back(std::move(c1));
    d.blocks().push_back(std::move(c2));
    TextLayout layout(d);
    ProxyTables tables;
    const Entity top = make_mleader("C0", 7.0);
    const std::vector<uint8_t> bytes = hdxf::internal::mleader_graphics(d, top, tables, layout, true);
    const ChainTrace t = trace_of(bytes, commands_of(bytes));
    // C2 twice, then C1's line, then C0's. Transforms: C0, C1, the MULTILEADER's block (its entities left out, the block
    // still entered), and C2 twice
    expect_lines(t, {300.0, 300.0, 200.0, 100.0}, 5);
}

// A block the drawn MULTILEADER's block holds is the one measured: the entities of its block go to the caller with the
// block's transform, and what the leaders draw afterwards counts again
TEST(mleader_extents_hand_over_top_entities) {
    Drawing d;
    build_graphics_chain(d, 10);
    hdxf::MLeaderData data = block_mleader_data("L0", 7.0);
    data.context.leaders.push_back(straight_leader());
    Entity top;
    top.type = EntityType::MLeader;
    top.data = hdxf::Boxed<hdxf::MLeaderData>(std::move(data));
    TextLayout layout(d);
    std::vector<EntityType> types;
    std::vector<double> moved;
    Extents out;
    const std::array<double, 16> identity = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    hdxf::internal::mleader_extents(d, top, layout, identity,
                                    [&](const Entity& e, const std::array<double, 16>& m) {
                                        types.push_back(e.type);
                                        moved.push_back(m[3]);
                                    },
                                    out);
    assert_true(types.size() == 2 && types[0] == EntityType::Line && types[1] == EntityType::Insert,
                "the block's own entities are handed over, and none of the ones inside them");
    assert_true(std::fabs(moved[0] - 7.0) < 1e-9 && std::fabs(moved[1] - 7.0) < 1e-9, "with the block's transform");
    // the leader's line (0,0) to (10,0) and its arrowhead count: nothing of the block does
    assert_true(!out.empty() && std::fabs(out.min.x) < 1e-9 && std::fabs(out.max.x - 10.0) < 1e-9 &&
                    std::fabs(out.max.y - 0.03) < 0.01,
                "the leaders count after the block");
}

// A table's *T block with a hundred thousand blocks inside each other
TEST(table_graphics_iterative) {
    Drawing d(DxfVersion::R2018);
    build_insert_chain(d, kDepth);
    std::vector<Entity> block;
    block.push_back(make_insert("T0", 2.0));
    const std::vector<uint8_t> bytes = table_stream(d, block);
    const ChainTrace t = trace_of(bytes, commands_of(bytes));
    // the table's transform (at x = 5), the INSERT's, then each block's INSERT of the next
    std::vector<double> pushed{5.0, 2.0};
    for (size_t k = 0; k + 1 < kDepth; ++k) pushed.push_back(1.0);
    expect_chain(t, pushed, kDepth, 1);
}

// The same walk keeps a block that holds itself, or one above it, to its own branch
TEST(table_graphics_cycles_keep_sibling_geometry) {
    Drawing d(DxfVersion::R2018);
    Block c0;
    c0.name = "C0";
    c0.entities.push_back(make_insert("C1", 1.0));
    c0.entities.push_back(make_line(100.0, 0.0, 1.0));
    Block c1;
    c1.name = "C1";
    c1.entities.push_back(make_insert("C0", 1.0));
    c1.entities.push_back(make_insert("C2", 1.0));
    c1.entities.push_back(make_insert("C2", 1.0));
    c1.entities.push_back(make_line(200.0, 0.0, 1.0));
    Block c2;
    c2.name = "C2";
    c2.entities.push_back(make_line(300.0, 0.0, 1.0));
    c2.entities.push_back(make_insert("C2", 1.0));
    d.blocks().push_back(std::move(c0));
    d.blocks().push_back(std::move(c1));
    d.blocks().push_back(std::move(c2));
    std::vector<Entity> block;
    block.push_back(make_insert("C0", 2.0));
    const std::vector<uint8_t> bytes = table_stream(d, block);
    const ChainTrace t = trace_of(bytes, commands_of(bytes));
    // the table, C0, C1 and C2 twice
    expect_lines(t, {300.0, 300.0, 200.0, 100.0}, 5);
}

// A MULTILEADER in the block it draws, two whose blocks hold each other, and a ring where every block has a leader to every other: the
// extents end, and the line that stands beside the leader in its block is counted
TEST(mleader_cycles_end_in_extents) {
    {
        Drawing d(DxfVersion::R2018);
        Block self;
        self.name = "SELF";
        self.entities.push_back(make_line(500.0, 0.0, 1.0));
        self.entities.push_back(make_mleader("SELF", 3.0));
        d.blocks().push_back(std::move(self));
        const Entity top = make_mleader("SELF", 7.0);
        Extents box;
        hdxf::internal::entity_extents(d, top, box);
        assert_true(!box.empty(), "the MULTILEADER has extents");
        assert_true(box.max.x >= 500.0, "the line beside it in the block: " + std::to_string(box.max.x));
    }
    {
        Drawing d(DxfVersion::R2018);
        Block p;
        p.name = "P";
        p.entities.push_back(make_line(600.0, 0.0, 1.0));
        p.entities.push_back(make_mleader("Q", 1.0));
        p.entities.push_back(make_mleader("Q", 2.0));
        Block q;
        q.name = "Q";
        q.entities.push_back(make_line(700.0, 0.0, 1.0));
        q.entities.push_back(make_mleader("P", 1.0));
        q.entities.push_back(make_mleader("P", 2.0));
        d.blocks().push_back(std::move(p));
        d.blocks().push_back(std::move(q));
        const Entity top = make_mleader("P", 7.0);
        Extents box;
        hdxf::internal::entity_extents(d, top, box);
        assert_true(box.max.x >= 700.0, "the line of the block further in: " + std::to_string(box.max.x));
    }
    {
        Drawing d(DxfVersion::R2018);
        const int count = 8;
        for (int i = 0; i < count; ++i) {
            Block b;
            b.name = "R" + std::to_string(i);
            b.entities.push_back(make_line(static_cast<double>(i), 0.0, 1.0));
            for (int k = 0; k < count; ++k) {
                if (k != i) b.entities.push_back(make_mleader("R" + std::to_string(k), 0.5));
            }
            d.blocks().push_back(std::move(b));
        }
        const Entity top = make_mleader("R0", 7.0);
        Extents box;
        hdxf::internal::entity_extents(d, top, box);
        assert_true(!box.empty(), "the ring has extents");
        // And its graphics, as the writer makes them, end too
        TextLayout layout(d);
        ProxyTables tables;
        assert_true(!hdxf::internal::mleader_graphics(d, top, tables, layout, true).empty(), "the ring has graphics");
    }
}

// Blocks that all insert each other, in a table's graphics: they end
TEST(table_graphics_ring_ends) {
    Drawing d(DxfVersion::R2018);
    const int count = 8;
    for (int i = 0; i < count; ++i) {
        Block b;
        b.name = "R" + std::to_string(i);
        b.entities.push_back(make_line(static_cast<double>(i), 0.0, 1.0));
        for (int k = 0; k < count; ++k) {
            if (k != i) b.entities.push_back(make_insert("R" + std::to_string(k), 0.5));
        }
        d.blocks().push_back(std::move(b));
    }
    std::vector<Entity> block;
    block.push_back(make_insert("R0", 2.0));
    assert_true(!table_stream(d, block).empty(), "the ring has graphics");
}

// Tables whose blocks hold other tables: the extents of the first are measured in one calculation however deep the
// chain goes, and are the union of all the levels
TEST(table_extents_share_traversal) {
    Drawing d(DxfVersion::R2018);
    build_table_chain(d, kExtentsTables, false);
    Extents box;
    const size_t before = hdxf::internal::extents_builders_started();
    hdxf::internal::entity_extents(d, d.entities().front(), box);
    const size_t started = hdxf::internal::extents_builders_started() - before;
    assert_true(started == 1, "one calculation for the chain, not one per table: " + std::to_string(started));
    assert_true(!box.empty(), "the chain has extents");
    const double last_x = static_cast<double>(kExtentsTables - 1);
    assert_true(std::fabs(box.min.x) < 1e-9 && std::fabs(box.min.y) < 1e-9 && std::fabs(box.min.z) < 1e-9,
                "the near corner: " + std::to_string(box.min.x) + ", " + std::to_string(box.min.y));
    assert_true(std::fabs(box.max.x - last_x) < 1e-9, "the far end is the last table's: " + std::to_string(box.max.x));
    assert_true(std::fabs(box.max.y - kDeepestY) < 1e-9, "the top is the last table's: " + std::to_string(box.max.y));
    assert_true(std::fabs(box.max.z) < 1e-9, "flat");
}

// The same chain, made by the drawing, saved and read: the stamps of the tables are made while reading and measure the
// chain the first table's cell shows
TEST(table_stamps_safe_on_load) {
    Drawing d(DxfVersion::R2018);
    build_table_chain(d, kTableDepth, true);
    // the first table shows the block of the second in its cell
    const std::string handle = "A00000";
    hdxf::TableCell* cell = d.entities().front().as_table().cell(0, 0);
    assert_true(cell != nullptr, "the first table has a cell");
    cell->type = hdxf::TableCellType::Block;
    cell->block_handle = handle;
    cell->block_scale = 1.0;
    Block* shown = d.find_block("B1");
    assert_true(shown != nullptr, "the block in the cell");
    shown->record.handle = handle;
    // laid out as it is: only the first table, which has the cell, is laid out again when the drawing is saved
    hdxf::internal::stamp_loaded_tables(d);
    const std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());

    Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), r.last_error());
    std::vector<Entity*> tables;
    for (Entity& e : r.entities()) {
        if (e.type == EntityType::Table) tables.push_back(&e);
    }
    for (Block& b : r.blocks()) {
        for (Entity& e : b.entities) {
            if (e.type == EntityType::Table) tables.push_back(&e);
        }
    }
    assert_true(tables.size() == kTableDepth, "the tables read: " + std::to_string(tables.size()));
    for (const Entity* t : tables) {
        assert_true(t->as_table().layout_stamp != 0, "a table read has its stamp");
    }
    // the first table's stamp is the one its surroundings give now
    Entity* first = nullptr;
    for (Entity& e : r.entities()) {
        if (e.type == EntityType::Table) first = &e;
    }
    assert_true(first != nullptr, "the first table is in model space");
    auto stamp_now = [&]() {
        const hdxf::Object* content = r.table_content(*first);
        return hdxf::internal::table_layout_stamp(r, *first, content ? &content->as_table_content() : nullptr);
    };
    assert_true(stamp_now() == first->as_table().layout_stamp, "the stamp read is the stamp of the table as it is");
    // the stamp holds the whole chain: the deepest line moves, and the stamp with it
    Block* last = r.find_block(name_of("B", kTableDepth - 1));
    assert_true(last != nullptr && last->entities.size() == 1, "the block of the last table");
    Block* anonymous = r.find_block(last->entities.front().as_table().block_name);
    assert_true(anonymous != nullptr && !anonymous->entities.empty() && anonymous->entities.front().type == EntityType::Line,
                "the last table's own block");
    anonymous->entities.front().as_line().end.y = kDeepestY + 2.0;
    assert_true(stamp_now() != first->as_table().layout_stamp, "the stamp follows the deepest table of the chain");
}
