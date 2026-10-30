#pragma once

#include "hdxf.hpp"
#include <cstddef>
#include <functional>
#include <string>

// AutoCAD's standard objects of a new drawing, as data: the materials and visual styles of
// AutoCAD 2017's new drawing saved as 2007 (drawing_r2007.cpp, plan phase 3, B34) and its
// visual styles saved as 2010 (drawing_r2010.cpp, plan phase 4, B45)

namespace hdxf {
namespace internal {

struct DataGroup {
    int code;
    const char* value;
};

// A record's groups after its subclass marker, its XDATA (1001 …) and, for a visual style,
// the data of its ACAD_XREC_ROUNDTRIP
struct DataRecord {
    const DataGroup* groups;
    size_t count;
    const DataGroup* xdata;
    size_t xdata_count;
    const DataGroup* roundtrip;
    size_t roundtrip_count;
};

// Takes out of a drawing the standard materials and visual styles a library-made 2007+ drawing has — those that are still
// exactly AutoCAD's — with their extension dictionaries and XRECORDs, and ACAD_MATERIAL / ACAD_VISUALSTYLE when nothing else
// is in them: a drawing set back to R12, R13 or R14, which have neither (as it loses 2000's layouts). A material or style of
// the program's own, or a standard one it changed, stays (and the save is refused with the named error); step 9.7,
// objects_style_defaults.cpp
void drop_standard_style_objects(Drawing& dwg);

// The materials ByBlock, ByLayer and Global (drawing_r2007.cpp)
const DataRecord* standard_materials(size_t& count);

// The visual styles in the file's order, in the form of a 2007 and of a 2010 file
const DataRecord* visual_styles_2007(size_t& count);
const DataRecord* visual_styles_2010(size_t& count);
const DataRecord* visual_styles_2013(size_t& count);

// The visual styles of a drawing built in memory in the form of the version it is written as:
// one that is AutoCAD's in another form (2007, 2010 or 2013) — its groups, XDATA and round-trip
// XRECORD — gets that style's in this form (2013: without the extension dictionary and XRECORD,
// which are made with new_handle going back); a style changed since is left as it is (B45, B59)
void match_visual_styles(Drawing& dwg, DxfVersion version, const std::function<std::string()>& new_handle);

} // namespace internal
} // namespace hdxf
