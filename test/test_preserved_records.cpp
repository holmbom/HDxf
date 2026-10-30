#include "test_framework.hpp"
#include "test_drawing_builder.hpp"
#include "hdxf.hpp"
#include "dxf_reader.hpp"
#include "group_io.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Preserved records (phase 0, step 0.4 — doc/plans/2026-10-22-fas-0-grund.md)
//
// Every record of a DXF 2000+ file must be reachable through the public API, complete
// and in order. The expected records are split straight from the file with GroupReader;
// the actual ones are collected from the Drawing.
// ---------------------------------------------------------------------------

using hdxf::internal::GroupCode;
using hdxf::internal::GroupReader;

namespace {

using Record = std::vector<hdxf::DxfGroup>;

struct Container {
    std::string name;               // "HEADER", "TABLE LAYER", "BLOCK *Model_Space", …
    std::vector<Record> records;
};

std::string trimmed(std::string s) {
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

bool is_type(const Record& r, const char* type) {
    return !r.empty() && r[0].code == 0 && trimmed(r[0].value) == type;
}

std::string group_value(const Record& r, int code) {
    for (const auto& g : r) {
        if (g.code == code) return trimmed(g.value);
    }
    return {};
}

// The file split into records at every code 0 (comments dropped), then into the same
// containers the Drawing has.
std::vector<Container> containers_from_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    GroupReader reader(in);
    reader.detect_format();

    std::vector<Record> records;
    GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0 || records.empty()) records.emplace_back();
        records.back().push_back({gc.code, gc.value});
        if (gc.code == 0 && trimmed(gc.value) == "EOF") break;
    }

    std::vector<Container> out;
    size_t i = 0;
    while (i < records.size()) {
        if (!is_type(records[i], "SECTION")) { ++i; continue; }
        std::string section = group_value(records[i], 2);

        if (section == "HEADER") {
            out.push_back({"HEADER", {records[i]}});
            i += 2;                                 // head + ENDSEC
            continue;
        }
        if (section == "TABLES") {
            ++i;
            while (i < records.size() && !is_type(records[i], "ENDSEC")) {
                if (is_type(records[i], "TABLE")) {
                    Container table{"TABLE " + group_value(records[i], 2), {records[i]}};
                    ++i;
                    while (i < records.size() && !is_type(records[i], "ENDTAB")) {
                        table.records.push_back(records[i++]);
                    }
                    out.push_back(std::move(table));
                }
                ++i;
            }
            ++i;
            continue;
        }
        if (section == "BLOCKS") {
            ++i;
            while (i < records.size() && !is_type(records[i], "ENDSEC")) {
                Container block{"BLOCK " + group_value(records[i], 2), {}};
                while (i < records.size() && !is_type(records[i], "ENDSEC")) {
                    block.records.push_back(records[i]);
                    if (is_type(records[i++], "ENDBLK")) break;
                }
                out.push_back(std::move(block));
            }
            ++i;
            continue;
        }
        // ACDSDATA (2013+) is typed into the bodies, layouts and views and kept internally
        // (plan phase 5, B61; test_acds.cpp checks it)
        if (section == "ACDSDATA") {
            while (i < records.size() && !is_type(records[i], "ENDSEC")) ++i;
            ++i;
            continue;
        }
        // ENTITIES and OBJECTS are interpreted: their SECTION record is a marker, not kept
        bool interpreted = section == "ENTITIES" || section == "OBJECTS";
        Container c{section, {}};
        if (!interpreted) c.records.push_back(records[i]);   // raw section head
        ++i;
        while (i < records.size() && !is_type(records[i], "ENDSEC")) {
            c.records.push_back(records[i++]);
        }
        ++i;
        if (section == "OBJECTS" && c.records.empty()) continue;
        out.push_back(std::move(c));
    }
    return out;
}

void add(std::vector<Record>& out, const hdxf::RawRecord& raw) {
    if (!raw.empty()) out.push_back(raw.groups());
}

void add_entities(std::vector<Record>& out, const std::vector<hdxf::Entity>& entities) {
    for (const auto& e : entities) {
        add(out, e.raw);
        if (e.type == hdxf::EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) add(out, v.raw);
            add(out, e.as_polyline().seqend_raw);
        } else if (e.type == hdxf::EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) add(out, a.raw);
            add(out, e.as_insert().seqend_raw);
        }
    }
}

template <typename T>
void add_entries(std::vector<Record>& out, const std::vector<T>& entries) {
    for (const auto& e : entries) add(out, e.raw);
}

