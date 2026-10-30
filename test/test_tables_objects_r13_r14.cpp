#include "test_framework.hpp"
#include "dxf_structure.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The tables, BLOCK_RECORD, OBJECTS and CLASSES of DXF R13 and R14 (plan phase 7, steps 7.6, 7.9 and
// 7.10, B90, B92): the records have the groups R13 and R14 have — the R13/R14 references and AutoCAD's own
// files (libre/r13_v.dxf, gvsig/dxfprueba.dxf, spatialauxetic/HEAD.DXF and the local Test3_* files:
// doc/reference/hdxf_r13_r14_format.md) — and none of 2000's. No golden files: what is checked is group
// by group what those files have and what the references say. The 330 owner groups are not looked at
// here (family N / family A2000 is the common helpers' choice).
// ---------------------------------------------------------------------------

namespace {

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using Group = std::pair<int, std::string>;

const DxfVersion older_versions[] = {DxfVersion::R13, DxfVersion::R14};
const DxfFormat both_formats[] = {DxfFormat::Ascii, DxfFormat::Binary};

std::string name_of(DxfVersion v) { return v == DxfVersion::R13 ? "R13" : "R14"; }
std::string name_of(DxfFormat f) { return f == DxfFormat::Ascii ? "ASCII" : "binary"; }

struct Rec {
    std::string section;
    std::vector<Group> g;       // g[0] is the 0 group

    const std::string& type() const { return g[0].second; }
    bool has(int code) const {
        return std::any_of(g.begin(), g.end(), [&](const Group& x) { return x.first == code; });
    }
    std::string value(int code) const {
        for (const auto& x : g) if (x.first == code) return x.second;
        return {};
    }
    int count(int code) const {
        return static_cast<int>(std::count_if(g.begin(), g.end(), [&](const Group& x) { return x.first == code; }));
    }
    std::string name() const { return value(2); }
    // The value of the first group with the code after the first group `100 <marker>`
    std::string after(const std::string& marker, int code) const {
        bool in = false;
        for (const auto& x : g) {
            if (x.first == 100) in = x.second == marker;
            else if (in && x.first == code) return x.second;
        }
        return {};
    }
};

std::vector<Rec> parse(const std::vector<uint8_t>& data) {
    std::istringstream in(std::string(data.begin(), data.end()), std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Rec> out;
    std::string section;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        std::string value = dxf_structure::number_text(gc.code, gc.value);
        while (!value.empty() && value.back() == ' ' && gc.code != 1) value.pop_back();
        if (gc.code == 0) out.push_back({section, {}});
        if (out.empty()) continue;
        out.back().g.push_back({gc.code, value});
        if (gc.code == 2 && out.back().type() == "SECTION") section = value;
        if (gc.code == 0 && value == "ENDSEC") section.clear();
    }
    hdxf_test::assert_true(reader.error().empty(), "reading the file: " + reader.error());
    return out;
}

std::vector<const Rec*> in_section(const std::vector<Rec>& recs, const std::string& section) {
    std::vector<const Rec*> out;
    for (const auto& r : recs) if (r.section == section) out.push_back(&r);
    return out;
}

// The records of a table (not its head), the TABLE record of a table
std::vector<const Rec*> table_records(const std::vector<Rec>& recs, const std::string& type) {
    std::vector<const Rec*> out;
    for (const auto* r : in_section(recs, "TABLES")) if (r->type() == type) out.push_back(r);
    return out;
}

const Rec* table_head(const std::vector<Rec>& recs, const std::string& table) {
    for (const auto* r : in_section(recs, "TABLES")) {
        if (r->type() == "TABLE" && r->value(2) == table) return r;
    }
    return nullptr;
}

const Rec* record_named(const std::vector<Rec>& recs, const std::string& type, const std::string& name) {
    for (const auto* r : table_records(recs, type)) if (r->name() == name) return r;
    return nullptr;
}

// The codes of a table record from its name (2) on, XDATA left out
std::vector<int> body_codes(const Rec& r) {
    std::vector<int> out;
    bool body = false;
    for (const auto& x : r.g) {
        if (x.first == 2) body = true;
        if (x.first >= 1000) break;
        if (body) out.push_back(x.first);
    }
    return out;
}

std::string codes_text(const std::vector<int>& codes) {
    std::string s;
    for (int c : codes) s += std::to_string(c) + " ";
    return s;
}

std::string dump(const Rec& r) {
    std::string s;
    for (const auto& [c, v] : r.g) s += std::to_string(c) + "=" + v + ";";
    return s;
}

std::vector<int> codes(std::initializer_list<int> list) { return std::vector<int>(list); }

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

bool exists(const std::string& name) { return std::filesystem::exists(external(name)); }

std::vector<uint8_t> read_bytes(const std::string& name) {
    std::ifstream in(external(name), std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

hdxf::Drawing load_file(const std::string& name) {
    hdxf::Drawing dwg;
    std::vector<uint8_t> bytes = read_bytes(name);
    hdxf_test::assert_true(dwg.load_from_memory(bytes.data(), bytes.size()), name + ": " + dwg.last_error());
    return dwg;
}

std::vector<uint8_t> save(hdxf::Drawing& dwg, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = dwg.save_to_memory(format);
    hdxf_test::assert_false(out.empty(), what + ": " + dwg.last_error());
    return out;
}

bool is_binary(const std::vector<uint8_t>& data) {
    return data.size() > 18 && std::string(data.begin(), data.begin() + 18) == "AutoCAD Binary DXF";
}

// The text of the groups of the records of a type in a section, to compare as a whole
std::vector<std::string> dumps_of(const std::vector<Rec>& recs, const std::string& section, const std::string& type) {
    std::vector<std::string> out;
    for (const auto* r : in_section(recs, section)) if (r->type() == type) out.push_back(dump(*r));
    return out;
}

hdxf::Object* find_named(hdxf::Drawing& dwg, const std::string& dictionary_name) {
    for (auto& o : dwg.objects()) {
        if (o.type != hdxf::ObjectType::Dictionary || o.owner != "0") continue;
        for (const auto& e : o.as_dictionary().entries) {
            if (e.name == dictionary_name) return dwg.find_object(e.handle);
        }
    }
    return nullptr;
}

// 7.6 ---------------------------------------------------------------------------------------------------

hdxf::Drawing tables_drawing(DxfVersion v) {
    hdxf::Drawing dwg(v);
    hdxf::Color red = hdxf::Color::from_aci(1);
    dwg.add_layer("WALLS", red);
    dwg.add_linetype("DASHED", "Dashed", {0.5, -0.25});
    dwg.add_text_style("NOTES", "simplex");
    hdxf::View view;
    view.name = "V1";
    view.height = 10;
    view.width = 20;
    view.center = {5, 5};
    view.direction = {0, 0, 1};
    view.lens_length = 50;
    dwg.views().push_back(view);
    hdxf::Ucs ucs;
    ucs.name = "U1";
    ucs.origin = {1, 2, 3};
    dwg.ucs_list().push_back(ucs);
    dwg.add_app_id("MYAPP");
    dwg.add_block("PART");
    dwg.add_line({0, 0, 0}, {10, 0, 0}, "WALLS");
    return dwg;
}

TEST(tables_r13_r14_new_records_have_the_older_form) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg = tables_drawing(v);
            std::vector<uint8_t> out = save(dwg, format, what);
            std::vector<Rec> recs = parse(out);

            // The nine tables in AutoCAD's order, VIEW and UCS with their one record each here
            std::vector<std::string> tables;
            for (const auto* r : in_section(recs, "TABLES")) {
                if (r->type() == "TABLE") {
                    tables.push_back(r->value(2));
                    hdxf_test::assert_eq(r->after("AcDbSymbolTable", 70).empty() ? 1 : 0, 0, what + ": 70 in the head");
                    hdxf_test::assert_false(r->has(71) || r->has(340), what + ": no 71/340 in the head of " + r->value(2));
                    hdxf_test::assert_false(r->has(1001), what + ": no XDATA in a new table head");
                }
            }
            hdxf_test::assert_true(tables == std::vector<std::string>({"VPORT", "LTYPE", "LAYER", "STYLE", "VIEW", "UCS",
                                                                       "APPID", "DIMSTYLE", "BLOCK_RECORD"}),
                                   what + ": tables");
            // (2000's AcDbDimStyleTable has no place in R13/R14)
            for (const auto* r : in_section(recs, "TABLES")) {
                hdxf_test::assert_false(r->has(100) && r->value(100) == "AcDbDimStyleTable", what);
            }

            // VPORT *ACTIVE: R13's groups, none of 2000's
            const Rec* vport = record_named(recs, "VPORT", "*ACTIVE");
            hdxf_test::assert_true(vport != nullptr, what + ": VPORT *ACTIVE");
            if (!vport) return;
            hdxf_test::assert_eq(codes_text(body_codes(*vport)),
                                 codes_text(codes({2, 70, 10, 20, 11, 21, 12, 22, 13, 23, 14, 24, 15, 25, 16, 26, 36, 17, 27, 37,
                                                   40, 41, 42, 43, 44, 50, 51, 71, 72, 73, 74, 75, 76, 77, 78})),
                                 what + ": VPORT groups");

            // LTYPE: the standard three in capitals, then DASHED with 49 and 74 for each element
            std::vector<std::string> linetypes;
            for (const auto* r : table_records(recs, "LTYPE")) linetypes.push_back(r->name());
            hdxf_test::assert_true(linetypes == std::vector<std::string>({"BYBLOCK", "BYLAYER", "CONTINUOUS", "DASHED"}),
                                   what + ": linetypes " + std::to_string(linetypes.size()));
            const Rec* continuous = record_named(recs, "LTYPE", "CONTINUOUS");
            hdxf_test::assert_eq(continuous->value(3), "Solid line", what + ": CONTINUOUS");
            hdxf_test::assert_eq(continuous->value(72), "65", what);
            const Rec* dashed = record_named(recs, "LTYPE", "DASHED");
            hdxf_test::assert_eq(codes_text(body_codes(*dashed)), codes_text(codes({2, 70, 3, 72, 73, 40, 49, 74, 49, 74})),
                                 what + ": DASHED groups");
            hdxf_test::assert_eq(dashed->value(73), "2", what);

            // LAYER: 70, 62 and 6 and nothing for lineweight, plotting or plot style
            for (const auto* r : table_records(recs, "LAYER")) {
                hdxf_test::assert_eq(codes_text(body_codes(*r)), codes_text(codes({2, 70, 62, 6})),
                                     what + ": LAYER " + r->name() + ": " + dump(*r));
            }
            const Rec* layer0 = record_named(recs, "LAYER", "0");
            hdxf_test::assert_true(layer0 != nullptr, what + ": layer 0");
            hdxf_test::assert_eq(layer0->value(62), "7", what);
            hdxf_test::assert_eq(layer0->value(6), "CONTINUOUS", what + ": the linetype of layer 0 in capitals");
            hdxf_test::assert_eq(record_named(recs, "LAYER", "WALLS")->value(62), "1", what);

            // STYLE: STANDARD (txt, 0.2) and NOTES
            const Rec* standard = record_named(recs, "STYLE", "STANDARD");
            hdxf_test::assert_true(standard != nullptr, what + ": STYLE STANDARD");
            hdxf_test::assert_eq(codes_text(body_codes(*standard)), codes_text(codes({2, 70, 40, 41, 50, 71, 42, 3, 4})), what);
            hdxf_test::assert_eq(standard->value(3), "txt", what);
            hdxf_test::assert_true(std::atof(standard->value(42).c_str()) == 0.2, what + ": last height " + standard->value(42));
            hdxf_test::assert_true(record_named(recs, "STYLE", "NOTES") != nullptr, what);

            // VIEW and UCS
            const Rec* view = record_named(recs, "VIEW", "V1");
            hdxf_test::assert_true(view != nullptr, what + ": VIEW");
            hdxf_test::assert_eq(codes_text(body_codes(*view)),
                                 codes_text(codes({2, 70, 40, 10, 20, 41, 11, 21, 31, 12, 22, 32, 42, 43, 44, 50, 71})),
                                 what + ": VIEW groups");
            const Rec* ucs = record_named(recs, "UCS", "U1");
            hdxf_test::assert_true(ucs != nullptr, what + ": UCS");
            hdxf_test::assert_eq(codes_text(body_codes(*ucs)), codes_text(codes({2, 70, 10, 20, 30, 11, 21, 31, 12, 22, 32})),
                                 what + ": UCS groups");

            // APPID, BLOCK_RECORD: the name (and flags) only — no 340, no references, no 70/280/281
            hdxf_test::assert_eq(codes_text(body_codes(*record_named(recs, "APPID", "MYAPP"))), codes_text(codes({2, 70})), what);
            std::vector<std::string> blocks;
            for (const auto* r : table_records(recs, "BLOCK_RECORD")) {
                blocks.push_back(r->name());
                hdxf_test::assert_eq(codes_text(body_codes(*r)), codes_text(codes({2})), what + ": BLOCK_RECORD " + r->name());
                hdxf_test::assert_false(r->has(340) || r->has(331) || r->has(1001), what + ": " + dump(*r));
            }
            hdxf_test::assert_true(blocks == std::vector<std::string>({"*MODEL_SPACE", "*PAPER_SPACE", "PART"}),
                                   what + ": BLOCK_RECORDs");

            // No 2000 group in any table record
            for (const auto* r : in_section(recs, "TABLES")) {
                if (r->type() == "DIMSTYLE" || r->type() == "TABLE" || r->type() == "ENDTAB") continue;
                for (int code : {281, 65, 110, 111, 112, 79, 146, 370, 390, 290, 347, 348, 345, 346}) {
                    hdxf_test::assert_false(r->has(code), what + ": " + r->type() + " " + r->name() + " has " + std::to_string(code));
                }
            }
        }
    }
}

