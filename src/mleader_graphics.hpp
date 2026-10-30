#pragma once

#include "hdxf.hpp"
#include "extents.hpp"
#include "text_layout.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// A MULTILEADER's proxy graphics as AutoCAD writes them (plan phase 3, B38): its content —
// the MTEXT's lines as text, or the block's entities — its leaders with their arrowheads,
// landings and the text's frame, with the colors, the layer, the linetypes and the
// lineweights AutoCAD sets for each part.

namespace hdxf {
namespace internal {

// The layers and linetypes as the file has them, in its order: proxy graphics name them by
// their index (a linetype among the ones other than ByBlock and ByLayer)
struct ProxyTables {
    std::vector<std::string> layers;
    std::vector<std::string> linetypes;
};

// The bytes (92/160 and 310); unicode: the text as UTF-16 (2007+), else in the drawing's
// code page
std::vector<uint8_t> mleader_graphics(const Drawing& dwg, const Entity& e, const ProxyTables& tables,
                                      TextLayout& layout, bool unicode);

// A MULTILEADER's extents: what its graphics draw — the leaders, arrowheads and landings,
// the text (its cells), the attributes' text and the frame — mapped by `map` (4 × 4, row by
// row); the entities of the blocks it draws (its content, its arrowheads') are given to
// `block_entity` with the block's transform (map included), to be counted as an entity of a
// block is (extents.cpp)
void mleader_extents(const Drawing& dwg, const Entity& e, TextLayout& layout, const std::array<double, 16>& map,
                     const std::function<void(const Entity&, const std::array<double, 16>&)>& block_entity,
                     Extents& out);

} // namespace internal
} // namespace hdxf
