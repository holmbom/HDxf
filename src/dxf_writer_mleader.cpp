#include "dxf_writer.hpp"
#include "mleader.hpp"
#include "handles.hpp"
#include <algorithm>
#include <cctype>

// MULTILEADER and MLEADERSTYLE as AutoCAD writes them (plan phase 3, B36/B37): the same groups
// in 2000, 2004 and 2007, the version in XDATA ACAD_MLEADERVER (2010 has it as 270 and 179 and
// adds 271-273; 2013 295 — written in those versions, which come in phases 4 and 5). The order
// is AutoCAD's (ACadSharp's sample_AC1015 … AC1021, LibreDWG's Leader.dxf); where no AutoCAD
// file has a group (the arrowheads per leader, 94/345; the column heights, 144), where
// Autodesk's reference lists it.

namespace hdxf {
namespace internal {

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// The handle a MULTILEADER's or MLEADERSTYLE's record as read has where `slot` says: the
// slot's code, its occurrence-th, in AcDbMLeaderStyle, or in AcDbMLeader's context data
// (CONTEXT_DATA{ … }, its leaders left out) or its common part after it
std::string read_pointer(const RawRecord& raw, const MLeaderSlot& slot) {
    if (raw.empty()) return {};
    enum class Part { Head, Context, Leader, Line, Common, Style };
    Part part = Part::Head;
    int seen = 0;
    for (const auto& g : raw.groups()) {
        if (g.code == 100) {
            if (g.value == "AcDbMLeader") part = Part::Common;
            else if (g.value == "AcDbMLeaderStyle") part = Part::Style;
            else part = Part::Head;
            continue;
        }
        if (g.code >= 1000) break;                              // XDATA
        if (part == Part::Common && g.code == 300 && g.value == "CONTEXT_DATA{") { part = Part::Context; continue; }
        if (part == Part::Context && g.code == 302 && g.value == "LEADER{") { part = Part::Leader; continue; }
        if (part == Part::Leader && g.code == 304 && g.value == "LEADER_LINE{") { part = Part::Line; continue; }
        if (part == Part::Line && g.code == 305) { part = Part::Leader; continue; }
        if (part == Part::Leader && g.code == 303) { part = Part::Context; continue; }
        if (part == Part::Context && g.code == 301) { part = Part::Common; continue; }
        bool here = slot.part == MLeaderPart::Style ? part == Part::Style
                  : slot.part == MLeaderPart::Context ? part == Part::Context
                  : slot.part == MLeaderPart::Line ? part == Part::Line : part == Part::Common;
        if (here && g.code == slot.code && seen++ == slot.occurrence) return g.value;
    }
    return {};
}

} // namespace

// A block, text style or linetype a MULTILEADER or MLEADERSTYLE names: its handle. One that
// the record as read pointed to and that has been removed since is what AutoCAD has for a
// pointer to nothing, 0, in its place (B19, B36); a name the drawing does not have otherwise
// is an error, as elsewhere.
std::string DxfWriter::mleader_reference(const std::string& what, MLeaderRef kind, const std::string& name,
                                         const RawRecord& raw, const MLeaderSlot& slot) {
    std::string found;
    switch (kind) {
        case MLeaderRef::Block: found = record_handle(name); break;
        case MLeaderRef::TextStyle: {
            if (name.empty()) return standard_style_handle_;
            auto it = style_handles_.find(upper(name));
            if (it != style_handles_.end()) found = it->second;
            break;
        }
        case MLeaderRef::Linetype: {
            auto it = ltype_handles_.find(upper(name));
            if (it != ltype_handles_.end()) found = it->second;
            break;
        }
    }
    if (!found.empty()) return found;
    if (template_mode()) {
        std::string was = read_pointer(raw, slot);
        uint64_t v = 0;
        bool had = parse_handle(was, v) == HandleParse::Ok && v != 0 && orig_handles_.count(v) > 0;
        // Removed, not renamed: the file had a record of that name
        std::string key = (kind == MLeaderRef::Block ? "B:" : kind == MLeaderRef::TextStyle ? "S:" : "L:") + upper(name);
        bool read_name = loaded_names_ && std::binary_search(loaded_names_->begin(), loaded_names_->end(), key);
        if (had && read_name && !is_live(was)) {
            ++dangling_;
            return "0";
        }
    }
    switch (kind) {
        case MLeaderRef::Block: return block_handle_for(what, name);
        case MLeaderRef::TextStyle: return style_handle(name);
        case MLeaderRef::Linetype: return linetype_handle(what, name);
    }
    return "0";
}

// The layers and linetypes in the order write_table_r2000 writes them: proxy graphics name
// them by their index
static ProxyTables proxy_tables_of(const Drawing& dwg, bool defpoints) {
    ProxyTables t;
    if (!has_entry_named(dwg.layers(), "0")) t.layers.push_back("0");
    if (defpoints) t.layers.push_back("Defpoints");
    for (const auto& l : dwg.layers()) t.layers.push_back(l.name);
    for (const char* name : {"ByBlock", "ByLayer", "Continuous"}) {
        if (!has_entry_named(dwg.linetypes(), name)) t.linetypes.push_back(name);
    }
    for (const auto& lt : dwg.linetypes()) t.linetypes.push_back(lt.name);
    return t;
}

static bool proxy_graphics_on(const Drawing& dwg) {
    const HeaderVar* hv = dwg.find_header("$PROXYGRAPHICS");
    return !hv || hv->values.empty() || std::atoi(hv->values[0].second.c_str()) != 0;
}

// The MLEADERSTYLEs by name (ACAD_MLEADERSTYLE), and each style's MULTILEADERs — they are
// its reactors (B36)
void DxfWriter::prepare_mleaders(const Drawing& dwg) {
    mleader_drawing_ = &dwg;
    proxy_layout_.reset();
    proxy_graphics_ = proxy_graphics_on(dwg);
    proxy_tables_ = proxy_tables_of(dwg, needs_defpoints_);
    if (template_mode()) {
        original_proxy_layout_.reset();
        original_proxy_graphics_ = proxy_graphics_on(*original_);
        original_proxy_tables_ = proxy_tables_of(*original_, needs_defpoints_);
    }
    for (const auto& root : dwg.objects()) {
        if (root.type != ObjectType::Dictionary || root.owner != "0") continue;
        for (const auto& entry : root.as_dictionary().entries) {
            if (entry.name != "ACAD_MLEADERSTYLE") continue;
            const Object* dict = dwg.find_object(entry.handle);
            if (!dict || dict->type != ObjectType::Dictionary) continue;
            for (const auto& e : dict->as_dictionary().entries) mleader_style_handles_.emplace(upper(e.name), e.handle);
        }
        break;
    }
    auto collect = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (e.type != EntityType::MLeader) continue;
            std::string style = mleader_style_of(e.as_mleader());
            if (!style.empty()) mleader_reactors_[upper(style)].push_back(e.props.handle);
            mleader_handles_.insert(upper(e.props.handle));
        }
    };
    collect(dwg.entities());
    for (const auto& b : dwg.blocks()) collect(b.entities);
}

