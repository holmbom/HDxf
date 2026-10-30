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
// MULTILEADER and MLEADERSTYLE (phase 3, step 3.3 — plan B36, B37): read from AutoCAD's files
// (ACadSharp's 15 in 2000, 2004 and 2007, LibreDWG's Leader drawing), written anew with
// AutoCAD's groups, written back from the record template, relations kept.
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

bool has_files(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (!std::filesystem::exists(external(n))) {
            hdxf_test::skip("external test file missing: " + n);
            return false;
        }
    }
    return true;
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

// A record's groups from the subclass on, with the numbers as values (AutoCAD writes 16
// significant digits: a value read and written again may differ in the last one)
struct Group {
    int code;
    std::string text;
    double number;
};

std::vector<Group> body_of(const Record& r, const std::string& subclass) {
    std::vector<Group> out;
    bool in = false;
    for (const auto& [code, v] : r) {
        if (code == 100 && v == subclass) in = true;
        if (!in) continue;
        bool real = hdxf::internal::group_value_type(code) == hdxf::internal::GroupValueType::Double;
        out.push_back({code, real ? std::string() : v, real ? std::strtod(v.c_str(), nullptr) : 0.0});
    }
    return out;
}

bool same_body(const std::vector<Group>& a, const std::vector<Group>& b, std::string& why) {
    if (a.size() != b.size()) {
        why = std::to_string(a.size()) + " / " + std::to_string(b.size()) + " groups";
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        bool ok = a[i].code == b[i].code && a[i].text == b[i].text &&
                  std::fabs(a[i].number - b[i].number) <= 1e-15 * std::max(1.0, std::fabs(a[i].number));
        if (!ok) {
            why = "group " + std::to_string(i) + ": " + std::to_string(a[i].code) + " " + a[i].text;
            return false;
        }
    }
    return true;
}

// The first value of a code in a record
std::string value_of_text(const Record& r, int code) {
    for (const auto& [c, v] : r) if (c == code) return v;
    return {};
}

hdxf::Entity* find_entity(hdxf::Drawing& dwg, const std::string& handle) {
    for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    return nullptr;
}

std::vector<std::string> reactors_in(const Record& r) {
    std::vector<std::string> out;
    bool in = false;
    for (const auto& [code, v] : r) {
        if (code == 102) { in = v == "{ACAD_REACTORS"; continue; }
        if (in && code == 330) out.push_back(v);
    }
    return out;
}

} // namespace

// The data of AutoCAD's multileader 7B8 and its style, the same in 2000, 2004 and 2007,
// ASCII and binary
TEST(mleader_read_values) {
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_binary.dxf",
                             "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1021_binary.dxf"}) {
        if (!has_files({name})) return;
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        int count = 0;
        for (const auto& e : dwg.entities()) count += e.type == hdxf::EntityType::MLeader;
        hdxf_test::assert_eq(count, 15, name);
        hdxf::Entity* e = find_entity(dwg, "7B8");
        hdxf_test::assert_true(e && e->type == hdxf::EntityType::MLeader, "7B8");
        const hdxf::MLeaderData& d = e->as_mleader();
        const hdxf::MLeaderContext& c = d.context;
        hdxf_test::assert_eq(c.text, "MLeader text, hello!");
        auto near = [](double a, double b) { return std::fabs(a - b) < 1e-12; };
        hdxf_test::assert_true(near(c.base_point.x, 1083.681045165763), "base point");
        hdxf_test::assert_true(near(c.text_location.y, 23.34704741990185), "text location");
        hdxf_test::assert_true(near(c.text_width, 1.447121418826739), "text width");
        hdxf_test::assert_eq(c.text_style, "Standard", "context text style (340 11)");
        hdxf_test::assert_eq(static_cast<int>(c.leaders.size()), 1, "leaders");
        const hdxf::MLeaderLeader& l = c.leaders[0];
        hdxf_test::assert_true(near(l.last_point.x, 1083.321045165764), "last point");
        hdxf_test::assert_eq(l.dogleg_length, 0.36, "dogleg length");
        hdxf_test::assert_eq(static_cast<int>(l.lines.size()), 1, "lines");
        hdxf_test::assert_true(near(l.lines[0].vertices.at(0).x, 1050.241562773277), "arrowhead");
        hdxf_test::assert_eq(d.style, "Standard", "style (340 D8)");
        hdxf_test::assert_eq(d.style_handle, "D8", "style handle");
        hdxf_test::assert_eq(d.override_flags, 279552, "90");
        hdxf_test::assert_eq(d.leader_linetype, "ByBlock", "341 14");
        hdxf_test::assert_eq(d.text_style, "Standard", "343 11");
        hdxf_test::assert_true(d.leader_color.raw == hdxf::CmColor::by_block().raw, "91 ByBlock");
        hdxf_test::assert_eq(d.content_type, 2, "MTEXT");
        hdxf_test::assert_eq(d.version, 2, "ACAD_MLEADERVER");
        for (const auto& x : e->props.xdata) hdxf_test::assert_true(x.app_name != "ACAD_MLEADERVER", "version taken");

        int styles = 0;
        for (const auto& o : dwg.objects()) {
            if (o.type != hdxf::ObjectType::MLeaderStyle) continue;
            ++styles;
            const hdxf::MLeaderStyleData& s = o.as_mleader_style();
            hdxf_test::assert_eq(s.text_style, "Standard", "style's 342");
            hdxf_test::assert_eq(s.leader_linetype, "ByBlock", "style's 340");
            hdxf_test::assert_eq(s.dogleg_length, 0.36, "43");
            hdxf_test::assert_eq(s.version, 2, "style's version");
            hdxf_test::assert_eq(s.annotative, o.handle == "E5", "296: E5 is Annotative");
        }
        hdxf_test::assert_eq(styles, 2, "styles");
    }
}

