#pragma once

#include "hdxf.hpp"
#include <string>

namespace hdxf {
namespace internal {

// What a drawing has that DXF R12, R13 or R14 cannot be written with (plan phase 7, step 7.20, B94, B98),
// the first fault found, or an empty string when there is none. The traversal covers everything
// before anything is written or changed: the entities of model space, paper space and every block,
// each INSERT's attributes, the blocks' own properties, the objects, the header, the dimension styles and
// the tables (older_tables_error()). The faults are the types and properties later versions
// introduced (a lineweight, a plot style, a LAYOUT, an LWPOLYLINE in R13 …), the names an older version
// cannot hold, and the 2000 fields of a viewport or a dimension; the properties the later versions
// introduced one by one (true color, transparency, material …) have their own checks after these
// (Drawing::write_dxf()).
//
// `original`: the drawing as it was read from the file it is written back to, null for a drawing built in
// memory. A record the file had is written from its preserved record: what the file's own record has is not
// the program's fault, only what it added or changed is.
std::string older_version_error(const Drawing& dwg, DxfVersion version, const Drawing* original);

// "<TYPE> requires DXF <version> or later" when the type is newer than `target` (an R12 target: ELLIPSE,
// XLINE, RAY, MTEXT, TOLERANCE, LEADER, SPLINE, MLINE, 3DSOLID, REGION and BODY are R13's, LWPOLYLINE,
// HATCH and IMAGE R14's, WIPEOUT, MULTILEADER, HELIX and MESH 2000's, format profile 12); empty
// for an R12 type and for the types whose own check says their version (surfaces, LIGHT,
// COORDINATION_MODEL). Used for R12, R13 and R14.
std::string entity_type_error(EntityType type, DxfVersion target);

} // namespace internal
} // namespace hdxf
