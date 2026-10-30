#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "text_encoding.hpp"
#include "dxf_structure.hpp"
#include "dxf_writer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// DXF 2004 (phase 2, step 2.1 — plan B20, B24, B25): a drawing in memory written as
// AutoCAD writes 2004, and AutoCAD's own 2004 file written back record for record.
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

std::filesystem::path synthetic(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / hdxf::path_from_utf8(name);
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
            if ((code == 5 || code == 105) && value == handle) return r;
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

// The codes of a record from its subclass `subclass` to the next one
std::vector<int> codes_in(const Record& r, const std::string& subclass) {
    std::vector<int> out;
    bool in = false;
    for (const auto& [code, v] : r) {
        if (code == 100) { in = v == subclass; continue; }
        if (in) out.push_back(code);
    }
    return out;
}

// The variable names of a file's HEADER, in order
std::vector<std::string> header_names(const std::vector<Record>& records) {
    std::vector<std::string> out;
    for (const auto& [code, v] : records.at(0)) if (code == 9) out.push_back(v);
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

const char* const kAscii = "acadsharp/sample_AC1018_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1018_binary.dxf";

} // namespace

// A new 2004 drawing: AutoCAD's 211 header variables in AutoCAD's order, AC1018 and 104,
// CLASSES with 91 and without LAYOUT/ACDBPLACEHOLDER, 76-78 in the layouts, 170 on the
// viewport
TEST(write_2004_new_drawing) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2004);
    dwg.add_line({0, 0, 0}, {10, 5, 0});
    hdxf::Entity& vp = dwg.add_viewport({100, 100, 0}, 80, 60);
    vp.as_viewport().shade_plot_mode = 2;
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "new 2004 drawing");
    std::vector<Record> recs = records_of(out);

    std::vector<std::string> names = header_names(recs);
    hdxf_test::assert_eq(static_cast<int>(names.size()), 211, "header variables");
    if (has_files({kAscii})) {
        std::vector<std::string> autocad = header_names(records_of(read_bytes(external(kAscii))));
        hdxf_test::assert_true(names == autocad, "AutoCAD's variables in AutoCAD's order");
    }
    std::string header;
    for (const auto& [code, v] : recs[0]) header += std::to_string(code) + " " + v + "\n";
    hdxf_test::assert_true(header.find("9 $ACADVER\n1 AC1018\n9 $ACADMAINTVER\n70 104\n") != std::string::npos, "version");
    hdxf_test::assert_true(header.find("9 $LASTSAVEDBY\n1 " + hdxf::internal::login_name() + "\n") != std::string::npos,
                           "$LASTSAVEDBY: the login name, as AutoCAD");
    hdxf_test::assert_true(header.find("9 $HIDETEXT\n280 1\n") != std::string::npos, "$HIDETEXT as AutoCAD writes it");

    int classes = 0;
    for (const auto& r : recs) {
        if (r[0].second != "CLASS") continue;
        ++classes;
        std::string name = value(r, 1);
        hdxf_test::assert_true(name != "LAYOUT" && name != "ACDBPLACEHOLDER", name + " is no class in 2004");
        std::vector<int> codes;
        for (const auto& g : r) codes.push_back(g.first);
        hdxf_test::assert_true(codes == std::vector<int>({0, 1, 2, 3, 90, 91, 280, 281}), name + ": groups");
        if (name == "ACDBDICTIONARYWDFLT") hdxf_test::assert_eq(value(r, 91), "1", "one instance");
    }
    hdxf_test::assert_true(classes > 0, "classes");

    int layouts = 0;
    for (const auto& r : recs) {
        if (r[0].second != "LAYOUT") continue;
        ++layouts;
        std::vector<int> codes = codes_in(r, "AcDbPlotSettings");
        auto at = std::find(codes.begin(), codes.end(), 147);
        hdxf_test::assert_true(at != codes.end() && codes.end() - at > 5, "147");
        hdxf_test::assert_true(std::vector<int>(at, at + 6) == std::vector<int>({147, 76, 77, 78, 148, 149}),
                               "76-78 after 147");
        hdxf_test::assert_eq(value(r, 77), "2", "normal resolution");
        hdxf_test::assert_eq(value(r, 78), "300", "300 dpi");
    }
    hdxf_test::assert_eq(layouts, 3, "Model, Layout1 and Layout2, as AutoCAD's new drawing");

    // The Model layout's last viewport (331) is VPORT *Active, as in AutoCAD's files
    std::string active;
    for (const auto& r : recs) if (r[0].second == "VPORT") active = value(r, 5);
    for (const auto& r : recs) {
        if (r[0].second != "LAYOUT") continue;
        bool model = false;
        for (const auto& [code, v] : r) model = model || (code == 1 && v == "Model");
        hdxf_test::assert_eq(value(r, 331), model ? active : std::string(), "331");
    }

    for (const auto& r : recs) {
        if (r[0].second != "VIEWPORT") continue;
        std::vector<int> codes = codes_in(r, "AcDbViewport");
        hdxf_test::assert_eq(codes.back(), 170, "170 last");
        hdxf_test::assert_eq(codes[codes.size() - 2], 146, "after 146");
        hdxf_test::assert_eq(r.back().second, "2", "hidden");
    }

    hdxf::Drawing back = load(out);
    hdxf_test::assert_true(back.version() == hdxf::DxfVersion::R2004, "version read back");
    int read_mode = -1;
    for (const auto& e : back.entities()) {
        if (e.type == hdxf::EntityType::Viewport) read_mode = e.as_viewport().shade_plot_mode;
    }
    hdxf_test::assert_eq(read_mode, 2, "170 read back");

    // The binary file has the same records
    std::vector<uint8_t> bin = save(dwg, hdxf::DxfFormat::Binary);
    hdxf_test::assert_eq(static_cast<int>(records_of(bin).size()), static_cast<int>(recs.size()), "binary");
}

