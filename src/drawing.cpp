#include "hdxf.hpp"
#include "ascii_form.hpp"
#include "dxf_writer.hpp"
#include "dxf_reader.hpp"
#include "header_defaults.hpp"
#include "handles.hpp"
#include "template_2000.hpp"
#include "extents.hpp"
#include "surface_graphics.hpp"
#include "text_layout.hpp"
#include "standard_objects.hpp"
#include "roundtrip.hpp"
#include "acis.hpp"
#include "acis_sab.hpp"
#include "dimstyle_legacy.hpp"
#include "older_checks.hpp"
#include "TypedRecordTypes.hpp"
#include "TableLayout.hpp"
#include <memory>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <algorithm>
#include <set>
#include <cmath>

namespace hdxf {

namespace {

// XLINE and RAY store a unit direction (AutoCAD normalizes it)
Point3 unit(Point3 v) {
    double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return len > 0.0 ? Point3{v.x / len, v.y / len, v.z / len} : v;
}

// A record the file had (its handle among `loaded`, sorted)
bool in_file(const std::vector<uint64_t>& loaded, const std::string& handle) {
    uint64_t v = 0;
    if (internal::parse_handle(handle, v) != internal::HandleParse::Ok) return false;
    return std::binary_search(loaded.begin(), loaded.end(), v);
}

// The DXF name of a surface type, nullptr for another
const char* surface_name(EntityType type) {
    switch (type) {
        case EntityType::Surface:         return "SURFACE";
        case EntityType::ExtrudedSurface: return "EXTRUDEDSURFACE";
        case EntityType::LoftedSurface:   return "LOFTEDSURFACE";
        case EntityType::RevolvedSurface: return "REVOLVEDSURFACE";
        case EntityType::SweptSurface:    return "SWEPTSURFACE";
        case EntityType::PlaneSurface:    return "PLANESURFACE";
        case EntityType::NurbsSurface:    return "NURBSURFACE";
        default:                          return nullptr;
    }
}

// Why the first body (3DSOLID, REGION, BODY, a surface) with SAT data cannot be written as 2013
// (SAB `format` 21800) or 2018 (22300), "<NAME>: <why>"; empty when each can. In 2013 and 2018 the
// ACIS data is SAB in the ACDSDATA section, made from the SAT lines when the body is written
// (src/acis_sab.cpp, plan phase 5 B62/B63 and phase 6 B80). A body read from such a file has no SAT data.
std::string sat_body_error(const Drawing& dwg, int format) {
    auto check = [format](const Entity& e) -> std::string {
        const char* name = nullptr;
        const AcisData* d = nullptr;
        switch (e.type) {
            case EntityType::Solid3D: name = "3DSOLID"; d = &e.as_acis(); break;
            case EntityType::Region:  name = "REGION"; d = &e.as_acis(); break;
            case EntityType::Body:    name = "BODY"; d = &e.as_acis(); break;
            default:
                name = surface_name(e.type);
                if (name) d = &e.as_surface().acis;
                break;
        }
        if (!d || d->lines.empty()) return std::string();
        std::vector<uint8_t> sab;
        std::string error;
        if (internal::sab_from_sat(internal::acis_deciphered(*d), sab, error, format)) return std::string();
        return std::string(name) + ": " + error;
    };
    for (const auto& e : dwg.entities()) {
        std::string error = check(e);
        if (!error.empty()) return error;
    }
    for (const auto& b : dwg.blocks()) {
        for (const auto& e : b.entities) {
            std::string error = check(e);
            if (!error.empty()) return error;
        }
    }
    return std::string();
}

// What the drawing has that DXF 2007 introduced, for the error when it is written as an
// older version (plan B35); empty when there is nothing. Converting it is phase 8.
// `loaded`: the records of the file the drawing is written back to (sorted handles), when it
// is written as the version it was read from — a surface of that file keeps its record (B39)
std::string r2007_property(const Drawing& dwg, const std::vector<uint64_t>* loaded, DxfVersion version) {
    const Vport vp0;
    const ViewportData d0;
    auto same_color = [](const Color& a, const Color& b) {
        return a.aci == b.aci && a.rgb == b.rgb && a.name == b.name;
    };
    for (const auto& l : dwg.layers()) {
        if (!l.material_handle.empty()) return "A layer's material";
    }
    for (const auto& vp : dwg.vports()) {
        if (!vp.visual_style_handle.empty()) return "A visual style";
        if (!vp.background_handle.empty()) return "A background";
        if (!vp.sun_handle.empty()) return "A sun";
        if (!vp.shade_plot_handle.empty()) return "A VPORT's shade plot object";
        if (vp.grid_flags != vp0.grid_flags || vp.grid_major != vp0.grid_major) return "A VPORT's grid settings";
        if (vp.default_lighting != vp0.default_lighting || vp.default_lighting_type != vp0.default_lighting_type ||
            vp.brightness != vp0.brightness || vp.contrast != vp0.contrast ||
            !same_color(vp.ambient_color, vp0.ambient_color)) {
            return "A VPORT's lighting";
        }
    }
    // DIMARCSYM is a group code (90) of DIMSTYLE from 2007; AutoCAD's 2000/2004 files have it in
    // neither group code nor XDATA
    const int arcsym0 = internal::autocad_dim_style().dimarcsym;
    for (const auto& ds : dwg.dim_styles()) {
        if (ds.dimarcsym != arcsym0) return "A dimension style's arc length symbol (DIMARCSYM)";
    }
    for (const auto& v : dwg.views()) {
        if (!v.visual_style_handle.empty()) return "A visual style";
        if (!v.background_handle.empty()) return "A background";
        if (!v.sun_handle.empty()) return "A sun";
        if (!v.live_section_handle.empty()) return "A live section";
        if (v.camera_plottable) return "A plottable camera";
    }
    for (const auto& b : dwg.blocks()) {
        const BlockRecord r0;
        if (b.record.insert_units != r0.insert_units || b.record.explodable != r0.explodable ||
            b.record.scalable != r0.scalable) {
            return "A block's units, explodability and scaling";
        }
    }
    std::string found;
    auto entities = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (!found.empty()) return;
            bool material = !e.props.material_handle.empty();
            if (e.type == EntityType::Insert) {
                for (const auto& a : e.as_insert().attributes) material = material || !a.props.material_handle.empty();
            }
            if (material) found = "A material";
            else if (e.props.shadow_mode != 0) found = "An entity's shadow mode";
            else if (e.type == EntityType::Solid3D && !e.as_acis().history_handle.empty()) found = "A solid's history";
            // A surface written anew in R12, R13 or R14, which have none, unless it is a surface of the file written back
            // as that file's version (its preserved record keeps it, B39). In 2000 and 2004: undrawn_surface()
            else if (surface_name(e.type) && !(loaded && !e.raw.empty() && in_file(*loaded, e.props.handle)) &&
                     version < DxfVersion::R2000) {
                found = surface_name(e.type);
            }
            else if (e.type == EntityType::Viewport) {
                const ViewportData& d = e.as_viewport();
                if (!d.visual_style_handle.empty()) found = "A visual style";
                else if (!d.background_handle.empty()) found = "A background";
                else if (!d.sun_handle.empty()) found = "A sun";
                else if (!d.shade_plot_handle.empty()) found = "A viewport's shade plot object";
                else if (d.grid_major != d0.grid_major) found = "A viewport's grid settings";
                else if (d.default_lighting != d0.default_lighting || d.default_lighting_type != d0.default_lighting_type ||
                         d.brightness != d0.brightness || d.contrast != d0.contrast ||
                         !same_color(d.ambient_color, d0.ambient_color)) {
                    found = "A viewport's lighting";
                }
            }
        }
    };
    entities(dwg.entities());
    for (const auto& b : dwg.blocks()) entities(b.entities);
    return found;
}

// A surface written anew as DXF 2000 or 2004 whose isolines HDxf does not make: AutoCAD's 2000 and 2004 files keep them as
// proxy graphics, which HDxf makes for the bodies it knows (surface_graphics.cpp, plan 9.16). The error says that, not a
// version the surface needs (Codex 9.S, 13); empty when every surface is drawn or kept as read (B39)
std::string undrawn_surface(const Drawing& dwg, const std::vector<uint64_t>* loaded) {
    std::string found;
    auto entities = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (!found.empty()) return;
            if (!surface_name(e.type) || (loaded && !e.raw.empty() && in_file(*loaded, e.props.handle))) continue;
            std::string why = internal::surface_isoline_error(e);
            if (!why.empty()) {
                found = std::string(surface_name(e.type)) + " " + e.props.handle + ": HDxf does not make its isolines (" + why +
                        "), which DXF 2000 and 2004 keep as proxy graphics";
            }
        }
    };
    entities(dwg.entities());
    for (const auto& b : dwg.blocks()) entities(b.entities);
    return found;
}

