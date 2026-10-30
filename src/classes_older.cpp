#include "dxf_writer.hpp"
#include "handles.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>

// The CLASSES section of DXF R13 and R14 (plan phase 7, step 7.10, B92; doc/reference/
// hdxf_r13_r14_format.md, section 4, D4). A CLASS record is 1, 2, 3, 90, 280, 281 — no 91 (the
// instance count is 2004's) — and the 90 of R13's records is 32768, the R13 proxy bit. The list is
// R14's default classes (R14 DXF Reference, "Default class values", and AutoCAD's own Test3_*.dxf,
// with the XRECORD class AutoCAD adds): LWPOLYLINE and HATCH and IMAGE are entities, the rest objects,
// and the application is written as AutoCAD does, `"AutoCAD"` with the quotes in the string and
// ISM without. A class is in the section when the drawing has an object or entity of it; a read file's
// own register (in its order, with the classes no instance uses) is the file's raw records, which
// write_classes_template() keeps. A class of a type that a later step writes (HATCH: 7.14, IMAGE and its
// objects: 7.15) is a line of the table below, and counts the entity or object as soon as the type is
// written: nothing else has to be added here.

namespace hdxf {
namespace internal {

namespace {

struct OlderClass {
    ClassEntry entry;
    ObjectType object = ObjectType::Unknown;
    EntityType entity = EntityType::Unknown;
    const char* unknown_object = nullptr;       // an untyped object of this DXF name
    const char* unknown_entity = nullptr;       // an untyped entity of this DXF name
};

// In the order of AutoCAD's R14 files (Test3_*.dxf)
const std::vector<OlderClass>& older_class_table() {
    static const std::vector<OlderClass> table = {
        {{"LWPOLYLINE", "AcDbPolyline", "\"AutoCAD\"", 0, true}, ObjectType::Unknown, EntityType::LwPolyline},
        {{"IMAGE", "AcDbRasterImage", "ISM", 127, true}, ObjectType::Unknown, EntityType::Image},
        {{"OLE2FRAME", "AcDbOle2Frame", "\"\"", 0, true}, ObjectType::Unknown, EntityType::Ole2Frame},
        {{"HATCH", "AcDbHatch", "\"ACAD_SEDONA\"", 0, true}, ObjectType::Unknown, EntityType::Hatch},
        {{"DICTIONARYVAR", "AcDbDictionaryVar", "\"AutoCAD\"", 0, false}, ObjectType::DictionaryVar},
        {{"RASTERVARIABLES", "AcDbRasterVariables", "ISM", 0, false}, ObjectType::RasterVariables},
        {{"IMAGEDEF", "AcDbRasterImageDef", "ISM", 0, false}, ObjectType::ImageDef},
        {{"IMAGEDEF_REACTOR", "AcDbRasterImageDefReactor", "ISM", 1, false}, ObjectType::ImageDefReactor},
        {{"IDBUFFER", "AcDbIdBuffer", "\"Acad\"", 0, false}, ObjectType::Unknown, EntityType::Unknown, "IDBUFFER"},
        {{"SPATIAL_FILTER", "AcDbSpatialFilter", "\"Acad\"", 0, false}, ObjectType::Unknown, EntityType::Unknown, "SPATIAL_FILTER"},
        {{"XRECORD", "AcDbXrecord", "\"<Built In>\"", 0, false}, ObjectType::XRecord},
        {{"SORTENTSTABLE", "AcDbSortentsTable", "\"AutoCAD\"", 0, false}, ObjectType::Unknown, EntityType::Unknown, "SORTENTSTABLE"},
        {{"LAYER_INDEX", "AcDbLayerIndex", "\"Acad\"", 0, false}, ObjectType::Unknown, EntityType::Unknown, "LAYER_INDEX"},
        {{"SPATIAL_INDEX", "AcDbSpatialIndex", "\"Acad\"", 0, false}, ObjectType::Unknown, EntityType::Unknown, "SPATIAL_INDEX"},
        {{"OBJECT_PTR", "CAseDLPNTableRecord", "\"ASE-LPNTableRecord\"", 1, false}, ObjectType::Unknown, EntityType::Unknown, "OBJECT_PTR"},
    };
    return table;
}

} // namespace

// The classes of the objects and entities the drawing has, R13 and R14 (see above)
std::vector<ClassEntry> older_classes_used(const Drawing& dwg, DxfVersion version,
                                           const std::unordered_set<uint64_t>* dropped,
                                           const std::vector<Block>* generated) {
    std::vector<ClassEntry> out;
    for (const OlderClass& c : older_class_table()) {
        int count = 0;
        auto entities = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) {
                if (c.entity != EntityType::Unknown) {
                    if (e.type == c.entity) ++count;
                } else if (c.unknown_entity && e.type == EntityType::Unknown &&
                           e.as_unknown().entity_type == c.unknown_entity) {
                    ++count;
                }
            }
        };
        entities(dwg.entities());
        for (const auto& b : dwg.blocks()) entities(b.entities);
        // R14's built-in arrows (_Dot) are LWPOLYLINEs in a block the writer makes
        if (generated) for (const auto& b : *generated) entities(b.entities);
        for (const auto& o : dwg.objects()) {
            uint64_t h = 0;
            if (dropped && parse_handle(o.handle, h) == HandleParse::Ok && dropped->count(h)) continue;
            bool kind = c.unknown_object ? o.type == ObjectType::Unknown && o.as_unknown().object_type == c.unknown_object
                                         : c.object != ObjectType::Unknown && o.type == c.object;
            if (kind) ++count;
        }
        if (count == 0) continue;
        out.push_back(c.entry);
        out.back().count = count;
        // R13: every record has the R13 proxy bit (AutoCAD's R13 files: 90 is 32768)
        if (version == DxfVersion::R13) out.back().flags = 32768;
    }
    // The classes of the typed records of phase 9 (R13's flags as above, in append_typed_classes)
    append_typed_classes(dwg, version, dropped, out);
    return out;
}

} // namespace internal
} // namespace hdxf
