#include "TypedRecordTypes.hpp"

// The version and generation matrix of the typed records (phase 9, step 9.0; B101): nine versions and two
// formats per type, with what the writer can make anew, why it cannot, and what AutoCAD has verified as
// three separate things.

namespace hdxf {
namespace internal {

namespace {

const char* version_label(DxfVersion v) {
    switch (v) {
        case DxfVersion::R12:   return "R12";
        case DxfVersion::R13:   return "R13";
        case DxfVersion::R14:   return "R14";
        case DxfVersion::R2000: return "2000";
        case DxfVersion::R2004: return "2004";
        case DxfVersion::R2007: return "2007";
        case DxfVersion::R2010: return "2010";
        case DxfVersion::R2013: return "2013";
        case DxfVersion::R2018: return "2018";
    }
    return "";
}

VersionCell& cell(VersionMatrix& m, DxfVersion v, size_t format) {
    return m.cells[static_cast<size_t>(v)][format];
}

} // namespace

VersionMatrix VersionMatrix::unestablished() {
    VersionMatrix m;
    for (size_t f = 0; f < 2; ++f) {
        cell(m, DxfVersion::R13, f).autocad = AutoCadStatus::NotSavable;
        cell(m, DxfVersion::R14, f).autocad = AutoCadStatus::NotSavable;
    }
    return m;
}

VersionMatrix VersionMatrix::native_from(DxfVersion first) {
    VersionMatrix m = unestablished();
    for (size_t v = 0; v < kVersionCount; ++v) {
        for (size_t f = 0; f < 2; ++f) {
            VersionCell& c = m.cells[v][f];
            if (v >= static_cast<size_t>(first)) {
                c.generation = Generation::Native;
                c.basis = ErrorBasis::NotApplicable;
            } else {
                c.generation = Generation::None;
                c.basis = ErrorBasis::Documented;
            }
        }
    }
    return m;
}

VersionMatrix& VersionMatrix::set(DxfVersion v, Generation g, ErrorBasis b) {
    for (size_t f = 0; f < 2; ++f) {
        cell(*this, v, f).generation = g;
        cell(*this, v, f).basis = b;
    }
    return *this;
}

VersionMatrix& VersionMatrix::set(DxfVersion v, DxfFormat format, Generation g, ErrorBasis b) {
    VersionCell& c = cell(*this, v, format == DxfFormat::Binary ? 1 : 0);
    c.generation = g;
    c.basis = b;
    return *this;
}

VersionMatrix& VersionMatrix::autocad(DxfVersion v, AutoCadStatus s) {
    for (size_t f = 0; f < 2; ++f) cell(*this, v, f).autocad = s;
    return *this;
}

VersionMatrix& VersionMatrix::autocad(DxfVersion v, DxfFormat format, AutoCadStatus s) {
    cell(*this, v, format == DxfFormat::Binary ? 1 : 0).autocad = s;
    return *this;
}

VersionMatrix& VersionMatrix::error(DxfVersion v, const char* text) {
    error_text[static_cast<size_t>(v)] = text;
    return *this;
}

std::optional<DxfVersion> VersionMatrix::first_version() const {
    for (size_t v = 0; v < kVersionCount; ++v) {
        if (cells[v][0].generation != Generation::None && cells[v][1].generation != Generation::None) {
            return static_cast<DxfVersion>(v);
        }
    }
    return std::nullopt;
}

std::string version_error_text(const std::string& dxf_name, const VersionMatrix& matrix, DxfVersion target) {
    if (const char* text = matrix.error_text[static_cast<size_t>(target)]) return text;
    if (std::optional<DxfVersion> first = matrix.first_version()) {
        return dxf_name + " requires DXF " + version_label(*first) + " or later";
    }
    return dxf_name + " is not available in DXF " + version_label(target);
}

std::string typed_version_error(const Drawing& dwg, DxfVersion version, DxfFormat format) {
    const TypedRegistry& registry = TypedRegistry::instance();
    auto check = [&](std::string_view name, const VersionMatrix& matrix) -> std::string {
        const VersionCell& c = matrix.at(version, format);
        if (c.generation != Generation::None || c.basis != ErrorBasis::Documented) return {};
        return version_error_text(std::string(name), matrix, version);
    };
    // A record the drawing has from a file has its preserved record, written as it was: not the
    // program's fault. Only a record built since (no preserved record) is checked
    auto entities = [&](const std::vector<Entity>& list) -> std::string {
        for (const Entity& e : list) {
            if (!e.raw.empty()) continue;
            const EntityCodec* codec = registry.find_entity(e.type);
            if (!codec) continue;
            std::string error = check(codec->dxf_name, codec->versions);
            if (!error.empty()) return error;
            // A value only a newer version holds (step 9.S: NURBSURFACE's AcDbNurbSurface before 2013)
            if (codec->value_error) {
                error = codec->value_error(e, version);
                if (!error.empty()) return error;
            }
        }
        return {};
    };
    std::string error = entities(dwg.entities());
    if (!error.empty()) return error;
    for (const Block& b : dwg.blocks()) {
        error = entities(b.entities);
        if (!error.empty()) return error;
    }
    for (const Object& o : dwg.objects()) {
        if (!o.raw.empty()) continue;
        const ObjectCodec* codec = registry.find_object(o.type);
        if (!codec) continue;
        error = check(codec->dxf_name, codec->versions);
        if (!error.empty()) return error;
    }
    return {};
}

} // namespace internal
} // namespace hdxf