// What the drawing has that DXF 2010 introduced, for the error when it is written as an
// older version (plan phase 4, B46); empty when there is nothing. Converting it is phase 8.
std::string r2010_property(const Drawing& dwg) {
    if (const HeaderVar* h = dwg.find_header("$DIMTXTDIRECTION");
        h && !h->values.empty() && std::atoi(h->values[0].second.c_str()) != 0) {
        return "DIMTXTDIRECTION";
    }
    for (const auto& ds : dwg.dim_styles()) {
        if (ds.dimtxtdirection != 0) return "DIMTXTDIRECTION";
    }
    for (const auto& o : dwg.objects()) {
        if (o.type == ObjectType::MLeaderStyle && o.as_mleader_style().text_attachment_direction != 0) {
            return "A multileader style's vertical attachment";
        }
        // AutoCAD writes a GEODATA as version 1 before 2010, with a WKT of its coordinate system
        // HDxf does not make (B50)
        if (o.type == ObjectType::GeoData && o.as_geodata().version >= 2) return "A GEODATA of version 2 or 3";
    }

    std::string found;
    auto entities = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (!found.empty()) return;
            if (e.type == EntityType::Image && e.as_image().clip_inside) found = "An image's clip mode inside";
            // A vertex's identifier (91): 2010's; AutoCAD's 2000 … 2007 files have none (plan 9.2, F9.21)
            if (e.type == EntityType::LwPolyline) {
                for (const auto& v : e.as_lwpolyline().vertices) {
                    if (v.id) found = "A polyline vertex's identifier";
                }
            }
            if (e.type == EntityType::Polyline) {
                for (const auto& v : e.as_polyline().vertices) {
                    if (v.id) found = "A polyline vertex's identifier";
                }
            }
            if (e.type == EntityType::Hatch) {
                for (const auto& p : e.as_hatch().paths) {
                    for (const auto& edge : p.edges) {
                        if (edge.type == HatchEdgeType::Spline && !edge.fit_points.empty()) {
                            found = "A hatch spline edge's fit points";
                        }
                    }
                }
            }
            if (e.type == EntityType::MLeader) {
                const MLeaderData& d = e.as_mleader();
                bool vertical = d.text_attachment_direction != 0;
                for (const auto& l : d.context.leaders) vertical = vertical || l.attachment_direction != 0;
                if (vertical) found = "A MULTILEADER's vertical attachment";
                for (const auto& l : d.context.leaders) {
                    for (const auto& line : l.lines) {
                        if (line.leader_type || line.color || line.linetype || line.lineweight || line.arrow_size ||
                            line.arrow_block || line.override_flags) {
                            found = "A MULTILEADER leader line's own properties";
                        }
                    }
                }
            }
        }
    };
    entities(dwg.entities());
    for (const auto& b : dwg.blocks()) entities(b.entities);
    return found;
}

// What the drawing has that DXF 2013 introduced and 2000 … 2010 cannot have, for the error when it
// is written as an older version (plan phase 5, B60): a layer's 348, a GEODATA of version 3 as 2010,
// a body with only SAB data (B61); empty when there is nothing. A linetype's
// upright elements, a multileader's and a multileader style's text_extended and a body's change
// GUID are written before 2013 as AutoCAD writes them there.
// (A GEODATA of version 3 before 2010 is the 2010 error: AutoCAD writes version 1 there, B50.)
std::string r2013_property(const Drawing& dwg, DxfVersion version) {
    for (const auto& l : dwg.layers()) {
        if (!l.pointer_348.empty() && l.pointer_348 != "0") return "Layer \"" + l.name + "\": 348";
    }
    for (const auto& o : dwg.objects()) {
        if (version >= DxfVersion::R2010 && o.type == ObjectType::GeoData && o.as_geodata().version >= 3) {
            return "A GEODATA of version 3";
        }
        // The coordination model (B78): AutoCAD 2013's. (The RAPIDRT objects are in AutoCAD's files from 2000 on; their
        // version errors are their codecs', step 9.11)
        if (o.type == ObjectType::NavisworksModelDef) return "A coordination model definition";
    }
    {
        std::string model;
        auto models = [&](const std::vector<Entity>& list) {
            for (const auto& e : list) if (model.empty() && e.type == EntityType::CoordinationModel) model = "A coordination model";
        };
        models(dwg.entities());
        for (const auto& b : dwg.blocks()) models(b.entities);
        if (!model.empty()) return model;
    }
    // A body with only SAB data (ACDSDATA): SAB → SAT is a conversion (phase 8). R13 and R14 have
    // their own error for it (internal::acis_target_error(), B95)
    const bool r13_r14 = dwg.version() == DxfVersion::R13 || dwg.version() == DxfVersion::R14;
    std::string found;
    auto bodies = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            const AcisData* acis = internal::acis_of(e);
            if (!r13_r14 && found.empty() && acis && acis->lines.empty() && !acis->sab.empty()) {
                const char* surface = surface_name(e.type);
                found = std::string("A ") + (surface ? surface : e.type == EntityType::Solid3D ? "3DSOLID"
                                                               : e.type == EntityType::Region  ? "REGION"
                                                                                               : "BODY") +
                        " with only SAB data";
            }
        }
    };
    bodies(dwg.entities());
    for (const auto& b : dwg.blocks()) bodies(b.entities);
    return found;
}

// What the drawing has that DXF 2018 introduced and 2013 and earlier cannot have, for the error when it
// is written as an older version (plan phase 6, B82); empty when there is nothing. An MTEXT's columns
// and frame, a multiline attribute and the view styles' display names have forms of their own before
// 2018 (B74, B76, B77).
std::string r2018_property(const Drawing& dwg) {
    std::string found;
    // R12 has no OBJECTS to hold a multiline attribute's text (B76): it is an error there, not a
    // silent loss of the text
    const bool r12 = dwg.version() < DxfVersion::R2000;
    auto attribute = [&](const OptionalBoxed<MultilineAttribute>& ml) {
        if (!found.empty() || !ml) return;
        if (r12 && ml->has_text) {
            found = "A multiline attribute";
            return;
        }
        if (!ml->has_text) found = "An attribute's type (71)";
        else if (ml->text.columns) found = "An attribute's text columns";
    };
    auto entities = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            if (e.type == EntityType::Attdef) attribute(e.as_attdef().multiline);
            if (e.type == EntityType::Attrib) attribute(e.as_attrib().multiline);
            if (e.type != EntityType::Insert) continue;
            for (const auto& a : e.as_insert().attributes) attribute(a.data.multiline);
        }
    };
    entities(dwg.entities());
    for (const auto& b : dwg.blocks()) entities(b.entities);
    return found;
}

// The first change GUID of a body that is not "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}"; empty for none
std::string invalid_change_guid(const Drawing& dwg) {
    std::string found;
    auto check = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            const AcisData* acis = internal::acis_of(e);
            XdataList x;
            if (found.empty() && acis && !acis->guid.empty() && !internal::change_guid_xdata(acis->guid, x)) {
                found = acis->guid;
            }
        }
    };
    check(dwg.entities());
    for (const auto& b : dwg.blocks()) check(b.entities);
    return found;
}

// What the drawing has that DXF 2004 introduced, for the error when it is written as an
// older version (plan B23); empty when there is nothing. Converting it is phase 8.
std::string r2004_property(const Drawing& dwg) {
    auto plot_settings = [](const PlotSettings& p) {
        PlotSettings standard;
        return p.shade_plot_mode != standard.shade_plot_mode ||
               p.shade_plot_resolution != standard.shade_plot_resolution ||
               p.shade_plot_dpi != standard.shade_plot_dpi || !p.shade_plot_handle.empty();
    };
    for (const auto& o : dwg.objects()) {
        if (o.type == ObjectType::Layout && plot_settings(o.as_layout().plot_settings)) return "A layout's shade plot settings";
        if (o.type == ObjectType::PlotSettings && plot_settings(o.as_plot_settings())) return "A page setup's shade plot settings";
    }
    for (const auto& o : dwg.objects()) {
        if (o.type == ObjectType::DbColor && (o.as_db_color().color.aci != 256 ||
                                              o.as_db_color().color.has_rgb() ||
                                              !o.as_db_color().color.name.empty())) {
            return "A DBCOLOR's color";
        }
    }
    std::string found;
    auto entities = [&](const std::vector<Entity>& list) {
        for (const auto& e : list) {
            bool transparent = !e.props.transparency.is_by_layer();
            if (e.type == EntityType::Insert) {
                for (const auto& a : e.as_insert().attributes) {
                    transparent = transparent || !a.props.transparency.is_by_layer();
                }
            }
            if (transparent) {
                found = "Transparency";
                return;
            }
            if (e.type == EntityType::Viewport && e.as_viewport().shade_plot_mode != 0) {
                found = "A viewport's shade plot mode";
                return;
            }
            if (e.type == EntityType::MText && e.as_mtext().background.flags != 0) {
                found = "An MTEXT background";
                return;
            }
            if (e.type == EntityType::Hatch && e.as_hatch().gradient) {
                found = "A gradient fill";
                return;
            }
        }
    };
    entities(dwg.entities());
    for (const auto& b : dwg.blocks()) {
        if (found.empty()) entities(b.entities);
    }
    if (!found.empty()) return found;
    internal::for_each_color(dwg, [&](const Color& c, bool) {
        if (!found.empty()) return;
        if (c.has_rgb()) found = "True color";
        else if (!c.name.empty()) found = "A color book color";
    });
    return found;
}

