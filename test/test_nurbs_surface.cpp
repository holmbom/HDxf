#include "test_framework.hpp"
#include "type_version_matrix.hpp"
#include "typed_corpus.hpp"
#include "hdxf.hpp"
#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <filesystem>
#include <map>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Step 9.16 of phase 9 (plan 2026-10-27-fas-9-typer.md): NURBSURFACE (F9.17).
//
// The AutoCAD originals are doc/facit/acad_<version>_dxf_surfaces_nurbs*.dxf (doc/facit/fas9_surfaces.scr: AutoCAD 2025's core
// makes a plane, extrusions, a sphere, a torus, a loft and a revolved line, converts them with CONVTONURBS and makes surfaces in
// SURFACEMODELINGMODE 1). What they show: before 2013 a NURBS surface is a SURFACE record (the class NURBSURFACE is in CLASSES
// all the same); from 2013 it is a NURBSURFACE whose AcDbNurbSurface has 170 and 290 and the points 10, 11, 12 and 13 — zero in
// every file, whatever surface it is: its knots and control points are in its ACIS body, which is what AcDbModelerGeometry
// holds (SAB in ACDSDATA from 2013). The name NURBSURFACE says nothing of knot fields.
// ---------------------------------------------------------------------------

using hdxf::Drawing;
using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf::Entity;
using hdxf::EntityType;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;

namespace {

Drawing facit(const std::string& name) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/" + name));
    if (in.empty()) hdxf_test::skip("missing doc/facit/" + name);
    Drawing d;
    assert_true(d.load_from_memory(in.data(), in.size()), name + ": " + d.last_error());
    return d;
}

std::vector<const Entity*> nurbs_of(const Drawing& d) {
    std::vector<const Entity*> out;
    for (const Entity& e : d.entities()) {
        if (e.type == EntityType::NurbsSurface) out.push_back(&e);
    }
    return out;
}

} // namespace

TEST(nurbs_surface_is_read_as_its_own_type_in_2013_and_2018) {
    for (const char* tag : {"2013", "2018"}) {
        for (const char* theme : {"surfaces_nurbs_converted", "surfaces_nurbs_made"}) {
            for (bool binary : {false, true}) {
                std::string name = std::string("acad_") + tag + "_dxf_" + theme + (binary ? "_bin" : "") + ".dxf";
                Drawing d = facit(name);
                std::vector<const Entity*> list = nurbs_of(d);
                assert_true(!list.empty(), name + ": a NURBSURFACE");
                for (const Entity* e : list) {
                    const hdxf::SurfaceData& s = e->as_surface();
                    assert_eq(static_cast<int>(s.nurbs.flag), 0, name + ": 170");
                    assert_false(s.nurbs.flag_290, name + ": 290");
                    assert_eq(s.nurbs.point_10.x, 0.0, name + ": 10");
                    assert_eq(s.nurbs.point_13.z, 0.0, name + ": 13");
                    assert_eq(s.u_isolines, 6, name + ": 71");
                    assert_true(!s.acis.sab.empty(), name + ": its body is in ACDSDATA");
                    assert_false(e->raw.empty(), name + ": its record is kept");
                }
            }
        }
    }
}

TEST(nurbs_surface_before_2013_is_a_surface_record) {
    for (const char* tag : {"2000", "2004", "2007", "2010"}) {
        Drawing d = facit(std::string("acad_") + tag + "_dxf_surfaces_nurbs_converted.dxf");
        assert_true(nurbs_of(d).empty(), std::string(tag) + ": no NURBSURFACE record");
        int surfaces = 0;
        for (const Entity& e : d.entities()) surfaces += e.type == EntityType::Surface ? 1 : 0;
        assert_true(surfaces > 0, std::string(tag) + ": the converted surfaces are SURFACE records");
    }
}

