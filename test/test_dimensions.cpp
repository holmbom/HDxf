#include "test_framework.hpp"
#include "hdxf.hpp"
#include "dimension_block.hpp"
#include "text_layout.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Dimensions as AutoCAD builds them (rättningsfas R.7): the dimension style's newer
// variables, and the dimension blocks against AutoCAD's own in ACadSharp's samples.
// ---------------------------------------------------------------------------

namespace {

std::filesystem::path acadsharp(const std::string& file) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "acadsharp" / file;
}

const hdxf::DimStyle* dim_style(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& s : dwg.dim_styles()) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

// The groups of an ASCII DXF file as (code, trimmed value)
std::vector<std::pair<int, std::string>> groups_of(const std::vector<uint8_t>& data) {
    std::vector<std::pair<int, std::string>> out;
    std::istringstream in(std::string(data.begin(), data.end()));
    std::string code, value;
    while (std::getline(in, code) && std::getline(in, value)) {
        if (!value.empty() && value.back() == '\r') value.pop_back();
        size_t a = code.find_first_not_of(' ');
        size_t b = value.find_first_not_of(' ');
        out.push_back({std::stoi(code.substr(a)), b == std::string::npos ? std::string() : value.substr(b)});
    }
    return out;
}

// The record of type `type` whose group 2 is `name`
std::vector<std::pair<int, std::string>> record(const std::vector<std::pair<int, std::string>>& g,
                                                const std::string& type, const std::string& name) {
    for (size_t i = 0; i < g.size(); ++i) {
        if (g[i].first != 0 || g[i].second != type) continue;
        size_t end = i + 1;
        while (end < g.size() && g[end].first != 0) ++end;
        for (size_t k = i + 1; k < end; ++k) {
            if (g[k].first == 2 && g[k].second == name) return {g.begin() + static_cast<long>(i), g.begin() + static_cast<long>(end)};
        }
    }
    return {};
}

void check_ltypes_style(const hdxf::DimStyle* s, const std::string& what) {
    hdxf_test::assert_true(s != nullptr, what + ": custom_style_ltypes");
    hdxf_test::assert_eq(s->dimltype, "BATTING", what + ": DIMLTYPE");
    hdxf_test::assert_eq(s->dimltex1, "TRACKS", what + ": DIMLTEX1");
    hdxf_test::assert_eq(s->dimltex2, "ZIGZAG", what + ": DIMLTEX2");
}

} // namespace

// AutoCAD's 2004 file keeps the newer variables in XDATA, its 2007 file as group codes:
// the same values either way (a 2007 DIMSTYLE's second 70 is DIMTFILLCLR, not the flags)
TEST(dimstyle_newer_variables_read) {
    for (const char* file : {"sample_AC1018_ascii.dxf", "sample_AC1018_binary.dxf", "sample_AC1021_ascii.dxf",
                             "sample_AC1032_ascii.dxf"}) {
        if (!std::filesystem::exists(acadsharp(file))) hdxf_test::skip(std::string(file) + " missing");
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(acadsharp(file)), dwg.last_error());
        const hdxf::DimStyle* fill = dim_style(dwg, "custom_dim_style");
        hdxf_test::assert_true(fill != nullptr, std::string(file) + ": custom_dim_style");
        hdxf_test::assert_eq(fill->flags, 0, std::string(file) + ": flags");
        hdxf_test::assert_eq(fill->dimtfill, 2, std::string(file) + ": DIMTFILL");
        hdxf_test::assert_eq(fill->dimtfillclr.aci, 97, std::string(file) + ": DIMTFILLCLR");
        bool is_2004 = std::string(file).find("1018") != std::string::npos;
        if (is_2004) hdxf_test::assert_eq(fill->dimtfillclr.rgb, 0x367542, std::string(file) + ": its RGB");
        // DIMBREAK, DIMJAG, DIMTALN stay XDATA
        hdxf_test::assert_eq(static_cast<int>(fill->xdata.size()), 3, std::string(file) + ": the rest of the XDATA");
        check_ltypes_style(dim_style(dwg, "custom_style_ltypes"), file);
    }
}

