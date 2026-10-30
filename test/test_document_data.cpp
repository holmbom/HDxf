#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include "DocumentDataTypes.hpp"
#include "TableTypes.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.8 of phase 9 (plan 2026-10-27-fas-9-typer.md): FIELD, FIELDLIST, DATATABLE, ACDBDATATABLE, OBJECT_PTR and
// VBA_PROJECT, and the pre-2007 round trip of the tables of DXF 2000 and 2004 (the extra assignment of step 9.1).
//
// The AutoCAD originals are doc/facit/acad_<version>_dxf_tables*.dxf (Erik's GUI files: fields of a table, a
// FIELDLIST of 17, the round-trip DATATABLE of a table with text), acad_<version>_dxf_table_empty*.dxf (fas9_tables.scr:
// AutoCAD's empty tables with their round-trip data table), acad_<version>_dxf_native_records.dxf (a DATATABLE with every
// value type but the owner pointers), acad_<version>_dxf_fields*.dxf (fas9_document_data.scr: TEXT with fields, the
// fields updated and erased) and ACadSharp's, LibreDWG's and Oxcart's files in build/test-data. They are local files
// (the facit is not in git): a test whose file is missing is skipped.
// ---------------------------------------------------------------------------

using hdxf::DataColumn;
using hdxf::DataTableData;
using hdxf::DataTableObject;
using hdxf::DataTableObjectKind;
using hdxf::DataTablePoint2D;
using hdxf::DataTablePoint3D;
using hdxf::DataTableValue;
using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::FieldData;
using hdxf::FieldKeyValue;
using hdxf::Object;
using hdxf::ObjectType;
using hdxf::Point3;
using hdxf::TableValue;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

using Group = std::pair<int, std::string>;
using Rec = std::vector<Group>;

const char* const kTags[] = {"2000", "2004", "2007", "2010", "2013", "2018"};
const DxfVersion kVersions[] = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007,
                                DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018};

std::filesystem::path facit_path(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

std::vector<uint8_t> facit_bytes(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(facit_path(name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
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

DxfFormat format_of(const std::vector<uint8_t>& bytes) {
    return bytes.size() > 18 && std::string(bytes.begin(), bytes.begin() + 18) == "AutoCAD Binary DXF" ? DxfFormat::Binary : DxfFormat::Ascii;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
    return s;
}

// An independent reader of an ASCII file: every record as its groups (code, value), the 0 first
std::vector<Rec> ascii_records(const std::vector<uint8_t>& bytes) {
    std::vector<Rec> out;
    std::string text(bytes.begin(), bytes.end());
    std::istringstream in(text);
    std::string code_line, value_line;
    while (std::getline(in, code_line) && std::getline(in, value_line)) {
        if (!value_line.empty() && value_line.back() == '\r') value_line.pop_back();
        int code = std::atoi(code_line.c_str());
        if (code == 0) out.emplace_back();
        if (!out.empty()) out.back().push_back({code, value_line});
    }
    return out;
}

Rec record_with_handle(const std::vector<Rec>& all, const std::string& type, const std::string& handle) {
    for (const Rec& r : all) {
        if (r.empty() || trim(r[0].second) != type) continue;
        for (const Group& g : r) {
            if (g.first == 5) {
                if (trim(g.second) == handle) return r;
                break;
            }
        }
    }
    return {};
}

std::vector<Rec> records_named(const std::vector<Rec>& all, const std::string& name) {
    std::vector<Rec> out;
    for (const Rec& r : all) if (!r.empty() && trim(r[0].second) == name) out.push_back(r);
    return out;
}

// Two values are the same when they are the same text or the same number (AutoCAD cuts the 16th digit where HDxf rounds:
// the same double)
bool same_value(const std::string& a, const std::string& b) {
    std::string x = trim(a), y = trim(b);
    if (x == y) return true;
    char* ex = nullptr;
    char* ey = nullptr;
    double dx = std::strtod(x.c_str(), &ex);
    double dy = std::strtod(y.c_str(), &ey);
    if (x.empty() || y.empty() || *ex != 0 || *ey != 0) return false;
    return dx == dy || std::fabs(dx - dy) <= 1e-15 * std::max(std::fabs(dx), std::fabs(dy));
}

std::string same_record(const Rec& a, const Rec& b) {
    if (a.size() != b.size()) return "the records have " + std::to_string(a.size()) + " and " + std::to_string(b.size()) + " groups";
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].first != b[i].first || !same_value(a[i].second, b[i].second)) {
            return "group " + std::to_string(i) + ": " + std::to_string(a[i].first) + " \"" + trim(a[i].second) + "\" and " +
                   std::to_string(b[i].first) + " \"" + trim(b[i].second) + "\"";
        }
    }
    return {};
}

std::vector<const Object*> objects_of(const Drawing& d, ObjectType type) {
    std::vector<const Object*> out;
    for (const Object& o : d.objects()) if (o.type == type) out.push_back(&o);
    return out;
}

bool is_document_type(ObjectType t) {
    return t == ObjectType::Field || t == ObjectType::FieldList || t == ObjectType::DataTable || t == ObjectType::LegacyDataTable ||
           t == ObjectType::ObjectPtr || t == ObjectType::VbaProject;
}

bool contains(const std::vector<uint8_t>& bytes, const std::string& text) {
    return std::search(bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
}

// The files of the corpus that have one of the record names
std::vector<std::filesystem::path> files_with(const std::vector<std::string>& names) {
    std::vector<std::filesystem::path> files;
    auto scan = [&](const std::filesystem::path& root, bool recursive) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return;
        auto take = [&](const std::filesystem::path& p) {
            std::string ext = p.extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".dxf") return;
            std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(p);
            for (const std::string& n : names) {
                if (contains(bytes, n)) { files.push_back(p); return; }
            }
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(root, ec); it != std::filesystem::recursive_directory_iterator();
                 it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file()) take(it->path());
            }
        } else {
            for (auto it = std::filesystem::directory_iterator(root, ec); it != std::filesystem::directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (it->is_regular_file() && it->path().filename().string().rfind("acad_", 0) == 0) take(it->path());
            }
        }
    };
    scan(hdxf::path_from_utf8(HDXF_EXTERNAL_DATA_DIR), true);
    scan(hdxf_test::source_path("doc/facit"), true);
    scan(hdxf_test::source_path("doc"), false);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

std::string lower_path(const std::filesystem::path& p) {
    std::string s = p.generic_string();
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The names of the four types AutoCAD's files have, as the files' own text
const std::vector<std::string> kRecordNames = {"FIELD", "DATATABLE", "OBJECT_PTR", "VBA_PROJECT"};

} // namespace

// ---------------------------------------------------------------------------
// The corpus: AutoCAD's records, typed, written back as they were and written from the typed values alone
// ---------------------------------------------------------------------------

TEST(document_data_every_record_of_the_corpus_is_typed_and_written_back_unchanged) {
    std::vector<std::filesystem::path> files = files_with(kRecordNames);
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    int loaded = 0, fields = 0, lists = 0, tables = 0, legacy = 0;
    std::vector<std::string> failures;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
        Drawing d;
        if (!d.load_from_memory(bytes.data(), bytes.size())) continue;
        ++loaded;
        for (const Object& o : d.objects()) {
            if (o.type == ObjectType::Unknown) {
                const std::string& n = o.as_unknown().object_type;
                if (n == "FIELD" || n == "FIELDLIST" || n == "DATATABLE" || n == "ACDBDATATABLE" || n == "OBJECT_PTR" || n == "VBA_PROJECT") {
                    failures.push_back(path.filename().string() + ": " + n + " " + o.handle + " is not typed");
                }
            }
            if (o.type == ObjectType::Field) ++fields;
            if (o.type == ObjectType::FieldList) ++lists;
            if (o.type == ObjectType::DataTable) ++tables;
            if (o.type == ObjectType::LegacyDataTable) ++legacy;
        }
        for (const std::string& w : d.warnings()) {
            if (w.find("FIELD") != std::string::npos || w.find("DATATABLE") != std::string::npos || w.find("OBJECT_PTR") != std::string::npos ||
                w.find("VBA_PROJECT") != std::string::npos) {
                failures.push_back(path.filename().string() + ": warning: " + w);
            }
        }
        // B12: written unchanged it is the file (ezdxf's files are another producer's: not held to it)
        if (lower_path(path).find("/ezdxf/") == std::string::npos) {
            std::vector<uint8_t> again = d.save_to_memory(format_of(bytes));
            if (again != bytes) failures.push_back(path.filename().string() + ": written unchanged, the bytes differ");
        }
    }
    std::cout << "document data corpus: " << loaded << " files, " << fields << " FIELD, " << lists << " FIELDLIST, " << tables
              << " DATATABLE, " << legacy << " ACDBDATATABLE\n";
    for (size_t i = 0; i < failures.size() && i < 12; ++i) std::cout << "  " << failures[i] << "\n";
    assert_true(failures.empty(), std::to_string(failures.size()) + " failure(s), the first: " + (failures.empty() ? std::string() : failures[0]));
    assert_true(fields > 0 && lists > 0 && tables > 0 && legacy > 0, "the corpus has all four types");
}

