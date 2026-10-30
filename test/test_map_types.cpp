#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "typed_corpus.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.18 of phase 9 (plan 2026-10-27-fas-9-typer.md): the eleven records of AutoCAD Map 3D's display management.
//
// The originals are third-party drawings (build/test-data/local-extensions/PROVENANCE.txt): an AutoCAD Map 3D drawing of AC1018 with
// one display map, one base element and a default style, and two files that have MAPRESOURCEMANAGEROBJECT. They are observations, not
// AutoCAD facit (F9.24: ej testat). No AutoCAD command makes a Map record (the core has none): a longer list, a second display map
// and a changed style reference are tested on the grammar of the records the files have, and reported as ej testat.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf::internal;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

std::vector<uint8_t> local_bytes(const std::string& name) {
    std::filesystem::path p = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / "local-extensions" / hdxf::path_from_utf8(name);
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(p);
    if (in.empty()) hdxf_test::skip("missing build/test-data/local-extensions/" + name);
    return in;
}

Drawing load(const std::vector<uint8_t>& bytes, const std::string& what) {
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), what + ": " + d.last_error());
    return d;
}

std::vector<uint8_t> save(Drawing& d, DxfFormat format, const std::string& what) {
    std::vector<uint8_t> out = d.save_to_memory(format);
    assert_true(!out.empty(), what + ": " + d.last_error());
    return out;
}

std::string trim(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
}

std::vector<Rec> ascii_records(const std::vector<uint8_t>& bytes) {
    std::vector<Rec> out;
    std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::string code_line, value_line;
    while (std::getline(in, code_line) && std::getline(in, value_line)) {
        if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();
        int code = std::atoi(code_line.c_str());
        if (code == 0) out.emplace_back();
        if (!out.empty()) out.back().push_back({code, trim(value_line)});
    }
    return out;
}

const char* const kMapNames[] = {"DMMAPMANAGER", "DMSTYLELIBRARY", "DMSTYLECATEGORY", "DMMAP", "DMBASEELEMENT", "DMDEFAULTSTYLE",
                                 "DMSTYLEREFERENCE", "DMSURROGATESTYLESETS", "DMSTYLIZEDENTITIESTABLE", "MAPMETADATAOBJECT",
                                 "MAPRESOURCEMANAGEROBJECT"};

// The records of the Map types, from their first subclass on (the head's reactors and owner are the framework's)
std::vector<Rec> map_records(const std::vector<uint8_t>& ascii) {
    std::vector<Rec> out;
    for (const Rec& r : ascii_records(ascii)) {
        if (r.empty() || r[0].first != 0) continue;
        if (std::find_if(std::begin(kMapNames), std::end(kMapNames), [&](const char* n) { return r[0].second == n; }) == std::end(kMapNames)) continue;
        Rec body;
        bool in_body = false;
        for (const Group& g : r) {
            if (g.first == 100 && !in_body) { in_body = true; body.push_back(r[0]); }
            if (in_body) body.push_back(g);
        }
        out.push_back(body);
    }
    return out;
}

bool is_real_code(int code) { return (code >= 10 && code <= 59) || (code >= 110 && code <= 149) || (code >= 210 && code <= 239); }

std::string differ(const Rec& a, const Rec& b) {
    if (a.size() != b.size()) return "group counts " + std::to_string(a.size()) + " and " + std::to_string(b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first) return "group " + std::to_string(i) + ": code " + std::to_string(a[i].first) + " and " + std::to_string(b[i].first);
        if (a[i].second == b[i].second) continue;
        if (is_real_code(a[i].first)) {
            double p = std::atof(a[i].second.c_str()), q = std::atof(b[i].second.c_str());
            if (std::fabs(p - q) <= 1e-12 * std::max(1.0, std::fabs(p))) continue;
        }
        return "group " + std::to_string(i) + " (" + std::to_string(a[i].first) + "): \"" + a[i].second + "\" and \"" + b[i].second + "\"";
    }
    return {};
}

bool is_map(const Object& o) {
    return o.type >= ObjectType::MapManager && o.type <= ObjectType::MapResourceManager;
}

