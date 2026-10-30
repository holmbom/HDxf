#include "test_framework.hpp"
#include "hdxf.hpp"
#include "text_encoding.hpp"
#include "text_layout.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The size of text (rättningsfas R.8): MTEXT against what AutoCAD computed — 42/43 in
// ACadSharp's 2018 file, and the lines AutoCAD wrote as TEXT in its R12 file of the same
// drawing — and TEXT, ATTRIB, SHAPE and the extents with the fonts.
// ---------------------------------------------------------------------------

namespace {

using hdxf::internal::TextBox;
using hdxf::internal::TextLayout;

std::filesystem::path windows_font(const std::string& file) {
    const char* windir = std::getenv("WINDIR");
    return windir ? hdxf::path_from_utf8(windir) / "Fonts" / file : std::filesystem::path(file);
}

const std::filesystem::path kAutoCadFonts = "C:/Program Files/Autodesk/DWG TrueView 2027 - English/Fonts";

// A 2000 drawing whose Standard is AutoCAD's: arial.ttf, family Arial
hdxf::Drawing arial_drawing() {
    if (!std::filesystem::exists(windows_font("arial.ttf"))) hdxf_test::skip("arial.ttf missing");
    hdxf::set_font_paths({windows_font("arial.ttf").parent_path()});
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::TextStyle& s = dwg.add_text_style("Standard", "arial.ttf");
    s.font_family = "Arial";
    s.font_flags = 34;
    return dwg;
}

void near(double a, double b, const std::string& what, double tolerance = 1e-9) {
    hdxf_test::assert_true(std::fabs(a - b) <= tolerance,
                           what + ": " + std::to_string(a) + " != " + std::to_string(b));
}

void same_box(const TextBox& a, const TextBox& b, const std::string& what, double tolerance = 1e-9) {
    hdxf_test::assert_true(a.valid && b.valid, what + ": boxes");
    near(a.min_x, b.min_x, what + " min x", tolerance);
    near(a.min_y, b.min_y, what + " min y", tolerance);
    near(a.max_x, b.max_x, what + " max x", tolerance);
    near(a.max_y, b.max_y, what + " max y", tolerance);
}

// A line AutoCAD wrote as TEXT (left, baseline) in the R12 file: its ink in Arial
struct R12Line {
    const char* text;
    double x, y;
};

TextBox r12_lines(TextLayout& layout, const std::vector<R12Line>& lines, double height) {
    TextBox box;
    const auto& fonts = layout.fonts("Standard");
    for (const auto& l : lines) {
        auto m = hdxf::internal::measure_line(fonts, hdxf::internal::text_characters(l.text, 1252),
                                              height, 1.0, 0.0);
        box.add(l.x + m.ink.min_x, l.y + m.ink.min_y);
        box.add(l.x + m.ink.max_x, l.y + m.ink.max_y);
    }
    return box;
}

hdxf::MTextData mtext(const char* content, double x, double y, double height, double width,
                      int attachment, int spacing = 1) {
    hdxf::MTextData d;
    d.content = content;
    d.insertion_point = {x, y, 0.0};
    d.height = height;
    d.reference_width = width;
    d.attachment = attachment;
    d.line_spacing_style = spacing;
    return d;
}

// The MTEXT's box placed at its insertion point (its direction is X)
TextBox placed(const hdxf::internal::MTextSize& s, const hdxf::MTextData& d) {
    TextBox b;
    b.add(d.insertion_point.x + s.lines.min_x, d.insertion_point.y + s.lines.min_y);
    b.add(d.insertion_point.x + s.lines.max_x, d.insertion_point.y + s.lines.max_y);
    return b;
}

struct FontPaths {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    ~FontPaths() { hdxf::set_font_paths(saved); }
};

} // namespace

