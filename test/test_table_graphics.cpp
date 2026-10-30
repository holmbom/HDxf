#include "test_framework.hpp"
#include "table_facit.hpp"
#include "type_version_matrix.hpp"
#include "hdxf.hpp"
#include "TableLayout.hpp"
#include "extents.hpp"
#include <cmath>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

// Step 9.5 (plan 2026-10-27-fas-9-typer.md, B106, B107, B108): what a table draws — its proxy graphics (the AcGi record
// of its *T block that programs that do not know the table show), its extents, and the *T block's follow-up when the table
// moves or a cell, the style or a block changes. The facit is AutoCAD's own files: its proxy graphics command by command
// (doc/facit/acad_*_dxf_tables*.dxf, acad_*_dxf_table_layout*.dxf, acad_*_dxf_table_empty*.dxf: AutoCAD 2025; the older
// AutoCAD versions' files of the corpus), its $EXTMIN/$EXTMAX of a drawing that has nothing but tables.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace hdxf_table;
using hdxf::internal::TableLayout;
using hdxf::internal::TextLayout;
using hdxf::internal::layout_table;
using hdxf::internal::table_proxy_graphics;

namespace {

const hdxf::TableContentData* content_of(Drawing& d, const Entity& e) {
    const hdxf::Object* c = d.table_content(e);
    return c ? &c->as_table_content() : nullptr;
}

// Written by AutoCAD 2025 (the GUI or its core): its proxy graphics have a marker before each text
bool from_autocad_2025(const std::filesystem::path& p) {
    const std::string s = p.generic_string();
    return s.find("doc/facit/") != std::string::npos && s.find("/web/") == std::string::npos;
}

// The proxy graphics compared command by command; what is not HDxf's to reproduce is named: the true color of a fill in a DXF 2000 file (the
// file has only its index), the INSERT of a turned table's anonymous block (AutoCAD puts its origin where the turn about the table's
// insertion point took it, HDxf leaves it at the origin: the same drawing)
struct ProxyOutcome {
    int tables = 0;
    int skipped_for_fonts = 0;
    int true_color_of_2000 = 0;
    int turned_anonymous_blocks = 0;
    int without_zero_markers = 0;
    std::vector<std::string> failures;
};

std::string proxy_of_table_difference(Drawing& d, Entity& e, bool markers, ProxyOutcome& out) {
    const hdxf::TableData& t = e.as_table();
    TextLayout text(d);
    TableLayout L;
    if (!layout_table(d, e, content_of(d, e), text, L)) return "the layout failed";
    if (!text.warnings().empty() || !L.warnings.empty()) {
        ++out.skipped_for_fonts;
        return {};
    }
    ++out.tables;
    const bool unicode = d.version() >= DxfVersion::R2007;
    std::vector<uint8_t> ours = table_proxy_graphics(d, e, L.block, L.field_texts, L.text_sizes, text, unicode);
    const bool turned = !(t.horizontal_direction.x == 1.0 && t.horizontal_direction.y == 0.0);
    // (AutoCAD 2025's files after an UNDO - the mirrored tables of fas9_table_layout.scr - have no marker 0 before a text, the others have it)
    bool zero_markers_skipped = false;
    std::vector<ProxyCommand> a = proxy_commands(t.proxy_graphics, !markers), b = proxy_commands(ours, !markers);
    if (a.size() != b.size() && markers) {
        a = proxy_commands(t.proxy_graphics, false, true);
        b = proxy_commands(ours, false, true);
        zero_markers_skipped = true;
    }
    if (zero_markers_skipped) ++out.without_zero_markers;
    if (a.size() != b.size()) return std::to_string(a.size()) + " commands against " + std::to_string(b.size());
    bool inside_turned_block = false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (command_equal(a[i], b[i])) continue;
        // the true color of a fill in 2000
        if (d.version() == DxfVersion::R2000 && a[i].type == 22 && b[i].type == 22 && a[i].data.size() == 4 && b[i].data.size() == 4 && a[i].data[3] == 0xC2 &&
            b[i].data[3] == 0xC3) {
            ++out.true_color_of_2000;
            continue;
        }
        // a turned table's anonymous block: its INSERT's transform and what is inside it
        if (turned && !L.cell_blocks.empty()) {
            if (a[i].type == 29 && b[i].type == 29 && !inside_turned_block) {
                inside_turned_block = true;
                ++out.turned_anonymous_blocks;
                continue;
            }
            if (inside_turned_block) continue;
        }
        return "command " + std::to_string(i) + ": type " + std::to_string(a[i].type) + " (" + std::to_string(a[i].data.size()) + " bytes) against type " +
               std::to_string(b[i].type) + " (" + std::to_string(b[i].data.size()) + " bytes), of " + std::to_string(a.size());
    }
    return {};
}

