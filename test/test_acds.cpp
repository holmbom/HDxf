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
// ACDSDATA typed (phase 5, step 5.4 — plan B61): the bodies' SAB (AcisData::sab) and the layouts'
// and views' preview images read from AutoCAD's 2013 files and written back as read; a removed
// body or layout takes its record along, a new one gets one, a changed one is written anew; a
// schema the drawing lacks goes last; records HDxf does not type are kept while their handle
// lives; before 2013 a body with only SAB is an error and a preview image a warning.
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

std::string value(const Record& r, int code, int occurrence = 0) {
    for (const auto& [c, v] : r) {
        if (c == code && occurrence-- == 0) return v;
    }
    return {};
}

std::vector<Record> section(const std::vector<Record>& records, const std::string& name) {
    std::vector<Record> out;
    bool in = false;
    for (const auto& r : records) {
        if (r[0].second == "SECTION") in = value(r, 2) == name;
        if (in) out.push_back(r);
        if (r[0].second == "ENDSEC") in = false;
    }
    return out;
}

// The section's schemas (their 1) and its records (schema index, 320, the data's 2, 94)
std::vector<std::string> schema_names(const std::vector<Record>& acds) {
    std::vector<std::string> out;
    for (const auto& r : acds) if (r[0].second == "ACDSSCHEMA") out.push_back(value(r, 1));
    return out;
}

struct AcdsRow {
    std::string schema;
    std::string handle;
    std::string data;
    std::string bytes;
    bool operator==(const AcdsRow&) const = default;
};

std::vector<AcdsRow> acds_rows(const std::vector<Record>& acds) {
    std::vector<AcdsRow> out;
    for (const auto& r : acds) {
        if (r[0].second == "ACDSRECORD") out.push_back({value(r, 90), value(r, 320), value(r, 2, 1), value(r, 94)});
    }
    return out;
}

// The bytes of a record's 310 groups
std::vector<uint8_t> record_bytes(const Record& r) {
    std::vector<uint8_t> out;
    for (const auto& [code, v] : r) {
        if (code != 310) continue;
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            out.push_back(static_cast<uint8_t>(std::strtoul(v.substr(i, 2).c_str(), nullptr, 16)));
        }
    }
    return out;
}

hdxf::Entity* entity(hdxf::Drawing& dwg, const std::string& handle) {
    for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    return nullptr;
}

hdxf::Object* layout_object(hdxf::Drawing& dwg, const std::string& handle) {
    for (auto* o : dwg.layouts()) if (o->handle == handle) return o;
    return nullptr;
}

bool is_png(const std::vector<uint8_t>& b) {
    const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return b.size() > 8 && std::equal(std::begin(signature), std::end(signature), b.begin());
}

bool is_sab(const std::vector<uint8_t>& b) {
    return b.size() > 15 && std::string(b.begin(), b.begin() + 15) == "ACIS BinaryFile";
}

const char* const kAscii = "acadsharp/sample_AC1027_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1027_binary.dxf";
const char* const kLibre = "libre/example_2013.dxf";
const char* const k3Ddraw = "jscad/3Ddraw.dxf";
const char* const k3Dlines = "jscad/3Dlines.dxf";
const char* const kEmpty = "jscad/empty2013.dxf";

void header_point(const hdxf::Drawing& dwg, const std::string& name, double v[3]) {
    const hdxf::HeaderVar* h = dwg.find_header(name);
    hdxf_test::assert_true(h != nullptr, name);
    if (!h) return;
    for (const auto& [code, s] : h->values) {
        if (code == 10) v[0] = std::stod(s);
        if (code == 20) v[1] = std::stod(s);
        if (code == 30) v[2] = std::stod(s);
    }
}

} // namespace

