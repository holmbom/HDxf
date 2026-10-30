#pragma once

#include "hdxf.hpp"
#include <string>
#include <vector>

// MTEXT's groups as data (plan phase 6, B72–B75): reading the AcDbMText groups of an MTEXT entity,
// of the text of a multiline attribute and of an XRECORD's copy of it, its embedded object's
// columns, and the column data of the XDATA up to 2013.

namespace hdxf {
namespace internal {

// What reading the groups of an MTEXT keeps until the text is whole: its parts (3, then the
// last 1) and its rotation (50, which gives the direction)
struct MTextParseState {
    std::string parts;
    std::string last;
    double rotation = 0.0;
    bool by_rotation = false;
};

// The AcDbMText group `code` into d (the entity's, or the text of a multiline attribute): false
// when it is not one of its codes
bool mtext_apply_group(MTextData& d, MTextParseState& state, int code, const std::string& value);

// A group of the embedded object of an MTEXT in 2018: 70 (the attachment point), the direction (10),
// the insertion point (11), the defined width (40) and height (41), the size as drawn (42, 43), then
// the columns: type (71), count (72), width (44), gutter (45), auto height (73), reversed (74) and
// the heights (46). AutoCAD takes the embedded object's attachment, direction, insertion point and
// defined height over the entity's own (B74), so those go to the entity's fields.
void mtext_apply_embedded_group(MTextData& d, MTextParseState& state, int code, const std::string& value);

// d.content from its parts, and the rotation as the direction it gives in the OCS of `extrusion`.
// The content is as the groups had it: the caller decodes it.
void mtext_finish(MTextData& d, MTextParseState& state, const Point3& extrusion);

// An MTEXT's column data in the XDATA ACAD of a file up to 2013: ACAD_MTEXT_COLUMN_INFO_BEGIN,
// 1070 75 (type), 79 (auto height), 76 (count), 78 (reversed), 48 (width), 49 (gutter), 50 (the
// number of heights) and the heights in 1040, ACAD_MTEXT_COLUMN_INFO_END; ACAD_MTEXT_COLUMNS_BEGIN,
// 1070 47, the number of columns, and a 1005 for each other column, ACAD_MTEXT_COLUMNS_END;
// ACAD_MTEXT_TEXT_BORDERS_BEGIN, 1070 80 (the flags of 90), 46 (the scale of 45), 81 (the number of
// frames) and a 1070 5 with a 1005 for each, ACAD_MTEXT_TEXT_BORDERS_END; and
// ACAD_MTEXT_DEFINED_HEIGHT_BEGIN, 1070 46, the height, ACAD_MTEXT_DEFINED_HEIGHT_END (AutoCAD's own
// files: ACadSharp's samples and ezdxf's mtext_framed_columns, mtext_text_frame). A block of exactly
// that form becomes the fields (and leaves the XDATA); any other stays in the XDATA. Taken by the
// reader, in this order, after the entity's own groups.
void take_mtext_xdata(std::vector<XdataList>& xdata, MTextData& d);

// The blocks of the fields that have one, in AutoCAD's order (borders, column info, columns, defined
// height), as one list ACAD; empty when there is nothing to write. `version`: the DXF version
// (defined height as XDATA from 2000; the frame and the columns' links only before 2018).
XdataList mtext_xdata(const MTextData& d, DxfVersion version);

// The text of a text with several columns cut as AutoCAD writes it before 2018 (plan B74): the pieces
// of the first column and the `count - 1` linked ones. Static columns end each piece with the
// column break \N (the rest of the text in the last); dynamic columns have the whole text in the
// first and nothing in the others.
std::vector<std::string> column_pieces(const std::string& text, const MTextColumns& columns);

} // namespace internal
} // namespace hdxf