// Written as AutoCAD writes 2004: each variable its own application, after the style's
// other XDATA, and their APPIDs in the table
TEST(dimstyle_newer_variables_written) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
    for (const char* lt : {"BATTING", "TRACKS", "ZIGZAG"}) {
        hdxf::Linetype l;
        l.name = lt;
        l.description = lt;
        dwg.linetypes().push_back(l);
    }
    hdxf::DimStyle& s = dwg.add_dim_style("Styled");
    s.dimtfill = 2;
    s.dimtfillclr = hdxf::Color::from_rgb(0x36, 0x75, 0x42);
    s.dimltype = "BATTING";
    s.dimltex1 = "TRACKS";
    s.dimltex2 = "ZIGZAG";
    s.dimfxlon = 1;
    s.dimfxl = 2.0;
    s.dimjogang = 90.0;
    s.xdata.push_back({"ACAD_DSTYLE_DIMBREAK", {{1070, "391"}, {1040, "0.125"}}});
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    auto g = groups_of(out);
    auto r = record(g, "DIMSTYLE", "Styled");
    size_t x = 0;
    while (x < r.size() && r[x].first != 1001) ++x;
    std::vector<std::pair<int, std::string>> xdata(r.begin() + static_cast<long>(x), r.end());
    std::vector<std::string> apps;
    for (const auto& [code, value] : xdata) {
        if (code == 1001) apps.push_back(value);
    }
    std::vector<std::string> want = {"ACAD_DSTYLE_DIMBREAK", "ACAD_DSTYLE_DIMTEXT_FILL", "ACAD_DSTYLE_DIM_LINETYPE",
                                     "ACAD_DSTYLE_DIM_EXT1_LINETYPE", "ACAD_DSTYLE_DIM_EXT2_LINETYPE",
                                     "ACAD_DSTYLE_DIMEXT_ENABLED", "ACAD_DSTYLE_DIMEXT_LENGTH",
                                     "ACAD_DSTYLE_DIMJOGGED_JOGANGLE_SYMBOL"};
    hdxf_test::assert_true(apps == want, "the applications in AutoCAD's order");
    // The fill exactly as AutoCAD's sample_AC1018
    bool fill = false;
    for (size_t i = 0; i + 4 < xdata.size(); ++i) {
        if (xdata[i].second != "ACAD_DSTYLE_DIMTEXT_FILL") continue;
        fill = xdata[i + 1].second == "376" && xdata[i + 2].second == "2" && xdata[i + 3].second == "377" &&
               xdata[i + 4] == std::pair<int, std::string>{1004, "1800000001000000427536C2000000000000000000000000"};
    }
    hdxf_test::assert_true(fill, "DIMTEXT_FILL as AutoCAD writes it");
    for (const auto& app : want) {
        hdxf_test::assert_false(record(g, "APPID", app).empty(), "APPID " + app);
    }
    // Read back: the same values
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    const hdxf::DimStyle* b = dim_style(back, "Styled");
    hdxf_test::assert_true(b != nullptr, "read back");
    hdxf_test::assert_eq(b->dimtfill, 2, "DIMTFILL back");
    hdxf_test::assert_eq(b->dimtfillclr.rgb, 0x367542, "DIMTFILLCLR back");
    hdxf_test::assert_eq(b->dimfxlon, 1, "DIMFXLON back");
    hdxf_test::assert_eq(b->dimfxl, 2.0, "DIMFXL back");
    hdxf_test::assert_true(std::fabs(b->dimjogang - 90.0) < 1e-9, "DIMJOGANG back");
    check_ltypes_style(nullptr != b ? b : nullptr, "back");
    hdxf_test::assert_eq(static_cast<int>(b->xdata.size()), 1, "DIMBREAK kept");

    // R12 has none of them: a warning
    hdxf::Drawing r12;
    r12.add_dim_style("Styled").dimtfill = 1;
    std::vector<uint8_t> r12_out = r12.save_to_memory();
    hdxf_test::assert_false(r12_out.empty(), r12.last_error());
    bool warned = false;
    for (const auto& w : r12.warnings()) warned = warned || w.find("Styled") != std::string::npos;
    hdxf_test::assert_true(warned, "R12 warns");
}

