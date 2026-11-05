#pragma once

// ZWCAD Mechanical's records.

#include "core.hpp"

namespace hdxf {

// ZWCAD Mechanical (the application ZWMCUSTCORE): ZWM*STD, the standard's styles. The components: ZwmObject, ZwmStd, ZwmSymbolStd and
// ZwmSymProperties, the repeated ZWMATTRIBUTEDEF, and what a style type has of its own
struct ZwmObjectHeader {                // ZwmObject: 70, 1 x4, 70
    int16_t version = 1;
    std::array<std::string, 4> text_1;  // 1 x4 ("", the standard's name, "", "")
    int16_t reserved_70_0 = 0;          // 70: the number of attribute definitions that follow (the style types COLOR, DIMSTYLE, LAYER,
                                        //     LINETYPE and TEXTSTYLE: the writer counts them); 0 in the others
};

// The eight handles, 16 + 8 integers and 8 strings that end ZwmStd, ZwmSymbolStd and a style type's last subclass (every handle 0 and
// every integer -1 in the files)
struct ZwmStdTail {
    std::array<std::string, 8> handle_340;
    std::array<int16_t, 16> reserved_70;
    std::array<int32_t, 8> reserved_90;
    std::array<std::string, 8> text_1;
    ZwmStdTail() {
        handle_340.fill("0");
        reserved_70.fill(-1);
        reserved_90.fill(-1);
    }
};

struct ZwmStd {                         // ZwmStd: 70 x3, 1 x2, the tail
    int16_t version = 1;
    int16_t reserved_70_0 = 1;
    int16_t reserved_70_1 = 0;
    std::array<std::string, 2> text_1;  // ("", the standard's name)
    ZwmStdTail tail;
};

struct ZwmSymbolStd {                   // ZwmSymbolStd: 70, the tail
    int16_t version = 1;
    ZwmStdTail tail;
};

struct ZwmTypeStd {                     // the type's own first subclass (ZwmBalloonStd ...): 70, 70, 1
    int16_t version = 1;
    int16_t reserved_70_0 = 1;
    std::string text_1_0;               // the standard's name
};

// What a ZWMATTRIBUTEDEF's value is follows from its fourth string (the value type): VT_R8 a real (40), VT_BOOL an integer (70),
// anything else a string (1)
struct ZwmAttributeValue {
    std::string text;                   // 1
    int16_t integer = 0;                // 70
    double real = 0.0;                  // 40
};

struct ZwmAttributeDefinition {         // ZWMATTRIBUTEDEF: 70, 70, 1 x7, 70 x11, the value
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
    std::array<std::string, 7> text_1;  // the name, the label, ..., [3] a label of the value type ("VT_BOOL", "VT_R8", "VT_BSTR" or ""), [4] the editor
    std::array<int16_t, 11> reserved_70_1 = {};     // [5] the OLE variant type, which says what the value is (8 text, 11 bool, 5
                                                    // real); when it is 0 the label in text_1[3] does
    ZwmAttributeValue value;
};

// An attribute definition of a map (ZwmSymProperties' and the properties of the BOM and TITLE types): `key` is the 1 before the
// definition's subclass (in the subclass before it), empty in a list that has none
struct ZwmAttributeEntry {
    std::string key;
    ZwmAttributeDefinition definition;
};

struct ZwmSymProperties {               // ZwmSymProperties: the symbol's properties and its attributes
    int16_t version = 2;                // 70
    std::array<std::string, 4> text_1;  // 1 x4 (the standard's name twice, "", its xml file "ISO.xml")
    int16_t reserved_70_0 = 0;          // 70 (91, 221, 401 ...: it differs per type)
    int32_t reserved_70_1 = 1;          // 70 (1; ZWMSTANDARD's 483351136 does not fit the 16 bits of a binary group)
    double reserved_40_0 = 0.0;         // 40
    double reserved_40_1 = 0.0;         // 40
    std::array<int16_t, 4> reserved_70_2 = {-1, -1, -1, -1};    // 70 x4
    double reserved_40_2 = 0.0;         // 40
    int16_t reserved_70_3 = 0;          // 70
    int16_t reserved_70_4 = 0;          // 70
    std::vector<int16_t> list_70_a;     // 70 counts them, then each
    std::vector<int16_t> list_70_b;     // 70 counts them, then each
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (the sizes "2.5" ... "20")
    std::vector<std::string> list_1_b;  // 70 counts them, then each 1
    std::string handle_340_0 = "0";
    std::array<double, 12> reserved_40_3 = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 3.5, 3.5, 3.5, 3.5, 3.5, 3.5};
    std::array<int16_t, 12> reserved_70_5 = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    std::array<std::string, 14> handle_340_1;
    std::array<int16_t, 16> reserved_70_6;
    std::array<int32_t, 8> reserved_90_0;
    std::array<std::string, 8> text_1_1;
    std::vector<ZwmAttributeEntry> attributes;      // 70 counts them; each is 1 (its key) and a ZWMATTRIBUTEDEF subclass
    ZwmSymProperties() {
        handle_340_1.fill("0");
        reserved_70_6.fill(-1);
        reserved_90_0.fill(-1);
    }
};

