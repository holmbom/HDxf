#include "TableLayout.hpp"
#include "extents.hpp"
#include "handles.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// The layout of a table (phase 9, step 9.5). What the rules are made of, and where AutoCAD's own files show them:
//
//   - a text content is its MTEXT laid out at the width of the cell less its margins: its box is the MTEXT's width
//     + 0.1 and its height (TABLEGEOMETRY of acad_2018_dxf_tables.dxf and ACadSharp's 528 and A35: every one of
//     the 90 text cells), a cell is that box and its margins (an empty cell: only the margins), a row is as high as
//     it was and as its highest cell needs, a column keeps its width (text wraps);
//   - the contents of a cell flow left to right with 0.18 between them and wrap to a new line when the next does not
//     fit the cell less its margins (the cells with two and three contents of both files; 13.34 high is 1 + 3.0634 +
//     0.18 + 8.1003 + 1);
//   - the alignment (1 … 9) places the contents' box in the cell, and the content's anchor on it: the MTEXT's own
//     attachment point, a block's insertion point;
//   - a block cell draws its block at its scale, or — auto-fit — as large as the cell less its margins allows;
//   - the *T block has the fills, the text, the blocks, the horizontal and then the vertical borders (each border once
//     for the edge two cells share, none inside a merged range, equal neighbours as one line), and the Defpoints marker.

namespace hdxf {
namespace internal {

namespace {

uint64_t handle_number(const std::string& h) {
    uint64_t v = 0;
    return parse_handle(h, v) == HandleParse::Ok ? v : 0;
}

const Block* block_of_record(const Drawing& dwg, const std::string& record_handle) {
    const uint64_t want = handle_number(record_handle);
    if (want == 0) return nullptr;
    for (const Block& b : dwg.blocks()) {
        if (handle_number(b.record.handle) == want) return &b;
    }
    return nullptr;
}

// A block's box: the extents of what it draws (an attribute definition draws nothing in the block it is defined in)
struct Box {
    bool valid = false;
    double min_x = 0.0, min_y = 0.0, max_x = 0.0, max_y = 0.0;
};

Box block_box(const Drawing& dwg, const Block& block) {
    Extents e;
    for (const Entity& be : block.entities) {
        if (be.type == EntityType::Attdef && !(be.as_attdef().flags & 2)) continue;
        entity_extents(dwg, be, e);
    }
    Box b;
    if (e.empty()) return b;
    b.valid = true;
    b.min_x = e.min.x;
    b.min_y = e.min.y;
    b.max_x = e.max.x;
    b.max_y = e.max.y;
    return b;
}

// AutoCAD's Standard table style, for a table whose style is not there
TableStyleData fallback_style() {
    TableStyleData s;
    s.horizontal_margin = 0.06;
    s.vertical_margin = 0.06;
    s.rows[0].text_height = 0.18;
    s.rows[0].alignment = 2;
    s.rows[1].text_height = 0.25;
    s.rows[1].alignment = 5;
    s.rows[2].text_height = 0.18;
    s.rows[2].alignment = 5;
    return s;
}

struct Border {
    int lineweight = -2;
    int color = 0;
    bool visible = true;
    bool operator==(const Border& o) const {
        return lineweight == o.lineweight && color == o.color && visible == o.visible;
    }
};

// One thing a cell shows: a text (an MTEXT), a block (an INSERT), or an empty text that still has a height
struct Item {
    bool is_block = false;
    bool empty = false;
    bool drop = false;                  // an empty text that is no content
    bool field = false;
    // text
    std::string text;
    std::string style;
    double height = 0.0;
    Color color;
    int alignment = 1;
    double rotation = 0.0;              // radians
    MTextSize size;
    double text_w = 0.0, text_h = 0.0;  // the MTEXT's own box + the pad, before rotation
    // block
    const Block* block = nullptr;
    std::string block_name;
    double scale = 1.0;
    Box box;
    std::vector<TableAttributeValue> attributes;
    // the box the item takes in the flow: its width and height after rotation
    double w = 0.0, h = 0.0;
    // placed: top-left of the box in the cell (x right, y up, 0 at the cell's top-left, so y <= 0), the anchor
    double px = 0.0, py = 0.0;
    double ax = 0.0, ay = 0.0;
};

// A cell's margins and the space between its contents: its cell style's (the CELLSTYLEMAP entry the cell uses), or
// the table's overrides
struct Margins {
    double left = 0.0, right = 0.0, top = 0.0, bottom = 0.0;
    double gap_h = kTableContentSpacing, gap_v = kTableContentSpacing;
    double horizontal() const { return left + right; }
    double vertical() const { return top + bottom; }
};

struct Resolved {
    Margins m;
    std::string style;
    double height = 0.0;
    bool height_override = false;
    int alignment = 1;
    Color color;
    std::optional<Color> fill;
    double rotation = 0.0;
    int row_type = 0;
};

struct Line {
    std::vector<size_t> items;
    double width = 0.0;
    double height = 0.0;
};

struct CellInfo {
    bool owner = true;
    size_t owner_index = 0;
    size_t span_rows = 1, span_columns = 1;
    Resolved res;
    bool multi = false;                 // contents from TABLECONTENT, in an anonymous block
    std::string content_block;          // its name
    std::vector<Item> items;
    std::vector<Line> lines;
    double flow_height = 0.0;           // the lines and the spaces between them
    double cell_w = 0.0, cell_h = 0.0;  // TABLEGEOMETRY's w and h
    bool auto_block = false;            // an auto-fit block: sized after the rows are
};

class Engine {
public:
    Engine(const Drawing& dwg, const Entity& table, const TableContentData* content, TextLayout& text, TableLayout& out)
        : dwg_(dwg), table_(table), t_(table.as_table()), content_(content), text_(text), out_(out) {}

    bool run();

private:
    const TableStyleRow& style_row(size_t r) const { return style_->rows[row_type_[r]]; }
    Resolved resolve(size_t r, size_t c) const;
    double span_width(size_t r, size_t c, size_t columns) const;
    double span_height(size_t r, size_t rows) const;

    Item text_item(const std::string& text, const Resolved& rs, double wrap, bool keep_empty);
    MTextSize measure_mtext(const Item& it, double wrap);
    void remeasure_text(Item& it, double wrap);
    double measure_text_minimum(const Item& it);
    Item block_item(const Block* block, double scale, const std::vector<TableAttributeValue>& attrs, bool auto_fit);
    void measure_cell(size_t r, size_t c, CellInfo& cell);
    void break_lines(CellInfo& cell, double avail);
    void place_cell(size_t r, size_t c, CellInfo& cell);
    void fit_block(CellInfo& cell, double W, double H);
    void anchor_item(Item& it, int alignment) const;

    Border horizontal_border(size_t boundary, size_t column) const;
    Border vertical_border(size_t boundary, size_t row) const;

