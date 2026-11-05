#include "hdxf/drawing.hpp"
#include "hdxf/ecs.hpp"
#include "hdxf/entity.hpp"
#include "hdxf/entity_mleader.hpp"
#include "hdxf/tables.hpp"
#include "graphics/mleader_graphics.hpp"
#include "geometry/acis.hpp"
#include "text/fonts.hpp"
#include "graphics/dimension_block.hpp"
#include "geometry/extents.hpp"
#include "graphics/proxy_graphics.hpp"
#include "geometry/mesh.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// What AutoCAD draws for a MULTILEADER, command by command, as in its files (ACadSharp's
// sample_AC1015 … AC1032, LibreDWG's Leader.dxf and the AutoCAD files of test/data/README.md):
//
//   the entity's color, 51, marker 15001, [the text's color], the text (a TEXT per run of
//   the MTEXT's lines; a stack's bar a polyline in Continuous) — or for a block: marker 15004,
//   the block's transform, each entity with its color and lineweight, the transform's end
//   and marker 15005 with the attributes —, 51, the layer, the leader's color and linetype,
//   per leader line: marker (its index + 1), the arrowhead (filled), marker 5001 + index, the
//   line; per leader: marker 10001 + its branch, the landing; the entity's color and
//   linetype, lineweight 0, marker 15002, [the underline], the entity's color, linetype and
//   lineweight, 51, [the text's frame: 51, the layer, the color, the frame, ACI 256,
//   lineweight -1, 51].

namespace hdxf {
namespace internal {

namespace {

constexpr int kTextMarker = 15001;
constexpr int kAfterLeadersMarker = 15002;
constexpr int kBlockMarker = 15004;
constexpr int kAttributesMarker = 15005;
constexpr int kLineMarker = 5001;
constexpr int kLandingMarker = 10001;
constexpr int kLinetypeByLayer = 0x7FFF;
constexpr int kLinetypeByBlock = 0x7FFE;

Point3 operator+(const Point3& a, const Point3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Point3 operator-(const Point3& a, const Point3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Point3 operator*(const Point3& a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(const Point3& a, const Point3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Point3 cross(const Point3& a, const Point3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Point3& a) { return std::sqrt(dot(a, a)); }
Point3 unit(const Point3& a) {
    double l = length(a);
    return l > 0.0 ? a * (1.0 / l) : a;
}

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

// A 4×4 transform, row by row
using Matrix = std::array<double, 16>;

Matrix identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix r{};
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            double sum = 0.0;
            for (int k = 0; k < 4; ++k) sum += a[4 * i + k] * b[4 * k + j];
            r[4 * i + j] = sum;
        }
    }
    return r;
}

Point3 transform_point(const Matrix& m, const Point3& p) {
    return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
            m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
}

Point3 apply_vector(const Matrix& m, const Point3& v) {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[4] * v.x + m[5] * v.y + m[6] * v.z,
            m[8] * v.x + m[9] * v.y + m[10] * v.z};
}

// The inverse of an affine transform
Matrix inverse(const Matrix& m) {
    double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det == 0.0) return m;
    double k = 1.0 / det;
    Matrix r = {(e * i - f * h) * k, (c * h - b * i) * k, (b * f - c * e) * k, 0,
                (f * g - d * i) * k, (a * i - c * g) * k, (c * d - a * f) * k, 0,
                (d * h - e * g) * k, (b * g - a * h) * k, (a * e - b * d) * k, 0,
                0, 0, 0, 1};
    r[3] = -(r[0] * m[3] + r[1] * m[7] + r[2] * m[11]);
    r[7] = -(r[4] * m[3] + r[5] * m[7] + r[6] * m[11]);
    r[11] = -(r[8] * m[3] + r[9] * m[7] + r[10] * m[11]);
    return r;
}

// The transform of a block placed at `position` in the plane of `normal`, turned and scaled
// (an INSERT's): the block's base point to the position
Matrix block_transform(const Point3& position, const Point3& normal, double rotation, const Point3& scale,
                       const Point3& base) {
    Point3 ax, ay;
    ecs::arbitrary_axis(normal, ax, ay);
    Point3 az = unit(normal);
    double c = std::cos(rotation), s = std::sin(rotation);
    Point3 x = (ax * c + ay * s) * scale.x;
    Point3 y = (ay * c - ax * s) * scale.y;
    Point3 z = az * scale.z;
    Matrix m = {x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0, 0, 0, 0, 1};
    Point3 t = position - apply_vector(m, base);
    m[3] = t.x;
    m[7] = t.y;
    m[11] = t.z;
    return m;
}

// A gap in a line drawn by broken_polyline(): the segment it is on and its two points
struct SegmentGap {
    int segment = 0;
    MLeaderGap gap;
};

// What a block's entities take from where the block is drawn: layer 0's layer, and the color
// and lineweight of ByBlock
struct BlockHost {
    std::string layer;
    int32_t color = 0;
    int lineweight = -3;
};

using BlockEntityExtents = std::function<void(const Entity&, const Matrix&)>;

class MLeaderDrawer;

// A leader line between its start and the end of its arrowhead: what the rest of the line needs
struct LineInProgress {
    int32_t color = 0;                  // what the line is drawn in
    int linetype = 0;
    int weight = 0;
    bool own_color = false;             // the line has its own color, linetype, lineweight
    bool own_linetype = false;
    bool own_weight = false;
    std::vector<Point3> points;
    bool trims = false;                 // the arrowhead shortens the line
    Point3 trim_to;                     // where the line starts then
};

// What a drawer does next while it walks the blocks it draws. The walk keeps these on the heap, so the depth of the
// blocks inside blocks is limited by memory and not by the call stack.
enum class DrawStepKind {
    Entity,         // an entity of a block: its color and lineweight, then its own graphics
    EntityEnd,      // after an entity that draws blocks: what it changed is put back
    Copies,         // the next copy of an INSERT's array, or the end of the array
    CopyEnd,        // the end of one copy: its transform is popped
    InsertEnd,      // after an INSERT's copies: its block is free again, then its attributes
    DimensionEnd,   // after a DIMENSION's block: the block is free again, its transform is popped
    BlockEnd,       // after a MULTILEADER's block: its transform is popped, its attributes, the rest of its drawing
    InnerEnd,       // after a MULTILEADER in a block: the drawer that drew it is released
    ArrowEnd        // after a leader line's arrowhead block: its transform is popped, the line and the leaders go on
};

struct DrawStep {
    DrawStepKind kind = DrawStepKind::Entity;
    MLeaderDrawer* drawer = nullptr;            // the drawer whose graphics and transforms this step uses
    const Entity* entity = nullptr;
    const BlockHost* host = nullptr;
    const Block* block = nullptr;
    size_t copy = 0;                            // Copies: the array element drawn next
    bool top = false;                           // Entity: an entity of the block the MULTILEADER draws
    bool trait = false;                         // EntityEnd: the thickness was set
    bool entered = false;                       // the block was marked as being drawn by this step's owner
    std::shared_ptr<MLeaderDrawer> keep;        // InnerEnd: the MULTILEADER that is drawn
};

// The blocks being drawn by one MULTILEADER and the ones inside its blocks: the steps still to do (the last is done
// first), the hosts the steps point to, and the blocks being drawn now (a block that inserts itself is drawn once on its
// way down; only that branch stops).
constexpr int kBlockMetLimit = 64;

struct BlockWalk {
    std::vector<DrawStep> steps;
    std::deque<BlockHost> hosts;                // stable addresses: steps point to them
    std::deque<Block> blocks;                   // the built-in arrowheads drawn, which steps point into
    std::unordered_set<const Block*> active;    // the blocks of INSERTs, DIMENSIONs and MULTILEADERs being drawn
    std::unordered_set<const Block*> arrows;    // the arrowhead blocks being drawn
    std::unordered_map<const Block*, int> met;  // how many times a block was met while it was being drawn

    // Whether a block is drawn now: not when it is being drawn (a block that inserts itself). Blocks that all insert each other have
    // more paths than can be drawn, so one met this many times is not entered any more
    bool enter(const Block* b) {
        if (auto seen = met.find(b); seen != met.end() && seen->second >= kBlockMetLimit) return false;
        if (active.insert(b).second) return true;
        ++met[b];
        return false;
    }

    const BlockHost* add_host(BlockHost host) {
        hosts.push_back(std::move(host));
        return &hosts.back();
    }

    void run();
};

class MLeaderDrawer {
public:
    // extents: what it draws, as the extents count it (null: only the graphics), mapped by
    // `map`; the entities of its blocks go to block_entity instead, with their transform.
    // graphics: the stream a MULTILEADER in a block draws into (the stream of the one it is in); null: its own
    MLeaderDrawer(const Drawing& dwg, const Entity& e, const ProxyTables& tables, TextLayout& layout, bool unicode,
                  Extents* extents, const Matrix& map = identity(), BlockEntityExtents block_entity = nullptr,
                  ProxyGraphics* graphics = nullptr)
        : dwg_(dwg), e_(e), d_(e.as_mleader()), c_(d_.context), tables_(tables), layout_(layout),
          unicode_(unicode), code_page_(drawing_code_page(dwg)), ext_(extents), block_entity_(std::move(block_entity)),
          g_(graphics ? *graphics : own_graphics_) {
        stack_.push_back(map);
    }

    std::vector<uint8_t> draw() {
        BlockWalk walk;
        begin(walk);
        walk.run();
        return g_.bytes();
    }

    // The drawing's first part: the content. A block's entities are left as steps of the walk; the rest of the drawing
    // follows the last of them (finish)
    void begin(BlockWalk& walk) {
        g_.true_color(entity_color());
        g_.command_51();
        if (c_.has_block && !c_.has_text) {
            draw_block(walk);
            return;
        }
        if (c_.has_text) draw_text();
        finish(walk);
    }

    // One step of the walk
    void step(BlockWalk& walk, const DrawStep& s) {
        switch (s.kind) {
            case DrawStepKind::Entity:
                draw_block_entity(walk, *s.entity, *s.host, s.top);
                break;
            case DrawStepKind::EntityEnd:
                end_entity(s.trait);
                break;
            case DrawStepKind::Copies:
                draw_insert_copy(walk, s);
                break;
            case DrawStepKind::CopyEnd:
                pop();
                break;
            case DrawStepKind::InsertEnd:
                if (s.entered) walk.active.erase(s.block);
                draw_insert_attributes(*s.entity, *s.host);
                break;
            case DrawStepKind::DimensionEnd:
                walk.active.erase(s.block);
                pop();
                break;
            case DrawStepKind::BlockEnd:
                if (s.entered) walk.active.erase(s.block);
                pop();
                draw_attributes(*s.block, block_matrix_);
                finish(walk);
                break;
            case DrawStepKind::ArrowEnd:
                walk.arrows.erase(s.block);
                pop();
                after_arrow_block();
                end_line(c_.leaders[leader_at_].lines[line_at_]);
                ++line_at_;
                draw_leaders(walk);
                break;
            case DrawStepKind::InnerEnd:
                break;                  // the step owns the drawer: it is released with the step
        }
    }

private:
    // The drawing after the content: the leaders, the underline and the frame. The leaders go on in the steps of an
    // arrowhead's block when a line has one.
    void finish(BlockWalk& walk) {
        // Invisible leaders: the content only — unless a line is visible by its own type (2010)
        if (d_.leader_type == 0 && !has_visible_line()) return;
        g_.command_51();
        g_.layer(layer_index(e_.props.layer));
        g_.true_color(leader_color());
        g_.linetype(leader_linetype());
        leader_at_ = 0;
        line_at_ = 0;
        draw_leaders(walk);
    }

