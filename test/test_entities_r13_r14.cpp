#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "group_codes.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Entities in DXF R13 (AC1012) and R14 (AC1014), plan phase 7, steps 7.11 – 7.13 (B93, B94, B97):
// the common head, BLOCK/ENDBLK, INSERT/ATTRIB/ATTDEF, the R12 types, R13's family (ELLIPSE, XLINE,
// RAY, TOLERANCE, SPLINE, MTEXT, LEADER, MLINE), LWPOLYLINE (R14) and POLYLINE/VERTEX/SEQEND.
//
// No golden files. The expected group order is what AutoCAD's own R13 and R14 files have
// (libre/r13_v.dxf, gvsig/dxfprueba.dxf, ezdxf/small_r13.dxf, spatialauxetic/simp.DXF, jscad/pyramid-base.dxf and the
// local-r13r14 copies, doc/reference/hdxf_r13_r14_format.md 7.1 – 7.6) and, where no AutoCAD file has the
// type, Autodesk's R13 and R14 references. The last test writes entities read from those files anew and
// compares them group by group with the file's own records.
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Point3;
using hdxf::internal::BinaryCodeWidth;
using hdxf::internal::GroupCode;
using hdxf::internal::GroupReader;
using Rec = dxf_structure::Rec;
using Codes = std::vector<int>;

const DxfVersion older_versions[] = {DxfVersion::R13, DxfVersion::R14};
const DxfFormat both_formats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfVersion v) { return v == DxfVersion::R13 ? "R13" : "R14"; }
std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

// Every version and format of the older family: fn(version, format, "R14 binary")
template <typename Fn>
void for_each_target(Fn fn) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat f : both_formats) fn(v, f, name_of(v) + " " + name_of(f));
    }
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), what + ": " + dwg.last_error());
    return out;
}

// The records of a file, ASCII or binary, with their sections
std::vector<Rec> records_of(const std::vector<uint8_t>& data) {
    std::istringstream in(std::string(data.begin(), data.end()), std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();
    std::vector<Rec> out;
    std::string section;
    GroupCode gc;
    while (reader.read(gc)) {
        std::string value = dxf_structure::number_text(gc.code, gc.value);
        if (gc.code == 0) out.push_back({section, {}});
        if (out.empty()) continue;
        out.back().groups.push_back({gc.code, value});
        if (gc.code == 2 && out.back().type() == "SECTION") section = value;
        if (gc.code == 0 && value == "ENDSEC") section.clear();
    }
    hdxf_test::assert_true(reader.error().empty(), "reading the file: " + reader.error());
    return out;
}

std::vector<const Rec*> in_section(const std::vector<Rec>& recs, const std::string& section) {
    std::vector<const Rec*> out;
    for (const auto& r : recs) {
        if (r.section == section && r.type() != "SECTION" && r.type() != "ENDSEC") out.push_back(&r);
    }
    return out;
}

// The records of a type in ENTITIES and BLOCKS, in file order
std::vector<const Rec*> of_type(const std::vector<Rec>& recs, const std::string& type) {
    std::vector<const Rec*> out;
    for (const auto& r : recs) {
        if ((r.section == "ENTITIES" || r.section == "BLOCKS") && r.type() == type) out.push_back(&r);
    }
    return out;
}

// The groups of a record without what the writer chooses (the handle, the owner, the reactor and
// extension dictionary groups) and without its XDATA
std::vector<dxf_structure::Group> plain(const Rec& r) {
    std::vector<dxf_structure::Group> out;
    bool in_102 = false;
    for (const auto& g : r.groups) {
        if (g.first >= 1000) break;
        if (g.first == 102) {
            in_102 = !g.second.empty() && g.second[0] == '{';
            continue;
        }
        if (in_102 || g.first == 5 || g.first == 330) continue;
        out.push_back(g);
    }
    return out;
}

Codes codes_of(const Rec& r) {
    Codes out;
    for (const auto& g : plain(r)) out.push_back(g.first);
    return out;
}

std::string codes_text(const Codes& c) {
    std::string t;
    for (int x : c) t += std::to_string(x) + " ";
    return t;
}

// The owner group (330 outside 102 braces): R13/R14 files of AutoCAD's own native family have none
// (format profile D1); when the writer writes it, it must name the right record
std::string owner_of(const Rec& r) {
    bool in_102 = false;
    for (const auto& g : r.groups) {
        if (g.first == 102) in_102 = !g.second.empty() && g.second[0] == '{';
        else if (!in_102 && g.first == 330) return g.second;
    }
    return {};
}

std::string handle_of(const Rec& r) { return r.value(r.type() == "DIMSTYLE" ? 105 : 5); }

// A value of a code from the subclass `marker` on
std::string value_after(const Rec& r, const std::string& marker, int code) {
    bool in = false;
    for (const auto& g : r.groups) {
        if (g.first == 100) in = g.second == marker;
        if (in && g.first == code) return g.second;
    }
    return {};
}

// The subclass markers of a record: "AcDbEntity AcDbLine"
std::string markers_of(const Rec& r) {
    std::string out;
    for (const auto& g : r.groups) {
        if (g.first == 100) out += (out.empty() ? "" : " ") + g.second;
    }
    return out;
}

std::vector<std::string> values_of(const Rec& r, int code) {
    std::vector<std::string> out;
    for (const auto& g : plain(r)) if (g.first == code) out.push_back(g.second);
    return out;
}

bool has_code(const Rec& r, int code) {
    for (const auto& g : plain(r)) if (g.first == code) return true;
    return false;
}

double num(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

bool near(double a, double b) { return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(a)); }

// The BLOCK_RECORD handle of a block, by its written name
std::string record_handle(const std::vector<Rec>& recs, const std::string& name) {
    for (const auto* r : in_section(recs, "TABLES")) {
        if (r->type() == "BLOCK_RECORD" && r->value(2) == name) return handle_of(*r);
    }
    return {};
}

// A new R13/R14 drawing is family N: no entity has an owner 330 (format profile D1; `expected` is the
// record the owner would be in family A2000, see entities_r13_r14_read_a2000_file_owner_follows_the_file)
void expect_owner_if_written(const Rec& r, const std::string& expected, const std::string& what) {
    (void)expected;
    hdxf_test::assert_eq(owner_of(r), std::string(), what + ": no owner on " + r.type() + " " + handle_of(r));
}

hdxf::Entity line_entity(Point3 a, Point3 b, const std::string& layer = "0") {
    hdxf::Drawing tmp;
    return tmp.add_line(a, b, layer);
}

// A drawing from the bytes written
hdxf::Drawing read_back(const std::vector<uint8_t>& data, const std::string& what) {
    hdxf::Drawing back;
    hdxf_test::assert_true(back.load_from_memory(data.data(), data.size()), what + ": reading back: " + back.last_error());
    return back;
}

// The error a drawing's save gives; the drawing is untouched (B98: no handle is given before the checks)
std::string save_error(hdxf::Drawing& dwg, DxfFormat format = DxfFormat::Ascii) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_true(out.empty(), "a save that must fail wrote " + std::to_string(out.size()) + " bytes");
    return dwg.last_error();
}

// Every handle of the file once, $HANDSEED above all of them
void expect_unique_handles(const std::vector<Rec>& recs, const std::string& what) {
    std::set<uint64_t> seen;
    uint64_t max = 0;
    std::string seed;
    for (const auto& r : recs) {
        const std::string& t = r.type();
        if (t == "SECTION" && r.value(2) == "HEADER") {
            for (size_t i = 0; i + 1 < r.groups.size(); ++i) {
                if (r.groups[i].first == 9 && r.groups[i].second == "$HANDSEED") seed = r.groups[i + 1].second;
            }
            continue;
        }
        if (t == "SECTION" || t == "ENDSEC" || t == "ENDTAB" || t == "EOF" || t == "CLASS") continue;
        std::string h = handle_of(r);
        if (h.empty()) continue;
        uint64_t v = dxf_structure::handle_value(h);
        hdxf_test::assert_true(seen.insert(v).second, what + ": handle " + h + " used twice (" + t + ")");
        max = std::max(max, v);
    }
    hdxf_test::assert_false(seed.empty(), what + ": no $HANDSEED");
    hdxf_test::assert_true(dxf_structure::handle_value(seed) > max, what + ": $HANDSEED " + seed + " not above all handles");
}

std::filesystem::path external(const std::string& dir, const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / dir / name;
}

std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

// ---------------------------------------------------------------------------
// 7.11: the common head, in model space, paper space and a block (B93, B97)
// ---------------------------------------------------------------------------

TEST(entities_r13_r14_common_head_in_model_paper_and_block) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        d.add_layer("L1", hdxf::Color::red());
        d.add_linetype("DASHED", "Dashed", {0.5, -0.25});
        hdxf::Entity& model = d.add_line({0, 0, 0}, {1, 1, 0}, "L1");
        model.props.color = hdxf::Color::from_aci(3);
        model.props.linetype = "DASHED";
        model.props.linetype_scale = 2.0;
        model.props.visible = false;
        model.props.thickness = 2.0;
        hdxf::Entity& paper = d.add_line({0, 0, 0}, {2, 2, 0}, "0");
        paper.props.paper_space = 1;
        hdxf::Block& blk = d.add_block("BLK");
        blk.entities.push_back(line_entity({5, 5, 0}, {6, 6, 0}));

        std::vector<Rec> recs = records_of(save(d, f, what));
        std::string model_record = record_handle(recs, "*MODEL_SPACE");
        std::string paper_record = record_handle(recs, "*PAPER_SPACE");
        std::string blk_record = record_handle(recs, "BLK");
        hdxf_test::assert_false(model_record.empty() || paper_record.empty() || blk_record.empty(), what + ": BLOCK_RECORDs");

        std::vector<const Rec*> lines = of_type(recs, "LINE");
        hdxf_test::assert_eq(3, static_cast<int>(lines.size()), what + ": three LINE records");
        for (const Rec* r : lines) {
            const std::string layer = r->value(8);
            std::string value;
            for (const auto& g : r->groups) {
                if (g.first == 370 || g.first == 380 || g.first == 390 || g.first == 410 || g.first == 440 ||
                    g.first == 347 || g.first == 284 || g.first == 92 || g.first == 160) {
                    hdxf_test::assert_true(false, what + ": 2000's group " + std::to_string(g.first) + " in a " + r->type());
                }
            }
            if (layer == "L1") {
                // 8, 6, 62, 48, 60 in the AcDbEntity part (R13:Common Entity Group Codes; libre/r13_v.dxf, gvsig/dxfprueba.dxf),
                // the type's own part after it: AcDbLine, thickness first
                hdxf_test::assert_eq(codes_text({0, 100, 8, 6, 62, 48, 60, 100, 39, 10, 20, 30, 11, 21, 31}),
                                     codes_text(codes_of(*r)), what + ": the head of a LINE with every property");
                hdxf_test::assert_eq("AcDbEntity", r->value(100), what + ": subclass marker");
                hdxf_test::assert_eq("DASHED", r->value(6), what);
                hdxf_test::assert_eq(3, std::atoi(r->value(62).c_str()), what);
                hdxf_test::assert_true(near(2.0, num(r->value(48))), what + ": 48");
                hdxf_test::assert_eq(1, std::atoi(r->value(60).c_str()), what + ": 60 1 for an invisible entity");
                expect_owner_if_written(*r, model_record, what);
            } else if (has_code(*r, 67)) {
                hdxf_test::assert_eq(codes_text({0, 100, 67, 8, 100, 10, 20, 30, 11, 21, 31}), codes_text(codes_of(*r)),
                                     what + ": a paper space LINE has 67 1 and nothing else extra");
                hdxf_test::assert_eq(1, std::atoi(r->value(67).c_str()), what);
                expect_owner_if_written(*r, paper_record, what);
            } else {
                hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 11, 21, 31}), codes_text(codes_of(*r)),
                                     what + ": a LINE of a block");
                expect_owner_if_written(*r, blk_record, what);
            }
        }
        expect_unique_handles(recs, what);

        // The block's LINE is between BLOCK and ENDBLK
        const auto blocks = in_section(recs, "BLOCKS");
        size_t begin = 0, end = 0;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i]->type() == "BLOCK" && blocks[i]->value(2) == "BLK") begin = i;
            if (blocks[i]->type() == "ENDBLK" && begin != 0 && end == 0) end = i;
        }
        hdxf_test::assert_true(begin != 0 && end == begin + 2 && blocks[begin + 1]->type() == "LINE",
                               what + ": BLOCK BLK, its LINE, ENDBLK");
    });
}

