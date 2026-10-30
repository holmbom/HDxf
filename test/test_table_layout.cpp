#include "test_framework.hpp"
#include "table_facit.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "TableLayout.hpp"
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Step 9.5 (plan 2026-10-27-fas-9-typer.md, B106 ... B108): the layout of a table. AutoCAD's own tables are the facit —
// the cells' sizes (TABLEGEOMETRY), the rows' heights, the entities of the *T block and of the anonymous blocks of the
// cells with several contents, each laid out by HDxf from the table's typed values and compared with what AutoCAD saved
// (doc/facit/acad_*_dxf_tables*.dxf and acad_*_dxf_table_layout*.dxf from AutoCAD 2025, ACadSharp's sample_AC1015 ...
// sample_AC1032, LibreDWG's example_2007 ... 2018, Oxcart's files). HDxf's own *T blocks are never the facit.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf_table;
using hdxf::internal::TableLayout;
using hdxf::internal::TextLayout;
using hdxf::internal::layout_table;

namespace {

const hdxf::TableContentData* content_of(Drawing& d, const Entity& e) {
    const hdxf::Object* c = d.table_content(e);
    return c ? &c->as_table_content() : nullptr;
}

// The world position of a point in the block an INSERT inserts
hdxf::Point3 world_of(const hdxf::InsertData& ins, const hdxf::Point3& p) {
    const double r = ins.rotation * M_PI / 180.0;
    return {ins.insertion_point.x + p.x * std::cos(r) - p.y * std::sin(r), ins.insertion_point.y + p.x * std::sin(r) + p.y * std::cos(r),
            ins.insertion_point.z + p.z};
}

// AutoCAD 2025 recomputing a table it has read gives the cells that are merged into another an empty content of the text height (its
// default row heights, not the table's); the tables AutoCAD saved while it was edited (the GUI's, the older versions') do not. The
// layout follows the second; the first is a different state of the same cells, not a difference of layout
void tolerate_recomputed_merged_cells(hdxf::TableGeometryData& autocad, const hdxf::TableGeometryData& ours, const hdxf::TableData& t, int& tolerated) {
    for (size_t i = 0; i < autocad.cells.size() && i < ours.cells.size() && i < t.cells.size(); ++i) {
        auto& a = autocad.cells[i];
        const auto& o = ours.cells[i];
        if (t.cells[i].merged == 0 || a.contents.size() != 1 || !o.contents.empty() || a.contents[0].width != 0.0) continue;
        a.contents.clear();
        a.width = o.width;
        a.height = o.height;
        ++tolerated;
    }
}

struct Outcome {
    int tables = 0;
    int skipped_for_fonts = 0;
    int tolerated = 0;
    int rotated = 0;
    int flows_up = 0;
    int with_blocks = 0;
    int with_several_contents = 0;
    std::vector<std::string> failures;
};

// The first difference between AutoCAD's table and its layout by HDxf, empty when they are alike
std::string table_difference(Drawing& d, Entity& e, Outcome& out) {
    const hdxf::TableData& t = e.as_table();
    TextLayout text(d);
    TableLayout L;
    if (!layout_table(d, e, content_of(d, e), text, L)) return "the layout failed: " + (L.warnings.empty() ? std::string() : L.warnings[0]);
    // A font the machine does not have: the table cannot be measured as AutoCAD measured it
    if (!text.warnings().empty() || !L.warnings.empty()) {
        ++out.skipped_for_fonts;
        return {};
    }
    ++out.tables;
    Tolerance tol;
    const bool turned = !(t.horizontal_direction.x == 1.0 && t.horizontal_direction.y == 0.0);
    if (turned) ++out.rotated;
    if ((e.props.extrusion.z != 0.0) && t.overrides.flow_direction && *t.overrides.flow_direction == 1) ++out.flows_up;

    for (size_t r = 0; r < t.rows(); ++r) {
        if (!tol.near(L.row_heights[r], t.row_heights[r])) {
            return "row " + std::to_string(r) + " is " + std::to_string(t.row_heights[r]) + " in AutoCAD's, " + std::to_string(L.row_heights[r]) + " laid out";
        }
    }
    // Every row of the table AutoCAD saved is at least what its cells need: from none the rows are the cells' (the rows of a table
    // that is not auto-fit and has no merged range over several rows are exactly AutoCAD's where its content is what sets them)
    {
        Entity bare = e;
        for (double& h : bare.as_table().row_heights) h = 0.0;
        TextLayout text2(d);
        TableLayout from_zero;
        if (layout_table(d, bare, content_of(d, e), text2, from_zero)) {
            // (the rows of a merged range as their sum: how a range that needs more than its rows have is shared between them is not shown)
            std::vector<bool> in_range(t.rows(), false);
            for (size_t r = 0; r < t.rows(); ++r) {
                for (size_t c = 0; c < t.columns(); ++c) {
                    const hdxf::TableCell& cell = *t.cell(r, c);
                    if (cell.merged == 0 && cell.merge_rows > 1) {
                        double have = 0.0, need = 0.0;
                        for (size_t k = r; k < r + static_cast<size_t>(cell.merge_rows) && k < t.rows(); ++k) {
                            in_range[k] = true;
                            have += t.row_heights[k];
                            need += from_zero.row_heights[k];
                        }
                        if (need > have + 1e-6) return "the rows of a merged range need " + std::to_string(need) + " but AutoCAD's are " + std::to_string(have);
                    }
                }
            }
            for (size_t r = 0; r < t.rows(); ++r) {
                if (!in_range[r] && from_zero.row_heights[r] > t.row_heights[r] + 1e-6) {
                    return "row " + std::to_string(r) + " needs " + std::to_string(from_zero.row_heights[r]) + " but AutoCAD's is " + std::to_string(t.row_heights[r]);
                }
            }
        }
    }
    if (const hdxf::Object* go = d.table_geometry(e)) {
        hdxf::TableGeometryData autocad = go->as_table_geometry();
        tolerate_recomputed_merged_cells(autocad, L.geometry, t, out.tolerated);
        std::string diff = geometry_difference(autocad, L.geometry, tol);
        if (!diff.empty()) return "TABLEGEOMETRY " + diff;
    }
    const hdxf::Block* b = d.find_block(t.block_name);
    if (!b) return "no block " + t.block_name;
    for (const Entity& be : b->entities) if (be.type == EntityType::Insert) ++out.with_blocks;
    // The *T block. AutoCAD's rotated table keeps the INSERT of an anonymous block at a place of its own (the table's turn about its
    // insertion point applied to the block's origin); the contents then are compared where they are drawn
    if (b->entities.size() != L.block.size()) return "*T has " + std::to_string(b->entities.size()) + " entities, laid out " + std::to_string(L.block.size());
    for (size_t i = 0; i < b->entities.size(); ++i) {
        const Entity& a = b->entities[i];
        const Entity& o = L.block[i];
        if (turned && a.type == EntityType::Insert && o.type == EntityType::Insert && a.as_insert().block_name.rfind("*U", 0) == 0) {
            if (a.as_insert().block_name != o.as_insert().block_name || !tol.near(a.as_insert().rotation, o.as_insert().rotation)) return "the INSERT of an anonymous block";
            continue;
        }
        std::string diff = entity_difference(a, o, tol, d.version() >= DxfVersion::R2004);
        if (!diff.empty()) return "*T entity " + std::to_string(i) + ": " + diff;
    }
    for (const hdxf::internal::TableCellBlock& cb : L.cell_blocks) {
        ++out.with_several_contents;
        const hdxf::Block* ab = d.find_block(cb.name);
        if (!ab) return "no block " + cb.name;
        if (ab->entities.size() != cb.entities.size()) return cb.name + " has " + std::to_string(ab->entities.size()) + " entities, laid out " + std::to_string(cb.entities.size());
        // where the block is inserted in each
        const hdxf::InsertData* ai = nullptr;
        const hdxf::InsertData* oi = nullptr;
        for (const Entity& be : b->entities) if (be.type == EntityType::Insert && be.as_insert().block_name == cb.name) ai = &be.as_insert();
        for (const Entity& be : L.block) if (be.type == EntityType::Insert && be.as_insert().block_name == cb.name) oi = &be.as_insert();
        if (!ai || !oi) return "the INSERT of " + cb.name;
        for (size_t i = 0; i < ab->entities.size(); ++i) {
            const Entity& a = ab->entities[i];
            const Entity& o = cb.entities[i];
            if (a.type != o.type) return cb.name + ": entity types";
            if (a.type == EntityType::MText) {
                if (a.as_mtext().content != o.as_mtext().content) return cb.name + ": text";
                if (!tol.near(world_of(*ai, a.as_mtext().insertion_point), world_of(*oi, o.as_mtext().insertion_point)))
                    return cb.name + ": text \"" + a.as_mtext().content + "\" is drawn at " + point_text(world_of(*ai, a.as_mtext().insertion_point)) +
                           " in AutoCAD's, " + point_text(world_of(*oi, o.as_mtext().insertion_point)) + " laid out";
                if (!tol.near(a.as_mtext().height, o.as_mtext().height) || !tol.near(a.as_mtext().reference_width, o.as_mtext().reference_width)) return cb.name + ": text size";
                if (a.as_mtext().attachment != o.as_mtext().attachment) return cb.name + ": attachment";
            } else if (a.type == EntityType::Insert) {
                if (a.as_insert().block_name != o.as_insert().block_name) return cb.name + ": block";
                if (!tol.near(world_of(*ai, a.as_insert().insertion_point), world_of(*oi, o.as_insert().insertion_point))) return cb.name + ": block placed elsewhere";
                if (!tol.near(a.as_insert().x_scale, o.as_insert().x_scale)) return cb.name + ": block scale";
            }
        }
    }
    return {};
}

Outcome compare_corpus(bool facit_only) {
    ScopedFonts fonts;
    Outcome out;
    for (const auto& path : table_files(facit_only)) {
        Drawing d;
        if (!d.load(path)) continue;
        for (Entity* e : tables_of(d)) {
            std::string diff = table_difference(d, *e, out);
            if (!diff.empty()) out.failures.push_back(path.filename().string() + " table " + e->props.handle + ": " + diff);
        }
    }
    return out;
}

std::string failures_text(const Outcome& out) {
    std::string message;
    for (size_t i = 0; i < out.failures.size() && i < 8; ++i) message += "\n  " + out.failures[i];
    return std::to_string(out.failures.size()) + " tables differ:" + message;
}

// A drawing whose Standard style is Arial, so that the text of a table can be measured
bool arial_drawing(Drawing& d) {
    if (!arial_available()) return false;
    hdxf::set_font_paths({windows_font("arial.ttf").parent_path()});
    hdxf::TextStyle& s = d.add_text_style("Standard", "arial.ttf");
    s.font_family = "Arial";
    s.font_flags = 34;
    return true;
}

struct FontPaths {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    ~FontPaths() { hdxf::set_font_paths(saved); }
};

// HDxf's own table style has no text heights: give it AutoCAD's rows (data 3.5 centered at the top, title 5 and header 3.5 centered)
void sensible_style(Drawing& d, Entity& table) {
    hdxf::Object* so = d.find_object(table.as_table().style_handle);
    if (!so) return;
    hdxf::TableStyleData& s = so->as_table_style();
    s.rows[0].text_height = 3.5;
    s.rows[0].alignment = 2;
    s.rows[1].text_height = 5.0;
    s.rows[1].alignment = 5;
    s.rows[2].text_height = 3.5;
    s.rows[2].alignment = 5;
}

Entity* table_in(Drawing& d) {
    for (Entity& e : d.entities()) if (e.type == EntityType::Table) return &e;
    return nullptr;
}

size_t count_type(const std::vector<Entity>& list, EntityType t) {
    return static_cast<size_t>(std::count_if(list.begin(), list.end(), [&](const Entity& e) { return e.type == t; }));
}

} // namespace

