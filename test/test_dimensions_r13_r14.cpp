#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "dimension_block.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DIMENSION, its blocks (*Dn, arrowheads, Defpoints), VIEWPORT and paper space in DXF R13 and R14
// (phase 7, step 7.16, B91, B93). No golden files. What the tests hold the output to: Autodesk's R13
// and R14 DXF references (DIMENSION, VIEWPORT with its ACAD/MVIEW extended data), AutoCAD's own
// records — acadsharp/sample_AC1009 (AutoCAD saving R12: 13 DIMENSIONs, a VIEWPORT) and, where the
// local copies are there, local-r13r14/BL15-1 (AutoCAD 2000 saving R14: 12 DIMENSIONs, `*D` blocks),
// Zurn_59251 (R14, AutoCAD 2005) and puppenhaus_4mm (R14: two VIEWPORTs) — and the model's own
// numbers (measurements, extents).
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::internal::GroupCode;
using hdxf::internal::GroupReader;
using Group = std::pair<int, std::string>;
using Rec = dxf_structure::Rec;

const DxfVersion older_versions[] = {DxfVersion::R13, DxfVersion::R14};
const DxfFormat both_formats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfVersion v) { return v == DxfVersion::R13 ? "R13" : "R14"; }
std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

struct FontPaths {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    ~FontPaths() { hdxf::set_font_paths(saved); }
};

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), what + ": " + dwg.last_error());
    return out;
}

// Every group of a file, ASCII or binary, to its EOF; numbers without their alignment
std::vector<Group> groups_of(const std::vector<uint8_t>& data) {
    std::istringstream in(std::string(data.begin(), data.end()), std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    std::vector<Group> out;
    GroupCode gc;
    while (reader.read(gc)) {
        std::string value = dxf_structure::number_text(gc.code, gc.value);
        while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
        out.push_back({gc.code, value});
    }
    hdxf_test::assert_true(reader.error().empty(), "reading the file: " + reader.error());
    return out;
}

std::vector<Rec> records_of(const std::vector<Group>& groups) {
    std::vector<Rec> out;
    std::string section;
    for (const auto& g : groups) {
        if (g.first == 0) out.push_back({section, {}});
        if (out.empty()) continue;
        out.back().groups.push_back(g);
        if (g.first == 2 && out.back().type() == "SECTION") section = g.second;
        if (g.first == 0 && g.second == "ENDSEC") section.clear();
    }
    return out;
}

std::vector<const Rec*> of_type(const std::vector<Rec>& recs, const std::string& section, const std::string& type) {
    std::vector<const Rec*> out;
    for (const auto& r : recs) {
        if (r.section == section && r.type() == type) out.push_back(&r);
    }
    return out;
}

// A record's groups before its XDATA
std::vector<Group> body_of(const Rec& r) {
    std::vector<Group> out;
    for (const auto& g : r.groups) {
        if (g.first >= 1000) break;
        out.push_back(g);
    }
    return out;
}

std::vector<Group> xdata_of(const Rec& r) {
    std::vector<Group> out;
    bool in = false;
    for (const auto& g : r.groups) {
        if (g.first >= 1000) in = true;
        if (in) out.push_back(g);
    }
    return out;
}

bool has_code(const std::vector<Group>& groups, int code) {
    return std::any_of(groups.begin(), groups.end(), [&](const Group& g) { return g.first == code; });
}

std::string value_of(const std::vector<Group>& groups, int code, size_t nth = 0) {
    for (const auto& g : groups) {
        if (g.first == code && nth-- == 0) return g.second;
    }
    return "<none>";
}

std::vector<std::string> markers_of(const std::vector<Group>& groups) {
    std::vector<std::string> out;
    for (const auto& g : groups) {
        if (g.first == 100) out.push_back(g.second);
    }
    return out;
}

std::string dump(const std::vector<Group>& groups) {
    std::string s;
    for (const auto& g : groups) s += std::to_string(g.first) + "=" + g.second + " ";
    return s;
}

bool numbers_equal(const std::string& a, const std::string& b) {
    char* end_a = nullptr;
    char* end_b = nullptr;
    double x = std::strtod(a.c_str(), &end_a);
    double y = std::strtod(b.c_str(), &end_b);
    if (a.empty() || b.empty() || *end_a != '\0' || *end_b != '\0') return a == b;
    return std::fabs(x - y) <= 1e-9 * std::max(1.0, std::fabs(y));
}

// The same groups, numbers by value
void assert_same_groups(const std::vector<Group>& a, const std::vector<Group>& b, const std::string& what) {
    hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + ": the number of groups");
    for (size_t i = 0; i < a.size(); ++i) {
        hdxf_test::assert_true(a[i].first == b[i].first && numbers_equal(a[i].second, b[i].second),
                               what + ": group " + std::to_string(i) + " " + std::to_string(a[i].first) + "=" + a[i].second +
                                   " against " + std::to_string(b[i].first) + "=" + b[i].second);
    }
}

hdxf::Drawing load_memory(const std::vector<uint8_t>& bytes, const std::string& what) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), what + ": " + dwg.last_error());
    return dwg;
}

// One dimension of each type (the geometry of the dimension tests of R.7)
hdxf::DimensionData dimension_of(hdxf::DimensionType type) {
    hdxf::DimensionData d;
    d.dim_type = type;
    switch (type) {
        case hdxf::DimensionType::Rotated:
            d.def_point2 = {0, 0, 0};
            d.def_point3 = {50, 0, 0};
            d.definition_point = {50, 10, 0};
            d.text_midpoint = {25, 10, 0};
            break;
        case hdxf::DimensionType::Aligned:
            d.def_point2 = {0, 0, 0};
            d.def_point3 = {40, 30, 0};
            d.definition_point = {34, 38, 0};
            d.text_midpoint = {17, 19, 0};
            break;
        case hdxf::DimensionType::Angular:
            d.def_point2 = {0, 0, 0};
            d.def_point3 = {40, 0, 0};
            d.def_point4 = {0, 0, 0};
            d.definition_point = {0, 40, 0};
            d.arc_point = {25, 25, 0};
            d.text_midpoint = {20, 20, 0};
            break;
        case hdxf::DimensionType::Angular3Pt:
            d.def_point2 = {30, 0, 0};
            d.def_point3 = {0, 30, 0};
            d.def_point4 = {0, 0, 0};
            d.definition_point = {20, 20, 0};
            d.text_midpoint = {18, 18, 0};
            break;
        case hdxf::DimensionType::Diameter:
            d.definition_point = {-20, 0, 0};
            d.def_point4 = {20, 0, 0};
            d.text_midpoint = {0, 0, 0};
            break;
        case hdxf::DimensionType::Radius:
            d.definition_point = {0, 0, 0};
            d.def_point4 = {21.21320343559643, 21.21320343559643, 0};
            d.text_midpoint = {10, 10, 0};
            break;
        case hdxf::DimensionType::Ordinate:
            d.definition_point = {0, 0, 0};
            d.def_point2 = {10, 5, 0};
            d.def_point3 = {16, 9, 0};
            d.text_midpoint = {18, 9, 0};
            d.flags = 64;                   // X-type
            break;
    }
    return d;
}

const hdxf::DimensionType all_types[] = {
    hdxf::DimensionType::Rotated,   hdxf::DimensionType::Aligned,  hdxf::DimensionType::Angular,
    hdxf::DimensionType::Angular3Pt, hdxf::DimensionType::Diameter, hdxf::DimensionType::Radius,
    hdxf::DimensionType::Ordinate};

std::string type_name(hdxf::DimensionType t) {
    switch (t) {
        case hdxf::DimensionType::Rotated: return "rotated";
        case hdxf::DimensionType::Aligned: return "aligned";
        case hdxf::DimensionType::Angular: return "angular";
        case hdxf::DimensionType::Angular3Pt: return "angular 3-point";
        case hdxf::DimensionType::Diameter: return "diameter";
        case hdxf::DimensionType::Radius: return "radius";
        case hdxf::DimensionType::Ordinate: return "ordinate";
    }
    return "?";
}

// The subclass chain after AcDbDimension (R13/R14 references, BL15-1)
std::vector<std::string> subclasses_of(hdxf::DimensionType t) {
    switch (t) {
        case hdxf::DimensionType::Rotated: return {"AcDbAlignedDimension", "AcDbRotatedDimension"};
        case hdxf::DimensionType::Aligned: return {"AcDbAlignedDimension"};
        // Type 2: AutoCAD's core reads it only with AcDb2LineAngularDimension (7.24)
        case hdxf::DimensionType::Angular: return {"AcDb2LineAngularDimension"};
        case hdxf::DimensionType::Angular3Pt: return {"AcDb3PointAngularDimension"};
        case hdxf::DimensionType::Diameter: return {"AcDbDiametricDimension"};
        case hdxf::DimensionType::Radius: return {"AcDbRadialDimension"};
        case hdxf::DimensionType::Ordinate: return {"AcDbOrdinateDimension"};
    }
    return {};
}