// The data read into the model: each body's SAB (94 bytes, "ACIS BinaryFile") and each layout's
// PNG (its signature, 94 bytes), in AutoCAD's files ASCII and binary; ACDSDATA is not a raw section
TEST(acds_read) {
    if (!has_files({kAscii, kBinary, kLibre, k3Ddraw})) return;
    struct Expected {
        const char* file;
        std::vector<std::pair<std::string, size_t>> bodies;
        std::vector<std::pair<std::string, size_t>> layouts;
    };
    const Expected files[] = {
        {kAscii, {{"D65", 7779}, {"D6A", 7489}, {"D69", 2876}}, {{"22", 1413}}},
        {kBinary, {{"D65", 7779}, {"D6A", 7489}, {"D69", 2876}}, {{"22", 1413}}},
        {kLibre, {{"2E1", 8798}, {"176", 1709}, {"37D", 2196}}, {{"22", 2282}}},
        {k3Ddraw, {{"FD", 7778}, {"101", 6083}, {"108", 2016}, {"10C", 690}, {"110", 2475}, {"114", 698}, {"118", 5555}},
         {{"A", 2110}}},
    };
    for (const auto& f : files) {
        hdxf::Drawing dwg = load(read_bytes(external(f.file)));
        for (const auto& s : dwg.raw_sections()) hdxf_test::assert_true(s.name != "ACDSDATA", std::string(f.file) + ": raw ACDSDATA");
        for (const auto& [h, size] : f.bodies) {
            hdxf::Entity* e = entity(dwg, h);
            hdxf_test::assert_true(e != nullptr, std::string(f.file) + " " + h);
            if (!e) continue;
            hdxf_test::assert_eq(static_cast<int>(e->as_acis().sab.size()), static_cast<int>(size), std::string(f.file) + " " + h + " SAB size");
            hdxf_test::assert_true(is_sab(e->as_acis().sab) && e->as_acis().lines.empty(), std::string(f.file) + " " + h + " SAB");
        }
        int with_preview = 0;
        for (const auto* o : dwg.layouts()) with_preview += !o->as_layout().thumbnail.empty();
        hdxf_test::assert_eq(with_preview, static_cast<int>(f.layouts.size()), std::string(f.file) + " previews");
        for (const auto& [h, size] : f.layouts) {
            hdxf::Object* o = layout_object(dwg, h);
            hdxf_test::assert_true(o != nullptr, std::string(f.file) + " layout " + h);
            if (!o) continue;
            hdxf_test::assert_eq(static_cast<int>(o->as_layout().thumbnail.size()), static_cast<int>(size), std::string(f.file) + " " + h + " PNG size");
            hdxf_test::assert_true(is_png(o->as_layout().thumbnail), std::string(f.file) + " " + h + " PNG");
        }
    }
}

// AutoCAD's files written back unchanged: byte for byte (ACadSharp's, ASCII and binary), the
// ACDSDATA section record for record (the others; their line endings differ)
TEST(acds_roundtrip) {
    if (!has_files({kAscii, kBinary, kLibre, k3Ddraw, k3Dlines, kEmpty})) return;
    for (const char* f : {kAscii, kBinary}) {
        std::vector<uint8_t> in = read_bytes(external(f));
        hdxf::Drawing dwg = load(in);
        std::vector<uint8_t> out = save(dwg, std::string(f) == kBinary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(out == in, std::string(f) + " byte for byte");
    }
    for (const char* f : {kLibre, k3Ddraw, k3Dlines, kEmpty}) {
        std::vector<uint8_t> in = read_bytes(external(f));
        hdxf::Drawing dwg = load(in);
        std::vector<Record> before = section(records_of(in), "ACDSDATA");
        std::vector<Record> after = section(records_of(save(dwg)), "ACDSDATA");
        hdxf_test::assert_true(!before.empty() && after == before, std::string(f) + ": ACDSDATA as read");
    }
}

// A removed solid takes its record along; the schema stays (KioskStation: a schema without
// records). All three removed: the ASM schema without records.
TEST(acds_removed_body) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = section(records_of(in), "ACDSDATA");
    hdxf::Drawing dwg = load(in);
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) { return e.props.handle == "D69"; }),
               ents.end());
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "a solid removed");
    std::vector<Record> after = section(records_of(out), "ACDSDATA");
    hdxf_test::assert_true(schema_names(after) == schema_names(before), "the schemas as read");
    std::vector<AcdsRow> rows = acds_rows(after);
    std::vector<AcdsRow> expected = {{"0", "22", "Thumbnail_Data", "1413"},
                                     {"1", "D65", "ASM_Data", "7779"},
                                     {"1", "D6A", "ASM_Data", "7489"}};
    hdxf_test::assert_true(rows == expected, "D69's record gone, the others as read");
    for (size_t i = 0, j = 0; i < before.size(); ++i) {
        if (value(before[i], 320) == "D69") continue;
        hdxf_test::assert_true(j < after.size() && after[j] == before[i], "record " + std::to_string(i) + " as read");
        ++j;
    }

    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) {
        return e.props.handle == "D65" || e.props.handle == "D6A";
    }), ents.end());
    after = section(records_of(save(dwg)), "ACDSDATA");
    hdxf_test::assert_true(schema_names(after) == schema_names(before), "the ASM schema stays without records");
    hdxf_test::assert_true(acds_rows(after) == std::vector<AcdsRow>{{"0", "22", "Thumbnail_Data", "1413"}}, "only the preview");
}