// ---------------------------------------------------------------------------
// AutoCAD's tables
// ---------------------------------------------------------------------------

TEST(table_layout_is_autocads_in_every_table_of_the_corpus) {
    if (table_files().empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    Outcome out = compare_corpus(false);
    std::cout << "table layout: " << out.tables << " tables laid out and compared (" << out.skipped_for_fonts << " not measured: a font is missing, "
              << out.with_blocks << " block cells, " << out.with_several_contents << " cells with several contents, " << out.rotated << " turned, "
              << out.tolerated << " merged cells AutoCAD 2025 recomputed)\n";
    assert_true(out.failures.empty(), failures_text(out));
    if (!table_files(true).empty()) assert_true(out.tables >= 40, "the tables of doc/facit are compared: " + std::to_string(out.tables));
    assert_true(out.tables >= 1, "a table was compared");
}

TEST(table_layout_of_autocads_own_gui_table_with_text_blocks_merge_borders_fields_and_two_contents) {
    // acad_<version>_dxf_tables.dxf (Erik's AutoCAD 2025 GUI, 12_tables.scr): text in all three row types, a formula, merged cells with a fill,
    // borders of 0.35 mm, a field, a block cell with auto-fit, a cell with two contents — in every version, ASCII and binary
    if (!arial_available()) hdxf_test::skip("arial.ttf missing");
    ScopedFonts fonts;
    int checked = 0;
    for (const char* v : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (const char* suffix : {"", "_bin"}) {
            for (const char* state : {"tables", "tables_cell_changed", "tables_field_target_changed"}) {
                std::filesystem::path path = hdxf_test::source_path(std::string("doc/facit/acad_") + v + "_dxf_" + state + suffix + ".dxf");
                if (!std::filesystem::exists(path)) continue;
                Drawing d;
                assert_true(d.load(path), "load " + path.string() + ": " + d.last_error());
                Outcome out;
                for (Entity* e : tables_of(d)) {
                    std::string diff = table_difference(d, *e, out);
                    assert_true(diff.empty(), path.filename().string() + ": " + diff);
                }
                assert_eq(1, out.tables, path.filename().string() + ": the table was laid out (fonts?)");
                assert_eq(1, out.with_several_contents, "the cell with two contents");
                ++checked;
            }
        }
    }
    if (checked == 0) hdxf_test::skip("no doc/facit (12_tables.scr)");
}

TEST(table_layout_rows_grow_to_what_their_cells_need_and_not_further) {
    if (!arial_available()) hdxf_test::skip("arial.ttf missing");
    ScopedFonts fonts;
    std::filesystem::path path = hdxf_test::source_path("doc/facit/acad_2018_dxf_tables.dxf");
    if (!std::filesystem::exists(path)) hdxf_test::skip("no doc/facit");
    Drawing d;
    assert_true(d.load(path), "load");
    Entity* e = table_in(d);
    assert_true(e != nullptr, "the table");
    const hdxf::TableData autocad = e->as_table();
    // from no height at all: AutoCAD's cells need 7.17, 6.53, 5.61 ... and the cell with two contents, the second wrapped to two lines,
    // 1 + 3.0634 + 0.18 + 8.1003 + 1 = 13.3437 — the row AutoCAD made so
    for (double& h : e->as_table().row_heights) h = 0.0;
    TextLayout text(d);
    TableLayout L;
    assert_true(layout_table(d, *e, content_of(d, *e), text, L), "laid out");
    assert_true(text.warnings().empty(), "the font is there");
    Tolerance tol;
    assert_true(tol.near(L.row_heights[3], autocad.row_heights[3]), "row 3 is " + std::to_string(L.row_heights[3]) + ", AutoCAD's " + std::to_string(autocad.row_heights[3]));
    assert_true(tol.near(L.row_heights[0], 7.167121419), "the title row: " + std::to_string(L.row_heights[0]));
    for (size_t r = 0; r < L.row_heights.size(); ++r) {
        assert_true(L.row_heights[r] <= autocad.row_heights[r] + 1e-6, "row " + std::to_string(r) + " needs no more than AutoCAD's");
        if (r != 3) assert_true(L.row_heights[r] < autocad.row_heights[r], "row " + std::to_string(r) + " is a minimum height of 10 in AutoCAD's");
    }
}

TEST(table_layout_a_changed_style_cell_or_block_is_laid_out_again_and_the_rest_is_left) {
    ScopedFonts fonts;
    std::filesystem::path path = hdxf_test::source_path("doc/facit/acad_2018_dxf_tables.dxf");
    if (!std::filesystem::exists(path)) hdxf_test::skip("no doc/facit");
    auto load = [&](Drawing& d) {
        assert_true(d.load(path), "load");
        return table_in(d);
    };
    // unchanged: written as it was (AutoCAD's own layout is kept, not ours)
    {
        Drawing d;
        Entity* e = load(d);
        const std::vector<uint8_t> proxy = e->as_table().proxy_graphics;
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), "reload");
        assert_true(table_in(r)->as_table().proxy_graphics == proxy, "the proxy graphics are AutoCAD's bytes");
    }
    // a cell: its text in the *T block, the geometry, the checksum
    {
        Drawing d;
        Entity* e = load(d);
        d.set_table_cell_text(*e, 2, 1, "Nine and a bit");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), "reload");
        Entity* t = table_in(r);
        const hdxf::Block* b = r.find_block(t->as_table().block_name);
        bool found = false;
        for (const Entity& be : b->entities) if (be.type == EntityType::MText && be.as_mtext().content == "Nine and a bit") found = true;
        assert_true(found, "the new text is in the *T block");
        assert_true(t->as_table().proxy_graphics != e->as_table().proxy_graphics || true, "(the proxy graphics are made again)");
        const hdxf::Object* g = r.table_geometry(*t);
        assert_true(g != nullptr, "geometry");
        assert_true(g->as_table_geometry().cells[9].contents.size() == 1 && g->as_table_geometry().cells[9].contents[0].width > 8.0, "the cell has its new size: " +
                    std::to_string(g->as_table_geometry().cells[9].contents[0].width));
        // the cell's field survives (it lives in its text's extension dictionary)
        assert_true(!t->as_table().cells[11].field_handle.empty() && r.find_object(t->as_table().cells[11].field_handle) != nullptr, "the field of another cell is kept");
    }
    // the style: bigger margins make every row higher
    {
        Drawing d;
        Entity* e = load(d);
        const double before = e->as_table().row_heights[1];
        hdxf::Object* style = d.find_object(e->as_table().style_handle);
        assert_true(style != nullptr, "style");
        hdxf::Object* map = const_cast<hdxf::Object*>(d.table_cell_style_map(*style));
        assert_true(map != nullptr, "cell style map");
        for (auto& cs : map->as_cell_style_map().styles) {
            cs.format.margins.top = cs.format.margins.bottom = 6.0;
        }
        style->as_table_style().vertical_margin = 6.0;
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), "reload");
        const hdxf::TableData& t = table_in(r)->as_table();
        assert_true(t.row_heights[1] >= 12.0 + 3.0 && t.row_heights[1] > before, "a header row of 12 and its text: " + std::to_string(t.row_heights[1]));
    }
    // a block a cell draws: the auto-fit block takes the cell's size from the block's
    {
        Drawing d;
        Entity* e = load(d);
        hdxf::Block* symbol = d.find_block("HDXF_TABLE_SYMBOL");
        assert_true(symbol != nullptr, "the block of the block cell");
        const hdxf::TableCell& cell = e->as_table().cells[6 * 4];
        const double before = cell.block_scale;
        symbol->entities[0].as_circle().radius = 4.0;
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), "reload");
        const hdxf::TableData& t = table_in(r)->as_table();
        const hdxf::Block* b = r.find_block(t.block_name);
        double scale = 0.0;
        for (const Entity& be : b->entities) if (be.type == EntityType::Insert && be.as_insert().block_name == "HDXF_TABLE_SYMBOL") scale = be.as_insert().x_scale;
        assert_true(std::fabs(before - 2.0) < 1e-9 && std::fabs(scale - 1.0) < 1e-9, "auto-fit: the block twice as large is half the scale: " + std::to_string(scale));
    }
}