// Every MULTILEADER and MLEADERSTYLE of AutoCAD's files written anew (its record emptied)
// has AutoCAD's groups — the AcDbMLeader / AcDbMLeaderStyle subclass and the XDATA — in
// every version that writes them
TEST(mleader_written_anew_as_autocad) {
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                             "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1021_binary.dxf",
                             "libre/Leader_2000.dxf", "libre/Leader_2004.dxf", "libre/Leader_2007.dxf",
                             "gngeovane/Universal_Pen_Holder.dxf", "acadsharp/sample_AC1024_ascii.dxf",
                             "acadsharp/sample_AC1024_binary.dxf", "libre/Leader_2010.dxf"}) {
        if (!has_files({name})) continue;
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        std::vector<std::string> handles;
        for (auto& e : dwg.entities()) {
            if (e.type != hdxf::EntityType::MLeader) continue;
            e.raw = hdxf::RawRecord();
            handles.push_back(e.props.handle);
        }
        std::vector<std::string> styles;
        for (auto& o : dwg.objects()) {
            if (o.type != hdxf::ObjectType::MLeaderStyle) continue;
            o.raw = hdxf::RawRecord();
            styles.push_back(o.handle);
        }
        hdxf_test::assert_false(handles.empty(), name);
        bool binary = std::string(name).find("binary") != std::string::npos;
        std::vector<Record> after = records_of(save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii));
        for (const auto& h : handles) {
            std::string why;
            hdxf_test::assert_true(same_body(body_of(record(before, h), "AcDbMLeader"),
                                             body_of(record(after, h), "AcDbMLeader"), why),
                                   std::string(name) + " MULTILEADER " + h + ": " + why);
        }
        for (const auto& h : styles) {
            std::vector<Group> a = body_of(record(before, h), "AcDbMLeaderStyle");
            std::vector<Group> b = body_of(record(after, h), "AcDbMLeaderStyle");
            // The program that wrote gnGeovane's file (Solid Edge's fonts) writes null pointers (341 0, 343 0) for
            // no arrow block and no block; AutoCAD leaves them out
            a.erase(std::remove_if(a.begin(), a.end(), [](const Group& g) {
                return (g.code == 341 || g.code == 343) && g.text == "0";
            }), a.end());
            std::string why;
            hdxf_test::assert_true(same_body(a, b, why), std::string(name) + " MLEADERSTYLE " + h + ": " + why);
            // AutoCAD's styles have their multileaders as reactors; that program's do not
            if (std::string(name).rfind("gngeovane", 0) == 0) continue;
            hdxf_test::assert_true(reactors_in(record(before, h)) == reactors_in(record(after, h)),
                                   std::string(name) + " " + h + ": reactors");
        }
    }
}

