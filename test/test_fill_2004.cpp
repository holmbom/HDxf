#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// MTEXT's background fill and HATCH's gradient fill (2004) — phase 2, step 2.4
// ---------------------------------------------------------------------------

namespace {

using Record = std::vector<std::pair<int, std::string>>;

std::vector<Record> records_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Record> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0 || out.empty()) out.emplace_back();
        out.back().push_back({gc.code, dxf_structure::number_text(gc.code, gc.value)});
    }
    return out;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

Record record(const std::vector<Record>& records, const std::string& handle) {
    for (const auto& r : records) {
        for (const auto& [code, value] : r) {
            if (code == 5 && value == handle) return r;
            if (code == 100) break;
        }
    }
    return {};
}

// The groups from the first `code` on (to the end of the record)
Record from_code(const Record& r, int code) {
    auto it = std::find_if(r.begin(), r.end(), [&](const auto& g) { return g.first == code; });
    return Record(it, r.end());
}

// The groups up to the first `code`
Record before_code(const Record& r, int code) {
    auto it = std::find_if(r.begin(), r.end(), [&](const auto& g) { return g.first == code; });
    return Record(r.begin(), it);
}

std::vector<int> codes(const Record& r) {
    std::vector<int> out;
    for (const auto& g : r) out.push_back(g.first);
    return out;
}

bool has_files(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (!std::filesystem::exists(external(n))) {
            hdxf_test::skip("external test file missing: " + n);
            return false;
        }
    }
    return true;
}

hdxf::Entity* entity(hdxf::Drawing& dwg, const std::string& handle) {
    for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    for (auto& b : dwg.blocks()) {
        for (auto& e : b.entities) if (e.props.handle == handle) return &e;
    }
    return nullptr;
}

const char* const kAscii = "acadsharp/sample_AC1018_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1018_binary.dxf";

const std::vector<int> kGradientCodes = {450, 451, 460, 461, 452, 462, 453, 463, 63, 421, 463, 63, 421, 470};

} // namespace

// What AutoCAD's 2004 file has: two MTEXT with a background, a linear and a spherical
// gradient — ASCII and binary alike
TEST(fill_2004_read) {
    if (!has_files({kAscii, kBinary})) return;
    for (const char* name : {kAscii, kBinary}) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(external(name)), dwg.last_error());
        const auto& bg = entity(dwg, "9DA")->as_mtext().background;
        hdxf_test::assert_eq(bg.flags, 1);
        hdxf_test::assert_eq(bg.color.aci, 97);
        hdxf_test::assert_eq(bg.color.rgb, 3568962);
        hdxf_test::assert_eq(bg.scale, 1.25);
        hdxf_test::assert_true(bg.transparency.is_by_layer(), "441 0");
        hdxf_test::assert_eq(entity(dwg, "CDC")->as_mtext().background.flags, 1);

        const auto& linear = entity(dwg, "371")->as_hatch();
        hdxf_test::assert_true(linear.gradient.has_value(), "371 gradient");
        hdxf_test::assert_eq(linear.gradient->name, "LINEAR");
        hdxf_test::assert_eq(linear.gradient->type, 1);
        hdxf_test::assert_false(linear.gradient->single_color, "two colors");
        hdxf_test::assert_eq(static_cast<int>(linear.gradient->colors.size()), 2);
        hdxf_test::assert_eq(linear.gradient->colors[0].value, 0.0);
        hdxf_test::assert_eq(linear.gradient->colors[0].color.aci, 5);
        hdxf_test::assert_eq(linear.gradient->colors[0].color.rgb, 255);
        hdxf_test::assert_eq(linear.gradient->colors[1].value, 1.0);
        hdxf_test::assert_eq(linear.gradient->colors[1].color.aci, 2);
        hdxf_test::assert_eq(linear.gradient->colors[1].color.rgb, 16776960);
        hdxf_test::assert_eq(linear.pattern_name, "SOLID,_O");
        hdxf_test::assert_true(linear.solid, "70 1");
        hdxf_test::assert_eq(entity(dwg, "376")->as_hatch().gradient->name, "SPHERICAL");
        hdxf_test::assert_false(entity(dwg, "35A")->as_hatch().gradient.has_value(), "ANSI31: none");
        hdxf_test::assert_eq(static_cast<int>(entity(dwg, "371")->props.xdata.size()), 3,
                             "GradientColor1ACI … stay XDATA");
    }
}