    // --- what is drawn, and its extents -----------------------------------------------

    void add(const Point3& p) {
        if (ext_) ext_->add(transform_point(stack_.back(), p));
    }

    void polyline(const std::vector<Point3>& points) {
        g_.polyline(points);
        for (const auto& p : points) add(p);
    }

    void polygon(const std::vector<Point3>& points) {
        g_.polygon(points);
        for (const auto& p : points) add(p);
    }

    // An arc (or circle) as an ellipse in the current transform: exactly
    void curve_extents(const Point3& center, double radius, const Point3& normal, double a0, double sweep, bool full) {
        if (!ext_) return;
        Point3 ax, ay;
        ecs::arbitrary_axis(normal, ax, ay);
        const Matrix& m = stack_.back();
        ellipse_extents(*ext_, transform_point(m, center), apply_vector(m, ax * radius), apply_vector(m, ay * radius),
                        a0, a0 + sweep, full);
    }

    void circle(const Point3& center, double radius, const Point3& normal) {
        g_.circle(center, radius, normal);
        curve_extents(center, radius, normal, 0.0, 2.0 * M_PI, true);
    }

    // start: from the center to the start, in the arc's plane; a0 its angle there
    void arc_to(const Point3& center, double radius, const Point3& normal, const Point3& start, double sweep,
                int arc_type) {
        g_.arc(center, radius, normal, start, sweep, arc_type);
        Point3 ax, ay;
        ecs::arbitrary_axis(normal, ax, ay);
        curve_extents(center, radius, normal, std::atan2(dot(start, ay), dot(start, ax)), sweep, false);
    }

    void push(const Matrix& m) {
        g_.push_transform(m);
        stack_.push_back(multiply(stack_.back(), m));
    }

    void pop() {
        g_.pop_transform();
        if (stack_.size() > 1) stack_.pop_back();
    }

    // A text's box in its plane (x along direction, y up) from origin, in the extents
    void add_box(const TextBox& b, const Point3& origin, const Point3& direction, const Point3& normal) {
        if (!b.valid) return;
        Point3 x = unit(direction);
        Point3 y = unit(cross(normal, x));
        for (double u : {b.min_x, b.max_x}) {
            for (double v : {b.min_y, b.max_y}) add(origin + x * u + y * v);
        }
    }

    // --- properties -------------------------------------------------------------------

    int layer_index(const std::string& name) const {
        for (size_t i = 0; i < tables_.layers.size(); ++i) {
            if (same_name(tables_.layers[i], name)) return static_cast<int>(i);
        }
        return 0;
    }

    int linetype_index(const std::string& name) const {
        if (name.empty() || same_name(name, "ByLayer")) return kLinetypeByLayer;
        if (same_name(name, "ByBlock")) return kLinetypeByBlock;
        int i = 0;
        for (const auto& lt : tables_.linetypes) {
            if (same_name(lt, "ByLayer") || same_name(lt, "ByBlock")) continue;
            if (same_name(lt, name)) return i;
            i++;
        }
        return kLinetypeByLayer;
    }

    int32_t entity_color() const { return CmColor::from_color(e_.props.color).raw; }
    int entity_linetype() const { return linetype_index(e_.props.linetype); }

    // ByBlock: the entity's
    int32_t resolved(const CmColor& c) const { return c.method() == 0xC1 ? entity_color() : c.raw; }

    int32_t leader_color() const { return resolved(d_.leader_color); }

    int leader_linetype() const {
        if (same_name(d_.leader_linetype, "ByBlock")) return entity_linetype();
        return linetype_index(d_.leader_linetype);
    }

    const Layer* find_layer(const std::string& name) const {
        for (const auto& l : dwg_.layers()) {
            if (same_name(l.name, name)) return &l;
        }
        return nullptr;
    }

    int continuous() const { return linetype_index("Continuous"); }

    // A layer's color as a color (an ACI, or its true color)
    int32_t layer_color(const std::string& name) const {
        const Layer* l = find_layer(name);
        Color c = l ? l->color : Color::white();
        if (!c.has_rgb()) c = Color::from_aci(std::abs(c.aci));
        return CmColor::from_color(c).raw;
    }

    int lineweight_default() const {
        if (const HeaderVar* hv = dwg_.find_header("$LWDEFAULT")) {
            if (!hv->values.empty()) return std::atoi(hv->values[0].second.c_str());
        }
        return 25;
    }

    // A lineweight as drawn: ByLayer its layer's (a block's layer 0 the MULTILEADER's),
    // the default as $LWDEFAULT
    int resolved_lineweight(int lw, const std::string& layer) const {
        if (lw == -2) lw = e_.props.lineweight == -2 ? -1 : e_.props.lineweight;
        if (lw == -1) {
            const Layer* l = find_layer(same_name(layer, "0") ? e_.props.layer : layer);
            lw = l ? l->lineweight : -3;
            if (lw == -1 || lw == -2) lw = -3;
        }
        if (lw == -3) lw = lineweight_default();
        return lw;
    }

    // A block entity's color as drawn: ByLayer its layer's (layer 0 the MULTILEADER's), ByBlock
    // the block content's
    int32_t resolved_block_color(const Entity& be) const {
        const Color& c = be.props.color;
        if (c.is_by_layer()) return layer_color(same_name(be.props.layer, "0") ? e_.props.layer : be.props.layer);
        if (c.is_by_block()) {
            int32_t b = resolved(c_.block_color);
            if (CmColor{b}.method() == 0xC0) return layer_color(e_.props.layer);
            return b;
        }
        return CmColor::from_color(c).raw;
    }

    // --- text -------------------------------------------------------------------------

    MTextData content_mtext() const {
        MTextData m;
        m.insertion_point = c_.text_location;
        m.height = c_.text_height;
        m.reference_width = c_.text_width;
        m.attachment = c_.text_attachment;
        m.drawing_direction = c_.text_flow;
        m.content = c_.text;
        m.style = c_.text_style.empty() ? "Standard" : c_.text_style;
        m.x_axis_direction = c_.text_direction;
        m.line_spacing_style = c_.line_spacing_style;
        m.line_spacing_factor = c_.line_spacing_factor;
        m.defined_height = c_.text_defined_height;
        return m;
    }

    // The runs of an MTEXT: each a TEXT, a bar a polyline in Continuous; a run's own color
    // (\C, \c) set before it, the color back after it
    // cells: the runs' cells in the extents (the content's text)
    void draw_runs(const MTextSize& s, const Point3& origin, const Point3& direction, const Point3& normal,
                   int32_t color, bool cells = false) {
        Point3 x = unit(direction);
        Point3 y = unit(cross(normal, x));
        int32_t current = color;
        for (const auto& run : s.runs) {
            int32_t want = run.has_color ? CmColor::from_color(run.color).raw : color;
            if (run.has_color && CmColor{want}.method() == 0xC1) want = color;
            if (run.has_color && CmColor{want}.method() == 0xC0) want = color;
            if (want != current) {
                g_.true_color(want);
                current = want;
            }
            if (run.bar) {
                g_.linetype(linetype_index("Continuous"));
                polyline({origin + x * run.x + y * run.y, origin + x * run.x2 + y * run.y2});
                continue;
            }
            // In the extents: its cell, from the pen to where it ends, from the font's descent
            // below the baseline to its ascent above (AutoCAD's $EXTMIN/$EXTMAX at MULTILEADERs
            // in the files of test/data/README.md: Arial 434 and 1854, Arial Unicode MS 555)
            // A shape font's run by its ink (aetel's MULTILEADERs in txt.shx: the extents end at
            // the baseline under "Also a thru hole")
            if (ext_ && cells && run.fonts && run.fonts->font) {
                const Font& f = *run.fonts->font;
                TextBox cell;
                if (f.is_shape_font()) {
                    LineMeasure m = measure_line(*run.fonts, code_points(run.text), run.height, run.width_factor,
                                                 run.oblique);
                    if (m.ink.valid) {
                        cell.add(run.x + m.ink.min_x * run.tracking, run.y + m.ink.min_y);
                        cell.add(run.x + m.ink.max_x * run.tracking, run.y + m.ink.max_y);
                    }
                } else {
                    cell.add(run.x, run.y - f.descent() * run.height);
                    cell.add(run.x + run.advance, run.y + f.ascent() * run.height);
                }
                add_box(cell, origin, direction, normal);
            }
            ProxyText t;
            t.position = origin + x * run.x + y * run.y;
            t.normal = normal;
            t.direction = x;
            t.text = run.text;
            t.height = run.height;
            t.width_factor = run.width_factor;
            t.oblique = run.oblique * M_PI / 180.0;
            t.tracking = run.tracking;
            t.underline = run.underline;
            t.overline = run.overline;
            set_font(t, run.fonts);
            g_.text(t, unicode_, code_page_);
        }
    }

    // The font as AutoCAD names it: a TrueType family by its face, a shape font by its file
    static void set_font(ProxyText& t, const StyleFonts* f) {
        if (!f) return;
        if (!f->family.empty()) {
            t.face = f->family;
            t.bold = f->bold;
            t.italic = f->italic;
            t.pitch_and_family = f->pitch_and_family;
        } else if (f->font && !f->font->is_shape_font()) {
            // A TrueType font by its file: its face as Windows names it (LibreDWG's Leader.dxf, style
            // "Arial.ttf": Arial, 34)
            FontFace face = f->font->face();
            t.face = face.family;
            t.bold = face.bold;
            t.italic = face.italic;
            t.pitch_and_family = face.pitch_and_family;
        } else {
            // A shape font by its file, with its extension (AutoCAD's "txt.shx" for "txt")
            t.font = f->file;
            if (!t.font.empty() && t.font.find('.') == std::string::npos) t.font += ".shx";
        }
        t.big_font = f->big_font_file;
    }

    void draw_text() {
        g_.marker(kTextMarker);
        int32_t color = c_.text_color.raw;
        if (c_.text_color.method() != 0xC0) g_.true_color(color);
        MTextData m = content_mtext();
        MTextSize s = layout_.mtext(m, true);
        text_size_ = s;
        draw_runs(s, c_.text_location, c_.text_direction, c_.text_normal, color, true);
    }

