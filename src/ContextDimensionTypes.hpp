#pragma once

#include "TypedRecordTypes.hpp"

// Annotation contexts, DIMASSOC and the special dimensions (phase 9, step 9.17; plan 2026-10-27-fas-9-typer.md, B100-B107,
// B112-B116). Internal; the data types are in hdxf.hpp. The codecs: object_context.cpp (the 16 contexts), dimassoc.cpp
// (DIMASSOC), dimension_special.cpp (ARC_DIMENSION, LARGE_RADIAL_DIMENSION); what they write is in
// dxf_writer_context_dimension.cpp.

namespace hdxf {
namespace internal {

// Registered by register_typed_families() (typed_families.cpp)
void register_context_codecs(TypedRegistry& registry);          // object_context.cpp
void register_dimassoc_codecs(TypedRegistry& registry);         // dimassoc.cpp
void register_special_dimension_codecs(TypedRegistry& registry);    // dimension_special.cpp

// What the codecs write (dxf_writer_context_dimension.cpp): each the whole record, its head and, last, its XDATA
void write_text_context(RecordWriter& w, const Object& o);
void write_leader_context(RecordWriter& w, const Object& o);
void write_mtext_context(RecordWriter& w, const Object& o);
void write_mtext_attribute_context(RecordWriter& w, const Object& o);
void write_mleader_context(RecordWriter& w, const Object& o);
void write_blkref_context(RecordWriter& w, const Object& o);
void write_aldim_context(RecordWriter& w, const Object& o);
void write_angdim_context(RecordWriter& w, const Object& o);
void write_dmdim_context(RecordWriter& w, const Object& o);
void write_orddim_context(RecordWriter& w, const Object& o);
void write_radim_context(RecordWriter& w, const Object& o);
void write_radimlg_context(RecordWriter& w, const Object& o);
void write_fcf_context(RecordWriter& w, const Object& o);
void write_annot_scale_context(RecordWriter& w, const Object& o);
void write_hatch_scale_context(RecordWriter& w, const Object& o);
void write_hatch_view_context(RecordWriter& w, const Object& o);
void write_dim_assoc(RecordWriter& w, const Object& o);
void write_arc_dimension(RecordWriter& w, const Entity& e, const std::string& owner);
void write_large_radial_dimension(RecordWriter& w, const Entity& e, const std::string& owner);

// The groups of AcDbDimension from `100 AcDbDimension` up to and including the style (3), for a dimension of any of the three
// entity types: the block (its own, or the one built for it), 10 and 11 (as built), 70, the measurement (42), the text, the
// extrusion. The head (the entity's AcDbEntity part) is written before it by the caller, the type's own subclass after.
// (dxf_writer_context_dimension.cpp; it shares its body with DxfWriter::write_dimension_r2000.)
void write_dimension_common(RecordWriter& w, const Entity& e, const DimensionData& d);

// Reads the AcDbDimension subclass of the record into `d` and the entity's extrusion (210): false with ctx.fail() when it is
// not there or a value is wrong. A group after the ones known is kept in the preserved record with a warning (B105). 72 and 41
// (line spacing) go to `spacing_style`/`spacing_factor` when read (the special dimensions' own fields; 9.S, Codex 10)
bool read_dimension_common(const RecordView& view, TypedReadContext& ctx, Entity& e, DimensionData& d,
                           std::optional<int16_t>* spacing_style = nullptr, std::optional<double>* spacing_factor = nullptr);

// The links of a dimension block the writer made or kept: the extents of an ARC_DIMENSION and a LARGE_RADIAL_DIMENSION are
// those of a DIMENSION (its block and its definition point): extents.cpp calls the same code
void dimension_extents_points(const Entity& e, TypedExtentsSink& sink);

} // namespace internal
} // namespace hdxf
