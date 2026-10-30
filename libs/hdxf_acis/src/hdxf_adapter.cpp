#include "hdxf_acis.hpp"

#include "sat_writer.hpp"

namespace hdxf::acis {

// The SAT lines enciphered as AutoCAD saves them: the DXF groups (1 and 3, the caret escape of ASCII DXF
// for the character "^" that a capital "A" becomes, the SAB of 2013 and 2018) are HDxf's.
Result<hdxf::AcisData> to_acis_data(const SatDocument& document) {
    if (document.version != 21500) {
        return make_diagnostic(ErrorCode::InvalidArgument, {}, {},
                               "to_acis_data: only SAT 21500 is written; the document is version " +
                                   std::to_string(document.version));
    }
    if (document.lines.size() < 4) {
        return make_diagnostic(ErrorCode::InvalidArgument, {}, {}, "to_acis_data: the document has no records");
    }
    Result<std::vector<std::string>> lines = detail::cipher_lines(document.lines);
    if (!lines) return lines.errors();

    hdxf::AcisData data;
    data.version = 1;                   // DXF group 70: not the SAT version
    data.lines = std::move(lines.value());
    data.history_handle.clear();
    data.guid.clear();
    data.sab.clear();
    return data;
}

} // namespace hdxf::acis
