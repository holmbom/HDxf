#pragma once

#include "TypedRecordTypes.hpp"

// Step 9.2 of phase 9 (plan 2026-10-27-fas-9-typer.md): the record types HDxf typed before phase 9 and that now
// have codecs, WIPEOUT, WIPEOUTVARIABLES and SCALE. The reading and the registration are in records_existing.cpp,
// the writing in dxf_writer_existing.cpp. The codecs give exactly the bytes the writer of 1.8.1 gave for a
// drawing that already worked (the plan's integration contract, point 4); what is new is in the plan's outcome.

namespace hdxf {
namespace internal {

// WIPEOUT: AcDbWipeout under AcDbEntity, the image fields of IMAGE (ImageData) without an IMAGEDEF
void write_wipeout_record(RecordWriter& w, const Entity& e, const std::string& owner);
// WIPEOUTVARIABLES: AcDbWipeoutVariables, 70 the frame display
void write_wipeout_variables_record(RecordWriter& w, const Object& o);
// SCALE: AcDbScale, an annotation scale of ACAD_SCALELIST
void write_scale_record(RecordWriter& w, const Object& o);

// The corners of what a WIPEOUT covers, in the WCS: its boundary as AutoCAD draws it (the clip polygon or
// rectangle through the pixel vectors) — what AutoCAD's own extents are made of (plan 9.2, F9.* measurement)
std::vector<Point3> wipeout_extent_points(const ImageData& d);

} // namespace internal
} // namespace hdxf
