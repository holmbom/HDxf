#pragma once

#include "hdxf.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace hdxf {
namespace internal {

// The ACDSDATA section (DXF 2013+, plan phase 5, B61; acds.cpp): in no reference — as AutoCAD
// writes it. Schemas (ACDSSCHEMA, 90 the index, 1 the name) and records (ACDSRECORD, 90 the
// schema's index, 320 the handle of the record it belongs to, then the data: 2 its name,
// 280 15, 94 the number of bytes and 310 the bytes).

// What HDxf types: a body's SAB (AcDb3DSolid_ASM_Data, ASM_Data) and a layout's or view's
// preview image (AcDb_Thumbnail_Schema, Thumbnail_Data)
enum class AcdsKind { Other, Body, Thumbnail };

const char* const acds_asm_schema = "AcDb3DSolid_ASM_Data";
const char* const acds_thumbnail_schema = "AcDb_Thumbnail_Schema";

// An ACDSRECORD as read
struct AcdsRecordInfo {
    int schema = -1;                // 90
    uint64_t handle = 0;            // 320; 0 = none
    std::string data_name;          // the 2 after 320
    std::vector<uint8_t> data;      // 310
};

// The record's fields; false when it is not an ACDSRECORD
bool acds_record_info(const RawRecord& record, AcdsRecordInfo& out);
// An ACDSSCHEMA's index (90) and name (1); false when it is not one
bool acds_schema_info(const RawRecord& schema, int& index, std::string& name);
// What a record of that schema and data name is
AcdsKind acds_kind(const std::string& schema_name, const std::string& data_name);

// Something that has typed ACDSDATA: a body, a layout or a view, and its data (empty = none)
struct AcdsHolder {
    AcdsKind kind = AcdsKind::Other;
    uint64_t handle = 0;
    const std::vector<uint8_t>* data = nullptr;
    const AcisData* acis = nullptr;     // a body's ACIS data (its SAT lines are made SAB, B62)
};

// Every body (in the blocks, then the entities — the order they are written), layout and view
// of the drawing, with or without data
std::vector<AcdsHolder> acds_holders(const Drawing& dwg);

} // namespace internal
} // namespace hdxf