hdxf::Entity& add_dimension(hdxf::Drawing& dwg, const hdxf::DimensionData& d, const std::string& layer = "0") {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Dimension;
    e.props.layer = layer;
    e.data = d;
    return dwg.add_entity(std::move(e));
}

const hdxf::MTextData* mtext_of(const hdxf::Block& b) {
    for (const auto& e : b.entities) {
        if (e.type == hdxf::EntityType::MText) return &e.as_mtext();
    }
    return nullptr;
}

int count_type(const hdxf::Block& b, hdxf::EntityType type) {
    return static_cast<int>(std::count_if(b.entities.begin(), b.entities.end(),
                                          [&](const hdxf::Entity& e) { return e.type == type; }));
}

bool near_point(const hdxf::Point3& a, const hdxf::Point3& b, double tolerance = 1e-6) {
    return std::fabs(a.x - b.x) <= tolerance && std::fabs(a.y - b.y) <= tolerance && std::fabs(a.z - b.z) <= tolerance;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

// ---------------------------------------------------------------------------
// DIMENSION
// ---------------------------------------------------------------------------

// Every type, with and without a block of the drawing's own, ASCII and binary: the groups of AutoCAD's
// R14 records and of the references — AcDbEntity, AcDbDimension (2, 10, 11, 70 with bit 32, 3), the type's
// subclasses; none of 2000's 71, 42, 73–75, 280 or lineweight/owner-less groups; ASCII and binary the same
TEST(dimension_r13_r14_every_type_groups) {
    FontPaths keep;
    hdxf::set_font_paths({});
    for (DxfVersion version : older_versions) {
        for (bool own_block : {false, true}) {
            hdxf::Drawing dwg(version);
            for (auto type : all_types) {
                hdxf::DimensionData d = dimension_of(type);
                if (own_block) d.block_name = "OWNBLOCK";
                add_dimension(dwg, d);
            }
            if (own_block) {
                hdxf::Block& b = dwg.add_block("OWNBLOCK");
                b.entities.push_back(hdxf::Entity{});
                b.entities.back().type = hdxf::EntityType::Line;
                b.entities.back().data = hdxf::LineData{{0, 0, 0}, {1, 1, 0}};
            }
            std::string what = name_of(version) + (own_block ? " with a block" : " without");
            std::vector<std::vector<Group>> per_format;
            for (DxfFormat format : both_formats) {
                std::string where = what + " " + name_of(format);
                std::vector<uint8_t> out = save(dwg, format, where);
                std::vector<Group> groups = groups_of(out);
                per_format.push_back(groups);
                std::vector<Rec> recs = records_of(groups);
                auto dims = of_type(recs, "ENTITIES", "DIMENSION");
                hdxf_test::assert_eq(static_cast<int>(dims.size()), 7, where + ": dimensions");
                size_t n = 0;
                std::set<std::string> blocks;
                for (const Rec* b : of_type(recs, "BLOCKS", "BLOCK")) blocks.insert(value_of(b->groups, 2));
                for (auto type : all_types) {
                    std::vector<Group> g = body_of(*dims[n++]);
                    std::string label = where + " " + type_name(type) + ": " + dump(g);
                    for (int code : {71, 42, 280, 73, 74, 75, 72, 41, 370, 380, 390, 410, 440}) {
                        hdxf_test::assert_false(has_code(g, code), label + " (has " + std::to_string(code) + ")");
                    }
                    std::vector<std::string> expected = {"AcDbEntity", "AcDbDimension"};
                    for (const auto& s : subclasses_of(type)) expected.push_back(s);
                    hdxf_test::assert_true(markers_of(g) == expected, label + " (subclass chain)");
                    int flags = std::atoi(value_of(g, 70).c_str());
                    hdxf_test::assert_true((flags & 32) != 0, label + " (bit 32 of 70)");
                    int wanted = static_cast<int>(type);
                    hdxf_test::assert_eq(flags & 15, wanted, label + " (type bits of 70)");
                    hdxf_test::assert_eq(value_of(g, 3), std::string("STANDARD"), label + " (3)");
                    std::string block = value_of(g, 2);
                    hdxf_test::assert_true(blocks.count(block) == 1, label + " (the block " + block + " is in BLOCKS)");
                    if (own_block) hdxf_test::assert_eq(block, std::string("OWNBLOCK"), label + " (the drawing's block)");
                    else hdxf_test::assert_true(block.rfind("*D", 0) == 0, label + " (a built block)");
                    // 2, 10, 11 come in this order, 70 after them, 3 last of the common part
                    auto at = [&](int code) {
                        for (size_t i = 0; i < g.size(); ++i) if (g[i].first == code) return static_cast<int>(i);
                        return -1;
                    };
                    hdxf_test::assert_true(at(2) < at(10) && at(10) < at(11) && at(11) < at(70) && at(70) < at(3), label + " (order)");
                    switch (type) {
                        case hdxf::DimensionType::Rotated:
                        case hdxf::DimensionType::Aligned:
                            hdxf_test::assert_true(has_code(g, 13) && has_code(g, 14), label + " (13, 14)");
                            break;
                        case hdxf::DimensionType::Angular:
                            hdxf_test::assert_true(has_code(g, 13) && has_code(g, 14) && has_code(g, 15) && has_code(g, 16),
                                                   label + " (13 14 15 16)");
                            break;
                        case hdxf::DimensionType::Angular3Pt:
                            hdxf_test::assert_true(has_code(g, 13) && has_code(g, 14) && has_code(g, 15) && !has_code(g, 16),
                                                   label + " (13 14 15)");
                            break;
                        case hdxf::DimensionType::Diameter:
                        case hdxf::DimensionType::Radius:
                            hdxf_test::assert_true(has_code(g, 15) && has_code(g, 40), label + " (15, 40)");
                            break;
                        case hdxf::DimensionType::Ordinate:
                            hdxf_test::assert_true(has_code(g, 13) && has_code(g, 14) && (flags & 64) != 0, label + " (13, 14, X-type)");
                            break;
                    }
                }
                // A drawing built here has handles and the structure of an R13/R14 file: unique handles
                hdxf::Drawing back = load_memory(out, where);
                hdxf_test::assert_true(back.version() == version, where + ": version read back");
                int read = 0;
                for (const auto& e : back.entities()) {
                    if (e.type == hdxf::EntityType::Dimension) {
                        ++read;
                        hdxf_test::assert_true(back.find_block(e.as_dimension().block_name) != nullptr,
                                               where + ": block " + e.as_dimension().block_name + " read back");
                    }
                }
                hdxf_test::assert_eq(read, 7, where + ": dimensions read back");
            }
            assert_same_groups(per_format[0], per_format[1], what + ": ASCII and binary");
        }
    }
}

// The clone point (12) is in the common part, before 70, as AutoCAD's own R14 record has it (BL15-1, `153`);
// the text override and the rotation, and an oblique angle, keep their groups
TEST(dimension_r13_r14_clone_point_and_optional_groups) {
    FontPaths keep;
    hdxf::set_font_paths({});
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg(version);
        hdxf::DimensionData d = dimension_of(hdxf::DimensionType::Rotated);
        d.block_translation = {120, 0, 0};
        d.text_override = "%%c<>.00/h8\\PINPUT";
        d.rotation = 90.0;
        d.def_point2 = {0, 0, 0};
        d.def_point3 = {0, 50, 0};
        d.definition_point = {10, 50, 0};
        d.oblique_angle = 15.0;
        d.horizontal_dir = 5.0;
        d.text_rotation = 3.0;
        add_dimension(dwg, d);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, name_of(version));
        std::vector<Rec> recs = records_of(groups_of(out));
        auto dims = of_type(recs, "ENTITIES", "DIMENSION");
        hdxf_test::assert_eq(static_cast<int>(dims.size()), 1, "one dimension");
        std::vector<Group> g = body_of(*dims[0]);
        auto at = [&](int code) {
            for (size_t i = 0; i < g.size(); ++i) if (g[i].first == code) return static_cast<int>(i);
            return -1;
        };
        std::string label = name_of(version) + ": " + dump(g);
        hdxf_test::assert_true(at(11) < at(12) && at(12) < at(70), label + " (12 between 11 and 70)");
        hdxf_test::assert_true(numbers_equal(value_of(g, 12), "120"), label);
        hdxf_test::assert_eq(value_of(g, 1), std::string("%%c<>.00/h8\\PINPUT"), label + " (1)");
        hdxf_test::assert_true(numbers_equal(value_of(g, 50), "90"), label + " (50)");
        hdxf_test::assert_true(numbers_equal(value_of(g, 52), "15"), label + " (52)");
        hdxf_test::assert_true(numbers_equal(value_of(g, 51), "5"), label + " (51)");
        hdxf_test::assert_true(numbers_equal(value_of(g, 53), "3"), label + " (53)");
        // 1, 53, 51 after 70 and before 3 (the references' order); 50 and 52 in the aligned subclass
        hdxf_test::assert_true(at(70) < at(1) && at(1) < at(53) && at(53) < at(51) && at(51) < at(3), label + " (order)");
        hdxf_test::assert_true(at(3) < at(13) && at(13) < at(14) && at(14) < at(50) && at(50) < at(52), label + " (subclass order)");
        // Read back: the typed dimension
        hdxf::Drawing back = load_memory(out, label);
        const hdxf::DimensionData* r = nullptr;
        for (const auto& e : back.entities()) if (e.type == hdxf::EntityType::Dimension) r = &e.as_dimension();
        hdxf_test::assert_true(r != nullptr, label);
        hdxf_test::assert_true(near_point(r->block_translation, {120, 0, 0}), label + " (block translation read back)");
        hdxf_test::assert_eq(r->text_override, d.text_override, label);
        hdxf_test::assert_true(std::fabs(r->rotation - 90.0) < 1e-9 && std::fabs(r->oblique_angle - 15.0) < 1e-9, label);
    }
}