// The same records as AutoCAD's own R13 and R14 files: the groups, in their order
TEST(tables_r13_r14_records_follow_autocads_files) {
    struct File { const char* path; DxfVersion version; };
    int compared = 0;
    for (const File& f : {File{"libre/r13_v.dxf", DxfVersion::R13}, File{"gvsig/dxfprueba.dxf", DxfVersion::R13},
                          File{"spatialauxetic/HEAD.DXF", DxfVersion::R14},
                          File{"local-r13r14/Test3_Flash_Tank_I_r14.dxf", DxfVersion::R14}}) {
        if (!exists(f.path)) continue;
        std::vector<Rec> facit = parse(read_bytes(f.path));
        hdxf::Drawing dwg = tables_drawing(f.version);
        std::vector<Rec> mine = parse(save(dwg, DxfFormat::Ascii, f.path));
        for (const char* type : {"VPORT", "LTYPE", "LAYER", "STYLE", "APPID", "BLOCK_RECORD"}) {
            for (const auto* theirs : table_records(facit, type)) {
                // The standard records AutoCAD's file has by those names: groups and, for the fixed ones, values
                const Rec* ours = record_named(mine, type, theirs->name());
                if (!ours) continue;
                hdxf_test::assert_eq(codes_text(body_codes(*ours)), codes_text(body_codes(*theirs)),
                                     std::string(f.path) + ": " + type + " " + theirs->name());
                ++compared;
                const std::string their_name = theirs->name();
                bool standard_linetype = their_name == "BYBLOCK" || their_name == "BYLAYER" || their_name == "CONTINUOUS";
                if ((std::string(type) == "LTYPE" && standard_linetype) || std::string(type) == "APPID" ||
                    std::string(type) == "BLOCK_RECORD" || (std::string(type) == "LAYER" && their_name == "0")) {
                    for (int code : {2, 3, 6, 62, 72, 73}) {
                        hdxf_test::assert_eq(ours->value(code), theirs->value(code),
                                             std::string(f.path) + ": " + type + " " + theirs->name() + " " + std::to_string(code));
                    }
                }
            }
        }
        // The table counts of a new drawing: AutoCAD's own for the same content (LTYPE and BLOCK_RECORD less two)
        hdxf_test::assert_eq(table_head(mine, "LTYPE")->after("AcDbSymbolTable", 70), "2", "LTYPE: four records, 70 is 2");
    }
    if (compared == 0) hdxf_test::skip("no R13/R14 file of AutoCAD's is there");
}

