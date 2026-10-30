#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// True color (420) and color book colors (430, DBCOLOR) in DXF 2004 — phase 2, step 2.2
// (plan B21, B23)
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

std::string value(const Record& r, int code, int occurrence = 0) {
    for (const auto& [c, v] : r) {
        if (c == code && occurrence-- == 0) return v;
    }
    return {};
}

// The groups of a record's AcDbEntity
Record entity_part(const Record& r) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 100) { in = g.second == "AcDbEntity"; continue; }
        if (in) out.push_back(g);
    }
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

const hdxf::Layer* layer(const hdxf::Drawing& dwg, const std::string& name) {
    for (const auto& l : dwg.layers()) if (l.name == name) return &l;
    return nullptr;
}

const hdxf::Entity* entity(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) if (e.props.handle == handle) return &e;
    }
    return nullptr;
}

const char* const kAscii = "acadsharp/sample_AC1018_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1018_binary.dxf";

} // namespace

// AutoCAD's palette as AutoCAD uses it
TEST(color_aci_palette) {
    hdxf_test::assert_eq(hdxf::aci_rgb(1), 0xFF0000);
    hdxf_test::assert_eq(hdxf::aci_rgb(7), 0xFFFFFF);
    hdxf_test::assert_eq(hdxf::aci_rgb(8), 0x808080);
    hdxf_test::assert_eq(hdxf::aci_rgb(9), 0xC0C0C0);
    hdxf_test::assert_eq(hdxf::aci_rgb(21), (255 << 16) | (159 << 8) | 127);
    hdxf_test::assert_eq(hdxf::aci_rgb(23), (204 << 16) | (127 << 8) | 102);
    hdxf_test::assert_eq(hdxf::aci_rgb(150), (0 << 16) | (127 << 8) | 255);
    hdxf_test::assert_eq(hdxf::aci_rgb(250), 0x333333);
    hdxf_test::assert_eq(hdxf::aci_rgb(255), 0xFFFFFF);
    hdxf_test::assert_eq(hdxf::aci_rgb(0), -1, "ByBlock");
    hdxf_test::assert_eq(hdxf::aci_rgb(256), -1, "ByLayer");
    // AutoCAD's own choices in its files
    hdxf_test::assert_eq(hdxf::nearest_aci(0x000000), 18, "black");
    hdxf_test::assert_eq(hdxf::nearest_aci((252 << 16) | (193 << 8) | 135), 31);
    hdxf_test::assert_eq(hdxf::nearest_aci(1151726), 150);
    for (int i = 1; i < 256; ++i) {
        int back = hdxf::nearest_aci(hdxf::aci_rgb(i));
        hdxf_test::assert_eq(hdxf::aci_rgb(back), hdxf::aci_rgb(i), "index " + std::to_string(i));
    }

    hdxf::Color c = hdxf::Color::from_rgb(17, 146, 238);
    hdxf_test::assert_eq(c.aci, 150);
    hdxf_test::assert_eq(c.rgb, 1151726);
    hdxf_test::assert_eq(c.rgb_red(), 17);
    hdxf_test::assert_eq(c.rgb_green(), 146);
    hdxf_test::assert_eq(c.rgb_blue(), 238);
    hdxf_test::assert_true(c.has_rgb() && !c.is_by_layer() && !c.is_by_block(), "true color");
    hdxf::Color book = hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 0xE2, 0x90, 0x00);
    hdxf_test::assert_eq(book.aci, 42);
    hdxf_test::assert_eq(book.name, "RAL CLASSIC$RAL 1006");
    hdxf_test::assert_true(hdxf::Color::from_aci(5).rgb == -1, "ACI has no RGB");
}

