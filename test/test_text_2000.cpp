#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// ---------------------------------------------------------------------------
// Text and annotation in DXF 2000, phase 1 step 1.6 (doc/plans/2026-10-23-fas-1-dxf-2000.md):
// MTEXT (its text in parts of 250 characters, B16), TOLERANCE and LEADER — read, built
// in memory, written as 2000, and a drawing read from 2000 changed and written back.
//
// Expected values are read from ACadSharp's AutoCAD sample (checked with ezdxf, which
// does not decode \U+ and ^J — HDxf does, plan B1), not from HDxf.
// ---------------------------------------------------------------------------

namespace {

using Groups = std::vector<std::pair<int, std::string>>;

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

const hdxf::Entity& entity(const hdxf::Drawing& dwg, const std::string& handle) {
    for (const auto& e : dwg.entities()) {
        if (e.props.handle == handle) return e;
    }
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) {
            if (e.props.handle == handle) return e;
        }
    }
    hdxf_test::assert_true(false, "entity " + handle + " not found");
    return dwg.entities().front();
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

// The groups of the record with this handle, from its "0" on (ASCII)
Groups record(const std::vector<uint8_t>& data, const std::string& handle) {
    std::string dxf(data.begin(), data.end());
    size_t pos = 0;
    auto line = [&]() {
        size_t end = dxf.find('\n', pos);
        std::string s = dxf.substr(pos, end - pos);
        pos = end == std::string::npos ? dxf.size() : end + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        return s;
    };
    Groups current;
    bool found = false;
    while (pos < dxf.size()) {
        int code = std::atoi(line().c_str());
        std::string value = line();
        if (code == 0) {
            if (found) break;
            current.clear();
        }
        current.push_back({code, dxf_structure::number_text(code, value)});
        if (code == 5 && value == handle && current.size() == 2) found = true;
    }
    return found ? current : Groups{};
}

std::vector<int> codes_of(const Groups& g, size_t from = 0) {
    std::vector<int> out;
    for (size_t i = from; i < g.size(); ++i) out.push_back(g[i].first);
    return out;
}

// The values of a code in a record, in order
std::vector<std::string> values(const Groups& g, int code) {
    std::vector<std::string> out;
    for (const auto& [c, v] : g) if (c == code) out.push_back(v);
    return out;
}

