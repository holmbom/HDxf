#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include <iostream>
#include <map>

// The persistent subentity managers and ids of step 9.10 (plan 2026-10-27-fas-9-typer.md, F9.11): ACDBASSOCPERSSUBENTMANAGER,
// ACDBPERSSUBENTMANAGER and the persistent subentity id of ACDBASSOCGEOMDEPENDENCY. The 90-tables of the two managers are
// the named scalars and lists of AssocPersSubentManagerData and PersSubentManagerData: every length of an AutoCAD file's
// record is explained by them (no int32 tape); a manager with history entries stays as it was read.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

bool is_manager(hdxf::ObjectType t) {
    return t == hdxf::ObjectType::AssocPersSubentManager || t == hdxf::ObjectType::PersSubentManager;
}

// The number of 90/290 groups of a manager record as the file has it (the subclass' own groups)
size_t groups_of_record(const std::string& record) {
    size_t n = 0;
    bool in_class = false;
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < record.size()) {
        size_t nl = record.find('\n', pos);
        lines.push_back(record.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        int code = std::atoi(lines[i].c_str());
        if (code == 100) { in_class = true; continue; }
        if (in_class && (code == 90 || code == 290)) ++n;
    }
    return n;
}

// What the named tables of a typed manager come to
size_t groups_of(const hdxf::AssocPersSubentManagerData& d) {
    // version, 3 header, next id, entries (0), list 1, list 2, [trailing], flag
    return 1 + 3 + 1 + 1 + (1 + d.first_list.size()) + (1 + d.second_list.size()) + (d.version >= 2 ? 1 : 0) + 1;
}

size_t groups_of(const hdxf::PersSubentManagerData& d) {
    // version, 2 header, next id, entries (0), the list
    return 1 + 2 + 1 + 1 + (1 + d.ids.size());
}

hdxf::Object make_assoc_manager(int version, int next_id, std::vector<int32_t> first, std::vector<int32_t> second) {
    hdxf::AssocPersSubentManagerData d;
    d.version = version;
    d.next_id = next_id;
    d.first_list = std::move(first);
    d.second_list = std::move(second);
    hdxf::Object o;
    o.type = hdxf::ObjectType::AssocPersSubentManager;
    o.owner = "0";
    o.data = hdxf::Boxed<hdxf::AssocPersSubentManagerData>(std::move(d));
    return o;
}

hdxf::Object make_manager(int next_id, std::vector<int32_t> ids) {
    hdxf::PersSubentManagerData d;
    d.next_id = next_id;
    d.ids = std::move(ids);
    hdxf::Object o;
    o.type = hdxf::ObjectType::PersSubentManager;
    o.owner = "0";
    o.data = hdxf::Boxed<hdxf::PersSubentManagerData>(std::move(d));
    return o;
}

std::vector<int32_t> ids_from(int first, int count) {
    std::vector<int32_t> v;
    for (int i = 0; i < count; ++i) v.push_back(first + i);
    return v;
}

} // namespace

TEST(assoc_perssubent_every_manager_without_history_is_typed_and_its_groups_are_its_tables) {
    std::vector<std::filesystem::path> files = files_with({"ACDBASSOCPERSSUBENTMANAGER", "ACDBPERSSUBENTMANAGER"});
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::set<std::string> seen;
    std::map<size_t, int> lengths;
    int typed_assoc = 0, typed_base = 0, with_history = 0, typed_with_history = 0, files_read = 0;
    std::vector<std::string> failures;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = bytes_of(path);
        if (is_binary_bytes(bytes)) continue;
        if (!seen.insert(hdxf_test::sha256_hex(bytes)).second) continue;
        hdxf::Drawing d;
        if (!load_bytes(d, bytes)) continue;
        ++files_read;
        for (const auto& o : d.objects()) {
            if (o.type == hdxf::ObjectType::AssocPersSubentManager || o.type == hdxf::ObjectType::PersSubentManager) {
                const bool assoc = o.type == hdxf::ObjectType::AssocPersSubentManager;
                std::string record = ascii_record(bytes, assoc ? "ACDBASSOCPERSSUBENTMANAGER" : "ACDBPERSSUBENTMANAGER", o.handle);
                size_t have = groups_of_record(record);
                size_t named = assoc ? groups_of(o.as_assoc_pers_subent_manager()) : groups_of(o.as_pers_subent_manager());
                ++lengths[have];
                if (assoc) ++typed_assoc; else ++typed_base;
                const bool has_history = assoc ? !o.as_assoc_pers_subent_manager().history.empty() : !o.as_pers_subent_manager().history.empty();
                if (has_history) ++typed_with_history;
                // (the entries of a manager with history are typed in step 9.15: test_assoc_extended.cpp)
                if (!has_history && have != named) {
                    failures.push_back(path.filename().string() + " " + o.handle + ": " + std::to_string(have) + " groups, the tables explain " + std::to_string(named));
                }
            } else if (o.type == hdxf::ObjectType::Unknown) {
                const std::string& name = o.as_unknown().object_type;
                if (name == "ACDBASSOCPERSSUBENTMANAGER" || name == "ACDBPERSSUBENTMANAGER") {
                    ++with_history;
                    bool warned = false;
                    for (const auto& w : d.warnings()) {
                        if (w.find(o.handle) != std::string::npos && w.find("history entries") != std::string::npos) warned = true;
                    }
                    if (!warned) failures.push_back(path.filename().string() + " " + name + " " + o.handle + ": untyped without the warning");
                }
            }
        }
    }
    std::cout << "persistent subentity managers: " << files_read << " files, " << typed_assoc << " ACDBASSOCPERSSUBENTMANAGER and "
              << typed_base << " ACDBPERSSUBENTMANAGER typed (" << typed_with_history << " with history entries), " << with_history << " kept as read; lengths:";
    for (const auto& [len, n] : lengths) std::cout << " " << len << "x" << n;
    std::cout << "\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 10; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " managers:" + message);
    assert_true(typed_assoc > 10 && typed_base > 3, "the corpus has both managers");
}

