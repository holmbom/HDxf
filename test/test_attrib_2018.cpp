#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include "dxf_text.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Multiline attributes in DXF 2018 (phase 6, steps 6.3 and 6.5 — plan B75, B76): the ATTRIB and
// ATTDEF that carry an MTEXT in their own record (71, 72, 11, 101 Embedded Object), typed beside
// the attribute's own text, tag and prompt; read from the XRECORD ACAD_MLATT of the versions before.
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

// The groups of a record from its first embedded object (101) to its XDATA
Record embedded_of(const Record& r) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first >= 1000) break;
        if (g.first == 101) in = true;
        if (in) out.push_back(g);
    }
    return out;
}

// The groups before the first embedded object
Record parent_of(const Record& r) {
    Record out;
    for (const auto& g : r) {
        if (g.first == 101 || g.first >= 1000) break;
        out.push_back(g);
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

Record groups_in(const Record& r, const std::string& subclass) {
    Record out;
    bool in = false;
    for (const auto& g : r) {
        if (g.first == 100) { in = g.second == subclass; continue; }
        if (in) out.push_back(g);
    }
    return out;
}

double number(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

hdxf::Entity* find_insert(hdxf::Drawing& dwg, const std::string& handle) {
    for (auto& e : dwg.entities()) if (e.props.handle == handle) return &e;
    for (auto& b : dwg.blocks()) for (auto& e : b.entities) if (e.props.handle == handle) return &e;
    return nullptr;
}

hdxf::Entity* find_attdef(hdxf::Drawing& dwg, const std::string& handle) {
    for (auto& b : dwg.blocks()) {
        for (auto& e : b.entities) if (e.type == hdxf::EntityType::Attdef && e.props.handle == handle) return &e;
    }
    return nullptr;
}

hdxf::InsertAttribute* find_attrib(hdxf::Drawing& dwg, const std::string& handle) {
    auto in = [&](std::vector<hdxf::Entity>& list) -> hdxf::InsertAttribute* {
        for (auto& e : list) {
            if (e.type != hdxf::EntityType::Insert) continue;
            for (auto& a : e.as_insert().attributes) if (a.props.handle == handle) return &a;
        }
        return nullptr;
    };
    if (auto* a = in(dwg.entities())) return a;
    for (auto& b : dwg.blocks()) if (auto* a = in(b.entities)) return a;
    return nullptr;
}

std::string group_text(int code, const std::string& v) {
    std::string c = std::to_string(code);
    return std::string(c.size() < 3 ? 3 - c.size() : 0, ' ') + c + "\n" + v + "\n";
}

const char* const kAscii = "acadsharp/sample_AC1032_ascii.dxf";
const char* const kBinary = "acadsharp/sample_AC1032_binary.dxf";
const char* const kAscii2013 = "acadsharp/sample_AC1027_ascii.dxf";

} // namespace

// AutoCAD's multiline attribute definition (796) and attribute (79D): 71, 72, 11 and the
// embedded MTEXT typed (B75), beside the attribute's own text, tag, prompt and justification,
// which are not overwritten by the embedded object's 10, 40, 1, 71, 72
TEST(attrib_2018_multiline_is_typed) {
    for (const char* file : {kAscii, kBinary}) {
        if (!has_files({file})) return;
        std::vector<uint8_t> in = read_bytes(external(file));
        std::vector<Record> recs = records_of(in);
        hdxf::Drawing dwg = load(in);

        hdxf::Entity* def = find_attdef(dwg, "796");
        hdxf::InsertAttribute* att = find_attrib(dwg, "79D");
        hdxf_test::assert_true(def && att, std::string(file) + ": 796 and 79D");
        if (!def || !att) continue;
        struct Case { const hdxf::MultilineAttribute* ml; Record r; int type; bool def; };
        hdxf_test::assert_true(def->as_attdef().multiline && att->data.multiline, "multiline");
        if (!def->as_attdef().multiline || !att->data.multiline) continue;
        for (int which = 0; which < 2; ++which) {
            const hdxf::MultilineAttribute& ml = which == 0 ? *def->as_attdef().multiline : *att->data.multiline;
            Record r = record(recs, which == 0 ? "796" : "79D");
            Record emb = embedded_of(r);
            Record own = parent_of(r);
            std::string what = std::string(file) + (which == 0 ? " 796" : " 79D");
            hdxf_test::assert_eq(ml.type, which == 0 ? 4 : 2, what + ": 71");
            hdxf_test::assert_eq(static_cast<int>(ml.secondary.size()), 0, what + ": 72");
            // 11 after the lock flag, the second of AcDbText/AcDbAttribute
            Record attr_groups;
            bool in_attr = false;
            for (const auto& g : own) {
                if (g.first == 100) in_attr = g.second != "AcDbText";
                if (in_attr) attr_groups.push_back(g);
            }
            hdxf_test::assert_eq(ml.alignment_point.x, number(value(attr_groups, 11)), what + ": 11");
            hdxf_test::assert_eq(ml.alignment_point.y, number(value(attr_groups, 21)), what + ": 21");
            hdxf_test::assert_eq(ml.alignment_point.z, number(value(attr_groups, 31)), what + ": 31");
            // The text
            hdxf_test::assert_eq(ml.text.insertion_point.x, number(value(emb, 10)), what + ": text insertion x");
            hdxf_test::assert_eq(ml.text.insertion_point.y, number(value(emb, 20)), what + ": text insertion y");
            hdxf_test::assert_eq(ml.text.height, number(value(emb, 40)), what + ": text height");
            hdxf_test::assert_eq(ml.text.reference_width, number(value(emb, 41)), what + ": 41");
            hdxf_test::assert_eq(ml.text.defined_height, number(value(emb, 46)), what + ": 46");
            hdxf_test::assert_eq(ml.text.attachment, std::atoi(value(emb, 71).c_str()), what + ": attachment");
            hdxf_test::assert_eq(ml.text.drawing_direction, std::atoi(value(emb, 72).c_str()), what + ": direction");
            hdxf_test::assert_eq(ml.text.content, value(emb, 1), what + ": text");
            hdxf_test::assert_eq(ml.text.line_spacing_style, std::atoi(value(emb, 73).c_str()), what + ": 73");
            hdxf_test::assert_eq(ml.text.line_spacing_factor, number(value(emb, 44)), what + ": 44");
            hdxf_test::assert_false(ml.text.columns.has_value(), what + ": no columns in an attribute's text");
        }
        // The attribute's own: the TEXT part's 1 empty, the tag, the prompt, the lock, the justification
        const hdxf::AttdefData& d = def->as_attdef();
        hdxf_test::assert_eq(d.default_value, "", "796: 1 empty");
        hdxf_test::assert_eq(d.tag, "MULTI_LINE_ATT", "796: tag");
        hdxf_test::assert_eq(d.prompt, "this is an example of prompt", "796: prompt");
        hdxf_test::assert_eq(d.v_justify, 3, "796: 74");
        hdxf_test::assert_true(d.lock_position, "796: lock");
        hdxf_test::assert_eq(d.height, number(value(parent_of(record(recs, "796")), 40)), "796: height");
        hdxf_test::assert_eq(d.start_point.x, number(value(record(recs, "796"), 10)), "796: start point");
        const hdxf::AttribData& a = att->data;
        hdxf_test::assert_eq(a.value, "", "79D: 1 empty");
        hdxf_test::assert_eq(a.tag, "MULTI_LINE_ATT", "79D: tag");
        hdxf_test::assert_eq(a.v_justify, 3, "79D: 74");
        hdxf_test::assert_eq(a.h_justify, 0, "79D: 72 is not the justification");
        hdxf_test::assert_eq(a.generation_flags, 0, "79D: 71 is not the generation flags");
        hdxf_test::assert_eq(a.alignment_point.y, number(value(record(recs, "79D"), 21)), "79D: the alignment point of AcDbText");
        // The text of 79D is in the embedded object
        hdxf_test::assert_eq(att->data.multiline->text.content, value(embedded_of(record(recs, "79D")), 1), "79D: text");
        hdxf_test::assert_true(att->data.multiline->text.content.rfind("my multi line text for the att", 0) == 0, "79D: text");
    }
}

// The attributes without multiline data have none (B75): AutoCAD's other ATTRIB and ATTDEF
TEST(attrib_2018_plain_attributes_have_no_multiline_data) {
    if (!has_files({kAscii})) return;
    hdxf::Drawing dwg = load(read_bytes(external(kAscii)));
    int plain = 0, multi = 0;
    auto count = [&](std::vector<hdxf::Entity>& list) {
        for (auto& e : list) {
            if (e.type == hdxf::EntityType::Attdef) (e.as_attdef().multiline ? multi : plain)++;
            if (e.type != hdxf::EntityType::Insert) continue;
            for (auto& a : e.as_insert().attributes) (a.data.multiline ? multi : plain)++;
        }
    };
    count(dwg.entities());
    for (auto& b : dwg.blocks()) count(b.entities);
    hdxf_test::assert_eq(multi, 2, "two multiline");
    hdxf_test::assert_true(plain >= 6, "single-line ones: " + std::to_string(plain));
}

// Up to 2013 the same data is the XRECORD ACAD_MLATT of the attribute's extension dictionary
// (B76): read to the same model, the hidden attribute tag of the XRECORD kept beside the ATTRIB's
TEST(attrib_2018_multiline_from_the_xrecord_before_2018) {
    if (!has_files({kAscii2013})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii2013));
    std::vector<Record> recs = records_of(in);
    hdxf::Drawing dwg = load(in);
    hdxf::Entity* def = find_attdef(dwg, "796");
    hdxf::InsertAttribute* att = find_attrib(dwg, "79D");
    hdxf_test::assert_true(def && att && def->as_attdef().multiline && att->data.multiline, "multiline from the XRECORD");
    if (!def || !att || !def->as_attdef().multiline || !att->data.multiline) return;
    struct Pair { const hdxf::MultilineAttribute* ml; const char* dictionary; int type; const char* text; };
    Record x796 = record(recs, "12E8"), x79d = record(recs, "12EB");
    for (const Pair& p : {Pair{&*def->as_attdef().multiline, "12E8", 4, ""},
                          Pair{&*att->data.multiline, "12EB", 2, "my multi line text for the attrrib"}}) {
        Record x = record(recs, p.dictionary);
        hdxf_test::assert_eq(p.ml->type, p.type, "70");
        hdxf_test::assert_false(p.ml->really_locked, "the second 70");
        hdxf_test::assert_eq(static_cast<int>(p.ml->secondary.size()), 0, "no secondary attributes");
        hdxf_test::assert_eq(p.ml->annotation_scale, 1.0, "40");
        hdxf_test::assert_eq(p.ml->tag, "", "the XRECORD's tag is the attribute's own");
        hdxf_test::assert_eq(p.ml->text.content, p.text, "text");
        hdxf_test::assert_eq(p.ml->text.height, number(value(x, 40, 1)), "text height");
        hdxf_test::assert_eq(p.ml->text.insertion_point.x, number(value(x, 10)), "text insertion");
        hdxf_test::assert_eq(p.ml->text.attachment, 1, "attachment");
        hdxf_test::assert_eq(p.ml->text.drawing_direction, 5, "direction");
    }
    // The ATTRIB's tag is the XRECORD's (its own 2 has the first line's "_001"); its own text is the first line
    hdxf_test::assert_eq(att->data.tag, "MULTI_LINE_ATT", "the attribute's tag");
    hdxf_test::assert_eq(att->data.value, value(record(recs, "79D"), 1), "the ATTRIB's text in 2013");
    hdxf_test::assert_true(att->data.value.rfind("my multi line text for the att", 0) == 0, "the ATTRIB's text in 2013");
    (void)x796;
    (void)x79d;
    // Unchanged: byte for byte
    hdxf_test::assert_true(save(dwg) == in, "unchanged: bytes");
}

// Unchanged files go through byte for byte, ASCII and binary (B72)
TEST(attrib_2018_unchanged_roundtrip) {
    for (const char* file : {kAscii, kBinary}) {
        if (!has_files({file})) return;
        std::vector<uint8_t> in = read_bytes(external(file));
        hdxf::Drawing dwg = load(in);
        bool binary = std::string(file) == kBinary;
        std::vector<uint8_t> out = save(dwg, binary ? hdxf::DxfFormat::Binary : hdxf::DxfFormat::Ascii);
        hdxf_test::assert_true(out == in, std::string(file) + ": bytes");
    }
}

// A changed multiline text is written anew in the same record: the embedded object from the typed
// values, the attribute's own groups as they were, no other record touched
TEST(attrib_2018_changing_the_text_changes_the_embedded_object_only) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> in = read_bytes(external(kAscii));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    hdxf::Entity* def = find_attdef(dwg, "796");
    hdxf::InsertAttribute* att = find_attrib(dwg, "79D");
    hdxf_test::assert_true(def && att && def->as_attdef().multiline && att->data.multiline, "multiline");
    if (!def || !att) return;
    att->data.multiline->text.content = "Rad ett\\PRad två ÅÄÖ";
    att->data.multiline->text.height = 2.25;
    att->data.multiline->alignment_point = {1.5, 2.5, 0.0};
    def->as_attdef().multiline->text.attachment = 5;
    def->as_attdef().multiline->secondary = {"ABC", "DEF"};
    std::vector<Record> after = records_of(save(dwg));

    Record a = record(after, "79D"), b = record(before, "79D");
    Record emb = embedded_of(a);
    hdxf_test::assert_eq(value(emb, 1), "Rad ett\\PRad två ÅÄÖ", "79D: the new text");
    hdxf_test::assert_eq(number(value(emb, 40)), 2.25, "79D: the new height");
    // The attribute's own TEXT follows its text (B75): the height; the alignment point of
    // AcDbAttribute is the one set; the justification, the tag and the empty 1 as they were
    Record pa = parent_of(a), pb = parent_of(b);
    hdxf_test::assert_eq(static_cast<int>(pa.size()), static_cast<int>(pb.size()), "79D: the same groups");
    hdxf_test::assert_eq(number(value(groups_in(pa, "AcDbText"), 40)), 2.25, "79D: the TEXT's height follows the text's");
    hdxf_test::assert_eq(value(groups_in(pa, "AcDbText"), 1), "", "79D: 1 stays empty");
    hdxf_test::assert_eq(value(groups_in(pa, "AcDbAttribute"), 2), "MULTI_LINE_ATT", "79D: the tag");
    hdxf_test::assert_eq(value(groups_in(pa, "AcDbAttribute"), 74), "3", "79D: top");
    hdxf_test::assert_eq(number(value(groups_in(pa, "AcDbAttribute"), 11)), 1.5, "79D: 11 of AcDbAttribute");
    hdxf_test::assert_eq(number(value(groups_in(pa, "AcDbAttribute"), 21)), 2.5, "79D: 21 of AcDbAttribute");
    hdxf_test::assert_eq(number(value(groups_in(pa, "AcDbText"), 11)), number(value(emb, 10)), "79D: the TEXT is justified at the text's insertion point");
    // The definition: the attachment and the secondary attributes
    Record d = record(after, "796");
    Record dp = parent_of(d);
    int count = 0;
    std::vector<std::string> handles;
    for (const auto& [code, v] : dp) {
        if (code == 340) handles.push_back(v);
        if (code == 72) count = std::atoi(v.c_str());
    }
    (void)count;
    hdxf_test::assert_true(handles == std::vector<std::string>({"ABC", "DEF"}), "796: the 340");
    hdxf_test::assert_eq(value(embedded_of(d), 71), "5", "796: the text's attachment");

    int same = 0, total = 0;
    for (const auto& r : before) {
        std::string h;
        for (const auto& [code, v] : r) { if (code == 5) { h = v; break; } if (code == 100) break; }
        if (h.empty() || r[0].second == "SECTION" || h == "79D" || h == "796") continue;
        ++total;
        if (record(after, h) == r) ++same;
    }
    hdxf_test::assert_eq(same, total, "the other records");
}