bool has_sample() {
    if (std::filesystem::exists(external("acadsharp/sample_AC1015_ascii.dxf"))) return true;
    hdxf_test::skip("external test file missing");
    return false;
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// paragraph: how the file breaks a line — AutoCAD wrote \P in 2000 and ^J (a line feed)
// in 2018
void check_sample(const std::string& file, const std::string& paragraph) {
    std::filesystem::path path = external(file);
    if (!std::filesystem::exists(path)) {
        hdxf_test::skip("external test file missing");
        return;
    }
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(path), dwg.last_error());

    // 13 parts of 250 characters and a last one: one string of 3436 (2000)
    const hdxf::Entity& long_text = entity(dwg, "779");
    hdxf_test::assert_true(long_text.type == hdxf::EntityType::MText, "MTEXT");
    const auto& m = long_text.as_mtext();
    hdxf_test::assert_eq(static_cast<int>(m.content.size()), 3434 + static_cast<int>(paragraph.size()),
                         "joined");
    std::string start = "this is a really long Mtext" + paragraph + "bl";
    hdxf_test::assert_eq(m.content.substr(0, start.size()), start);
    hdxf_test::assert_eq(m.content.substr(m.content.size() - 8), " bla bla");
    hdxf_test::assert_true(std::fabs(m.insertion_point.x - 183.5889280790413) < 1e-12, "insertion");
    hdxf_test::assert_eq(m.height, 1.0);
    hdxf_test::assert_true(std::fabs(m.reference_width - 15.74141424498748) < 1e-12, "width");
    hdxf_test::assert_eq(m.attachment, 1);
    hdxf_test::assert_eq(m.drawing_direction, 5);
    hdxf_test::assert_eq(m.style, "Standard");
    hdxf_test::assert_eq(m.line_spacing_style, 1);
    hdxf_test::assert_eq(m.line_spacing_factor, 1.0);
    hdxf_test::assert_eq(m.x_axis_direction.x, 1.0);

    // \U+2205 is ∅; 11 is the direction (here 45°)
    const auto& diameter = entity(dwg, "64B").as_mtext();
    hdxf_test::assert_eq(diameter.content, "\xE2\x88\x85" "45,6");
    const auto& rotated = entity(dwg, "662").as_mtext();
    hdxf_test::assert_true(std::fabs(rotated.x_axis_direction.x - 0.7071067811865479) < 1e-12, "11");
    hdxf_test::assert_true(std::fabs(rotated.x_axis_direction.y - 0.7071067811865472) < 1e-12, "21");
    hdxf_test::assert_eq(entity(dwg, "3EC").as_mtext().content,
                         "this is a Mtext" + paragraph + "with multiple lines in it");

    const hdxf::Entity& leader = entity(dwg, "512");
    hdxf_test::assert_true(leader.type == hdxf::EntityType::Leader, "LEADER");
    const auto& l = leader.as_leader();
    hdxf_test::assert_eq(l.dim_style, "ISO-25");
    hdxf_test::assert_true(l.arrowhead, "71 left out: an arrowhead");
    hdxf_test::assert_eq(l.path_type, 0);
    hdxf_test::assert_eq(l.annotation_type, 0, "MTEXT");
    hdxf_test::assert_eq(l.hookline_direction, 0);
    hdxf_test::assert_true(l.hookline, "hookline");
    hdxf_test::assert_true(std::fabs(l.text_height - 1.844821309997272) < 1e-12, "40");
    hdxf_test::assert_true(std::fabs(l.text_width - 16.37400410337938) < 1e-12, "41");
    hdxf_test::assert_eq(static_cast<int>(l.vertices.size()), 4);
    hdxf_test::assert_true(std::fabs(l.vertices[1].y - 48.53878603809858) < 1e-12, "vertex");
    hdxf_test::assert_true(std::fabs(l.vertices[3].x - 740.3987500252849) < 1e-12, "last vertex");
    hdxf_test::assert_eq(l.annotation_handle, "513");
    hdxf_test::assert_eq(entity(dwg, "513").as_mtext().content, "Sample annotation");
    hdxf_test::assert_eq(entity(dwg, "513").as_mtext().attachment, 7);

    // ^J (in binary: the byte) is a line break
    const hdxf::Entity& tolerance = entity(dwg, "822");
    hdxf_test::assert_true(tolerance.type == hdxf::EntityType::Tolerance, "TOLERANCE");
    hdxf_test::assert_eq(tolerance.as_tolerance().dim_style, "Standard");
    hdxf_test::assert_eq(tolerance.as_tolerance().content,
                         "{\\Fgdt;j}%%v{\\Fgdt;n}%%v%%v%%v%%v\n{\\Fgdt;d}%%v%%v%%v%%v%%v\n.5\n5");
    hdxf_test::assert_true(std::fabs(tolerance.as_tolerance().insertion_point.y + 4.320437747060623) < 1e-12,
                           "insertion");
    hdxf_test::assert_eq(entity(dwg, "823").as_tolerance().content, "99\na");
}

} // namespace

TEST(text_2000_read_ascii)  { check_sample("acadsharp/sample_AC1015_ascii.dxf", "\\P"); }
TEST(text_2000_read_binary) { check_sample("acadsharp/sample_AC1015_binary.dxf", "\\P"); }
TEST(text_2000_read_2018)   { check_sample("acadsharp/sample_AC1032_ascii.dxf", "\n"); }