void forget_raw(Drawing& d) {
    for (Object& o : d.objects()) if (is_map(o)) o.raw = hdxf::RawRecord();
}

const Object* find(const Drawing& d, ObjectType t) {
    for (const Object& o : d.objects()) if (o.type == t) return &o;
    return nullptr;
}

bool erase_object(Drawing& d, const std::string& handle) {
    std::vector<Object>& list = d.objects();
    auto it = std::find_if(list.begin(), list.end(), [&](const Object& o) { return o.handle == handle; });
    if (it == list.end()) return false;
    list.erase(it);
    return true;
}

bool warned(const Drawing& d, const std::string& part) {
    return std::any_of(d.warnings().begin(), d.warnings().end(), [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

const char* const kDrawing3 = "map_drawing3.dxf";

} // namespace

TEST(map_types_the_registry_has_eleven_codecs_with_their_classes) {
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const char* name : kMapNames) {
        const ObjectCodec* c = registry.find_object(name);
        assert_true(c != nullptr, name);
        assert_eq(18, c->step, "step 9.18");
        assert_true(c->read && c->write, "read and write");
        assert_true(c->cls.cpp_name != nullptr && c->cls.application != nullptr, "a class");
        assert_true(c->versions.can_generate(DxfVersion::R2000, DxfFormat::Ascii) && c->versions.can_generate(DxfVersion::R2018, DxfFormat::Binary), "2000 on");
        assert_false(c->versions.can_generate(DxfVersion::R12, DxfFormat::Ascii), "not R12");
        assert_false(c->versions.can_generate(DxfVersion::R14, DxfFormat::Ascii), "R13/R14 are not established");
        assert_true(registry.implemented(name), name);
    }
    assert_eq(std::string("AcMapDMMap"), std::string(registry.find_object("DMMAP")->cls.cpp_name));
    assert_eq(1153, registry.find_object("DMSURROGATESTYLESETS")->cls.proxy_flags);
    assert_eq(std::string("AcMapSharedComponents"), std::string(registry.find_object("MAPRESOURCEMANAGEROBJECT")->cls.application));
    assert_true(registry.find_object(ObjectType::MapDisplayMap) == registry.find_object("DMMAP"), "by type");
}

TEST(map_types_the_map_3d_drawing_is_read_subclass_by_subclass_and_written_unchanged) {
    std::vector<uint8_t> in = local_bytes(kDrawing3);
    Drawing d = load(in, kDrawing3);
    for (ObjectType t : {ObjectType::MapManager, ObjectType::MapStyleLibrary, ObjectType::MapStyleCategory, ObjectType::MapDisplayMap,
                         ObjectType::MapBaseElement, ObjectType::MapDefaultStyle, ObjectType::MapStyleReference,
                         ObjectType::MapSurrogateStyleSets, ObjectType::MapStylizedEntitiesTable, ObjectType::MapMetadata}) {
        assert_true(find(d, t) != nullptr, "a typed record of the type");
    }
    assert_false(warned(d, "kept as they were read"), "nothing left untyped");
    const hdxf::MapManagerData& m = find(d, ObjectType::MapManager)->as_map_manager();
    assert_eq(1, m.dictionary.cloning, "281");
    assert_eq(1, static_cast<int>(m.dictionary.entries.size()), "one display map");
    assert_eq(std::string("DisplayMap1"), m.dictionary.entries[0].name);
    assert_eq(std::string("1B7"), m.dictionary.entries[0].handle);
    assert_false(m.dictionary.entries[0].hard_owner, "350 is a soft owner");
    assert_eq(std::string("1B7"), m.default_handle, "340 of AcDbDictionaryWithDefault");
    assert_eq(std::string("1B7"), m.current_map, "the current map");
    const hdxf::MapDisplayMapData& map = find(d, ObjectType::MapDisplayMap)->as_map_display_map();
    assert_eq(std::string("Default"), map.header.name, "occurrence 0");
    assert_eq(std::string("0"), map.header.parent_map);
    assert_eq(1, static_cast<int>(map.elements.elements.size()), "occurrence 1: its element");
    assert_eq(std::string("1BA"), map.elements.elements[0]);
    assert_eq(1, static_cast<int>(map.elements.scales.size()));
    assert_true(map.elements.scales[0] == hdxf::kMapNoLimit, "no limit");
    assert_eq(std::string("Legend"), map.legend.name, "occurrence 2: the legend");
    assert_eq(1, static_cast<int>(map.legend.items.size()));
    assert_eq(std::string("1BB"), map.legend.items[0]);
    assert_eq(2, static_cast<int>(map.legend.version), "its version is 2");
    const hdxf::MapBaseElementData& e = find(d, ObjectType::MapBaseElement)->as_map_base_element();
    assert_eq(std::string("Map Base"), e.header.name);
    assert_eq(std::string("1B7"), e.header.map, "330 is the map");
    assert_eq(std::string("1BD"), e.style.style_reference, "360 of the second subclass");
    const hdxf::MapDefaultStyleData& s = find(d, ObjectType::MapDefaultStyle)->as_map_default_style();
    assert_eq(std::string("Default"), s.name);
    assert_eq(100, static_cast<int>(s.reserved_280_0), "280 of the second subclass");
    const Object* style = find(d, ObjectType::MapDefaultStyle);
    assert_eq(1, static_cast<int>(style->xdata.size()), "AcMapDMDisplayStyleRegapp is XDATA");
    assert_eq(std::string("AcMapDMDisplayStyleRegapp"), style->xdata[0].app_name);
    assert_eq(std::string("1BB"), find(d, ObjectType::MapStyleReference)->as_map_style_reference().style);
    assert_eq(std::string("1B7"), find(d, ObjectType::MapStyleReference)->as_map_style_reference().map);
    assert_eq(std::string("1BD"), find(d, ObjectType::MapSurrogateStyleSets)->as_map_surrogate_style_sets().style_reference);
    assert_eq(1, static_cast<int>(find(d, ObjectType::MapStyleLibrary)->as_map_style_library().categories.size()));
    assert_eq(std::string("Display Styles"), find(d, ObjectType::MapStyleCategory)->as_map_style_category().name);
    assert_eq(1, static_cast<int>(find(d, ObjectType::MapMetadata)->as_map_metadata().version));

    // B12: written as read, the records are the file's, byte for byte; the whole file's Map records
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "unchanged");
    std::vector<Rec> want = map_records(in), got = map_records(out);
    assert_true(want.size() >= 10, "the records");
    assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), "the same records");
    for (size_t i = 0; i < want.size(); ++i) {
        assert_true(want[i] == got[i], want[i][0].second + ": unchanged is the file's");
    }
}

