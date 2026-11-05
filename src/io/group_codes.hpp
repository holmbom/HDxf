#pragma once

#include <string>

namespace hdxf {
namespace internal {

// One group: a group code and its value. Every value is held as text, the way ASCII
// DXF stores it; binary values are converted to and from that text (see group_io.hpp).
struct GroupCode {
    int code = 0;
    std::string value;
    // ASCII only: the width of the code line where it is not AutoCAD's three columns (" 70"). 0 = three
    // columns; 4 = "  70" (the records AutoCAD 2025 exports other types as in R12: POLYLINE, VERTEX, TEXT
    // have it for some codes); 1 = no padding ("70", R14 files of other writers); 2 … 7 as in the file
    int code_columns = 0;
};

// Value type of a group code, as stored in binary DXF. ASCII DXF stores every
// value as text, but the reader and writer need the type to convert.
enum class GroupValueType {
    String,
    Double,         // 8 bytes, IEEE 754
    Int16,
    Int32,
    Int64,
    Bool,           // 1 byte
    BinaryChunk,    // 1-byte length + raw bytes; held as upper-case hex text
    Unknown         // not in the DXF reference — cannot be read from binary
};

// The same table for every version, R12 to 2018. Ranges from the DXF reference's group
// code value types; the binary widths were confirmed by reading AutoCAD's own binary
// files (R12 … 2018) to the end. Where the reference is unclear, AutoCAD's files decide:
// 280-289 are Int16 (not 8-bit) and 450-459 are Int32.
inline GroupValueType group_value_type(int code) {
    if (code >= 0 && code <= 9) return GroupValueType::String;
    if (code >= 10 && code <= 59) return GroupValueType::Double;
    if (code >= 60 && code <= 79) return GroupValueType::Int16;
    if (code >= 90 && code <= 99) return GroupValueType::Int32;
    if (code >= 100 && code <= 102) return GroupValueType::String;  // subclass, 101 embedded object, 102 control
    if (code == 105) return GroupValueType::String;                 // hex handle
    if (code >= 110 && code <= 149) return GroupValueType::Double;
    if (code >= 160 && code <= 169) return GroupValueType::Int64;
    if (code >= 170 && code <= 179) return GroupValueType::Int16;
    if (code >= 210 && code <= 239) return GroupValueType::Double;
    if (code >= 270 && code <= 289) return GroupValueType::Int16;
    if (code >= 290 && code <= 299) return GroupValueType::Bool;
    if (code >= 300 && code <= 309) return GroupValueType::String;
    if (code >= 310 && code <= 319) return GroupValueType::BinaryChunk;
    if (code >= 320 && code <= 369) return GroupValueType::String;  // hex handles
    if (code >= 370 && code <= 389) return GroupValueType::Int16;
    if (code >= 390 && code <= 399) return GroupValueType::String;  // hex handle
    if (code >= 400 && code <= 409) return GroupValueType::Int16;
    if (code >= 410 && code <= 419) return GroupValueType::String;
    if (code >= 420 && code <= 429) return GroupValueType::Int32;
    if (code >= 430 && code <= 439) return GroupValueType::String;
    if (code >= 440 && code <= 459) return GroupValueType::Int32;
    if (code >= 460 && code <= 469) return GroupValueType::Double;
    if (code >= 470 && code <= 479) return GroupValueType::String;
    if (code >= 480 && code <= 481) return GroupValueType::String;  // hex handle
    if (code == 999) return GroupValueType::String;                 // comment
    if (code >= 1000 && code <= 1003) return GroupValueType::String;
    if (code == 1004) return GroupValueType::BinaryChunk;
    if (code >= 1005 && code <= 1009) return GroupValueType::String; // 1005 hex handle
    if (code >= 1010 && code <= 1059) return GroupValueType::Double;
    if (code >= 1060 && code <= 1070) return GroupValueType::Int16;
    if (code == 1071) return GroupValueType::Int32;
    return GroupValueType::Unknown;
}

// The same, in the record type (the value of the last group 0) the group is in. The width of a
// group code can depend on the record: AutoCAD writes group 71 of DATATABLE (ACDBDATATABLE in
// 2000) as an int32, the cell value of a Bool, where it is an int16 everywhere else.
inline GroupValueType group_value_type(int code, const std::string& record) {
    if (code == 71 && (record == "DATATABLE" || record == "ACDBDATATABLE")) return GroupValueType::Int32;
    return group_value_type(code);
}

} // namespace internal
} // namespace hdxf