// 42/43 as AutoCAD wrote them in ACadSharp's sample_AC1032 (the embedded MTEXT)
TEST(mtext_size_as_autocad) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    TextLayout layout(dwg);
    {
        // 3EC: two lines, at least
        auto d = mtext("this is a Mtext\\Pwith multiple lines in it", 183.5889280790413, 5.226370718413136,
                       1.0, 15.74141424498748, 1);
        auto s = layout.mtext(d);
        near(s.width, 13.49522510231923, "3EC 42");
        near(s.height, 2.944292860391086, "3EC 43");
    }
    {
        // 3F6: exactly
        auto d = mtext("this is a Mtext\\Pwith multiple lines in it", 199.3303423240288, 15.32610174482911,
                       1.0, 15.74141424498748, 6, 2);
        auto s = layout.mtext(d);
        near(s.width, 13.49522510231923, "3F6 42");
        near(s.height, 3.0, "3F6 43");
    }
    {
        // 779: 123 lines wrapped at the width
        std::string content = "this is a really long Mtext\\P";
        for (int i = 0; i < 121 * 7 + 5 - 1; ++i) content += "bla ";   // 121 lines of 7, then 5
        content += "bla";
        auto d = mtext(content.c_str(), 183.5889280790413, -1.38637568506374, 1.0, 15.74141424498748, 1);
        auto s = layout.mtext(d);
        near(s.width, 15.26057298772169, "779 42");
        near(s.height, 204.3497044110957, "779 43", 1e-8);
    }
}

// The lines AutoCAD wrote as TEXT in sample_AC1009 for the MTEXT of sample_AC1015: their
// ink is the MTEXT's box — attachment, wrapping, centered lines, ink tops above the height
TEST(mtext_lines_as_autocad) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    TextLayout layout(dwg);
    struct Case {
        const char* name;
        hdxf::MTextData d;
        std::vector<R12Line> lines;
    };
    std::vector<Case> cases = {
        {"3EC", mtext("this is a Mtext\\Pwith multiple lines in it", 183.5889280790413, 5.226370718413136,
                      1.0, 15.74141424498748, 1),
         {{"this is a Mtext", 183.5643714623973892, 4.2263707184131363},
          {"with multiple lines in it", 183.5848353096006917, 2.5597040517464689}}},
        {"3F6", mtext("this is a Mtext\\Pwith multiple lines in it", 199.3303423240288, 15.32610174482911,
                      1.0, 15.74141424498748, 6, 2),
         {{"this is a Mtext", 190.8010108097041098, 15.8261017448291099},
          {"with multiple lines in it", 185.8310244522689061, 14.1594350781624492}}},
        {"513", mtext("Sample annotation", 741.0237500252849, 14.69681240225556, 1.424925205719706, 0.0, 7),
         {{"Sample annotation", 740.934327706781346, 15.0924089634615104}}},
        {"576", mtext("This table will be imported as an           Insert entity", 51.29730740590947,
                      -32.95648440086006, 4.0, 102.5262184019443, 5),
         {{"This table will be imported as an", 11.51285992978395, -31.0351565245526508},
          {"Insert entity", 36.619271935240981, -37.7018231912193116}}},
        {"573", mtext("Text", 17.09910246863649, -10.66658296853037, 4.0, 34.12980852739841, 5),
         {{"Text", 11.6202484440798699, -12.6338408130051292}}},
        {"572", mtext("Table sample", 51.29730740590947, -3.745517683606088, 4.0, 102.5262184019443, 5),
         {{"Table sample", 34.5756157278740019, -5.1902652961572482}}},
        {"A3D", mtext("0.000000, 0.000000, 0.000000", 6.25, -1.12008640291043, 0.18, 2.379999999999999, 2),
         {{"0.000000, 0.000000,", 5.1023465211459751, -1.300823101409748},
          {"0.000000", 5.7263915416098232, -1.6015597999090661}}},
        {"A40", mtext("35.00%", 1.25, -1.724629376989554, 0.18, 2.379999999999999, 2),
         {{"35.00%", 0.8260300136425648, -1.9076989540700451}}},
        {"A41", mtext("$100.00", 3.75, -1.724629376989554, 0.18, 2.379999999999999, 2),
         {{"$100.00", 3.2970532060027282, -1.921205093224206}}},
        {"1499", mtext("Cell with 2 blocks", 11.39350613915416, -0.5133333333333497, 0.18, 1.0206275579809, 1),
         {{"Cell with", 11.3809822646657501, -0.6964029104138408},
          {"2 blocks", 11.3861391541609809, -0.9971396089131586}}},
    };
    for (const auto& c : cases) {
        auto s = layout.mtext(c.d);
        same_box(placed(s, c.d), r12_lines(layout, c.lines, c.d.height), c.name);
    }
}

