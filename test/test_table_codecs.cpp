#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include "TableTypes.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

// The table family of step 9.1 (plan 2026-10-27-fas-9-typer.md): ACAD_TABLE, TABLESTYLE, CELLSTYLEMAP,
// TABLECONTENT and TABLEGEOMETRY. Every AutoCAD-saved file of the corpus that has one of them is read, every
// record must come out typed, written again unchanged it must be the file's own bytes (B12), and written from
// the typed values alone (its preserved record emptied) it must be AutoCAD's own record.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

bool is_binary_bytes(const std::vector<uint8_t>& b) {
    static const char sentinel[] = "AutoCAD Binary DXF";
    return b.size() >= sizeof(sentinel) - 1 && std::equal(sentinel, sentinel + sizeof(sentinel) - 1, b.begin());
}

bool contains(const std::vector<uint8_t>& bytes, const std::string& needle) {
    return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

// The files of the corpus that have one of the table records
std::vector<std::filesystem::path> table_files(const std::vector<std::string>& names) {
    std::vector<std::filesystem::path> files;
    auto scan = [&](const std::filesystem::path& root, bool recursive) {
        std::error_code ec;
        if (!std::filesystem::exists(root, ec)) return;
        auto take = [&](const std::filesystem::path& p) {
            std::string ext = p.extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (ext != ".dxf") return;
            // HDXF_TABLE_ONLY=text: only the files whose name has the text (to look at one file)
            if (const char* only = std::getenv("HDXF_TABLE_ONLY")) {
                if (p.filename().string().find(only) == std::string::npos) return;
            }
            std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(p);
            for (const std::string& n : names) {
                if (contains(bytes, n)) { files.push_back(p); return; }
            }
        };
        if (recursive) {
            for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
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
    return files;
}

bool is_table_object(hdxf::ObjectType t) {
    return t == hdxf::ObjectType::TableStyle || t == hdxf::ObjectType::CellStyleMap ||
           t == hdxf::ObjectType::TableContent || t == hdxf::ObjectType::TableGeometry;
}

struct Census {
    int tables = 0, styles = 0, maps = 0, contents = 0, geometries = 0;
    std::vector<std::string> untyped;
};

Census census(hdxf::Drawing& d) {
    Census c;
    auto list = [&](const std::vector<hdxf::Entity>& entities) {
        for (const auto& e : entities) {
            if (e.type == hdxf::EntityType::Table) ++c.tables;
        }
    };
    list(d.entities());
    for (const auto& b : d.blocks()) list(b.entities);
    for (const auto& o : d.objects()) {
        switch (o.type) {
            case hdxf::ObjectType::TableStyle: ++c.styles; break;
            case hdxf::ObjectType::CellStyleMap: ++c.maps; break;
            case hdxf::ObjectType::TableContent: ++c.contents; break;
            case hdxf::ObjectType::TableGeometry: ++c.geometries; break;
            default: break;
        }
    }
    return c;
}

// The text lines of a record of an ASCII file: from "  0\nTYPE\n  5\nHANDLE" to the next "  0"
std::string ascii_record(const std::vector<uint8_t>& bytes, const std::string& type, const std::string& handle) {
    std::string text(bytes.begin(), bytes.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    std::string key = "  0\n" + type + "\n  5\n" + handle + "\n";
    size_t at = text.find(key);
    if (at == std::string::npos) {
        key = "  0\n" + type + "\n  5\n" + handle + "\n";
        return {};
    }
    size_t end = text.find("\n  0\n", at + 4);
    return text.substr(at, end == std::string::npos ? std::string::npos : end + 1 - at);
}

} // namespace

TEST(table_codecs_every_table_record_of_the_corpus_is_typed) {
    std::vector<std::filesystem::path> files =
        table_files({"ACAD_TABLE", "TABLECONTENT", "TABLEGEOMETRY", "CELLSTYLEMAP", "TABLESTYLE"});
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    int loaded = 0;
    Census total;
    std::vector<std::string> failures;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
        hdxf::Drawing d;
        if (!d.load_from_memory(bytes.data(), bytes.size())) continue;
        ++loaded;
        Census c = census(d);
        total.tables += c.tables;
        total.styles += c.styles;
        total.maps += c.maps;
        total.contents += c.contents;
        total.geometries += c.geometries;
        for (const auto& w : d.warnings()) {
            if (w.find("TABLE") != std::string::npos || w.find("CELLSTYLEMAP") != std::string::npos) {
                failures.push_back(path.filename().string() + ": " + w);
            }
        }
        // a record of a table type that is still untyped
        for (const auto& o : d.objects()) {
            if (o.type != hdxf::ObjectType::Unknown) continue;
            const std::string& name = o.as_unknown().object_type;
            if (name == "TABLESTYLE" || name == "CELLSTYLEMAP" || name == "TABLECONTENT" || name == "TABLEGEOMETRY") {
                failures.push_back(path.filename().string() + ": " + name + " " + o.handle + " is untyped");
            }
        }
    }
    std::cout << "table corpus: " << loaded << " files, " << total.tables << " tables, " << total.styles << " styles, "
              << total.maps << " cell style maps, " << total.contents << " contents, " << total.geometries << " geometries\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 12; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " table records are not typed:" + message);
    assert_true(total.tables > 0 && total.contents > 0, "the corpus has tables");
}

namespace {

// The first line where two ASCII files differ, with a little context
std::string first_difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    auto lines = [](const std::vector<uint8_t>& bytes) {
        std::vector<std::string> out;
        std::string cur;
        for (uint8_t c : bytes) {
            if (c == '\n') { out.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += static_cast<char>(c);
        }
        out.push_back(cur);
        return out;
    };
    std::vector<std::string> x = lines(a), y = lines(b);
    std::string header;
    for (size_t i = 0; i < x.size() && i < y.size(); ++i) {
        if (!x[i].empty() && x[i][0] == '$') header = x[i];
        if (x[i] != y[i]) {
            // a drawing that was changed names the last saver and has new GUIDs
            if (i >= 2 && (header == "$LASTSAVEDBY" || header == "$VERSIONGUID" || header == "$FINGERPRINTGUID") && x[i - 2] == header) continue;
            // AutoCAD writes a double's 16 digits cut off where HDxf rounds them (…089 and …090 for the same
            // double): two texts that are the same double are the same value
            auto number = [](const std::string& s, double& v) {
                if (s.find('.') == std::string::npos) return false;
                char* end = nullptr;
                v = std::strtod(s.c_str(), &end);
                return end && *end == '\0' && end != s.c_str();
            };
            double da = 0.0, db = 0.0;
            if (number(x[i], da) && number(y[i], db) && da == db) continue;
            std::string out = "line " + std::to_string(i + 1) + ": \"" + x[i] + "\" against \"" + y[i] + "\" (before:";
            for (size_t k = i >= 6 ? i - 6 : 0; k < i; ++k) out += " [" + x[k] + "]";
            return out + ")";
        }
    }
    if (x.size() == y.size()) return std::string();
    return "one has " + std::to_string(x.size()) + " lines, the other " + std::to_string(y.size());
}

// Empties the preserved record of every table record: they are written from the typed values alone
int forget_table_records(hdxf::Drawing& d) {
    int n = 0;
    auto list = [&](std::vector<hdxf::Entity>& entities) {
        for (auto& e : entities) {
            if (e.type == hdxf::EntityType::Table) { e.raw = hdxf::RawRecord(); ++n; }
        }
    };
    list(d.entities());
    for (auto& b : d.blocks()) list(b.entities);
    for (auto& o : d.objects()) {
        if (is_table_object(o.type)) { o.raw = hdxf::RawRecord(); ++n; }
    }
    return n;
}

} // namespace


namespace {

struct RecordId {
    std::string type;
    std::string handle;
};

// The table records of a drawing: their DXF names and handles
std::vector<RecordId> table_record_ids(const hdxf::Drawing& d) {
    std::vector<RecordId> ids;
    auto list = [&](const std::vector<hdxf::Entity>& entities) {
        for (const auto& e : entities) if (e.type == hdxf::EntityType::Table) ids.push_back({"ACAD_TABLE", e.props.handle});
    };
    list(d.entities());
    for (const auto& b : d.blocks()) list(b.entities);
    for (const auto& o : d.objects()) {
        switch (o.type) {
            case hdxf::ObjectType::TableStyle: ids.push_back({"TABLESTYLE", o.handle}); break;
            case hdxf::ObjectType::CellStyleMap: ids.push_back({"CELLSTYLEMAP", o.handle}); break;
            case hdxf::ObjectType::TableContent: ids.push_back({"TABLECONTENT", o.handle}); break;
            case hdxf::ObjectType::TableGeometry: ids.push_back({"TABLEGEOMETRY", o.handle}); break;
            default: break;
        }
    }
    return ids;
}

// A binary file as ASCII, by reading it and writing it again (its preserved records, unchanged)
std::vector<uint8_t> as_ascii(const std::vector<uint8_t>& bytes) {
    hdxf::Drawing d;
    if (!d.load_from_memory(bytes.data(), bytes.size())) return {};
    return d.save_to_memory(DxfFormat::Ascii);
}

// "102 {ACAD_REACTORS ... 102 } 102 {ACAD_XDICTIONARY ... 102 }" the other way round, as AutoCAD writes the two blocks
std::string extension_dictionary_first(const std::string& record) {
    const std::string reactors = "102\n{ACAD_REACTORS\n";
    const std::string xdict = "102\n{ACAD_XDICTIONARY\n";
    const std::string close = "102\n}\n";
    size_t r = record.find(reactors);
    if (r == std::string::npos) return record;
    size_t r_end = record.find(close, r);
    if (r_end == std::string::npos) return record;
    r_end += close.size();
    if (record.compare(r_end, xdict.size(), xdict) != 0) return record;
    size_t x_end = record.find(close, r_end);
    if (x_end == std::string::npos) return record;
    x_end += close.size();
    return record.substr(0, r) + record.substr(r_end, x_end - r_end) + record.substr(r, r_end - r) + record.substr(x_end);
}

std::string record_difference(const std::string& expected, const std::string& got) {
    std::vector<uint8_t> a(expected.begin(), expected.end()), b(got.begin(), got.end());
    return first_difference(a, b);
}

} // namespace

TEST(table_codecs_written_unchanged_is_the_file_and_from_the_typed_values_is_autocads_record) {
    std::vector<std::filesystem::path> files =
        table_files({"ACAD_TABLE", "TABLECONTENT", "TABLEGEOMETRY", "CELLSTYLEMAP", "TABLESTYLE"});
    if (files.empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    std::set<std::string> seen;
    std::set<std::string> seen_records;
    std::vector<std::string> failures;
    int checked = 0, records = 0, binary = 0;
    for (const auto& path : files) {
        std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
        if (!seen.insert(hdxf_test::sha256_hex(bytes)).second) continue;
        const bool is_binary = is_binary_bytes(bytes);
        // The ASCII files all; of the binary ones the tables' own (their twins have the same records, which the
        // typed-records test shows) and ACadSharp's two
        const std::string name = path.filename().string();
        if (is_binary && name.find("_tables_bin") == std::string::npos && name.find("sample_AC1015_binary") == std::string::npos &&
            name.find("sample_AC1032_binary") == std::string::npos) {
            continue;
        }
        // ezdxf's files are not AutoCAD's: the head's groups are in the reference's order, and MTEXT's frames and
        // columns become entities when they are saved (their handles move $HANDSEED)
        if (path.generic_string().find("/ezdxf/") != std::string::npos) continue;
        hdxf::Drawing d;
        if (!d.load_from_memory(bytes.data(), bytes.size())) continue;
        std::vector<RecordId> ids = table_record_ids(d);
        if (ids.empty()) continue;
        // A file whose table records all have the text of records already checked (the styles of a hundred files
        // are alike) adds nothing
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
                if (const char* dump = std::getenv("HDXF_TABLE_DUMP")) {
                    std::ofstream(dump, std::ios::binary).write(reinterpret_cast<const char*>(again.data()), static_cast<std::streamsize>(again.size()));
                }
                failures.push_back(name + " unchanged: " + difference);
                continue;
            }
        }
        // from the typed values alone: the records are AutoCAD's own, byte for byte
        forget_table_records(d);
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
            // A file another program wrote may have the head's two 102 blocks the other way round; AutoCAD's files all
            // have the extension dictionary first (every record of acad_*_dxf_*.dxf that has both)
            if (name.rfind("acad_", 0) != 0) expected = extension_dictionary_first(expected);
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
    std::cout << "table regeneration: " << checked << " files (" << binary << " binary), " << records << " records\n";
    std::string message;
    for (size_t i = 0; i < failures.size() && i < 10; ++i) message += "\n  " + failures[i];
    assert_true(failures.empty(), std::to_string(failures.size()) + " files differ:" + message);
}


// ---------------------------------------------------------------------------
// The version matrix (B101): nine versions x ASCII and binary for each type
// ---------------------------------------------------------------------------

namespace {

hdxf::Object make_table_object(hdxf::ObjectType type) {
    hdxf::Object o;
    o.type = type;
    o.owner = "0";
    switch (type) {
        case hdxf::ObjectType::TableStyle: o.data = hdxf::Boxed<hdxf::TableStyleData>(hdxf::TableStyleData()); break;
        case hdxf::ObjectType::CellStyleMap: o.data = hdxf::Boxed<hdxf::CellStyleMapData>(hdxf::CellStyleMapData()); break;
        case hdxf::ObjectType::TableContent: o.data = hdxf::Boxed<hdxf::TableContentData>(hdxf::TableContentData()); break;
        default: o.data = hdxf::Boxed<hdxf::TableGeometryData>(hdxf::TableGeometryData()); break;
    }
    return o;
}

std::string verify_object(const hdxf::Drawing& r, hdxf::ObjectType type) {
    int n = 0;
    for (const auto& o : r.objects()) if (o.type == type) ++n;
    return n == 1 ? std::string() : "expected one typed record, found " + std::to_string(n);
}

hdxf_test::MatrixReport run_table_matrix(const char* name, hdxf::ObjectType type) {
    const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object(name);
    assert_true(codec != nullptr, std::string(name) + " has a codec");
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
        name, codec->versions, [&](hdxf::Drawing& d) { d.objects().push_back(make_table_object(type)); },
        [&](const hdxf::Drawing& r, DxfVersion, DxfFormat) { return verify_object(r, type); });
    std::cout << name << ": " << report.generated << " cells generated, " << report.named_errors << " named errors, "
              << report.not_established << " not established, AutoCAD verified " << report.autocad_verified << ", ej testat "
              << report.autocad_not_tested << ", not savable " << report.autocad_not_savable << "\n";
    return report;
}

} // namespace

TEST(table_codecs_version_matrix_of_the_style_and_its_cell_style_map) {
    for (const auto& [name, type] : {std::pair<const char*, hdxf::ObjectType>{"TABLESTYLE", hdxf::ObjectType::TableStyle},
                                      {"CELLSTYLEMAP", hdxf::ObjectType::CellStyleMap}}) {
        hdxf_test::MatrixReport report = run_table_matrix(name, type);
        assert_eq(12, report.generated, std::string(name) + ": 2000 … 2018, both formats, are made");
        assert_eq(2, report.named_errors, std::string(name) + ": R12, both formats, has the named error");
        assert_eq(4, report.not_established, std::string(name) + ": R13 and R14 are not established (no AutoCAD file, none savable)");
        assert_eq(12, report.autocad_verified, std::string(name) + ": AutoCAD's own files of 2000 … 2018, both formats");
        assert_eq(4, report.autocad_not_savable, std::string(name) + ": AutoCAD 2025 cannot save R13/R14");
    }
}

TEST(table_codecs_version_matrix_of_the_content_and_the_geometry) {
    for (const auto& [name, type] : {std::pair<const char*, hdxf::ObjectType>{"TABLECONTENT", hdxf::ObjectType::TableContent},
                                      {"TABLEGEOMETRY", hdxf::ObjectType::TableGeometry}}) {
        const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object(name);
        assert_true(codec != nullptr, std::string(name) + " has a codec");
        // 2000 and 2004 keep the table in a round-trip DATATABLE too (step 9.8), and a table with content has these records
        // there as well (AutoCAD's acad_2000/2004_dxf_tables.dxf): made like 2007's
        for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004}) {
            assert_true(codec->versions.at(v, DxfFormat::Ascii).generation == hdxf::internal::Generation::Native, std::string(name) + ": made in 2000/2004");
        }
        hdxf_test::MatrixReport report = run_table_matrix(name, type);
        assert_eq(12, report.generated, std::string(name) + ": 2000 … 2018, both formats");
        assert_eq(2, report.named_errors, std::string(name) + ": R12");
        assert_eq(4, report.not_established, std::string(name) + ": R13 and R14");
        assert_eq(12, report.autocad_verified, std::string(name) + ": AutoCAD's own files of 2000 … 2018 (read and written back)");
    }
}