// A new multiline attribute definition in a block and two INSERTs with different multiline values
// (B75): 2018's own form — no MTEXT entity and no handle for the text — in ASCII and binary, read back
TEST(attrib_2018_new_definition_and_two_inserts) {
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
        hdxf::Block& block = dwg.add_block("TITEL");
        hdxf::AttdefData def;
        def.tag = "NOTE";
        def.prompt = "Anteckning";
        def.start_point = {0, 0, 0};
        def.height = 2.0;
        hdxf::MultilineAttribute ml;
        ml.type = 4;
        ml.alignment_point = {0, 0, 0};
        ml.text.insertion_point = {0, 0, 0};
        ml.text.height = 2.0;
        ml.text.reference_width = 30.0;
        ml.text.attachment = 1;
        ml.text.drawing_direction = 5;
        ml.text.content = "Standardtext";
        def.multiline = ml;
        hdxf::Entity attdef;
        attdef.type = hdxf::EntityType::Attdef;
        attdef.data = def;
        block.entities.push_back(attdef);

        std::vector<std::string> texts = {"Första raden\\PAndra raden", "日本語のテキスト 😀"};
        for (size_t i = 0; i < texts.size(); ++i) {
            hdxf::Entity& ins = dwg.add_insert("TITEL", {static_cast<double>(i) * 100.0, 0, 0});
            hdxf::InsertAttribute attr;
            attr.data.tag = "NOTE";
            attr.data.start_point = {static_cast<double>(i) * 100.0, 0, 0};
            attr.data.height = 2.0;
            hdxf::MultilineAttribute m;
            m.type = 2;
            m.alignment_point = attr.data.start_point;
            m.text.insertion_point = attr.data.start_point;
            m.text.height = 2.0;
            m.text.reference_width = 30.0;
            m.text.drawing_direction = 5;
            m.text.content = texts[i];
            attr.data.multiline = m;
            ins.as_insert().attributes.push_back(attr);
        }
        std::vector<uint8_t> out = save(dwg, format);
        std::vector<Record> recs = records_of(out);
        int mtexts = 0, embedded = 0;
        for (const auto& r : recs) {
            if (r[0].second == "MTEXT") ++mtexts;
            if (r[0].second == "ATTRIB" || r[0].second == "ATTDEF") {
                ++embedded;
                Record own = parent_of(r), emb = embedded_of(r);
                hdxf_test::assert_false(emb.empty(), "an embedded object");
                hdxf_test::assert_eq(value(own, 71, 0), r[0].second == "ATTDEF" ? "4" : "2", "71");
                hdxf_test::assert_eq(value(own, 72, 0), "0", "72");
                hdxf_test::assert_eq(number(value(emb, 40)), 2.0, "the text's height");
                for (const auto& g : emb) hdxf_test::assert_true(g.first != 100 && g.first != 5, "no subclass and no handle in the text");
            }
        }
        hdxf_test::assert_eq(mtexts, 0, "no MTEXT entity");
        hdxf_test::assert_eq(embedded, 3, "one definition, two attributes");

        hdxf::Drawing back = load(out);
        hdxf::Block* b = back.find_block("TITEL");
        hdxf_test::assert_true(b != nullptr, "the block");
        if (!b) continue;
        bool found_def = false;
        for (auto& e : b->entities) {
            if (e.type != hdxf::EntityType::Attdef) continue;
            found_def = true;
            hdxf_test::assert_true(e.as_attdef().multiline.has_value(), "definition read back");
            hdxf_test::assert_eq(e.as_attdef().multiline->text.content, "Standardtext", "definition text");
            hdxf_test::assert_eq(e.as_attdef().multiline->type, 4, "definition type");
        }
        hdxf_test::assert_true(found_def, "the ATTDEF");
        std::vector<std::string> got;
        for (auto& e : back.entities()) {
            if (e.type != hdxf::EntityType::Insert) continue;
            for (auto& a : e.as_insert().attributes) {
                hdxf_test::assert_true(a.data.multiline.has_value(), "attribute read back");
                if (a.data.multiline) got.push_back(a.data.multiline->text.content);
            }
        }
        hdxf_test::assert_true(got == texts, "the two texts");
        // Written back: the same records
        hdxf_test::assert_true(records_of(save(back, format)) == recs, "read back and written");
    }
}

