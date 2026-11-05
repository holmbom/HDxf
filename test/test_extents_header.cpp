#include "test_framework.hpp"
#include "hdxf.hpp"
#include "geometry/extents.hpp"
#include "io/group_io.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// $EXTMIN/$EXTMAX of a drawing read from a file: the file's values stay while the drawing is as it was
// read and nothing is calculated; a space whose geometry changed gets the drawing's own; a space with an entity removed gets
// the drawing's own without a comparison; a value the program set wins. R12 writes its body before its header, as the other
// versions do.
// ---------------------------------------------------------------------------

namespace {

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Point3;

using Group = std::pair<int, std::string>;

struct VersionName {
    DxfVersion version;
    const char* tag;            // as in the file names of doc/facit
};

const VersionName kVersions[] = {
    {DxfVersion::R12, "r12"},     {DxfVersion::R13, "r13"},     {DxfVersion::R14, "r14"},
    {DxfVersion::R2000, "2000"},  {DxfVersion::R2004, "2004"},  {DxfVersion::R2007, "2007"},
    {DxfVersion::R2010, "2010"},  {DxfVersion::R2013, "2013"},  {DxfVersion::R2018, "2018"},
};

// The versions AutoCAD 2025 saves, whose files are in doc/facit
const VersionName kFacitVersions[] = {
    {DxfVersion::R12, "r12"},     {DxfVersion::R2000, "2000"},  {DxfVersion::R2004, "2004"},
    {DxfVersion::R2007, "2007"},  {DxfVersion::R2010, "2010"},  {DxfVersion::R2013, "2013"},
    {DxfVersion::R2018, "2018"},
};

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

// Every group of a file, ASCII or binary, with its value as text
std::vector<Group> groups_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Group> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) out.emplace_back(gc.code, trimmed(gc.value));
    return out;
}

// The point of a header variable (10, 20, 30 after its 9)
bool header_point(const std::vector<Group>& groups, const std::string& name, Point3& p) {
    for (size_t i = 0; i + 3 < groups.size(); ++i) {
        if (groups[i].first != 9 || groups[i].second != name) continue;
        if (groups[i + 1].first != 10 || groups[i + 2].first != 20 || groups[i + 3].first != 30) return false;
        p = {std::strtod(groups[i + 1].second.c_str(), nullptr), std::strtod(groups[i + 2].second.c_str(), nullptr),
             std::strtod(groups[i + 3].second.c_str(), nullptr)};
        return true;
    }
    return false;
}

Point3 header_point_of(const std::vector<uint8_t>& data, const std::string& name) {
    Point3 p;
    hdxf_test::assert_true(header_point(groups_of(data), name, p), name + " is in the header");
    return p;
}

void same_point(const Point3& a, const Point3& b, double tolerance, const std::string& what) {
    char buf[300];
    std::snprintf(buf, sizeof buf, "%s: (%.12g %.12g %.12g) != (%.12g %.12g %.12g)", what.c_str(), a.x, a.y, a.z, b.x, b.y,
                  b.z);
    hdxf_test::assert_true(std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance &&
                               std::fabs(a.z - b.z) <= tolerance,
                           buf);
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path facit_file(const std::string& version, const std::string& state, bool binary) {
    std::string name = "acad_" + version + "_dxf_extents_changed";
    if (!state.empty()) name += "_" + state;
    if (binary) name += "_bin";
    return hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit" / hdxf::path_from_utf8(name + ".dxf");
}

Drawing load(const std::vector<uint8_t>& bytes) {
    Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), dwg.last_error());
    return dwg;
}

std::vector<uint8_t> save(Drawing& dwg, DxfFormat format) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

// A drawing as HDxf saves it: two lines and a circle inside (0,0) ... (10,9), and a line out to (100,50) when `far` is set
std::vector<uint8_t> made(DxfVersion version, DxfFormat format, bool far = false) {
    Drawing dwg(version);
    dwg.add_line({0, 0, 0}, {10, 5, 0});
    dwg.add_line({2, 2, 0}, {8, 9, 0});
    dwg.add_circle({5, 5, 0}, 2.0);
    if (far) dwg.add_line({0, 0, 0}, {100, 50, 0});
    return save(dwg, format);
}

hdxf::Entity* first_of(Drawing& dwg, hdxf::EntityType type) {
    for (auto& e : dwg.entities()) {
        if (e.type == type) return &e;
    }
    return nullptr;
}

