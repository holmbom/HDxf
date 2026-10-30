#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "text_encoding.hpp"
#include "dxf_structure.hpp"
#include "dxf_text.hpp"
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
// A drawing read from DXF 2000 written back as 2000 (phase 1, step 1.4 — plan B12, B19):
// what did not change is written as it was, record for record; what changed is written
// anew; what went with a removed record goes too.
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

std::vector<uint8_t> save(hdxf::Drawing& dwg, hdxf::DxfFormat format) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

// The record whose handle (5) is `handle`, empty if there is none
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

bool has_files(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (!std::filesystem::exists(external(n))) {
            hdxf_test::skip("external test file missing: " + n);
            return false;
        }
    }
    return true;
}

} // namespace

// Read and written in the same format: every record is the one in the file, group for
// group, the text as it was — $VERSIONGUID too, since nothing changed
TEST(roundtrip_2000_files_record_for_record) {
    struct File { std::filesystem::path path; hdxf::DxfFormat format; };
    std::vector<File> files = {{synthetic("r2000_large_handles.dxf"), hdxf::DxfFormat::Ascii}};
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1015_binary.dxf",
                             "ezdxf/bin_dxf_r2000.dxf"}) {
        if (std::filesystem::exists(external(name))) {
            files.push_back({external(name), std::string(name).find("bin") != std::string::npos
                                                 ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii});
        }
    }
    for (const auto& f : files) {
        std::string what = hdxf::utf8_from_path(f.path.filename());
        std::vector<uint8_t> in = read_bytes(f.path);
        hdxf::Drawing dwg = load(in);
        std::vector<Record> before = records_of(in);
        std::vector<Record> after = records_of(save(dwg, f.format));
        hdxf_test::assert_eq(static_cast<int>(after.size()), static_cast<int>(before.size()),
                             what + ": records");
        for (size_t i = 0; i < before.size(); ++i) {
            hdxf_test::assert_true(before[i] == after[i], what + ": record " + std::to_string(i) +
                                   " (" + before[i][0].second + ") differs");
        }
        hdxf_test::assert_true(hdxf_test::warnings_besides_fonts(dwg.warnings()).empty(), what + ": warnings");
    }
}

