#include "test_framework.hpp"
#include "hdxf.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// External test files (phase 0, step 0.2 — doc/plans/2026-10-22-fas-0-grund.md)
//
// The files are downloaded at configure time (test/data/manifest.cmake) and are not
// checked in. Each test is skipped when its file is missing. The facit below is counted
// from the files themselves, not with HDxf — test/data/README.md lists it in full.
// ---------------------------------------------------------------------------

namespace {

struct TypeCount {
    std::string type;
    int count;
};

struct Facit {
    std::string file;                   // relative to HDXF_EXTERNAL_DATA_DIR
    hdxf::DxfVersion version;           // $ACADVER (R12 when the file has none)
    std::vector<TypeCount> entities;    // top-level records in ENTITIES
    int vertices;                       // VERTEX records owned by a POLYLINE
    int attribs;                        // ATTRIB records owned by an INSERT
    std::vector<std::string> layers;    // LAYER table entries, in file order
    int blocks;                         // BLOCK records in BLOCKS
    std::vector<std::string> texts;     // TEXT group 1, in ENTITIES or in a block
};

std::string dxf_type(const hdxf::Entity& e) {
    switch (e.type) {
        case hdxf::EntityType::Line:      return "LINE";
        case hdxf::EntityType::Circle:    return "CIRCLE";
        case hdxf::EntityType::Arc:       return "ARC";
        case hdxf::EntityType::Point:     return "POINT";
        case hdxf::EntityType::Text:      return "TEXT";
        case hdxf::EntityType::Polyline:  return "POLYLINE";
        case hdxf::EntityType::Trace:     return "TRACE";
        case hdxf::EntityType::Solid:     return "SOLID";
        case hdxf::EntityType::Face3D:    return "3DFACE";
        case hdxf::EntityType::Shape:     return "SHAPE";
        case hdxf::EntityType::Insert:    return "INSERT";
        case hdxf::EntityType::Attdef:    return "ATTDEF";
        case hdxf::EntityType::Attrib:    return "ATTRIB";
        case hdxf::EntityType::Dimension: return "DIMENSION";
        case hdxf::EntityType::Viewport:  return "VIEWPORT";
        case hdxf::EntityType::LwPolyline: return "LWPOLYLINE";
        case hdxf::EntityType::Ellipse:   return "ELLIPSE";
        case hdxf::EntityType::Xline:     return "XLINE";
        case hdxf::EntityType::Ray:       return "RAY";
        case hdxf::EntityType::MText:     return "MTEXT";
        case hdxf::EntityType::Tolerance: return "TOLERANCE";
        case hdxf::EntityType::Leader:    return "LEADER";
        case hdxf::EntityType::Spline:    return "SPLINE";
        case hdxf::EntityType::Hatch:     return "HATCH";
        case hdxf::EntityType::Mline:     return "MLINE";
        case hdxf::EntityType::Image:     return "IMAGE";
        case hdxf::EntityType::Solid3D:   return "3DSOLID";
        case hdxf::EntityType::Region:    return "REGION";
        case hdxf::EntityType::Body:      return "BODY";
        case hdxf::EntityType::MLeader:   return "MULTILEADER";
        case hdxf::EntityType::Surface:   return "SURFACE";
        case hdxf::EntityType::ExtrudedSurface: return "EXTRUDEDSURFACE";
        case hdxf::EntityType::LoftedSurface:   return "LOFTEDSURFACE";
        case hdxf::EntityType::RevolvedSurface: return "REVOLVEDSURFACE";
        case hdxf::EntityType::SweptSurface:    return "SWEPTSURFACE";
        case hdxf::EntityType::PlaneSurface:    return "PLANESURFACE";
        case hdxf::EntityType::Helix:     return "HELIX";
        case hdxf::EntityType::Mesh:      return "MESH";
        case hdxf::EntityType::Wipeout:   return "WIPEOUT";
        case hdxf::EntityType::Light:     return "LIGHT";
        case hdxf::EntityType::Table:     return "ACAD_TABLE";
        case hdxf::EntityType::PdfUnderlay: return "PDFUNDERLAY";
        case hdxf::EntityType::DwfUnderlay: return "DWFUNDERLAY";
        case hdxf::EntityType::DgnUnderlay: return "DGNUNDERLAY";
        case hdxf::EntityType::CoordinationModel: return "COORDINATION_MODEL";
        case hdxf::EntityType::SectionObject: return "SECTIONOBJECT";
        case hdxf::EntityType::Unknown:   return e.as_unknown().entity_type;
    }
    return "?";
}

void collect_texts(const std::vector<hdxf::Entity>& entities, std::vector<std::string>& out) {
    for (const auto& e : entities) {
        if (e.type == hdxf::EntityType::Text) out.push_back(e.as_text().content);
    }
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    for (const auto& s : list) {
        if (s == value) return true;
    }
    return false;
}

void check_file(const Facit& facit) {
    std::filesystem::path path =
        hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(facit.file);
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip(facit.file + " missing (downloaded when HDXF_TEST_DATA=ON)");
    }