TEST(nurbs_surface_writes_back_unchanged_and_its_registry_entry_is_typed) {
    for (const char* tag : {"2013", "2018"}) {
        std::string name = std::string("acad_") + tag + "_dxf_surfaces_nurbs_converted.dxf";
        std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/" + name));
        if (in.empty()) hdxf_test::skip("missing " + name);
        Drawing d;
        assert_true(d.load_from_memory(in.data(), in.size()), d.last_error());
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        assert_true(!out.empty(), d.last_error());
        assert_true(out == in, name + ": written back byte for byte");
    }
    const hdxf::internal::TypedRegistry& registry = hdxf::internal::TypedRegistry::instance();
    (void)registry;
}

TEST(nurbs_surface_new_one_is_written_as_nurbsurface_from_2013_and_as_surface_before) {
    Drawing src = facit("acad_2013_dxf_surfaces_nurbs_made.dxf");
    std::vector<const Entity*> list = nurbs_of(src);
    assert_true(!list.empty(), "a NURBSURFACE in the file");
    hdxf::SurfaceData data = list[0]->as_surface();
    data.nurbs.flag = 3;
    data.nurbs.point_12 = {1.0, 2.0, 3.0};
    // 2013: the entity and its own subclass
    Drawing d13(DxfVersion::R2013);
    d13.add_surface(EntityType::NurbsSurface, data);
    std::vector<uint8_t> out = d13.save_to_memory();
    assert_true(!out.empty(), d13.last_error());
    Drawing back;
    assert_true(back.load_from_memory(out.data(), out.size()), back.last_error());
    std::vector<const Entity*> again = nurbs_of(back);
    assert_true(again.size() == static_cast<size_t>(1), "a NURBSURFACE comes back");
    assert_eq(static_cast<int>(again[0]->as_surface().nurbs.flag), 3, "170");
    assert_eq(again[0]->as_surface().nurbs.point_12.z, 3.0, "12");
    // 2010 and before: AutoCAD's SURFACE (the SAB of 2013 cannot be written there: SAB → SAT is a conversion)
    Drawing d10(DxfVersion::R2010);
    d10.add_surface(EntityType::NurbsSurface, data);
    assert_true(d10.save_to_memory().empty(), "a body of SAB only is an error before 2013");
}

// ---------------------------------------------------------------------------
// Step 9.S (Codex 18 and 8): NURBSURFACE as a codec of the registry, its version matrix over nine versions and two formats, and
// the values an older version cannot hold as a named error before anything changes
// ---------------------------------------------------------------------------

namespace {

// The body of AutoCAD's converted surface 177A in the version's own file (2000 ... 2010: SAT lines; 2013 and 2018: its SAB, as
// read from ACDSDATA): the one test_surface_isolines checks AutoCAD's isolines of in 2000 and 2004. R12, R13, R14: 2010's
hdxf::AcisData nurbs_body(DxfVersion v) {
    static std::map<int, hdxf::AcisData> cache;
    const DxfVersion from = v < DxfVersion::R2000 ? DxfVersion::R2010 : v;
    auto it = cache.find(static_cast<int>(from));
    if (it != cache.end()) return it->second;
    const char* tag = from == DxfVersion::R2000 ? "2000" : from == DxfVersion::R2004 ? "2004" : from == DxfVersion::R2007 ? "2007"
                      : from == DxfVersion::R2010 ? "2010" : from == DxfVersion::R2013 ? "2013" : "2018";
    Drawing d = facit(std::string("acad_") + tag + "_dxf_surfaces_nurbs_converted.dxf");
    for (const Entity& e : d.entities()) {
        if (e.props.handle == "177A") {
            cache[static_cast<int>(from)] = e.as_surface().acis;
            return e.as_surface().acis;
        }
    }
    assert_true(false, std::string(tag) + ": the surface 177A");
    return {};
}

void add_nurbs(Drawing& d, const hdxf::NurbSurface& n) {
    hdxf::SurfaceData data;
    data.acis = nurbs_body(d.version());
    data.nurbs = n;
    d.add_surface(EntityType::NurbsSurface, data);
}

} // namespace