ProxyOutcome compare_proxies(bool facit_only) {
    ScopedFonts fonts;
    ProxyOutcome out;
    for (const auto& path : table_files(facit_only)) {
        Drawing d;
        if (!d.load(path)) continue;
        const bool markers = from_autocad_2025(path);
        for (Entity* e : tables_of(d)) {
            std::string diff = proxy_of_table_difference(d, *e, markers, out);
            if (!diff.empty()) out.failures.push_back(path.filename().string() + " table " + e->props.handle + ": " + diff);
        }
    }
    return out;
}

struct FontPaths {
    std::vector<std::filesystem::path> saved = hdxf::font_paths();
    ~FontPaths() { hdxf::set_font_paths(saved); }
};

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

bool arial_drawing(Drawing& d) {
    if (!arial_available()) return false;
    hdxf::set_font_paths({windows_font("arial.ttf").parent_path()});
    hdxf::TextStyle& s = d.add_text_style("Standard", "arial.ttf");
    s.font_family = "Arial";
    s.font_flags = 34;
    return true;
}

Entity* table_in(Drawing& d) {
    for (Entity& e : d.entities()) if (e.type == EntityType::Table) return &e;
    return nullptr;
}

std::vector<uint8_t> proxy_bytes_of_file(const std::filesystem::path& path, size_t nth = 0) {
    Drawing d;
    if (!d.load(path)) return {};
    std::vector<Entity*> tables = tables_of(d);
    return nth < tables.size() ? tables[nth]->as_table().proxy_graphics : std::vector<uint8_t>();
}

} // namespace

// ---------------------------------------------------------------------------
// The proxy graphics
// ---------------------------------------------------------------------------

TEST(table_graphics_proxy_graphics_are_autocads_command_by_command_in_every_table_of_the_corpus) {
    if (table_files().empty()) hdxf_test::skip("no corpus (build/test-data, doc/facit)");
    ProxyOutcome out = compare_proxies(false);
    std::cout << "table proxy graphics: " << out.tables << " tables compared command by command (" << out.skipped_for_fonts << " not measured: a font is missing, "
              << out.true_color_of_2000 << " fills' true colors that DXF 2000 does not have, " << out.turned_anonymous_blocks << " anonymous blocks of turned tables, " << out.without_zero_markers
              << " tables saved without the marker 0)\n";
    std::string message;
    for (size_t i = 0; i < out.failures.size() && i < 8; ++i) message += "\n  " + out.failures[i];
    assert_true(out.failures.empty(), std::to_string(out.failures.size()) + " tables differ:" + message);
    if (!table_files(true).empty()) assert_true(out.tables >= 40, "the tables of doc/facit are compared: " + std::to_string(out.tables));
    assert_true(out.tables >= 1, "a table was compared");
}