// What AutoCAD's 2004 file has, read the same from ASCII and binary
TEST(color_2004_read) {
    if (!has_files({kAscii, kBinary})) return;
    for (const char* name : {kAscii, kBinary}) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(external(name)), dwg.last_error());
        const hdxf::Layer* tc = layer(dwg, "Layer_true_color");
        hdxf_test::assert_true(tc != nullptr, "Layer_true_color");
        hdxf_test::assert_eq(tc->color.aci, 150);
        hdxf_test::assert_eq(tc->color.rgb, 1151726);
        hdxf_test::assert_eq(tc->color.name, "");
        const hdxf::Layer* book = layer(dwg, "Layer_color_book");
        hdxf_test::assert_eq(book->color.aci, 31);
        hdxf_test::assert_eq(book->color.rgb, 16564615);
        hdxf_test::assert_eq(book->color.name, "DIC COLOR GUIDE(R)$DIC 7");
        hdxf_test::assert_eq(layer(dwg, "color_125_33_79")->color.rgb, (125 << 16) | (33 << 8) | 79);

        const hdxf::Entity* circle = entity(dwg, "99F");
        hdxf_test::assert_eq(circle->props.color.aci, 42);
        hdxf_test::assert_eq(circle->props.color.rgb, 14848000);
        hdxf_test::assert_eq(circle->props.color.name, "RAL CLASSIC$RAL 1006");
        const hdxf::Entity* in_block = entity(dwg, "9D4");     // a LINE in dimension block *D14
        hdxf_test::assert_eq(in_block->props.color.aci, 183);
        hdxf_test::assert_eq(in_block->props.color.rgb, 8404418);

        const hdxf::Object* dbcolor = dwg.find_object("996");
        hdxf_test::assert_true(dbcolor && dbcolor->type == hdxf::ObjectType::DbColor, "DBCOLOR typed");
        hdxf_test::assert_eq(dbcolor->as_db_color().color.aci, 42);
        hdxf_test::assert_eq(dbcolor->as_db_color().color.rgb, 14848000);
        hdxf_test::assert_eq(dbcolor->as_db_color().color.name, "RAL CLASSIC$RAL 1006");
        hdxf_test::assert_true(dbcolor->xdata.empty(), "DBColXD is the color, not XDATA");
    }
}

// Every true color AutoCAD wrote has the 62 HDxf would choose
TEST(color_2004_nearest_aci_as_autocad) {
    if (!has_files({kAscii})) return;
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(external(kAscii)), dwg.last_error());
    int checked = 0;
    auto check = [&](const hdxf::Color& c, const std::string& what) {
        if (!c.has_rgb()) return;
        ++checked;
        hdxf_test::assert_eq(hdxf::nearest_aci(c.rgb), c.aci, what);
    };
    for (const auto& l : dwg.layers()) check(l.color, "layer " + l.name);
    for (const auto& e : dwg.entities()) check(e.props.color, "entity " + e.props.handle);
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) check(e.props.color, "entity " + e.props.handle);
    }
    hdxf_test::assert_true(checked > 20, "true colors: " + std::to_string(checked));
}

