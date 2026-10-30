#include "test_framework.hpp"
#include "hdxf.hpp"
#include "fonts.hpp"
#include "group_io.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Fonts and the text style's font family (rättningsfas R.8)
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
        std::string v = gc.value;
        size_t a = v.find_first_not_of(' ');
        out.back().push_back({gc.code, a == std::string::npos ? std::string() : v.substr(a)});
    }
    return out;
}

// The STYLE record named `name`
Record style_record(const std::vector<Record>& records, const std::string& name) {
    for (const auto& r : records) {
        if (r.empty() || r[0].second != "STYLE") continue;
        for (const auto& g : r) {
            if (g.first == 2 && g.second == name) return r;
        }
    }
    return {};
}

// The groups from the first 1001 on
Record xdata_of(const Record& r) {
    for (size_t i = 0; i < r.size(); ++i) {
        if (r[i].first == 1001) return Record(r.begin() + static_cast<long>(i), r.end());
    }
    return {};
}

std::filesystem::path acadsharp(const std::string& file) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "acadsharp" / file;
}

const hdxf::TextStyle* style_named(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& s : dwg.text_styles()) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

std::filesystem::path windows_font(const std::string& file) {
    const char* windir = std::getenv("WINDIR");
    return windir ? hdxf::path_from_utf8(windir) / "Fonts" / file : std::filesystem::path(file);
}

const std::filesystem::path kAutoCadFonts = "C:/Program Files/Autodesk/DWG TrueView 2027 - English/Fonts";

} // namespace

// AutoCAD's own file: the family (ACAD 1000/1071) is the style's, the other XDATA stays
TEST(style_font_family_read) {
    if (!std::filesystem::exists(acadsharp("sample_AC1018_ascii.dxf"))) hdxf_test::skip("sample_AC1018_ascii.dxf missing");
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(acadsharp("sample_AC1018_ascii.dxf")), dwg.last_error());
    const hdxf::TextStyle* standard = style_named(dwg, "Standard");
    hdxf_test::assert_true(standard != nullptr, "Standard");
    hdxf_test::assert_eq(standard->font_family, "Arial", "Standard's family");
    hdxf_test::assert_eq(standard->font_flags, 34, "Standard's flags");
    hdxf_test::assert_true(standard->xdata.empty(), "no other XDATA");
    const hdxf::TextStyle* annotative = style_named(dwg, "Annotative");
    hdxf_test::assert_eq(annotative->font_family, "Arial", "Annotative's family");
    hdxf_test::assert_eq(static_cast<int>(annotative->xdata.size()), 1, "AcadAnnotative stays");
    hdxf_test::assert_eq(annotative->xdata[0].app_name, "AcadAnnotative", "AcadAnnotative");
    const hdxf::TextStyle* custom = style_named(dwg, "custom_text_style");
    hdxf_test::assert_eq(custom->font_family, "Consolas", "custom family");
    hdxf_test::assert_eq(custom->font_flags, 49, "custom flags");
    const hdxf::TextStyle* shapes = nullptr;
    for (const auto& s : dwg.text_styles()) {
        if (s.font_file == "ltypeshp.shx") shapes = &s;
    }
    hdxf_test::assert_true(shapes && shapes->font_family.empty(), "a shape file has no family");

    // A changed style is written anew: its XDATA, then ACAD last, as AutoCAD
    hdxf::TextStyle* edit = nullptr;
    for (auto& s : dwg.text_styles()) {
        if (s.name == "Annotative") edit = &s;
    }
    edit->width_factor = 0.8;
    Record xd = xdata_of(style_record(records_of(dwg.save_to_memory()), "Annotative"));
    Record want = {{1001, "AcadAnnotative"}, {1000, "AnnotativeData"}, {1002, "{"}, {1070, "1"}, {1070, "1"},
                   {1002, "}"}, {1001, "ACAD"}, {1000, "Arial"}, {1071, "34"}};
    hdxf_test::assert_true(xd == want, "AcadAnnotative, then ACAD");
}

// A new 2000 drawing: the family written and read back; R12 has no TrueType fonts
TEST(style_font_family_written) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::TextStyle& s = dwg.add_text_style("Bold", "arialbd.ttf");
    s.font_family = "Arial";
    s.font_flags = 0x02000022;
    Record xd = xdata_of(style_record(records_of(dwg.save_to_memory()), "Bold"));
    Record want = {{1001, "ACAD"}, {1000, "Arial"}, {1071, std::to_string(0x02000022)}};
    hdxf_test::assert_true(xd == want, "ACAD 1000 1071");
    hdxf::Drawing back;
    std::vector<uint8_t> data = dwg.save_to_memory();
    hdxf_test::assert_true(back.load_from_memory(data.data(), data.size()), back.last_error());
    const hdxf::TextStyle* b = style_named(back, "Bold");
    hdxf_test::assert_true(b && b->font_family == "Arial" && b->font_flags == 0x02000022, "read back");

    // R12: a TrueType style as txt, as AutoCAD writes it, with a warning
    hdxf::Drawing r12;
    r12.add_text_style("T", "arial.ttf").font_family = "Arial";
    std::vector<Record> recs = records_of(r12.save_to_memory());
    hdxf_test::assert_true(!recs.empty(), r12.last_error());
    Record t = style_record(recs, "T");
    bool txt = false;
    for (const auto& g : t) {
        if (g.first == 3) txt = g.second == "txt";
        hdxf_test::assert_true(g.first < 1000, "no XDATA in R12");
    }
    hdxf_test::assert_true(txt, "font txt");
    hdxf_test::assert_eq(static_cast<int>(r12.warnings().size()), 1, "one warning");
    // Other style XDATA cannot be written as R12
    r12.text_styles().back().xdata.push_back({"APP", {}});
    hdxf_test::assert_true(r12.save_to_memory().empty(), "refused");
    hdxf_test::assert_true(r12.last_error().find("XDATA") != std::string::npos, r12.last_error());
}