// The AcCmColor values, among them gnGeovane's "none" (0xC8) of a text background
TEST(mleader_cm_color) {
    hdxf_test::assert_true(hdxf::CmColor::from_color(hdxf::Color::by_layer()).raw == static_cast<int32_t>(0xC0000000u), "ByLayer");
    hdxf_test::assert_true(hdxf::CmColor::from_color(hdxf::Color::by_block()).raw == static_cast<int32_t>(0xC1000000u), "ByBlock");
    hdxf_test::assert_true(hdxf::CmColor::from_color(hdxf::Color::from_aci(7)).raw == -1023410169, "ACI 7");
    hdxf_test::assert_true(hdxf::CmColor::from_color(hdxf::Color::from_rgb(1, 2, 3)).raw == static_cast<int32_t>(0xC2010203u), "RGB");
    hdxf::CmColor rgb{static_cast<int32_t>(0xC2367542u)};
    hdxf_test::assert_eq(rgb.color().rgb, 0x367542, "RGB back");
    hdxf_test::assert_eq(hdxf::CmColor{-1023410170}.color().aci, 6, "ACI 6");
    hdxf_test::assert_true(hdxf::CmColor::none().is_none(), "none");
    if (!has_files({"gngeovane/Universal_Pen_Holder.dxf"})) return;
    hdxf::Drawing dwg = load(read_bytes(external("gngeovane/Universal_Pen_Holder.dxf")));
    int none = 0;
    for (const auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::MLeader) none += e.as_mleader().context.background_color.is_none();
    }
    hdxf_test::assert_eq(none, 5, "the background color none");
}

// A changed multileader in AutoCAD's 2007 file: its AcDbMLeader written anew, everything else
// as it was; one added gets its style's reactor, one removed loses it (B19)
TEST(mleader_changes_in_2007_file) {
    const char* name = "acadsharp/sample_AC1021_ascii.dxf";
    if (!has_files({name})) return;
    std::vector<uint8_t> in = read_bytes(external(name));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    hdxf::Entity* e = find_entity(dwg, "7B8");
    e->as_mleader().context.text = "Ändrad text";
    hdxf::Entity copy = *find_entity(dwg, "B1A");
    copy.raw = hdxf::RawRecord();
    copy.props.handle.clear();
    copy.as_mleader().context.base_point.x += 10.0;
    dwg.add_entity(copy);
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& x) { return x.props.handle == "B1B"; }),
               ents.end());
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "changed multileaders");
    std::vector<Record> after = records_of(out);
    std::string added = dwg.entities().back().props.handle;

    Record changed = record(after, "7B8");
    bool text = false;
    for (const auto& [code, v] : changed) text = text || (code == 304 && v == "Ändrad text");
    hdxf_test::assert_true(text, "the new text");
    std::string why;
    std::vector<Group> a = body_of(record(before, "7B8"), "AcDbMLeader");
    std::vector<Group> b = body_of(changed, "AcDbMLeader");
    hdxf_test::assert_eq(static_cast<int>(b.size()), static_cast<int>(a.size()), "the same groups");
    // D8 (Standard) has its multileaders as reactors: without B1B, with the new one last
    std::vector<std::string> was = reactors_in(record(before, "D8"));
    std::vector<std::string> now = reactors_in(record(after, "D8"));
    std::vector<std::string> expected;
    for (const auto& r : was) if (r != "B1B") expected.push_back(r);
    expected.push_back(added);
    hdxf_test::assert_true(now == expected, "the style's reactors");
    // The class counts as read (B24); the new MULTILEADER is written with the version XDATA
    Record fresh = record(after, added);
    hdxf_test::assert_true(fresh.size() > 3 && fresh[fresh.size() - 2].second == "ACAD_MLEADERVER",
                           "ACAD_MLEADERVER");
    hdxf::Drawing back = load(out);
    int count = 0;
    for (const auto& x : back.entities()) count += x.type == hdxf::EntityType::MLeader;
    hdxf_test::assert_eq(count, 15, "multileaders read back");
}