namespace {

TextBox corners_box(const hdxf::internal::TextCorners& c) {
    TextBox b;
    for (const auto& q : c.corners) b.add(q.x, q.y);
    return b;
}

hdxf::internal::TextLine line_of(const char* content, double height, int h, int v, hdxf::Point3 p,
                                 hdxf::Point3 a = {}) {
    hdxf::internal::TextLine t;
    t.style = "Standard";
    t.content = content;
    t.height = height;
    t.h_justify = h;
    t.v_justify = v;
    t.position = p;
    t.alignment_point = a;
    return t;
}

hdxf::Point3 header_point(const hdxf::Drawing& dwg, const std::string& name) {
    const hdxf::HeaderVar* hv = dwg.find_header(name);
    hdxf_test::assert_true(hv != nullptr, name);
    hdxf::Point3 p;
    for (const auto& [code, value] : hv->values) {
        if (code == 10) p.x = std::stod(value);
        if (code == 20) p.y = std::stod(value);
        if (code == 30) p.z = std::stod(value);
    }
    return p;
}

} // namespace

// TEXT: the ink box placed by the justification, the height, mirrored and turned
TEST(text_box_justified) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    TextLayout layout(dwg);
    const auto& fonts = layout.fonts("Standard");
    auto m = hdxf::internal::measure_line(fonts, hdxf::internal::text_characters("Hello", 1252), 2.0, 1.0, 0.0);
    double d = fonts.font->descent() * 2.0;
    struct Case { int h, v; double dx, dy; };
    for (const Case& c : {Case{0, 0, 0, 0}, Case{1, 0, m.advance / 2, 0}, Case{2, 0, m.advance, 0},
                          Case{0, 1, 0, -d}, Case{0, 2, 0, 1.0}, Case{2, 3, m.advance, 2.0},
                          Case{4, 0, m.advance / 2, (m.ink.min_y + m.ink.max_y) / 2}}) {
        auto box = corners_box(layout.text(line_of("Hello", 2.0, c.h, c.v, {9, 9, 0}, {10, 20, 0})));
        bool left = c.h == 0 && c.v == 0;
        double x = left ? 9.0 : 10.0, y = left ? 9.0 : 20.0;
        TextBox want;
        want.add(x + m.ink.min_x - c.dx, y + m.ink.min_y - c.dy);
        want.add(x + m.ink.max_x - c.dx, y + m.ink.max_y - c.dy);
        same_box(box, want, "72 " + std::to_string(c.h) + " 73 " + std::to_string(c.v));
    }
    // Turned 90°: x up; backward: to the left; upside down: below
    auto t = line_of("Hello", 2.0, 0, 0, {0, 0, 0});
    t.rotation = 90.0;
    TextBox turned;
    turned.add(-m.ink.max_y, m.ink.min_x);
    turned.add(-m.ink.min_y, m.ink.max_x);
    same_box(corners_box(layout.text(t)), turned, "rotation");
    t.rotation = 0.0;
    t.generation_flags = 2;
    TextBox back;
    back.add(-m.ink.max_x, m.ink.min_y);
    back.add(-m.ink.min_x, m.ink.max_y);
    same_box(corners_box(layout.text(t)), back, "backward");
    t.generation_flags = 4;
    TextBox upside;
    upside.add(m.ink.min_x, -m.ink.max_y);
    upside.add(m.ink.max_x, -m.ink.min_y);
    same_box(corners_box(layout.text(t)), upside, "upside down");
    // Aligned: twice the advance from 10 to 11 doubles the height; Fit only the width
    double len = 2.0 * m.advance;
    auto aligned = corners_box(layout.text(line_of("Hello", 2.0, 3, 0, {0, 0, 0}, {len, 0, 0})));
    near(aligned.min_x, 2.0 * m.ink.min_x, "aligned left");
    near(aligned.max_y, 2.0 * m.ink.max_y, "aligned height");
    auto fit = corners_box(layout.text(line_of("Hello", 2.0, 5, 0, {0, 0, 0}, {len, 0, 0})));
    near(fit.max_x, 2.0 * m.ink.max_x, "fit width");
    near(fit.max_y, m.ink.max_y, "fit height");
    // Spaces only: nothing drawn
    hdxf_test::assert_false(layout.text(line_of("   ", 2.0, 0, 0, {})).ink, "spaces draw nothing");
    // %%u: the underline is ink below the baseline
    auto u = corners_box(layout.text(line_of("%%uHello", 2.0, 0, 0, {})));
    near(u.min_y, std::min(m.ink.min_y, -fonts.font->underline() * 2.0), "underline");
}

