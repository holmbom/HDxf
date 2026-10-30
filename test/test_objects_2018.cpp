#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
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
// The objects' fields of DXF 2018 (phase 6, step 6.6 — plan B77): the detail and section view
// styles' display name and flags (300 and 90 in 2018, the XRECORD of the extension dictionary
// before), DBCOLOR without its XDATA copy, and LIGHT, SUN and VISUALSTYLE without the XDATA that
// says the version that saved them.
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
        for (const auto& [code, v] : r) {
            if ((code == 5 || code == 105) && v == handle) return r;
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

Record groups_in(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 100) { in = g.second == subclass; continue; }
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

const char* const kAscii = "acadsharp/sample_AC1032_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1032_binary.dxf";
const char* const kAscii2013 = "acadsharp/sample_AC1027_ascii.dxf";

hdxf::Object* find_style(hdxf::Drawing& dwg, const std::string& handle) {
    return dwg.find_object(handle);
}

} // namespace

// AutoCAD's four view styles (detail and section, Imperial24 and Metric50): the base typed (B77),
// from 2018's 300 and 90 and from the XRECORD of 2013's extension dictionary, the same values
TEST(objects_2018_view_styles_are_typed) {
    for (const char* file : {kAscii, kBinary, kAscii2013}) {
        if (!has_files({file})) return;
        std::vector<uint8_t> in = read_bytes(external(file));
        std::vector<Record> recs = records_of(in);
        hdxf::Drawing dwg = load(in);
        struct Expect { const char* handle; hdxf::ObjectType type; const char* name; };
        for (const Expect& x : {Expect{"1AC", hdxf::ObjectType::DetailViewStyle, "Imperial24"},
                                Expect{"39C", hdxf::ObjectType::DetailViewStyle, "Metric50"},
                                Expect{"1AA", hdxf::ObjectType::SectionViewStyle, "Imperial24"},
                                Expect{"39B", hdxf::ObjectType::SectionViewStyle, "Metric50"}}) {
            hdxf::Object* o = find_style(dwg, x.handle);
            std::string what = std::string(file) + " " + x.handle;
            hdxf_test::assert_true(o != nullptr && o->type == x.type, what + ": typed");
            if (!o || o->type != x.type) continue;
            const hdxf::ModelDocViewStyleData& d = o->as_view_style();
            Record r = record(recs, x.handle);
            hdxf_test::assert_eq(d.name, x.name, what + ": 3");
            hdxf_test::assert_eq(d.version, 0, what + ": 70");
            hdxf_test::assert_false(d.flag, what + ": 290");
            hdxf_test::assert_eq(d.display_name, x.name, what + ": the display name");
            hdxf_test::assert_eq(d.display_flags, 0, what + ": its flags");
            // The derived subclass is typed (step 9.12): its first 40 is the identifier's height, its 71 are the five parts
            Record derived = groups_in(r, x.type == hdxf::ObjectType::DetailViewStyle ? "AcDbDetailViewStyle" : "AcDbSectionViewStyle");
            hdxf_test::assert_false(derived.empty(), what + ": the derived subclass");
            double height = x.type == hdxf::ObjectType::DetailViewStyle ? o->as_detail_view_style().identifier.height
                                                                        : o->as_section_view_style().identifier.height;
            hdxf_test::assert_eq(height, std::stod(value(derived, 40)), what + ": the identifier's height");
            int parts = 0;
            for (const auto& g : derived) if (g.first == 71) ++parts;
            hdxf_test::assert_eq(parts, 5, what + ": five parts");
        }
        // Written back as it is
        bool binary = std::string(file) == kBinary;
        hdxf_test::assert_true(save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii) == in,
                               std::string(file) + ": bytes");
    }
}