TEST(document_data_from_the_typed_values_alone_is_autocads_record) {
    std::vector<std::filesystem::path> files = files_with(kRecordNames);
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    int compared = 0;
    std::vector<std::string> failures;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
        if (format_of(bytes) == DxfFormat::Binary) continue;
        if (lower_path(path).find("/ezdxf/") != std::string::npos) continue;
        Drawing d;
        if (!d.load_from_memory(bytes.data(), bytes.size())) continue;
        std::set<std::string> handles;
        std::map<std::string, std::string> types;
        for (Object& o : d.objects()) {
            if (!is_document_type(o.type) || o.raw.empty()) continue;
            handles.insert(o.handle);
            types[o.handle] = o.type == ObjectType::Field ? "FIELD" : o.type == ObjectType::FieldList ? "FIELDLIST"
                              : o.type == ObjectType::DataTable ? "DATATABLE" : o.type == ObjectType::LegacyDataTable ? "ACDBDATATABLE" : "";
            o.raw = hdxf::RawRecord();
        }
        if (handles.empty()) continue;
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        if (out.empty()) { failures.push_back(path.filename().string() + ": " + d.last_error()); continue; }
        std::vector<Rec> before = ascii_records(bytes), after = ascii_records(out);
        for (const std::string& h : handles) {
            Rec a = record_with_handle(before, types[h], h), b = record_with_handle(after, types[h], h);
            if (a.empty()) continue;
            std::string why = b.empty() ? std::string("missing") : same_record(a, b);
            if (!why.empty()) failures.push_back(path.filename().string() + " " + types[h] + " " + h + ": " + why);
            ++compared;
        }
    }
    std::cout << "document data from typed values: " << compared << " records compared\n";
    for (size_t i = 0; i < failures.size() && i < 12; ++i) std::cout << "  " << failures[i] << "\n";
    assert_true(failures.empty(), std::to_string(failures.size()) + " failure(s), the first: " + (failures.empty() ? std::string() : failures[0]));
    assert_true(compared > 200, "AutoCAD's records were compared (" + std::to_string(compared) + ")");
}

// ---------------------------------------------------------------------------
// What AutoCAD's own files hold
// ---------------------------------------------------------------------------

TEST(document_data_reads_the_fields_of_autocads_text_group_by_group) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_fields.dxf");
        Drawing d = load(in, tag);
        std::vector<const Object*> fields = objects_of(d, ObjectType::Field);
        assert_eq(11, static_cast<int>(fields.size()), std::string(tag) + ": eleven fields (five texts with fields: parents and children)");
        std::vector<Rec> all = ascii_records(in);
        // the two-field text: a `_text` field with two child fields (90 2, 360 360)
        const FieldData* both = nullptr;
        for (const Object* o : fields) if (o->as_field().code.rfind("Both:", 0) == 0) both = &o->as_field();
        assert_true(both != nullptr, "the text of two fields");
        assert_eq(std::string("_text"), both->evaluator_id, "the evaluator of the text");
        assert_eq(2, static_cast<int>(both->child_fields.size()), "two children");
        assert_eq(std::string("Both: %<\\_FldIdx 0>% and %<\\_FldIdx 1>%"), both->code);
        for (const std::string& h : both->child_fields) {
            const Object* child = d.find_object(h);
            assert_true(child != nullptr && child->type == ObjectType::Field, "a child is a FIELD");
        }
        const FieldData* expr = nullptr;
        for (const Object* o : fields) if (o->as_field().evaluator_id == "AcExpr" && o->as_field().code.find("2.5+7.25") != std::string::npos) expr = &o->as_field();
        assert_true(expr != nullptr, "the expression field");
        assert_eq(std::string("\\AcExpr (2.5+7.25) \\f \"%lu2%pr2\""), expr->code);
        assert_eq(2, expr->evaluation_status, "evaluated (95)");
        assert_eq(63, expr->evaluation_option, "automatic (91)");
        assert_true(expr->cache.type == hdxf::TableValueType::Double && std::get<double>(expr->cache.value) == 9.75, "the cache is 9.75");
        if (std::string(tag) != "2000" && std::string(tag) != "2004") {
            assert_eq(std::string("%lu2%pr2"), expr->cache.format, "with its format string");
            assert_eq(std::string("9.75"), expr->cache.text, "and its formatted text");
        } else {
            assert_true(expr->cache.format.empty() && expr->cache.text.empty(), "before 2007 the value has neither (the short AcValue)");
        }
        assert_eq(std::string("9.75"), expr->format, "301");
        assert_eq(std::string("ACFD_FIELD_VALUE"), expr->cache_key, "7");
        // a parent's data is the checksum of its text (6 ACFD_FIELDTEXT_CHECKSUM, an AcValue double)
        const FieldData* parent = nullptr;
        for (const Object* o : fields) if (o->as_field().code == "Sum = %<\\_FldIdx 0>%") parent = &o->as_field();
        assert_true(parent != nullptr, "a parent field");
        assert_eq(1, static_cast<int>(parent->data.size()), "one key");
        assert_eq(std::string("ACFD_FIELDTEXT_CHECKSUM"), parent->data[0].key, "the checksum key");
        assert_true(parent->data[0].value.type == hdxf::TableValueType::Double, "of a double");
        // the FIELDLIST has them all
        std::vector<const Object*> lists = objects_of(d, ObjectType::FieldList);
        assert_eq(1, static_cast<int>(lists.size()), "one FIELDLIST");
        assert_eq(11, static_cast<int>(lists[0]->as_field_list().fields.size()), "of the eleven");
        std::vector<Rec> list_rec = records_named(all, "FIELDLIST");
        int declared = 0;
        for (const Group& g : list_rec[0]) if (g.first == 90) declared = std::atoi(g.second.c_str());
        assert_eq(11, declared, "its 90 is the number of fields");
        // an AcVar field of a file name holds the path as a string value and the file's name as its text
        const FieldData* name = nullptr;
        for (const Object* o : fields) if (o->as_field().evaluator_id == "AcVar" && o->as_field().code.find("Filename") != std::string::npos &&
                                          o->as_field().code.find("fn6") != std::string::npos) name = &o->as_field();
        assert_true(name != nullptr, "the file name field");
        assert_true(name->cache.type == hdxf::TableValueType::String && std::get<std::string>(name->cache.value).find("acad_") != std::string::npos,
                    "its cache is a string");
        if (std::string(tag) != "2000" && std::string(tag) != "2004") assert_eq(std::string("%fn6"), name->cache.format, "format");
        assert_true(name->format_length.has_value() == false || *name->format_length != 0, "98 follows the cache's text");
    }
}

TEST(document_data_reads_the_tables_fields_with_objects_and_the_two_forms_of_the_value) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_tables.dxf");
        Drawing d = load(in, tag);
        std::vector<const Object*> fields = objects_of(d, ObjectType::Field);
        assert_eq(15, static_cast<int>(fields.size()), std::string(tag) + ": the table's fields");
        int with_objects = 0, with_ids = 0, with_two_children = 0;
        for (const Object* o : fields) {
            const FieldData& f = o->as_field();
            if (!f.objects.empty()) {
                ++with_objects;
                assert_eq(1, static_cast<int>(f.objects.size()), "one object (97, 331)");
                assert_true(d.find_object(f.objects[0]) != nullptr || d.find_entity(f.objects[0]) != nullptr, "the object is in the drawing");
            }
            for (const FieldKeyValue& kv : f.data) {
                if (kv.value.type == hdxf::TableValueType::ObjectId) {
                    ++with_ids;
                    assert_eq(std::string("ObjectPropertyId"), kv.key, "the key");
                }
            }
            if (f.child_fields.size() == 2) ++with_two_children;
            // 4 (the format of the code) is only in 2000 and 2004
            const bool older = std::string(tag) == "2000" || std::string(tag) == "2004";
            assert_eq(older, f.format_pre2007.has_value(), std::string(tag) + ": group 4 only before 2007");
        }
        assert_true(with_objects >= 2 && with_ids >= 2, "object-property fields with the object in 331 and as a value");
        assert_true(with_two_children >= 1, "a parent of two children");
    }
}

