#include "hdxf.hpp"
#include "text_layout.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

// MLEADERSTYLE in a drawing (plan phase 3, B37): in ACAD_MLEADERSTYLE, as AutoCAD 2017 makes
// one (jscad's empty2000/2004/2007.dxf — the same object in all three): AutoCAD's Standard
// values, an extension dictionary with the round-trip XRECORD ACAD_XREC_ROUNDTRIP
// (ACAD_ROUNDTRIP_2012_MLEADER_STYLE, 298 1: what a 2013 file has as the style's 298).

namespace hdxf {

namespace {

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// The text of a MULTILEADER laid out at the origin: its box, its baselines and how deep its
// ink reaches below a baseline
struct ContentText {
    internal::MTextSize size;
    double depth = 0.0;                 // the deepest ink below its line's baseline
    double first_bottom = 0.0;          // the first line's ink bottom, below the top
};

ContentText lay_out(const Drawing& dwg, const MLeaderContext& c) {
    internal::TextLayout layout(dwg);
    MTextData m;
    m.height = c.text_height;
    m.reference_width = c.text_width;
    m.attachment = c.text_attachment;
    m.drawing_direction = c.text_flow;
    m.content = c.text;
    m.style = c.text_style.empty() ? "Standard" : c.text_style;
    m.line_spacing_style = c.line_spacing_style;
    m.line_spacing_factor = c.line_spacing_factor;
    ContentText out;
    out.size = layout.mtext(m, true);
    const auto& lines = out.size.text_lines;
    for (const auto& run : out.size.runs) {
        if (run.bar || !run.fonts) continue;
        internal::LineMeasure ink = internal::measure_line(*run.fonts, internal::code_points(run.text), run.height,
                                                           run.width_factor, run.oblique);
        if (!ink.ink.valid) continue;
        // The line the run is on: the one whose baseline is nearest above its bottom
        double bottom = run.y + ink.ink.min_y;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (std::fabs(run.y - lines[i].baseline) > 1e-9 * std::max(1.0, std::fabs(run.y))) continue;
            out.depth = std::max(out.depth, lines[i].baseline - bottom);
            if (i == 0) out.first_bottom = std::max(out.first_bottom, -bottom);
        }
    }
    if (!lines.empty()) out.first_bottom = std::max(out.first_bottom, -lines.front().baseline);
    return out;
}

// How far below the text's top an attachment puts the landing, as AutoCAD's MULTILEADERs
// have it (the files of test/data/README.md): 0 the top; 1 the middle of the top line, half
// of the height and the deepest ink; 4 the ink's bottom and the landing gap; 5 a fifth of the
// height under the last baseline; 6 the height and a fifth, at least the first line's ink.
// 2, 3, 7 and 8 have no AutoCAD file (checkpoints): the middle of the text, the middle of the
// bottom line, the first line's ink bottom, as 5.
double attachment_depth(int attachment, const ContentText& t, double h, double gap) {
    const auto& lines = t.size.text_lines;
    // No text (Oxcart's empty MULTILEADER, attachment 5: the landing the height below)
    double last = lines.empty() ? 0.0 : -lines.back().baseline;
    double bottom = t.size.lines.valid ? -t.size.lines.min_y : h;
    switch (attachment) {
        case 0: return 0.0;
        case 1: return (h + t.depth) / 2.0;
        case 2: return bottom / 2.0;
        case 3: return last + (t.depth - h) / 2.0;
        case 4: return bottom + gap;
        case 5: case 8: return std::max(last + 0.2 * h, bottom);
        case 6: return std::max(1.2 * h, t.first_bottom);
        case 7: return t.first_bottom;
        default: return (h + t.depth) / 2.0;
    }
}

} // namespace