// Why the drawing cannot be written as its version, the first fault; empty when there is none (plan
// B11/B20/B30/B42/B56/B69/B86/B98). The one list of the checks, in the one order they run in, each once:
//   1. R13 and R14: layouts, the objects, the entities of model space, paper space and every block with
//      their attributes, the header, the dimension styles and the tables (internal::older_version_error(),
//      names and 2000's properties, a lineweight and a plot style among them)
//   1b. the typed records of phase 9 (internal::typed_version_error()): a record of a type whose version matrix
//      has no form in the version, with a documented basis (B101)
//   2. R12: the XDATA and transparency of a layer, the XDATA of a text style
//   3. GEODATA as 2010 or 2013, a body without SAT as 2013 or later
//   4. what 2018, 2013 (and a body's change GUID), 2010, 2007 and 2004 introduced, newest first
//   5. R12: the types R13, R14 and 2000 introduced (R13 and R14 name theirs in 1)
//   6. R13 and R14: the ACIS data of the bodies (B95)
// `loaded`: the handles of the file the drawing was read from (null for a drawing built in memory, and
// for one read from R12); `original`: its preserved records read again (null likewise, and for R12
// read from a file only the R12 write needs it). What the file had is written from its preserved record
// and is not the program's fault (7.20).
std::string target_version_error(const Drawing& dwg, const std::vector<uint64_t>* loaded, const Drawing* original,
                                 DxfFormat format) {
    const DxfVersion version = dwg.version();
    const bool older = version == DxfVersion::R13 || version == DxfVersion::R14;   // no layouts, one paper space

    if (older) {
        std::string error = internal::older_version_error(dwg, version, loaded ? original : nullptr);
        if (!error.empty()) return error;
    }
    // The records of the types phase 9 typed, where their version matrix says the version has none (B101)
    {
        std::string error = internal::typed_version_error(dwg, version, format);
        if (!error.empty()) return error;
    }
    // A layer's XDATA and transparency (XDATA too) are R13's and 2000's: R12 has no XDATA on tables
    if (version == DxfVersion::R12) {
        for (const auto& l : dwg.layers()) {
            if (!l.transparency.is_by_layer()) return "Layer \"" + l.name + "\": transparency requires DXF 2000 or later";
            if (!l.xdata.empty()) return "Layer \"" + l.name + "\": layer XDATA requires DXF R13 or later";
        }
        for (const auto& s : dwg.text_styles()) {
            if (!s.xdata.empty()) return "Text style \"" + s.name + "\": text style XDATA requires DXF R13 or later";
        }
    }
    // A GEODATA of version 1 as 2010: AutoCAD makes it version 2 — a conversion (B50); of version
    // 1 as 2013: AutoCAD makes it version 3 (B64)
    if (version >= DxfVersion::R2010) {
        for (const auto& o : dwg.objects()) {
            if (o.type != ObjectType::GeoData) continue;
            // A GEODATA read from a file is written as it was. Version 2 is a record of 2013 and 2018 too: AutoCAD 2025
            // keeps version 2 of a file it opened when it saves them (doc/facit/acad_2013_dxf_proxy.dxf, ext_map_drawing3,
            // step 9.3), so it is written from its values as well
            if (!o.raw.empty()) continue;
            int v = o.as_geodata().version;
            if (version >= DxfVersion::R2013 && v < 2) {
                return "A GEODATA of version " + std::to_string(v) + " is written as version 3 in DXF 2013";
            }
            if (v == 1) return "Writing a GEODATA of version 1 as DXF 2010 is not supported yet (AutoCAD makes it version 2)";
        }
    }
    if (version >= DxfVersion::R2013) {
        std::string error = sat_body_error(dwg, internal::sab_format_for(version));
        if (!error.empty()) return error;
    }
    // What 2018 introduced cannot be written as an older version (B82)
    if (version < DxfVersion::R2018) {
        std::string what = r2018_property(dwg);
        if (!what.empty()) {
            return what + (version < DxfVersion::R2000 && what == "A multiline attribute"
                               ? " requires DXF 2000 or later" : " requires DXF 2018 or later");
        }
    }
    // What 2013 introduced cannot be written as R12 … 2010 (B60); a body's change GUID is XDATA
    // before 2013, made from its string
    if (version < DxfVersion::R2013) {
        std::string what = r2013_property(dwg, version);
        if (!what.empty()) return what + " requires DXF 2013 or later";
        std::string guid = invalid_change_guid(dwg);
        if (!guid.empty()) return "A body's change GUID \"" + guid + "\" is not a GUID";
    }
    // What 2010 introduced cannot be written as R12, 2000, 2004 or 2007 (B46)
    if (version < DxfVersion::R2010) {
        std::string what = r2010_property(dwg);
        if (!what.empty()) return what + " requires DXF 2010 or later";
    }
    // A surface whose isolines HDxf does not make cannot be written as 2000 or 2004
    if (version >= DxfVersion::R2000 && version < DxfVersion::R2007) {
        std::string what = undrawn_surface(dwg, loaded);
        if (!what.empty()) return what;
    }
    // What 2007 introduced cannot be written as R12, 2000 or 2004 (B35)
    if (version < DxfVersion::R2007) {
        std::string what = r2007_property(dwg, loaded, version);
        if (!what.empty()) return what + " requires DXF 2007 or later";
    }
    // What 2004 introduced cannot be written as R12 or 2000 (B23)
    if (version < DxfVersion::R2004) {
        std::string what = r2004_property(dwg);
        if (!what.empty()) return what + " requires DXF 2004 or later";
    }
    // The types R13, R14 and 2000 introduced cannot be written as R12 (B11, B98)
    if (version == DxfVersion::R12) {
        for (const auto& e : dwg.entities()) {
            std::string error = internal::entity_type_error(e.type, version);
            if (!error.empty()) return error;
        }
        for (const auto& b : dwg.blocks()) {
            for (const auto& e : b.entities) {
                std::string error = internal::entity_type_error(e.type, version);
                if (!error.empty()) return error;
            }
        }
    }
    // ACIS in R13 and R14 (B95): the SAT as it is, or an error naming why not — never a changed SAT header,
    // a SAB → SAT conversion or an ACDSDATA record
    if (older) return internal::acis_target_error(dwg, version, original);
    return {};
}