// D5: 70 of the table heads
TEST(tables_r13_r14_counts) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing empty(v);
            std::vector<Rec> recs = parse(save(empty, format, what));
            auto count = [&](const char* table) { return table_head(recs, table)->after("AcDbSymbolTable", 70); };
            hdxf_test::assert_eq(count("VPORT"), v == DxfVersion::R13 ? "2" : "1", what + ": VPORT");
            hdxf_test::assert_eq(count("LTYPE"), "1", what + ": LTYPE (BYBLOCK and BYLAYER not counted)");
            hdxf_test::assert_eq(count("LAYER"), "1", what + ": LAYER");
            hdxf_test::assert_eq(count("STYLE"), "1", what + ": STYLE");
            hdxf_test::assert_eq(count("VIEW"), "0", what + ": VIEW");
            hdxf_test::assert_eq(count("UCS"), "0", what + ": UCS");
            hdxf_test::assert_eq(count("APPID"), "1", what + ": APPID");
            hdxf_test::assert_eq(count("DIMSTYLE"), "1", what + ": DIMSTYLE");
            hdxf_test::assert_eq(count("BLOCK_RECORD"), "0", what + ": BLOCK_RECORD (the two spaces not counted)");

            hdxf::Drawing more = tables_drawing(v);
            std::vector<Rec> m = parse(save(more, format, what));
            auto count2 = [&](const char* table) { return table_head(m, table)->after("AcDbSymbolTable", 70); };
            hdxf_test::assert_eq(count2("LTYPE"), "2", what + ": LTYPE with DASHED");
            hdxf_test::assert_eq(count2("LAYER"), "2", what + ": LAYER");
            hdxf_test::assert_eq(count2("VIEW"), "1", what);
            hdxf_test::assert_eq(count2("UCS"), "1", what);
            hdxf_test::assert_eq(count2("BLOCK_RECORD"), "1", what + ": BLOCK_RECORD with PART");
        }
    }
}

// A read file: only what changed is written anew, in the older form; the rest as it was
TEST(tables_r13_r14_read_file_records_rewritten) {
    struct File { const char* path; };
    int run = 0;
    for (const File& f : {File{"libre/r13_v.dxf"}, File{"gvsig/dxfprueba.dxf"}, File{"spatialauxetic/HEAD.DXF"},
                          File{"ezdxf/small_r13.dxf"}, File{"ezdxf/bin_dxf_r13.dxf"}, File{"ezdxf/bin_dxf_r14.dxf"},
                          File{"local-r13r14/Test3_Flash_Tank_I_r14.dxf"}}) {
        if (!exists(f.path)) continue;
        ++run;
        const std::string what = f.path;
        std::vector<uint8_t> original_bytes = read_bytes(f.path);
        std::vector<Rec> original = parse(original_bytes);

        for (DxfFormat format : both_formats) {
            const bool same_format = is_binary(original_bytes) == (format == DxfFormat::Binary);
            hdxf::Drawing dwg = load_file(f.path);
            // Changed: a layer, a linetype, a text style, an application; added: one record of every table
            hdxf_test::assert_false(dwg.layers().empty(), what);
            std::string layer0 = dwg.layers()[0].name;
            dwg.layers()[0].color = hdxf::Color::from_aci(5);
            dwg.layers()[0].frozen = true;
            if (!dwg.linetypes().empty()) dwg.linetypes().back().description = "changed description";
            std::string style0 = dwg.text_styles().empty() ? std::string() : dwg.text_styles()[0].name;
            if (!dwg.text_styles().empty()) dwg.text_styles()[0].width_factor = 0.8;
            dwg.add_layer("NEWLAYER", hdxf::Color::from_aci(2));
            dwg.add_linetype("NEWLT", "new", {1.0, -1.0});
            dwg.add_text_style("NEWSTYLE", "simplex");
            dwg.add_app_id("NEWAPP");
            hdxf::View view;
            view.name = "NEWVIEW";
            view.height = 4;
            view.width = 5;
            view.direction = {0, 0, 1};
            dwg.views().push_back(view);
            hdxf::Ucs ucs;
            ucs.name = "NEWUCS";
            dwg.ucs_list().push_back(ucs);
            dwg.add_block("NEWBLOCK");
            std::vector<uint8_t> out = save(dwg, format, what + " " + name_of(format));
            std::vector<Rec> recs = parse(out);

            // The changed layer: 62 and 70 changed, the groups the same and none of 2000's
            const Rec* before = record_named(original, "LAYER", layer0);
            const Rec* after = record_named(recs, "LAYER", layer0);
            hdxf_test::assert_true(before && after, what + ": layer " + layer0);
            if (!before || !after) continue;
            hdxf_test::assert_eq(after->value(62), "5", what);
            hdxf_test::assert_true((std::atoi(after->value(70).c_str()) & 1) != 0, what + ": frozen " + after->value(70));
            hdxf_test::assert_eq(codes_text(body_codes(*after)), codes_text(body_codes(*before)), what + ": layer groups as read");
            hdxf_test::assert_eq(after->value(6), before->value(6), what);

            // The records no change touched are as they were, group for group: LAYER (the others), LTYPE,
            // STYLE, APPID, VPORT, BLOCK_RECORD
            for (const char* type : {"LAYER", "LTYPE", "STYLE", "APPID", "VPORT", "BLOCK_RECORD"}) {
                for (const auto* r : table_records(original, type)) {
                    const Rec* now = nullptr;
                    for (const auto* c : table_records(recs, type)) if (c->value(5) == r->value(5)) now = c;
                    hdxf_test::assert_true(now != nullptr, what + ": " + type + " " + r->name() + " is still there");
                    if (!now) continue;
                    bool changed = (std::string(type) == "LAYER" && r->name() == layer0) ||
                                   (std::string(type) == "LTYPE" && r == table_records(original, "LTYPE").back() &&
                                    now->value(3) == "changed description") ||
                                   (std::string(type) == "STYLE" && r->name() == style0);
                    if (!changed && same_format) hdxf_test::assert_eq(dump(*now), dump(*r), what + ": " + type + " " + r->name() + " as it was: " + dump(*now) + " <> " + dump(*r));
                }
            }

            // What was added is in the older form
            const Rec* layer = record_named(recs, "LAYER", "NEWLAYER");
            hdxf_test::assert_true(layer != nullptr, what + ": NEWLAYER");
            if (layer) hdxf_test::assert_eq(codes_text(body_codes(*layer)), codes_text(codes({2, 70, 62, 6})), what + ": NEWLAYER");
            const Rec* lt = record_named(recs, "LTYPE", "NEWLT");
            hdxf_test::assert_true(lt != nullptr, what + ": NEWLT");
            if (lt) hdxf_test::assert_eq(codes_text(body_codes(*lt)), codes_text(codes({2, 70, 3, 72, 73, 40, 49, 74, 49, 74})), what);
            const Rec* st = record_named(recs, "STYLE", "NEWSTYLE");
            hdxf_test::assert_true(st != nullptr, what + ": NEWSTYLE");
            const Rec* app = record_named(recs, "APPID", "NEWAPP");
            hdxf_test::assert_true(app != nullptr, what + ": NEWAPP");
            const Rec* nv = record_named(recs, "VIEW", "NEWVIEW");
            hdxf_test::assert_true(nv != nullptr, what + ": NEWVIEW");
            if (nv) hdxf_test::assert_false(nv->has(281) || nv->has(72) || nv->has(110) || nv->has(79), what + ": " + dump(*nv));
            const Rec* nu = record_named(recs, "UCS", "NEWUCS");
            hdxf_test::assert_true(nu != nullptr, what + ": NEWUCS");
            if (nu) hdxf_test::assert_false(nu->has(79) || nu->has(146) || nu->has(346), what + ": " + dump(*nu));
            const Rec* nb = record_named(recs, "BLOCK_RECORD", "NEWBLOCK");
            hdxf_test::assert_true(nb != nullptr, what + ": NEWBLOCK");
            if (nb) {
                hdxf_test::assert_eq(codes_text(body_codes(*nb)), codes_text(codes({2})), what + ": " + dump(*nb));
                hdxf_test::assert_false(nb->has(340), what);
            }

            // No 2000 group anywhere in a table of the new or changed records
            for (const char* type : {"VPORT", "LAYER", "VIEW", "UCS"}) {
                for (const auto* r : table_records(recs, type)) {
                    bool in_file = false;
                    for (const auto* o : table_records(original, type)) in_file = in_file || o->value(5) == r->value(5);
                    if (in_file && r->value(2) != layer0) continue;
                    for (int code : {370, 390, 290, 281, 65, 110, 79, 146}) {
                        bool was = false;
                        for (const auto* o : table_records(original, type)) was = was || (o->value(5) == r->value(5) && o->has(code));
                        if (!was) hdxf_test::assert_false(r->has(code), what + ": " + type + " " + r->name() + " has " + std::to_string(code));
                    }
                }
            }

            // The table heads: the file's count raised for each record added
            for (const char* table : {"LAYER", "VIEW", "UCS", "APPID"}) {
                int was = std::atoi(table_head(original, table)->after("AcDbSymbolTable", 70).c_str());
                int now = std::atoi(table_head(recs, table)->after("AcDbSymbolTable", 70).c_str());
                hdxf_test::assert_true(now > was, what + ": " + table + " count " + std::to_string(was) + " -> " + std::to_string(now));
            }
            // The tables in the file's order
            std::vector<std::string> was_tables, now_tables;
            for (const auto* r : in_section(original, "TABLES")) if (r->type() == "TABLE") was_tables.push_back(r->value(2));
            for (const auto* r : in_section(recs, "TABLES")) if (r->type() == "TABLE") now_tables.push_back(r->value(2));
            hdxf_test::assert_true(was_tables == now_tables, what + ": the order of the tables");
        }
    }
    if (run == 0) hdxf_test::skip("no R13/R14 file is there");
}

