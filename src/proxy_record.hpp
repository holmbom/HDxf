#pragma once

#include "TypedRecordTypes.hpp"
#include <string>
#include <unordered_set>
#include <vector>

// ACAD_PROXY_ENTITY and ACAD_PROXY_OBJECT (phase 9, step 9.3): what the rest of the library calls of the proxy
// codecs (proxy_record.cpp). The codecs themselves are registered by register_proxy_codecs() in
// typed_families.cpp.

namespace hdxf {
namespace internal {

// The CLASS records of the applications the drawing's proxies stand for, added to `out` after the classes
// already there (a class that is there by its DXF name is not added again): one record for each class the
// proxies name that way, in order of first use, with the number of proxies of it (B113). Called by
// append_typed_classes(); `dropped` as there
void append_proxy_classes(const Drawing& dwg, DxfVersion version, const std::unordered_set<uint64_t>* dropped,
                          std::vector<ClassEntry>& out);

// After a file is read: a proxy's class (91 = 500 + the position of its CLASS record) is looked up in the
// file's CLASSES section and kept in the proxy's `application_class`. A class id that is not in the register
// is a warning (the id is kept as read). The warnings are appended to `warnings`
void resolve_proxy_classes(Drawing& dwg, std::vector<std::string>& warnings);

} // namespace internal
} // namespace hdxf