// AutoCAD's binary file comes back byte for byte
TEST(roundtrip_2000_binary_bytes) {
    if (!has_files({"acadsharp/sample_AC1015_binary.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_binary.dxf"));
    hdxf::Drawing dwg = load(in);
    std::vector<uint8_t> out = save(dwg, hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(out == in, "bytes");
}

// Into the other format: the same values. ASCII → binary keeps every number exactly;
// binary → ASCII writes them as AutoCAD does, with up to 16 significant digits.
TEST(roundtrip_2000_ascii_binary_ascii) {
    if (!has_files({"acadsharp/sample_AC1015_ascii.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_ascii.dxf"));
    hdxf::Drawing ascii = load(in);
    std::vector<uint8_t> bin = save(ascii, hdxf::DxfFormat::Binary);
    hdxf::Drawing binary = load(bin);
    std::vector<uint8_t> back = save(binary, hdxf::DxfFormat::Ascii);

    std::vector<Record> a = records_of(in), b = records_of(bin), c = records_of(back);
    hdxf_test::assert_eq(static_cast<int>(b.size()), static_cast<int>(a.size()), "records (binary)");
    hdxf_test::assert_eq(static_cast<int>(c.size()), static_cast<int>(a.size()), "records (ASCII)");
    // x is ASCII; y is binary (carets as the characters they stand for) or ASCII
    auto same = [](const Record& x, const Record& y, double tolerance, const std::string& what,
                   bool y_binary = false) {
        hdxf_test::assert_eq(static_cast<int>(y.size()), static_cast<int>(x.size()), what + ": groups");
        for (size_t g = 0; g < x.size(); ++g) {
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
                hdxf_test::assert_true(std::atoll(y[g].second.c_str()) == std::atoll(x[g].second.c_str()), what + ": " + x[g].second);
            }
        }
    };
    for (size_t i = 0; i < a.size(); ++i) {
        same(a[i], b[i], 0.0, "binary record " + std::to_string(i), true);
        same(a[i], c[i], 1e-15, "ASCII record " + std::to_string(i));
    }
}

// Changes: what is written anew, what stays, and what goes with a removed record (B19)
TEST(roundtrip_2000_changes) {
    if (!has_files({"acadsharp/sample_AC1015_ascii.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_ascii.dxf"));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);

    auto find = [&](const std::string& handle) -> hdxf::Entity* {
        for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
        return nullptr;
    };
    // A LINE moved, a layer recoloured and frozen (Layer_Lock has 70 = 4)
    find("2C7")->as_line().end.x = 50.0;
    for (auto& l : dwg.layers()) {
        if (l.name == "Layer1") l.color = hdxf::Color::from_aci(3);
        if (l.name == "Layer_Lock") {
            l.frozen = true;
            l.flags |= 64;      // a bit HDxf does not interpret ("referenced"): kept
        }
    }
    // An LWPOLYLINE closed (70 1); another raised (38 is new: before the vertices, as
    // AutoCAD writes it) and given one width (43 0.0 → 0.25); a third's vertex list
    // changed (one inserted, the last removed): written whole
    find("2E2")->as_lwpolyline().flags = 1;
    auto& raised = find("2E3")->as_lwpolyline();
    raised.elevation = 2.5;
    for (auto& v : raised.vertices) v.start_width = v.end_width = 0.25;
    auto& widths = find("2E5")->as_lwpolyline().vertices;
    widths.insert(widths.begin() + 1, hdxf::LwVertex{{36.0, 6.0}, 1.0, 2.0, 0.5});
    widths.pop_back();
    // A WIPEOUT's brightness 50 → 60
    find("44D")->as_wipeout().brightness = 60;
    // A rotated dimension made an aligned one: other subclasses
    find("514")->as_dimension().dim_type = hdxf::DimensionType::Aligned;
    // Removed: a member of GROUP *A1 (69D) and a TEXT with an extension dictionary (3BA)
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) {
        return e.props.handle == "69D" || e.props.handle == "3BA" || e.props.handle == "704";
    }), ents.end());
    // Added: a LINE, put in GROUP *A1
    dwg.add_line({0, 0, 0}, {1, 1, 0}, "Layer1");
    std::vector<uint8_t> first = save(dwg, hdxf::DxfFormat::Ascii);      // gives it a handle
    std::string added = dwg.entities().back().props.handle;
    dwg.find_object("69E")->as_group().entities.push_back(added);

    std::vector<uint8_t> out = save(dwg, hdxf::DxfFormat::Ascii);
    dxf_structure::check_structure(out, "changed AutoCAD file");
    std::vector<Record> after = records_of(out);

    Record line = record(after, "2C7");
    hdxf_test::assert_eq(std::strtod(value(line, 11).c_str(), nullptr), 50.0, "moved");
    hdxf_test::assert_eq(value(line, 10), value(record(before, "2C7"), 10), "start as it was");

    Record layer1;
    Record lock;
    for (const auto& r : after) {
        if (r[0].second == "LAYER" && value(r, 2) == "Layer1") layer1 = r;
        if (r[0].second == "LAYER" && value(r, 2) == "Layer_Lock") lock = r;
    }
    hdxf_test::assert_eq(std::atoi(value(layer1, 62).c_str()), 3, "colour");
    hdxf_test::assert_eq(std::atoi(value(lock, 70).c_str()), 64 | 4 | 1, "frozen, still locked, bit 64 kept");
    hdxf_test::assert_eq(value(layer1, 1001), "AcAecLayerStandard", "the layer's XDATA as it was");

    hdxf_test::assert_eq(std::atoi(value(record(after, "2E2"), 70).c_str()), 1, "LWPOLYLINE closed");
    hdxf_test::assert_eq(value(record(after, "2E2"), 10), value(record(before, "2E2"), 10),
                         "LWPOLYLINE vertices as they were");

    // From AcDbPolyline on: code and value
    auto body = [](const Record& r) {
        Record out;
        bool in = false;
        for (const auto& g : r) {
            if (g.first == 100) in = g.second == "AcDbPolyline";
            else if (in) out.push_back(g);
        }
        return out;
    };
    Record raised_after = body(record(after, "2E3"));
    Record raised_before = body(record(before, "2E3"));
    hdxf_test::assert_eq(raised_after[2].first, 43);
    hdxf_test::assert_eq(std::strtod(raised_after[2].second.c_str(), nullptr), 0.25, "one width");
    hdxf_test::assert_eq(raised_after[3].first, 38, "elevation after 43, before the vertices");
    hdxf_test::assert_eq(std::strtod(raised_after[3].second.c_str(), nullptr), 2.5);
    hdxf_test::assert_eq(static_cast<int>(raised_after.size()), static_cast<int>(raised_before.size()) + 1);
    for (size_t i = 3; i < raised_before.size(); ++i) {
        hdxf_test::assert_true(raised_after[i + 1] == raised_before[i], "vertex groups as they were");
    }

    Record widths_after = body(record(after, "2E5"));
    Record widths_before = body(record(before, "2E5"));
    hdxf_test::assert_eq(std::atoi(widths_after[0].second.c_str()), 5, "90: the count");
    // The first vertex as it was, then the new one, then the old second to fourth
    std::vector<int> codes;
    for (const auto& g : widths_after) codes.push_back(g.first);
    std::vector<int> expected = {90, 70, 10, 20, 40, 41, 42, 10, 20, 40, 41, 42, 10, 20, 40, 41,
                                 42, 10, 20, 40, 41, 10, 20, 40, 41};
    hdxf_test::assert_true(codes == expected, "LWPOLYLINE groups in AutoCAD's order");
    for (size_t i = 2; i < 7; ++i) {
        hdxf_test::assert_eq(std::strtod(widths_after[i].second.c_str(), nullptr),
                             std::strtod(widths_before[i].second.c_str(), nullptr), "first vertex");
    }
    hdxf_test::assert_eq(std::strtod(widths_after[7].second.c_str(), nullptr), 36.0, "new vertex");
    hdxf_test::assert_eq(std::strtod(widths_after[10].second.c_str(), nullptr), 2.0, "its end width");
    hdxf_test::assert_eq(std::strtod(widths_after[12].second.c_str(), nullptr),
                         std::strtod(widths_before[7].second.c_str(), nullptr), "old second vertex");

    Record wipeout = record(after, "44D");
    Record wipeout_before = record(before, "44D");
    hdxf_test::assert_eq(static_cast<int>(wipeout.size()), static_cast<int>(wipeout_before.size()), "WIPEOUT shape");
    for (size_t i = 0; i < wipeout.size(); ++i) {
        bool brightness = wipeout_before[i].first == 281;
        hdxf_test::assert_eq(wipeout[i].first, wipeout_before[i].first, "WIPEOUT code");
        hdxf_test::assert_eq(wipeout[i].second, brightness ? "60" : wipeout_before[i].second, "WIPEOUT");
    }

    Record dim = record(after, "514");
    bool rotated = false;
    for (const auto& [code, v] : dim) rotated = rotated || (code == 100 && v == "AcDbRotatedDimension");
    hdxf_test::assert_false(rotated, "no AcDbRotatedDimension any more");
    hdxf_test::assert_eq(value(dim, 100, 2), "AcDbAlignedDimension");

    // Gone, and what went with it: the TEXT's extension dictionary and its contents
    for (const char* h : {"69D", "3BA", "3C3", "3C4", "3C5"}) {
        hdxf_test::assert_true(record(after, h).empty(), std::string(h) + " still there");
    }
    Record group = record(after, "69E");
    std::vector<std::string> members;
    for (const auto& [code, v] : group) if (code == 340) members.push_back(v);
    hdxf_test::assert_eq(static_cast<int>(members.size()), 6, "members");
    hdxf_test::assert_eq(members.front(), "69C", "69D left out");
    hdxf_test::assert_eq(members.back(), added, "the new member");
    Record new_line = record(after, added);
    hdxf_test::assert_eq(value(new_line, 330, 0), "69E", "reactor of the new member");
    hdxf_test::assert_eq(value(new_line, 330, 1), "1F", "owner of the new line: *Model_Space");

    // INSERT 704 removed: MyBlock's BLOCK_RECORD (6F2) lists it no more
    Record my_block = record(after, "6F2");
    for (const auto& [code, v] : my_block) hdxf_test::assert_true(!(code == 331 && v == "704"), "BLKREFS");
    hdxf_test::assert_eq(value(my_block, 2), "MyBlock");

    // A pointer HDxf does not interpret (an XRECORD of AutoCAD's, ACDB_RECOMPOSE_DATA,
    // lists 3BA) to a removed record becomes 0 (none), in its place; the count stays
    Record xrecord = record(after, "14A0");
    Record xrecord_before = record(before, "14A0");
    hdxf_test::assert_eq(static_cast<int>(xrecord.size()), static_cast<int>(xrecord_before.size()), "shape");
    for (size_t i = 0; i < xrecord.size(); ++i) {
        bool was_3ba = xrecord_before[i].second == "3BA";
        hdxf_test::assert_eq(xrecord[i].second, was_3ba ? "0" : xrecord_before[i].second, "XRECORD");
    }
    bool warned = false;
    for (const auto& w : dwg.warnings()) warned = warned || w.find("set to none") != std::string::npos;
    hdxf_test::assert_true(warned, "warning");

    // Everything else as it was: compare the records that are in both
    int same = 0;
    for (const auto& r : before) {
        std::string h;
        for (const auto& [code, v] : r) { if (code == 5) { h = v; break; } if (code == 100) break; }
        if (h.empty() || r[0].second == "SECTION" || h == "2C7" || h == "2E2" || h == "2E3" ||
            h == "2E5" || h == "44D" || h == "514" || h == "6F2" || h == "69E" || h == "14A0") continue;
        Record w = record(after, h);
        if (w.empty()) continue;
        if (w == r) ++same;
        else if (r[0].second != "LAYER" && r[0].second != "BLOCK_RECORD") {
            hdxf_test::assert_true(false, "record " + h + " (" + r[0].second + ") changed");
        }
    }
    // 1098 records, less the ones removed, changed and the old geometry of dimension 514
    hdxf_test::assert_true(same > 950, "unchanged records: " + std::to_string(same));

    // $VERSIONGUID changed with the drawing, $FINGERPRINTGUID did not
    hdxf_test::assert_true(value(after[0], 2, 1) == value(before[0], 2, 1), "first 2 is HEADER");
    std::string fingerprint, version_before, version_after;
    for (size_t i = 0; i + 1 < before[0].size(); ++i) {
        if (before[0][i].second == "$VERSIONGUID") version_before = before[0][i + 1].second;
        if (before[0][i].second == "$FINGERPRINTGUID") fingerprint = before[0][i + 1].second;
    }
    for (size_t i = 0; i + 1 < after[0].size(); ++i) {
        if (after[0][i].second == "$VERSIONGUID") version_after = after[0][i + 1].second;
        if (after[0][i].second == "$FINGERPRINTGUID") {
            hdxf_test::assert_eq(after[0][i + 1].second, fingerprint, "fingerprint");
        }
    }
    hdxf_test::assert_false(version_after.empty() || version_after == version_before, "version GUID");

    // And it reads back
    hdxf::Drawing back = load(out);
    hdxf_test::assert_eq(static_cast<int>(back.entities().size()),
                         static_cast<int>(dwg.entities().size()), "entities");
}