// Built in memory: the background after 44 and the gradient after the seed points, in
// AutoCAD's order
TEST(fill_2004_written) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
    hdxf::Entity& text = dwg.add_mtext({0, 0, 0}, "Bakgrund", 2.5);
    text.as_mtext().background.flags = 1;
    text.as_mtext().background.color = hdxf::Color::from_rgb(54, 117, 66);
    text.as_mtext().background.scale = 1.25;
    hdxf::HatchGradient gradient;
    gradient.name = "SPHERICAL";
    gradient.angle = 0.5;
    gradient.colors = {{0.0, hdxf::Color::from_aci(5)}, {1.0, hdxf::Color::from_rgb(255, 255, 0)}};
    dwg.add_hatch({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, gradient);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "fills");
    std::vector<Record> recs = records_of(out);

    Record mtext, hatch;
    for (const auto& r : recs) {
        if (r[0].second == "MTEXT") mtext = r;
        if (r[0].second == "HATCH") hatch = r;
    }
    hdxf_test::assert_true(from_code(mtext, 90) == Record({{90, "1"}, {63, "97"}, {421, "3568962"},
                                                           {45, "1.25"}, {441, "0"}}),
                           "background as AutoCAD's 9DA");
    hdxf_test::assert_eq(from_code(mtext, 44).size() > 0 ? from_code(mtext, 44)[1].first : 0, 90, "after 44");
    Record fill = from_code(hatch, 450);
    hdxf_test::assert_true(codes(fill) == kGradientCodes, "gradient groups in AutoCAD's order");
    hdxf_test::assert_eq(fill[8].second, "5");
    hdxf_test::assert_eq(fill[9].second, "255", "RGB of ACI 5");
    hdxf_test::assert_eq(fill.back().second, "SPHERICAL");
    hdxf_test::assert_eq(from_code(hatch, 2)[0].second, "SOLID");
    hdxf_test::assert_eq(from_code(hatch, 70)[0].second, "1");

    hdxf::Drawing back = load(out);
    for (const auto& e : back.entities()) {
        if (e.type == hdxf::EntityType::Hatch) {
            hdxf_test::assert_eq(e.as_hatch().gradient->angle, 0.5);
            hdxf_test::assert_eq(e.as_hatch().gradient->colors[1].color.rgb, 0xFFFF00);
        }
        if (e.type == hdxf::EntityType::MText) hdxf_test::assert_eq(e.as_mtext().background.scale, 1.25);
    }
    hdxf_test::assert_eq(static_cast<int>(records_of(save(dwg, hdxf::DxfFormat::Binary)).size()),
                         static_cast<int>(recs.size()), "binary");

    // A gradient has two colors
    hdxf::Drawing wrong(hdxf::DxfVersion::R2004);
    hdxf::HatchGradient one;
    one.colors = {{0.0, hdxf::Color::from_aci(1)}};
    wrong.add_hatch({{0, 0}, {1, 0}, {1, 1}}, one);
    hdxf_test::assert_true(wrong.save_to_memory().empty(), "one color");
    hdxf_test::assert_eq(wrong.last_error(), "A gradient fill has two colors (1 given)");

    // Not as 2000 or R12 (B23)
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R12}) {
        dwg.set_version(v);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "older version");
    }
    hdxf::Drawing plain(hdxf::DxfVersion::R2000);
    plain.add_mtext({0, 0, 0}, "x", 1.0).as_mtext().background.flags = 3;
    hdxf_test::assert_true(plain.save_to_memory().empty(), "2000 background");
    hdxf_test::assert_eq(plain.last_error(), "An MTEXT background requires DXF 2004 or later");
    plain.entities()[0].as_mtext().background.flags = 0;
    plain.add_hatch({{0, 0}, {1, 0}, {1, 1}}, gradient);
    hdxf_test::assert_true(plain.save_to_memory().empty(), "2000 gradient");
    hdxf_test::assert_eq(plain.last_error(), "A gradient fill requires DXF 2004 or later");
}