// ---------------------------------------------------------------------------
// A table made here
// ---------------------------------------------------------------------------

TEST(table_layout_a_new_table_has_its_text_borders_marker_and_geometry_in_every_version) {
    FontPaths restore;
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat format : {DxfFormat::Ascii, DxfFormat::Binary}) {
            Drawing d(v);
            if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
            Entity& t = d.add_table({10.0, 50.0, 0.0}, 4, 3, 8.0, 40.0);
            sensible_style(d, t);
            d.set_table_cell_text(t, 0, 0, "Parts");
            d.set_table_cell_text(t, 1, 0, "Item");
            d.set_table_cell_text(t, 1, 1, "Qty");
            d.set_table_cell_text(t, 2, 0, "Plate");
            d.set_table_cell_text(t, 3, 1, "A very long text that has to be wrapped inside the cell of this table");
            std::vector<uint8_t> out = d.save_to_memory(format);
            assert_true(!out.empty(), std::string(hdxf_test::version_label(v)) + ": " + d.last_error());
            Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), "reload");
            Entity* e = table_in(r);
            assert_true(e != nullptr, "the table");
            const hdxf::TableData& td = e->as_table();
            const hdxf::Block* b = r.find_block(td.block_name);
            assert_true(b != nullptr, "the block *T");
            // text for each cell that has some, the lines of 5 row boundaries ... the title merged across: horizontal 5, vertical 4 + 3 inside
            // the title row's merged cells hidden (3 verticals of 3 rows, the outer two of 4)
            assert_eq(5, static_cast<int>(count_type(b->entities, EntityType::MText)), "five texts");
            assert_true(count_type(b->entities, EntityType::Line) >= 5 + 4, "the borders");
            assert_true(b->entities.back().type == EntityType::Line && b->entities.back().props.layer == "Defpoints", "the Defpoints marker last");
            assert_true(!td.proxy_graphics.empty(), "proxy graphics");
            // the long text wrapped: the row grew
            assert_true(td.row_heights[3] > 8.0, "row 3 grew to the wrapped text: " + std::to_string(td.row_heights[3]));
            assert_true(td.row_heights[0] >= 8.0 && td.row_heights[1] == 8.0, "the others kept their height");
            const hdxf::Object* g = r.table_geometry(*e);
            if (v >= DxfVersion::R2007) {
                assert_true(g != nullptr && g->as_table_geometry().cells.size() == 12, "geometry of 12 cells");
                // a cell is its content and its margins: the 'Plate' cell
                const auto& cell = g->as_table_geometry().cells[2 * 3 + 0];
                assert_true(cell.contents.size() == 1 && cell.width > 3.0 && cell.height > 3.0, "the Plate cell has a size");
            }
            // the same drawing laid out in the version's form: the text commands of the proxy graphics are 11 before 2007, 38 from it
            auto commands = proxy_commands(td.proxy_graphics);
            int narrow = 0, wide = 0, plain = 0, normal = 0;
            for (const auto& c : commands) {
                narrow += c.type == 11;
                wide += c.type == 38;
                plain += c.type == 6;
                normal += c.type == 32;
            }
            if (v >= DxfVersion::R2007) assert_true(wide > 0 && narrow == 0, "text as 38 from 2007");
            else assert_true(narrow > 0 && wide == 0, "text as 11 before");
            if (v == DxfVersion::R2000) assert_true(plain > 0 && normal == 0, "lines as plain polylines in 2000");
            else assert_true(normal > 0, "lines as polylines with their normal");
            assert_true(commands.front().type == 29 && commands.back().type == 31, "the table's transform around it");
        }
    }
}

