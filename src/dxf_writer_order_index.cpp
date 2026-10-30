#include "OrderIndexTypes.hpp"
#include <cctype>

// Draw order, clipping and indexes (phase 9, step 9.4): writing the six object types as AutoCAD writes them (the
// groups in the order of its own files). A pointer to a record that is gone is left out (B19): the pair of a
// draw order, the entry of a layer index, the member of an IDBUFFER go whole, as AutoCAD removes them.

namespace hdxf {
namespace internal {

namespace {

bool same_name(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

// A layer's name as the version writes it: R13 and R14 name a table record in the spelling of the record
// (the writer's rule for every layer name there); 2000 and later as it is given
std::string layer_text(const RecordWriter& w, const std::string& name) {
    if (!w.older()) return name;
    for (const Layer& l : w.drawing().layers()) {
        if (same_name(l.name, name)) return l.name;
    }
    return name;
}

// The number of entries of a layer's IDBUFFER: the members that are written when the drawing has the IDBUFFER, what
// the entry holds when it does not (and while the drawing as it was read is written for comparison, B12)
int idbuffer_count(const RecordWriter& w, const LayerIndexEntry& entry) {
    if (!w.filtering()) return entry.count;
    const Object* buffer = w.drawing().find_object(entry.idbuffer_handle);
    if (!buffer || buffer->type != ObjectType::IdBuffer) return entry.count;
    int n = 0;
    for (const std::string& h : buffer->as_id_buffer().object_handles) {
        if (w.live(h)) ++n;
    }
    return n;
}

} // namespace

void write_sortents_table(RecordWriter& w, const Object& o) {
    const SortentsTableData& d = o.as_sortents_table();
    w.object_head("SORTENTSTABLE", o);
    w.subclass("AcDbSortentsTable");
    if (!d.block_record_handle.empty() && !w.live(d.block_record_handle)) {
        w.warn("SORTENTSTABLE " + o.handle + ": the block record " + d.block_record_handle + " no longer exists, set to none (0)");
    }
    w.pointer(330, d.block_record_handle, PointerWrite::ZeroDead);
    for (const SortEntry& e : d.entries) {
        const std::string& entity = e.entity_handle.empty() ? std::string("0") : e.entity_handle;
        if (!w.live(entity)) continue;              // the entity is gone: its pair goes (the key is no pointer)
        w.text(331, entity);
        w.text(5, e.sort_key);
    }
    w.xdata(o.xdata);
}

void write_spatial_filter(RecordWriter& w, const Object& o) {
    const SpatialFilterData& d = o.as_spatial_filter();
    w.object_head("SPATIAL_FILTER", o);
    w.subclass("AcDbFilter");
    w.subclass("AcDbSpatialFilter");
    w.integer(70, static_cast<int64_t>(d.boundary.size()));
    for (const Point2& p : d.boundary) w.point2(10, p);
    w.point3(210, d.normal);
    w.point3(11, d.origin);
    w.boolean(71, d.enabled);
    w.boolean(72, d.front_distance.has_value());
    if (d.front_distance) w.real(40, *d.front_distance);
    w.boolean(73, d.back_distance.has_value());
    if (d.back_distance) w.real(41, *d.back_distance);
    for (double v : d.inverse_insert_transform) w.real(40, v);
    for (double v : d.clip_transform) w.real(40, v);
    w.xdata(o.xdata);
}

void write_layer_index(RecordWriter& w, const Object& o) {
    const LayerIndexData& d = o.as_layer_index();
    w.object_head("LAYER_INDEX", o);
    w.subclass("AcDbIndex");
    w.real(40, d.timestamp);
    w.subclass("AcDbLayerIndex");
    for (const LayerIndexEntry& e : d.entries) {
        const std::string& buffer = e.idbuffer_handle.empty() ? std::string("0") : e.idbuffer_handle;
        if (!w.live(buffer)) continue;              // the IDBUFFER is gone: the entry goes whole (name, owner, count)
        w.text(8, layer_text(w, e.layer_name));
        w.text(360, buffer);
        w.integer(90, idbuffer_count(w, e));
    }
    w.xdata(o.xdata);
}

void write_spatial_index(RecordWriter& w, const Object& o) {
    const SpatialIndexData& d = o.as_spatial_index();
    w.object_head("SPATIAL_INDEX", o);
    w.subclass("AcDbIndex");
    w.real(40, d.timestamp);
    w.subclass("AcDbSpatialIndex");
    // R13 and R14 have the index empty (the reference); the tree is AutoCAD's 2000-and-later form
    if (d.tree && !w.older()) {
        const SpatialIndexTree& t = *d.tree;
        for (double v : t.values) w.real(40, v);
        w.integer(90, t.reserved);
        w.integer(90, static_cast<int64_t>(t.data.size()));
        w.bytes(310, t.data);
        w.text(1, t.end_marker);
    }
    w.xdata(o.xdata);
}

void write_id_buffer(RecordWriter& w, const Object& o) {
    w.object_head("IDBUFFER", o);
    w.subclass("AcDbIdBuffer");
    for (const std::string& h : o.as_id_buffer().object_handles) {
        const std::string& member = h.empty() ? std::string("0") : h;
        if (w.live(member)) w.text(330, member);
    }
    w.xdata(o.xdata);
}

void write_layer_filter(RecordWriter& w, const Object& o) {
    // R13 and R14: no file shows the record there (no version error is claimed, a record read stays as it was): a
    // new one is not made
    if (w.older() && o.raw.empty()) {
        w.fail("LAYER_FILTER in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    w.object_head("LAYER_FILTER", o);
    w.subclass("AcDbFilter");
    w.subclass("AcDbLayerFilter");
    for (const std::string& layer : o.as_layer_filter().layers) w.text(8, layer_text(w, layer));
    w.xdata(o.xdata);
}

} // namespace internal
} // namespace hdxf
