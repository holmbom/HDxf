#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include <iostream>
#include <map>

// The other ACDBASSOC classes of step 9.15 (plan 2026-10-27-fas-9-typer.md, F9.12): the 2D constraint group, variables and
// value dependencies, the surface-operation, dimension, multileader and array bodies, the other parameters. Every AutoCAD-saved
// file of the corpus (doc/facit/fas9_assoc_extended.scr in 2000 … 2018 and both formats, Autodesk's installed dynamic block
// samples, the surfaces and associative files of 9.10) is read, every record of these types must come out typed, written
// again unchanged it must be the file's own bytes (B12), and written from the typed values alone (its preserved record
// emptied) it must be AutoCAD's own record.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

// The record types of this step: the codecs that say step 15
bool is_ext_type(hdxf::ObjectType t) {
    const hdxf::internal::ObjectCodec* c = hdxf::internal::TypedRegistry::instance().find_object(t);
    return c && c->step == 15;
}

std::string ext_name(hdxf::ObjectType t) {
    const hdxf::internal::ObjectCodec* c = hdxf::internal::TypedRegistry::instance().find_object(t);
    return c ? std::string(c->dxf_name) : std::string();
}

std::vector<std::string> ext_names() {
    std::vector<std::string> names;
    for (const auto& c : hdxf::internal::TypedRegistry::instance().object_codecs()) {
        if (c.step == 15) names.push_back(std::string(c.dxf_name));
    }
    return names;
}

struct RecordRef {
    std::string type;
    std::string handle;
};

// The records of the step's types in an ASCII file's text, found without loading it
std::vector<RecordRef> ext_records_in(const std::vector<uint8_t>& bytes) {
    static const std::vector<std::string> names = ext_names();
    std::string text(bytes.begin(), bytes.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    std::vector<RecordRef> out;
    for (const std::string& n : names) {
        const std::string key = "  0\n" + n + "\n  5\n";
        size_t at = 0;
        while ((at = text.find(key, at)) != std::string::npos) {
            size_t start = at + key.size();
            size_t end = text.find('\n', start);
            out.push_back({n, text.substr(start, end - start)});
            at = start;
        }
    }
    return out;
}

// The corpus files that can have a record of the step's types: AutoCAD's files of doc/facit by their names (the step's own
// facit, 9.10's, Eriks' surface and associative files, the installed dynamic block samples; not the manager history files,
// which have none of them) and every file of build/test-data. HDXF_EXT_ONLY=text keeps the files whose name has the text
std::vector<std::filesystem::path> corpus_files() {
    std::vector<std::filesystem::path> out;
    const char* only = std::getenv("HDXF_EXT_ONLY");
    auto take = [&](const std::filesystem::path& p) {
        if (p.extension() != ".dxf") return;
        if (only && p.filename().string().find(only) == std::string::npos) return;
        out.push_back(p);
    };
    std::error_code ec;
    for (auto it = std::filesystem::directory_iterator(hdxf_test::source_path("doc/facit"), ec); it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        // (the manager history files have none of these types, but history_surf has ACDBASSOCTRIMMINGBODYACTIONPARAM)
        if (name.rfind("acad_", 0) != 0 || (name.find("history") != std::string::npos && name.find("history_surf") == std::string::npos)) continue;
        static const char* const kinds[] = {"assoc_extended", "assoc_core", "associative", "surfaces", "sample_dynblocks", "vertexids",
                                            "history_surf"};
        bool wanted = false;
        for (const char* k : kinds) wanted = wanted || name.find(k) != std::string::npos;
        if (wanted) take(it->path());
    }
    const std::filesystem::path root = hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR);
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file()) take(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST(assoc_extended_every_record_of_the_corpus_is_typed) {
    std::vector<std::filesystem::path> files = corpus_files();
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::map<std::string, int> total;
    std::vector<std::string> failures;
    std::set<std::string> seen;
    std::set<std::string> seen_records;
    int loaded = 0;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = bytes_of(path);
        if (is_binary_bytes(bytes)) continue;
        if (!seen.insert(hdxf_test::sha256_hex(bytes)).second) continue;
        std::vector<RecordRef> refs = ext_records_in(bytes);
        if (refs.empty()) continue;
        bool fresh = false;
        for (const RecordRef& r : refs) {
            if (seen_records.insert(r.type + "\n" + ascii_record(bytes, r.type, r.handle)).second) fresh = true;
        }
        if (!fresh) continue;
        hdxf::Drawing d;
        if (!load_bytes(d, bytes)) continue;
        ++loaded;
        for (const auto& o : d.objects()) {
            if (is_ext_type(o.type)) ++total[ext_name(o.type)];
        }
        for (const RecordRef& r : refs) {
            const hdxf::Object* o = find_object(d, r.handle);
            if (!o || !is_ext_type(o->type)) failures.push_back(path.filename().string() + ": " + r.type + " " + r.handle + " is not typed");
        }
        for (const auto& w : d.warnings()) {
            for (const std::string& n : ext_names()) {
                if (w.find(n) != std::string::npos) failures.push_back(path.filename().string() + ": " + w);
            }
        }
    }
    std::cout << "assoc extended corpus: " << loaded << " files";
    for (const auto& [name, n] : total) std::cout << ", " << n << " " << name;
    std::cout << "\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 12; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " records are not typed:" + message);
    // every type that AutoCAD's files have is in the corpus (the ones none of its commands make are the ej testat of the step)
    static const std::set<std::string> no_autocad_file = {"ACDBASSOCCOMPOUNDACTIONPARAM", "ACDBASSOCOBJECTACTIONPARAM",
                                                          "ACDBASSOCENTITYCLONEACTIONBODY"};
    for (const std::string& n : ext_names()) {
        if (no_autocad_file.count(n)) continue;
        assert_true(total[n] > 0, "the corpus has no " + n);
    }
}