// A drawing in memory with true colors and color book colors, written as AutoCAD writes
// them: 62 420 430 after 8, a DBCOLOR for the entity's color book color (none for the
// layer's), the class with its count
TEST(color_2004_written) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
    dwg.add_layer("Blue", hdxf::Color::from_rgb(17, 146, 238));
    dwg.add_layer("Book", hdxf::Color::from_book("DIC COLOR GUIDE(R)$DIC 7", 0xFC, 0xC2, 0x87));
    dwg.add_line({0, 0, 0}, {10, 0, 0}, "Blue").props.color = hdxf::Color::from_rgb(128, 64, 32);
    dwg.add_circle({5, 5, 0}, 2.0).props.color = hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 0xE2, 0x90, 0x00);
    dwg.add_circle({9, 5, 0}, 1.0).props.color = hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 0xE2, 0x90, 0x00);
    size_t objects = dwg.objects().size();

    std::vector<uint8_t> out = save(dwg);
    const hdxf::Entity& line = dwg.entities()[0];
    const hdxf::Entity& circle = dwg.entities()[1];
    dxf_structure::check_structure(out, "colors");
    std::vector<Record> recs = records_of(out);

    Record head = entity_part(record(recs, line.props.handle));
    std::vector<int> codes;
    for (const auto& g : head) codes.push_back(g.first);
    std::string got;
    for (int code : codes) got += " " + std::to_string(code);
    hdxf_test::assert_true(codes == std::vector<int>({8, 62, 420}), "LINE: 8 62 420, got" + got);
    hdxf_test::assert_eq(value(head, 62), std::to_string(hdxf::nearest_aci((128 << 16) | (64 << 8) | 32)));
    hdxf_test::assert_eq(value(head, 420), std::to_string((128 << 16) | (64 << 8) | 32));
    Record c = entity_part(record(recs, circle.props.handle));
    hdxf_test::assert_true(c == Record({{8, "0"}, {62, "42"}, {420, "14848000"}, {430, "RAL CLASSIC$RAL 1006"}}),
                           "CIRCLE as AutoCAD's 99F");
    for (const auto& r : recs) {
        if (r[0].second == "LAYER" && value(r, 2) == "Blue") {
            hdxf_test::assert_eq(value(r, 62), "150");
            hdxf_test::assert_eq(value(r, 420), "1151726");
        }
        if (r[0].second == "LAYER" && value(r, 2) == "Book") {
            hdxf_test::assert_eq(value(r, 430), "DIC COLOR GUIDE(R)$DIC 7");
        }
    }

    // One DBCOLOR, for the circles' color, in ACAD_COLOR — as AutoCAD's 996
    hdxf_test::assert_eq(static_cast<int>(dwg.objects().size()), static_cast<int>(objects) + 2,
                         "ACAD_COLOR and one DBCOLOR");
    std::vector<Record> dbcolors;
    for (const auto& r : recs) if (r[0].second == "DBCOLOR") dbcolors.push_back(r);
    hdxf_test::assert_eq(static_cast<int>(dbcolors.size()), 1, "one DBCOLOR");
    Record expected_tail = {{100, "AcDbColor"}, {62, "42"}, {420, "14848000"}, {430, "RAL CLASSIC$RAL 1006"},
                            {1001, "ACAD"}, {1000, "DBColXD"}, {1070, "42"}, {1071, "-1025339392"},
                            {1000, "RAL CLASSIC$RAL 1006"}};
    Record tail(dbcolors[0].end() - static_cast<long>(expected_tail.size()), dbcolors[0].end());
    hdxf_test::assert_true(tail == expected_tail, "DBCOLOR as AutoCAD writes it");
    std::string dict = value(dbcolors[0], 330, 1);
    Record colors = record(recs, dict);
    hdxf_test::assert_eq(value(colors, 3), "RAL CLASSIC$RAL 1006", "ACAD_COLOR entry");
    hdxf_test::assert_eq(value(colors, 350), value(dbcolors[0], 5));
    bool class_found = false;
    for (const auto& r : recs) {
        if (r[0].second == "CLASS" && value(r, 1) == "DBCOLOR") {
            class_found = true;
            hdxf_test::assert_eq(value(r, 2), "AcDbColor");
            hdxf_test::assert_eq(value(r, 91), "1");
        }
    }
    hdxf_test::assert_true(class_found, "DBCOLOR class");

    // A second save adds nothing; the colors read back as written
    std::vector<uint8_t> again = save(dwg);
    hdxf_test::assert_true(again == out, "same file again");
    hdxf::Drawing back = load(out);
    hdxf_test::assert_eq(back.entities()[1].props.color.name, "RAL CLASSIC$RAL 1006");
    hdxf_test::assert_eq(back.entities()[0].props.color.rgb, (128 << 16) | (64 << 8) | 32);
    hdxf_test::assert_eq(layer(back, "Book")->color.rgb, (0xFC << 16) | (0xC2 << 8) | 0x87);
    hdxf_test::assert_eq(static_cast<int>(records_of(save(dwg, hdxf::DxfFormat::Binary)).size()),
                         static_cast<int>(recs.size()), "binary");
}

