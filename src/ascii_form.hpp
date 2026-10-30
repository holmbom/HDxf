#pragma once

#include <map>
#include <optional>
#include <string>

namespace hdxf {
namespace internal {

// How the ASCII file a drawing was read from is laid out as text, where that is not AutoCAD's form: kept so that
// the file written back is the file (plan phase 7, B12). Some R13/R14 files have no padding in their code lines
// and integers, keywords with their own column widths, no line ending after EOF or a blank line after it.
struct AsciiForm {
    bool plain = false;                         // code lines and integers without padding ("70", "8")
    std::optional<std::string> after_eof;       // what followed "EOF": the line ending, nothing, or more
    // The code line width of the keywords that mark the structure where it is not three columns: keys
    // "SECTION:<name>", "NAME:<name>", "ENDSEC:<name>", "ENDTAB#<n>" and "EOF"
    std::map<std::string, int> marker_columns;
};

} // namespace internal
} // namespace hdxf