    void make_geometry();
    void make_entities();
    Entity base_entity(EntityType type) const;
    // turn: with the table's turn (the *T block's), or without (an anonymous block's, which its INSERT turns)
    Entity text_entity(const Item& it, double ox, double oy, double wrap_width, bool turn = true) const;
    Entity insert_entity(const Item& it, double ox, double oy, bool turn = true) const;
    Entity line_entity(const Point3& a, const Point3& b, const Border& border) const;

    const Drawing& dwg_;
    const Entity& table_;
    const TableData& t_;
    const TableContentData* content_;
    TextLayout& text_;
    TableLayout& out_;

    TableStyleData own_style_;
    const TableStyleData* style_ = nullptr;
    std::vector<int> row_type_;
    const CellStyleMapData* cell_styles_ = nullptr;
    // The table turned (its horizontal direction): AutoCAD turns the *T block's own geometry about the table's origin; the
    // table's transform keeps only where it is
    double rot_cos_ = 1.0, rot_sin_ = 0.0, rot_deg_ = 0.0;
    Point3 turned(double x, double y) const { return {x * rot_cos_ - y * rot_sin_, x * rot_sin_ + y * rot_cos_, 0.0}; }
    Point3 turned_vector(const Point3& v) const { return turned(v.x, v.y); }
    bool flow_up_ = false;              // the table's rows go up from its insertion point (its flow direction 1)
    // A row boundary's place in the block: down from the origin, or up
    double edge_y(size_t b) const { return flow_up_ ? out_.y[b] : -out_.y[b]; }
    size_t rows_ = 0, cols_ = 0;
    std::vector<CellInfo> cells_;
    std::vector<double> row_h_, col_w_;
    std::set<std::string> reported_;
};

Resolved Engine::resolve(size_t r, size_t c) const {
    const TableCell& cell = *t_.cell(r, c);
    const TableStyleRow& sr = style_row(r);
    Resolved rs;
    rs.row_type = row_type_[r];
    if (cell.overrides.text_style) rs.style = *cell.overrides.text_style;
    else if (sr.text_style && !sr.text_style->empty()) rs.style = *sr.text_style;
    else rs.style = "Standard";
    rs.height_override = cell.overrides.text_height.has_value();
    if (cell.overrides.text_height) rs.height = *cell.overrides.text_height;
    else if (t_.overrides.rows[row_type_[r]].text_height) rs.height = *t_.overrides.rows[row_type_[r]].text_height;
    else rs.height = sr.text_height;
    rs.alignment = cell.overrides.alignment ? *cell.overrides.alignment : sr.alignment;
    // A style that says no text height and no alignment (HDxf's own default one) gets AutoCAD's: the title and header
    // rows centered, the data rows centered at the top, the heights of the drawing's units (0.18 / 0.25 inch, 4.5 / 6 mm)
    if (rs.height <= 0.0) {
        bool metric = false;
        if (const HeaderVar* hv = dwg_.find_header("$MEASUREMENT")) metric = !hv->values.empty() && std::atoi(hv->values[0].second.c_str()) == 1;
        rs.height = rs.row_type == 1 ? (metric ? 6.0 : 0.25) : (metric ? 4.5 : 0.18);
    }
    if (rs.alignment < 1 || rs.alignment > 9) rs.alignment = rs.row_type == 0 ? 2 : 5;
    rs.color = cell.overrides.content_color ? *cell.overrides.content_color : Color::from_aci(sr.text_color);
    if (cell.overrides.fill_color) rs.fill = *cell.overrides.fill_color;
    else if (sr.fill_enabled && !(cell.overrides.fill_enabled && !*cell.overrides.fill_enabled)) {
        rs.fill = Color::from_aci(sr.fill_color);
    }
    rs.rotation = cell.rotation;
    // The cell style the cell uses (TABLECONTENT's cell: its id; else the row type's): its margins, else the table
    // style's (the legacy 40, 41); the table's own override above both
    int style_id = rs.row_type == 1 ? 1 : rs.row_type == 2 ? 2 : 3;
    if (content_ && r < content_->rows.size() && c < content_->rows[r].cells.size() && content_->rows[r].cells[c].cell_flags != 0) {
        style_id = content_->rows[r].cells[c].cell_flags;
    }
    rs.m.left = rs.m.right = style_->horizontal_margin;
    rs.m.top = rs.m.bottom = style_->vertical_margin;
    if (cell_styles_) {
        for (const TableCellStyle& cs : cell_styles_->styles) {
            if (cs.id != style_id) continue;
            if (cs.format.has_margins) {
                rs.m.left = cs.format.margins.left;
                rs.m.right = cs.format.margins.right;
                rs.m.top = cs.format.margins.top;
                rs.m.bottom = cs.format.margins.bottom;
                if (cs.format.margins.spacing_1 > 0.0) rs.m.gap_h = cs.format.margins.spacing_1;
                if (cs.format.margins.spacing_2 > 0.0) rs.m.gap_v = cs.format.margins.spacing_2;
            }
            break;
        }
    }
    // The cell's own format (TABLECONTENT's TABLEFORMAT of the cell) with margins of its own: AutoCAD's SCALE puts the scaled margins and
    // spacing there
    if (content_ && r < content_->rows.size() && c < content_->rows[r].cells.size() && content_->rows[r].cells[c].format.has_margins) {
        const TableCellMargins& cm = content_->rows[r].cells[c].format.margins;
        rs.m.left = cm.left;
        rs.m.right = cm.right;
        rs.m.top = cm.top;
        rs.m.bottom = cm.bottom;
        if (cm.spacing_1 > 0.0) rs.m.gap_h = cm.spacing_1;
        if (cm.spacing_2 > 0.0) rs.m.gap_v = cm.spacing_2;
    }
    if (t_.overrides.horizontal_margin) rs.m.left = rs.m.right = *t_.overrides.horizontal_margin;
    if (t_.overrides.vertical_margin) rs.m.top = rs.m.bottom = *t_.overrides.vertical_margin;
    return rs;
}

double Engine::span_width(size_t r, size_t c, size_t columns) const {
    (void)r;
    double w = 0.0;
    for (size_t k = c; k < c + columns && k < cols_; ++k) w += col_w_[k];
    return w;
}

double Engine::span_height(size_t r, size_t rows) const {
    double h = 0.0;
    for (size_t k = r; k < r + rows && k < rows_; ++k) h += row_h_[k];
    return h;
}

// A text laid out as its cell lays it: MTEXT at the width given, its box with the pad AutoCAD adds
Item Engine::text_item(const std::string& text, const Resolved& rs, double wrap, bool keep_empty) {
    Item it;
    it.text = text;
    it.style = rs.style;
    it.height = rs.height;
    it.color = rs.color;
    it.alignment = rs.alignment;
    it.rotation = rs.rotation;
    if (text.empty()) {
        // An empty text that is a content has the text's height and no width: a value that has a type and no text
        // (ACadSharp's A35: a point, a date and a number that are not set), a cell with a text height of its own
        // (528's empty data cells; not the title row's)
        it.empty = true;
        if (keep_empty) { it.text_w = 0.0; it.text_h = rs.height; }
        else { it.drop = true; return it; }
        it.w = it.text_w;
        it.h = it.text_h;
        return it;
    }
    remeasure_text(it, wrap);
    return it;
}

// The MTEXT of an item at a width: its own size and the box it takes, turned about its anchor when the cell's text is
MTextSize Engine::measure_mtext(const Item& it, double wrap) {
    MTextData m;
    m.content = it.text;
    m.height = it.height;
    m.style = it.style;
    m.attachment = it.alignment;
    m.drawing_direction = 5;
    m.reference_width = wrap > 0.0 ? wrap : 0.0;
    m.x_axis_direction = {std::cos(it.rotation), std::sin(it.rotation), 0.0};
    MTextSize s = text_.mtext(m, true);
    if (!s.font) {
        // Neither the style's font nor simplex.shx is found: the width of the characters at half the height
        if (reported_.insert(it.style).second) {
            out_.warnings.push_back("The font of text style \"" + it.style + "\" was not found: the table text is measured without it");
        }
        s.height = it.height;
        s.width = 0.5 * it.height * static_cast<double>(code_points(it.text).size());
    }
    return s;
}

void Engine::remeasure_text(Item& it, double wrap) {
    it.size = measure_mtext(it, wrap);
    it.text_w = it.size.width + kTableTextPad;
    it.text_h = it.size.height;
    it.w = it.text_w;
    it.h = it.text_h;
    if (std::fabs(it.rotation) >= 1e-12) {
        const double cs = std::fabs(std::cos(it.rotation)), sn = std::fabs(std::sin(it.rotation));
        it.w = it.text_w * cs + it.text_h * sn;
        it.h = it.text_w * sn + it.text_h * cs;
    }
}

// The width of the text's longest word and the pad: the least width a text can be laid out at
double Engine::measure_text_minimum(const Item& it) {
    return measure_mtext(it, 1e-6).width + kTableTextPad;
}

Item Engine::block_item(const Block* block, double scale, const std::vector<TableAttributeValue>& attrs, bool auto_fit) {
    Item it;
    it.is_block = true;
    it.block = block;
    it.block_name = block ? block->name : std::string();
    it.scale = scale;
    it.attributes = attrs;
    if (block) it.box = block_box(dwg_, *block);
    if (it.box.valid) {
        it.w = (it.box.max_x - it.box.min_x) * scale;
        it.h = (it.box.max_y - it.box.min_y) * scale;
    }
    (void)auto_fit;
    return it;
}

// The cell's items (a single content from the entity, or the contents of its TABLECONTENT cell), their lines
void Engine::measure_cell(size_t r, size_t c, CellInfo& cell) {
    const TableCell& tc = *t_.cell(r, c);
    const double W = span_width(r, c, cell.span_columns);
    const double H = span_height(r, cell.span_rows);
    const double avail = std::max(0.0, W - cell.res.m.horizontal());
    // Turned a quarter, the text wraps at the cell's height
    const double turned = std::fabs(std::sin(cell.res.rotation));
    const double wrap = turned > 0.7 ? std::max(0.0, H - cell.res.m.vertical()) : avail;

    const TableContentCell* cc = nullptr;
    if (content_ && r < content_->rows.size() && c < content_->rows[r].cells.size()) cc = &content_->rows[r].cells[c];
    cell.multi = tc.type == TableCellType::Block && cc && !cc->attachments.empty() && !cc->contents.empty();
    if (cell.multi) {
        const Block* anon = block_of_record(dwg_, tc.block_handle);
        if (!anon) cell.multi = false;
        else cell.content_block = anon->name;
    }

    if (cell.multi) {
        for (const TableCellContent& ct : cc->contents) {
            if (ct.kind == TableContentKind::Block) {
                const Block* b = block_of_record(dwg_, ct.handle);
                double s = ct.format ? ct.format->scale : 1.0;
                if (s == 0.0) s = 1.0;
                cell.items.push_back(block_item(b, s, {}, false));
            } else {
                std::string text = ct.kind == TableContentKind::Field ? table_field_text(dwg_, ct.handle) : table_value_text(ct.value);
                const bool keep = ct.value.type != TableValueType::Unset || ct.kind == TableContentKind::Field ||
                                  (cell.res.height_override && cell.res.row_type != 1);
                Item it = text_item(text, cell.res, avail, keep);
                it.field = ct.kind == TableContentKind::Field;
                if (!it.drop) cell.items.push_back(std::move(it));
            }
        }
    } else if (tc.type == TableCellType::Block) {
        const Block* b = block_of_record(dwg_, tc.block_handle);
        Item it = block_item(b, tc.block_scale, tc.attributes, tc.auto_fit);
        cell.auto_block = tc.auto_fit && b;
        cell.items.push_back(std::move(it));
    } else {
        std::string text;
        bool field = false;
        if (!tc.field_handle.empty()) {
            text = table_field_text(dwg_, tc.field_handle);
            field = true;
        } else {
            text = table_value_text(tc.value);
        }
        const bool keep = tc.value.type != TableValueType::Unset || field || (cc && !cc->contents.empty()) ||
                          (cell.res.height_override && cell.res.row_type != 1);
        Item it = text_item(text, cell.res, wrap, keep);
        it.field = field;
        if (!it.drop) cell.items.push_back(std::move(it));
    }

    break_lines(cell, avail);
}

// The items in lines: each as many as fit in the width, left to right with the spacing between them
void Engine::break_lines(CellInfo& cell, double avail) {
    cell.lines.clear();
    Line cur;
    auto close = [&]() {
        if (!cur.items.empty()) cell.lines.push_back(cur);
        cur = Line();
    };
    for (size_t i = 0; i < cell.items.size(); ++i) {
        Item& it = cell.items[i];
        if (cell.auto_block) {            // sized after the rows
            cur.items.push_back(i);
            continue;
        }
        if (!cur.items.empty()) {
            const Item& before = cell.items[cur.items.back()];
            if (it.is_block || it.empty) {
                // A block goes on the line when it fits
                if (cur.width + cell.res.m.gap_h + it.w > avail + 1e-9) close();
            } else if (cur.width + cell.res.m.gap_h + it.w <= avail + 1e-9) {
                // A text that fits whole goes on the line (AutoCAD 2025 recomputing the cell with two short texts: both on one line)
            } else if (!before.is_block) {
                // A text after a text that does not fit starts a line, as wide as the cell (the two contents of Erik's
                // cell: the second, 34 wide, a line below the first, wrapped to the cell's 32)
                close();
            } else {
                // A text after a block takes the rest of the line, wrapped to it (the A35 cell: the text of 2 lines to the
                // right of the two blocks, laid out at the width left); when even its longest word does not fit, a new line
                const double rest = avail - cur.width - cell.res.m.gap_h;
                if (rest > 0.0 && measure_text_minimum(it) <= rest + 1e-9) {
                    remeasure_text(it, rest);
                    if (it.w > rest + 1e-9) close();
                } else {
                    close();
                }
            }
        }
        cur.width = cur.items.empty() ? it.w : cur.width + cell.res.m.gap_h + it.w;
        cur.height = std::max(cur.height, it.h);
        cur.items.push_back(i);
    }
    close();
    cell.flow_height = 0.0;
    for (size_t i = 0; i < cell.lines.size(); ++i) {
        cell.flow_height += cell.lines[i].height + (i ? cell.res.m.gap_v : 0.0);
    }
}

// An auto-fit block as large as the cell less its margins allows
void Engine::fit_block(CellInfo& cell, double W, double H) {
    for (Item& it : cell.items) {
        if (!it.is_block || !it.box.valid) continue;
        const double bw = it.box.max_x - it.box.min_x, bh = it.box.max_y - it.box.min_y;
        double s = it.scale;
        const double aw = std::max(0.0, W - cell.res.m.horizontal()), ah = std::max(0.0, H - cell.res.m.vertical());
        if (bw > 0.0 && bh > 0.0) s = std::min(aw / bw, ah / bh);
        else if (bw > 0.0) s = aw / bw;
        else if (bh > 0.0) s = ah / bh;
        it.scale = s;
        it.w = bw * s;
        it.h = bh * s;
    }
    cell.lines.clear();
    Line l;
    for (size_t i = 0; i < cell.items.size(); ++i) {
        l.items.push_back(i);
        l.width = l.items.size() == 1 ? cell.items[i].w : l.width + cell.res.m.gap_h + cell.items[i].w;
        l.height = std::max(l.height, cell.items[i].h);
    }
    if (!l.items.empty()) cell.lines.push_back(l);
    cell.flow_height = cell.lines.empty() ? 0.0 : cell.lines[0].height;
}

// The anchor of an item from its box: the MTEXT's attachment point, the INSERT's insertion point
void Engine::anchor_item(Item& it, int alignment) const {
    const int h = (alignment - 1) % 3;      // 0 left, 1 center, 2 right
    const int v = (alignment - 1) / 3;      // 0 top, 1 middle, 2 bottom
    if (it.is_block) {
        const Block* b = it.block;
        const double bx = b ? b->base_point.x : 0.0, by = b ? b->base_point.y : 0.0;
        // the INSERT puts the block's base point at its insertion point; the box's top-left is
        // (min_x - base.x, max_y - base.y) times the scale from it
        it.ax = it.px - (it.box.min_x - bx) * it.scale;
        it.ay = it.py - (it.box.max_y - by) * it.scale;
        return;
    }
    // the box about the anchor (x right, y up): the anchor is at (ox, oy) from its top-left
    const double ox = h == 0 ? 0.0 : h == 1 ? it.text_w / 2.0 : it.text_w;
    const double oy = v == 0 ? 0.0 : v == 1 ? it.text_h / 2.0 : it.text_h;
    const double cs = std::cos(it.rotation), sn = std::sin(it.rotation);
    double min_x = 1e300, max_y = -1e300;
    for (double x : {-ox, it.text_w - ox}) {
        for (double y : {oy - it.text_h, oy}) {
            min_x = std::min(min_x, x * cs - y * sn);
            max_y = std::max(max_y, x * sn + y * cs);
        }
    }
    it.ax = it.px - min_x;
    it.ay = it.py - max_y;
}

// The contents' places in the cell: the lines laid out from the top left by the alignment
void Engine::place_cell(size_t r, size_t c, CellInfo& cell) {
    const double W = span_width(r, c, cell.span_columns);
    const double H = span_height(r, cell.span_rows);
    const double avail = std::max(0.0, W - cell.res.m.horizontal());
    const int h = (cell.res.alignment - 1) % 3;
    const int v = (cell.res.alignment - 1) / 3;
    double top;
    if (v == 0) top = -cell.res.m.top;
    else if (v == 1) top = -(H - cell.flow_height) / 2.0;
    else top = -(H - cell.res.m.bottom - cell.flow_height);
    double y = top;
    for (size_t li = 0; li < cell.lines.size(); ++li) {
        const Line& line = cell.lines[li];
        double x;
        if (h == 0) x = cell.res.m.left;
        else if (h == 1) x = cell.res.m.left + (avail - line.width) / 2.0;
        else x = cell.res.m.left + avail - line.width;
        for (size_t k = 0; k < line.items.size(); ++k) {
            Item& it = cell.items[line.items[k]];
            double dy = v == 0 ? 0.0 : v == 1 ? (line.height - it.h) / 2.0 : line.height - it.h;
            it.px = x;
            it.py = y - dy;
            anchor_item(it, cell.res.alignment);
            x += it.w + cell.res.m.gap_h;
        }
        y -= line.height + cell.res.m.gap_v;
    }
}

Border Engine::horizontal_border(size_t boundary, size_t column) const {
    // Between the row above and the row below; the cell below's top, else the cell above's bottom, else the style's
    const bool has_up = boundary > 0, has_down = boundary < rows_;
    Border b;
    if (has_down) {
        const TableCell& cell = *t_.cell(boundary, column);
        const TableEdgeOverride& o = cell.overrides.top;
        const TableStyleBorder* sb;
        if (!has_up) sb = &style_row(boundary).borders[static_cast<int>(TableGridEdge::HorizontalTop)];
        else if (row_type_[boundary - 1] == row_type_[boundary]) sb = &style_row(boundary).borders[static_cast<int>(TableGridEdge::HorizontalInside)];
        else sb = &style_row(boundary).borders[static_cast<int>(TableGridEdge::HorizontalTop)];
        b = {sb->lineweight, sb->color, sb->visible};
        if (o.lineweight) b.lineweight = *o.lineweight;
        if (o.color) b.color = *o.color;
        if (o.visible) b.visible = *o.visible;
        if (has_up && !o.any()) {
            const TableEdgeOverride& ob = t_.cell(boundary - 1, column)->overrides.bottom;
            if (ob.lineweight) b.lineweight = *ob.lineweight;
            if (ob.color) b.color = *ob.color;
            if (ob.visible) b.visible = *ob.visible;
        }
    } else {
        const TableCell& cell = *t_.cell(boundary - 1, column);
        const TableStyleBorder& sb = style_row(boundary - 1).borders[static_cast<int>(TableGridEdge::HorizontalBottom)];
        b = {sb.lineweight, sb.color, sb.visible};
        const TableEdgeOverride& o = cell.overrides.bottom;
        if (o.lineweight) b.lineweight = *o.lineweight;
        if (o.color) b.color = *o.color;
        if (o.visible) b.visible = *o.visible;
    }
    return b;
}

Border Engine::vertical_border(size_t boundary, size_t row) const {
    const bool has_left = boundary > 0, has_right = boundary < cols_;
    Border b;
    const TableStyleRow& sr = style_row(row);
    if (has_right) {
        const TableCell& cell = *t_.cell(row, boundary);
        const TableEdgeOverride& o = cell.overrides.left;
        const TableStyleBorder& sb = sr.borders[static_cast<int>(has_left ? TableGridEdge::VerticalInside : TableGridEdge::VerticalLeft)];
        b = {sb.lineweight, sb.color, sb.visible};
        if (o.lineweight) b.lineweight = *o.lineweight;
        if (o.color) b.color = *o.color;
        if (o.visible) b.visible = *o.visible;
        if (has_left && !o.any()) {
            const TableEdgeOverride& orr = t_.cell(row, boundary - 1)->overrides.right;
            if (orr.lineweight) b.lineweight = *orr.lineweight;
            if (orr.color) b.color = *orr.color;
            if (orr.visible) b.visible = *orr.visible;
        }
    } else {
        const TableCell& cell = *t_.cell(row, boundary - 1);
        const TableStyleBorder& sb = sr.borders[static_cast<int>(TableGridEdge::VerticalRight)];
        b = {sb.lineweight, sb.color, sb.visible};
        const TableEdgeOverride& o = cell.overrides.right;
        if (o.lineweight) b.lineweight = *o.lineweight;
        if (o.color) b.color = *o.color;
        if (o.visible) b.visible = *o.visible;
    }
    return b;
}

Entity Engine::base_entity(EntityType type) const {
    Entity e;
    e.type = type;
    e.props.layer = table_.props.layer;
    e.props.transparency = Transparency::by_block();
    return e;
}

Entity Engine::line_entity(const Point3& a, const Point3& b, const Border& border) const {
    Entity e = base_entity(EntityType::Line);
    e.props.color = Color::from_aci(border.color);
    e.props.linetype = "ByBlock";
    e.props.lineweight = border.lineweight;
    e.data = LineData{turned_vector(a), turned_vector(b)};
    return e;
}

Entity Engine::text_entity(const Item& it, double ox, double oy, double wrap_width, bool turn) const {
    Entity e = base_entity(EntityType::MText);
    e.props.color = it.color;
    MTextData m;
    m.insertion_point = turn ? turned(ox + it.ax, oy + it.ay) : Point3{ox + it.ax, oy + it.ay, 0.0};
    m.height = it.height;
    m.reference_width = wrap_width;
    m.attachment = it.alignment;
    m.drawing_direction = 5;
    m.content = it.text;
    m.style = it.style;
    const double turn_angle = turn ? std::atan2(rot_sin_, rot_cos_) : 0.0;
    m.x_axis_direction = {std::cos(it.rotation + turn_angle), std::sin(it.rotation + turn_angle), 0.0};
    e.data = std::move(m);
    return e;
}

Entity Engine::insert_entity(const Item& it, double ox, double oy, bool turn) const {
    Entity e = base_entity(EntityType::Insert);
    e.props.color = Color::by_block();
    InsertData d;
    d.block_name = it.block_name;
    d.insertion_point = turn ? turned(ox + it.ax, oy + it.ay) : Point3{ox + it.ax, oy + it.ay, 0.0};
    const double rot_deg = turn ? rot_deg_ : 0.0;
    d.rotation = rot_deg;
    d.x_scale = d.y_scale = d.z_scale = it.scale;
    // ATTRIBs: the cell's values at where each ATTDEF puts them (as the INSERT maps them)
    if (it.block) {
        for (const TableAttributeValue& av : it.attributes) {
            const uint64_t want = handle_number(av.attdef_handle);
            for (const Entity& be : it.block->entities) {
                if (be.type != EntityType::Attdef || handle_number(be.props.handle) != want) continue;
                const AttdefData& ad = be.as_attdef();
                InsertAttribute ia;
                ia.props = base_entity(EntityType::Attrib).props;
                ia.props.color = be.props.color;
                ia.props.layer = be.props.layer == "0" ? table_.props.layer : be.props.layer;
                AttribData& a = ia.data;
                const double bx = it.block->base_point.x, by = it.block->base_point.y;
                // (the block turned with the table, so its attribute definitions are)
                const double sx = std::cos(rot_deg * M_PI / 180.0), sy = std::sin(rot_deg * M_PI / 180.0);
                auto place = [&](const Point3& q) {
                    const double x = (q.x - bx) * it.scale, y = (q.y - by) * it.scale;
                    return Point3{d.insertion_point.x + x * sx - y * sy, d.insertion_point.y + x * sy + y * sx, 0.0};
                };
                a.start_point = place(ad.start_point);
                a.alignment_point = place(ad.alignment_point);
                a.height = ad.height * it.scale;
                a.value = av.text;
                a.tag = ad.tag;
                a.flags = ad.flags;
                a.rotation = ad.rotation + rot_deg;
                a.width_factor = ad.width_factor;
                a.oblique_angle = ad.oblique_angle;
                a.style = ad.style;
                a.generation_flags = ad.generation_flags;
                a.h_justify = ad.h_justify;
                a.v_justify = ad.v_justify;
                d.attributes.push_back(std::move(ia));
                break;
            }
        }
    }
    e.data = std::move(d);
    // AutoCAD's INSERTs in a table block carry this application's data
    XdataList x;
    x.app_name = "ACAD";
    x.items.push_back({1000, "_DRAW_ANNO"});
    e.props.xdata.push_back(std::move(x));
    return e;
}

void Engine::make_geometry() {
    TableGeometryData& g = out_.geometry;
    g.rows = static_cast<int32_t>(rows_);
    g.columns = static_cast<int32_t>(cols_);
    g.cells.clear();
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            const CellInfo& cell = cells_[r * cols_ + c];
            TableCellGeometry cg;
            cg.flags = 7;
            cg.handle = "0";
            cg.width = cell.res.m.horizontal();
            cg.height = cell.res.m.vertical();
            if (cell.multi) {
                cg.flags = 4;
                cg.width = cg.height = 0.0;
                for (const Item& it : cell.items) {
                    TableContentGeometry cgc;
                    cgc.anchor = turned(it.ax, it.ay);
                    cg.contents.push_back(cgc);
                }
            } else {
                for (const Item& it : cell.items) {
                    TableContentGeometry cgc;
                    cgc.point = turned(it.px, it.py);
                    cgc.anchor = turned(it.ax, it.ay);
                    cgc.width = it.w;
                    cgc.height = it.h;
                    cgc.content_width = it.w;
                    cgc.content_height = it.h;
                    cg.contents.push_back(cgc);
                    cg.width = it.w + cell.res.m.horizontal();
                    cg.height = it.h + cell.res.m.vertical();
                }
            }
            g.cells.push_back(std::move(cg));
        }
    }
}

void Engine::make_entities() {
    std::vector<Entity>& block = out_.block;
    block.clear();
    auto origin = [&](size_t r, size_t c, double& ox, double& oy) {
        ox = out_.x[c];
        oy = flow_up_ ? out_.y[r + 1] : -out_.y[r];
    };
    // Fills: a merged range's whole rectangle, once, on its first cell
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            const CellInfo& cell = cells_[r * cols_ + c];
            if (!cell.owner || !cell.res.fill) continue;
            const double x0 = out_.x[c], x1 = out_.x[c + cell.span_columns];
            const double y0 = flow_up_ ? out_.y[r + cell.span_rows] : -out_.y[r];
            const double y1 = flow_up_ ? out_.y[r] : -out_.y[r + cell.span_rows];
            Entity e = base_entity(EntityType::Solid);
            e.props.color = *cell.res.fill;
            // A table that flows up has them lower edge first (AutoCAD's mirrored table)
            if (flow_up_) e.data = SolidData{turned(x0, y1), turned(x1, y1), turned(x0, y0), turned(x1, y0)};
            else e.data = SolidData{turned(x0, y0), turned(x0, y1), turned(x1, y0), turned(x1, y1)};
            block.push_back(std::move(e));
        }
    }
    // Text: row by row
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            const CellInfo& cell = cells_[r * cols_ + c];
            if (!cell.owner || cell.multi) continue;
            double ox, oy;
            origin(r, c, ox, oy);
            const double W = out_.x[c + cell.span_columns] - out_.x[c];
            for (const Item& it : cell.items) {
                if (it.is_block || it.empty) continue;
                block.push_back(text_entity(it, ox, oy, std::max(0.0, W - cell.res.m.horizontal())));
                if (it.field) out_.field_texts.insert({std::string(), block.size() - 1});
                out_.text_sizes[{std::string(), block.size() - 1}] = it.size;
            }
        }
    }
    // Blocks: a block cell's INSERT, or the INSERT of the anonymous block that holds several contents
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            const CellInfo& cell = cells_[r * cols_ + c];
            if (!cell.owner) continue;
            double ox, oy;
            origin(r, c, ox, oy);
            if (cell.multi) {
                Entity e = base_entity(EntityType::Insert);
                e.props.color = Color::by_block();
                InsertData d;
                d.block_name = cell.content_block;
                d.rotation = rot_deg_;
                e.data = std::move(d);
                XdataList x;
                x.app_name = "ACAD";
                x.items.push_back({1000, "_DRAW_ANNO"});
                e.props.xdata.push_back(std::move(x));
                block.push_back(std::move(e));
                // its contents, in the table's coordinates: text first, then blocks
                TableCellBlock cb;
                cb.name = cell.content_block;
                for (const Item& it : cell.items) {
                    if (it.is_block || it.empty) continue;
                    cb.entities.push_back(text_entity(it, ox, oy, it.w, false));
                    if (it.field) out_.field_texts.insert({cell.content_block, cb.entities.size() - 1});
                    out_.text_sizes[{cell.content_block, cb.entities.size() - 1}] = it.size;
                }
                for (const Item& it : cell.items) {
                    if (it.is_block && it.block) cb.entities.push_back(insert_entity(it, ox, oy, false));
                }
                out_.cell_blocks.push_back(std::move(cb));
                continue;
            }
            for (const Item& it : cell.items) {
                if (it.is_block && it.block) block.push_back(insert_entity(it, ox, oy));
            }
        }
    }
    // Horizontal borders, each row boundary from the top, equal neighbours as one line (a table without rows or columns has none)
    for (size_t b = 0; b <= rows_ && rows_ > 0 && cols_ > 0; ++b) {
        bool open = false;
        size_t start = 0;
        Border current;
        auto flush = [&](size_t end) {
            if (!open) return;
            block.push_back(line_entity({out_.x[start], edge_y(b), 0.0}, {out_.x[end], edge_y(b), 0.0}, current));
            open = false;
        };
        for (size_t c = 0; c < cols_; ++c) {
            bool inside = false;
            if (b > 0 && b < rows_) inside = cells_[(b - 1) * cols_ + c].owner_index == cells_[b * cols_ + c].owner_index;
            Border border = horizontal_border(b, c);
            if (inside || !border.visible) { flush(c); continue; }
            if (open && border == current) continue;
            flush(c);
            open = true;
            start = c;
            current = border;
        }
        flush(cols_);
    }
    // Vertical borders, each column boundary from the left, top to bottom
    for (size_t b = 0; b <= cols_ && rows_ > 0 && cols_ > 0; ++b) {
        bool open = false;
        size_t start = 0;
        Border current;
        auto flush = [&](size_t end) {
            if (!open) return;
            block.push_back(line_entity({out_.x[b], edge_y(start), 0.0}, {out_.x[b], edge_y(end), 0.0}, current));
            open = false;
        };
        for (size_t r = 0; r < rows_; ++r) {
            bool inside = false;
            if (b > 0 && b < cols_) inside = cells_[r * cols_ + b - 1].owner_index == cells_[r * cols_ + b].owner_index;
            Border border = vertical_border(b, r);
            if (inside || !border.visible) { flush(r); continue; }
            if (open && border == current) continue;
            flush(r);
            open = true;
            start = r;
            current = border;
        }
        flush(rows_);
    }
    // The marker AutoCAD puts at the table's origin: a point-sized line on Defpoints, not shown
    Entity marker = base_entity(EntityType::Line);
    marker.props.layer = "Defpoints";
    marker.props.color = Color::from_aci(8);
    marker.props.lineweight = 0;
    marker.props.visible = false;
    marker.data = LineData{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}};
    block.push_back(std::move(marker));
}