    hdxf::Drawing dwg;
    bool ok = dwg.load(path);
    hdxf_test::assert_true(ok, facit.file + ": " + dwg.last_error());
    hdxf_test::assert_true(dwg.version() == facit.version, facit.file + ": version " +
                           std::string(hdxf::acadver_string(dwg.version())));

    std::map<std::string, int> types;
    int vertices = 0;
    int attribs = 0;
    for (const auto& e : dwg.entities()) {
        types[dxf_type(e)]++;
        if (e.type == hdxf::EntityType::Polyline) {
            vertices += static_cast<int>(e.as_polyline().vertices.size());
        }
        if (e.type == hdxf::EntityType::Insert) {
            attribs += static_cast<int>(e.as_insert().attributes.size());
        }
    }
    int total = 0;
    for (const auto& tc : facit.entities) {
        hdxf_test::assert_eq(types[tc.type], tc.count, facit.file + ": " + tc.type);
        total += tc.count;
    }
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), total,
                         facit.file + ": entity count");
    hdxf_test::assert_eq(vertices, facit.vertices, facit.file + ": vertices");
    hdxf_test::assert_eq(attribs, facit.attribs, facit.file + ": attribs");

    std::vector<std::string> layers;
    for (const auto& layer : dwg.layers()) layers.push_back(layer.name);
    for (const auto& name : facit.layers) {
        hdxf_test::assert_true(contains(layers, name), facit.file + ": layer " + name);
    }
    hdxf_test::assert_eq(static_cast<int>(layers.size()), static_cast<int>(facit.layers.size()),
                         facit.file + ": layer count");

    hdxf_test::assert_eq(static_cast<int>(dwg.blocks().size()), facit.blocks,
                         facit.file + ": block count");

    std::vector<std::string> texts;
    collect_texts(dwg.entities(), texts);
    for (const auto& block : dwg.blocks()) collect_texts(block.entities, texts);
    for (const auto& text : facit.texts) {
        hdxf_test::assert_true(contains(texts, text), facit.file + ": text \"" + text + "\"");
    }
}

// --- ACadSharp: one drawing saved by AutoCAD in every version ---------------

const std::vector<TypeCount> acadsharp_r12_entities = {
    {"3DFACE", 1}, {"ARC", 1}, {"CIRCLE", 8}, {"DIMENSION", 11}, {"INSERT", 45},
    {"LINE", 28}, {"POINT", 6}, {"POLYLINE", 26}, {"SHAPE", 1}, {"SOLID", 1},
    {"TEXT", 29}, {"VIEWPORT", 2},
};

const std::vector<std::string> acadsharp_r12_layers = {
    "0", "LAYER1", "LAYER_OFF", "LAYER_FREEZE", "LAYER_LOCK", "LAYER_NOPLOT",
    "LAYER_COLOR_80", "LAYER_LT_DASH", "LAYER_LW_035", "LAYER_TRANSP_70", "LAYER_VP_FREEZE",
    "LAYER_DESC", "LAYER_TRUE_COLOR", "LAYER_COLOR_BOOK", "DEFPOINTS", "LAYER2",
    "LAYER_LW_050", "APP_LAYER", "COLOR_125_33_79", "0___1", "LAYER_COLOR_80___1",
};

// AutoCAD writes ∅ as Ø (0xD8 in ANSI_1252) in R12; there is no $DWGCODEPAGE, so
// ANSI_1252 is assumed. Strings are UTF-8 in the API (plan B1).
const std::vector<std::string> acadsharp_r12_texts = {
    "Hello this is a single line text", "XData All", "Materials", "\xC3\x98" "45,6",
};

const std::vector<TypeCount> acadsharp_2000_entities = {
    {"3DFACE", 1}, {"3DSOLID", 2}, {"ACAD_TABLE", 2}, {"ARC", 1}, {"CIRCLE", 8},
    {"DIMENSION", 11}, {"ELLIPSE", 1}, {"HATCH", 8}, {"IMAGE", 1}, {"INSERT", 6},
    {"LEADER", 1}, {"LINE", 26}, {"LWPOLYLINE", 21}, {"MESH", 2}, {"MLINE", 3},
    {"MTEXT", 5}, {"MULTILEADER", 15}, {"PDFUNDERLAY", 1}, {"POINT", 6}, {"POLYLINE", 2},
    {"RAY", 1}, {"REGION", 1}, {"SHAPE", 1}, {"SOLID", 1}, {"SPLINE", 2}, {"TEXT", 29},
    {"TOLERANCE", 3}, {"VIEWPORT", 2}, {"WIPEOUT", 1}, {"XLINE", 1},
};