// Reactors, extension dictionary and XDATA of the records of a read file stay; a new record has its own
TEST(tables_r13_r14_reactors_extension_dictionary_and_xdata_kept) {
    // bin_dxf_r14: the VPORT table's extension dictionary, STANDARD's XDATA ACAD (the font family)
    if (exists("ezdxf/bin_dxf_r14.dxf")) {
        std::vector<Rec> original = parse(read_bytes("ezdxf/bin_dxf_r14.dxf"));
        hdxf::Drawing dwg = load_file("ezdxf/bin_dxf_r14.dxf");
        dwg.add_layer("MORE", hdxf::Color::from_aci(3));
        dwg.text_styles()[0].fixed_height = 1.5;
        for (DxfFormat format : both_formats) {
            std::vector<Rec> recs = parse(save(dwg, format, "bin_dxf_r14"));
            const Rec* head = table_head(recs, "VPORT");
            hdxf_test::assert_eq(head->value(360), table_head(original, "VPORT")->value(360), "VPORT table: extension dictionary");
            hdxf_test::assert_eq(head->value(102), "{ACAD_XDICTIONARY", "VPORT table: 102");
            const Rec* style = record_named(recs, "STYLE", "STANDARD");
            const Rec* was = record_named(original, "STYLE", "STANDARD");
            hdxf_test::assert_eq(style->value(40), "1.5", "STYLE changed");
            hdxf_test::assert_eq(style->value(1001), "ACAD", "STYLE XDATA kept");
            hdxf_test::assert_eq(style->value(1000), was->value(1000), "STYLE XDATA family kept");
            hdxf_test::assert_eq(style->value(1071), was->value(1071), "STYLE XDATA flags kept");
            hdxf_test::assert_eq(static_cast<int>(dumps_of(recs, "TABLES", "APPID").size()), static_cast<int>(dumps_of(original, "TABLES", "APPID").size()), "APPIDs");
        }
    }
    // small_r13 (AutoCAD 2000 family): the extension dictionaries of the space blocks stay with their records
    if (exists("ezdxf/small_r13.dxf")) {
        std::vector<Rec> original = parse(read_bytes("ezdxf/small_r13.dxf"));
        hdxf::Drawing dwg = load_file("ezdxf/small_r13.dxf");
        dwg.add_block("EXTRA");
        std::vector<Rec> recs = parse(save(dwg, DxfFormat::Ascii, "small_r13"));
        for (const char* space : {"*MODEL_SPACE", "*PAPER_SPACE"}) {
            const Rec* was = record_named(original, "BLOCK_RECORD", space);
            const Rec* now = record_named(recs, "BLOCK_RECORD", space);
            hdxf_test::assert_true(was && now, space);
            if (was && now) {
                hdxf_test::assert_true(now->has(360), std::string(space) + ": extension dictionary");
                hdxf_test::assert_eq(dump(*now), dump(*was), std::string(space) + " as it was");
            }
        }
    }
    // A new record with XDATA, reactors and an extension dictionary of its own, in the older form
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            hdxf::Layer& layer = dwg.add_layer("WITHDATA", hdxf::Color::from_aci(4));
            layer.xdata.push_back({"MYAPP", {{1000, "layer data"}, {1070, "7"}}});
            layer.xdictionary = "B10";
            hdxf::TextStyle& style = dwg.add_text_style("STYLEDATA");
            style.xdata.push_back({"MYAPP", {{1000, "style data"}}});
            hdxf::Block& block = dwg.add_block("DATABLOCK");
            block.record.xdata.push_back({"MYAPP", {{1000, "record data"}}});
            block.record.xdictionary = "B11";
            for (const char* h : {"B10", "B11"}) {
                hdxf::Object dict;
                dict.type = hdxf::ObjectType::Dictionary;
                dict.handle = h;
                dict.owner = std::string(h) == "B10" ? layer.handle : block.record.handle;
                dict.data = hdxf::DictionaryData{};
                dwg.objects().push_back(dict);
            }
            dwg.add_app_id("MYAPP");
            std::vector<Rec> recs = parse(save(dwg, format, what));
            const Rec* l = record_named(recs, "LAYER", "WITHDATA");
            hdxf_test::assert_true(l != nullptr, what);
            if (l) {
                hdxf_test::assert_eq(l->value(360), "B10", what + ": layer extension dictionary");
                hdxf_test::assert_eq(l->value(1000), "layer data", what + ": layer XDATA");
                hdxf_test::assert_eq(l->value(1001), "MYAPP", what);
                hdxf_test::assert_false(l->has(370) || l->has(390) || l->has(290), what);
            }
            const Rec* s = record_named(recs, "STYLE", "STYLEDATA");
            hdxf_test::assert_true(s && s->value(1000) == "style data", what + ": style XDATA");
            const Rec* b = record_named(recs, "BLOCK_RECORD", "DATABLOCK");
            hdxf_test::assert_true(b != nullptr, what);
            if (b) {
                hdxf_test::assert_eq(b->value(360), "B11", what + ": block record extension dictionary");
                hdxf_test::assert_eq(b->value(1000), "record data", what + ": block record XDATA");
            }
        }
    }
}