TEST(document_data_reads_autocads_data_tables_value_by_value) {
    // A DATATABLE with every value type AutoCAD's files show: bool, integer, double, text, 2D point, 3D point, object
    for (const char* tag : kTags) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_native_records.dxf");
        Drawing d = load(in, tag);
        const bool legacy = std::string(tag) == "2000";
        std::vector<const Object*> tables = objects_of(d, legacy ? ObjectType::LegacyDataTable : ObjectType::DataTable);
        const Object* table = nullptr;
        for (const Object* o : tables) if (o->as_data_table().name == "HDXF_TYPED_DATA") table = o;
        assert_true(table != nullptr, std::string(tag) + ": Erik's data table");
        const DataTableData& t = table->as_data_table();
        assert_eq(7, static_cast<int>(t.columns.size()), "seven columns");
        assert_eq(2, t.rows, "two rows");
        assert_eq(2, t.version, "70");
        assert_eq(hdxf::DataColumnType::Boolean, t.columns[0].type);
        assert_eq(std::string("Bool"), t.columns[0].name);
        assert_true(std::get<bool>(t.columns[0].values[0]) && !std::get<bool>(t.columns[0].values[1]), "71 1, 71 0");
        assert_eq(12, std::get<int32_t>(t.columns[1].values[0]), "93 12");
        assert_eq(42, std::get<int32_t>(t.columns[1].values[1]), "93 42");
        assert_eq(1.25, std::get<double>(t.columns[2].values[0]), "40 1.25");
        assert_eq(std::string("First"), std::get<std::string>(t.columns[3].values[0]), "3 First");
        assert_eq(hdxf::DataColumnType::Point2D, t.columns[4].type);
        assert_true(std::get<DataTablePoint2D>(t.columns[4].values[1]).point.y == 4.0, "10 20 30 of the second row");
        assert_eq(hdxf::DataColumnType::Point3D, t.columns[5].type);
        assert_true(std::get<DataTablePoint3D>(t.columns[5].values[1]).point.y == 1.0, "11 21 31 of the second row");
        assert_eq(hdxf::DataColumnType::ObjectId, t.columns[6].type);
        const DataTableObject& object = std::get<DataTableObject>(t.columns[6].values[0]);
        assert_true(object.kind == DataTableObjectKind::ObjectValue, "an object value (331)");
        assert_true(d.find_object(object.handle) != nullptr || d.find_entity(object.handle) != nullptr, "the object is in the drawing");
    }
}

TEST(document_data_the_text_of_a_field_is_what_autocads_text_and_table_cell_show) {
    for (const char* tag : kTags) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_fields.dxf");
        Drawing d = load(in, tag);
        int checked = 0;
        for (const hdxf::Entity& e : d.entities()) {
            if (e.type != hdxf::EntityType::Text || e.props.xdictionary.empty()) continue;
            // the TEXT's extension dictionary: ACAD_FIELD, its dictionary and the entry TEXT: the `_text` field
            const Object* xdict = d.find_object(e.props.xdictionary);
            assert_true(xdict != nullptr && !xdict->as_dictionary().entries.empty(), "the extension dictionary");
            const Object* fields = d.find_object(xdict->as_dictionary().entries[0].handle);
            assert_true(fields != nullptr && !fields->as_dictionary().entries.empty(), "the ACAD_FIELD dictionary");
            const Object* field = d.find_object(fields->as_dictionary().entries[0].handle);
            assert_true(field != nullptr && field->type == ObjectType::Field, "the field");
            // AutoCAD shows (group 1 of the TEXT) the field's code with the texts of its fields in it
            assert_eq(e.as_text().content, d.field_text(*field), std::string(tag) + ": the text shows what its fields were evaluated to");
            ++checked;
        }
        assert_eq(5, checked, "five texts have fields (the sixth is plain)");
        // an object that is not a field shows nothing
        assert_eq(std::string(), d.field_text(d.objects().front()), "not a field");
    }
    // a table's field cells show their fields' text
    for (const char* tag : kTags) {
        std::vector<uint8_t> in = facit_bytes(std::string("acad_") + tag + "_dxf_tables.dxf");
        Drawing d = load(in, tag);
        const hdxf::Entity* table = nullptr;
        for (const hdxf::Entity& e : d.entities()) if (e.type == hdxf::EntityType::Table && !table) table = &e;
        assert_true(table != nullptr, "the table");
        bool seen_length = false;
        for (size_t row = 0; row < table->as_table().rows(); ++row) {
            for (size_t column = 0; column < table->as_table().columns(); ++column) {
                const hdxf::TableCell* cell = table->as_table().cell(row, column);
                if (cell->field_handle.empty()) {
                    assert_eq(hdxf::internal::table_value_text(cell->value), d.table_cell_text(*table, row, column), "a text cell shows its text");
                } else if (d.table_cell_text(*table, row, column) == "100.000") {
                    seen_length = true;
                }
            }
        }
        assert_true(seen_length, std::string(tag) + ": the cell of the length field shows 100.000");
    }
}

// ---------------------------------------------------------------------------
// The version matrix (B101): nine versions x ASCII and binary for each type
// ---------------------------------------------------------------------------

namespace {

Object field_object() {
    FieldData f;
    f.evaluator_id = "AcVar";
    f.code = "\\AcVar Filename \\f \"%fn6\"";
    f.evaluation_option = 63;
    f.state = 59;
    f.evaluation_status = 2;
    FieldKeyValue kv;
    kv.key = "Variable";
    kv.value = TableValue::from_text("Filename");
    f.data.push_back(kv);
    f.cache = TableValue::from_text("C:\\x\\y.dxf");
    f.cache.format = "%fn6";
    f.cache.text = "y.dxf";
    f.format = "y.dxf";
    Object o;
    o.type = ObjectType::Field;
    o.owner = "0";
    o.data = hdxf::Boxed<FieldData>(f);
    return o;
}

Object data_table_object(ObjectType type) {
    DataTableData t;
    t.name = "T";
    DataColumn a;
    a.type = hdxf::DataColumnType::Integer;
    a.name = "n";
    a.values = {DataTableValue(int32_t(1)), DataTableValue(int32_t(2))};
    DataColumn b;
    b.type = hdxf::DataColumnType::Text;
    b.name = "s";
    b.values = {DataTableValue(std::string("x")), DataTableValue(std::string("y"))};
    t.columns = {a, b};
    t.rows = 2;
    Object o;
    o.type = type;
    o.owner = "0";
    o.data = hdxf::Boxed<DataTableData>(t);
    return o;
}

Object vba_object() {
    hdxf::VbaProjectData v;
    for (int i = 0; i < 300; ++i) v.project.push_back(static_cast<uint8_t>(i * 7));
    Object o;
    o.type = ObjectType::VbaProject;
    o.owner = "0";
    o.data = v;
    return o;
}

Object object_ptr_object() {
    Object o;
    o.type = ObjectType::ObjectPtr;
    o.owner = "0";
    o.data = hdxf::ObjectPointerData{};
    hdxf::XdataList x;
    x.app_name = "DC015";
    x.items.push_back({1000, "ASE"});
    x.items.push_back({1070, "7"});
    o.xdata.push_back(x);
    return o;
}

hdxf_test::MatrixReport run_matrix(const char* name, ObjectType type, const std::function<Object()>& make) {
    const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object(name);
    assert_true(codec != nullptr, std::string(name) + " has a codec");
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
        name, codec->versions, [&](Drawing& d) { d.objects().push_back(make()); },
        [&](const Drawing& r, DxfVersion, DxfFormat) {
            int n = 0;
            for (const auto& o : r.objects()) if (o.type == type) ++n;
            return n == 1 ? std::string() : "expected one typed record, found " + std::to_string(n);
        });
    std::cout << name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, "
              << report.not_established << " not established, AutoCAD verified " << report.autocad_verified << ", ej testat "
              << report.autocad_not_tested << ", not savable " << report.autocad_not_savable << "\n";
    return report;
}

} // namespace

TEST(document_data_version_matrix_of_the_field_and_the_field_list) {
    for (const auto& [name, type] : {std::pair<const char*, ObjectType>{"FIELD", ObjectType::Field}, {"FIELDLIST", ObjectType::FieldList}}) {
        hdxf_test::MatrixReport report = run_matrix(name, type, [&]() {
            if (type == ObjectType::Field) return field_object();
            Object o;
            o.type = ObjectType::FieldList;
            o.owner = "0";
            o.data = hdxf::FieldListData{};
            return o;
        });
        assert_eq(12, report.generated, std::string(name) + ": 2000 … 2018, both formats, are made");
        assert_eq(2, report.named_errors, std::string(name) + ": R12 has the named error");
        assert_eq(4, report.not_established, std::string(name) + ": R13 and R14 are not established");
        assert_eq(12, report.autocad_verified, std::string(name) + ": AutoCAD's own files of 2000 … 2018");
        assert_eq(4, report.autocad_not_savable, std::string(name) + ": AutoCAD 2025 cannot save R13/R14");
    }
}