TEST(table_graphics_a_moved_table_has_the_transform_of_autocads_moved_table_and_keeps_its_block) {
    for (const char* v : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (const char* suffix : {"", "_bin"}) {
            const std::filesystem::path base = hdxf_test::source_path(std::string("doc/facit/acad_") + v + "_dxf_table_layout" + suffix + ".dxf");
            const std::filesystem::path moved = hdxf_test::source_path(std::string("doc/facit/acad_") + v + "_dxf_table_layout_moved" + suffix + ".dxf");
            if (!std::filesystem::exists(base) || !std::filesystem::exists(moved)) hdxf_test::skip("no doc/facit (fas9_table_layout.scr)");
            Drawing d;
            assert_true(d.load(base), "load");
            Drawing m;
            assert_true(m.load(moved), "load moved");
            Entity* table = table_in(d);
            Entity* was = table_in(m);
            assert_true(table != nullptr && was != nullptr, "the tables");
            // AutoCAD's MOVE changed the table's place and nothing else
            assert_true(table->as_table().insertion.x != was->as_table().insertion.x, "the table was moved");
            const std::vector<hdxf::Entity> block_before = d.find_block(table->as_table().block_name)->entities;
            table->as_table().insertion = was->as_table().insertion;
            std::vector<uint8_t> out = d.save_to_memory(std::string(suffix).empty() ? DxfFormat::Ascii : DxfFormat::Binary);
            assert_true(!out.empty(), std::string(v) + ": " + d.last_error());
            Drawing r;
            assert_true(r.load_from_memory(out.data(), out.size()), "reload");
            Entity* moved_here = table_in(r);
            // the proxy graphics are AutoCAD's moved stream (up to the unwritten bytes of its text commands)
            std::string diff = proxy_difference(was->as_table().proxy_graphics, moved_here->as_table().proxy_graphics, false);
            assert_true(diff.empty(), std::string(v) + suffix + ": moved proxy graphics: " + diff);
            // the *T block was not laid out again: the same entities, the same handles
            const hdxf::Block* b = r.find_block(moved_here->as_table().block_name);
            assert_eq(static_cast<int>(block_before.size()), static_cast<int>(b->entities.size()), "the same block");
            for (size_t i = 0; i < block_before.size(); ++i) {
                assert_true(entity_difference(block_before[i], b->entities[i]).empty(), "the block's entities are as they were");
                assert_eq(block_before[i].props.handle, b->entities[i].props.handle, "their handles");
            }
        }
    }
}

TEST(table_graphics_a_new_table_with_no_text_is_autocads_empty_table_drawn) {
    // AutoCAD 2025's -TABLE (fas9_tables.scr) makes an empty table with its *T block and proxy graphics: HDxf's add_table with the same sizes and
    // style draws the same, command by command
    ScopedFonts fonts;
    int checked = 0;
    for (const char* v : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        const std::filesystem::path path = hdxf_test::source_path(std::string("doc/facit/acad_") + v + "_dxf_table_empty.dxf");
        if (!std::filesystem::exists(path)) continue;
        Drawing a;
        assert_true(a.load(path), "load");
        Entity* at = table_in(a);
        assert_true(at != nullptr, "AutoCAD's table");
        const hdxf::TableData& autocad = at->as_table();
        const DxfVersion version = std::string(v) == "2000" ? DxfVersion::R2000 : std::string(v) == "2004" ? DxfVersion::R2004 : std::string(v) == "2007" ? DxfVersion::R2007
                                   : std::string(v) == "2010" ? DxfVersion::R2010 : std::string(v) == "2013" ? DxfVersion::R2013 : DxfVersion::R2018;
        Drawing d(version);
        Entity& t = d.add_table(autocad.insertion, autocad.rows(), autocad.columns(), 1.0, 1.0);
        t.as_table().row_heights = autocad.row_heights;
        t.as_table().column_widths = autocad.column_widths;
        if (const hdxf::Object* style = a.table_style(*at)) {
            if (hdxf::Object* ours = d.find_object(t.as_table().style_handle)) {
                ours->as_table_style() = style->as_table_style();
                const hdxf::Object* map = a.table_cell_style_map(*style);
                const hdxf::Object* our_map = d.table_cell_style_map(*ours);
                if (map && our_map) d.find_object(our_map->handle)->as_cell_style_map() = map->as_cell_style_map();
            }
        }
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), std::string(v) + ": " + d.last_error());
        Drawing r;
        assert_true(r.load_from_memory(out.data(), out.size()), "reload");
        Entity* rt = table_in(r);
        assert_true(rt != nullptr, "our table");
        // the *T block: the same lines (AutoCAD's block for the second, smaller table is the other)
        const hdxf::Block* ob = r.find_block(rt->as_table().block_name);
        const hdxf::Block* ab = a.find_block(autocad.block_name);
        assert_true(ob != nullptr && ab != nullptr, "the blocks");
        std::string block_diff = entities_difference(ab->entities, ob->entities);
        assert_true(block_diff.empty(), std::string(v) + ": *T: " + block_diff);
        std::string proxy_diff = proxy_difference(autocad.proxy_graphics, rt->as_table().proxy_graphics, false);
        assert_true(proxy_diff.empty(), std::string(v) + ": proxy graphics: " + proxy_diff);
        ++checked;
    }
    if (checked == 0) hdxf_test::skip("no doc/facit (fas9_tables.scr)");
}