// AutoCAD's own records: the DIMENSIONs of AutoCAD saving R12 (ACadSharp's sample_AC1009: all the types) and
// of AutoCAD saving R14 (BL15-1, Zurn_59251, 7H12R10F) each built from their typed data in a new R13 and R14
// drawing: the same groups, in the same order, in the groups HDxf takes from the model (10 and 11 are built
// with the block)
TEST(dimension_r13_r14_against_autocad_records) {
    FontPaths keep;
    hdxf::set_font_paths({});
    struct Source {
        std::string path;
        bool local;
    };
    const Source sources[] = {{"acadsharp/sample_AC1009_ascii.dxf", false},
                              {"local-r13r14/BL15-1_davidsmarx_r14_dimension_hatch.dxf", true},
                              {"local-r13r14/Zurn_59251_r14_acad2000family.dxf", true},
                              {"local-r13r14/7H12R10F_schneider_r14_dimension_spline.dxf", true}};
    int compared = 0;
    for (const auto& source : sources) {
        std::filesystem::path path = external(source.path);
        if (!std::filesystem::exists(path)) {
            if (!source.local) hdxf_test::skip("external test file missing: " + source.path);
            continue;
        }
        hdxf::Drawing autocad;
        hdxf_test::assert_true(autocad.load(path), source.path + ": " + autocad.last_error());
        // The records as AutoCAD wrote them
        std::vector<uint8_t> file;
        {
            std::ifstream in(path, std::ios::binary);
            file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        std::vector<Rec> theirs = records_of(groups_of(file));
        auto their_dims = of_type(theirs, "ENTITIES", "DIMENSION");
        for (DxfVersion version : older_versions) {
            hdxf::Drawing dwg(version);
            for (const auto& e : autocad.entities()) {
                if (e.type != hdxf::EntityType::Dimension) continue;
                hdxf::Entity copy;
                copy.type = hdxf::EntityType::Dimension;
                copy.props.layer = e.props.layer;
                copy.props.extrusion = e.props.extrusion;
                copy.props.xdata = e.props.xdata;
                hdxf::DimensionData d = e.as_dimension();
                d.block_name.clear();
                copy.data = d;
                dwg.add_entity(std::move(copy));
            }
            std::string what = source.path + " as " + name_of(version);
            std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
            std::vector<Rec> mine = records_of(groups_of(out));
            auto my_dims = of_type(mine, "ENTITIES", "DIMENSION");
            hdxf_test::assert_eq(static_cast<int>(my_dims.size()), static_cast<int>(their_dims.size()), what + ": dimensions");
            for (size_t i = 0; i < my_dims.size(); ++i) {
                std::vector<Group> a = body_of(*my_dims[i]);
                std::vector<Group> b = body_of(*their_dims[i]);
                // AutoCAD's R12 form has no subclass markers, no 330 and no style for Standard: compare what
                // both have, by value, in order
                auto strip = [&](const std::vector<Group>& g) {
                    std::vector<Group> out;
                    for (const auto& x : g) {
                        if (x.first == 5 || x.first == 330 || x.first == 100 || x.first == 2 || x.first == 8 || x.first == 62 ||
                            x.first == 6 || x.first == 48 || x.first == 60 || x.first == 370 || x.first == 67 || x.first == 10 ||
                            x.first == 11 || x.first == 20 || x.first == 21 || x.first == 30 || x.first == 31 || x.first == 71 ||
                            x.first == 42 || x.first == 3) {
                            continue;
                        }
                        out.push_back(x);
                    }
                    return out;
                };
                std::vector<Group> sa = strip(a), sb = strip(b);
                // bit 32 of 70: always in R13/R14 (the R12 file has none)
                auto flags_of = [](const std::vector<Group>& g) { return std::atoi(value_of(g, 70).c_str()); };
                hdxf_test::assert_eq(flags_of(a) & ~32, flags_of(b) & ~32, what + ": dimension " + std::to_string(i) + " 70: " + dump(a) + " against " + dump(b));
                hdxf_test::assert_true((flags_of(a) & 32) != 0, what + ": bit 32");
                auto drop70 = [](std::vector<Group> g) {
                    g.erase(std::remove_if(g.begin(), g.end(), [](const Group& x) { return x.first == 70; }), g.end());
                    return g;
                };
                assert_same_groups(drop70(sa), drop70(sb), what + ": dimension " + std::to_string(i) + ": " + dump(a) + " against " + dump(b));
                ++compared;
            }
        }
    }
    hdxf_test::assert_true(compared > 0, "some dimension compared");
}

// The measurement and the text of each type as the block shows it (42 is 2000's: R13 and R14 have it only in the
// block's text)
TEST(dimension_r13_r14_block_text_and_measurement) {
    FontPaths keep;
    hdxf::set_font_paths({});
    struct Case {
        hdxf::DimensionType type;
        std::string prefix;
        std::string number;
    };
    const Case cases[] = {{hdxf::DimensionType::Rotated, "", "50"},
                          {hdxf::DimensionType::Aligned, "", "50"},
                          {hdxf::DimensionType::Radius, "R", "30"},
                          {hdxf::DimensionType::Ordinate, "", "10"}};
    for (DxfVersion version : older_versions) {
        for (const auto& c : cases) {
            hdxf::Drawing dwg(version);
            hdxf::DimStyle& s = dwg.add_dim_style("Plain");
            s.dimdec = 2;
            hdxf::DimensionData d = dimension_of(c.type);
            d.dim_style = "Plain";
            add_dimension(dwg, d);
            std::string what = name_of(version) + " " + type_name(c.type);
            std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
            hdxf::Drawing back = load_memory(out, what);
            const hdxf::Block* b = nullptr;
            for (const auto& e : back.entities()) {
                if (e.type == hdxf::EntityType::Dimension) b = back.find_block(e.as_dimension().block_name);
            }
            hdxf_test::assert_true(b != nullptr, what + ": block");
            const hdxf::MTextData* text = mtext_of(*b);
            hdxf_test::assert_true(text != nullptr, what + ": the text is an MTEXT");
            std::string content = text->content;
            size_t semicolon = content.rfind(';');
            if (content.rfind("\\A", 0) == 0 && semicolon != std::string::npos) content = content.substr(semicolon + 1);
            hdxf_test::assert_eq(content, c.prefix + c.number + ".00", what + ": the measurement as text");
            // The measurement as the model computes it
            double measured = hdxf::internal::dimension_measurement(d, {0, 0, 1});
            hdxf_test::assert_true(std::fabs(measured - std::atof(c.number.c_str())) < 1e-6, what + ": measurement");
        }
    }
}

// The block built as AutoCAD builds it: extension lines, dimension line, arrowheads (SOLID), the text (MTEXT),
// the definition points (POINT on Defpoints); on layer 0 and ByBlock in linetype as AutoCAD's own R14 blocks
// (BL15-1, `*D0`), no lineweight; and the same entities as the 2000 builder makes of the same dimension
TEST(dimension_r13_r14_block_content) {
    FontPaths keep;
    hdxf::set_font_paths({});
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg(version);
        add_dimension(dwg, dimension_of(hdxf::DimensionType::Rotated), "DIMS");
        std::string what = name_of(version);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
        hdxf::Drawing back = load_memory(out, what);
        const hdxf::Block* b = nullptr;
        for (const auto& e : back.entities()) {
            if (e.type == hdxf::EntityType::Dimension) b = back.find_block(e.as_dimension().block_name);
        }
        hdxf_test::assert_true(b != nullptr, what + ": block");
        hdxf_test::assert_true(b->name.rfind("*D", 0) == 0 && (b->flags & 1) != 0, what + ": an anonymous *D block");
        hdxf_test::assert_eq(count_type(*b, hdxf::EntityType::Solid), 2, what + ": two arrowheads");
        hdxf_test::assert_eq(count_type(*b, hdxf::EntityType::MText), 1, what + ": the text");
        hdxf_test::assert_eq(count_type(*b, hdxf::EntityType::Point), 3, what + ": three definition points");
        hdxf_test::assert_true(count_type(*b, hdxf::EntityType::Line) >= 3, what + ": extension lines and dimension line");
        for (const auto& e : b->entities) {
            if (e.type == hdxf::EntityType::Point) {
                hdxf_test::assert_eq(lower(e.props.layer), std::string("defpoints"), what + ": the points are on Defpoints");
            } else {
                hdxf_test::assert_eq(e.props.layer, std::string("0"), what + ": layer 0");
            }
            hdxf_test::assert_eq(lower(e.props.linetype), std::string("byblock"), what + ": ByBlock");
            hdxf_test::assert_eq(e.props.lineweight, -1, what + ": no lineweight");
        }
        // The definition points: 13, 14 and 10 in AutoCAD's order
        std::vector<hdxf::Point3> points;
        for (const auto& e : b->entities) if (e.type == hdxf::EntityType::Point) points.push_back(e.as_point().location);
        hdxf_test::assert_true(near_point(points[0], {0, 0, 0}) && near_point(points[1], {50, 0, 0}) && near_point(points[2], {50, 10, 0}),
                               what + ": defpoints 13, 14, 10");
        // The Defpoints layer exists
        bool defpoints = false;
        for (const auto& l : back.layers()) defpoints = defpoints || lower(l.name) == "defpoints";
        hdxf_test::assert_true(defpoints, what + ": layer Defpoints");
        // The same entities as 2000's block: type by type, the points, the arrowheads and the lines alike
        hdxf::Drawing r2000(DxfVersion::R2000);
        add_dimension(r2000, dimension_of(hdxf::DimensionType::Rotated), "DIMS");
        std::vector<uint8_t> out2000 = r2000.save_to_memory();
        hdxf_test::assert_false(out2000.empty(), r2000.last_error());
        hdxf::Drawing back2000 = load_memory(out2000, "2000");
        const hdxf::Block* b2000 = nullptr;
        for (const auto& e : back2000.entities()) {
            if (e.type == hdxf::EntityType::Dimension) b2000 = back2000.find_block(e.as_dimension().block_name);
        }
        hdxf_test::assert_true(b2000 != nullptr, "2000's block");
        hdxf_test::assert_eq(static_cast<int>(b->entities.size()), static_cast<int>(b2000->entities.size()), what + ": entities as 2000's");
        for (size_t i = 0; i < b->entities.size(); ++i) {
            hdxf_test::assert_true(b->entities[i].type == b2000->entities[i].type, what + ": entity " + std::to_string(i) + " type as 2000's");
            if (b->entities[i].type == hdxf::EntityType::Line) {
                hdxf_test::assert_true(near_point(b->entities[i].as_line().start, b2000->entities[i].as_line().start) &&
                                           near_point(b->entities[i].as_line().end, b2000->entities[i].as_line().end),
                                       what + ": line " + std::to_string(i) + " as 2000's");
            }
            if (b->entities[i].type == hdxf::EntityType::Solid) {
                hdxf_test::assert_true(near_point(b->entities[i].as_solid().p2, b2000->entities[i].as_solid().p2),
                                       what + ": arrow tip " + std::to_string(i) + " as 2000's");
            }
        }
    }
}

