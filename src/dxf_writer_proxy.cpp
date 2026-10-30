#include "TypedRecordTypes.hpp"
#include "dxf_writer.hpp"
#include <cctype>

// The writer's side of the proxy classes (phase 9, step 9.3): where a class stands in the CLASSES section that
// is being written. A proxy's class id (91) is 500 plus the position of its CLASS record; the writer notes
// each record it writes (DxfWriter::class_register_), whether the section is made from the drawing or kept as
// the file had it.

namespace hdxf {
namespace internal {

int RecordWriter::class_id(const std::string& dxf_name) const {
    const std::vector<std::string>& names = w_.class_register_;
    for (size_t i = 0; i < names.size(); ++i) {
        const std::string& have = names[i];
        if (have.size() != dxf_name.size()) continue;
        bool same = true;
        for (size_t k = 0; k < have.size() && same; ++k) {
            same = std::toupper(static_cast<unsigned char>(have[k])) == std::toupper(static_cast<unsigned char>(dxf_name[k]));
        }
        if (same) return 500 + static_cast<int>(i);
    }
    return 0;
}

} // namespace internal
} // namespace hdxf
