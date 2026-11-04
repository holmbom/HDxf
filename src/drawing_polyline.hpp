#pragma once

#include "hdxf.hpp"
#include <string>
#include <utility>
#include <vector>

namespace hdxf {
namespace internal {

// The fields sync_polyline_properties() changed, with the values they had. They have them again when the log ends, whether the
// save succeeded or not. It points at the fields: no entity may move while it lives
class PolylineSyncLog {
public:
    PolylineSyncLog() = default;
    PolylineSyncLog(const PolylineSyncLog&) = delete;
    PolylineSyncLog& operator=(const PolylineSyncLog&) = delete;
    ~PolylineSyncLog();

    void note(std::string& field) { strings_.emplace_back(&field, field); }
    void note(Color& field) { colors_.emplace_back(&field, field); }

private:
    std::vector<std::pair<std::string*, std::string>> strings_;
    std::vector<std::pair<Color*, Color>> colors_;
};

// A POLYLINE's VERTEX records and its SEQEND, in the entities and blocks of a drawing, get the layer, linetype and color of their
// owner for one save: the file has them, and `log` gives the fields back afterwards.
// - A drawing read from a file (`original`: the drawing as read, from the preserved records): a property of a record that was
//   the owner's as read follows the owner when the owner has changed it, unless the record changed it itself. A value that
//   differed from the owner's as read stays. Records are paired with the original by handle, else (R12 files without
//   handles) by their place among the records the file had. A record that was read and has no counterpart is left as it is.
// - A record without a preserved record, and every record of a drawing built in memory (`original` null): an empty linetype
//   takes the owner's, and a color that is ByLayer takes the owner's. (The layer needs nothing: an empty one is the owner's.)
//   The API cannot tell a ByLayer color that was set from the default one, so a vertex set to ByLayer under a colored polyline
//   is written with the polyline's color.
// `log` gets each field before it is changed.
void sync_polyline_properties(std::vector<Entity>& entities, std::vector<Block>& blocks, const Drawing* original,
                              PolylineSyncLog& log);

} // namespace internal
} // namespace hdxf