TEST(table_codecs_version_matrix_of_the_table) {
    const hdxf::internal::EntityCodec* codec = hdxf::internal::TypedRegistry::instance().find_entity("ACAD_TABLE");
    assert_true(codec != nullptr, "ACAD_TABLE has a codec");
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
        "ACAD_TABLE", codec->versions,
        [](hdxf::Drawing& d) {
            hdxf::Entity& t = d.add_table({1, 2, 0}, 3, 2, 6.0, 25.0);
            d.set_table_cell_text(t, 1, 1, "cell");
        },
        [](const hdxf::Drawing& r, DxfVersion, DxfFormat) -> std::string {
            for (const auto& e : r.entities()) {
                if (e.type != hdxf::EntityType::Table) continue;
                const hdxf::TableData& t = e.as_table();
                if (t.rows() != 3 || t.columns() != 2) return "the table has " + std::to_string(t.rows()) + " rows";
                return hdxf::internal::table_value_text(t.cell(1, 1)->value) == "cell" ? std::string() : "the text of the cell";
            }
            return "no typed table";
        });
    std::cout << "ACAD_TABLE: " << report.generated << " cells generated, " << report.named_errors << " named errors, "
              << report.not_established << " not established, AutoCAD verified " << report.autocad_verified << "\n";
    assert_eq(12, report.generated, "2000 … 2018, both formats, are made");
    assert_eq(2, report.named_errors, "R12 has the named error");
    assert_eq(4, report.not_established, "R13 and R14");
    assert_eq(12, report.autocad_verified, "AutoCAD's own files of 2000 … 2018, both formats");
}