// AutoCAD's own R14 blocks (BL15-1) against the ones built here for the same dimensions: the kinds of entity
// of each block, the definition points and the arrowheads' tips
TEST(dimension_r13_r14_block_against_autocad_r14) {
    FontPaths keep;
    hdxf::set_font_paths({});
    std::filesystem::path path = external("local-r13r14/BL15-1_davidsmarx_r14_dimension_hatch.dxf");
    if (!std::filesystem::exists(path)) hdxf_test::skip("local test file missing: BL15-1");
    hdxf::Drawing autocad;
    hdxf_test::assert_true(autocad.load(path), autocad.last_error());
    hdxf::Drawing dwg(DxfVersion::R14);
    // Their dimension styles (METRIC: its DIMFIT moves the text beside the dimension line)
    for (const auto& ds : autocad.dim_styles()) dwg.dim_styles().push_back(ds);
    std::vector<std::string> their_blocks;
    for (const auto& e : autocad.entities()) {
        if (e.type != hdxf::EntityType::Dimension) continue;
        if (!e.props.xdata.empty()) continue;           // the styles' overrides change the form
        hdxf::Entity copy;
        copy.type = hdxf::EntityType::Dimension;
        copy.props.layer = e.props.layer;
        hdxf::DimensionData d = e.as_dimension();
        their_blocks.push_back(d.block_name);
        d.block_name.clear();
        copy.data = d;
        dwg.add_entity(std::move(copy));
    }
    hdxf_test::assert_true(!their_blocks.empty(), "dimensions without overrides");
    std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, "BL15-1's dimensions");
    hdxf::Drawing back = load_memory(out, "written");
    size_t n = 0;
    int compared = 0;
    for (const auto& e : back.entities()) {
        if (e.type != hdxf::EntityType::Dimension) continue;
        const hdxf::Block* mine = back.find_block(e.as_dimension().block_name);
        const hdxf::Block* theirs = autocad.find_block(their_blocks[n++]);
        hdxf_test::assert_true(mine != nullptr && theirs != nullptr, "both blocks");
        std::string what = "dimension " + std::to_string(n - 1) + " (" + theirs->name + ")";
        // The definition points are the same, in the same order
        auto points = [](const hdxf::Block& b) {
            std::vector<hdxf::Point3> p;
            for (const auto& x : b.entities) if (x.type == hdxf::EntityType::Point) p.push_back(x.as_point().location);
            return p;
        };
        std::vector<hdxf::Point3> pa = points(*mine), pb = points(*theirs);
        hdxf_test::assert_eq(static_cast<int>(pa.size()), static_cast<int>(pb.size()), what + ": the definition points");
        for (size_t i = 0; i < pa.size(); ++i) {
            hdxf_test::assert_true(near_point(pa[i], pb[i], 1e-4),
                                   what + ": definition point " + std::to_string(i) + " " + std::to_string(pa[i].x) + "," +
                                       std::to_string(pa[i].y) + " against AutoCAD's " + std::to_string(pb[i].x) + "," +
                                       std::to_string(pb[i].y));
        }
        // Their layer, color and linetype: AutoCAD's R14 blocks are layer 0, ByBlock
        for (const auto& x : mine->entities) {
            if (x.type == hdxf::EntityType::Point) continue;
            hdxf_test::assert_eq(x.props.layer, theirs->entities.front().props.layer, what + ": layer as AutoCAD's");
            hdxf_test::assert_eq(lower(x.props.linetype), lower(theirs->entities.front().props.linetype), what + ": linetype as AutoCAD's");
        }
        // The arrowheads (SOLID) and the text (MTEXT) are the same in number
        hdxf_test::assert_eq(count_type(*mine, hdxf::EntityType::Solid), count_type(*theirs, hdxf::EntityType::Solid), what + ": arrowheads");
        ++compared;
    }
    hdxf_test::assert_true(compared > 0, "some block compared");
}