TEST(assoc_extended_written_unchanged_is_the_file_and_from_the_typed_values_is_autocads_record) {
    std::vector<std::filesystem::path> files = corpus_files();
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::set<std::string> seen;
    std::set<std::string> seen_records;
    std::vector<std::string> failures;
    int checked = 0, records = 0, binary = 0;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = bytes_of(path);
        if (!seen.insert(hdxf_test::sha256_hex(bytes)).second) continue;
        const bool is_binary = is_binary_bytes(bytes);
        const std::string name = path.filename().string();
        if (path.generic_string().find("/ezdxf/") != std::string::npos) continue;
        // a binary file has the records of its ASCII twin: the twins of this step's own facit are checked, not the others
        if (is_binary && name.find("history_surf") == std::string::npos &&
            (name.find("assoc_extended") == std::string::npos ||
             (name.find("acad_2000_") == std::string::npos && name.find("acad_2018_") == std::string::npos))) continue;
        std::vector<uint8_t> text_bytes = is_binary ? as_ascii(bytes) : bytes;
        if (text_bytes.empty()) continue;
        std::vector<RecordRef> refs = ext_records_in(text_bytes);
        if (refs.empty()) continue;
        if (!is_binary) {
            bool fresh = false;
            for (const RecordRef& r : refs) {
                if (seen_records.insert(r.type + "\n" + ascii_record(bytes, r.type, r.handle)).second) fresh = true;
            }
            if (!fresh) continue;
        }
        hdxf::Drawing d;
        if (!load_bytes(d, bytes)) continue;
        const DxfFormat format = is_binary ? DxfFormat::Binary : DxfFormat::Ascii;
        std::vector<uint8_t> again = d.save_to_memory(format);
        if (again != bytes) {
            std::string difference = is_binary ? std::string("binary differs") : first_difference(bytes, again);
            if (!difference.empty()) {
                failures.push_back(name + " unchanged: " + difference);
                continue;
            }
        }
        int n = 0;
        for (auto& o : d.objects()) {
            if (is_ext_type(o.type)) { o.raw = hdxf::RawRecord(); ++n; }
        }
        std::vector<uint8_t> regenerated = d.save_to_memory(format);
        ++checked;
        if (is_binary) ++binary;
        if (regenerated.empty()) {
            failures.push_back(name + " from the typed values: not written: " + d.last_error());
            continue;
        }
        std::vector<uint8_t> regenerated_text = is_binary ? as_ascii(regenerated) : regenerated;
        for (const RecordRef& r : refs) {
            std::string expected = ascii_record(text_bytes, r.type, r.handle);
            std::string got = ascii_record(regenerated_text, r.type, r.handle);
            ++records;
            std::string difference = expected.empty() ? std::string("not in the file")
                                     : got.empty() ? std::string("not written") : record_difference(expected, got);
            if (!difference.empty()) {
                failures.push_back(name + " " + r.type + " " + r.handle + ": " + difference);
                break;
            }
        }
    }
    std::cout << "assoc extended regeneration: " << checked << " files (" << binary << " binary), " << records << " records\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 10; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " files differ:" + message);
}