bool Engine::run() {
    rows_ = t_.rows();
    cols_ = t_.columns();
    if (t_.cells.size() != rows_ * cols_) {
        out_.warnings.push_back("The table has " + std::to_string(t_.cells.size()) + " cells, not " + std::to_string(rows_) + " x " +
                                std::to_string(cols_) + ": it is not laid out");
        return false;
    }
    if (const Object* so = dwg_.find_object(t_.style_handle); so && so->type == ObjectType::TableStyle) {
        style_ = &so->as_table_style();
    } else {
        own_style_ = fallback_style();
        style_ = &own_style_;
    }
    if (const Object* so = dwg_.find_object(t_.style_handle); so && so->type == ObjectType::TableStyle) {
        if (const Object* map = dwg_.table_cell_style_map(*so)) cell_styles_ = &map->as_cell_style_map();
    }

    {
        // The horizontal direction in the table's plane: its angle from the plane's x axis
        Point3 ax, ay;
        ecs::arbitrary_axis(table_.props.extrusion, ax, ay);
        const Point3& dir = t_.horizontal_direction;
        const double u = dir.x * ax.x + dir.y * ax.y + dir.z * ax.z, v = dir.x * ay.x + dir.y * ay.y + dir.z * ay.z;
        const double len = std::sqrt(u * u + v * v);
        if (len > 0.0 && (std::fabs(v) > 1e-12 || u < 0.0)) {
            rot_cos_ = u / len;
            rot_sin_ = v / len;
            rot_deg_ = std::atan2(v, u) * 180.0 / M_PI;
        }
    }
    flow_up_ = (t_.overrides.flow_direction ? *t_.overrides.flow_direction : style_->flow_direction) == 1;

    // Row types: the title row and the header row unless the table (or its style) suppresses them
    const bool title_off = t_.overrides.title_suppressed ? *t_.overrides.title_suppressed : style_->title_suppressed;
    const bool header_off = t_.overrides.header_suppressed ? *t_.overrides.header_suppressed : style_->header_suppressed;
    row_type_.assign(rows_, 0);
    size_t next = 0;
    if (!title_off && next < rows_) row_type_[next++] = 1;
    if (!header_off && next < rows_) row_type_[next++] = 2;

    row_h_ = t_.row_heights;
    col_w_ = t_.column_widths;
    out_.x.assign(cols_ + 1, 0.0);
    for (size_t c = 0; c < cols_; ++c) out_.x[c + 1] = out_.x[c] + col_w_[c];

    // Merged ranges: the first cell has the extent; every cell in it knows its owner
    cells_.assign(rows_ * cols_, CellInfo());
    for (size_t i = 0; i < cells_.size(); ++i) cells_[i].owner_index = i;
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            const TableCell& tc = *t_.cell(r, c);
            if (tc.merged != 0 || (tc.merge_columns <= 1 && tc.merge_rows <= 1)) continue;
            CellInfo& first = cells_[r * cols_ + c];
            first.span_rows = static_cast<size_t>(std::max<int>(1, tc.merge_rows));
            first.span_columns = static_cast<size_t>(std::max<int>(1, tc.merge_columns));
            first.span_rows = std::min(first.span_rows, rows_ - r);
            first.span_columns = std::min(first.span_columns, cols_ - c);
            for (size_t rr = r; rr < r + first.span_rows; ++rr) {
                for (size_t cc = c; cc < c + first.span_columns; ++cc) {
                    if (rr == r && cc == c) continue;
                    cells_[rr * cols_ + cc].owner = false;
                    cells_[rr * cols_ + cc].owner_index = r * cols_ + c;
                }
            }
        }
    }

    // Pass 1: the contents, their sizes, the rows they make
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            CellInfo& cell = cells_[r * cols_ + c];
            cell.res = resolve(r, c);
            if (!cell.owner) continue;
            measure_cell(r, c, cell);
        }
    }
    // A row is as high as it was and as its highest single-row cell needs (an auto-fit block takes the height it has);
    // a merged range that needs more than its rows have adds the rest to its last row
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            const CellInfo& cell = cells_[r * cols_ + c];
            if (!cell.owner || cell.span_rows != 1 || cell.auto_block) continue;
            const double need = cell.items.empty() ? cell.res.m.vertical() : cell.flow_height + cell.res.m.vertical();
            row_h_[r] = std::max(row_h_[r], need);
        }
    }
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            const CellInfo& cell = cells_[r * cols_ + c];
            if (!cell.owner || cell.span_rows <= 1 || cell.auto_block) continue;
            const double need = cell.items.empty() ? cell.res.m.vertical() : cell.flow_height + cell.res.m.vertical();
            const double have = span_height(r, cell.span_rows);
            if (need > have) row_h_[r + cell.span_rows - 1] += need - have;
        }
    }
    out_.y.assign(rows_ + 1, 0.0);
    for (size_t r = 0; r < rows_; ++r) out_.y[r + 1] = out_.y[r] + row_h_[r];

    // Pass 2: auto-fit blocks, then each cell's contents placed
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            CellInfo& cell = cells_[r * cols_ + c];
            if (!cell.owner) continue;
            if (cell.auto_block) fit_block(cell, span_width(r, c, cell.span_columns), span_height(r, cell.span_rows));
            place_cell(r, c, cell);
        }
    }
    // The cells that are merged into another: sized and placed as cells of their own, for TABLEGEOMETRY
    for (size_t r = 0; r < rows_; ++r) {
        for (size_t c = 0; c < cols_; ++c) {
            CellInfo& cell = cells_[r * cols_ + c];
            if (cell.owner) continue;
            const size_t keep_rows = cell.span_rows, keep_cols = cell.span_columns;
            cell.span_rows = cell.span_columns = 1;
            measure_cell(r, c, cell);
            place_cell(r, c, cell);
            cell.span_rows = keep_rows;
            cell.span_columns = keep_cols;
        }
    }
    out_.row_heights = row_h_;
    out_.column_widths = col_w_;
    make_geometry();
    make_entities();
    return true;
}

} // namespace

