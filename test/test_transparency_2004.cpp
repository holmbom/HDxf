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
// Transparency — an entity's 440 (2004+), a layer's XDATA AcCmTransparency (2000+) —
// phase 2, step 2.3 (plan B22, B23)
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
            if (code == 5 && value == handle) return r;
            if (code == 100) break;
        }
    }
    return {};
}

Record layer_record(const std::vector<Record>& records, const std::string& name) {
    for (const auto& r : records) {
        if (r[0].second != "LAYER") continue;
        for (const auto& [code, v] : r) if (code == 2 && v == name) return r;
    }
    return {};
}

std::string value(const Record& r, int code, int occurrence = 0) {
    for (const auto& [c, v] : r) {
        if (c == code && occurrence-- == 0) return v;
    }
    return {};
}

// From the first 1001 on
Record xdata_of(const Record& r) {
    auto it = std::find_if(r.begin(), r.end(), [](const auto& g) { return g.first == 1001; });
    return Record(it, r.end());
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
const char* const k2000 = "acadsharp/sample_AC1015_ascii.dxf";

} // namespace

TEST(transparency_values) {
    hdxf_test::assert_eq(hdxf::Transparency::by_block().raw, 16777216);
    hdxf_test::assert_eq(hdxf::Transparency::from_alpha(76).raw, 33554508);
    hdxf_test::assert_eq(hdxf::Transparency::from_alpha(76).alpha(), 76);
    hdxf_test::assert_eq(hdxf::Transparency::by_layer().alpha(), -1);
    hdxf_test::assert_true(hdxf::Transparency{}.is_by_layer(), "ByLayer by default");
    hdxf_test::assert_true(hdxf::Transparency::by_block().is_by_block(), "ByBlock");
}

// What AutoCAD's files have: 150 entities ByBlock and a SOLID at alpha 102 in 2004; a
// layer's AcCmTransparency is the field — 70 % (alpha 76) for Layer_transp_70; none (1071 0)
// for the others, which stays in xdata so the layer written anew is AutoCAD's (plan 5.9) —
// the rest of its XDATA in xdata
TEST(transparency_2004_read) {
    if (!has_files({kAscii, kBinary, k2000})) return;
    for (const char* name : {kAscii, kBinary}) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(external(name)), dwg.last_error());
        int by_block = 0;
        auto count = [&](const std::vector<hdxf::Entity>& list) {
            for (const auto& e : list) by_block += e.props.transparency.is_by_block();
        };
        count(dwg.entities());
        for (const auto& b : dwg.blocks()) count(b.entities);
        hdxf_test::assert_eq(by_block, 150, "ByBlock");
        const hdxf::Entity* solid = nullptr;
        for (const auto& e : dwg.entities()) if (e.props.handle == "432") solid = &e;
        hdxf_test::assert_eq(solid->props.transparency.alpha(), 102, "SOLID 432");
    }
    for (const char* name : {kAscii, k2000}) {
        hdxf::Drawing dwg;
        hdxf_test::assert_true(dwg.load(external(name)), dwg.last_error());
        for (const auto& l : dwg.layers()) {
            if (l.name == "Layer_transp_70") hdxf_test::assert_eq(l.transparency.alpha(), 76, l.name);
            else hdxf_test::assert_true(l.transparency.is_by_layer(), l.name + ": 1071 0");
            for (const auto& x : l.xdata) {
                if (x.app_name != "AcCmTransparency") continue;
                hdxf_test::assert_true(x.items.size() == 1 && x.items[0].code == 1071 && std::stoi(x.items[0].value) == 0,
                                       l.name + ": only 1071 0 in xdata");
            }
        }
        const hdxf::Layer* layer1 = nullptr;
        for (const auto& l : dwg.layers()) if (l.name == "Layer1") layer1 = &l;
        hdxf_test::assert_eq(static_cast<int>(layer1->xdata.size()), 2, "AcAecLayerStandard, AcCmTransparency 0");
        hdxf_test::assert_eq(layer1->xdata[0].app_name, "AcAecLayerStandard");
        hdxf_test::assert_eq(layer1->xdata[1].app_name, "AcCmTransparency");
    }
}