// The % codes and the characters fonts number by the code page
TEST(text_characters_codes) {
    using namespace hdxf::internal;
    std::u32string s = text_characters("%%c%%d%%p%%%%%u%%176x", 1252);
    std::u32string want = {kDiameter, kDegree, kPlusMinus, U'%', kUnderline, U'°', U'x'};
    hdxf_test::assert_true(s == want, "%% codes");
    hdxf_test::assert_eq(static_cast<int>(code_page_code(1252, U'°')), 0xB0, "degree in 1252");
    hdxf_test::assert_eq(static_cast<int>(code_page_code(932, U'あ')), 0x82A0, "hiragana a in Shift-JIS");
    hdxf_test::assert_eq(static_cast<int>(code_page_code(1252, U'あ')), 0, "hiragana a not in 1252");
}

// A big font: a character of the code page's double-byte set from the big font
TEST(text_big_font) {
    FontPaths keep;
    if (!std::filesystem::exists(kAutoCadFonts / "extfont.shx")) hdxf_test::skip("AutoCAD's fonts missing");
    hdxf::set_font_paths({kAutoCadFonts});
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.set_header_string("$DWGCODEPAGE", 3, "ANSI_932");
    hdxf::TextStyle& s = dwg.add_text_style("JP", "txt");
    s.big_font = "extfont.shx";
    TextLayout layout(dwg);
    const auto& fonts = layout.fonts("JP");
    hdxf_test::assert_true(fonts.big_font != nullptr && fonts.big_font->big_font(), "extfont.shx");
    hdxf_test::assert_eq(fonts.code_page, 932, "the drawing's code page");
    auto g = hdxf::internal::text_glyph(fonts, U'あ');
    hdxf_test::assert_true(g.found && g.has_ink, "hiragana a in the big font");
    auto q = hdxf::internal::text_glyph(fonts, U'?');
    hdxf_test::assert_true(g.advance != q.advance || g.max_x != q.max_x, "not the ? of txt");
}

// SHAPE: the shape from the shape file a style names, scaled by its size
TEST(shape_box) {
    FontPaths keep;
    if (!std::filesystem::exists(kAutoCadFonts / "ltypeshp.shx")) hdxf_test::skip("AutoCAD's fonts missing");
    hdxf::set_font_paths({kAutoCadFonts});
    hdxf::Drawing dwg;
    hdxf::TextStyle& s = dwg.add_text_style("", "ltypeshp.shx");
    s.flags = 1;
    auto file = hdxf::internal::find_font("ltypeshp.shx");
    hdxf_test::assert_true(file != nullptr, "ltypeshp.shx");
    auto g = file->glyph(file->shape_number("BOX"));
    hdxf_test::assert_true(g.has_ink, "BOX");
    hdxf::ShapeData d;
    d.name = "BOX";
    d.size = 2.0;
    d.insertion_point = {5, 5, 0};
    TextLayout layout(dwg);
    auto c = layout.shape(d);
    hdxf_test::assert_true(c.font && c.ink, "found");
    TextBox want;
    want.add(5 + 2 * g.min_x, 5 + 2 * g.min_y);
    want.add(5 + 2 * g.max_x, 5 + 2 * g.max_y);
    same_box(corners_box(c), want, "BOX");
}

