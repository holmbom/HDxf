#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// A record written anew (its record as read dropped, or changed) is AutoCAD's record (phase 5,
// step 5.9): what HDxf lost or wrote otherwise in 2000 … 2013 — BLOCK's entity properties and
// description, BLOCK_RECORD's preview, extension dictionaries of LAYER, VIEW, STYLE and VPORT,
// VPORT and LTYPE XDATA, a layer's AcCmTransparency 1071 0, reactors to records HDxf does not
// type (and DIMSTYLE's), LTYPE's 340 to a shape file's style, a dimension's 42 (in its plane, the
// angle through the arc, DIMLFAC), MTEXT's parts, tiny numbers, ARC's 210, a rational spline's
// weights, 380, DIMSTYLE 90 and ByBlock linetypes, VIEWPORT's 348 in AcDbEntity.
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

std::string value(const Record& r, int code) {
    for (const auto& [c, v] : r) if (c == code) return v;
    return {};
}

void drop_entities(std::vector<hdxf::Entity>& list) {
    for (auto& e : list) {
        if (e.type == hdxf::EntityType::Unknown) continue;
        e.raw = hdxf::RawRecord();
        if (e.type == hdxf::EntityType::Insert) {
            for (auto& a : e.as_insert().attributes) a.raw = hdxf::RawRecord();
            e.as_insert().seqend_raw = hdxf::RawRecord();
        }
        if (e.type == hdxf::EntityType::Polyline) {
            for (auto& v : e.as_polyline().vertices) v.raw = hdxf::RawRecord();
            e.as_polyline().seqend_raw = hdxf::RawRecord();
        }
    }
}

// Every record HDxf types written anew
void drop_records(hdxf::Drawing& dwg) {
    drop_entities(dwg.entities());
    for (auto& b : dwg.blocks()) {
        drop_entities(b.entities);
        b.raw = hdxf::RawRecord();
        b.endblk_raw = hdxf::RawRecord();
        b.record.raw = hdxf::RawRecord();
    }
    for (auto& x : dwg.layers()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.linetypes()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.text_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.dim_styles()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.views()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.ucs_list()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.vports()) x.raw = hdxf::RawRecord();
    for (auto& x : dwg.app_ids()) x.raw = hdxf::RawRecord();
    for (auto& o : dwg.objects()) {
        if (o.type != hdxf::ObjectType::Unknown) o.raw = hdxf::RawRecord();
    }
}

// AutoCAD's file and the same written with every typed record anew
struct Anew {
    std::vector<Record> theirs, ours;
};

Anew written_anew(const std::string& name, hdxf::DxfFormat format = hdxf::DxfFormat::Ascii) {
    std::vector<uint8_t> in = read_bytes(external(name));
    hdxf::Drawing dwg = load(in);
    drop_records(dwg);
    return {records_of(in), records_of(save(dwg, format))};
}

// Equal, numbers to AutoCAD's 16 digits (a value AutoCAD wrote from more digits than the file has)
bool same_record(const Record& a, const Record& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first) return false;
        if (a[i].second == b[i].second) continue;
        char* end_a = nullptr;
        char* end_b = nullptr;
        double x = std::strtod(a[i].second.c_str(), &end_a), y = std::strtod(b[i].second.c_str(), &end_b);
        if (*end_a != '\0' || *end_b != '\0' || a[i].second.empty()) return false;
        if (std::fabs(x - y) > 1e-12 * std::max(1.0, std::fabs(x))) return false;
    }
    return true;
}

void assert_autocads(const Anew& a, const std::string& handle, const std::string& what) {
    Record theirs = record(a.theirs, handle), ours = record(a.ours, handle);
    hdxf_test::assert_false(theirs.empty(), what + ": in AutoCAD's file");
    hdxf_test::assert_true(same_record(theirs, ours), what + " " + handle + ": AutoCAD's record");
}

} // namespace

