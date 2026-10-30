#pragma once

#include "test_framework.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

// The version and generation matrix of a typed record type (phase 9, step 9.0; plan 2026-10-27-fas-9-typer.md,
// B101, "Gemensamt testkontrakt T"). Every family test registers its type here with a function that adds one
// record to a drawing built for a version, and one that checks the record in the drawing read back:
//
//     auto report = hdxf_test::run_version_matrix("TABLESTYLE", codec.versions, add_one, verify);
//
// The helper goes through all nine versions and both formats and checks each cell against the type's
// VersionMatrix:
//   - a cell that can generate (Native, OlderForm): the drawing is written, read back, `verify` passes, the
//     record written again from what was read is byte for byte the same (B12), and written in the other
//     format it reads back with the same content
//   - a cell with a Documented version error: nothing is written, last_error() is the matrix's named error,
//     the drawing is unchanged, and no file is made (the error comes before any mutation)
//   - a cell with NotEstablished basis: no claim is checked; it is counted and reported
// The AutoCAD axis is only counted (verified / ej testat / not savable): a test does not set it.

namespace hdxf_test {

inline const hdxf::DxfVersion all_versions[9] = {
    hdxf::DxfVersion::R12,   hdxf::DxfVersion::R13,   hdxf::DxfVersion::R14,
    hdxf::DxfVersion::R2000, hdxf::DxfVersion::R2004, hdxf::DxfVersion::R2007,
    hdxf::DxfVersion::R2010, hdxf::DxfVersion::R2013, hdxf::DxfVersion::R2018};

inline const char* version_label(hdxf::DxfVersion v) {
    switch (v) {
        case hdxf::DxfVersion::R12:   return "R12";
        case hdxf::DxfVersion::R13:   return "R13";
        case hdxf::DxfVersion::R14:   return "R14";
        case hdxf::DxfVersion::R2000: return "2000";
        case hdxf::DxfVersion::R2004: return "2004";
        case hdxf::DxfVersion::R2007: return "2007";
        case hdxf::DxfVersion::R2010: return "2010";
        case hdxf::DxfVersion::R2013: return "2013";
        case hdxf::DxfVersion::R2018: return "2018";
    }
    return "";
}

inline const char* format_label(hdxf::DxfFormat f) { return f == hdxf::DxfFormat::Binary ? "binary" : "ASCII"; }

struct MatrixReport {
    int generated = 0;              // cells where a record was made, read back and written again
    int named_errors = 0;           // cells whose named version error was checked
    int not_established = 0;        // cells that claim nothing (no file, no grammar)
    int autocad_verified = 0;
    int autocad_not_tested = 0;     // ej testat
    int autocad_not_savable = 0;    // R13/R14: AutoCAD 2025 cannot save them
    std::vector<std::string> not_tested;    // "NAME 2010 binary: ej testat" for every cell AutoCAD has not verified
};

inline MatrixReport run_version_matrix(
    const std::string& name, const hdxf::internal::VersionMatrix& matrix,
    const std::function<void(hdxf::Drawing&)>& add,
    const std::function<std::string(const hdxf::Drawing&, hdxf::DxfVersion, hdxf::DxfFormat)>& verify) {
    using hdxf::DxfFormat;
    using hdxf::internal::AutoCadStatus;
    using hdxf::internal::ErrorBasis;
    using hdxf::internal::Generation;
    MatrixReport report;
    const DxfFormat formats[2] = {DxfFormat::Ascii, DxfFormat::Binary};
    for (hdxf::DxfVersion v : all_versions) {
        for (DxfFormat f : formats) {
            const std::string where = name + " " + version_label(v) + " " + format_label(f);
            const hdxf::internal::VersionCell& cell = matrix.at(v, f);

            if (cell.autocad == AutoCadStatus::Verified) {
                ++report.autocad_verified;
            } else if (cell.autocad == AutoCadStatus::NotSavable) {
                ++report.autocad_not_savable;
                report.not_tested.push_back(where + ": AutoCAD 2025 cannot save this version");
            } else {
                ++report.autocad_not_tested;
                report.not_tested.push_back(where + ": ej testat (no AutoCAD file)");
            }

            hdxf::Drawing d(v);
            add(d);
            if (cell.generation != Generation::None) {
                std::vector<uint8_t> bytes = d.save_to_memory(f);
                assert_true(!bytes.empty(), where + ": cannot be written: " + d.last_error());
                hdxf::Drawing r;
                assert_true(r.load_from_memory(bytes.data(), bytes.size()), where + ": cannot be read back: " + r.last_error());
                std::string why = verify(r, v, f);
                assert_true(why.empty(), where + ": " + why);
                // B12: nothing changed, the same bytes
                std::vector<uint8_t> again = r.save_to_memory(f);
                assert_true(again == bytes, where + ": written again unchanged, the bytes differ (" +
                                                std::to_string(bytes.size()) + " then " + std::to_string(again.size()) + ")");
                // The other format has the same content
                DxfFormat other = f == DxfFormat::Ascii ? DxfFormat::Binary : DxfFormat::Ascii;
                std::vector<uint8_t> converted = r.save_to_memory(other);
                assert_true(!converted.empty(), where + ": the other format cannot be written: " + r.last_error());
                hdxf::Drawing r2;
                assert_true(r2.load_from_memory(converted.data(), converted.size()),
                            where + ": the other format cannot be read: " + r2.last_error());
                why = verify(r2, v, other);
                assert_true(why.empty(), where + " (as " + format_label(other) + "): " + why);
                ++report.generated;
            } else if (cell.basis == ErrorBasis::Documented) {
                const size_t entities = d.entities().size();
                const size_t objects = d.objects().size();
                std::string handles;
                for (const auto& e : d.entities()) handles += e.props.handle + ";";
                for (const auto& o : d.objects()) handles += o.handle + ";";
                std::vector<uint8_t> bytes = d.save_to_memory(f);
                assert_true(bytes.empty(), where + ": a version without the type wrote a file");
                assert_eq(hdxf::internal::version_error_text(name, matrix, v), d.last_error(), where + ": the named error");
                // Before any mutation: nothing was added, no handle was given, and no file is made
                assert_eq(static_cast<int>(entities), static_cast<int>(d.entities().size()), where + ": entities changed");
                assert_eq(static_cast<int>(objects), static_cast<int>(d.objects().size()), where + ": objects changed");
                std::string after;
                for (const auto& e : d.entities()) after += e.props.handle + ";";
                for (const auto& o : d.objects()) after += o.handle + ";";
                assert_eq(handles, after, where + ": a failed save gave handles");
                std::filesystem::path path = std::filesystem::temp_directory_path() /
                                             ("hdxf_matrix_" + std::to_string(reinterpret_cast<uintptr_t>(&report)) + ".dxf");
                std::filesystem::remove(path);
                assert_false(d.save_as(path, f), where + ": save_as succeeded");
                assert_false(std::filesystem::exists(path), where + ": a partial file was left");
                std::filesystem::remove(path);
                ++report.named_errors;
            } else {
                ++report.not_established;
            }
        }
    }
    return report;
}

} // namespace hdxf_test
