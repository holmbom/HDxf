#include "test_framework.hpp"
#include "hdxf.hpp"
#include "io/group_io.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// A record that is the same as the original's is written from its preserved record, unless what
// merge_record() does with it differs: a pointer to a record that is gone, an empty known 102 block, a code line width the
// merge normalizes, records the drawing no longer writes. These tests look at the written file, so they hold for either path.
// ---------------------------------------------------------------------------

namespace {

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;

using Group = std::pair<int, std::string>;
using Record = std::vector<Group>;

struct VersionTag {
    DxfVersion version;
    const char* tag;
};

const VersionTag kFacitVersions[] = {
    {DxfVersion::R12, "r12"},     {DxfVersion::R2000, "2000"},  {DxfVersion::R2004, "2004"},
    {DxfVersion::R2007, "2007"},  {DxfVersion::R2010, "2010"},  {DxfVersion::R2013, "2013"},
    {DxfVersion::R2018, "2018"},
};

std::string trimmed(const std::string& s) {
    size_t a = s.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(' ') - a + 1);
}

std::vector<Group> groups_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Group> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        out.emplace_back(gc.code, trimmed(gc.value));
    }
    return out;
}

std::vector<Record> records_of(const std::vector<uint8_t>& data) {
    std::vector<Record> out;
    for (const Group& g : groups_of(data)) {
        if (g.first == 0 || out.empty()) out.emplace_back();
        out.back().push_back(g);
    }
    return out;
}

std::string type_of(const Record& r) { return r.empty() ? std::string() : r[0].second; }

std::string handle_of(const Record& r) {
    for (const Group& g : r) {
        if (g.first == 5 || g.first == 105) return g.second;
        if (g.first == 100) break;
    }
    return {};
}

std::vector<uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::filesystem::path facit_path(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit" / hdxf::path_from_utf8(name);
}

Drawing load(const std::vector<uint8_t>& bytes) {
    Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), dwg.last_error());
    return dwg;
}

std::vector<uint8_t> save(Drawing& dwg, DxfFormat format) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

DxfFormat format_of(const std::vector<uint8_t>& bytes) {
    return bytes.size() >= 18 && std::string(bytes.begin(), bytes.begin() + 18) == "AutoCAD Binary DXF" ? DxfFormat::Binary
                                                                                                        : DxfFormat::Ascii;
}

bool is_pointer_code(int code) { return (code >= 320 && code <= 369) || (code >= 390 && code <= 399); }

// A drawing as 2018, saved as text with LF line endings
std::string made_text() {
    Drawing dwg(DxfVersion::R2018);
    dwg.add_line({0, 0, 0}, {10, 5, 0});
    dwg.add_line({2, 2, 0}, {8, 9, 0});
    std::vector<uint8_t> bytes = save(dwg, DxfFormat::Ascii);
    return hdxf_test::lf_text(bytes);
}

std::vector<uint8_t> bytes_of(const std::string& text) { return std::vector<uint8_t>(text.begin(), text.end()); }

// The text of the first LINE record: from its "0" to the next "0"
bool first_line_record(const std::string& text, size_t& begin, size_t& end) {
    begin = text.find("  0\nLINE\n");
    if (begin == std::string::npos) return false;
    end = text.find("  0\n", begin + 4);
    return end != std::string::npos;
}

} // namespace