TEST(document_data_version_matrix_of_the_data_tables) {
    hdxf_test::MatrixReport legacy = run_matrix("ACDBDATATABLE", ObjectType::LegacyDataTable, [] { return data_table_object(ObjectType::LegacyDataTable); });
    assert_eq(2, legacy.generated, "ACDBDATATABLE: only 2000, both formats");
    assert_eq(12, legacy.named_errors, "R12 and 2004 … 2018 name the error (DXF 2004 and later write DATATABLE)");
    assert_eq(4, legacy.not_established, "R13 and R14");
    hdxf_test::MatrixReport table = run_matrix("DATATABLE", ObjectType::DataTable, [] { return data_table_object(ObjectType::DataTable); });
    assert_eq(10, table.generated, "DATATABLE: 2004 … 2018, both formats");
    assert_eq(4, table.named_errors, "R12 and 2000 (which names it ACDBDATATABLE) have the named error");
    assert_eq(4, table.not_established, "R13 and R14");
    assert_eq(12, table.autocad_verified, "AutoCAD's own files of 2000 (as ACDBDATATABLE) … 2018");
    // the named errors say what AutoCAD does
    Drawing d(DxfVersion::R2000);
    d.objects().push_back(data_table_object(ObjectType::DataTable));
    assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "a DATATABLE is not made as 2000");
    assert_eq(std::string("DATATABLE is written as ACDBDATATABLE in DXF 2000"), d.last_error());
    Drawing e(DxfVersion::R2018);
    e.objects().push_back(data_table_object(ObjectType::LegacyDataTable));
    assert_true(e.save_to_memory(DxfFormat::Ascii).empty(), "an ACDBDATATABLE is not made as 2018");
    assert_eq(std::string("ACDBDATATABLE is DXF 2000's record: DXF 2004 and later write DATATABLE"), e.last_error());
}

TEST(document_data_version_matrix_of_object_ptr_and_vba_project) {
    // No AutoCAD file shows either (F9.22): the form is Autodesk's reference, generated in 2000 … 2018, ej testat
    hdxf_test::MatrixReport ptr = run_matrix("OBJECT_PTR", ObjectType::ObjectPtr, object_ptr_object);
    hdxf_test::MatrixReport vba = run_matrix("VBA_PROJECT", ObjectType::VbaProject, vba_object);
    for (const hdxf_test::MatrixReport* report : {&ptr, &vba}) {
        assert_eq(12, report->generated, "2000 … 2018, both formats");
        assert_eq(2, report->named_errors, "R12");
        assert_eq(4, report->not_established, "R13 and R14");
        assert_eq(0, report->autocad_verified, "no AutoCAD file");
        assert_eq(14, report->autocad_not_tested, "ej testat (F9.22): 2000 … 2018 and the R12 cells");
    }
}

// ---------------------------------------------------------------------------
// A new record: the values, the lists, the counts
// ---------------------------------------------------------------------------

namespace {

Drawing reload(const std::vector<uint8_t>& bytes) {
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
    return d;
}

std::string long_text(size_t units) {
    std::string s;
    for (size_t i = 0; i < units; ++i) s += static_cast<char>('a' + i % 26);
    return s;
}

} // namespace

TEST(document_data_a_new_field_is_written_and_read_back_in_every_version_and_format) {
    for (DxfVersion v : kVersions) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            Object o = field_object();
            FieldData& field = o.as_field();
            // text that looks like markers, a long code, UTF-8 above the BMP, a key that is a marker, every value type
            field.code = "ACVALUE_END " + long_text(600) + " \xE2\x82\xAC \xF0\x9F\x98\x80";
            field.format = long_text(520);
            field.error_message = "ACVALUE_END";
            FieldKeyValue k1;
            k1.key = "ACVALUE_END";
            k1.value = TableValue::from_long(42, "%d", "42");
            FieldKeyValue k2;
            k2.key = "CELL";
            k2.value = TableValue::from_double(2.5, "%lu2%pr1", "2.5");
            FieldKeyValue k3;
            k3.key = "Id";
            k3.value.type = hdxf::TableValueType::ObjectId;
            k3.value.flags = 6;
            k3.value.value = hdxf::TableHandle{"0"};
            FieldKeyValue k4;
            k4.key = "Empty";
            field.data.clear();
            field.data.push_back(k1);
            field.data.push_back(k2);
            field.data.push_back(k3);
            field.data.push_back(k4);
            field.child_fields.clear();
            field.objects.clear();
            if (v < DxfVersion::R2007) field.format_pre2007 = "%lu2%pr3";
            d.objects().push_back(o);
            std::vector<uint8_t> bytes = save(d, f, std::string(hdxf_test::version_label(v)) + " " + hdxf_test::format_label(f));
            Drawing r = reload(bytes);
            std::vector<const Object*> back = objects_of(r, ObjectType::Field);
            assert_eq(1, static_cast<int>(back.size()), "one field");
            const FieldData& g = back[0]->as_field();
            assert_eq(field.code, g.code, "the long code comes back whole (2 and 3)");
            assert_eq(field.format, g.format, "the long format comes back whole (301 and 9)");
            assert_eq(std::string("ACVALUE_END"), g.error_message, "marker text is text");
            assert_eq(4, static_cast<int>(g.data.size()), std::string("four keys in ") + hdxf_test::version_label(v) + " " + hdxf_test::format_label(f));
            assert_eq(std::string("ACVALUE_END"), g.data[0].key, "a key that is a marker");
            // DXF 2007 and later: the whole AcValue (flags, unit, format, text). Before it only the type and the value
            auto same_ac = [&](const TableValue& a, const TableValue& b) {
                if (v >= DxfVersion::R2007) return a == b;
                if (a.type != b.type) return false;
                if (!a.has_value() || !b.has_value()) return true;
                if (const int32_t* x = std::get_if<int32_t>(&a.value)) return std::get_if<int32_t>(&b.value) && *x == *std::get_if<int32_t>(&b.value);
                if (const double* x = std::get_if<double>(&a.value)) return std::get_if<double>(&b.value) && *x == *std::get_if<double>(&b.value);
                if (const std::string* x = std::get_if<std::string>(&a.value)) return std::get_if<std::string>(&b.value) && *x == *std::get_if<std::string>(&b.value);
                return true;
            };
            assert_true(same_ac(g.data[0].value, k1.value), "a long value");
            assert_true(same_ac(g.data[1].value, k2.value), "a double value");
            assert_true(g.data[2].value.type == hdxf::TableValueType::ObjectId, "an object id value");
            assert_true(same_ac(g.data[3].value, k4.value), "an unset value");
            assert_eq(v < DxfVersion::R2007, g.format_pre2007.has_value(), "group 4 only before 2007");
            assert_true(same_ac(g.cache, field.cache), "the cache");
            // 98 is the length of the format when the file has no other
            assert_false(g.format_length.has_value(), "98 is the format's length");
            std::vector<uint8_t> again = save(r, f, "again");
            assert_true(again == bytes, "written again unchanged");
        }
    }
}

TEST(document_data_a_changed_field_changes_what_it_is_in_the_file) {
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_fields.dxf");
    Drawing d = load(in, "2018");
    Object* sum = nullptr;
    for (Object& o : d.objects()) if (o.type == ObjectType::Field && o.as_field().code == "Sum = %<\\_FldIdx 0>%") sum = &o;
    assert_true(sum != nullptr, "the field");
    // the code, the status and the error change; the others stay as they were
    sum->as_field().code = "Total = %<\\_FldIdx 0>%";
    sum->as_field().evaluation_status = 16;
    sum->as_field().error_code = 600;
    sum->as_field().error_message = "not found";
    const std::string handle = sum->handle;
    std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "changed");
    std::vector<Rec> before = ascii_records(in), after = ascii_records(out);
    Rec a = record_with_handle(before, "FIELD", handle), b = record_with_handle(after, "FIELD", handle);
    assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), "the same number of groups");
    int differing = 0;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++differing;
    assert_eq(4, differing, "the code (2), the status (95), the error code (96) and the message (300) differ");
    // every other FIELD is the record it was
    for (const Object& o : d.objects()) {
        if (o.type != ObjectType::Field || o.handle == handle) continue;
        assert_eq(std::string(), same_record(record_with_handle(before, "FIELD", o.handle), record_with_handle(after, "FIELD", o.handle)),
                  "FIELD " + o.handle + " is as it was");
    }
    // a field's code grows to need the overflow group and shrinks again
    Drawing r = reload(out);
    for (Object& o : r.objects()) if (o.handle == handle) o.as_field().code = "Total " + long_text(700);
    std::vector<uint8_t> longer = save(r, DxfFormat::Ascii, "longer");
    Drawing r2 = reload(longer);
    assert_eq(std::string("Total ") + long_text(700), r2.find_object(handle)->as_field().code, "the long code, in 2 and 3");
    for (Object& o : r2.objects()) if (o.handle == handle) o.as_field().code = "short";
    Drawing r3 = reload(save(r2, DxfFormat::Ascii, "shorter"));
    assert_eq(std::string("short"), r3.find_object(handle)->as_field().code, "and short again");
    // counters follow the lists: a child added to a field that has none
    for (Object& o : r3.objects()) if (o.handle == handle) { o.as_field().objects.push_back(handle); }
    Drawing r4 = reload(save(r3, DxfFormat::Ascii, "object added"));
    assert_eq(1, static_cast<int>(r4.find_object(handle)->as_field().objects.size()), "97 and one 331");
}