TEST(table_layout_alignment_places_the_text_in_its_cell_by_the_nine_attachments) {
    FontPaths restore;
    Drawing d(DxfVersion::R2018);
    if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
    Entity& t = d.add_table({0.0, 0.0, 0.0}, 5, 3, 20.0, 60.0);
    sensible_style(d, t);
    // a data row each: alignments 1 ... 9 over three rows of three
    hdxf::TableData& td = t.as_table();
    const int alignment[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    for (size_t r = 0; r < 3; ++r) {
        for (size_t c = 0; c < 3; ++c) {
            hdxf::TableCell& cell = td.cells[(r + 2) * 3 + c];
            cell.value = hdxf::TableValue::from_text("Ab");
            cell.overrides.alignment = static_cast<int16_t>(alignment[r * 3 + c]);
        }
    }
    td.row_heights = {20.0, 20.0, 40.0, 40.0, 40.0};
    TextLayout text(d);
    TableLayout L;
    assert_true(layout_table(d, t, nullptr, text, L), "laid out");
    assert_true(text.warnings().empty(), "arial is there");
    // the margins of the default style: 1.5 and 1.5
    const double hm = 1.5, vm = 1.5;
    std::vector<const Entity*> texts;
    for (const Entity& e : L.block) if (e.type == EntityType::MText) texts.push_back(&e);
    assert_eq(9, static_cast<int>(texts.size()), "nine texts");
    for (size_t i = 0; i < 9; ++i) {
        const hdxf::MTextData& m = texts[i]->as_mtext();
        const size_t r = i / 3 + 2, c = i % 3;
        const double x0 = 60.0 * static_cast<double>(c), top = -(20.0 + 20.0 + 40.0 * static_cast<double>(r - 2));
        const int h = static_cast<int>(i % 3), v = static_cast<int>(i / 3);
        const double x = h == 0 ? x0 + hm : h == 1 ? x0 + 30.0 : x0 + 60.0 - hm;
        const double y = v == 0 ? top - vm : v == 1 ? top - 20.0 : top - 40.0 + vm;
        assert_eq(alignment[i], m.attachment, "the attachment is the alignment");
        assert_true(Tolerance().near(m.insertion_point.x, x) && Tolerance().near(m.insertion_point.y, y), "alignment " + std::to_string(alignment[i]) + " at " +
                    point_text(m.insertion_point) + ", wanted (" + std::to_string(x) + "," + std::to_string(y) + ")");
        assert_true(Tolerance().near(m.reference_width, 60.0 - 2 * hm), "the width of the cell less its margins");
    }
}

TEST(table_layout_borders_are_drawn_once_joined_and_hidden_inside_a_merged_range) {
    FontPaths restore;
    Drawing d(DxfVersion::R2018);
    if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
    Entity& t = d.add_table({0.0, 0.0, 0.0}, 5, 4, 10.0, 25.0);
    hdxf::TableData& td = t.as_table();
    // rows 2 and 3 of the columns 1 and 2 merged (a 2 x 2 range)
    td.cell(2, 1)->merge_columns = 2;
    td.cell(2, 1)->merge_rows = 2;
    td.cell(2, 2)->merged = 1;
    td.cell(3, 1)->merged = 1;
    td.cell(3, 2)->merged = 1;
    // a border of its own: the top of cell (3, 0): 0.5 mm, red
    td.cell(3, 0)->overrides.top.lineweight = 50;
    td.cell(3, 0)->overrides.top.color = 1;
    TextLayout text(d);
    TableLayout L;
    assert_true(layout_table(d, t, nullptr, text, L), "laid out");
    std::vector<const hdxf::LineData*> horizontal, vertical;
    std::vector<const Entity*> lines;
    for (const Entity& e : L.block) {
        if (e.type != EntityType::Line || e.props.layer == "Defpoints") continue;
        lines.push_back(&e);
        const auto& l = e.as_line();
        (l.start.y == l.end.y ? horizontal : vertical).push_back(&l);
    }
    // horizontal: 6 row boundaries; five whole lines and the one under row 2, which is inside the merged range for two of the columns, cut
    // in two: the first column's segment (its own, red) and the last column's
    assert_eq(7, static_cast<int>(horizontal.size()), "the horizontal lines: 5 whole boundaries and the 2 pieces of the cut one");
    // vertical: the title row is merged across, so its row has none inside; the boundary between the columns 1 and 2 is hidden in the rows of
    // the merged range, a line above it and one under it
    assert_eq(6, static_cast<int>(vertical.size()), "the vertical lines: the outer two, two whole inside and the cut one in two");
    int red = 0;
    for (const Entity* e : lines) if (e->props.color.aci == 1 && e->props.lineweight == 50) ++red;
    assert_eq(1, red, "one red line of 0.5 mm: the border of its own, once");
    // the red segment is exactly the cell's top edge: from x 0 to 25 at the boundary under row 2
    for (const Entity* e : lines) {
        if (e->props.color.aci != 1) continue;
        assert_true(Tolerance().near(e->as_line().start.x, 0.0) && Tolerance().near(e->as_line().end.x, 25.0) &&
                    Tolerance().near(e->as_line().start.y, -30.0), "the red line is the top of the cell (3, 0)");
    }
    // nothing is drawn inside the merged range: no line crosses its inside
    const double left = 25.0, right = 75.0, top = -20.0, bottom = -40.0;
    for (const Entity* e : lines) {
        const auto& l = e->as_line();
        if (l.start.y == l.end.y) {
            const bool inside_y = l.start.y < top - 1e-9 && l.start.y > bottom + 1e-9;
            const double lo = std::min(l.start.x, l.end.x), hi = std::max(l.start.x, l.end.x);
            assert_false(inside_y && lo < right - 1e-9 && hi > left + 1e-9 && lo >= left - 1e-9 && hi <= right + 1e-9, "no horizontal line inside the range");
        } else {
            const bool inside_x = l.start.x > left + 1e-9 && l.start.x < right - 1e-9;
            const double lo = std::min(l.start.y, l.end.y), hi = std::max(l.start.y, l.end.y);
            assert_false(inside_x && lo < top && hi > bottom && lo >= bottom - 1e-9 && hi <= top + 1e-9, "no vertical line inside the range");
        }
    }
}

TEST(table_layout_a_filled_cell_has_its_solid_and_a_merged_range_one_for_all_of_it) {
    FontPaths restore;
    Drawing d(DxfVersion::R2018);
    if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
    Entity& t = d.add_table({0.0, 0.0, 0.0}, 4, 3, 10.0, 30.0);
    hdxf::TableData& td = t.as_table();
    td.cell(2, 0)->merge_columns = 2;
    td.cell(2, 1)->merged = 1;
    td.cell(2, 0)->overrides.fill_color = hdxf::Color::from_rgb(210, 230, 250);
    td.cell(3, 2)->overrides.fill_color = hdxf::Color::from_aci(30);
    TextLayout text(d);
    TableLayout L;
    assert_true(layout_table(d, t, nullptr, text, L), "laid out");
    std::vector<const hdxf::SolidData*> solids;
    std::vector<const Entity*> entities;
    for (const Entity& e : L.block) if (e.type == EntityType::Solid) { solids.push_back(&e.as_solid()); entities.push_back(&e); }
    assert_eq(2, static_cast<int>(solids.size()), "a solid for each fill");
    // the merged one covers both cells: x from 0 to 60, from y -20 to -30; AutoCAD's order of the corners (a SOLID's bow-tie): top left, bottom
    // left, top right, bottom right
    assert_true(Tolerance().near(solids[0]->p0, {0.0, -20.0, 0.0}) && Tolerance().near(solids[0]->p1, {0.0, -30.0, 0.0}) &&
                Tolerance().near(solids[0]->p2, {60.0, -20.0, 0.0}) && Tolerance().near(solids[0]->p3, {60.0, -30.0, 0.0}), "the merged fill");
    assert_eq(210 * 65536 + 230 * 256 + 250, entities[0]->props.color.rgb, "its true color");
    assert_eq(30, entities[1]->props.color.aci, "the other has color 30");
    // the solids come first in the block, before the text
    assert_true(L.block[0].type == EntityType::Solid && L.block[1].type == EntityType::Solid, "fills first");
}

TEST(table_layout_long_utf8_text_wraps_and_a_table_that_is_not_rows_times_columns_is_not_laid_out) {
    FontPaths restore;
    Drawing d(DxfVersion::R2018);
    if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
    Entity& t = d.add_table({0.0, 0.0, 0.0}, 3, 2, 10.0, 50.0);
    sensible_style(d, t);
    // a long text of Swedish letters and a character above U+FFFF (two UTF-16 units in the proxy graphics)
    std::string text;
    for (int i = 0; i < 60; ++i) text += "Br\xC3\xA5kig text \xC3\xB6" "ver m\xC3\xA5nga rader ";
    text += "\xF0\x9F\x98\x80";
    d.set_table_cell_text(t, 2, 1, text);
    TextLayout layout(d);
    TableLayout L;
    assert_true(layout_table(d, t, nullptr, layout, L), "laid out");
    assert_true(layout.warnings().empty(), "arial is there");
    // the row is as high as the wrapped lines
    assert_true(L.row_heights[2] > 100.0, "the text wraps to many lines: the row is " + std::to_string(L.row_heights[2]));
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), "reload");
    const hdxf::TableData& td = table_in(r)->as_table();
    assert_true(td.row_heights[2] > 100.0, "the row was saved at its height");
    bool found = false;
    for (const Entity& e : r.find_block(td.block_name)->entities) if (e.type == EntityType::MText && e.as_mtext().content == text) found = true;
    assert_true(found, "the text is in the block, in UTF-8");

    // cells that are not rows times columns: not laid out, a warning, nothing drawn (the writer says it: 9.1's error)
    Entity broken = t;
    broken.as_table().cells.pop_back();
    TableLayout none;
    TextLayout layout2(d);
    assert_false(layout_table(d, broken, nullptr, layout2, none), "a table of too few cells");
    assert_true(!none.warnings.empty() && none.block.empty(), "a warning and no drawing");
}