namespace {

using hdxf::internal::DimensionBuild;
using hdxf::internal::TextLayout;

std::filesystem::path windows_font(const std::string& file) {
    const char* windir = std::getenv("WINDIR");
    return windir ? hdxf::path_from_utf8(windir) / "Fonts" / file : std::filesystem::path(file);
}

struct FontPaths {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    ~FontPaths() { hdxf::set_font_paths(saved); }
};

bool near(double a, double b, double tolerance = 1e-9) {
    return std::fabs(a - b) <= tolerance * std::max(1.0, std::fabs(b));
}
bool near(const hdxf::Point3& a, const hdxf::Point3& b, double tolerance = 1e-9) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Two XDATA values alike: as numbers when both are, else as written
bool same_value(const std::string& a, const std::string& b) {
    char* ea = nullptr;
    char* eb = nullptr;
    double x = std::strtod(a.c_str(), &ea), y = std::strtod(b.c_str(), &eb);
    bool na = ea && *ea == '\0' && !a.empty(), nb = eb && *eb == '\0' && !b.empty();
    if (na && nb) return near(x, y);
    auto trim = [](const std::string& s) {
        size_t f = s.find_first_not_of(' ');
        return f == std::string::npos ? std::string() : s.substr(f);
    };
    return trim(a) == trim(b);
}

// What differs between our entity and AutoCAD's; empty when they are the same
std::string difference(const hdxf::Entity& a, const hdxf::Entity& b) {
    if (a.type != b.type) return "type";
    if (lower(a.props.layer) != lower(b.props.layer)) return "layer";
    if (a.props.color.aci != b.props.color.aci) return "color";
    if (a.props.lineweight != b.props.lineweight) return "lineweight";
    if (lower(a.props.linetype) != lower(b.props.linetype)) return "linetype";
    if (a.props.transparency.raw != b.props.transparency.raw) return "transparency";
    switch (a.type) {
        case hdxf::EntityType::Line:
            if (!near(a.as_line().start, b.as_line().start) || !near(a.as_line().end, b.as_line().end)) return "line";
            break;
        case hdxf::EntityType::Solid: {
            const auto& x = a.as_solid();
            const auto& y = b.as_solid();
            if (!near(x.p0, y.p0) || !near(x.p1, y.p1) || !near(x.p2, y.p2) || !near(x.p3, y.p3)) return "solid";
            break;
        }
        case hdxf::EntityType::Arc: {
            const auto& x = a.as_arc();
            const auto& y = b.as_arc();
            if (!near(x.center, y.center) || !near(x.radius, y.radius) || !near(x.start_angle, y.start_angle) ||
                !near(x.end_angle, y.end_angle)) return "arc";
            break;
        }
        case hdxf::EntityType::Insert: {
            const auto& x = a.as_insert();
            const auto& y = b.as_insert();
            if (lower(x.block_name) != lower(y.block_name) || !near(x.insertion_point, y.insertion_point) ||
                !near(x.x_scale, y.x_scale) || !near(x.rotation, y.rotation)) return "insert";
            break;
        }
        case hdxf::EntityType::MText: {
            const auto& x = a.as_mtext();
            const auto& y = b.as_mtext();
            // The direction to 1e-14: AutoCAD's 90° is (1e-16, 1), cos(90°) is 6e-17; its 45°
            // is (0.70710678118655035, 0.70710678118654469)
            if (x.content != y.content) return "text " + x.content + " / " + y.content;
            if (!near(x.insertion_point, y.insertion_point) || !near(x.height, y.height) ||
                !near(x.x_axis_direction, y.x_axis_direction, 1e-14)) {
                char text[256];
                std::snprintf(text, sizeof(text), "mtext (%.17g %.17g) h %.17g dir (%.17g %.17g) / (%.17g %.17g) h %.17g dir (%.17g %.17g)",
                              x.insertion_point.x, x.insertion_point.y, x.height, x.x_axis_direction.x, x.x_axis_direction.y,
                              y.insertion_point.x, y.insertion_point.y, y.height, y.x_axis_direction.x, y.x_axis_direction.y);
                return text;
            }
            if (x.background.flags != y.background.flags || x.background.color.aci != y.background.color.aci ||
                x.background.color.rgb != y.background.color.rgb || !near(x.background.scale, y.background.scale)) return "background";
            break;
        }
        case hdxf::EntityType::Point:
            if (!near(a.as_point().location, b.as_point().location)) return "point";
            break;
        default:
            break;
    }
    if (a.props.xdata.size() != b.props.xdata.size()) return "xdata";
    for (size_t i = 0; i < a.props.xdata.size(); ++i) {
        const auto& x = a.props.xdata[i];
        const auto& y = b.props.xdata[i];
        if (x.app_name != y.app_name || x.items.size() != y.items.size()) return "xdata " + x.app_name;
        for (size_t k = 0; k < x.items.size(); ++k) {
            if (x.items[k].code != y.items[k].code || !same_value(x.items[k].value, y.items[k].value)) {
                return "xdata " + x.app_name + " item " + std::to_string(k);
            }
        }
    }
    return {};
}

} // namespace