bool layout_table(const Drawing& dwg, const Entity& table, const TableContentData* content, TextLayout& text,
                  TableLayout& out) {
    out = TableLayout();
    if (table.type != EntityType::Table) return false;
    Engine engine(dwg, table, content, text, out);
    return engine.run();
}

// The text a FIELD shows (step 9.8's typed field: its cached value, or for a text field the text with each
// child's value where %<\_FldIdx n>% stands)
std::string table_field_text(const Drawing& dwg, const std::string& field_handle) {
    const Object* o = dwg.find_object(field_handle);
    return o ? dwg.field_text(*o) : std::string();
}

namespace {

// FNV-1a over what the layout reads
struct Hasher {
    uint64_t h = 1469598103934665603ULL;
    void bytes(const void* data, size_t n) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ULL;
        }
    }
    void u64(uint64_t v) { bytes(&v, sizeof v); }
    void i64(int64_t v) { bytes(&v, sizeof v); }
    void dbl(double v) { bytes(&v, sizeof v); }
    void flag(bool v) { u64(v ? 1 : 0); }
    void str(const std::string& s) {
        u64(s.size());
        bytes(s.data(), s.size());
    }
    void color(const Color& c) {
        i64(c.aci);
        i64(c.rgb);
    }
    template <typename T>
    void opt_dbl(const std::optional<T>& v) {
        flag(v.has_value());
        if (v) dbl(static_cast<double>(*v));
    }
};