    // --- block ------------------------------------------------------------------------

    // The block's transform is pushed here and its entities are steps of the walk; the step BlockEnd pops it, draws the
    // attributes and the rest of the drawing. A block already being drawn (a MULTILEADER in a block of itself) has no
    // entities drawn.
    void draw_block(BlockWalk& walk) {
        g_.marker(kBlockMarker);
        const Block* b = dwg_.find_block(c_.block);
        if (!b) {
            finish(walk);
            return;
        }
        Matrix m = block_transform(c_.block_position, c_.block_normal, c_.block_rotation, c_.block_scale,
                                   b->base_point);
        block_ = b;
        block_matrix_ = m;
        push(m);
        BlockHost host;
        host.layer = e_.props.layer;
        int32_t by_block = resolved(c_.block_color);
        host.color = CmColor{by_block}.method() == 0xC0 ? layer_color(e_.props.layer) : by_block;
        host.lineweight = e_.props.lineweight == -2 ? -1 : e_.props.lineweight;
        const BlockHost* block_host = walk.add_host(std::move(host));
        DrawStep end;
        end.kind = DrawStepKind::BlockEnd;
        end.drawer = this;
        end.block = b;
        end.entered = walk.enter(b);
        walk.steps.push_back(end);
        if (!end.entered) return;
        for (size_t i = b->entities.size(); i-- > 0;) {
            DrawStep s;
            s.kind = DrawStepKind::Entity;
            s.drawer = this;
            s.entity = &b->entities[i];
            s.host = block_host;
            s.top = true;
            walk.steps.push_back(s);
        }
    }

    // The block's attributes after its entities: marker 15005, then each ATTDEF's value
    // (the MULTILEADER's 302 for it) as a TEXT where the ATTDEF places it, in the WCS
    void draw_attributes(const Block& b, const Matrix& m) {
        bool any = false;
        for (const auto& be : b.entities) any = any || be.type == EntityType::Attdef;
        if (!any) return;
        g_.marker(kAttributesMarker);
        for (const auto& be : b.entities) {
            if (be.type != EntityType::Attdef) continue;
            const auto& a = be.data.get<AttdefData>();
            if (a.flags & 2) continue;                  // constant: drawn in the block
            const MLeaderAttribute* value = nullptr;
            for (const auto& v : d_.attributes) {
                if (same_name(v.attdef_handle, be.props.handle)) value = &v;
            }
            if (!value || value->text.empty()) continue;
            TextLine line{a.style, value->text, a.height, a.width_factor, a.oblique_angle, a.rotation,
                          a.generation_flags, a.h_justify, a.v_justify, a.start_point, a.alignment_point};
            // Centered (72 = 1, 4), AutoCAD centers an attribute of the content on its ink, not
            // on its advance as TEXT (the MULTILEADERs with attributes in test/data/README.md)
            draw_text_line(line, be.props.extrusion, m, true);
        }
    }

    // A line of text (TEXT, ATTDEF, ATTRIB) as AcGi's text: from where its pen starts (TEXT's
    // justification), its characters with the %% codes drawn, in the plane of its extrusion,
    // mapped by m; its box in the extents. on_ink: centered on its ink (a MULTILEADER's
    // attribute)
    void draw_text_line(const TextLine& line, const Point3& extrusion, const Matrix& m, bool on_ink) {
        TextPen pen = layout_.text_pen(line);
        if (!pen.font) return;
        double r = pen.rotation * M_PI / 180.0;
        if (on_ink && (line.h_justify == 1 || line.h_justify == 4)) {
            LineMeasure lm = measure_line(layout_.fonts(line.style), text_characters(line.content, code_page_),
                                          pen.height, pen.width_factor, line.oblique_angle);
            if (lm.ink.valid) {
                double shift = lm.advance / 2.0 - (lm.ink.min_x + lm.ink.max_x) / 2.0;
                double sx = (line.generation_flags & 2) ? -1.0 : 1.0;
                pen.pen.x += std::cos(r) * shift * sx;
                pen.pen.y += std::sin(r) * shift * sx;
            }
        }
        Point3 ax, ay;
        ecs::arbitrary_axis(extrusion, ax, ay);
        Point3 dir = apply_vector(m, ax * std::cos(r) + ay * std::sin(r));
        Point3 up = apply_vector(m, ay * std::cos(r) - ax * std::sin(r));
        ProxyText t;
        t.position = transform_point(m, ecs::ecs_to_wcs(pen.pen, extrusion));
        t.normal = unit(cross(dir, up));
        t.direction = unit(dir);
        t.text = drawn_text(line.content, layout_.fonts(line.style));
        t.height = pen.height * length(up);
        t.width_factor = pen.width_factor * length(dir) / std::max(length(up), 1e-300);
        t.oblique = line.oblique_angle * M_PI / 180.0;
        t.backward = (line.generation_flags & 2) != 0;
        t.upside_down = (line.generation_flags & 4) != 0;
        set_font(t, &layout_.fonts(line.style));
        g_.text(t, unicode_, code_page_);
        // Its box: as TEXT places the line, moved as the pen moved
        if (ext_) {
            TextPen plain = layout_.text_pen(line);
            Point3 shift = pen.pen - plain.pen;
            TextCorners corners = layout_.text(line);
            if (corners.ink) {
                for (const auto& q : corners.corners) add(transform_point(m, ecs::ecs_to_wcs(q + shift, extrusion)));
            }
        }
    }

    // TEXT's characters as drawn: the %% codes as the characters
    std::string drawn_text(const std::string& content, const StyleFonts& f) const {
        std::string out;
        for (char32_t c : text_characters(content, code_page_)) {
            if (c == kUnderline || c == kOverline) continue;
            if (c == kDegree) c = U'°';
            else if (c == kPlusMinus) c = U'±';
            else if (c == kDiameter) c = f.font && !f.font->is_shape_font() && f.font->glyph(U'Ø').found ? U'Ø' : U'∅';
            append_utf8(out, c);
        }
        return out;
    }

    static void append_utf8(std::string& out, char32_t c) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }

    // A block entity's color as drawn: ByLayer its layer's (layer 0: the host's), ByBlock the
    // host's
    int32_t drawn_color(const EntityProperties& p, const BlockHost& host) const {
        const Color& c = p.color;
        if (c.is_by_layer()) return layer_color(same_name(p.layer, "0") ? host.layer : p.layer);
        if (c.is_by_block()) return host.color;
        return CmColor::from_color(c).raw;
    }

    // A block entity's lineweight as drawn: ByBlock the host's, ByLayer its layer's (layer 0:
    // the host's), the default as $LWDEFAULT
    int drawn_lineweight(const EntityProperties& p, const BlockHost& host) const {
        int lw = p.lineweight;
        if (lw == -2) lw = host.lineweight;
        if (lw == -1) {
            const Layer* l = find_layer(same_name(p.layer, "0") ? host.layer : p.layer);
            lw = l ? l->lineweight : -3;
            if (lw == -1 || lw == -2) lw = -3;
        }
        if (lw == -3) lw = lineweight_default();
        return lw;
    }

