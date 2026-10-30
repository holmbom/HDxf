#include "test_framework.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TableTypes.hpp"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

// The tables in a drawing (step 9.1; plan B19, B104, B108): a table with its style, its content, its geometry and
// its round-trip dictionary — made, changed, and taken away — and AutoCAD's own file of a changed cell as the facit
// of what a change does to the companions.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

std::filesystem::path facit(const std::string& name) { return hdxf_test::source_path("doc/facit/" + name); }

bool load_file(hdxf::Drawing& d, const std::filesystem::path& path) {
    std::vector<uint8_t> bytes = hdxf_test::read_file_bytes(path);
    return !bytes.empty() && d.load_from_memory(bytes.data(), bytes.size());
}

hdxf::Entity* table_of(hdxf::Drawing& d) {
    for (auto& e : d.entities()) if (e.type == hdxf::EntityType::Table) return &e;
    for (auto& b : d.blocks()) for (auto& e : b.entities) if (e.type == hdxf::EntityType::Table) return &e;
    return nullptr;
}

size_t count_objects(const hdxf::Drawing& d, hdxf::ObjectType type) {
    size_t n = 0;
    for (const auto& o : d.objects()) if (o.type == type) ++n;
    return n;
}

hdxf::Drawing reload(const std::vector<uint8_t>& bytes) {
    hdxf::Drawing r;
    bool ok = r.load_from_memory(bytes.data(), bytes.size());
    assert_true(ok, "the saved drawing is not readable: " + r.last_error());
    return r;
}

// The text of one record of an ASCII file, its handle-valued groups masked (they differ between two sessions)
std::string masked_record(const std::vector<uint8_t>& bytes, const std::string& type, const std::string& handle) {
    std::string text(bytes.begin(), bytes.end());
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    std::string key = "  0\n" + type + "\n  5\n" + handle + "\n";
    size_t at = text.find(key);
    if (at == std::string::npos) return {};
    size_t end = text.find("\n  0\n", at + 4);
    std::string record = text.substr(at, end == std::string::npos ? std::string::npos : end + 1 - at);
    // lines come in pairs: the code, the value
    std::vector<std::string> lines;
    {
        size_t pos = 0;
        while (pos < record.size()) {
            size_t nl = record.find('\n', pos);
            lines.push_back(record.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
    }
    std::string out;
    std::set<int> handles = {5, 330, 331, 340, 342, 343, 344, 360, 361};
    bool in_entity = false;
    for (size_t i = 0; i + 1 < lines.size(); i += 2) {
        const int code = std::atoi(lines[i].c_str());
        const std::string& value = lines[i + 1];
        if (code == 100) in_entity = value == "AcDbEntity";
        // the proxy graphics (92 before 2010, 160, 310) are the layout's (step 9.5): AutoCAD draws a changed table anew
        if (in_entity && (code == 92 || code == 160 || code == 310)) continue;
        out += lines[i] + "\n";
        // a handle only where the value is hexadecimal (340 0 and 330 0 stay as they are)
        bool handle = handles.count(code) && value != "0" && value.find_first_not_of("0123456789ABCDEFabcdef ") == std::string::npos;
        out += (handle ? std::string("H") : value) + "\n";
    }
    return out;
}

// Two record texts that must be the same: the first line that is not, with what is before it
void assert_same_record(const std::string& expected, const std::string& got, const std::string& label) {
    if (expected == got) return;
    auto split = [](const std::string& s) {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos < s.size()) {
            size_t nl = s.find('\n', pos);
            out.push_back(s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
            if (nl == std::string::npos) break;
            pos = nl + 1;
        }
        return out;
    };
    std::vector<std::string> a = split(expected), b = split(got);
    std::string where = expected.empty() ? " (AutoCAD's record is not found)" : got.empty() ? " (our record is not found)" : "";
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i] != b[i]) {
            std::string before;
            for (size_t k = i >= 8 ? i - 8 : 0; k < i; ++k) before += " [" + a[k] + "]";
            throw hdxf_test::AssertFailure{label + ": line " + std::to_string(i + 1) + " AutoCAD \"" + a[i] + "\" ours \"" + b[i] + "\", before:" + before};
        }
    }
    throw hdxf_test::AssertFailure{label + ": " + std::to_string(a.size()) + " lines against " + std::to_string(b.size()) + where};
}