TEST(document_data_a_data_table_of_every_value_type_is_written_and_read_back) {
    for (DxfVersion v : kVersions) {
        const bool legacy = v == DxfVersion::R2000;
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            DataTableData t;
            t.name = "values";
            t.version = 2;
            auto column = [&](int32_t type, const std::string& name, std::vector<DataTableValue> values) {
                DataColumn c;
                c.type = type;
                c.name = name;
                c.values = std::move(values);
                t.columns.push_back(std::move(c));
            };
            column(hdxf::DataColumnType::Boolean, "b", {DataTableValue(true), DataTableValue(false), DataTableValue(true)});
            column(hdxf::DataColumnType::Integer, "i", {DataTableValue(int32_t(-5)), DataTableValue(int32_t(0)), DataTableValue(int32_t(2147483647))});
            column(hdxf::DataColumnType::Double, "d", {DataTableValue(0.1), DataTableValue(-2.5e10), DataTableValue(1.0 / 3.0)});
            column(hdxf::DataColumnType::Text, "s",
                   {DataTableValue(std::string("")), DataTableValue(std::string("ACVALUE_END")), DataTableValue(std::string("\xE2\x82\xAC \xF0\x9F\x98\x80") + long_text(400))});
            column(hdxf::DataColumnType::Point2D, "p", {DataTableValue(DataTablePoint2D{{1, 2, 3}}), DataTableValue(DataTablePoint2D{{0, 0, 0}}), DataTableValue(DataTablePoint2D{{-1.5, 2.5, 3.5}})});
            column(hdxf::DataColumnType::Point3D, "v", {DataTableValue(DataTablePoint3D{{1, 2, 3}}), DataTableValue(DataTablePoint3D{{0, 0, 1}}), DataTableValue(DataTablePoint3D{{4, 5, 6}})});
            column(hdxf::DataColumnType::ObjectId, "o",
                   {DataTableValue(DataTableObject{DataTableObjectKind::ObjectValue, "0"}), DataTableValue(DataTableObject{DataTableObjectKind::ObjectValue, "0"}),
                    DataTableValue(DataTableObject{DataTableObjectKind::ObjectValue, "0"})});
            // the other pointer kinds the reference lists, in a column of a type number HDxf has not seen (kept as it is)
            column(77, "other", {DataTableValue(DataTableObject{DataTableObjectKind::SoftOwner, "0"}), DataTableValue(DataTableObject{DataTableObjectKind::SoftOwner, "0"}),
                                 DataTableValue(DataTableObject{DataTableObjectKind::SoftOwner, "0"})});
            t.rows = 3;
            d.add_data_table(t, "0");
            std::vector<uint8_t> bytes = save(d, f, std::string(hdxf_test::version_label(v)));
            Drawing r = reload(bytes);
            std::vector<const Object*> back = objects_of(r, legacy ? ObjectType::LegacyDataTable : ObjectType::DataTable);
            assert_eq(1, static_cast<int>(back.size()), "the data table, under the name the version has");
            const DataTableData& g = back[0]->as_data_table();
            assert_eq(8, static_cast<int>(g.columns.size()), "eight columns (90)");
            assert_eq(3, g.rows, "three rows (91)");
            for (size_t i = 0; i < t.columns.size(); ++i) {
                assert_eq(t.columns[i].type, g.columns[i].type, "the column type");
                assert_eq(t.columns[i].name, g.columns[i].name, "the name");
                assert_eq(3, static_cast<int>(g.columns[i].values.size()), "one value for each row");
                for (size_t k = 0; k < 3; ++k) assert_true(t.columns[i].values[k] == g.columns[i].values[k], "column " + t.columns[i].name + " row " + std::to_string(k));
            }
            assert_true(save(r, f, "again") == bytes, "written again unchanged");
        }
    }
}

TEST(document_data_a_data_table_changes_by_column_row_and_type) {
    std::vector<uint8_t> in = facit_bytes("acad_2018_dxf_native_records.dxf");
    Drawing d = load(in, "2018");
    Object* table = nullptr;
    for (Object& o : d.objects()) if (o.type == ObjectType::DataTable && o.as_data_table().name == "HDXF_TYPED_DATA") table = &o;
    assert_true(table != nullptr, "the table");
    const std::string handle = table->handle;
    DataTableData& t = table->as_data_table();
    // a value of one column changes; the others stay
    t.columns[1].values[1] = DataTableValue(int32_t(99));
    // a row is added to every column (91 follows), a column is added (90 follows), a column is renamed
    for (DataColumn& c : t.columns) c.values.push_back(c.values[0]);
    DataColumn extra;
    extra.type = hdxf::DataColumnType::Integer;
    extra.name = "Extra";
    extra.values = {DataTableValue(int32_t(1)), DataTableValue(int32_t(2)), DataTableValue(int32_t(3))};
    t.columns.push_back(extra);
    t.columns[0].name = "Flag";
    Drawing r = reload(save(d, DxfFormat::Ascii, "changed"));
    const DataTableData& g = r.find_object(handle)->as_data_table();
    assert_eq(8, static_cast<int>(g.columns.size()), "90 follows");
    assert_eq(3, g.rows, "91 follows the rows");
    assert_eq(std::string("Flag"), g.columns[0].name, "renamed");
    assert_eq(99, std::get<int32_t>(g.columns[1].values[1]), "the changed value");
    assert_eq(12, std::get<int32_t>(g.columns[1].values[0]), "the unchanged one");
    // a column with fewer values than the first is refused: every column has one value for each row
    Drawing bad = reload(save(d, DxfFormat::Ascii, "again"));
    bad.find_object(handle)->as_data_table().columns[2].values.pop_back();
    assert_true(bad.save_to_memory(DxfFormat::Ascii).empty(), "a column of another length is refused");
    assert_true(bad.last_error().find("every column has one value for each row") != std::string::npos, bad.last_error());
    // a value of another type than its column's is refused
    Drawing wrong = reload(save(d, DxfFormat::Ascii, "again"));
    wrong.find_object(handle)->as_data_table().columns[1].values[0] = DataTableValue(std::string("not an integer"));
    assert_true(wrong.save_to_memory(DxfFormat::Ascii).empty(), "a value of another type than its column's is refused");
    assert_true(wrong.last_error().find("column") != std::string::npos, wrong.last_error());
}

// ---------------------------------------------------------------------------
// OBJECT_PTR and VBA_PROJECT
// ---------------------------------------------------------------------------

TEST(document_data_a_vba_project_keeps_its_bytes_and_a_wrong_length_is_not_typed) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2007, DxfVersion::R2018}) {
        for (size_t size : {size_t(0), size_t(1), size_t(127), size_t(128), size_t(1000), size_t(5000)}) {
            Drawing d(v);
            Object o = vba_object();
            o.as_vba_project().project.clear();
            for (size_t i = 0; i < size; ++i) o.as_vba_project().project.push_back(static_cast<uint8_t>((i * 31 + 7) & 0xFF));
            d.objects().push_back(o);
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                std::vector<uint8_t> bytes = save(d, f, "vba");
                Drawing r = reload(bytes);
                std::vector<const Object*> back = objects_of(r, ObjectType::VbaProject);
                assert_eq(1, static_cast<int>(back.size()), "typed");
                assert_true(back[0]->as_vba_project().project == o.as_vba_project().project, "the bytes are intact (" + std::to_string(size) + ")");
                assert_true(save(r, f, "again") == bytes, "written again unchanged");
            }
        }
    }
}

TEST(document_data_object_ptr_is_the_head_and_its_ase_xdata_and_nothing_else) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2018}) {
        Drawing d(v);
        d.objects().push_back(object_ptr_object());
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            std::vector<uint8_t> bytes = save(d, f, "object_ptr");
            Drawing r = reload(bytes);
            std::vector<const Object*> back = objects_of(r, ObjectType::ObjectPtr);
            assert_eq(1, static_cast<int>(back.size()), "typed");
            assert_eq(1, static_cast<int>(back[0]->xdata.size()), "the XDATA is kept");
            assert_eq(std::string("DC015"), back[0]->xdata[0].app_name, "the ASE application");
            assert_eq(2, static_cast<int>(back[0]->xdata[0].items.size()), "its groups");
            assert_true(save(r, f, "again") == bytes, "written again unchanged");
        }
    }
    // a record with more than the head and the XDATA is not the reference's record: kept, with a warning
    Drawing d(DxfVersion::R2000);
    d.add_line({0, 0, 0}, {1, 1, 0});
    std::string text = hdxf_test::lf_text(save(d, DxfFormat::Ascii, "line"));
    size_t at = text.rfind("  0\nENDSEC\n  0\nEOF");
    std::string record = "  0\nOBJECT_PTR\n  5\nFFF\n330\nC\n100\nAcDbSomething\n 70\n1\n";
    std::string with = text.substr(0, at) + record + text.substr(at);
    std::vector<uint8_t> bytes(with.begin(), with.end());
    Drawing r;
    assert_true(r.load_from_memory(bytes.data(), bytes.size()), r.last_error());
    assert_true(objects_of(r, ObjectType::ObjectPtr).empty(), "a record with a subclass is not typed");
    bool warned = false;
    for (const std::string& w : r.warnings()) warned = warned || w.find("OBJECT_PTR") != std::string::npos;
    assert_true(warned, "and there is a warning that names it");
}

