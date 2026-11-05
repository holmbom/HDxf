#pragma once

// The part ACAD_PROXY_ENTITY and ACAD_PROXY_OBJECT share, and ACAD_PROXY_OBJECT.

#include "boxed.hpp"
#include "core.hpp"
#include "fwd.hpp"

namespace hdxf {

// What a reference of a proxy is: 330, 340, 350 or 360
enum class ProxyPointerKind : int32_t {
    SoftPointer = 330,
    HardPointer = 340,
    SoftOwner = 350,
    HardOwner = 360
};

// One object id of the proxy's id list: the handle ("0" is a null id, as AutoCAD writes it)
struct ProxyReference {
    ProxyPointerKind kind = ProxyPointerKind::HardPointer;
    std::string handle;
};

// A CLASS record of the application's class, as the CLASSES section has it (1, 2, 3, 90, 281)
struct ProxyApplicationClass {
    std::string dxf_name;                       // 1: the DXF name of the class ("DMMAPMANAGER")
    std::string cpp_name;                       // 2: the C++ class name ("AcMapDMMapManager")
    std::string application;                    // 3: the application ("AcMapDisplayManagement")
    int32_t proxy_flags = 0;                    // 90
    bool entity = false;                        // 281: the class is an entity's
};

// 95 (or 71 with 97 in 2018): the drawing format the object had when it became a proxy; the low word is
// AcDbDwgVersion, the high word the maintenance release
struct ProxyDrawingFormat {
    uint16_t dwg_version = 0;
    uint16_t maintenance_version = 0;
    uint32_t combined() const { return (static_cast<uint32_t>(maintenance_version) << 16) | dwg_version; }
};

// The class data of a proxy entity or object (AcDbProxyEntity, AcDbProxyObject)
struct ProxyClassData {
    int32_t class_id = 499;                     // 90: 498 for an entity, 499 for an object
    int32_t application_class_id = 500;         // 91: 500 + the class's position in CLASSES, as the file has it;
                                                //     written from `application_class` when that names a class
    ProxyApplicationClass application_class;    // the class 91 names (read from CLASSES; empty when it names none)
    std::optional<ProxyDrawingFormat> drawing_format;   // 95 / 71 + 97; none in R14's form
    int16_t original_format = 0;                // 70: 0 the data is DWG's, 1 DXF's (then the class's own subclasses
                                                //     follow the proxy's, typed in `class_data`: no 93/161, no ids)
    // The graphics the application drew: AutoCAD 2007 and later keep them apart from the data (96 before 2010,
    // 162 from, the length in bits and the bytes in 311); R14 has them as 92 (bytes) + 310; 2000 and 2004
    // have them inside the data. A proxy of DXF data (70 = 1) has them in bytes: 92 before 2010, 160 from, and
    // 310 (AutoCAD's TurboCAD point). `graphics_bits` counts the bits of `graphics` (8 per byte in the byte forms)
    std::vector<uint8_t> graphics;
    uint32_t graphics_bits = 0;
    // The application's object data: 93 (161 from 2010) is its length in bits, 310 its bytes
    std::vector<uint8_t> data;
    uint32_t data_bits = 0;
    std::vector<ProxyReference> references;     // 330/340/350/360 up to 94, in order (the data refers to them by place)
    bool zombie_marker = false;                 // R13 files of other producers name the subclass AcDbZombieObject /
                                                //     AcDbZombieEntity: read so, written so
};

// The class's own data of a proxy of DXF data (70 = 1, step 9.S): AutoCAD writes the class's subclasses after the proxy's
// (TurboCAD's TCPOINTENTITY: AcDbPoint and TcPointEntity; Map's, ZWM's and TCOPTIONALDATA's objects), and in the version the
// object was made in, the class's own record. They are the type HDxf has for the class (defined after EntityData and
// ObjectData); a proxy whose class HDxf has no type for stays untyped, as it was read

// ACAD_PROXY_OBJECT (2000 and later; R13 and R14 as read)
struct ProxyObjectData {
    ProxyClassData proxy;
    OptionalBoxed<ProxyObjectClass> class_data; // 70 = 1: the class's subclasses as its type (TCOPTIONALDATA, DM…, ZWM…)
};

} // namespace hdxf