// BLOCK and ENDBLK keep their entity properties (62/420, 6, 370, 48, 60), BLOCK its description
// (4) and BLOCK_RECORD its preview (310) — ACadSharp's my_block_v2 (color 193 / RGB 10175212),
// its dynamic block's description, in 2000, 2004, 2007, 2010 and 2013
TEST(written_anew_block_properties) {
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                             "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1024_ascii.dxf",
                             "acadsharp/sample_AC1027_ascii.dxf", "acadsharp/sample_AC1027_binary.dxf"}) {
        if (!has_files({name})) return;
        bool binary = std::string(name).find("binary") != std::string::npos;
        Anew a = written_anew(name, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        for (const char* h : {"9D2", "9D3", "A52", "A53", "77F", "A7E"}) assert_autocads(a, h, name);
        hdxf_test::assert_eq(value(record(a.ours, "9D2"), 62), "193", "BLOCK's color");
        hdxf::Drawing dwg = load(read_bytes(external(name)));
        const hdxf::Block* dim = nullptr;
        const hdxf::Block* mine = dwg.find_block("my_block_v2");
        for (const auto& blk : dwg.blocks()) if (blk.handle == "9D2") dim = &blk;
        hdxf_test::assert_true(dim && dim->block_color && dim->block_color->aci == 193 && dim->endblk_color && dim->endblk_color->aci == 193, "BLOCK 9D2 and ENDBLK 9D3 colors typed, each its own");
        hdxf_test::assert_true(mine && !mine->record.preview.empty(), "my_block_v2's preview typed");
        bool described = false;
        for (const auto& blk : dwg.blocks()) described = described || blk.description == "My dynamic block description.";
        hdxf_test::assert_true(described || std::string(name).find("AC1015") != std::string::npos, "description");
    }
}

// Extension dictionaries of LAYER and VIEW, VPORT's XDATA (ACAD_NAV_VCDISPLAY) and a layer's
// AcCmTransparency 1071 0 stay — ACadSharp's, LibreDWG's and AutoCAD 2017's files
TEST(written_anew_table_entries) {
    struct Case { const char* file; std::vector<const char*> handles; };
    const Case cases[] = {
        {"acadsharp/sample_AC1018_ascii.dxf", {"10", "302", "94", "275", "278"}},
        {"acadsharp/sample_AC1027_ascii.dxf", {"10", "302", "94", "275"}},
        {"libre/example_2013.dxf", {"10", "89", "8A", "25A"}},
        {"jscad/empty2013.dxf", {"4", "25", "7D"}},
    };
    for (const auto& c : cases) {
        if (!has_files({c.file})) return;
        Anew a = written_anew(c.file);
        for (const char* h : c.handles) assert_autocads(a, h, c.file);
    }
    hdxf::Drawing dwg = load(read_bytes(external("acadsharp/sample_AC1018_ascii.dxf")));
    for (const auto& l : dwg.layers()) {
        if (l.handle == "10") hdxf_test::assert_eq(l.xdictionary, "E6", "layer 0's extension dictionary");
    }
    hdxf_test::assert_eq(dwg.vports()[0].xdata[0].app_name, "ACAD_NAV_VCDISPLAY");
}

// An entity's reactors as read — to a DIMASSOC (CIRCLE, DIMENSION, LINE), to a hatch that no
// longer lists it (LWPOLYLINE), a VERTEX's, a surface's to its ACDBASSOCDEPENDENCY — and a
// DIMSTYLE's to its dimensions; a reactor whose record is removed goes (B19)
TEST(written_anew_reactors) {
    if (!has_files({"acadsharp/sample_AC1018_ascii.dxf", "libre/example_2010.dxf", "jscad/3Dlines.dxf"})) return;
    Anew a = written_anew("acadsharp/sample_AC1018_ascii.dxf");
    for (const char* h : {"51D", "51E", "51F", "522", "9CD", "36C", "36F", "372", "39A", "994", "C88"}) {
        assert_autocads(a, h, "ACadSharp 2004");
    }
    Anew libre = written_anew("libre/example_2010.dxf");
    for (const char* h : {"398", "41A", "41E", "419", "37D", "27"}) assert_autocads(libre, h, "LibreDWG 2010");
    Anew lines = written_anew("jscad/3Dlines.dxf");
    for (const char* h : {"10D", "11C", "FC", "FE", "F5", "FD"}) assert_autocads(lines, h, "3Dlines");

    // The DIMASSOC 521 removed: the CIRCLE 51D's reactor to it goes, the file stays whole
    hdxf::Drawing dwg = load(read_bytes(external("acadsharp/sample_AC1018_ascii.dxf")));
    auto& objects = dwg.objects();
    for (size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].handle == "521") { objects.erase(objects.begin() + static_cast<long>(i)); break; }
    }
    drop_records(dwg);
    std::vector<uint8_t> out = save(dwg);
    dxf_structure::check_structure(out, "DIMASSOC removed");
    Record circle = record(records_of(out), "51D");
    for (const auto& [c, v] : circle) hdxf_test::assert_true(!(c == 330 && v == "521"), "no reactor to 521");
}