// AutoCAD's file changed: a gradient turned (the hatch's list rewritten from 91, the head
// and XDATA as they were), a gradient removed, one added to a pattern hatch, a background
// recolored
TEST(fill_2004_changed_file) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    entity(dwg, "371")->as_hatch().gradient->angle = 0.75;
    entity(dwg, "376")->as_hatch().gradient.reset();
    hdxf::HatchGradient added;
    added.colors = {{0.0, hdxf::Color::from_aci(1)}, {1.0, hdxf::Color::from_aci(3)}};
    entity(dwg, "35A")->as_hatch().gradient = added;
    entity(dwg, "9DA")->as_mtext().background.color = hdxf::Color::from_aci(4);

    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "fills changed");
    std::vector<Record> after = records_of(out);

    Record turned = record(after, "371");
    Record turned_before = record(before, "371");
    hdxf_test::assert_true(before_code(turned, 91) == before_code(turned_before, 91), "head as it was");
    hdxf_test::assert_true(from_code(turned, 1001) == from_code(turned_before, 1001), "XDATA as it was");
    Record fill = from_code(turned, 450);
    hdxf_test::assert_eq(std::strtod(fill[2].second.c_str(), nullptr), 0.75, "460");
    hdxf_test::assert_true(codes(Record(fill.begin(), fill.begin() + 14)) == kGradientCodes, "order");

    hdxf_test::assert_true(from_code(record(after, "376"), 450).empty() ||
                           from_code(record(after, "376"), 450)[0].first != 450, "no gradient");
    for (const auto& g : record(after, "376")) hdxf_test::assert_true(g.first != 450 && g.first != 470, "376");
    Record pattern = record(after, "35A");
    Record pattern_fill = from_code(pattern, 450);
    hdxf_test::assert_true(codes(Record(pattern_fill.begin(), pattern_fill.begin() + 14)) == kGradientCodes,
                           "added to 35A");
    hdxf_test::assert_eq(from_code(pattern, 98)[0].second, "1", "after its seed point");

    Record text = record(after, "9DA");
    Record bg = from_code(text, 90);
    hdxf_test::assert_true(codes(bg) == std::vector<int>({90, 63, 45, 441}), "background recolored: no 421 for ACI");
    hdxf_test::assert_eq(std::atoi(bg[1].second.c_str()), 4, "63");
    hdxf_test::assert_eq(bg[0].second, "1", "90");
    // An MTEXT's 90-441 and a HATCH's gradient from AutoCAD: unchanged ones as they were
    hdxf_test::assert_true(record(after, "CDC") == record(before, "CDC"), "CDC as it was");
}

// Codex review of 2.4: a background's color book name with non-ASCII characters comes
// back the same through ASCII and binary
TEST(fill_2004_background_name_round_trip) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
    hdxf::Entity& t = dwg.add_mtext({0, 0, 0}, "x", 1.0);
    t.as_mtext().background.flags = 1;
    t.as_mtext().background.color = hdxf::Color::from_book("BOOK$Blå", 0, 0, 200);
    for (hdxf::DxfFormat f : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing back = load(save(dwg, f));
        hdxf_test::assert_eq(back.entities()[0].as_mtext().background.color.name, "BOOK$Blå");
    }
}

// The window's color is bit 2 with bit 1 (90 3): 2 alone makes a file TrueView does not
// open (Erik, 2026-10-23: 90 2 failed with and without 63, 90 3 with 63 256 opened)
TEST(fill_2004_window_color) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
    hdxf::Entity& t = dwg.add_mtext({0, 0, 0}, "x", 1.0);
    t.as_mtext().background.flags = 3;
    t.as_mtext().background.color = hdxf::Color::by_layer();
    Record mtext;
    for (const auto& r : records_of(save(dwg))) if (r[0].second == "MTEXT") mtext = r;
    hdxf_test::assert_true(from_code(mtext, 90) == Record({{90, "3"}, {63, "256"}, {45, "1.5"}, {441, "0"}}),
                           "as TrueView opens it");
    dwg.entities()[0].as_mtext().background.flags = 2;
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "2 alone");
    hdxf_test::assert_eq(dwg.last_error(), "An MTEXT background with the window's color (2) is 3: 2 needs 1");
}