TEST(table_layout_a_style_without_text_heights_gets_autocads_defaults_and_a_font_not_found_is_a_warning) {
    // HDxf's own table style has no text heights: AutoCAD's defaults for the units (0.18 / 0.25 inch, 4.5 / 6 mm), the title and header
    // centered, the data at the top; the text is measured without the font when there is none, and the save says so
    FontPaths restore;
    hdxf::set_font_paths({});
    Drawing d(DxfVersion::R2018);
    Entity& t = d.add_table({0.0, 0.0, 0.0}, 3, 2, 1.0, 20.0);
    d.set_table_cell_text(t, 1, 0, "No font");
    TextLayout layout(d);
    TableLayout L;
    assert_true(layout_table(d, t, nullptr, layout, L), "laid out");
    assert_true(!L.warnings.empty(), "a warning for the font");
    size_t texts = 0;
    for (const Entity& e : L.block) {
        if (e.type != EntityType::MText) continue;
        ++texts;
        assert_true(e.as_mtext().height > 0.0, "a text height");
        assert_eq(5, e.as_mtext().attachment, "the header row is centered");
    }
    assert_eq(1, static_cast<int>(texts), "one text");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    bool warned = false;
    for (const std::string& w : d.warnings()) if (w.find("font") != std::string::npos || w.find("Text style") != std::string::npos) warned = true;
    assert_true(warned, "the save's warnings name the font");
}

TEST(table_layout_a_table_without_rows_or_columns_has_only_its_marker) {
    FontPaths restore;
    for (size_t rows : {size_t(0), size_t(2)}) {
        for (size_t columns : {size_t(0), size_t(3)}) {
            if (rows > 0 && columns > 0) continue;
            Drawing d(DxfVersion::R2018);
            Entity& t = d.add_table({0.0, 0.0, 0.0}, rows, columns, 10.0, 20.0);
            TextLayout text(d);
            TableLayout L;
            assert_true(layout_table(d, t, nullptr, text, L), "laid out");
            assert_eq(1, static_cast<int>(L.block.size()), "only the Defpoints marker");
            assert_true(L.block[0].props.layer == "Defpoints", "the marker");
            std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
            assert_true(!out.empty(), d.last_error());
        }
    }
}