// The style's handle: the one set, while its record is there, else the style of that name
std::string DxfWriter::mleader_style_of(const MLeaderData& d) const {
    if (!d.style_handle.empty() && (!filter_live_ || is_live(d.style_handle))) return d.style_handle;
    auto it = mleader_style_handles_.find(upper(d.style));
    return it != mleader_style_handles_.end() ? it->second : std::string();
}

// A style's reactors: its own (the dictionary …) and the MULTILEADERs that use it now — those
// it had in the order it had them, new ones after
std::vector<std::string> DxfWriter::mleader_style_reactors(const Object& o) const {
    std::vector<std::string> out;
    auto now = mleader_reactors_.find(upper(o.handle));
    const std::vector<std::string> none;
    const std::vector<std::string>& users = now != mleader_reactors_.end() ? now->second : none;
    auto uses = [&](const std::string& h) {
        return std::any_of(users.begin(), users.end(), [&](const std::string& u) { return names_equal(u, h); });
    };
    for (const auto& r : o.reactors) {
        bool mleader = mleader_handles_.count(upper(r)) > 0;
        if (!mleader || uses(r)) out.push_back(r);
    }
    for (const auto& u : users) {
        if (std::none_of(out.begin(), out.end(), [&](const std::string& r) { return names_equal(r, u); })) {
            out.push_back(u);
        }
    }
    return out;
}

