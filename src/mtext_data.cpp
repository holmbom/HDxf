#include "mtext_data.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace hdxf {

namespace internal {

static bool take_column_info(std::vector<XdataList>& xdata, MTextColumns& columns) {
    for (size_t li = 0; li < xdata.size(); ++li) {
        XdataList& list = xdata[li];
        if (list.app_name != "ACAD") continue;
        size_t begin = list.items.size(), end = list.items.size();
        for (size_t i = 0; i < list.items.size(); ++i) {
            if (list.items[i].code == 1000 && list.items[i].value == "ACAD_MTEXT_COLUMN_INFO_BEGIN") begin = i;
            if (list.items[i].code == 1000 && list.items[i].value == "ACAD_MTEXT_COLUMN_INFO_END" && begin < i) {
                end = i;
                break;
            }
        }
        if (end >= list.items.size()) continue;
        MTextColumns c;
        size_t i = begin + 1;
        bool ok = true;
        std::vector<int> seen;
        while (i < end && ok) {
            const XdataItem& key = list.items[i];
            if (key.code != 1070 || i + 1 >= end) { ok = false; break; }
            int k = std::atoi(key.value.c_str());
            const XdataItem& value = list.items[i + 1];
            if (std::find(seen.begin(), seen.end(), k) != seen.end()) { ok = false; break; }
            seen.push_back(k);
            i += 2;
            switch (k) {
                case 75: ok = value.code == 1070; c.type = std::atoi(value.value.c_str()); break;
                case 79: ok = value.code == 1070; c.auto_height = std::atoi(value.value.c_str()) != 0; break;
                case 76: ok = value.code == 1070; c.count = std::atoi(value.value.c_str()); break;
                case 78: ok = value.code == 1070; c.reversed = std::atoi(value.value.c_str()) != 0; break;
                case 48: ok = value.code == 1040; c.width = std::atof(value.value.c_str()); break;
                case 49: ok = value.code == 1040; c.gutter = std::atof(value.value.c_str()); break;
                case 50: {
                    ok = value.code == 1070;
                    int n = std::atoi(value.value.c_str());
                    for (int h = 0; h < n && ok; ++h) {
                        if (i >= end || list.items[i].code != 1040) { ok = false; break; }
                        c.heights.push_back(std::atof(list.items[i++].value.c_str()));
                    }
                    break;
                }
                default: ok = false; break;
            }
        }
        // Only the form AutoCAD writes (the keys in its order) is taken
        // (static columns have no heights: no 50, ezdxf's mtext_framed_columns)
        static const std::vector<int> order = {75, 79, 76, 78, 48, 49, 50};
        static const std::vector<int> without_heights = {75, 79, 76, 78, 48, 49};
        if (!ok || (seen != order && seen != without_heights)) continue;
        columns = std::move(c);
        list.items.erase(list.items.begin() + static_cast<long>(begin), list.items.begin() + static_cast<long>(end) + 1);
        if (list.items.empty()) xdata.erase(xdata.begin() + static_cast<long>(li));
        return true;
    }
    return false;
}

bool mtext_apply_group(MTextData& d, MTextParseState& state, int code, const std::string& value) {
    switch (code) {
        case 10: d.insertion_point.x = std::atof(value.c_str()); break;
        case 20: d.insertion_point.y = std::atof(value.c_str()); break;
        case 30: d.insertion_point.z = std::atof(value.c_str()); break;
        case 40: d.height = std::atof(value.c_str()); break;
        case 41: d.reference_width = std::atof(value.c_str()); break;
        case 46: d.defined_height = std::atof(value.c_str()); break;    // 2007+
        case 71: d.attachment = std::atoi(value.c_str()); break;
        case 72: d.drawing_direction = std::atoi(value.c_str()); break;
        case 3:  state.parts += value; break;
        case 1:  state.last = value; break;
        case 7:  d.style = value; break;
        case 11: d.x_axis_direction.x = std::atof(value.c_str()); state.by_rotation = false; break;
        case 21: d.x_axis_direction.y = std::atof(value.c_str()); break;
        case 31: d.x_axis_direction.z = std::atof(value.c_str()); break;
        case 50: state.rotation = std::atof(value.c_str()); state.by_rotation = true; break;
        case 73: d.line_spacing_style = std::atoi(value.c_str()); break;
        case 44: d.line_spacing_factor = std::atof(value.c_str()); break;
        // 2004+: the background fill
        case 90: d.background.flags = std::atoi(value.c_str()); break;
        case 63: d.background.color.aci = std::atoi(value.c_str()); break;
        case 421: d.background.color.rgb = std::atoi(value.c_str()); break;
        case 431: d.background.color.name = value; break;    // decoded (only 1 and 3 wait)
        case 45: d.background.scale = std::atof(value.c_str()); break;
        case 441: d.background.transparency.raw = std::atoi(value.c_str()); break;
        case 42: break;                 // the text's width and height as drawn:
        case 43: break;                 // read-only, AutoCAD computes them
        default: return false;
    }
    return true;
}

void mtext_finish(MTextData& d, MTextParseState& state, const Point3& extrusion) {
    d.content = state.parts + state.last;
    if (state.by_rotation) {
        Point3 ax, ay;
        ecs::arbitrary_axis(extrusion, ax, ay);
        double c = std::cos(state.rotation);
        double s = std::sin(state.rotation);
        d.x_axis_direction = {c * ax.x + s * ay.x, c * ax.y + s * ay.y, c * ax.z + s * ay.z};
    }
}

void mtext_apply_embedded_group(MTextData& d, MTextParseState& state, int code, const std::string& value) {
    if (!d.embedded) d.embedded.emplace();
    if (!d.columns) d.columns.emplace();
    MTextEmbedded& e = *d.embedded;
    MTextColumns& c = *d.columns;
    double v = std::atof(value.c_str());
    int i = std::atoi(value.c_str());
    switch (code) {
        case 70: d.attachment = i; break;
        case 10: d.x_axis_direction.x = v; state.by_rotation = false; break;
        case 20: d.x_axis_direction.y = v; break;
        case 30: d.x_axis_direction.z = v; break;
        case 11: d.insertion_point.x = v; break;
        case 21: d.insertion_point.y = v; break;
        case 31: d.insertion_point.z = v; break;
        case 40: break;                 // the width: the entity's 41
        case 41: d.defined_height = v; break;
        case 42: e.measured_width = v; break;
        case 43: e.measured_height = v; break;
        case 71: c.type = i; break;
        case 72: c.count = i; break;
        case 44: c.width = v; break;
        case 45: c.gutter = v; break;
        case 73: c.auto_height = i != 0; break;
        case 74: c.reversed = i != 0; break;
        case 46: c.heights.push_back(v); break;
        default: break;
    }
}

static void column_info_items(const MTextColumns& c, std::vector<XdataItem>& items) {
    XdataList list;
    auto key = [&](int k, int code, const std::string& v) {
        list.items.push_back({1070, std::to_string(k)});
        list.items.push_back({code, v});
    };
    list.items.push_back({1000, "ACAD_MTEXT_COLUMN_INFO_BEGIN"});
    key(75, 1070, std::to_string(c.type));
    key(79, 1070, c.auto_height ? "1" : "0");
    key(76, 1070, std::to_string(c.count));
    key(78, 1070, c.reversed ? "1" : "0");
    key(48, 1040, format_double(c.width, NumberStyle::AutoCad));
    key(49, 1040, format_double(c.gutter, NumberStyle::AutoCad));
    // Static columns have no heights, and then no 50
    if (c.type != 1 || !c.heights.empty()) {
        key(50, 1070, std::to_string(c.heights.size()));
        for (double h : c.heights) list.items.push_back({1040, format_double(h, NumberStyle::AutoCad)});
    }
    list.items.push_back({1000, "ACAD_MTEXT_COLUMN_INFO_END"});
    items.insert(items.end(), list.items.begin(), list.items.end());
}

// The items between name_BEGIN and name_END of the ACAD list that has them: their index range
// (begin = the BEGIN item); false when there is none
static bool find_block(const std::vector<XdataList>& xdata, const char* name, size_t& list, size_t& begin, size_t& end) {
    std::string b = std::string(name) + "_BEGIN", e = std::string(name) + "_END";
    for (size_t li = 0; li < xdata.size(); ++li) {
        if (xdata[li].app_name != "ACAD") continue;
        const auto& items = xdata[li].items;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].code != 1000 || items[i].value != b) continue;
            for (size_t k = i + 1; k < items.size(); ++k) {
                if (items[k].code == 1000 && items[k].value == e) {
                    list = li;
                    begin = i;
                    end = k;
                    return true;
                }
            }
        }
    }
    return false;
}

