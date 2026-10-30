#pragma once

#include "hdxf.hpp"
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The older form of a dimension style (DXF R13 and R14, plan phase 7, steps 7.7 and 7.8, B91).
//
// DIMUNIT (group 270, header $DIMUNIT) and DIMFIT (287, $DIMFIT) are the R13/R14 variables that
// 2000 split: DIMUNIT into DIMLUNIT (277) and DIMFRAC (276), DIMFIT into DIMATFIT (289) and
// DIMTMOVE (279). Autodesk's DXF reference calls 270 "obsolete, now use DIMLUNIT AND DIMFRAC" and
// 287 "obsolete, now DIMATFIT and DIMTMOVE"; the mapping is the one in Autodesk's own
// documentation: the system variable DIMFIT ("When DIMFIT is 0 through 3, DIMATFIT takes the same
// value and DIMTMOVE is 0. If DIMFIT is set to 4 or 5, then DIMATFIT is set to 3 and DIMTMOVE is
// set to 1 or 2") and ObjectARX's AcDmUtil::dimunit()/AcDbDatabase::setDimunit() for DIMUNIT:
//
//   DIMUNIT  1 scientific            -> DIMLUNIT 1
//            2 decimal               -> DIMLUNIT 2
//            3 engineering           -> DIMLUNIT 3
//            4 architectural stacked -> DIMLUNIT 4, DIMFRAC 0
//            5 fractional stacked    -> DIMLUNIT 5, DIMFRAC 0
//            6 architectural         -> DIMLUNIT 4, DIMFRAC 2 (not stacked)
//            7 fractional            -> DIMLUNIT 5, DIMFRAC 2
//            8 Windows desktop       -> DIMLUNIT 6
//
// The other way, AutoCAD's own dimunit() gives DIMFRAC 1 (diagonal stacking) the value of DIMFRAC 0;
// that would lose a setting without a word, so here it has no older value (an error where an
// older file is written). DIMFRAC means nothing to DIMLUNIT 1, 2, 3 and 6.
//
// Free functions, so that the DIMSTYLE writer, the header ($DIMUNIT, $DIMFIT of the current style)
// and the dimension blocks use one mapping.

namespace hdxf {
namespace internal {

struct UnitFormat {
    int dimlunit = 2;
    int dimfrac = 0;
};

struct FitRule {
    int dimatfit = 3;
    int dimtmove = 0;
};

// What DIMUNIT says as DIMLUNIT and DIMFRAC; none for a value outside 1 … 8
std::optional<UnitFormat> modern_from_dimunit(int dimunit);
// What DIMFIT says as DIMATFIT and DIMTMOVE; none for a value outside 0 … 5
std::optional<FitRule> modern_from_dimfit(int dimfit);
// DIMLUNIT and DIMFRAC as DIMUNIT; none when it has no older value
std::optional<int> legacy_dimunit(int dimlunit, int dimfrac);
// DIMATFIT and DIMTMOVE as DIMFIT; none when it has no older value
std::optional<int> legacy_dimfit(int dimatfit, int dimtmove);

// A style's DIMUNIT and DIMFIT as an R13/R14 file has them: the value as read while the style
// still says it (or, outside the ranges above, while DIMLUNIT, DIMFRAC and the like are the
// defaults), else the one its newer variables give. None: no older value.
std::optional<int> dimunit_of(const DimStyle& style);
std::optional<int> dimfit_of(const DimStyle& style);

// A DIMUNIT or DIMFIT as read: the field, and the newer variables it stands for (a value outside
// the ranges leaves them as they are)
void set_dimunit(DimStyle& style, int dimunit);
void set_dimfit(DimStyle& style, int dimfit);

// One group of the older record
struct LegacyGroup {
    enum class Kind { String, Int, Double };
    int code = 0;
    Kind kind = Kind::Int;
    std::string text;
    int integer = 0;
    double real = 0.0;
};

// What an R13/R14 DIMSTYLE has no group for, when the style uses it: the error, empty when the
// style can be written. `Dimension style "N": DIMDSEP requires DXF 2000 or later`.
std::string dimstyle_older_error(const DimStyle& style);

// The record's groups from 2 (name) to 288, in the order of AutoCAD's R13 and R14 files
// (small_r13.dxf, bin_dxf_r13.dxf, bin_dxf_r14.dxf), every variable as they have it: the old
// form writes them all, nothing is left out for being AutoCAD's default. 340 is
// `text_style_handle` (the handle of the style's STYLE record). The groups are empty and the
// error set when the style has something the old form lacks.
std::vector<LegacyGroup> legacy_dimstyle_groups(const DimStyle& style, const std::string& text_style_handle,
                                                std::string& error);

} // namespace internal
} // namespace hdxf
