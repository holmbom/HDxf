#include "TypedRecordTypes.hpp"
#include <algorithm>
#include <string>

// NURBSURFACE as a codec of the registry (phase 9, step 9.S; Codex's review of 9.16, findings 8 and 18). The model is 9.16's
// (`SurfaceData` with `NurbSurface`, plan 2026-10-27-fas-9-typer.md "9.16"); what this file adds is the integration contract
// of 9.0: the reader, the writer, the version matrix and the error for the values an older version cannot hold.
//
// AutoCAD's files (doc/facit/acad_<version>_dxf_surfaces_nurbs*.dxf, doc/facit/fas9_surfaces.scr, F9.17):
//   2013, 2018     NURBSURFACE: AcDbEntity, AcDbModelerGeometry (290 and the change GUID 2; the body's SAB in ACDSDATA),
//                  AcDbSurface (71, 72), AcDbNurbSurface (170, 290, 10, 11, 12, 13 — 0 in every AutoCAD file)
//   2000 … 2010    the same surface as SURFACE: AcDbModelerGeometry with the SAT lines, AcDbSurface; no AcDbNurbSurface. The
//                  proxy graphics of 2000 and 2004 are its isolines (surface_graphics.cpp)
//   R12            no surface (AutoCAD's R12 file has none); R13 and R14 have no surfaces
// So the older form holds what AutoCAD's 0s hold and nothing else: a NURBSURFACE whose 170, 290 or 10 … 13 is not 0 is a
// named error before 2013 ("NURBSURFACE's AcDbNurbSurface 170 = 3 requires DXF 2013 or later"), checked before anything
// changes (typed_version_error(), as the matrix's errors). The record is written by the writer of the surfaces
// (DxfWriter::write_surface_r2000 through RecordWriter::surface): its name, proxy graphics and body per version are the ones
// the five other surface types have.

namespace hdxf {
namespace internal {

namespace {

// The SAT lines of AcDbModelerGeometry (the reader's rule, dxf_reader.cpp AcisLineReader): a line is its 3s and the 1 that ends
// it; a 3 after a 1 of 255 characters or more goes on with that 1 (the reference's form). The length counted is the value's as
// decoded (a caret escape in a 1 of 254 characters is counted once here; no AutoCAD file has a NURBSURFACE with SAT lines:
// AutoCAD writes them only from 2013, with the body in ACDSDATA)
class SatLines {
public:
    explicit SatLines(AcisData& d) : d_(d) {}