    // A block's entity as AutoCAD draws it in a MULTILEADER's proxy graphics: its color and
    // lineweight, then its own graphics — what its worldDraw() records. Seen in AutoCAD's
    // files: a MULTILEADER's block (CIRCLE and ARC with fill off, MTEXT, ATTDEF — its color
    // and lineweight but nothing drawn), a dimension's block (LINE a polyline with its normal,
    // POINT a polyline of one point in Continuous without fill, SOLID a filled shell of four
    // vertices in Continuous), a table's (an INSERT its block in its transform), a 2004
    // surface's (an ellipse, 44), an IMAGE's (its clip boundary), a HELIX's (its polyline).
    // The others as ODA's File Converter draws them (the second oracle, B38): LWPOLYLINE
    // and 2D POLYLINE as LWPLINE (33), 3D POLYLINE a polyline in Continuous, a polygon mesh a
    // mesh and a polyface mesh a shell (their markers ODA's), 3DFACE a 2 × 2 mesh, HATCH a
    // shell (solid) or its pattern's dashes, RAY and XLINE, LEADER and MLINE their lines,
    // TOLERANCE its texts and frame. A block's entity's extents are counted by extents.cpp
    // (top: an entity of the block the MULTILEADER draws, given to block_entity_).
    // An INSERT, a DIMENSION and a MULTILEADER draw blocks: their entities become steps of the walk, and the entity
    // ends with its own step after them.
    void draw_block_entity(BlockWalk& walk, const Entity& be, const BlockHost& host, bool top) {
        g_.true_color(drawn_color(be.props, host));
        g_.lineweight(drawn_lineweight(be.props, host));
        // Its extents are measured by extents.cpp, whatever it holds: drawing it here would add nothing to them
        if (top && ext_ && block_entity_) {
            block_entity_(be, stack_.back());
            return;
        }
        const bool draws_blocks = be.type == EntityType::Insert || be.type == EntityType::Dimension ||
                                  be.type == EntityType::MLeader;
        if (draws_blocks) {
            DrawStep end;
            end.kind = DrawStepKind::EntityEnd;
            end.drawer = this;
            walk.steps.push_back(end);
        }
        const Point3& n = be.props.extrusion;
        double thickness = be.props.thickness;
        bool trait = thickness != 0.0 &&
                     (be.type == EntityType::Line || be.type == EntityType::Point || be.type == EntityType::Circle ||
                      be.type == EntityType::Arc || be.type == EntityType::Solid || be.type == EntityType::Trace ||
                      be.type == EntityType::Text || be.type == EntityType::Shape);
        if (trait) g_.thickness(thickness);
        switch (be.type) {
            case EntityType::Line: {
                const auto& l = be.as_line();
                polyline_normal({l.start, l.end}, n);
                break;
            }
            case EntityType::Point: {
                const Point3& p = be.as_point().location;
                g_.linetype(continuous());
                g_.fill(false);
                polyline_normal({p, p}, n);
                g_.fill(true);
                break;
            }
            case EntityType::Circle: {
                const auto& ci = be.as_circle();
                g_.fill(false);
                circle(ecs::ecs_to_wcs(ci.center, n), ci.radius, n);
                g_.fill(true);
                break;
            }
            case EntityType::Arc: {
                const auto& a = be.as_arc();
                Point3 ax, ay;
                ecs::arbitrary_axis(n, ax, ay);
                double a0 = a.start_angle * M_PI / 180.0;
                double sweep = (a.end_angle - a.start_angle) * M_PI / 180.0;
                while (sweep <= 0.0) sweep += 2.0 * M_PI;
                Point3 start = (ax * std::cos(a0) + ay * std::sin(a0)) * a.radius;
                g_.fill(false);
                arc_to(ecs::ecs_to_wcs(a.center, n), a.radius, n, start, sweep, 0);
                g_.fill(true);
                break;
            }
            case EntityType::Ellipse: {
                // 44 as AutoCAD's 2004 surfaces have it: center, normal, the radii, the
                // parameters, the major axis's angle in the normal's OCS
                const auto& d = be.as_ellipse();
                Point3 normal = unit(n);
                Point3 ax, ay;
                ecs::arbitrary_axis(normal, ax, ay);
                double major = length(d.major_axis);
                double angle = std::atan2(dot(d.major_axis, ay), dot(d.major_axis, ax));
                g_.fill(false);
                g_.ellipse_arc(d.center, normal, major, major * d.ratio, d.start_param, d.end_param, angle, 0);
                g_.fill(true);
                break;
            }
            case EntityType::LwPolyline:
                g_.fill(true);
                g_.lwpolyline(lwpline_data(be.as_lwpolyline(), thickness, n));
                break;
            case EntityType::Polyline:
                draw_polyline(be);
                break;
            case EntityType::Spline:
                polyline(spline_points(be.as_spline()));
                break;
            case EntityType::Helix:
                // As AutoCAD draws it (its proxy graphics, B39)
                polyline(helix_points(be.as_helix()));
                break;
            case EntityType::Mesh: {
                // As AutoCAD draws it (its proxy graphics, B48): each edge after its marker
                std::vector<std::vector<Point3>> wire = mesh_wire(be.as_mesh());
                for (size_t i = 0; i < wire.size(); ++i) {
                    g_.marker(static_cast<int>(8 * i + 2));
                    polyline(wire[i]);
                }
                break;
            }
            case EntityType::Text: {
                const auto& d = be.as_text();
                draw_text_line({d.style, d.content, d.height, d.width_factor, d.oblique_angle, d.rotation,
                                d.generation_flags, d.h_justify, d.v_justify, d.position, d.alignment_point},
                               n, identity(), false);
                break;
            }
            case EntityType::Attdef: {
                // A constant one is part of the block, with its value; the others are drawn
                // after the block with the MULTILEADER's values
                const auto& d = be.as_attdef();
                if (!(d.flags & 2)) break;
                draw_text_line({d.style, d.default_value, d.height, d.width_factor, d.oblique_angle, d.rotation,
                                d.generation_flags, d.h_justify, d.v_justify, d.start_point, d.alignment_point},
                               n, identity(), false);
                break;
            }
            case EntityType::MText: {
                const auto& mt = be.as_mtext();
                MTextSize sz = layout_.mtext(mt, true);
                draw_runs(sz, mt.insertion_point, mt.x_axis_direction, n, drawn_color(be.props, host));
                break;
            }
            case EntityType::Solid: {
                const auto& q = be.as_solid();
                draw_filled_quad({ecs::ecs_to_wcs(q.p0, n), ecs::ecs_to_wcs(q.p1, n), ecs::ecs_to_wcs(q.p3, n),
                                  ecs::ecs_to_wcs(q.p2, n)});
                break;
            }
            case EntityType::Trace: {
                const auto& q = be.as_trace();
                draw_filled_quad({ecs::ecs_to_wcs(q.p0, n), ecs::ecs_to_wcs(q.p1, n), ecs::ecs_to_wcs(q.p3, n),
                                  ecs::ecs_to_wcs(q.p2, n)});
                break;
            }
            case EntityType::Face3D:
                draw_face(be.as_face3d());
                break;
            case EntityType::Insert:
                start_insert(walk, be, host);
                break;
            case EntityType::Dimension:
                start_dimension(walk, be, host);
                break;
            case EntityType::Hatch:
                draw_hatch(be);
                break;
            case EntityType::Xline: {
                const auto& d = be.as_xline();
                g_.xline(d.base_point, d.base_point + d.direction);
                break;
            }
            case EntityType::Ray: {
                const auto& d = be.as_ray();
                g_.ray(d.base_point, d.base_point + d.direction);
                break;
            }
            case EntityType::Leader: {
                // Each segment, its marker 4 on (ODA's)
                const auto& v = be.as_leader().vertices;
                for (size_t i = 0; i + 1 < v.size(); ++i) {
                    g_.marker(4 + static_cast<int>(i));
                    polyline_normal({v[i], v[i + 1]}, n);
                }
                break;
            }
            case EntityType::Mline:
                draw_mline(be);
                break;
            case EntityType::Tolerance:
                draw_tolerance(be);
                break;
            case EntityType::Shape:
                draw_shape(be);
                break;
            case EntityType::Image:
            case EntityType::Wipeout:
                draw_image(be.as_image());
                break;
            case EntityType::Solid3D:
            case EntityType::Region:
            case EntityType::Body:
            case EntityType::Surface:
            case EntityType::ExtrudedSurface:
            case EntityType::LoftedSurface:
            case EntityType::RevolvedSurface:
            case EntityType::SweptSurface:
            case EntityType::NurbsSurface:
            case EntityType::PlaneSurface: {
                // Its B-rep's edges (acis.cpp), each its polyline with its marker (ODA's)
                if (!acis_) acis_ = std::make_unique<AcisSource>(dwg_);
                int k = 0;
                for (const auto& line : acis_edge_polylines(acis_->bodies(be))) {
                    g_.marker(4 * ++k + 2);
                    polyline(line);
                }
                break;
            }
            case EntityType::MLeader:
                // A multileader in the block: its own graphics, in the block's transform
                start_inner(walk, be);
                break;
            case EntityType::Unknown:
                // An entity HDxf does not know: its own proxy graphics, as AutoCAD drew it
                g_.append(own_proxy(be));
                break;
            case EntityType::Attrib:
            case EntityType::Viewport:
            case EntityType::ArcDimension:
            case EntityType::LargeRadialDimension:
            case EntityType::Table:
            case EntityType::SectionObject:
            case EntityType::Light:
            case EntityType::CoordinationModel:
            case EntityType::PdfUnderlay:
            case EntityType::DwfUnderlay:
            case EntityType::DgnUnderlay:
            case EntityType::OleFrame:
            case EntityType::Ole2Frame:
            case EntityType::ProxyEntity:
            case EntityType::TcPoint:
                break;
        }
        if (!draws_blocks) end_entity(trait);
    }

    // After an entity: its thickness is off again
    void end_entity(bool trait) {
        if (trait) g_.thickness(0.0);
    }