// ---------------------------------------------------------------------------
// Extents
// ---------------------------------------------------------------------------

TEST(table_graphics_extents_of_a_drawing_of_tables_are_autocads_extmin_and_extmax) {
    ScopedFonts fonts;
    int checked = 0;
    for (const char* v : {"2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (const char* suffix : {"", "_bin"}) {
            const std::filesystem::path path = hdxf_test::source_path(std::string("doc/facit/acad_") + v + "_dxf_table_empty" + suffix + ".dxf");
            if (!std::filesystem::exists(path)) continue;
            Drawing d;
            assert_true(d.load(path), "load");
            hdxf::internal::Extents model, paper;
            std::vector<std::string> warnings;
            hdxf::internal::drawing_extents(d, {}, {}, model, paper, warnings);
            const hdxf::HeaderVar* lo = d.find_header("$EXTMIN");
            const hdxf::HeaderVar* hi = d.find_header("$EXTMAX");
            assert_true(lo && hi && lo->values.size() >= 2 && hi->values.size() >= 2, "AutoCAD's extents");
            const double xmin = std::atof(lo->values[0].second.c_str()), ymin = std::atof(lo->values[1].second.c_str());
            const double xmax = std::atof(hi->values[0].second.c_str()), ymax = std::atof(hi->values[1].second.c_str());
            Tolerance tol;
            tol.absolute = 1e-9;
            assert_true(tol.near(model.min.x, xmin) && tol.near(model.min.y, ymin) && tol.near(model.max.x, xmax) && tol.near(model.max.y, ymax),
                        std::string(v) + suffix + ": extents (" + std::to_string(model.min.x) + "," + std::to_string(model.min.y) + ")-(" + std::to_string(model.max.x) + "," +
                        std::to_string(model.max.y) + ") against AutoCAD's (" + std::to_string(xmin) + "," + std::to_string(ymin) + ")-(" + std::to_string(xmax) + "," +
                        std::to_string(ymax) + ")");
            ++checked;
        }
    }
    if (checked == 0) hdxf_test::skip("no doc/facit (fas9_tables.scr)");
}

