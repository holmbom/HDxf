#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// SAT → SAB (plan phase 5, B62/B63): a body's SAT text made into the SAB 21800 that AutoCAD
// writes in the ACDSDATA section of a DXF 2013 file
//
// The SAB is AutoCAD's: "ACIS BinaryFile", the head (21800, 0, 2, 12), the product, the ACIS
// version and the date of the SAT's header (an empty date stays empty), the unit, resabs and
// resnor, then each record in 21800's form — its fields typed by the record type's field list
// in the SAT's version (src/acis_sab.cpp) — and End-of-ASM-data. Token for token AutoCAD's own
// SAB of the same body (ACadSharp's and LibreDWG's 2007/2010 files against their 2013 files,
// and AutoCAD's chain for the others, B66). Every version from 1.0 to 21800 (SAT 106 … 21800):
// before 20800 the asmheader is put in front (ODA's version 208.0.4.7009, or the SAT header's
// "ASM <version>"), and what the version does not save the SAB has as AutoCAD's chain makes it.
//
// DXF 2018 (phase 6, step 6.8, B80/B81): the same records in 22300's SAB, "ASM BinaryFile4" with the head
// (22300, 0, 2, 4). AutoCAD's 22300 stream is its 21800 stream, record for record and value for value
// (ACadSharp's and LibreDWG's 2018 files against their 2013 files, and ODA's ACAD2018 DXF against its
// ACAD2013 DXF of the same DWG for 190 bodies): signature, version and flags are all that differs.
// ---------------------------------------------------------------------------

namespace hdxf {
namespace internal {

// The SAB's format version (its head's first number): 21800, "ACIS BinaryFile", in DXF 2013 …
// 2017 files; 22300, "ASM BinaryFile4", in DXF 2018 files (phase 6, B80)
constexpr int sab_format_acis = 21800;
constexpr int sab_format_asm = 22300;

// The format of the SAB made for a drawing of that version: 22300 from 2018 on, else 21800
inline int sab_format_for(DxfVersion version) {
    return version >= DxfVersion::R2018 ? sab_format_asm : sab_format_acis;
}

// The SAT text (acis_deciphered()) as SAB 21800. False, with `error` saying why, when the data
// cannot be read as SAT, is of a version after 21800, has a record type, subtype or word the
// encoder does not have (no oracle has them: AutoCAD's core reads none of them), or a record does
// not have its type's fields.
// `format` says which SAB it is made as; the topology, the geometry and the attributes are the same
// records, only the head differs (signature, version, flags: B81). Errors name DXF 2013 for 21800
// and DXF 2018 for 22300.
bool sab_from_sat(const std::string& sat, std::vector<uint8_t>& out, std::string& error,
                  int format = sab_format_acis);

} // namespace internal
} // namespace hdxf