namespace internal {

// The text content's base point (10) and location (12) from its first leader, as AutoCAD
// places them (the files of test/data/README.md): the content is a strip — the landing gap,
// the text, the gap — beside the landing's end, the base point at its left end; the
// landing at the depth the side's attachment says, the base point at the depth of the left
// attachment (174)
void place_mleader_text(const Drawing& dwg, MLeaderData& d) {
    MLeaderContext& c = d.context;
    if (c.leaders.empty()) return;
    const MLeaderLeader& leader = c.leaders.front();
    Point3 x = c.text_direction;
    double xl = std::sqrt(x.x * x.x + x.y * x.y + x.z * x.z);
    x = xl > 0.0 ? Point3{x.x / xl, x.y / xl, x.z / xl} : Point3{1.0, 0.0, 0.0};
    Point3 n = c.text_normal;
    Point3 y = {n.y * x.z - n.z * x.y, n.z * x.x - n.x * x.z, n.x * x.y - n.y * x.x};
    bool from_left = leader.dogleg_direction.x * x.x + leader.dogleg_direction.y * x.y + leader.dogleg_direction.z * x.z >= 0.0;
    ContentText t = lay_out(dwg, c);
    // Attached vertically (2010, plan phase 4, B47): the text centered on the leader's last
    // point, its box's bottom the gap above it — or its top the gap below, for a leader from
    // above — the base point the last point (ACadSharp's B27 and Oxcart's 148, both from below:
    // exact; from above a checkpoint)
    if (leader.attachment_direction == 1) {
        Point3 from = leader.lines.empty() || leader.lines.front().vertices.empty() ? leader.last_point
                                                                                   : leader.lines.front().vertices.back();
        Point3 up = {leader.last_point.x - from.x, leader.last_point.y - from.y, leader.last_point.z - from.z};
        bool above = up.x * y.x + up.y * y.y + up.z * y.z >= 0.0;
        int column = (c.text_attachment - 1) % 3;           // MTEXT's: 0 left, 1 center, 2 right
        double u = (column - 1) * t.size.width / 2.0;
        double v = above ? c.landing_gap + t.size.height : -c.landing_gap;
        const Point3& p = leader.last_point;
        c.text_location = {p.x + x.x * u + y.x * v, p.y + x.y * u + y.y * v, p.z + x.z * u + y.z * v};
        c.base_point = p;
        return;
    }
    double h = c.text_height, gap = c.landing_gap;
    double width = t.size.lines.valid ? t.size.lines.max_x - t.size.lines.min_x : 0.0;
    double min_x = t.size.lines.valid ? t.size.lines.min_x : 0.0;
    double len = leader.dogleg_length;
    Point3 end = {leader.last_point.x + leader.dogleg_direction.x * len, leader.last_point.y + leader.dogleg_direction.y * len,
                  leader.last_point.z + leader.dogleg_direction.z * len};
    double strip = from_left ? 0.0 : -(2.0 * gap + width);
    int used = from_left ? c.left_attachment : c.right_attachment;
    double u = strip + gap - min_x;
    double v = attachment_depth(used, t, h, gap);
    auto at = [&](double a, double b) {
        return Point3{end.x + x.x * a + y.x * b, end.y + x.y * a + y.y * b, end.z + x.z * a + y.z * b};
    };
    c.text_location = at(u, v);
    c.base_point = at(strip, v - attachment_depth(c.left_attachment, t, h, gap));
}

} // namespace internal

// The MULTILEADER's common part from the style: what AutoCAD copies into a new one
static MLeaderData mleader_from_style(const MLeaderStyleData& s, const std::string& style, const std::string& handle) {
    MLeaderData d;
    d.style = style;
    d.style_handle = handle;
    d.leader_type = s.leader_type;
    d.leader_color = s.leader_color;
    d.leader_linetype = s.leader_linetype.empty() ? "ByBlock" : s.leader_linetype;
    d.leader_lineweight = s.leader_lineweight;
    d.landing = s.landing;
    d.dogleg = s.dogleg;
    d.dogleg_length = s.dogleg_length;
    d.arrow_block = s.arrow_block;
    d.arrow_size = s.arrow_size;
    d.content_type = s.content_type;
    d.text_style = s.text_style;
    d.text_left_attachment = s.text_left_attachment;
    d.text_right_attachment = s.text_right_attachment;
    d.text_angle_type = s.text_angle_type;
    d.text_alignment = 0;
    d.text_color = s.text_color;
    d.text_frame = s.text_frame;
    d.block = s.block;
    d.block_color = s.block_color;
    d.block_scale = s.block_scale;
    d.block_rotation = s.block_rotation;
    d.block_connection = s.block_connection;
    d.annotation_scale = s.annotative;
    d.scale = s.scale;
    d.text_attachment_direction = s.text_attachment_direction;
    d.bottom_attachment_direction = s.bottom_attachment_direction;
    d.top_attachment_direction = s.top_attachment_direction;
    // 295 as the style's 298 (LibreDWG's example_2013: the style's 1, its multileader's 1;
    // ACadSharp's: 0 and 0 in all 15; B58)
    d.text_extended = s.text_extended;
    MLeaderContext& c = d.context;
    c.scale = s.scale;
    c.text_height = s.text_height * s.scale;
    c.arrow_size = s.arrow_size * s.scale;
    c.landing_gap = s.landing_gap * s.scale;
    c.left_attachment = s.text_left_attachment;
    c.right_attachment = s.text_right_attachment;
    c.text_style = s.text_style;
    c.text_flow = 5;
    c.background_color = CmColor::by_layer();
    c.background_scale = 0.0;
    c.bottom_attachment_direction = s.bottom_attachment_direction;
    c.top_attachment_direction = s.top_attachment_direction;
    return d;
}