std::string content_handle(hdxf::Drawing& d, const hdxf::Entity& table) {
    const hdxf::Object* c = d.table_content(table);
    return c ? c->handle : std::string();
}

std::string geometry_handle(hdxf::Drawing& d, const hdxf::Entity& table) {
    const hdxf::Object* g = d.table_geometry(table);
    return g ? g->handle : std::string();
}

} // namespace

// ---------------------------------------------------------------------------
// The checksum
// ---------------------------------------------------------------------------

TEST(table_relations_text_checksum_is_autocads_sum_of_unit_times_position) {
    using hdxf::internal::table_text_checksum;
    // AutoCAD's own values (doc/facit/acad_2018_dxf_tables.dxf)
    assert_eq(1044.0, table_text_checksum("Item"), "Item");
    assert_eq(1660.0, table_text_checksum("Count"), "Count");
    assert_eq(15230.0, table_text_checksum("HDxf TABLE and FIELD"), "the title");
    assert_eq(40495.0, table_text_checksum("Merged cells with background"), "merged");
    assert_eq(51.0, table_text_checksum("3"), "3");
    assert_eq(57.0, table_text_checksum("9"), "9 (AutoCAD's changed file)");
    assert_eq(0.0, table_text_checksum(""), "empty");
    // a character above U+FFFF is two units (its surrogates), a UTF-8 two-byte character one
    assert_eq(static_cast<double>(1 * 0xD83D + 2 * 0xDE00), table_text_checksum("\xF0\x9F\x98\x80"), "surrogates");
    assert_eq(static_cast<double>(1 * 0xE5 + 2 * 0x61), table_text_checksum("\xC3\xA5" "a"), "a two-byte character");
}

TEST(table_relations_every_checksum_of_autocads_files_is_the_sum_of_its_cells_text) {
    int checked = 0;
    std::vector<std::string> failures;
    for (const char* name : {"acad_2000_dxf_tables.dxf", "acad_2004_dxf_tables.dxf", "acad_2007_dxf_tables.dxf",
                             "acad_2010_dxf_tables.dxf", "acad_2013_dxf_tables.dxf", "acad_2018_dxf_tables.dxf"}) {
        hdxf::Drawing d;
        if (!load_file(d, facit(name))) hdxf_test::skip("no doc/facit");
        hdxf::Entity* table = table_of(d);
        const hdxf::Object* content = table ? d.table_content(*table) : nullptr;
        assert_true(content != nullptr, std::string(name) + ": the table has its TABLECONTENT");
        const hdxf::TableData& t = table->as_table();
        const hdxf::TableContentData& c = content->as_table_content();
        assert_eq(static_cast<int>(t.rows()), static_cast<int>(c.rows.size()), std::string(name) + ": rows");
        for (size_t r = 0; r < t.rows(); ++r) {
            for (size_t col = 0; col < t.columns(); ++col) {
                const hdxf::TableCell& cell = t.cells[r * t.columns() + col];
                if (cell.type != hdxf::TableCellType::Text || !cell.field_handle.empty()) continue;   // a field's: its runtime text
                const hdxf::TableContentCell& cc = c.rows[r].cells[col];
                for (const auto& item : cc.custom_data) {
                    if (item.name != hdxf::internal::kTableChecksumName) continue;
                    double stored = std::get<double>(item.value.value);
                    double derived = hdxf::internal::table_text_checksum(hdxf::internal::table_value_text(cell.value));
                    ++checked;
                    if (stored != derived) {
                        failures.push_back(std::string(name) + " cell " + std::to_string(r) + "," + std::to_string(col) + ": " +
                                           std::to_string(stored) + " against " + std::to_string(derived));
                    }
                }
            }
        }
    }
    assert_true(failures.empty(), failures.empty() ? "" : failures[0]);
    assert_true(checked > 100, "checked " + std::to_string(checked));
}

// ---------------------------------------------------------------------------
// AutoCAD's own file of a changed cell
// ---------------------------------------------------------------------------