struct ZwmTextPair {                    // two strings: a name and its value
    std::string text_1_0;
    std::string text_1_1;
};

struct ZwmIntPair {
    int16_t reserved_70_0 = 0;
    int16_t reserved_70_1 = 0;
};

struct ZwmNameValue {                   // 1 and 70
    std::string text_1_0;
    int16_t reserved_70_0 = 0;
};

struct ZwmRealInt {                     // 40 and 70
    double reserved_40_0 = 0.0;
    int16_t reserved_70_0 = 0;
};

// ZWMBALLOONSTD
struct ZwmBalloonShape {                // 1, 1, 1, 70
    std::string text_1_0;
    std::string text_1_1;
    std::string text_1_2;               // a drawing file ("Table.dwg")
    int16_t reserved_70_0 = 0;
};

struct ZwmBalloonProperties {           // ZwmBalloonProperties
    int16_t version = 1;
    int16_t reserved_70_0 = 1;
    std::vector<int16_t> list_70_a;     // 70 counts them, then each
    std::vector<ZwmBalloonShape> shapes;    // 70 counts them
    std::array<std::string, 2> text_1;  // ("[RELPOS]", "[BALLOON_PART_COUNT]")
    int16_t reserved_70_1 = 0;
    std::array<double, 5> reserved_40 = {};
    std::string handle_340_0 = "0";
    std::string text_1_2;
    int16_t reserved_70_2 = 0;
};

struct ZwmBalloonStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmBalloonProperties properties;
};

// ZWMBOMSTYLESTD: the attribute definitions are the parts list's columns
struct ZwmBomProperties {               // ZwmBomProperties: 70, 1 x6, 70 x4, 70 (the count), the definitions
    int16_t version = 1;
    std::array<std::string, 6> text_1;  // (the style's name twice, the head and body drawing files, the xml file, "")
    std::array<int16_t, 4> reserved_70 = {};
    std::vector<ZwmAttributeEntry> attributes;      // 70 counts them; no key
};

struct ZwmBomStyleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmTypeStd type_std;
    ZwmBomProperties properties;
};

// ZWMBREAKSYMSTD
struct ZwmBreakSymProperties {
    int16_t version = 1;
    std::array<double, 2> reserved_40 = {};
};

struct ZwmBreakSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmBreakSymProperties properties;
};

// ZWMCENTERHOLESTD
struct ZwmCenterHoleProperties {
    int16_t version = 1;
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 ("R1.0/2.12" ...)
    int16_t reserved_70_0 = 0;
};

struct ZwmCenterHoleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmCenterHoleProperties properties;
};