// Records that did not change are what the file had, byte for byte in value: a layer is recolored, and every other record of
// the file (entities, blocks, objects) is the same afterwards
TEST(matches_merge_for_unchanged) {
    size_t compared = 0;
    std::vector<std::filesystem::path> files = {hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "acad_r12_dxf.dxf"};
    for (const auto& v : kFacitVersions) {
        files.push_back(facit_path(std::string("acad_") + v.tag + "_dxf_polyline_owner.dxf"));
        files.push_back(facit_path(std::string("acad_") + v.tag + "_dxf_polyline_owner_bin.dxf"));
    }
    for (const auto& path : files) {
        if (!std::filesystem::exists(path)) continue;
        std::vector<uint8_t> in = read_bytes(path);
        const DxfFormat format = format_of(in);
        Drawing dwg = load(in);
        hdxf_test::assert_true(!dwg.layers().empty(), "a layer");
        dwg.layers()[0].color = hdxf::Color::from_aci(3);
        std::vector<uint8_t> out = save(dwg, format);
        std::vector<Record> before = records_of(in);
        std::vector<Record> after = records_of(out);
        const std::string what = path.filename().string();
        hdxf_test::assert_eq(static_cast<int>(before.size()), static_cast<int>(after.size()), what + ": the records");
        int different = 0;
        for (size_t i = 0; i < before.size(); ++i) {
            const std::string type = type_of(before[i]);
            if (type == "SECTION" || type == "LAYER") continue;     // the header, and the layer that was recolored
            if (before[i] != after[i]) {
                ++different;
                hdxf_test::assert_true(false, what + ": record " + std::to_string(i) + " (" + type + " " + handle_of(before[i]) +
                                                  ") differs");
            }
        }
        hdxf_test::assert_eq(0, different);
        ++compared;
    }
    if (compared == 0) hdxf_test::skip("no facit files");
}

// An entity is removed: everything that pointed at it leaves it out or says none (0), in the record types that have
// relation descriptors (the draw order's pairs) and in the ones that do not (XRECORD data, 102 blocks)
TEST(checks_descriptor_references) {
    size_t compared = 0;
    for (const char* tag : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (bool binary : {false, true}) {
            const std::filesystem::path path =
                facit_path(std::string("acad_") + tag + "_dxf_order_index" + (binary ? "_bin" : "") + ".dxf");
            if (!std::filesystem::exists(path)) continue;
            const std::string what = std::string(tag) + (binary ? " binary" : " ascii");
            std::vector<uint8_t> in = read_bytes(path);
            Drawing dwg = load(in);

            // The model space's draw order: one of its entities goes
            const hdxf::Object* table = nullptr;
            for (const auto& o : dwg.objects()) {
                if (o.type == hdxf::ObjectType::SortentsTable && !o.as_sortents_table().entries.empty() && !table) table = &o;
            }
            hdxf_test::assert_true(table != nullptr, what + ": a draw order");
            const std::string block_record = table->as_sortents_table().block_record_handle;
            const size_t pairs = table->as_sortents_table().entries.size();
            std::string removed;
            auto& entities = dwg.entities();
            for (const auto& entry : table->as_sortents_table().entries) {
                auto it = std::find_if(entities.begin(), entities.end(),
                                       [&](const hdxf::Entity& e) { return e.props.handle == entry.entity_handle; });
                if (it == entities.end()) continue;
                removed = it->props.handle;
                entities.erase(it);
                break;
            }
            hdxf_test::assert_false(removed.empty(), what + ": an entity of the draw order");

            std::vector<uint8_t> out = save(dwg, binary ? DxfFormat::Binary : DxfFormat::Ascii);
            for (const Group& g : groups_of(out)) {
                hdxf_test::assert_false(is_pointer_code(g.first) && g.second == removed,
                                        what + ": a pointer " + std::to_string(g.first) + " to the removed " + removed);
            }
            // The draw order of the same block record has one pair less
            int left = -1;
            for (const Record& r : records_of(out)) {
                if (type_of(r) != "SORTENTSTABLE") continue;
                bool ours = false;
                int count = 0;
                for (const Group& g : r) {
                    if (g.first == 330 && g.second == block_record) ours = true;
                    if (g.first == 331) ++count;
                }
                if (ours) left = count;
            }
            hdxf_test::assert_eq(static_cast<int>(pairs) - 1, left, what + ": the pairs of the draw order");
            ++compared;
        }
    }
    if (compared == 0) hdxf_test::skip("no order_index facit files");
}