// TrueType: AutoCAD's text height is the height of "H"; the descent is usWinDescent
TEST(font_truetype_metrics) {
    std::filesystem::path arial = windows_font("arial.ttf");
    if (!std::filesystem::exists(arial)) hdxf_test::skip("arial.ttf missing");
    auto f = hdxf::internal::load_font(arial);
    hdxf_test::assert_true(f != nullptr, "loaded");
    hdxf::internal::Glyph h = f->glyph(U'H');
    hdxf_test::assert_true(h.found && h.has_ink, "H");
    hdxf_test::assert_true(std::fabs(h.max_y - 1.0) < 1e-12, "H is the text height");
    hdxf_test::assert_true(std::fabs(f->descent() - 434.0 / 1466.0) < 1e-12, "descent");
    hdxf_test::assert_true(!f->glyph(U' ').has_ink && f->glyph(U' ').advance > 0, "a space");
    hdxf_test::assert_true(!f->glyph(U'\U0001F600').found, "not in the font");
}

// CFF outlines (OpenType "OTTO"): the boxes from the charstrings
TEST(font_cff_boxes) {
    std::filesystem::path otf = windows_font("SourceHanSansJP-Normal.otf");
    if (!std::filesystem::exists(otf)) hdxf_test::skip("SourceHanSansJP-Normal.otf missing");
    auto f = hdxf::internal::load_font(otf);
    hdxf_test::assert_true(f != nullptr, "loaded");
    hdxf::internal::Glyph h = f->glyph(U'H');
    hdxf_test::assert_true(h.has_ink && std::fabs(h.max_y - 1.0) < 1e-12, "H is the text height");
    hdxf_test::assert_true(h.min_y > -1e-9 && h.min_y < 1e-9, "H stands on the baseline");
    hdxf::internal::Glyph g = f->glyph(U'g');
    hdxf_test::assert_true(g.has_ink && g.min_y < -0.2 && g.max_y < 1.0, "g has a descender");
    hdxf::internal::Glyph o = f->glyph(U'O');
    hdxf_test::assert_true(o.has_ink && o.max_y > 1.0 && o.min_y < 0.0, "O's curves pass the lines");
    hdxf::internal::Glyph kanji = f->glyph(U'\u6F22');
    hdxf_test::assert_true(kanji.has_ink && kanji.max_x > kanji.min_x, "a kanji");
}

// By family, as Windows chooses: bold is another face
TEST(font_by_family) {
    if (!std::filesystem::exists(windows_font("arialbd.ttf"))) hdxf_test::skip("arialbd.ttf missing");
    auto regular = hdxf::internal::find_font_family("Arial", false, false);
    auto bold = hdxf::internal::find_font_family("arial", true, false);
    hdxf_test::assert_true(regular && bold, "found");
    hdxf_test::assert_true(bold->glyph(U'i').advance > regular->glyph(U'i').advance, "bold's i is wider");
    hdxf_test::assert_true(!hdxf::internal::find_font_family("No Such Family", false, false), "none");
}

// Shape fonts: unifont txt.shx, below over above; a shape file's shapes by name
TEST(font_shape_files) {
    if (!std::filesystem::exists(kAutoCadFonts / "txt.shx")) hdxf_test::skip("AutoCAD's fonts missing");
    auto txt = hdxf::internal::load_font(kAutoCadFonts / "txt.shx");
    hdxf_test::assert_true(txt && txt->is_shape_font(), "txt.shx");
    hdxf::internal::Glyph h = txt->glyph(U'H');
    hdxf_test::assert_true(h.has_ink && std::fabs(h.max_y - 1.0) < 1e-12 && h.min_y == 0.0, "H");
    hdxf_test::assert_true(txt->descent() > 0.0 && txt->descent() < 1.0, "descent");
    auto shapes = hdxf::internal::load_font(kAutoCadFonts / "ltypeshp.shx");
    hdxf_test::assert_true(shapes != nullptr, "ltypeshp.shx");
    uint32_t box = shapes->shape_number("box");
    hdxf_test::assert_true(box != 0 && shapes->glyph(box).has_ink, "BOX by name");
    hdxf_test::assert_eq(static_cast<int>(shapes->shape_number("NOSUCH")), 0, "no such shape");
}