// A new drawing's MLEADERSTYLE as AutoCAD 2017 makes one (jscad's empty2007.dxf): the same
// groups, the round-trip XRECORD in its extension dictionary; a multileader in it can be written
// as 2000, 2004 and 2007, not as R12
TEST(mleader_style_new_drawing) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007}) {
        hdxf::Drawing dwg(v);
        hdxf::Object& style = dwg.add_mleader_style("Standard");
        hdxf_test::assert_true(&dwg.add_mleader_style("Standard") == &style, "the same style again");
        std::vector<Record> recs = records_of(save(dwg));
        Record r = record(recs, style.handle);
        std::vector<int> classes;
        for (const auto& c : recs) {
            if (c[0].second == "CLASS" && c.size() > 1 && c[1].second == "MLEADERSTYLE") classes.push_back(1);
        }
        hdxf_test::assert_eq(static_cast<int>(classes.size()), 1, "class");
        if (has_files({"jscad/empty2007.dxf"})) {
            std::vector<Record> autocad = records_of(read_bytes(external("jscad/empty2007.dxf")));
            Record theirs;
            for (const auto& x : autocad) if (x[0].second == "MLEADERSTYLE") theirs = x;
            std::string why;
            std::vector<Group> a = body_of(theirs, "AcDbMLeaderStyle");
            std::vector<Group> b = body_of(r, "AcDbMLeaderStyle");
            // The handles differ: 340 (ByBlock) and 342 (Standard) are compared as present
            for (auto& g : a) if (g.code == 340 || g.code == 342) g.text = "h";
            for (auto& g : b) if (g.code == 340 || g.code == 342) g.text = "h";
            hdxf_test::assert_true(same_body(a, b, why), "AutoCAD's Standard: " + why);
        }
        std::string xdict;
        bool in = false;
        for (const auto& [code, val] : r) {
            if (code == 102) in = val == "{ACAD_XDICTIONARY";
            if (in && code == 360) xdict = val;
        }
        Record dict = record(recs, xdict);
        std::string xrec;
        for (const auto& [code, val] : dict) if (code == 360) xrec = val;
        Record roundtrip = record(recs, xrec);
        hdxf_test::assert_true(roundtrip.size() >= 2 && roundtrip[roundtrip.size() - 2].second == "ACAD_ROUNDTRIP_2012_MLEADER_STYLE" &&
                                   roundtrip.back() == std::pair<int, std::string>{298, "1"},
                               "the round-trip XRECORD");
    }
}

// A drawing read from a file whose multileader's and style's blocks, text style and linetype are
// removed: their pointers become 0, what AutoCAD has for none (B19, B36) — the save does not
// fail; a name the drawing never had is still an error
TEST(mleader_removed_references) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2007);
    hdxf::Block& arrow = dwg.add_block("PIL");
    arrow.entities.push_back(hdxf::Entity{});
    arrow.entities.back().type = hdxf::EntityType::Line;
    arrow.entities.back().data = hdxf::LineData{{-1, 0, 0}, {0, 0, 0}};
    hdxf::Block& balloon = dwg.add_block("BALLONG");
    balloon.entities.push_back(hdxf::Entity{});
    balloon.entities.back().type = hdxf::EntityType::Circle;
    hdxf::CircleData cd;
    cd.radius = 1.0;
    balloon.entities.back().data = cd;
    dwg.add_text_style("Notes", "txt");
    hdxf::Linetype dashed;
    dashed.name = "DASHED";
    dashed.description = "__ __";
    dashed.pattern = {0.5, -0.25};
    dashed.total_length = 0.75;
    dwg.linetypes().push_back(dashed);
    hdxf::Object& style = dwg.add_mleader_style("Pilar");
    style.as_mleader_style().arrow_block = "PIL";
    style.as_mleader_style().block = "BALLONG";
    style.as_mleader_style().text_style = "Notes";
    style.as_mleader_style().leader_linetype = "DASHED";
    hdxf::Entity& text = dwg.add_mleader({{0, 0, 0}, {4, 3, 0}}, "Text", "Pilar");
    hdxf::MLeaderData& t = text.as_mleader();
    t.arrowheads.push_back({0, "PIL"});
    t.context.text_style = "Notes";
    dwg.add_mleader_block({{10, 0, 0}, {14, 3, 0}}, "BALLONG", "Pilar");
    hdxf::Drawing back;
    {
        std::vector<uint8_t> out = save(dwg);
        back = load(out);
    }
    auto& blocks = back.blocks();
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(),
                                [](const hdxf::Block& b) { return b.name == "PIL" || b.name == "BALLONG"; }),
                 blocks.end());
    auto& styles = back.text_styles();
    styles.erase(std::remove_if(styles.begin(), styles.end(), [](const hdxf::TextStyle& s) { return s.name == "Notes"; }),
                 styles.end());
    auto& lts = back.linetypes();
    lts.erase(std::remove_if(lts.begin(), lts.end(), [](const hdxf::Linetype& l) { return l.name == "DASHED"; }),
              lts.end());
    std::vector<uint8_t> out = back.save_to_memory();
    hdxf_test::assert_false(out.empty(), "saved with the references removed: " + back.last_error());
    dxf_structure::check_structure(out, "references removed");
    std::vector<Record> recs = records_of(out);
    int mleaders = 0, styles_seen = 0;
    for (const auto& r : recs) {
        if (r[0].second == "MULTILEADER") {
            ++mleaders;
            bool after = false;
            for (const auto& [code, v] : r) {
                if (code == 301) after = true;
                // 342/345 the arrowheads, 344 the content block, 341 the linetype, 343 the text style
                if (after && (code == 341 || code == 342 || code == 343 || code == 344 || code == 345)) {
                    hdxf_test::assert_eq(v, std::string("0"), "MULTILEADER " + std::to_string(code));
                }
            }
        }
        // The style with a block (343): Pilar
        auto value_of = [&](int code) {
            for (const auto& [c, v] : r) if (c == code) return v;
            return std::string();
        };
        if (r[0].second == "MLEADERSTYLE" && !value_of(343).empty()) {
            ++styles_seen;
            for (int code : {340, 341, 342, 343}) {
                hdxf_test::assert_eq(value_of(code), std::string("0"), "MLEADERSTYLE " + std::to_string(code));
            }
        }
    }
    hdxf_test::assert_eq(mleaders, 2, "multileaders");
    hdxf_test::assert_eq(styles_seen, 1, "the style Pilar");

    // A name the drawing does not have, newly given (a new multileader's arrowhead): an error, as
    // before
    back.add_mleader({{0, 10, 0}, {2, 12, 0}}, "Ny").as_mleader().arrow_block = "FINNS_INTE";
    hdxf_test::assert_true(back.save_to_memory().empty(), "an unknown block saved");
    hdxf_test::assert_true(back.last_error().find("block \"FINNS_INTE\" does not exist") != std::string::npos,
                           back.last_error());

    // A read multileader whose removed arrowhead is replaced by a name the file never had: an
    // error too, not a removal
    back.entities().pop_back();
    for (auto& e : back.entities()) {
        if (e.type != hdxf::EntityType::MLeader) continue;
        for (auto& a : e.as_mleader().arrowheads) a.block = "OKÄND";
    }
    hdxf_test::assert_true(back.save_to_memory().empty(), "an unknown block in place of a removed one saved");
    hdxf_test::assert_true(back.last_error().find("block \"OKÄND\" does not exist") != std::string::npos,
                           back.last_error());
}