// A changed display name and flags: 2018's 300 and 90, the derived subclass as it was; before, the XRECORD
TEST(objects_2018_view_style_display_name_changes) {
    if (!has_files({kAscii, kAscii2013})) return;
    for (const char* file : {kAscii, kAscii2013}) {
        bool r2018 = std::string(file) == kAscii;
        std::vector<uint8_t> in = read_bytes(external(file));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        hdxf::Object* o = find_style(dwg, "1AC");
        hdxf_test::assert_true(o != nullptr, "the style");
        o->as_view_style().display_name = "Detaljvy \xC3\xA5\xC3\xA4\xC3\xB6";
        o->as_view_style().display_flags = 5;
        std::vector<Record> after = records_of(save(dwg));
        Record a = record(after, "1AC"), b = record(before, "1AC");
        if (r2018) {
            Record base = groups_in(a, "AcDbModelDocViewStyle");
            hdxf_test::assert_eq(value(base, 300), "Detaljvy \xC3\xA5\xC3\xA4\xC3\xB6", "300");
            hdxf_test::assert_eq(value(base, 90), "5", "90");
            hdxf_test::assert_true(groups_in(a, "AcDbDetailViewStyle") == groups_in(b, "AcDbDetailViewStyle"), "the derived subclass as it was");
        } else {
            // The XRECORD of the extension dictionary says them, the record itself as it was
            hdxf_test::assert_true(a == b, "the style's record as it was");
            std::string xdict = value(a, 360);
            Record dict = record(after, xdict);
            std::string x_handle;
            for (size_t i = 0; i + 1 < dict.size(); ++i) if (dict[i].first == 3 && dict[i].second == "ACAD_XREC_ROUNDTRIP") x_handle = dict[i + 1].second;
            Record x = groups_in(record(after, x_handle), "AcDbXrecord");
            hdxf_test::assert_eq(value(x, 1), "Detaljvy \xC3\xA5\xC3\xA4\xC3\xB6", "DISPLAYNAME's 1");
            hdxf_test::assert_eq(value(x, 90), "5", "FLAGS' 90");
        }
        // The other styles as they were
        for (const char* h : {"39C", "1AA", "39B"}) hdxf_test::assert_true(record(after, h) == record(before, h), std::string(h) + " as it was");
        // Read back
        std::vector<uint8_t> out = save(dwg);
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(find_style(back, "1AC")->as_view_style().display_name, "Detaljvy \xC3\xA5\xC3\xA4\xC3\xB6", "read back");
        hdxf_test::assert_eq(find_style(back, "1AC")->as_view_style().display_flags, 5, "its flags read back");
    }
}

// A style made in memory: its extension dictionary and XRECORD before 2018, none in 2018; the styles'
// relations (B19): a removed style takes them with it
TEST(objects_2018_view_style_made_and_removed) {
    if (!has_files({kAscii})) return;
    hdxf::Drawing src = load(read_bytes(external(kAscii)));
    hdxf::Object proto = *src.find_object("1AA");
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    dwg.add_line({0, 0, 0}, {1, 1, 0});
    proto.handle.clear();
    proto.raw = hdxf::RawRecord();
    proto.reactors.clear();
    proto.owner = "0";
    proto.as_view_style().name = "Mine";
    proto.as_view_style().display_name = "My section";
    proto.as_view_style().display_flags = 2;
    dwg.objects().push_back(proto);
    auto count_xrecords = [&](const std::vector<Record>& recs) {
        int n = 0;
        for (const auto& r : recs) {
            if (r[0].second != "XRECORD") continue;
            for (const auto& g : r) if (g.second == "DISPLAYNAME") ++n;
        }
        return n;
    };
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013,
                               hdxf::DxfVersion::R2018}) {
        dwg.set_version(v);
        std::vector<Record> recs = records_of(save(dwg));
        bool r2018 = v == hdxf::DxfVersion::R2018;
        hdxf_test::assert_eq(count_xrecords(recs), r2018 ? 0 : 1, std::string(hdxf::acadver_string(v)) + ": the XRECORD");
        Record style;
        for (const auto& r : recs) if (r[0].second == "ACDBSECTIONVIEWSTYLE") style = r;
        hdxf_test::assert_false(style.empty(), "the style is written");
        Record base = groups_in(style, "AcDbModelDocViewStyle");
        hdxf_test::assert_eq(value(base, 3), "Mine", "its name");
        hdxf_test::assert_eq(value(base, 300), r2018 ? "My section" : "", "300 only in 2018");
        hdxf_test::assert_eq(value(base, 90), r2018 ? "2" : "", "90 only in 2018");
        hdxf_test::assert_eq(value(style, 360).empty(), r2018, "its extension dictionary");
        // Its class
        bool class_there = false;
        for (const auto& r : recs) if (r[0].second == "CLASS" && value(r, 1) == "ACDBSECTIONVIEWSTYLE") class_there = true;
        hdxf_test::assert_true(class_there, "its class");
    }
    // Taken away, with what it owns
    dwg.set_version(hdxf::DxfVersion::R2013);
    save(dwg);
    auto& objects = dwg.objects();
    objects.erase(std::remove_if(objects.begin(), objects.end(), [](const hdxf::Object& o) { return o.type == hdxf::ObjectType::SectionViewStyle; }),
                  objects.end());
    std::vector<Record> recs = records_of(save(dwg));
    hdxf_test::assert_eq(count_xrecords(recs), 0, "the XRECORD went with the style");
}