TEST(table_graphics_extents_follow_the_block_of_a_new_table_a_move_a_turn_and_a_table_in_a_block) {
    FontPaths restore;
    Drawing d(DxfVersion::R2018);
    if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
    Entity& t = d.add_table({100.0, 200.0, 0.0}, 3, 2, 10.0, 30.0);
    sensible_style(d, t);
    d.set_table_cell_text(t, 1, 0, "Name");
    hdxf::internal::Extents model, paper;
    std::vector<std::string> warnings;
    std::vector<uint8_t> saved = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!saved.empty(), d.last_error());
    hdxf::internal::drawing_extents(d, {}, {}, model, paper, warnings);
    // 2 columns of 30 and 3 rows of at least 10 down from the insertion point
    assert_true(std::fabs(model.min.x - 100.0) < 1e-9 && std::fabs(model.max.x - 160.0) < 1e-9, "the width of the columns: " + std::to_string(model.min.x) + " .. " + std::to_string(model.max.x));
    assert_true(std::fabs(model.max.y - 200.0) < 1e-9 && model.min.y < 171.0 + 1e-9 && model.min.y > 150.0, "the height of the rows: " + std::to_string(model.min.y));
    // moved by 50 to the right: the proxy graphics and the extents follow
    t.as_table().insertion.x = 150.0;
    saved = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!saved.empty(), d.last_error());
    hdxf::internal::Extents moved, p2;
    hdxf::internal::drawing_extents(d, {}, {}, moved, p2, warnings);
    assert_true(std::fabs(moved.min.x - 150.0) < 1e-9 && std::fabs(moved.max.x - 210.0) < 1e-9, "the extents moved with the table");
    // turned a quarter: its width is its height now
    t.as_table().horizontal_direction = {0.0, 1.0, 0.0};
    saved = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!saved.empty(), d.last_error());
    hdxf::internal::Extents turned, p3;
    hdxf::internal::drawing_extents(d, {}, {}, turned, p3, warnings);
    assert_true(std::fabs((turned.max.x - turned.min.x) - (model.max.y - model.min.y)) < 1e-6, "turned: the width is the height: " + std::to_string(turned.max.x - turned.min.x));
    assert_true(std::fabs((turned.max.y - turned.min.y) - 60.0) < 1e-6, "turned: the height is the width of the columns");
    // the *T block of the turned table has the turn in its geometry (AutoCAD's ROTATE): the first text is turned, the transform only moves
    Drawing r;
    assert_true(r.load_from_memory(saved.data(), saved.size()), "reload");
    const hdxf::TableData& td = table_in(r)->as_table();
    const hdxf::Block* b = r.find_block(td.block_name);
    bool turned_text = false;
    for (const Entity& e : b->entities) if (e.type == EntityType::MText) turned_text = std::fabs(e.as_mtext().x_axis_direction.x) < 1e-9 && std::fabs(e.as_mtext().x_axis_direction.y - 1.0) < 1e-9;
    assert_true(turned_text, "the text runs along the table's horizontal direction");
    auto commands = proxy_commands(td.proxy_graphics);
    assert_true(commands.front().type == 29, "the transform");
    double m00 = 0.0;
    std::memcpy(&m00, commands.front().data.data(), 8);
    assert_true(m00 == 1.0, "the transform has no turn of its own");
}

TEST(table_graphics_a_table_in_a_block_is_counted_where_the_block_is_inserted) {
    FontPaths restore;
    Drawing d(DxfVersion::R2018);
    if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
    // a table made in model space and moved into a block of its own, the block inserted at (1000, 0)
    Entity table = d.add_table({0.0, 0.0, 0.0}, 2, 2, 10.0, 20.0);
    d.entities().pop_back();
    d.add_block("WITH_TABLE");
    d.find_block("WITH_TABLE")->entities.push_back(table);
    d.add_insert("WITH_TABLE", {1000.0, 0.0, 0.0});
    std::vector<uint8_t> saved = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!saved.empty(), d.last_error());
    hdxf::internal::Extents model, paper;
    std::vector<std::string> warnings;
    hdxf::internal::drawing_extents(d, {}, {}, model, paper, warnings);
    assert_true(std::fabs(model.min.x - 1000.0) < 1e-6 && std::fabs(model.max.x - 1040.0) < 1e-6, "the table counted where its block is inserted: " + std::to_string(model.min.x) +
                " .. " + std::to_string(model.max.x));
}