void hash_edge(Hasher& h, const TableEdgeOverride& e) {
    h.flag(e.lineweight.has_value());
    if (e.lineweight) h.i64(*e.lineweight);
    h.flag(e.color.has_value());
    if (e.color) h.i64(*e.color);
    h.flag(e.visible.has_value());
    if (e.visible) h.flag(*e.visible);
}

void hash_block(Hasher& h, const Drawing& dwg, const std::string& record_handle) {
    const Block* b = block_of_record(dwg, record_handle);
    h.flag(b != nullptr);
    if (!b) return;
    h.str(b->name);
    h.u64(b->entities.size());
    h.dbl(b->base_point.x);
    h.dbl(b->base_point.y);
    Box box = block_box(dwg, *b);
    h.flag(box.valid);
    h.dbl(box.min_x);
    h.dbl(box.min_y);
    h.dbl(box.max_x);
    h.dbl(box.max_y);
}

void hash_value(Hasher& h, const TableValue& v) {
    h.i64(static_cast<int64_t>(v.type));
    h.str(table_value_text(v));
}

} // namespace

// A hash of everything the layout reads of the table and its surroundings: the table's cells and overrides, its style
// and cell styles, the blocks and fields its cells show, its TABLECONTENT's contents, the text styles, and the layer,
// color, lineweight and version the proxy graphics resolve. Not the place of the table (table_placement_stamp).
uint64_t table_layout_stamp(const Drawing& dwg, const Entity& table, const TableContentData* content) {
    if (table.type != EntityType::Table) return 0;
    const TableData& t = table.as_table();
    Hasher h;
    h.u64(static_cast<uint64_t>(dwg.version()));
    h.str(table.props.layer);
    h.color(table.props.color);
    h.str(table.props.linetype);
    h.i64(table.props.lineweight);
    h.dbl(t.horizontal_direction.x);
    h.dbl(t.horizontal_direction.y);
    h.dbl(t.horizontal_direction.z);
    h.dbl(table.props.extrusion.x);
    h.dbl(table.props.extrusion.y);
    h.dbl(table.props.extrusion.z);
    h.str(t.style_handle);
    h.u64(t.flags);
    h.u64(t.overrides.flags);
    h.u64(t.overrides.border_color_flags);
    h.u64(t.overrides.border_lineweight_flags);
    h.u64(t.overrides.border_visibility_flags);
    h.opt_dbl(t.overrides.flow_direction);
    h.opt_dbl(t.overrides.horizontal_margin);
    h.opt_dbl(t.overrides.vertical_margin);
    h.flag(t.overrides.title_suppressed.has_value());
    if (t.overrides.title_suppressed) h.flag(*t.overrides.title_suppressed);
    h.flag(t.overrides.header_suppressed.has_value());
    if (t.overrides.header_suppressed) h.flag(*t.overrides.header_suppressed);
    for (const TableRowTypeOverride& r : t.overrides.rows) h.opt_dbl(r.text_height);
    h.u64(t.row_heights.size());
    for (double v : t.row_heights) h.dbl(v);
    h.u64(t.column_widths.size());
    for (double v : t.column_widths) h.dbl(v);
    h.u64(t.cells.size());
    for (const TableCell& c : t.cells) {
        h.i64(static_cast<int64_t>(c.type));
        h.i64(c.flags);
        h.i64(c.merged);
        h.flag(c.auto_fit);
        h.i64(c.merge_columns);
        h.i64(c.merge_rows);
        h.i64(c.override_flags);
        h.i64(c.virtual_edge);
        h.dbl(c.rotation);
        hash_value(h, c.value);
        h.str(c.field_handle);
        if (!c.field_handle.empty()) h.str(table_field_text(dwg, c.field_handle));
        h.str(c.block_handle);
        if (!c.block_handle.empty()) hash_block(h, dwg, c.block_handle);
        h.dbl(c.block_scale);
        h.u64(c.attributes.size());
        for (const TableAttributeValue& a : c.attributes) {
            h.str(a.attdef_handle);
            h.str(a.text);
        }
        h.i64(c.extended_flags);
        const TableCellOverrides& o = c.overrides;
        h.flag(o.text_style.has_value());
        if (o.text_style) h.str(*o.text_style);
        h.opt_dbl(o.text_height);
        h.opt_dbl(o.alignment);
        h.flag(o.content_color.has_value());
        if (o.content_color) h.color(*o.content_color);
        h.flag(o.fill_color.has_value());
        if (o.fill_color) h.color(*o.fill_color);
        h.flag(o.fill_enabled.has_value());
        if (o.fill_enabled) h.flag(*o.fill_enabled);
        hash_edge(h, o.top);
        hash_edge(h, o.right);
        hash_edge(h, o.bottom);
        hash_edge(h, o.left);
    }
    // The style and the cell styles
    if (const Object* so = dwg.find_object(t.style_handle); so && so->type == ObjectType::TableStyle) {
        const TableStyleData& s = so->as_table_style();
        h.dbl(s.horizontal_margin);
        h.dbl(s.vertical_margin);
        h.flag(s.title_suppressed);
        h.flag(s.header_suppressed);
        h.i64(s.flow_direction);
        for (const TableStyleRow& r : s.rows) {
            h.flag(r.text_style.has_value());
            if (r.text_style) h.str(*r.text_style);
            h.dbl(r.text_height);
            h.i64(r.alignment);
            h.i64(r.text_color);
            h.i64(r.fill_color);
            h.flag(r.fill_enabled);
            for (const TableStyleBorder& b : r.borders) {
                h.i64(b.lineweight);
                h.flag(b.visible);
                h.i64(b.color);
            }
        }
        if (const Object* map = dwg.table_cell_style_map(*so)) {
            for (const TableCellStyle& cs : map->as_cell_style_map().styles) {
                h.i64(cs.id);
                h.flag(cs.format.has_margins);
                h.dbl(cs.format.margins.top);
                h.dbl(cs.format.margins.left);
                h.dbl(cs.format.margins.bottom);
                h.dbl(cs.format.margins.right);
                h.dbl(cs.format.margins.spacing_1);
                h.dbl(cs.format.margins.spacing_2);
            }
        }
    }
    // The contents of the cells that have several
    if (content) {
        for (const TableContentRow& row : content->rows) {
            for (const TableContentCell& cell : row.cells) {
                h.i64(cell.cell_flags);
                h.u64(cell.attachments.size());
                h.u64(cell.contents.size());
                h.flag(cell.format.has_margins);
                h.dbl(cell.format.margins.top);
                h.dbl(cell.format.margins.left);
                h.dbl(cell.format.margins.bottom);
                h.dbl(cell.format.margins.right);
                h.dbl(cell.format.margins.spacing_1);
                h.dbl(cell.format.margins.spacing_2);
                for (const TableCellContent& ct : cell.contents) {
                    h.i64(static_cast<int64_t>(ct.kind));
                    hash_value(h, ct.value);
                    h.str(ct.handle);
                    if (ct.kind == TableContentKind::Field) h.str(table_field_text(dwg, ct.handle));
                    if (ct.kind == TableContentKind::Block) hash_block(h, dwg, ct.handle);
                    h.flag(ct.format.has_value());
                    if (ct.format) h.dbl(ct.format->scale);
                }
            }
        }
    }
    // The text styles the text is laid out in
    for (const TextStyle& st : dwg.text_styles()) {
        h.str(st.name);
        h.str(st.font_file);
        h.str(st.big_font);
        h.str(st.font_family);
        h.dbl(st.width_factor);
        h.dbl(st.oblique_angle);
        h.dbl(st.fixed_height);
    }
    uint64_t v = h.h;
    return v == 0 ? 1 : v;
}

// A hash of where the table is: its insertion point (the translation of its proxy graphics' transform)
uint64_t table_placement_stamp(const Entity& table) {
    if (table.type != EntityType::Table) return 0;
    const TableData& t = table.as_table();
    Hasher h;
    h.dbl(t.insertion.x);
    h.dbl(t.insertion.y);
    h.dbl(t.insertion.z);
    return h.h == 0 ? 1 : h.h;
}

// The tables of a drawing just read: their stamps, so that the layout is made again only for what has changed since
void stamp_loaded_tables(Drawing& dwg) {
    auto stamp = [&](std::vector<Entity>& list) {
        for (Entity& e : list) {
            if (e.type != EntityType::Table) continue;
            const Object* co = dwg.table_content(e);
            TableData& t = e.as_table();
            t.layout_stamp = table_layout_stamp(dwg, e, co ? &co->as_table_content() : nullptr);
            t.placement_stamp = table_placement_stamp(e);
        }
    };
    stamp(dwg.entities());
    for (Block& b : dwg.blocks()) stamp(b.entities);
}

} // namespace internal
} // namespace hdxf