// B16: the parts are at most 250 characters as written, split between characters —
// never inside a \U+XXXX or a UTF-8 sequence — and read back as one string
TEST(text_2000_mtext_parts) {
    const std::string a_ring = "\xC3\xA5";             // å: \U+00E5, 7 characters as written
    const std::string snowman = "\xE2\x98\x83";        // ☃: \U+2603
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        // 247 + 7 > 250: å starts the second part; then a caret (^ is "^ " in ASCII) and
        // a line feed (^J) at the second boundary
        std::string content = std::string(247, 'a') + a_ring + std::string(240, 'b') + snowman +
                              "^\n" + std::string(300, 'c') + "\\Pend";
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        dwg.add_mtext({1, 2, 0}, content, 3.5);
        std::vector<uint8_t> out = save(dwg, format);
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(back.entities()[0].as_mtext().content, content, "read back whole");
        if (format == hdxf::DxfFormat::Binary) continue;

        Groups g = record(out, back.entities()[0].props.handle);
        std::vector<std::string> parts = values(g, 3);
        std::string last = values(g, 1).at(0);
        hdxf_test::assert_eq(static_cast<int>(parts.size()), 3, "three parts of 3, then 1");
        hdxf_test::assert_eq(parts[0], std::string(247, 'a'), "å not split");
        hdxf_test::assert_eq(parts[1].substr(0, 7), "\\U+00E5");
        for (const auto& p : parts) hdxf_test::assert_true(p.size() <= 250, "at most 250");
        hdxf_test::assert_true(last.size() < 250, "the last part in 1");
        // The second part: å (7) + 240 b = 247, ☃ (7) does not fit
        hdxf_test::assert_eq(static_cast<int>(parts[1].size()), 247);
        hdxf_test::assert_eq(parts[2].substr(0, 11), "\\U+2603^ ^J");
        // 1 is the last group of the text, 3 before it
        size_t one = 0, last_three = 0;
        for (size_t i = 0; i < g.size(); ++i) {
            if (g[i].first == 1) one = i;
            if (g[i].first == 3) last_three = i;
        }
        hdxf_test::assert_eq(static_cast<int>(one), static_cast<int>(last_three) + 1);
    }
    // A text of exactly 250 characters is one group 1
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_mtext({0, 0, 0}, std::string(250, 'x'));
    std::vector<uint8_t> out = save(dwg);
    Groups g = record(out, load(out).entities()[0].props.handle);
    hdxf_test::assert_true(values(g, 3).empty(), "no 3");
    hdxf_test::assert_eq(static_cast<int>(values(g, 1).at(0).size()), 250);
}

// A file whose writer split a \U+XXXX or a UTF-8 character between two parts: the parts
// are joined before they are decoded (B16)
TEST(text_2000_mtext_split_inside_escape) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_mtext({0, 0, 0}, "PLACEHOLDER");
    std::vector<uint8_t> out = save(dwg);
    std::string dxf = hdxf_test::lf_text(out);
    size_t at = dxf.find("  1\nPLACEHOLDER\n");
    hdxf_test::assert_true(at != std::string::npos, "the text group");
    dxf.replace(at, std::string("  1\nPLACEHOLDER\n").size(), "  3\nab\\U+00\n  3\nE5c\n  1\nd\n");
    hdxf::Drawing back = load(std::vector<uint8_t>(dxf.begin(), dxf.end()));
    hdxf_test::assert_eq(back.entities()[0].as_mtext().content, "ab\xC3\xA5" "cd");
}

// A rotation (50, radians) instead of a direction reads as the direction it gives
TEST(text_2000_mtext_rotation) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_mtext({0, 0, 0}, "R");
    std::vector<uint8_t> out = save(dwg);
    std::string dxf = hdxf_test::lf_text(out);
    size_t at = dxf.find(" 73\n", dxf.find("AcDbMText"));
    dxf.insert(at, " 50\n1.5707963267948966\n");
    const auto& m = load(std::vector<uint8_t>(dxf.begin(), dxf.end())).entities()[0].as_mtext();
    hdxf_test::assert_true(std::fabs(m.x_axis_direction.x) < 1e-15, "x");
    hdxf_test::assert_true(std::fabs(m.x_axis_direction.y - 1.0) < 1e-15, "y");
}