// One change of the extents facit: the state `state` is the one before with this change
struct Step {
    const char* state;
    std::function<void(Drawing&)> change;
};

std::vector<Step> facit_steps() {
    return {
        {"moved", [](Drawing& d) {
             hdxf::Entity* insert = first_of(d, hdxf::EntityType::Insert);
             hdxf_test::assert_true(insert != nullptr, "the INSERT");
             insert->as_insert().insertion_point.x += 100.0;
         }},
        {"block", [](Drawing& d) {
             hdxf::Block* block = d.find_block("BX");
             hdxf_test::assert_true(block != nullptr, "block BX");
             for (auto& e : block->entities) {
                 if (e.type == hdxf::EntityType::Line) {
                     e.as_line().end = {50.0, 0.0, 0.0};
                     return;
                 }
             }
             hdxf_test::assert_true(false, "a line in BX");
         }},
        {"removed", [](Drawing& d) {
             auto& entities = d.entities();
             auto it = std::find_if(entities.begin(), entities.end(),
                                    [](const hdxf::Entity& e) { return e.type == hdxf::EntityType::Insert; });
             hdxf_test::assert_true(it != entities.end(), "the INSERT");
             entities.erase(it);
         }},
        {"added", [](Drawing& d) { d.add_line({-50, -50, 0}, {0, 0, 0}); }},
    };
}

} // namespace

// An unchanged read drawing is saved with the file's values, and no extents are calculated for it
TEST(unchanged_save_computes_nothing) {
    for (const auto& v : kVersions) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string what = std::string(v.tag) + (format == DxfFormat::Binary ? " binary" : " ascii");
            std::vector<uint8_t> bytes = made(v.version, format);
            Drawing dwg = load(bytes);
            const size_t before = hdxf::internal::drawing_extents_started();
            std::vector<uint8_t> out = save(dwg, format);
            hdxf_test::assert_true(hdxf::internal::drawing_extents_started() == before, what + ": extents calculated");
            same_point(header_point_of(out, "$EXTMIN"), header_point_of(bytes, "$EXTMIN"), 0.0, what + " $EXTMIN");
            same_point(header_point_of(out, "$EXTMAX"), header_point_of(bytes, "$EXTMAX"), 0.0, what + " $EXTMAX");
        }
    }
    // AutoCAD's own files: also byte for byte
    for (const auto& v : kFacitVersions) {
        const std::filesystem::path path =
            hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / hdxf::path_from_utf8(std::string("acad_") + v.tag + "_dxf.dxf");
        if (!std::filesystem::exists(path)) continue;
        std::vector<uint8_t> bytes = read_bytes(path);
        Drawing dwg = load(bytes);
        const size_t before = hdxf::internal::drawing_extents_started();
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii);
        hdxf_test::assert_true(hdxf::internal::drawing_extents_started() == before,
                               std::string(v.tag) + ": extents calculated for AutoCAD's file");
        hdxf_test::assert_true(out == bytes, std::string(v.tag) + ": AutoCAD's file byte for byte");
    }
}

// A line moved out: the header says so, in every version
TEST(moved_geometry_updates_header) {
    for (const auto& v : kVersions) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string what = std::string(v.tag) + (format == DxfFormat::Binary ? " binary" : " ascii");
            Drawing dwg = load(made(v.version, format));
            hdxf::Entity* line = first_of(dwg, hdxf::EntityType::Line);
            hdxf_test::assert_true(line != nullptr, what + ": a line");
            line->as_line().end = {100.0, 50.0, 0.0};
            const size_t before = hdxf::internal::drawing_extents_started();
            std::vector<uint8_t> out = save(dwg, format);
            hdxf_test::assert_true(hdxf::internal::drawing_extents_started() > before, what + ": extents calculated");
            same_point(header_point_of(out, "$EXTMIN"), {0.0, 0.0, 0.0}, 1e-9, what + " $EXTMIN");
            same_point(header_point_of(out, "$EXTMAX"), {100.0, 50.0, 0.0}, 1e-9, what + " $EXTMAX");
        }
    }
}