// A changed dimension style and a style override change the block the way the style says (B91): arrow size, text height
// and decimals; DSTYLE override in the dimension's XDATA wins over the style
TEST(dimension_r13_r14_changed_style_and_override) {
    FontPaths keep;
    hdxf::set_font_paths({});
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg(version);
        hdxf::DimStyle& s = dwg.add_dim_style("Big");
        s.dimasz = 5.0;
        s.dimtxt = 4.0;
        s.dimdec = 2;
        s.dimtad = 1;
        hdxf::DimensionData d = dimension_of(hdxf::DimensionType::Rotated);
        d.dim_style = "Big";
        add_dimension(dwg, d);
        hdxf::DimensionData d2 = d;
        hdxf::Entity& e2 = add_dimension(dwg, d2);
        // The override: DIMDEC (271) 1
        e2.props.xdata.push_back({"ACAD", {{1000, "DSTYLE"}, {1002, "{"}, {1070, "271"}, {1070, "1"}, {1002, "}"}}});
        std::string what = name_of(version);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
        hdxf::Drawing back = load_memory(out, what);
        std::vector<const hdxf::Block*> blocks;
        std::vector<const hdxf::Entity*> dims;
        for (const auto& e : back.entities()) {
            if (e.type != hdxf::EntityType::Dimension) continue;
            blocks.push_back(back.find_block(e.as_dimension().block_name));
            dims.push_back(&e);
        }
        hdxf_test::assert_eq(static_cast<int>(blocks.size()), 2, what + ": two dimensions");
        const hdxf::MTextData* t1 = mtext_of(*blocks[0]);
        const hdxf::MTextData* t2 = mtext_of(*blocks[1]);
        hdxf_test::assert_true(t1 && t2, what + ": texts");
        hdxf_test::assert_true(std::fabs(t1->height - 4.0) < 1e-9, what + ": DIMTXT 4");
        hdxf_test::assert_true(t1->content.find("50.00") != std::string::npos, what + ": two decimals: " + t1->content);
        hdxf_test::assert_true(t2->content.find("50.0") != std::string::npos && t2->content.find("50.00") == std::string::npos,
                               what + ": the override's one decimal: " + t2->content);
        // The arrowhead is DIMASZ long: the first SOLID's tip and base
        const hdxf::Entity* arrow = nullptr;
        for (const auto& e : blocks[0]->entities) if (e.type == hdxf::EntityType::Solid) { arrow = &e; break; }
        hdxf_test::assert_true(arrow != nullptr, what + ": an arrowhead");
        double length = std::hypot(arrow->as_solid().p2.x - arrow->as_solid().p0.x, arrow->as_solid().p2.y - arrow->as_solid().p0.y);
        hdxf_test::assert_true(length > 4.9 && length < 5.2, what + ": DIMASZ 5, the arrow is " + std::to_string(length));
        // The override is the dimension's XDATA, written as it was given
        std::vector<Rec> recs = records_of(groups_of(out));
        auto written = of_type(recs, "ENTITIES", "DIMENSION");
        hdxf_test::assert_eq(static_cast<int>(xdata_of(*written[0]).size()), 0, what + ": no XDATA of the first");
        hdxf_test::assert_true(dump(xdata_of(*written[1])).find("1000=DSTYLE") != std::string::npos, what + ": DSTYLE kept");
        hdxf_test::assert_true(dims.size() == 2, what);
        // The style's own record: DIMASZ, DIMTXT, DIMDEC, DIMTAD (41, 140, 271, 77) with 270 and 287 of R13/R14
        auto styles = of_type(recs, "TABLES", "DIMSTYLE");
        const Rec* big = nullptr;
        for (const Rec* r : styles) if (value_of(r->groups, 2) == "Big") big = r;
        hdxf_test::assert_true(big != nullptr, what + ": DIMSTYLE Big");
        hdxf_test::assert_true(numbers_equal(value_of(big->groups, 41), "5") && numbers_equal(value_of(big->groups, 140), "4") &&
                                   value_of(big->groups, 271) == "2" && value_of(big->groups, 77) == "1",
                               what + ": " + dump(big->groups));
        hdxf_test::assert_true(has_code(big->groups, 270) && has_code(big->groups, 287), what + ": DIMUNIT and DIMFIT");
    }
}

// Arrowheads are blocks of the file: the built-in ones as `_Dot` and so on (POLYLINE in R13, which has no LWPOLYLINE,
// LWPOLYLINE in R14), ByBlock, no lineweight; R13/R14's own DIMBLK names ("ARCHTICK") are the built-ins without
// their underscore, and DIMBLK names a block the drawing has stays that block
TEST(dimension_r13_r14_arrow_blocks) {
    FontPaths keep;
    hdxf::set_font_paths({});
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg(version);
        hdxf::DimStyle& dots = dwg.add_dim_style("Dots");
        dots.dimblk = "_Dot";
        hdxf::DimStyle& old = dwg.add_dim_style("Old");
        old.dimblk = "ARCHTICK";
        hdxf::DimStyle& mine = dwg.add_dim_style("Mine");
        mine.dimblk = "OPEN";                   // the drawing's own block of that name
        dwg.add_block("OPEN").entities.push_back(hdxf::Entity{});
        dwg.find_block("OPEN")->entities.back().type = hdxf::EntityType::Line;
        dwg.find_block("OPEN")->entities.back().data = hdxf::LineData{{-1, 0, 0}, {0, 0, 0}};
        for (const char* style : {"Dots", "Old", "Mine"}) {
            hdxf::DimensionData d = dimension_of(hdxf::DimensionType::Rotated);
            d.dim_style = style;
            add_dimension(dwg, d);
        }
        std::string what = name_of(version);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
        hdxf::Drawing back = load_memory(out, what);
        const hdxf::Block* dot = back.find_block("_Dot");
        hdxf_test::assert_true(dot != nullptr && dot->entities.size() == 2, what + ": the _Dot block");
        auto wide = version == DxfVersion::R13 ? hdxf::EntityType::Polyline : hdxf::EntityType::LwPolyline;
        hdxf_test::assert_true(dot->entities[0].type == wide, what + ": the dot's wide polyline form");
        for (const auto& e : dot->entities) {
            hdxf_test::assert_eq(lower(e.props.linetype), std::string("byblock"), what + ": ByBlock");
            hdxf_test::assert_eq(e.props.lineweight, -1, what + ": no lineweight");
        }
        hdxf_test::assert_true(back.find_block("_ArchTick") != nullptr, what + ": ARCHTICK is the _ArchTick block");
        std::vector<std::string> inserted;
        for (const auto& e : back.entities()) {
            if (e.type != hdxf::EntityType::Dimension) continue;
            const hdxf::Block* b = back.find_block(e.as_dimension().block_name);
            std::string names;
            for (const auto& x : b->entities) if (x.type == hdxf::EntityType::Insert) names += x.as_insert().block_name + " ";
            inserted.push_back(names);
        }
        hdxf_test::assert_eq(static_cast<int>(inserted.size()), 3, what);
        hdxf_test::assert_true(inserted[0].find("_Dot") != std::string::npos, what + ": _Dot inserted: " + inserted[0]);
        hdxf_test::assert_true(inserted[1].find("_ArchTick") != std::string::npos, what + ": _ArchTick inserted: " + inserted[1]);
        hdxf_test::assert_true(inserted[2].find("OPEN") != std::string::npos && back.find_block("_Open") == nullptr,
                               what + ": the drawing's OPEN stays: " + inserted[2]);
        // The DIMSTYLE records keep the names the styles have
        std::vector<Rec> recs = records_of(groups_of(out));
        std::string names;
        for (const Rec* r : of_type(recs, "TABLES", "DIMSTYLE")) names += value_of(r->groups, 2) + "=" + value_of(r->groups, 5) + " ";
        hdxf_test::assert_true(names.find("Old=ARCHTICK") != std::string::npos && names.find("Dots=_Dot") != std::string::npos, what + ": " + names);
    }
}

// What has no older form: ARC_DIMENSION and LARGE_RADIAL_DIMENSION, and a dimension style's newer variables
// (7.8); R13/R14 are refused before anything is written, the drawing keeps its version and handles
TEST(dimension_r13_r14_newer_things_are_errors) {
    for (DxfVersion version : older_versions) {
        for (const char* type : {"ARC_DIMENSION", "LARGE_RADIAL_DIMENSION"}) {
            hdxf::Drawing dwg(version);
            hdxf::Entity e;
            e.type = hdxf::EntityType::Unknown;
            hdxf::UnknownEntityData u;
            u.entity_type = type;
            e.data = u;
            dwg.add_entity(std::move(e));
            std::vector<uint8_t> out = dwg.save_to_memory();
            hdxf_test::assert_true(out.empty(), std::string(type) + " saved as " + name_of(version));
            hdxf_test::assert_eq(dwg.last_error(), std::string(type) + " requires DXF 2000 or later", name_of(version));
            hdxf_test::assert_true(dwg.version() == version, "version kept");
        }
        // A dimension style with a variable of 2000
        hdxf::Drawing dwg(version);
        hdxf::DimStyle& s = dwg.add_dim_style("Newer");
        s.dimtfill = 1;
        add_dimension(dwg, dimension_of(hdxf::DimensionType::Rotated));
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "DIMTFILL as " + name_of(version));
        hdxf_test::assert_true(dwg.last_error().find("requires DXF 2000 or later") != std::string::npos, dwg.last_error());
    }
}