// What 2004 introduced cannot be written as an older version (B23): an error before
// anything is changed
TEST(write_2004_properties_refused_as_older) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    hdxf::Entity& vp = dwg.add_viewport({100, 100, 0}, 80, 60);
    vp.as_viewport().shade_plot_mode = 1;
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved as 2000");
    hdxf_test::assert_eq(dwg.last_error(), "A viewport's shade plot mode requires DXF 2004 or later");
    hdxf_test::assert_true(dwg.entities()[0].props.handle.empty(), "no handles given");
    dwg.set_version(hdxf::DxfVersion::R12);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved as R12");
    dwg.set_version(hdxf::DxfVersion::R2004);
    hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());

    hdxf::Drawing layouts(hdxf::DxfVersion::R2000);
    for (auto& l : layouts.layouts()) l->as_layout().plot_settings.shade_plot_dpi = 600;
    hdxf_test::assert_true(layouts.save_to_memory().empty(), "saved as 2000");
    hdxf_test::assert_eq(layouts.last_error(), "A layout's shade plot settings requires DXF 2004 or later");
}

// Read and written in the same format: every record as in the file, group for group
TEST(roundtrip_2004_files_record_for_record) {
    struct File { std::filesystem::path path; hdxf::DxfFormat format; };
    std::vector<File> files = {{synthetic("r2004_unicode_escapes.dxf"), hdxf::DxfFormat::Ascii},
                               {synthetic("r2004_thumbnail.dxf"), hdxf::DxfFormat::Ascii}};
    if (std::filesystem::exists(external(kAscii))) files.push_back({external(kAscii), hdxf::DxfFormat::Ascii});
    if (std::filesystem::exists(external(kBinary))) files.push_back({external(kBinary), hdxf::DxfFormat::Binary});
    for (const auto& f : files) {
        std::string what = hdxf::utf8_from_path(f.path.filename());
        std::vector<uint8_t> in = read_bytes(f.path);
        hdxf::Drawing dwg = load(in);
        std::vector<Record> before = records_of(in);
        std::vector<Record> after = records_of(save(dwg, f.format));
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()),
                             what + ": records");
        for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
            hdxf_test::assert_true(before[i] == after[i], what + ": record " + std::to_string(i) +
                                   " (" + before[i][0].second + ") differs");
        }
        hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), what + ": warnings");
    }
}

// AutoCAD's binary 2004 file comes back byte for byte
TEST(roundtrip_2004_binary_bytes) {
    if (!has_files({kBinary})) return;
    std::vector<uint8_t> in = read_bytes(external(kBinary));
    hdxf::Drawing dwg = load(in);
    hdxf_test::assert_true(save(dwg, hdxf::DxfFormat::Binary) == in, "bytes");
}