// The same containers, collected from the Drawing through the public API
std::vector<Container> containers_from_drawing(const hdxf::Drawing& dwg) {
    std::vector<Container> out;
    for (const auto& s : dwg.raw_sections()) {
        if (s.name == "HEADER") out.push_back({"HEADER", {s.head.groups()}});
    }
    for (const auto& t : dwg.raw_tables()) {
        Container c{"TABLE " + t.name, {t.header.groups()}};
        if (t.name == "VPORT") add_entries(c.records, dwg.vports());
        if (t.name == "LTYPE") add_entries(c.records, dwg.linetypes());
        if (t.name == "LAYER") add_entries(c.records, dwg.layers());
        if (t.name == "STYLE") add_entries(c.records, dwg.text_styles());
        if (t.name == "VIEW") add_entries(c.records, dwg.views());
        if (t.name == "UCS") add_entries(c.records, dwg.ucs_list());
        if (t.name == "APPID") add_entries(c.records, dwg.app_ids());
        if (t.name == "DIMSTYLE") add_entries(c.records, dwg.dim_styles());
        if (t.name == "BLOCK_RECORD") {
            for (const auto& b : dwg.blocks()) add(c.records, b.record.raw);
        }
        for (const auto& r : t.entries) add(c.records, r);
        out.push_back(std::move(c));
    }
    for (const auto& b : dwg.blocks()) {
        Container c{"BLOCK " + b.name, {}};
        add(c.records, b.raw);
        add_entities(c.records, b.entities);
        add(c.records, b.endblk_raw);
        out.push_back(std::move(c));
    }
    Container entities{"ENTITIES", {}};
    add_entities(entities.records, dwg.entities());
    out.push_back(std::move(entities));
    if (!dwg.objects().empty()) {
        Container objects{"OBJECTS", {}};
        for (const auto& o : dwg.objects()) add(objects.records, o.raw);
        out.push_back(std::move(objects));
    }
    for (const auto& s : dwg.raw_sections()) {
        if (s.name == "HEADER") continue;
        Container c{s.name, {}};
        add(c.records, s.head);
        for (const auto& r : s.records) add(c.records, r);
        out.push_back(std::move(c));
    }
    return out;
}

std::string describe(const Record& r) {
    std::string s;
    for (size_t i = 0; i < r.size() && i < 4; ++i) {
        s += std::to_string(r[i].code) + "=" + r[i].value + " ";
    }
    return s;
}

// The file's containers must all be in the Drawing, each with the same records in the
// same order. The order of containers differs (HEADER, tables, blocks, entities, raw
// sections), so they are matched by name and occurrence.
void check_preserved(const std::filesystem::path& path, const std::string& label) {
    hdxf::Drawing dwg;
    {
        std::ifstream in(path, std::ios::binary);
        hdxf::internal::DxfReader reader(in);
        hdxf_test::assert_true(reader.read(dwg), label + ": " + reader.error());
    }

    std::vector<Container> expected = containers_from_file(path);
    std::vector<Container> actual = containers_from_drawing(dwg);

    // Records no parser took end up under the section name — there must be none
    for (const auto& s : dwg.raw_sections()) {
        hdxf_test::assert_true(s.name != "TABLES" && s.name != "BLOCKS" &&
                               s.name != "ENTITIES" && !s.name.empty(),
                               label + ": stray records in \"" + s.name + "\": " +
                               (s.records.empty() ? "" : describe(s.records[0].groups())));
    }
    for (const auto& t : dwg.raw_tables()) {
        bool parsed = t.name == "VPORT" || t.name == "LTYPE" || t.name == "LAYER" ||
                      t.name == "STYLE" || t.name == "VIEW" || t.name == "UCS" ||
                      t.name == "APPID" || t.name == "DIMSTYLE";
        hdxf_test::assert_true(!parsed || t.entries.empty(),
                               label + ": unparsed records in table " + t.name);
    }

    hdxf_test::assert_eq(static_cast<int>(actual.size()), static_cast<int>(expected.size()),
                         label + ": container count");
    std::vector<bool> used(actual.size(), false);
    for (const auto& exp : expected) {
        bool found = false;
        for (size_t j = 0; j < actual.size() && !found; ++j) {
            if (used[j] || actual[j].name != exp.name) continue;
            used[j] = true;
            found = true;
            const auto& act = actual[j];
            hdxf_test::assert_eq(static_cast<int>(act.records.size()),
                                 static_cast<int>(exp.records.size()),
                                 label + ": record count in " + exp.name);
            for (size_t k = 0; k < exp.records.size(); ++k) {
                const Record& a = act.records[k];
                const Record& e = exp.records[k];
                bool same = a.size() == e.size();
                for (size_t g = 0; same && g < a.size(); ++g) {
                    same = a[g].code == e[g].code && a[g].value == e[g].value;
                }
                hdxf_test::assert_true(same, label + ": " + exp.name + " record " +
                                             std::to_string(k) + " differs: " +
                                             describe(e) + "| " + describe(a));
            }
        }
        hdxf_test::assert_true(found, label + ": missing " + exp.name);
    }
}

std::filesystem::path external(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / hdxf::path_from_utf8(name);
}

std::filesystem::path synthetic(const std::string& name) {
    return hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / hdxf::path_from_utf8(name);
}

} // namespace