// What R13 and R14 have no group for is an error that names it, and nothing is written (B90, B98)
TEST(tables_r13_r14_errors_for_what_has_no_form) {
    struct Case {
        const char* what;
        std::function<void(hdxf::Drawing&)> make;
        const char* error;
    };
    const Case cases[] = {
        {"lineweight", [](hdxf::Drawing& d) { d.add_layer("A").lineweight = 50; }, "Lineweight requires DXF 2000 or later"},
        {"plot style", [](hdxf::Drawing& d) { d.add_layer("A").plot_style_handle = "ABC"; }, "Plot style requires DXF 2000 or later"},
        {"plot flag", [](hdxf::Drawing& d) { d.add_layer("A").plot = false; }, "the plot flag requires DXF 2000 or later"},
        {"layer name 32", [](hdxf::Drawing& d) { d.add_layer(std::string(32, 'L')); }, "Layer name"},
        {"linetype name 32", [](hdxf::Drawing& d) { d.add_linetype(std::string(32, 'T'), "x", {1.0, -1.0}); }, "Linetype name"},
        {"style name 32", [](hdxf::Drawing& d) { d.add_text_style(std::string(32, 'S')); }, "Text style name"},
        {"app name 32", [](hdxf::Drawing& d) { d.add_app_id(std::string(32, 'P')); }, "Application name"},
        {"block name 32", [](hdxf::Drawing& d) { d.add_block(std::string(32, 'B')); }, "Block name"},
        {"view UCS", [](hdxf::Drawing& d) { hdxf::View v; v.name = "V"; v.has_ucs = true; d.views().push_back(v); },
         "its UCS and render mode require DXF 2000 or later"},
        {"UCS orthographic", [](hdxf::Drawing& d) { hdxf::Ucs u; u.name = "U"; u.ortho_type = 3; d.ucs_list().push_back(u); },
         "require DXF 2000 or later"},
        {"viewport UCS", [](hdxf::Drawing& d) { hdxf::Vport p; p.ucs_origin = {1, 2, 3}; d.vports().push_back(p); },
         "require DXF 2000 or later"},
    };
    for (DxfVersion v : older_versions) {
        for (const Case& c : cases) {
            const std::string what = name_of(v) + " " + c.what;
            hdxf::Drawing dwg(v);
            dwg.add_line({0, 0, 0}, {1, 1, 0});
            c.make(dwg);
            std::vector<uint8_t> out = dwg.save_to_memory(DxfFormat::Ascii);
            hdxf_test::assert_true(out.empty(), what + ": refused");
            hdxf_test::assert_true(dwg.last_error().find(c.error) != std::string::npos, what + ": " + dwg.last_error());
            if (std::string(c.what).find("name 32") != std::string::npos) {
                hdxf_test::assert_true(dwg.last_error().find("31 characters") != std::string::npos, what + ": " + dwg.last_error());
            }
        }
        // 31 characters are fine, and so are the names of the standard records
        hdxf::Drawing ok(v);
        ok.add_layer(std::string(31, 'L'));
        ok.add_linetype(std::string(31, 'T'), "x", {1.0, -1.0});
        ok.add_block(std::string(31, 'B'));
        hdxf_test::assert_false(ok.save_to_memory(DxfFormat::Ascii).empty(), name_of(v) + ": " + ok.last_error());
        // A drawing of 2000 and its lineweight are fine as 2000
        hdxf::Drawing d2000(DxfVersion::R2000);
        d2000.add_layer("A").lineweight = 50;
        hdxf_test::assert_false(d2000.save_to_memory(DxfFormat::Ascii).empty(), "2000: " + d2000.last_error());
    }
}

// 7.9 ---------------------------------------------------------------------------------------------------

// The records of OBJECTS by handle
const Rec* object_of(const std::vector<Rec>& recs, const std::string& handle) {
    for (const auto* r : in_section(recs, "OBJECTS")) if (r->value(5) == handle) return r;
    return nullptr;
}

// The groups from the first `100 <marker>` to the end of the subclass, with the XDATA left out
std::vector<Group> subclass_of(const Rec& r, const std::string& marker) {
    std::vector<Group> out;
    bool in = false;
    for (const auto& x : r.g) {
        if (x.first >= 1000) break;
        if (x.first == 100) in = x.second == marker;
        else if (in) out.push_back(x);
    }
    return out;
}

