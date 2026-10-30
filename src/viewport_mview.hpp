#pragma once

#include "hdxf.hpp"

// The view of a VIEWPORT in R12, R13 and R14: XDATA of the application ACAD, 1000 MVIEW (Autodesk's
// R12, R13 and R14 DXF references, VIEWPORT). From 2000 the same values are group codes of the
// entity (ViewportData). Written by DxfWriter::write_viewport_older() (R13, R14), read here for
// the files of those versions (phase 7, step 7.16) and for R12's when it is written as 2000 or later.

namespace hdxf {
namespace internal {

// Whether the XDATA list is a viewport's ACAD/MVIEW
bool is_mview(const XdataList& xd);

// The values of an ACAD/MVIEW list in the viewport's typed fields (view target and direction,
// twist, height, center, lens, clipping, the view mode and on/off settings as the bits of
// `status_flags`, snap and grid, `circle_zoom`, the frozen layers by name); false, and the
// viewport untouched, when the list is not a complete MVIEW
bool view_from_mview(const XdataList& xd, ViewportData& d);

} // namespace internal
} // namespace hdxf
