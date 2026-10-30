#include "test_framework.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The type register and the inventory of phase 9 (step 9.0, plan 2026-10-27-fas-9-typer.md): every 0-record
// name of the old plan's appendices A, B and C is in the register with its kind, status and owning step; the
// register agrees with the reader's dispatch; the corpus (build/test-data, doc/acad_*_dxf.dxf, doc/facit) is
// counted once per content (SHA-256) and every record name in it is in the register. The families' own tests
// add the per-type evidence (T); this file is the whole-register view that stays when they are done.
// ---------------------------------------------------------------------------

using namespace hdxf::internal;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

const char* kind_text(TypedKind k) {
    switch (k) {
        case TypedKind::Entity:     return "entity";
        case TypedKind::Object:     return "object";
        case TypedKind::Structural: return "structural";
    }
    return "";
}

const char* status_text(InventoryStatus s) {
    switch (s) {
        case InventoryStatus::AlreadyTyped: return "Typed";
        case InventoryStatus::Structural:   return "Structural";
        case InventoryStatus::Pending:      return "Pending";
        case InventoryStatus::ZeroSamples:  return "Zero";
        case InventoryStatus::DerivedAlias: return "Alias";
        case InventoryStatus::ExternalClass: return "External";
    }
    return "";
}

std::string text_dxf(const std::string& section, const std::string& body) {
    return "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  0\nENDSEC\n"
           "  0\nSECTION\n  2\n" + section + "\n" + body + "  0\nENDSEC\n  0\nEOF\n";
}

// A drawing with one minimal record of the name: is it typed by the reader's dispatch?
// (true: typed data, false: an untyped record, nothing: the file could not be read)
std::optional<bool> typed_by_dispatch(const std::string& name, TypedKind kind, std::string& error,
                                      std::vector<std::string>* warnings = nullptr) {
    std::string text = kind == TypedKind::Entity
                           ? text_dxf("ENTITIES", "  0\n" + name + "\n  5\n100\n100\nAcDbEntity\n  8\n0\n")
                           : text_dxf("OBJECTS", "  0\n" + name + "\n  5\n101\n330\n0\n");
    hdxf::Drawing d;
    if (!d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size())) {
        error = d.last_error();
        return std::nullopt;
    }
    if (warnings) *warnings = d.warnings();
    if (kind == TypedKind::Entity) {
        if (d.entities().size() != 1) { error = "the record was not read as one entity"; return std::nullopt; }
        return d.entities()[0].type != hdxf::EntityType::Unknown;
    }
    if (d.objects().size() != 1) { error = "the record was not read as one object"; return std::nullopt; }
    return d.objects()[0].type != hdxf::ObjectType::Unknown;
}