// DBCOLOR in 2018 has no XDATA copy of its color (B77); a DBCOLOR with ACAD XDATA of its own keeps it
TEST(objects_2018_dbcolor_has_no_xdata) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        hdxf::Drawing dwg(v);
        hdxf::Color c = hdxf::Color::from_rgb(0xC8, 0x64, 0x20);
        c.name = "RAL CLASSIC$RAL 1006";
        dwg.add_line({0, 0, 0}, {1, 1, 0}).props.color = c;
        std::vector<Record> recs = records_of(save(dwg));
        bool found = false, xdata = false;
        for (const auto& r : recs) {
            if (r[0].second != "DBCOLOR") continue;
            found = true;
            for (const auto& g : r) xdata = xdata || g.second == "DBColXD";
            hdxf_test::assert_eq(value(r, 420), std::to_string(0xC86420), "the color's RGB");
            hdxf_test::assert_eq(value(r, 430), "RAL CLASSIC$RAL 1006", "the name");
        }
        hdxf_test::assert_true(found, "a DBCOLOR");
        hdxf_test::assert_eq(xdata, v != hdxf::DxfVersion::R2018, std::string(hdxf::acadver_string(v)) + ": DBColXD");
    }
}

// LIGHT, SUN and VISUALSTYLE lose only the XDATA that says the version that saved them, and in 2018
// (B77): any other ACAD XDATA of theirs stays, and they have it unchanged in the version they were read in
TEST(objects_2018_saved_by_version_xdata) {
    hdxf::XdataList saved_by;
    saved_by.app_name = "ACAD";
    saved_by.items = {{1000, "AcDbSavedByObjectVersion"}, {1070, "0"}};
    hdxf::XdataList own;
    own.app_name = "ACAD";
    own.items = {{1000, "MY_OWN"}, {1070, "7"}};
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018}) {
        hdxf::Drawing dwg(v);
        hdxf::LightData light;
        light.name = "L";
        hdxf::Entity e;
        e.type = hdxf::EntityType::Light;
        e.data = light;
        e.props.xdata = {saved_by, own};
        dwg.entities().push_back(e);
        hdxf::Object sun;
        sun.type = hdxf::ObjectType::Sun;
        sun.data = hdxf::SunData{};
        sun.owner = "0";
        sun.xdata = {saved_by, own};
        dwg.objects().push_back(sun);
        hdxf::Object style;
        style.type = hdxf::ObjectType::VisualStyle;
        hdxf::VisualStyleData vs;           // the newest form, no properties
        vs.form = hdxf::VisualStyleForm::Form2013;
        vs.description = "MyStyle";
        vs.style_type = 4;
        vs.format_version = 3;
        style.data = hdxf::Boxed<hdxf::VisualStyleData>(vs);
        style.owner = "0";
        style.xdata = {saved_by, own};
        dwg.objects().push_back(style);
        // (a SUN and a VISUALSTYLE with an owner of "0" are enough for this)
        std::vector<Record> recs = records_of(save(dwg));
        int saved = 0, mine = 0;
        for (const auto& r : recs) {
            if (r[0].second != "LIGHT" && r[0].second != "SUN" && r[0].second != "VISUALSTYLE") continue;
            bool s = false, m = false;
            for (const auto& g : r) {
                s = s || g.second == "AcDbSavedByObjectVersion";
                m = m || g.second == "MY_OWN";
            }
            if (s) ++saved;
            if (m) ++mine;
        }
        bool r2018 = v == hdxf::DxfVersion::R2018;
        hdxf_test::assert_eq(saved, r2018 ? 0 : 3, std::string(hdxf::acadver_string(v)) + ": the SavedBy XDATA");
        hdxf_test::assert_eq(mine, 3, std::string(hdxf::acadver_string(v)) + ": the XDATA of their own");
    }
}