// A layout's preview: a new layout with one gets a record (Thumbnail schema), read back; removed,
// it takes its record along; a layout whose preview is cleared loses its record
TEST(acds_layout_preview) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    hdxf::Drawing dwg = load(in);
    std::vector<uint8_t> png = layout_object(dwg, "22")->as_layout().thumbnail;
    hdxf::Object& layout = dwg.add_layout("Preview");
    layout.as_layout().thumbnail = png;
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "a layout with a preview");
    std::vector<Record> acds = section(records_of(out), "ACDSDATA");
    std::vector<AcdsRow> rows = acds_rows(acds);
    hdxf_test::assert_eq(static_cast<int>(rows.size()), 5, "a record more");
    hdxf_test::assert_true(rows.size() == 5 && rows[1] == AcdsRow{"0", layout.handle, "Thumbnail_Data", "1413"},
                           "after the Thumbnail schema's record, before the ASM schema's");
    hdxf::Drawing back = load(out);
    hdxf::Object* read = layout_object(back, layout.handle);
    hdxf_test::assert_true(read && read->as_layout().thumbnail == png, "read back");

    // Removed (the LAYOUT and its block): its record goes
    std::string handle = layout.handle;
    std::string block_record = layout.as_layout().block_record_handle;
    auto& objs = back.objects();
    objs.erase(std::remove_if(objs.begin(), objs.end(), [&](const hdxf::Object& o) { return o.handle == handle; }), objs.end());
    auto& blocks = back.blocks();
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [&](const hdxf::Block& b) {
        return b.record.handle == block_record;
    }), blocks.end());
    std::vector<uint8_t> removed = save(back);
    dxf_structure::check_structure(removed, "the layout removed");
    hdxf_test::assert_true(acds_rows(section(records_of(removed), "ACDSDATA")) ==
                           acds_rows(section(records_of(in), "ACDSDATA")), "its record gone");

    // The Model layout's preview cleared: no record for it
    hdxf::Drawing cleared = load(in);
    layout_object(cleared, "22")->as_layout().thumbnail.clear();
    rows = acds_rows(section(records_of(save(cleared)), "ACDSDATA"));
    hdxf_test::assert_true(rows.size() == 3 && rows[0].handle == "D65", "no preview record");
}

// A view's preview (2 in the collections' 2013 files): written in the Thumbnail schema, read back
TEST(acds_view_preview) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::View v;
    v.name = "Front";
    v.height = 10;
    v.width = 10;
    v.direction = {0, 0, 1};
    v.thumbnail = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 1, 2, 3};
    dwg.views().push_back(v);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "a view with a preview");
    hdxf::Drawing back = load(out);
    hdxf_test::assert_true(back.views().size() == 1 && back.views()[0].thumbnail == v.thumbnail, "read back");
    std::vector<AcdsRow> rows = acds_rows(section(records_of(out), "ACDSDATA"));
    hdxf_test::assert_true(rows.size() == 1 && rows[0].schema == "0" && rows[0].handle == back.views()[0].handle &&
                           rows[0].bytes == "11", "its record");
    // Unchanged: written back as read
    hdxf_test::assert_true(save(back) == out, "byte for byte");
}

