#include "mleader.hpp"
#include <cstdlib>

// MULTILEADER and MLEADERSTYLE read from their groups (plan phase 3, B36/B37), in the order
// AutoCAD writes them (ACadSharp's 15 in sample_AC1015 … AC1032, LibreDWG's Leader.dxf 2000 …
// 2018, the MLEADERs of AutoCAD files found on GitHub; Autodesk's DXF reference names the
// codes). The context data holds the content and the leaders, each leader its lines; a
// code means different things in each part, so the parts are followed by their markers
// (300 CONTEXT_DATA{, 302 LEADER{, 304 LEADER_LINE{ and their closing 301, 303, 305).

namespace hdxf {
namespace internal {

namespace {

int to_int(const std::string& s) { return std::atoi(s.c_str()); }
double to_double(const std::string& s) { return std::atof(s.c_str()); }

// One coordinate of a point: 10-39 and 110-139 by the code's last digit's group (x 10s, y
// 20s, z 30s)
void set_coordinate(Point3& p, int code, const std::string& value) {
    int axis = (code % 100) / 10;
    double v = to_double(value);
    if (axis == 1) p.x = v;
    else if (axis == 2) p.y = v;
    else p.z = v;
}

} // namespace

void parse_mleader(const std::vector<DxfGroup>& groups, MLeaderData& d) {
    enum class Part { Head, Context, Leader, Line, Tail };
    Part part = Part::Head;
    MLeaderContext& c = d.context;
    MLeaderLeader* leader = nullptr;
    MLeaderLine* line = nullptr;
    bool line_index_read = false;       // a line's groups after 91 are 2010's own

    for (const DxfGroup& g : groups) {
        const int code = g.code;
        const std::string& v = g.value;
        switch (part) {
            case Part::Head:
                if (code == 270) d.version = to_int(v);
                else if (code == 300) part = Part::Context;     // CONTEXT_DATA{
                break;

            case Part::Context:
                if (code == 302 && v == "LEADER{") {
                    c.leaders.emplace_back();
                    leader = &c.leaders.back();
                    part = Part::Leader;
                    break;
                }
                if (code == 301) {
                    part = Part::Tail;
                    break;
                }
                switch (code) {
                    case 40:  c.scale = to_double(v); break;
                    case 10: case 20: case 30: set_coordinate(c.base_point, code, v); break;
                    case 41:  c.text_height = to_double(v); break;
                    case 140: c.arrow_size = to_double(v); break;
                    case 145: c.landing_gap = to_double(v); break;
                    case 174: c.left_attachment = to_int(v); break;
                    case 175: c.right_attachment = to_int(v); break;
                    case 176: c.text_align = to_int(v); break;
                    case 177: c.block_connection = to_int(v); break;
                    case 290: c.has_text = to_int(v) != 0; break;
                    case 304: c.text = v; break;
                    case 11: case 21: case 31: set_coordinate(c.text_normal, code, v); break;
                    case 340: c.text_style = v; break;
                    case 12: case 22: case 32: set_coordinate(c.text_location, code, v); break;
                    case 13: case 23: case 33: set_coordinate(c.text_direction, code, v); break;
                    case 42:  c.text_rotation = to_double(v); break;
                    case 43:  c.text_width = to_double(v); break;
                    case 44:  c.text_defined_height = to_double(v); break;
                    case 45:  c.line_spacing_factor = to_double(v); break;
                    case 170: c.line_spacing_style = to_int(v); break;
                    case 90:  c.text_color = CmColor{to_int(v)}; break;
                    case 171: c.text_attachment = to_int(v); break;
                    case 172: c.text_flow = to_int(v); break;
                    case 91:  c.background_color = CmColor{to_int(v)}; break;
                    case 141: c.background_scale = to_double(v); break;
                    case 92:  c.background_transparency = to_int(v); break;
                    case 291: c.background_color_on = to_int(v) != 0; break;
                    case 292: c.background_fill_on = to_int(v) != 0; break;
                    case 173: c.column_type = to_int(v); break;
                    case 293: c.auto_height = to_int(v) != 0; break;
                    case 142: c.column_width = to_double(v); break;
                    case 143: c.column_gutter = to_double(v); break;
                    case 294: c.column_flow_reversed = to_int(v) != 0; break;
                    case 144: c.column_heights.push_back(to_double(v)); break;
                    case 295: c.word_break = to_int(v) != 0; break;
                    case 296: c.has_block = to_int(v) != 0; break;
                    case 341: c.block = v; break;
                    case 14: case 24: case 34: set_coordinate(c.block_normal, code, v); break;
                    case 15: case 25: case 35: set_coordinate(c.block_position, code, v); break;
                    case 16: case 26: case 36: set_coordinate(c.block_scale, code, v); break;
                    case 46:  c.block_rotation = to_double(v); break;
                    case 93:  c.block_color = CmColor{to_int(v)}; break;
                    case 47:  c.block_matrix.push_back(to_double(v)); break;
                    case 110: case 120: case 130: set_coordinate(c.plane_origin, code, v); break;
                    case 111: case 121: case 131: set_coordinate(c.plane_x_axis, code, v); break;
                    case 112: case 122: case 132: set_coordinate(c.plane_y_axis, code, v); break;
                    case 297: c.plane_normal_reversed = to_int(v) != 0; break;
                    case 272: c.bottom_attachment_direction = to_int(v); break;
                    case 273: c.top_attachment_direction = to_int(v); break;
                }
                break;

            case Part::Leader:
                if (code == 304 && v == "LEADER_LINE{") {
                    leader->lines.emplace_back();
                    line = &leader->lines.back();
                    line_index_read = false;
                    part = Part::Line;
                    break;
                }
                if (code == 303) {
                    part = Part::Context;
                    break;
                }
                switch (code) {
                    case 290: leader->has_last_point = to_int(v) != 0; break;
                    case 291: leader->has_dogleg = to_int(v) != 0; break;
                    case 10: case 20: case 30: set_coordinate(leader->last_point, code, v); break;
                    case 11: case 21: case 31: set_coordinate(leader->dogleg_direction, code, v); break;
                    case 12: case 22: case 32:
                        if (code == 12) leader->breaks.emplace_back();
                        if (!leader->breaks.empty()) set_coordinate(leader->breaks.back().start, code, v);
                        break;
                    case 13: case 23: case 33:
                        if (!leader->breaks.empty()) set_coordinate(leader->breaks.back().end, code, v);
                        break;
                    case 90:  leader->branch_index = to_int(v); break;
                    case 40:  leader->dogleg_length = to_double(v); break;
                    case 271: leader->attachment_direction = to_int(v); break;
                }
                break;

            case Part::Line:
                if (code == 305) {
                    part = Part::Leader;
                    break;
                }
                // 2010+: the line's own properties after its index (B47)
                if (line_index_read) {
                    switch (code) {
                        case 170: line->leader_type = to_int(v); break;
                        case 92:  line->color = CmColor{to_int(v)}; break;
                        case 340: line->linetype = v; break;
                        case 171: line->lineweight = to_int(v); break;
                        case 40:  line->arrow_size = to_double(v); break;
                        case 341: line->arrow_block = v; break;
                        case 93:  line->override_flags = to_int(v); break;
                    }
                    break;
                }
                switch (code) {
                    case 10:
                        line->vertices.emplace_back();
                        line->vertices.back().x = to_double(v);
                        break;
                    case 20: case 30:
                        if (!line->vertices.empty()) set_coordinate(line->vertices.back(), code, v);
                        break;
                    case 90:
                        line->breaks.push_back({to_int(v), {}});
                        break;
                    case 11: case 21: case 31:
                        if (line->breaks.empty()) break;
                        if (code == 11) line->breaks.back().gaps.emplace_back();
                        if (!line->breaks.back().gaps.empty()) {
                            set_coordinate(line->breaks.back().gaps.back().start, code, v);
                        }
                        break;
                    case 12: case 22: case 32:
                        if (!line->breaks.empty() && !line->breaks.back().gaps.empty()) {
                            set_coordinate(line->breaks.back().gaps.back().end, code, v);
                        }
                        break;
                    case 91:
                        line->index = to_int(v);
                        line_index_read = true;
                        break;
                }
                break;

            case Part::Tail:
                switch (code) {
                    case 340: d.style_handle = v; break;
                    case 90:  d.override_flags = to_int(v); break;
                    case 170: d.leader_type = to_int(v); break;
                    case 91:  d.leader_color = CmColor{to_int(v)}; break;
                    case 341: d.leader_linetype = v; break;
                    case 171: d.leader_lineweight = to_int(v); break;
                    case 290: d.landing = to_int(v) != 0; break;
                    case 291: d.dogleg = to_int(v) != 0; break;
                    case 41:  d.dogleg_length = to_double(v); break;
                    case 342: d.arrow_block = v; break;
                    case 42:  d.arrow_size = to_double(v); break;
                    case 172: d.content_type = to_int(v); break;
                    case 343: d.text_style = v; break;
                    case 173: d.text_left_attachment = to_int(v); break;
                    case 95:  d.text_right_attachment = to_int(v); break;
                    case 174: d.text_angle_type = to_int(v); break;
                    case 175: d.text_alignment = to_int(v); break;
                    case 92:  d.text_color = CmColor{to_int(v)}; break;
                    case 292: d.text_frame = to_int(v) != 0; break;
                    case 344: d.block = v; break;
                    case 93:  d.block_color = CmColor{to_int(v)}; break;
                    case 10: case 20: case 30: set_coordinate(d.block_scale, code, v); break;
                    case 43:  d.block_rotation = to_double(v); break;
                    case 176: d.block_connection = to_int(v); break;
                    case 293: d.annotation_scale = to_int(v) != 0; break;
                    case 94:  d.arrowheads.push_back({to_int(v), {}}); break;
                    case 345: if (!d.arrowheads.empty()) d.arrowheads.back().block = v; break;
                    case 330: d.attributes.push_back({v, 0, 0.0, {}}); break;
                    case 177: if (!d.attributes.empty()) d.attributes.back().index = to_int(v); break;
                    case 44:  if (!d.attributes.empty()) d.attributes.back().width = to_double(v); break;
                    case 302: if (!d.attributes.empty()) d.attributes.back().text = v; break;
                    case 294: d.text_direction_negative = to_int(v) != 0; break;
                    case 178: d.text_align_in_ipe = to_int(v); break;
                    case 179: d.text_attachment_point = to_int(v); break;
                    case 45:  d.scale = to_double(v); break;
                    case 271: d.text_attachment_direction = to_int(v); break;
                    case 272: d.bottom_attachment_direction = to_int(v); break;
                    case 273: d.top_attachment_direction = to_int(v); break;
                    case 295: d.text_extended = to_int(v) != 0; break;
                }
                break;
        }
    }
}

MLeaderStyleData parse_mleader_style(const std::vector<DxfGroup>& groups) {
    MLeaderStyleData d;
    // A new style's text style and block are none until the file names them
    d.text_style.clear();
    d.leader_linetype.clear();
    for (const DxfGroup& g : groups) {
        const std::string& v = g.value;
        switch (g.code) {
            case 179: d.version = to_int(v); break;
            case 170: d.content_type = to_int(v); break;
            case 171: d.draw_mleader_order = to_int(v); break;
            case 172: d.draw_leader_order = to_int(v); break;
            case 90:  d.max_leader_points = to_int(v); break;
            case 40:  d.first_segment_angle = to_double(v); break;
            case 41:  d.second_segment_angle = to_double(v); break;
            case 173: d.leader_type = to_int(v); break;
            case 91:  d.leader_color = CmColor{to_int(v)}; break;
            case 340: d.leader_linetype = v; break;
            case 92:  d.leader_lineweight = to_int(v); break;
            case 290: d.landing = to_int(v) != 0; break;
            case 42:  d.landing_gap = to_double(v); break;
            case 291: d.dogleg = to_int(v) != 0; break;
            case 43:  d.dogleg_length = to_double(v); break;
            case 3:   d.description = v; break;
            case 341: d.arrow_block = v; break;
            case 44:  d.arrow_size = to_double(v); break;
            case 300: d.default_text = v; break;
            case 342: d.text_style = v; break;
            case 174: d.text_left_attachment = to_int(v); break;
            case 178: d.text_right_attachment = to_int(v); break;
            case 175: d.text_angle_type = to_int(v); break;
            case 176: d.text_alignment = to_int(v); break;
            case 93:  d.text_color = CmColor{to_int(v)}; break;
            case 45:  d.text_height = to_double(v); break;
            case 292: d.text_frame = to_int(v) != 0; break;
            case 297: d.text_always_left = to_int(v) != 0; break;
            case 46:  d.align_space = to_double(v); break;
            case 343: d.block = v; break;
            case 94:  d.block_color = CmColor{to_int(v)}; break;
            case 47:  d.block_scale.x = to_double(v); break;
            case 49:  d.block_scale.y = to_double(v); break;
            case 140: d.block_scale.z = to_double(v); break;
            case 293: d.block_scale_enabled = to_int(v) != 0; break;
            case 141: d.block_rotation = to_double(v); break;
            case 294: d.block_rotation_enabled = to_int(v) != 0; break;
            case 177: d.block_connection = to_int(v); break;
            case 142: d.scale = to_double(v); break;
            case 295: d.overwrite_property = to_int(v) != 0; break;
            case 296: d.annotative = to_int(v) != 0; break;
            case 143: d.break_gap = to_double(v); break;
            case 271: d.text_attachment_direction = to_int(v); break;
            case 272: d.bottom_attachment_direction = to_int(v); break;
            case 273: d.top_attachment_direction = to_int(v); break;
            case 298: d.text_extended = to_int(v) != 0; break;
        }
    }
    return d;
}

void take_mleader_version(std::vector<XdataList>& xdata, int& version) {
    for (size_t i = 0; i < xdata.size(); ++i) {
        const XdataList& x = xdata[i];
        if (x.app_name != "ACAD_MLEADERVER" || x.items.size() != 1 || x.items[0].code != 1070) continue;
        version = to_int(x.items[0].value);
        xdata.erase(xdata.begin() + static_cast<long>(i));
        return;
    }
}

} // namespace internal
} // namespace hdxf