TEST(table_graphics_a_removed_content_block_leaves_the_cell_empty_and_the_table_whole) {
    ScopedFonts fonts;
    std::filesystem::path path = hdxf_test::source_path("doc/facit/acad_2018_dxf_tables.dxf");
    if (!std::filesystem::exists(path)) hdxf_test::skip("no doc/facit");
    Drawing d;
    assert_true(d.load(path), "load");
    Entity* e = table_in(d);
    assert_true(e != nullptr, "the table");
    // the block of the block cell removed from the drawing: AutoCAD's rule is a null pointer in the cell and nothing drawn for it
    auto& blocks = d.blocks();
    const size_t before = blocks.size();
    blocks.erase(std::remove_if(blocks.begin(), blocks.end(), [](const hdxf::Block& b) { return b.name == "HDXF_TABLE_SYMBOL"; }), blocks.end());
    assert_eq(static_cast<int>(before) - 1, static_cast<int>(blocks.size()), "the block is gone");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), d.last_error());
    Drawing r;
    assert_true(r.load_from_memory(out.data(), out.size()), "reload");
    const hdxf::TableData& t = table_in(r)->as_table();
    assert_true(t.cells[6 * 4].block_handle.empty() || t.cells[6 * 4].block_handle == "0", "the cell points at nothing");
    const hdxf::Block* b = r.find_block(t.block_name);
    for (const Entity& be : b->entities) assert_false(be.type == EntityType::Insert && be.as_insert().block_name == "HDXF_TABLE_SYMBOL", "nothing drawn for it");
    assert_true(!t.proxy_graphics.empty(), "proxy graphics");
}

TEST(table_graphics_a_table_in_paper_space_and_one_in_a_mirrored_turned_insert_are_counted_where_they_are_drawn) {
    FontPaths restore;
    Drawing d(DxfVersion::R2018);
    if (!arial_drawing(d)) hdxf_test::skip("arial.ttf missing");
    // a table in paper space: counted in $PEXTMIN / $PEXTMAX, not in the model's
    Entity& paper_table = d.add_table({10.0, 20.0, 0.0}, 2, 2, 10.0, 20.0);
    paper_table.props.paper_space = 1;
    hdxf::internal::Extents model, paper;
    std::vector<std::string> warnings;
    std::vector<uint8_t> saved = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!saved.empty(), d.last_error());
    hdxf::internal::drawing_extents(d, {}, {}, model, paper, warnings);
    assert_true(model.empty(), "nothing in the model");
    assert_true(!paper.empty() && std::fabs(paper.min.x - 10.0) < 1e-9 && std::fabs(paper.max.x - 50.0) < 1e-9, "the table is counted in paper space");

    // a table in a block, the block inserted mirrored (x scale -1) and turned a quarter: its extents are the block's mapped, where it is drawn
    Drawing e(DxfVersion::R2018);
    arial_drawing(e);
    Entity inner = e.add_table({0.0, 0.0, 0.0}, 2, 2, 10.0, 20.0);
    e.entities().pop_back();
    e.add_block("T_IN_BLOCK");
    e.find_block("T_IN_BLOCK")->entities.push_back(inner);
    Entity& ins = e.add_insert("T_IN_BLOCK", {100.0, 100.0, 0.0});
    ins.as_insert().x_scale = -1.0;
    ins.as_insert().rotation = 90.0;
    std::vector<uint8_t> out = e.save_to_memory(DxfFormat::Ascii);
    assert_true(!out.empty(), e.last_error());
    hdxf::internal::Extents m2, p2;
    hdxf::internal::drawing_extents(e, {}, {}, m2, p2, warnings);
    // the table is 40 wide and 20 or more high from its insertion point down; mirrored in x and turned 90 degrees its width on the sheet is
    // its height and its height its width, from the insertion point
    const double width = m2.max.x - m2.min.x, height = m2.max.y - m2.min.y;
    assert_true(std::fabs(height - 40.0) < 1e-6 && width >= 20.0 - 1e-9, "turned and mirrored: " + std::to_string(width) + " x " + std::to_string(height));
    assert_true(std::fabs(m2.max.x - 100.0) < 1e-6 || std::fabs(m2.min.x - 100.0) < 1e-6, "from the insertion point");
}
