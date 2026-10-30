#include "dxf_writer.hpp"
#include "acds.hpp"
#include "acis.hpp"
#include "acis_sab.hpp"
#include "handles.hpp"
#include <unordered_map>
#include <unordered_set>

// The ACDSDATA section (DXF 2013+, plan phase 5, B59/B61): in no reference; as AutoCAD writes it.
// A new drawing has the schemas of AutoCAD 2017's new drawing saved as 2013 (jscad's
// empty2013.dxf) — AcDb_Thumbnail_Schema and the four system schemas — with
// AcDb3DSolid_ASM_Data first when it has bodies with SAB (3Ddraw, 3Dlines); HDxf draws no
// preview image. A drawing read from a 2013 file keeps its schemas and records as read; a body,
// layout or view whose data changed gets its record anew, one removed takes its record along,
// a new one gets one (B19), and a schema the drawing needs but lacks goes last (Thumbnail,
// system, ASM — AutoCAD's own files that got bodies after their first save). A body with SAT
// data has the SAB made from it (src/acis_sab.cpp, B62).

namespace hdxf {
namespace internal {

namespace {

// The system schemas, in AutoCAD's order: name, the attribute, its type (280)
struct AcdsSystemSchema {
    const char* name;
    const char* attribute;
    int type;
};

const AcdsSystemSchema acds_system_schemas[] = {
    {"AcDbDs::TreatedAsObjectDataSchema", "AcDbDs::TreatedAsObjectData", 1},
    {"AcDbDs::LegacySchema", "AcDbDs::Legacy", 1},
    {"AcDbDs::IndexedPropertySchema", "AcDs:Indexable", 1},
    {"AcDbDs::HandleAttributeSchema", "AcDbDs::HandleAttribute", 7},
};

// A schema in the section written: one as read, or one written anew (raw null) — a data
// schema (ASM or Thumbnail) or a system schema (system ≥ 0)
struct AcdsSchemaOut {
    const RawRecord* raw = nullptr;
    std::string name;
    int system = -1;
};

// A record in the section written: as read (raw), or anew with its owner's data
struct AcdsRecordOut {
    int schema = -1;
    const RawRecord* raw = nullptr;
    uint64_t handle = 0;
    const char* data_name = nullptr;
    const std::vector<uint8_t>* data = nullptr;
};

} // namespace

void DxfWriter::write_acdsdata(const Drawing& dwg) {
    std::vector<AcdsHolder> holders = acds_holders(dwg);

    // Before 2013 there is no ACDSDATA: AutoCAD's 2000 … 2010 files have no preview images (B60)
    if (version_ < DxfVersion::R2013) {
        int previews = 0;
        for (const auto& h : holders) previews += h.kind == AcdsKind::Thumbnail && !h.data->empty();
        if (previews > 0) {
            warnings_.push_back(std::to_string(previews) +
                                " preview image(s) of layouts and views not written: they need DXF 2013 or later");
        }
        return;
    }

    // A body with SAT lines: its data is the SAB made from them (B62), as AutoCAD writes it.
    // write_dxf() has checked that each can be made. The SAB is 22300 ("ASM BinaryFile4") in a 2018
    // file, 21800 before it (plan phase 6, B80); a SAB already there is written as it is.
    const int format = sab_format_for(version_);
    std::vector<std::vector<uint8_t>> made;
    made.reserve(holders.size());
    for (auto& h : holders) {
        if (h.kind != AcdsKind::Body || !h.acis || h.acis->lines.empty()) continue;
        made.emplace_back();
        std::string error;
        if (!sab_from_sat(acis_deciphered(*h.acis), made.back(), error, format) && error_.empty()) error_ = error;
        h.data = &made.back();
    }

    bool from_file = template_mode() && !dwg.acds_head_.empty();

    // The schemas: as read, or the new drawing's
    std::vector<AcdsSchemaOut> schemas;
    if (from_file) {
        for (const auto& s : dwg.acds_schemas_) {
            AcdsSchemaOut out;
            out.raw = &s;
            int index = -1;
            acds_schema_info(s, index, out.name);
            for (int i = 0; i < 4; ++i) {
                if (out.name == acds_system_schemas[i].name) out.system = i;
            }
            schemas.push_back(out);
        }
    }
    auto schema_index = [&](const std::string& name) {
        for (size_t i = 0; i < schemas.size(); ++i) {
            if (schemas[i].name == name) return static_cast<int>(i);
        }
        return -1;
    };
    // The index of each read schema is its position: AutoCAD's 90 (0, 1, …) in all its files
    std::unordered_map<int, int> read_index;   // 90 as read → position
    for (size_t i = 0; i < schemas.size(); ++i) {
        int index = -1;
        std::string name;
        acds_schema_info(*schemas[i].raw, index, name);
        read_index.emplace(index, static_cast<int>(i));
    }

    // The records as read: each typed one by its holder now, the others while their handle lives
    std::unordered_map<uint64_t, const AcdsHolder*> bodies;
    std::unordered_map<uint64_t, const AcdsHolder*> thumbnails;
    for (const auto& h : holders) {
        if (h.handle == 0) continue;
        (h.kind == AcdsKind::Body ? bodies : thumbnails).emplace(h.handle, &h);
    }
    std::unordered_set<const AcdsHolder*> written;
    std::vector<AcdsRecordOut> records;
    if (from_file) {
        for (const auto& r : dwg.acds_records_) {
            AcdsRecordInfo info;
            AcdsRecordOut out;
            out.raw = &r;
            if (!acds_record_info(r, info)) {
                records.push_back(out);
                continue;
            }
            auto position = read_index.find(info.schema);
            out.schema = position == read_index.end() ? info.schema : position->second;
            AcdsKind kind = position == read_index.end() ? AcdsKind::Other
                                                         : acds_kind(schemas[position->second].name, info.data_name);
            const AcdsHolder* holder = nullptr;
            if (kind != AcdsKind::Other && info.handle != 0) {
                auto& map = kind == AcdsKind::Body ? bodies : thumbnails;
                auto it = map.find(info.handle);
                if (it != map.end() && written.count(it->second) == 0) holder = it->second;
            }
            if (holder) {
                written.insert(holder);
                if (*holder->data == info.data) {
                    records.push_back(out);
                } else if (!holder->data->empty()) {
                    changed_ = true;
                    out.raw = nullptr;
                    out.handle = holder->handle;
                    out.data_name = kind == AcdsKind::Body ? "ASM_Data" : "Thumbnail_Data";
                    out.data = holder->data;
                    records.push_back(out);
                } else {
                    changed_ = true;      // its data is gone
                }
            } else if (info.handle == 0 || is_live(format_handle(info.handle))) {
                records.push_back(out);
            } else {
                changed_ = true;          // the record it belonged to is gone
            }
        }
    }

    // New records: the holders with data that have none, in the order they are written
    bool new_bodies = false;
    bool new_thumbnails = false;
    for (const auto& h : holders) {
        if (h.data->empty() || h.handle == 0 || written.count(&h) > 0) continue;
        (h.kind == AcdsKind::Body ? new_bodies : new_thumbnails) = true;
    }
    // A schema the drawing needs and lacks: a new drawing's (B59), else last (B61)
    if (!from_file && !template_mode()) {
        if (new_bodies) schemas.push_back({nullptr, acds_asm_schema, -1});
        schemas.push_back({nullptr, acds_thumbnail_schema, -1});
        for (int i = 0; i < 4; ++i) schemas.push_back({nullptr, acds_system_schemas[i].name, i});
    } else {
        bool asm_missing = new_bodies && schema_index(acds_asm_schema) < 0;
        bool thumbnail_missing = new_thumbnails && schema_index(acds_thumbnail_schema) < 0;
        if (thumbnail_missing) schemas.push_back({nullptr, acds_thumbnail_schema, -1});
        if (asm_missing || thumbnail_missing) {
            for (int i = 0; i < 4; ++i) {
                if (schema_index(acds_system_schemas[i].name) < 0) schemas.push_back({nullptr, acds_system_schemas[i].name, i});
            }
        }
        if (asm_missing) schemas.push_back({nullptr, acds_asm_schema, -1});
        if (asm_missing || thumbnail_missing) changed_ = true;
    }
    // A drawing read from a 2013 file without the section that needs none: none (as read)
    if (template_mode() && !from_file && schemas.empty()) return;
    if (template_mode() && !from_file) changed_ = true;      // a section the file did not have
    // Each new record after the last of its schema (BLOCKS before ENTITIES, as written)
    for (const auto& h : holders) {
        if (h.data->empty() || h.handle == 0 || written.count(&h) > 0) continue;
        AcdsRecordOut out;
        bool body = h.kind == AcdsKind::Body;
        out.schema = schema_index(body ? acds_asm_schema : acds_thumbnail_schema);
        out.handle = h.handle;
        out.data_name = body ? "ASM_Data" : "Thumbnail_Data";
        out.data = h.data;
        size_t at = records.size();
        for (size_t i = records.size(); i-- > 0;) {
            if (records[i].schema >= 0 && records[i].schema <= out.schema) { at = i + 1; break; }
            if (records[i].schema > out.schema) at = i;
        }
        records.insert(records.begin() + static_cast<std::ptrdiff_t>(at), out);
        if (template_mode()) changed_ = true;
    }

    // The head: as read, or AutoCAD's (ODA writes 71 2)
    if (from_file) {
        write_raw_record(dwg.acds_head_);
    } else {
        write_code(0, "SECTION");
        write_code(2, "ACDSDATA");
        groups_.write_int(70, 2);
        groups_.write_int(71, 8);
    }

    // The schemas; a data schema's four attribute records point at the system schemas by index
    std::vector<int> system_index(4, -1);
    for (size_t i = 0; i < schemas.size(); ++i) {
        if (schemas[i].system >= 0) system_index[schemas[i].system] = static_cast<int>(i);
    }
    for (size_t i = 0; i < schemas.size(); ++i) {
        const AcdsSchemaOut& s = schemas[i];
        if (s.raw) {
            write_raw_record(*s.raw);
            continue;
        }
        write_code(0, "ACDSSCHEMA");
        groups_.write_int(90, static_cast<int64_t>(i));
        if (s.system >= 0) {
            const AcdsSystemSchema& system = acds_system_schemas[s.system];
            write_code(1, system.name);
            write_code(2, system.attribute);
            groups_.write_int(280, system.type);
            bool handle = s.system == 3;
            groups_.write_int(91, handle ? 1 : 0);
            if (handle) groups_.write_int(284, 1);
            continue;
        }
        bool body = s.name == acds_asm_schema;
        write_code(1, s.name);
        write_code(2, "AcDbDs::ID");
        groups_.write_int(280, 10);
        groups_.write_int(91, 8);
        write_code(2, body ? "ASM_Data" : "Thumbnail_Data");
        groups_.write_int(280, 15);
        groups_.write_int(91, 0);
        write_code(101, "ACDSRECORD");
        groups_.write_int(95, static_cast<int64_t>(i));
        groups_.write_int(90, system_index[0]);
        write_code(2, "AcDbDs::TreatedAsObjectData");
        groups_.write_int(280, 1);
        groups_.write_int(291, 1);
        write_code(101, "ACDSRECORD");
        groups_.write_int(95, static_cast<int64_t>(i));
        groups_.write_int(90, system_index[1]);
        write_code(2, "AcDbDs::Legacy");
        groups_.write_int(280, 1);
        groups_.write_int(291, 1);
        write_code(101, "ACDSRECORD");
        write_code(1, "AcDbDs::ID");
        groups_.write_int(90, system_index[2]);
        write_code(2, "AcDs:Indexable");
        groups_.write_int(280, 1);
        groups_.write_int(291, 1);
        write_code(101, "ACDSRECORD");
        write_code(1, "AcDbDs::ID");
        groups_.write_int(90, system_index[3]);
        write_code(2, "AcDbDs::HandleAttribute");
        groups_.write_int(280, 7);
        groups_.write_int(282, 1);
    }

    // The records, grouped by schema
    for (const auto& r : records) {
        if (r.raw) {
            write_raw_record(*r.raw);
            continue;
        }
        write_code(0, "ACDSRECORD");
        groups_.write_int(90, r.schema);
        write_code(2, "AcDbDs::ID");
        groups_.write_int(280, 10);
        write_code(320, format_handle(r.handle));
        write_code(2, r.data_name);
        groups_.write_int(280, 15);
        groups_.write_int(94, static_cast<int64_t>(r.data->size()));
        write_binary_chunks(*r.data);
    }
    write_code(0, "ENDSEC");
}

} // namespace internal
} // namespace hdxf