TEST(entities_r13_r14_lineweight_plot_style_and_newer_properties_are_errors) {
    for (DxfVersion v : older_versions) {
        const std::string what = name_of(v);
        struct Case {
            std::string message;
            std::function<void(hdxf::Drawing&)> build;
        };
        const std::vector<Case> cases = {
            {"Lineweight requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.lineweight = 50; }},
            {"Lineweight requires DXF 2000 or later",
             [](hdxf::Drawing& d) {
                 hdxf::Block& b = d.add_block("B");
                 b.entities.push_back(line_entity({0, 0, 0}, {1, 0, 0}));
                 b.entities.back().props.lineweight = 30;
             }},
            {"Lineweight requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_block("B").lineweight = 30; }},
            {"Lineweight requires DXF 2000 or later",
             [](hdxf::Drawing& d) {
                 hdxf::Block& b = d.add_block("B");
                 hdxf::AttdefData a;
                 a.tag = "T";
                 hdxf::Entity e;
                 e.type = hdxf::EntityType::Attdef;
                 e.data = a;
                 b.entities.push_back(e);
                 hdxf::Entity& ins = d.add_insert("B", {0, 0, 0});
                 hdxf::InsertAttribute at;
                 at.data.tag = "T";
                 at.props.lineweight = 35;
                 ins.as_insert().attributes.push_back(at);
             }},
            {"Plot style requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.plot_style_type = 2; }},
            {"Plot style requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_circle({0, 0, 0}, 1).props.plot_style_handle = "1A"; }},
            {"Transparency requires DXF 2004 or later",
             [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.transparency = hdxf::Transparency::from_alpha(100); }},
            {"True color requires DXF 2004 or later",
             [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.color = hdxf::Color::from_rgb(10, 20, 30); }},
            {"A material requires DXF 2007 or later",
             [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.material_handle = "2A"; }},
            {"An entity's shadow mode requires DXF 2007 or later",
             [](hdxf::Drawing& d) { d.add_line({0, 0, 0}, {1, 0, 0}).props.shadow_mode = 1; }},
            {"A block's description requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_block("B").description = "d"; }},
            {"An MTEXT background requires DXF 2004 or later",
             [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "t").as_mtext().background.flags = 1; }},
            {"An MTEXT with columns requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "t").as_mtext().columns.emplace(); }},
            {"An MTEXT's defined height requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "t").as_mtext().defined_height = 7.0; }},
            {"An MTEXT's line spacing requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "t").as_mtext().line_spacing_factor = 2.0; }},
            {"An MTEXT's line spacing requires DXF 2000 or later",
             [](hdxf::Drawing& d) { d.add_mtext({0, 0, 0}, "t").as_mtext().line_spacing_style = 2; }},
        };
        for (const auto& c : cases) {
            hdxf::Drawing d(v);
            c.build(d);
            hdxf_test::assert_eq(c.message, save_error(d), what + ": " + c.message);
            // Nothing was given a handle by the failed save
            for (const auto& e : d.entities()) hdxf_test::assert_true(e.props.handle.empty(), what + ": a handle after a failed save");
            hdxf_test::assert_true(d.save_to_memory(DxfFormat::Binary).empty(), what + ": it fails in binary too");
        }

        // A polyline vertex's id (2010) and an LWPOLYLINE's
        hdxf::Drawing pl(v);
        pl.add_polyline({{0, 0}, {1, 0}}).as_polyline().vertices[0].id = 5;
        hdxf_test::assert_eq("A polyline vertex's identifier requires DXF 2010 or later", save_error(pl), what);
        if (v == DxfVersion::R14) {
            hdxf::Drawing lw(v);
            lw.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}}).as_lwpolyline().vertices[0].id = 3;
            hdxf_test::assert_eq("An LWPOLYLINE's vertex identifiers require DXF 2010 or later", save_error(lw), what);
        }

        // The same properties are written in 2000: the error is the version's, not the data's
        hdxf::Drawing r2000(DxfVersion::R2000);
        r2000.add_line({0, 0, 0}, {1, 0, 0}).props.lineweight = 50;
        std::vector<Rec> recs = records_of(save(r2000, DxfFormat::Ascii, "2000"));
        hdxf_test::assert_eq(50, std::atoi(of_type(recs, "LINE")[0]->value(370).c_str()), "2000 writes 370");
    }
}

TEST(entities_r13_r14_a_failed_save_leaves_the_drawing_and_the_file) {
    for (DxfVersion v : older_versions) {
        const std::filesystem::path path = std::filesystem::temp_directory_path() / ("hdxf_f_" + name_of(v) + ".dxf");
        hdxf::Drawing ok(v);
        ok.add_line({0, 0, 0}, {1, 1, 0});
        hdxf_test::assert_true(ok.save_as(path), ok.last_error());
        std::vector<uint8_t> before = read_file(path);

        hdxf::Drawing d(v);
        d.add_line({0, 0, 0}, {1, 1, 0}).props.lineweight = 25;
        std::string seed_before;
        if (const hdxf::HeaderVar* h = d.find_header("$HANDSEED")) seed_before = h->values[0].second;
        hdxf_test::assert_false(d.save_as(path), "lineweight");
        hdxf_test::assert_eq("Lineweight requires DXF 2000 or later", d.last_error());
        hdxf_test::assert_true(read_file(path) == before, "the file is as it was");
        hdxf_test::assert_true(d.entities()[0].props.handle.empty(), "no handle given");
        std::string seed_after;
        if (const hdxf::HeaderVar* h = d.find_header("$HANDSEED")) seed_after = h->values[0].second;
        hdxf_test::assert_eq(seed_before, seed_after, "$HANDSEED");
        d.entities()[0].props.lineweight = -1;
        hdxf_test::assert_true(d.save_as(path), d.last_error());
        std::filesystem::remove(path);
    }
}

// ---------------------------------------------------------------------------
// 7.11: BLOCK and ENDBLK
// ---------------------------------------------------------------------------

TEST(entities_r13_r14_block_and_endblk) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        d.add_linetype("DASHED", "Dashed", {0.5, -0.25});
        hdxf::Block& plain_block = d.add_block("PLAIN", {1, 2, 3});
        plain_block.entities.push_back(line_entity({0, 0, 0}, {1, 0, 0}));
        hdxf::Block& styled = d.add_block("STYLED");
        styled.block_color = hdxf::Color::from_aci(5);
        styled.endblk_color = hdxf::Color::from_aci(5);
        styled.linetype = "DASHED";
        styled.linetype_scale = 3.0;
        styled.visible = false;
        styled.xdata.push_back({"HDXF_TEST", {{1000, "block"}, {1070, "7"}}});
        hdxf::Block& nested = d.add_block("NESTED");
        hdxf::Entity ins;
        ins.type = hdxf::EntityType::Insert;
        hdxf::InsertData insert;
        insert.block_name = "PLAIN";
        insert.insertion_point = {10, 10, 0};
        ins.data = insert;
        nested.entities.push_back(ins);
        nested.entities.push_back(line_entity({0, 0, 0}, {0, 1, 0}));
        d.add_insert("NESTED", {3, 3, 0});

        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto begins = of_type(recs, "BLOCK");
        const auto ends = of_type(recs, "ENDBLK");
        hdxf_test::assert_eq(5, static_cast<int>(begins.size()), what + ": two space blocks and three of ours");
        hdxf_test::assert_eq(5, static_cast<int>(ends.size()), what);
        std::map<std::string, const Rec*> by_name;
        for (const Rec* r : begins) by_name[r->value(2)] = r;

        // *MODEL_SPACE and *PAPER_SPACE in upper case, the paper space block with 67 1 (libre/r13_v.dxf)
        hdxf_test::assert_true(by_name.count("*MODEL_SPACE") && by_name.count("*PAPER_SPACE"), what + ": space block names");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 2, 70, 10, 20, 30, 3, 1}), codes_text(codes_of(*by_name["*MODEL_SPACE"])), what);
        hdxf_test::assert_eq(codes_text({0, 100, 67, 8, 100, 2, 70, 10, 20, 30, 3, 1}), codes_text(codes_of(*by_name["*PAPER_SPACE"])), what);
        hdxf_test::assert_eq("*PAPER_SPACE", by_name["*PAPER_SPACE"]->value(3), what + ": 3 repeats the name");
        hdxf_test::assert_eq("AcDbEntity AcDbBlockBegin", markers_of(*by_name["PLAIN"]), what);

        // No 4 description, 1 (the xref path) empty on every block, 3 the name, layer 0
        const Rec& pb = *by_name["PLAIN"];
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 2, 70, 10, 20, 30, 3, 1}), codes_text(codes_of(pb)), what + ": BLOCK head");
        hdxf_test::assert_eq("0", pb.value(8), what);
        hdxf_test::assert_eq("PLAIN", pb.value(3), what);
        hdxf_test::assert_eq("", pb.value(1), what + ": the xref path is empty");
        hdxf_test::assert_true(near(1.0, num(pb.value(10))) && near(2.0, num(pb.value(20))) && near(3.0, num(pb.value(30))), what + ": base point");
        for (const Rec* r : begins) hdxf_test::assert_false(has_code(*r, 4), what + ": no 4 in BLOCK");

        // The block's own color, linetype, scale, visibility and XDATA: on BLOCK and on ENDBLK alike
        const Rec& sb = *by_name["STYLED"];
        hdxf_test::assert_eq(codes_text({0, 100, 8, 6, 62, 48, 60, 100, 2, 70, 10, 20, 30, 3, 1}), codes_text(codes_of(sb)), what + ": styled BLOCK");
        const Rec* styled_end = nullptr;
        for (size_t i = 0; i < ends.size(); ++i) {
            if (begins[i] == &sb) styled_end = ends[i];
        }
        hdxf_test::assert_true(styled_end != nullptr, what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 6, 62, 48, 60, 100}), codes_text(codes_of(*styled_end)), what + ": styled ENDBLK");
        hdxf_test::assert_eq("DASHED", styled_end->value(6), what);
        bool xdata_seen = false;
        for (const auto& g : sb.groups) xdata_seen = xdata_seen || (g.first == 1001 && g.second == "HDXF_TEST");
        hdxf_test::assert_true(xdata_seen, what + ": the BLOCK's XDATA");

        // An INSERT in a block, then the block's LINE, before ENDBLK; owners when written
        std::string nested_record = record_handle(recs, "NESTED");
        const auto blocks = in_section(recs, "BLOCKS");
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i]->type() == "BLOCK" && blocks[i]->value(2) == "NESTED") {
                hdxf_test::assert_eq("INSERT", blocks[i + 1]->type(), what);
                hdxf_test::assert_eq("LINE", blocks[i + 2]->type(), what);
                hdxf_test::assert_eq("ENDBLK", blocks[i + 3]->type(), what);
                expect_owner_if_written(*blocks[i + 1], nested_record, what);
                expect_owner_if_written(*blocks[i + 2], nested_record, what);
                expect_owner_if_written(*blocks[i], nested_record, what);
                expect_owner_if_written(*blocks[i + 3], nested_record, what);
            }
        }
        expect_unique_handles(recs, what);
    });
}