// Saving a drawing with a multileader as R12 fails (B11)
TEST(mleader_refused_as_r12) {
    hdxf::Drawing dwg;
    hdxf::Entity e;
    e.type = hdxf::EntityType::MLeader;
    e.data = hdxf::MLeaderData{};
    dwg.add_entity(e);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "saved as R12");
    hdxf_test::assert_eq(dwg.last_error(), "MULTILEADER requires DXF 2000 or later");
}

// ---------------------------------------------------------------------------
// MULTILEADER in 2010 (phase 4, step 4.3 — plan B47)
// ---------------------------------------------------------------------------

// A leader line's own properties (2010): read and written after 91 in AutoCAD's order (170, 92,
// 340, 171, 40, 341, 93), those it has — none for a line without; the linetype and arrowhead by
// name; refused as 2007
TEST(mleader_2010_line_properties) {
    auto build = [](hdxf::DxfVersion v) {
        hdxf::Drawing dwg(v);
        dwg.add_linetype("STRECK", "Streck", {1.0, -0.5});
        dwg.add_block("_Dot");
        hdxf::Entity& m = dwg.add_mleader({{0, 0, 0}, {3, 2, 0}}, "Text");
        hdxf::MLeaderLine& line = m.as_mleader().context.leaders.at(0).lines.at(0);
        line.leader_type = 1;
        line.color = hdxf::CmColor::from_color(hdxf::Color::from_aci(1));
        line.linetype = "STRECK";
        line.lineweight = 35;
        line.arrow_size = 0.25;
        line.arrow_block = "_Dot";
        line.override_flags = 2 | 4 | 32;
        dwg.add_mleader({{10, 0, 0}, {13, 2, 0}}, "Utan");
        return dwg;
    };
    hdxf::Drawing dwg = build(hdxf::DxfVersion::R2010);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "2010 line properties");
    std::vector<Record> recs = records_of(out);
    int lines = 0;
    for (const auto& r : recs) {
        if (r[0].second != "MULTILEADER") continue;
        for (size_t i = 0; i < r.size(); ++i) {
            if (r[i] != Record::value_type{304, "LEADER_LINE{"}) continue;
            size_t k = i;
            while (r[k].first != 91) ++k;
            std::vector<int> after;
            for (size_t j = k + 1; r[j].first != 305; ++j) after.push_back(r[j].first);
            ++lines;
            bool own = value_of_text(r, 304) == "Text";
            hdxf_test::assert_true(own ? after == std::vector<int>({170, 92, 340, 171, 40, 341, 93}) : after.empty(),
                                   own ? "the seven after 91" : "none");
        }
    }
    hdxf_test::assert_eq(lines, 2, "lines");
    hdxf::Drawing back = load(out);
    const hdxf::MLeaderLine& line = back.entities().at(0).as_mleader().context.leaders.at(0).lines.at(0);
    hdxf_test::assert_true(line.linetype && *line.linetype == "STRECK", "linetype by name");
    hdxf_test::assert_true(line.arrow_block && *line.arrow_block == "_Dot", "arrowhead by name");
    hdxf_test::assert_true(line.override_flags && *line.override_flags == 38, "93");
    hdxf_test::assert_true(line.arrow_size && *line.arrow_size == 0.25 && line.lineweight && *line.lineweight == 35, "40, 171");
    const hdxf::MLeaderLine& plain = back.entities().at(1).as_mleader().context.leaders.at(0).lines.at(0);
    hdxf_test::assert_false(plain.color.has_value() || plain.linetype.has_value(), "none read");
    // Written back from the record: the same; a removed linetype a null pointer (B19)
    hdxf_test::assert_true(records_of(save(back)) == recs, "round trip");
    auto& lts = back.linetypes();
    lts.erase(std::remove_if(lts.begin(), lts.end(), [](const hdxf::Linetype& l) { return l.name == "STRECK"; }), lts.end());
    back.entities().at(0).as_mleader().context.leaders.at(0).lines.at(0).lineweight = 50;   // written anew
    std::vector<Record> again = records_of(save(back));
    for (const auto& r : again) {
        if (r[0].second != "MULTILEADER" || value_of_text(r, 304) != "Text") continue;
        auto at = std::find(r.begin(), r.end(), Record::value_type{171, "50"});
        hdxf_test::assert_true(at != r.end() && (at - 1)->first == 340 && (at - 1)->second == "0", "340 0");
    }

    hdxf::Drawing old = build(hdxf::DxfVersion::R2007);
    hdxf_test::assert_true(old.save_to_memory().empty(), "2007");
    hdxf_test::assert_eq(old.last_error(), "A MULTILEADER leader line's own properties requires DXF 2010 or later");
}