TEST(assoc_perssubent_the_tables_hold_every_length_from_the_smallest_to_the_largest_observed) {
    // The observed records have 6 … 1625 int32 groups (the old inventory: ACDBASSOCPERSSUBENTMANAGER 8 … 1625, ACDBPERSSUBENTMANAGER
    // 6 … 8): any length is a number of ids in the two lists
    for (int ids : {0, 1, 2, 3, 5, 10, 100, 1000, 1625}) {
        for (int version : {1, 2}) {
            hdxf::Drawing d(version == 2 ? DxfVersion::R2013 : DxfVersion::R2010);
            d.objects().push_back(make_assoc_manager(version, ids, ids_from(1, ids), ids_from(1, ids / 2)));
            d.objects().push_back(make_manager(ids, ids_from(1, ids)));
            std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
            assert_true(!bytes.empty(), "the drawing is not written: " + d.last_error());
            hdxf::Drawing r = reload(bytes);
            const hdxf::Object* a = nullptr;
            const hdxf::Object* b = nullptr;
            for (const auto& o : r.objects()) {
                if (o.type == hdxf::ObjectType::AssocPersSubentManager) a = &o;
                if (o.type == hdxf::ObjectType::PersSubentManager) b = &o;
            }
            assert_true(a && b, "both managers are read back typed");
            assert_eq(version, a->as_assoc_pers_subent_manager().version, "the version");
            assert_eq(ids, static_cast<int>(a->as_assoc_pers_subent_manager().first_list.size()), "the first list");
            assert_eq(ids / 2, static_cast<int>(a->as_assoc_pers_subent_manager().second_list.size()), "the second list");
            assert_eq(ids, static_cast<int>(b->as_pers_subent_manager().ids.size()), "the base manager's list");
            std::vector<uint8_t> again = r.save_to_memory(DxfFormat::Ascii);
            assert_true(again == bytes, "written again unchanged is the same bytes (" + std::to_string(ids) + " ids, version " + std::to_string(version) + ")");
            // the binary form
            std::vector<uint8_t> binary = d.save_to_memory(DxfFormat::Binary);
            hdxf::Drawing rb = reload(binary);
            assert_eq(ids, static_cast<int>(rb.objects().back().as_pers_subent_manager().ids.size()), "binary");
        }
    }
}

TEST(assoc_perssubent_a_change_to_a_scalar_and_to_a_list_is_written_where_it_was) {
    hdxf::Drawing d(DxfVersion::R2018);
    d.objects().push_back(make_assoc_manager(2, 3, {1, 2, 3}, {1}));
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    hdxf::Drawing r = reload(bytes);
    for (auto& o : r.objects()) {
        if (o.type != hdxf::ObjectType::AssocPersSubentManager) continue;
        hdxf::AssocPersSubentManagerData& m = o.as_assoc_pers_subent_manager();
        m.next_id = 7;
        m.first_list.push_back(7);
        m.second_list.clear();
    }
    std::vector<uint8_t> changed = r.save_to_memory(DxfFormat::Ascii);
    hdxf::Drawing c = reload(changed);
    for (const auto& o : c.objects()) {
        if (o.type != hdxf::ObjectType::AssocPersSubentManager) continue;
        const hdxf::AssocPersSubentManagerData& m = o.as_assoc_pers_subent_manager();
        assert_eq(7, m.next_id, "the next id");
        assert_eq(4, static_cast<int>(m.first_list.size()), "one id more in the first list");
        assert_eq(0, static_cast<int>(m.second_list.size()), "the second list empty");
        assert_eq(7, m.first_list.back(), "the new id");
    }
}