// ---------------------------------------------------------------------------
// 7.11: INSERT, ATTRIB, ATTDEF and SEQEND
// ---------------------------------------------------------------------------

namespace {

hdxf::Entity attdef_entity(const std::string& tag, const std::string& prompt, const std::string& value) {
    hdxf::AttdefData a;
    a.tag = tag;
    a.prompt = prompt;
    a.default_value = value;
    a.height = 2.0;
    a.start_point = {1, 1, 0};
    hdxf::Entity e;
    e.type = hdxf::EntityType::Attdef;
    e.data = a;
    return e;
}

hdxf::InsertAttribute attribute(const std::string& tag, const std::string& value, double rotation = 0.0) {
    hdxf::InsertAttribute a;
    a.data.tag = tag;
    a.data.value = value;
    a.data.height = 2.0;
    a.data.start_point = {1, 1, 0};
    a.data.rotation = rotation;
    return a;
}

} // namespace

TEST(entities_r13_r14_insert_with_two_attribute_sets) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        hdxf::Block& tag = d.add_block("TAG");
        tag.entities.push_back(line_entity({0, 0, 0}, {4, 0, 0}));
        tag.entities.push_back(attdef_entity("NAME", "Name?", "none"));
        tag.entities.push_back(attdef_entity("NUMBER", "Number?", "0"));
        d.entities().reserve(8);

        hdxf::Entity& first = d.add_insert("TAG", {10, 0, 0});
        first.as_insert().attributes.push_back(attribute("NAME", "Anna"));
        first.as_insert().attributes.push_back(attribute("NUMBER", "17", 45.0));
        first.as_insert().attributes[1].props.color = hdxf::Color::from_aci(2);
        hdxf::Entity& second = d.add_insert("TAG", {20, 0, 0});
        second.as_insert().attributes.push_back(attribute("NAME", "Bo"));
        second.as_insert().attributes.push_back(attribute("NUMBER", "99"));
        second.as_insert().x_scale = 2.0;
        second.as_insert().rotation = 30.0;
        hdxf::Entity& third = d.add_insert("TAG", {30, 0, 0});     // in paper space, with attributes
        third.props.paper_space = 1;
        third.as_insert().attributes.push_back(attribute("NAME", "Cy"));
        hdxf::Entity& plain_insert = d.add_insert("TAG", {40, 0, 0});    // none: no 66, no SEQEND
        (void)plain_insert;

        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto entities = in_section(recs, "ENTITIES");
        std::string model_record = record_handle(recs, "*MODEL_SPACE");
        std::string paper_record = record_handle(recs, "*PAPER_SPACE");

        // INSERT (66 1), ATTRIB, ATTRIB, SEQEND — twice, then one in paper space, then the plain INSERT
        std::vector<std::string> types;
        for (const Rec* r : entities) types.push_back(r->type());
        std::vector<std::string> paper_first = {"INSERT", "ATTRIB", "SEQEND"};
        std::string joined;
        for (const auto& t : types) joined += t + " ";
        hdxf_test::assert_true(joined.find("INSERT ATTRIB ATTRIB SEQEND INSERT ATTRIB ATTRIB SEQEND") != std::string::npos ||
                               joined.find("INSERT ATTRIB SEQEND INSERT ATTRIB ATTRIB SEQEND INSERT ATTRIB ATTRIB SEQEND") != std::string::npos,
                               what + ": sequence " + joined);

        for (size_t i = 0; i < entities.size(); ++i) {
            const Rec& r = *entities[i];
            if (r.type() != "INSERT") continue;
            std::string handle = handle_of(r);
            const bool attributes = has_code(r, 66);
            const bool paper = has_code(r, 67);
            Codes expected = {0, 100};
            if (paper) expected.push_back(67);
            for (int c : {8, 100}) expected.push_back(c);
            if (attributes) expected.push_back(66);
            for (int c : {2, 10, 20, 30}) expected.push_back(c);
            if (r.value(10) == "20.0" || num(r.value(10)) == 20.0) {
                for (int c : {41, 50}) expected.push_back(c);
            }
            hdxf_test::assert_eq(codes_text(expected), codes_text(codes_of(r)), what + ": INSERT at " + r.value(10));
            expect_owner_if_written(r, paper ? paper_record : model_record, what);
            if (!attributes) {
                hdxf_test::assert_true(i + 1 >= entities.size() || entities[i + 1]->type() != "SEQEND", what + ": no SEQEND for a plain INSERT");
                continue;
            }
            size_t k = i + 1;
            int count = 0;
            for (; k < entities.size() && entities[k]->type() == "ATTRIB"; ++k, ++count) {
                const Rec& a = *entities[k];
                expect_owner_if_written(a, handle, what + ": ATTRIB");
                const std::string tag_name = value_after(a, "AcDbAttribute", 2);
                hdxf_test::assert_true(tag_name == "NAME" || tag_name == "NUMBER", what + ": ATTRIB tag " + tag_name);
                // text part, then AcDbAttribute: 2, 70 (R13:ATTRIB; altera_*: 5 100 8 100 10 40 1 100 2 70)
                Codes exp = {0, 100};
                if (paper) exp.push_back(67);
                exp.push_back(8);
                if (a.value(62) != "") exp.push_back(62);
                for (int c : {100, 10, 20, 30, 40, 1}) exp.push_back(c);
                if (tag_name == "NUMBER" && num(a.value(50)) != 0.0) exp.push_back(50);
                for (int c : {100, 2, 70}) exp.push_back(c);
                // the color is in the head: 62 follows 8
                Codes got = codes_of(a);
                if (a.value(62) != "") {
                    exp.erase(std::find(exp.begin(), exp.end(), 62));
                    exp.insert(std::find(exp.begin(), exp.end(), 8) + 1, 62);
                }
                if (tag_name == "NUMBER" && num(a.value(50)) != 0.0) {
                    // 50 follows 1, before the second marker
                    exp.erase(std::find(exp.begin(), exp.end(), 50));
                    exp.insert(std::find(exp.begin(), exp.end(), 1) + 1, 50);
                }
                hdxf_test::assert_eq(codes_text(exp), codes_text(got), what + ": ATTRIB");
            }
            hdxf_test::assert_eq(r.value(10) == "30.0" || num(r.value(10)) == 30.0 ? 1 : 2, count, what + ": attributes of the INSERT");
            hdxf_test::assert_true(k < entities.size() && entities[k]->type() == "SEQEND", what + ": SEQEND after the attributes");
            const Rec& seq = *entities[k];
            hdxf_test::assert_eq(codes_text(paper ? Codes{0, 100, 67, 8} : Codes{0, 100, 8}), codes_text(codes_of(seq)), what + ": SEQEND");
            expect_owner_if_written(seq, handle, what + ": SEQEND");
        }

        // The ATTDEFs in the block: text part, AcDbAttributeDefinition 3, 2, 70; the block has flag 2
        const auto defs = of_type(recs, "ATTDEF");
        hdxf_test::assert_eq(2, static_cast<int>(defs.size()), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40, 1, 100, 3, 2, 70}), codes_text(codes_of(*defs[0])), what + ": ATTDEF");
        hdxf_test::assert_eq("Name?", defs[0]->value(3), what);
        hdxf_test::assert_eq("NAME", defs[0]->value(2), what);
        for (const Rec* r : of_type(recs, "BLOCK")) {
            if (r->value(2) == "TAG") hdxf_test::assert_eq(2, std::atoi(r->value(70).c_str()) & 2, what + ": BLOCK flag 2 for ATTDEFs");
        }
        expect_unique_handles(recs, what);

        // Back: the attributes of each INSERT, in order
        hdxf::Drawing back = read_back(save(d, f, what), what);
        std::vector<std::vector<std::string>> values;
        for (const auto& e : back.entities()) {
            if (e.type != hdxf::EntityType::Insert) continue;
            std::vector<std::string> v2;
            for (const auto& a : e.as_insert().attributes) v2.push_back(a.data.tag + "=" + a.data.value);
            values.push_back(v2);
        }
        hdxf_test::assert_eq(4, static_cast<int>(values.size()), what);
        std::set<std::string> flat;
        for (const auto& v2 : values) {
            std::string s;
            for (const auto& x : v2) s += x + ";";
            flat.insert(s);
        }
        hdxf_test::assert_true(flat.count("NAME=Anna;NUMBER=17;") && flat.count("NAME=Bo;NUMBER=99;") && flat.count("NAME=Cy;") &&
                                   flat.count(""),
                               what + ": two INSERTs with different attributes read back");
    });
}

TEST(entities_r13_r14_attribute_lock_and_multiline) {
    for (DxfVersion v : older_versions) {
        // A locked attribute is XDATA AcDbAttr in R13 and R14 as in R12 (nothing newer exists)
        hdxf::Drawing d(v);
        hdxf::Block& b = d.add_block("B");
        hdxf::Entity def = attdef_entity("T", "p", "v");
        def.as_attdef().lock_position = true;
        b.entities.push_back(def);
        hdxf::Entity& ins = d.add_insert("B", {0, 0, 0});
        hdxf::InsertAttribute a = attribute("T", "x");
        a.data.lock_position = true;
        ins.as_insert().attributes.push_back(a);
        std::vector<Rec> recs = records_of(save(d, DxfFormat::Ascii, name_of(v)));
        for (const char* type : {"ATTDEF", "ATTRIB"}) {
            const Rec& r = *of_type(recs, type)[0];
            bool seen = false;
            for (const auto& g : r.groups) seen = seen || (g.first == 1001 && g.second == "AcDbAttr");
            hdxf_test::assert_true(seen, name_of(v) + ": " + type + " has XDATA AcDbAttr when locked");
            hdxf_test::assert_false(has_code(r, 280), name_of(v) + ": no 280 in " + type);
        }

        // A multiline attribute has no form here: a specific error
        hdxf::Drawing m(v);
        hdxf::Block& mb = m.add_block("B");
        hdxf::Entity mdef = attdef_entity("T", "p", "v");
        mdef.as_attdef().multiline.emplace();
        mb.entities.push_back(mdef);
        hdxf_test::assert_eq("A multiline attribute requires DXF 2000 or later", save_error(m), name_of(v));
    }
}

// ---------------------------------------------------------------------------
// 7.11 and 7.12: the R12 types, group by group
// ---------------------------------------------------------------------------