// ---------------------------------------------------------------------------
// Values: every type in both forms, text that looks like a marker, text in pieces
// ---------------------------------------------------------------------------

namespace {

hdxf::TableValue value_of_type(hdxf::TableValueType type) {
    hdxf::TableValue v;
    v.type = type;
    v.flags = 6;
    switch (type) {
        case hdxf::TableValueType::Long: v.value = int32_t(-7); break;
        case hdxf::TableValueType::Double: v.value = 12.75; break;
        case hdxf::TableValueType::String: v.value = std::string("a string"); break;
        case hdxf::TableValueType::Date: {
            hdxf::TableDate date;
            date.year = 2026; date.month = 10; date.day_of_week = 2; date.day = 27; date.hour = 13; date.minute = 5;
            v.value = date;
            break;
        }
        case hdxf::TableValueType::Point2D: v.value = hdxf::Point2{1.5, -2.5}; break;
        case hdxf::TableValueType::Point3D: v.value = hdxf::Point3{1.5, -2.5, 3.5}; break;
        case hdxf::TableValueType::ObjectId: v.value = hdxf::TableHandle{"2A"}; break;
        case hdxf::TableValueType::Buffer: v.value = std::vector<uint8_t>{1, 2, 3, 250, 251}; break;
        default: break;
    }
    v.unit_type = 1;
    v.format = "%lu6%ds[\\,]";
    v.text = "formatted";
    return v;
}

hdxf::Object content_with_values(const std::vector<hdxf::TableValue>& values) {
    hdxf::Object o = make_table_object(hdxf::ObjectType::TableContent);
    hdxf::TableContentData& d = o.as_table_content();
    d.name = "CELL";                    // a name that is a marker's text
    hdxf::TableContentRow row;
    row.format.kind = 2;
    hdxf::TableContentCell cell;
    cell.format.kind = 1;
    for (const hdxf::TableValue& v : values) {
        hdxf::TableCustomData item;
        item.name = "ACVALUE_END";      // and a custom data's name
        item.value = v;
        cell.custom_data.push_back(item);
        hdxf::TableCellContent content;
        content.value = v;
        cell.contents.push_back(content);
    }
    row.cells.push_back(cell);
    d.rows.push_back(row);
    hdxf::TableContentColumn column;
    column.format.kind = 3;
    d.columns.push_back(column);
    return o;
}

} // namespace

