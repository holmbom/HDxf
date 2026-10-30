#include "assoc_test_support.hpp"
#include <iostream>
#include <map>

// The persistent subentity managers' history entries (step 9.15, F9.11: the first gap 9.10 left). Every AutoCAD-saved file of the
// corpus that has a manager is read, ASCII and binary; every manager is typed, written unchanged it is the file's own bytes, and
// from the typed values alone it is AutoCAD's own record. The kinds 8 and 10 (SURFTRIM and SURFFILLET) are derived from
// doc/facit/fas9_assoc_history_surf.scr, fas9_assoc_extended.scr and fas9_surfaces.scr.

using hdxf::DxfFormat;
using hdxf_test::assert_eq;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

bool is_manager_type(hdxf::ObjectType t) {
    return t == hdxf::ObjectType::AssocPersSubentManager || t == hdxf::ObjectType::PersSubentManager;
}

const std::vector<hdxf::PersHistoryEntry>& history_of(const hdxf::Object& o) {
    return o.type == hdxf::ObjectType::AssocPersSubentManager ? o.as_assoc_pers_subent_manager().history
                                                              : o.as_pers_subent_manager().history;
}

std::vector<std::filesystem::path> manager_files() {
    std::vector<std::filesystem::path> out;
    const char* only = std::getenv("HDXF_EXT_ONLY");
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(hdxf_test::source_path("doc/facit"), ec); it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.rfind("acad_", 0) != 0 || it->path().extension() != ".dxf") continue;
        static const char* const kinds[] = {"history", "solids", "surfaces", "assoc_core", "assoc_extended", "sample_dynblocks"};
        bool wanted = false;
        for (const char* k : kinds) wanted = wanted || name.find(k) != std::string::npos;
        if (!wanted) continue;
        if (only && name.find(only) == std::string::npos) continue;
        out.push_back(it->path());
    }
    const std::filesystem::path root = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR);
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file() && it->path().extension() == ".dxf" && (!only || it->path().filename().string().find(only) != std::string::npos)) {
            out.push_back(it->path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

const char* manager_name(hdxf::ObjectType t) {
    return t == hdxf::ObjectType::AssocPersSubentManager ? "ACDBASSOCPERSSUBENTMANAGER" : "ACDBPERSSUBENTMANAGER";
}

} // namespace

TEST(assoc_history_manager_entries_are_typed_and_written_from_the_typed_values) {
    std::vector<std::filesystem::path> files = manager_files();
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::vector<std::string> failures;
    int typed = 0, entries = 0, trims = 0, fillets = 0, files_read = 0, binary_files = 0;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = bytes_of(path);
        if (!contains(bytes, "PERSSUBENTMANAGER")) continue;
        const bool binary = is_binary_bytes(bytes);
        const DxfFormat format = binary ? DxfFormat::Binary : DxfFormat::Ascii;
        const std::string name = path.filename().string();
        hdxf::Drawing d;
        if (!load_bytes(d, bytes)) continue;
        std::vector<std::pair<std::string, std::string>> refs;
        bool any_history = false;
        for (const auto& o : d.objects()) {
            if (is_manager_type(o.type)) {
                refs.emplace_back(manager_name(o.type), o.handle);
                const auto& h = history_of(o);
                any_history = any_history || !h.empty();
                if (!h.empty()) { ++typed; entries += static_cast<int>(h.size()); }
                for (const auto& e : h) {
                    if (std::holds_alternative<hdxf::PersHistoryTrim>(e)) ++trims;
                    if (std::holds_alternative<hdxf::PersHistoryFillet>(e)) ++fillets;
                }
            } else if (o.type == hdxf::ObjectType::Unknown) {
                const std::string& type = o.as_unknown().object_type;
                if (type == "ACDBASSOCPERSSUBENTMANAGER" || type == "ACDBPERSSUBENTMANAGER") {
                    std::string why;
                    for (const auto& w : d.warnings()) if (w.find(o.handle) != std::string::npos) why = w;
                    failures.push_back(name + " " + type + " " + o.handle + ": untyped: " + why);
                }
            }
        }
        // a manager without entries is test_assoc_perssubent.cpp's (9.10); here the files whose managers have entries
        if (refs.empty() || !any_history) continue;
        ++files_read;
        if (binary) ++binary_files;
        // unchanged: the file's own bytes
        std::vector<uint8_t> again = d.save_to_memory(format);
        if (again != bytes) {
            std::string diff = binary ? first_difference(as_ascii(bytes), as_ascii(again)) : first_difference(bytes, again);
            if (binary && diff.empty()) diff = "the binary bytes differ";
            if (!diff.empty()) { failures.push_back(name + " unchanged: " + diff); continue; }
        }
        // from the typed values alone: AutoCAD's own record
        for (auto& o : d.objects()) if (is_manager_type(o.type)) o.raw = hdxf::RawRecord();
        std::vector<uint8_t> regenerated = d.save_to_memory(format);
        if (regenerated.empty()) { failures.push_back(name + ": not written from the typed values: " + d.last_error()); continue; }
        const std::vector<uint8_t> expected_text = binary ? as_ascii(bytes) : bytes;
        const std::vector<uint8_t> got_text = binary ? as_ascii(regenerated) : regenerated;
        for (const auto& r : refs) {
            std::string expected = ascii_record(expected_text, r.first, r.second);
            std::string got = ascii_record(got_text, r.first, r.second);
            if (expected.empty() || got.empty()) { failures.push_back(name + " " + r.first + " " + r.second + ": the record is missing"); continue; }
            std::string diff = record_difference(expected, got);
            if (!diff.empty()) failures.push_back(name + " " + r.first + " " + r.second + ": " + diff);
        }
    }
    std::cout << "manager history: " << files_read << " files (" << binary_files << " binary), " << typed << " typed managers with entries ("
              << entries << " entries, " << trims << " of kind 8, " << fillets << " of kind 10)\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 10; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " managers:" + message);
    assert_true(typed > 10, "the corpus has typed managers with history");
    if (!std::getenv("HDXF_EXT_ONLY")) assert_true(trims > 0 && fillets > 0, "the corpus has entries of kind 8 and 10");
}