TEST(table_relations_a_changed_cell_is_what_autocads_changed_file_has) {
    hdxf::Drawing d;
    if (!load_file(d, facit("acad_2018_dxf_tables.dxf"))) hdxf_test::skip("no doc/facit");
    std::vector<uint8_t> changed_file = hdxf_test::read_file_bytes(facit("acad_2018_dxf_tables_cell_changed.dxf"));
    hdxf::Drawing was;
    assert_true(was.load_from_memory(changed_file.data(), changed_file.size()), "AutoCAD's changed file");

    hdxf::Entity* table = table_of(d);
    assert_true(table != nullptr, "the table");
    const std::string content = content_handle(d, *table);
    const std::string geometry = geometry_handle(d, *table);
    // AutoCAD's change: the text of the cell (2, 1) from "3" (a number with a format) to "9" (a string)
    d.set_table_cell_text(*table, 2, 1, "9");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), "saved: " + d.last_error());

    hdxf::Entity* was_table = table_of(was);
    assert_true(was_table != nullptr, "the changed file's table");
    // the table, its content and its geometry as AutoCAD's changed file has them
    assert_same_record(masked_record(changed_file, "ACAD_TABLE", was_table->props.handle), masked_record(out, "ACAD_TABLE", table->props.handle),
                       "ACAD_TABLE");
    assert_same_record(masked_record(changed_file, "TABLECONTENT", content_handle(was, *was_table)), masked_record(out, "TABLECONTENT", content),
                       "TABLECONTENT");
    // the geometry: AutoCAD lays the cell out again (its new sizes are the layout's, step 9.5); the structure is the same
    {
        const hdxf::TableGeometryData& a = was.table_geometry(*was_table)->as_table_geometry();
        hdxf::Drawing r = reload(out);
        const hdxf::TableGeometryData& b = r.table_geometry(*table_of(r))->as_table_geometry();
        assert_eq(a.rows, b.rows, "geometry rows");
        assert_eq(a.columns, b.columns, "geometry columns");
        assert_eq(static_cast<int>(a.cells.size()), static_cast<int>(b.cells.size()), "geometry cells");
        for (size_t i = 0; i < a.cells.size(); ++i) {
            assert_eq(a.cells[i].flags, b.cells[i].flags, "geometry flags of cell " + std::to_string(i));
            assert_eq(static_cast<int>(a.cells[i].contents.size()), static_cast<int>(b.cells[i].contents.size()),
                      "geometry contents of cell " + std::to_string(i));
        }
        (void)geometry;
    }
}

TEST(table_relations_a_cell_that_did_not_change_keeps_its_content_and_checksum) {
    hdxf::Drawing d;
    if (!load_file(d, facit("acad_2018_dxf_tables.dxf"))) hdxf_test::skip("no doc/facit");
    hdxf::Entity* table = table_of(d);
    hdxf::TableContentData before = d.table_content(*table)->as_table_content();
    hdxf::TableContentData& after = d.table_content(*table)->as_table_content();
    d.set_table_cell_text(*table, 1, 0, "Pos");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), "saved: " + d.last_error());
    hdxf::Drawing r = reload(out);
    const hdxf::TableContentData& now = r.table_content(*table_of(r))->as_table_content();
    for (size_t row = 0; row < before.rows.size(); ++row) {
        for (size_t col = 0; col < before.rows[row].cells.size(); ++col) {
            const auto& a = before.rows[row].cells[col];
            const auto& b = now.rows[row].cells[col];
            bool same = a.custom_data.size() == b.custom_data.size() && a.contents.size() == b.contents.size();
            if (same && !a.custom_data.empty()) same = a.custom_data[0].value == b.custom_data[0].value;
            if (row == 1 && col == 0) {
                assert_false(same, "the changed cell's checksum changed");
                assert_eq(hdxf::internal::table_text_checksum("Pos"), std::get<double>(b.custom_data[0].value.value), "its new checksum");
            } else {
                assert_true(same, "cell " + std::to_string(row) + "," + std::to_string(col) + " is as it was");
            }
        }
    }
    (void)after;
}

// ---------------------------------------------------------------------------
// A table made in a drawing
// ---------------------------------------------------------------------------