void DxfWriter::write_mleader_r2000(const Entity& e, const std::string& owner) {
    const MLeaderData& d = e.as_mleader();
    const MLeaderContext& c = d.context;
    const EntityProperties& p = e.props;
    const std::string what = "MULTILEADER " + p.handle;
    bool r2010 = version_ >= DxfVersion::R2010;
    auto flag = [&](int code, bool v) { groups_.write_int(code, v ? 1 : 0); };
    auto color = [&](int code, const CmColor& col) { groups_.write_int(code, col.raw); };
    auto pointer = [&](int code, const std::string& handle) {
        write_code(code, (!filter_live_ || is_live(handle)) ? handle : "0");
    };

    std::vector<uint8_t> proxy = mleader_proxy(e);
    write_entity_head("MULTILEADER", p.handle, owner, p, &proxy);
    write_code(100, "AcDbMLeader");
    if (r2010) groups_.write_int(270, d.version);

    write_code(300, "CONTEXT_DATA{");
    groups_.write_double(40, c.scale);
    write_point(10, c.base_point);
    groups_.write_double(41, c.text_height);
    groups_.write_double(140, c.arrow_size);
    groups_.write_double(145, c.landing_gap);
    groups_.write_int(174, c.left_attachment);
    groups_.write_int(175, c.right_attachment);
    groups_.write_int(176, c.text_align);
    groups_.write_int(177, c.block_connection);
    flag(290, c.has_text);
    if (c.has_text) {
        write_code(304, c.text);
        write_point(11, c.text_normal);
        write_code(340, mleader_reference(what, MLeaderRef::TextStyle, c.text_style, e.raw, {MLeaderPart::Context, 340, 0}));
        write_point(12, c.text_location);
        write_point(13, c.text_direction);
        groups_.write_double(42, c.text_rotation);
        groups_.write_double(43, c.text_width);
        groups_.write_double(44, c.text_defined_height);
        groups_.write_double(45, c.line_spacing_factor);
        groups_.write_int(170, c.line_spacing_style);
        color(90, c.text_color);
        groups_.write_int(171, c.text_attachment);
        groups_.write_int(172, c.text_flow);
        color(91, c.background_color);
        groups_.write_double(141, c.background_scale);
        groups_.write_int(92, c.background_transparency);
        flag(291, c.background_color_on);
        flag(292, c.background_fill_on);
        groups_.write_int(173, c.column_type);
        flag(293, c.auto_height);
        groups_.write_double(142, c.column_width);
        groups_.write_double(143, c.column_gutter);
        flag(294, c.column_flow_reversed);
        for (double h : c.column_heights) groups_.write_double(144, h);
        flag(295, c.word_break);
    }
    flag(296, c.has_block);
    if (c.has_block) {
        write_code(341, mleader_reference(what, MLeaderRef::Block, c.block, e.raw, {MLeaderPart::Context, 341, 0}));
        write_point(14, c.block_normal);
        write_point(15, c.block_position);
        write_point(16, c.block_scale);
        groups_.write_double(46, c.block_rotation);
        color(93, c.block_color);
        if (c.block_matrix.size() != 16 && error_.empty()) {
            error_ = what + ": the block's matrix has 16 values (" + std::to_string(c.block_matrix.size()) + " given)";
        }
        for (double m : c.block_matrix) groups_.write_double(47, m);
    }
    write_point(110, c.plane_origin);
    write_point(111, c.plane_x_axis);
    write_point(112, c.plane_y_axis);
    flag(297, c.plane_normal_reversed);
    int line_linetypes = 0, line_arrows = 0;        // the lines' pointers so far, in the record's order
    for (const auto& leader : c.leaders) {
        write_code(302, "LEADER{");
        flag(290, leader.has_last_point);
        flag(291, leader.has_dogleg);
        write_point(10, leader.last_point);
        write_point(11, leader.dogleg_direction);
        for (const auto& gap : leader.breaks) {
            write_point(12, gap.start);
            write_point(13, gap.end);
        }
        groups_.write_int(90, leader.branch_index);
        groups_.write_double(40, leader.dogleg_length);
        for (const auto& line : leader.lines) {
            write_code(304, "LEADER_LINE{");
            for (const auto& v : line.vertices) write_point(10, v);
            for (const auto& segment : line.breaks) {
                groups_.write_int(90, segment.segment);
                for (const auto& gap : segment.gaps) {
                    write_point(11, gap.start);
                    write_point(12, gap.end);
                }
            }
            groups_.write_int(91, line.index);
            // 2010: the line's own properties, those it has, in AutoCAD's order (B47)
            if (r2010) {
                if (line.leader_type) groups_.write_int(170, *line.leader_type);
                if (line.color) color(92, *line.color);
                if (line.linetype) {
                    write_code(340, line.linetype->empty() ? "0"
                                    : mleader_reference(what, MLeaderRef::Linetype, *line.linetype, e.raw,
                                                        {MLeaderPart::Line, 340, line_linetypes}));
                    ++line_linetypes;
                }
                if (line.lineweight) groups_.write_int(171, *line.lineweight);
                if (line.arrow_size) groups_.write_double(40, *line.arrow_size);
                if (line.arrow_block) {
                    write_code(341, line.arrow_block->empty() ? "0"
                                    : mleader_reference(what, MLeaderRef::Block, *line.arrow_block, e.raw,
                                                        {MLeaderPart::Line, 341, line_arrows}));
                    ++line_arrows;
                }
                if (line.override_flags) groups_.write_int(93, *line.override_flags);
            }
            write_code(305, "}");
        }
        if (r2010) groups_.write_int(271, leader.attachment_direction);
        write_code(303, "}");
    }
    if (r2010) {
        groups_.write_int(272, c.bottom_attachment_direction);
        groups_.write_int(273, c.top_attachment_direction);
    }
    write_code(301, "}");

    std::string style = mleader_style_of(d);
    if (style.empty() && error_.empty()) error_ = "MULTILEADER style \"" + d.style + "\" is not in ACAD_MLEADERSTYLE";
    write_code(340, style.empty() ? "0" : style);
    groups_.write_int(90, d.override_flags);
    groups_.write_int(170, d.leader_type);
    color(91, d.leader_color);
    // AutoCAD leaves 341 out of a multileader without a leader linetype of its own; a name that is
    // given must exist
    if (!d.leader_linetype.empty()) {
        write_code(341, mleader_reference(what, MLeaderRef::Linetype, d.leader_linetype, e.raw, {MLeaderPart::Common, 341, 0}));
    }
    groups_.write_int(171, d.leader_lineweight);
    flag(290, d.landing);
    flag(291, d.dogleg);
    groups_.write_double(41, d.dogleg_length);
    if (!d.arrow_block.empty()) write_code(342, mleader_reference(what, MLeaderRef::Block, d.arrow_block, e.raw, {MLeaderPart::Common, 342, 0}));
    groups_.write_double(42, d.arrow_size);
    groups_.write_int(172, d.content_type);
    write_code(343, mleader_reference(what, MLeaderRef::TextStyle, d.text_style, e.raw, {MLeaderPart::Common, 343, 0}));
    groups_.write_int(173, d.text_left_attachment);
    groups_.write_int(95, d.text_right_attachment);
    groups_.write_int(174, d.text_angle_type);
    groups_.write_int(175, d.text_alignment);
    color(92, d.text_color);
    flag(292, d.text_frame);
    if (!d.block.empty()) write_code(344, mleader_reference(what, MLeaderRef::Block, d.block, e.raw, {MLeaderPart::Common, 344, 0}));
    color(93, d.block_color);
    write_point(10, d.block_scale);
    groups_.write_double(43, d.block_rotation);
    groups_.write_int(176, d.block_connection);
    flag(293, d.annotation_scale);
    int arrowhead = 0;
    for (const auto& a : d.arrowheads) {
        groups_.write_int(94, a.index);
        write_code(345, mleader_reference(what, MLeaderRef::Block, a.block, e.raw, {MLeaderPart::Common, 345, arrowhead++}));
    }
    for (const auto& a : d.attributes) {
        pointer(330, a.attdef_handle);
        groups_.write_int(177, a.index);
        groups_.write_double(44, a.width);
        write_code(302, a.text);
    }
    flag(294, d.text_direction_negative);
    groups_.write_int(178, d.text_align_in_ipe);
    groups_.write_int(179, d.text_attachment_point);
    groups_.write_double(45, d.scale);
    if (r2010) {
        groups_.write_int(271, d.text_attachment_direction);
        groups_.write_int(272, d.bottom_attachment_direction);
        groups_.write_int(273, d.top_attachment_direction);
    }
    if (version_ >= DxfVersion::R2013) flag(295, d.text_extended);
}

