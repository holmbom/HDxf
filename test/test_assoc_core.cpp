#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include <iostream>
#include <map>

// The ACDBASSOC framework of step 9.10 (plan 2026-10-27-fas-9-typer.md): ACDBASSOCACTION, ACDBASSOCNETWORK,
// ACDBASSOCDEPENDENCY, ACDBASSOCGEOMDEPENDENCY, the action parameters (PATH, VERTEX, OSNAPPOINTREF, EDGE) and the
// action bodies (EXTRUDED, LOFTED, REVOLVED, SWEPT, PLANE, ALIGNEDDIM). Every AutoCAD-saved file of the corpus that has
// one of them is read, every record must come out typed, written again unchanged it must be the file's own bytes
// (B12), and written from the typed values alone (its preserved record emptied) it must be AutoCAD's own record.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

// The record types of this test: the managers' tests are in test_assoc_perssubent.cpp
bool is_core_type(hdxf::ObjectType t) {
    return is_assoc_type(t) && t != hdxf::ObjectType::AssocPersSubentManager && t != hdxf::ObjectType::PersSubentManager;
}

struct Census {
    std::map<std::string, int> typed;
    std::vector<std::string> untyped;
};

Census census(hdxf::Drawing& d, const std::string& file) {
    Census c;
    for (const auto& o : d.objects()) {
        if (is_core_type(o.type)) {
            ++c.typed[dxf_name_of(o.type)];
        } else if (o.type == hdxf::ObjectType::Unknown) {
            const std::string& name = o.as_unknown().object_type;
            for (const std::string& n : assoc_names()) {
                if (n == name && n != "ACDBASSOCPERSSUBENTMANAGER" && n != "ACDBPERSSUBENTMANAGER") {
                    c.untyped.push_back(file + ": " + name + " " + o.handle + " is untyped");
                }
            }
        }
    }
    return c;
}

} // namespace

TEST(assoc_core_every_record_of_the_corpus_is_typed) {
    std::vector<std::filesystem::path> files = files_with(assoc_names());
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::map<std::string, int> total;
    std::vector<std::string> failures;
    int loaded = 0;
    for (const auto& path : files) {
        hdxf::Drawing d;
        if (!load_file(d, path)) continue;
        ++loaded;
        Census c = census(d, path.filename().string());
        for (const auto& [name, n] : c.typed) total[name] += n;
        failures.insert(failures.end(), c.untyped.begin(), c.untyped.end());
        // a typed record's warning would be a part of the record HDxf does not know
        for (const auto& w : d.warnings()) {
            if (w.find("ACDBASSOC") != std::string::npos && w.find("ACDBASSOCPERSSUBENTMANAGER") == std::string::npos) {
                failures.push_back(path.filename().string() + ": " + w);
            }
        }
    }
    std::cout << "assoc corpus: " << loaded << " files";
    for (const auto& [name, n] : total) std::cout << ", " << n << " " << name;
    std::cout << "\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 12; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " records are not typed:" + message);
    for (const std::string& n : assoc_names()) {
        if (n == "ACDBASSOCPERSSUBENTMANAGER" || n == "ACDBPERSSUBENTMANAGER") continue;
        assert_true(total[n] > 0, "the corpus has no " + n);
    }
}

TEST(assoc_core_written_unchanged_is_the_file_and_from_the_typed_values_is_autocads_record) {
    std::vector<std::filesystem::path> files = files_with(assoc_names());
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
        // ezdxf's files are not AutoCAD's: the head's groups are in the reference's order
        if (path.generic_string().find("/ezdxf/") != std::string::npos) continue;
        hdxf::Drawing d;
        if (!load_bytes(d, bytes)) continue;
        std::vector<RecordId> ids = assoc_record_ids(d);
        if (ids.empty()) continue;
        // A binary file has the records of its ASCII twin; the twins of the facit files are checked, the others skipped
        // (the binary files of this step's own facit and ACadSharp's; the other twins have the same records)
        if (is_binary && name.find("assoc_") == std::string::npos && name.find("sample_AC10") == std::string::npos) continue;
        // A file whose records all have the text of records already checked adds nothing
        if (!is_binary) {
            bool fresh = false;
            for (const RecordId& id : ids) {
                std::string text = ascii_record(bytes, id.type, id.handle);
                if (seen_records.insert(id.type + "\n" + text).second) fresh = true;
            }
            if (!fresh) continue;
        }
        const DxfFormat format = is_binary ? DxfFormat::Binary : DxfFormat::Ascii;
        // unchanged: the file's own bytes
        std::vector<uint8_t> again = d.save_to_memory(format);
        if (again != bytes) {
            std::string difference = is_binary ? std::string("binary differs") : first_difference(bytes, again);
            if (!difference.empty()) {
                failures.push_back(name + " unchanged: " + difference);
                continue;
            }
        }
        // from the typed values alone: the records are AutoCAD's own
        forget_assoc_records(d);
        std::vector<uint8_t> regenerated = d.save_to_memory(format);
        ++checked;
        if (is_binary) ++binary;
        if (regenerated.empty()) {
            failures.push_back(name + " from the typed values: not written: " + d.last_error());
            continue;
        }
        std::vector<uint8_t> original_text = is_binary ? as_ascii(bytes) : bytes;
        std::vector<uint8_t> regenerated_text = is_binary ? as_ascii(regenerated) : regenerated;
        for (const RecordId& id : ids) {
            std::string expected = ascii_record(original_text, id.type, id.handle);
            std::string got = ascii_record(regenerated_text, id.type, id.handle);
            ++records;
            std::string difference = expected.empty() ? std::string("not in the file")
                                     : got.empty() ? std::string("not written") : record_difference(expected, got);
            if (!difference.empty()) {
                failures.push_back(name + " " + id.type + " " + id.handle + ": " + difference);
                break;
            }
        }
    }
    std::cout << "assoc regeneration: " << checked << " files (" << binary << " binary), " << records << " records\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 10; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " files differ:" + message);
}