TEST(table_relations_a_new_table_gets_its_style_content_geometry_and_round_trip_dictionary) {
    for (DxfVersion v : {DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            hdxf::Drawing d(v);
            hdxf::Entity& t = d.add_table({10, 20, 0}, 4, 3, 8.0, 30.0);
            d.set_table_cell_text(t, 0, 0, "Parts");
            d.set_table_cell_text(t, 1, 0, "Item");
            d.set_table_cell_text(t, 1, 1, "Qty");
            d.set_table_cell_text(t, 2, 0, "Pole \xC3\xA5\xC3\xA4\xC3\xB6");
            std::vector<uint8_t> out = d.save_to_memory(f);
            assert_true(!out.empty(), "saved: " + d.last_error());
            hdxf::Drawing r = reload(out);
            hdxf::Entity* table = table_of(r);
            assert_true(table != nullptr && table->type == hdxf::EntityType::Table, "a typed table");
            const hdxf::TableData& td = table->as_table();
            assert_eq(4, static_cast<int>(td.rows()), "rows");
            assert_eq(3, static_cast<int>(td.columns()), "columns");
            assert_eq("Parts", hdxf::internal::table_value_text(td.cell(0, 0)->value), "the title");
            assert_eq(3, td.cell(0, 0)->merge_columns, "the title row is merged");
            assert_eq(1, td.cell(0, 1)->merged, "and its other cells are merged into it");
            assert_true(td.cell(2, 0)->value.has_value(), "UTF-8 text");
            const hdxf::Object* style = r.table_style(*table);
            assert_true(style != nullptr, "the style Standard");
            assert_true(r.table_cell_style_map(*style) != nullptr, "its CELLSTYLEMAP");
            const hdxf::Object* content = r.table_content(*table);
            const hdxf::Object* geometry = r.table_geometry(*table);
            assert_true(content != nullptr && geometry != nullptr, "the companions");
            const hdxf::TableContentData& c = content->as_table_content();
            assert_eq(3, static_cast<int>(c.columns.size()), "content columns");
            assert_eq(4, static_cast<int>(c.rows.size()), "content rows");
            assert_eq(1, static_cast<int>(c.merges.size()), "the title row's merge");
            assert_eq(12, static_cast<int>(geometry->as_table_geometry().cells.size()), "geometry cells");
            // the checksum of a text cell is the sum of its text
            const auto& item = c.rows[1].cells[0].custom_data;
            assert_true(!item.empty() && item[0].name == hdxf::internal::kTableChecksumName, "the checksum is there");
            assert_eq(hdxf::internal::table_text_checksum("Item"), std::get<double>(item[0].value.value), "and is Item's");
            // the style has the table as a reactor, and the table's dictionary owns the companions
            assert_true(std::find(style->reactors.begin(), style->reactors.end(), table->props.handle) != style->reactors.end(),
                        "the style has the table as a reactor");
            assert_eq(table->props.handle, r.find_object(table->props.xdictionary)->owner, "the extension dictionary's owner");
            assert_true(r.warnings().empty(), "no warning: " + (r.warnings().empty() ? std::string() : r.warnings()[0]));
            // saved again as it was read: the same bytes
            assert_true(r.save_to_memory(f) == out, "written again unchanged, the same bytes");
        }
    }
}

TEST(table_relations_an_empty_table_is_autocads_empty_table_with_no_companions) {
    // doc/facit/fas9_tables.scr: AutoCAD's -TABLE made a table of 4 columns and 7 (+ 2) rows with no text at all
    // (2000 and 2004: AutoCAD gives even an empty table an extension dictionary with its round-trip XRECORD and a
    // round-trip ACDBDATATABLE of the cells, a type of step 9.8: not made here, ej testat)
    for (const char* version : {"2007", "2010", "2013", "2018"}) {
        std::filesystem::path path = facit(std::string("acad_") + version + "_dxf_table_empty.dxf");
        std::vector<uint8_t> file = hdxf_test::read_file_bytes(path);
        if (file.empty()) hdxf_test::skip("no doc/facit (fas9_tables.scr)");
        hdxf::Drawing a;
        assert_true(a.load_from_memory(file.data(), file.size()), "AutoCAD's file");
        hdxf::Entity* at = table_of(a);
        assert_true(at != nullptr, "AutoCAD's table");
        const hdxf::TableData& autocad = at->as_table();
        assert_true(a.table_content(*at) == nullptr, "AutoCAD makes no TABLECONTENT for an empty table");
        assert_true(at->props.xdictionary.empty(), "and no extension dictionary");

        const DxfVersion v = std::string(version) == "2007" ? DxfVersion::R2007 : std::string(version) == "2010" ? DxfVersion::R2010
                             : std::string(version) == "2013" ? DxfVersion::R2013 : DxfVersion::R2018;
        hdxf::Drawing d(v);
        hdxf::Entity& t = d.add_table(autocad.insertion, autocad.rows(), autocad.columns(), 1.0, 1.0);
        t.as_table().row_heights = autocad.row_heights;
        t.as_table().column_widths = autocad.column_widths;
        // The table style of AutoCAD's file too (its margins and text heights): since step 9.5 a row is as high as its
        // cells need (a style of 1.5 margins does not fit rows of 0.6)
        if (const hdxf::Object* style = a.table_style(*at)) {
            if (hdxf::Object* ours = d.find_object(t.as_table().style_handle)) {
                ours->as_table_style() = style->as_table_style();
                // and its cell styles, which carry the margins of the cells
                const hdxf::Object* map = a.table_cell_style_map(*style);
                const hdxf::Object* our_map = d.table_cell_style_map(*ours);
                if (map && our_map) d.find_object(our_map->handle)->as_cell_style_map() = map->as_cell_style_map();
            }
        }
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), "saved: " + d.last_error());
        hdxf::Drawing r = reload(out);
        assert_true(r.table_content(*table_of(r)) == nullptr, "an empty table of ours has none either");
        assert_same_record(masked_record(file, "ACAD_TABLE", at->props.handle), masked_record(out, "ACAD_TABLE", table_of(r)->props.handle),
                           std::string("ACAD_TABLE ") + version);
    }
}

