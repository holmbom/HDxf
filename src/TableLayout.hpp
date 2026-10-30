#pragma once

#include "TableTypes.hpp"
#include "text_layout.hpp"
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// The layout of a table (phase 9, step 9.5; plan B106 … B108): the cells' sizes, the rows' heights, where each cell's
// content goes, and the entities of the table's anonymous block *T that draw it — text, fills, blocks, borders —
// in the order AutoCAD's own *T blocks have them. One stream drives the *T block (table_graphics.cpp), the proxy
// graphics of the ACAD_TABLE and its extents. The rules are read from AutoCAD's own tables
// (doc/facit/acad_*_dxf_tables.dxf, the corpus of build/test-data): a text cell's content is its MTEXT's
// width + 0.1 by its height, a cell is the content plus its margins, a row is as high as its highest cell, borders
// are drawn once, hidden inside a merged range and joined where equal, contents flow left to right and wrap.

namespace hdxf {
namespace internal {

// The extra width AutoCAD gives a text content (its geometry's width is the MTEXT's own + 0.1)
constexpr double kTableTextPad = 0.1;
// The space between the contents of a cell (CELLMARGIN's last two values: 0.18 in every AutoCAD file)
constexpr double kTableContentSpacing = 0.18;

// A block a table cell draws: its content block (*U…) rebuilt, or a block cell's INSERT
struct TableCellBlock {
    std::string name;                   // the block to replace the entities of (an anonymous *U block)
    std::vector<Entity> entities;
};

struct TableLayout {
    // The rows' heights and the columns' widths as the layout makes them (a row is as high as it was and as its
    // highest cell needs)
    std::vector<double> row_heights;
    std::vector<double> column_widths;
    // The edges: x[c] is the left edge of column c (x.back() the right edge of the table), y[r] the top edge of row
    // r counted down from the table's top (positive)
    std::vector<double> x;
    std::vector<double> y;
    // TABLEGEOMETRY: every cell, row by row
    TableGeometryData geometry;
    // The entities of *T in AutoCAD's order: fills, text, blocks, horizontal then vertical borders, the
    // Defpoints marker
    std::vector<Entity> block;
    // The anonymous blocks of the cells that hold more than one content (*U…), rebuilt
    std::vector<TableCellBlock> cell_blocks;
    // The MTEXTs that show a FIELD (their proxy marker is AutoCAD's 2000000000): by the block they are in ("" for *T)
    // and their place in its entities
    std::set<std::pair<std::string, size_t>> field_texts;
    // The MTEXTs as the layout set them (their lines and runs, which the proxy graphics draw: AutoCAD does not wrap an
    // anonymous block's MTEXT again at the width it was given), by the same key
    std::map<std::pair<std::string, size_t>, MTextSize> text_sizes;
    // What the layout could not do (a font not found is the text layout's own warning)
    std::vector<std::string> warnings;
};

// Lays the table out. `content` is the table's TABLECONTENT when it has one (the cells with several contents; the
// spacing between them). False (and a warning) when the table cannot be laid out: the cells are not rows x columns.
bool layout_table(const Drawing& dwg, const Entity& table, const TableContentData* content, TextLayout& text,
                  TableLayout& out);

// The text a FIELD shows (its cached value; a text field with children as its text with each child's value in
// place of %<\_FldIdx n>%). Empty when the field is not there.
std::string table_field_text(const Drawing& dwg, const std::string& field_handle);

// A hash of everything the layout reads of a table (cells, overrides, style, cell styles, the blocks and fields the cells
// show, the contents of cells with several, the text styles, the layer, color, lineweight and version the proxy graphics
// resolve) and one of where it is. A table is laid out again when the first has changed since it was read or last laid
// out, only its proxy graphics' transform follows when the second has (B108)
uint64_t table_layout_stamp(const Drawing& dwg, const Entity& table, const TableContentData* content);
uint64_t table_placement_stamp(const Entity& table);
// The stamps of the tables of a drawing just read
void stamp_loaded_tables(Drawing& dwg);

// The bytes of the table's proxy graphics (160 / 92 and 310): its transform, then each entity of the *T block as
// AutoCAD's AcGi records it
std::vector<uint8_t> table_proxy_graphics(const Drawing& dwg, const Entity& table, const std::vector<Entity>& block,
                                          const std::set<std::pair<std::string, size_t>>& field_texts,
                                          const std::map<std::pair<std::string, size_t>, MTextSize>& text_sizes,
                                          TextLayout& text, bool unicode);

// A moved or rotated table: the transform of its proxy graphics as it is now (the stream otherwise as it was). False
// when the stream has no transform first.
bool table_proxy_retarget(const Entity& table, std::vector<uint8_t>& stream);

// The table's extents: the extents of its *T block as an INSERT of it would have them. False when the table has no
// block to measure (the caller falls back to its grid)
bool table_block_extents(const Entity& table, TypedExtentsSink& sink);

// The layout of the table's and the placement the table has now: what the hook of table_sync.cpp calls
void apply_table_layout(Drawing& dwg, Entity& table, TableContentData* content, TableGeometryData* geometry,
                        std::vector<std::string>& warnings);

} // namespace internal
} // namespace hdxf