// The entity that made the box large is removed: the drawing made from the records that are left cannot tell, so the
// drawing's own extents are written
TEST(removed_outer_entity_shrinks_box) {
    for (const auto& v : kVersions) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const std::string what = std::string(v.tag) + (format == DxfFormat::Binary ? " binary" : " ascii");
            std::vector<uint8_t> bytes = made(v.version, format, true);
            same_point(header_point_of(bytes, "$EXTMAX"), {100.0, 50.0, 0.0}, 1e-9, what + " as made");
            Drawing dwg = load(bytes);
            auto& entities = dwg.entities();
            auto far = std::find_if(entities.begin(), entities.end(), [](const hdxf::Entity& e) {
                return e.type == hdxf::EntityType::Line && e.as_line().end.x == 100.0;
            });
            hdxf_test::assert_true(far != entities.end(), what + ": the far line");
            entities.erase(far);
            std::vector<uint8_t> out = save(dwg, format);
            same_point(header_point_of(out, "$EXTMIN"), {0.0, 0.0, 0.0}, 1e-9, what + " $EXTMIN");
            same_point(header_point_of(out, "$EXTMAX"), {10.0, 9.0, 0.0}, 1e-9, what + " $EXTMAX");
        }
    }
}

// A value the program set in the header is kept, also when the geometry changed
TEST(explicit_header_value_wins) {
    for (const auto& v : kVersions) {
        Drawing dwg = load(made(v.version, DxfFormat::Ascii));
        hdxf::Entity* line = first_of(dwg, hdxf::EntityType::Line);
        hdxf_test::assert_true(line != nullptr, std::string(v.tag) + ": a line");
        line->as_line().end = {100.0, 50.0, 0.0};
        dwg.set_header_point("$EXTMAX", {777.0, 888.0, 0.0});
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii);
        same_point(header_point_of(out, "$EXTMAX"), {777.0, 888.0, 0.0}, 1e-9, std::string(v.tag) + " $EXTMAX");
        same_point(header_point_of(out, "$EXTMIN"), {0.0, 0.0, 0.0}, 1e-9, std::string(v.tag) + " $EXTMIN");
    }
}

// A drawing read from a file that has no VPORT record gets the default *Active view, which shows the extents: they are
// calculated before the body then, and used at the header too
TEST(missing_active_has_view) {
    for (DxfVersion version : {DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2018}) {
        Drawing dwg = load(made(version, DxfFormat::Ascii));
        dwg.vports().clear();
        const size_t before = hdxf::internal::drawing_extents_started();
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii);
        const size_t calculations = hdxf::internal::drawing_extents_started() - before;
        hdxf_test::assert_true(calculations >= 1 && calculations <= 2,
                               "the extents are calculated once for the drawing (and once for the original): " +
                                   std::to_string(calculations));

        // The *Active viewport looks at the middle of the box
        std::vector<Group> groups = groups_of(out);
        double center_x = 0.0, center_y = 0.0;
        bool found = false;
        for (size_t i = 0; i < groups.size() && !found; ++i) {
            if (groups[i].first != 0 || groups[i].second != "VPORT") continue;
            size_t end = i + 1;
            bool active = false;
            double cx = 0.0, cy = 0.0;
            for (; end < groups.size() && groups[end].first != 0; ++end) {
                if (groups[end].first == 2) {
                    std::string name = groups[end].second;
                    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    active = name == "*ACTIVE";
                }
                if (groups[end].first == 12) cx = std::strtod(groups[end].second.c_str(), nullptr);
                if (groups[end].first == 22) cy = std::strtod(groups[end].second.c_str(), nullptr);
            }
            if (active) {
                found = true;
                center_x = cx;
                center_y = cy;
            }
        }
        hdxf_test::assert_true(found, "a VPORT *Active in the output");
        Point3 low = header_point_of(out, "$EXTMIN");
        Point3 high = header_point_of(out, "$EXTMAX");
        hdxf_test::assert_true(std::fabs(center_x - (low.x + high.x) / 2.0) < 1e-9, "view center x");
        hdxf_test::assert_true(std::fabs(center_y - (low.y + high.y) / 2.0) < 1e-9, "view center y");
    }
}