TEST(map_types_the_typed_values_alone_write_the_files_records) {
    std::vector<uint8_t> in = local_bytes(kDrawing3);
    Drawing d = load(in, kDrawing3);
    forget_raw(d);
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "typed alone");
    std::vector<Rec> want = map_records(in), got = map_records(out);
    assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()), "the same records");
    for (size_t i = 0; i < want.size(); ++i) {
        std::string why = differ(want[i], got[i]);
        assert_true(why.empty(), want[i][0].second + ": " + why);
    }
    // Binary: the same content
    std::vector<uint8_t> bin = save(d, DxfFormat::Binary, "binary");
    Drawing back = load(bin, "binary back");
    assert_eq(std::string("Legend"), find(back, ObjectType::MapDisplayMap)->as_map_display_map().legend.name);
    assert_eq(std::string("1BD"), find(back, ObjectType::MapBaseElement)->as_map_base_element().style.style_reference);
}

TEST(map_types_a_changed_value_and_a_longer_list_keep_their_counts_and_occurrences) {
    std::vector<uint8_t> in = local_bytes(kDrawing3);
    Drawing d = load(in, kDrawing3);
    Object* map = nullptr;
    for (Object& o : d.objects()) if (o.type == ObjectType::MapDisplayMap) map = &o;
    hdxf::MapDisplayMapData& data = map->as_map_display_map();
    data.header.name = "Default \xC3\xA5";
    data.elements.elements.push_back("1B3");            // a second element (the style category's handle: a record that is there)
    data.elements.scales.push_back(500.0);
    data.legend.name = "Legend 2";
    data.legend.items.clear();
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "changed");
    Drawing back = load(out, "changed back");
    const hdxf::MapDisplayMapData& b = find(back, ObjectType::MapDisplayMap)->as_map_display_map();
    assert_eq(std::string("Default \xC3\xA5"), b.header.name, "occurrence 0 changed");
    assert_eq(2, static_cast<int>(b.elements.elements.size()), "the count follows (90)");
    assert_eq(std::string("1B3"), b.elements.elements[1]);
    assert_eq(2, static_cast<int>(b.elements.scales.size()));
    assert_true(b.elements.scales[1] == 500.0, "the new scale");
    assert_true(b.elements.scales[0] == hdxf::kMapNoLimit, "the old stays");
    assert_eq(std::string("Legend 2"), b.legend.name, "occurrence 2 changed");
    assert_eq(0, static_cast<int>(b.legend.items.size()), "an empty list");
    // the other records are as they were
    std::vector<Rec> was = map_records(in), now = map_records(out);
    for (size_t i = 0; i < was.size(); ++i) {
        if (was[i][0].second == "DMMAP") continue;
        assert_true(was[i] == now[i], was[i][0].second + " untouched");
    }
    // B12 again
    assert_true(save(back, DxfFormat::Ascii, "again") == out, "written again unchanged");
}

