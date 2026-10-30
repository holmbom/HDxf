#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <string>
#include <vector>

// MULTILEADER and MLEADERSTYLE in DXF (plan phase 3, B36/B37): their groups read into the
// typed data. Writing is the writer's (dxf_writer_mleader.cpp).

namespace hdxf {
namespace internal {

// The groups of a MULTILEADER's AcDbMLeader subclass (after its 100, before XDATA). Table
// records and blocks are their handles here; DxfReader::resolve_names() makes them names.
void parse_mleader(const std::vector<DxfGroup>& groups, MLeaderData& d);

// The groups of an MLEADERSTYLE's AcDbMLeaderStyle subclass (after its 100, before XDATA),
// handles as for MULTILEADER
MLeaderStyleData parse_mleader_style(const std::vector<DxfGroup>& groups);

// The version AutoCAD 2000-2007 keep in XDATA ACAD_MLEADERVER 1070 <version>: a block of
// exactly that form is taken out of xdata into version
void take_mleader_version(std::vector<XdataList>& xdata, int& version);

} // namespace internal
} // namespace hdxf