// A new MULTILEADER with a vertically attached style (2010, B47): its leader attached
// vertically, no landing in its graphics, the text centered over the leader's last point the
// gap above it (as AutoCAD placed ACadSharp's B27 and Oxcart's 148); refused as 2007
TEST(mleader_2010_vertical_attachment) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    hdxf::MLeaderStyleData& s = dwg.add_mleader_style("Lodrät").as_mleader_style();
    s.text_attachment_direction = 1;
    s.text_alignment = 1;
    hdxf::Entity& e = dwg.add_mleader({{0, 0, 0}, {2, 5, 0}}, "Text", "Lodrät");
    const hdxf::MLeaderData& d = e.as_mleader();
    const hdxf::MLeaderContext& c = d.context;
    hdxf_test::assert_eq(c.leaders.at(0).attachment_direction, 1, "271 of the leader");
    hdxf_test::assert_eq(d.text_attachment_direction, 1, "271");
    hdxf_test::assert_eq(c.text_attachment, 2, "top center");
    hdxf_test::assert_true(c.base_point.x == 2 && c.base_point.y == 5, "the base point the last point");
    hdxf::Entity m;
    m.type = hdxf::EntityType::MText;
    hdxf::MTextData t;
    t.content = c.text;
    t.height = c.text_height;
    t.attachment = c.text_attachment;
    m.data = t;
    double w = 0, h = 0;
    if (dwg.mtext_size(m, w, h)) {
        hdxf_test::assert_true(std::fabs(c.text_location.x - 2) < 1e-12, "centered");
        hdxf_test::assert_true(std::fabs(c.text_location.y - (5 + c.landing_gap + h)) < 1e-12, "the gap above");
    }
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "vertical MULTILEADER");
    for (const auto& r : records_of(out)) {
        if (r[0].second != "MULTILEADER") continue;
        auto at = std::find(r.begin(), r.end(), Record::value_type{305, "}"});
        hdxf_test::assert_true(at != r.end() && *(at + 1) == Record::value_type{271, "1"}, "271 1 in LEADER{}");
    }
    dwg.set_version(hdxf::DxfVersion::R2007);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "2007");
    hdxf_test::assert_eq(dwg.last_error(), "A multileader style's vertical attachment requires DXF 2010 or later");
}

