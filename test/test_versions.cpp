#include "test_framework.hpp"
#include "test_drawing_builder.hpp"
#include "hdxf.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// ---------------------------------------------------------------------------
// New versions turned on (phase 0, step 0.9) and the write rule (plan B3)
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path synthetic(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / hdxf::path_from_utf8(name);
}

std::string read_all(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

TEST(versions_acadver_strings) {
    const std::pair<hdxf::DxfVersion, const char*> all[] = {
        {hdxf::DxfVersion::R12, "AC1009"},   {hdxf::DxfVersion::R13, "AC1012"},
        {hdxf::DxfVersion::R14, "AC1014"},   {hdxf::DxfVersion::R2000, "AC1015"},
        {hdxf::DxfVersion::R2004, "AC1018"}, {hdxf::DxfVersion::R2007, "AC1021"},
        {hdxf::DxfVersion::R2010, "AC1024"}, {hdxf::DxfVersion::R2013, "AC1027"},
        {hdxf::DxfVersion::R2018, "AC1032"},
    };
    for (const auto& [version, acadver] : all) {
        hdxf_test::assert_eq(std::string(hdxf::acadver_string(version)), std::string(acadver));
        auto back = hdxf::version_from_acadver(acadver);
        hdxf_test::assert_true(back && *back == version, acadver);
    }
    hdxf_test::assert_false(hdxf::version_from_acadver("AC1006").has_value(), "AC1006");
    hdxf_test::assert_false(hdxf::version_from_acadver("ac1015").has_value(), "lower case");
}

// B86 (phase 7, was B42/B30/B20/B11/B3): a drawing read from R13 or R14 is saved as that version
// and no other — not with save(), save_as() or save_to_memory(), also after set_version(): another
// version would be a conversion (phase 8), which fails before anything is changed or written
TEST(versions_b86_drawing_from_r13_and_r14) {
    struct Source {
        const char* file;
        hdxf::DxfVersion version;
        const char* name;
    };
    const Source sources[] = {{"small_r13.dxf", hdxf::DxfVersion::R13, "R13"},
                              {"small_r14.dxf", hdxf::DxfVersion::R14, "R14"}};
    for (const auto& source : sources) {
        std::filesystem::path external = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "ezdxf" / source.file;
        if (!std::filesystem::exists(external)) {
            hdxf_test::skip(std::string("external test file missing: ezdxf/") + source.file);
            return;
        }
        std::filesystem::path copy = std::filesystem::temp_directory_path() / "hdxf_b3_source.dxf";
        std::filesystem::path target = std::filesystem::temp_directory_path() / "hdxf_b3_target.dxf";
        std::filesystem::copy_file(external, copy, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(target);
        const std::string original = read_all(copy);

        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(copy), dwg.last_error());
        hdxf_test::assert_true(dwg.version() == source.version, "version");
        const size_t entities = dwg.entities().size();
        const hdxf::HeaderVar* seed = dwg.find_header("$HANDSEED");
        const std::string handseed = seed ? seed->values[0].second : "";

        // As its own version it is saved, in both formats
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            hdxf_test::assert_false(dwg.save_to_memory(format).empty(), std::string(source.name) + ": " + dwg.last_error());
        }
        hdxf_test::assert_true(dwg.save_as(target), std::string(source.name) + ": " + dwg.last_error());
        hdxf_test::assert_true(std::filesystem::exists(target), "target file");
        std::filesystem::remove(target);

        // Any other version is refused, with the message of every conversion
        for (hdxf::DxfVersion other : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R13, hdxf::DxfVersion::R14,
                                       hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2018}) {
            if (other == source.version) continue;
            dwg.set_version(other);
            std::string as;
            switch (other) {
                case hdxf::DxfVersion::R12:   as = "R12"; break;
                case hdxf::DxfVersion::R13:   as = "R13"; break;
                case hdxf::DxfVersion::R14:   as = "R14"; break;
                case hdxf::DxfVersion::R2000: as = "2000"; break;
                case hdxf::DxfVersion::R2010: as = "2010"; break;
                default:                      as = "2018"; break;
            }
            const std::string message = std::string("Writing DXF ") + source.name + " as " + as +
                                        " is not supported yet (the drawing was read from that version)";
            hdxf_test::assert_false(dwg.save(), "save()");
            hdxf_test::assert_eq(dwg.last_error(), message);
            hdxf_test::assert_false(dwg.save_as(target), "save_as()");
            hdxf_test::assert_eq(dwg.last_error(), message);
            hdxf_test::assert_true(dwg.save_to_memory().empty(), "save_to_memory()");
            hdxf_test::assert_eq(dwg.last_error(), message);
            hdxf_test::assert_false(std::filesystem::exists(target), "target file created");
            hdxf_test::assert_eq(read_all(copy), original, "source file unchanged");
        }
        hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), static_cast<int>(entities));
        seed = dwg.find_header("$HANDSEED");
        hdxf_test::assert_eq(seed ? seed->values[0].second : "", handseed, "$HANDSEED unchanged");

        // The version back: save() writes the loaded file again, and the other version's refusal
        // changed nothing
        dwg.set_version(source.version);
        hdxf_test::assert_true(dwg.save(), std::string(source.name) + ": " + dwg.last_error());
        std::filesystem::remove(copy);
    }
}