// What goes with a removed record also goes when nothing else of the record changed: the blocks a block record lists its
// INSERTs in
TEST(keeps_removal_effects) {
    Drawing dwg(DxfVersion::R2018);
    dwg.add_block("B").entities.push_back([] {
        hdxf::Entity line;
        line.type = hdxf::EntityType::Line;
        line.data = hdxf::LineData{{0, 0, 0}, {1, 1, 0}};
        return line;
    }());
    dwg.add_insert("B", {0, 0, 0});
    dwg.add_insert("B", {5, 5, 0});
    std::vector<uint8_t> bytes = save(dwg, DxfFormat::Ascii);
    Drawing read = load(bytes);

    auto references = [](const std::vector<Record>& records, const std::string& block) {
        std::vector<std::string> handles;
        for (const Record& r : records) {
            if (type_of(r) != "BLOCK_RECORD") continue;
            bool ours = false;
            for (const Group& g : r) ours = ours || (g.first == 2 && g.second == block);
            if (!ours) continue;
            for (const Group& g : r) {
                if (g.first == 331) handles.push_back(g.second);
            }
        }
        return handles;
    };
    std::vector<std::string> before = references(records_of(bytes), "B");
    hdxf_test::assert_eq(2, static_cast<int>(before.size()), "two INSERTs listed");

    // One INSERT removed: the other stays listed
    std::string gone = read.entities()[0].props.handle;
    std::string stays = read.entities()[1].props.handle;
    read.entities().erase(read.entities().begin());
    std::vector<uint8_t> once = save(read, DxfFormat::Ascii);
    std::vector<std::string> after_one = references(records_of(once), "B");
    hdxf_test::assert_eq(1, static_cast<int>(after_one.size()), "one INSERT left in the list");
    hdxf_test::assert_eq(stays, after_one[0]);
    for (const Group& g : groups_of(once)) {
        hdxf_test::assert_false(is_pointer_code(g.first) && g.second == gone, "a pointer to the removed INSERT");
    }

    // Both removed: the block record has no list at all
    Drawing again = load(bytes);
    again.entities().clear();
    std::vector<uint8_t> none = save(again, DxfFormat::Ascii);
    hdxf_test::assert_eq(0, static_cast<int>(references(records_of(none), "B").size()), "no INSERT left in the list");
    for (const Record& r : records_of(none)) {
        if (type_of(r) != "BLOCK_RECORD") continue;
        for (const Group& g : r) hdxf_test::assert_false(g.first == 102 && g.second == "{BLKREFS", "an empty {BLKREFS block");
    }
}

// An INSERT loses its attributes: its SEQEND goes (the original made from the records that are left says the same as the
// drawing, so only the records the file had left over tell that it is not written any more)
TEST(rejects_incomplete_mapping) {
    size_t compared = 0;
    for (const auto& v : kFacitVersions) {
        if (v.version == DxfVersion::R12) continue;
        for (bool binary : {false, true}) {
            const std::filesystem::path path =
                facit_path(std::string("acad_") + v.tag + "_dxf_polyline_owner" + (binary ? "_bin" : "") + ".dxf");
            if (!std::filesystem::exists(path)) continue;
            const std::string what = std::string(v.tag) + (binary ? " binary" : " ascii");
            std::vector<uint8_t> in = read_bytes(path);
            auto count_of = [](const std::vector<Record>& records, const std::string& type) {
                int n = 0;
                for (const Record& r : records) n += type_of(r) == type ? 1 : 0;
                return n;
            };
            Drawing dwg = load(in);
            bool cleared = false;
            for (auto& e : dwg.entities()) {
                if (e.type == hdxf::EntityType::Insert && !e.as_insert().attributes.empty()) {
                    e.as_insert().attributes.clear();
                    cleared = true;
                }
            }
            hdxf_test::assert_true(cleared, what + ": an INSERT with an attribute");
            std::vector<uint8_t> out = save(dwg, binary ? DxfFormat::Binary : DxfFormat::Ascii);
            std::vector<Record> before = records_of(in);
            std::vector<Record> after = records_of(out);
            hdxf_test::assert_eq(count_of(before, "SEQEND") - 1, count_of(after, "SEQEND"), what + ": the INSERT's SEQEND");
            hdxf_test::assert_eq(count_of(before, "ATTRIB") - 1, count_of(after, "ATTRIB"), what + ": the ATTRIB");
            ++compared;
        }
    }
    if (compared == 0) hdxf_test::skip("no polyline_owner facit files");
}