// LTYPE's 340 to the shape file's style, which has no name (ACadSharp's ltypeshp.shx, 27F) — not
// to Standard; a new drawing's element names such a style by its file
TEST(written_anew_linetype_shape_style) {
    if (!has_files({"acadsharp/sample_AC1027_ascii.dxf"})) return;
    Anew a = written_anew("acadsharp/sample_AC1027_ascii.dxf");
    for (const char* h : {"6BF", "6CC", "6D9"}) assert_autocads(a, h, "ACadSharp 2013");
    hdxf::Drawing dwg = load(read_bytes(external("acadsharp/sample_AC1027_ascii.dxf")));
    for (const auto& lt : dwg.linetypes()) {
        if (lt.handle == "6BF") hdxf_test::assert_eq(lt.elements[0].style, "ltypeshp.shx", "by its file");
    }

    hdxf::Drawing fresh(hdxf::DxfVersion::R2010);
    hdxf::TextStyle& shapes = fresh.add_text_style("");
    shapes.flags = 1;
    shapes.font_file = "ltypeshp.shx";
    hdxf::Linetype lt;
    lt.name = "TRACKS";
    lt.pattern = {0.15, 0.15};
    lt.total_length = 0.3;
    hdxf::LinetypeElement el;
    el.type = 4;
    el.shape_number = 130;
    el.style = "ltypeshp.shx";
    lt.elements = {el, hdxf::LinetypeElement{}};
    fresh.linetypes().push_back(lt);
    std::vector<Record> recs = records_of(save(fresh));
    std::string style_handle, element_style;
    for (const auto& r : recs) {
        if (r[0].second == "STYLE" && value(r, 3) == "ltypeshp.shx") style_handle = value(r, 5);
        if (r[0].second == "LTYPE" && value(r, 2) == "TRACKS") element_style = value(r, 340);
    }
    hdxf_test::assert_false(style_handle.empty(), "the shape style");
    hdxf_test::assert_eq(element_style, style_handle, "340 to it");
}

// A dimension's 42: the length in its plane (LibreDWG 37E: 13 at z 1), the angle through the arc
// (43B: 108°, not 72°), scaled by DIMLFAC; and nothing else of LibreDWG's dimensions changes
TEST(written_anew_dimension_measurement) {
    if (!has_files({"libre/example_2010.dxf", "libre/example_2013.dxf"})) return;
    for (const char* name : {"libre/example_2010.dxf", "libre/example_2013.dxf"}) {
        Anew a = written_anew(name);
        for (const char* h : {"37E", "43B", "420", "453", "467", "47F", "497", "4AF"}) assert_autocads(a, h, name);
        hdxf_test::assert_true(std::fabs(std::atof(value(record(a.ours, "43B"), 42).c_str()) - 1.8849555921538759) < 1e-12,
                               "108°");
    }
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    hdxf::DimStyle& detail = dwg.add_dim_style("DETAIL");
    detail.dimlfac = 0.2;
    hdxf::Entity& d = dwg.add_dimension(hdxf::DimensionType::Aligned, {2.5, 1, 0}, {1.25, 1.5, 0});
    d.as_dimension().dim_style = "DETAIL";
    d.as_dimension().def_point2 = {0, 0, 0};
    d.as_dimension().def_point3 = {2.5, 0, 0};
    std::vector<Record> recs = records_of(save(dwg));
    hdxf_test::assert_eq(value(record(recs, d.props.handle), 42), "0.5", "DIMLFAC 0.2: 2.5 × 0.2");
}

// MTEXT in parts of 250 characters from 2004 (^J one), 250 as written in 2000 (^J two) —
// ACadSharp's 779 in every version as AutoCAD's
TEST(written_anew_mtext_parts) {
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                             "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1027_ascii.dxf"}) {
        if (!has_files({name})) return;
        Anew a = written_anew(name);
        assert_autocads(a, "779", name);
    }
    // Characters above U+FFFF count two (UTF-16 units; written as their two \U+ escapes, B55)
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    std::string text;
    for (int i = 0; i < 130; ++i) text += "a\xF0\x9F\x98\x80";       // a😀
    dwg.add_mtext({0, 0, 0}, text);
    std::vector<Record> recs = records_of(save(dwg));
    std::string first;
    for (int i = 0; i < 83; ++i) first += "a\\U+D83D\\U+DE00";
    first += "a";
    for (const auto& r : recs) {
        if (r[0].second == "MTEXT") hdxf_test::assert_eq(value(r, 3), first, "250 units: 83 pairs and an a");
    }
}