// The extents with the fonts: the text's box; without them its insertion point and a
// warning
TEST(text_extents_with_fonts) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    dwg.add_text({10, 20, 0}, "Hello", 2.0);
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf_test::assert_true(dwg.warnings().empty(), "Arial found");
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    TextLayout layout(dwg);
    auto m = hdxf::internal::measure_line(layout.fonts("Standard"), hdxf::internal::text_characters("Hello", 1252),
                                          2.0, 1.0, 0.0);
    hdxf::Point3 lo = header_point(back, "$EXTMIN"), hi = header_point(back, "$EXTMAX");
    near(lo.x, 10 + m.ink.min_x, "EXTMIN x", 1e-12);
    near(lo.y, 20 + m.ink.min_y, "EXTMIN y", 1e-12);
    near(hi.x, 10 + m.ink.max_x, "EXTMAX x", 1e-12);
    near(hi.y, 20 + m.ink.max_y, "EXTMAX y", 1e-12);

    hdxf::set_font_paths({});
    out = dwg.save_to_memory();
    hdxf_test::assert_eq(static_cast<int>(dwg.warnings().size()), 1, "the missing font");
    hdxf_test::assert_true(dwg.warnings()[0].find("arial.ttf") != std::string::npos, dwg.warnings()[0]);
    hdxf::Drawing again;
    hdxf_test::assert_true(again.load_from_memory(out.data(), out.size()), again.last_error());
    near(header_point(again, "$EXTMIN").x, 10, "insertion point");
    near(header_point(again, "$EXTMAX").y, 20, "insertion point");
}

// A constant ATTDEF has no ATTRIB: it is shown in the insert with its value
TEST(text_extents_constant_attdef) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    hdxf::Block& b = dwg.add_block("B");
    hdxf::Entity def;
    def.type = hdxf::EntityType::Attdef;
    hdxf::AttdefData a;
    a.tag = "T";
    a.default_value = "WIDE CONSTANT VALUE";
    a.flags = 2;
    a.height = 1.0;
    def.data = a;
    b.entities.push_back(def);
    dwg.add_insert("B", {0, 0, 0});
    std::vector<uint8_t> out = dwg.save_to_memory();
    hdxf_test::assert_false(out.empty(), dwg.last_error());
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    TextLayout layout(dwg);
    auto m = hdxf::internal::measure_line(layout.fonts("Standard"),
                                          hdxf::internal::text_characters("WIDE CONSTANT VALUE", 1252), 1.0, 1.0, 0.0);
    near(header_point(back, "$EXTMAX").x, m.ink.max_x, "the value's width", 1e-12);
}

// TOLERANCE: the frames ODA File Converter draws for ACadSharp's three (sample_AC1015 as
// R12): rows DIMTXT + 2 DIMGAP high, boxes the ink and DIMGAP each side. Rows with Arial
// alone exactly; GDT symbols to 0.01, as ODA draws gdt.shx's arcs as polygons.
TEST(tolerance_frames_as_oda) {
    FontPaths keep;
    std::filesystem::path file = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "acadsharp" / "sample_AC1015_ascii.dxf";
    std::filesystem::path support = "C:/Program Files/Autodesk/DWG TrueView 2027 - English/UserDataCache/Support";
    if (!std::filesystem::exists(file)) hdxf_test::skip("sample_AC1015_ascii.dxf missing");
    if (!std::filesystem::exists(support / "gdt.shx")) hdxf_test::skip("gdt.shx missing");
    if (!std::filesystem::exists(windows_font("arial.ttf"))) hdxf_test::skip("arial.ttf missing");
    hdxf::set_font_paths({windows_font("arial.ttf").parent_path(), support});
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(file), dwg.last_error());
    TextLayout layout(dwg);
    struct Case {
        const char* handle;
        double min_x, min_y, max_x, max_y, tolerance;
    };
    for (const Case& c : {Case{"822", 214.1752393775127, -5.580437747060625, 214.9799174327033, -4.140437747060624, 0.01},
                          Case{"823", 215.9040473487617, -4.809691710787356, 216.3422601727727, -4.089691710787355, 1e-9},
                          Case{"824", 217.1045235281035, -4.537234233770897, 217.5512120957776, -4.177234233770896, 0.01}}) {
        const hdxf::Entity* e = nullptr;
        for (const auto& x : dwg.entities()) {
            if (x.type == hdxf::EntityType::Tolerance && x.props.handle == c.handle) e = &x;
        }
        hdxf_test::assert_true(e != nullptr, c.handle);
        const auto& d = e->as_tolerance();
        auto s = layout.tolerance(d, e->props.xdata);
        hdxf_test::assert_true(s.ink, std::string(c.handle) + " drawn");
        std::string what = std::string("TOLERANCE ") + c.handle;
        near(d.insertion_point.x + s.box.min_x, c.min_x, what + " left", 1e-9);
        near(d.insertion_point.y + s.box.min_y, c.min_y, what + " bottom", 1e-9);
        near(d.insertion_point.y + s.box.max_y, c.max_y, what + " top", 1e-9);
        near(d.insertion_point.x + s.box.max_x, c.max_x, what + " right", c.tolerance);
    }
}