// 2000–2007 also have "0 @ 1" and "Layer_color_80 @ 1"; 2010+ do not
std::vector<std::string> acadsharp_2000_layers(bool with_xref_copies) {
    std::vector<std::string> layers = {
        "0", "Layer1", "Layer_Off", "Layer_Freeze", "Layer_Lock", "Layer_NoPlot",
        "Layer_color_80", "Layer_Lt_dash", "Layer_lw_035", "Layer_transp_70",
        "Layer_vp_freeze", "Layer_desc", "Layer_true_color", "Layer_color_book", "Defpoints",
        "Layer2", "Layer_lw_050", "app_layer", "color_125_33_79",
    };
    if (with_xref_copies) {
        layers.push_back("0 @ 1");
        layers.push_back("Layer_color_80 @ 1");
    }
    return layers;
}

Facit acadsharp_r12(const std::string& file) {
    return {file, hdxf::DxfVersion::R12, acadsharp_r12_entities, 490, 4, acadsharp_r12_layers, 95,
            acadsharp_r12_texts};
}

Facit acadsharp_2000(const std::string& file, hdxf::DxfVersion version,
                     bool with_xref_copies) {
    return {file, version, acadsharp_2000_entities, 12, 4,
            acadsharp_2000_layers(with_xref_copies), 27,
            {"Hello this is a single line text", "XData All", "Materials"}};
}

} // namespace

TEST(external_acadsharp_r12_ascii)    { check_file(acadsharp_r12("acadsharp/sample_AC1009_ascii.dxf")); }
TEST(external_acadsharp_r12_binary)   { check_file(acadsharp_r12("acadsharp/sample_AC1009_binary.dxf")); }
TEST(external_acadsharp_2000_ascii)   { check_file(acadsharp_2000("acadsharp/sample_AC1015_ascii.dxf", hdxf::DxfVersion::R2000, true)); }
TEST(external_acadsharp_2000_binary)  { check_file(acadsharp_2000("acadsharp/sample_AC1015_binary.dxf", hdxf::DxfVersion::R2000, true)); }
TEST(external_acadsharp_2004_ascii)   { check_file(acadsharp_2000("acadsharp/sample_AC1018_ascii.dxf", hdxf::DxfVersion::R2004, true)); }
TEST(external_acadsharp_2004_binary)  { check_file(acadsharp_2000("acadsharp/sample_AC1018_binary.dxf", hdxf::DxfVersion::R2004, true)); }
TEST(external_acadsharp_2007_ascii)   { check_file(acadsharp_2000("acadsharp/sample_AC1021_ascii.dxf", hdxf::DxfVersion::R2007, true)); }
TEST(external_acadsharp_2007_binary)  { check_file(acadsharp_2000("acadsharp/sample_AC1021_binary.dxf", hdxf::DxfVersion::R2007, true)); }
TEST(external_acadsharp_2010_ascii)   { check_file(acadsharp_2000("acadsharp/sample_AC1024_ascii.dxf", hdxf::DxfVersion::R2010, false)); }
TEST(external_acadsharp_2010_binary)  { check_file(acadsharp_2000("acadsharp/sample_AC1024_binary.dxf", hdxf::DxfVersion::R2010, false)); }
TEST(external_acadsharp_2013_ascii)   { check_file(acadsharp_2000("acadsharp/sample_AC1027_ascii.dxf", hdxf::DxfVersion::R2013, false)); }
TEST(external_acadsharp_2013_binary)  { check_file(acadsharp_2000("acadsharp/sample_AC1027_binary.dxf", hdxf::DxfVersion::R2013, false)); }
TEST(external_acadsharp_2018_ascii)   { check_file(acadsharp_2000("acadsharp/sample_AC1032_ascii.dxf", hdxf::DxfVersion::R2018, false)); }
TEST(external_acadsharp_2018_binary)  { check_file(acadsharp_2000("acadsharp/sample_AC1032_binary.dxf", hdxf::DxfVersion::R2018, false)); }

// --- ezdxf: small files ------------------------------------------------------

TEST(external_ezdxf_bin_r12) {
    check_file({"ezdxf/bin_dxf_r12.dxf", hdxf::DxfVersion::R12, {{"LINE", 3}}, 0, 0, {"0"}, 2, {}});
}