// ---------------------------------------------------------------------------
// VIEWPORT and paper space
// ---------------------------------------------------------------------------

namespace {

// Paper space with the paper's own viewport (id 1) and a model viewport with frozen layers
hdxf::Drawing paper_drawing(DxfVersion version) {
    hdxf::Drawing dwg(version);
    dwg.add_layer("FROZEN1");
    dwg.add_layer("FROZEN2");
    dwg.add_layer("LIVE");
    // Model space
    dwg.add_line({0, 0, 0}, {100, 50, 0}, "LIVE");
    dwg.add_line({0, 50, 0}, {100, 0, 0}, "FROZEN1");
    // Paper space
    hdxf::Entity& page = dwg.add_viewport({148.5, 105, 0}, 297, 210);
    page.props.paper_space = 1;
    auto& p = page.as_viewport();
    p.id = 1;
    p.status = 1;
    hdxf::Entity& vp = dwg.add_viewport({150, 100, 0}, 100, 80);
    vp.props.paper_space = 1;
    auto& d = vp.as_viewport();
    d.id = 2;
    d.status = 2;
    d.view_target = {5, 6, 0};
    d.view_direction = {0, 0, 1};
    d.view_center = {50, 25};
    d.view_height = 120;
    d.twist_angle = 0.5;
    d.lens_length = 50;
    d.circle_zoom = 100;
    d.snap_spacing = {0.5, 0.5};
    d.grid_spacing = {2, 2};
    d.status_flags = 32768 | 32 | 128 | 512;        // UCS icon on, fast zoom, grid
    d.frozen_layers = {"FROZEN1", "FROZEN2"};
    hdxf::Entity& line = dwg.add_line({0, 0, 0}, {297, 210, 0});
    line.props.paper_space = 1;
    return dwg;
}

} // namespace

// VIEWPORT as AutoCAD's R14 file and the references have it: 100 AcDbEntity, 67 1, 8, 100 AcDbViewport, 10 40 41 68 69
// and then only the XDATA ACAD/MVIEW (none of 2000's 12 … 17, 42 … 51, 72, 90, 331 …); the view in the fixed order of the
// reference, the frozen layers as names; ASCII and binary the same; the view reads back into the typed fields
TEST(viewport_r13_r14_old_form_and_view) {
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg = paper_drawing(version);
        std::string what = name_of(version);
        std::vector<std::vector<Group>> per_format;
        for (DxfFormat format : both_formats) {
            std::string where = what + " " + name_of(format);
            std::vector<uint8_t> out = save(dwg, format, where);
            std::vector<Group> groups = groups_of(out);
            per_format.push_back(groups);
            std::vector<Rec> recs = records_of(groups);
            auto vps = of_type(recs, "ENTITIES", "VIEWPORT");
            hdxf_test::assert_eq(static_cast<int>(vps.size()), 2, where + ": viewports");
            for (const Rec* r : vps) {
                std::vector<Group> g = body_of(*r);
                std::string label = where + ": " + dump(r->groups);
                std::vector<std::string> expected = {"AcDbEntity", "AcDbViewport"};
                hdxf_test::assert_true(markers_of(g) == expected, label + " (markers)");
                hdxf_test::assert_eq(value_of(g, 67), std::string("1"), label + " (paper space)");
                for (int code : {12, 13, 14, 15, 16, 17, 42, 43, 44, 45, 50, 51, 72, 90, 331, 340, 281, 71, 74, 110, 146, 170, 61, 348, 370}) {
                    hdxf_test::assert_false(has_code(g, code), label + " (has " + std::to_string(code) + ")");
                }
                // 10 20 30, 40, 41, 68, 69 in this order after the marker
                std::vector<int> codes;
                for (const auto& x : g) codes.push_back(x.first);
                auto pos = [&](int code) { return static_cast<int>(std::find(codes.begin(), codes.end(), code) - codes.begin()); };
                hdxf_test::assert_true(pos(100) < pos(10) && pos(10) < pos(20) && pos(20) < pos(30) && pos(30) < pos(40) &&
                                           pos(40) < pos(41) && pos(41) < pos(68) && pos(68) < pos(69),
                                       label + " (order)");
                // The XDATA: the order of the reference
                std::vector<Group> x = xdata_of(*r);
                std::vector<int> xc;
                for (const auto& y : x) xc.push_back(y.first);
                std::vector<int> want = {1001, 1000, 1002, 1070, 1010, 1020, 1030, 1010, 1020, 1030, 1040, 1040, 1040, 1040, 1040, 1040, 1040,
                                         1070, 1070, 1070, 1070, 1070, 1070, 1070, 1070, 1040, 1040, 1040, 1040, 1040, 1040, 1040, 1070, 1002};
                std::vector<int> head(xc.begin(), xc.begin() + static_cast<long>(want.size()));
                hdxf_test::assert_true(head == want, label + " (MVIEW order)");
                hdxf_test::assert_eq(x[0].second, std::string("ACAD"), label);
                hdxf_test::assert_eq(x[1].second, std::string("MVIEW"), label);
                hdxf_test::assert_eq(x[3].second, std::string("16"), label + " (version 16)");
                hdxf_test::assert_true(x.back().first == 1002 && x.back().second == "}", label + " (closed)");
            }
            // The id and status as given, the paper space viewport 1
            std::vector<Group> first = body_of(*vps[0]);
            hdxf_test::assert_eq(value_of(first, 69), std::string("1"), where + ": the paper space viewport is 1");
            hdxf_test::assert_eq(value_of(first, 68), std::string("1"), where + ": its status");
            // The second: frozen layers by name inside the second pair of braces
            std::vector<Group> x = xdata_of(*vps[1]);
            std::vector<std::string> frozen;
            for (const auto& y : x) if (y.first == 1003) frozen.push_back(y.second);
            hdxf_test::assert_true(frozen == std::vector<std::string>({"FROZEN1", "FROZEN2"}), where + ": frozen layers " + dump(x));
            std::vector<std::string> mode;
            for (const auto& y : x) if (y.first == 1070) mode.push_back(y.second);
            // version, view mode, circle zoom, fast zoom, UCSICON, snap, grid, snap style, isopair, hidden
            hdxf_test::assert_true(mode == std::vector<std::string>({"16", "0", "100", "1", "1", "0", "1", "0", "0", "0"}), where + ": " + dump(x));
            std::vector<std::string> reals;
            for (const auto& y : x) if (y.first == 1040) reals.push_back(y.second);
            // twist, height, center x y, lens, front, back, snap angle, snap base x y, snap x y, grid x y
            const double want_reals[] = {0.5, 120, 50, 25, 50, 0, 0, 0, 0, 0, 0.5, 0.5, 2, 2};
            hdxf_test::assert_eq(static_cast<int>(reals.size()), 14, where + ": the view's reals");
            for (size_t i = 0; i < reals.size(); ++i) {
                hdxf_test::assert_true(numbers_equal(reals[i], std::to_string(want_reals[i])) || std::fabs(std::atof(reals[i].c_str()) - want_reals[i]) < 1e-9,
                                       where + ": real " + std::to_string(i) + " " + reals[i]);
            }
            // The view target and direction
            hdxf_test::assert_true(numbers_equal(value_of(x, 1010, 0), "5") && numbers_equal(value_of(x, 1020, 0), "6") &&
                                       numbers_equal(value_of(x, 1030, 1), "1"),
                                   where + ": target and direction");
            // Reads back: the view is the typed fields, the XDATA is gone from the entity
            hdxf::Drawing back = load_memory(out, where);
            int seen = 0;
            for (const auto& e : back.entities()) {
                if (e.type != hdxf::EntityType::Viewport) continue;
                const hdxf::ViewportData& v = e.as_viewport();
                for (const auto& xd : e.props.xdata) hdxf_test::assert_true(xd.app_name != "ACAD", where + ": the MVIEW is typed");
                if (v.id == 2) {
                    ++seen;
                    hdxf_test::assert_true(near_point(v.view_target, {5, 6, 0}) && near_point(v.view_direction, {0, 0, 1}), where + ": view");
                    hdxf_test::assert_true(std::fabs(v.view_height - 120) < 1e-9 && std::fabs(v.twist_angle - 0.5) < 1e-9 &&
                                               std::fabs(v.view_center.x - 50) < 1e-9 && std::fabs(v.view_center.y - 25) < 1e-9,
                                           where + ": height, twist, center");
                    hdxf_test::assert_eq(v.circle_zoom, 100, where + ": circle zoom");
                    hdxf_test::assert_true(v.frozen_layers == std::vector<std::string>({"FROZEN1", "FROZEN2"}), where + ": frozen layers read back");
                    hdxf_test::assert_true((v.status_flags & 128) && (v.status_flags & 512) && (v.status_flags & 32) && !(v.status_flags & 256),
                                           where + ": the on/off settings");
                    hdxf_test::assert_eq(v.status, 2, where + ": status");
                    hdxf_test::assert_true(std::fabs(v.width - 100) < 1e-9 && std::fabs(v.height - 80) < 1e-9, where + ": size");
                }
            }
            hdxf_test::assert_eq(seen, 1, where + ": viewport 2 read back");
        }
        assert_same_groups(per_format[0], per_format[1], what + ": ASCII and binary");
    }
}