// A body's SAB changed: its record anew (as AutoCAD's: 94, 310 of 127 bytes); a body copied in
// with a new handle: a record after the last of the ASM schema
TEST(acds_changed_and_new_body) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = section(records_of(in), "ACDSDATA");
    hdxf::Drawing dwg = load(in);
    std::vector<uint8_t> d69 = entity(dwg, "D69")->as_acis().sab;
    entity(dwg, "D65")->as_acis().sab = d69;
    hdxf::Entity copy = *entity(dwg, "D6A");
    copy.props.handle.clear();
    copy.raw = hdxf::RawRecord();
    hdxf::Entity& added = dwg.add_entity(copy);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "a body changed and one added");
    std::vector<Record> after = section(records_of(out), "ACDSDATA");
    std::vector<AcdsRow> rows = acds_rows(after);
    hdxf_test::assert_true(rows.size() == 5, "five records");
    if (rows.size() != 5) return;
    hdxf_test::assert_true(rows[1] == AcdsRow{"1", "D65", "ASM_Data", "2876"}, "D65 anew with D69's data");
    hdxf_test::assert_true(rows[4] == AcdsRow{"1", added.props.handle, "ASM_Data", "7489"}, "the new body last");
    // The record written anew has AutoCAD's groups: the same as D69's apart from 320
    auto find = [](const std::vector<Record>& rs, const std::string& h) {
        for (const auto& r : rs) if (r[0].second == "ACDSRECORD" && value(r, 320) == h) return r;
        return Record{};
    };
    Record anew = find(after, "D65");
    Record autocad = find(before, "D69");
    for (auto& g : anew) if (g.first == 320) g.second = "D69";
    hdxf_test::assert_true(anew == autocad, "D65's record as AutoCAD writes D69's");
    hdxf_test::assert_true(record_bytes(find(after, added.props.handle)) == entity(dwg, "D6A")->as_acis().sab, "the copy's SAB");
    hdxf::Drawing back = load(out);
    hdxf_test::assert_true(entity(back, "D65")->as_acis().sab == d69, "read back");
}

// A record HDxf does not type (its 320 is not a body, layout or view: here a LINE's) is kept while
// that record lives, and goes with it; one whose 320 leads nowhere already in the file is kept
TEST(acds_other_records) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> file = read_bytes(external(kAscii));
    std::string text(file.begin(), file.end());
    size_t at = text.find("ACDSDATA");
    size_t thumb = text.find("320\n22\n", at);
    hdxf_test::assert_true(at != std::string::npos && thumb != std::string::npos, "the preview's 320");
    if (thumb == std::string::npos) return;
    std::string to_line = text;
    to_line.replace(thumb, 7, "320\n2C7\n");
    std::string nowhere = text;
    nowhere.replace(thumb, 7, "320\nFFFFF\n");

    std::vector<uint8_t> in(to_line.begin(), to_line.end());
    hdxf::Drawing dwg = load(in);
    hdxf_test::assert_true(layout_object(dwg, "22")->as_layout().thumbnail.empty(), "not the layout's");
    hdxf_test::assert_true(save(dwg) == in, "kept as read");
    entity(dwg, "2C7")->as_line().end.x = 3;
    hdxf_test::assert_eq(static_cast<int>(acds_rows(section(records_of(save(dwg)), "ACDSDATA")).size()), 4, "kept while the LINE lives");
    auto& ents = dwg.entities();
    ents.erase(std::remove_if(ents.begin(), ents.end(), [](const hdxf::Entity& e) { return e.props.handle == "2C7"; }),
               ents.end());
    std::vector<AcdsRow> rows = acds_rows(section(records_of(save(dwg)), "ACDSDATA"));
    hdxf_test::assert_true(rows.size() == 3 && rows[0].handle == "D65", "gone with the LINE");

    std::vector<uint8_t> in2(nowhere.begin(), nowhere.end());
    hdxf::Drawing dwg2 = load(in2);
    entity(dwg2, "D69")->as_acis().sab.clear();
    rows = acds_rows(section(records_of(save(dwg2)), "ACDSDATA"));
    hdxf_test::assert_true(rows.size() == 3 && rows[0].handle == "FFFFF", "kept: it led nowhere in the file");
}

