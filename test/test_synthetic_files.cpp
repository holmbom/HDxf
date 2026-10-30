#include "test_framework.hpp"
#include "hdxf.hpp"
#include <filesystem>
#include <string>
#include <utility>

// ---------------------------------------------------------------------------
// Synthetic test files in test/data/synthetic/ (phase 0, step 0.2)
//
// Small files of our own for what the external files do not cover: text encodings,
// an unknown code page, handles close to 16 hex digits and an embedded object.
// test/data/README.md describes each file.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path synthetic(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / hdxf::path_from_utf8(name);
}

bool has_layer(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& layer : dwg.layers()) {
        if (layer.name == name) return true;
    }
    return false;
}

std::string first_text(const hdxf::Drawing& dwg) {
    for (const auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::Text) return e.as_text().content;
    }
    return {};
}

} // namespace

// åäö ÅÄÖ € in ANSI_1252 — UTF-8 in the API (plan B1)
TEST(synthetic_r12_ansi_1252) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(synthetic("r12_ansi_1252.dxf")), dwg.last_error());
    hdxf_test::assert_true(has_layer(dwg, "Lager_\xC3\xA5\xC3\xA4\xC3\xB6"), "layer Lager_åäö");
    hdxf_test::assert_eq(first_text(dwg),
                         std::string("\xC3\xA5\xC3\xA4\xC3\xB6 \xC3\x85\xC3\x84\xC3\x96 \xE2\x82\xAC"));
}

// $DWGCODEPAGE ANSI_9999 with bytes >= 0x80: no raw bytes are let through (plan B1)
TEST(synthetic_r12_unknown_codepage) {
    hdxf::Drawing dwg;
    hdxf_test::assert_false(dwg.load(synthetic("r12_unknown_codepage.dxf")), "loaded");
    hdxf_test::assert_true(dwg.last_error().find("Unsupported code page: ANSI_9999") !=
                           std::string::npos, dwg.last_error());
}

// The 2000+ files are read with their version (step 0.9); saving them is refused (B3)
TEST(synthetic_2000_plus_read) {
    const std::pair<const char*, hdxf::DxfVersion> files[] = {
        {"r2000_large_handles.dxf", hdxf::DxfVersion::R2000},
        {"r2004_unicode_escapes.dxf", hdxf::DxfVersion::R2004},
        {"r2007_utf8.dxf", hdxf::DxfVersion::R2007},
        {"r2018_embedded_object.dxf", hdxf::DxfVersion::R2018},
    };
    for (const auto& [name, version] : files) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(synthetic(name)), std::string(name) + ": " +
                                                          dwg.last_error());
        hdxf_test::assert_true(dwg.version() == version, std::string(name) + ": version");
        hdxf_test::assert_false(dwg.entities().empty(), std::string(name) + ": entities");
        // Plan B30/B69: 2000 … 2018 are written as read
        hdxf_test::assert_false(dwg.save_to_memory().empty(), std::string(name) + ": saved " + dwg.last_error());
    }
}
