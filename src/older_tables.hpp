#pragma once

#include "hdxf.hpp"
#include <string>

namespace hdxf {
namespace internal {

// The rule for a symbol name written as DXF R13 or R14 (plan phase 7, step 7.21, B90, B98): at most 31
// characters, and the letters A to Z, the digits 0 to 9, the dollar sign, the underscore and the hyphen —
// Autodesk's description of $EXTNAMES 0, "Release 14 compatibility" (the DXF reference, header
// variables); a name AutoCAD itself saved as R13 or R14 has no other character (the 1376 names of the 27
// files of format profile 1.4). A name that starts with "*" is a system or anonymous name (*Model_Space,
// *D1, *U2, *ACTIVE): the rest of it follows the rule. A record that depends on an xref (70 bit 16) has
// "|" too. `what` names the kind ("Layer", "Block" …). An empty string when the name is fine, otherwise
// the error: never a shortened or renamed name.
std::string older_symbol_name_error(const std::string& what, const std::string& name, bool xref_dependent = false);

// A multiline style's name, DXF R13 and R14: at most 32 characters (R13 reference, MLINE and
// $CMLSTYLE: "string of up to 32 characters"); the reference names no characters
std::string older_mline_style_name_error(const std::string& name);

// What the tables of a drawing have that DXF R13 and R14 cannot hold (plan phase 7, steps 7.6 and
// 7.21, B90, B98); empty when there is nothing. A symbol name that breaks older_symbol_name_error(),
// a layer's lineweight, plot style, plot flag and transparency, and the UCS data of a VPORT, VIEW or
// UCS record, which these versions have no groups for: an error that names it, never a changed name or a
// lost value. `original`: the drawing as it was read from the file it is written back to (null for a
// drawing built in memory): a record the file had with the same fault is written from its preserved
// record and is not the program's.
std::string older_tables_error(const Drawing& dwg, const Drawing* original);

} // namespace internal
} // namespace hdxf
