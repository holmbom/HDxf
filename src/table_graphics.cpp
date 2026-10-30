#include "TableLayout.hpp"
#include "extents.hpp"
#include "fonts.hpp"
#include "handles.hpp"
#include "proxy_graphics.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// The drawing of a table (phase 9, step 9.5; plan B106, B107, B108): its *T block and its proxy graphics made from the
// layout (table_layout.cpp), its extents, and the connection to the synchronisation of table_sync.cpp.
//
// The proxy graphics are AcGi's record of the table: the transform of the table, then each entity of the *T block as
// AutoCAD draws it (all seen in doc/facit/acad_*_dxf_tables.dxf, doc/facit/acad_*_dxf_table_layout*.dxf and ACadSharp's
// sample_AC1015 ... AC1032):
//   a fill     color, lineweight, linetype, fill on, linetype, marker -1, the shell of its four corners, fill on
//   a text     color, lineweight, linetype, then per line of the MTEXT: marker (0, a field's 2000000000), its TEXT
//   a block    color, lineweight, linetype, the block's transform, then each entity of it with its traits (layer,
//              linetype, color, 24, 26, lineweight, 34, 51, thickness), its marker and its graphics, the transform's end
//   a border   color, lineweight, linetype, the polyline of the two ends with the normal (DXF 2000: a plain polyline)
//   the marker the same, in Defpoints' color (8), lineweight 0
// A fill, a border and a text outside a block come in the same order as the *T block. A table that is turned has the
// turn in the geometry of its block (AutoCAD's ROTATE), so the transform is only where the table is.

namespace hdxf {
namespace internal {

namespace {

using Matrix = std::array<double, 16>;

constexpr int kLinetypeByLayer = 0x7FFF;
constexpr int kLinetypeByBlock = 0x7FFE;
constexpr int kFieldMarker = 2000000000;

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

Point3 unit(const Point3& a) {
    double len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    return len > 0.0 ? Point3{a.x / len, a.y / len, a.z / len} : Point3{0.0, 0.0, 1.0};
}

// The table's transform: the axes of its plane (the world's for a table on it) and where it is. The table's own
// turn (its horizontal direction) is in the *T block's geometry, as AutoCAD saves it, not in the transform.
Matrix table_matrix(const Entity& table) {
    const TableData& d = table.as_table();
    Point3 ax, ay;
    ecs::arbitrary_axis(table.props.extrusion, ax, ay);
    const Point3 z = unit(table.props.extrusion);
    return {ax.x, ay.x, z.x, d.insertion.x,
            ax.y, ay.y, z.y, d.insertion.y,
            ax.z, ay.z, z.z, d.insertion.z,
            0.0, 0.0, 0.0, 1.0};
}

// An INSERT's transform: scale, rotation about the normal, translation to the insertion point (the block's base
// point maps to it)
Matrix insert_matrix(const InsertData& d, const Point3& base) {
    const double r = d.rotation * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    const double a = c * d.x_scale, b = -s * d.y_scale, e = s * d.x_scale, f = c * d.y_scale, g = d.z_scale;
    const double tx = d.insertion_point.x - (a * base.x + b * base.y);
    const double ty = d.insertion_point.y - (e * base.x + f * base.y);
    const double tz = d.insertion_point.z - g * base.z;
    return {a, b, 0.0, tx, e, f, 0.0, ty, 0.0, 0.0, g, tz, 0.0, 0.0, 0.0, 1.0};
}

class TableProxy {
public:
    TableProxy(const Drawing& dwg, const Entity& table, const std::set<std::pair<std::string, size_t>>& fields,
               const std::map<std::pair<std::string, size_t>, MTextSize>& sizes, TextLayout& text, bool unicode)
        : dwg_(dwg), table_(table), fields_(fields), sizes_(sizes), text_(text), unicode_(unicode),
          code_page_(drawing_code_page(dwg)) {
        // The layers and linetypes in the order the file's tables have them: proxy graphics name them by index
        if (!find_layer("0")) layers_.push_back("0");
        for (const Layer& l : dwg.layers()) layers_.push_back(l.name);
        for (const char* name : {"ByBlock", "ByLayer", "Continuous"}) {
            bool has = false;
            for (const auto& lt : dwg.linetypes()) has = has || same_name(lt.name, name);
            if (!has) linetypes_.push_back(name);
        }
        for (const auto& lt : dwg.linetypes()) linetypes_.push_back(lt.name);
    }