// Another embedded object after the text's, and XDATA after both: unchanged they stay as they were (B72)
TEST(attrib_2018_unknown_embedded_object_stays) {
    if (!has_files({kAscii})) return;
    std::vector<uint8_t> bytes = read_bytes(external(kAscii));
    std::string text(bytes.begin(), bytes.end());
    size_t at = text.find("\n  5\n79D\n");
    size_t end = text.find("\n  0\n", at);
    std::string extra = group_text(101, "Embedded Object") + group_text(100, "AcDbHdxfTest") + group_text(10, "1.5") +
                        group_text(1, "unknown") + group_text(71, "9") + group_text(1001, "MY_APP") +
                        group_text(1000, "after") + group_text(1070, "3");
    text.insert(end + 1, extra);
    std::vector<uint8_t> in(text.begin(), text.end());
    hdxf::Drawing dwg = load(in);
    hdxf::InsertAttribute* att = find_attrib(dwg, "79D");
    hdxf_test::assert_true(att && att->data.multiline, "79D");
    if (!att || !att->data.multiline) return;
    // (the second embedded object is the text's columns in this model)
    hdxf_test::assert_true(att->data.multiline->text.content.rfind("my multi line text for the att", 0) == 0, "the text");
    hdxf_test::assert_eq(att->data.tag, "MULTI_LINE_ATT", "the tag");
    hdxf_test::assert_eq(att->data.alignment_point.y, 28.7508142234822, "the alignment point");
    hdxf_test::assert_true(save(dwg) == in, "unchanged: bytes");
    att->data.multiline->text.height = 3.0;
    Record r = record(records_of(save(dwg)), "79D");
    hdxf_test::assert_eq(r.back().second, "3", "the XDATA last");
    hdxf_test::assert_eq(number(value(embedded_of(r), 40)), 3.0, "the new height");
}