// Numbers below 1e-15 as AutoCAD writes them (16 decimals; an MLINE's directions with an
// exponent), ARC's 210 in AcDbCircle, a rational spline's weight after each point, VIEWPORT's
// 348 where the file had it, 380, DIMSTYLE 90 and its ByBlock linetypes — ACadSharp's HATCH and
// MLINE in every version, a new drawing's ARC and SPLINE
TEST(written_anew_numbers_and_order) {
    for (const char* name : {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                             "acadsharp/sample_AC1024_ascii.dxf", "acadsharp/sample_AC1027_ascii.dxf"}) {
        if (!has_files({name})) return;
        Anew a = written_anew(name);
        for (const char* h : {"BBB", "BC4", "38B"}) assert_autocads(a, h, name);
    }
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::Entity& arc = dwg.add_arc({1, 2, 0}, 3.0, 10.0, 80.0);
    arc.props.extrusion = {0.0, 0.0, -1.0};
    arc.props.plot_style_type = 1;
    hdxf::DimStyle& am = dwg.add_dim_style("AM");
    am.dimarcsym = 1;
    am.dimltype = "ByBlock";
    std::vector<Record> recs = records_of(save(dwg));
    Record r = record(recs, arc.props.handle);
    std::vector<int> codes;
    for (const auto& g : r) codes.push_back(g.first);
    std::vector<int> expected = {0, 5, 330, 100, 8, 380, 100, 10, 20, 30, 40, 210, 220, 230, 100, 50, 51};
    hdxf_test::assert_true(codes == expected, "ARC: 380 in AcDbEntity, 210 in AcDbCircle");
    for (const auto& x : recs) {
        if (x[0].second == "DIMSTYLE" && value(x, 2) == "AM") {
            hdxf_test::assert_eq(value(x, 90), "1", "DIMARCSYM");
            hdxf_test::assert_false(value(x, 345).empty(), "ByBlock linetype");
        }
    }
    hdxf::Drawing back = load(save(dwg));
    for (const auto& ds : back.dim_styles()) {
        if (ds.name == "AM") {
            hdxf_test::assert_eq(ds.dimarcsym, 1);
            hdxf_test::assert_eq(ds.dimltype, "ByBlock");
        }
    }
    hdxf_test::assert_eq(back.entities()[0].props.plot_style_type, 1, "380 read");
}

// Every typed record of AutoCAD's files written anew is AutoCAD's record — but the proxy
// graphics AutoCAD computes itself (MULTILEADER, IMAGE, WIPEOUT, MESH, HELIX: its own numerics and
// padding) and MTEXT's \U+00B0 in 2007+, which AutoCAD's own reading makes the character (plan
// 5.9, the chain): ACadSharp 2000 … 2013 ASCII and binary, LibreDWG 2007 … 2013, AutoCAD 2017's
// new drawings and 3Ddraw, 3Dlines, 3Dmesh01
TEST(written_anew_every_record_as_autocad) {
    const char* files[] = {"acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1018_ascii.dxf",
                           "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1024_ascii.dxf",
                           "acadsharp/sample_AC1027_ascii.dxf", "acadsharp/sample_AC1015_binary.dxf",
                           "acadsharp/sample_AC1027_binary.dxf", "libre/example_2007.dxf",
                           "libre/example_2010.dxf", "libre/example_2013.dxf", "jscad/empty2007.dxf",
                           "jscad/empty2010.dxf", "jscad/empty2013.dxf", "jscad/3Ddraw.dxf", "jscad/3Dlines.dxf"};
    // A multiline attribute's TEXT mirror before 2018 is laid out in its font: without Windows' fonts (Linux) the
    // attributes are not compared
    const char* windir = std::getenv("WINDIR");
    std::error_code ec;
    const bool fonts = std::filesystem::exists(std::filesystem::path(windir ? windir : "C:/Windows") / "Fonts" / "arial.ttf", ec);
    for (const char* name : files) {
        if (!has_files({name})) return;
        bool binary = std::string(name).find("binary") != std::string::npos;
        Anew a = written_anew(name, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        int checked = 0;
        for (const auto& theirs : a.theirs) {
            const std::string& type = theirs[0].second;
            if (type == "SECTION" || type == "MULTILEADER" || type == "IMAGE" || type == "WIPEOUT" ||
                type == "MESH" || type == "HELIX" || type == "LAYOUT") {
                continue;           // LAYOUT: its 143 … AutoCAD's 16 digits of a computed value
            }
            if (!fonts && (type == "ATTRIB" || type == "ATTDEF")) continue;
            std::string handle = value(theirs, type == "DIMSTYLE" ? 105 : 5);
            if (handle.empty()) continue;
            Record ours = record(a.ours, handle);
            bool escaped = type == "MTEXT" && value(theirs, 1).find("\\U+00B0") != std::string::npos;
            if (escaped) continue;
            ++checked;
            hdxf_test::assert_true(same_record(theirs, ours), std::string(name) + " " + type + " " + handle);
        }
        hdxf_test::assert_true(checked > 20, std::string(name) + ": records compared");
    }
}