// ---------------------------------------------------------------------------
// A MULTILEADER without a leader linetype (faciträttning F4): AutoCAD leaves group 341 out of the
// common part, and so does the writer; a linetype that is named must exist
// ---------------------------------------------------------------------------

namespace {

// The lines of a file, each with its line ending
std::vector<std::string> file_lines(const std::vector<uint8_t>& data) {
    std::string text(data.begin(), data.end());
    std::vector<std::string> lines;
    size_t from = 0;
    while (from < text.size()) {
        size_t end = text.find('\n', from);
        end = end == std::string::npos ? text.size() : end + 1;
        lines.push_back(text.substr(from, end - from));
        from = end;
    }
    return lines;
}

std::string trimmed_line(const std::string& line) {
    size_t a = line.find_first_not_of(' ');
    size_t b = line.find_last_not_of(" \r\n");
    return a == std::string::npos ? std::string() : line.substr(a, b - a + 1);
}

// The line of the first group `code` in the first MULTILEADER record, or the file's size
size_t multileader_group(const std::vector<std::string>& lines, const std::string& code) {
    size_t i = 0;
    while (i + 1 < lines.size() && !(trimmed_line(lines[i]) == "0" && trimmed_line(lines[i + 1]) == "MULTILEADER")) i += 2;
    for (i += 2; i + 1 < lines.size() && trimmed_line(lines[i]) != "0"; i += 2) {
        if (trimmed_line(lines[i]) == code) return i;
    }
    return lines.size();
}

} // namespace

TEST(mleader_without_linetype_has_no_341) {
    for (hdxf::DxfVersion version : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010}) {
        // The multileader as written now has 341 (the style's ByBlock)
        hdxf::Drawing dwg(version);
        dwg.add_mleader({{10, 10, 0}, {5, 5, 0}}, "Text");
        std::vector<uint8_t> file = save(dwg);
        std::vector<std::string> lines = file_lines(file);
        size_t at = multileader_group(lines, "341");
        hdxf_test::assert_true(at < lines.size(), "341 in the multileader");

        // AutoCAD's multileader 3C9 has none
        lines.erase(lines.begin() + static_cast<long>(at), lines.begin() + static_cast<long>(at) + 2);
        std::string autocad;
        for (const auto& l : lines) autocad += l;
        std::vector<uint8_t> autocad_bytes(autocad.begin(), autocad.end());
        hdxf::Drawing read = load(autocad_bytes);
        const hdxf::Entity* mleader = nullptr;
        for (const auto& e : read.entities()) if (e.type == hdxf::EntityType::MLeader) mleader = &e;
        hdxf_test::assert_true(mleader != nullptr, "read");
        hdxf_test::assert_true(mleader->as_mleader().leader_linetype.empty(), "no linetype");
        hdxf_test::assert_true(save(read) == autocad_bytes, "written as it was");

        // One made with no linetype is written without 341 too
        hdxf::Drawing made(version);
        hdxf::Entity& fresh = made.add_mleader({{10, 10, 0}, {5, 5, 0}}, "Text");
        fresh.as_mleader().leader_linetype.clear();
        std::vector<std::string> made_lines = file_lines(save(made));
        hdxf_test::assert_true(multileader_group(made_lines, "341") == made_lines.size(), "no 341");

        // A linetype that is named but does not exist is still an error
        hdxf::Entity& bad = made.add_mleader({{10, 10, 0}, {5, 5, 0}}, "Text");
        bad.as_mleader().leader_linetype = "NO_SUCH_LINETYPE";
        hdxf_test::assert_true(made.save_to_memory().empty(), "an unknown linetype fails");
        hdxf_test::assert_true(made.last_error().find("NO_SUCH_LINETYPE") != std::string::npos, made.last_error());
    }
}