TEST(nurbs_surface_has_its_codec_and_its_version_matrix_in_nine_versions_and_two_formats) {
    const hdxf::internal::TypedRegistry& registry = hdxf::internal::TypedRegistry::instance();
    const hdxf::internal::EntityCodec* codec = registry.find_entity("NURBSURFACE");
    assert_true(codec != nullptr && codec->read && codec->write && codec->value_error, "NURBSURFACE has a codec");
    assert_eq(16, codec->step, "of step 9.16");
    assert_true(codec->type == EntityType::NurbsSurface, "keyed on EntityType::NurbsSurface");
    const hdxf::internal::TypeRegistryEntry* entry = registry.find_entry("NURBSURFACE");
    assert_true(entry != nullptr && entry->status != hdxf::internal::InventoryStatus::AlreadyTyped,
                "the register has it typed by the codec, not by 1.8.1's reader");
    auto add = [](Drawing& d) { add_nurbs(d, hdxf::NurbSurface()); };
    auto verify = [](const Drawing& r, DxfVersion v, DxfFormat) -> std::string {
        const Entity* found = nullptr;
        for (const Entity& e : r.entities()) {
            if (e.type == EntityType::NurbsSurface || e.type == EntityType::Surface) found = &e;
        }
        if (!found) return "no surface";
        if (v >= DxfVersion::R2013) {
            if (found->type != EntityType::NurbsSurface) return "not a NURBSURFACE";
            if (found->as_surface().acis.sab.empty()) return "its body is not in ACDSDATA";
            const hdxf::NurbSurface& n = found->as_surface().nurbs;
            if (n.flag != 0 || n.flag_290 || n.point_12.z != 0.0) return "AcDbNurbSurface";
        } else {
            // AutoCAD's SURFACE before 2013, with the body's SAT lines
            if (found->type != EntityType::Surface) return "not a SURFACE";
            if (found->as_surface().acis.lines != nurbs_body(v).lines) return "its SAT lines";
        }
        if (found->as_surface().u_isolines != 6 || found->as_surface().v_isolines != 6) return "71 and 72";
        return {};
    };
    hdxf_test::MatrixReport report = hdxf_test::run_version_matrix("NURBSURFACE", codec->versions, add, verify);
    // 2000 ... 2018 in both formats (2000 ... 2010 as AutoCAD's SURFACE); R12, R13, R14 the named error
    assert_eq(12, report.generated, "generated");
    assert_eq(6, report.named_errors, "named errors");
    assert_eq(0, report.not_established, "not established");
    assert_eq(14, report.autocad_verified, "AutoCAD saved NURBS surfaces as R12 and 2000 ... 2018");
    assert_eq(4, report.autocad_not_savable, "R13, R14");
}