// The structure merge_record() looks at: a known 102 block that is empty is not written, and the code line width of a 102
// block the merge rewrites is the usual one
TEST(checks_structure) {
    // An empty {ACAD_REACTORS block in a LINE
    {
        std::string text = made_text();
        size_t begin = 0, end = 0;
        hdxf_test::assert_true(first_line_record(text, begin, end), "a LINE");
        const size_t owner = text.find("330\n", begin);
        hdxf_test::assert_true(owner != std::string::npos && owner < end, "the LINE's owner");
        const std::string empty_block = "102\n{ACAD_REACTORS\n102\n}\n";
        std::string edited = text;
        edited.insert(owner, empty_block);
        Drawing dwg = load(bytes_of(edited));
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii);
        std::vector<Record> in_records = records_of(bytes_of(edited));
        std::vector<Record> out_records = records_of(out);
        bool found = false;
        for (size_t i = 0; i < in_records.size(); ++i) {
            if (type_of(in_records[i]) != "LINE") continue;
            Record expected;
            for (size_t k = 0; k < in_records[i].size(); ++k) {
                if (in_records[i][k].first == 102) {
                    ++k;                // the block's name and its closing
                    continue;
                }
                expected.push_back(in_records[i][k]);
            }
            hdxf_test::assert_true(expected == out_records[i], "the empty 102 block is not written");
            found = true;
            break;
        }
        hdxf_test::assert_true(found, "the LINE in the output");
    }

    // A 102 block with a code line of four columns: the merge writes the block with the usual ones
    {
        std::string text = made_text();
        size_t begin = 0, end = 0;
        hdxf_test::assert_true(first_line_record(text, begin, end), "a LINE");
        const size_t owner = text.find("330\n", begin);
        hdxf_test::assert_true(owner != std::string::npos && owner < end, "the LINE's owner");
        const size_t value_at = owner + 4;
        const std::string owner_handle = text.substr(value_at, text.find('\n', value_at) - value_at);
        std::string edited = text;
        edited.insert(owner, "102\n{ACAD_REACTORS\n 330\n" + owner_handle + "\n102\n}\n");
        Drawing dwg = load(bytes_of(edited));
        std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii);
        const std::string out_text = hdxf_test::lf_text(out);
        hdxf_test::assert_true(out_text.find("102\n{ACAD_REACTORS\n330\n" + owner_handle + "\n102\n}\n") != std::string::npos,
                               "the block with the usual code lines");
        hdxf_test::assert_true(out_text.find(" 330\n" + owner_handle + "\n102\n}\n") == std::string::npos,
                               "no code line of four columns left in the block");
    }
}

// The preserved strings are in the file's code page, and are converted when $DWGCODEPAGE changed
TEST(converts_code_page) {
    Drawing dwg;
    const std::filesystem::path path = hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / "r12_ansi_1252.dxf";
    hdxf_test::assert_true(dwg.load(path), dwg.last_error());
    dwg.set_header_string("$DWGCODEPAGE", 3, "DOS850");
    std::vector<uint8_t> out = save(dwg, DxfFormat::Ascii);
    const std::string text(out.begin(), out.end());
    const std::string in_850 = "Lager_\x86\x84\x94";
    const std::string in_1252 = "Lager_\xE5\xE4\xF6";
    size_t found = 0;
    for (size_t at = text.find(in_850); at != std::string::npos; at = text.find(in_850, at + 1)) ++found;
    hdxf_test::assert_true(found >= 2, "the layer's name in the table and in the TEXT, in DOS850");
    hdxf_test::assert_true(text.find(in_1252) == std::string::npos, "no name in the old code page");

    // Read again, the same names
    Drawing back = load(out);
    bool layer = false;
    for (const auto& l : back.layers()) layer = layer || l.name == "Lager_\xC3\xA5\xC3\xA4\xC3\xB6";
    hdxf_test::assert_true(layer, "the layer Lager_åäö after the conversion");
}