// ---------------------------------------------------------------------------
// Odd and cut records: the counts are checked before anything is allocated (B105)
// ---------------------------------------------------------------------------

namespace {

std::string drawing_text_with(const std::string& record) {
    Drawing d(DxfVersion::R2000);
    d.add_line({0, 0, 0}, {1, 1, 0});
    std::string text = hdxf_test::lf_text(d.save_to_memory(DxfFormat::Ascii));
    size_t at = text.rfind("  0\nENDSEC\n  0\nEOF");
    return text.substr(0, at) + record + text.substr(at);
}

// A drawing with `record` among its objects, read; the warnings say what happened to it
std::vector<std::string> read_with(const std::string& record, size_t* typed_of, ObjectType type) {
    std::string text = drawing_text_with(record);
    std::vector<uint8_t> bytes(text.begin(), text.end());
    Drawing r;
    assert_true(r.load_from_memory(bytes.data(), bytes.size()), r.last_error());
    *typed_of = objects_of(r, type).size();
    return r.warnings();
}

bool mentions(const std::vector<std::string>& warnings, const std::string& text) {
    for (const std::string& w : warnings) if (w.find(text) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(document_data_cut_and_impossible_records_stay_as_read_with_a_warning) {
    size_t typed = 0;
    // a FIELD that says 999999 children and has none
    std::vector<std::string> w = read_with("  0\nFIELD\n  5\nFFF\n330\nC\n100\nAcDbField\n  1\nAcVar\n  2\ncode\n 90\n999999\n 97\n0\n", &typed, ObjectType::Field);
    assert_eq(0, static_cast<int>(typed), "not typed");
    assert_true(mentions(w, "FIELD FFF"), "the warning names it");
    // a FIELD that ends in the middle (no cache)
    w = read_with("  0\nFIELD\n  5\nFFF\n330\nC\n100\nAcDbField\n  1\nAcVar\n  2\ncode\n 90\n0\n 97\n0\n 91\n63\n 92\n0\n 94\n0\n 95\n0\n 96\n0\n300\n\n 93\n0\n", &typed, ObjectType::Field);
    assert_eq(0, static_cast<int>(typed), "a field without its cache is not typed");
    assert_true(mentions(w, "FIELD FFF"), "and says so");
    // a FIELDLIST without its 90
    w = read_with("  0\nFIELDLIST\n  5\nFFF\n330\nC\n100\nAcDbIdSet\n330\n1\n100\nAcDbFieldList\n", &typed, ObjectType::FieldList);
    assert_eq(0, static_cast<int>(typed), "a list without 90 is not typed");
    // a DATATABLE with a number of rows the groups cannot hold, and a column that is cut
    w = read_with("  0\nDATATABLE\n  5\nFFF\n330\nC\n100\nAcDbDataTable\n 70\n2\n 90\n1\n 91\n2000000000\n  1\nT\n 92\n1\n  2\nn\n 93\n1\n", &typed, ObjectType::DataTable);
    assert_eq(0, static_cast<int>(typed), "rows beyond the groups are not typed (and nothing is allocated for them)");
    assert_true(mentions(w, "DATATABLE FFF"), "named");
    w = read_with("  0\nDATATABLE\n  5\nFFF\n330\nC\n100\nAcDbDataTable\n 70\n2\n 90\n1\n 91\n3\n  1\nT\n 92\n1\n  2\nn\n 93\n1\n 93\n2\n", &typed, ObjectType::DataTable);
    assert_eq(0, static_cast<int>(typed), "a column with a row too few is not typed");
    // a value in a group no column has
    w = read_with("  0\nDATATABLE\n  5\nFFF\n330\nC\n100\nAcDbDataTable\n 70\n2\n 90\n1\n 91\n1\n  1\nT\n 92\n1\n  2\nn\n 42\n7\n", &typed, ObjectType::DataTable);
    assert_eq(0, static_cast<int>(typed), "an unknown value group is not typed");
    // a DATATABLE with no columns is valid and typed
    w = read_with("  0\nDATATABLE\n  5\nFFF\n330\nC\n100\nAcDbDataTable\n 70\n2\n 90\n0\n 91\n0\n  1\nT\n", &typed, ObjectType::DataTable);
    assert_eq(1, static_cast<int>(typed), "no columns, no rows: typed");
    // a data table of a later version: groups after the ones HDxf knows are kept with a warning
    w = read_with("  0\nDATATABLE\n  5\nFFF\n330\nC\n100\nAcDbDataTable\n 70\n3\n 90\n0\n 91\n0\n  1\nT\n 42\n7\n", &typed, ObjectType::DataTable);
    assert_eq(1, static_cast<int>(typed), "typed");
    assert_true(mentions(w, "DATATABLE FFF"), "with the warning that names it");
    // the minimal ones: OBJECT_PTR with its head is typed; VBA_PROJECT without its subclass is not
    w = read_with("  0\nOBJECT_PTR\n  5\nFFF\n330\nC\n", &typed, ObjectType::ObjectPtr);
    assert_eq(1, static_cast<int>(typed), "OBJECT_PTR");
    w = read_with("  0\nVBA_PROJECT\n  5\nFFF\n330\nC\n", &typed, ObjectType::VbaProject);
    assert_eq(0, static_cast<int>(typed), "VBA_PROJECT without AcDbVbaProject is not typed");
    assert_true(mentions(w, "VBA_PROJECT FFF"), "named");
    w = read_with("  0\nVBA_PROJECT\n  5\nFFF\n330\nC\n100\nAcDbVbaProject\n 90\n10\n310\n01020304\n", &typed, ObjectType::VbaProject);
    assert_eq(0, static_cast<int>(typed), "90 says 10 bytes, 310 holds 4: not typed");
    assert_true(mentions(w, "VBA_PROJECT FFF"), "named");
}

TEST(document_data_r12_r13_and_r14_have_a_named_error_or_no_claim) {
    for (DxfVersion v : {DxfVersion::R12}) {
        Drawing d(v);
        d.objects().push_back(field_object());
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "FIELD is not in R12");
        assert_eq(std::string("FIELD requires DXF 2000 or later"), d.last_error());
    }
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14}) {
        Drawing d(v);
        d.objects().push_back(field_object());
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "a new FIELD is not made in R13/R14 (no file shows it)");
        assert_true(d.last_error().find("FIELD") != std::string::npos, d.last_error());
    }
}

// ---------------------------------------------------------------------------
// The pre-2007 round trip of a table (DXF 2000 and 2004)
// ---------------------------------------------------------------------------

namespace {

struct RoundTrip {
    const Object* dictionary = nullptr;
    const Object* xrecord = nullptr;
    const Object* data_table = nullptr;
};

RoundTrip round_trip_of(const Drawing& d, const hdxf::Entity& table) {
    RoundTrip rt;
    if (table.props.xdictionary.empty()) return rt;
    rt.dictionary = d.find_object(table.props.xdictionary);
    rt.data_table = d.table_round_trip_data(table);
    if (rt.dictionary) {
        for (const auto& e : rt.dictionary->as_dictionary().entries) {
            if (e.name == "ACAD_XREC_ROUNDTRIP") rt.xrecord = d.find_object(e.handle);
        }
    }
    return rt;
}

const hdxf::Entity* first_table(const Drawing& d, size_t index = 0) {
    size_t n = 0;
    for (const auto& e : d.entities()) {
        if (e.type == hdxf::EntityType::Table && n++ == index) return &e;
    }
    return nullptr;
}

std::string group_text(const std::vector<hdxf::DxfGroup>& groups) {
    std::string s;
    for (const auto& g : groups) s += std::to_string(g.code) + "=" + trim(g.value) + ";";
    return s;
}

} // namespace