// Every dimension of ACadSharp's samples built as AutoCAD built it: the block's entities
// in AutoCAD's order with their properties, geometry, text, background and XDATA; 10, 11
// and 42 as AutoCAD keeps them — in 2000 and in 2004
TEST(dimension_blocks_as_autocad) {
    FontPaths keep;
    if (!std::filesystem::exists(windows_font("arial.ttf"))) hdxf_test::skip("arial.ttf missing");
    hdxf::set_font_paths({windows_font("arial.ttf").parent_path()});
    struct File {
        const char* name;
        hdxf::DxfVersion version;
    };
    for (const File& f : {File{"sample_AC1015_ascii.dxf", hdxf::DxfVersion::R2000},
                          File{"sample_AC1015_binary.dxf", hdxf::DxfVersion::R2000},
                          File{"sample_AC1018_ascii.dxf", hdxf::DxfVersion::R2004},
                          File{"sample_AC1018_binary.dxf", hdxf::DxfVersion::R2004}}) {
        if (!std::filesystem::exists(acadsharp(f.name))) hdxf_test::skip(std::string(f.name) + " missing");
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(acadsharp(f.name)), dwg.last_error());
        TextLayout layout(dwg);
        int dimensions = 0;
        for (const auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::Dimension) continue;
            ++dimensions;
            const auto& d = e.as_dimension();
            std::string what = std::string(f.name) + " " + d.block_name;
            const hdxf::Block* acad = dwg.find_block(d.block_name);
            hdxf_test::assert_true(acad != nullptr, what + ": AutoCAD's block");
            DimensionBuild built;
            std::string why;
            hdxf_test::assert_true(hdxf::internal::build_dimension(dwg, layout, e, f.version, built, why), what + ": " + why);
            hdxf_test::assert_eq(static_cast<int>(built.entities.size()), static_cast<int>(acad->entities.size()), what + ": entities");
            for (size_t i = 0; i < built.entities.size(); ++i) {
                std::string diff = difference(built.entities[i], acad->entities[i]);
                hdxf_test::assert_true(diff.empty(), what + " entity " + std::to_string(i) + ": " + diff);
            }
            hdxf_test::assert_true(near(built.definition_point, d.definition_point), what + ": 10");
            hdxf_test::assert_true(near(built.text_position, d.text_midpoint), what + ": 11");
            if (d.dim_type == hdxf::DimensionType::Angular) {
                hdxf_test::assert_true(near(built.arc_point, d.arc_point), what + ": 16");
            }
        }
        hdxf_test::assert_eq(dimensions, 11, std::string(f.name) + ": dimensions");
    }
}