// Copies are independent (B84)
TEST(attrib_2018_copies_are_independent) {
    hdxf::AttribData a;
    hdxf::MultilineAttribute ml;
    ml.text.content = "one";
    a.multiline = ml;
    hdxf::AttribData b = a;
    b.multiline->text.content = "two";
    b.multiline->secondary.push_back("1A");
    hdxf_test::assert_eq(a.multiline->text.content, "one", "the original's text");
    hdxf_test::assert_true(a.multiline->secondary.empty(), "the original's secondary");
    hdxf::InsertAttribute ia;
    ia.data = a;
    hdxf::InsertAttribute copy = ia;
    copy.data.multiline->text.content = "three";
    hdxf_test::assert_eq(ia.data.multiline->text.content, "one", "an attribute copy");
    hdxf::AttdefData d;
    d.multiline = ml;
    hdxf::AttdefData e = std::move(d);
    hdxf_test::assert_eq(e.multiline->text.content, "one", "moved");
    hdxf_test::assert_true(sizeof(hdxf::Entity) <= 736, "sizeof(Entity) = " + std::to_string(sizeof(hdxf::Entity)));
}

// A long text (parts of 250 in 3, the last in 1) in the embedded object, and the definition's prompt
// (also a 3, in the attribute's own part): neither is taken for the other (B75)
TEST(attrib_2018_long_text_and_the_prompt) {
    std::string long_text;
    for (int i = 0; i < 60; ++i) long_text += "Ord " + std::to_string(i) + " å ä ö \\P";
    for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
        hdxf::Block& block = dwg.add_block("B");
        hdxf::AttdefData def;
        def.tag = "T";
        def.prompt = "Ange texten (prompt, inte textdel)";
        hdxf::MultilineAttribute ml;
        ml.type = 4;
        ml.text.content = long_text;
        def.multiline = ml;
        hdxf::Entity attdef;
        attdef.type = hdxf::EntityType::Attdef;
        attdef.data = def;
        block.entities.push_back(attdef);
        std::vector<uint8_t> out = save(dwg, format);
        std::vector<Record> recs = records_of(out);
        for (const auto& r : recs) {
            if (r[0].second != "ATTDEF") continue;
            Record own = parent_of(r), emb = embedded_of(r);
            int threes_own = 0, threes_text = 0;
            for (const auto& g : own) threes_own += g.first == 3;
            for (const auto& g : emb) threes_text += g.first == 3;
            hdxf_test::assert_eq(threes_own, 1, "the prompt is the one 3 of the attribute's own part");
            hdxf_test::assert_true(threes_text >= 2, "the text in parts: " + std::to_string(threes_text));
        }
        hdxf::Drawing back = load(out);
        hdxf::Block* b = back.find_block("B");
        hdxf_test::assert_true(b && !b->entities.empty(), "the block");
        if (!b || b->entities.empty()) continue;
        const hdxf::AttdefData& got = b->entities[0].as_attdef();
        hdxf_test::assert_eq(got.prompt, "Ange texten (prompt, inte textdel)", "the prompt");
        hdxf_test::assert_true(got.multiline.has_value() && got.multiline->text.content == long_text, "the long text");
    }
}

// ---------------------------------------------------------------------------
// Before 2018 (B76): ACAD_MLATT and ACAD_MATT_HEIGHT in the extension dictionary, the first line
// as TEXT, the other lines as secondary attributes
// ---------------------------------------------------------------------------

namespace {

// Arial's directory as the font path, for the first line's pen
struct ArialPath {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    ArialPath() {
        const char* windir = std::getenv("WINDIR");
        std::filesystem::path arial = windir ? hdxf::path_from_utf8(windir) / "Fonts" / "arial.ttf" : std::filesystem::path("arial.ttf");
        if (!std::filesystem::exists(arial)) hdxf_test::skip("arial.ttf missing");
        hdxf::set_font_paths({arial.parent_path()});
    }
    ~ArialPath() { hdxf::set_font_paths(saved); }
};

hdxf::Entity* find_in_block(hdxf::Drawing& dwg, const std::string& block, hdxf::EntityType type) {
    hdxf::Block* b = dwg.find_block(block);
    if (!b) return nullptr;
    for (auto& e : b->entities) if (e.type == type) return &e;
    return nullptr;
}

// The XRECORD with the dictionary entry `name` of the extension dictionary `xdictionary` in a file's records
Record xrecord_of(const std::vector<Record>& recs, const std::string& xdictionary, const std::string& name) {
    Record dict = record(recs, xdictionary);
    for (size_t i = 0; i + 1 < dict.size(); ++i) {
        if (dict[i].first == 3 && dict[i].second == name) return record(recs, dict[i + 1].second);
    }
    return {};
}

} // namespace

// AutoCAD's own records from the same attributes made from scratch: 796's and 79D's XRECORDs and
// their ACAD_MATT_HEIGHT, the extension dictionaries, and 79D's TEXT — its pen from the layout exactly
TEST(attrib_2018_xrecords_made_as_autocad_makes_them) {
    if (!has_files({kAscii2013})) return;
    ArialPath arial;
    std::vector<uint8_t> in = read_bytes(external(kAscii2013));
    std::vector<Record> before = records_of(in);
    hdxf::Drawing dwg = load(in);
    // The objects taken away, the attributes written anew
    hdxf::Entity* def = find_attdef(dwg, "796");
    hdxf::InsertAttribute* att = find_attrib(dwg, "79D");
    hdxf_test::assert_true(def && att, "796 and 79D");
    if (!def || !att) return;
    std::string d_def = def->props.xdictionary, d_att = att->props.xdictionary;
    std::set<std::string> gone = {d_def, d_att};
    for (const std::string& d : {d_def, d_att}) {
        hdxf::Object* dict = dwg.find_object(d);
        for (const auto& e : dict->as_dictionary().entries) gone.insert(e.handle);
    }
    auto& objects = dwg.objects();
    objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const hdxf::Object& o) { return gone.count(o.handle) > 0; }), objects.end());
    def->props.xdictionary.clear();
    att->props.xdictionary.clear();
    def->raw = hdxf::RawRecord();
    att->raw = hdxf::RawRecord();
    std::vector<Record> after = records_of(save(dwg));

    // The extension dictionaries: made, owned by the attribute, hard owner, ACAD_MLATT then ACAD_XREC_ROUNDTRIP
    for (const char* h : {"796", "79D"}) {
        Record ours = record(after, h), theirs = record(before, h);
        std::string xd = value(ours, 360), xd_theirs = value(theirs, 360);
        hdxf_test::assert_false(xd.empty(), std::string(h) + ": an extension dictionary");
        Record dict = record(after, xd), dict_theirs = record(before, xd_theirs);
        hdxf_test::assert_eq(value(dict, 330), h, std::string(h) + ": its dictionary's owner");
        std::vector<std::string> names, names_theirs;
        for (const auto& g : dict) if (g.first == 3) names.push_back(g.second);
        for (const auto& g : dict_theirs) if (g.first == 3) names_theirs.push_back(g.second);
        hdxf_test::assert_true(names == names_theirs, std::string(h) + ": the entries");
        hdxf_test::assert_eq(value(dict, 280), value(dict_theirs, 280), "280");
        hdxf_test::assert_eq(value(dict, 281), value(dict_theirs, 281), "281");
        for (const char* name : {"ACAD_MLATT", "ACAD_XREC_ROUNDTRIP"}) {
            Record x = xrecord_of(after, xd, name), x_theirs = xrecord_of(before, xd_theirs, name);
            hdxf_test::assert_false(x.empty(), std::string(h) + " " + name);
            // The groups after the owner: the data, as AutoCAD's (numbers to their last digits)
            Record a = groups_in(x, "AcDbXrecord"), b = groups_in(x_theirs, "AcDbXrecord");
            hdxf_test::assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), std::string(h) + " " + name + ": groups");
            for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
                hdxf_test::assert_eq(a[i].first, b[i].first, std::string(h) + " " + name + ": code");
                if (hdxf::internal::group_value_type(a[i].first) == hdxf::internal::GroupValueType::Double) {
                    hdxf_test::assert_true(std::fabs(number(a[i].second) - number(b[i].second)) < 1e-9,
                                           std::string(h) + " " + name + ": " + a[i].second + " / " + b[i].second);
                } else {
                    hdxf_test::assert_eq(a[i].second, b[i].second, std::string(h) + " " + name);
                }
            }
            hdxf_test::assert_eq(value(x, 330), xd, "the XRECORD's owner");
        }
    }
    // 79D's own TEXT: its first line (Arial's pen, to the digit), the tag with _001; 796's: justified, the tag as it is
    Record a = record(after, "79D"), b = record(before, "79D");
    hdxf_test::assert_true(std::fabs(number(value(a, 10)) - number(value(b, 10))) < 1e-9, "79D: the pen's x");
    hdxf_test::assert_true(std::fabs(number(value(a, 20)) - number(value(b, 20))) < 1e-9, "79D: the pen's y");
    hdxf_test::assert_eq(value(a, 1), value(b, 1), "79D: the first line");
    hdxf_test::assert_eq(value(a, 2), "MULTI_LINE_ATT_001", "79D: the tag");
    hdxf_test::assert_true(groups_in(a, "AcDbAttribute") == groups_in(b, "AcDbAttribute"), "79D: AcDbAttribute as AutoCAD's");
    Record c = record(after, "796"), d = record(before, "796");
    hdxf_test::assert_true(groups_in(c, "AcDbText") == groups_in(d, "AcDbText"), "796: AcDbText as AutoCAD's");
    hdxf_test::assert_true(groups_in(c, "AcDbAttributeDefinition") == groups_in(d, "AcDbAttributeDefinition"), "796: AcDbAttributeDefinition as AutoCAD's");
}