// Conflicts are errors, and the drawing is as it was
TEST(color_2004_conflicts) {
    struct Case {
        const char* what;
        hdxf::Color a;
        hdxf::Color b;
        const char* error;
    };
    hdxf::Color no_rgb;
    no_rgb.aci = 42;
    no_rgb.name = "RAL CLASSIC$RAL 1006";
    const Case cases[] = {
        {"two RGB", hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 0xE2, 0x90, 0x00),
         hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 0xE2, 0x90, 0x01),
         "Color \"RAL CLASSIC$RAL 1006\" is used with two RGB values"},
        {"no RGB", hdxf::Color::from_aci(1), no_rgb,
         "Color \"RAL CLASSIC$RAL 1006\" from a color book has no RGB value"},
    };
    for (const auto& c : cases) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
        dwg.add_line({0, 0, 0}, {1, 0, 0}).props.color = c.a;
        dwg.add_line({0, 1, 0}, {1, 1, 0}).props.color = c.b;
        size_t objects = dwg.objects().size();
        hdxf_test::assert_true(dwg.save_to_memory().empty(), c.what);
        hdxf_test::assert_eq(dwg.last_error(), c.error, c.what);
        hdxf_test::assert_eq(static_cast<int>(dwg.objects().size()), static_cast<int>(objects), "objects as they were");
        hdxf_test::assert_true(dwg.entities()[0].props.handle.empty(), "no handles given");
    }

    // An existing DBCOLOR with another RGB (the circle that used it is plain now)
    if (!has_files({kAscii})) return;
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(external(kAscii)), dwg.last_error());
    for (auto& e : dwg.entities()) {
        if (e.props.handle == "99F") e.props.color = hdxf::Color::from_aci(42);
    }
    dwg.add_line({0, 0, 0}, {1, 0, 0}).props.color = hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 1, 2, 3);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved");
    hdxf_test::assert_eq(dwg.last_error(), "Color \"RAL CLASSIC$RAL 1006\" is used with another RGB value than its DBCOLOR");
}

// True colors in a drawing saved as R12 or 2000: an error (B23)
TEST(color_2004_refused_as_older) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000}) {
        hdxf::Drawing dwg(v);
        dwg.add_line({0, 0, 0}, {1, 0, 0}).props.color = hdxf::Color::from_rgb(1, 2, 3);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved");
        hdxf_test::assert_eq(dwg.last_error(), "True color requires DXF 2004 or later");
        dwg.entities()[0].props.color = hdxf::Color::from_aci(5);
        dwg.add_layer("L", hdxf::Color::from_rgb(1, 2, 3));
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "layer");
        dwg.layers().back().color = hdxf::Color::from_aci(5);
        hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    }
}

// AutoCAD's file changed: a color made true (62 420 where AutoCAD puts them), a color
// book color made plain, a new color book color in its ACAD_COLOR; the rest as it was
TEST(color_2004_changed_file) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    auto find = [&](const std::string& handle) -> hdxf::Entity* {
        for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
        return nullptr;
    };
    find("2C7")->props.color = hdxf::Color::from_rgb(200, 10, 10);
    find("99F")->props.color = hdxf::Color::from_aci(3);
    dwg.add_line({0, 0, 0}, {5, 5, 0}).props.color = hdxf::Color::from_book("RAL CLASSIC$RAL 3020", 0xC1, 0x12, 0x1C);
    for (auto& l : dwg.layers()) {
        if (l.name == "Layer_true_color") l.color = hdxf::Color::from_rgb(10, 200, 10);
    }

    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "colors changed");
    std::vector<Record> after = records_of(out);

    Record line = entity_part(record(after, "2C7"));
    Record line_before = entity_part(record(before, "2C7"));
    hdxf_test::assert_eq(static_cast<int>(line.size()), static_cast<int>(line_before.size()) + 2, "62 and 420 added");
    hdxf_test::assert_eq(line[0].first, 8);
    hdxf_test::assert_eq(line[1].first, 62, "62 after 8");
    hdxf_test::assert_eq(line[2].first, 420, "420 after 62");
    hdxf_test::assert_eq(line[2].second, std::to_string((200 << 16) | (10 << 8) | 10));
    Record circle = entity_part(record(after, "99F"));
    hdxf_test::assert_true(circle == Record({{8, "0"}, {62, "3"}}), "CIRCLE plain green");

    for (const auto& r : after) {
        if (r[0].second == "LAYER" && value(r, 2) == "Layer_true_color") {
            hdxf_test::assert_eq(value(r, 420), std::to_string((10 << 16) | (200 << 8) | 10));
            hdxf_test::assert_eq(value(r, 62), std::to_string(hdxf::nearest_aci((10 << 16) | (200 << 8) | 10)));
        }
    }
    // The new color in AutoCAD's ACAD_COLOR (73), its entries in order
    Record colors = record(after, "73");
    std::vector<std::string> names;
    for (const auto& [code, v] : colors) if (code == 3) names.push_back(v);
    hdxf_test::assert_true(names == std::vector<std::string>({"RAL CLASSIC$RAL 1006", "RAL CLASSIC$RAL 3020"}),
                           "ACAD_COLOR entries");
    // The DBCOLOR of the color nobody uses any more stays, as it was
    hdxf_test::assert_true(record(after, "996") == record(before, "996"), "DBCOLOR 996 as it was");

    // A DBCOLOR changed: its XDATA follows
    hdxf::Object* dbcolor = dwg.find_object("996");
    dbcolor->as_db_color().color = hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 0xE2, 0x90, 0x10);
    std::vector<Record> third = records_of(save(dwg));
    Record changed = record(third, "996");
    hdxf_test::assert_eq(value(changed, 420), std::to_string((0xE2 << 16) | (0x90 << 8) | 0x10));
    hdxf_test::assert_eq(value(changed, 1071), std::to_string(static_cast<int32_t>(0xC2E29010u)));
}