TEST(map_types_a_removed_element_is_taken_out_of_its_owners_list_b19) {
    std::vector<uint8_t> in = local_bytes(kDrawing3);
    Drawing d = load(in, kDrawing3);
    assert_true(erase_object(d, "1BA"), "the base element");
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "removed");
    Drawing back = load(out, "removed back");
    assert_true(back.find_object("1BA") == nullptr, "gone");
    const Object* map = find(back, ObjectType::MapDisplayMap);
    assert_true(map != nullptr, "the map stays");
    assert_eq(0, static_cast<int>(map->as_map_display_map().elements.elements.size()), "its list lost the member and the count follows");
    assert_true(find(back, ObjectType::MapBaseElement) == nullptr, "no base element");
    // The manager's pair for the map is not touched: the map is there
    assert_eq(1, static_cast<int>(find(back, ObjectType::MapManager)->as_map_manager().dictionary.entries.size()));
    // Removing the map takes its pair out of the manager's dictionary
    Drawing d2 = load(in, kDrawing3);
    assert_true(erase_object(d2, "1B7"), "the map");
    Drawing back2 = load(save(d2, DxfFormat::Ascii, "map removed"), "map removed back");
    assert_eq(0, static_cast<int>(find(back2, ObjectType::MapManager)->as_map_manager().dictionary.entries.size()), "the pair (3, 350) goes");
    assert_eq(std::string("0"), find(back2, ObjectType::MapManager)->as_map_manager().current_map, "340 is none");
    // The map owns its base element (360) and that owns its style reference: they go with the map, as every hard-owned record
    // goes with its owner (the style reference's 330 named the base element, which is gone)
    assert_true(find(back2, ObjectType::MapBaseElement) == nullptr, "the map's base element goes with it");
    assert_true(find(back2, ObjectType::MapStyleReference) == nullptr, "and the element's style reference");
}

TEST(map_types_records_that_do_not_fit_stay_untyped_and_preserved_with_a_warning) {
    std::vector<uint8_t> in = local_bytes(kDrawing3);
    std::string text(in.begin(), in.end());
    // the library's count says more than there are
    size_t at = text.find("AcMapDMStyleLibrary\n 70\n     1\n 70\n     1\n");
    assert_true(at != std::string::npos, "the library");
    std::string bad = text;
    bad.replace(at, 42, "AcMapDMStyleLibrary\n 70\n     1\n 70\n  9000\n");
    std::vector<uint8_t> bytes(bad.begin(), bad.end());
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
    assert_true(warned(d, "DMSTYLELIBRARY"), "a warning names the record");
    assert_true(find(d, ObjectType::MapStyleLibrary) == nullptr && find(d, ObjectType::MapManager) != nullptr, "one left untyped");
    assert_true(d.save_to_memory(DxfFormat::Ascii) == bytes, "kept byte for byte");
    // a DMMAP with only two AcMapDMMap subclasses
    std::string two = text;
    size_t m = two.find("AcMapDMMap\n 70\n     2\n");
    assert_true(m != std::string::npos, "the legend");
    two.replace(m, 10, "AcMapDMMa_");
    std::vector<uint8_t> b2(two.begin(), two.end());
    Drawing e;
    assert_true(e.load_from_memory(b2.data(), b2.size()), e.last_error());
    assert_true(warned(e, "DMMAP"), "a warning names the DMMAP");
    assert_true(find(e, ObjectType::MapDisplayMap) == nullptr, "left untyped");
}