// A new drawing: the XRECORDs, the first line and the others as ATTRIBs; read back to the same
// attributes; 2018 has none of it; every version is switched to and from again and again
TEST(attrib_2018_multiline_in_every_version) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::Block& block = dwg.add_block("B");
    hdxf::AttdefData def;
    def.tag = "NOTE";
    def.prompt = "Note";
    def.height = 2.0;
    hdxf::MultilineAttribute md;
    md.type = 4;
    md.text.insertion_point = {5, 6, 0};
    md.text.height = 2.0;
    md.text.content = "Default";
    md.alignment_point = {5, 6, 0};
    def.multiline = md;
    hdxf::Entity attdef;
    attdef.type = hdxf::EntityType::Attdef;
    attdef.data = def;
    block.entities.push_back(attdef);
    hdxf::Entity& ins = dwg.add_insert("B", {100, 200, 0});
    hdxf::InsertAttribute attr;
    attr.data.tag = "NOTE";
    attr.data.height = 2.0;
    hdxf::MultilineAttribute m;
    m.type = 2;
    m.text.insertion_point = {105, 206, 0};
    m.text.height = 2.0;
    m.text.content = "Första raden\\PAndra raden\\PTredje raden";
    m.alignment_point = {105, 206, 0};
    attr.data.multiline = m;
    ins.as_insert().attributes.push_back(attr);

    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2010,
                               hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2007, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2000,
                               hdxf::DxfVersion::R2018}) {
        for (hdxf::DxfFormat format : {hdxf::DxfFormat::Ascii, hdxf::DxfFormat::Binary}) {
            dwg.set_version(v);
            std::vector<uint8_t> out = save(dwg, format);
            std::string what = std::string(hdxf::acadver_string(v)) + (format == hdxf::DxfFormat::Binary ? " binary" : " ASCII");
            if (format == hdxf::DxfFormat::Ascii) dxf_structure::check_structure(out, what);
            std::vector<Record> recs = records_of(out);
            bool r2018 = v == hdxf::DxfVersion::R2018;
            int attribs = 0, xrecords = 0;
            for (const auto& r : recs) {
                if (r[0].second == "ATTRIB") ++attribs;
                if (r[0].second == "XRECORD") {
                    for (const auto& g : r) if (g.second == "ACAD_MATT_HEIGHT") ++xrecords;
                }
            }
            hdxf_test::assert_eq(attribs, r2018 ? 1 : 3, what + ": the ATTRIBs (a line each before 2018)");
            hdxf_test::assert_eq(xrecords, r2018 ? 0 : 2, what + ": ACAD_MATT_HEIGHT");
            // Read back: the same attributes
            hdxf::Drawing back = load(out);
            hdxf::Entity* d = find_in_block(back, "B", hdxf::EntityType::Attdef);
            hdxf_test::assert_true(d && d->as_attdef().multiline && d->as_attdef().multiline->has_text, what + ": the definition");
            if (d && d->as_attdef().multiline) {
                hdxf_test::assert_eq(d->as_attdef().tag, "NOTE", what + ": the definition's tag");
                hdxf_test::assert_eq(d->as_attdef().multiline->text.content, "Default", what + ": the definition's text");
                hdxf_test::assert_eq(d->as_attdef().multiline->type, 4, what + ": the definition's type");
            }
            int found = 0;
            for (auto& e : back.entities()) {
                if (e.type != hdxf::EntityType::Insert) continue;
                for (auto& a : e.as_insert().attributes) {
                    if (!a.data.secondary_of.empty()) continue;
                    ++found;
                    hdxf_test::assert_true(a.data.multiline && a.data.multiline->has_text, what + ": the attribute");
                    if (!a.data.multiline) continue;
                    hdxf_test::assert_eq(a.data.tag, "NOTE", what + ": the attribute's tag");
                    hdxf_test::assert_eq(a.data.multiline->text.content, "Första raden\\PAndra raden\\PTredje raden", what + ": the text");
                    hdxf_test::assert_eq(static_cast<int>(a.data.multiline->secondary.size()), r2018 ? 0 : 2, what + ": the other lines");
                }
            }
            hdxf_test::assert_eq(found, 1, what + ": one attribute");
            // Written back as it is
            hdxf_test::assert_true(records_of(save(back, format)) == recs, what + ": written back unchanged");
        }
    }
}