// ACDBASSOCTRIMMINGBODYACTIONPARAM (doc/facit/fas9_assoc_history_surf.scr): the parameter of the surface SURFTRIM cuts with, in
// every version AutoCAD saves it (2000 … 2018) and both formats: the face parameter's subclasses and AcDbAssocTrimmingBodyActionParam
// 90 0 and 280 (0 for the plane cut by a vertical surface, T3; 1 for the two walls that trim each other, T5). Written unchanged it is
// the file; written from the typed values alone it is AutoCAD's record
TEST(assoc_extended_trimming_body_parameter_as_autocad_writes_it) {
    int files = 0;
    for (const char* ver : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (const char* suffix : {"", "_bin"}) {
            const std::string name = std::string("acad_") + ver + "_dxf_history_surf" + suffix + ".dxf";
            std::vector<uint8_t> bytes = bytes_of(hdxf_test::source_path("doc/facit/" + name));
            if (bytes.empty()) hdxf_test::skip("missing doc/facit/" + name + " (fas9_assoc_history_surf.scr)");
            const bool binary = is_binary_bytes(bytes);
            const DxfFormat format = binary ? DxfFormat::Binary : DxfFormat::Ascii;
            hdxf::Drawing d;
            assert_true(load_bytes(d, bytes), name + ": read: " + d.last_error());
            std::vector<int> flags;
            std::vector<std::string> handles;
            for (const auto& o : d.objects()) {
                if (o.type != hdxf::ObjectType::AssocTrimmingBodyActionParam) continue;
                const auto& t = o.as_assoc_trimming_body_action_param();
                assert_eq(std::string("TrimmingFace"), t.face.param.name, name + ": the parameter's name");
                assert_eq(std::string(ver) >= "2013" ? 1 : 0, t.face.param.version, name + ": the parameter's version");
                assert_eq(1, t.face.value_90, name + ": the face's second 90");
                assert_eq(0, t.version, name + ": the version");
                assert_true(t.face.single.dependency != "0" && !t.face.single.dependency.empty(), name + ": the dependency");
                flags.push_back(t.value_280);
                handles.push_back(o.handle);
            }
            assert_true(flags == std::vector<int>{0, 1}, name + ": two parameters, 280 0 (T3) and 1 (T5)");
            assert_true(d.save_to_memory(format) == bytes, name + ": written unchanged it is the file");
            const std::vector<uint8_t> text = binary ? as_ascii(bytes) : bytes;
            for (auto& o : d.objects()) {
                if (o.type == hdxf::ObjectType::AssocTrimmingBodyActionParam) o.raw = hdxf::RawRecord();
            }
            std::vector<uint8_t> regenerated = d.save_to_memory(format);
            assert_true(!regenerated.empty(), name + ": written from the typed values: " + d.last_error());
            const std::vector<uint8_t> regenerated_text = binary ? as_ascii(regenerated) : regenerated;
            for (const std::string& h : handles) {
                std::string expected = ascii_record(text, "ACDBASSOCTRIMMINGBODYACTIONPARAM", h);
                std::string got = ascii_record(regenerated_text, "ACDBASSOCTRIMMINGBODYACTIONPARAM", h);
                assert_true(!expected.empty() && !got.empty(), name + " " + h + ": the record");
                assert_eq(std::string(), record_difference(expected, got), name + " " + h + ": from the typed values");
            }
            ++files;
        }
    }
    assert_eq(12, files, "2000 … 2018, ASCII and binary");
}