// ZWMCHAMFERSTD
struct ZwmChamferProperties {
    int16_t version = 1;
    std::array<int16_t, 2> reserved_70 = {};
};

struct ZwmChamferStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmChamferProperties properties;
};

// ZWMCIRCLEMARKSTD
struct ZwmCircleMarkProperties {
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
};

struct ZwmCircleMarkStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmCircleMarkProperties properties;
};

// ZWMCOLORSTD: the attribute definitions follow ZwmObject (counted there); the color names and their values are the style's own
struct ZwmColorStdBody {                // ZwmColorStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> colors;    // the name of a color ("ZWMCL_CYAN") and its number as text
    ZwmStdTail tail;
};

struct ZwmColorStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;     // after ZwmObject, which counts them
    ZwmStd standard;
    ZwmColorStdBody color;
};

// ZWMDATUMIDSTD
struct ZwmDatumIdProperties {
    int16_t version = 1;
    std::array<double, 2> reserved_40 = {};
};

struct ZwmDatumIdStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmDatumIdProperties properties;
};

// ZWMDATUMTARGETSTD
struct ZwmDatumTargetProperties {
    int16_t version = 1;
    std::array<double, 3> reserved_40 = {};
    std::array<std::string, 2> handle_340 = {"0", "0"};
    std::array<int16_t, 2> reserved_70 = {};
};

struct ZwmDatumTargetStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmDatumTargetProperties properties;
};

// ZWMDETAILSTD
struct ZwmDetailProperties {
    int16_t version = 2;
    std::string handle_340_0 = "0";
    std::array<int16_t, 3> reserved_70_0 = {};
    std::string text_1_0;               // the label's text with its fields ("[DETAIL_LABEL] ([DETAIL_ABSSCALE])")
    int16_t reserved_70_1 = 0;
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (the scales "1000:1" ...)
    std::vector<std::string> list_1_b;  // 70 counts them, then each 1 (the identifiers "A" ...)
};

struct ZwmDetailStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmDetailProperties properties;
};

// ZWMDIMSTYLESTD
struct ZwmDimStyleStdBody {             // ZwmDimStyleStd: 70, 70 x3, 40 x2, 70 x3, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::array<int16_t, 3> reserved_70_0 = {};
    std::array<double, 2> reserved_40 = {};
    std::array<int16_t, 3> reserved_70_1 = {};
    std::vector<ZwmTextPair> styles;    // the name of a dimension style ("ZWMDS_MAIN") and AutoCAD's ("ISO_DIMSTYLE")
    ZwmStdTail tail;
};

struct ZwmDimStyleStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmDimStyleStdBody dim_style;
};

// ZWMELEVSYMSTD
struct ZwmElevSymProperties {
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
    std::array<double, 5> reserved_40 = {};
};

struct ZwmElevSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmElevSymProperties properties;
};

// ZWMFCFRAMESTD
struct ZwmFCFrameProperties {
    int16_t version = 1;
    int16_t reserved_70_0 = 0;
    std::array<double, 2> reserved_40_0 = {};
    std::array<int16_t, 8> reserved_70_1 = {};
    std::vector<ZwmIntPair> rows_a;     // 70 counts them, then 70 70 each
    std::vector<ZwmIntPair> rows_b;     // 70 counts them, then 70 70 each
    std::array<double, 2> reserved_40_1 = {};
};

struct ZwmFCFrameStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmFCFrameProperties properties;
};

// ZWMFEATUREIDSTD
struct ZwmFeatureIdProperties {
    int16_t version = 1;
    std::array<double, 3> reserved_40 = {};
};

struct ZwmFeatureIdStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmFeatureIdProperties properties;
};

