#include "acds.hpp"
#include "dxf_reader.hpp"
#include "handles.hpp"
#include "roundtrip.hpp"
#include <cstdlib>
#include <unordered_map>

// ACDSDATA (DXF 2013+, plan phase 5, B61): read into the model. The section is in no reference
// (the 2014 reference's list of sections, PDF page 251); AutoCAD's 2013 files decide. A body's
// ASM_Data record gives its AcisData::sab, a layout's or view's Thumbnail_Data record its
// thumbnail — found by the record's 320; every record stays as read beside them, so an
// unchanged drawing is written back byte for byte (dxf_writer_acds.cpp).

namespace hdxf {
namespace internal {

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 310 as the reader keeps it: hex in ASCII and binary alike
void append_hex(const std::string& hex, std::vector<uint8_t>& out) {
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int a = hex_digit(hex[i]);
        int b = hex_digit(hex[i + 1]);
        if (a < 0 || b < 0) return;
        out.push_back(static_cast<uint8_t>(a * 16 + b));
    }
}

uint64_t handle_of(const std::string& text) {
    uint64_t v = 0;
    return parse_handle(text, v) == HandleParse::Ok ? v : 0;
}

} // namespace

bool acds_record_info(const RawRecord& record, AcdsRecordInfo& out) {
    std::vector<DxfGroup> groups = record.groups();
    if (groups.empty() || groups[0].code != 0 || groups[0].value != "ACDSRECORD") return false;
    out = AcdsRecordInfo();
    bool after_handle = false;
    for (size_t i = 1; i < groups.size(); ++i) {
        const DxfGroup& g = groups[i];
        if (g.code == 90 && out.schema < 0 && !after_handle) out.schema = std::atoi(g.value.c_str());
        else if (g.code == 320 && !after_handle) {
            out.handle = handle_of(g.value);
            after_handle = true;
        } else if (g.code == 2 && after_handle && out.data_name.empty()) out.data_name = g.value;
        else if (g.code == 310 && after_handle) append_hex(g.value, out.data);
    }
    return true;
}

bool acds_schema_info(const RawRecord& schema, int& index, std::string& name) {
    std::vector<DxfGroup> groups = schema.groups();
    if (groups.empty() || groups[0].code != 0 || groups[0].value != "ACDSSCHEMA") return false;
    index = -1;
    name.clear();
    for (size_t i = 1; i < groups.size(); ++i) {
        if (groups[i].code == 90 && index < 0) index = std::atoi(groups[i].value.c_str());
        if (groups[i].code == 1 && name.empty()) name = groups[i].value;
    }
    return true;
}

AcdsKind acds_kind(const std::string& schema_name, const std::string& data_name) {
    if (schema_name == acds_asm_schema && data_name == "ASM_Data") return AcdsKind::Body;
    if (schema_name == acds_thumbnail_schema && data_name == "Thumbnail_Data") return AcdsKind::Thumbnail;
    return AcdsKind::Other;
}

std::vector<AcdsHolder> acds_holders(const Drawing& dwg) {
    std::vector<AcdsHolder> out;
    auto bodies = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (const AcisData* acis = acis_of(e)) out.push_back({AcdsKind::Body, handle_of(e.props.handle), &acis->sab, acis});
        }
    };
    for (const auto& b : dwg.blocks()) bodies(b.entities);
    bodies(dwg.entities());
    for (const auto& o : dwg.objects()) {
        if (o.type == ObjectType::Layout) {
            out.push_back({AcdsKind::Thumbnail, handle_of(o.handle), &o.as_layout().thumbnail});
        }
    }
    for (const auto& v : dwg.views()) out.push_back({AcdsKind::Thumbnail, handle_of(v.handle), &v.thumbnail});
    return out;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

// The section record by record, as capture_section() keeps one: the head (0 SECTION … 71), then
// the schemas and the records. The 0/SECTION record is still open when this is called.
void DxfReader::parse_acdsdata(Drawing& dwg) {
    bool head_taken = false;
    GroupCode gc;
    while (read_group(gc)) {
        if (gc.code != 0) continue;
        RawRecord record = take_record();
        if (!head_taken) {
            dwg.acds_head_ = std::move(record);
            head_taken = true;
        } else {
            std::vector<DxfGroup> groups = record.groups();
            bool schema = !groups.empty() && groups[0].value == "ACDSSCHEMA";
            (schema ? dwg.acds_schemas_ : dwg.acds_records_).push_back(std::move(record));
        }
        if (gc.value == "ENDSEC") break;
    }
}

// Each typed record's data to the body, layout or view whose handle it has; the first record
// of a holder counts (another is kept as it is)
void DxfReader::type_acdsdata(Drawing& dwg) {
    std::unordered_map<int, std::string> schema_names;
    for (const auto& s : dwg.acds_schemas_) {
        int index = -1;
        std::string name;
        if (acds_schema_info(s, index, name)) schema_names[index] = name;
    }
    std::unordered_map<uint64_t, std::vector<uint8_t>*> bodies;
    std::unordered_map<uint64_t, std::vector<uint8_t>*> thumbnails;
    auto add_bodies = [&](std::vector<Entity>& list) {
        for (auto& e : list) {
            if (AcisData* acis = acis_of(e)) {
                if (uint64_t h = handle_of(e.props.handle)) bodies.emplace(h, &acis->sab);
            }
        }
    };
    for (auto& b : dwg.blocks_) add_bodies(b.entities);
    add_bodies(dwg.entities_);
    for (auto& o : dwg.objects_) {
        if (o.type != ObjectType::Layout) continue;
        if (uint64_t h = handle_of(o.handle)) thumbnails.emplace(h, &o.as_layout().thumbnail);
    }
    for (auto& v : dwg.views_) {
        if (uint64_t h = handle_of(v.handle)) thumbnails.emplace(h, &v.thumbnail);
    }

    for (const auto& r : dwg.acds_records_) {
        AcdsRecordInfo info;
        if (!acds_record_info(r, info) || info.handle == 0) continue;
        auto name = schema_names.find(info.schema);
        if (name == schema_names.end()) continue;
        AcdsKind kind = acds_kind(name->second, info.data_name);
        auto& holders = kind == AcdsKind::Body ? bodies : thumbnails;
        if (kind == AcdsKind::Other) continue;
        auto it = holders.find(info.handle);
        if (it == holders.end()) continue;
        *it->second = std::move(info.data);
        holders.erase(it);
    }
}

} // namespace internal
} // namespace hdxf