// The view XDATA against AutoCAD's own: the VIEWPORT of AutoCAD saving R12 (ACadSharp's sample_AC1009) written as R13 and
// R14 has the same values group by group from 10 on (the R12 file has no 100 markers), and against puppenhaus_4mm
// (AutoCAD R14: two viewports, one at 68 -1) when the local copy is there
TEST(viewport_r13_r14_against_autocad_records) {
    std::filesystem::path path = external("acadsharp/sample_AC1009_ascii.dxf");
    if (!std::filesystem::exists(path)) hdxf_test::skip("external test file missing: sample_AC1009_ascii.dxf");
    std::vector<uint8_t> file;
    {
        std::ifstream in(path, std::ios::binary);
        file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<Rec> theirs = records_of(groups_of(file));
    auto their_vps = of_type(theirs, "ENTITIES", "VIEWPORT");
    hdxf_test::assert_eq(static_cast<int>(their_vps.size()), 2, "AutoCAD's viewports (the paper space view and a model viewport at 68 2, UCSICON 3)");
    auto from_ten = [](const Rec& r) {
        std::vector<Group> out;
        bool in = false;
        for (const auto& g : r.groups) {
            if (g.first == 10) in = true;
            if (in && g.first != 100) out.push_back(g);
        }
        return out;
    };
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(path), dwg.last_error());
        dwg.set_version(version);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, "sample_AC1009 as " + name_of(version));
        std::vector<Rec> mine = records_of(groups_of(out));
        auto my_vps = of_type(mine, "ENTITIES", "VIEWPORT");
        hdxf_test::assert_eq(static_cast<int>(my_vps.size()), 2, "the viewports");
        for (size_t i = 0; i < 2; ++i) assert_same_groups(from_ten(*my_vps[i]), from_ten(*their_vps[i]), "sample_AC1009 viewport " + std::to_string(i) + " as " + name_of(version));
        for (size_t i = 0; i < 2; ++i) hdxf_test::assert_eq(value_of(my_vps[i]->groups, 67), std::string("1"), "paper space");
    }
    // AutoCAD's R14 (puppenhaus_4mm): its two viewports, read as R14 and written as R14 from the typed view
    std::filesystem::path pup = external("local-r13r14/puppenhaus_4mm_r14_viewport_view.dxf");
    if (!std::filesystem::exists(pup)) return;
    std::vector<uint8_t> pup_file;
    {
        std::ifstream in(pup, std::ios::binary);
        pup_file.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<Rec> pup_recs = records_of(groups_of(pup_file));
    auto pup_vps = of_type(pup_recs, "ENTITIES", "VIEWPORT");
    hdxf_test::assert_eq(static_cast<int>(pup_vps.size()), 2, "puppenhaus' viewports");
    hdxf::Drawing autocad;
    hdxf_test::assert_true(autocad.load(pup), autocad.last_error());
    // Both viewports into a new R14 drawing (new records: the writer makes them from the typed fields)
    hdxf::Drawing dwg(DxfVersion::R14);
    for (const auto& e : autocad.entities()) {
        if (e.type != hdxf::EntityType::Viewport) continue;
        hdxf::Entity copy;
        copy.type = hdxf::EntityType::Viewport;
        copy.props.layer = e.props.layer;
        copy.props.paper_space = 1;
        copy.data = e.as_viewport();
        dwg.add_entity(std::move(copy));
    }
    std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, "puppenhaus' viewports");
    std::vector<Rec> mine = records_of(groups_of(out));
    auto my_vps = of_type(mine, "ENTITIES", "VIEWPORT");
    hdxf_test::assert_eq(static_cast<int>(my_vps.size()), 2, "the viewports");
    for (size_t i = 0; i < 2; ++i) {
        assert_same_groups(from_ten(*my_vps[i]), from_ten(*pup_vps[i]), "puppenhaus viewport " + std::to_string(i));
    }
}

// What a viewport has from 2000 and no older form is refused, naming 2000 (nothing is dropped without a word)
TEST(viewport_r13_r14_newer_things_are_errors) {
    struct Case {
        const char* what;
        std::function<void(hdxf::ViewportData&)> set;
    };
    const Case cases[] = {
        {"A viewport's clip boundary", [](hdxf::ViewportData& d) { d.clip_boundary_handle = "2F"; }},
        {"A viewport's plot style sheet", [](hdxf::ViewportData& d) { d.plot_style_sheet = "acad.ctb"; }},
        {"A viewport's render mode", [](hdxf::ViewportData& d) { d.render_mode = 3; }},
        {"A viewport's elevation", [](hdxf::ViewportData& d) { d.elevation = 2.0; }},
        {"A viewport's own UCS", [](hdxf::ViewportData& d) { d.ucs_origin = {1, 2, 3}; }},
    };
    for (DxfVersion version : older_versions) {
        for (const auto& c : cases) {
            hdxf::Drawing dwg = paper_drawing(version);
            for (auto& e : dwg.entities()) {
                if (e.type == hdxf::EntityType::Viewport) c.set(e.as_viewport());
            }
            std::vector<uint8_t> out = dwg.save_to_memory();
            hdxf_test::assert_true(out.empty(), std::string(c.what) + " as " + name_of(version));
            hdxf_test::assert_eq(dwg.last_error(), std::string(c.what) + " requires DXF 2000 or later", name_of(version));
        }
        // 2004's shade plot mode and 2007's lighting keep their own versions
        hdxf::Drawing dwg = paper_drawing(version);
        for (auto& e : dwg.entities()) {
            if (e.type == hdxf::EntityType::Viewport) e.as_viewport().shade_plot_mode = 2;
        }
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "shade plot mode");
        hdxf_test::assert_eq(dwg.last_error(), std::string("A viewport's shade plot mode requires DXF 2004 or later"), name_of(version));
    }
}

// Paper space: the entities are in ENTITIES with 67 1 and the *PAPER_SPACE block stays empty (R13:BLOCKS); R13 writes
// all the paper space entities first (the reference), R14 in the order the drawing has them (AutoCAD's R14 files:
// the viewports after the model); the model entities have no 67
TEST(paper_space_r13_r14_entities_and_blocks) {
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg = paper_drawing(version);
        std::string what = name_of(version);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
        std::vector<Rec> recs = records_of(groups_of(out));
        std::string order;
        for (const auto& r : recs) {
            if (r.section != "ENTITIES" || r.type() == "SECTION" || r.type() == "ENDSEC") continue;
            order += r.type() + (value_of(r.groups, 67) == "1" ? "*" : "") + " ";
        }
        if (version == DxfVersion::R13) {
            hdxf_test::assert_eq(order, std::string("VIEWPORT* VIEWPORT* LINE* LINE LINE "), what + ": paper space first");
        } else {
            hdxf_test::assert_eq(order, std::string("LINE LINE VIEWPORT* VIEWPORT* LINE* "), what + ": the drawing's order");
        }
        // The space blocks are empty, *PAPER_SPACE has 67 1 on BLOCK and ENDBLK
        bool in_paper = false;
        bool paper_flag = false;
        int in_blocks = 0;
        for (const auto& r : recs) {
            if (r.section != "BLOCKS") continue;
            if (r.type() == "BLOCK") {
                std::string name = value_of(r.groups, 2);
                in_paper = lower(name) == "*paper_space";
                if (in_paper) paper_flag = value_of(r.groups, 67) == "1";
                in_blocks = 0;
            } else if (r.type() == "ENDBLK") {
                if (in_paper) hdxf_test::assert_eq(in_blocks, 0, what + ": *PAPER_SPACE holds no entity");
                in_paper = false;
            } else if (in_paper) {
                ++in_blocks;
            }
        }
        hdxf_test::assert_true(paper_flag, what + ": 67 1 on the *PAPER_SPACE BLOCK");
        // No layout of 2000
        for (const auto& r : recs) {
            hdxf_test::assert_true(r.type() != "LAYOUT" && r.type() != "PLOTSETTINGS", what + ": no " + r.type());
        }
        hdxf::Drawing back = load_memory(out, what);
        int paper = 0;
        for (const auto& e : back.entities()) paper += e.props.paper_space ? 1 : 0;
        hdxf_test::assert_eq(paper, 3, what + ": paper space entities read back");
    }
}