// Codex's review of R.8: tabs count when a line is broken, \U+ needs four hex digits,
// \A out of range, the overline slants with the text, long text is not quadratic
TEST(mtext_review_cases) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    TextLayout layout(dwg);
    // "A" then a tab to 4 h, then "B": wider than 4.5 h, so B goes on the next line
    auto tabbed = layout.mtext(mtext("A\tB", 0, 0, 1.0, 4.5, 1));
    auto one = layout.mtext(mtext("A", 0, 0, 1.0, 0.0, 1));
    auto two_lines = layout.mtext(mtext("A\\PB", 0, 0, 1.0, 0.0, 1));
    near(tabbed.height, two_lines.height, "B wrapped after the tab");
    // \U+-001 is not an escape: shown as written, wider than one character
    auto bad = layout.mtext(mtext("\\U+-001", 0, 0, 1.0, 0.0, 1));
    hdxf_test::assert_true(bad.width > one.width * 3.0, "\\U+-001 as text");
    auto good = layout.mtext(mtext("\\U+0041", 0, 0, 1.0, 0.0, 1));
    near(good.width, one.width, "\\U+0041 is A");
    // \A1e309 is out of range: as \A0
    auto huge = layout.mtext(mtext("\\A1e309;A", 0, 0, 1.0, 0.0, 1));
    near(huge.width, one.width, "\\A1e309");
    // The overline slants: \Q45 moves its ends right by its height
    auto upright = layout.mtext(mtext("\\OA", 0, 0, 1.0, 0.0, 1));
    auto slanted = layout.mtext(mtext("\\Q45;\\OA", 0, 0, 1.0, 0.0, 1));
    hdxf_test::assert_true(slanted.lines.max_x > upright.lines.max_x + 0.9, "the overline slanted");
    // 20 000 words in a narrow column
    std::string many;
    for (int i = 0; i < 20000; ++i) many += "word ";
    auto big = layout.mtext(mtext(many.c_str(), 0, 0, 1.0, 10.0, 1));
    hdxf_test::assert_true(big.height > 1000.0, "laid out");
}

// The public API: MTEXT's 42/43 and an entity's extents
TEST(text_size_api) {
    FontPaths keep;
    hdxf::Drawing dwg = arial_drawing();
    hdxf::Entity& m = dwg.add_mtext({183.5889280790413, 5.226370718413136, 0}, "this is a Mtext\\Pwith multiple lines in it", 1.0);
    std::get<hdxf::MTextData>(m.data).reference_width = 15.74141424498748;
    double w = 0, h = 0;
    hdxf_test::assert_true(dwg.mtext_size(dwg.entities().back(), w, h), "MTEXT measured");
    near(w, 13.49522510231923, "42");
    near(h, 2.944292860391086, "43");
    hdxf::Point3 lo, hi;
    hdxf_test::assert_true(dwg.entity_extents(dwg.entities().back(), lo, hi), "MTEXT extents");
    // The lines, not the width it is set in (9.S, Codex 17: AutoCAD 2025's $EXTMAX of an MTEXT "A" set in a width of 100 ends at
    // the "A", doc/facit/fas9_mtext_width.scr; R.8 had the width from a forum report)
    near(hi.x - lo.x, 13.49522510231923, "the lines, not the width it is set in");
    near(hi.y - lo.y, 2.944292860391086, "its height");
    // The background fill: half the height around it at 1.5
    std::get<hdxf::MTextData>(dwg.entities().back().data).background.flags = 1;
    hdxf::Point3 blo, bhi;
    hdxf_test::assert_true(dwg.entity_extents(dwg.entities().back(), blo, bhi), "with its fill");
    near(blo.x, lo.x - 0.5, "fill left");
    near(bhi.y, hi.y + 0.5, "fill top");
    hdxf::Entity& x = dwg.add_xline({0, 0, 0}, {1, 0, 0});
    hdxf_test::assert_false(dwg.entity_extents(x, lo, hi), "XLINE has none");
    hdxf::Entity& t = dwg.add_text({0, 0, 0}, "Hello", 1.0);
    hdxf_test::assert_false(dwg.mtext_size(t, w, h), "TEXT is not MTEXT");
}