// A changed dimension gets new geometry in its block; an unchanged one keeps AutoCAD's
TEST(roundtrip_2000_dimension_block) {
    if (!has_files({"acadsharp/sample_AC1015_ascii.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_ascii.dxf"));
    hdxf::Drawing dwg = load(in);
    std::string block_name;
    for (auto& e : dwg.entities()) {
        if (e.props.handle != "514") continue;
        e.as_dimension().def_point3.x += 10.0;
        block_name = e.as_dimension().block_name;
    }
    const hdxf::Block* b = dwg.find_block(block_name);
    hdxf_test::assert_true(b != nullptr, "dimension block");
    std::vector<std::string> old_handles;
    for (const auto& e : b->entities) old_handles.push_back(e.props.handle);

    std::vector<uint8_t> out = save(dwg, hdxf::DxfFormat::Ascii);
    dxf_structure::check_structure(out, "changed dimension");
    hdxf::Drawing back = load(out);
    const hdxf::Block* nb = back.find_block(block_name);
    hdxf_test::assert_true(nb != nullptr && !nb->entities.empty(), "regenerated");
    for (const auto& e : nb->entities) {
        hdxf_test::assert_true(std::find(old_handles.begin(), old_handles.end(), e.props.handle) ==
                               old_handles.end(), "new geometry, new handles");
    }
}

// A file without ACAD_LAYOUT or classes: a layout added gets its dictionary and its class
TEST(roundtrip_2000_missing_dictionary_and_class) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load(synthetic("r2000_large_handles.dxf")), dwg.last_error());
    dwg.add_layout("Sheet");
    std::vector<uint8_t> out = save(dwg, hdxf::DxfFormat::Ascii);
    dxf_structure::check_structure(out, "added layout");
    std::string s = hdxf_test::lf_text(out);
    hdxf_test::assert_true(s.find("  0\nCLASS\n  1\nLAYOUT\n") != std::string::npos, "LAYOUT class");
    hdxf_test::assert_true(s.find("  3\nACAD_LAYOUT\n") != std::string::npos, "ACAD_LAYOUT");
    hdxf::Drawing back = load(out);
    hdxf_test::assert_eq(static_cast<int>(back.layouts().size()), 1, "layouts");
    hdxf_test::assert_eq(back.layouts()[0]->as_layout().name, "Sheet");
}