// ZWMFRAMESTYLESTD: a frame style is a list of frames; a frame is 1 (its name, in the subclass before it), its ZwmFrameProperties
// subclass and a 70 that ends it
struct ZwmFrameProperties {             // ZwmFrameProperties: 70, 1, 40 x9, 70, 1 x6, 90
    int16_t version = 1;
    std::string text_1_0;               // the sheet ("A3")
    std::array<double, 9> reserved_40 = {};     // the sheet's size (420, 297) and margins
    int16_t reserved_70_0 = 0;
    std::array<std::string, 6> text_1;  // ("Partition", "Partition", "L", the drawing file, "", "landscape")
    int32_t reserved_90_0 = 0;
};

struct ZwmFrameEntry {
    std::string name;                   // 1, before the subclass ("A3_Partition_landscape")
    ZwmFrameProperties properties;
    int16_t reserved_70_0 = 0;          // the 70 after the subclass's groups
};

struct ZwmFrameStyleStd {               // ZwmFrameStyleStd: 70, 70 (the count), 1 (the first frame's name)
    int16_t version = 1;
    std::vector<ZwmFrameEntry> frames;
};

struct ZwmFrameStyleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmFrameStyleStd style;
};

// ZWMHOLECHARTSTD
struct ZwmHoleChartProperties {
    int16_t version = 2;
    std::array<int16_t, 8> reserved_70_0 = {};
    std::array<ZwmRealInt, 5> pairs = {};
    std::array<int16_t, 8> reserved_70_1 = {};
    std::array<double, 6> reserved_40_0 = {};
    std::array<int16_t, 6> reserved_70_2 = {};
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (the columns of a table)
    std::vector<std::string> list_1_b;
    std::vector<std::string> list_1_c;
    std::array<std::vector<ZwmTextPair>, 6> rows;   // six lists of 70 (their count) and a pair of strings each
    std::array<int16_t, 5> reserved_70_3 = {};
    std::array<double, 2> reserved_40_1 = {};
};

struct ZwmHoleChartStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmHoleChartProperties properties;
};

// ZWMLAYERSTD
struct ZwmLayerStdBody {                // ZwmLayerStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> layers;    // ZWCAD's name of a layer ("ZWMLY_DATUMID_ARROW") and the layer's ("AM_5")
    ZwmStdTail tail;
};

struct ZwmLayerStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmLayerStdBody layer;
};

// ZWMLINETYPESTD
struct ZwmLineTypeStdBody {             // ZwmLineStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> linetypes; // ZWCAD's name of a linetype ("ZWMLT_PHANTOM") and the linetype's ("AM_ISO09W050")
    ZwmStdTail tail;
};

struct ZwmLineTypeStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmLineTypeStdBody linetype;
};

// ZWMNOTESTD
struct ZwmNoteProperties {
    int16_t version = 1;
    std::array<int16_t, 2> reserved_70 = {};
    std::array<double, 4> reserved_40 = {};
};

struct ZwmNoteStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmNoteProperties properties;
};

// ZWMPARTREFSTD
struct ZwmPartRefProperties {
    int16_t version = 1;
    double reserved_40_0 = 0.0;
};

struct ZwmPartRefStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmPartRefProperties properties;
};

// ZWMSECTIONSYMSTD
struct ZwmSectionSymProperties {
    int16_t version = 2;
    std::array<int16_t, 3> reserved_70_0 = {};
    std::string text_1_0;               // the label's text with its fields
    int16_t reserved_70_1 = 0;
    std::string handle_340_0 = "0";
    std::array<int16_t, 4> reserved_70_2 = {};
    std::array<double, 2> reserved_40 = {};
    std::vector<std::string> list_1_a;  // the scales
    std::vector<std::string> list_1_b;  // the identifiers
};

struct ZwmSectionSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmSectionSymProperties properties;
};

// ZWMSTANDARD: the root of the standard; it owns the other ZWM*STD records (360)
struct ZwmStandardProperties {
    int16_t version = 1;
    std::array<double, 8> reserved_40_0 = {};
    std::array<std::string, 2> handle_340 = {"0", "0"};
    std::array<int16_t, 7> reserved_70_0 = {};
    double reserved_40_1 = 0.0;
    std::vector<std::string> owned;     // 70 counts them, then each 360: the ZWM*STD records it owns
    std::vector<ZwmNameValue> names;    // 70 counts them; the names of the colors ("ZWMCL_CYAN") and their numbers
};