// Built in memory: the groups in AutoCAD's order, defaults left out as AutoCAD does; a
// leader's annotation has the leader as reactor
TEST(text_2000_built) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
        hdxf::Entity& note = dwg.add_mtext({10, 20, 0}, "Rum 1\\Påäö", 2.5, "Text");
        note.as_mtext().reference_width = 30.0;
        note.as_mtext().attachment = 7;
        note.as_mtext().style = "Notes";
        note.as_mtext().x_axis_direction = {0, 1, 0};
        dwg.add_text_style("Notes", "txt");
        hdxf::Entity& frame = dwg.add_tolerance({0, 0, 0}, "{\\Fgdt;j}%%v0.1\n{\\Fgdt;n}");
        frame.as_tolerance().dim_style = "Standard";
        hdxf::Entity* annotation = &dwg.entities()[0];
        hdxf::Entity& leader = dwg.add_leader({{0, 0, 0}, {5, 5, 0}, {8, 5, 0}}, annotation);
        leader.as_leader().hookline = true;
        std::string note_handle = dwg.entities()[0].props.handle;
        hdxf_test::assert_false(note_handle.empty(), "the annotation got a handle");
        hdxf_test::assert_eq(leader.as_leader().annotation_handle, note_handle);
        hdxf_test::assert_eq(leader.as_leader().annotation_type, 0);
        hdxf_test::assert_eq(leader.as_leader().text_height, 2.5);
        hdxf_test::assert_eq(leader.as_leader().text_width, 30.0);
        dwg.add_leader({{20, 0, 0}, {25, 5, 0}}).as_leader().arrowhead = false;

        std::vector<uint8_t> out = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "text and annotation");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_eq(static_cast<int>(back.entities().size()), 4);
        const auto& m = back.entities()[0].as_mtext();
        hdxf_test::assert_eq(m.content, "Rum 1\\Påäö");
        hdxf_test::assert_eq(m.reference_width, 30.0);
        hdxf_test::assert_eq(m.attachment, 7);
        hdxf_test::assert_eq(m.style, "Notes");
        hdxf_test::assert_eq(m.x_axis_direction.y, 1.0);
        hdxf_test::assert_eq(back.entities()[0].props.layer, "Text");
        hdxf_test::assert_eq(back.entities()[1].as_tolerance().content, "{\\Fgdt;j}%%v0.1\n{\\Fgdt;n}");
        const auto& l = back.entities()[2].as_leader();
        hdxf_test::assert_eq(l.annotation_handle, note_handle);
        hdxf_test::assert_eq(static_cast<int>(l.vertices.size()), 3);
        hdxf_test::assert_eq(l.vertices[1].x, 5.0);
        hdxf_test::assert_true(l.hookline, "hookline");
        hdxf_test::assert_true(l.arrowhead, "arrowhead");
        hdxf_test::assert_false(back.entities()[3].as_leader().arrowhead, "no arrowhead");
        hdxf_test::assert_eq(back.entities()[3].as_leader().annotation_type, 3, "no annotation");
        if (format == hdxf::DxfFormat::Binary) continue;

        Groups mtext = record(out, note_handle);
        std::vector<int> expected_mtext = {0, 5, 102, 330, 102, 330, 100, 8, 100, 10, 20, 30, 40, 41,
                                           71, 72, 1, 7, 11, 21, 31, 73, 44};
        hdxf_test::assert_true(codes_of(mtext) == expected_mtext, "MTEXT groups");
        hdxf_test::assert_eq(mtext[2].second, "{ACAD_REACTORS");
        hdxf_test::assert_eq(mtext[3].second, back.entities()[2].props.handle, "reactor: the leader");
        hdxf_test::assert_eq(values(mtext, 1).at(0), "Rum 1\\P\\U+00E5\\U+00E4\\U+00F6");

        Groups tol = record(out, back.entities()[1].props.handle);
        std::vector<int> expected_tol = {0, 5, 330, 100, 8, 100, 3, 10, 20, 30, 1};
        hdxf_test::assert_true(codes_of(tol) == expected_tol, "TOLERANCE groups");
        hdxf_test::assert_eq(values(tol, 1).at(0), "{\\Fgdt;j}%%v0.1^J{\\Fgdt;n}");

        Groups lead = record(out, back.entities()[2].props.handle);
        std::vector<int> expected_leader = {0, 5, 330, 100, 8, 100, 3, 73, 74, 75, 40, 41, 76,
                                            10, 20, 30, 10, 20, 30, 10, 20, 30, 340};
        hdxf_test::assert_true(codes_of(lead) == expected_leader, "LEADER groups");
        Groups bare = record(out, back.entities()[3].props.handle);
        hdxf_test::assert_eq(bare[6].first, 3);
        hdxf_test::assert_eq(bare[7].first, 71, "71 0: no arrowhead");
        hdxf_test::assert_eq(bare[7].second, "0");
    }
}