    void continue_line(const std::string& part) {
        if (!has_pending_ && last_long_) {
            d_.lines.back() += part;
            return;
        }
        pending_ += part;
        has_pending_ = true;
    }
    void end_line(const std::string& part) {
        last_long_ = !has_pending_ && part.size() >= 255;
        d_.lines.push_back(pending_ + part);
        pending_.clear();
        has_pending_ = false;
    }
    void finish() {
        if (has_pending_) end_line(std::string());
    }

private:
    AcisData& d_;
    std::string pending_;
    bool has_pending_ = false;
    bool last_long_ = false;
};

constexpr const char* kNullGuid = "{00000000-0000-0000-0000-000000000000}";

bool read_nurbs_surface(const RecordView& view, TypedReadContext& context, Entity& e) {
    SurfaceData d;
    {
        GroupCursor c(view, "AcDbModelerGeometry");
        if (!c.valid()) return context.fail("NURBSURFACE " + view.head().handle + " has no AcDbModelerGeometry subclass");
        SatLines sat(d.acis);
        while (c.ok() && !c.at_end()) {
            switch (c.code()) {
                case 70: c.integer(70, d.acis.version); break;
                case 290: {
                    bool has_guid = false;
                    c.boolean(290, has_guid);
                    break;
                }
                case 2: {
                    std::string guid;
                    c.string(2, guid);
                    if (guid != kNullGuid) d.acis.guid = guid;
                    break;
                }
                case 1: {
                    std::string part;
                    c.string(1, part);
                    sat.end_line(part);
                    break;
                }
                case 3: {
                    std::string part;
                    c.string(3, part);
                    sat.continue_line(part);
                    break;
                }
                default:
                    return context.fail("NURBSURFACE " + view.head().handle + ": AcDbModelerGeometry has a group " +
                                        std::to_string(c.code()) + " that AutoCAD's surfaces do not have");
            }
        }
        if (!c.ok()) return context.fail(c.error());
        sat.finish();
    }
    {
        GroupCursor c(view, "AcDbSurface");
        if (!c.valid()) return context.fail("NURBSURFACE " + view.head().handle + " has no AcDbSurface subclass");
        c.integer(71, d.u_isolines);
        c.integer(72, d.v_isolines);
        if (!c.ok()) return context.fail(c.error());
        if (!c.at_end()) {
            return context.fail("NURBSURFACE " + view.head().handle + ": AcDbSurface has a group " + std::to_string(c.code()) +
                                " after 71 and 72");
        }
    }
    {
        // AutoCAD's order: 170, 290, 10, 11, 12, 13
        GroupCursor c(view, "AcDbNurbSurface");
        if (!c.valid()) return context.fail("NURBSURFACE " + view.head().handle + " has no AcDbNurbSurface subclass");
        c.integer(170, d.nurbs.flag);
        c.boolean(290, d.nurbs.flag_290);
        c.point3(10, d.nurbs.point_10);
        c.point3(11, d.nurbs.point_11);
        c.point3(12, d.nurbs.point_12);
        c.point3(13, d.nurbs.point_13);
        if (!c.ok()) return context.fail(c.error());
        if (!c.at_end()) {
            return context.fail("NURBSURFACE " + view.head().handle + ": AcDbNurbSurface has a group " + std::to_string(c.code()) +
                                " that is not AutoCAD's 170, 290, 10, 11, 12, 13 in that order");
        }
    }
    e.data = Boxed<SurfaceData>(std::move(d));
    return true;
}

void write_nurbs_surface(RecordWriter& w, const Entity& e, const std::string& owner) { w.surface(e, owner); }

bool is_zero(const Point3& p) { return p.x == 0.0 && p.y == 0.0 && p.z == 0.0; }

std::string point_text(const Point3& p) {
    auto number = [](double v) {
        std::string s = std::to_string(v);
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    };
    return "(" + number(p.x) + ", " + number(p.y) + ", " + number(p.z) + ")";
}

// Before 2013 AutoCAD writes a NURBS surface as SURFACE, without AcDbNurbSurface: only the 0s it has in every one of its files
// are held there (its files of 2000 … 2010). Another value is the first one named
std::string nurbs_value_error(const Entity& e, DxfVersion version) {
    if (version >= DxfVersion::R2013 || version < DxfVersion::R2000) return {};
    const NurbSurface& n = e.as_surface().nurbs;
    const std::string head = "NURBSURFACE's AcDbNurbSurface ";
    const std::string tail = " requires DXF 2013 or later (before 2013 AutoCAD writes the surface as SURFACE, without it)";
    if (n.flag != 0) return head + "170 = " + std::to_string(n.flag) + tail;
    if (n.flag_290) return head + "290 = 1" + tail;
    const Point3* points[4] = {&n.point_10, &n.point_11, &n.point_12, &n.point_13};
    for (int i = 0; i < 4; ++i) {
        if (!is_zero(*points[i])) return head + std::to_string(10 + i) + " = " + point_text(*points[i]) + tail;
    }
    return {};
}

} // namespace

void register_nurbs_surface_codecs(TypedRegistry& registry) {
    EntityCodec c;
    c.dxf_name = "NURBSURFACE";
    c.type = EntityType::NurbsSurface;
    c.read = read_nurbs_surface;
    c.write = write_nurbs_surface;
    c.value_error = nurbs_value_error;
    // The class NURBSURFACE / AcDbNurbSurface is the writer's own row (dxf_writer_2000.cpp): no ClassSpec, so not twice.
    // Extents: none here — the bodies' ACIS extents (extents.cpp) take it as every surface
    c.versions = VersionMatrix::unestablished();
    // R12: AutoCAD's R12 file of the NURBS surfaces has none; R13 and R14 have no surfaces (the R14 reference's entities)
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R13, DxfVersion::R14}) c.versions.set(v, Generation::None, ErrorBasis::Documented);
    // 2000 … 2010: AutoCAD's SURFACE with the same body (OlderForm); 2013 and 2018: NURBSURFACE
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010}) {
        c.versions.set(v, Generation::OlderForm, ErrorBasis::NotApplicable);
    }
    for (DxfVersion v : {DxfVersion::R2013, DxfVersion::R2018}) c.versions.set(v, Generation::Native, ErrorBasis::NotApplicable);
    // AutoCAD 2025 saved NURBS surfaces (converted and made) as R12 and 2000 … 2018, ASCII and binary (F9.17)
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        c.versions.autocad(v, AutoCadStatus::Verified);
    }
    c.step = 16;
    registry.add(std::move(c));
}

} // namespace internal
} // namespace hdxf
