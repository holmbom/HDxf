#include "StyleTypes.hpp"
#include <cmath>

// MATERIAL (phase 9, step 9.7): reading the record and registering its codec. The groups are AutoCAD's own where its files
// show them (doc/facit/acad_*_dxf_native_records*.dxf, ACadSharp's samples, jscad's empty2007 …) and Autodesk's DXF
// reference (the MATERIAL group codes) where they do not. AutoCAD writes a material as one run of groups in a fixed order
// in which every group but the name is there only when the material has it; the reader takes them in that order and
// every optional group of MaterialData is present or not as the record has it.

namespace hdxf {
namespace internal {

namespace {

struct MapCodes {
    int blend, source, file, projection, tiling, auto_transform, transform;
};

constexpr MapCodes kDiffuse{42, 72, 3, 73, 74, 75, 43};
constexpr MapCodes kSpecular{46, 77, 4, 78, 79, 170, 47};
constexpr MapCodes kReflection{48, 171, 6, 172, 173, 174, 49};
constexpr MapCodes kOpacity{141, 175, 7, 176, 177, 178, 142};
constexpr MapCodes kBump{143, 179, 8, 270, 271, 272, 144};
constexpr MapCodes kRefraction{146, 273, 9, 274, 275, 276, 147};
constexpr MapCodes kNormal = kDiffuse;      // 2007's normal map has the diffuse map's codes

// One channel of a map: the seven values in order, each when it is there
bool read_map(GroupCursor& c, const MapCodes& k, MaterialMap& m) {
    double d = 0.0;
    int16_t i = 0;
    std::string s;
    if (c.real(k.blend, d)) m.blend = d;
    if (c.integer(k.source, i)) m.source = i;
    if (c.string(k.file, s)) m.file_name = s;
    if (c.integer(k.projection, i)) m.projection = i;
    if (c.integer(k.tiling, i)) m.tiling = i;
    if (c.integer(k.auto_transform, i)) m.auto_transform = i;
    if (c.ok() && c.code() == k.transform) {
        std::array<double, 16> t{};
        for (size_t n = 0; n < t.size(); ++n) {
            if (!c.real(k.transform, t[n])) {
                if (c.ok()) c.fail("the mapper transform (" + std::to_string(k.transform) + ") has " + std::to_string(n) + " of its 16 values");
                return false;
            }
        }
        m.transform = t;
    }
    return c.ok();
}

void read_color(GroupCursor& c, int method_code, int factor_code, int color_code, MaterialColor& m) {
    int16_t method = 0;
    double factor = 0.0;
    int32_t color = 0;
    if (c.integer(method_code, method)) m.method = method;
    if (c.real(factor_code, factor)) m.factor = factor;
    if (c.integer(color_code, color)) m.color = color;
}

template <typename T>
void read_opt_real(GroupCursor& c, int code, std::optional<T>& out) {
    double v = 0.0;
    if (c.real(code, v)) out = v;
}

template <typename T>
void read_opt_int(GroupCursor& c, int code, std::optional<T>& out) {
    T v{};
    if (c.integer(code, v)) out = v;
}

// A procedural material's table: 300, then the values the reference lists (291, 271, 469, 301, 62 with 420 and 430) and 292
bool read_procedure(GroupCursor& c, MaterialProcedure& p) {
    c.string(300, p.name);
    for (;;) {
        MaterialProcedureValue v;
        bool got = true;
        switch (c.code()) {
            case 291:
                v.kind = MaterialProcedureValue::Kind::Boolean;
                c.boolean(291, v.boolean);
                break;
            case 271:
                v.kind = MaterialProcedureValue::Kind::Integer;
                c.integer(271, v.integer);
                break;
            case 469:
                v.kind = MaterialProcedureValue::Kind::Real;
                c.real(469, v.real);
                break;
            case 301:
                v.kind = MaterialProcedureValue::Kind::Text;
                c.string(301, v.text);
                break;
            case 62: {
                v.kind = MaterialProcedureValue::Kind::Color;
                int aci = 0, rgb = 0;
                c.integer(62, aci);
                v.color.aci = aci;
                if (c.integer(420, rgb)) v.color.rgb = rgb;
                c.string(430, v.color.name);
                break;
            }
            default:
                got = false;
                break;
        }
        if (!got || !c.ok()) break;
        p.values.push_back(std::move(v));
    }
    bool end = false;
    if (c.boolean(292, end)) p.table_end = end;
    return c.ok();
}

} // namespace

bool read_material_subclass(const RecordView& view, TypedReadContext& ctx, MaterialData& d, const std::string& what) {
    GroupCursor c(view, "AcDbMaterial");
    if (!c.valid()) return ctx.fail(what + " has no AcDbMaterial subclass");
    d = MaterialData();
    c.require_string(1, d.name);
    std::string text;
    if (c.string(2, text)) d.description = text;
    read_color(c, 70, 40, 90, d.ambient);
    read_color(c, 71, 41, 91, d.diffuse);
    read_map(c, kDiffuse, d.diffuse_map);
    read_opt_real(c, 44, d.gloss);
    read_color(c, 76, 45, 92, d.specular);
    read_map(c, kSpecular, d.specular_map);
    read_map(c, kReflection, d.reflection_map);
    read_opt_real(c, 140, d.opacity);
    read_map(c, kOpacity, d.opacity_map);
    read_map(c, kBump, d.bump_map);
    read_opt_real(c, 145, d.refraction_index);
    read_map(c, kRefraction, d.refraction_map);
    // 2007 and later (the reference's order)
    read_opt_real(c, 460, d.color_bleed_scale);
    read_opt_real(c, 461, d.indirect_dump_scale);
    read_opt_real(c, 462, d.reflectance_scale);
    read_opt_real(c, 463, d.transmittance_scale);
    {
        bool v = false;
        if (c.boolean(290, v)) d.two_sided = v;
    }
    read_opt_real(c, 464, d.luminance);
    read_opt_int(c, 270, d.luminance_mode);
    read_opt_int(c, 271, d.normal_map_method);
    read_opt_real(c, 465, d.normal_map_strength);
    read_map(c, kNormal, d.normal_map);
    {
        bool v = false;
        if (c.boolean(293, v)) d.anonymous = v;
    }
    read_opt_int(c, 272, d.global_illumination_mode);
    read_opt_int(c, 273, d.final_gather_mode);
    while (c.ok() && c.code() == 300) {
        MaterialProcedure p;
        if (!read_procedure(c, p)) break;
        d.procedures.push_back(std::move(p));
    }
    read_opt_int(c, 270, d.map_u_tile);
    read_opt_real(c, 148, d.translucence);
    read_opt_int(c, 90, d.self_illumination);
    read_opt_real(c, 468, d.reflectivity);
    read_opt_int(c, 93, d.illumination_model);
    read_opt_int(c, 94, d.channel_flags);
    if (!c.ok()) return ctx.fail(c.error());
    if (!c.at_end()) {
        d.trailing = c.take_rest();
        ctx.warn(what + ": " + std::to_string(d.trailing.size()) + " group(s) after the ones HDxf knows (subclass AcDbMaterial) are kept as they were read");
    }
    return true;
}

namespace {

bool same_real(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }

bool same_optional_real(const std::optional<double>& a, const std::optional<double>& b) {
    return a.has_value() == b.has_value() && (!a || same_real(*a, *b));
}

bool same_map(const MaterialMap& a, const MaterialMap& b) {
    if (!same_optional_real(a.blend, b.blend) || a.source != b.source || a.file_name != b.file_name ||
        a.projection != b.projection || a.tiling != b.tiling || a.auto_transform != b.auto_transform ||
        a.transform.has_value() != b.transform.has_value()) {
        return false;
    }
    if (a.transform) {
        for (size_t i = 0; i < 16; ++i) if (!same_real((*a.transform)[i], (*b.transform)[i])) return false;
    }
    return true;
}

bool same_color(const MaterialColor& a, const MaterialColor& b) {
    return a.method == b.method && same_optional_real(a.factor, b.factor) && a.color == b.color;
}

bool same_procedure(const MaterialProcedure& a, const MaterialProcedure& b) {
    if (a.name != b.name || a.table_end != b.table_end || a.values.size() != b.values.size()) return false;
    for (size_t i = 0; i < a.values.size(); ++i) {
        const MaterialProcedureValue& x = a.values[i];
        const MaterialProcedureValue& y = b.values[i];
        if (x.kind != y.kind || x.boolean != y.boolean || x.integer != y.integer || !same_real(x.real, y.real) || x.text != y.text ||
            x.color.aci != y.color.aci || x.color.rgb != y.color.rgb || x.color.name != y.color.name) {
            return false;
        }
    }
    return true;
}

bool read_material(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MaterialData d;
    if (!read_material_subclass(view, ctx, d, "MATERIAL " + o.handle)) return false;
    o.data = Boxed<MaterialData>(std::move(d));
    return true;
}

} // namespace

bool material_equal(const MaterialData& a, const MaterialData& b) {
    if (a.name != b.name || a.description != b.description || !same_color(a.ambient, b.ambient) ||
        !same_color(a.diffuse, b.diffuse) || !same_color(a.specular, b.specular) || !same_map(a.diffuse_map, b.diffuse_map) ||
        !same_map(a.specular_map, b.specular_map) || !same_map(a.reflection_map, b.reflection_map) ||
        !same_map(a.opacity_map, b.opacity_map) || !same_map(a.bump_map, b.bump_map) ||
        !same_map(a.refraction_map, b.refraction_map) || !same_map(a.normal_map, b.normal_map)) {
        return false;
    }
    const std::optional<double> ra[] = {a.gloss, a.opacity, a.refraction_index, a.color_bleed_scale, a.indirect_dump_scale,
                                        a.reflectance_scale, a.transmittance_scale, a.luminance, a.normal_map_strength,
                                        a.translucence, a.reflectivity};
    const std::optional<double> rb[] = {b.gloss, b.opacity, b.refraction_index, b.color_bleed_scale, b.indirect_dump_scale,
                                        b.reflectance_scale, b.transmittance_scale, b.luminance, b.normal_map_strength,
                                        b.translucence, b.reflectivity};
    for (size_t i = 0; i < sizeof(ra) / sizeof(ra[0]); ++i) if (!same_optional_real(ra[i], rb[i])) return false;
    if (a.two_sided != b.two_sided || a.luminance_mode != b.luminance_mode || a.normal_map_method != b.normal_map_method ||
        a.anonymous != b.anonymous || a.global_illumination_mode != b.global_illumination_mode ||
        a.final_gather_mode != b.final_gather_mode || a.map_u_tile != b.map_u_tile ||
        a.self_illumination != b.self_illumination || a.illumination_model != b.illumination_model ||
        a.channel_flags != b.channel_flags || a.procedures.size() != b.procedures.size() || a.trailing.size() != b.trailing.size()) {
        return false;
    }
    for (size_t i = 0; i < a.procedures.size(); ++i) if (!same_procedure(a.procedures[i], b.procedures[i])) return false;
    for (size_t i = 0; i < a.trailing.size(); ++i) {
        if (a.trailing[i].code != b.trailing[i].code || a.trailing[i].value != b.trailing[i].value) return false;
    }
    return true;
}

void register_material_codec(TypedRegistry& registry) {
    ObjectCodec c;
    c.dxf_name = "MATERIAL";
    c.type = ObjectType::Material;
    c.read = read_material;
    c.write = write_material;
    // The class as AutoCAD writes it (every 2007+ file; the built-in list of the writer has the same entry): no R13/R14 class
    c.cls.cpp_name = "AcDbMaterial";
    c.cls.application = "ObjectDBX Classes";
    c.cls.proxy_flags = 1153;
    // A material is a unit while it is written back (B12): the groups of its subclass mean different things where they
    // stand (the codes of the maps and of the 2007 tail are shared), so a changed record has its subclass written whole
    ListSpec whole;
    whole.subclass = "AcDbMaterial";
    whole.shape = ListShape::WholeSubclass;
    c.lists = {whole};
    // AutoCAD's own 2000 … 2018 files have materials (the standard three at least); R12 has no OBJECTS section, R13 and R14
    // have no ACAD_MATERIAL (the format profile, "Root dictionary"): the named error before 2000. R13/R14 cannot be saved
    // by AutoCAD 2025
    c.versions = VersionMatrix::native_from(DxfVersion::R2000);
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        c.versions.autocad(v, AutoCadStatus::Verified);
    }
    c.step = 7;
    registry.add(std::move(c));
}

} // namespace internal
} // namespace hdxf