// The other lines of the text are ATTRIBs of their own before 2018: the same as AutoCAD's core
// gives for the same attribute (ODA writing AutoCAD's DWG as 2013 — att3 of the phase 6 oracle
// run, Arial, text 30 wide in three lines: "Centered", "text that is a bit longer")
TEST(attrib_2018_other_lines_are_attribs_before_2018) {
    ArialPath arial;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::TextStyle& s = dwg.add_text_style("Standard", "arial.ttf");
    s.font_family = "Arial";
    s.font_flags = 34;
    hdxf::Block& block = dwg.add_block("B");
    (void)block;
    hdxf::Entity& ins = dwg.add_insert("B", {100, 200, 0});
    hdxf::InsertAttribute attr;
    attr.data.tag = "NOTE";
    attr.data.height = 2.0;
    hdxf::MultilineAttribute m;
    m.type = 2;
    m.text.insertion_point = {105, 206, 0};
    m.text.height = 2.0;
    m.text.reference_width = 30.0;
    m.text.attachment = 5;
    m.text.drawing_direction = 5;
    m.text.content = "Centered\\Ptext that is a bit longer";
    m.alignment_point = m.text.insertion_point;
    attr.data.multiline = m;
    ins.as_insert().attributes.push_back(attr);
    std::vector<Record> recs = records_of(save(dwg));
    std::vector<Record> attribs;
    for (const auto& r : recs) if (r[0].second == "ATTRIB") attribs.push_back(r);
    hdxf_test::assert_eq(static_cast<int>(attribs.size()), 2, "two ATTRIBs: the first line and the second");
    if (attribs.size() != 2) return;
    // AutoCAD's: the first line as TEXT with the tag's _001, the second with _002 and the lock (280 1)
    hdxf_test::assert_eq(value(attribs[0], 1), "Centered", "the first line");
    hdxf_test::assert_eq(value(attribs[0], 2), "NOTE_001", "its tag");
    hdxf_test::assert_true(std::fabs(number(value(attribs[0], 10)) - 99.28444747612552) < 1e-9, "its pen x");
    hdxf_test::assert_true(std::fabs(number(value(attribs[0], 20)) - 206.943610732151) < 1e-9, "its pen y");
    hdxf_test::assert_eq(value(attribs[1], 1), "text that is a bit longer", "the second line");
    hdxf_test::assert_eq(value(attribs[1], 2), "NOTE_002", "its tag");
    hdxf_test::assert_true(std::fabs(number(value(attribs[1], 10)) - 91.28922237380628) < 1e-9, "its pen x");
    hdxf_test::assert_true(std::fabs(number(value(attribs[1], 20)) - 203.6102773988176) < 1e-9, "its pen y");
    hdxf_test::assert_eq(value(groups_in(attribs[1], "AcDbAttribute"), 280, 1), "1", "locked");
    hdxf_test::assert_eq(value(groups_in(attribs[0], "AcDbAttribute"), 280, 1), "0", "the first one is not");
    // The XRECORD lists the second
    Record x = xrecord_of(recs, value(attribs[0], 360), "ACAD_MLATT");
    hdxf_test::assert_eq(value(groups_in(x, "AcDbXrecord"), 70, 2), "1", "one other line");
    hdxf_test::assert_eq(value(groups_in(x, "AcDbXrecord"), 340), value(attribs[1], 5), "its handle");
    // The model: the other line is told whose it is, and is not counted as an attribute of its own
    hdxf_test::assert_eq(static_cast<int>(dwg.entities()[0].as_insert().attributes.size()), 2, "two attributes in the INSERT");
    hdxf_test::assert_eq(dwg.entities()[0].as_insert().attributes[1].data.secondary_of, dwg.entities()[0].as_insert().attributes[0].props.handle, "secondary_of");
    // Changing the text changes the lines; taking the attribute away takes the others
    dwg.entities()[0].as_insert().attributes[0].data.multiline->text.content = "One line";
    recs = records_of(save(dwg));
    int n = 0;
    for (const auto& r : recs) n += r[0].second == "ATTRIB";
    hdxf_test::assert_eq(n, 1, "one line, one ATTRIB");
    dwg.entities()[0].as_insert().attributes[0].data.multiline->text.content = "A\\PB\\PC";
    recs = records_of(save(dwg));
    n = 0;
    for (const auto& r : recs) n += r[0].second == "ATTRIB";
    hdxf_test::assert_eq(n, 3, "three lines, three ATTRIBs");
    dwg.entities()[0].as_insert().attributes.erase(dwg.entities()[0].as_insert().attributes.begin());
    recs = records_of(save(dwg));
    n = 0;
    for (const auto& r : recs) n += r[0].second == "ATTRIB";
    hdxf_test::assert_eq(n, 0, "the attribute gone, its other lines gone with it");
    for (const auto& r : recs) {
        for (const auto& g : r) hdxf_test::assert_true(g.second != "ACAD_MATT_HEIGHT", "no XRECORD of an attribute that is gone");
    }
}

// AutoCAD reads an attribute definition up to 2013 from its TEXT: its bottom row is a third of the
// height and TEXT's bottom offset higher than the text (measured with AutoCAD's core on every
// attachment): the alignment point is the insertion point less that
TEST(attrib_2018_definition_bottom_row_before_2018) {
    ArialPath arial;
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::TextStyle& s = dwg.add_text_style("Standard", "arial.ttf");
    s.font_family = "Arial";
    s.font_flags = 34;
    hdxf::Block& block = dwg.add_block("B");
    for (int a : {1, 5, 7}) {
        hdxf::AttdefData def;
        def.tag = "T" + std::to_string(a);
        hdxf::MultilineAttribute md;
        md.type = 4;
        md.text.insertion_point = {10.0 * a, 6, 0};
        md.text.height = 2.0;
        md.text.attachment = a;
        md.text.content = "Default";
        md.alignment_point = md.text.insertion_point;
        def.multiline = md;
        hdxf::Entity e;
        e.type = hdxf::EntityType::Attdef;
        e.data = def;
        block.entities.push_back(e);
    }
    std::vector<Record> recs = records_of(save(dwg));
    for (const auto& r : recs) {
        if (r[0].second != "ATTDEF") continue;
        std::string tag = value(r, 2);
        double y = number(value(groups_in(r, "AcDbText"), 21));
        if (tag == "T7") hdxf_test::assert_true(std::fabs(y - (6.0 - 0.62938 * 2.0)) < 1e-3, "bottom row: " + std::to_string(y));
        else hdxf_test::assert_eq(y, 6.0, tag + ": the insertion point");
    }
    // 2018 reads the embedded object: the alignment point is the insertion point
    dwg.set_version(hdxf::DxfVersion::R2018);
    for (const auto& r : records_of(save(dwg))) {
        if (r[0].second == "ATTDEF") hdxf_test::assert_eq(number(value(groups_in(r, "AcDbText"), 21)), 6.0, value(r, 2) + " in 2018");
    }
}

// A single-line attribute's 71 and 72 (ODA writes them on every attribute) are kept without any
// text: written back as they were, and not made into a multiline text (B75)
TEST(attrib_2018_single_line_71_is_kept) {
    if (!std::filesystem::exists(external("jscad/empty2013.dxf"))) hdxf_test::skip("no files");
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    hdxf::Block& block = dwg.add_block("B");
    hdxf::AttdefData def;
    def.tag = "T";
    hdxf::MultilineAttribute m;
    m.type = 1;
    m.has_text = false;
    m.has_alignment_point = false;
    def.multiline = m;
    hdxf::Entity e;
    e.type = hdxf::EntityType::Attdef;
    e.data = def;
    block.entities.push_back(e);
    std::vector<uint8_t> out = save(dwg);
    for (const auto& r : records_of(out)) {
        if (r[0].second != "ATTDEF") continue;
        Record own = groups_in(r, "AcDbAttributeDefinition");
        hdxf_test::assert_eq(value(own, 71), "1", "71");
        hdxf_test::assert_eq(value(own, 72), "0", "72");
        hdxf_test::assert_true(embedded_of(r).empty(), "no embedded object");
        for (const auto& g : own) hdxf_test::assert_true(g.first != 11, "no alignment point");
    }
    hdxf::Drawing back = load(out);
    hdxf::Entity* d = find_in_block(back, "B", hdxf::EntityType::Attdef);
    hdxf_test::assert_true(d && d->as_attdef().multiline && !d->as_attdef().multiline->has_text && d->as_attdef().multiline->type == 1, "read back");
    // As an older version it has no form: an error, before anything is changed
    dwg.set_version(hdxf::DxfVersion::R2013);
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "refused in 2013");
    hdxf_test::assert_eq(dwg.last_error(), "An attribute's type (71) requires DXF 2018 or later");
}