TEST(objects_r13_r14_new_forms) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            dwg.add_line({0, 0, 0}, {5, 0, 0});
            dwg.add_circle({1, 1, 0}, 2);
            hdxf::Entity& line = dwg.entities()[0];
            hdxf::Entity& circle = dwg.entities()[1];
            line.props.handle = "A00";
            circle.props.handle = "A01";
            hdxf::Object& group = dwg.add_group("PAIR", {&line, &circle}, "two members");
            group.as_group().selectable = false;
            std::string group_handle = group.handle;

            // A dictionary with a soft and a hard entry, hard-owning; an XRECORD in it; a multiline style
            hdxf::Object* root = nullptr;
            for (auto& o : dwg.objects()) if (o.type == hdxf::ObjectType::Dictionary && o.owner == "0") root = &o;
            hdxf_test::assert_true(root != nullptr, what);
            hdxf::Object dict;
            dict.type = hdxf::ObjectType::Dictionary;
            dict.handle = "C10";
            dict.owner = root->handle;
            dict.reactors = {root->handle};
            hdxf::DictionaryData dd;
            dd.hard_owner = true;
            dd.entries = {{"ONE", "C11", true}, {"TWO", "C12", false}};
            dict.data = dd;
            hdxf::Object xr;
            xr.type = hdxf::ObjectType::XRecord;
            xr.handle = "C11";
            xr.owner = "C10";
            hdxf::XRecordData xd;
            xd.data = {{340, "A00"}, {1, "text"}, {70, "5"}, {10, "1.5"}, {20, "2.5"}, {30, "0"}};
            xr.data = xd;
            hdxf::Object xr2 = xr;
            xr2.handle = "C12";
            xr2.as_xrecord().data = {{1000, "d"}};
            root->as_dictionary().entries.push_back({"MY_BRANCH", "C10", false});
            hdxf::Object mline;
            mline.type = hdxf::ObjectType::MlineStyle;
            mline.handle = "C20";
            hdxf::MlineStyleData ms;
            ms.name = "DOUBLE";
            ms.flags = 3;
            ms.description = "double line";
            ms.fill_color = hdxf::Color::from_aci(5);
            ms.start_angle = 80;
            ms.end_angle = 100;
            ms.elements = {{1.0, hdxf::Color::from_aci(1), "CONTINUOUS"}, {0.0, hdxf::Color::by_layer(), "BYLAYER"},
                           {-1.0, hdxf::Color::from_aci(2), "BYLAYER"}};
            mline.data = ms;
            hdxf::Object* styles = find_named(dwg, "ACAD_MLINESTYLE");
            hdxf_test::assert_true(styles != nullptr, what);
            mline.owner = styles->handle;
            mline.reactors = {styles->handle};
            styles->as_dictionary().entries.push_back({"DOUBLE", "C20", false});
            dict.xdata.clear();
            for (hdxf::Object* o : {&dict, &xr, &xr2, &mline}) dwg.objects().push_back(*o);

            std::vector<uint8_t> out = save(dwg, format, what);
            std::vector<Rec> recs = parse(out);

            // DICTIONARY: 100 AcDbDictionary, then the entries — no 280, no 281 (R13/R14 have neither)
            for (const auto* r : in_section(recs, "OBJECTS")) {
                if (r->type() != "DICTIONARY") continue;
                std::vector<Group> sub = subclass_of(*r, "AcDbDictionary");
                for (const auto& [code, value] : sub) {
                    hdxf_test::assert_true(code == 3 || code == 350 || code == 360, what + ": " + dump(*r));
                }
            }
            const Rec* d = object_of(recs, "C10");
            hdxf_test::assert_true(d != nullptr, what + ": dictionary C10");
            if (d) {
                std::vector<Group> sub = subclass_of(*d, "AcDbDictionary");
                hdxf_test::assert_eq(static_cast<int>(sub.size()), 4, what + ": entries " + dump(*d));
                if (sub.size() == 4) {
                    hdxf_test::assert_true(sub[0] == Group(3, "ONE") && sub[1] == Group(360, "C11"), what + ": hard entry");
                    hdxf_test::assert_true(sub[2] == Group(3, "TWO") && sub[3] == Group(350, "C12"), what + ": soft entry");
                }
                hdxf_test::assert_eq(d->value(102), "{ACAD_REACTORS", what + ": reactor of the dictionary");
            }

            // XRECORD: 100 AcDbXrecord, then the data (no 280)
            const Rec* x = object_of(recs, "C11");
            hdxf_test::assert_true(x != nullptr, what + ": XRECORD");
            if (x) {
                std::vector<Group> sub = subclass_of(*x, "AcDbXrecord");
                hdxf_test::assert_eq(static_cast<int>(sub.size()), 6, what + ": " + dump(*x));
                hdxf_test::assert_true(!sub.empty() && sub[0] == Group(340, "A00"), what + ": the data starts the record: " + dump(*x));
                hdxf_test::assert_false(x->has(280), what);
            }

            // GROUP: AcDbGroup, 300, 70, 71, one 340 per member (the group is a reactor of each)
            const Rec* g = object_of(recs, group_handle);
            hdxf_test::assert_true(g != nullptr, what + ": GROUP");
            if (g) {
                std::vector<Group> sub = subclass_of(*g, "AcDbGroup");
                hdxf_test::assert_eq(static_cast<int>(sub.size()), 5, what + ": " + dump(*g));
                if (sub.size() == 5) {
                    hdxf_test::assert_true(sub[0] == Group(300, "two members") && sub[1] == Group(70, "0") &&
                                           sub[2] == Group(71, "0") && sub[3] == Group(340, "A00") && sub[4] == Group(340, "A01"),
                                           what + ": " + dump(*g));
                }
            }
            for (const auto* r : in_section(recs, "ENTITIES")) {
                if (r->value(5) != "A00" && r->value(5) != "A01") continue;
                bool reactor = false;
                for (size_t i = 0; i + 1 < r->g.size(); ++i) {
                    reactor = reactor || (r->g[i] == Group(102, "{ACAD_REACTORS") && r->g[i + 1] == Group(330, group_handle));
                }
                hdxf_test::assert_true(reactor, what + ": the group is a reactor of " + r->value(5));
            }
            const Rec* groups_dictionary = nullptr;
            for (const auto* r : in_section(recs, "OBJECTS")) {
                for (const auto& [code, value] : r->g) if (code == 350 && value == group_handle) groups_dictionary = r;
            }
            hdxf_test::assert_true(groups_dictionary != nullptr, what + ": ACAD_GROUP lists the group");

            // MLINESTYLE: AcDbMlineStyle, 2, 70, 3, 62, 51, 52, 71, then 49, 62, 6 for each element
            const Rec* m = object_of(recs, "C20");
            hdxf_test::assert_true(m != nullptr, what + ": MLINESTYLE");
            if (m) {
                std::vector<Group> sub = subclass_of(*m, "AcDbMlineStyle");
                std::vector<int> c;
                for (const auto& s : sub) c.push_back(s.first);
                hdxf_test::assert_eq(codes_text(c), codes_text(codes({2, 70, 3, 62, 51, 52, 71, 49, 62, 6, 49, 62, 6, 49, 62, 6})),
                                     what + ": " + dump(*m));
                hdxf_test::assert_eq(m->value(2), "DOUBLE", what);
                hdxf_test::assert_eq(m->value(70), "3", what);
                hdxf_test::assert_eq(m->value(62), "5", what + ": the fill color");
                hdxf_test::assert_eq(m->value(71), "3", what);
            }

            // Read back: the typed values survive an external read
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(out.data(), out.size()), what + ": " + back.last_error());
            const hdxf::Object* bm = back.find_object("C20");
            hdxf_test::assert_true(bm && bm->type == hdxf::ObjectType::MlineStyle, what + ": MLINESTYLE read back");
            if (bm && bm->type == hdxf::ObjectType::MlineStyle) {
                hdxf_test::assert_eq(bm->as_mline_style().elements.size() == 3 ? 3 : 0, 3, what);
                hdxf_test::assert_eq(bm->as_mline_style().fill_color.aci, 5, what);
                hdxf_test::assert_eq(bm->as_mline_style().description, "double line", what);
            }
            const hdxf::Object* bg = back.find_object(group_handle);
            hdxf_test::assert_true(bg && bg->type == hdxf::ObjectType::Group, what + ": GROUP read back");
            if (bg && bg->type == hdxf::ObjectType::Group) {
                hdxf_test::assert_eq(static_cast<int>(bg->as_group().entities.size()), 2, what);
                hdxf_test::assert_false(bg->as_group().selectable, what);
            }
            const hdxf::Object* bx = back.find_object("C11");
            hdxf_test::assert_true(bx && bx->type == hdxf::ObjectType::XRecord, what + ": XRECORD read back");
            if (bx && bx->type == hdxf::ObjectType::XRecord) {
                hdxf_test::assert_eq(static_cast<int>(bx->as_xrecord().data.size()), 6, what + ": the data, all of it");
            }
        }
    }
}

// A standard MLINESTYLE as AutoCAD's files have it: group by group
TEST(objects_r13_r14_standard_mline_style_follows_autocads_file) {
    if (!exists("libre/r13_v.dxf")) hdxf_test::skip("libre/r13_v.dxf is not there");
    std::vector<Rec> facit = parse(read_bytes("libre/r13_v.dxf"));
    const Rec* theirs = nullptr;
    for (const auto* r : in_section(facit, "OBJECTS")) if (r->type() == "MLINESTYLE") theirs = r;
    hdxf_test::assert_true(theirs != nullptr, "MLINESTYLE in AutoCAD's file");
    for (DxfVersion v : older_versions) {
        hdxf::Drawing dwg(v);
        std::vector<Rec> mine = parse(save(dwg, DxfFormat::Ascii, name_of(v)));
        const Rec* ours = nullptr;
        for (const auto* r : in_section(mine, "OBJECTS")) if (r->type() == "MLINESTYLE") ours = r;
        hdxf_test::assert_true(ours != nullptr, name_of(v));
        std::vector<Group> a = subclass_of(*ours, "AcDbMlineStyle");
        std::vector<Group> b = subclass_of(*theirs, "AcDbMlineStyle");
        hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), name_of(v) + ": " + dump(*ours));
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            hdxf_test::assert_eq(a[i].first, b[i].first, name_of(v) + ": code " + std::to_string(i));
            if (a[i].first != 2) hdxf_test::assert_eq(a[i].second, b[i].second, name_of(v) + ": value of " + std::to_string(a[i].first));
        }
        // (the root dictionary and the group dictionary: head and form as AutoCAD's)
        const Rec* root = in_section(mine, "OBJECTS")[0];
        const Rec* facit_root = in_section(facit, "OBJECTS")[0];
        std::vector<int> root_codes;
        for (const auto& x : subclass_of(*root, "AcDbDictionary")) root_codes.push_back(x.first);
        hdxf_test::assert_eq(codes_text(root_codes), codes_text(codes({3, 350, 3, 350})), name_of(v) + ": root dictionary groups");
        hdxf_test::assert_false(facit_root->has(280) || facit_root->has(281), "AutoCAD's root has neither 280 nor 281");
    }
}