// A pointer that led nowhere already in the file is kept as it was, not "repaired"
// (ezdxf's bin_dxf_r2000.dxf has a dictionary entry for a handle it does not have)
TEST(roundtrip_2000_existing_dangling_pointer_kept) {
    if (!has_files({"ezdxf/bin_dxf_r2000.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("ezdxf/bin_dxf_r2000.dxf"));
    hdxf::Drawing dwg = load(in);
    std::vector<uint8_t> out = save(dwg, hdxf::DxfFormat::Binary);
    hdxf_test::assert_true(records_of(out) == records_of(in), "as it was");
}

// A list that changed is written whole, in AutoCAD's order, where it stood (B12): a
// linetype's elements and an XRECORD's data
TEST(roundtrip_2000_lists) {
    if (!has_files({"acadsharp/sample_AC1015_ascii.dxf"})) return;
    std::vector<uint8_t> in = read_bytes(external("acadsharp/sample_AC1015_ascii.dxf"));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);

    // GAS_LINE: a dash put first (dash, text, gap → dash, dash, text, gap)
    for (auto& lt : dwg.linetypes()) {
        if (lt.name != "GAS_LINE") continue;
        lt.pattern.insert(lt.pattern.begin(), 0.25);
        lt.elements.insert(lt.elements.begin(), hdxf::LinetypeElement{});
        lt.total_length = 1.2;
    }
    // XRECORD 1A5 (300 ACD, 300 2025, 300 ""): a group put second
    auto& data = dwg.find_object("1A5")->as_xrecord().data;
    data.insert(data.begin() + 1, hdxf::DxfGroup{1, "added"});

    std::vector<Record> after = records_of(save(dwg, hdxf::DxfFormat::Ascii));
    auto linetype = [](const std::vector<Record>& records) {
        for (const auto& r : records) {
            if (r[0].second == "LTYPE" && value(r, 2) == "GAS_LINE") return r;
        }
        return Record{};
    };
    Record gas = linetype(after);
    Record gas_before = linetype(before);
    // From the count (73) on
    auto from = [](const Record& r, int code) {
        Record out;
        bool in = false;
        for (const auto& g : r) {
            in = in || g.first == code;
            if (in) out.push_back(g);
        }
        return out;
    };
    Record tail = from(gas, 73);
    std::vector<int> codes;
    for (const auto& g : tail) codes.push_back(g.first);
    std::vector<int> expected = {73, 40, 49, 74, 49, 74, 49, 74, 75, 340, 46, 50, 44, 45, 9, 49, 74};
    hdxf_test::assert_true(codes == expected, "LTYPE groups in AutoCAD's order");
    hdxf_test::assert_eq(std::atoi(tail[0].second.c_str()), 4, "73: the count");
    hdxf_test::assert_eq(std::strtod(tail[2].second.c_str(), nullptr), 0.25, "the new dash");
    Record tail_before = from(gas_before, 49);
    // The same values (a list written anew has HDxf's text: "0", not AutoCAD's "     0")
    for (size_t i = 0; i < tail_before.size(); ++i) {
        const auto& [code, text] = tail[4 + i];
        hdxf_test::assert_eq(code, tail_before[i].first, "the old elements' codes");
        bool number = code != 340 && code != 9;
        if (number) hdxf_test::assert_eq(std::strtod(text.c_str(), nullptr),
                                         std::strtod(tail_before[i].second.c_str(), nullptr), "value");
        else hdxf_test::assert_eq(text, tail_before[i].second, "the old elements' text");
    }
    hdxf_test::assert_true(from(gas, 2).size() - tail.size() == from(gas_before, 2).size() -
                           from(gas_before, 73).size(), "the groups before the list");

    Record x = record(after, "1A5");
    Record x_before = record(before, "1A5");
    Record x_data = from(x, 280);
    Record expected_data = {x_data[0], {300, "ACD"}, {1, "added"}, {300, "2025"}, {300, ""}};
    hdxf_test::assert_true(x_data == expected_data, "XRECORD data");
    hdxf_test::assert_eq(static_cast<int>(x.size()), static_cast<int>(x_before.size()) + 1, "one group more");
    hdxf_test::assert_true(Record(x.begin(), x.begin() + 7) == Record(x_before.begin(), x_before.begin() + 7),
                           "head as it was");
}

// ---------------------------------------------------------------------------
// Faciträttningar F1, F3, F6, F7: what AutoCAD wrote in a record is written again — a DIMENSION
// it left without a block, the RAPIDRT objects in 2000 … 2010, a block record without BLKREFS and
// a reactor list without the LEADER or HATCH that points at it. The files are written by HDxf and
// then given AutoCAD's form group by group (a technical stimulus: the facit itself is AutoCAD's
// doc/facit, which test_autocad_facit.cpp round-trips).
// ---------------------------------------------------------------------------

namespace {

using namespace dxf_text;

const std::vector<hdxf::DxfVersion> all_2000_versions = {
    hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007,
    hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018};

} // namespace

// F1: a DIMENSION AutoCAD wrote without a block (a dynamic constraint, "d1=80": no group 2, 10 and 70
// as the constraint has them) stays without one, also when its geometry is edited; a new DIMENSION
// still gets AutoCAD's block
TEST(facit_dimension_without_block_stays_without) {
    for (hdxf::DxfVersion version : all_2000_versions) {
        hdxf::Drawing built(version);
        hdxf::Entity& e = built.add_dimension(hdxf::DimensionType::Aligned, {40, -20, 0}, {40, -20, 0});
        e.as_dimension().def_point2 = {0, 0, 0};
        e.as_dimension().def_point3 = {80, 0, 0};
        std::vector<uint8_t> file = save(built, hdxf::DxfFormat::Ascii);
        std::vector<std::string> lines = text_lines(text_of(file));
        size_t at = first_record_of(lines, "DIMENSION");
        hdxf_test::assert_true(at < lines.size(), "the DIMENSION");
        erase_group(lines, at, "2");
        std::string constraint = joined(lines);

        hdxf::Drawing dwg = load(bytes_of(constraint));
        hdxf_test::assert_true(dwg.entities().back().as_dimension().block_name.empty(), "no block name");
        std::vector<uint8_t> same = save(dwg, hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(same == bytes_of(constraint), "unchanged: the file as it was: " + first_difference(same, bytes_of(constraint)));

        // Edited: the change is written, no block is added
        for (auto& x : dwg.entities()) {
            if (x.type == hdxf::EntityType::Dimension) x.as_dimension().definition_point = {41, -21, 0};
        }
        std::string edited = text_of(save(dwg, hdxf::DxfFormat::Ascii));
        std::vector<std::string> edited_lines = text_lines(edited);
        hdxf_test::assert_eq(static_cast<int>(count_records(edited_lines, "BLOCK")),
                             static_cast<int>(count_records(text_lines(constraint), "BLOCK")), "no new block");
        hdxf_test::assert_eq(static_cast<int>(count_records(edited_lines, "BLOCK_RECORD")),
                             static_cast<int>(count_records(text_lines(constraint), "BLOCK_RECORD")), "no new block record");
        hdxf::Drawing back = load(bytes_of(edited));
        const hdxf::DimensionData& d = back.entities().back().as_dimension();
        hdxf_test::assert_true(d.block_name.empty(), "still no block");
        hdxf_test::assert_eq(d.definition_point.x, 41.0, "the edited point");

        // A DIMENSION made now has its block
        hdxf::Entity& added = back.add_dimension(hdxf::DimensionType::Aligned, {10, 5, 0}, {10, 5, 0});
        added.as_dimension().def_point2 = {0, 0, 0};
        added.as_dimension().def_point3 = {20, 0, 0};
        hdxf::Drawing made = load(save(back, hdxf::DxfFormat::Ascii));
        int blocked = 0, blockless = 0;
        for (const auto& x : made.entities()) {
            if (x.type != hdxf::EntityType::Dimension) continue;
            if (x.as_dimension().block_name.empty()) ++blockless;
            else ++blocked;
        }
        hdxf_test::assert_eq(blockless, 1, "the constraint has none");
        hdxf_test::assert_eq(blocked, 1, "the new DIMENSION has a block");
    }
}

// F3: the RAPIDRT objects are written in 2000 … 2010 as AutoCAD 2025 writes them there
TEST(facit_rapidrt_objects_before_2013) {
    for (hdxf::DxfVersion version : all_2000_versions) {
        hdxf::Drawing dwg(version);
        hdxf::RapidRtRenderSettingsData preset;
        preset.name = "Preset";
        dwg.add_rapidrt_render_settings(preset);
        hdxf::RapidRtRenderEnvironmentData environment;
        environment.enabled = true;
        dwg.set_rapidrt_render_environment(environment);
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            std::vector<uint8_t> file = save(dwg, format);
            bool settings = false, env = false;
            for (const auto& r : records_of(file)) {
                settings = settings || r[0].second == "RAPIDRTRENDERSETTINGS";
                env = env || r[0].second == "RAPIDRTRENDERENVIRONMENT";
            }
            hdxf_test::assert_true(settings && env, "both objects written");
            hdxf::Drawing back = load(file);
            int found = 0;
            for (const auto& o : back.objects()) {
                found += o.type == hdxf::ObjectType::RapidRtRenderSettings || o.type == hdxf::ObjectType::RapidRtRenderEnvironment;
            }
            hdxf_test::assert_eq(found, 2, "both objects read");
            hdxf_test::assert_true(save(back, format) == file, "written again: the same bytes");
        }
    }
}

// F6: a BLOCK_RECORD AutoCAD wrote without the INSERTs of its block stays so; an INSERT added since is
// the only one listed
TEST(facit_block_record_without_blkrefs) {
    for (hdxf::DxfVersion version : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2018}) {
        hdxf::Drawing built(version);
        built.add_block("UNIT");
        built.add_insert("UNIT", {1, 1, 0});
        std::vector<uint8_t> file = save(built, hdxf::DxfFormat::Ascii);
        hdxf::Drawing once = load(file);
        std::string record = once.find_block("UNIT")->record.handle;
        std::vector<std::string> lines = text_lines(text_of(file));
        size_t at = record_at(lines, record);
        hdxf_test::assert_true(at < lines.size() && has_102_block(lines, at, "{BLKREFS"), "the writer lists the INSERT");
        erase_102_block(lines, at, "{BLKREFS");
        std::string autocad = joined(lines);

        hdxf::Drawing dwg = load(bytes_of(autocad));
        std::vector<uint8_t> same = save(dwg, hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(same == bytes_of(autocad), "unchanged: no list is added: " + first_difference(bytes_of(autocad), same));
        dwg.add_insert("UNIT", {2, 2, 0});
        std::vector<uint8_t> after = save(dwg, hdxf::DxfFormat::Ascii);
        hdxf::Drawing back = load(after);
        std::vector<std::string> now = text_lines(text_of(after));
        hdxf_test::assert_true(has_102_block(now, record_at(now, record), "{BLKREFS"), "the new INSERT is listed");
        const hdxf::Block* unit = back.find_block("UNIT");
        hdxf_test::assert_eq(static_cast<int>(unit->record.block_refs.size()), 1, "only the new INSERT");
        hdxf_test::assert_true(unit->record.block_refs[0] == back.entities().back().props.handle, "which");
    }
}

// F7: the reactors of an INSERT (a LEADER's annotation) and of a boundary object (an associative HATCH)
// stay as AutoCAD wrote them; a LEADER pointed at another annotation moves its reactor
TEST(facit_leader_and_hatch_reactors_unchanged) {
    for (hdxf::DxfVersion version : {hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2007}) {
        hdxf::Drawing built(version);
        built.add_block("NOTE");
        built.add_insert("NOTE", {0, 0, 0}).props.handle = "A001";
        built.add_insert("NOTE", {5, 5, 0}).props.handle = "A002";
        built.add_lwpolyline({{0, 0}, {10, 0}, {10, 10}, {0, 10}}, true).props.handle = "A003";
        hdxf::Entity& leader = built.add_leader({{20, 20, 0}, {2, 2, 0}});
        leader.props.handle = "A004";
        leader.as_leader().annotation_handle = "A001";
        leader.as_leader().annotation_type = 2;
        hdxf::Entity& hatch = built.add_hatch({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        hatch.props.handle = "A005";
        hatch.as_hatch().paths[0].source_handles = {"A003"};
        std::vector<uint8_t> file = save(built, hdxf::DxfFormat::Ascii);
        std::vector<std::string> lines = text_lines(text_of(file));
        size_t insert_at = record_at(lines, "A001");
        size_t frame_at = record_at(lines, "A003");
        hdxf_test::assert_true(insert_at < lines.size() && frame_at < lines.size(), "the records");
        hdxf_test::assert_true(contains(reactors_in(lines, "A001"), "A004"), "the LEADER is a reactor");
        hdxf_test::assert_true(contains(reactors_in(lines, "A003"), "A005"), "the HATCH is a reactor");
        // AutoCAD's 2000 … 2007 files lack these reactors (the annotative drawing of the facit)
        erase_102_block(lines, insert_at, "{ACAD_REACTORS", "A004");
        erase_102_block(lines, record_at(lines, "A003"), "{ACAD_REACTORS", "A005");
        std::string autocad = joined(lines);

        hdxf::Drawing dwg = load(bytes_of(autocad));
        std::vector<uint8_t> same = save(dwg, hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(same == bytes_of(autocad), "unchanged: no reactor is added: " + first_difference(bytes_of(autocad), same));

        // The LEADER now points at the other INSERT: only that INSERT gets the reactor
        for (auto& e : dwg.entities()) {
            if (e.type == hdxf::EntityType::Leader) e.as_leader().annotation_handle = "A002";
        }
        std::vector<std::string> now = text_lines(text_of(save(dwg, hdxf::DxfFormat::Ascii)));
        hdxf_test::assert_true(contains(reactors_in(now, "A002"), "A004"), "the new annotation has the reactor");
        hdxf_test::assert_false(contains(reactors_in(now, "A001"), "A004"), "the old one has none");
        hdxf_test::assert_false(contains(reactors_in(now, "A003"), "A005"), "the unchanged HATCH adds nothing");
    }
}

// A drawing built now has the full relations, each once
TEST(facit_new_drawing_has_full_relations) {
    hdxf::Drawing fresh(hdxf::DxfVersion::R2000);
    fresh.add_block("NOTE");
    fresh.add_insert("NOTE", {0, 0, 0}).props.handle = "B001";
    hdxf::Entity& leader = fresh.add_leader({{20, 20, 0}, {2, 2, 0}});
    leader.props.handle = "B002";
    leader.as_leader().annotation_handle = "B001";
    leader.as_leader().annotation_type = 2;
    std::vector<std::string> lines = text_lines(text_of(save(fresh, hdxf::DxfFormat::Ascii)));
    std::vector<std::string> reactors = reactors_in(lines, "B001");
    hdxf_test::assert_eq(static_cast<int>(std::count(reactors.begin(), reactors.end(), "B002")), 1, "the LEADER once");
    size_t block = first_record_of(lines, "BLOCK_RECORD");
    bool listed = false;
    for (size_t i = block; i + 1 < lines.size(); i += 2) {
        if (bare(lines[i]) == "331" && bare(lines[i + 1]) == "B001") listed = true;
    }
    hdxf_test::assert_true(listed, "the block lists its INSERT");
}