// ---------------------------------------------------------------------------
// A multiline attribute AutoCAD wrote with more fragments than lines (faciträttning F2: it splits a
// line wherever its format changes, "Second ", "red", " line"): the fragments it was read with stay
// while the text is unchanged — with their handles, tags and edits — and are made again from the
// lines when the text changes
// ---------------------------------------------------------------------------

namespace {

// Two lines (an ATTRIB for each) written for 2010, then a third fragment added to the file the way
// AutoCAD's has them: another ATTRIB after the second, listed in the XRECORD
std::string file_with_a_fragment(std::string& fragment_handle) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2010);
    dwg.add_text_style("Standard", "arial.ttf").font_family = "Arial";
    dwg.add_block("B");
    hdxf::Entity& ins = dwg.add_insert("B", {100, 200, 0});
    hdxf::InsertAttribute attr;
    attr.data.tag = "NOTE";
    attr.data.height = 2.0;
    hdxf::MultilineAttribute m;
    m.type = 2;
    m.text.insertion_point = {105, 206, 0};
    m.text.height = 2.0;
    m.text.reference_width = 30.0;
    m.text.attachment = 5;
    m.text.drawing_direction = 5;
    m.text.content = "Centered\\Ptext that is a bit longer";
    m.alignment_point = m.text.insertion_point;
    attr.data.multiline = m;
    ins.as_insert().attributes.push_back(attr);
    std::vector<std::string> lines = dxf_text::text_lines(dxf_text::text_of(save(dwg)));
    const std::string eol = lines[0].size() >= 2 && lines[0][lines[0].size() - 2] == '\r' ? "\r\n" : "\n";

    // The second ATTRIB, copied after itself with a handle and a tag of its own
    std::vector<size_t> attribs;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        if (dxf_text::bare(lines[i]) == "0" && dxf_text::bare(lines[i + 1]) == "ATTRIB") attribs.push_back(i);
    }
    hdxf_test::assert_eq(static_cast<int>(attribs.size()), 2, "two ATTRIBs");
    size_t begin = attribs[1], end = begin + 2;
    while (dxf_text::bare(lines[end]) != "0") end += 2;
    std::vector<std::string> copy(lines.begin() + static_cast<long>(begin), lines.begin() + static_cast<long>(end));
    fragment_handle = "7FF0";
    for (size_t i = 0; i + 1 < copy.size(); i += 2) {
        if (dxf_text::bare(copy[i]) == "5") copy[i + 1] = fragment_handle + eol;
        if (dxf_text::bare(copy[i]) == "2") copy[i + 1] = "NOTE_003" + eol;
        if (dxf_text::bare(copy[i]) == "1") copy[i + 1] = " fragment" + eol;
    }
    if (dxf_text::has_102_block(copy, 0, "{ACAD_XDICTIONARY")) dxf_text::erase_102_block(copy, 0, "{ACAD_XDICTIONARY");
    lines.insert(lines.begin() + static_cast<long>(end), copy.begin(), copy.end());

    // The next handle is above the new one, as AutoCAD's is
    for (size_t i = 0; i + 3 < lines.size(); i += 2) {
        if (dxf_text::bare(lines[i + 1]) == "$HANDSEED") lines[i + 3] = "7FF1" + eol;
    }

    // The XRECORD lists three other lines now
    // (the XRECORD with the text: its 1 says "Embedded Object")
    size_t x = 0;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        if (dxf_text::bare(lines[i]) == "0") x = i;
        if (dxf_text::bare(lines[i]) == "1" && dxf_text::bare(lines[i + 1]) == "Embedded Object") break;
    }
    hdxf_test::assert_true(x > 0 && dxf_text::bare(lines[x + 1]) == "XRECORD", "the XRECORD");
    int seventies = 0;
    for (size_t i = x + 2; i + 1 < lines.size() && dxf_text::bare(lines[i]) != "0"; i += 2) {
        if (dxf_text::bare(lines[i]) == "70" && ++seventies == 3) {
            hdxf_test::assert_eq(dxf_text::bare(lines[i + 1]), "1", "one other line");
            lines[i + 1] = "     2" + eol;
        }
        if (dxf_text::bare(lines[i]) == "340") {
            lines.insert(lines.begin() + static_cast<long>(i) + 2, {"340" + eol, fragment_handle + eol});
            break;
        }
    }
    return dxf_text::joined(lines);
}

} // namespace

TEST(attrib_2013_fragments_stay_while_the_text_is_unchanged) {
    ArialPath arial;
    std::string handle;
    std::string autocad = file_with_a_fragment(handle);
    std::vector<uint8_t> in = dxf_text::bytes_of(autocad);
    hdxf::Drawing dwg = load(in);
    auto& attributes = dwg.entities()[0].as_insert().attributes;
    hdxf_test::assert_eq(static_cast<int>(attributes.size()), 3, "the first line and two fragments");
    hdxf_test::assert_eq(static_cast<int>(attributes[0].data.multiline->secondary.size()), 2, "the XRECORD lists both");
    std::vector<uint8_t> again = save(dwg);
    hdxf_test::assert_true(again == in, "unchanged: the fragments as they were, record for record: " + dxf_text::first_difference(in, again));

    // A fragment edited by itself while the text is unchanged keeps its edit
    attributes[2].data.value = " edited";
    hdxf::Drawing edited = load(save(dwg));
    auto& edited_attributes = edited.entities()[0].as_insert().attributes;
    hdxf_test::assert_eq(static_cast<int>(edited_attributes.size()), 3, "still three");
    hdxf_test::assert_eq(edited_attributes[2].data.value, " edited", "the edit");
    hdxf_test::assert_eq(edited_attributes[2].props.handle, handle, "its handle");
    hdxf_test::assert_eq(edited_attributes[2].data.tag, "NOTE_003", "its tag");

    // A changed text is laid out again: a fragment for each of its lines, the old ones gone
    edited_attributes[0].data.multiline->text.content = "One\\PTwo\\PThree\\PFour";
    hdxf::Drawing changed = load(save(edited));
    hdxf_test::assert_eq(static_cast<int>(changed.entities()[0].as_insert().attributes.size()), 4, "four lines, four ATTRIBs");
    hdxf_test::assert_eq(static_cast<int>(changed.entities()[0].as_insert().attributes[0].data.multiline->secondary.size()), 3,
                         "three others");
    std::vector<std::string> lines = dxf_text::text_lines(dxf_text::text_of(save(changed)));
    int seventies = 0;
    size_t xrecord = 0;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        if (dxf_text::bare(lines[i]) == "0") xrecord = i;
        if (dxf_text::bare(lines[i]) == "1" && dxf_text::bare(lines[i + 1]) == "Embedded Object") break;
    }
    for (size_t i = xrecord + 2; i + 1 < lines.size() && dxf_text::bare(lines[i]) != "0"; i += 2) {
        if (dxf_text::bare(lines[i]) == "70" && ++seventies == 3) hdxf_test::assert_eq(dxf_text::bare(lines[i + 1]), "3", "the XRECORD's count");
    }
}