TEST(external_ezdxf_bin_r13) {
    check_file({"ezdxf/bin_dxf_r13.dxf", hdxf::DxfVersion::R13, {{"LINE", 3}}, 0, 0, {"0"}, 2, {}});
}

TEST(external_ezdxf_bin_r14) {
    check_file({"ezdxf/bin_dxf_r14.dxf", hdxf::DxfVersion::R14, {{"LINE", 3}}, 0, 0, {"0"}, 2, {}});
}

TEST(external_ezdxf_bin_r2000) {
    check_file({"ezdxf/bin_dxf_r2000.dxf", hdxf::DxfVersion::R2000, {{"TEXT", 1}}, 0, 0, {"0", "Defpoints"}, 2,
                {"ezdxf"}});
}

TEST(external_ezdxf_small_r13) {
    check_file({"ezdxf/small_r13.dxf", hdxf::DxfVersion::R13, {{"POLYLINE", 16}}, 43, 0,
                {"0", "3USMM_PKG", "FOOT_PRINT"}, 2, {}});
}

TEST(external_ezdxf_small_r14) {
    check_file({"ezdxf/small_r14.dxf", hdxf::DxfVersion::R14, {}, 0, 0, {"0"}, 2, {}});
}

// The layer name in the file is UTF-8 mixed with a \U+ escape: "Tschüss mit \U+00FC"
TEST(external_ezdxf_unicode_2013) {
    check_file({"ezdxf/dxf_unicode.dxf", hdxf::DxfVersion::R2013, {}, 0, 0,
                {"0", "Defpoints", "Tsch\xC3\xBCss mit \xC3\xBC"}, 2, {}});
}

// Only an ENTITIES section — no HEADER, no tables
TEST(external_ezdxf_minimal_r12) {
    check_file({"ezdxf/Minimal_DXF_AC1009.dxf", hdxf::DxfVersion::R12, {}, 0, 0, {}, 0, {}});
}

TEST(external_ezdxf_r12_trash_beyond_eof) {
    check_file({"ezdxf/R12_with_trash_beyond_EOF.dxf", hdxf::DxfVersion::R12, {{"CIRCLE", 1}}, 0, 0, {}, 0, {}});
}

// 4 BLOCK records, two of them without a name: every one is kept (step 0.8 — the reader
// no longer merges blocks with the same name)
TEST(external_ezdxf_r12_blocks_with_no_names) {
    check_file({"ezdxf/r12_blocks_with_no_names.dxf", hdxf::DxfVersion::R12, {}, 0, 0, {"0", "Defpoints"}, 4,
                {}});
}

TEST(external_ezdxf_leica_disto) {
    check_file({"ezdxf/Leica_Disto_S910.dxf", hdxf::DxfVersion::R12,
                {{"LINE", 10}, {"POINT", 11}, {"TEXT", 11}}, 0, 0,
                {"LEICA_DISTO_ID", "LEICA_DISTO_3D", "LEICA_DISTO_POINT_ENTITY"}, 0,
                {"1 (IMG_20160506_121212.jpg)"}});
}

TEST(external_ezdxf_cloudcompare_dxflib) {
    check_file({"ezdxf/cc_dxflib.dxf", hdxf::DxfVersion::R12, {{"POLYLINE", 1}}, 5, 0, {"0", "POLYLINE_001"}, 3,
                {}});
}

TEST(external_ezdxf_minimal_2007_libdxfrw) {
    check_file({"ezdxf/Minimal_DXF_AC1021.dxf", hdxf::DxfVersion::R2007, {}, 0, 0, {"0"}, 2, {}});
}

// An R12 file whose records have no handles is written back with handles off: AutoCAD discards a
// drawing with $HANDLING 1 or a $HANDSEED whose records have none ("No database handle specified")
TEST(external_r12_without_handles_is_written_without_them) {
    for (const char* name : {"ezdxf/R12_with_trash_beyond_EOF.dxf", "ezdxf/cc_dxflib.dxf"}) {
        const std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
        if (!std::filesystem::exists(path)) hdxf_test::skip(std::string("missing ") + name);
        hdxf::Drawing d;
        hdxf_test::assert_true(d.load(path), d.last_error());
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            std::vector<uint8_t> out = d.save_to_memory(format);
            hdxf_test::assert_true(!out.empty(), d.last_error());
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
            const hdxf::HeaderVar* handling = back.find_header("$HANDLING");
            hdxf_test::assert_true(!handling || handling->values.empty() || handling->values[0].second == "0",
                                   std::string(name) + ": handles off");
            hdxf_test::assert_true(back.find_header("$HANDSEED") == nullptr, std::string(name) + ": no $HANDSEED");
        }
    }
}