namespace {

// A dimension built in a drawing whose Standard is Arial
DimensionBuild build(hdxf::Drawing& dwg, const std::function<void(hdxf::DimStyle&)>& style,
                     const std::function<void(hdxf::DimensionData&)>& dimension) {
    hdxf::DimStyle& s = dwg.add_dim_style("Case" + std::to_string(dwg.dim_styles().size()));
    s.dimasz = 2.5; s.dimexo = 0.625; s.dimexe = 1.25; s.dimtxt = 2.5; s.dimgap = 0.625;
    s.dimtad = 1; s.dimtih = 0; s.dimtoh = 0; s.dimzin = 8; s.dimdec = 2; s.dimdsep = ','; s.dimtofl = 1;
    s.dimcen = 2.5; s.dimtolj = 0;
    style(s);
    hdxf::Entity e;
    e.type = hdxf::EntityType::Dimension;
    hdxf::DimensionData d;
    d.dim_style = s.name;
    dimension(d);
    e.data = d;
    TextLayout layout(dwg);
    DimensionBuild out;
    std::string why;
    hdxf_test::assert_true(hdxf::internal::build_dimension(dwg, layout, e, hdxf::DxfVersion::R2000, out, why), why);
    return out;
}

hdxf::Drawing arial_drawing() {
    if (!std::filesystem::exists(windows_font("arial.ttf"))) hdxf_test::skip("arial.ttf missing");
    hdxf::set_font_paths({windows_font("arial.ttf").parent_path()});
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::TextStyle& s = dwg.add_text_style("Standard", "arial.ttf");
    s.font_family = "Arial";
    s.font_flags = 34;
    return dwg;
}

const hdxf::Entity* nth(const DimensionBuild& b, hdxf::EntityType type, int n) {
    for (const auto& e : b.entities) {
        if (e.type == type && n-- == 0) return &e;
    }
    return nullptr;
}

} // namespace

// The rules beyond AutoCAD's samples, as ODA File Converter's dimension blocks have them
// (its blocks match AutoCAD's for all of the samples): what fits between the extension
// lines, text beside the line outside, a horizontal landing, a short ordinate's jog, text
// on the inside of a turned angular arc
TEST(dimension_blocks_as_oda) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    // Linear, 8 long, text in the line: the arrows fit, the text does not — it goes beyond
    // extension line 2, an arrow size and a gap on
    DimensionBuild a = build(dwg, [](hdxf::DimStyle& s) { s.dimtad = 0; },
                             [](hdxf::DimensionData& d) { d.def_point2 = {0, 0, 0}; d.def_point3 = {8, 0, 0}; d.definition_point = {8, 10, 0}; });
    hdxf_test::assert_true(near(a.text_position, {11.94866985, 10, 0}, 1e-9), "text beyond extension line 2");
    // Linear, 5 long, text above: the arrows go out with their tails
    DimensionBuild b = build(dwg, [](hdxf::DimStyle&) {},
                             [](hdxf::DimensionData& d) { d.def_point2 = {0, 0, 0}; d.def_point3 = {5, 0, 0}; d.definition_point = {5, 10, 0}; });
    const hdxf::Entity* tail = nth(b, hdxf::EntityType::Line, 3);
    hdxf_test::assert_true(tail && near(tail->as_line().start, {-2.5, 10, 0}) && near(tail->as_line().end, {-5, 10, 0}), "arrow 1's tail");
    // Radius 30 at 45°, horizontal text outside: a jog an arrow size out, then a landing
    DimensionBuild c = build(dwg, [](hdxf::DimStyle& s) { s.dimtoh = 1; s.dimtih = 1; },
                             [](hdxf::DimensionData& d) {
                                 d.dim_type = hdxf::DimensionType::Radius;
                                 d.definition_point = {0, 0, 0};
                                 d.def_point4 = {21.21320343559643, 21.21320343559643, 0};
                             });
    hdxf_test::assert_true(near(c.text_position, {28.3563430714, 26.6510224711, 0}, 1e-10), "radius text on its landing");
    const hdxf::Entity* landing = nth(c, hdxf::EntityType::Line, 0);
    hdxf_test::assert_true(landing && near(landing->as_line().start, {31.3389488, 24.7487373, 0}, 1e-8), "the landing's end");
    // Ordinate whose end is nearer than four arrow sizes: the jog goes back
    DimensionBuild o = build(dwg, [](hdxf::DimStyle&) {},
                             [](hdxf::DimensionData& d) {
                                 d.dim_type = hdxf::DimensionType::Ordinate;
                                 d.definition_point = {0, 0, 0};
                                 d.def_point2 = {10, 5, 0};
                                 d.def_point3 = {16, 9, 0};
                             });
    const hdxf::Entity* jog = nth(o, hdxf::EntityType::Line, 1);
    hdxf_test::assert_true(jog && near(jog->as_line().start, {15, 5, 0}) && near(jog->as_line().end, {11, 9, 0}), "the jog");
    // Two lines, the arc on the far side: the turned text inside the arc
    DimensionBuild g = build(dwg, [](hdxf::DimStyle&) {},
                             [](hdxf::DimensionData& d) {
                                 d.dim_type = hdxf::DimensionType::Angular;
                                 d.def_point2 = {0, 0, 0};
                                 d.def_point3 = {20, 0, 0};
                                 d.def_point4 = {0, 0, 0};
                                 d.definition_point = {10, 10, 0};
                                 d.arc_point = {-20, -15, 0};
                             });
    hdxf_test::assert_true(near(g.text_position, {-21.3450203, -8.8413969, 0}, 1e-8), "turned text inside the arc");
    hdxf_test::assert_true(near(g.arc_point, {-24.14814565722691, -6.470476127563, 0}, 1e-9), "16 a third along the arc");
}