// Codex review of 2.2: an unknown entity keeps its common fields when written anew; a
// DBCOLOR with ACAD XDATA of its own gets no second ACAD block; color book names compare
// without case
TEST(color_2004_review_cases) {
    if (!has_files({kAscii})) return;
    hdxf::Drawing sample;
    hdxf_test::assert_true(sample.load(external(kAscii)), sample.last_error());
    const hdxf::Entity* underlay = entity(sample, "D07");          // PDFUNDERLAY, 62 193 420 …
    hdxf_test::assert_true(underlay && underlay->type == hdxf::EntityType::PdfUnderlay, "a PDFUNDERLAY (typed since step 9.6 of phase 9)");
    hdxf_test::assert_eq(underlay->props.color.rgb, 10175212, "its true color in props");

    hdxf::Drawing fresh(hdxf::DxfVersion::R2004);
    hdxf::Entity copy = *underlay;
    copy.raw = hdxf::RawRecord();
    copy.props.handle.clear();
    copy.props.lineweight = 30;
    fresh.entities().push_back(copy);
    std::vector<Record> recs = records_of(save(fresh));
    Record head;
    for (const auto& r : recs) if (r[0].second == "PDFUNDERLAY") head = entity_part(r);
    hdxf_test::assert_eq(value(head, 62), "193");
    hdxf_test::assert_eq(value(head, 420), "10175212");
    hdxf_test::assert_eq(value(head, 370), "30");

    hdxf::Object* dbcolor = sample.find_object("996");
    dbcolor->xdata.push_back({"ACAD", {{1000, "DBColXD"}, {1070, "42"}, {1071, "-1025339392"},
                                       {1000, "RAL CLASSIC$RAL 1006"}, {1000, "more"}}});
    dbcolor->as_db_color().color = hdxf::Color::from_book("RAL CLASSIC$RAL 1006", 0xE2, 0x90, 0x00);
    Record written = record(records_of(save(sample)), "996");
    int acad = 0;
    for (const auto& [code, v] : written) acad += code == 1001 && v == "ACAD";
    hdxf_test::assert_eq(acad, 1, "one ACAD block");

    hdxf::Drawing cases(hdxf::DxfVersion::R2004);
    cases.add_line({0, 0, 0}, {1, 0, 0}).props.color = hdxf::Color::from_book("BOOK$COLOR", 1, 2, 3);
    cases.add_line({0, 1, 0}, {1, 1, 0}).props.color = hdxf::Color::from_book("book$color", 4, 5, 6);
    hdxf_test::assert_true(cases.save_to_memory().empty(), "case variants with two RGB");
    hdxf_test::assert_eq(cases.last_error(), "Color \"book$color\" is used with two RGB values");
}
