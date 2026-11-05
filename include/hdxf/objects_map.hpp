#pragma once

// AutoCAD Map's records.

#include "objects_base.hpp"

namespace hdxf {

// --- 9.18 AutoCAD Map, TurboCAD and ZWCAD records (plan phase 9, step 9.18; map_objects.cpp, tc_records.cpp, zwm_records.cpp) -------------
// Records of other programs' classes that AutoCAD keeps in a drawing and does not document. The grammar of each is what the files
// that program saved show (the old plan's appendix B.47 ... B.55, B.59, B.60, B.74, B.75, B.77 ... B.104): third-party observations,
// not AutoCAD facit (F9.24, F9.25: ej testat). Where a file does not explain a group it keeps its place as its code and its position
// (`reserved_70_0`, `text_1_1`, `handle_340_0`, `list_70_a` ...), never as a raw group. A list a count (70 or 90) gives is a vector
// whose size the writer counts; an empty one writes a count of 0.

// AutoCAD Map 3D (the application AcMapDisplayManagement): the display manager of a drawing. The unlimited scale AutoCAD Map writes is
// 1.797693134862314E+99.
inline constexpr double kMapNoLimit = 1.797693134862314e+99;

struct MapManagerData {                 // DMMAPMANAGER
    DictionaryData dictionary;          // AcDbDictionary: 281 and (3 the map's name, 350 the DMMAP it soft-owns) for each display map
    std::string default_handle = "0";   // AcDbDictionaryWithDefault 340: the default map
    int16_t version = 1;                // AcMapDMMapManager 70
    std::string current_map = "0";      // 340: the display map in use
};

struct MapStyleLibraryData {            // DMSTYLELIBRARY: AcMapDMStyleLibrary 70, 70 (count), 360 each
    int16_t version = 1;
    std::vector<std::string> categories;    // 360: the DMSTYLECATEGORYs it owns
};

struct MapStyleCategoryData {           // DMSTYLECATEGORY: AcMapDMStyleCategory 70, 1, 70
    int16_t version = 1;
    std::string name;                   // 1 ("Display Styles")
    int16_t reserved_70_0 = 0;          // 70 (0 in the one file: what a non-zero value is followed by is not established)
};

struct MapMetadataData {                // MAPMETADATAOBJECT: AcMapMetadataPersistObject 90, 90
    int32_t version = 1;
    int32_t reserved_90_0 = 0;          // 90 (0 in the one file)
};

struct MapResourceManagerData {         // MAPRESOURCEMANAGEROBJECT: AcMapResourceManager 70, 90
    int16_t version = 1;
    int32_t reserved_90_0 = 0;          // 90 (0 in both files)
};

struct MapStylizedEntitiesTableData {   // DMSTYLIZEDENTITIESTABLE: AcMapDMStylizedEntitiesTable 70
    int16_t version = 1;
};

// DMMAP has three AcMapDMMap subclasses of its own, told apart by their occurrence (0, 1, 2)
struct MapDisplayMapHeader {            // occurrence 0: 70, 1, 70, 330
    int16_t version = 1;
    std::string name;                   // 1 ("Default")
    int16_t reserved_70_0 = 1;          // 70
    std::string parent_map = "0";       // 330 (0 for none)
};

struct MapDisplayMapElements {          // occurrence 1: 70, 90 (count), 360 each, 90 (count), 40 each, 70
    int16_t version = 1;
    std::vector<std::string> elements;  // 360: the DMBASEELEMENTs it owns
    std::vector<double> scales;         // 40 (kMapNoLimit: no limit)
    int16_t reserved_70_0 = 1;          // 70
};

struct MapDisplayMapLegend {            // occurrence 2: the legend
    int16_t version = 2;                // 70
    int16_t reserved_70_0 = 0;          // 70
    double reserved_40_0 = kMapNoLimit; // 40
    bool reserved_290_0 = true;         // 290
    bool reserved_290_1 = true;         // 290
    double reserved_40_1 = 1.0;         // 40
    double reserved_40_2 = 3.0;         // 40
    std::string name;                   // 1 ("Legend")
    std::vector<double> scales;         // 90 (count), 40 each
    std::string text_1_0;               // 1
    int16_t reserved_70_1 = 0;          // 70
    double reserved_40_3 = 1.0;         // 40
    int16_t reserved_70_2 = 1;          // 70
    int32_t reserved_90_0 = 0;          // 90 (0 in the one file)
    std::vector<std::string> items;     // 90 (count), 360 each: owned records (a DMDEFAULTSTYLE in the one file)
};

struct MapDisplayMapData {              // DMMAP
    MapDisplayMapHeader header;
    MapDisplayMapElements elements;
    MapDisplayMapLegend legend;
};

// DMBASEELEMENT has three AcMapDMBaseElement subclasses
struct MapBaseElementHeader {           // occurrence 0: 70, 1, 70, 330
    int16_t version = 1;
    std::string name;                   // 1 ("Map Base")
    int16_t reserved_70_0 = 1;          // 70
    std::string map = "0";              // 330: the DMMAP it belongs to
};

struct MapBaseElementStyle {            // occurrence 1: 70, 1, 70, 1, 70, 40, 70 x4, 360
    int16_t version = 1;
    std::string text_1_0;               // 1
    int16_t reserved_70_0 = 2;          // 70
    std::string text_1_1;               // 1
    int16_t reserved_70_1 = 1;          // 70
    double reserved_40_0 = kMapNoLimit; // 40
    std::array<int16_t, 4> reserved_70_2 = {1, 1, 1, 1};    // 70 x4
    std::string style_reference = "0";  // 360: the DMSTYLEREFERENCE it owns
};

struct MapBaseElementData {             // DMBASEELEMENT
    MapBaseElementHeader header;
    MapBaseElementStyle style;
    int16_t version_c = 1;              // occurrence 2: 70
};

// DMDEFAULTSTYLE has two AcMapDMDefaultStyle subclasses; AcMapDMDisplayStyleRegapp is the record's XDATA
struct MapDefaultStyleData {            // DMDEFAULTSTYLE
    int16_t version = 1;                // occurrence 0: 70
    int32_t reserved_90_0 = 1;          // 90
    std::string name;                   // 1 ("Default")
    int16_t version_b = 1;              // occurrence 1: 70
    int16_t reserved_280_0 = 100;       // 280
    int32_t reserved_90_1 = 0;          // 90
};

struct MapStyleReferenceData {          // DMSTYLEREFERENCE: AcMapDMStyleReference 70, 70, 340, 330
    int16_t version = 1;
    int16_t flags = 0;                  // 70
    std::string style = "0";            // 340: the DMDEFAULTSTYLE it refers to
    std::string map = "0";              // 330: the DMMAP
};

struct MapSurrogateStyleSetsData {      // DMSURROGATESTYLESETS: AcMapDMSurrogateStyleSets 70, 1, 70, 40, 70 x4, 330, 70
    int16_t version = 1;
    std::string name;                   // 1 ("Map Base")
    int16_t reserved_70_0 = 1;          // 70
    double reserved_40_0 = kMapNoLimit; // 40
    std::array<int16_t, 4> reserved_70_1 = {1, 1, 1, 1};    // 70 x4
    std::string style_reference = "0";  // 330: the DMSTYLEREFERENCE
    int16_t reserved_70_2 = 0;          // 70
};

} // namespace hdxf