TEST(table_relations_2000_and_2004_tables_have_the_content_the_geometry_and_the_round_trip_data_table) {
    // Step 9.8 made the round-trip DATATABLE: a table with text has, as AutoCAD's own acad_2000/2004_dxf_tables.dxf, the
    // TABLECONTENT and the TABLEGEOMETRY and the data table of its cells (test_document_data.cpp compares them with AutoCAD's)
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004}) {
        hdxf::Drawing d(v);
        hdxf::Entity& t = d.add_table({0, 0, 0}, 2, 2, 10.0, 20.0);
        d.set_table_cell_text(t, 1, 1, "x\n");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), "saved: " + d.last_error());
        hdxf::Drawing r = reload(out);
        hdxf::Entity* table = table_of(r);
        assert_true(table != nullptr && table->type == hdxf::EntityType::Table, "a typed table");
        assert_true(r.table_content(*table) != nullptr && r.table_geometry(*table) != nullptr, "the TABLECONTENT and the TABLEGEOMETRY");
        assert_true(r.table_round_trip_data(*table) != nullptr, "and the data table of the cells");
        assert_true(r.table_style(*table) != nullptr, "its style");
    }
}

TEST(table_relations_rows_and_columns_added_follow_in_content_geometry_and_the_round_trip_record) {
    hdxf::Drawing d;
    if (!load_file(d, facit("acad_2018_dxf_tables.dxf"))) hdxf_test::skip("no doc/facit");
    hdxf::Entity* table = table_of(d);
    hdxf::TableData& t = table->as_table();
    // a row of four cells at the end
    t.row_heights.push_back(9.0);
    for (size_t c = 0; c < t.columns(); ++c) {
        hdxf::TableCell cell;
        cell.value = hdxf::TableValue::from_text("new " + std::to_string(c));
        cell.override_flags = 262144;
        t.cells.push_back(cell);
    }
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), "saved: " + d.last_error());
    hdxf::Drawing r = reload(out);
    hdxf::Entity* nt = table_of(r);
    assert_eq(8, static_cast<int>(nt->as_table().rows()), "rows");
    const hdxf::TableContentData& c = r.table_content(*nt)->as_table_content();
    assert_eq(8, static_cast<int>(c.rows.size()), "content rows");
    assert_eq(9.0, c.rows[7].height, "the new row's height");
    assert_eq(hdxf::internal::table_text_checksum("new 2"), std::get<double>(c.rows[7].cells[2].custom_data[0].value.value), "its checksum");
    const hdxf::TableGeometryData& g = r.table_geometry(*nt)->as_table_geometry();
    assert_eq(8, g.rows, "geometry rows");
    assert_eq(32, static_cast<int>(g.cells.size()), "geometry cells");
    assert_true(r.warnings().empty(), "no warning");
}

// ---------------------------------------------------------------------------
// Taking away: B19
// ---------------------------------------------------------------------------

