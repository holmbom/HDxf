#include "hdxf.hpp"
#include "handles.hpp"
#include "mtext_data.hpp"
#include "text_layout.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// MTEXT with several columns and with a frame up to 2013 (plan phase 6, B73, B74): the other
// columns are MTEXT entities and the frame LWPOLYLINEs of their own, linked from the first by
// XDATA — AutoCAD's own files (ezdxf's mtext_framed_columns and mtext_text_frame). In the drawing
// the text is one value: reading folds the pieces into the first entity's content (the others'
// are empty) and the writer cuts them again — static columns after each column break \N,
// dynamic ones leave the others empty, as AutoCAD writes them. Saving makes, updates and removes
// the linked entities after the first one's columns and frame.

namespace hdxf {

namespace {

uint64_t handle_value(const std::string& h) {
    uint64_t v = 0;
    return internal::parse_handle(h, v) == internal::HandleParse::Ok ? v : 0;
}

// The fields of a text a linked entity or frame is computed from
std::string key_of(const Entity& e) {
    const MTextData& d = e.as_mtext();
    std::ostringstream o;
    o.precision(17);
    o << d.insertion_point.x << ',' << d.insertion_point.y << ',' << d.insertion_point.z << ';' << d.height << ';'
      << d.reference_width << ';' << d.attachment << ';' << d.drawing_direction << ';' << d.defined_height << ';'
      << d.style << ';' << d.x_axis_direction.x << ',' << d.x_axis_direction.y << ',' << d.x_axis_direction.z << ';'
      << d.line_spacing_style << ';' << d.line_spacing_factor << ';' << d.background.flags << ';' << d.background.scale
      << ';' << e.props.extrusion.x << ',' << e.props.extrusion.y << ',' << e.props.extrusion.z << ';' << e.props.layer << ';' << e.props.color.aci << ',' << e.props.color.rgb << ','
      << e.props.color.name << ';' << e.props.linetype << ';' << e.props.visible << ';' << e.props.linetype_scale << ';'
      << e.props.thickness << ';' << e.props.paper_space << ';' << e.props.lineweight << ';' << e.props.plot_style_type << ','
      << e.props.plot_style_handle << ';' << e.props.transparency.raw << ';' << e.props.material_handle << ';'
      << e.props.shadow_mode << ';';
    if (d.columns) {
        const MTextColumns& c = *d.columns;
        o << c.type << ',' << c.count << ',' << c.width << ',' << c.gutter << ',' << c.auto_height << ',' << c.reversed;
        for (double h : c.heights) o << ',' << h;
    }
    o << ';' << d.content;
    return o.str();
}

Point3 unit(Point3 v) {
    double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return l > 0.0 ? Point3{v.x / l, v.y / l, v.z / l} : Point3{1.0, 0.0, 0.0};
}

// The properties a linked entity has as the first one has them
EntityProperties linked_props(const EntityProperties& p) {
    EntityProperties out = p;
    out.handle.clear();
    out.xdictionary.clear();
    out.reactors.clear();
    out.xdata.clear();
    return out;
}

// A linked entity that is kept takes the first one's display properties (its layer, color …) and
// keeps its own handle, extension dictionary, reactors and XDATA
void inherit_props(EntityProperties& child, const EntityProperties& parent) {
    EntityProperties p = linked_props(parent);
    p.handle = child.handle;
    p.xdictionary = child.xdictionary;
    p.reactors = child.reactors;
    p.xdata = child.xdata;
    child = std::move(p);
}

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

} // namespace

const Entity* Drawing::find_entity(const std::string& handle) const {
    uint64_t v = handle_value(handle);
    if (v == 0) return nullptr;
    for (const auto& e : entities_) if (handle_value(e.props.handle) == v) return &e;
    for (const auto& b : blocks_) {
        for (const auto& e : b.entities) if (handle_value(e.props.handle) == v) return &e;
    }
    return nullptr;
}

// Reading: the text of a first column gets the pieces of the others (they keep none); each
// linked entity is told whose it is
void Drawing::link_mtext_columns() {
    auto link = [](std::vector<Entity>& list) {
        std::unordered_map<uint64_t, Entity*> by_handle;
        for (auto& e : list) {
            if (uint64_t v = handle_value(e.props.handle)) by_handle[v] = &e;
        }
        for (auto& e : list) {
            if (e.type != EntityType::MText) continue;
            MTextData& d = e.as_mtext();
            if (d.columns) {
                for (const auto& h : d.columns->linked) {
                    auto it = by_handle.find(handle_value(h));
                    if (it == by_handle.end() || it->second->type != EntityType::MText) continue;
                    MTextData& child = it->second->as_mtext();
                    if (!child.column_parent.empty()) continue;         // folded already
                    child.column_parent = e.props.handle;
                    d.content += child.content;
                    child.content.clear();
                }
            }
            for (const auto& h : d.background.border_handles) {
                auto it = by_handle.find(handle_value(h));
                if (it != by_handle.end() && it->second->type == EntityType::LwPolyline) {
                    it->second->as_lwpolyline().frame_of = e.props.handle;
                }
            }
        }
    };
    link(entities_);
    for (auto& b : blocks_) link(b.entities);
}

namespace {

// A text's frame in the plane of the text: the box of the column `column` of `columns` from the
// insertion point (x along the direction, y up), grown by `offset` on every side
struct FrameBox {
    double x0, y0, x1, y1;
};

} // namespace

// Saving (plan B74). Before 2018 a text with columns has an entity for each column but the
// first, and one with a frame an LWPOLYLINE for each column; a text that is new or has been
// changed since it was read (`original`: the drawing as read, or none) gets them made, or its
// own updated. From 2018 the linked entities are not there: their pieces go back into the first
// text. What no text points to any more (a text removed, its columns or frame taken away) goes.
void Drawing::sync_mtext_columns(const Drawing* original) {
    bool older = version_ < DxfVersion::R2018;
    std::unique_ptr<internal::TextLayout> layout;
    scan_handles();                 // the handles it gives out are above every one the drawing has

    auto sync = [&](std::vector<Entity>& list, const std::vector<Entity>* before) {
        std::unordered_map<uint64_t, std::string> original_keys;
        if (before) {
            for (const auto& e : *before) {
                if (e.type == EntityType::MText) original_keys[handle_value(e.props.handle)] = key_of(e);
            }
        }
        // Every linked entity by its first column's handle
        auto linked_of = [&](const std::string& parent, EntityType type) {
            std::vector<size_t> out;
            for (size_t i = 0; i < list.size(); ++i) {
                if (list[i].type != type) continue;
                const std::string& owner = type == EntityType::MText ? list[i].as_mtext().column_parent
                                                                     : list[i].as_lwpolyline().frame_of;
                if (!owner.empty() && handle_value(owner) == handle_value(parent)) out.push_back(i);
            }
            return out;
        };
        auto index_of = [&](const std::string& handle) {
            size_t at = 0;
            while (at < list.size() && handle_value(list[at].props.handle) != handle_value(handle)) ++at;
            return at;
        };
        auto erase_all = [&](std::vector<size_t> indices) {
            std::sort(indices.begin(), indices.end(), std::greater<size_t>());
            for (size_t i : indices) list.erase(list.begin() + static_cast<long>(i));
        };

        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].type != EntityType::MText || !list[i].as_mtext().column_parent.empty()) continue;
            // Only a text with several columns, a frame, or entities linked to it is looked at
            {
                const MTextData& d = list[i].as_mtext();
                bool columns = d.columns && (d.columns->count > 1 || !d.columns->linked.empty());
                bool frame = (d.background.flags & 16) || !d.background.border_handles.empty();
                if (!columns && !frame) continue;
            }
            if (list[i].props.handle.empty()) list[i].props.handle = next_handle();
            const std::string parent = list[i].props.handle;
            auto changed = [&]() {
                auto it = original_keys.find(handle_value(parent));
                return !before || it == original_keys.end() || it->second != key_of(list[i]);
            };