// A new drawing: 440 where AutoCAD has it, a layer's transparency as AutoCAD's XDATA with
// its APPID; in 2000 the layer's too, the entity's not (B23)
TEST(transparency_2004_written) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2000}) {
        hdxf::Drawing dwg(v);
        hdxf::Layer& glass = dwg.add_layer("Glass");
        glass.transparency = hdxf::Transparency::from_alpha(127);
        glass.xdata.push_back({"ACAD", {{1000, "note"}}});
        if (v == hdxf::DxfVersion::R2004) {
            hdxf::Entity& line = dwg.add_line({0, 0, 0}, {1, 0, 0}, "Glass");
            line.props.color = hdxf::Color::from_rgb(10, 20, 30);
            line.props.transparency = hdxf::Transparency::from_alpha(76);
            line.props.lineweight = 25;
            dwg.add_line({0, 1, 0}, {1, 1, 0}).props.transparency = hdxf::Transparency::by_block();
        }
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, "transparency");
        std::vector<Record> recs = records_of(out);
        Record layer = layer_record(recs, "Glass");
        hdxf_test::assert_true(xdata_of(layer) == Record({{1001, "ACAD"}, {1000, "note"}, {1001, "AcCmTransparency"},
                                                          {1071, "33554559"}}), "layer XDATA");
        bool app = false;
        for (const auto& r : recs) app = app || (r[0].second == "APPID" && value(r, 2) == "AcCmTransparency");
        hdxf_test::assert_true(app, "APPID AcCmTransparency");
        hdxf::Drawing back = load(out);
        for (const auto& l : back.layers()) {
            if (l.name != "Glass") continue;
            hdxf_test::assert_eq(l.transparency.alpha(), 127, "read back");
            hdxf_test::assert_eq(static_cast<int>(l.xdata.size()), 1, "the rest");
        }
        if (v != hdxf::DxfVersion::R2004) continue;
        Record line = record(recs, back.entities()[0].props.handle);
        std::vector<int> codes;
        bool in = false;
        for (const auto& [code, val] : line) {
            if (code == 100) { in = val == "AcDbEntity"; continue; }
            if (in) codes.push_back(code);
        }
        hdxf_test::assert_true(codes == std::vector<int>({8, 62, 420, 440, 370}), "8 62 420 440 370");
        hdxf_test::assert_eq(value(line, 440), "33554508");
        hdxf_test::assert_eq(value(record(recs, back.entities()[1].props.handle), 440), "16777216", "ByBlock");
        hdxf_test::assert_eq(back.entities()[0].props.transparency.alpha(), 76);
    }

    hdxf::Drawing old(hdxf::DxfVersion::R2000);
    old.add_line({0, 0, 0}, {1, 0, 0}).props.transparency = hdxf::Transparency::from_alpha(1);
    hdxf_test::assert_true(old.save_to_memory().empty(), "2000 with an entity's transparency");
    hdxf_test::assert_eq(old.last_error(), "Transparency requires DXF 2004 or later");

    hdxf::Drawing r12;
    r12.add_layer("Glass").transparency = hdxf::Transparency::from_alpha(127);
    hdxf_test::assert_true(r12.save_to_memory().empty(), "R12 with a layer's transparency");
    hdxf_test::assert_eq(r12.last_error(), "Layer \"Glass\": transparency requires DXF 2000 or later");

    hdxf::Drawing twice(hdxf::DxfVersion::R2004);
    hdxf::Layer& l = twice.add_layer("L");
    l.transparency = hdxf::Transparency::from_alpha(5);
    l.xdata.push_back({"AcCmTransparency", {{1071, "33554437"}}});
    hdxf_test::assert_true(twice.save_to_memory().empty(), "both");
    hdxf_test::assert_eq(twice.last_error(), "Layer \"L\" has its transparency both in xdata and in transparency");

    // AutoCAD's 1071 0 (no transparency) in xdata gives way to a transparency that is set
    hdxf::Drawing zero(hdxf::DxfVersion::R2004);
    hdxf::Layer& z = zero.add_layer("Z");
    z.xdata.push_back({"AcCmTransparency", {{1071, "0"}}});
    hdxf_test::assert_true(xdata_of(layer_record(records_of(save(zero)), "Z")) ==
                           Record({{1001, "AcCmTransparency"}, {1071, "0"}}), "1071 0 as it is");
    z.transparency = hdxf::Transparency::from_alpha(5);
    hdxf_test::assert_true(xdata_of(layer_record(records_of(save(zero)), "Z")) ==
                           Record({{1001, "AcCmTransparency"}, {1071, "33554437"}}), "replaced");
}