namespace {

// An entity of each R12 type with every optional group set
struct TypeCase {
    std::string type;
    std::function<void(hdxf::Drawing&)> build;
    Codes older_codes;      // the codes of the record without the head (the first 0 and the head 100, 8 are in `head`)
};

const Codes entity_head = {0, 100, 8};

} // namespace

TEST(entities_r13_r14_the_r12_types_without_raw) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        auto set_normal = [](hdxf::Entity& e) { e.props.extrusion = {0, 1, 0}; e.props.thickness = 1.5; };
        hdxf::Entity& line = d.add_line({0, 0, 1}, {3, 4, 5});
        set_normal(line);
        hdxf::Entity& circle = d.add_circle({1, 2, 3}, 4.5);
        set_normal(circle);
        hdxf::Entity& arc = d.add_arc({1, 2, 3}, 4.5, 10, 80);
        set_normal(arc);
        hdxf::Entity& point = d.add_point({7, 8, 9});
        set_normal(point);
        point.as_point().x_axis_angle = 15.0;
        hdxf::Entity& text = d.add_text({1, 2, 0}, "Hello", 3.0);
        set_normal(text);
        text.as_text().rotation = 20.0;
        text.as_text().width_factor = 0.8;
        text.as_text().oblique_angle = 10.0;
        text.as_text().generation_flags = 2;
        text.as_text().h_justify = 1;
        text.as_text().v_justify = 2;
        text.as_text().alignment_point = {5, 5, 0};
        hdxf::Entity& trace = d.add_trace({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0});
        set_normal(trace);
        hdxf::Entity& solid = d.add_solid({0, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0});
        set_normal(solid);
        hdxf::Entity& face = d.add_face3d({0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 1});
        face.as_face3d().invisible_edges = 5;
        hdxf::Entity& shape = d.add_shape({3, 3, 0}, "SQUARE", 2.0);
        set_normal(shape);
        shape.as_shape().rotation = 12.0;
        shape.as_shape().x_scale = 1.5;
        shape.as_shape().oblique_angle = 5.0;

        std::vector<Rec> recs = records_of(save(d, f, what));
        auto only = [&](const std::string& type) -> const Rec& {
            auto list = of_type(recs, type);
            hdxf_test::assert_eq(1, static_cast<int>(list.size()), what + ": one " + type);
            return *list[0];
        };
        auto expect = [&](const std::string& type, const Codes& codes) {
            hdxf_test::assert_eq(codes_text(codes), codes_text(codes_of(only(type))), what + ": " + type);
        };
        // R13:LINE 39 10 11 210; CIRCLE 39 10 40 210; ARC AcDbCircle 39 10 40 [210] / AcDbArc 50 51;
        // POINT 10 39 210 50; TRACE/SOLID 10 11 12 13 39 210; 3DFACE 10..13 70; SHAPE 39 10 40 2 50 41 51 210
        expect("LINE", {0, 100, 8, 100, 39, 10, 20, 30, 11, 21, 31, 210, 220, 230});
        expect("CIRCLE", {0, 100, 8, 100, 39, 10, 20, 30, 40, 210, 220, 230});
        // (210 stays in AcDbCircle, before AcDbArc, as in 2000: R13:ARC lists it last but no AutoCAD file has one)
        expect("ARC", {0, 100, 8, 100, 39, 10, 20, 30, 40, 210, 220, 230, 100, 50, 51});
        expect("POINT", {0, 100, 8, 100, 10, 20, 30, 39, 210, 220, 230, 50});
        expect("TRACE", {0, 100, 8, 100, 10, 20, 30, 11, 21, 31, 12, 22, 32, 13, 23, 33, 39, 210, 220, 230});
        expect("SOLID", {0, 100, 8, 100, 10, 20, 30, 11, 21, 31, 12, 22, 32, 13, 23, 33, 39, 210, 220, 230});
        expect("3DFACE", {0, 100, 8, 100, 10, 20, 30, 11, 21, 31, 12, 22, 32, 13, 23, 33, 70});
        expect("SHAPE", {0, 100, 8, 100, 39, 10, 20, 30, 40, 2, 50, 41, 51, 210, 220, 230});
        // TEXT: the first AcDbText, 100 AcDbText again, then 73 (libre/r13_v.dxf, gvsig/dxfprueba.dxf)
        expect("TEXT", {0, 100, 8, 100, 39, 10, 20, 30, 40, 1, 50, 41, 51, 71, 72, 11, 21, 31, 210, 220, 230, 100, 73});
        hdxf_test::assert_eq("AcDbEntity AcDbText AcDbText", markers_of(only("TEXT")), what);
        hdxf_test::assert_eq("Hello", only("TEXT").value(1), what);

        // Back: the typed values of every type
        hdxf::Drawing back = read_back(save(d, f, what), what);
        std::map<hdxf::EntityType, const hdxf::Entity*> by_type;
        for (const auto& e : back.entities()) by_type[e.type] = &e;
        hdxf_test::assert_eq(9, static_cast<int>(by_type.size()), what + ": nine types read back");
        const hdxf::Entity& bl = *by_type[hdxf::EntityType::Line];
        hdxf_test::assert_true(near(5.0, bl.as_line().end.z) && near(1.5, bl.props.thickness) && near(1.0, bl.props.extrusion.y), what + ": LINE");
        const hdxf::Entity& ba = *by_type[hdxf::EntityType::Arc];
        hdxf_test::assert_true(near(80.0, ba.as_arc().end_angle) && near(1.0, ba.props.extrusion.y) && near(1.5, ba.props.thickness),
                               what + ": ARC with its normal");
        const hdxf::Entity& bt = *by_type[hdxf::EntityType::Text];
        hdxf_test::assert_true(bt.as_text().content == "Hello" && near(20.0, bt.as_text().rotation) && near(0.8, bt.as_text().width_factor) &&
                                   bt.as_text().v_justify == 2 && bt.as_text().h_justify == 1 && bt.as_text().generation_flags == 2,
                               what + ": TEXT");
        const hdxf::Entity& bp = *by_type[hdxf::EntityType::Point];
        hdxf_test::assert_true(near(15.0, bp.as_point().x_axis_angle), what + ": POINT");
        const hdxf::Entity& bs = *by_type[hdxf::EntityType::Shape];
        hdxf_test::assert_true(bs.as_shape().name == "SQUARE" && near(1.5, bs.as_shape().x_scale), what + ": SHAPE");
        hdxf_test::assert_eq(5, by_type[hdxf::EntityType::Face3D]->as_face3d().invisible_edges, what + ": 3DFACE");
    });
}

TEST(entities_r13_r14_defaults_are_left_out) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        d.add_line({0, 0, 0}, {1, 0, 0});
        d.add_circle({0, 0, 0}, 1);
        d.add_arc({0, 0, 0}, 1, 0, 90);
        d.add_text({0, 0, 0}, "t", 1);
        d.add_shape({0, 0, 0}, "S", 1);
        std::vector<Rec> recs = records_of(save(d, f, what));
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 11, 21, 31}), codes_text(codes_of(*of_type(recs, "LINE")[0])), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40}), codes_text(codes_of(*of_type(recs, "CIRCLE")[0])), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40, 100, 50, 51}), codes_text(codes_of(*of_type(recs, "ARC")[0])), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40, 1, 100}), codes_text(codes_of(*of_type(recs, "TEXT")[0])), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40, 2}), codes_text(codes_of(*of_type(recs, "SHAPE")[0])), what);
    });
}

// ---------------------------------------------------------------------------
// 7.12: the R13 family
// ---------------------------------------------------------------------------

TEST(entities_r13_r14_ellipse_xline_ray_tolerance) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        hdxf::Entity& ellipse = d.add_ellipse({1, 2, 0}, {5, 0, 0}, 0.4, 0.5, 4.0);
        ellipse.props.extrusion = {0, 0, -1};
        d.add_xline({1, 1, 0}, {1, 1, 0});
        d.add_ray({2, 2, 0}, {0, 1, 0});
        hdxf::Entity& tol = d.add_tolerance({4, 4, 0}, "{\\Fgdt;j}%%v0.5%%v");
        tol.as_tolerance().dim_style = "Standard";
        tol.as_tolerance().x_axis_direction = {0, 1, 0};
        tol.props.extrusion = {0, 0, 1};

        std::vector<Rec> recs = records_of(save(d, f, what));
        // R13:ELLIPSE 10 11 210 40 41 42 (mii MII200HB: 210 is written for every ellipse)
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 11, 21, 31, 210, 220, 230, 40, 41, 42}),
                             codes_text(codes_of(*of_type(recs, "ELLIPSE")[0])), what + ": ELLIPSE");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 11, 21, 31}), codes_text(codes_of(*of_type(recs, "XLINE")[0])), what);
        hdxf_test::assert_eq("AcDbEntity AcDbXline", markers_of(*of_type(recs, "XLINE")[0]), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 11, 21, 31}), codes_text(codes_of(*of_type(recs, "RAY")[0])), what);
        hdxf_test::assert_eq("AcDbEntity AcDbRay", markers_of(*of_type(recs, "RAY")[0]), what);
        // R13:TOLERANCE 3 10 1 210 11 (210 only when not the default)
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 3, 10, 20, 30, 1, 11, 21, 31}), codes_text(codes_of(*of_type(recs, "TOLERANCE")[0])), what);
        hdxf_test::assert_eq("AcDbEntity AcDbFcf", markers_of(*of_type(recs, "TOLERANCE")[0]), what);

        hdxf::Drawing back = read_back(save(d, f, what), what);
        std::map<hdxf::EntityType, const hdxf::Entity*> by_type;
        for (const auto& e : back.entities()) by_type[e.type] = &e;
        const hdxf::Entity& be = *by_type[hdxf::EntityType::Ellipse];
        hdxf_test::assert_true(near(0.4, be.as_ellipse().ratio) && near(0.5, be.as_ellipse().start_param) && near(4.0, be.as_ellipse().end_param) &&
                                   near(-1.0, be.props.extrusion.z) && near(5.0, be.as_ellipse().major_axis.x),
                               what + ": ELLIPSE back");
        hdxf_test::assert_true(near(1.0, by_type[hdxf::EntityType::Ray]->as_ray().direction.y) && near(2.0, by_type[hdxf::EntityType::Ray]->as_ray().base_point.x),
                               what + ": RAY back");
        hdxf_test::assert_true(by_type[hdxf::EntityType::Tolerance]->as_tolerance().content == "{\\Fgdt;j}%%v0.5%%v" &&
                                   near(1.0, by_type[hdxf::EntityType::Tolerance]->as_tolerance().x_axis_direction.y),
                               what + ": TOLERANCE back");
    });
}

