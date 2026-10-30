#include "test_framework.hpp"
#include "hdxf.hpp"

#include <filesystem>
#include <string>
#include <string_view>

// Paths with non-ASCII characters. On Windows a narrow std::ifstream(std::string)
// goes to fopen and is read in the ANSI code page, so these files are only found
// when the public API takes std::filesystem::path.

// ---------------------------------------------------------------------------
// UTF-8 helpers round-trip
// ---------------------------------------------------------------------------

TEST(path_utf8_helpers_roundtrip) {
    const std::string utf8 = reinterpret_cast<const char*>(u8"ritning_åäö.dxf");

    auto path = hdxf::path_from_utf8(utf8);
    hdxf_test::assert_eq(hdxf::utf8_from_path(path), utf8);
}

// ---------------------------------------------------------------------------
// Helper edge cases: empty view, sub-view that is not null-terminated
// ---------------------------------------------------------------------------

TEST(path_utf8_helpers_edge_cases) {
    hdxf_test::assert_true(hdxf::path_from_utf8(std::string_view{}).empty(),
                           "empty view should give an empty path");
    hdxf_test::assert_true(hdxf::path_from_utf8("").empty(),
                           "empty string should give an empty path");
    hdxf_test::assert_eq(hdxf::utf8_from_path(std::filesystem::path{}), std::string());

    // Sub-view without a terminating NUL — only size() may be relied on.
    const std::string utf8 = reinterpret_cast<const char*>(u8"ritning_åäö.dxf_SKRÄP");
    const std::string_view view(utf8.data(), utf8.size() - std::string("_SKRÄP").size());
    hdxf_test::assert_eq(hdxf::utf8_from_path(hdxf::path_from_utf8(view)),
                         std::string(view));
}

// ---------------------------------------------------------------------------
// save_as / load round-trip on a non-ASCII path (ASCII format)
// ---------------------------------------------------------------------------

TEST(path_non_ascii_roundtrip_ascii) {
    const std::string utf8 = reinterpret_cast<const char*>(u8"test_åäö_ritning.dxf");
    auto path = hdxf::path_from_utf8(utf8);

    hdxf::Drawing dwg;
    dwg.add_layer("TestLayer", hdxf::Color::red());
    dwg.add_line({0, 0, 0}, {10, 10, 0}, "TestLayer");
    dwg.add_circle({5, 5, 0}, 3.0);

    hdxf_test::assert_true(dwg.save_as(path), "save_as() on a non-ASCII path failed");

    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load(path), "load() of a non-ASCII path failed");
    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 2);
    hdxf_test::assert_true(loaded.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_true(loaded.entities()[1].type == hdxf::EntityType::Circle);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// save_as / load round-trip on a non-ASCII path (Binary format)
// ---------------------------------------------------------------------------

TEST(path_non_ascii_roundtrip_binary) {
    const std::string utf8 = reinterpret_cast<const char*>(u8"test_åäö_binär.dxf");
    auto path = hdxf::path_from_utf8(utf8);

    hdxf::Drawing dwg;
    dwg.add_line({1, 2, 3}, {4, 5, 6});
    dwg.add_text({0, 0, 0}, "Hello", 2.5);

    hdxf_test::assert_true(dwg.save_as(path, hdxf::DxfFormat::Binary),
                           "save_as(Binary) on a non-ASCII path failed");

    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load(path), "load() of a non-ASCII binary path failed");
    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 2);
    hdxf_test::assert_true(loaded.entities()[0].type == hdxf::EntityType::Line);
    hdxf_test::assert_true(loaded.entities()[1].type == hdxf::EntityType::Text);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// save() writes back to the path load()/save_as() remembered
// ---------------------------------------------------------------------------

TEST(path_non_ascii_save_reuses_path) {
    const std::string utf8 = reinterpret_cast<const char*>(u8"test_åäö_spara.dxf");
    auto path = hdxf::path_from_utf8(utf8);

    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    hdxf_test::assert_true(dwg.save_as(path), "save_as() on a non-ASCII path failed");

    dwg.add_circle({2, 2, 0}, 1.0);
    hdxf_test::assert_true(dwg.save(), "save() to the remembered non-ASCII path failed");

    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load(path), "load() after save() failed");
    hdxf_test::assert_eq(static_cast<int>(loaded.entities().size()), 2);

    std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// Error message keeps the path as UTF-8
// ---------------------------------------------------------------------------

TEST(path_non_ascii_load_error_is_utf8) {
    const std::string utf8 = reinterpret_cast<const char*>(u8"finns_inte_åäö.dxf");
    auto path = hdxf::path_from_utf8(utf8);

    hdxf::Drawing dwg;
    hdxf_test::assert_false(dwg.load(path), "load() of a missing file should fail");
    hdxf_test::assert_true(dwg.last_error().find(utf8) != std::string::npos,
                           "last_error() should contain the UTF-8 path");
}

// ---------------------------------------------------------------------------
// save() without a path reports why
// ---------------------------------------------------------------------------

TEST(path_save_without_filename_sets_error) {
    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {1, 1, 0});

    hdxf_test::assert_false(dwg.save(), "save() should fail when no filename is set");
    hdxf_test::assert_eq(dwg.last_error(), std::string("No filename set"));
}

// ---------------------------------------------------------------------------
// Failed writes report the path as UTF-8; a later success clears the error
// ---------------------------------------------------------------------------

TEST(path_save_as_error_is_utf8_and_cleared_on_success) {
    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {1, 1, 0});

    // Use an existing regular file as the parent directory — that always fails
    // to open, without depending on what happens to exist in the test's cwd.
    const std::string blocker = reinterpret_cast<const char*>(u8"test_åäö_blockerare.dxf");
    const auto blocker_path = hdxf::path_from_utf8(blocker);
    hdxf_test::assert_true(dwg.save_as(blocker_path), "save_as() of the blocker failed");

    const std::string unreachable = blocker + "/ritning.dxf";
    hdxf_test::assert_false(dwg.save_as(hdxf::path_from_utf8(unreachable)),
                            "save_as() below a regular file should fail");
    hdxf_test::assert_true(dwg.last_error().find(unreachable) != std::string::npos,
                           "last_error() should contain the UTF-8 path");
    std::filesystem::remove(blocker_path);

    const auto ok_path = hdxf::path_from_utf8(
        reinterpret_cast<const char*>(u8"test_åäö_efter_fel.dxf"));
    hdxf_test::assert_true(dwg.save_as(ok_path), "save_as() to a valid path failed");
    hdxf_test::assert_eq(dwg.last_error(), std::string(),
                         "last_error() should be cleared after a successful save_as()");

    std::filesystem::remove(ok_path);
}

// ---------------------------------------------------------------------------
// Source compatibility: string literals and std::string still convert
// ---------------------------------------------------------------------------

TEST(path_accepts_string_literal_and_std_string) {
    hdxf::Drawing dwg;
    dwg.add_line({0, 0, 0}, {1, 1, 0});

    hdxf_test::assert_true(dwg.save_as("test_path_literal.dxf"), "save_as(literal) failed");

    const std::string name = "test_path_string.dxf";
    hdxf_test::assert_true(dwg.save_as(name), "save_as(std::string) failed");

    hdxf::Drawing loaded;
    hdxf_test::assert_true(loaded.load("test_path_literal.dxf"), "load(literal) failed");
    hdxf_test::assert_true(loaded.load(name), "load(std::string) failed");

    std::filesystem::remove("test_path_literal.dxf");
    std::filesystem::remove(name);
}
