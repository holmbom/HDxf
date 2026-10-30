#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace hdxf {
namespace internal {

// The preserved records of a drawing read from a DXF 2000 file, as an ASCII DXF text in
// the file's structure: HEADER, TABLES, BLOCKS, ENTITIES, OBJECTS. Reading it gives the
// drawing as it was read — what the writer compares the drawing with now to see what has
// changed (plan B12). Records added since have no preserved record and are not in it.
// source_binary: the file was binary DXF (its strings then get carets here).
std::string template_text(const Drawing& dwg, bool source_binary);

// The handles of every preserved record (5, or 105 for DIMSTYLE), sorted — taken when the
// drawing is read, so a pointer to a record removed since can be told from one that led
// nowhere already in the file (B19)
std::vector<uint64_t> preserved_handles(const Drawing& dwg);

// The entities of each block with a preserved record: (its BLOCK_RECORD's handle, the entity's
// handle), sorted — taken when the drawing is read, so an entity removed from a block since
// can be told (a MULTILEADER's proxy graphics drawn from the block, B38)
std::vector<std::pair<uint64_t, uint64_t>> preserved_block_entities(const Drawing& dwg);

// The objects a typed object owns by a hard pointer without being their owner group (a layer
// index's IDBUFFERs: their 330 is 0): (target's handle, owner's handle), sorted — taken when the
// drawing is read, so the targets go with an owner removed since (B19, B104)
std::vector<std::pair<uint64_t, uint64_t>> preserved_hard_owned(const Drawing& dwg);

} // namespace internal
} // namespace hdxf