// The style for a new MULTILEADER: of the name in ACAD_MLEADERSTYLE; Standard is made
static const Object* mleader_style_object(Drawing& dwg, const std::string& name) {
    if (same_name(name, "Standard")) dwg.add_mleader_style("Standard");
    for (const auto& o : dwg.objects()) {
        if (o.type != ObjectType::Dictionary || o.owner != "0") continue;
        for (const auto& entry : o.as_dictionary().entries) {
            if (entry.name != "ACAD_MLEADERSTYLE") continue;
            const Object* dict = dwg.find_object(entry.handle);
            if (!dict || dict->type != ObjectType::Dictionary) return nullptr;
            for (const auto& e : dict->as_dictionary().entries) {
                const Object* style = dwg.find_object(e.handle);
                if (same_name(e.name, name) && style && style->type == ObjectType::MLeaderStyle) return style;
            }
        }
    }
    return nullptr;
}

// The leader: its line's vertices up to the last point, the landing toward the side away from
// the leader's last segment (along X)
static MLeaderLeader mleader_leader(const std::vector<Point3>& points, double dogleg_length, bool& from_left) {
    MLeaderLeader leader;
    leader.last_point = points.back();
    double dx = points.size() >= 2 ? points.back().x - points[points.size() - 2].x : 1.0;
    from_left = dx >= 0.0;
    leader.dogleg_direction = {from_left ? 1.0 : -1.0, 0.0, 0.0};
    leader.dogleg_length = dogleg_length;
    MLeaderLine line;
    line.vertices.assign(points.begin(), points.end() - 1);
    if (line.vertices.empty()) line.vertices.push_back(points.back());
    leader.lines.push_back(line);
    return leader;
}