// Built-in arrowheads become blocks of the file (R12: wide POLYLINE), the definition
// points their layer Defpoints, and text keeps its form by version
TEST(dimension_blocks_written) {
    FontPaths keep;
    hdxf::set_font_paths({});
    for (auto version : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004}) {
        hdxf::Drawing dwg(version);
        hdxf::DimStyle& s = dwg.add_dim_style("Dots");
        s.dimblk = "_Dot";
        hdxf::Entity& e = dwg.add_dimension(hdxf::DimensionType::Rotated, {50, 10, 0}, {25, 12, 0});
        auto& d = std::get<hdxf::DimensionData>(e.data);
        d.def_point2 = {0, 0, 0};
        d.def_point3 = {50, 0, 0};
        d.dim_style = "Dots";
        std::vector<uint8_t> out = dwg.save_to_memory();
        hdxf_test::assert_false(out.empty(), dwg.last_error());
        hdxf::Drawing back;
        hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
        const hdxf::Block* dot = back.find_block("_Dot");
        hdxf_test::assert_true(dot != nullptr && dot->entities.size() == 2, "the _Dot block");
        auto wide = version == hdxf::DxfVersion::R12 ? hdxf::EntityType::Polyline : hdxf::EntityType::LwPolyline;
        hdxf_test::assert_true(dot->entities[0].type == wide, "the dot's wide polyline");
        bool defpoints = false;
        for (const auto& l : back.layers()) defpoints = defpoints || l.name == "Defpoints";
        hdxf_test::assert_true(defpoints, "layer Defpoints");
    }
    // Architectural units: 2000 "\S1/4;", 2004 "{\H1x;\S1#4;}" (diagonal, DIMFRAC 1)
    hdxf::DimStyle arch;
    arch.dimlunit = 4;
    arch.dimfrac = 1;
    arch.dimdec = 2;
    arch.dimzin = 0;
    hdxf::DimensionData dim;
    hdxf_test::assert_eq(hdxf::internal::dimension_text(arch, dim, 48.3635652311, false, hdxf::DxfVersion::R2000),
                         "4'-0\\S1/4;\"", "2000");
    hdxf_test::assert_eq(hdxf::internal::dimension_text(arch, dim, 48.3635652311, false, hdxf::DxfVersion::R2004),
                         "4'-0{\\H1x;\\S1#4;}\"", "2004");
}

