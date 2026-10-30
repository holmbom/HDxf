#include "dxf_writer.hpp"
#include "dimstyle_legacy.hpp"

// DIMSTYLE as AutoCAD R13 and R14 write it (plan phase 7, step 7.8, B91): the name form 5/6/7 of
// the arrow blocks, DIMUNIT (270) and DIMFIT (287) where 2000 has 276/277 and 279/289, and the text
// style as a handle (340). The groups and their order are in dimstyle_legacy.cpp.

namespace hdxf {
namespace internal {

void DxfWriter::write_dimstyle_legacy(const DimStyle& ds, const std::string& handle) {
    std::string error;
    std::vector<LegacyGroup> groups = legacy_dimstyle_groups(ds, style_handle(ds.dimtxsty), error);
    if (!error.empty()) {
        if (error_.empty()) error_ = error;
        return;
    }
    // 105 (the handle), the reactors, 330, the two subclass markers, as AutoCAD's files have them
    write_table_entry_head("DIMSTYLE", handle, table_handles_["DIMSTYLE"], "AcDbDimStyleTableRecord", {},
                           &ds.reactors);
    for (const auto& g : groups) {
        switch (g.kind) {
            case LegacyGroup::Kind::String:
                // 5, 6 and 7 are the names of the arrow blocks: spelled as the blocks of the file
                if (g.code == 5 || g.code == 6 || g.code == 7) write_code(g.code, symbol_ref(SymbolKind::Block, g.text));
                else write_code(g.code, g.text);
                break;
            case LegacyGroup::Kind::Int: groups_.write_int(g.code, g.integer); break;
            case LegacyGroup::Kind::Double: groups_.write_double(g.code, g.real); break;
        }
    }
    write_xdata(ds.xdata);
}

} // namespace internal
} // namespace hdxf