static void erase_block(std::vector<XdataList>& xdata, size_t list, size_t begin, size_t end) {
    auto& items = xdata[list].items;
    items.erase(items.begin() + static_cast<long>(begin), items.begin() + static_cast<long>(end) + 1);
    if (items.empty()) xdata.erase(xdata.begin() + static_cast<long>(list));
}

static bool item_number(const XdataItem& it, int code, double& out) {
    if (it.code != code) return false;
    out = std::atof(it.value.c_str());
    return true;
}

// A key: a 1070 with this integer (the file pads it: "    47")
static bool is_key(const XdataItem& it, int key) {
    return it.code == 1070 && std::atoi(it.value.c_str()) == key;
}

void take_mtext_xdata(std::vector<XdataList>& xdata, MTextData& d) {
    // The columns
    if (!d.columns) {
        MTextColumns columns;
        if (take_column_info(xdata, columns)) d.columns = std::move(columns);
    }
    size_t list = 0, begin = 0, end = 0;
    // The other columns: 47, their number, and a 1005 each
    if (d.columns && find_block(xdata, "ACAD_MTEXT_COLUMNS", list, begin, end)) {
        const auto& items = xdata[list].items;
        bool ok = end - begin >= 3 && is_key(items[begin + 1], 47) &&
                  items[begin + 2].code == 1070;
        std::vector<std::string> handles;
        for (size_t i = begin + 3; ok && i < end; ++i) {
            if (items[i].code != 1005) ok = false;
            else handles.push_back(items[i].value);
        }
        if (ok) {
            d.columns->linked = handles;
            erase_block(xdata, list, begin, end);
        }
    }
    // The frame: 80 (the flags of 90), 46 (the scale of 45), 81 (their number) and a 5 with a 1005 each
    if (find_block(xdata, "ACAD_MTEXT_TEXT_BORDERS", list, begin, end)) {
        const auto& items = xdata[list].items;
        double flags = 0, scale = 0, count = 0;
        bool ok = end - begin >= 7 && is_key(items[begin + 1], 80) &&
                  item_number(items[begin + 2], 1070, flags) && is_key(items[begin + 3], 46) && item_number(items[begin + 4], 1040, scale) &&
                  is_key(items[begin + 5], 81) &&
                  item_number(items[begin + 6], 1070, count);
        std::vector<std::string> handles;
        size_t at = begin + 7;
        for (int k = 0; ok && k < static_cast<int>(count); ++k, at += 2) {
            ok = at + 1 < end && is_key(items[at], 5) && items[at + 1].code == 1005;
            if (ok) handles.push_back(items[at + 1].value);
        }
        // A frame without a fill has no group 45 (AutoCAD's core refuses it): its scale is the XDATA's
        // 80 is the frame bit (16) alone in AutoCAD 2013's file, whose 90 has the fill bits with it
        // (19); a text whose 90 has no frame bit (AutoCAD 2010's, 90 3) keeps the block as it is
        bool fill = (d.background.flags & 3) != 0;
        bool same_flags = static_cast<int>(flags) == d.background.flags ||
                          (static_cast<int>(flags) == 16 && (d.background.flags & 16) != 0);
        ok = ok && at == end && same_flags && (!fill || scale == d.background.scale);
        if (ok) {
            if (!fill) d.background.scale = scale;
            d.background.border_handles = handles;
            erase_block(xdata, list, begin, end);
        }
    }
    // The defined height: 2000 and 2004 have only this; later ones the 46 as well
    if (find_block(xdata, "ACAD_MTEXT_DEFINED_HEIGHT", list, begin, end)) {
        const auto& items = xdata[list].items;
        double height = 0;
        bool ok = end - begin == 3 && is_key(items[begin + 1], 46) &&
                  item_number(items[begin + 2], 1040, height);
        if (ok && (d.defined_height == 0.0 || d.defined_height == height)) {
            d.defined_height = height;
            erase_block(xdata, list, begin, end);
        }
    }
}