    std::vector<uint8_t> run(const std::vector<Entity>& block) {
        g_.push_transform(table_matrix(table_));
        for (size_t i = 0; i < block.size(); ++i) draw_top(block[i], i);
        g_.pop_transform();
        return g_.bytes();
    }

private:
    const Layer* find_layer(const std::string& name) const {
        for (const Layer& l : dwg_.layers()) if (same_name(l.name, name)) return &l;
        return nullptr;
    }

    int layer_index(const std::string& name) const {
        for (size_t i = 0; i < layers_.size(); ++i) if (same_name(layers_[i], name)) return static_cast<int>(i);
        return 0;
    }

    // A linetype name's number among the linetypes other than ByLayer and ByBlock
    int linetype_index(const std::string& name) const {
        int i = 0;
        for (const auto& lt : linetypes_) {
            if (same_name(lt, "ByLayer") || same_name(lt, "ByBlock")) continue;
            if (same_name(lt, name)) return i;
            ++i;
        }
        return 0;
    }

    int32_t layer_color(const std::string& name) const {
        const Layer* l = find_layer(name);
        Color c = l ? l->color : Color::white();
        if (!c.has_rgb()) c = Color::from_aci(std::abs(c.aci));
        return CmColor::from_color(c).raw;
    }

    // The table's own color, lineweight and linetype: what ByBlock is in its block
    int32_t host_color() const {
        const Color& c = table_.props.color;
        if (c.is_by_layer() || c.is_by_block()) return layer_color(table_.props.layer);
        return CmColor::from_color(c).raw;
    }

    int layer_lineweight(const std::string& name) const {
        const Layer* l = find_layer(name);
        int lw = l ? l->lineweight : -3;
        return lw == -1 || lw == -2 ? -3 : lw;
    }

    int host_lineweight() const {
        int lw = table_.props.lineweight;
        if (lw == -1 || lw == -2) return layer_lineweight(table_.props.layer);
        return lw;
    }

    std::string layer_linetype(const std::string& name) const {
        const Layer* l = find_layer(name);
        return l ? l->linetype : std::string("Continuous");
    }

    std::string host_linetype() const {
        const std::string& lt = table_.props.linetype;
        if (lt.empty() || same_name(lt, "ByLayer") || same_name(lt, "ByBlock")) return layer_linetype(table_.props.layer);
        return lt;
    }

    int32_t drawn_color(const EntityProperties& p) const {
        const Color& c = p.color;
        if (c.is_by_layer()) return layer_color(p.layer);
        if (c.is_by_block()) return host_color();
        return CmColor::from_color(c).raw;
    }

    int drawn_lineweight(const EntityProperties& p, bool fill = false) const {
        int lw = p.lineweight;
        if (lw == -2) return host_lineweight();
        // ByLayer is the table's own lineweight when it has one (AutoCAD: the table set to 0.5 mm draws its text so;
        // not its fills, which have their layer's)
        if (lw == -1) return !fill && table_.props.lineweight >= 0 ? table_.props.lineweight : layer_lineweight(p.layer);
        return lw;
    }

    int drawn_linetype(const EntityProperties& p) const {
        const std::string& lt = p.linetype;
        if (lt.empty() || same_name(lt, "ByLayer")) return linetype_index(layer_linetype(p.layer));
        if (same_name(lt, "ByBlock")) return linetype_index(host_linetype());
        return linetype_index(lt);
    }

    // The three commands AutoCAD gives each entity of the block before its drawing
    void traits(const EntityProperties& p, bool fill = false) {
        g_.true_color(drawn_color(p));
        g_.lineweight(drawn_lineweight(p, fill));
        g_.linetype(drawn_linetype(p));
    }

    static void set_font(ProxyText& t, const StyleFonts* f) {
        if (!f) return;
        if (!f->family.empty()) {
            t.face = f->family;
            t.bold = f->bold;
            t.italic = f->italic;
            t.pitch_and_family = f->pitch_and_family;
        } else if (f->font && !f->font->is_shape_font()) {
            FontFace face = f->font->face();
            t.face = face.family;
            t.bold = face.bold;
            t.italic = face.italic;
            t.pitch_and_family = face.pitch_and_family;
        } else {
            t.font = f->file;
            if (!t.font.empty() && t.font.find('.') == std::string::npos) t.font += ".shx";
        }
        t.big_font = f->big_font_file;
    }