TEST(table_codecs_every_value_type_is_written_and_read_in_both_forms) {
    std::vector<hdxf::TableValue> values;
    for (hdxf::TableValueType t : {hdxf::TableValueType::Long, hdxf::TableValueType::Double, hdxf::TableValueType::String,
                                   hdxf::TableValueType::Date, hdxf::TableValueType::Point2D, hdxf::TableValueType::Point3D,
                                   hdxf::TableValueType::ObjectId, hdxf::TableValueType::Buffer}) {
        values.push_back(value_of_type(t));
    }
    hdxf::TableValue unset;                                 // an empty value, the empty string, a typed value without its value
    values.push_back(unset);
    values.push_back(hdxf::TableValue::from_text(""));
    hdxf::TableValue typed_empty;
    typed_empty.type = hdxf::TableValueType::Long;
    typed_empty.flags = 5;
    values.push_back(typed_empty);
    for (DxfVersion v : {DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2018}) {
        const bool full = v >= DxfVersion::R2007;
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            hdxf::Drawing d(v);
            d.objects().push_back(content_with_values(values));
            std::vector<uint8_t> out = d.save_to_memory(f);
            assert_true(!out.empty(), "saved: " + d.last_error());
            hdxf::Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), "read: " + r.last_error());
            const hdxf::Object* found = nullptr;
            for (const auto& o : r.objects()) if (o.type == hdxf::ObjectType::TableContent) found = &o;
            assert_true(found != nullptr, "typed again (a marker's text is a name here): " +
                                              (r.warnings().empty() ? std::string("no warning") : r.warnings()[0]));
            const hdxf::TableContentData& c = found->as_table_content();
            assert_eq("CELL", c.name, "the name");
            const hdxf::TableContentCell& cell = c.rows[0].cells[0];
            assert_eq(static_cast<int>(values.size()), static_cast<int>(cell.custom_data.size()), "custom data");
            for (size_t i = 0; i < values.size(); ++i) {
                hdxf::TableValue expected = values[i];
                if (!full) {
                    // 2000 and 2004 keep the type and the value only
                    expected.flags = 7;
                    expected.unit_type = 0;
                    expected.format.clear();
                    expected.text.clear();
                    // the old form has no value-less type: the zero one
                    if (!expected.has_value()) expected.value = int32_t(0);
                }
                for (const hdxf::TableValue* got : {&cell.custom_data[i].value, &cell.contents[i].value}) {
                    if (!full) {
                        // the flags AutoCAD's 2000 form does not have come out as the default
                        hdxf::TableValue g = *got;
                        g.flags = expected.flags;
                        assert_true(g == expected,
                                    "value " + std::to_string(i) + " (type " + std::to_string(static_cast<int>(expected.type)) + ") as 2004");
                    } else {
                        assert_true(*got == expected, "value " + std::to_string(i) + " (type " + std::to_string(static_cast<int>(expected.type)) + ")");
                    }
                }
            }
            assert_true(r.save_to_memory(f) == out, "written again unchanged, the same bytes");
        }
    }
}