// What the kinds 8 and 10 mean in AutoCAD's own file (fas9_assoc_history_surf.scr): F4 is a fillet of a plane (step 36) and a wall
// of two faces (step 37); T1's two planes trimmed by one circle are two entries; a hole in a revolved surface (fas9_surfaces.scr)
// names two paths of two ids, each id with its step
TEST(assoc_history_trim_and_fillet_entries_as_autocad_writes_them) {
    const std::filesystem::path path = hdxf_test::source_path("doc/facit/acad_2018_dxf_history_surf.dxf");
    std::vector<uint8_t> bytes = bytes_of(path);
    if (bytes.empty()) hdxf_test::skip("missing doc/facit/acad_2018_dxf_history_surf.dxf (fas9_assoc_history_surf.scr)");
    hdxf::Drawing d;
    assert_true(load_bytes(d, bytes), "read: " + d.last_error());
    const hdxf::Object* manager = nullptr;
    for (const auto& o : d.objects()) if (o.type == hdxf::ObjectType::AssocPersSubentManager) manager = &o;
    assert_true(manager != nullptr, "the assoc manager is typed");
    const auto& h = manager->as_assoc_pers_subent_manager().history;
    int trims = 0, fillets = 0;
    const hdxf::PersHistoryFillet* two_faces = nullptr;
    for (const auto& e : h) {
        if (std::holds_alternative<hdxf::PersHistoryTrim>(e)) ++trims;
        if (const auto* f = std::get_if<hdxf::PersHistoryFillet>(&e)) {
            ++fillets;
            if (f->step == 41) two_faces = f;
        }
    }
    assert_eq(7, trims, "T1 (two entries), T2, T3, T4 (two entries) and T5");
    assert_eq(5, fillets, "F1 … F5");
    assert_true(two_faces != nullptr, "F4's fillet (step 41)");
    assert_eq(2, static_cast<int>(two_faces->surfaces.size()), "two surfaces");
    assert_eq(1, static_cast<int>(two_faces->surfaces[0].size()), "the plane: one face");
    assert_eq(2, static_cast<int>(two_faces->surfaces[1].size()), "the wall: two faces");
    assert_eq(36, two_faces->surfaces[0][0].ids[0].step, "the plane's step");
    assert_eq(37, two_faces->surfaces[1][1].ids[0].step, "the wall's step");
    assert_eq(50000, two_faces->surfaces[1][1].ids[0].id, "the wall's second face");
    assert_true(two_faces->groups.size() == 1 && two_faces->groups[0].size() == 2 && two_faces->groups[0][1] == std::vector<int32_t>{0, 1},
                "the fillet stands on both faces of the wall");
    // the kind 3 entry before it (step 40) names a vertex where a face of the wall of two segments is the path `2, 1 21, 1 22`
    bool path_face = false;
    for (const auto& e : h) {
        const auto* item = std::get_if<hdxf::PersHistoryItem>(&e);
        if (!item || item->step != 40) continue;
        for (const auto& r : item->records) {
            for (const auto& f : r.faces) {
                if (f.id == 1 && f.parameter == 21 && f.more.size() == 1 && f.more[0].id == 1 && f.more[0].parameter == 22) path_face = true;
            }
        }
    }
    assert_true(path_face, "a face of two ids in a name record");
}