// A reference that is scaled or rotated has an attribute for each run of the text — where its format
// changes —, an unscaled, unrotated one has its lines (AutoCAD's facit, 2000 … 2013: "Second {\C1;red}
// line" is "Second ", "red" and " line" in the rotated references, one ATTRIB in the first)
TEST(attrib_2013_attributes_by_runs_in_a_transformed_reference) {
    ArialPath arial;
    auto build = [](double rotation) {
        hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
        hdxf::TextStyle& s = dwg.add_text_style("Standard", "arial.ttf");
        s.font_family = "Arial";
        s.font_flags = 34;
        dwg.add_block("B");
        hdxf::Entity& ins = dwg.add_insert("B", {100, 200, 0});
        ins.as_insert().rotation = rotation;
        hdxf::InsertAttribute attr;
        attr.data.tag = "NOTE";
        attr.data.height = 2.0;
        hdxf::MultilineAttribute m;
        m.type = 2;
        m.text.insertion_point = {105, 206, 0};
        m.text.height = 2.0;
        m.text.reference_width = 60.0;
        m.text.attachment = 1;
        m.text.drawing_direction = 5;
        m.text.content = "Second {\\C1;red} line";
        // The text of a rotated reference runs along the rotated direction (as in AutoCAD's XRECORD)
        double angle = rotation * 3.14159265358979323846 / 180.0;
        m.text.x_axis_direction = {std::cos(angle), std::sin(angle), 0.0};
        m.alignment_point = m.text.insertion_point;
        attr.data.multiline = m;
        ins.as_insert().attributes.push_back(attr);
        return dwg;
    };
    auto attribs_of = [](hdxf::Drawing dwg) {
        std::vector<Record> out;
        for (const auto& r : records_of(save(dwg))) if (r[0].second == "ATTRIB") out.push_back(r);
        return out;
    };
    std::vector<Record> plain = attribs_of(build(0.0));
    hdxf_test::assert_eq(static_cast<int>(plain.size()), 1, "unrotated: the line");
    if (plain.size() == 1) hdxf_test::assert_eq(value(plain[0], 1), "Second red line", "whole");

    std::vector<Record> runs = attribs_of(build(30.0));
    hdxf_test::assert_eq(static_cast<int>(runs.size()), 3, "rotated: a run each");
    if (runs.size() != 3) return;
    hdxf_test::assert_eq(value(runs[0], 1), "Second ", "the first run");
    hdxf_test::assert_eq(value(runs[1], 1), "red", "the red one");
    hdxf_test::assert_eq(value(runs[2], 1), " line", "the last");
    hdxf_test::assert_eq(value(runs[0], 2), "NOTE_001");
    hdxf_test::assert_eq(value(runs[1], 2), "NOTE_002");
    hdxf_test::assert_eq(value(runs[2], 2), "NOTE_003");
    // On one baseline, one after the other: along the rotated direction
    double dx1 = number(value(runs[1], 10)) - number(value(runs[0], 10));
    double dy1 = number(value(runs[1], 20)) - number(value(runs[0], 20));
    double dx2 = number(value(runs[2], 10)) - number(value(runs[1], 10));
    double dy2 = number(value(runs[2], 20)) - number(value(runs[1], 20));
    hdxf_test::assert_true(std::fabs(dy1 / dx1 - std::tan(30.0 * 3.14159265358979323846 / 180.0)) < 1e-9, "on the line of the text");
    hdxf_test::assert_true(std::fabs(dy2 / dx2 - std::tan(30.0 * 3.14159265358979323846 / 180.0)) < 1e-9, "the same line");
    hdxf_test::assert_true(dx1 > 0.0 && dx2 > 0.0, "left to right");
    hdxf_test::assert_eq(value(runs[1], 50), value(runs[0], 50), "the same rotation");
    // The XRECORD lists the two others
    hdxf::Drawing rotated = build(30.0);
    std::vector<Record> all = records_of(save(rotated));
    Record x = xrecord_of(all, value(runs[0], 360), "ACAD_MLATT");
    hdxf_test::assert_eq(value(groups_in(x, "AcDbXrecord"), 70, 2), "2", "two others");
}

// Made again, the attributes of AutoCAD's own references are what AutoCAD wrote: the same runs, the
// same text and where the layout puts them (the references in doc/facit, as AutoCAD 2025 saved them;
// the wrapped one of the references too)
TEST(attrib_2013_regenerated_fragments_match_autocad) {
    ArialPath arial;
    std::filesystem::path path = hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit" / "acad_2013_dxf_blocks_attributes.dxf";
    if (!std::filesystem::exists(path)) hdxf_test::skip("doc/facit has no 2013 blocks_attributes");
    std::vector<uint8_t> in = read_bytes(path);
    hdxf::Drawing dwg = load(in);

    struct Fragment {
        std::string value;
        hdxf::Point3 pen;
    };
    auto fragments_of = [](const hdxf::Drawing& d) {
        std::vector<std::vector<Fragment>> out;
        for (const auto& e : d.entities()) {
            if (e.type != hdxf::EntityType::Insert || e.as_insert().block_name != "HDXF_TAGGED") continue;
            std::vector<Fragment> list;
            bool in_notes = false;
            for (const auto& a : e.as_insert().attributes) {
                if (a.data.tag.rfind("NOTES", 0) == 0) {
                    in_notes = true;
                    list.push_back({a.data.value, a.data.start_point});
                }
            }
            (void)in_notes;
            out.push_back(std::move(list));
        }
        return out;
    };
    std::vector<std::vector<Fragment>> before = fragments_of(dwg);
    hdxf_test::assert_eq(static_cast<int>(before.size()), 3, "three references (the identity, the scaled and the MINSERT)");

    // The XRECORD no longer lists the fragments: they are made again from the text
    for (auto& e : dwg.entities()) {
        if (e.type != hdxf::EntityType::Insert) continue;
        for (auto& a : e.as_insert().attributes) {
            if (a.data.multiline && a.data.multiline->has_text) a.data.multiline->secondary.clear();
        }
    }
    hdxf::Drawing again = load(save(dwg));
    std::vector<std::vector<Fragment>> after = fragments_of(again);
    hdxf_test::assert_eq(static_cast<int>(after.size()), 3, "still three");
    for (size_t k = 0; k < before.size() && k < after.size(); ++k) {
        std::string what = "reference " + std::to_string(k);
        hdxf_test::assert_eq(static_cast<int>(after[k].size()), static_cast<int>(before[k].size()), what + ": the number of fragments");
        for (size_t i = 0; i < before[k].size() && i < after[k].size(); ++i) {
            std::string where = what + ", fragment " + std::to_string(i);
            hdxf_test::assert_eq(after[k][i].value, before[k][i].value, where + ": its text");
            hdxf_test::assert_true(std::fabs(after[k][i].pen.x - before[k][i].pen.x) < 1e-6 &&
                                       std::fabs(after[k][i].pen.y - before[k][i].pen.y) < 1e-6,
                                   where + ": its pen " + std::to_string(after[k][i].pen.x) + "," + std::to_string(after[k][i].pen.y) +
                                       " against AutoCAD's " + std::to_string(before[k][i].pen.x) + "," + std::to_string(before[k][i].pen.y));
        }
    }
}