// A dimension whose block is gone gets it built again, as AutoCAD's — not a reference to
// nothing
TEST(dimension_block_missing_is_built) {
    FontPaths keep;
    if (!std::filesystem::exists(acadsharp("sample_AC1015_ascii.dxf"))) hdxf_test::skip("sample_AC1015_ascii.dxf missing");
    if (!std::filesystem::exists(windows_font("arial.ttf"))) hdxf_test::skip("arial.ttf missing");
    hdxf::set_font_paths({windows_font("arial.ttf").parent_path()});
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(acadsharp("sample_AC1015_ascii.dxf")), dwg.last_error());
    std::string removed;
    for (const auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::Dimension && removed.empty()) removed = e.as_dimension().block_name;
    }
    const hdxf::Block* original = dwg.find_block(removed);
    hdxf_test::assert_true(original != nullptr, "AutoCAD's block " + removed);
    std::vector<hdxf::Entity> autocad = original->entities;
    auto& blocks = dwg.blocks();
    blocks.erase(std::find_if(blocks.begin(), blocks.end(), [&](const hdxf::Block& b) { return b.name == removed; }));
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    int dimensions = 0;
    const hdxf::Block* rebuilt = nullptr;
    for (const auto& e : back.entities()) {
        if (e.type != hdxf::EntityType::Dimension) continue;
        const hdxf::Block* b = back.find_block(e.as_dimension().block_name);
        hdxf_test::assert_true(b != nullptr, "the block of " + e.as_dimension().block_name);
        if (dimensions++ == 0) rebuilt = b;
    }
    hdxf_test::assert_eq(dimensions, 11, "dimensions");
    hdxf_test::assert_true(rebuilt != nullptr && rebuilt->name != removed, "a new name");
    hdxf_test::assert_eq(static_cast<int>(rebuilt->entities.size()), static_cast<int>(autocad.size()), "entities");
    for (size_t i = 0; i < autocad.size(); ++i) {
        std::string diff = difference(rebuilt->entities[i], autocad[i]);
        hdxf_test::assert_true(diff.empty(), "entity " + std::to_string(i) + ": " + diff);
    }
}

// A dimension in a plane of its own (extrusion 0,0,-1: OCS x is -WCS x): the filled arrows'
// corners in the OCS (SOLID), group 11 kept in the OCS as given
TEST(dimension_block_in_its_ocs) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    hdxf::DimStyle& s = dwg.add_dim_style("Plane");
    hdxf::Entity e;
    e.type = hdxf::EntityType::Dimension;
    e.props.extrusion = {0, 0, -1};
    hdxf::DimensionData d;
    d.dim_style = s.name;
    d.def_point2 = {0, 0, 0};
    d.def_point3 = {50, 0, 0};
    d.definition_point = {50, 10, 0};
    e.data = d;
    TextLayout layout(dwg);
    DimensionBuild built;
    std::string why;
    hdxf_test::assert_true(hdxf::internal::build_dimension(dwg, layout, e, hdxf::DxfVersion::R2000, built, why), why);
    auto wcs = [](const hdxf::Point3& p) { return hdxf::Point3{-p.x, p.y, -p.z}; };
    std::vector<hdxf::Point3> tips;
    for (const auto& x : built.entities) {
        if (x.type == hdxf::EntityType::Solid) tips.push_back(wcs(x.as_solid().p2));
    }
    hdxf_test::assert_eq(static_cast<int>(tips.size()), 2, "two filled arrows");
    bool at0 = false, at50 = false;
    for (const auto& t : tips) {
        at0 = at0 || near(t, {0, 10, 0});
        at50 = at50 || near(t, {50, 10, 0});
    }
    hdxf_test::assert_true(at0 && at50, "the arrows' tips at the dimension line's ends");

    d.flags |= 128;
    d.text_midpoint = {-25, 14, 0};
    e.data = d;
    DimensionBuild placed;
    hdxf_test::assert_true(hdxf::internal::build_dimension(dwg, layout, e, hdxf::DxfVersion::R2000, placed, why), why);
    hdxf_test::assert_true(near(placed.text_position, {-25, 14, 0}), "11 as given");
    const hdxf::Entity* text = nth(placed, hdxf::EntityType::MText, 0);
    hdxf_test::assert_true(text && std::fabs(text->as_mtext().insertion_point.x - 25.0) < 1e-9, "the text at WCS x 25");
}