struct ZwmStandardData {
    ZwmObjectHeader header;
    ZwmStd standard;
    int16_t version = 1;                // ZwmStandard: 70
    ZwmSymProperties symbol;
    ZwmStandardProperties properties;
};

// ZWMSURFSYMSTD
struct ZwmSurfSymProperties {
    int16_t version = 2;
    std::array<int16_t, 6> reserved_70_0 = {};
    std::array<double, 3> reserved_40_0 = {};
    std::array<int16_t, 14> reserved_70_1 = {};
    std::vector<std::string> list_1_a;  // 70 counts them, then each 1 (roughness values)
    std::vector<std::string> list_1_b;
    int16_t reserved_70_2 = 0;
    std::array<double, 2> reserved_40_1 = {};
};

struct ZwmSurfSymStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmSurfSymProperties properties;
};

// ZWMTAPERSLOPESTD
struct ZwmTaperSlopeProperties {
    int16_t version = 1;
    std::array<int16_t, 11> reserved_70 = {};
};

struct ZwmTaperSlopeStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmTaperSlopeProperties properties;
};

// ZWMTEXTSTYLESTD
struct ZwmTextStyleStdBody {            // ZwmTextStyleStd: 70, 70 (the count), (1, 1) each, the tail
    int16_t version = 1;
    std::vector<ZwmTextPair> styles;    // ZWCAD's name of a text style ("ZWMTS_KAITI") and the style's ("Regular_Script")
    ZwmStdTail tail;
};

struct ZwmTextStyleStdData {
    ZwmObjectHeader header;
    std::vector<ZwmAttributeDefinition> attributes;
    ZwmStd standard;
    ZwmTextStyleStdBody text_style;
};

// ZWMTITLESTYLESTD: the attribute definitions are the title block's fields
struct ZwmTitleProperties {             // ZwmTitleProperties: 70, 1 x7, 70 x2, 70 (the count), the definitions, the tail
    int16_t version = 1;
    std::array<std::string, 7> text_1;  // (the style's name twice, "", the drawing file, the xml file, "", "")
    std::array<int16_t, 2> reserved_70 = {};
    std::vector<ZwmAttributeEntry> attributes;      // 70 counts them; no key
    ZwmStdTail tail;                    // in the last definition's groups (or after the count when there is none)
};

struct ZwmTitleStyleStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmTypeStd type_std;
    ZwmTitleProperties properties;
};

// ZWMVIEWDIRECTIONSTD
struct ZwmViewDirectionProperties {
    int16_t version = 1;
    std::string text_1_0;               // the label's text with its fields ("[VIEW_NAME]\P\O[VIEW_SCALE]")
    double reserved_40_0 = 0.0;
    int16_t reserved_70_0 = 0;
    std::vector<std::string> list_1_a;  // the scales
    std::vector<std::string> list_1_b;  // the identifiers
};

struct ZwmViewDirectionStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmViewDirectionProperties properties;
};

// ZWMWELDINGSTD
struct ZwmWeldingProperties {
    int16_t version = 1;
    std::string handle_340_0 = "0";
    double reserved_40_0 = 0.0;
    std::vector<ZwmIntPair> rows_a;     // 70 counts them, then 70 70 each
    std::vector<ZwmIntPair> rows_b;
    std::vector<ZwmIntPair> rows_c;
};

struct ZwmWeldingStdData {
    ZwmObjectHeader header;
    ZwmStd standard;
    ZwmSymbolStd symbol_std;
    ZwmTypeStd type_std;
    ZwmSymProperties symbol;
    ZwmWeldingProperties properties;
};

} // namespace hdxf