// A read file: a dictionary and a group changed, the rest of the object graph as it was
TEST(objects_r13_r14_read_file_dictionary_changed_unknown_branch_kept) {
    int run = 0;
    for (const char* path : {"libre/r13_v.dxf", "ezdxf/small_r14.dxf", "ezdxf/small_r13.dxf", "ezdxf/bin_dxf_r13.dxf",
                             "ezdxf/bin_dxf_r14.dxf", "local-r13r14/Test3_Flash_Tank_I_r14.dxf"}) {
        if (!exists(path)) continue;
        ++run;
        const std::string what = path;
        std::vector<uint8_t> original_bytes = read_bytes(path);
        std::vector<Rec> original = parse(original_bytes);
        for (DxfFormat format : both_formats) {
            const bool same_format = is_binary(original_bytes) == (format == DxfFormat::Binary);
            hdxf::Drawing dwg = load_file(path);
            if (dwg.entities().empty()) dwg.add_line({0, 0, 0}, {1, 1, 0});
            hdxf::Entity& member = dwg.entities()[0];
            dwg.add_group("NEWGROUP", {&member}, "added");
            std::vector<uint8_t> out = save(dwg, format, what);
            std::vector<Rec> recs = parse(out);

            // The group dictionary has the new entry; no 280 or 281 came with it
            const Rec* groups = nullptr;
            const Rec* root = in_section(recs, "OBJECTS")[0];
            for (size_t i = 0; i + 1 < root->g.size(); ++i) {
                if (root->g[i] == Group(3, "ACAD_GROUP")) groups = object_of(recs, root->g[i + 1].second);
            }
            hdxf_test::assert_true(groups != nullptr, what + ": ACAD_GROUP");
            if (groups) {
                bool has = false;
                for (const auto& [c, v] : groups->g) has = has || (c == 3 && v == "NEWGROUP");
                hdxf_test::assert_true(has, what + ": NEWGROUP in ACAD_GROUP: " + dump(*groups));
                const Rec* was = object_of(original, groups->value(5));
                if (was) {
                    hdxf_test::assert_eq(groups->has(280) ? 1 : 0, was->has(280) ? 1 : 0, what + ": 280 as read");
                    hdxf_test::assert_eq(groups->has(281) ? 1 : 0, was->has(281) ? 1 : 0, what + ": 281 as read");
                }
            }
            // Every other object (the unknown branches, the extension dictionaries, proxy objects, the
            // DICTIONARYVARs, XRECORDs) is written as it was read, group for group
            int same = 0;
            for (const auto* r : in_section(original, "OBJECTS")) {
                if (r->type() == "EOF") continue;
                const Rec* now = object_of(recs, r->value(5));
                hdxf_test::assert_true(now != nullptr, what + ": object " + r->value(5) + " (" + r->type() + ") is gone");
                if (!now) continue;
                bool touched = (groups && r->value(5) == groups->value(5)) || r->value(5) == root->value(5);
                if (!touched && same_format) {
                    hdxf_test::assert_eq(dump(*now), dump(*r), what + ": object " + r->value(5) + " as read");
                    ++same;
                }
            }
            hdxf_test::assert_true(same > 0 || !same_format, what);
        }
    }
    if (run == 0) hdxf_test::skip("no R13/R14 file is there");
}

// The extension dictionary of a block and its XRECORD follow the block's removal (B19, B97)
TEST(objects_r13_r14_xrecord_link_through_block_extension_dictionary) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            hdxf::Drawing dwg(v);
            hdxf::Block& block = dwg.add_block("LINKED");
            block.record.handle = "D01";
            block.record.xdictionary = "D02";
            hdxf::Object dict;
            dict.type = hdxf::ObjectType::Dictionary;
            dict.handle = "D02";
            dict.owner = "D01";
            hdxf::DictionaryData dd;
            dd.entries = {{"ACAD_LAYOUTSELFREF", "D03", false}};
            dict.data = dd;
            hdxf::Object xr;
            xr.type = hdxf::ObjectType::XRecord;
            xr.handle = "D03";
            xr.owner = "D02";
            xr.reactors = {"D02"};
            hdxf::XRecordData xd;
            xd.data = {{340, "D01"}};
            xr.data = xd;
            dwg.objects().push_back(dict);
            dwg.objects().push_back(xr);

            std::vector<Rec> recs = parse(save(dwg, format, what));
            const Rec* record = record_named(recs, "BLOCK_RECORD", "LINKED");
            hdxf_test::assert_true(record != nullptr, what);
            if (record) hdxf_test::assert_eq(record->value(360), "D02", what + ": the record's extension dictionary");
            const Rec* x = object_of(recs, "D03");
            hdxf_test::assert_true(x != nullptr, what + ": XRECORD 75/77-link");
            if (x) {
                hdxf_test::assert_eq(x->after("AcDbXrecord", 340), "D01", what + ": it points to the record");
                hdxf_test::assert_false(x->has(280), what);
            }
            hdxf_test::assert_true(object_of(recs, "D02") != nullptr, what);

            // The file read again, the block removed: its dictionary and the XRECORD in it are not written
            std::vector<uint8_t> first = save(dwg, format, what);
            hdxf::Drawing back;
            hdxf_test::assert_true(back.load_from_memory(first.data(), first.size()), what + ": " + back.last_error());
            auto& blocks = back.blocks();
            blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](const hdxf::Block& b) { return b.name == "LINKED"; }), blocks.end());
            std::vector<Rec> without = parse(save(back, format, what));
            hdxf_test::assert_true(object_of(without, "D02") == nullptr && object_of(without, "D03") == nullptr,
                                   what + ": the dictionary and the XRECORD of the removed block are gone");
            hdxf_test::assert_true(record_named(without, "BLOCK_RECORD", "LINKED") == nullptr, what + ": the record is gone");
        }
    }
}

// 7.10 --------------------------------------------------------------------------------------------------

std::vector<const Rec*> classes_of(const std::vector<Rec>& recs) {
    std::vector<const Rec*> out;
    for (const auto* r : in_section(recs, "CLASSES")) if (r->type() == "CLASS") out.push_back(r);
    return out;
}

TEST(classes_r13_r14_new_drawing_registers_what_it_writes) {
    for (DxfVersion v : older_versions) {
        for (DxfFormat format : both_formats) {
            const std::string what = name_of(v) + " " + name_of(format);
            // Nothing needs a class in a new drawing
            hdxf::Drawing empty(v);
            hdxf_test::assert_eq(static_cast<int>(classes_of(parse(save(empty, format, what))).size()), 0, what + ": none");

            // An XRECORD object, a DICTIONARYVAR and (R14) an LWPOLYLINE need theirs
            hdxf::Drawing dwg(v);
            if (v == DxfVersion::R14) dwg.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}});
            else dwg.add_line({0, 0, 0}, {1, 0, 0});
            for (const char* h : {"E01", "E02"}) {
                hdxf::Object o;
                o.handle = h;
                o.owner = "0";
                if (std::string(h) == "E01") {
                    o.type = hdxf::ObjectType::XRecord;
                    hdxf::XRecordData xd;
                    xd.data = {{1, "x"}};
                    o.data = xd;
                } else {
                    o.type = hdxf::ObjectType::DictionaryVar;
                    hdxf::DictionaryVarData dv;
                    dv.value = "1";
                    o.data = dv;
                }
                dwg.objects().push_back(o);
            }
            std::vector<Rec> recs = parse(save(dwg, format, what));
            std::vector<const Rec*> classes = classes_of(recs);
            std::vector<std::string> names;
            for (const auto* c : classes) names.push_back(c->value(1));
            std::vector<std::string> expected = v == DxfVersion::R14
                ? std::vector<std::string>{"LWPOLYLINE", "DICTIONARYVAR", "XRECORD"}
                : std::vector<std::string>{"DICTIONARYVAR", "XRECORD"};
            hdxf_test::assert_true(names == expected, what + ": " + std::to_string(names.size()) + " classes, first " +
                                                          (names.empty() ? std::string() : names[0]));
            for (const auto* c : classes) {
                // 1, 2, 3, 90, 280, 281: no 91 (2004's instance count)
                std::vector<int> cs;
                for (const auto& x : c->g) cs.push_back(x.first);
                hdxf_test::assert_eq(codes_text(cs), codes_text(codes({0, 1, 2, 3, 90, 280, 281})), what + ": " + dump(*c));
                // R13: the R13 proxy bit in 90; R14: AutoCAD's own values
                hdxf_test::assert_eq(c->value(90), v == DxfVersion::R13 ? "32768" : "0", what + ": 90 of " + c->value(1));
                hdxf_test::assert_eq(c->value(280), "0", what);
            }
            for (const auto* c : classes) {
                if (c->value(1) == "XRECORD") {
                    hdxf_test::assert_eq(c->value(2), "AcDbXrecord", what);
                    hdxf_test::assert_eq(c->value(3), "\"<Built In>\"", what + ": the application as AutoCAD writes it");
                    hdxf_test::assert_eq(c->value(281), "0", what);
                } else if (c->value(1) == "DICTIONARYVAR") {
                    hdxf_test::assert_eq(c->value(3), "\"AutoCAD\"", what);
                    hdxf_test::assert_eq(c->value(281), "0", what);
                } else if (c->value(1) == "LWPOLYLINE") {
                    hdxf_test::assert_eq(c->value(2), "AcDbPolyline", what);
                    hdxf_test::assert_eq(c->value(281), "1", what + ": an entity");
                }
            }
        }
    }
}