TEST(preserved_records_external_2000_plus) {
    const char* files[] = {
        "acadsharp/sample_AC1015_ascii.dxf", "acadsharp/sample_AC1015_binary.dxf",
        "acadsharp/sample_AC1018_ascii.dxf", "acadsharp/sample_AC1018_binary.dxf",
        "acadsharp/sample_AC1021_ascii.dxf", "acadsharp/sample_AC1021_binary.dxf",
        "acadsharp/sample_AC1024_ascii.dxf", "acadsharp/sample_AC1024_binary.dxf",
        "acadsharp/sample_AC1027_ascii.dxf", "acadsharp/sample_AC1027_binary.dxf",
        "acadsharp/sample_AC1032_ascii.dxf", "acadsharp/sample_AC1032_binary.dxf",
        "ezdxf/small_r13.dxf", "ezdxf/small_r14.dxf", "ezdxf/bin_dxf_r13.dxf",
        "ezdxf/bin_dxf_r14.dxf", "ezdxf/bin_dxf_r2000.dxf", "ezdxf/dxf_unicode.dxf",
        "ezdxf/Minimal_DXF_AC1021.dxf",
    };
    int found = 0;
    for (const char* name : files) {
        if (!std::filesystem::exists(external(name))) continue;
        found++;
        check_preserved(external(name), name);
    }
    if (found == 0) hdxf_test::skip("external test files missing");
}

TEST(preserved_records_synthetic_2000_plus) {
    for (const char* name : {"r2000_large_handles.dxf", "r2004_unicode_escapes.dxf",
                             "r2007_utf8.dxf", "r2018_embedded_object.dxf"}) {
        check_preserved(synthetic(name), name);
    }
}

// R12 files keep their records too (plan F9–F22): the file is written back from them
TEST(preserved_records_kept_for_r12) {
    hdxf::Drawing dwg = r12_drawing_read_back();
    hdxf_test::assert_true(!dwg.raw_sections().empty(), "the HEADER");
    hdxf_test::assert_true(!dwg.raw_tables().empty(), "raw tables");
    hdxf_test::assert_true(!dwg.entities().empty() && !dwg.layers().empty() && !dwg.blocks().empty(), "something to check");
    for (const auto& e : dwg.entities()) hdxf_test::assert_true(!e.raw.empty(), "entity raw");
    for (const auto& l : dwg.layers()) hdxf_test::assert_true(!l.raw.empty(), "layer raw");
    for (const auto& b : dwg.blocks()) hdxf_test::assert_true(!b.raw.empty(), "block raw");
}

// Plan 0.4: Entity may grow by at most 32 bytes (one field for the record) — 520 → 552
// bytes with 64-bit GCC. Phase 1 plan B18: EntityProperties grows by the lineweight and
// plot style (+40); EntityData does not grow — a large, rare type (VIEWPORT) is Boxed.
// Phase 2 plan B27: Color grows by the true color and color book name, EntityProperties
// by the transparency — at most 48 more. Phase 3 plan B40: EntityProperties grows by the
// material (a string, 32). Phase 5 plan B67: by the extension dictionary (a string, 32).
TEST(preserved_records_entity_size_budget) {
#if defined(__GNUC__) && !defined(__clang__)
    if (sizeof(void*) == 8) {
        // + the 2010 visual style (348) and shadow mode (284); + the extension dictionary
        // (EntityProperties::xdictionary, phase 5 plan B67)
        hdxf_test::assert_true(sizeof(hdxf::Entity) <= 552 + 40 + 48 + 32 + 40 + 32,
                               "sizeof(Entity) = " + std::to_string(sizeof(hdxf::Entity)));
        return;
    }
#endif
    hdxf_test::skip("size budget is defined for 64-bit GCC");
}

TEST(raw_record_groups_round_trip) {
    // Through the reader, the only writer of RawRecord: one record with awkward values
    std::string dxf =
        "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  0\nENDSEC\n"
        "  0\nSECTION\n  2\nENTITIES\n"
        "  0\nLINE  \n  5\nFFFFFFFFFFFFFF00\n  8\nlayer with trailing space \n"
        "999\na comment\n 10\n1.0\n1071\n-5\n  1\n\n  0\nENDSEC\n  0\nEOF\n";
    std::istringstream in(dxf, std::ios::binary);
    hdxf::Drawing dwg;
    hdxf::internal::DxfReader reader(in);
    hdxf_test::assert_true(reader.read(dwg), reader.error());
    hdxf_test::assert_eq(static_cast<int>(dwg.entities().size()), 1);

    std::vector<hdxf::DxfGroup> g = dwg.entities()[0].raw.groups();
    hdxf_test::assert_eq(static_cast<int>(g.size()), 6, "999 not kept");
    hdxf_test::assert_eq(g[0].value, std::string("LINE  "), "kept as written");
    hdxf_test::assert_eq(g[2].value, std::string("layer with trailing space "));
    hdxf_test::assert_eq(g[4].code, 1071);
    hdxf_test::assert_eq(g[5].code, 1);
    hdxf_test::assert_eq(g[5].value, std::string(""), "empty value");
}