TEST(table_relations_a_removed_table_takes_its_content_geometry_and_dictionary_with_it) {
    hdxf::Drawing d;
    if (!load_file(d, facit("acad_2018_dxf_tables.dxf"))) hdxf_test::skip("no doc/facit");
    size_t contents = count_objects(d, hdxf::ObjectType::TableContent);
    assert_true(contents > 0, "the file has one");
    const std::string handle = table_of(d)->props.handle;
    auto remove_in = [&](std::vector<hdxf::Entity>& list) {
        list.erase(std::remove_if(list.begin(), list.end(), [&](const hdxf::Entity& e) { return e.props.handle == handle; }), list.end());
    };
    remove_in(d.entities());
    for (auto& b : d.blocks()) remove_in(b.entities);
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), "saved: " + d.last_error());
    hdxf::Drawing r = reload(out);
    assert_true(table_of(r) == nullptr, "the table is gone");
    assert_eq(static_cast<int>(contents) - 1, static_cast<int>(count_objects(r, hdxf::ObjectType::TableContent)), "its TABLECONTENT");
    assert_eq(0, static_cast<int>(count_objects(r, hdxf::ObjectType::TableGeometry)), "its TABLEGEOMETRY");
    // its fields (owned by the TABLECONTENT) went with it; no record points to a handle that is not there
    for (const auto& o : r.objects()) {
        if (o.type == hdxf::ObjectType::TableStyle) {
            for (const auto& re : o.reactors) assert_true(r.find_object(re) != nullptr, "a reactor of the style is a record of the drawing");
        }
    }
}

TEST(table_relations_a_removed_style_becomes_a_null_pointer_and_the_record_keeps_its_shape) {
    hdxf::Drawing d;
    if (!load_file(d, facit("acad_2018_dxf_tables.dxf"))) hdxf_test::skip("no doc/facit");
    hdxf::Entity* table = table_of(d);
    const std::string style = table->as_table().style_handle;
    auto& objects = d.objects();
    // the style, its extension dictionary and what that owns, and its entry in ACAD_TABLESTYLE
    std::set<std::string> gone;
    gone.insert(style);
    if (const hdxf::Object* s = d.find_object(style)) {
        gone.insert(s->xdictionary);
        if (const hdxf::Object* x = d.find_object(s->xdictionary)) {
            for (const auto& e : x->as_dictionary().entries) gone.insert(e.handle);
        }
    }
    objects.erase(std::remove_if(objects.begin(), objects.end(), [&](const hdxf::Object& o) { return gone.count(o.handle) > 0; }), objects.end());
    for (auto& o : objects) {
        if (o.type != hdxf::ObjectType::Dictionary) continue;
        auto& entries = o.as_dictionary().entries;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const hdxf::DictionaryEntry& e) { return gone.count(e.handle) > 0; }), entries.end());
    }
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), "saved: " + d.last_error());
    hdxf::Drawing r = reload(out);
    hdxf::Entity* nt = table_of(r);
    assert_true(nt != nullptr, "the table is still there");
    assert_eq("0", nt->as_table().style_handle, "its style pointer is a null pointer");
    assert_eq(static_cast<int>(table_of(d)->as_table().cells.size()), static_cast<int>(nt->as_table().cells.size()), "the cells are as they were");
    const hdxf::Object* content = r.table_content(*nt);
    assert_true(content != nullptr, "the content is still there");
    assert_eq("0", content->as_table_content().style_handle, "and its style pointer is a null pointer");
}

// ---------------------------------------------------------------------------
// Versions that cannot hold a table
// ---------------------------------------------------------------------------

TEST(table_relations_r12_has_a_named_error_r13_and_r14_are_not_established) {
    {
        hdxf::Drawing d(DxfVersion::R12);
        d.add_table({0, 0, 0}, 2, 2, 5.0, 10.0);
        assert_true(d.save_to_memory(DxfFormat::Ascii).empty(), "R12 holds no table");
        assert_eq("ACAD_TABLE requires DXF 2000 or later", d.last_error(), "the named error");
    }
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14}) {
        hdxf::Drawing d(v);
        d.add_table({0, 0, 0}, 2, 2, 5.0, 10.0);
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(out.empty(), "no AutoCAD file shows a table in R13/R14: nothing is written");
        assert_true(d.last_error().find("not established") != std::string::npos, "the error says so: " + d.last_error());
    }
}