// AutoCAD's own R14 classes, value for value
TEST(classes_r13_r14_values_follow_autocads_r14_files) {
    if (!exists("local-r13r14/Test3_Flash_Tank_I_r14.dxf")) hdxf_test::skip("Test3_VII_r14.dxf is not there");
    std::vector<Rec> facit = parse(read_bytes("local-r13r14/Test3_Flash_Tank_I_r14.dxf"));
    hdxf::Drawing dwg(DxfVersion::R14);
    dwg.add_lwpolyline({{0, 0}, {1, 0}, {1, 1}});
    hdxf::Object o;
    o.type = hdxf::ObjectType::XRecord;
    o.handle = "E01";
    o.owner = "0";
    o.data = hdxf::XRecordData{};
    dwg.objects().push_back(o);
    std::vector<Rec> mine = parse(save(dwg, DxfFormat::Ascii, "R14"));
    int compared = 0;
    for (const auto* c : classes_of(mine)) {
        for (const auto* f : classes_of(facit)) {
            if (f->value(1) != c->value(1)) continue;
            ++compared;
            hdxf_test::assert_eq(dump(*c), dump(*f), "CLASS " + c->value(1));
        }
    }
    hdxf_test::assert_eq(compared, 2, "LWPOLYLINE and XRECORD are in AutoCAD's file");
}

// A read file's register is its own: all of it, in its order, whatever the objects are; the class indexes
// of the proxy objects stay right
TEST(classes_r13_r14_read_register_kept) {
    int run = 0;
    for (const char* path : {"ezdxf/bin_dxf_r13.dxf", "ezdxf/bin_dxf_r14.dxf", "ezdxf/small_r13.dxf", "ezdxf/small_r14.dxf",
                             "libre/r13_v.dxf", "local-r13r14/Test3_Flash_Tank_I_r14.dxf", "local-r13r14/Test3_FourPlusUnits_r14.dxf"}) {
        if (!exists(path)) continue;
        ++run;
        const std::string what = path;
        std::vector<uint8_t> original_bytes = read_bytes(path);
        std::vector<Rec> original = parse(original_bytes);
        std::vector<const Rec*> was = classes_of(original);
        for (DxfFormat format : both_formats) {
            const bool same_format = is_binary(original_bytes) == (format == DxfFormat::Binary);
            hdxf::Drawing dwg = load_file(path);
            dwg.add_layer("CHANGED", hdxf::Color::from_aci(3));
            if (!dwg.entities().empty()) dwg.entities()[0].props.layer = "CHANGED";
            std::vector<Rec> recs = parse(save(dwg, format, what));
            std::vector<const Rec*> now = classes_of(recs);
            hdxf_test::assert_eq(static_cast<int>(now.size()), static_cast<int>(was.size()), what + ": the register's size");
            for (size_t i = 0; i < now.size() && i < was.size(); ++i) {
                for (int code : {1, 2, 3, 90, 91, 280, 281}) {
                    hdxf_test::assert_eq(now[i]->value(code), was[i]->value(code), what + ": CLASS " + std::to_string(i) + " " + std::to_string(code));
                }
                hdxf_test::assert_eq(static_cast<int>(now[i]->g.size()), static_cast<int>(was[i]->g.size()), what + ": CLASS groups");
            }
            // The proxy objects' class indexes (90 and 91 after AcDbProxyObject) are as they were
            std::vector<std::string> a = dumps_of(original, "OBJECTS", "ACAD_PROXY_OBJECT");
            std::vector<std::string> b = dumps_of(recs, "OBJECTS", "ACAD_PROXY_OBJECT");
            hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), what + ": the proxy objects");
            if (same_format) hdxf_test::assert_true(a == b, what + ": the proxy objects as read");
        }
    }
    if (run == 0) hdxf_test::skip("no R13/R14 file is there");
}

// The classes of the types that steps 7.14 and 7.15 write are in the register as AutoCAD's R14 files
// have them (HATCH, the raster image objects, an untyped IDBUFFER)
TEST(classes_r13_r14_hatch_image_and_untyped_objects) {
    if (!exists("local-r13r14/Test3_Flash_Tank_I_r14.dxf")) hdxf_test::skip("Test3_Flash_Tank_I_r14.dxf is not there");
    std::vector<Rec> facit = parse(read_bytes("local-r13r14/Test3_Flash_Tank_I_r14.dxf"));
    for (DxfFormat format : both_formats) {
        hdxf::Drawing dwg(DxfVersion::R14);
        dwg.add_hatch({{0, 0}, {4, 0}, {4, 4}, {0, 4}});
        auto add = [&](hdxf::ObjectType type, const char* handle, hdxf::ObjectData data) {
            hdxf::Object o;
            o.type = type;
            o.handle = handle;
            o.owner = "0";
            o.data = std::move(data);
            dwg.objects().push_back(o);
        };
        add(hdxf::ObjectType::RasterVariables, "F01", hdxf::RasterVariablesData{});
        add(hdxf::ObjectType::ImageDef, "F02", hdxf::ImageDefData{});
        hdxf::UnknownObjectData idbuffer;
        idbuffer.object_type = "IDBUFFER";
        add(hdxf::ObjectType::Unknown, "F03", idbuffer);
        std::vector<Rec> recs = parse(save(dwg, format, name_of(format)));
        std::vector<std::string> names;
        for (const auto* c : classes_of(recs)) {
            names.push_back(c->value(1));
            for (const auto* f : classes_of(facit)) {
                if (f->value(1) == c->value(1)) hdxf_test::assert_eq(dump(*c), dump(*f), "CLASS " + c->value(1));
            }
        }
        hdxf_test::assert_true(names == std::vector<std::string>({"HATCH", "RASTERVARIABLES", "IMAGEDEF", "IDBUFFER"}),
                               name_of(format) + ": " + std::to_string(names.size()) + " classes");
    }
}

// A class is added for an object made since the file was read, not for the ones it had without one
TEST(classes_r13_r14_read_file_new_object_gets_its_class) {
    if (!exists("ezdxf/small_r14.dxf")) hdxf_test::skip("small_r14.dxf is not there");
    // small_r14 holds 34 XRECORDs and no CLASS: written as it is, a new XRECORD brings the class
    hdxf::Drawing dwg = load_file("ezdxf/small_r14.dxf");
    std::vector<uint8_t> bytes = read_bytes("ezdxf/small_r14.dxf");
    hdxf_test::assert_eq(static_cast<int>(classes_of(parse(save(dwg, DxfFormat::Ascii, "small_r14"))).size()), 0, "no class added to what the file had");
    hdxf::Object o;
    o.type = hdxf::ObjectType::XRecord;
    o.handle = "FFF1";
    o.owner = "0";
    hdxf::XRecordData xd;
    xd.data = {{1, "new"}};
    o.data = xd;
    dwg.objects().push_back(o);
    std::vector<Rec> recs = parse(save(dwg, DxfFormat::Ascii, "small_r14"));
    std::vector<const Rec*> classes = classes_of(recs);
    hdxf_test::assert_eq(static_cast<int>(classes.size()), 1, "XRECORD registered");
    if (classes.size() == 1) hdxf_test::assert_eq(classes[0]->value(1), "XRECORD", "the class");
}

} // namespace