TEST(document_data_a_new_2000_and_2004_table_has_what_autocads_empty_table_has) {
    // fas9_tables.scr: `-TABLE 4 columns, 7 rows` is the 9 rows AutoCAD gives it (a title and a header row), and then 3 x 3 (5 rows)
    for (const char* tag : {"2000", "2004"}) {
        const DxfVersion v = std::string(tag) == "2000" ? DxfVersion::R2000 : DxfVersion::R2004;
        std::vector<uint8_t> file = facit_bytes(std::string("acad_") + tag + "_dxf_table_empty.dxf");
        Drawing autocad = load(file, tag);
        const hdxf::Entity* at = first_table(autocad);
        assert_true(at != nullptr, "AutoCAD's table");
        RoundTrip want = round_trip_of(autocad, *at);
        assert_true(want.dictionary && want.xrecord && want.data_table, "AutoCAD's round-trip records");
        assert_true(autocad.table_content(*at) == nullptr && autocad.table_geometry(*at) == nullptr, "an empty table has no content and no geometry");

        Drawing d(v);
        hdxf::Entity& t = d.add_table(at->as_table().insertion, at->as_table().rows(), at->as_table().columns(), 1.0, 1.0);
        t.as_table().row_heights = at->as_table().row_heights;
        t.as_table().column_widths = at->as_table().column_widths;
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        Drawing r = reload(out);
        const hdxf::Entity* nt = first_table(r);
        assert_true(nt != nullptr, "our table");
        RoundTrip got = round_trip_of(r, *nt);
        assert_true(got.dictionary && got.xrecord && got.data_table, std::string(tag) + ": an empty table has the round-trip dictionary, XRECORD and data table");
        assert_true(r.table_content(*nt) == nullptr, "and no content");
        // the extension dictionary, the XRECORD and the data table are AutoCAD's records (handles aside)
        std::vector<Rec> a = ascii_records(file), b = ascii_records(out);
        auto masked = [](Rec rec, const std::set<std::string>& drop) {
            Rec out_rec;
            for (const Group& g : rec) {
                if (g.first == 5 || g.first == 330 || g.first == 360) continue;
                (void)drop;
                out_rec.push_back(g);
            }
            return out_rec;
        };
        assert_eq(std::string(), same_record(masked(record_with_handle(a, "DICTIONARY", want.dictionary->handle), {}), masked(record_with_handle(b, "DICTIONARY", got.dictionary->handle), {})),
                  "the extension dictionary");
        assert_eq(std::string(), same_record(masked(record_with_handle(a, "XRECORD", want.xrecord->handle), {}), masked(record_with_handle(b, "XRECORD", got.xrecord->handle), {})),
                  "the round-trip XRECORD (its rows and columns)");
        const std::string name = std::string(tag) == "2000" ? "ACDBDATATABLE" : "DATATABLE";
        assert_eq(std::string(), same_record(masked(record_with_handle(a, name, want.data_table->handle), {}), masked(record_with_handle(b, name, got.data_table->handle), {})),
                  "the data table (columns 2, 1, 1, 6 of the cells' checksums, two zeros and the XRECORDs)");
        // each cell's XRECORD: AutoCAD's empty cell
        const DataColumn& cells = got.data_table->as_data_table().columns[3];
        assert_eq(static_cast<int>(at->as_table().cells.size()), static_cast<int>(cells.values.size()), "a record for each cell");
        for (const DataTableValue& value : cells.values) {
            const Object* x = r.find_object(std::get<DataTableObject>(value).handle);
            assert_true(x != nullptr && x->type == ObjectType::XRecord, "an XRECORD");
            assert_eq(got.data_table->handle, x->owner, "owned by the data table");
            assert_eq(std::string("93=7;90=0;94=0;300=;302=;"), group_text(x->as_xrecord().data), "an empty cell: 93 7, 90 0, 94 0, 300, 302");
        }
        // the table is written again unchanged
        assert_true(save(r, DxfFormat::Ascii, "again") == out, "written again unchanged");
        // the typed ACDBDATATABLE/DATATABLE of AutoCAD's own file is the one HDxf made, value for value
        const DataTableData& x = want.data_table->as_data_table();
        const DataTableData& y = got.data_table->as_data_table();
        assert_eq(static_cast<int>(x.columns.size()), static_cast<int>(y.columns.size()), "four columns");
        for (size_t i = 0; i < 3; ++i) {
            assert_eq(x.columns[i].type, y.columns[i].type, "the column type");
            for (size_t k = 0; k < x.columns[i].values.size(); ++k) assert_true(x.columns[i].values[k] == y.columns[i].values[k], "the values");
        }
        // the ownership AutoCAD's file has: the table owns the dictionary, the dictionary the XRECORD, the XRECORD the data table
        assert_eq(nt->props.handle, got.dictionary->owner, "the table's dictionary");
        assert_eq(got.dictionary->handle, got.xrecord->owner, "the XRECORD's owner");
        assert_eq(got.xrecord->handle, got.data_table->owner, "the data table's owner");
    }
}

TEST(document_data_a_2000_and_2004_table_with_text_has_the_content_the_geometry_and_the_cells_data) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004}) {
        Drawing d(v);
        hdxf::Entity& t = d.add_table({0, 0, 0}, 4, 3, 5.0, 20.0);
        d.set_table_cell_text(t, 0, 0, "Title");
        d.set_table_cell_text(t, 1, 0, "Item");
        d.set_table_cell_text(t, 1, 1, "Count");
        d.set_table_cell_text(t, 2, 1, "7");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, "table");
        Drawing r = reload(out);
        const hdxf::Entity* nt = first_table(r);
        RoundTrip rt = round_trip_of(r, *nt);
        assert_true(rt.xrecord && rt.data_table, "the round-trip records");
        assert_true(r.table_content(*nt) != nullptr && r.table_geometry(*nt) != nullptr, "the content and the geometry too, as AutoCAD's own table with text has them");
        // the XRECORD has the 2008 part and then the pre-2007 part (AutoCAD's order)
        const auto& groups = rt.xrecord->as_xrecord().data;
        assert_eq(std::string("ACAD_ROUNDTRIP_2008_TABLE_ENTITY"), groups[0].value, "the 2008 part first");
        size_t rows = 0, columns = 0;
        assert_true(hdxf::internal::pre2007_table_size(rt.xrecord->as_xrecord(), rows, columns), "the pre-2007 part");
        assert_eq(4, static_cast<int>(rows), "its rows");
        assert_eq(3, static_cast<int>(columns), "its columns");
        assert_eq(rt.data_table->handle, hdxf::internal::pre2007_data_table_handle(rt.xrecord->as_xrecord()), "and the data table it names");
        // the checksum column is the text's checksum (the same formula as the content's)
        const DataColumn& sums = rt.data_table->as_data_table().columns[0];
        assert_eq(12, static_cast<int>(sums.values.size()), "a checksum for each cell");
        assert_eq(hdxf::internal::table_text_checksum("Title"), std::get<double>(sums.values[0]), "Title");
        assert_eq(hdxf::internal::table_text_checksum("Item"), std::get<double>(sums.values[3]), "Item");
        assert_eq(hdxf::internal::table_text_checksum("7"), std::get<double>(sums.values[7]), "7");
        assert_eq(0.0, std::get<double>(sums.values[8]), "an empty cell");
        // each cell's XRECORD is the cell's value as AutoCAD's own: 93 6, 90 4 (text), 1 text, 94 0, 300, 302
        const DataColumn& owned = rt.data_table->as_data_table().columns[3];
        const Object* item = r.find_object(std::get<DataTableObject>(owned.values[3]).handle);
        assert_eq(std::string("93=6;90=4;1=Item;94=0;300=;302=Item;"), group_text(item->as_xrecord().data), "the XRECORD of a text cell");
        // saved again unchanged
        assert_true(save(r, DxfFormat::Ascii, "again") == out, "written again unchanged");
        // a cell's text changes: its checksum and its XRECORD follow, the others are as they were
        Drawing c = reload(out);
        hdxf::Entity* ct = nullptr;
        for (auto& e : c.entities()) if (e.type == hdxf::EntityType::Table) ct = &e;
        const std::string item_xrecord = item->handle;
        c.set_table_cell_text(*ct, 1, 0, "Changed");
        Drawing c2 = reload(save(c, DxfFormat::Ascii, "changed"));
        const hdxf::Entity* c2t = first_table(c2);
        RoundTrip rt2 = round_trip_of(c2, *c2t);
        assert_eq(hdxf::internal::table_text_checksum("Changed"), std::get<double>(rt2.data_table->as_data_table().columns[0].values[3]), "the checksum follows");
        assert_eq(std::string("93=6;90=4;1=Changed;94=0;300=;302=Changed;"), group_text(c2.find_object(item_xrecord)->as_xrecord().data), "and the cell's XRECORD");
        assert_eq(hdxf::internal::table_text_checksum("Count"), std::get<double>(rt2.data_table->as_data_table().columns[0].values[4]), "another cell is as it was");
        // a row is added: the data table is made anew from the cells (the row-by-row order moved), the old XRECORDs go
        Drawing a = reload(out);
        hdxf::Entity* at = nullptr;
        for (auto& e : a.entities()) if (e.type == hdxf::EntityType::Table) at = &e;
        hdxf::TableData& td = at->as_table();
        td.row_heights.push_back(5.0);
        for (size_t col = 0; col < td.columns(); ++col) {
            hdxf::TableCell cell = td.cells[col];
            cell.value = TableValue::from_text("row " + std::to_string(col));
            td.cells.push_back(cell);
        }
        const size_t objects_before = a.objects().size();
        Drawing a2 = reload(save(a, DxfFormat::Ascii, "row added"));
        RoundTrip rt3 = round_trip_of(a2, *first_table(a2));
        assert_eq(15, static_cast<int>(rt3.data_table->as_data_table().columns[0].values.size()), "15 cells (91)");
        assert_eq(15, static_cast<int>(rt3.data_table->as_data_table().columns[3].values.size()), "and 15 XRECORDs");
        rows = 0;
        hdxf::internal::pre2007_table_size(rt3.xrecord->as_xrecord(), rows, columns);
        assert_eq(5, static_cast<int>(rows), "the XRECORD says 5 rows");
        assert_eq(static_cast<int>(objects_before) + 3, static_cast<int>(a2.objects().size()), "three XRECORDs more, none left over");
    }
}

