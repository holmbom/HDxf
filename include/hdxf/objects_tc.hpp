#pragma once

// TurboCAD's TCOPTIONALDATA.

#include "core.hpp"

namespace hdxf {

// TurboCAD (the application ExTcadCustObjs): TCOPTIONALDATA, a property set of an entity or a drawing. Its second 70 is a set of
// flags that say which groups follow, in this order: 1 (the text), 10 (the point) when 2, 90 and the properties when 128, 1 and
// 70 (the font) when 16, 40 when 32. A property is 1 (its name), 70 (its type: 5 a real, 8 a string, 18 an integer) and its value;
// a string of a version 1 record is group 1, of version 2 the bytes (90 their number, 310). Another type or another flag bit
// leaves the groups after the ones that are known in the preserved record.
struct TcProperty {
    std::string name;                   // 1
    int16_t type = 8;                   // 70: 5 (40), 8 (1 in version 1, 90 and 310 in version 2), 18 (70)
    std::string text;                   // type 8, version 1: 1
    std::vector<uint8_t> blob;          // type 8, version 2: 90 counts them, 310
    double real = 0.0;                  // type 5: 40
    int16_t integer = 0;                // type 18: 70
};

struct TcFont {
    std::string name;                   // 1 ("Arial")
    int16_t reserved_70_0 = 0;          // 70
};

struct TcOptionalDataData {             // TCOPTIONALDATA
    int16_t version = 2;                // 70 (1 or 2)
    uint16_t other_flags = 0;           // the bits of the second 70 that are none of the five below (kept as they are)
    std::optional<std::string> text;    // flag 1: group 1 ("Line/Polyline")
    std::optional<Point3> point;        // flag 2: 10 20 30
    std::optional<std::vector<TcProperty>> properties;      // flag 128: 90 counts them
    std::optional<TcFont> font;         // flag 16: 1 and 70
    std::optional<double> real;         // flag 32: 40
};

} // namespace hdxf