TEST(entities_r13_r14_spline_weights_and_knots) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        hdxf::Entity& spline = d.add_spline({{0, 0, 0}, {1, 2, 0}, {3, 3, 0}, {5, 1, 0}, {6, 0, 0}}, 3);
        hdxf::SplineData& s = spline.as_spline();
        s.flags |= 4;                                   // rational
        s.weights = {1.0, 0.5, 2.0, 1.5, 1.0};
        s.knots = {0, 0, 0, 0, 0.4, 1, 1, 1, 1};
        hdxf::Entity& fit = d.add_spline({{0, 0, 0}, {2, 2, 0}, {4, 0, 0}}, 2);
        fit.as_spline().fit_points = {{0, 0, 0}, {2, 1, 0}, {4, 0, 0}};
        fit.as_spline().start_tangent = {1, 1, 0};
        fit.as_spline().end_tangent = {1, -1, 0};

        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto splines = of_type(recs, "SPLINE");
        hdxf_test::assert_eq(2, static_cast<int>(splines.size()), what);
        // 210 (planar), 70 71 72 73 74 42 43, knots 40, then each control point with its weight 41 after it
        Codes expected = {0, 100, 8, 100, 210, 220, 230, 70, 71, 72, 73, 74, 42, 43};
        for (int i = 0; i < 9; ++i) expected.push_back(40);
        for (int i = 0; i < 5; ++i) for (int c : {10, 20, 30, 41}) expected.push_back(c);
        hdxf_test::assert_eq(codes_text(expected), codes_text(codes_of(*splines[0])), what + ": rational SPLINE");
        hdxf_test::assert_eq(5, std::atoi(values_of(*splines[0], 73)[0].c_str()), what);
        hdxf_test::assert_eq(9, std::atoi(values_of(*splines[0], 72)[0].c_str()), what);
        // fit points: 44, the tangents 12 and 13, knots, control points, 11 per fit point
        Codes exp2 = {0, 100, 8, 100, 210, 220, 230, 70, 71, 72, 73, 74, 42, 43, 44, 12, 22, 32, 13, 23, 33};
        for (size_t i = 0; i < 6; ++i) exp2.push_back(40);
        for (int i = 0; i < 3; ++i) for (int c : {10, 20, 30}) exp2.push_back(c);
        for (int i = 0; i < 3; ++i) for (int c : {11, 21, 31}) exp2.push_back(c);
        hdxf_test::assert_eq(codes_text(exp2), codes_text(codes_of(*splines[1])), what + ": fit point SPLINE");

        hdxf::Drawing back = read_back(save(d, f, what), what);
        const hdxf::SplineData* rational = nullptr;
        const hdxf::SplineData* fitted = nullptr;
        for (const auto& e : back.entities()) {
            if (e.type != hdxf::EntityType::Spline) continue;
            if (e.as_spline().is_rational()) rational = &e.as_spline();
            else fitted = &e.as_spline();
        }
        hdxf_test::assert_true(rational && fitted, what);
        hdxf_test::assert_eq(5, static_cast<int>(rational->weights.size()), what);
        hdxf_test::assert_true(near(0.5, rational->weights[1]) && near(2.0, rational->weights[2]) && near(0.4, rational->knots[4]), what + ": weights and knots");
        hdxf_test::assert_eq(3, static_cast<int>(fitted->fit_points.size()), what);
        hdxf_test::assert_true(near(-1.0, fitted->end_tangent.y), what + ": tangents");
    });
}

TEST(entities_r13_r14_mtext) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        d.add_text_style("Romans", "romans");
        hdxf::Entity& m = d.add_mtext({1, 2, 0}, "Hello\\Pworld", 3.5);
        m.as_mtext().reference_width = 40.0;
        m.as_mtext().attachment = 5;
        m.as_mtext().drawing_direction = 3;
        m.as_mtext().style = "Romans";
        m.as_mtext().x_axis_direction = {0, 1, 0};
        m.props.extrusion = {0, 0, 1};
        std::string long_text;
        for (int i = 0; i < 600; ++i) long_text += static_cast<char>('a' + i % 26);
        d.add_mtext({0, 0, 0}, long_text, 2.5);
        d.add_mtext({0, 5, 0}, "plain", 2.5);

        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto texts = of_type(recs, "MTEXT");
        hdxf_test::assert_eq(3, static_cast<int>(texts.size()), what);
        // R13 and R14 (AutoCAD's own R14: bne, z59, schneider): 10 40 41 71 72 1, 7 and 11 when not the default;
        // no 73, no 44 (2000's line spacing)
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40, 41, 71, 72, 1, 7, 11, 21, 31}), codes_text(codes_of(*texts[0])), what + ": MTEXT");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40, 41, 71, 72, 1}), codes_text(codes_of(*texts[2])), what + ": a plain MTEXT");
        for (const Rec* r : texts) hdxf_test::assert_false(has_code(*r, 73) || has_code(*r, 44) || has_code(*r, 46) || has_code(*r, 90), what);
        hdxf_test::assert_eq(3, std::atoi(texts[0]->value(72).c_str()), what + ": 72 is 3 for top to bottom in both versions");
        // 3 in parts of 255 (R13) or 250 (R14), 1 the rest
        const size_t part = v == DxfVersion::R13 ? 255 : 250;
        std::vector<std::string> threes = values_of(*texts[1], 3);
        std::string joined;
        for (const auto& t : threes) {
            hdxf_test::assert_eq(static_cast<int>(part), static_cast<int>(t.size()), what + ": a 3 part");
            joined += t;
        }
        joined += texts[1]->value(1);
        hdxf_test::assert_eq(long_text, joined, what + ": the parts are the text");
        hdxf_test::assert_eq(2, static_cast<int>(threes.size()), what + ": two 3 parts for 600 characters");
        hdxf_test::assert_eq(static_cast<int>(600 - 2 * part), static_cast<int>(texts[1]->value(1).size()), what);

        hdxf::Drawing back = read_back(save(d, f, what), what);
        std::vector<const hdxf::Entity*> mt;
        for (const auto& e : back.entities()) if (e.type == hdxf::EntityType::MText) mt.push_back(&e);
        hdxf_test::assert_eq(3, static_cast<int>(mt.size()), what);
        hdxf_test::assert_true(mt[0]->as_mtext().content == "Hello\\Pworld" && mt[0]->as_mtext().style == "Romans" && mt[0]->as_mtext().attachment == 5 &&
                                   mt[0]->as_mtext().drawing_direction == 3 && near(40.0, mt[0]->as_mtext().reference_width),
                               what + ": MTEXT back");
        hdxf_test::assert_eq(long_text, mt[1]->as_mtext().content, what + ": long MTEXT back");
    });

    // R13's drawing directions are 1, 2, 3, 4; R14's 1, 3, 5
    hdxf::Drawing r13(DxfVersion::R13);
    r13.add_mtext({0, 0, 0}, "t").as_mtext().drawing_direction = 5;
    hdxf_test::assert_eq("An MTEXT's drawing direction by style requires DXF R14 or later", save_error(r13), "R13 by style");
    r13.entities()[0].as_mtext().drawing_direction = 2;
    hdxf_test::assert_false(r13.save_to_memory().empty(), "R13 right to left");
    hdxf::Drawing r14(DxfVersion::R14);
    r14.add_mtext({0, 0, 0}, "t").as_mtext().drawing_direction = 5;
    hdxf_test::assert_false(r14.save_to_memory().empty(), "R14 by style");
    r14.entities()[0].as_mtext().drawing_direction = 4;
    hdxf_test::assert_eq("An MTEXT's right to left or bottom to top drawing direction is DXF R13's only", save_error(r14), "R14 bottom to top");
}

TEST(entities_r13_r14_leader_annotation_and_reactor) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        hdxf::Entity& text = d.add_mtext({5, 5, 0}, "note", 2.5);
        hdxf::Entity& leader = d.add_leader({{0, 0, 0}, {2, 2, 0}, {4, 2, 0}}, &text);
        leader.as_leader().annotation_offset = {0, 0.5, 0};
        leader.as_leader().hookline = true;
        leader.as_leader().byblock_color = 3;
        hdxf::Entity& tol = d.add_tolerance({9, 9, 0}, "{\\Fgdt;j}%%v1");
        d.add_leader({{7, 7, 0}, {8, 8, 0}}, &tol);
        d.add_leader({{7, 1, 0}, {8, 2, 0}});     // without an annotation

        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto leaders = of_type(recs, "LEADER");
        hdxf_test::assert_eq(3, static_cast<int>(leaders.size()), what);
        // 3, [71 72 at their defaults: left out], 73 74 75 40 41 76 and the vertices, 77, 340, 210, 211, 212 (R13: the annotation offset;
        // R14: 213 for it)
        Codes first = {0, 100, 8, 100, 3, 73, 74, 75, 40, 41, 76, 10, 20, 30, 10, 20, 30, 10, 20, 30, 77, 340};
        first.push_back(v == DxfVersion::R13 ? 212 : 213);
        for (int c : {222, 232}) { if (v == DxfVersion::R13) first.push_back(c); else first.push_back(c + 1); }
        hdxf_test::assert_eq(codes_text(first), codes_text(codes_of(*leaders[0])), what + ": LEADER with an annotation");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 3, 73, 74, 75, 40, 41, 76, 10, 20, 30, 10, 20, 30}), codes_text(codes_of(*leaders[2])), what + ": no annotation");
        hdxf_test::assert_eq(0, std::atoi(leaders[0]->value(73).c_str()), what + ": annotation type MTEXT");
        hdxf_test::assert_eq(1, std::atoi(leaders[1]->value(73).c_str()), what + ": annotation type TOLERANCE");

        // 340 names the MTEXT, which has the LEADER as its reactor
        const Rec* mtext = of_type(recs, "MTEXT")[0];
        hdxf_test::assert_eq(handle_of(*mtext), leaders[0]->value(340), what + ": 340 is the MTEXT's handle");
        bool reactor = false;
        bool in_reactors = false;
        for (const auto& g : mtext->groups) {
            if (g.first == 102) in_reactors = g.second == "{ACAD_REACTORS";
            if (in_reactors && g.first == 330 && g.second == handle_of(*leaders[0])) reactor = true;
        }
        hdxf_test::assert_true(reactor, what + ": the annotation has the leader as a reactor");
        expect_unique_handles(recs, what);

        hdxf::Drawing back = read_back(save(d, f, what), what);
        const hdxf::Entity* l0 = nullptr;
        for (const auto& e : back.entities()) if (e.type == hdxf::EntityType::Leader && !l0) l0 = &e;
        hdxf_test::assert_true(l0 != nullptr, what);
        hdxf_test::assert_true(l0->as_leader().annotation_handle == handle_of(*mtext) && l0->as_leader().vertices.size() == 3 &&
                                   l0->as_leader().hookline && l0->as_leader().byblock_color == 3,
                               what + ": LEADER back");
        // R13's 212 is the annotation offset; R14's 213 is
        hdxf_test::assert_true(near(0.5, l0->as_leader().annotation_offset.y) && near(0.0, l0->as_leader().block_offset.y), what + ": the annotation offset back");
    });

    // The block's offset is R14's 212; R13 has none
    hdxf::Drawing r13(DxfVersion::R13);
    r13.add_leader({{0, 0, 0}, {1, 1, 0}}).as_leader().block_offset = {1, 0, 0};
    hdxf_test::assert_eq("A leader's block offset requires DXF R14 or later", save_error(r13), "R13");
    hdxf::Drawing r14(DxfVersion::R14);
    r14.add_leader({{0, 0, 0}, {1, 1, 0}}).as_leader().block_offset = {1, 0, 0};
    std::vector<Rec> recs = records_of(r14.save_to_memory());
    hdxf_test::assert_eq(1.0, num(of_type(recs, "LEADER")[0]->value(212)), "R14 212");
}