            // ---- The other columns
            {
                MTextData& d = list[i].as_mtext();
                std::vector<size_t> kids = linked_of(parent, EntityType::MText);
                size_t needed = older && d.columns && d.columns->type != 0 && d.columns->count > 1
                                    ? static_cast<size_t>(d.columns->count - 1) : 0;
                bool consistent = needed == kids.size() && (!d.columns || d.columns->linked.size() == needed);
                for (size_t k = 0; consistent && k < kids.size(); ++k) {
                    consistent = handle_value(list[kids[k]].props.handle) == handle_value(d.columns->linked[k]);
                }
                if (needed == 0 && !kids.empty()) {
                    // The columns are gone, or this is 2018: their pieces go back into the text
                    for (size_t k : kids) {
                        d.content += list[k].as_mtext().content;
                        list[k].as_mtext().content.clear();
                    }
                    erase_all(kids);
                    if (d.columns) d.columns->linked.clear();
                } else if (needed > 0 && (!consistent || changed())) {
                    if (!consistent) {
                        erase_all(kids);
                        kids.clear();
                        // erase_all may have moved the first column
                        i = index_of(parent);
                    }
                    MTextData& pd = list[i].as_mtext();
                    const MTextColumns& c = *pd.columns;
                    Point3 dir = unit(pd.x_axis_direction);
                    double pitch = c.width + c.gutter;
                    double total = c.width * static_cast<double>(c.count) + c.gutter * static_cast<double>(c.count - 1);
                    std::vector<std::string> handles;
                    std::vector<Entity> made;
                    for (size_t k = 0; k < needed; ++k) {
                        Entity child;
                        if (consistent) {
                            child = list[kids[k]];
                            inherit_props(child.props, list[i].props);
                        } else {
                            child.type = EntityType::MText;
                            child.props = linked_props(list[i].props);
                            child.props.handle = next_handle();
                        }
                        MTextData m = pd;
                        m.content.clear();
                        m.columns.reset();
                        m.embedded.reset();
                        m.background.border_handles.clear();
                        m.background.flags &= ~16;
                        m.column_parent = parent;
                        m.reference_width = total;
                        double step = pitch * static_cast<double>(k + 1);
                        m.insertion_point = {pd.insertion_point.x + dir.x * step, pd.insertion_point.y + dir.y * step,
                                             pd.insertion_point.z + dir.z * step};
                        child.data = std::move(m);
                        handles.push_back(child.props.handle);
                        made.push_back(std::move(child));
                    }
                    if (consistent) {
                        for (size_t k = 0; k < needed; ++k) list[kids[k]] = std::move(made[k]);
                    } else {
                        list.insert(list.begin() + static_cast<long>(i) + 1, std::make_move_iterator(made.begin()),
                                    std::make_move_iterator(made.end()));
                    }
                    list[i].as_mtext().columns->linked = handles;
                }
            }

