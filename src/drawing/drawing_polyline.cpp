#include "hdxf/drawing.hpp"
#include "hdxf/entity.hpp"
#include "hdxf/tables.hpp"
#include "drawing/drawing_polyline.hpp"
#include "io/handles.hpp"
#include <unordered_map>

namespace hdxf {

namespace internal {

PolylineSyncLog::~PolylineSyncLog() {
    for (auto it = strings_.rbegin(); it != strings_.rend(); ++it) *it->first = std::move(it->second);
    for (auto it = colors_.rbegin(); it != colors_.rend(); ++it) *it->first = std::move(it->second);
}

namespace {

uint64_t handle_number(const std::string& handle) {
    uint64_t value = 0;
    return parse_handle(handle, value) == HandleParse::Ok ? value : 0;
}

bool same_color(const Color& a, const Color& b) {
    return a.aci == b.aci && a.rgb == b.rgb && a.name == b.name;
}

// The three properties a VERTEX or SEQEND has of its owner, with the values the owner has now and as read (`was` null: no
// original). A child (VERTEX or SEQEND) is `layer`, `linetype` and `color`, and `child_was` what it was as read.
class OwnerValues {
public:
    OwnerValues(const EntityProperties& now, const EntityProperties* was, PolylineSyncLog& log)
        : now_(now), was_(was), log_(log) {}

    // Whether any of the owner's three properties differs from the original's
    bool changed() const {
        return was_ && (now_.layer != was_->layer || now_.linetype != was_->linetype || !same_color(now_.color, was_->color));
    }

    // A record with a counterpart in the original: each property follows the owner that changed it, if the record
    // had the owner's value as read and did not change it since
    void follow_as_read(std::string& layer, std::string& linetype, Color& color, const std::string& layer_was,
                        const std::string& linetype_was, const Color& color_was) const {
        if (now_.layer != was_->layer && !layer.empty()) {
            const std::string& effective_was = layer_was.empty() ? was_->layer : layer_was;
            if (layer == layer_was && effective_was == was_->layer) set(layer, now_.layer);
        }
        if (now_.linetype != was_->linetype && linetype == linetype_was && linetype_was == was_->linetype) {
            set(linetype, now_.linetype);
        }
        if (!same_color(now_.color, was_->color) && same_color(color, color_was) && same_color(color_was, was_->color)) {
            set(color, now_.color);
        }
    }

    // A record without an original: an empty linetype and a color that is ByLayer take the owner's
    void follow_as_made(std::string& linetype, Color& color) const {
        if (linetype.empty() && !now_.linetype.empty()) set(linetype, now_.linetype);
        if (color.is_by_layer() && !now_.color.is_by_layer()) set(color, now_.color);
    }

private:
    void set(std::string& field, const std::string& value) const {
        log_.note(field);
        field = value;
    }
    void set(Color& field, const Color& value) const {
        log_.note(field);
        field = value;
    }

    const EntityProperties& now_;
    const EntityProperties* was_;
    PolylineSyncLog& log_;
};

size_t records_read(const std::vector<Vertex>& vertices) {
    size_t n = 0;
    for (const auto& v : vertices) {
        if (!v.raw.empty()) ++n;
    }
    return n;
}

size_t records_read(const std::vector<Entity>& entities) {
    size_t n = 0;
    for (const auto& e : entities) {
        if (!e.raw.empty()) ++n;
    }
    return n;
}

// The vertices of one polyline against the original's
void sync_vertices(PolylineData& d, const PolylineData& was, const OwnerValues& owner) {
    if (!owner.changed()) {
        // Nothing to follow for the vertices read; only the ones made since take the owner's values
        for (auto& v : d.vertices) {
            if (v.raw.empty()) owner.follow_as_made(v.linetype, v.color);
        }
        return;
    }
    std::unordered_map<uint64_t, const Vertex*> by_handle;
    for (const auto& v : was.vertices) {
        if (uint64_t h = handle_number(v.handle)) by_handle[h] = &v;
    }
    const bool by_place = records_read(d.vertices) == was.vertices.size();
    size_t place = 0;
    for (auto& v : d.vertices) {
        if (v.raw.empty()) {
            owner.follow_as_made(v.linetype, v.color);
            continue;
        }
        const size_t at = place++;
        const Vertex* counterpart = nullptr;
        if (uint64_t h = handle_number(v.handle)) {
            auto it = by_handle.find(h);
            if (it != by_handle.end()) counterpart = it->second;
        }
        if (!counterpart && by_place) counterpart = &was.vertices[at];
        if (counterpart) owner.follow_as_read(v.layer, v.linetype, v.color, counterpart->layer, counterpart->linetype, counterpart->color);
    }
}

void sync_polyline(Entity& e, const Entity* was, PolylineSyncLog& log) {
    PolylineData& d = e.as_polyline();
    if (!was) {
        OwnerValues owner(e.props, nullptr, log);
        for (auto& v : d.vertices) owner.follow_as_made(v.linetype, v.color);
        owner.follow_as_made(d.seqend_linetype, d.seqend_color);
        return;
    }
    const PolylineData& wd = was->as_polyline();
    OwnerValues owner(e.props, &was->props, log);
    sync_vertices(d, wd, owner);
    if (d.seqend_raw.empty()) {
        owner.follow_as_made(d.seqend_linetype, d.seqend_color);
    } else if (owner.changed()) {
        owner.follow_as_read(d.seqend_layer, d.seqend_linetype, d.seqend_color, wd.seqend_layer, wd.seqend_linetype,
                             wd.seqend_color);
    }
}

// One list of entities against the original's list, and the polylines of the whole original by handle
void sync_list(std::vector<Entity>& list, const std::vector<Entity>* before,
               const std::unordered_map<uint64_t, const Entity*>& polylines, bool read_drawing, PolylineSyncLog& log) {
    const bool by_place = before && records_read(list) == before->size();
    size_t place = 0;
    for (auto& e : list) {
        const size_t at = e.raw.empty() ? 0 : place++;
        if (e.type != EntityType::Polyline) continue;
        const Entity* was = nullptr;
        if (read_drawing && !e.raw.empty()) {
            if (uint64_t h = handle_number(e.props.handle)) {
                auto it = polylines.find(h);
                if (it != polylines.end()) was = it->second;
            }
            if (!was && by_place && (*before)[at].type == EntityType::Polyline) was = &(*before)[at];
            if (!was) continue;     // read from the file and not found in it: left as it is
        }
        sync_polyline(e, was, log);
    }
}

} // namespace

void sync_polyline_properties(std::vector<Entity>& entities, std::vector<Block>& blocks, const Drawing* original,
                              PolylineSyncLog& log) {
    std::unordered_map<uint64_t, const Entity*> polylines;
    if (original) {
        auto collect = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) {
                if (e.type != EntityType::Polyline) continue;
                if (uint64_t h = handle_number(e.props.handle)) polylines[h] = &e;
            }
        };
        collect(original->entities());
        for (const auto& b : original->blocks()) collect(b.entities);
    }
    sync_list(entities, original ? &original->entities() : nullptr, polylines, original != nullptr, log);
    for (auto& b : blocks) {
        const Block* was = original ? original->find_block(b.name) : nullptr;
        sync_list(b.entities, was ? &was->entities : nullptr, polylines, original != nullptr, log);
    }
}

} // namespace internal

} // namespace hdxf