TEST(entities_r13_r14_mline_and_its_style) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        d.add_mline({{0, 0, 0}, {5, 0, 0}, {5, 5, 0}}, false, 2.0, 1);
        d.add_mline({{0, 10, 0}, {5, 10, 0}, {5, 15, 0}, {0, 15, 0}}, true);
        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto lines = of_type(recs, "MLINE");
        hdxf_test::assert_eq(2, static_cast<int>(lines.size()), what);
        std::string style_handle;
        for (const auto* o : in_section(recs, "OBJECTS")) {
            if (o->type() == "MLINESTYLE" && o->value(2) == "STANDARD") style_handle = handle_of(*o);
        }
        hdxf_test::assert_false(style_handle.empty(), what + ": MLINESTYLE STANDARD in OBJECTS");
        const Rec& first = *lines[0];
        hdxf_test::assert_eq("STANDARD", first.value(2), what);
        hdxf_test::assert_eq(style_handle, first.value(340), what + ": 340 is the style");
        hdxf_test::assert_eq(1, std::atoi(first.value(71).c_str()), what + ": 71 1 for an open mline (R13: 1 open, 3 closed; R14: bit 1 has vertices)");
        hdxf_test::assert_eq(3, std::atoi(lines[1]->value(71).c_str()), what + ": 71 3 for a closed one");
        hdxf_test::assert_eq(3, std::atoi(first.value(72).c_str()), what + ": vertices");
        hdxf_test::assert_eq(2, std::atoi(first.value(73).c_str()), what + ": elements of the style");
        hdxf_test::assert_true(near(2.0, num(first.value(40))) && std::atoi(first.value(70).c_str()) == 1, what + ": scale and justification");
        // 2 340 40 70 71 72 73 10, 210, then per vertex 11 12 13 and per element 74 41.. 75
        Codes codes = codes_of(first);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 2, 340, 40, 70, 71, 72, 73, 10, 20, 30, 210, 220, 230}),
                             codes_text(Codes(codes.begin(), codes.begin() + 17)), what + ": the head of MLINE");
        hdxf_test::assert_eq(3 * 9, static_cast<int>(std::count(codes.begin(), codes.end(), 11) + std::count(codes.begin(), codes.end(), 21) +
                                                       std::count(codes.begin(), codes.end(), 31) + std::count(codes.begin(), codes.end(), 12) +
                                                       std::count(codes.begin(), codes.end(), 22) + std::count(codes.begin(), codes.end(), 32) +
                                                       std::count(codes.begin(), codes.end(), 13) + std::count(codes.begin(), codes.end(), 23) +
                                                       std::count(codes.begin(), codes.end(), 33)), what + ": 11 12 13 of each vertex");

        hdxf::Drawing back = read_back(save(d, f, what), what);
        const hdxf::Entity* ml = nullptr;
        for (const auto& e : back.entities()) if (e.type == hdxf::EntityType::Mline && !ml) ml = &e;
        hdxf_test::assert_true(ml && ml->as_mline().style == "STANDARD" && ml->as_mline().vertices.size() == 3 && near(2.0, ml->as_mline().scale),
                               what + ": MLINE back");
        hdxf_test::assert_eq(style_handle, ml->as_mline().style_handle, what + ": the style by its handle");

        // A style that is not there is an error, not a dangling pointer
        hdxf::Drawing bad(v);
        bad.add_mline({{0, 0, 0}, {1, 0, 0}}, false, 1.0, 0, "NOSUCH");
        hdxf_test::assert_eq("MLINE style \"NOSUCH\" is not in ACAD_MLINESTYLE", save_error(bad, f), what);
    });

    // R13's 71 is open/closed only; the cap bits are R14's
    hdxf::Drawing r13(DxfVersion::R13);
    r13.add_mline({{0, 0, 0}, {1, 0, 0}}).as_mline().flags = 1 | 4;
    hdxf_test::assert_eq("An MLINE's suppressed caps require DXF R14 or later", save_error(r13), "R13 caps");
    hdxf::Drawing r14(DxfVersion::R14);
    r14.add_mline({{0, 0, 0}, {1, 0, 0}}).as_mline().flags = 1 | 4 | 8;
    std::vector<Rec> recs = records_of(r14.save_to_memory());
    hdxf_test::assert_eq(13, std::atoi(of_type(recs, "MLINE")[0]->value(71).c_str()), "R14 caps");
}

// ---------------------------------------------------------------------------
// 7.13: POLYLINE and LWPOLYLINE
// ---------------------------------------------------------------------------

namespace {

hdxf::Entity polyline_entity(int flags, const std::vector<Point3>& points) {
    hdxf::Drawing tmp;
    hdxf::Entity e = tmp.add_polyline({{0, 0}, {1, 0}});
    e.as_polyline().flags = flags;
    e.as_polyline().vertices.clear();
    for (const auto& p : points) {
        hdxf::Vertex v;
        v.position = p;
        e.as_polyline().vertices.push_back(v);
    }
    return e;
}

} // namespace

TEST(entities_r13_r14_polyline_open_closed_bulge_widths) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        d.add_polyline({{0, 0}, {5, 0}, {5, 5}});                         // open, plain
        hdxf::Entity& closed = d.add_polyline({{0, 10}, {5, 10}, {5, 15}, {0, 15}}, true);
        closed.as_polyline().vertices[1].bulge = 0.5;
        closed.as_polyline().vertices[2].start_width = 0.2;
        closed.as_polyline().vertices[2].end_width = 0.4;
        closed.as_polyline().default_start_width = 0.1;
        closed.as_polyline().default_end_width = 0.3;
        closed.as_polyline().elevation = 7.5;
        closed.props.extrusion = {0, 0, -1};
        closed.props.thickness = 2.0;
        closed.as_polyline().vertices[0].xdata.push_back({"HDXF_TEST", {{1000, "v0"}, {1070, "3"}}});
        closed.as_polyline().vertices[3].layer = "OTHER";
        d.add_layer("OTHER");

        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto polys = of_type(recs, "POLYLINE");
        const auto verts = of_type(recs, "VERTEX");
        const auto ends = of_type(recs, "SEQEND");
        hdxf_test::assert_eq(2, static_cast<int>(polys.size()), what);
        hdxf_test::assert_eq(7, static_cast<int>(verts.size()), what);
        hdxf_test::assert_eq(2, static_cast<int>(ends.size()), what);
        // AutoCAD's R13 and R14 files (small_r13, az, mii, 7H12R10F): 66 10 [39] [70] — 70 is left out at 0, the vertex's too
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 66, 10, 20, 30}), codes_text(codes_of(*polys[0])), what + ": an open 2D POLYLINE has no 70");
        hdxf_test::assert_eq("AcDbEntity AcDb2dPolyline", markers_of(*polys[0]), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 66, 10, 20, 30, 39, 70, 40, 41, 210, 220, 230}), codes_text(codes_of(*polys[1])),
                             what + ": a closed POLYLINE with everything");
        hdxf_test::assert_eq(1, std::atoi(polys[1]->value(66).c_str()), what);
        hdxf_test::assert_eq(1, std::atoi(polys[1]->value(70).c_str()), what);
        hdxf_test::assert_true(near(7.5, num(polys[1]->value(30))), what + ": the elevation in 30");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 100, 10, 20, 30}), codes_text(codes_of(*verts[0])), what + ": a VERTEX has no 70 at 0");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 100, 10, 20, 30, 42}), codes_text(codes_of(*verts[4])), what + ": bulge");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 100, 10, 20, 30, 40, 41}), codes_text(codes_of(*verts[5])), what + ": widths");
        hdxf_test::assert_eq("OTHER", verts[6]->value(8), what + ": the vertex's own layer");
        hdxf_test::assert_eq(codes_text({0, 100, 8}), codes_text(codes_of(*ends[0])), what + ": SEQEND");
        // Vertex XDATA: after the vertex's own groups
        bool xdata = false;
        for (const auto& g : verts[3]->groups) xdata = xdata || (g.first == 1001 && g.second == "HDXF_TEST");
        hdxf_test::assert_true(xdata, what + ": vertex XDATA");
        for (const Rec* r : verts) hdxf_test::assert_eq("AcDbEntity AcDbVertex AcDb2dVertex", markers_of(*r), what);
        expect_unique_handles(recs, what);

        // Back: the values
        hdxf::Drawing back = read_back(save(d, f, what), what);
        std::vector<const hdxf::PolylineData*> bp;
        std::vector<const hdxf::Entity*> be;
        for (const auto& e : back.entities()) if (e.type == hdxf::EntityType::Polyline) { bp.push_back(&e.as_polyline()); be.push_back(&e); }
        hdxf_test::assert_eq(2, static_cast<int>(bp.size()), what);
        hdxf_test::assert_true(bp[1]->is_closed() && bp[1]->vertices.size() == 4 && near(0.5, bp[1]->vertices[1].bulge) &&
                                   near(0.2, bp[1]->vertices[2].start_width) && near(0.4, bp[1]->vertices[2].end_width) &&
                                   near(0.1, bp[1]->default_start_width) && near(0.3, bp[1]->default_end_width) && near(7.5, bp[1]->elevation) &&
                                   near(-1.0, be[1]->props.extrusion.z) && near(2.0, be[1]->props.thickness),
                               what + ": POLYLINE back");
        hdxf_test::assert_eq(1, static_cast<int>(bp[1]->vertices[0].xdata.size()), what + ": vertex XDATA back");
        hdxf_test::assert_eq("OTHER", bp[1]->vertices[3].layer, what);
        hdxf_test::assert_false(bp[0]->is_closed(), what + ": the open one is open");
    });
}