    // An unknown entity's proxy graphics: the 310 groups after its 92 (160 from 2010) in
    // AcDbEntity
    static std::vector<uint8_t> own_proxy(const Entity& e) {
        std::vector<uint8_t> out;
        if (e.raw.empty()) return out;
        bool in = false, entity = false;
        auto hex = [](char c) {
            return c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0));
        };
        for (const auto& g : e.raw.groups()) {
            if (g.code == 100) {
                if (in) break;
                entity = g.value == "AcDbEntity";
                continue;
            }
            if (!in) {
                in = entity && (g.code == 92 || g.code == 160);
                continue;
            }
            if (g.code != 310) break;
            for (size_t i = 0; i + 1 < g.value.size(); i += 2) {
                out.push_back(static_cast<uint8_t>(hex(g.value[i]) * 16 + hex(g.value[i + 1])));
            }
        }
        return out;
    }

    // SOLID and TRACE: a filled shell of their four corners in their outline's order (the
    // 1st, 2nd, 4th and 3rd), in Continuous, its edges' markers 1 … 4 and visible (AutoCAD's
    // dimension blocks)
    void draw_filled_quad(const std::vector<Point3>& corners) {
        g_.fill(true);
        g_.linetype(continuous());
        g_.marker(-1);
        g_.shell(corners, {4, 0, 1, 2, 3}, {0x60, 1, 2, 3, 4, 1, 1, 1, 1, 0, 0});
        for (const auto& p : corners) add(p);
        g_.fill(true);
    }

    // 3DFACE: a mesh of 2 × 2 (the 1st, 2nd, 4th and 3rd corner), in Continuous; its edges'
    // markers 1 … 4 and their visibility — the rows' edges (1st–2nd, 4th–3rd), then the
    // columns' (1st–4th, 2nd–3rd) — as ODA draws it
    void draw_face(const Face3DData& f) {
        auto visible = [&](int bit) { return (f.invisible_edges & bit) ? 0 : 1; };
        g_.linetype(continuous());
        g_.mesh(2, 2, {f.p0, f.p1, f.p3, f.p2}, {0x60, 1, 2, 3, 4, visible(1), visible(4), visible(8), visible(2), 0, 0});
    }

    // POLYLINE: 2D as LWPLINE (its vertices, bulges and widths; the spline frame's control
    // points left out); 3D a polyline in Continuous; a polygon mesh a mesh (closed: its first
    // row or column again) and a polyface mesh a shell, in Continuous, with ODA's markers —
    // an edge 4k + 6, a face 4k + 5 — and a polyface's edge visibility (a negative index)
    void draw_polyline(const Entity& be) {
        const auto& d = be.as_polyline();
        const Point3& n = be.props.extrusion;
        std::vector<const Vertex*> drawn;
        for (const auto& v : d.vertices) {
            if (!(v.flags & 16)) drawn.push_back(&v);
        }
        if (d.is_polyface_mesh()) {
            std::vector<Point3> points;
            std::vector<int32_t> faces, markers, visible;
            for (const auto& v : d.vertices) {
                if ((v.flags & 128) && (v.flags & 64)) points.push_back(v.position);
            }
            int faces_count = 0;
            for (const auto& v : d.vertices) {
                if (!(v.flags & 128) || (v.flags & 64)) continue;
                int index[4];
                int count = 0;
                for (int k = 0; k < 4; ++k) if (v.face[k] != 0) index[count++] = v.face[k];
                if (count == 0) continue;
                faces.push_back(count);
                for (int k = 0; k < count; ++k) {
                    faces.push_back(std::abs(index[k]) - 1);
                    visible.push_back(index[k] < 0 ? 0 : 1);
                }
                ++faces_count;
            }
            std::vector<int32_t> traits = {0x60};
            for (size_t k = 0; k < visible.size(); ++k) traits.push_back(static_cast<int32_t>(4 * k + 6));
            traits.insert(traits.end(), visible.begin(), visible.end());
            traits.push_back(0x63);
            for (int k = 0; k < faces_count; ++k) traits.push_back(256);
            for (int k = 0; k < faces_count; ++k) traits.push_back(layer_index(be.props.layer));
            for (int k = 0; k < faces_count; ++k) traits.push_back(4 * k + 5);
            for (int k = 0; k < faces_count; ++k) traits.push_back(1);
            traits.push_back(0);
            g_.linetype(continuous());
            g_.shell(points, faces, traits);
            return;
        }
        if (d.is_polygon_mesh()) {
            int rows = d.m_count, columns = d.n_count;
            if (rows < 1 || columns < 1 || static_cast<size_t>(rows * columns) > d.vertices.size()) return;
            bool closed_m = (d.flags & 1) != 0, closed_n = (d.flags & 32) != 0;
            int out_rows = rows + (closed_m ? 1 : 0), out_columns = columns + (closed_n ? 1 : 0);
            std::vector<Point3> points;
            for (int r = 0; r < out_rows; ++r) {
                for (int c = 0; c < out_columns; ++c) {
                    points.push_back(d.vertices[static_cast<size_t>((r % rows) * columns + (c % columns))].position);
                }
            }
            int edges = (out_rows - 1) * out_columns + (out_columns - 1) * out_rows;
            int faces = (out_rows - 1) * (out_columns - 1);
            std::vector<int32_t> traits = {0x20};
            for (int k = 0; k < edges; ++k) traits.push_back(4 * k + 6);
            traits.push_back(0x20);
            for (int k = 0; k < faces; ++k) traits.push_back(4 * k + 5);
            traits.push_back(0);
            g_.linetype(continuous());
            g_.mesh(out_rows, out_columns, points, traits);
            return;
        }
        if (d.is_3d_polyline()) {
            std::vector<Point3> points;
            for (const Vertex* v : drawn) points.push_back(v->position);
            if (d.is_closed() && !points.empty()) points.push_back(points.front());
            g_.linetype(continuous());
            if (points.size() >= 2) polyline(points);
            return;
        }
        LwPolylineData lw;
        lw.flags = d.flags & (1 | 128);
        lw.elevation = drawn.empty() ? 0.0 : drawn.front()->position.z;
        for (const Vertex* v : drawn) {
            LwVertex x;
            x.position = {v->position.x, v->position.y};
            x.start_width = v->start_width;
            x.end_width = v->end_width;
            x.bulge = v->bulge;
            lw.vertices.push_back(x);
        }
        g_.fill(true);
        g_.lwpolyline(lwpline_data(lw, be.props.thickness, n));
    }

    // SPLINE as a polyline: each knot span halved until the curve's midpoint is within a
    // thousandth of the control points' extent of the chord (at least 2, at most 256 parts)
    std::vector<Point3> spline_points(const SplineData& d) const {
        SplineData s = spline_curve(d);
        std::vector<Point3> out;
        size_t p = static_cast<size_t>(std::max(0, s.degree));
        if (s.control_points.size() < p + 1 || s.knots.size() != s.control_points.size() + p + 1) return out;
        Extents box;
        for (const auto& q : s.control_points) box.add(q);
        double tol = 1e-3 * length(box.max - box.min);
        std::function<void(double, double, const Point3&, const Point3&, int)> split =
            [&](double a, double b, const Point3& pa, const Point3& pb, int depth) {
                double m = (a + b) / 2.0;
                Point3 pm = nurbs_point(s, m);
                if (depth < 8 && (depth < 1 || length(pm - (pa + pb) * 0.5) > tol)) {
                    split(a, m, pa, pm, depth + 1);
                    split(m, b, pm, pb, depth + 1);
                    return;
                }
                out.push_back(pb);
            };
        const auto& k = s.knots;
        for (size_t i = p; i < s.control_points.size(); ++i) {
            if (k[i + 1] <= k[i]) continue;
            Point3 pa = nurbs_point(s, k[i]);
            if (out.empty()) out.push_back(pa);
            // The span's end from its left (the last span's end is the curve's)
            double end = k[i + 1];
            Point3 pb = i + 1 == s.control_points.size() ? s.control_points.back() : nurbs_point(s, end);
            if (s.is_rational() && i + 1 == s.control_points.size()) pb = nurbs_point(s, std::nextafter(end, k[i]));
            split(k[i], end, pa, pb, 0);
        }
        return out;
    }

    // An INSERT in the block: its block in its transform (each copy of an array), its
    // entities' layer 0 and ByBlock taken from the INSERT; then its attributes. The copies are
    // steps of the walk (Copies draws one and queues the next); InsertEnd draws the attributes.
    void start_insert(BlockWalk& walk, const Entity& be, const BlockHost& host) {
        const auto& d = be.as_insert();
        BlockHost inner;
        inner.layer = same_name(be.props.layer, "0") ? host.layer : be.props.layer;
        inner.color = drawn_color(be.props, host);
        inner.lineweight = drawn_lineweight(be.props, host);
        const Block* b = dwg_.find_block(d.block_name);
        const bool entered = b && walk.enter(b);
        if (!entered) {
            draw_insert_attributes(be, host);
            return;
        }
        DrawStep end;
        end.kind = DrawStepKind::InsertEnd;
        end.drawer = this;
        end.entity = &be;
        end.host = &host;
        end.block = b;
        end.entered = true;
        walk.steps.push_back(end);
        DrawStep copies;
        copies.kind = DrawStepKind::Copies;
        copies.drawer = this;
        copies.entity = &be;
        copies.host = walk.add_host(std::move(inner));
        copies.block = b;
        walk.steps.push_back(copies);
    }

    // The copy `s.copy` of an INSERT's array (row by row): its transform is pushed, its block's entities and the end of
    // the copy are steps, and the next copy follows them
    void draw_insert_copy(BlockWalk& walk, const DrawStep& s) {
        const auto& d = s.entity->as_insert();
        const Block& b = *s.block;
        const size_t columns = static_cast<size_t>(std::max(1, d.column_count));
        const size_t total = static_cast<size_t>(std::max(1, d.row_count)) * columns;
        if (s.copy >= total) return;
        if (s.copy + 1 < total) {
            DrawStep next = s;
            next.copy = s.copy + 1;
            walk.steps.push_back(next);
        }
        DrawStep end;
        end.kind = DrawStepKind::CopyEnd;
        end.drawer = this;
        walk.steps.push_back(end);
        for (size_t i = b.entities.size(); i-- > 0;) {
            DrawStep e;
            e.kind = DrawStepKind::Entity;
            e.drawer = this;
            e.entity = &b.entities[i];
            e.host = s.host;
            walk.steps.push_back(e);
        }
        const Point3& n = s.entity->props.extrusion;
        double r = d.rotation * M_PI / 180.0;
        const int row = static_cast<int>(s.copy / columns), column = static_cast<int>(s.copy % columns);
        double ox = column * d.column_spacing, oy = row * d.row_spacing;
        Point3 at = {d.insertion_point.x + std::cos(r) * ox - std::sin(r) * oy,
                     d.insertion_point.y + std::sin(r) * ox + std::cos(r) * oy, d.insertion_point.z};
        push(block_transform(ecs::ecs_to_wcs(at, n), n, r, {d.x_scale, d.y_scale, d.z_scale}, b.base_point));
    }

    // An INSERT's attributes, with the properties of the block they are in (host)
    void draw_insert_attributes(const Entity& be, const BlockHost& host) {
        const auto& d = be.as_insert();
        for (const auto& a : d.attributes) {
            if (a.data.flags & 1) continue;             // invisible
            g_.true_color(drawn_color(a.props, host));
            g_.lineweight(drawn_lineweight(a.props, host));
            const AttribData& t = a.data;
            draw_text_line({t.style, t.value, t.height, t.width_factor, t.oblique_angle, t.rotation,
                            t.generation_flags, t.h_justify, t.v_justify, t.start_point, t.alignment_point},
                           a.props.extrusion, identity(), false);
        }
    }

    // A DIMENSION in the block: its block, as AutoCAD's dimensions draw it (marker 0, the
    // block in its transform)
    void start_dimension(BlockWalk& walk, const Entity& be, const BlockHost& host) {
        const auto& d = be.as_dimension();
        const Block* b = dwg_.find_block(d.block_name);
        if (!b || !walk.enter(b)) return;
        BlockHost inner;
        inner.layer = same_name(be.props.layer, "0") ? host.layer : be.props.layer;
        inner.color = drawn_color(be.props, host);
        inner.lineweight = drawn_lineweight(be.props, host);
        g_.marker(0);
        push(block_transform(d.block_translation, {0.0, 0.0, 1.0}, 0.0, {1.0, 1.0, 1.0}, b->base_point));
        DrawStep end;
        end.kind = DrawStepKind::DimensionEnd;
        end.drawer = this;
        end.block = b;
        walk.steps.push_back(end);
        const BlockHost* inner_host = walk.add_host(std::move(inner));
        for (size_t i = b->entities.size(); i-- > 0;) {
            DrawStep s;
            s.kind = DrawStepKind::Entity;
            s.drawer = this;
            s.entity = &b->entities[i];
            s.host = inner_host;
            walk.steps.push_back(s);
        }
    }

    // A MULTILEADER in a block: drawn by a drawer of its own into this one's stream, in the order the commands come. It
    // shares the walk, and so the blocks being drawn.
    void start_inner(BlockWalk& walk, const Entity& be) {
        auto inner = std::make_shared<MLeaderDrawer>(dwg_, be, tables_, layout_, unicode_, nullptr, identity(), nullptr,
                                                     &g_);
        DrawStep done;
        done.kind = DrawStepKind::InnerEnd;
        done.drawer = this;
        done.keep = inner;
        walk.steps.push_back(std::move(done));
        inner->begin(walk);
    }

    // HATCH in its OCS at its elevation: solid (and gradient) a filled shell of its loops
    // (the first of them the face, the others holes — AcGi's negative count), its edges
    // visible; a pattern its dashes clipped to the loops (odd parity; the outermost loops for
    // style 1, the external ones for 2), each a polyline with its marker 1 … — in Continuous
    void draw_hatch(const Entity& be) {
        const auto& d = be.as_hatch();
        const Point3& n = be.props.extrusion;
        std::vector<std::vector<Point2>> loops;
        for (const auto& path : d.paths) {
            if (d.style == 1 && !(path.flags & (1 | 16))) continue;
            if (d.style == 2 && !(path.flags & 1)) continue;
            std::vector<Point2> loop = hatch_loop(path);
            if (loop.size() >= 3) loops.push_back(loop);
        }
        if (loops.empty()) return;
        auto wcs = [&](const Point2& q) { return ecs::ecs_to_wcs({q.x, q.y, d.elevation}, n); };
        g_.linetype(continuous());
        if (d.solid) {
            std::vector<Point3> points;
            std::vector<int32_t> faces;
            int edges = 0;
            for (size_t i = 0; i < loops.size(); ++i) {
                faces.push_back(static_cast<int32_t>(i == 0 ? loops[i].size() : -static_cast<int>(loops[i].size())));
                for (const auto& q : loops[i]) {
                    faces.push_back(static_cast<int32_t>(points.size()));
                    points.push_back(wcs(q));
                    ++edges;
                }
            }
            std::vector<int32_t> traits = {0x40};
            for (int k = 0; k < edges; ++k) traits.push_back(1);
            traits.push_back(0);
            traits.push_back(0);
            g_.fill(true);
            g_.shell(points, faces, traits);
            return;
        }
        int marker = 0;
        for (const auto& line : d.pattern_lines) {
            double a = line.angle * M_PI / 180.0;
            Point2 u = {std::cos(a), std::sin(a)};
            Point2 v = {-u.y, u.x};
            double spacing = line.offset.x * v.x + line.offset.y * v.y;   // across the lines
            double shift = line.offset.x * u.x + line.offset.y * u.y;     // along them
            if (std::fabs(spacing) < 1e-12) continue;
            // The loops' extent across the lines
            double lo = 1e300, hi = -1e300;
            for (const auto& loop : loops) {
                for (const auto& q : loop) {
                    double w = (q.x - line.base_point.x) * v.x + (q.y - line.base_point.y) * v.y;
                    lo = std::min(lo, w);
                    hi = std::max(hi, w);
                }
            }
            long k0 = static_cast<long>(std::floor(std::min(lo, hi) / spacing)) - 1;
            long k1 = static_cast<long>(std::ceil(std::max(lo, hi) / spacing)) + 1;
            if (k0 > k1) std::swap(k0, k1);
            for (long k = k0; k <= k1; ++k) {
                Point2 origin = {line.base_point.x + line.offset.x * k, line.base_point.y + line.offset.y * k};
                (void)shift;
                // Where the line crosses the loops, along it from origin
                std::vector<double> cuts;
                for (const auto& loop : loops) {
                    for (size_t i = 0; i < loop.size(); ++i) {
                        const Point2& p0 = loop[i];
                        const Point2& p1 = loop[(i + 1) % loop.size()];
                        double w0 = (p0.x - origin.x) * v.x + (p0.y - origin.y) * v.y;
                        double w1 = (p1.x - origin.x) * v.x + (p1.y - origin.y) * v.y;
                        if ((w0 > 0.0) == (w1 > 0.0)) continue;
                        double t = w0 / (w0 - w1);
                        Point2 x = {p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t};
                        cuts.push_back((x.x - origin.x) * u.x + (x.y - origin.y) * u.y);
                    }
                }
                std::sort(cuts.begin(), cuts.end());
                for (size_t i = 0; i + 1 < cuts.size(); i += 2) {
                    for (const auto& [s0, s1] : dashes(line.dashes, cuts[i], cuts[i + 1])) {
                        g_.marker(++marker);
                        polyline({wcs({origin.x + u.x * s0, origin.y + u.y * s0}),
                                  wcs({origin.x + u.x * s1, origin.y + u.y * s1})});
                    }
                }
            }
        }
    }

    // The dashes of a pattern line between a and b along it (its pattern repeated from the
    // line's origin): a dash its piece, a dot (0) a piece of no length, a gap nothing
    static std::vector<std::pair<double, double>> dashes(const std::vector<double>& pattern, double a, double b) {
        std::vector<std::pair<double, double>> out;
        if (pattern.empty()) {
            out.push_back({a, b});
            return out;
        }
        double period = 0.0;
        for (double x : pattern) period += std::fabs(x);
        if (period <= 0.0) {
            out.push_back({a, b});
            return out;
        }
        double start = std::floor(a / period) * period;
        for (double at = start; at < b; ) {
            for (double x : pattern) {
                double next = at + std::fabs(x);
                if (x >= 0.0) {
                    double s0 = std::max(at, a), s1 = std::min(next, b);
                    if (x == 0.0 ? (at >= a && at <= b) : (s1 > s0)) out.push_back({s0, x == 0.0 ? s0 : s1});
                }
                at = next;
                if (at >= b) break;
            }
        }
        return out;
    }

    // A hatch path as points in the OCS: a polyline's vertices with its bulges' arcs, the
    // edges' lines, arcs, ellipses and splines — curves 64 segments a turn, splines 8 a
    // knot span
    std::vector<Point2> hatch_loop(const HatchBoundaryPath& path) const {
        std::vector<Point2> out;
        // From angle a0 to a1 (a1 > a0) about c: c + major cos t ± minor sin t, the minor
        // direction turned for a clockwise edge (as the extents count it)
        auto arc = [&](const Point2& c, const Point2& major, double ratio, double a0, double a1, bool ccw) {
            int segments = std::max(1, static_cast<int>(std::ceil((a1 - a0) / (2.0 * M_PI / 64.0) - 1e-9)));
            double turn = ccw ? 1.0 : -1.0;
            Point2 minor = {-major.y * ratio * turn, major.x * ratio * turn};
            for (int i = 0; i <= segments; ++i) {
                double t = a0 + (a1 - a0) * i / segments;
                out.push_back({c.x + major.x * std::cos(t) + minor.x * std::sin(t),
                               c.y + major.y * std::cos(t) + minor.y * std::sin(t)});
            }
        };
        if (path.is_polyline()) {
            const auto& v = path.vertices;
            for (size_t i = 0; i < v.size(); ++i) {
                out.push_back(v[i].position);
                bool last = i + 1 == v.size();
                if (v[i].bulge == 0.0 || (last && !path.closed)) continue;
                const Point2& a = v[i].position;
                const Point2& b = v[last ? 0 : i + 1].position;
                double dx = b.x - a.x, dy = b.y - a.y, chord = std::sqrt(dx * dx + dy * dy);
                if (chord == 0.0) continue;
                double theta = 4.0 * std::atan(v[i].bulge);
                double r = chord / (2.0 * std::sin(theta / 2.0));
                double h = r * std::cos(theta / 2.0);
                Point2 c = {(a.x + b.x) / 2.0 - dy / chord * h, (a.y + b.y) / 2.0 + dx / chord * h};
                double a0 = std::atan2(a.y - c.y, a.x - c.x);
                int segments = std::max(1, static_cast<int>(std::ceil(std::fabs(theta) / (2.0 * M_PI / 64.0) - 1e-9)));
                for (int k = 1; k < segments; ++k) {
                    double t = a0 + theta * k / segments;
                    out.push_back({c.x + std::fabs(r) * std::cos(t), c.y + std::fabs(r) * std::sin(t)});
                }
            }
            return out;
        }
        for (const auto& e : path.edges) {
            std::vector<Point2> piece;
            switch (e.type) {
                case HatchEdgeType::Line:
                    piece = {e.start, e.end};
                    break;
                case HatchEdgeType::Arc:
                case HatchEdgeType::Ellipse: {
                    // Angles counterclockwise for 73 = 1, else clockwise (the minor direction turned)
                    bool is_arc = e.type == HatchEdgeType::Arc;
                    Point2 major = is_arc ? Point2{e.radius, 0.0} : e.major_axis;
                    double ratio = is_arc ? 1.0 : e.ratio;
                    double a0 = e.start_angle * M_PI / 180.0, a1 = e.end_angle * M_PI / 180.0;
                    if (a1 < a0) a1 += 2.0 * M_PI;
                    std::vector<Point2> saved;
                    saved.swap(out);
                    arc(e.center, major, ratio, a0, a1, e.counterclockwise);
                    piece.swap(out);
                    out.swap(saved);
                    break;
                }
                case HatchEdgeType::Spline: {
                    SplineData s;
                    s.degree = e.degree;
                    s.knots = e.knots;
                    for (const auto& q : e.control_points) s.control_points.push_back({q.x, q.y, 0.0});
                    if (e.rational) {
                        s.weights = e.weights;
                        s.flags |= 4;
                    }
                    for (const auto& f : e.fit_points) s.fit_points.push_back({f.x, f.y, 0.0});
                    s.start_tangent = {e.start_tangent.x, e.start_tangent.y, 0.0};
                    s.end_tangent = {e.end_tangent.x, e.end_tangent.y, 0.0};
                    for (const auto& q : spline_points(s)) piece.push_back({q.x, q.y});
                    break;
                }
            }
            for (const auto& q : piece) {
                if (!out.empty() && std::fabs(out.back().x - q.x) < 1e-12 && std::fabs(out.back().y - q.y) < 1e-12) continue;
                out.push_back(q);
            }
        }
        if (out.size() > 1 && std::fabs(out.back().x - out.front().x) < 1e-12 &&
            std::fabs(out.back().y - out.front().y) < 1e-12) {
            out.pop_back();
        }
        return out;
    }

    // MLINE: each element's line through the vertices moved along their miters, its marker
    // the element's number from 1 (ODA's), closed back to the first
    void draw_mline(const Entity& be) {
        const auto& d = be.as_mline();
        size_t elements = 0;
        for (const auto& v : d.vertices) elements = std::max(elements, v.elements.size());
        for (size_t k = 0; k < elements; ++k) {
            std::vector<Point3> points;
            for (const auto& v : d.vertices) {
                double a = k < v.elements.size() && !v.elements[k].params.empty() ? v.elements[k].params[0] : 0.0;
                points.push_back(v.position + v.miter * a);
            }
            if (d.is_closed() && !points.empty()) points.push_back(points.front());
            g_.marker(static_cast<int>(k) + 1);
            if (points.size() >= 2) polyline_normal(points, be.props.extrusion);
        }
    }

    // TOLERANCE: per row its frames' texts (each frame an MTEXT in the dimension style's
    // text style and height, from the frame's corner and the gap), then its lines — the
    // left side, each frame's right side, the top, the bottom — as ODA draws it
    void draw_tolerance(const Entity& be) {
        const auto& d = be.as_tolerance();
        DimStyle style;
        for (const auto& ds : dwg_.dim_styles()) {
            if (same_name(ds.name, d.dim_style)) { style = ds; break; }
        }
        double scale = style.dimscale > 0.0 ? style.dimscale : 1.0;
        double h = style.dimtxt * scale, gap = std::fabs(style.dimgap) * scale;
        double row = h + 2.0 * gap;
        std::string text_style = style.dimtxsty.empty() ? std::string("Standard") : style.dimtxsty;
        Point3 x = unit(d.x_axis_direction);
        Point3 y = unit(cross(be.props.extrusion, x));
        auto at = [&](double u, double v) { return d.insertion_point + x * u + y * v; };
        std::vector<std::string> rows(1);
        for (char ch : d.content) {
            if (ch == '\n') rows.emplace_back();
            else rows.back() += ch;
        }
        for (size_t r = 0; r < rows.size(); ++r) {
            std::vector<std::string> cells(1);
            const std::string& s = rows[r];
            for (size_t i = 0; i < s.size();) {
                if (s.compare(i, 3, "%%v") == 0 || s.compare(i, 3, "%%V") == 0) {
                    if (!cells.back().empty()) cells.emplace_back();
                    i += 3;
                    continue;
                }
                cells.back() += s[i++];
            }
            double top = row / 2.0 - static_cast<double>(r) * row;
            double left = 0.0;
            std::vector<double> rights;
            for (const auto& cell : cells) {
                if (cell.empty()) continue;
                MTextData m;
                m.content = cell;
                m.height = h;
                m.style = text_style;
                m.attachment = 1;
                m.x_axis_direction = x;
                m.insertion_point = at(left + gap, top - gap);
                MTextSize size = layout_.mtext(m, true);
                if (!size.ink) continue;
                draw_runs(size, m.insertion_point, x, be.props.extrusion, CmColor::by_layer().raw);
                left += size.width + 2.0 * gap;
                rights.push_back(left);
            }
            if (rights.empty()) continue;
            polyline({at(0.0, top - row), at(0.0, top)});
            for (double xr : rights) polyline({at(xr, top), at(xr, top - row)});
            polyline({at(0.0, top), at(rights.back(), top)});
            polyline({at(rights.back(), top - row), at(0.0, top - row)});
        }
    }

    // SHAPE: its shape as a character of its shape file, in the shape's size, rotation, width
    // and slant
    void draw_shape(const Entity& be) {
        const auto& d = be.as_shape();
        for (const auto& ts : dwg_.text_styles()) {
            if (!(ts.flags & 1)) continue;
            std::shared_ptr<const Font> f = find_font(ts.font_file);
            uint32_t number = f ? f->shape_number(d.name) : 0;
            if (!number) continue;
            const Point3& n = be.props.extrusion;
            Point3 ax, ay;
            ecs::arbitrary_axis(n, ax, ay);
            double r = d.rotation * M_PI / 180.0;
            ProxyText t;
            t.position = ecs::ecs_to_wcs(d.insertion_point, n);
            t.normal = unit(n);
            t.direction = ax * std::cos(r) + ay * std::sin(r);
            append_utf8(t.text, static_cast<char32_t>(number));
            t.height = d.size;
            t.width_factor = d.x_scale != 0.0 ? d.x_scale : 1.0;
            t.oblique = d.oblique_angle * M_PI / 180.0;
            t.font = ts.font_file;
            if (t.font.find('.') == std::string::npos) t.font += ".shx";
            g_.text(t, unicode_, code_page_);
            return;
        }
    }

    // IMAGE: its clip boundary (or its frame) closed, in the WCS — as its own proxy graphics
    void draw_image(const ImageData& d) {
        polyline(image_outline(d));
    }

    void polyline_normal(const std::vector<Point3>& points, const Point3& normal) {
        g_.polyline_normal(points, normal);
        for (const auto& p : points) add(p);
    }

    // --- leaders ----------------------------------------------------------------------

    // An arrowhead's block (the drawing's, else AutoCAD's built-in one of that name): one unit
    // long with its tip at the origin along +X, placed at the tip pointing away from the line,
    // scaled by the arrow size; its ByBlock parts in its line's color and lineweight (the
    // leader's, or the line's own, B47; no AutoCAD file has one — a checkpoint)
    // The block's transform is pushed here and its entities are steps of the walk; the step ArrowEnd pops it and goes on
    // with the line. False when nothing is drawn (no such block, or the block is an arrowhead being drawn already).
    bool start_arrow_block(BlockWalk& walk, const std::string& name, const Point3& tip, const Point3& dir, double size,
                           int32_t color, int lineweight) {
        const Block* b = dwg_.find_block(name);
        if (!b) {
            if (!is_builtin_arrow(name)) return false;
            walk.blocks.push_back(builtin_arrow_block(name));
            b = &walk.blocks.back();
        }
        if (!walk.arrows.insert(b).second) return false;
        Point3 normal = unit(cross(c_.plane_x_axis, c_.plane_y_axis));
        Point3 ax, ay;
        ecs::arbitrary_axis(normal, ax, ay);
        Point3 out = dir * -1.0;
        double rotation = std::atan2(dot(out, ay), dot(out, ax));
        Matrix m = block_transform(tip, normal, rotation, {size, size, size}, b->base_point);
        BlockHost host;
        host.layer = e_.props.layer;
        host.color = color;
        host.lineweight = lineweight;
        const BlockHost* arrow_host = walk.add_host(std::move(host));
        push(m);
        DrawStep end;
        end.kind = DrawStepKind::ArrowEnd;
        end.drawer = this;
        end.block = b;
        walk.steps.push_back(end);
        for (size_t i = b->entities.size(); i-- > 0;) {
            DrawStep s;
            s.kind = DrawStepKind::Entity;
            s.drawer = this;
            s.entity = &b->entities[i];
            s.host = arrow_host;
            s.top = true;
            walk.steps.push_back(s);
        }
        return true;
    }

    std::string arrow_of(int leader) const {
        for (const auto& a : d_.arrowheads) {
            if (a.index == leader) return a.block;
        }
        return d_.arrow_block;
    }

    // A leader line's type: its own where its override flags say so (2010, B47), else the
    // multileader's
    int line_type(const MLeaderLine& line) const {
        if ((line.override_flags.value_or(0) & 1) && line.leader_type) return *line.leader_type;
        return d_.leader_type;
    }

    bool has_visible_line(const MLeaderLeader& leader) const {
        for (const auto& line : leader.lines) {
            if (line_type(line) != 0) return true;
        }
        return false;
    }

    bool has_visible_line() const {
        for (const auto& leader : c_.leaders) {
            if (has_visible_line(leader)) return true;
        }
        return false;
    }

    // The leaders from the leader and line the drawer is at (leader_at_, line_at_), then the rest of the drawing. An
    // arrowhead's block is steps of the walk, so a line stops there; the step ArrowEnd goes on from the same line.
    void draw_leaders(BlockWalk& walk) {
        while (leader_at_ < c_.leaders.size()) {
            const MLeaderLeader& leader = c_.leaders[leader_at_];
            while (line_at_ < leader.lines.size()) {
                const MLeaderLine& line = leader.lines[line_at_];
                // 2010: the line's own properties where its override flags say so (B47; no
                // AutoCAD file has a line with overrides — the commands around the line are a
                // checkpoint)
                if (line_type(line) != 0) {
                    if (begin_line(walk, leader, line)) return;
                    end_line(line);
                }
                ++line_at_;
            }
            // The landing: its marker when it has a length, the line when landings are on; a
            // leader attached vertically (2010) has none, nor one of invisible leaders whose lines
            // are all invisible (a line visible by its own type, B47, keeps its leader's)
            if (leader.dogleg_length > 0.0 && leader.attachment_direction != 1 &&
                (d_.leader_type != 0 || has_visible_line(leader))) {
                g_.marker(kLandingMarker + leader.branch_index);
                if (d_.dogleg && leader.has_dogleg) {
                    std::vector<SegmentGap> gaps;
                    for (const auto& gap : leader.breaks) gaps.push_back({0, gap});
                    broken_polyline({leader.last_point,
                                     leader.last_point + unit(leader.dogleg_direction) * leader.dogleg_length},
                                    gaps);
                }
            }
            ++leader_at_;
            line_at_ = 0;
        }
        finish_after_leaders();
    }

    // A leader line up to and including its arrowhead. True when the arrowhead is a block whose entities are steps of the
    // walk: the line goes on in the step ArrowEnd.
    bool begin_line(BlockWalk& walk, const MLeaderLeader& leader, const MLeaderLine& line) {
        const int flags = line.override_flags.value_or(0);
        LineInProgress& l = line_;
        l = LineInProgress();
        l.own_color = (flags & 2) && line.color;
        l.own_linetype = (flags & 4) && line.linetype;
        l.own_weight = (flags & 8) && line.lineweight;
        // What the line is drawn in: its arrowhead block's ByBlock parts too
        l.color = l.own_color ? resolved(*line.color) : leader_color();
        l.linetype = !l.own_linetype ? leader_linetype()
                   : line.linetype->empty() || same_name(*line.linetype, "ByBlock")
                       ? entity_linetype()
                       : linetype_index(*line.linetype);
        l.weight = l.own_weight ? resolved_lineweight(*line.lineweight, e_.props.layer)
                                : resolved_lineweight(d_.leader_lineweight, "0");
        if (l.own_color) g_.true_color(l.color);
        if (l.own_linetype) g_.linetype(l.linetype);
        if (l.own_weight) g_.lineweight(l.weight);
        const std::string arrow = (flags & 32) && line.arrow_block ? *line.arrow_block : arrow_of(static_cast<int>(leader_at_));
        const double arrow_size = (flags & 16) && line.arrow_size ? *line.arrow_size : c_.arrow_size;
        g_.marker(line.index + 1);
        l.points = line.vertices;
        Point3 end;
        if (block_leader_end(l.points, end)) l.points.push_back(end);
        else if (leader.has_last_point) l.points.push_back(leader.last_point);
        if (l.points.size() >= 2) {
            const Point3 tip = l.points[0];
            const Point3 dir = unit(l.points[1] - l.points[0]);
            const double size = arrow_size;
            const bool filled = arrow.empty() || same_name(arrow, "_ClosedFilled");
            // The arrowhead only on a segment at least twice its size (AutoCAD's: none
            // at 1.82 sizes, drawn at 2.01 — the files of test/data/README.md)
            if (length(l.points[1] - l.points[0]) >= 2.0 * size && !same_name(arrow, "_None")) {
                l.trims = arrow_trims(arrow);
                l.trim_to = tip + dir * size;
                if (filled) {
                    Point3 normal = unit(cross(c_.plane_x_axis, c_.plane_y_axis));
                    Point3 side = unit(cross(dir, normal));
                    Point3 base = tip + dir * size;
                    g_.fill(true);
                    polygon({base + side * (size / 6.0), tip, base - side * (size / 6.0)});
                    g_.fill(true);
                    if (l.trims) l.points[0] = l.trim_to;
                } else {
                    if (start_arrow_block(walk, arrow, tip, dir, size, l.color, l.weight)) return true;
                    after_arrow_block();
                }
            }
        }
        return false;
    }

    // After an arrowhead's block: the line's own traits again (the block's entities set theirs), and the line starts
    // where the arrowhead ends
    void after_arrow_block() {
        g_.true_color(line_.color);
        g_.linetype(line_.linetype);
        g_.lineweight(line_.weight);
        if (line_.trims) line_.points[0] = line_.trim_to;
    }

    // A leader line after its arrowhead: the line itself, and the traits it set put back
    void end_line(const MLeaderLine& line) {
        const LineInProgress& l = line_;
        g_.marker(kLineMarker + line.index);
        std::vector<SegmentGap> gaps;
        for (const auto& segment : line.breaks) {
            for (const auto& gap : segment.gaps) gaps.push_back({segment.segment, gap});
        }
        broken_polyline(l.points, gaps);
        if (l.own_color) g_.true_color(leader_color());
        if (l.own_linetype) g_.linetype(leader_linetype());
        if (l.own_weight) g_.lineweight(e_.props.lineweight);
    }

    // The drawing after the leaders: the underline and the frame
    void finish_after_leaders() {
        const int32_t color = entity_color();
        g_.true_color(color);
        g_.linetype(entity_linetype());
        g_.lineweight(0);
        g_.marker(kAfterLeadersMarker);
        draw_underline();
        g_.true_color(color);
        g_.linetype(entity_linetype());
        g_.lineweight(e_.props.lineweight);
        g_.command_51();
        if (d_.text_frame && c_.has_text) draw_frame();
    }

    // A line through `points` with its breaks (DIMBREAK's gaps): a gap on segment s (from
    // points[s] to points[s + 1]; a leader line's from its arrowhead, the landing's 0) from its
    // start to its end point, both on the segment as the DXF reference defines them. The line
    // is drawn in its pieces between the gaps, a polyline each; without gaps one polyline.
    void broken_polyline(const std::vector<Point3>& points, const std::vector<SegmentGap>& gaps) {
        if (points.size() < 2) return;
        std::vector<Point3> piece = {points.front()};
        auto flush = [&]() {
            if (piece.size() >= 2) polyline(piece);
            piece.clear();
        };
        for (size_t s = 0; s + 1 < points.size(); ++s) {
            Point3 a = points[s], b = points[s + 1];
            Point3 ab = b - a;
            double len2 = dot(ab, ab);
            // The segment's gaps as parameters along it (0 at its start, 1 at its end), in order
            std::vector<std::pair<double, double>> cuts;
            for (const auto& g : gaps) {
                if (g.segment != static_cast<int>(s) || len2 <= 0.0) continue;
                double t0 = std::clamp(dot(g.gap.start - a, ab) / len2, 0.0, 1.0);
                double t1 = std::clamp(dot(g.gap.end - a, ab) / len2, 0.0, 1.0);
                if (t1 < t0) std::swap(t0, t1);
                if (t1 > t0) cuts.push_back({t0, t1});
            }
            std::sort(cuts.begin(), cuts.end());
            double at = 0.0;
            for (const auto& [t0, t1] : cuts) {
                if (t1 <= at) continue;
                if (t0 > at) piece.push_back(a + ab * t0);
                flush();
                at = std::max(at, t1);
                piece.push_back(a + ab * at);
            }
            if (at < 1.0) piece.push_back(b);           // else a gap to its end: the piece starts at b
        }
        flush();
    }

    // A block without landings: the leader goes from its last vertex toward the block's
    // connection point (176: 1 its insertion point, 0 the middle of its extents) and stops
    // where it first meets the block's lines and arcs (the balloons of AutoCAD in the files
    // of test/data/README.md: at the circle)
    bool block_leader_end(const std::vector<Point3>& vertices, Point3& end) const {
        if (!block_ || d_.dogleg || vertices.empty()) return false;
        Point3 target = c_.block_position;
        if (d_.block_connection == 0) {
            Extents ext;
            for (const auto& be : block_->entities) {
                if (be.type != EntityType::Attdef) entity_extents(dwg_, be, ext);
            }
            if (!ext.empty()) target = transform_point(block_matrix_, (ext.min + ext.max) * 0.5);
        }
        const Point3& from = vertices.back();
        end = target;
        // In the block's coordinates
        Matrix inv = inverse(block_matrix_);
        Point3 a = transform_point(inv, from), b = transform_point(inv, target);
        double dx = b.x - a.x, dy = b.y - a.y;
        double best = 2.0;
        auto take = [&](double t) {
            if (t >= 0.0 && t <= 1.0 && t < best) best = t;
        };
        auto segment = [&](double x0, double y0, double x1, double y1) {
            double ex = x1 - x0, ey = y1 - y0;
            double den = dx * ey - dy * ex;
            if (std::fabs(den) < 1e-300) return;
            double t = ((x0 - a.x) * ey - (y0 - a.y) * ex) / den;
            double u = ((x0 - a.x) * dy - (y0 - a.y) * dx) / den;
            if (u >= 0.0 && u <= 1.0) take(t);
        };
        // An arc from a0 counterclockwise to a1 (radians), or a full circle
        auto arc = [&](double cx, double cy, double r, double a0, double a1, bool full) {
            double fx = a.x - cx, fy = a.y - cy;
            double qa = dx * dx + dy * dy, qb = 2.0 * (fx * dx + fy * dy), qc = fx * fx + fy * fy - r * r;
            double disc = qb * qb - 4.0 * qa * qc;
            if (qa <= 0.0 || disc < 0.0) return;
            for (double sign : {-1.0, 1.0}) {
                double t = (-qb + sign * std::sqrt(disc)) / (2.0 * qa);
                if (!full) {
                    double angle = std::atan2(a.y + t * dy - cy, a.x + t * dx - cx);
                    double sweep = std::fmod(a1 - a0 + 4.0 * M_PI, 2.0 * M_PI);
                    double at = std::fmod(angle - a0 + 4.0 * M_PI, 2.0 * M_PI);
                    if (at > sweep) continue;
                }
                take(t);
            }
        };
        for (const auto& be : block_->entities) {
            switch (be.type) {
                case EntityType::Line: {
                    const auto& l = be.data.get<LineData>();
                    segment(l.start.x, l.start.y, l.end.x, l.end.y);
                    break;
                }
                case EntityType::Circle: {
                    const auto& ci = be.data.get<CircleData>();
                    arc(ci.center.x, ci.center.y, ci.radius, 0.0, 0.0, true);
                    break;
                }
                case EntityType::Arc: {
                    const auto& ar = be.data.get<ArcData>();
                    arc(ar.center.x, ar.center.y, ar.radius, ar.start_angle * M_PI / 180.0,
                        ar.end_angle * M_PI / 180.0, false);
                    break;
                }
                case EntityType::LwPolyline: {
                    const auto& pl = be.data.get<LwPolylineData>();
                    size_t n = pl.vertices.size();
                    size_t count = pl.is_closed() ? n : (n > 0 ? n - 1 : 0);
                    for (size_t i = 0; i < count; ++i) {
                        const Point2& p0 = pl.vertices[i].position;
                        const Point2& p1 = pl.vertices[(i + 1) % n].position;
                        double bulge = pl.vertices[i].bulge;
                        if (bulge == 0.0) {
                            segment(p0.x, p0.y, p1.x, p1.y);
                            continue;
                        }
                        // The bulge's arc: the center on the chord's normal, left of it for a
                        // positive bulge (counterclockwise from p0 to p1)
                        double chord = std::hypot(p1.x - p0.x, p1.y - p0.y);
                        if (chord <= 0.0) continue;
                        double theta = 4.0 * std::atan(bulge);
                        double r = chord / (2.0 * std::sin(theta / 2.0));
                        double offset = r * std::cos(theta / 2.0);
                        double nx = -(p1.y - p0.y) / chord, ny = (p1.x - p0.x) / chord;
                        double cx = (p0.x + p1.x) / 2.0 + nx * offset, cy = (p0.y + p1.y) / 2.0 + ny * offset;
                        double s0 = std::atan2(p0.y - cy, p0.x - cx), s1 = std::atan2(p1.y - cy, p1.x - cx);
                        if (bulge > 0.0) arc(cx, cy, std::fabs(r), s0, s1, false);
                        else arc(cx, cy, std::fabs(r), s1, s0, false);
                    }
                    break;
                }
                default:
                    break;
            }
        }
        if (best <= 1.0) end = from + (target - from) * best;
        return true;
    }

    // --- underline and frame ----------------------------------------------------------

    // The text's underline (attachment 5 underline bottom line, 6 underline top line, 8
    // underline all text — the left attachment for a leader on the content's left, the
    // right one for a leader on its right): on the landing's line, from the landing's end to
    // the text's far side, left to right along the text
    void draw_underline() {
        if (!c_.has_text || c_.leaders.empty()) return;
        const MLeaderLeader& leader = c_.leaders.front();
        Point3 x = unit(c_.text_direction);
        bool from_left = dot(leader.dogleg_direction, x) >= 0.0;
        int attachment = from_left ? c_.left_attachment : c_.right_attachment;
        if (attachment != 5 && attachment != 6 && attachment != 8) return;
        Point3 landing_end = leader.last_point + unit(leader.dogleg_direction) * leader.dogleg_length;
        // No text: its insertion point
        double far = !text_size_.lines.valid ? 0.0 : from_left ? text_size_.lines.max_x : text_size_.lines.min_x;
        Point3 end = landing_end + x * dot(c_.text_location + x * far - landing_end, x);
        if (from_left) polyline({landing_end, end});
        else polyline({end, landing_end});
    }

    void draw_frame() {
        g_.command_51();
        g_.layer(layer_index(e_.props.layer));
        g_.true_color(entity_color());
        const TextBox& b = text_size_.lines;
        double gap = c_.landing_gap;
        Point3 x = unit(c_.text_direction);
        Point3 y = unit(cross(c_.text_normal, x));
        auto at = [&](double u, double v) { return c_.text_location + x * u + y * v; };
        polyline({at(b.min_x - gap, b.max_y + gap), at(b.max_x + gap, b.max_y + gap),
                     at(b.max_x + gap, b.min_y - gap), at(b.min_x - gap, b.min_y - gap),
                     at(b.min_x - gap, b.max_y + gap)});
        g_.color(256);
        g_.lineweight(-1);
        g_.command_51();
    }

    const Drawing& dwg_;
    const Entity& e_;
    const MLeaderData& d_;
    const MLeaderContext& c_;
    const ProxyTables& tables_;
    TextLayout& layout_;
    bool unicode_;
    int code_page_;
    Extents* ext_ = nullptr;
    BlockEntityExtents block_entity_;   // the extents of a block's entities
    std::unique_ptr<AcisSource> acis_;
    std::vector<Matrix> stack_;         // the transforms pushed, multiplied
    ProxyGraphics own_graphics_;
    ProxyGraphics& g_;                  // what is drawn goes here: own_graphics_, or the stream of the drawer this one is in
    MTextSize text_size_;
    const Block* block_ = nullptr;
    Matrix block_matrix_{};
    size_t leader_at_ = 0;              // the leader and its line being drawn
    size_t line_at_ = 0;
    LineInProgress line_;               // that line, from its start to the end of its arrowhead
};

// Does the steps, the last first; a step may add steps that are done before the ones under it
void BlockWalk::run() {
    while (!steps.empty()) {
        DrawStep s = std::move(steps.back());
        steps.pop_back();
        s.drawer->step(*this, s);
    }
}

} // namespace

std::vector<uint8_t> mleader_graphics(const Drawing& dwg, const Entity& e, const ProxyTables& tables,
                                      TextLayout& layout, bool unicode) {
    MLeaderDrawer drawer(dwg, e, tables, layout, unicode, nullptr);
    return drawer.draw();
}

void mleader_extents(const Drawing& dwg, const Entity& e, TextLayout& layout, const std::array<double, 16>& map,
                     const std::function<void(const Entity&, const std::array<double, 16>&)>& block_entity,
                     Extents& out) {
    ProxyTables none;
    MLeaderDrawer drawer(dwg, e, none, layout, true, &out, map, block_entity);
    drawer.draw();
}

} // namespace internal
} // namespace hdxf