// The blocks, text styles and linetypes of a drawing as read: "B:", "S:" or "L:" and the name
// in upper case, sorted (Drawing::loaded_names_)
std::vector<std::string> record_names(const Drawing& dwg) {
    std::vector<std::string> names;
    auto add = [&](const char* kind, std::string name) {
        for (auto& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        names.push_back(kind + name);
    };
    for (const auto& b : dwg.blocks()) add("B:", b.name);
    for (const auto& s : dwg.text_styles()) add("S:", s.name);
    for (const auto& l : dwg.linetypes()) add("L:", l.name);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructors & init
// ---------------------------------------------------------------------------

Drawing::Drawing() {
    init_defaults();
}

Drawing::Drawing(DxfVersion version)
    : version_(version)
{
    init_defaults();
    if (version_ >= DxfVersion::R2007) ensure_r2007_objects();
    else if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) {
        apply_older_header_defaults();
        ensure_older_structure();
    }
}

void Drawing::apply_older_header_defaults() {
    if (version_ != DxfVersion::R13 && version_ != DxfVersion::R14) return;
    if (source_version_ != DxfVersion::R12 || loaded_r12_) return;     // read from a file: its own values
    const std::vector<HeaderVar> r12 = internal::new_drawing_r12_headers();
    const std::vector<HeaderVar> own = version_ == DxfVersion::R13 ? internal::default_r13_headers()
                                                                   : internal::default_r14_headers();
    auto find = [](const std::vector<HeaderVar>& list, const std::string& name) -> const HeaderVar* {
        for (const auto& v : list) if (v.name == name) return &v;
        return nullptr;
    };
    for (const char* name : {"$MIRRTEXT", "$PDMODE", "$MENU", "$BLIPMODE", "$MAXACTVP", "$VISRETAIN"}) {
        HeaderVar* now = find_header(name);
        const HeaderVar* was = find(r12, name);
        const HeaderVar* wanted = find(own, name);
        if (now && was && wanted && now->values == was->values) now->values = wanted->values;
    }
}

void Drawing::init_defaults() {
    header_vars_ = internal::new_drawing_r12_headers();
}

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

DxfVersion Drawing::version() const {
    return version_;
}

void Drawing::set_version(DxfVersion version) {
    version_ = version;
    // A drawing read from R13 … 2018 is written only as that version (B86): another one is a conversion the
    // save refuses (phase 8), so the drawing is not given another version's structure meanwhile — it would
    // still be there when the version is set back (B98)
    if (source_version_ != DxfVersion::R12 && version_ != source_version_) return;
    // R12 has no OBJECTS section: the standard materials and visual styles a 2007 drawing got go (step 9.7)
    if (version_ == DxfVersion::R12) internal::drop_standard_style_objects(*this);
    if (version_ >= DxfVersion::R2007 && source_version_ == DxfVersion::R12) ensure_r2007_objects();
    else if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    // R13 and R14 (phase 7): a drawing built in memory gets their structure — and loses 2000's
    // layouts when it had them; one read from a file is not touched (it is written as that version)
    else if (version_ >= DxfVersion::R13) {
        apply_older_header_defaults();
        ensure_older_structure();
    }
}

// ---------------------------------------------------------------------------
// Entity add methods
// ---------------------------------------------------------------------------

Entity& Drawing::add_line(Point3 start, Point3 end, const std::string& layer) {
    Entity e;
    e.type = EntityType::Line;
    e.props.layer = layer;
    e.data = LineData{start, end};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_circle(Point3 center, double radius, const std::string& layer) {
    Entity e;
    e.type = EntityType::Circle;
    e.props.layer = layer;
    e.data = CircleData{center, radius};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_arc(Point3 center, double radius, double start_angle,
                          double end_angle, const std::string& layer) {
    Entity e;
    e.type = EntityType::Arc;
    e.props.layer = layer;
    e.data = ArcData{center, radius, start_angle, end_angle};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_point(Point3 location, const std::string& layer) {
    Entity e;
    e.type = EntityType::Point;
    e.props.layer = layer;
    e.data = PointData{location};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_text(Point3 position, const std::string& content,
                           double height, const std::string& layer) {
    Entity e;
    e.type = EntityType::Text;
    e.props.layer = layer;
    TextData td;
    td.position = position;
    td.height = height;
    td.content = content;
    e.data = std::move(td);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_polyline(const std::vector<Point2>& vertices, bool closed,
                                const std::string& layer) {
    Entity e;
    e.type = EntityType::Polyline;
    e.props.layer = layer;
    PolylineData pd;
    pd.flags = closed ? 1 : 0;
    for (const auto& v : vertices) {
        Vertex vtx;
        vtx.position = Point3{v.x, v.y, 0.0};
        pd.vertices.push_back(vtx);
    }
    e.data = std::move(pd);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_lwpolyline(const std::vector<Point2>& vertices, bool closed,
                                const std::string& layer) {
    Entity e;
    e.type = EntityType::LwPolyline;
    e.props.layer = layer;
    LwPolylineData d;
    d.flags = closed ? 1 : 0;
    for (const auto& v : vertices) {
        LwVertex vtx;
        vtx.position = v;
        d.vertices.push_back(vtx);
    }
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_ellipse(Point3 center, Point3 major_axis, double ratio,
                             double start_param, double end_param, const std::string& layer) {
    Entity e;
    e.type = EntityType::Ellipse;
    e.props.layer = layer;
    e.data = EllipseData{center, major_axis, ratio, start_param, end_param};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_xline(Point3 base_point, Point3 direction, const std::string& layer) {
    Entity e;
    e.type = EntityType::Xline;
    e.props.layer = layer;
    e.data = XlineData{base_point, unit(direction)};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_ray(Point3 base_point, Point3 direction, const std::string& layer) {
    Entity e;
    e.type = EntityType::Ray;
    e.props.layer = layer;
    e.data = RayData{base_point, unit(direction)};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_mtext(Point3 insertion_point, const std::string& content, double height,
                           const std::string& layer) {
    Entity e;
    e.type = EntityType::MText;
    e.props.layer = layer;
    MTextData d;
    d.insertion_point = insertion_point;
    d.content = content;
    d.height = height;
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_tolerance(Point3 insertion_point, const std::string& content,
                               const std::string& layer) {
    Entity e;
    e.type = EntityType::Tolerance;
    e.props.layer = layer;
    ToleranceData d;
    d.insertion_point = insertion_point;
    d.content = content;
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_leader(const std::vector<Point3>& vertices, Entity* annotation,
                            const std::string& layer) {
    Entity e;
    e.type = EntityType::Leader;
    e.props.layer = layer;
    LeaderData d;
    d.vertices = vertices;
    // Before the push_back, which may move the entity the pointer points to
    if (annotation) {
        scan_handles();
        if (annotation->props.handle.empty()) annotation->props.handle = next_handle();
        d.annotation_handle = annotation->props.handle;
        switch (annotation->type) {
            case EntityType::MText:
                d.annotation_type = 0;
                d.text_height = annotation->as_mtext().height;
                d.text_width = annotation->as_mtext().reference_width;
                break;
            case EntityType::Tolerance: d.annotation_type = 1; break;
            case EntityType::Insert:    d.annotation_type = 2; break;
            default:                    d.annotation_type = 3; break;
        }
    }
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_spline(const std::vector<Point3>& control_points, int degree,
                            const std::string& layer) {
    Entity e;
    e.type = EntityType::Spline;
    e.props.layer = layer;
    SplineData d;
    d.degree = degree;
    d.control_points = control_points;
    // degree + 1 zeros, the interior knots evenly, degree + 1 ones (as AutoCAD writes it)
    int n = static_cast<int>(control_points.size());
    int spans = std::max(1, n - degree);
    for (int i = 0; i <= degree; ++i) d.knots.push_back(0.0);
    for (int i = 1; i < spans; ++i) d.knots.push_back(static_cast<double>(i) / spans);
    for (int i = 0; i <= degree; ++i) d.knots.push_back(1.0);
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_hatch(const std::vector<Point2>& boundary, const std::string& layer) {
    Entity e;
    e.type = EntityType::Hatch;
    e.props.layer = layer;
    HatchData d;
    HatchBoundaryPath path;
    path.flags = 3;                     // external, polyline
    for (const auto& p : boundary) path.vertices.push_back({p, 0.0});
    d.paths.push_back(std::move(path));
    e.data = std::move(d);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_hatch(const std::vector<Point2>& boundary, const HatchGradient& gradient,
                           const std::string& layer) {
    Entity& e = add_hatch(boundary, layer);
    e.as_hatch().gradient = gradient;
    return e;
}

Entity& Drawing::add_trace(Point3 p0, Point3 p1, Point3 p2, Point3 p3,
                             const std::string& layer) {
    Entity e;
    e.type = EntityType::Trace;
    e.props.layer = layer;
    e.data = TraceData{p0, p1, p2, p3};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_solid(Point3 p0, Point3 p1, Point3 p2, Point3 p3,
                             const std::string& layer) {
    Entity e;
    e.type = EntityType::Solid;
    e.props.layer = layer;
    e.data = SolidData{p0, p1, p2, p3};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_face3d(Point3 p0, Point3 p1, Point3 p2, Point3 p3,
                              const std::string& layer) {
    Entity e;
    e.type = EntityType::Face3D;
    e.props.layer = layer;
    e.data = Face3DData{p0, p1, p2, p3};
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_shape(Point3 insertion_point, const std::string& name,
                             double size, const std::string& layer) {
    Entity e;
    e.type = EntityType::Shape;
    e.props.layer = layer;
    ShapeData sd;
    sd.insertion_point = insertion_point;
    sd.name = name;
    sd.size = size;
    e.data = std::move(sd);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_insert(const std::string& block_name, Point3 insertion_point,
                              const std::string& layer) {
    Entity e;
    e.type = EntityType::Insert;
    e.props.layer = layer;
    InsertData id;
    id.block_name = block_name;
    id.insertion_point = insertion_point;
    e.data = std::move(id);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_dimension(DimensionType dim_type, Point3 def_point,
                                Point3 text_midpoint, const std::string& layer) {
    Entity e;
    e.type = EntityType::Dimension;
    e.props.layer = layer;
    DimensionData dd;
    dd.dim_type = dim_type;
    dd.definition_point = def_point;
    dd.text_midpoint = text_midpoint;
    e.data = std::move(dd);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_viewport(Point3 center, double width, double height,
                                const std::string& layer) {
    Entity e;
    e.type = EntityType::Viewport;
    e.props.layer = layer;
    ViewportData vd;
    vd.center = center;
    vd.width = width;
    vd.height = height;
    // The view (2000+): model space at the viewport's own place and size
    vd.view_center = {center.x, center.y};
    vd.view_height = height;
    e.data = std::move(vd);
    entities_.push_back(std::move(e));
    return entities_.back();
}

Entity& Drawing::add_entity(Entity entity) {
    entities_.push_back(std::move(entity));
    return entities_.back();
}

// ---------------------------------------------------------------------------
// Entity accessors
// ---------------------------------------------------------------------------

const std::vector<Entity>& Drawing::entities() const {
    return entities_;
}

std::vector<Entity>& Drawing::entities() {
    return entities_;
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

Layer& Drawing::add_layer(const std::string& name, Color color) {
    for (auto& l : layers_) {
        if (l.name == name) {
            l.color = color;
            return l;
        }
    }
    Layer l;
    l.name = name;
    l.color = color;
    layers_.push_back(std::move(l));
    return layers_.back();
}

const std::vector<Layer>& Drawing::layers() const {
    return layers_;
}

std::vector<Layer>& Drawing::layers() {
    return layers_;
}

bool Drawing::remove_layer(const std::string& name) {
    if (name == "0") return false;
    auto it = std::find_if(layers_.begin(), layers_.end(),
                           [&](const Layer& l) { return l.name == name; });
    if (it == layers_.end()) return false;
    layers_.erase(it);

    // Move entities on removed layer to "0" — a polyline's vertices and SEQEND have layers of
    // their own
    auto move_to_layer_0 = [&](Entity& e) {
        if (e.props.layer == name) e.props.layer = "0";
        if (e.type != EntityType::Polyline) return;
        PolylineData& polyline = e.as_polyline();
        for (auto& v : polyline.vertices) {
            if (v.layer == name) v.layer = "0";
        }
        if (polyline.seqend_layer == name) polyline.seqend_layer = "0";
    };
    for (auto& e : entities_) move_to_layer_0(e);
    for (auto& b : blocks_) {
        for (auto& e : b.entities) move_to_layer_0(e);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Blocks
// ---------------------------------------------------------------------------

Block& Drawing::add_block(const std::string& name, Point3 base_point) {
    if (Block* existing = find_block(name)) {
        existing->base_point = base_point;
        return *existing;
    }
    Block b;
    b.name = name;
    b.base_point = base_point;
    blocks_.push_back(std::move(b));
    block_index_.emplace(name, blocks_.size() - 1);
    return blocks_.back();
}

Block* Drawing::find_block(const std::string& name) {
    return const_cast<Block*>(static_cast<const Drawing*>(this)->find_block(name));
}

// Name → position index. A hit is checked against the block itself, because a name can
// change through the pointer find_block() returns; a stale entry or a block found only
// by the linear search rebuilds the index.
const Block* Drawing::find_block(const std::string& name) const {
    auto it = block_index_.find(name);
    if (it != block_index_.end() && it->second < blocks_.size() &&
        blocks_[it->second].name == name) {
        return &blocks_[it->second];
    }
    for (const auto& b : blocks_) {
        if (b.name == name) {
            rebuild_block_index();
            return &b;
        }
    }
    if (it != block_index_.end()) rebuild_block_index();
    return nullptr;
}

void Drawing::rebuild_block_index() const {
    block_index_.clear();
    block_index_.reserve(blocks_.size());
    for (size_t i = 0; i < blocks_.size(); ++i) {
        block_index_.emplace(blocks_[i].name, i);   // the first block of a name wins
    }
}

const std::vector<Block>& Drawing::blocks() const {
    return blocks_;
}

std::vector<Block>& Drawing::blocks() {
    return blocks_;
}

// ---------------------------------------------------------------------------
// Linetypes
// ---------------------------------------------------------------------------

Linetype& Drawing::add_linetype(const std::string& name, const std::string& description,
                                 const std::vector<double>& pattern) {
    for (auto& lt : linetypes_) {
        if (lt.name == name) {
            lt.description = description;
            lt.pattern = pattern;
            lt.total_length = 0.0;
            for (double d : pattern) lt.total_length += std::abs(d);
            return lt;
        }
    }
    Linetype lt;
    lt.name = name;
    lt.description = description;
    lt.pattern = pattern;
    lt.total_length = 0.0;
    for (double d : pattern) lt.total_length += std::abs(d);
    linetypes_.push_back(std::move(lt));
    return linetypes_.back();
}

const std::vector<Linetype>& Drawing::linetypes() const {
    return linetypes_;
}

std::vector<Linetype>& Drawing::linetypes() {
    return linetypes_;
}

// ---------------------------------------------------------------------------
// Text styles
// ---------------------------------------------------------------------------

TextStyle& Drawing::add_text_style(const std::string& name, const std::string& font_file) {
    for (auto& ts : text_styles_) {
        if (ts.name == name) {
            ts.font_file = font_file;
            return ts;
        }
    }
    TextStyle ts;
    ts.name = name;
    ts.font_file = font_file;
    text_styles_.push_back(std::move(ts));
    return text_styles_.back();
}

const std::vector<TextStyle>& Drawing::text_styles() const {
    return text_styles_;
}

std::vector<TextStyle>& Drawing::text_styles() {
    return text_styles_;
}

// ---------------------------------------------------------------------------
// Dimension styles
// ---------------------------------------------------------------------------

DimStyle& Drawing::add_dim_style(const std::string& name) {
    for (auto& ds : dim_styles_) {
        if (ds.name == name) return ds;
    }
    DimStyle ds;
    ds.name = name;
    dim_styles_.push_back(std::move(ds));
    return dim_styles_.back();
}

const std::vector<DimStyle>& Drawing::dim_styles() const {
    return dim_styles_;
}

std::vector<DimStyle>& Drawing::dim_styles() {
    return dim_styles_;
}

// ---------------------------------------------------------------------------
// Views
// ---------------------------------------------------------------------------

const std::vector<View>& Drawing::views() const {
    return views_;
}

std::vector<View>& Drawing::views() {
    return views_;
}

// ---------------------------------------------------------------------------
// UCS
// ---------------------------------------------------------------------------

const std::vector<Ucs>& Drawing::ucs_list() const {
    return ucs_list_;
}

std::vector<Ucs>& Drawing::ucs_list() {
    return ucs_list_;
}

// ---------------------------------------------------------------------------
// Vports
// ---------------------------------------------------------------------------

const std::vector<Vport>& Drawing::vports() const {
    return vports_;
}

std::vector<Vport>& Drawing::vports() {
    return vports_;
}

// ---------------------------------------------------------------------------
// App IDs
// ---------------------------------------------------------------------------

AppId& Drawing::add_app_id(const std::string& name) {
    for (auto& a : app_ids_) {
        if (a.name == name) return a;
    }
    AppId a;
    a.name = name;
    app_ids_.push_back(std::move(a));
    return app_ids_.back();
}

const std::vector<AppId>& Drawing::app_ids() const {
    return app_ids_;
}

std::vector<AppId>& Drawing::app_ids() {
    return app_ids_;
}

// ---------------------------------------------------------------------------
// Header variables
// ---------------------------------------------------------------------------

const std::vector<HeaderVar>& Drawing::header_vars() const {
    return header_vars_;
}

HeaderVar* Drawing::find_header(const std::string& name) {
    for (auto& h : header_vars_) {
        if (h.name == name) return &h;
    }
    return nullptr;
}

const HeaderVar* Drawing::find_header(const std::string& name) const {
    for (const auto& h : header_vars_) {
        if (h.name == name) return &h;
    }
    return nullptr;
}

void Drawing::set_header_string(const std::string& name, int code, const std::string& val) {
    HeaderVar* h = find_header(name);
    if (h) {
        h->values = {{code, val}};
    } else {
        header_vars_.push_back({name, {{code, val}}});
    }
}

void Drawing::set_header_int(const std::string& name, int code, int val) {
    std::string s = std::to_string(val);
    HeaderVar* h = find_header(name);
    if (h) {
        h->values = {{code, s}};
    } else {
        header_vars_.push_back({name, {{code, s}}});
    }
}

void Drawing::set_header_double(const std::string& name, int code, double val) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", val);
    std::string s(buf);
    HeaderVar* h = find_header(name);
    if (h) {
        h->values = {{code, s}};
    } else {
        header_vars_.push_back({name, {{code, s}}});
    }
}

void Drawing::set_header_point(const std::string& name, const Point3& p) {
    char bx[64], by[64], bz[64];
    std::snprintf(bx, sizeof(bx), "%.17g", p.x);
    std::snprintf(by, sizeof(by), "%.17g", p.y);
    std::snprintf(bz, sizeof(bz), "%.17g", p.z);
    HeaderVar* h = find_header(name);
    if (h) {
        h->values = {{10, std::string(bx)}, {20, std::string(by)}, {30, std::string(bz)}};
    } else {
        header_vars_.push_back({name, {{10, std::string(bx)}, {20, std::string(by)}, {30, std::string(bz)}}});
    }
}

// ---------------------------------------------------------------------------
// The size of entities (rättningsfas R.8)
// ---------------------------------------------------------------------------

bool Drawing::entity_extents(const Entity& e, Point3& min, Point3& max) const {
    internal::Extents box;
    internal::entity_extents(*this, e, box);
    if (box.empty()) return false;
    min = box.min;
    max = box.max;
    return true;
}

bool Drawing::mtext_size(const Entity& e, double& width, double& height) const {
    if (e.type != EntityType::MText) return false;
    internal::TextLayout layout(*this);
    internal::MTextSize size = layout.mtext(e.as_mtext());
    if (!size.font) return false;
    width = size.width;
    height = size.height;
    return true;
}

// ---------------------------------------------------------------------------
// Handle generation
// ---------------------------------------------------------------------------

std::string Drawing::next_handle() {
    return internal::format_handle(next_handle_++);
}

// Handles are 64-bit (up to 16 hex digits). Every handle already in use counts —
// including ATTRIB and the records preserved from DXF 2000+ files (OBJECTS,
// BLOCK_RECORD, VERTEX …) — so a new handle never collides with one of them.
bool Drawing::scan_handles() {
    std::string bad_handle;
    auto update_max = [&](const std::string& h) {
        uint64_t value = 0;
        switch (internal::parse_handle(h, value)) {
            case internal::HandleParse::Ok:
                if (value >= next_handle_) {
                    if (value == UINT64_MAX) bad_handle = h;   // nothing left above it
                    else next_handle_ = value + 1;
                }
                break;
            case internal::HandleParse::TooLong:
                bad_handle = h;
                break;
            case internal::HandleParse::Empty:
            case internal::HandleParse::NotHex:
                break;
        }
    };
    auto update_max_raw = [&](const RawRecord& raw) {
        if (raw.empty()) return;
        for (const auto& g : raw.groups()) {
            if (g.code == 5 || g.code == 105) update_max(g.value);
        }
    };
    auto update_max_entity = [&](const Entity& e) {
        update_max(e.props.handle);
        update_max_raw(e.raw);
        if (e.type == EntityType::Polyline) {
            for (const auto& v : e.as_polyline().vertices) {
                update_max(v.handle);
                update_max_raw(v.raw);
            }
            update_max(e.as_polyline().seqend_handle);
            update_max_raw(e.as_polyline().seqend_raw);
        } else if (e.type == EntityType::Insert) {
            for (const auto& a : e.as_insert().attributes) {
                update_max(a.props.handle);
                update_max_raw(a.raw);
            }
            update_max(e.as_insert().seqend_handle);
            update_max_raw(e.as_insert().seqend_raw);
        }
    };

    for (auto& l : layers_) { update_max(l.handle); update_max_raw(l.raw); }
    for (auto& lt : linetypes_) { update_max(lt.handle); update_max_raw(lt.raw); }
    for (auto& s : text_styles_) { update_max(s.handle); update_max_raw(s.raw); }
    // (R12's DIMSTYLE has no handle: its group 5 is DIMBLK, a block name)
    for (auto& d : dim_styles_) {
        update_max(d.handle);
        if (source_version_ != DxfVersion::R12) update_max_raw(d.raw);
    }
    for (auto& v : views_) { update_max(v.handle); update_max_raw(v.raw); }
    for (auto& u : ucs_list_) { update_max(u.handle); update_max_raw(u.raw); }
    for (auto& vp : vports_) { update_max(vp.handle); update_max_raw(vp.raw); }
    for (auto& a : app_ids_) { update_max(a.handle); update_max_raw(a.raw); }
    for (auto& b : blocks_) {
        update_max(b.handle);
        update_max(b.endblk_handle);
        update_max(b.record.handle);
        update_max_raw(b.raw);
        update_max_raw(b.endblk_raw);
        update_max_raw(b.record.raw);
        for (auto& e : b.entities) update_max_entity(e);
    }
    for (auto& e : entities_) update_max_entity(e);
    for (auto& o : objects_) { update_max(o.handle); update_max_raw(o.raw); }
    for (auto& t : raw_tables_) {
        update_max_raw(t.header);
        for (auto& r : t.entries) update_max_raw(r);
    }
    for (auto& sec : raw_sections_) {
        if (sec.name == "HEADER") continue;   // $HANDSEED is not a handle in use
        update_max_raw(sec.head);
        for (auto& r : sec.records) update_max_raw(r);
    }

    // $HANDSEED as read: AutoCAD keeps it above the handles it has handed out, also those of
    // records since removed, so it can be above every handle in the file
    if (const HeaderVar* seed = find_header("$HANDSEED")) {
        uint64_t value = 0;
        if (!seed->values.empty() &&
            internal::parse_handle(seed->values[0].second, value) == internal::HandleParse::Ok &&
            value > next_handle_ && value != UINT64_MAX) {
            next_handle_ = value;
        }
    }

    if (!bad_handle.empty()) {
        last_error_ = "Handle out of range: \"" + bad_handle +
                      "\" (handles have at most 16 hex digits, and one must be free above it)";
        return false;
    }
    return true;
}

bool Drawing::assign_handles(bool r2000, std::vector<std::string*>& assigned) {
    if (!scan_handles()) return false;

    // Every record that gets a handle, in the order the handles are given. Counted
    // first, so running out of handles changes nothing.
    std::vector<std::string*> missing;
    // A record of an R12 file written back as R12 stays as the file has it: one without a handle
    // gets none
    bool keep_file = loaded_r12_ && source_version_ == DxfVersion::R12 && version_ == DxfVersion::R12;
    auto want = [&](std::string& h, const RawRecord& raw) {
        if (h.empty() && !(keep_file && !raw.empty())) missing.push_back(&h);
    };
    auto want_entity = [&](Entity& e) {
        want(e.props.handle, e.raw);
        if (e.type == EntityType::Polyline) {
            for (auto& v : e.as_polyline().vertices) want(v.handle, v.raw);
            want(e.as_polyline().seqend_handle, e.as_polyline().seqend_raw);
        } else if (e.type == EntityType::Insert && !e.as_insert().attributes.empty()) {
            for (auto& a : e.as_insert().attributes) want(a.props.handle, a.raw);
            want(e.as_insert().seqend_handle, e.as_insert().seqend_raw);
        }
    };

    // R12 as AutoCAD writes it: handles on entities, ATTRIB, VERTEX, SEQEND and ENDBLK,
    // none on table entries or BLOCK (DIMSTYLE group 5 is DIMBLK there)
    if (r2000) {
        for (auto& l : layers_) want(l.handle, l.raw);
        for (auto& lt : linetypes_) want(lt.handle, lt.raw);
        for (auto& s : text_styles_) want(s.handle, s.raw);
        for (auto& d : dim_styles_) want(d.handle, d.raw);
        for (auto& v : views_) want(v.handle, v.raw);
        for (auto& u : ucs_list_) want(u.handle, u.raw);
        for (auto& vp : vports_) want(vp.handle, vp.raw);
        for (auto& a : app_ids_) want(a.handle, a.raw);
    }
    for (auto& b : blocks_) {
        if (r2000) {
            want(b.record.handle, b.record.raw);
            want(b.handle, b.raw);
        }
        want(b.endblk_handle, b.endblk_raw);
        for (auto& e : b.entities) want_entity(e);
    }
    for (auto& e : entities_) want_entity(e);
    if (r2000) for (auto& o : objects_) want(o.handle, o.raw);

    if (missing.size() > UINT64_MAX - next_handle_) {
        last_error_ = "Out of handles: the drawing uses handles up to FFFFFFFFFFFFFFFF";
        return false;
    }
    for (std::string* h : missing) {
        *h = next_handle();
        assigned.push_back(h);
    }

    // $HANDSEED is the next free handle, so it must be above every handle in use
    set_header_string("$HANDSEED", 5, internal::format_handle(next_handle_));
    return true;
}

// ---------------------------------------------------------------------------
// Save / Load
// ---------------------------------------------------------------------------

bool Drawing::save(DxfFormat format) {
    last_error_.clear();
    warnings_.clear();
    if (filename_.empty()) {
        last_error_ = "No filename set";
        return false;
    }
    return write_file(filename_, format);
}

bool Drawing::save_as(const std::filesystem::path& path, DxfFormat format) {
    last_error_.clear();
    warnings_.clear();
    if (!write_file(path, format)) return false;
    filename_ = path;
    return true;
}

// The whole file is written to memory first, so nothing on disk is touched when
// writing fails (for example on a string that is not UTF-8)
// An R13 or R14 file is of family A2000 (AutoCAD 2002 … 2005 saving as R13/R14) when a table head or
// a BLOCK record has a 330 owner outside a {…} group, family N (AutoCAD R13 and R14) otherwise
static bool has_owner_group(const RawRecord& record) {
    bool in_list = false;
    for (const DxfGroup& g : record.groups()) {
        if (g.code == 102) in_list = !g.value.empty() && g.value[0] == '{';
        else if (g.code == 330 && !in_list) return true;
    }
    return false;
}

void Drawing::note_loaded(bool ok, DxfVersion version) {
    owner_groups_ = false;
    if (ok && (version == DxfVersion::R13 || version == DxfVersion::R14)) {
        for (const auto& t : raw_tables_) owner_groups_ = owner_groups_ || has_owner_group(t.header);
        for (const auto& b : blocks_) owner_groups_ = owner_groups_ || has_owner_group(b.raw);
    }
    loaded_r12_ = ok && version == DxfVersion::R12;
    loaded_table_sizes_.clear();
    if (!loaded_r12_) return;
    loaded_table_sizes_ = {{"VPORT", vports_.size()},       {"LTYPE", linetypes_.size()},
                           {"LAYER", layers_.size()},       {"STYLE", text_styles_.size()},
                           {"VIEW", views_.size()},         {"UCS", ucs_list_.size()},
                           {"APPID", app_ids_.size()},      {"DIMSTYLE", dim_styles_.size()}};
}

static std::string version_name(DxfVersion version) {
    switch (version) {
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
    return "?";
}

bool Drawing::write_dxf(DxfFormat format, std::string& out) {
    // Plan B11/B20/B30/B42/B56/B69/B86: R12, R13, R14, 2000, 2004, 2007, 2010, 2013 and 2018 can be
    // written; a drawing read from R13 … 2018 only as that version (converting is phase 8). Checked
    // before anything is changed or any file is opened.
    const bool older = version_ == DxfVersion::R13 || version_ == DxfVersion::R14;   // no layouts, one paper space
    bool from_file = source_version_ == version_ && version_ >= DxfVersion::R13;
    // A drawing read from an R12 file and written as R12 is written from its preserved records too
    bool r12_from_file = loaded_r12_ && source_version_ == DxfVersion::R12 && version_ == DxfVersion::R12;
    if (source_version_ != DxfVersion::R12 && !from_file) {
        last_error_ = "Writing DXF " + version_name(source_version_) + " as " +
                      version_name(version_) +
                      " is not supported yet (the drawing was read from that version)";
        return false;
    }

    // The drawing as it was read: its preserved records read again, to compare with
    std::unique_ptr<Drawing> original;
    if (from_file || r12_from_file) {
        std::istringstream in(internal::template_text(*this, source_binary_), std::ios::binary);
        original = std::make_unique<Drawing>();
        original->header_vars_.clear();
        internal::DxfReader reader(in);
        if (!reader.read(*original)) {
            last_error_ = "Could not read the drawing's preserved records: " + reader.error();
            return false;
        }
        if (from_file) {
            original->read_xclipframe_variable(version_);
            original->read_roundtrip_records(version_);
        }
    }

    // Everything the version cannot hold, in one list and one order, before anything is changed, given
    // a handle or opened (plan B98): a failed save leaves the drawing, its handles, its file name and the
    // destination as they were
    std::string version_fault = target_version_error(*this, from_file ? &loaded_handles_ : nullptr, original.get(), format);
    if (!version_fault.empty()) {
        last_error_ = version_fault;
        return false;
    }

    bool r2000 = version_ >= DxfVersion::R2000;   // 2000 … 2018 (B20, B30, B42, B56)
    if (version_ >= DxfVersion::R2007 && !from_file) ensure_r2007_objects();
    else if (r2000) ensure_r2000_structure();
    else if (older) ensure_older_structure();
    // A drawing built in memory: its visual styles in this version's form (B45, B59), the
    // round-trip XRECORDs of 2013's values before 2013 (B60), $XCLIPFRAME's
    // variable as AutoCAD has it for the value (B43) — in a drawing read from 2000, 2004 or
    // 2007 when it no longer says the value (the variable as read otherwise)
    if (r2000 && !from_file) {
        internal::match_visual_styles(*this, version_, [this] { return next_handle(); });
        sync_roundtrip_records();
        sync_xclipframe_variable(false);
    }
    if (from_file && version_ < DxfVersion::R2010) sync_xclipframe_variable(true);
    // Read from 2000 … 2010: the round-trip XRECORDs say the typed values (B60)
    if (from_file) sync_roundtrip_records();

    // MTEXT's other columns and its frame as entities before 2018 (B74)
    if (r2000) {
        sync_mtext_columns(original.get());
        sync_multiline_attributes(original.get());
        sync_view_style_records(original.get());
        sync_table_records(original.get());
    }

    // A failed save takes back the handles it gave, so the drawing is as it was
    uint64_t saved_next = next_handle_;
    const HeaderVar* seed = find_header("$HANDSEED");
    std::optional<HeaderVar> saved_seed;
    if (seed) saved_seed = *seed;
    std::vector<std::string*> assigned;
    auto take_back = [&]() {
        for (std::string* h : assigned) h->clear();
        next_handle_ = saved_next;
        if (saved_seed) {
            *find_header("$HANDSEED") = *saved_seed;
        } else {
            header_vars_.erase(std::remove_if(header_vars_.begin(), header_vars_.end(),
                                              [](const HeaderVar& h) { return h.name == "$HANDSEED"; }),
                               header_vars_.end());
        }
    };

    // Plan B21: the DBCOLORs of the color book colors in use, before handles are given out
    std::vector<Object> objects_before;
    bool colors_added = false;
    if (version_ >= DxfVersion::R2004) {
        std::string error = ensure_color_objects(objects_before, colors_added);
        if (!error.empty()) {
            last_error_ = error;
            take_back();
            if (colors_added) objects_ = std::move(objects_before);
            return false;
        }
    }
    // The handles first: some of them are in the objects that are taken back
    auto take_back_all = [&]() {
        take_back();
        if (colors_added) objects_ = std::move(objects_before);
    };

    if (!assign_handles(version_ >= DxfVersion::R13, assigned)) {
        take_back_all();
        return false;
    }
    std::ostringstream oss(std::ios::out | std::ios::binary);
    internal::DxfWriter writer(oss, version_, format);
    writer.set_next_handle(next_handle_);
    writer.set_plain_ascii(source_form_ && source_form_->plain);
    writer.set_handles_given(assigned.size());
    if (from_file && source_form_) writer.set_marker_columns(&source_form_->marker_columns);
    if (original && r12_from_file) {
        writer.set_template(original.get(), source_binary_, nullptr, nullptr);
        writer.set_loaded_table_sizes(&loaded_table_sizes_);
    } else if (original) {
        writer.set_template(original.get(), source_binary_, &loaded_handles_, &loaded_block_entities_);
        writer.set_loaded_names(&loaded_names_);
        writer.set_loaded_hard_owned(&loaded_hard_owned_);
    }
    if (!writer.write(*this)) {
        last_error_ = writer.error().empty() ? "Failed to write DXF data" : writer.error();
        take_back_all();
        return false;
    }
    for (const auto& w : writer.warnings()) warnings_.push_back(w);
    out = oss.str();
    // ASCII DXF has the line ending of the file read, CRLF as AutoCAD writes it otherwise
    if (format == DxfFormat::Ascii && source_crlf_) {
        std::string crlf;
        crlf.reserve(out.size() + out.size() / 16);
        for (char c : out) {
            if (c == '\n') crlf += '\r';
            crlf += c;
        }
        out = std::move(crlf);
    }
    // What followed "EOF" in the ASCII file read, as it was (a missing final line ending, a blank line)
    if (format == DxfFormat::Ascii && source_form_ && source_form_->after_eof && (from_file || r12_from_file)) {
        const std::string ending = source_crlf_ ? "\r\n" : "\n";
        if (out.size() >= 3 + ending.size() &&
            out.compare(out.size() - ending.size() - 3, 3, "EOF") == 0 &&
            out.compare(out.size() - ending.size(), ending.size(), ending) == 0) {
            out.resize(out.size() - ending.size());
            out += *source_form_->after_eof;
        }
    }
    return true;
}

bool Drawing::write_file(const std::filesystem::path& path, DxfFormat format) {
    std::string data;
    if (!write_dxf(format, data)) return false;

    // The data has its line endings already: written as it is
    std::ofstream file(path, std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        last_error_ = "Could not open file for writing: " + utf8_from_path(path);
        return false;
    }
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    // Close explicitly — a flush error (full disk, dropped network share) is
    // only visible once the buffer is written out.
    file.close();
    if (!file.good()) {
        last_error_ = "Failed to write file: " + utf8_from_path(path);
        return false;
    }
    return true;
}

// True when the first line of an ASCII DXF ends with CRLF (a stream at its start; left there)
static bool first_line_is_crlf(std::istream& in) {
    char c;
    char previous = 0;
    bool crlf = true;
    while (in.get(c)) {
        if (c == '\n') {
            crlf = previous == '\r';
            break;
        }
        previous = c;
    }
    in.clear();
    in.seekg(0);
    return crlf;
}

// The text form of an ASCII file, for writing it back as it was (a stream at its start; left there):
// whether its first code line has no padding, and what follows "EOF" at the end
struct AsciiLayout {
    bool plain = false;
    std::optional<std::string> after_eof;
};

static AsciiLayout ascii_layout(std::istream& in) {
    AsciiLayout layout;
    std::string first;
    std::getline(in, first);
    if (!first.empty() && first.back() == '\r') first.pop_back();
    layout.plain = first == "0";
    in.clear();
    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    if (size > 0) {
        std::streamoff n = std::min<std::streamoff>(size, 64);
        std::string tail(static_cast<size_t>(n), '\0');
        in.seekg(size - n);
        in.read(tail.data(), n);
        size_t eof = tail.rfind("EOF");
        if (eof != std::string::npos) layout.after_eof = tail.substr(eof + 3);
    }
    in.clear();
    in.seekg(0);
    return layout;
}

// The text form of the ASCII file just read: what the reader found (the keywords' widths) with what the layout says
static void note_source_form(std::shared_ptr<const internal::AsciiForm>& form, bool binary, const AsciiLayout& layout) {
    if (binary) {
        form.reset();
        return;
    }
    auto now = std::make_shared<internal::AsciiForm>();
    if (form) now->marker_columns = form->marker_columns;
    now->plain = layout.plain;
    now->after_eof = layout.after_eof;
    form = std::move(now);
}

bool Drawing::load(const std::filesystem::path& path) {
    last_error_.clear();
    warnings_.clear();
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        last_error_ = "Could not open file: " + utf8_from_path(path);
        return false;
    }
    // Clear all data
    entities_.clear();
    layers_.clear();
    blocks_.clear();
    linetypes_.clear();
    text_styles_.clear();
    dim_styles_.clear();
    views_.clear();
    ucs_list_.clear();
    vports_.clear();
    app_ids_.clear();
    header_vars_.clear();
    objects_.clear();
    raw_tables_.clear();
    raw_sections_.clear();
    thumbnail_.clear();
    thumbnail_raw_ = RawRecord();
    acds_head_ = RawRecord();
    acds_schemas_.clear();
    acds_records_.clear();
    loaded_r12_ = false;
    loaded_table_sizes_.clear();

    source_crlf_ = first_line_is_crlf(file);
    AsciiLayout layout = ascii_layout(file);
    source_form_.reset();
    internal::DxfReader reader(file);
    bool ok = reader.read(*this);
    rebuild_block_index();
    version_ = reader.detected_version();
    source_version_ = version_;
    source_binary_ = reader.binary();
    note_source_form(source_form_, source_binary_, layout);
    read_xclipframe_variable(version_);
    read_roundtrip_records(version_);
    loaded_handles_ = internal::preserved_handles(*this);
    loaded_block_entities_ = internal::preserved_block_entities(*this);
    loaded_hard_owned_ = internal::preserved_hard_owned(*this);
    loaded_names_ = record_names(*this);
    internal::stamp_loaded_tables(*this);        // step 9.5: the tables as they were read
    note_loaded(ok, version_);
    if (!ok) {
        last_error_ = reader.error();
    } else {
        filename_ = path;
    }
    return ok;
}

bool Drawing::load_from_memory(const uint8_t* data, size_t size) {
    last_error_.clear();
    warnings_.clear();
    std::string str(reinterpret_cast<const char*>(data), size);
    std::istringstream iss(str, std::ios::binary);

    entities_.clear();
    layers_.clear();
    blocks_.clear();
    linetypes_.clear();
    text_styles_.clear();
    dim_styles_.clear();
    views_.clear();
    ucs_list_.clear();
    vports_.clear();
    app_ids_.clear();
    header_vars_.clear();
    objects_.clear();
    raw_tables_.clear();
    raw_sections_.clear();
    thumbnail_.clear();
    thumbnail_raw_ = RawRecord();
    acds_head_ = RawRecord();
    acds_schemas_.clear();
    acds_records_.clear();
    loaded_r12_ = false;
    loaded_table_sizes_.clear();

    source_crlf_ = first_line_is_crlf(iss);
    AsciiLayout layout = ascii_layout(iss);
    source_form_.reset();
    internal::DxfReader reader(iss);
    bool ok = reader.read(*this);
    rebuild_block_index();
    version_ = reader.detected_version();
    source_version_ = version_;
    source_binary_ = reader.binary();
    note_source_form(source_form_, source_binary_, layout);
    read_xclipframe_variable(version_);
    read_roundtrip_records(version_);
    loaded_handles_ = internal::preserved_handles(*this);
    loaded_block_entities_ = internal::preserved_block_entities(*this);
    loaded_hard_owned_ = internal::preserved_hard_owned(*this);
    loaded_names_ = record_names(*this);
    internal::stamp_loaded_tables(*this);        // step 9.5: the tables as they were read
    note_loaded(ok, version_);
    if (!ok) last_error_ = reader.error();
    return ok;
}

std::vector<uint8_t> Drawing::save_to_memory(DxfFormat format) {
    last_error_.clear();
    warnings_.clear();
    std::string s;
    if (!write_dxf(format, s)) return {};
    return std::vector<uint8_t>(s.begin(), s.end());
}

const std::string& Drawing::last_error() const {
    return last_error_;
}

const std::vector<uint8_t>& Drawing::thumbnail() const {
    return thumbnail_;
}

void Drawing::set_thumbnail(std::vector<uint8_t> bytes) {
    thumbnail_ = std::move(bytes);
}

const RawRecord& Drawing::thumbnail_raw() const {
    return thumbnail_raw_;
}

const std::vector<std::string>& Drawing::warnings() const {
    return warnings_;
}

// ---------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------

const std::vector<Object>& Drawing::objects() const {
    return objects_;
}

std::vector<Object>& Drawing::objects() {
    return objects_;
}

// Handles are compared as numbers, so "1A" and "01a" are the same object
static bool same_handle(const std::string& a, const std::string& b) {
    uint64_t va = 0;
    uint64_t vb = 0;
    if (internal::parse_handle(a, va) != internal::HandleParse::Ok) return false;
    if (internal::parse_handle(b, vb) != internal::HandleParse::Ok) return false;
    return va == vb;
}

Object* Drawing::find_object(const std::string& handle) {
    for (auto& o : objects_) {
        if (same_handle(o.handle, handle)) return &o;
    }
    return nullptr;
}

const Object* Drawing::find_object(const std::string& handle) const {
    for (const auto& o : objects_) {
        if (same_handle(o.handle, handle)) return &o;
    }
    return nullptr;
}

std::vector<Object*> Drawing::layouts() {
    std::vector<Object*> out;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Layout) out.push_back(&o);
    }
    return out;
}

std::vector<const Object*> Drawing::layouts() const {
    std::vector<const Object*> out;
    for (const auto& o : objects_) {
        if (o.type == ObjectType::Layout) out.push_back(&o);
    }
    return out;
}

const std::vector<RawTable>& Drawing::raw_tables() const {
    return raw_tables_;
}

const std::vector<RawSection>& Drawing::raw_sections() const {
    return raw_sections_;
}

} // namespace hdxf