// A drawing read from 2000, changed: the long text written whole in new parts, a leader
// moved, an annotation removed (its leader no longer points to it, B19), and a removed
// leader no longer a reactor of its annotation
TEST(text_2000_changes) {
    if (!has_sample()) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_ascii.dxf"));

    {
        hdxf::Drawing dwg = load(in);
        for (auto& e : dwg.entities()) {
            if (e.props.handle == "779") e.as_mtext().content = "short \xC3\xA5" + e.as_mtext().content;
            if (e.props.handle == "512") e.as_leader().vertices[0].x = 700.0;
        }
        auto& ents = dwg.entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [](const hdxf::Entity& e) { return e.props.handle == "513"; }),
                   ents.end());
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "changed text");
        Groups mtext = record(out, "779");
        Groups before = record(in, "779");
        std::vector<std::string> parts = values(mtext, 3);
        for (const auto& p : parts) hdxf_test::assert_true(p.size() <= 250, "at most 250");
        std::string joined;
        for (const auto& p : parts) joined += p;
        joined += values(mtext, 1).at(0);
        std::string joined_before;
        for (const auto& p : values(before, 3)) joined_before += p;
        joined_before += values(before, 1).at(0);
        hdxf_test::assert_eq(joined, "short \\U+00E5" + joined_before, "the new text, whole");
        hdxf_test::assert_eq(values(mtext, 1001).at(0), "ACAD", "XDATA as it was");

        Groups leader = record(out, "512");
        hdxf_test::assert_eq(std::strtod(values(leader, 10).at(0).c_str(), nullptr), 700.0, "moved");
        hdxf_test::assert_eq(values(leader, 10).at(1), values(record(in, "512"), 10).at(1));
        hdxf_test::assert_true(values(leader, 340).empty(), "no annotation any more");
        hdxf::Drawing back = load(out);
        hdxf_test::assert_true(entity(back, "512").as_leader().annotation_handle.empty(), "read back");
    }
    {
        hdxf::Drawing dwg = load(in);
        auto& ents = dwg.entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [](const hdxf::Entity& e) { return e.props.handle == "512"; }),
                   ents.end());
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "leader removed");
        Groups mtext = record(out, "513");
        for (const auto& [code, value] : mtext) {
            hdxf_test::assert_true(code != 102, "no reactors left on the annotation");
        }
        Groups before = record(in, "513");
        hdxf_test::assert_eq(static_cast<int>(mtext.size()), static_cast<int>(before.size()) - 3);
    }
}

// Extents: MTEXT and TOLERANCE by their insertion point (as TEXT), LEADER by its vertices
TEST(text_2000_extents) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2000);
    dwg.add_mtext({10, 20, 0}, "A");
    dwg.add_tolerance({-5, 3, 0}, "{\\Fgdt;j}");
    dwg.add_leader({{0, -7, 1}, {30, 0, 0}});
    hdxf::Drawing back = load(save(dwg));
    const hdxf::HeaderVar* min = back.find_header("$EXTMIN");
    const hdxf::HeaderVar* max = back.find_header("$EXTMAX");
    auto at = [](const hdxf::HeaderVar* h, int code) {
        for (const auto& [c, v] : h->values) if (c == code) return std::stod(v);
        return 0.0;
    };
    hdxf_test::assert_eq(at(min, 10), -5.0);
    hdxf_test::assert_eq(at(min, 20), -7.0);
    hdxf_test::assert_eq(at(max, 10), 30.0);
    hdxf_test::assert_eq(at(max, 20), 20.0);
    hdxf_test::assert_eq(at(max, 30), 1.0);
}