TEST(assoc_perssubent_a_manager_with_history_entries_or_a_future_form_stays_as_read_with_a_warning) {
    hdxf::Drawing d(DxfVersion::R2018);
    d.objects().push_back(make_assoc_manager(2, 3, {1, 2, 3}, {1}));
    d.objects().push_back(make_manager(2, {1, 2}));
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    std::string text(bytes.begin(), bytes.end());
    auto replace_in_record = [&](const std::string& type, const std::string& from, const std::string& to) {
        std::string t = text;
        size_t at = t.find("  0\r\n" + type + "\r\n");
        assert_true(at != std::string::npos, "the record is in the file");
        size_t sub = t.find("100\r\nAcDb", at);
        size_t pos = t.find(from, sub);
        assert_true(pos != std::string::npos, "the group is in the record");
        t.replace(pos, from.size(), to);
        return t;
    };
    // The number of history entries (the sixth 90 of the subclass) is 1: the entries' grammar is not established
    {
        std::string t = text;
        size_t at = t.find("  0\r\nACDBASSOCPERSSUBENTMANAGER\r\n");
        size_t sub = t.find("100\r\nAcDbAssocPersSubentManager", at);
        size_t pos = sub;
        for (int i = 0; i < 6; ++i) pos = t.find("90\r\n", pos + 1);
        size_t value = pos + 4;
        size_t end = t.find("\r\n", value);
        t.replace(value, end - value, "1");
        hdxf::Drawing r;
        assert_true(r.load_from_memory(reinterpret_cast<const uint8_t*>(t.data()), t.size()), "readable");
        bool untyped = false;
        for (const auto& o : r.objects()) {
            if (o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == "ACDBASSOCPERSSUBENTMANAGER") untyped = true;
        }
        assert_true(untyped, "the manager with an entry is untyped");
        bool warned = false;
        for (const auto& w : r.warnings()) warned = warned || w.find("history entries") != std::string::npos;
        assert_true(warned, "and it has a warning");
        assert_true(r.save_to_memory(DxfFormat::Ascii) == std::vector<uint8_t>(t.begin(), t.end()), "written back as it was read");
    }
    // A version that no file has
    {
        std::string t = replace_in_record("ACDBPERSSUBENTMANAGER", "AcDbPersSubentManager\r\n 90\r\n        2\r\n", "AcDbPersSubentManager\r\n 90\r\n        9\r\n");
        hdxf::Drawing r;
        assert_true(r.load_from_memory(reinterpret_cast<const uint8_t*>(t.data()), t.size()), "readable");
        bool untyped = false;
        for (const auto& o : r.objects()) {
            if (o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == "ACDBPERSSUBENTMANAGER") untyped = true;
        }
        assert_true(untyped, "a version of the base manager that no file has stays untyped");
    }
}

TEST(assoc_perssubent_version_matrix_of_the_two_managers) {
    for (const char* name : {"ACDBASSOCPERSSUBENTMANAGER", "ACDBPERSSUBENTMANAGER"}) {
        const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object(name);
        assert_true(codec != nullptr, std::string(name) + " has a codec");
        const bool assoc = std::string(name) == "ACDBASSOCPERSSUBENTMANAGER";
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
            name, codec->versions,
            [&](hdxf::Drawing& d) { d.objects().push_back(assoc ? make_assoc_manager(d.version() >= DxfVersion::R2013 ? 2 : 1, 4, {1, 2, 3, 4}, {1}) : make_manager(3, {1, 2, 3})); },
            [&](const hdxf::Drawing& r, DxfVersion, DxfFormat) -> std::string {
                int n = 0;
                for (const auto& o : r.objects()) if (o.type == (assoc ? hdxf::ObjectType::AssocPersSubentManager : hdxf::ObjectType::PersSubentManager)) ++n;
                return n == 1 ? std::string() : "expected one typed manager, found " + std::to_string(n);
            });
        std::cout << name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, "
                  << report.not_established << " not established, AutoCAD verified " << report.autocad_verified << ", ej testat "
                  << report.autocad_not_tested << ", not savable " << report.autocad_not_savable << "\n";
        assert_eq(12, report.generated, std::string(name) + ": 2000 … 2018, both formats");
        assert_eq(2, report.named_errors, std::string(name) + ": R12");
        assert_eq(4, report.not_established, std::string(name) + ": R13 and R14");
        assert_eq(12, report.autocad_verified, std::string(name) + ": AutoCAD's own files of 2000 … 2018");
    }
}