// B69 (phase 6): a drawing read from 2018 is saved as 2018, not as another version (conversion is
// phase 8) — and a refusal changes nothing
TEST(versions_b69_drawing_from_2018) {
    std::filesystem::path source = synthetic("r2018_embedded_object.dxf");
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(source), dwg.last_error());
    hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R2018, "version");
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    const size_t entities = dwg.entities().size();
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R13, hdxf::DxfVersion::R14,
                               hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004,
                               hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013}) {
        dwg.set_version(v);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved as another version");
        hdxf_test::assert_true(dwg.last_error().find("Writing DXF 2018 as ") != std::string::npos &&
                               dwg.last_error().find("is not supported yet") != std::string::npos,
                               dwg.last_error());
    }
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), static_cast<int>(entities));
    dwg.set_version(hdxf::DxfVersion::R2018);
    hdxf_test::assert_true(dwg.save_to_memory() == out, "saved as 2018 again: the same bytes");
}

// B11/B20/B30/B42: a drawing read from 2000 is saved as 2000, one read from 2004 as 2004, one
// read from 2007 as 2007; as another version it would be a conversion (phase 8), which fails
TEST(versions_b11_drawing_from_2000) {
    struct Case {
        const char* file;
        hdxf::DxfVersion read;
        std::vector<hdxf::DxfVersion> refused;
    };
    const Case cases[] = {
        {"r2000_large_handles.dxf", hdxf::DxfVersion::R2000,
         {hdxf::DxfVersion::R12, hdxf::DxfVersion::R13, hdxf::DxfVersion::R14, hdxf::DxfVersion::R2004,
          hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010}},
        {"r2004_unicode_escapes.dxf", hdxf::DxfVersion::R2004,
         {hdxf::DxfVersion::R12, hdxf::DxfVersion::R13, hdxf::DxfVersion::R14, hdxf::DxfVersion::R2000,
          hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010}},
        {"r2007_utf8.dxf", hdxf::DxfVersion::R2007,
         {hdxf::DxfVersion::R12, hdxf::DxfVersion::R13, hdxf::DxfVersion::R14, hdxf::DxfVersion::R2000,
          hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2010}},
    };
    for (const auto& c : cases) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(synthetic(c.file)), dwg.last_error());
        hdxf_test::assert_true(dwg.version() == c.read, c.file);
        hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
        for (hdxf::DxfVersion v : c.refused) {
            dwg.set_version(v);
            hdxf_test::assert_true(dwg.save_to_memory().empty(), std::string(c.file) + ": saved as another version");
            hdxf_test::assert_true(dwg.last_error().find(" as ") != std::string::npos &&
                                   dwg.last_error().find("is not supported yet") != std::string::npos,
                                   dwg.last_error());
        }
    }
}

// Plan B56/B69/B86 (phases 5, 6 and 7): a drawing built in memory can be saved as R12, R13, R14, 2000,
// 2004, 2007, 2010, 2013 and 2018
TEST(versions_b11_in_memory_other_version) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2010,
                               hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2000,
                               hdxf::DxfVersion::R14, hdxf::DxfVersion::R13, hdxf::DxfVersion::R12,
                               hdxf::DxfVersion::R2018}) {
        dwg.set_version(v);
        hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    }
}

// After reading an R12 file, the drawing is R12 and can be saved as before
TEST(versions_r12_file_saves) {
    hdxf::Drawing dwg = r12_drawing_read_back();
    hdxf_test::assert_true(dwg.version() == hdxf::DxfVersion::R12, "version");
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
}