    // An MTEXT: a TEXT per run of its lines (a stack's bar a polyline), each after its marker. The runs are the layout's
    // when it made them (AutoCAD does not wrap an anonymous block's MTEXT again at the width it was given)
    void draw_mtext(const Entity& e, const std::string& block, size_t index) {
        const int marker = marker_of(block, index);
        const MTextData& m = e.as_mtext();
        auto laid = sizes_.find({block, index});
        MTextSize s = laid != sizes_.end() ? laid->second : text_.mtext(m, true);
        Point3 n = unit(e.props.extrusion);
        Point3 x = unit(m.x_axis_direction);
        Point3 y = unit({n.y * x.z - n.z * x.y, n.z * x.x - n.x * x.z, n.x * x.y - n.y * x.x});
        const int32_t base = drawn_color(e.props);
        // A color of the MTEXT's own (not ByBlock, not ByLayer) is set again before each line (AutoCAD's 528: the
        // two lines of its merged cell, color 7)
        const bool own_color = !e.props.color.is_by_block() && !e.props.color.is_by_layer();
        int32_t current = base;
        for (const auto& run : s.runs) {
            int32_t want = run.has_color ? CmColor::from_color(run.color).raw : base;
            if (run.has_color && (CmColor{want}.method() == 0xC1 || CmColor{want}.method() == 0xC0)) want = base;
            if (want != current || (own_color && !run.bar)) {
                g_.true_color(want);
                current = want;
            }
            Point3 at = {m.insertion_point.x + x.x * run.x + y.x * run.y, m.insertion_point.y + x.y * run.x + y.y * run.y,
                         m.insertion_point.z + x.z * run.x + y.z * run.y};
            if (run.bar) {
                g_.linetype(linetype_index("Continuous"));
                Point3 to = {m.insertion_point.x + x.x * run.x2 + y.x * run.y2, m.insertion_point.y + x.y * run.x2 + y.y * run.y2,
                             m.insertion_point.z + x.z * run.x2 + y.z * run.y2};
                g_.polyline({at, to});
                continue;
            }
            g_.marker(marker);
            ProxyText t;
            t.position = at;
            t.normal = n;
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

    // A filled quadrilateral (a SOLID) as a shell of the perimeter
    void draw_filled_quad(const SolidData& q) {
        g_.fill(true);
        g_.linetype(linetype_index("Continuous"));
        g_.marker(-1);
        g_.shell({q.p0, q.p1, q.p3, q.p2}, {4, 0, 1, 2, 3}, {0x60, 1, 2, 3, 4, 1, 1, 1, 1, 0, 0});
        g_.fill(true);
    }

    // The entities of the *T block, in its order
    void draw_top(const Entity& e, size_t index) {
        traits(e.props, e.type == EntityType::Solid);
        switch (e.type) {
            case EntityType::Solid:
                draw_filled_quad(e.as_solid());
                break;
            case EntityType::MText:
                draw_mtext(e, std::string(), index);
                break;
            case EntityType::Insert:
                draw_insert(e);
                break;
            case EntityType::Line:
                draw_line(e.as_line(), {0.0, 0.0, 1.0});
                break;
            default:
                break;
        }
    }

    // A line: a polyline with its normal; in DXF 2000 a plain polyline (AutoCAD 2000's own files)
    void draw_line(const LineData& l, const Point3& normal) {
        if (dwg_.version() < DxfVersion::R2004) g_.polyline({l.start, l.end});
        else g_.polyline_normal({l.start, l.end}, normal);
    }

    int marker_of(const std::string& block, size_t index) const {
        return fields_.count({block, index}) ? kFieldMarker : 0;
    }

    // An INSERT: its block in its transform; each entity of the block with its traits first
    void draw_insert(const Entity& e) {
        const InsertData& d = e.as_insert();
        const Block* b = dwg_.find_block(d.block_name);
        if (b && std::find(open_.begin(), open_.end(), b) == open_.end()) {
            open_.push_back(b);
            g_.push_transform(insert_matrix(d, b->base_point));
            for (size_t i = 0; i < b->entities.size(); ++i) draw_nested(b->entities[i], b->name, i);
            g_.pop_transform();
            open_.pop_back();
        }
        for (const auto& a : d.attributes) {
            if (a.data.flags & 1) continue;
            g_.true_color(drawn_color(a.props));
            g_.lineweight(drawn_lineweight(a.props));
            draw_text_line(a.data.style, a.data.value, a.data.height, a.data.width_factor, a.data.oblique_angle,
                           a.data.rotation, a.data.generation_flags, a.data.h_justify, a.data.v_justify,
                           a.data.start_point, a.data.alignment_point, a.props.extrusion);
        }
    }

    // A line of TEXT in the WCS (an ATTRIB of a block cell)
    void draw_text_line(const std::string& style, const std::string& content, double height, double width_factor,
                        double oblique, double rotation, int generation, int hj, int vj, const Point3& position,
                        const Point3& alignment, const Point3& extrusion) {
        TextLine line{style, content, height, width_factor, oblique, rotation, generation, hj, vj, position, alignment};
        TextPen pen = text_.text_pen(line);
        if (!pen.font) return;
        const double r = pen.rotation * M_PI / 180.0;
        Point3 ax, ay;
        ecs::arbitrary_axis(extrusion, ax, ay);
        Point3 dir = {ax.x * std::cos(r) + ay.x * std::sin(r), ax.y * std::cos(r) + ay.y * std::sin(r),
                      ax.z * std::cos(r) + ay.z * std::sin(r)};
        ProxyText t;
        t.position = ecs::ecs_to_wcs(pen.pen, extrusion);
        t.normal = unit(extrusion);
        t.direction = unit(dir);
        t.text = content;
        t.height = pen.height;
        t.width_factor = pen.width_factor;
        t.oblique = oblique * M_PI / 180.0;
        t.backward = (generation & 2) != 0;
        t.upside_down = (generation & 4) != 0;
        set_font(t, &text_.fonts(style));
        g_.text(t, unicode_, code_page_);
    }

    // The traits AutoCAD records before an entity inside an INSERT
    void nested_traits(const Entity& e, bool complex) {
        const EntityProperties& p = e.props;
        g_.layer(layer_index(p.layer));
        if (p.linetype.empty() || same_name(p.linetype, "ByLayer")) g_.linetype(kLinetypeByLayer);
        else if (same_name(p.linetype, "ByBlock")) g_.linetype(kLinetypeByBlock);
        else g_.linetype(linetype_index(p.linetype));
        g_.color(p.color.aci);
        g_.linetype_scale(p.linetype_scale);
        g_.command_26(0.0);
        g_.lineweight(p.lineweight);
        if (dwg_.version() >= DxfVersion::R2004) g_.command_34(0x96);
        g_.command_51(complex ? 1 : 0);
        g_.thickness(p.thickness);
    }

    void draw_nested(const Entity& e, const std::string& block_name, size_t index) {
        const bool text = e.type == EntityType::MText || e.type == EntityType::Text;
        nested_traits(e, text || e.type == EntityType::Insert);
        const Point3& n = e.props.extrusion;
        switch (e.type) {
            case EntityType::Circle: {
                const CircleData& c = e.as_circle();
                g_.fill(false);
                g_.circle(ecs::ecs_to_wcs(c.center, n), c.radius, n);
                g_.fill(true);
                break;
            }
            case EntityType::Arc: {
                const ArcData& a = e.as_arc();
                Point3 ax, ay;
                ecs::arbitrary_axis(n, ax, ay);
                const double a0 = a.start_angle * M_PI / 180.0;
                double sweep = (a.end_angle - a.start_angle) * M_PI / 180.0;
                while (sweep <= 0.0) sweep += 2.0 * M_PI;
                Point3 start = {(ax.x * std::cos(a0) + ay.x * std::sin(a0)) * a.radius,
                                (ax.y * std::cos(a0) + ay.y * std::sin(a0)) * a.radius,
                                (ax.z * std::cos(a0) + ay.z * std::sin(a0)) * a.radius};
                g_.fill(false);
                g_.arc(ecs::ecs_to_wcs(a.center, n), a.radius, n, start, sweep, 0);
                g_.fill(true);
                break;
            }
            case EntityType::Line:
                draw_line(e.as_line(), n);
                break;
            case EntityType::Point: {
                const Point3& p = e.as_point().location;
                g_.linetype(linetype_index("Continuous"));
                g_.fill(false);
                g_.polyline_normal({p, p}, n);
                g_.fill(true);
                break;
            }
            case EntityType::LwPolyline:
                g_.fill(true);
                g_.lwpolyline(lwpline_data(e.as_lwpolyline(), e.props.thickness, n));
                break;
            case EntityType::Solid:
                draw_filled_quad({ecs::ecs_to_wcs(e.as_solid().p0, n), ecs::ecs_to_wcs(e.as_solid().p1, n),
                                  ecs::ecs_to_wcs(e.as_solid().p2, n), ecs::ecs_to_wcs(e.as_solid().p3, n)});
                break;
            case EntityType::Text: {
                const TextData& d = e.as_text();
                draw_text_line(d.style, d.content, d.height, d.width_factor, d.oblique_angle, d.rotation,
                               d.generation_flags, d.h_justify, d.v_justify, d.position, d.alignment_point, n);
                break;
            }
            case EntityType::Attdef: {
                const AttdefData& d = e.as_attdef();
                if (!(d.flags & 2)) break;
                draw_text_line(d.style, d.default_value, d.height, d.width_factor, d.oblique_angle, d.rotation,
                               d.generation_flags, d.h_justify, d.v_justify, d.start_point, d.alignment_point, n);
                break;
            }
            case EntityType::MText:
                draw_mtext(e, block_name, index);
                break;
            case EntityType::Insert:
                draw_insert(e);
                break;
            default:
                // A kind of entity the table's proxy graphics have no observed form for: not drawn there (the block
                // still has it)
                break;
        }
    }

    const Drawing& dwg_;
    const Entity& table_;
    const std::set<std::pair<std::string, size_t>>& fields_;
    const std::map<std::pair<std::string, size_t>, MTextSize>& sizes_;
    TextLayout& text_;
    bool unicode_;
    int code_page_;
    ProxyGraphics g_;
    std::vector<std::string> layers_;
    std::vector<std::string> linetypes_;
    std::vector<const Block*> open_;
};

// The entities of a block laid out again take what the old ones of the same cells had that is not geometry: their handle,
// their extension dictionary (a cell's FIELD lives in its text's: ACAD_FIELD, TEXT, then the field the table's cell
// points at), their reactors and their preserved record (so the file keeps what did not change). The texts match in
// order when there are as many as before, else by their text.
void reuse_entities(const std::vector<Entity>& old_entities, std::vector<Entity>& fresh) {
    std::vector<const Entity*> old_texts;
    std::vector<size_t> new_texts;
    for (const Entity& e : old_entities) if (e.type == EntityType::MText) old_texts.push_back(&e);
    for (size_t i = 0; i < fresh.size(); ++i) if (fresh[i].type == EntityType::MText) new_texts.push_back(i);
    std::vector<const Entity*> matched(new_texts.size(), nullptr);
    if (old_texts.size() == new_texts.size()) {
        for (size_t k = 0; k < new_texts.size(); ++k) matched[k] = old_texts[k];
    } else {
        std::vector<bool> used(old_texts.size(), false);
        for (size_t k = 0; k < new_texts.size(); ++k) {
            for (size_t j = 0; j < old_texts.size(); ++j) {
                if (!used[j] && old_texts[j]->as_mtext().content == fresh[new_texts[k]].as_mtext().content) {
                    used[j] = true;
                    matched[k] = old_texts[j];
                    break;
                }
            }
        }
    }
    for (size_t k = 0; k < new_texts.size(); ++k) {
        if (!matched[k]) continue;
        Entity& e = fresh[new_texts[k]];
        e.props.handle = matched[k]->props.handle;
        e.props.xdictionary = matched[k]->props.xdictionary;
        e.props.reactors = matched[k]->props.reactors;
        e.raw = matched[k]->raw;
    }
}

bool has_layer(const Drawing& dwg, const std::string& name) {
    for (const Layer& l : dwg.layers()) if (same_name(l.name, name)) return true;
    return false;
}

} // namespace

std::vector<uint8_t> table_proxy_graphics(const Drawing& dwg, const Entity& table, const std::vector<Entity>& block,
                                          const std::set<std::pair<std::string, size_t>>& field_texts,
                                          const std::map<std::pair<std::string, size_t>, MTextSize>& text_sizes,
                                          TextLayout& text, bool unicode) {
    TableProxy proxy(dwg, table, field_texts, text_sizes, text, unicode);
    return proxy.run(block);
}

// The stream's first command is the table's transform (29, 128 bytes of matrix): changed in place
bool table_proxy_retarget(const Entity& table, std::vector<uint8_t>& stream) {
    // total size, count, then command size, type, 16 doubles
    if (stream.size() < 8 + 8 + 128) return false;
    auto at = [&](size_t o) {
        uint32_t u = 0;
        for (int i = 0; i < 4; ++i) u |= static_cast<uint32_t>(stream[o + i]) << (8 * i);
        return u;
    };
    if (at(8) != 136 || at(12) != 29) return false;
    Matrix m = table_matrix(table);
    for (size_t i = 0; i < 16; ++i) {
        uint64_t u = 0;
        std::memcpy(&u, &m[i], 8);
        for (int k = 0; k < 8; ++k) stream[16 + i * 8 + static_cast<size_t>(k)] = static_cast<uint8_t>(u >> (8 * k));
    }
    return true;
}

bool table_block_extents(const Entity& table, TypedExtentsSink& sink) {
    if (table.type != EntityType::Table || !sink.drawing) return false;
    const TableData& t = table.as_table();
    const Block* b = sink.drawing->find_block(t.block_name);
    if (!b || b->entities.empty()) return false;
    // As an INSERT of the block at the table's place would have them (the block has the table's turn already)
    Entity ins;
    ins.type = EntityType::Insert;
    ins.props = table.props;
    InsertData d;
    d.block_name = t.block_name;
    const Point3 n = unit(table.props.extrusion);
    d.insertion_point = ecs::wcs_to_ecs(t.insertion, n);
    ins.data = std::move(d);
    Extents box;
    entity_extents(*sink.drawing, ins, box);
    if (box.empty()) return false;
    for (double x : {box.min.x, box.max.x}) {
        for (double y : {box.min.y, box.max.y}) sink.add_wcs({x, y, box.min.z});
    }
    if (box.max.z != box.min.z) sink.add_wcs({box.min.x, box.min.y, box.max.z});
    return true;
}

void apply_table_layout(Drawing& dwg, Entity& table, TableContentData* content, TableGeometryData* geometry,
                        std::vector<std::string>& warnings) {
    if (table.type != EntityType::Table) return;
    TableData& t = table.as_table();
    // Laid out and nothing it reads has changed: as it is; if it has been moved, its proxy graphics follow
    const uint64_t place = table_placement_stamp(table);
    if (t.layout_stamp != 0 && t.layout_stamp == table_layout_stamp(dwg, table, content)) {
        if (t.placement_stamp != place) {
            if (!t.proxy_graphics.empty()) table_proxy_retarget(table, t.proxy_graphics);
            t.placement_stamp = place;
        }
        return;
    }
    TextLayout text(dwg);
    TableLayout L;
    if (!layout_table(dwg, table, content, text, L)) {
        for (const std::string& w : L.warnings) warnings.push_back(w);
        return;
    }
    for (const std::string& w : L.warnings) warnings.push_back(w);

    // The Defpoints layer the marker is on
    if (!has_layer(dwg, "Defpoints")) {
        Layer l;
        l.name = "Defpoints";
        l.color = Color::white();
        l.linetype = "CONTINUOUS";
        l.plot = false;
        dwg.layers().push_back(l);
    }

    t.row_heights = L.row_heights;
    if (Block* b = dwg.find_block(t.block_name)) {
        reuse_entities(b->entities, L.block);
        b->entities = L.block;
    }
    for (TableCellBlock& cb : L.cell_blocks) {
        if (Block* b = dwg.find_block(cb.name)) {
            reuse_entities(b->entities, cb.entities);
            b->entities = cb.entities;
        }
    }
    if (geometry) {
        // A cell's geometry keeps the pointer it had
        for (size_t i = 0; i < L.geometry.cells.size() && i < geometry->cells.size(); ++i) {
            L.geometry.cells[i].handle = geometry->cells[i].handle.empty() ? "0" : geometry->cells[i].handle;
        }
        *geometry = L.geometry;
    }
    if (content) {
        for (size_t r = 0; r < content->rows.size() && r < L.row_heights.size(); ++r) content->rows[r].height = L.row_heights[r];
    }
    t.proxy_graphics = table_proxy_graphics(dwg, table, L.block, L.field_texts, L.text_sizes, text, dwg.version() >= DxfVersion::R2007);
    for (const std::string& w : text.warnings()) {
        if (std::find(warnings.begin(), warnings.end(), w) == warnings.end()) warnings.push_back(w);
    }
    t.layout_stamp = table_layout_stamp(dwg, table, content);
    t.placement_stamp = place;
}

} // namespace internal
} // namespace hdxf