// The numbers of the extents are written in the form of the version: R12's, AutoCAD's 16 digits from R13 on
TEST(header_number_format_per_version) {
    const double x = 12345.678901234567;
    for (const auto& v : kVersions) {
        Drawing dwg = load(made(v.version, DxfFormat::Ascii));
        hdxf::Entity* line = first_of(dwg, hdxf::EntityType::Line);
        hdxf_test::assert_true(line != nullptr, std::string(v.tag) + ": a line");
        line->as_line().end = {x, 1.0, 0.0};
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii);
        std::vector<Group> groups = groups_of(out);
        std::string written;
        for (size_t i = 0; i + 1 < groups.size(); ++i) {
            if (groups[i].first == 9 && groups[i].second == "$EXTMAX") {
                written = groups[i + 1].second;
                break;
            }
        }
        std::string expected;
        if (v.version == DxfVersion::R12) {
            expected = hdxf::internal::format_double(x, hdxf::internal::NumberStyle::R12);
        } else {
            const std::string text = hdxf::internal::format_double(x, hdxf::internal::NumberStyle::AutoCad);
            expected = hdxf::internal::format_double(std::strtod(text.c_str(), nullptr), hdxf::internal::NumberStyle::AutoCad);
        }
        hdxf_test::assert_eq(written, expected, std::string(v.tag) + " $EXTMAX x");
    }
}

// R12 written body first: an unchanged file comes out as it was, ASCII and binary, and a changed one has its header as it was
TEST(r12_body_first_keeps_bytes) {
    const std::filesystem::path facit_dir = hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc";
    bool any = false;
    for (const std::filesystem::path& path :
         {facit_dir / "acad_r12_dxf.dxf", facit_file("r12", "", false), facit_file("r12", "", true)}) {
        if (!std::filesystem::exists(path)) continue;
        any = true;
        std::vector<uint8_t> in = read_bytes(path);
        const DxfFormat format = in.size() >= 18 && std::string(in.begin(), in.begin() + 18) == "AutoCAD Binary DXF" ? DxfFormat::Binary
                                                                                                                   : DxfFormat::Ascii;
        Drawing dwg = load(in);
        std::vector<uint8_t> out = save(dwg, format);
        hdxf_test::assert_true(out == in, path.filename().string() + ": unchanged, byte for byte");

        // A layer changed: the header is the file's (HANDSEED and all), the body follows it
        Drawing changed = load(in);
        hdxf_test::assert_true(!changed.layers().empty(), "a layer");
        changed.layers()[0].color = hdxf::Color::from_aci(3);
        std::vector<uint8_t> after = save(changed, format);
        hdxf_test::assert_true(after != in, "the change is written");
        std::vector<Group> before_groups = groups_of(in);
        std::vector<Group> after_groups = groups_of(after);
        auto header_of = [](const std::vector<Group>& groups) {
            std::vector<Group> header;
            for (const auto& g : groups) {
                header.push_back(g);
                if (g.first == 0 && g.second == "ENDSEC") break;
            }
            return header;
        };
        hdxf_test::assert_true(header_of(before_groups) == header_of(after_groups),
                               path.filename().string() + ": the header as it was");
        size_t tables = 0;
        while (tables < after_groups.size() && !(after_groups[tables].first == 2 && after_groups[tables].second == "TABLES")) ++tables;
        hdxf_test::assert_true(tables < after_groups.size(), "TABLES follows the header");
    }
    if (!any) hdxf_test::skip("no R12 facit file");
}

// The states of AutoCAD's extents facit, one after the other: HDxf makes the same change in the state before and writes
// the extents AutoCAD has in the next
TEST(extents_follow_autocad_states) {
    size_t compared = 0;
    for (const auto& v : kFacitVersions) {
        for (bool binary : {false, true}) {
            std::string state;
            for (const Step& step : facit_steps()) {
                const std::filesystem::path before = facit_file(v.tag, state, binary);
                const std::filesystem::path next = facit_file(v.tag, step.state, binary);
                state = step.state;
                if (!std::filesystem::exists(before) || !std::filesystem::exists(next)) continue;
                const std::string what = std::string(v.tag) + (binary ? " binary " : " ascii ") + step.state;
                Drawing dwg = load(read_bytes(before));
                step.change(dwg);
                std::vector<uint8_t> out = save(dwg, binary ? DxfFormat::Binary : DxfFormat::Ascii);
                std::vector<uint8_t> facit = read_bytes(next);
                same_point(header_point_of(out, "$EXTMIN"), header_point_of(facit, "$EXTMIN"), 1e-5, what + " $EXTMIN");
                same_point(header_point_of(out, "$EXTMAX"), header_point_of(facit, "$EXTMAX"), 1e-5, what + " $EXTMAX");
                ++compared;
            }
        }
    }
    if (compared == 0) hdxf_test::skip("no extents facit files in doc/facit");
}