TEST(entities_r13_r14_polyline_3d_mesh_and_polyface) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        // 3D polyline: AcDb3dPolyline, 70 8, vertices AcDb3dPolylineVertex 70 32
        hdxf::Entity p3 = polyline_entity(8, {{0, 0, 0}, {1, 1, 1}, {2, 0, 3}});
        for (auto& vtx : p3.as_polyline().vertices) vtx.flags = 32;
        d.add_entity(p3);
        // Polygon mesh 2 x 3: AcDbPolygonMesh, 71 72, vertices 70 64
        hdxf::Entity mesh = polyline_entity(16, {{0, 0, 0}, {1, 0, 0}, {2, 0, 1}, {0, 1, 0}, {1, 1, 1}, {2, 1, 0}});
        mesh.as_polyline().m_count = 2;
        mesh.as_polyline().n_count = 3;
        mesh.as_polyline().smooth_m = 6;
        mesh.as_polyline().smooth_n = 6;
        mesh.as_polyline().smooth_type = 6;
        for (auto& vtx : mesh.as_polyline().vertices) vtx.flags = 64;
        d.add_entity(mesh);
        // Polyface mesh: four vertices (flags 192), two face records (flags 128, indices)
        hdxf::Entity pf = polyline_entity(64, {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 1}});
        for (auto& vtx : pf.as_polyline().vertices) vtx.flags = 192;
        pf.as_polyline().m_count = 4;
        pf.as_polyline().n_count = 2;
        for (int i = 0; i < 2; ++i) {
            hdxf::Vertex face;
            face.flags = 128;
            face.face[0] = 1;
            face.face[1] = 2 + i;
            face.face[2] = 3 + i;
            face.face[3] = i == 0 ? 0 : -4;
            pf.as_polyline().vertices.push_back(face);
        }
        d.add_entity(pf);

        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto polys = of_type(recs, "POLYLINE");
        hdxf_test::assert_eq(3, static_cast<int>(polys.size()), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 66, 10, 20, 30, 70}), codes_text(codes_of(*polys[0])), what + ": 3D POLYLINE");
        hdxf_test::assert_eq(8, std::atoi(polys[0]->value(70).c_str()), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 66, 10, 20, 30, 70, 71, 72, 73, 74, 75}), codes_text(codes_of(*polys[1])), what + ": polygon mesh");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 66, 10, 20, 30, 70, 71, 72}), codes_text(codes_of(*polys[2])), what + ": polyface mesh");
        hdxf_test::assert_eq("AcDbEntity AcDb3dPolyline", markers_of(*polys[0]), what);
        hdxf_test::assert_eq("AcDbEntity AcDbPolygonMesh", markers_of(*polys[1]), what);
        hdxf_test::assert_eq("AcDbEntity AcDbPolyFaceMesh", markers_of(*polys[2]), what);

        const auto verts = of_type(recs, "VERTEX");
        hdxf_test::assert_eq(3 + 6 + 6, static_cast<int>(verts.size()), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 100, 10, 20, 30, 70}), codes_text(codes_of(*verts[0])), what + ": 3D vertex");
        hdxf_test::assert_eq("AcDbEntity AcDbVertex AcDb3dPolylineVertex", markers_of(*verts[0]), what);
        hdxf_test::assert_eq(32, std::atoi(verts[0]->value(70).c_str()), what);
        hdxf_test::assert_eq("AcDbEntity AcDbVertex AcDbPolygonMeshVertex", markers_of(*verts[3]), what);
        hdxf_test::assert_eq(64, std::atoi(verts[3]->value(70).c_str()), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 100, 10, 20, 30, 70}), codes_text(codes_of(*verts[9])), what + ": polyface vertex");
        hdxf_test::assert_eq("AcDbEntity AcDbVertex AcDbPolyFaceMeshVertex", markers_of(*verts[9]), what);
        hdxf_test::assert_eq(192, std::atoi(verts[9]->value(70).c_str()), what);
        // A face record has its own subclass, 10 0 0 0, 70 128 and the indices that are not 0
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 70, 71, 72, 73}), codes_text(codes_of(*verts[13])), what + ": face record");
        hdxf_test::assert_eq("AcDbEntity AcDbFaceRecord", markers_of(*verts[13]), what);
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 70, 71, 72, 73, 74}), codes_text(codes_of(*verts[14])), what + ": face record with a hidden edge");
        hdxf_test::assert_eq(-4, std::atoi(verts[14]->value(74).c_str()), what);
        expect_unique_handles(recs, what);

        hdxf::Drawing back = read_back(save(d, f, what), what);
        hdxf_test::assert_eq(3, static_cast<int>(back.entities().size()), what);
        const auto& b3 = back.entities()[0].as_polyline();
        const auto& bm = back.entities()[1].as_polyline();
        const auto& bf = back.entities()[2].as_polyline();
        hdxf_test::assert_true(b3.is_3d_polyline() && b3.vertices.size() == 3 && near(3.0, b3.vertices[2].position.z) && b3.vertices[0].flags == 32, what + ": 3D back");
        hdxf_test::assert_true(bm.is_polygon_mesh() && bm.m_count == 2 && bm.n_count == 3 && bm.smooth_m == 6 && bm.smooth_type == 6, what + ": mesh back");
        hdxf_test::assert_true(bf.is_polyface_mesh() && bf.vertices.size() == 6 && bf.vertices[5].face[3] == -4 && bf.vertices[4].flags == 128, what + ": polyface back");
    });
}

TEST(entities_r13_r14_polyline_in_paper_space_and_in_a_block) {
    for_each_target([](DxfVersion v, DxfFormat f, const std::string& what) {
        hdxf::Drawing d(v);
        hdxf::Entity& paper = d.add_polyline({{0, 0}, {1, 0}, {1, 1}});
        paper.props.paper_space = 1;
        hdxf::Block& blk = d.add_block("PBLK");
        blk.entities.push_back(polyline_entity(0, {{0, 0, 0}, {2, 0, 0}}));
        d.add_insert("PBLK", {5, 5, 0});
        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto polys = of_type(recs, "POLYLINE");
        hdxf_test::assert_eq(2, static_cast<int>(polys.size()), what);
        const Rec* in_paper = has_code(*polys[0], 67) ? polys[0] : polys[1];
        const Rec* in_block = in_paper == polys[0] ? polys[1] : polys[0];
        hdxf_test::assert_eq(1, std::atoi(in_paper->value(67).c_str()), what);
        // VERTEX and SEQEND of a paper space polyline have 67 1 too (R13:Common Entity Group Codes)
        const auto& all = recs;
        bool inside = false;
        int children = 0;
        for (const auto& r : all) {
            if (&r == in_paper) { inside = true; continue; }
            if (!inside) continue;
            if (r.type() == "VERTEX" || r.type() == "SEQEND") {
                hdxf_test::assert_eq(1, std::atoi(r.value(67).c_str()), what + ": " + r.type() + " of a paper space polyline");
                ++children;
                if (r.type() == "SEQEND") break;
            }
        }
        hdxf_test::assert_eq(4, children, what);
        hdxf_test::assert_false(has_code(*in_block, 67), what);
        expect_owner_if_written(*in_block, record_handle(recs, "PBLK"), what);
        expect_owner_if_written(*in_paper, record_handle(recs, "*PAPER_SPACE"), what);
        // the owner of a vertex is its polyline, when owners are written
        for (const Rec* p : {in_paper, in_block}) {
            bool inside2 = false;
            for (const auto& r : all) {
                if (&r == p) { inside2 = true; continue; }
                if (!inside2) continue;
                if (r.type() != "VERTEX" && r.type() != "SEQEND") break;
                expect_owner_if_written(r, handle_of(*p), what);
            }
        }
        expect_unique_handles(recs, what);
    });
}

TEST(entities_r13_r14_lwpolyline_is_r14s) {
    for (DxfFormat f : both_formats) {
        const std::string what = "R14 " + name_of(f);
        hdxf::Drawing d(DxfVersion::R14);
        hdxf::Entity& lw = d.add_lwpolyline({{0, 0}, {5, 0}, {5, 5}, {0, 5}}, true);
        lw.as_lwpolyline().elevation = 4.0;
        lw.as_lwpolyline().vertices[1].bulge = 0.3;
        lw.props.thickness = 1.0;
        lw.props.extrusion = {0, 0, -1};
        hdxf::Entity& wide = d.add_lwpolyline({{0, 10}, {5, 10}, {5, 15}}, false);
        wide.as_lwpolyline().vertices[0].start_width = 0.5;
        wide.as_lwpolyline().vertices[0].end_width = 0.25;
        wide.as_lwpolyline().vertices[1].start_width = 0.25;
        wide.as_lwpolyline().vertices[1].end_width = 0.1;
        d.add_lwpolyline({{0, 20}, {1, 20}});
        std::vector<Rec> recs = records_of(save(d, f, what));
        const auto polys = of_type(recs, "LWPOLYLINE");
        hdxf_test::assert_eq(3, static_cast<int>(polys.size()), what);
        hdxf_test::assert_eq("AcDbEntity AcDbPolyline", markers_of(*polys[0]), what);
        // 90 70 43 [38] [39] vertices (10 20 [42]) 210 (puppenhaus, Stephen-legg's plan: 90 70 43 10 20 …)
        Codes first = {0, 100, 8, 100, 90, 70, 43, 38, 39};
        for (int c : {10, 20, 10, 20, 42, 10, 20, 10, 20}) first.push_back(c);
        for (int c : {210, 220, 230}) first.push_back(c);
        hdxf_test::assert_eq(codes_text(first), codes_text(codes_of(*polys[0])), what + ": LWPOLYLINE");
        hdxf_test::assert_eq(4, std::atoi(polys[0]->value(90).c_str()), what);
        hdxf_test::assert_eq(1, std::atoi(polys[0]->value(70).c_str()), what);
        // different widths: 40 and 41 per vertex, no 43
        Codes second = {0, 100, 8, 100, 90, 70};
        for (int i = 0; i < 3; ++i) for (int c : {10, 20, 40, 41}) second.push_back(c);
        hdxf_test::assert_eq(codes_text(second), codes_text(codes_of(*polys[1])), what + ": varying widths");
        hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 90, 70, 43, 10, 20, 10, 20}), codes_text(codes_of(*polys[2])), what + ": plain");
        // No POLYLINE was made of them: no flattening, no conversion
        hdxf_test::assert_eq(0, static_cast<int>(of_type(recs, "POLYLINE").size()), what);
        expect_unique_handles(recs, what);

        hdxf::Drawing back = read_back(save(d, f, what), what);
        const auto& bl = back.entities()[0].as_lwpolyline();
        hdxf_test::assert_true(bl.is_closed() && bl.vertices.size() == 4 && near(0.3, bl.vertices[1].bulge) && near(4.0, bl.elevation) &&
                                   near(-1.0, back.entities()[0].props.extrusion.z) && near(1.0, back.entities()[0].props.thickness),
                               what + ": LWPOLYLINE back");
        const auto& bw = back.entities()[1].as_lwpolyline();
        hdxf_test::assert_true(near(0.5, bw.vertices[0].start_width) && near(0.25, bw.vertices[0].end_width) && near(0.1, bw.vertices[1].end_width),
                               what + ": widths back");
    }

    // R13: the exact error, the drawing as it was (no automatic POLYLINE)
    for (DxfFormat f : both_formats) {
        hdxf::Drawing d(DxfVersion::R13);
        d.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}});
        hdxf_test::assert_eq("LWPOLYLINE requires DXF R14 or later", save_error(d, f), "R13");
        hdxf_test::assert_eq(1, static_cast<int>(d.entities().size()), "R13: still there");
        hdxf_test::assert_true(d.entities()[0].type == hdxf::EntityType::LwPolyline, "R13: not converted");
        hdxf_test::assert_true(d.entities()[0].props.handle.empty(), "R13: no handle given");
        // a block's LWPOLYLINE too
        hdxf::Drawing b(DxfVersion::R13);
        b.add_block("B").entities.push_back(d.entities()[0]);
        hdxf_test::assert_eq("LWPOLYLINE requires DXF R14 or later", save_error(b, f), "R13 in a block");
    }
}

// ---------------------------------------------------------------------------
// The 2000 writer is unchanged
// ---------------------------------------------------------------------------

TEST(entities_r13_r14_2000_form_is_unchanged) {
    hdxf::Drawing d(DxfVersion::R2000);
    d.add_polyline({{0, 0}, {1, 0}, {1, 1}});
    d.add_mtext({0, 0, 0}, "t").as_mtext().line_spacing_factor = 1.5;
    d.add_leader({{0, 0, 0}, {1, 1, 0}}).as_leader().block_offset = {1, 0, 0};
    d.add_arc({0, 0, 0}, 1, 0, 90).props.extrusion = {0, 1, 0};
    d.add_line({0, 0, 0}, {1, 0, 0}).props.lineweight = 35;
    std::vector<Rec> recs = records_of(save(d, DxfFormat::Ascii, "2000"));
    hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 66, 10, 20, 30, 70}), codes_text(codes_of(*of_type(recs, "POLYLINE")[0])), "2000 POLYLINE has 70");
    hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 100, 10, 20, 30, 70}), codes_text(codes_of(*of_type(recs, "VERTEX")[0])), "2000 VERTEX has 70");
    hdxf_test::assert_true(has_code(*of_type(recs, "MTEXT")[0], 73) && has_code(*of_type(recs, "MTEXT")[0], 44), "2000 MTEXT has 73 and 44");
    hdxf_test::assert_true(has_code(*of_type(recs, "LEADER")[0], 212), "2000 LEADER 212");
    hdxf_test::assert_eq(codes_text({0, 100, 8, 100, 10, 20, 30, 40, 210, 220, 230, 100, 50, 51}), codes_text(codes_of(*of_type(recs, "ARC")[0])), "2000 ARC: 210 before AcDbArc");
    hdxf_test::assert_true(has_code(*of_type(recs, "LINE")[0], 370), "2000 LINE has 370");
}