// jscad's 3Ddraw: its seven bodies copied (with their SAB) into a new 2013 drawing — the schemas
// as AutoCAD 2017's new drawing with solids (ASM, Thumbnail, system), seven records, the drawing's
// extents R.9's (AutoCAD's $EXTMIN/$EXTMAX), and the section as AutoCAD's but for the preview
TEST(acds_new_drawing_bodies) {
    if (!has_files({k3Ddraw})) return;
    std::vector<uint8_t> in = read_bytes(external(k3Ddraw));
    hdxf::Drawing source = load(in);
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        int bodies = 0;
        for (const auto& e : source.entities()) {
            if (e.type != hdxf::EntityType::Solid3D) continue;
            hdxf::AcisData data = e.as_acis();
            data.history_handle.clear();            // its history stays in 3Ddraw
            dwg.add_acis(e.type, data, e.props.layer);
            ++bodies;
        }
        hdxf_test::assert_eq(bodies, 7, "3Ddraw's solids");
        std::vector<uint8_t> out = save(dwg, format);
        if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, "3Ddraw's bodies in a new 2013 drawing");
        std::vector<Record> acds = section(records_of(out), "ACDSDATA");
        std::vector<Record> autocad = section(records_of(in), "ACDSDATA");
        hdxf_test::assert_true(schema_names(acds) == schema_names(autocad), "ASM, Thumbnail, system");
        std::vector<Record> ours_schemas, autocad_schemas;
        for (const auto& r : acds) if (r[0].second != "ACDSRECORD") ours_schemas.push_back(r);
        for (const auto& r : autocad) if (r[0].second != "ACDSRECORD") autocad_schemas.push_back(r);
        hdxf_test::assert_true(ours_schemas == autocad_schemas, "the schemas group for group as AutoCAD's");
        std::vector<AcdsRow> rows = acds_rows(acds);
        std::vector<AcdsRow> autocad_rows = acds_rows(autocad);
        hdxf_test::assert_eq(static_cast<int>(rows.size()), 7, "seven records");
        for (size_t i = 0; i < rows.size() && i < 7; ++i) {
            hdxf_test::assert_true(rows[i].schema == "0" && rows[i].data == "ASM_Data" &&
                                   rows[i].bytes == autocad_rows[i].bytes, "record " + std::to_string(i));
        }
        // Each solid with 290 and its GUID (2), no 70 and SAT lines (B58)
        int solids = 0;
        for (const auto& r : records_of(out)) {
            if (r[0].second != "3DSOLID") continue;
            ++solids;
            hdxf_test::assert_true(!value(r, 290).empty() && value(r, 70).empty() && value(r, 1).empty(), "290/2 only");
        }
        hdxf_test::assert_eq(solids, 7, "seven solids");
        hdxf::Drawing back = load(out);
        int same = 0;
        for (const auto& e : back.entities()) {
            for (const auto& s : source.entities()) {
                same += e.type == hdxf::EntityType::Solid3D && s.type == e.type && s.as_acis().sab == e.as_acis().sab;
            }
        }
        hdxf_test::assert_eq(same, 7, "each SAB read back");
        double lo[3] = {}, hi[3] = {};
        header_point(back, "$EXTMIN", lo);
        header_point(back, "$EXTMAX", hi);
        const double expected[6] = {4.950490243203606, 5.999999999999996, -2.5, 29.00000000000001, 20.0, 9.0};
        for (int k = 0; k < 3; ++k) {
            hdxf_test::assert_true(std::fabs(lo[k] - expected[k]) < 1e-12 && std::fabs(hi[k] - expected[k + 3]) < 1e-12,
                                   "extents " + std::to_string(k));
        }
    }
}

