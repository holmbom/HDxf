#pragma once

#include "hdxf.hpp"
#include "text_layout.hpp"
#include <string>
#include <vector>

// The TEXT part of a multiline attribute and its XRECORD (plan phase 6, B75, B76)

namespace hdxf {
namespace internal {

// A line of a multiline attribute's text as TEXT draws it: where its pen starts (in the OCS of the
// attribute), its height, its text with TEXT's %% codes
struct AttributeLine {
    Point3 pen;
    double height = 0.0;
    std::string text;
};

// The lines of the text: the layout's when the style's font is found, else its paragraphs
// below one another. `by_runs`: a line is split wherever its format changes, each part a line
// of its own at its own pen — as AutoCAD makes the attributes of a block reference that is
// scaled or rotated (a "Second {\\C1;red} line" is three: AutoCAD's facit, 2000 … 2013).
std::vector<AttributeLine> attribute_lines(const MultilineAttribute& ml, const Point3& extrusion, TextLayout& layout,
                                           bool by_runs = false);

// Whether the attributes of a block reference are made from the text's runs: when the reference
// is scaled or rotated (an unscaled, unrotated one has its lines whole)
bool attributes_by_runs(const InsertData& insert);

// What the attribute's own TEXT groups say, as AutoCAD writes them (B75, B76). In 2018 and in an
// attribute definition the text is justified by the MTEXT's attachment, at its insertion point
// (11), with an empty 1 in 2018 and the first line before, its pen (10) where TEXT's justification
// puts it for the text shown — the value, or the tag when there is none (ACadSharp's 796 and 79D
// in 2018 and CY7_diode_clamp's 267 exactly); an attribute up to 2013 is the first line as TEXT
// with the tag's "_001" (ACadSharp's 79D: the layout's pen exactly), and the other lines are
// secondary attributes of their own.
struct AttributeMirror {
    std::string value;
    Point3 start_point;
    Point3 alignment_point;
    int h_justify = 0;
    int v_justify = 0;
    double height = 0.0;
    double rotation = 0.0;              // degrees
    std::string style;
    std::string tag;
};

// `width_factor`, `oblique` (degrees) and `generation_flags` are the attribute's own, which the text's
// pen depends on
AttributeMirror attribute_mirror(const MultilineAttribute& ml, const std::string& tag, bool definition,
                                 DxfVersion version, const Point3& extrusion, TextLayout& layout,
                                 double width_factor = 1.0, double oblique = 0.0, int generation_flags = 0,
                                 bool by_runs = false);

// The data of the XRECORD ACAD_MLATT (70 type, 70 really locked, 70 their number, 340 each of the
// secondary attributes, 40 the annotation scale, 2 the tag, 1 "Embedded Object" and the text's
// AcDbMText groups) and of ACAD_MATT_HEIGHT (102, 140 the height)
std::vector<DxfGroup> mlatt_xrecord_data(const MultilineAttribute& ml, const std::string& tag);
std::vector<DxfGroup> matt_height_xrecord_data(const MultilineAttribute& ml);

// The text in parts of 250 characters (3 …, then the last in 1)
std::vector<std::string> text_in_parts(const std::string& text);

} // namespace internal
} // namespace hdxf