            // ---- The frame
            {
                MTextData& d = list[i].as_mtext();
                std::vector<size_t> frames = linked_of(parent, EntityType::LwPolyline);
                size_t columns = d.columns && d.columns->type != 0 && d.columns->count > 1 ? static_cast<size_t>(d.columns->count) : 1;
                size_t needed = older && (d.background.flags & 16) ? columns : 0;
                bool consistent = needed == frames.size() && d.background.border_handles.size() == needed;
                for (size_t k = 0; consistent && k < frames.size(); ++k) {
                    consistent = handle_value(list[frames[k]].props.handle) == handle_value(d.background.border_handles[k]);
                }
                if (needed == 0) {
                    erase_all(frames);
                    d.background.border_handles.clear();
                } else if (!consistent || changed()) {
                    if (!consistent) {
                        erase_all(frames);
                        frames.clear();
                        i = index_of(parent);
                    }
                    const Entity first = list[i];
                    const MTextData& pd = first.as_mtext();
                    // The text's size: the width of its lines and the height of the column
                    internal::MTextSize size;
                    if (!layout) layout = std::make_unique<internal::TextLayout>(*this);
                    size = layout->mtext(pd);
                    double text_height = size.font ? size.height : pd.height;
                    double text_width = size.font ? size.width : 0.0;
                    double offset = (pd.background.scale - 1.0) * pd.height;
                    Point3 dir = unit(pd.x_axis_direction);
                    Point3 n = unit(first.props.extrusion);
                    Point3 up = {n.y * dir.z - n.z * dir.y, n.z * dir.x - n.x * dir.z, n.x * dir.y - n.y * dir.x};
                    double column_width = pd.columns && pd.columns->width > 0.0 ? pd.columns->width
                                          : pd.reference_width > 0.0 ? pd.reference_width : text_width;
                    double pitch = pd.columns ? pd.columns->width + pd.columns->gutter : 0.0;
                    // Attachment: the box hangs from the insertion point as the text does
                    int col = pd.attachment >= 1 && pd.attachment <= 9 ? (pd.attachment - 1) % 3 : 0;
                    int row = pd.attachment >= 1 && pd.attachment <= 9 ? (pd.attachment - 1) / 3 : 0;
                    double total_width = column_width * static_cast<double>(columns) +
                                         (pd.columns ? pd.columns->gutter : 0.0) * static_cast<double>(columns - 1);
                    double x_left = -total_width * col / 2.0;
                    double y_top = text_height * row / 2.0;
                    double column_height = pd.defined_height > 0.0 && columns > 1 ? pd.defined_height : text_height;
                    std::vector<std::string> handles;
                    for (size_t k = 0; k < needed; ++k) {
                        double x0 = x_left + pitch * static_cast<double>(k) - offset;
                        double x1 = x_left + pitch * static_cast<double>(k) + column_width + offset;
                        double y0 = y_top + offset;
                        double y1 = y_top - column_height - offset;
                        Entity frame;
                        if (consistent) {
                            frame = list[frames[k]];
                            inherit_props(frame.props, first.props);
                        } else {
                            frame.type = EntityType::LwPolyline;
                            frame.props = linked_props(first.props);
                            frame.props.handle = next_handle();
                        }
                        LwPolylineData poly;
                        poly.flags = 1;
                        poly.frame_of = parent;
                        const Point3 corners[4] = {{x0, y0, 0}, {x0, y1, 0}, {x1, y1, 0}, {x1, y0, 0}};
                        for (const Point3& c : corners) {
                            Point3 w = {pd.insertion_point.x + dir.x * c.x + up.x * c.y,
                                        pd.insertion_point.y + dir.y * c.x + up.y * c.y,
                                        pd.insertion_point.z + dir.z * c.x + up.z * c.y};
                            Point3 o = ecs::wcs_to_ecs(w, first.props.extrusion);
                            LwVertex v;
                            v.position = {o.x, o.y};
                            poly.vertices.push_back(v);
                            poly.elevation = o.z;
                        }
                        frame.data = std::move(poly);
                        handles.push_back(frame.props.handle);
                        if (consistent) {
                            list[frames[k]] = std::move(frame);
                        } else {
                            // After the first column and its linked ones
                            size_t at = i + 1;
                            while (at < list.size() && list[at].type == EntityType::MText &&
                                   handle_value(list[at].as_mtext().column_parent) == handle_value(parent)) ++at;
                            at += k;
                            list.insert(list.begin() + static_cast<long>(at), std::move(frame));
                        }
                    }
                    list[i].as_mtext().background.border_handles = handles;
                }
            }
        }

        // What no text points to any more
        std::unordered_set<uint64_t> owned;
        for (const auto& e : list) {
            if (e.type != EntityType::MText) continue;
            if (e.as_mtext().columns) for (const auto& h : e.as_mtext().columns->linked) owned.insert(handle_value(h));
            for (const auto& h : e.as_mtext().background.border_handles) owned.insert(handle_value(h));
        }
        std::vector<size_t> orphans;
        for (size_t i = 0; i < list.size(); ++i) {
            bool linked = (list[i].type == EntityType::MText && !list[i].as_mtext().column_parent.empty()) ||
                          (list[i].type == EntityType::LwPolyline && !list[i].as_lwpolyline().frame_of.empty());
            if (linked && !owned.count(handle_value(list[i].props.handle))) orphans.push_back(i);
        }
        erase_all(orphans);
    };

    sync(entities_, original ? &original->entities_ : nullptr);
    for (size_t b = 0; b < blocks_.size(); ++b) {
        const std::vector<Entity>* before = nullptr;
        if (original) {
            for (const auto& ob : original->blocks_) if (same_name(ob.name, blocks_[b].name)) before = &ob.entities;
        }
        sync(blocks_[b].entities, before);
    }
}

} // namespace hdxf