// AutoCAD's files changed (2004 and 2000): a layer's transparency written into its XDATA,
// the rest of the XDATA as it was; set back to none, the block goes; an entity's 440
TEST(transparency_changed_files) {
    if (!has_files({kAscii, k2000})) return;
    for (const char* name : {kAscii, k2000}) {
        std::vector<uint8_t> in = read_bytes(external(name));
        std::vector<Record> before = records_of(in);
        hdxf::Drawing dwg = load(in);
        for (auto& l : dwg.layers()) {
            if (l.name == "Layer1") l.transparency = hdxf::Transparency::from_alpha(127);
        }
        bool r2004 = dwg.version() == hdxf::DxfVersion::R2004;
        if (r2004) {
            for (auto& e : dwg.entities()) {
                if (e.props.handle == "432") e.props.transparency = hdxf::Transparency::from_alpha(200);
                if (e.props.handle == "2C7") e.props.transparency = hdxf::Transparency::by_block();
            }
        }
        std::vector<uint8_t> out = save(dwg);
        dxf_structure::check_structure(out, std::string(name) + " changed");
        std::vector<Record> after = records_of(out);
        Record xd = xdata_of(layer_record(after, "Layer1"));
        Record xd_before = xdata_of(layer_record(before, "Layer1"));
        hdxf_test::assert_eq(static_cast<int>(xd.size()), static_cast<int>(xd_before.size()), "same shape");
        hdxf_test::assert_true(Record(xd.begin(), xd.end() - 1) == Record(xd_before.begin(), xd_before.end() - 1),
                               "AcAecLayerStandard as it was");
        hdxf_test::assert_true(xd.back() == std::make_pair(1071, std::string("33554559")), "new transparency");
        hdxf_test::assert_true(layer_record(after, "Layer2") == layer_record(before, "Layer2"), "Layer2 as it was");
        if (r2004) {
            hdxf_test::assert_eq(value(record(after, "432"), 440), std::to_string(0x020000C8));
            hdxf_test::assert_eq(value(record(after, "2C7"), 440), "16777216");
        }

        // Back to none: the AcCmTransparency block goes
        hdxf::Drawing again = load(out);
        for (auto& l : again.layers()) {
            if (l.name == "Layer1") l.transparency = hdxf::Transparency::by_layer();
        }
        Record cleared = xdata_of(layer_record(records_of(save(again)), "Layer1"));
        hdxf_test::assert_true(std::none_of(cleared.begin(), cleared.end(), [](const auto& g) {
            return g.first == 1001 && g.second == "AcCmTransparency";
        }), "block removed");
        hdxf_test::assert_eq(value(cleared, 1001), "AcAecLayerStandard", "the rest kept");
    }

    // A file without the APPID: it is added
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(synthetic("r2004_unicode_escapes.dxf")), dwg.last_error());
    for (const auto& a : dwg.app_ids()) hdxf_test::assert_true(a.name != "AcCmTransparency", "no APPID yet");
    dwg.layers().back().transparency = hdxf::Transparency::from_alpha(10);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "APPID added");
    bool app = false;
    for (const auto& r : records_of(out)) app = app || (r[0].second == "APPID" && value(r, 2) == "AcCmTransparency");
    hdxf_test::assert_true(app, "APPID AcCmTransparency added");
}

// Codex review of 2.3: two AcCmTransparency blocks on a layer stay XDATA, as they were
TEST(transparency_two_blocks_kept) {
    if (!has_files({kAscii})) return;
    std::string text;
    {
        std::vector<uint8_t> in = read_bytes(external(kAscii));
        text.assign(in.begin(), in.end());
    }
    // Layer_transp_70 gets a second block
    const std::string block = "1001\nAcCmTransparency\n1071\n 33554508\n";
    size_t at = text.find(block);
    hdxf_test::assert_true(at != std::string::npos, "Layer_transp_70's block");
    text.insert(at + block.size(), "1001\nAcCmTransparency\n1071\n 33554509\n");
    std::vector<uint8_t> in(text.begin(), text.end());
    hdxf::Drawing dwg = load(in);
    for (const auto& l : dwg.layers()) {
        if (l.name != "Layer_transp_70") continue;
        hdxf_test::assert_true(l.transparency.is_by_layer(), "not taken");
        hdxf_test::assert_eq(static_cast<int>(l.xdata.size()), 3, "both blocks in xdata");
    }
    std::vector<Record> after = records_of(save(dwg));
    hdxf_test::assert_true(layer_record(after, "Layer_transp_70") == layer_record(records_of(in), "Layer_transp_70"),
                           "as it was");
}