TEST(map_types_new_records_in_every_version_and_format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    struct Spec {
        const char* name;
        ObjectType type;
        std::function<void(Object&)> fill;
        std::function<std::string(const Object&)> check;
    };
    const Spec specs[] = {
        {"DMMAPMANAGER", ObjectType::MapManager,
         [](Object& o) {
             hdxf::MapManagerData d;
             d.dictionary.entries.push_back({"Mine", "7B01", false});     // the DMMAP add() puts in the drawing
             d.current_map = "7B01";
             o.data = hdxf::Boxed<hdxf::MapManagerData>(d);
         },
         [](const Object& o) { return o.as_map_manager().dictionary.entries.size() == 1 && o.as_map_manager().dictionary.entries[0].name == "Mine" ? std::string() : std::string("the entry"); }},
        {"DMSTYLELIBRARY", ObjectType::MapStyleLibrary,
         [](Object& o) { hdxf::MapStyleLibraryData d; d.categories = {"7B01", "7B02", "7B03"}; o.data = hdxf::Boxed<hdxf::MapStyleLibraryData>(d); },
         [](const Object& o) { return o.as_map_style_library().categories.size() == 3 ? std::string() : std::string("the categories"); }},
        {"DMSTYLECATEGORY", ObjectType::MapStyleCategory,
         [](Object& o) { hdxf::MapStyleCategoryData d; d.name = "Mine \xC3\xA5"; o.data = hdxf::Boxed<hdxf::MapStyleCategoryData>(d); },
         [](const Object& o) { return o.as_map_style_category().name == "Mine \xC3\xA5" ? std::string() : std::string("the name"); }},
        {"DMMAP", ObjectType::MapDisplayMap,
         [](Object& o) {
             hdxf::MapDisplayMapData d;
             d.header.name = "Mine";
             d.elements.elements = {"7B01", "7B02"};
             d.elements.scales = {hdxf::kMapNoLimit, 250.0};
             d.legend.name = "Legend";
             d.legend.items = {"7B03"};
             o.data = hdxf::Boxed<hdxf::MapDisplayMapData>(d);
         },
         [](const Object& o) {
             const hdxf::MapDisplayMapData& d = o.as_map_display_map();
             return d.header.name == "Mine" && d.elements.elements.size() == 2 && d.elements.scales.size() == 2 && d.legend.items.size() == 1 ? std::string() : std::string("the lists");
         }},
        {"DMBASEELEMENT", ObjectType::MapBaseElement,
         [](Object& o) { hdxf::MapBaseElementData d; d.header.name = "Mine"; o.data = hdxf::Boxed<hdxf::MapBaseElementData>(d); },
         [](const Object& o) { return o.as_map_base_element().header.name == "Mine" ? std::string() : std::string("the name"); }},
        {"DMDEFAULTSTYLE", ObjectType::MapDefaultStyle,
         [](Object& o) { hdxf::MapDefaultStyleData d; d.name = "Mine"; o.data = hdxf::Boxed<hdxf::MapDefaultStyleData>(d); },
         [](const Object& o) { return o.as_map_default_style().name == "Mine" ? std::string() : std::string("the name"); }},
        {"DMSTYLEREFERENCE", ObjectType::MapStyleReference,
         [](Object& o) { hdxf::MapStyleReferenceData d; d.flags = 7; o.data = hdxf::Boxed<hdxf::MapStyleReferenceData>(d); },
         [](const Object& o) { return o.as_map_style_reference().flags == 7 ? std::string() : std::string("the flags"); }},
        {"DMSURROGATESTYLESETS", ObjectType::MapSurrogateStyleSets,
         [](Object& o) { hdxf::MapSurrogateStyleSetsData d; d.name = "Mine"; o.data = hdxf::Boxed<hdxf::MapSurrogateStyleSetsData>(d); },
         [](const Object& o) { return o.as_map_surrogate_style_sets().name == "Mine" ? std::string() : std::string("the name"); }},
        {"DMSTYLIZEDENTITIESTABLE", ObjectType::MapStylizedEntitiesTable,
         [](Object& o) { hdxf::MapStylizedEntitiesTableData d; d.version = 3; o.data = hdxf::Boxed<hdxf::MapStylizedEntitiesTableData>(d); },
         [](const Object& o) { return o.as_map_stylized_entities_table().version == 3 ? std::string() : std::string("the version"); }},
        {"MAPMETADATAOBJECT", ObjectType::MapMetadata,
         [](Object& o) { hdxf::MapMetadataData d; d.version = 5; o.data = hdxf::Boxed<hdxf::MapMetadataData>(d); },
         [](const Object& o) { return o.as_map_metadata().version == 5 ? std::string() : std::string("the version"); }},
        {"MAPRESOURCEMANAGEROBJECT", ObjectType::MapResourceManager,
         [](Object& o) { hdxf::MapResourceManagerData d; d.version = 4; o.data = hdxf::Boxed<hdxf::MapResourceManagerData>(d); },
         [](const Object& o) { return o.as_map_resource_manager().version == 4 ? std::string() : std::string("the version"); }},
    };
    for (const Spec& spec : specs) {
        auto add = [&](Drawing& d) {
            Object o;
            o.type = spec.type;
            o.owner = "0";
            spec.fill(o);
            d.add_line({0, 0, 0}, {1, 1, 0});
            d.objects().push_back(o);
            // The records the lists and entries name (7B01 ... 7B03): a pointer to a record the drawing does not have is
            // left out when written
            for (const char* handle : {"7B01", "7B02", "7B03"}) {
                Object target;
                target.type = ObjectType::XRecord;
                target.handle = handle;
                target.owner = "0";
                target.data = hdxf::XRecordData{};
                d.objects().push_back(target);
            }
        };
        auto verify = [&](const Drawing& r, DxfVersion, DxfFormat) -> std::string {
            const Object* o = find(r, spec.type);
            if (!o) return "the record";
            return spec.check(*o);
        };
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(spec.name, registry.find_object(spec.name)->versions, add, verify);
        assert_eq(12, report.generated, std::string(spec.name) + ": 2000 ... 2018");
        assert_eq(2, report.named_errors, std::string(spec.name) + ": R12");
        assert_eq(4, report.not_established, std::string(spec.name) + ": R13, R14");
        std::cout << "  " << spec.name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, not established "
                  << report.not_established << ", AutoCAD verified " << report.autocad_verified << ", ej testat " << report.autocad_not_tested
                  << ", R13/R14 not savable " << report.autocad_not_savable << "\n";
    }
}

TEST(map_types_the_resource_manager_of_two_files_is_read_and_written_unchanged) {
    std::vector<uint8_t> in = local_bytes("mapres_andywar65_sample.dxf");
    Drawing d = load(in, "mapres");
    const Object* o = find(d, ObjectType::MapResourceManager);
    assert_true(o != nullptr, "typed");
    assert_eq(1, static_cast<int>(o->as_map_resource_manager().version));
    assert_eq(0, o->as_map_resource_manager().reserved_90_0);
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "mapres");
    std::vector<Rec> want = map_records(in), got = map_records(out);
    assert_eq(static_cast<int>(want.size()), static_cast<int>(got.size()));
    for (size_t i = 0; i < want.size(); ++i) assert_true(want[i] == got[i], "unchanged");
    // the class the file has is written once with the application's name
    assert_true(std::string(out.begin(), out.end()).find("AcMapSharedComponents") != std::string::npos, "its class");
}