// $EXTMIN/$EXTMAX of the model and $PEXTMIN/$PEXTMAX of the paper: the model's lines (and a dimension with its block),
// the paper's line and the viewport that is not the paper's own (id 1 is the view itself); the same numbers as 2000's
TEST(extents_r13_r14_model_and_paper_with_dimension) {
    FontPaths keep;
    hdxf::set_font_paths({});
    auto numbers = [](const std::vector<Group>& groups, const std::string& name) {
        std::vector<double> v;
        bool in = false;
        for (size_t i = 0; i + 1 < groups.size(); ++i) {
            if (groups[i].first == 9) in = groups[i].second == name;
            else if (in && groups[i].first >= 10 && groups[i].first <= 30) v.push_back(std::atof(groups[i].second.c_str()));
            else if (in && groups[i].first == 0) break;
        }
        return v;
    };
    std::vector<std::vector<double>> by_version[2];
    int n = 0;
    for (DxfVersion version : {DxfVersion::R13, DxfVersion::R14, DxfVersion::R2000}) {
        hdxf::Drawing dwg = paper_drawing(version);
        add_dimension(dwg, dimension_of(hdxf::DimensionType::Rotated), "LIVE");
        std::string what = version == DxfVersion::R2000 ? "2000" : name_of(version);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
        std::vector<Group> groups = groups_of(out);
        std::vector<double> emin = numbers(groups, "$EXTMIN"), emax = numbers(groups, "$EXTMAX");
        std::vector<double> pmin = numbers(groups, "$PEXTMIN"), pmax = numbers(groups, "$PEXTMAX");
        hdxf_test::assert_eq(static_cast<int>(emin.size()), 3, what + ": $EXTMIN");
        // The model: the two lines 0..100 by 0..50, the dimension at y 10 with its text above and its extension lines
        hdxf_test::assert_true(emin[0] <= 0.0 + 1e-9 && emin[1] <= 0.0 + 1e-9, what + ": EXTMIN covers the lines");
        hdxf_test::assert_true(emax[0] >= 100.0 - 1e-9 && emax[1] >= 50.0 - 1e-9, what + ": EXTMAX covers the lines");
        // The paper: the line 0,0 – 297,210 and the viewport 100 x 80 at 150,100: id 1 does not count
        hdxf_test::assert_true(std::fabs(pmin[0]) < 1e-9 && std::fabs(pmin[1]) < 1e-9, what + ": $PEXTMIN");
        hdxf_test::assert_true(std::fabs(pmax[0] - 297) < 1e-9 && std::fabs(pmax[1] - 210) < 1e-9, what + ": $PEXTMAX");
        if (n < 2) {
            by_version[n].push_back(emin);
            by_version[n].push_back(emax);
            by_version[n].push_back(pmin);
            by_version[n].push_back(pmax);
        } else {
            for (int k = 0; k < 2; ++k) {
                for (size_t i = 0; i < 4; ++i) {
                    for (size_t j = 0; j < 3; ++j) {
                        hdxf_test::assert_true(std::fabs(by_version[k][i][j] - (i == 0 ? emin : i == 1 ? emax : i == 2 ? pmin : pmax)[j]) < 1e-9,
                                               what + ": the extents are the same as " + name_of(k == 0 ? DxfVersion::R13 : DxfVersion::R14));
                    }
                }
            }
        }
        ++n;
    }
}

// A paper space viewport with a changed view survives a write and a read and a second write unchanged: the group list of
// the VIEWPORT is the same (the typed fields are the whole truth)
TEST(viewport_r13_r14_written_again_is_the_same) {
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg = paper_drawing(version);
        std::vector<uint8_t> first = save(dwg, DxfFormat::Ascii, name_of(version));
        hdxf::Drawing back = load_memory(first, name_of(version));
        // Change one thing in a viewport read from the file: it is written again from the typed fields
        for (auto& e : back.entities()) {
            if (e.type != hdxf::EntityType::Viewport) continue;
            auto& v = e.as_viewport();
            if (v.id == 2) {
                v.view_height = 240;
                v.frozen_layers.push_back("LIVE");
            }
        }
        std::vector<uint8_t> second = save(back, DxfFormat::Ascii, name_of(version) + " again");
        std::vector<Rec> recs = records_of(groups_of(second));
        auto vps = of_type(recs, "ENTITIES", "VIEWPORT");
        hdxf_test::assert_eq(static_cast<int>(vps.size()), 2, name_of(version) + ": viewports");
        std::vector<Group> x = xdata_of(*vps[1]);
        std::vector<std::string> frozen;
        for (const auto& y : x) if (y.first == 1003) frozen.push_back(y.second);
        hdxf_test::assert_true(frozen == std::vector<std::string>({"FROZEN1", "FROZEN2", "LIVE"}), name_of(version) + ": frozen layers: " + dump(x));
        hdxf_test::assert_true(numbers_equal(value_of(x, 1040, 1), "240"), name_of(version) + ": the view height is the changed one");
        // The first viewport is as it was
        std::vector<Rec> before = records_of(groups_of(first));
        auto vps_before = of_type(before, "ENTITIES", "VIEWPORT");
        std::vector<Group> a = xdata_of(*vps[0]), b = xdata_of(*vps_before[0]);
        assert_same_groups(a, b, name_of(version) + ": the unchanged viewport");
    }
}

// A baseline or continued dimension has a clone point (12): the block is where the dimension would be without the
// offset (AutoCAD's R14 block of such a dimension, BL15-1 `*D11`: the definition points less 12); the extents count
// the displayed place
TEST(dimension_r13_r14_clone_point_moves_the_block) {
    FontPaths keep;
    hdxf::set_font_paths({});
    for (DxfVersion version : older_versions) {
        hdxf::Drawing dwg(version);
        hdxf::DimensionData d = dimension_of(hdxf::DimensionType::Rotated);
        d.block_translation = {120, 0, 0};
        add_dimension(dwg, d);
        std::string what = version == DxfVersion::R2000 ? "2000" : name_of(version);
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii, what);
        hdxf::Drawing back = load_memory(out, what);
        const hdxf::Block* b = nullptr;
        for (const auto& e : back.entities()) {
            if (e.type == hdxf::EntityType::Dimension) b = back.find_block(e.as_dimension().block_name);
        }
        hdxf_test::assert_true(b != nullptr, what + ": block");
        std::vector<hdxf::Point3> points;
        for (const auto& e : b->entities) if (e.type == hdxf::EntityType::Point) points.push_back(e.as_point().location);
        hdxf_test::assert_eq(static_cast<int>(points.size()), 3, what + ": points");
        hdxf_test::assert_true(near_point(points[0], {-120, 0, 0}) && near_point(points[1], {-70, 0, 0}) && near_point(points[2], {-70, 10, 0}),
                               what + ": the block's definition points are the dimension's less 12");
        // The extents are of the displayed place: the block less 12 plus 12 is the dimension's own place
        std::vector<Group> groups = groups_of(out);
        double max_x = -1e30;
        bool in = false;
        for (const auto& g : groups) {
            if (g.first == 9) in = g.second == "$EXTMAX";
            else if (in && g.first == 10) { max_x = std::atof(g.second.c_str()); break; }
        }
        hdxf_test::assert_true(max_x >= 50.0 - 1e-9, what + ": $EXTMAX x is the displayed place: " + std::to_string(max_x));
    }
}