// The proxy graphics AutoCAD writes for a MULTILEADER (B38): as mleader_graphics() draws it,
// the text in UTF-16 from 2007 and in the drawing's code page before; none when
// $PROXYGRAPHICS is 0. The fonts not found are warnings.
//
// A MULTILEADER read from a file keeps AutoCAD's graphics while what it draws is as it was
// (B12): its original is drawn with the drawing as read — its blocks, styles, layers and
// header — so a change in any of them gives new graphics, as a change in the MULTILEADER
// does. Something it draws with that has been removed since (not in the drawing as read
// either) makes the original's graphics none, so they are new too.
std::vector<uint8_t> DxfWriter::mleader_proxy(const Entity& e) {
    if (!mleader_drawing_) return {};
    bool unicode = version_ >= DxfVersion::R2007;
    if (!filter_live_ && template_mode()) {
        if (!original_proxy_graphics_ || mleader_dependency_removed(e)) return {};
        if (!original_proxy_layout_) original_proxy_layout_ = std::make_unique<TextLayout>(*original_);
        return mleader_graphics(*original_, e, original_proxy_tables_, *original_proxy_layout_, unicode);
    }
    if (!proxy_graphics_) return {};
    if (!proxy_layout_) proxy_layout_ = std::make_unique<TextLayout>(*mleader_drawing_);
    std::vector<uint8_t> bytes = mleader_graphics(*mleader_drawing_, e, proxy_tables_, *proxy_layout_, unicode);
    for (const auto& w : proxy_layout_->warnings()) {
        if (std::find(warnings_.begin(), warnings_.end(), w) == warnings_.end()) warnings_.push_back(w);
    }
    return bytes;
}