std::vector<std::filesystem::path> corpus_files() {
    std::vector<std::filesystem::path> files;
    auto scan = [&](const std::filesystem::path& root) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return;
        for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file()) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext == ".dxf") files.push_back(it->path());
        }
    };
    scan(hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR));
    scan(hdxf_test::source_path("doc/facit"));
    // doc/acad_*_dxf.dxf (not the facit directory again)
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(hdxf_test::source_path("doc"), ec); it != std::filesystem::directory_iterator();
         it.increment(ec)) {
        if (ec) break;
        std::string name = it->path().filename().string();
        if (it->is_regular_file() && name.rfind("acad_", 0) == 0 && name.size() > 4 && name.substr(name.size() - 4) == ".dxf") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool is_binary_dxf(const std::vector<uint8_t>& bytes) {
    static const char sentinel[] = "AutoCAD Binary DXF";
    return bytes.size() >= sizeof(sentinel) - 1 && std::equal(sentinel, sentinel + sizeof(sentinel) - 1, bytes.begin());
}

// A file's binary form (or ASCII form) twin: acad_X_bin.dxf / acad_X.dxf, sample_ACxxxx_binary / _ascii
std::filesystem::path twin_of(const std::filesystem::path& path) {
    std::string name = path.filename().string();
    auto swap = [&](const std::string& from, const std::string& to) -> std::filesystem::path {
        size_t at = name.rfind(from);
        if (at == std::string::npos) return {};
        std::string other = name;
        other.replace(at, from.size(), to);
        return path.parent_path() / other;
    };
    if (name.size() > 8 && name.substr(name.size() - 8) == "_bin.dxf") return swap("_bin.dxf", ".dxf");
    if (name.find("_binary.dxf") != std::string::npos) return swap("_binary.dxf", "_ascii.dxf");
    if (name.find("_ascii.dxf") != std::string::npos) return swap("_ascii.dxf", "_binary.dxf");
    if (name.rfind("acad_", 0) == 0) return swap(".dxf", "_bin.dxf");
    return {};
}

// The record names of a drawing: every entity (BLOCKS and ENTITIES) and object, the typed ones by the name
// of their type, the untyped by their own
struct Names {
    std::map<std::string, int> typed;       // names of records the reader types, by a name for the type
    std::map<std::string, int> untyped;     // the 0-names of the untyped records
};

void count_entities(const std::vector<hdxf::Entity>& list, Names& out) {
    for (const hdxf::Entity& e : list) {
        if (e.type == hdxf::EntityType::Unknown) ++out.untyped[e.as_unknown().entity_type];
        else ++out.typed["entity:" + std::to_string(static_cast<int>(e.type))];
    }
}

Names names_of(const hdxf::Drawing& d) {
    Names out;
    count_entities(d.entities(), out);
    for (const hdxf::Block& b : d.blocks()) count_entities(b.entities, out);
    for (const hdxf::Object& o : d.objects()) {
        if (o.type == hdxf::ObjectType::Unknown) ++out.untyped[o.as_unknown().object_type];
        else ++out.typed["object:" + std::to_string(static_cast<int>(o.type))];
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// The tools of the inventory
// ---------------------------------------------------------------------------

TEST(all_typed_records_sha256_known_answers) {
    assert_eq("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hdxf_test::sha256_hex({}));
    const std::string abc = "abc";
    assert_eq("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              hdxf_test::sha256_hex(std::vector<uint8_t>(abc.begin(), abc.end())));
    // 56 bytes: the padding falls into a second block
    const std::string long_text = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    assert_eq("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
              hdxf_test::sha256_hex(std::vector<uint8_t>(long_text.begin(), long_text.end())));
}

TEST(all_typed_records_invalid_template_is_an_error_not_a_type) {
    // The old inventory's one invalid file: a template with @@LASTSAVEDBY@@ where a group code stands
    std::string text = "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1032\n  9\n$LASTSAVEDBY\n@@LASTSAVEDBY@@\n  1\nx\n"
                       "  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing d;
    assert_false(d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), "the template is not a DXF file");
    assert_false(d.last_error().empty(), "with a reason");
    assert_true(d.objects().empty() && d.entities().empty(), "and no record from it");
    assert_true(TypedRegistry::instance().find_entry("@@LASTSAVEDBY@@") == nullptr, "it is no type");
}

// ---------------------------------------------------------------------------
// The register against the tables
// ---------------------------------------------------------------------------

TEST(all_typed_records_register_agrees_with_the_inventory_tables) {
    const TypedRegistry& registry = TypedRegistry::instance();
    auto types = hdxf_test::read_tsv(hdxf_test::source_path("test/data/typed_types.tsv"));
    auto forms = hdxf_test::read_tsv(hdxf_test::source_path("test/data/typed_forms.tsv"));
    assert_true(!types.empty() && !forms.empty(), "the tables are in test/data (tools/gen_type_registry.py)");
    assert_eq(static_cast<int>(registry.entries().size()), static_cast<int>(types.size()), "one row per register entry");

    int appendix_a = 0;
    std::set<std::string> names;
    for (const auto& row : types) {
        const std::string& name = row.at("name");
        names.insert(name);
        const TypeRegistryEntry* entry = registry.find_entry(name);
        assert_true(entry != nullptr, "not in the register: " + name);
        assert_eq(row.at("kind"), kind_text(entry->kind), name + ": kind");
        assert_eq(row.at("status"), status_text(entry->status), name + ": status");
        assert_eq(row.at("planned_type"), std::string(entry->planned_type), name + ": planned type");
        assert_eq(row.at("step"), std::to_string(entry->step), name + ": step");
        assert_eq(row.at("source"), std::string(entry->source), name + ": source");
        if (row.at("source")[0] == 'A') ++appendix_a;
    }
    // the old plan's figures: 166 names in appendix A, 362 observed orders of 104 untyped types in appendix B
    assert_eq(166, appendix_a, "the 166 names of appendix A");
    assert_eq(362, static_cast<int>(forms.size()), "the 362 orders of appendix B");
    std::map<std::string, int> forms_per_name;
    for (const auto& row : forms) ++forms_per_name[row.at("name")];
    assert_eq(104, static_cast<int>(forms_per_name.size()), "104 types have orders");

    int pending = 0, zero = 0;
    for (const TypeRegistryEntry& e : registry.entries()) {
        assert_true(names.count(std::string(e.dxf_name)) == 1, "the table lacks " + std::string(e.dxf_name));
        std::string name(e.dxf_name);
        if (e.status == InventoryStatus::Pending) {
            ++pending;
            // (a name the corpus of 1.8.1 added to the old plan's has no order in appendix B: the step records its own)
            assert_true(forms_per_name.count(name) == 1 || e.source == "corpus", "an untyped type with samples has its orders: " + name);
        }
        if (e.status == InventoryStatus::ZeroSamples) {
            ++zero;
            assert_true(forms_per_name.count(name) == 0, "a type with no samples has no order: " + name);
        }
    }
    assert_true(pending >= 95 && zero >= 100, "pending " + std::to_string(pending) + ", zero " + std::to_string(zero));

    // every order names a type of the register and has its version counts and its sample
    for (const auto& row : forms) {
        assert_true(registry.find_entry(row.at("name")) != nullptr, "an order of a name not in the register: " + row.at("name"));
        assert_true(!row.at("sequence").empty() && row.at("sequence").find("S:") == 0, row.at("name") + " " + row.at("form") + ": the sequence starts at a subclass");
        assert_eq(64, static_cast<int>(row.at("sha256").size()), row.at("name") + " " + row.at("form") + ": SHA256");
        assert_true(!row.at("handle").empty() && !row.at("versions").empty(), row.at("name") + ": handle and versions");
    }
}

TEST(all_typed_records_samples_are_the_files_the_register_names) {
    // Each order's sample that is among the pinned test data (build/test-data/...) has the SHA-256 the old plan
    // gave it: the register's provenance is the file, not the plan's word. Samples that were in a scratch
    // directory of the plan (S/...) are not available: counted, not claimed.
    auto forms = hdxf_test::read_tsv(hdxf_test::source_path("test/data/typed_forms.tsv"));
    std::map<std::string, std::string> hashes;     // sample path -> sha256 of the file on disk
    int checked = 0, missing = 0, scratch = 0;
    for (const auto& row : forms) {
        const std::string& sample = row.at("sample");
        if (sample.rfind("build/test-data/", 0) != 0) { ++scratch; continue; }
        std::filesystem::path path = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR) / sample.substr(std::string("build/test-data/").size());
        auto cached = hashes.find(sample);
        if (cached == hashes.end()) {
            std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
            cached = hashes.emplace(sample, bytes.empty() ? std::string() : hdxf_test::sha256_hex(bytes)).first;
        }
        if (cached->second.empty()) { ++missing; continue; }
        assert_eq(row.at("sha256"), cached->second, row.at("name") + " " + row.at("form") + ": " + sample);
        ++checked;
    }
    std::cout << "typed_forms.tsv: " << checked << " orders with their sample verified, " << missing << " samples not downloaded, "
              << scratch << " in the old plan's scratch directory (not available)\n";
    if (checked == 0) hdxf_test::skip("the pinned test data is not downloaded");
}

// ---------------------------------------------------------------------------
// The register against the reader's dispatch
// ---------------------------------------------------------------------------

TEST(all_typed_records_register_agrees_with_the_readers_dispatch) {
    const TypedRegistry& registry = TypedRegistry::instance();
    int checked = 0;
    for (const TypeRegistryEntry& e : registry.entries()) {
        if (e.kind == TypedKind::Structural) continue;
        std::string name(e.dxf_name);
        std::string error;
        std::vector<std::string> warnings;
        std::optional<bool> typed = typed_by_dispatch(name, e.kind, error, &warnings);
        assert_true(typed.has_value(), "a minimal " + name + " could not be read: " + error);
        // A type with a codec (a family step's) reads what its grammar requires: the minimal record of this test
        // has no subclass, so the codec cannot read it and the record stays untyped with a warning that names it
        // (B105); a codec that can read it types it
        if (registry.implemented(name) && !*typed) {
            bool warned = false;
            for (const std::string& w : warnings) warned = warned || w.find(name) != std::string::npos;
            assert_true(warned, name + ": a codec that cannot read the minimal record leaves it untyped with a warning");
            ++checked;
            continue;
        }
        bool expected = e.status == InventoryStatus::AlreadyTyped || registry.implemented(name);
        assert_true(*typed == expected, name + ": the register says " + status_text(e.status) +
                                            (registry.implemented(name) ? " with a codec" : "") + ", the reader " +
                                            (*typed ? "types it" : "keeps it untyped"));
        ++checked;
    }
    assert_true(checked >= 270, "checked " + std::to_string(checked));
}

TEST(all_typed_records_pending_types_are_not_made_typed_by_registering_a_class_name_alone) {
    // A codec is what makes a type typed (B114): a name in the register, in CLASSES or among the samples is not
    const TypedRegistry& registry = TypedRegistry::instance();
    for (const TypeRegistryEntry& e : registry.entries()) {
        if (e.status == InventoryStatus::Pending || e.status == InventoryStatus::ZeroSamples) {
            if (registry.implemented(e.dxf_name)) {
                // implemented: the codec must have all nine versions decided, none left as unset
                const ObjectCodec* o = registry.find_object(e.dxf_name);
                const EntityCodec* en = registry.find_entity(e.dxf_name);
                const VersionMatrix& m = o ? o->versions : en->versions;
                bool any = false;
                for (size_t v = 0; v < kVersionCount; ++v) {
                    for (size_t f = 0; f < 2; ++f) {
                        any = any || m.cells[v][f].generation != Generation::None || m.cells[v][f].basis != ErrorBasis::NotEstablished;
                    }
                }
                assert_true(any, std::string(e.dxf_name) + " is implemented but its version matrix says nothing");
                assert_true((o ? o->step : en->step) == e.step, std::string(e.dxf_name) + ": registered by step " + std::to_string(e.step));
            }
        }
    }
    // every codec in the registry belongs to a name of the register, of the right kind
    for (const ObjectCodec& c : registry.object_codecs()) {
        const TypeRegistryEntry* e = registry.find_entry(c.dxf_name);
        assert_true(e != nullptr && e->kind == TypedKind::Object, "an object codec for a name not in the register: " + std::string(c.dxf_name));
    }
    for (const EntityCodec& c : registry.entity_codecs()) {
        const TypeRegistryEntry* e = registry.find_entry(c.dxf_name);
        assert_true(e != nullptr && e->kind == TypedKind::Entity, "an entity codec for a name not in the register: " + std::string(c.dxf_name));
    }
}

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

TEST(all_typed_records_every_record_name_of_the_corpus_is_in_the_register) {
    const TypedRegistry& registry = TypedRegistry::instance();
    std::vector<std::filesystem::path> files = corpus_files();
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");

    std::set<std::string> seen_content;
    std::map<std::string, std::set<std::string>> unregistered;      // untyped name -> files
    std::map<std::string, int> untyped_names;                        // the untyped records per name
    std::set<std::string> typed_as_untyped;
    int identical = 0, twins = 0, unreadable = 0, loaded = 0;
    std::vector<std::string> failures;
    for (const std::filesystem::path& path : files) {
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
        if (bytes.empty()) continue;
        if (!seen_content.insert(hdxf_test::sha256_hex(bytes)).second) { ++identical; continue; }
        // a binary file whose ASCII twin is read is not read again: the pair has the same types (tested below)
        if (is_binary_dxf(bytes)) {
            std::filesystem::path twin = twin_of(path);
            if (!twin.empty() && std::filesystem::exists(twin)) { ++twins; continue; }
        }
        hdxf::Drawing d;
        if (!d.load_from_memory(bytes.data(), bytes.size())) {
            ++unreadable;
            failures.push_back(path.filename().string() + ": " + d.last_error());
            continue;
        }
        ++loaded;
        Names names = names_of(d);
        for (const auto& [name, count] : names.untyped) {
            untyped_names[name] += count;
            const TypeRegistryEntry* entry = registry.find_entry(name);
            if (!entry) unregistered[name].insert(path.filename().string());
            else if (entry->status == InventoryStatus::AlreadyTyped && !registry.implemented(name)) typed_as_untyped.insert(name);
        }
    }
    std::cout << "corpus: " << files.size() << " files, " << identical << " with the content of another, " << twins
              << " binary twins not read again, " << loaded << " read, " << unreadable << " not readable; "
              << untyped_names.size() << " untyped record names in it\n";
    for (const std::string& f : failures) std::cout << "  not readable: " << f << "\n";

    std::string message;
    for (const auto& [name, in] : unregistered) message += " " + name + " (" + *in.begin() + ")";
    assert_true(unregistered.empty(), "record names of the corpus that are not in the register:" + message);
    std::string typed_message;
    for (const std::string& n : typed_as_untyped) typed_message += " " + n;
    assert_true(typed_as_untyped.empty(), "typed types that came out untyped:" + typed_message);
    assert_true(loaded > 0, "read none");
}

TEST(all_typed_records_binary_and_ascii_twins_have_the_same_types) {
    struct Pair { std::string ascii; std::string binary; };
    std::vector<Pair> pairs;
    for (const char* version : {"AC1015", "AC1021", "AC1027", "AC1032"}) {
        pairs.push_back({std::string(HDXF_EXTERNAL_DATA_DIR) + "/acadsharp/sample_" + version + "_ascii.dxf",
                         std::string(HDXF_EXTERNAL_DATA_DIR) + "/acadsharp/sample_" + version + "_binary.dxf"});
    }
    for (const char* name : {"objects", "tables", "dynamic_blocks", "associative"}) {
        for (const char* version : {"2000", "2013", "2018"}) {
            pairs.push_back({hdxf_test::source_path(std::string("doc/facit/acad_") + version + "_dxf_" + name + ".dxf").string(),
                             hdxf_test::source_path(std::string("doc/facit/acad_") + version + "_dxf_" + name + "_bin.dxf").string()});
        }
    }
    int compared = 0;
    for (const Pair& p : pairs) {
        std::vector<uint8_t> a = hdxf_test::read_file_bytes(hdxf::path_from_utf8(p.ascii));
        std::vector<uint8_t> b = hdxf_test::read_file_bytes(hdxf::path_from_utf8(p.binary));
        if (a.empty() || b.empty()) continue;
        assert_false(is_binary_dxf(a), p.ascii + " is ASCII");
        assert_true(is_binary_dxf(b), p.binary + " is binary");
        hdxf::Drawing da, db;
        assert_true(da.load_from_memory(a.data(), a.size()), p.ascii + ": " + da.last_error());
        assert_true(db.load_from_memory(b.data(), b.size()), p.binary + ": " + db.last_error());
        Names na = names_of(da), nb = names_of(db);
        // the same types (not the same counts: AutoCAD wrote the two files in two saves, ACadSharp's AC1021 pair has
        // four DICTIONARYVAR more in the binary file)
        auto keys = [](const std::map<std::string, int>& m) {
            std::set<std::string> out;
            for (const auto& entry : m) out.insert(entry.first);
            return out;
        };
        auto difference = [&](const std::map<std::string, int>& a, const std::map<std::string, int>& b) {
            std::string out;
            for (const std::string& k : keys(a)) if (!b.count(k)) out += " " + k + " only in ASCII";
            for (const std::string& k : keys(b)) if (!a.count(k)) out += " " + k + " only in binary";
            return out;
        };
        assert_true(keys(na.typed) == keys(nb.typed) && keys(na.untyped) == keys(nb.untyped),
                    p.ascii + " and its binary twin have different record types:" + difference(na.typed, nb.typed) +
                        difference(na.untyped, nb.untyped));
        ++compared;
    }
    if (compared == 0) hdxf_test::skip("no twin pairs available");
}

TEST(all_typed_records_a_content_is_counted_once_however_many_copies_there_are) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "hdxf_inventory_dedup";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string a = text_dxf("ENTITIES", "  0\nLINE\n  5\n100\n  8\n0\n");
    std::string b = text_dxf("ENTITIES", "  0\nPOINT\n  5\n100\n  8\n0\n");
    for (const char* name : {"one.dxf", "two.dxf", "copy_of_one.dxf"}) {
        std::ofstream(dir / name, std::ios::binary) << (std::string(name) == "two.dxf" ? b : a);
    }
    std::set<std::string> contents;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        contents.insert(hdxf_test::sha256_hex(hdxf_test::read_file_bytes(entry.path())));
    }
    std::filesystem::remove_all(dir);
    assert_eq(2, static_cast<int>(contents.size()), "three files, two contents");
}