TEST(nurbs_surface_values_only_2013_holds_are_a_named_error_before_it_and_kept_from_it) {
    struct Case {
        const char* what;
        hdxf::NurbSurface n;
        std::string error;
    };
    std::vector<Case> cases;
    {
        hdxf::NurbSurface n;
        n.flag = 3;
        cases.push_back({"170", n, "NURBSURFACE's AcDbNurbSurface 170 = 3 requires DXF 2013 or later"});
    }
    {
        hdxf::NurbSurface n;
        n.flag_290 = true;
        cases.push_back({"290", n, "NURBSURFACE's AcDbNurbSurface 290 = 1 requires DXF 2013 or later"});
    }
    {
        hdxf::NurbSurface n;
        n.point_12 = {1.0, 2.5, -3.0};
        cases.push_back({"12", n, "NURBSURFACE's AcDbNurbSurface 12 = (1, 2.5, -3) requires DXF 2013 or later"});
    }
    {
        hdxf::NurbSurface n;
        n.point_13 = {0.0, 0.0, 7.0};
        cases.push_back({"13", n, "NURBSURFACE's AcDbNurbSurface 13 = (0, 0, 7) requires DXF 2013 or later"});
    }
    for (const Case& c : cases) {
        for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
            for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
                const std::string where = std::string(c.what) + " " + hdxf_test::version_label(v) + " " + hdxf_test::format_label(f);
                Drawing d(v);
                add_nurbs(d, c.n);
                const size_t entities = d.entities().size();
                const std::string handle = d.entities().back().props.handle;
                std::vector<uint8_t> out = d.save_to_memory(f);
                if (v < DxfVersion::R2013) {
                    assert_true(out.empty(), where + ": refused");
                    assert_true(d.last_error().rfind(c.error, 0) == 0, where + ": the named error, not \"" + d.last_error() + "\"");
                    assert_eq(static_cast<int>(entities), static_cast<int>(d.entities().size()), where + ": nothing changed");
                    assert_eq(handle, d.entities().back().props.handle, where + ": the same handle");
                    std::filesystem::path path = std::filesystem::temp_directory_path() / "hdxf_nurbs_value_error.dxf";
                    std::filesystem::remove(path);
                    assert_false(d.save_as(path, f), where + ": save_as refused");
                    assert_false(std::filesystem::exists(path), where + ": no file");
                    continue;
                }
                assert_true(!out.empty(), where + ": " + d.last_error());
                Drawing r;
                assert_true(r.load_from_memory(out.data(), out.size()), where + ": " + r.last_error());
                std::vector<const Entity*> list = nurbs_of(r);
                assert_eq(1, static_cast<int>(list.size()), where + ": a NURBSURFACE");
                const hdxf::NurbSurface& n = list[0]->as_surface().nurbs;
                assert_true(n.flag == c.n.flag && n.flag_290 == c.n.flag_290 && n.point_12.x == c.n.point_12.x &&
                                n.point_12.y == c.n.point_12.y && n.point_12.z == c.n.point_12.z && n.point_13.z == c.n.point_13.z,
                            where + ": the values come back");
                assert_true(r.save_to_memory(f) == out, where + ": written again unchanged");
            }
        }
    }
}

TEST(nurbs_surface_codec_reads_autocads_records_and_refuses_a_group_they_do_not_have) {
    std::vector<uint8_t> in = hdxf_test::read_file_bytes(hdxf_test::source_path("doc/facit/acad_2018_dxf_surfaces_nurbs_converted.dxf"));
    if (in.empty()) hdxf_test::skip("missing acad_2018_dxf_surfaces_nurbs_converted.dxf");
    // a group after AcDbNurbSurface's 13 that AutoCAD's records do not have: the record stays as read, with a warning
    std::string text(in.begin(), in.end());
    const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    size_t at = text.find("AcDbNurbSurface" + eol);
    assert_true(at != std::string::npos, "a NURBSURFACE in the file");
    size_t z13 = text.find(" 33" + eol, at);
    assert_true(z13 != std::string::npos, "its 33");
    size_t value_end = text.find(eol, z13 + 3 + eol.size());
    text.insert(value_end + eol.size(), " 14" + eol + "1.0" + eol);
    std::vector<uint8_t> bytes(text.begin(), text.end());
    Drawing d;
    assert_true(d.load_from_memory(bytes.data(), bytes.size()), d.last_error());
    int untyped = 0;
    for (const Entity& e : d.entities()) untyped += e.type == EntityType::Unknown && e.as_unknown().entity_type == "NURBSURFACE";
    assert_eq(1, untyped, "the changed record is untyped");
    bool warned = false;
    for (const auto& w : d.warnings()) warned = warned || (w.find("NURBSURFACE") != std::string::npos && w.find("kept as it was read") != std::string::npos);
    assert_true(warned, "a warning names it");
    std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
    assert_true(out == bytes, "written back as it was read");
}