// Version 2 of the assoc manager ends with a third counted list before the 290: empty in most of AutoCAD's files, [15] in the 2013
// and 2018 files of fas9_assoc_extended.scr (2010, version 1, has no third list)
TEST(assoc_history_manager_third_list_as_autocad_writes_it) {
    for (const char* name : {"acad_2013_dxf_assoc_extended.dxf", "acad_2018_dxf_assoc_extended_bin.dxf"}) {
        std::vector<uint8_t> bytes = bytes_of(hdxf_test::source_path(std::string("doc/facit/") + name));
        if (bytes.empty()) hdxf_test::skip(std::string("missing doc/facit/") + name + " (fas9_assoc_extended.scr)");
        hdxf::Drawing d;
        assert_true(load_bytes(d, bytes), "read: " + d.last_error());
        bool found = false;
        for (const auto& o : d.objects()) {
            if (o.type != hdxf::ObjectType::AssocPersSubentManager) continue;
            found = true;
            assert_true(o.as_assoc_pers_subent_manager().third_list == std::vector<int32_t>{15}, std::string(name) + ": the third list is [15]");
        }
        assert_true(found, std::string(name) + ": the assoc manager is typed");
    }
}

TEST(assoc_history_trim_and_fillet_made_from_typed_values_are_written_and_read) {
    using namespace hdxf;
    for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2018);
        AssocPersSubentManagerData m;
        PersHistoryTrim trim;
        trim.step = 58;
        PersHistoryTrimItem ti;
        ti.item.kind = 3;
        ti.item.step = 0;
        ti.item.groups = {{58}};
        PersHistoryRecord rec;
        rec.id = PersHistoryId{1, 1};
        ti.item.records = {rec};
        ti.paths = {PersHistoryPath{{{1, 0, 51}, {1, 1, 51}}}, PersHistoryPath{{{1, 0, 51}, {1, 1, 51}}}};
        trim.items = {ti};
        trim.parameters = {1.25, -3.0, -1.25, -3.0};
        PersHistoryFillet fillet;
        fillet.step = 41;
        fillet.surfaces = {{PersHistoryPath{{{1000000000, 1001, 36}}}}, {PersHistoryPath{{{1, 0, 37}}}, PersHistoryPath{{{50000, 0, 37}}}}};
        fillet.groups = {{{0}, {0, 1}}};
        m.history = {trim, fillet};
        m.next_id = 58;
        m.first_list = {1, 2};
        Object o;
        o.type = ObjectType::AssocPersSubentManager;
        o.handle = "2000";
        o.owner = "0";
        o.data = Boxed<AssocPersSubentManagerData>(m);
        d.objects().push_back(o);
        std::vector<uint8_t> bytes = d.save_to_memory(f);
        assert_true(!bytes.empty(), "not written: " + d.last_error());
        if (f == DxfFormat::Ascii) {
            // AutoCAD's own entry of fas9_surfaces.scr's revolved surface: payload 58 0 58 51 51 51 51, the block with two paths
            std::string ints;
            const std::string record = ascii_record(bytes, "ACDBASSOCPERSSUBENTMANAGER", "2000");
            std::vector<std::string> lines = split_lines(std::vector<uint8_t>(record.begin(), record.end()));
            for (size_t i = 0; i + 1 < lines.size(); i += 2) {
                if (lines[i] == " 90") ints += " " + lines[i + 1].substr(lines[i + 1].find_first_not_of(' '));
            }
            assert_true(ints.find(" 8 0 7 58 0 58 51 51 51 51 28 0 0 0 1 3 0 2 8 0 0 1 1 1 0 0 0 0 2 2 1 0 1 1 2 1 0 1 1 4") != std::string::npos,
                        "the trim as AutoCAD writes it:" + ints);
            assert_true(ints.find(" 10 0 4 41 36 37 37 22 0 0 0 2 1 1 1000000000 1001 2 1 1 0 1 50000 0 1 2 1 0 2 0 1 0") != std::string::npos,
                        "the fillet as AutoCAD writes it:" + ints);
        }
        Drawing r = reload(bytes);
        const Object* ro = find_object(r, "2000");
        assert_true(ro && ro->type == ObjectType::AssocPersSubentManager, "the manager is typed with its history");
        const auto& h = ro->as_assoc_pers_subent_manager().history;
        assert_eq(2, static_cast<int>(h.size()), "two entries");
        const auto* rt = std::get_if<PersHistoryTrim>(&h[0]);
        const auto* rf = std::get_if<PersHistoryFillet>(&h[1]);
        assert_true(rt && rt->items.size() == 1 && rt->items[0].paths.size() == 2 && rt->items[0].paths[1].ids[1].parameter == 1 &&
                        rt->items[0].paths[1].ids[1].step == 51 && rt->parameters.size() == 4,
                    "the trim read back");
        assert_true(rf && rf->surfaces.size() == 2 && rf->surfaces[1][1].ids[0].id == 50000 && rf->surfaces[1][1].ids[0].step == 37 &&
                        rf->groups == std::vector<std::vector<std::vector<int32_t>>>{{{0}, {0, 1}}},
                    "the fillet read back");
        assert_true(r.save_to_memory(f) == bytes, "written again unchanged is the same bytes");
    }
}