XdataList mtext_xdata(const MTextData& d, DxfVersion version) {
    XdataList list;
    list.app_name = "ACAD";
    auto& items = list.items;
    auto number = [&](int key, int code, const std::string& v) {
        items.push_back({1070, std::to_string(key)});
        items.push_back({code, v});
    };
    if (version < DxfVersion::R2018 && (d.background.flags & 16)) {
        items.push_back({1000, "ACAD_MTEXT_TEXT_BORDERS_BEGIN"});
        number(80, 1070, "16");         // the frame; the fill's bits are in 90 only
        number(46, 1040, format_double(d.background.scale, NumberStyle::AutoCad));
        number(81, 1070, std::to_string(d.background.border_handles.size()));
        for (const auto& h : d.background.border_handles) number(5, 1005, h);
        items.push_back({1000, "ACAD_MTEXT_TEXT_BORDERS_END"});
    }
    if (version < DxfVersion::R2018 && d.columns) {
        column_info_items(*d.columns, items);
        if (!d.columns->linked.empty()) {
            items.push_back({1000, "ACAD_MTEXT_COLUMNS_BEGIN"});
            number(47, 1070, std::to_string(d.columns->linked.size() + 1));
            for (const auto& h : d.columns->linked) items.push_back({1005, h});
            items.push_back({1000, "ACAD_MTEXT_COLUMNS_END"});
        }
    }
    if (version < DxfVersion::R2018 && d.defined_height != 0.0) {
        items.push_back({1000, "ACAD_MTEXT_DEFINED_HEIGHT_BEGIN"});
        number(46, 1040, format_double(d.defined_height, NumberStyle::AutoCad));
        items.push_back({1000, "ACAD_MTEXT_DEFINED_HEIGHT_END"});
    }
    if (items.empty()) list.app_name.clear();
    return list;
}

std::vector<std::string> column_pieces(const std::string& text, const MTextColumns& columns) {
    size_t count = columns.count > 1 && columns.type != 0 ? static_cast<size_t>(columns.count) : 1;
    std::vector<std::string> pieces(count);
    if (columns.type != 1) {
        pieces[0] = text;
        return pieces;
    }
    // After each column break: \N, where the backslash is not itself escaped
    size_t from = 0, piece = 0;
    for (size_t i = 0; i < text.size() && piece + 1 < count; ++i) {
        if (text[i] != '\\' || i + 1 >= text.size()) continue;
        if (text[i + 1] == 'N') {
            pieces[piece++] = text.substr(from, i + 2 - from);
            from = i + 2;
        }
        ++i;
    }
    pieces[piece] = text.substr(from);
    return pieces;
}

} // namespace internal
} // namespace hdxf