TEST(table_codecs_text_that_looks_like_a_marker_is_text_in_a_cell_and_long_text_comes_in_pieces) {
    std::string long_text;
    for (int i = 0; i < 40; ++i) long_text += "Pole " + std::to_string(i) + " \xC3\xA5\xC3\xA4\xC3\xB6 \xF0\x9F\x98\x80; ";
    const std::vector<std::string> texts = {"CELL", "ACVALUE_END", "CELL_VALUE", "TABLEFORMAT_END", "LINKEDTABLEDATACELL_END",
                                            "VALUE", "  leading and trailing  ", long_text, std::string(250, 'x'), std::string(249, 'y'),
                                            std::string(500, 'z')};
    for (DxfVersion v : {DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2018}) {
        hdxf::Drawing d(v);
        hdxf::Entity& t = d.add_table({0, 0, 0}, 2, static_cast<size_t>(texts.size()), 5.0, 10.0);
        for (size_t i = 0; i < texts.size(); ++i) d.set_table_cell_text(t, 1, i, texts[i]);
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), "saved: " + d.last_error());
        hdxf::Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), "read: " + r.last_error());
        hdxf::Entity* table = nullptr;
        for (auto& e : r.entities()) if (e.type == hdxf::EntityType::Table) table = &e;
        assert_true(table != nullptr, "typed table: " + (r.warnings().empty() ? std::string() : r.warnings()[0]));
        for (size_t i = 0; i < texts.size(); ++i) {
            assert_eq(texts[i], hdxf::internal::table_value_text(table->as_table().cell(1, i)->value), "cell " + std::to_string(i));
        }
        if (v >= DxfVersion::R2007) {
            const hdxf::Object* content = r.table_content(*table);
            assert_true(content != nullptr, "its content");
            for (size_t i = 0; i < texts.size(); ++i) {
                assert_eq(texts[i], hdxf::internal::table_value_text(content->as_table_content().rows[1].cells[i].contents[0].value),
                          "content cell " + std::to_string(i));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Records that are not what the grammar says: kept as they were, with a warning (B105)
// ---------------------------------------------------------------------------

namespace {

std::string objects_text(const std::string& body) {
    return "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1032\n  0\nENDSEC\n"
           "  0\nSECTION\n  2\nOBJECTS\n" + body + "  0\nENDSEC\n  0\nEOF\n";
}

bool untyped_with_warning(const std::string& body, const std::string& word) {
    std::string text = objects_text(body);
    hdxf::Drawing d;
    if (!d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size())) return false;
    if (d.objects().size() != 1 || d.objects()[0].type != hdxf::ObjectType::Unknown) return false;
    for (const auto& w : d.warnings()) if (w.find(word) != std::string::npos) return true;
    return false;
}

} // namespace

TEST(table_codecs_a_record_the_grammar_does_not_fit_stays_as_read_with_a_warning) {
    // a count that the groups cannot hold (no allocation of it), an absent subclass, a group of a later version, a cut
    assert_true(untyped_with_warning("  0\nTABLEGEOMETRY\n  5\n10\n330\n0\n100\nAcDbTableGeometry\n 90\n     2\n 91\n     2\n 92\n 99999999\n", "TABLEGEOMETRY"),
                "a huge cell count");
    {
        // a record that is only its head has no subclass to read: the defaults (a cut-off one has some, not the others)
        std::string text = objects_text("  0\nTABLEGEOMETRY\n  5\n10\n330\n0\n");
        hdxf::Drawing d;
        assert_true(d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), "read");
        assert_true(d.objects().size() == 1 && d.objects()[0].type == hdxf::ObjectType::TableGeometry, "a record without subclasses is the defaults");
    }
    assert_true(untyped_with_warning("  0\nTABLESTYLE\n  5\n10\n330\n0\n100\nAcDbTableStyle\n 70\n     0\n", "TABLESTYLE"), "a style cut off in its fields");
    assert_true(untyped_with_warning("  0\nCELLSTYLEMAP\n  5\n10\n330\n0\n100\nAcDbCellStyleMap\n 90\n 99999999\n", "CELLSTYLEMAP"), "a huge style count");
    assert_true(untyped_with_warning("  0\nCELLSTYLEMAP\n  5\n10\n330\n0\n100\nAcDbCellStyleMap\n 90\n     1\n300\nCELLSTYLE\n  1\nTABLEFORMAT_BEGIN\n 90\n     5\n", "CELLSTYLEMAP"),
                "a format cut off");
    assert_true(untyped_with_warning("  0\nTABLESTYLE\n  5\n10\n330\n0\n100\nAcDbTableStyle\n 70\n     0\n 71\n     0\n 40\n1.5\n 41\n1.5\n280\n     0\n281\n     0\n", "TABLESTYLE"),
                "row styles missing");
    assert_true(untyped_with_warning("  0\nTABLECONTENT\n  5\n10\n330\n0\n100\nAcDbLinkedData\n  1\n\n300\n\n100\nAcDbLinkedTableData\n 90\n 99999999\n", "TABLECONTENT"),
                "a huge column count");
    assert_true(untyped_with_warning("  0\nTABLECONTENT\n  5\n10\n330\n0\n100\nAcDbLinkedData\n  1\n\n300\n\n100\nAcDbLinkedTableData\n 90\n     1\n300\nCOLUMN\n  1\nLINKEDTABLEDATACOLUMN_BEGIN\n", "TABLECONTENT"),
                "a column cut off");
}