// A read MULTILEADER's dependency that has been removed since: a record its own record pointed
// to (a block, an arrowhead's block, a style, a linetype, an attribute's definition), or an
// entity of a block it draws (its content, its arrowheads' blocks and the blocks they insert)
bool DxfWriter::mleader_dependency_removed(const Entity& e) const {
    auto gone = [&](const std::string& h) {
        uint64_t v = 0;
        return parse_handle(h, v) == HandleParse::Ok && v != 0 && orig_handles_.count(v) > 0 && !live_.count(v);
    };
    bool body = false;
    for (const auto& g : e.raw.groups()) {
        if (g.code == 100) body = g.value == "AcDbMLeader";
        if (g.code >= 1000) break;
        if (body && g.code >= 330 && g.code <= 369 && gone(g.value)) return true;
    }
    // The blocks it draws, and the blocks they insert
    const MLeaderData& d = e.as_mleader();
    std::vector<std::string> names;
    if (d.context.has_block) names.push_back(d.context.block);
    if (!d.arrow_block.empty()) names.push_back(d.arrow_block);
    for (const auto& a : d.arrowheads) names.push_back(a.block);
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < names.size(); ++i) {
        if (!seen.insert(upper(names[i])).second) continue;
        const Block* b = mleader_drawing_->find_block(names[i]);
        if (!b) continue;
        uint64_t record = 0;
        if (parse_handle(b->record.handle, record) == HandleParse::Ok) {
            auto it = orig_block_entities_.find(record);
            if (it != orig_block_entities_.end()) {
                for (uint64_t v : it->second) {
                    if (!live_.count(v)) return true;
                }
            }
        }
        for (const auto& be : b->entities) {
            if (be.type == EntityType::Insert) names.push_back(be.as_insert().block_name);
        }
    }
    return false;
}