// Into the other format and back: the same values (binary exactly, ASCII with AutoCAD's
// 16 significant digits)
TEST(roundtrip_2004_ascii_binary_ascii) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    hdxf::Drawing ascii = load(in);
    std::vector<uint8_t> bin = save(ascii, hdxf::DxfFormat::Binary);
    hdxf::Drawing binary = load(bin);
    std::vector<uint8_t> back = save(binary, hdxf::DxfFormat::Ascii);

    std::vector<Record> a = records_of(in), b = records_of(bin), c = records_of(back);
    hdxf_test::assert_eq(static_cast<int>(b.size()), static_cast<int>(a.size()), "records (binary)");
    hdxf_test::assert_eq(static_cast<int>(c.size()), static_cast<int>(a.size()), "records (ASCII)");
    auto same = [](const Record& x, const Record& y, double tolerance, const std::string& what, bool y_binary) {
        hdxf_test::assert_eq(static_cast<int>(y.size()), static_cast<int>(x.size()), what + ": groups");
        for (size_t g = 0; g < x.size() && g < y.size(); ++g) {
            hdxf_test::assert_eq(y[g].first, x[g].first, what + ": code");
            using hdxf::internal::GroupValueType;
            GroupValueType t = hdxf::internal::group_value_type(x[g].first);
            if (t == GroupValueType::Double) {
                double u = std::strtod(x[g].second.c_str(), nullptr);
                double v = std::strtod(y[g].second.c_str(), nullptr);
                hdxf_test::assert_true(std::fabs(u - v) <= tolerance * std::max(1.0, std::fabs(u)),
                                       what + ": " + x[g].second + " / " + y[g].second);
            } else if (t == GroupValueType::String || t == GroupValueType::BinaryChunk) {
                std::string expected = (y_binary && t == GroupValueType::String)
                                           ? hdxf::internal::decode_carets(x[g].second) : x[g].second;
                hdxf_test::assert_eq(y[g].second, expected, what);
            } else {
                hdxf_test::assert_true(std::atoll(y[g].second.c_str()) == std::atoll(x[g].second.c_str()),
                                       what + ": " + x[g].second);
            }
        }
    };
    for (size_t i = 0; i < a.size() && i < b.size() && i < c.size(); ++i) {
        same(a[i], b[i], 0.0, "binary record " + std::to_string(i), true);
        same(a[i], c[i], 1e-15, "ASCII record " + std::to_string(i), false);
    }
}

// Changes to AutoCAD's 2004 file: written anew where they are, everything else as it was,
// CLASSES as read (B24)
TEST(roundtrip_2004_changes) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);

    auto find = [&](const std::string& handle) -> hdxf::Entity* {
        for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
        return nullptr;
    };
    hdxf_test::assert_true(find("2C7") != nullptr && find("69D") != nullptr && find("3BA") != nullptr,
                           "the entities of the 2000 test are in the 2004 file too");
    find("2C7")->as_line().end.x = 50.0;
    for (auto& l : dwg.layers()) {
        if (l.name == "Layer1") l.color = hdxf::Color::from_aci(3);
    }
    // Removed: a member of GROUP *A1 (69D) and a TEXT with an extension dictionary (3BA)
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) {
        return e.props.handle == "69D" || e.props.handle == "3BA";
    }), ents.end());
    dwg.add_line({0, 0, 0}, {1, 1, 0}, "Layer1");
    std::vector<uint8_t> first = save(dwg);
    std::string added = dwg.entities().back().props.handle;
    dwg.find_object("69E")->as_group().entities.push_back(added);

    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "changed AutoCAD 2004 file");
    std::vector<Record> after = records_of(out);

    Record line = record(after, "2C7");
    hdxf_test::assert_eq(std::strtod(value(line, 11).c_str(), nullptr), 50.0, "moved");
    hdxf_test::assert_eq(value(line, 10), value(record(before, "2C7"), 10), "start as it was");
    for (const auto& r : after) {
        if (r[0].second == "LAYER" && value(r, 2) == "Layer1") {
            hdxf_test::assert_eq(value(r, 62), "3", "colour");
        }
    }
    for (const char* h : {"69D", "3BA"}) hdxf_test::assert_true(record(after, h).empty(), std::string(h) + " still there");
    Record group = record(after, "69E");
    std::vector<std::string> members;
    for (const auto& [code, v] : group) if (code == 340) members.push_back(v);
    hdxf_test::assert_eq(members.back(), added, "the new member");
    hdxf_test::assert_eq(value(record(after, added), 330, 0), "69E", "reactor of the new member");

    // CLASSES as read, 91 included (AutoCAD's count, B24)
    std::vector<Record> classes_before, classes_after;
    for (const auto& r : before) if (r[0].second == "CLASS") classes_before.push_back(r);
    for (const auto& r : after) if (r[0].second == "CLASS") classes_after.push_back(r);
    hdxf_test::assert_true(classes_before == classes_after, "CLASSES as read");

    // Everything else as it was
    int same = 0;
    for (const auto& r : before) {
        std::string h;
        for (const auto& [code, v] : r) { if (code == 5) { h = v; break; } if (code == 100) break; }
        if (h.empty() || r[0].second == "SECTION" || h == "2C7" || h == "69E") continue;
        Record w = record(after, h);
        if (w.empty()) continue;
        if (w == r) ++same;
        else if (r[0].second != "LAYER" && r[0].second != "XRECORD" && r[0].second != "DICTIONARY") {
            hdxf_test::assert_true(false, "record " + h + " (" + r[0].second + ") changed");
        }
    }
    // 1066 records, less the ones removed, changed and the section heads
    hdxf_test::assert_true(same > 950, "unchanged records: " + std::to_string(same));
    hdxf::Drawing back = load(out);
    hdxf_test::assert_eq(static_cast<int>(back.entities().size()), static_cast<int>(dwg.entities().size()));
}

