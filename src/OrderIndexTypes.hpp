#pragma once

#include "TypedRecordTypes.hpp"

// Draw order, clipping and indexes (phase 9, step 9.4; plan 2026-10-27-fas-9-typer.md): the six object types
// SORTENTSTABLE, SPATIAL_FILTER, LAYER_INDEX, SPATIAL_INDEX, IDBUFFER and LAYER_FILTER, one codec each.
// The read functions and the registration are in objects_order_index.cpp, the write functions in
// dxf_writer_order_index.cpp.

namespace hdxf {
namespace internal {

// typed_families.cpp calls this
void register_order_index_codecs(TypedRegistry& registry);

// The writers (dxf_writer_order_index.cpp): the whole record, head and XDATA included
void write_sortents_table(RecordWriter& w, const Object& o);
void write_spatial_filter(RecordWriter& w, const Object& o);
void write_layer_index(RecordWriter& w, const Object& o);
void write_spatial_index(RecordWriter& w, const Object& o);
void write_id_buffer(RecordWriter& w, const Object& o);
void write_layer_filter(RecordWriter& w, const Object& o);

} // namespace internal
} // namespace hdxf
