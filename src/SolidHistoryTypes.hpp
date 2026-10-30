#pragma once

#include "TypedRecordTypes.hpp"

// The solid history of AutoCAD 2007 and later (phase 9, step 9.14; plan 2026-10-27-fas-9-typer.md, B111): ACSH_HISTORY_CLASS and
// the node objects the history's ACAD_EVALUATION_GRAPH (step 9.9) owns. Internal; the data types are in hdxf.hpp (ShHistoryData,
// ShBoxData … ShFilletData). The codecs are in solid_history.cpp, what they write in dxf_writer_history.cpp.

namespace hdxf {
namespace internal {

// Registered by register_typed_families() (typed_families.cpp): the 16 codecs of the step
void register_solid_history_codecs(TypedRegistry& registry);

// What the codecs write (dxf_writer_history.cpp): each the whole record, its head and, last, its XDATA
void write_sh_history(RecordWriter& w, const Object& o);
void write_sh_box(RecordWriter& w, const Object& o);
void write_sh_wedge(RecordWriter& w, const Object& o);
void write_sh_sphere(RecordWriter& w, const Object& o);
void write_sh_cylinder(RecordWriter& w, const Object& o);
void write_sh_cone(RecordWriter& w, const Object& o);
void write_sh_torus(RecordWriter& w, const Object& o);
void write_sh_pyramid(RecordWriter& w, const Object& o);
void write_sh_extrusion(RecordWriter& w, const Object& o);
void write_sh_sweep(RecordWriter& w, const Object& o);
void write_sh_revolve(RecordWriter& w, const Object& o);
void write_sh_loft(RecordWriter& w, const Object& o);
void write_sh_boolean(RecordWriter& w, const Object& o);
void write_sh_brep(RecordWriter& w, const Object& o);
void write_sh_chamfer(RecordWriter& w, const Object& o);
void write_sh_fillet(RecordWriter& w, const Object& o);

} // namespace internal
} // namespace hdxf