TEST(document_data_an_empty_autocad_table_gets_its_content_when_a_cell_gets_text) {
    for (const char* tag : {"2000", "2004"}) {
        std::vector<uint8_t> file = facit_bytes(std::string("acad_") + tag + "_dxf_table_empty.dxf");
        Drawing d = load(file, tag);
        hdxf::Entity* table = nullptr;
        for (auto& e : d.entities()) if (e.type == hdxf::EntityType::Table && !table) table = &e;
        assert_true(table != nullptr, "AutoCAD's empty table");
        d.set_table_cell_text(*table, 2, 1, "x");
        Drawing r = reload(save(d, DxfFormat::Ascii, "text"));
        const hdxf::Entity* nt = first_table(r);
        assert_true(r.table_content(*nt) != nullptr && r.table_geometry(*nt) != nullptr, "the content and the geometry are made as the cell has content");
        RoundTrip rt = round_trip_of(r, *nt);
        assert_eq(std::string("ACAD_ROUNDTRIP_2008_TABLE_ENTITY"), rt.xrecord->as_xrecord().data[0].value, "the 2008 part is before the pre-2007 part");
        assert_eq(hdxf::internal::table_text_checksum("x"), std::get<double>(rt.data_table->as_data_table().columns[0].values[2 * nt->as_table().columns() + 1]), "the checksum of the cell");
        // an untouched second table of the same drawing is as it was
        const hdxf::Entity* second = first_table(r, 1);
        assert_true(second != nullptr && r.table_content(*second) == nullptr, "the other table is as it was");
    }
}

TEST(document_data_a_removed_table_takes_its_round_trip_records_with_it) {
    for (const char* tag : {"2000", "2004"}) {
        std::vector<uint8_t> file = facit_bytes(std::string("acad_") + tag + "_dxf_table_empty.dxf");
        Drawing d = load(file, tag);
        const hdxf::Entity* table = first_table(d);
        RoundTrip rt = round_trip_of(d, *table);
        std::set<std::string> gone = {rt.dictionary->handle, rt.xrecord->handle, rt.data_table->handle};
        for (const DataTableValue& v : rt.data_table->as_data_table().columns[3].values) gone.insert(std::get<DataTableObject>(v).handle);
        const std::string handle = table->props.handle;
        auto& entities = d.entities();
        entities.erase(std::remove_if(entities.begin(), entities.end(), [&](const hdxf::Entity& e) { return e.props.handle == handle; }), entities.end());
        Drawing r = reload(save(d, DxfFormat::Ascii, "removed"));
        for (const std::string& h : gone) assert_true(r.find_object(h) == nullptr, std::string(tag) + ": " + h + " went with the table");
        // the other table's records are there
        assert_true(first_table(r) != nullptr && round_trip_of(r, *first_table(r)).data_table != nullptr, "the other table keeps its data table");
        // and the classes: the data table's class is there while a data table is
        std::string text = hdxf_test::lf_text(save(r, DxfFormat::Ascii, "again"));
        assert_true(text.find(std::string("\n  1\n") + (std::string(tag) == "2000" ? "ACDBDATATABLE" : "DATATABLE") + "\n") != std::string::npos, "its class");
    }
}

namespace {

// A record of an ASCII file with its handle-valued groups masked (two sessions give other handles); the proxy graphics of a
// table (92, 160, 310 of AcDbEntity) are the layout's (step 9.5): AutoCAD draws a changed table anew
Rec masked(const Rec& record) {
    Rec out;
    bool in_entity = false;
    for (const Group& g : record) {
        if (g.first == 100) in_entity = trim(g.second) == "AcDbEntity";
        if (in_entity && (g.first == 92 || g.first == 160 || g.first == 310)) continue;
        const std::string v = trim(g.second);
        const bool handle_code = g.first == 5 || g.first == 330 || g.first == 331 || g.first == 340 || g.first == 342 || g.first == 343 ||
                                 g.first == 344 || g.first == 360 || g.first == 361;
        const bool is_handle = handle_code && v != "0" && v.find_first_not_of("0123456789ABCDEFabcdef") == std::string::npos;
        out.push_back({g.first, is_handle ? std::string("H") : g.second});
    }
    return out;
}

} // namespace

TEST(document_data_a_changed_cell_of_a_2000_and_2004_table_is_what_autocads_changed_file_has) {
    for (const char* tag : {"2000", "2004"}) {
        std::vector<uint8_t> original = facit_bytes(std::string("acad_") + tag + "_dxf_tables.dxf");
        std::vector<uint8_t> changed = facit_bytes(std::string("acad_") + tag + "_dxf_tables_cell_changed.dxf");
        Drawing d = load(original, tag);
        Drawing was = load(changed, tag);
        hdxf::Entity* table = nullptr;
        for (auto& e : d.entities()) if (e.type == hdxf::EntityType::Table) table = &e;
        const hdxf::Entity* was_table = first_table(was);
        assert_true(table != nullptr && was_table != nullptr, "the tables");
        // AutoCAD's change: the text of the cell (2, 1) from "3" (a number with a format) to "9"
        d.set_table_cell_text(*table, 2, 1, "9");
        std::vector<uint8_t> out = save(d, DxfFormat::Ascii, tag);
        Drawing r = reload(out);
        const hdxf::Entity* nt = first_table(r);
        std::vector<Rec> a = ascii_records(changed), b = ascii_records(out);
        assert_eq(std::string(), same_record(masked(record_with_handle(a, "ACAD_TABLE", was_table->props.handle)), masked(record_with_handle(b, "ACAD_TABLE", nt->props.handle))),
                  std::string(tag) + ": ACAD_TABLE");
        assert_eq(std::string(), same_record(masked(record_with_handle(a, "TABLECONTENT", was.table_content(*was_table)->handle)), masked(record_with_handle(b, "TABLECONTENT", r.table_content(*nt)->handle))),
                  std::string(tag) + ": TABLECONTENT");
        RoundTrip want = round_trip_of(was, *was_table), got = round_trip_of(r, *nt);
        assert_true(want.data_table && got.data_table && want.xrecord && got.xrecord, "the round-trip records");
        assert_eq(std::string(), same_record(masked(record_with_handle(a, "XRECORD", want.xrecord->handle)), masked(record_with_handle(b, "XRECORD", got.xrecord->handle))),
                  std::string(tag) + ": the round-trip XRECORD");
        // the data table: the checksums, the two integer columns and the values of every cell XRECORD
        const DataTableData& x = want.data_table->as_data_table();
        const DataTableData& y = got.data_table->as_data_table();
        assert_eq(static_cast<int>(x.columns[0].values.size()), static_cast<int>(y.columns[0].values.size()), "the cells");
        for (size_t col = 0; col < 3; ++col) {
            for (size_t i = 0; i < x.columns[col].values.size(); ++i) {
                assert_true(x.columns[col].values[i] == y.columns[col].values[i], std::string(tag) + ": data table column " + std::to_string(col) + " cell " + std::to_string(i));
            }
        }
        for (size_t i = 0; i < x.columns[3].values.size(); ++i) {
            const Object* xa = was.find_object(std::get<DataTableObject>(x.columns[3].values[i]).handle);
            const Object* xb = r.find_object(std::get<DataTableObject>(y.columns[3].values[i]).handle);
            assert_true(xa != nullptr && xb != nullptr, "the cell records");
            assert_eq(group_text(xa->as_xrecord().data), group_text(xb->as_xrecord().data), std::string(tag) + ": the XRECORD of cell " + std::to_string(i));
        }
    }
}