// ---------------------------------------------------------------------------
// The persistent subentity id of a geometry dependency
// ---------------------------------------------------------------------------

namespace {

hdxf::Object make_geom_dependency(const hdxf::PersistentSubentityId& id) {
    hdxf::AssocGeomDependencyData d;
    d.subentity_id = id;
    d.dependency.dependent_on_object = "0";
    hdxf::Object o;
    o.type = hdxf::ObjectType::AssocGeomDependency;
    o.owner = "0";
    o.data = hdxf::Boxed<hdxf::AssocGeomDependencyData>(std::move(d));
    return o;
}

} // namespace

TEST(assoc_perssubent_every_persistent_subentity_id_kind_is_written_and_read_in_every_version_and_format) {
    using hdxf::PersistentSubentityKind;
    std::vector<hdxf::PersistentSubentityId> ids;
    {
        hdxf::PersistentSubentityId id;
        id.kind = PersistentSubentityKind::SingleEdge;
        ids.push_back(id);
        id.kind = PersistentSubentityKind::Edge;
        id.version = 0;
        id.start_vertex = 4;
        id.end_vertex = 1;
        ids.push_back(id);
        id = hdxf::PersistentSubentityId();
        id.kind = PersistentSubentityKind::Simple;
        id.subentity_type = -1003;
        id.subentity_index = 3;
        ids.push_back(id);
        id.kind = PersistentSubentityKind::AsmBasedEntity;
        id.subentity_type = 3;
        id.subentity_index = 3;
        ids.push_back(id);
    }
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            hdxf::Drawing d(v);
            for (const auto& id : ids) d.objects().push_back(make_geom_dependency(id));
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), "not written: " + d.last_error());
            hdxf::Drawing r = reload(bytes);
            size_t n = 0;
            for (const auto& o : r.objects()) {
                if (o.type != hdxf::ObjectType::AssocGeomDependency) continue;
                const hdxf::PersistentSubentityId& got = o.as_assoc_geom_dependency().subentity_id;
                assert_true(n < ids.size() && got.kind == ids[n].kind && got.version == ids[n].version && got.start_vertex == ids[n].start_vertex &&
                                got.end_vertex == ids[n].end_vertex && got.subentity_type == ids[n].subentity_type &&
                                got.subentity_index == ids[n].subentity_index,
                            std::string("the id ") + hdxf::persistent_subentity_class_name(ids[n].kind));
                ++n;
            }
            assert_eq(4, static_cast<int>(n), "four geometry dependencies");
            assert_true(r.save_to_memory(f) == bytes, "written again unchanged");
        }
    }
    hdxf::PersistentSubentityKind kind;
    assert_false(hdxf::persistent_subentity_kind_of("AcDbAssocNamedPersSubentId", kind), "a class no file has");
    assert_true(hdxf::persistent_subentity_kind_of("AcDbAssocEdgePersSubentId", kind) && kind == PersistentSubentityKind::Edge, "a class that is");
}

TEST(assoc_perssubent_an_unknown_persistent_subentity_id_class_leaves_the_dependency_as_read) {
    hdxf::Drawing d(DxfVersion::R2018);
    hdxf::PersistentSubentityId id;
    id.kind = hdxf::PersistentSubentityKind::Edge;
    id.start_vertex = 1;
    id.end_vertex = 2;
    d.objects().push_back(make_geom_dependency(id));
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    std::string text(bytes.begin(), bytes.end());
    size_t at = text.find("AcDbAssocEdgePersSubentId");
    assert_true(at != std::string::npos, "the class name is in the file");
    text.replace(at, std::string("AcDbAssocEdgePersSubentId").size(), "AcDbAssocNamedPersSubentId");
    hdxf::Drawing r;
    assert_true(r.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), "readable");
    bool untyped = false;
    for (const auto& o : r.objects()) {
        if (o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == "ACDBASSOCGEOMDEPENDENCY") untyped = true;
    }
    assert_true(untyped, "the dependency with an unknown class stays untyped");
    bool warned = false;
    for (const auto& w : r.warnings()) warned = warned || w.find("not established") != std::string::npos;
    assert_true(warned, "with a warning that says so");
    assert_true(r.save_to_memory(DxfFormat::Ascii) == std::vector<uint8_t>(text.begin(), text.end()), "and is written back as read");
}