TEST(table_codecs_a_table_of_a_version_before_2000_or_a_cut_one_stays_as_read) {
    // an entity: ACAD_TABLE without its AcDbTable and with a row count the groups cannot hold
    std::string text = "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1032\n  0\nENDSEC\n  0\nSECTION\n  2\nENTITIES\n"
                       "  0\nACAD_TABLE\n  5\n10\n100\nAcDbEntity\n  8\n0\n100\nAcDbBlockReference\n  2\n*T1\n 10\n0.0\n 20\n0.0\n 30\n0.0\n"
                       "100\nAcDbTable\n280\n     0\n342\n0\n343\n0\n 11\n1.0\n 21\n0.0\n 31\n0.0\n 90\n    22\n 91\n 99999\n 92\n 99999\n"
                       " 93\n     0\n 94\n     0\n 95\n     0\n 96\n     0\n  0\nENDSEC\n  0\nEOF\n";
    hdxf::Drawing d;
    assert_true(d.load_from_memory(reinterpret_cast<const uint8_t*>(text.data()), text.size()), "read: " + d.last_error());
    assert_eq(1, static_cast<int>(d.entities().size()), "one entity");
    assert_true(d.entities()[0].type == hdxf::EntityType::Unknown, "the table that cannot be typed is kept");
    bool warned = false;
    for (const auto& w : d.warnings()) warned = warned || w.find("ACAD_TABLE") != std::string::npos;
    assert_true(warned, "with a warning");
}