// The version in XDATA, as AutoCAD writes MULTILEADER and MLEADERSTYLE up to 2007 — after
// the record's other XDATA
void DxfWriter::write_mleader_version(int version) {
    if (version_ >= DxfVersion::R2010) return;
    write_code(1001, "ACAD_MLEADERVER");
    groups_.write_int(1070, version);
}

void DxfWriter::write_mleader_style(const MLeaderStyleData& s, const RawRecord& raw) {
    const std::string what = "MLEADERSTYLE";
    auto flag = [&](int code, bool v) { groups_.write_int(code, v ? 1 : 0); };
    auto color = [&](int code, const CmColor& col) { groups_.write_int(code, col.raw); };
    write_code(100, "AcDbMLeaderStyle");
    if (version_ >= DxfVersion::R2010) groups_.write_int(179, s.version);
    groups_.write_int(170, s.content_type);
    groups_.write_int(171, s.draw_mleader_order);
    groups_.write_int(172, s.draw_leader_order);
    groups_.write_int(90, s.max_leader_points);
    groups_.write_double(40, s.first_segment_angle);
    groups_.write_double(41, s.second_segment_angle);
    groups_.write_int(173, s.leader_type);
    color(91, s.leader_color);
    if (!s.leader_linetype.empty()) write_code(340, mleader_reference(what, MLeaderRef::Linetype, s.leader_linetype, raw, {MLeaderPart::Style, 340, 0}));
    groups_.write_int(92, s.leader_lineweight);
    flag(290, s.landing);
    groups_.write_double(42, s.landing_gap);
    flag(291, s.dogleg);
    groups_.write_double(43, s.dogleg_length);
    write_code(3, s.description);
    if (!s.arrow_block.empty()) write_code(341, mleader_reference(what, MLeaderRef::Block, s.arrow_block, raw, {MLeaderPart::Style, 341, 0}));
    groups_.write_double(44, s.arrow_size);
    write_code(300, s.default_text);
    if (!s.text_style.empty()) write_code(342, mleader_reference(what, MLeaderRef::TextStyle, s.text_style, raw, {MLeaderPart::Style, 342, 0}));
    groups_.write_int(174, s.text_left_attachment);
    groups_.write_int(178, s.text_right_attachment);
    groups_.write_int(175, s.text_angle_type);
    groups_.write_int(176, s.text_alignment);
    color(93, s.text_color);
    groups_.write_double(45, s.text_height);
    flag(292, s.text_frame);
    flag(297, s.text_always_left);
    groups_.write_double(46, s.align_space);
    if (!s.block.empty()) write_code(343, mleader_reference(what, MLeaderRef::Block, s.block, raw, {MLeaderPart::Style, 343, 0}));
    color(94, s.block_color);
    groups_.write_double(47, s.block_scale.x);
    groups_.write_double(49, s.block_scale.y);
    groups_.write_double(140, s.block_scale.z);
    flag(293, s.block_scale_enabled);
    groups_.write_double(141, s.block_rotation);
    flag(294, s.block_rotation_enabled);
    groups_.write_int(177, s.block_connection);
    groups_.write_double(142, s.scale);
    // 295: AutoCAD writes 0 before 2013 (every style of its 2000-2010 files; B60)
    flag(295, version_ >= DxfVersion::R2013 && s.overwrite_property);
    flag(296, s.annotative);
    groups_.write_double(143, s.break_gap);
    if (version_ >= DxfVersion::R2010) {
        groups_.write_int(271, s.text_attachment_direction);
        groups_.write_int(272, s.bottom_attachment_direction);
        groups_.write_int(273, s.top_attachment_direction);
    }
    // 2013: 298 last (in no reference; AutoCAD's 2013 files, B58)
    if (version_ >= DxfVersion::R2013) flag(298, s.text_extended);
}

} // namespace internal
} // namespace hdxf