Entity& Drawing::add_mleader(const std::vector<Point3>& points, const std::string& text, const std::string& style,
                             const std::string& layer) {
    const Object* so = mleader_style_object(*this, style);
    MLeaderStyleData s = so ? so->as_mleader_style() : MLeaderStyleData{};
    MLeaderData d = mleader_from_style(s, style, so ? so->handle : std::string());
    // A new text MULTILEADER's overrides as AutoCAD writes them (the content type, the text
    // alignment and 0x40000: 279552 in every file of AutoCAD's MLEADER command)
    d.override_flags = 279552;
    d.content_type = 2;
    MLeaderContext& c = d.context;
    c.has_text = true;
    c.has_block = false;
    c.text = text;
    std::vector<Point3> leader_points = points.empty() ? std::vector<Point3>{Point3{}} : points;
    bool from_left = true;
    MLeaderLeader leader = mleader_leader(leader_points, s.dogleg_length * s.scale, from_left);
    c.plane_origin = leader_points.front();
    // The text justified by the side (top left, or top right), unless the style keeps it left;
    // attached vertically (2010, B47): no side — the style's alignment, along the top
    bool left = from_left || s.text_always_left;
    c.text_attachment = left ? 1 : 3;
    c.text_align = left ? 0 : 2;
    leader.attachment_direction = s.text_attachment_direction;
    if (leader.attachment_direction == 1) {
        c.text_align = s.text_alignment;
        c.text_attachment = 1 + s.text_alignment;
    }
    // The text color: the style's, ByBlock as the entity's (ByLayer)
    c.text_color = s.text_color.method() == 0xC1 ? CmColor::by_layer() : s.text_color;

    c.leaders.push_back(leader);
    internal::place_mleader_text(*this, d);

    Entity e;
    e.type = EntityType::MLeader;
    e.props.layer = layer;
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_mleader_block(const std::vector<Point3>& points, const std::string& block,
                                   const std::string& style, const std::string& layer) {
    const Object* so = mleader_style_object(*this, style);
    MLeaderStyleData s = so ? so->as_mleader_style() : MLeaderStyleData{};
    MLeaderData d = mleader_from_style(s, style, so ? so->handle : std::string());
    // A block MULTILEADER's overrides as AutoCAD writes them (Oxcart's balloons: 6816768)
    d.override_flags = 6816768;
    d.content_type = 1;
    d.block = block;
    MLeaderContext& c = d.context;
    c.has_text = false;
    c.has_block = true;
    c.text = "";
    c.block = block;
    c.block_connection = s.block_connection;
    c.block_scale = {s.block_scale.x * s.scale, s.block_scale.y * s.scale, s.block_scale.z * s.scale};
    c.block_rotation = s.block_rotation;
    c.block_color = s.block_color;
    std::vector<Point3> leader_points = points.empty() ? std::vector<Point3>{Point3{}} : points;
    bool from_left = true;
    MLeaderLeader leader = mleader_leader(leader_points, s.dogleg_length * s.scale, from_left);
    c.plane_origin = leader_points.front();
    Point3 position = {leader.last_point.x + leader.dogleg_direction.x * leader.dogleg_length, leader.last_point.y,
                       leader.last_point.z};
    c.block_position = position;
    c.base_point = position;
    // The block's transform, row by row: turned and scaled about its base point at the position
    const Block* b = find_block(block);
    Point3 base = b ? b->base_point : Point3{};
    double co = std::cos(c.block_rotation), si = std::sin(c.block_rotation);
    double sx = c.block_scale.x, sy = c.block_scale.y, sz = c.block_scale.z;
    double tx = position.x - (co * sx * base.x - si * sy * base.y);
    double ty = position.y - (si * sx * base.x + co * sy * base.y);
    double tz = position.z - sz * base.z;
    c.block_matrix = {co * sx, -si * sy, 0.0, tx, si * sx, co * sy, 0.0, ty, 0.0, 0.0, sz, tz, 0.0, 0.0, 0.0, 1.0};
    c.leaders.push_back(leader);
    // Its attributes with their default values, linked to their ATTDEFs by handle (330) as
    // AutoCAD keeps them: a definition built in memory gets its handle now, so the link holds
    // from the first save on
    if (Block* defs = find_block(block)) {
        scan_handles();
        for (auto& be : defs->entities) {
            if (be.type == EntityType::Attdef && be.props.handle.empty()) be.props.handle = next_handle();
        }
    }
    if (b) {
        int index = 0;
        for (const auto& be : b->entities) {
            // A constant attribute is part of the block, with no value of its own
            if (be.type != EntityType::Attdef || (std::get<AttdefData>(be.data).flags & 2)) continue;
            MLeaderAttribute a;
            a.attdef_handle = be.props.handle;
            a.index = index++;
            a.text = std::get<AttdefData>(be.data).default_value;
            d.attributes.push_back(a);
        }
    }

    Entity e;
    e.type = EntityType::MLeader;
    e.props.layer = layer;
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Object& Drawing::add_mleader_style(const std::string& name) {
    ensure_r2000_structure();
    std::string dict_handle = named_dictionary("ACAD_MLEADERSTYLE");
    if (Object* dict = find_object(dict_handle)) {
        for (const auto& e : dict->as_dictionary().entries) {
            Object* existing = find_object(e.handle);
            if (same_name(e.name, name) && existing && existing->type == ObjectType::MLeaderStyle) return *existing;
        }
    }
    scan_handles();
    std::string handle = next_handle();
    if (Object* dict = find_object(dict_handle)) {
        auto& entries = dict->as_dictionary().entries;
        auto at = std::find_if(entries.begin(), entries.end(), [&](const DictionaryEntry& e) { return e.name > name; });
        entries.insert(at, {name, handle, false});
    }

    // AutoCAD's values: 295 and 298 1 (AutoCAD 2017's new drawing as 2013, jscad's empty2013);
    // before 2013 the 298 is the round-trip XRECORD sync_roundtrip_records() keeps (B59, B60)
    Object style;
    style.type = ObjectType::MLeaderStyle;
    style.handle = handle;
    style.owner = dict_handle;
    style.reactors = {dict_handle};
    MLeaderStyleData data;
    data.overwrite_property = true;
    data.text_extended = true;
    style.data = data;
    objects_.push_back(std::move(style));
    sync_roundtrip_records();

    // Standard is the current multileader style: DICTIONARYVAR CMLEADERSTYLE in
    // AcDbVariableDictionary, as AutoCAD's new drawing has it (jscad's empty2007: "STANDARD")
    if (same_name(name, "Standard")) {
        std::string vars = named_dictionary("AcDbVariableDictionary");
        if (Object* dict = find_object(vars); dict && dict->type == ObjectType::Dictionary) {
            auto& entries = dict->as_dictionary().entries;
            bool present = std::any_of(entries.begin(), entries.end(),
                                       [](const DictionaryEntry& e) { return e.name == "CMLEADERSTYLE"; });
            if (!present) {
                std::string var = next_handle();
                auto at = std::find_if(entries.begin(), entries.end(),
                                       [](const DictionaryEntry& e) { return e.name > "CMLEADERSTYLE"; });
                entries.insert(at, {"CMLEADERSTYLE", var, false});
                Object v;
                v.type = ObjectType::DictionaryVar;
                v.handle = var;
                v.owner = vars;
                v.reactors = {vars};
                v.data = DictionaryVarData{0, "STANDARD"};
                objects_.push_back(std::move(v));
            }
        }
    }
    return *find_object(handle);
}

} // namespace hdxf