// A drawing read from a 2013 file without the ASM schema (AutoCAD 2017's new drawing) that gets a
// body: the schema last, its attribute records pointing at the system schemas (Thumbnail, system,
// ASM — AutoCAD's files that got solids after their first save)
TEST(acds_schema_added_last) {
    if (!has_files({kEmpty, k3Ddraw})) return;
    std::vector<uint8_t> in = read_bytes(external(kEmpty));
    hdxf::Drawing dwg = load(in);
    hdxf::Drawing source = load(read_bytes(external(k3Ddraw)));
    hdxf::AcisData data = entity(source, "FD")->as_acis();
    data.history_handle.clear();
    dwg.add_acis(hdxf::EntityType::Solid3D, data);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "empty2013 with a solid");
    std::vector<Record> acds = section(records_of(out), "ACDSDATA");
    std::vector<Record> before = section(records_of(in), "ACDSDATA");
    std::vector<std::string> names = schema_names(acds);
    hdxf_test::assert_true(names.size() == 6 && names[5] == "AcDb3DSolid_ASM_Data", "ASM last");
    // The new schema as 3Ddraw's ASM schema, its indices the drawing's
    std::vector<Record> draw = section(records_of(read_bytes(external(k3Ddraw))), "ACDSDATA");
    Record ours = acds.size() > 6 ? acds[6] : Record{};
    Record autocad = draw.size() > 1 ? draw[1] : Record{};
    for (auto& g : autocad) {
        if (g.first == 90 || g.first == 95) g.second = std::to_string(std::stoi(g.second) == 0 ? 5 : std::stoi(g.second) - 1);
    }
    hdxf_test::assert_true(ours == autocad, "the ASM schema as AutoCAD's, its indices 5 and 1 … 4");
    for (size_t i = 0; i < before.size() - 1 && i < 6; ++i) hdxf_test::assert_true(acds[i] == before[i], "as read " + std::to_string(i));
    std::vector<AcdsRow> rows = acds_rows(acds);
    hdxf_test::assert_true(rows.size() == 2 && rows[0].handle == "A" && rows[1].schema == "5" && rows[1].bytes == "7778",
                           "the preview, then the body");
}

// Before 2013: a body with only SAB is an error (SAB → SAT is a conversion, phase 8) before
// anything is changed; a preview image is a warning, and there is no ACDSDATA
TEST(acds_before_2013) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R12, hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004,
                               hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2010}) {
        hdxf::AcisData sab;
        sab.sab = {'A', 'C', 'I', 'S'};
        hdxf::Drawing dwg(v);
        dwg.add_acis(hdxf::EntityType::Solid3D, sab);
        hdxf_test::assert_true(dwg.save_to_memory().empty(), "refused");
        hdxf_test::assert_eq(dwg.last_error(), std::string("A 3DSOLID with only SAB data requires DXF 2013 or later"), "the error");
        hdxf_test::assert_true(dwg.entities().size() == 1 && dwg.entities()[0].as_acis().lines.empty(), "nothing changed");
        // With SAT lines too the lines are the data
        dwg.entities()[0].as_acis().lines = {"700 0 1 0"};
        if (v != hdxf::DxfVersion::R12) hdxf_test::assert_false(dwg.save_to_memory().empty(), dwg.last_error());
    }
    hdxf::Drawing region(hdxf::DxfVersion::R2010);
    hdxf::AcisData sab;
    sab.sab = {1};
    region.add_acis(hdxf::EntityType::Region, sab);
    hdxf_test::assert_true(region.save_to_memory().empty(), "REGION refused");
    hdxf_test::assert_eq(region.last_error(), std::string("A REGION with only SAB data requires DXF 2013 or later"), "REGION");

    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    dwg.add_layout("Sheet").as_layout().thumbnail = {0x89, 'P', 'N', 'G'};
    std::vector<uint8_t> out = save(dwg);
    std::string text(out.begin(), out.end());
    hdxf_test::assert_true(text.find("ACDSDATA") == std::string::npos, "no ACDSDATA");
    hdxf_test::assert_true(dwg.warnings().size() == 1 &&
                           dwg.warnings()[0] == "1 preview image(s) of layouts and views not written: they need DXF 2013 or later",
                           "the warning");
    dwg.set_version(hdxf::DxfVersion::R2013);
    std::vector<AcdsRow> rows = acds_rows(section(records_of(save(dwg)), "ACDSDATA"));
    hdxf_test::assert_true(rows.size() == 1 && rows[0].data == "Thumbnail_Data" && dwg.warnings().empty(), "in 2013");
}