// A class added to a read 2004 file gets the number written (B24)
TEST(roundtrip_2004_class_added) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(synthetic("r2004_unicode_escapes.dxf")), dwg.last_error());
    dwg.add_image("photo.jpg", {0, 0, 0}, {10, 10}, 10.0);
    dwg.add_image("photo.jpg", {20, 0, 0}, {10, 10}, 10.0);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "image added to 2004");
    bool image = false;
    for (const auto& r : records_of(out)) {
        if (r[0].second != "CLASS" || value(r, 1) != "IMAGE") continue;
        image = true;
        hdxf_test::assert_eq(value(r, 91), "2", "two images");
    }
    hdxf_test::assert_true(image, "IMAGE class");
}

// A new layout's shade plot pointer (333) to an object that is gone is left out (B19)
TEST(roundtrip_2004_shade_plot_pointer_removed) {
    if (!has_files({kAscii})) return;
    hdxf::Drawing dwg = load(read_bytes(external(kAscii)));
    std::string style;
    for (const auto& o : dwg.objects()) {
        if (style.empty() && o.type == hdxf::ObjectType::VisualStyle) style = o.handle;
    }
    hdxf_test::assert_false(style.empty(), "a VISUALSTYLE");
    hdxf::Object& layout = dwg.add_layout("Sheet");
    layout.as_layout().plot_settings.shade_plot_handle = style;
    std::string layout_handle = layout.handle;
    auto& objects = dwg.objects();
    objects.erase(std::remove_if(objects.begin(), objects.end(),
                                 [&](const hdxf::Object& o) { return o.handle == style; }), objects.end());
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "333 to a removed object");
    hdxf_test::assert_eq(value(record(records_of(out), layout_handle), 333), "", "no 333");
}

// Added after reading and removed again before saving: what went with it goes too, and
// the classes count what is written (B19, B24)
TEST(roundtrip_2004_added_and_removed) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(synthetic("r2004_unicode_escapes.dxf")), dwg.last_error());
    dwg.add_image("photo.jpg", {0, 0, 0}, {10, 10}, 10.0);
    dwg.add_image("photo.jpg", {20, 0, 0}, {10, 10}, 10.0);
    std::string removed = dwg.entities().back().props.handle;
    std::string reactor = dwg.entities().back().as_image().reactor_handle;
    dwg.entities().pop_back();
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "image added and removed");
    std::vector<Record> recs = records_of(out);
    hdxf_test::assert_true(record(recs, removed).empty(), "image gone");
    hdxf_test::assert_true(record(recs, reactor).empty(), "its IMAGEDEF_REACTOR gone");
    for (const auto& r : recs) {
        if (r[0].second != "CLASS") continue;
        if (value(r, 1) == "IMAGE" || value(r, 1) == "IMAGEDEF_REACTOR") {
            hdxf_test::assert_eq(value(r, 91), "1", value(r, 1));
        }
    }
}

// $LASTSAVEDBY of a read file: as it was in an unchanged round trip, the login name when
// the drawing changed (as AutoCAD writes it at every save), a name the program set stays
TEST(write_2004_last_saved_by) {
    if (!has_files({kAscii})) return;
    auto saved_by = [](const std::vector<uint8_t>& out) {
        for (size_t i = 0; i + 1 < records_of(out)[0].size(); ++i) {
            if (records_of(out)[0][i].second == "$LASTSAVEDBY") return records_of(out)[0][i + 1].second;
        }
        return std::string("?");
    };
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    hdxf::Drawing same = load(in);
    hdxf_test::assert_eq(saved_by(save(same)), "alber", "unchanged");
    hdxf::Drawing changed = load(in);
    changed.add_line({0, 0, 0}, {1, 1, 0});
    hdxf_test::assert_eq(saved_by(save(changed)), hdxf::internal::login_name(), "changed");
    hdxf::Drawing named = load(in);
    named.add_line({0, 0, 0}, {1, 1, 0});
    named.set_header_string("$LASTSAVEDBY", 1, "HDxf");
    hdxf_test::assert_eq(saved_by(save(named)), "HDxf", "set by the program");
}
