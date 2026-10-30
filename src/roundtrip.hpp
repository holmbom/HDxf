#pragma once

#include "hdxf.hpp"
#include <string>

namespace hdxf {
namespace internal {

// What 2013 has in its records and 2000 … 2010 as round-trip XRECORDs and XDATA (plan phase 5,
// B60; drawing_roundtrip.cpp)

// The ACIS body of a 3DSOLID, REGION, BODY or surface; nullptr for any other type
const AcisData* acis_of(const Entity& e);
AcisData* acis_of(Entity& e);

// A change GUID "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" as AutoCAD's XDATA before 2013:
// ACAD_OBJECT_CHANGE_GUID, 1070 1, 1071 Data1, 1070 Data2, 1070 Data3 (16 bits with sign) and
// a 1070 per byte of Data4. False when the string is not a GUID.
bool change_guid_xdata(const std::string& guid, XdataList& out);
// The GUID ("{…}", lowercase) of exactly that XDATA; false for anything else
bool change_guid_of(const XdataList& x, std::string& guid);

} // namespace internal
} // namespace hdxf