// ---------------------------------------------------------------------------
// Entities AutoCAD's own R13 and R14 files hold, written anew and compared group by group
// ---------------------------------------------------------------------------

namespace {

// A sample's record against ours: same codes, same values — except what the writer chooses (handles, owners)
// and numbers, compared as numbers
std::string compare_records(const Rec& sample, const Rec& ours) {
    auto a = plain(sample);
    auto b = plain(ours);
    // The reactor/extension dictionary groups are left out of both
    std::string text_a, text_b;
    for (const auto& g : a) text_a += std::to_string(g.first) + " ";
    for (const auto& g : b) text_b += std::to_string(g.first) + " ";
    if (text_a != text_b) return "codes\n  file: " + text_a + "\n  ours: " + text_b;
    for (size_t i = 0; i < a.size(); ++i) {
        const int code = a[i].first;
        const auto type = hdxf::internal::group_value_type(code);
        if (type == hdxf::internal::GroupValueType::Double) {
            if (!near(num(a[i].second), num(b[i].second))) {
                return "value of " + std::to_string(code) + ": file " + a[i].second + ", ours " + b[i].second;
            }
        } else if (type == hdxf::internal::GroupValueType::Int16 || type == hdxf::internal::GroupValueType::Int32) {
            if (std::atoi(a[i].second.c_str()) != std::atoi(b[i].second.c_str())) {
                return "value of " + std::to_string(code) + ": file " + a[i].second + ", ours " + b[i].second;
            }
        } else if (a[i].second != b[i].second) {
            return "value of " + std::to_string(code) + ": file \"" + a[i].second + "\", ours \"" + b[i].second + "\"";
        }
    }
    return {};
}

// Reads `path`, writes its ENTITIES of the wanted types into a new drawing of `version` and compares
// the records one by one. Returns the number of records compared; skips when the file is not there.
int compare_with_file(const std::filesystem::path& path, DxfVersion version, const std::set<std::string>& types,
                      size_t limit = 40) {
    if (!std::filesystem::exists(path)) hdxf_test::skip("the test file is not there: " + hdxf::utf8_from_path(path));
    std::vector<uint8_t> bytes = read_file(path);
    hdxf::Drawing source = read_back(bytes, hdxf::utf8_from_path(path));
    hdxf_test::assert_true(source.version() == version, hdxf::utf8_from_path(path) + ": version");

    std::vector<Rec> sample_recs = records_of(bytes);
    std::vector<const Rec*> sample;
    {
        bool take = false;
        for (const auto* r : in_section(sample_recs, "ENTITIES")) {
            const std::string& t = r->type();
            bool is_child = t == "VERTEX" || t == "SEQEND" || t == "ATTRIB";
            if (!is_child) take = types.count(t) > 0;
            if (take) sample.push_back(r);
        }
    }

    hdxf::Drawing d(version);
    size_t kept = 0;
    std::set<std::string> layers;
    for (const auto& e : source.entities()) {
        const char* name = nullptr;
        switch (e.type) {
            case hdxf::EntityType::Line: name = "LINE"; break;
            case hdxf::EntityType::Circle: name = "CIRCLE"; break;
            case hdxf::EntityType::Arc: name = "ARC"; break;
            case hdxf::EntityType::Point: name = "POINT"; break;
            case hdxf::EntityType::Text: name = "TEXT"; break;
            case hdxf::EntityType::Polyline: name = "POLYLINE"; break;
            case hdxf::EntityType::Solid: name = "SOLID"; break;
            case hdxf::EntityType::Trace: name = "TRACE"; break;
            case hdxf::EntityType::Face3D: name = "3DFACE"; break;
            case hdxf::EntityType::Ellipse: name = "ELLIPSE"; break;
            case hdxf::EntityType::Spline: name = "SPLINE"; break;
            case hdxf::EntityType::MText: name = "MTEXT"; break;
            case hdxf::EntityType::LwPolyline: name = "LWPOLYLINE"; break;
            default: break;
        }
        if (!name || !types.count(name) || kept >= limit) continue;
        hdxf::Entity copy = e;
        copy.raw = hdxf::RawRecord{};
        copy.props.handle.clear();
        copy.props.reactors.clear();
        copy.props.xdictionary.clear();
        if (copy.type == hdxf::EntityType::Polyline) {
            for (auto& v : copy.as_polyline().vertices) { v.handle.clear(); v.reactors.clear(); v.raw = hdxf::RawRecord{}; }
            copy.as_polyline().seqend_handle.clear();
            copy.as_polyline().seqend_raw = hdxf::RawRecord{};
        }
        if (layers.insert(copy.props.layer).second && copy.props.layer != "0") d.add_layer(copy.props.layer);
        d.add_entity(copy);
        ++kept;
    }
    hdxf_test::assert_true(kept > 0, hdxf::utf8_from_path(path) + ": no entity of the wanted types");

    std::vector<Rec> mine_recs = records_of(save(d, DxfFormat::Ascii, hdxf::utf8_from_path(path)));
    std::vector<const Rec*> mine;
    for (const auto* r : in_section(mine_recs, "ENTITIES")) mine.push_back(r);

    // The sample's records are those of the entities we took, in order: compare as many as we wrote
    hdxf_test::assert_true(sample.size() >= mine.size(), hdxf::utf8_from_path(path) + ": the file has fewer records than we wrote");
    int compared = 0;
    for (size_t i = 0; i < mine.size(); ++i) {
        std::string diff = compare_records(*sample[i], *mine[i]);
        hdxf_test::assert_true(diff.empty(), hdxf::utf8_from_path(path) + ": " + mine[i]->type() + " no. " + std::to_string(i) + ", handle " +
                                                 handle_of(*sample[i]) + ": " + diff);
        ++compared;
    }
    return compared;
}

} // namespace

TEST(entities_r13_r14_written_anew_equal_autocad_records_r13) {
    // AutoCAD R13 itself (format profile 1.4: v13, dxfp): LINE and TEXT; ARC, CIRCLE, LINE, POLYLINE
    int n = compare_with_file(external("libre", "r13_v.dxf"), DxfVersion::R13, {"LINE", "TEXT"});
    hdxf_test::assert_true(n > 10, "r13_v: records compared");
    n = compare_with_file(external("gvsig", "dxfprueba.dxf"), DxfVersion::R13, {"LINE", "ARC", "CIRCLE", "POLYLINE"});
    hdxf_test::assert_true(n > 30, "dxfprueba: records compared");
}

TEST(entities_r13_r14_written_anew_equal_autocad_records_polylines) {
    // AutoCAD 2000's family saving as R13: 16 POLYLINE, 43 VERTEX, 16 SEQEND
    int n = compare_with_file(external("ezdxf", "small_r13.dxf"), DxfVersion::R13, {"POLYLINE"});
    hdxf_test::assert_eq(75, n, "small_r13: 16 + 43 + 16 records");
    // AutoCAD R14's polygon meshes (jscad/pyramid-base.dxf)
    n = compare_with_file(external("jscad", "pyramid-base.dxf"), DxfVersion::R14, {"POLYLINE"}, 10);
    hdxf_test::assert_true(n > 20, "pyramid-base: records compared");
}

TEST(entities_r13_r14_written_anew_equal_autocad_records_r14_faces) {
    int n = compare_with_file(external("spatialauxetic", "simp.DXF"), DxfVersion::R14, {"3DFACE"}, 50);
    hdxf_test::assert_true(n >= 50, "simp: records compared");
}

TEST(entities_r13_r14_written_anew_equal_local_autocad_records) {
    const std::filesystem::path dir = external("local-r13r14", "");
    int total = 0;
    // R13: MII200HB (ELLIPSE, POLYLINE, ARC, CIRCLE, LINE), az2150 (TEXT, POINT, POLYLINE)
    total += compare_with_file(external("local-r13r14", "MII200HB_arribada.dxf"), DxfVersion::R13, {"ELLIPSE"}, 8);
    total += compare_with_file(external("local-r13r14", "az2150_waterloo.dxf"), DxfVersion::R13, {"POLYLINE", "TEXT", "POINT"}, 30);
    // R14: SPLINE and MTEXT (7H12R10F), LWPOLYLINE (puppenhaus), SOLID
    total += compare_with_file(external("local-r13r14", "7H12R10F_schneider_r14_dimension_spline.dxf"), DxfVersion::R14, {"SPLINE", "SOLID", "POLYLINE"}, 40);
    total += compare_with_file(external("local-r13r14", "puppenhaus_4mm_r14_viewport_view.dxf"), DxfVersion::R14, {"LWPOLYLINE"}, 40);
    hdxf_test::assert_true(total > 100, "records compared: " + std::to_string(total));
    (void)dir;
}

// ---------------------------------------------------------------------------
// Reactors and the extension dictionary: R13/R14 put the reactor group first (R13 and R14 references,
// format profile 7.1), 2000 the extension dictionary
// ---------------------------------------------------------------------------

TEST(entities_r13_r14_reactors_before_the_extension_dictionary) {
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14, DxfVersion::R2000}) {
        for (DxfFormat f : both_formats) {
            const std::string what = std::string(v == DxfVersion::R2000 ? "2000" : name_of(v)) + " " + name_of(f);
            hdxf::Drawing d(v);
            hdxf::Entity& line = d.add_line({0, 0, 0}, {1, 1, 0});
            line.props.handle = "A00";
            for (const char* h : {"A01", "A02"}) {
                hdxf::Object dict;
                dict.type = hdxf::ObjectType::Dictionary;
                dict.handle = h;
                dict.owner = h == std::string("A01") ? "A00" : "0";
                hdxf::DictionaryData data;
                data.cloning = 1;
                dict.data = data;
                d.objects().push_back(dict);
            }
            line.props.xdictionary = "A01";
            line.props.reactors = {"A02"};
            std::vector<Rec> recs = records_of(save(d, f, what));
            const Rec* r = nullptr;
            for (const Rec* x : of_type(recs, "LINE")) if (handle_of(*x) == "A00") r = x;
            hdxf_test::assert_true(r != nullptr, what + ": the LINE keeps its handle");
            std::vector<std::string> order;
            for (const auto& g : r->groups) {
                if (g.first == 102 && !g.second.empty() && g.second[0] == '{') order.push_back(g.second);
            }
            hdxf_test::assert_eq(2, static_cast<int>(order.size()), what);
            const bool older = v != DxfVersion::R2000;
            hdxf_test::assert_eq(older ? "{ACAD_REACTORS" : "{ACAD_XDICTIONARY", order[0], what + ": first");
            hdxf_test::assert_eq(older ? "{ACAD_XDICTIONARY" : "{ACAD_REACTORS", order[1], what + ": second");
            expect_unique_handles(recs, what);
        }
    }
}
