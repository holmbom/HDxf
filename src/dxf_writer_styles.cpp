#include "StyleTypes.hpp"

// MATERIAL and VISUALSTYLE (phase 9, step 9.7): writing the records as AutoCAD writes them (the groups in the order of its
// own files). A record read from a file is written from its preserved record (B12); these functions write a record made by
// a program and what changed in one read.

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
constexpr MapCodes kNormal = kDiffuse;

void write_map(RecordWriter& w, const MapCodes& k, const MaterialMap& m) {
    if (m.blend) w.real(k.blend, *m.blend);
    if (m.source) w.integer(k.source, *m.source);
    if (m.file_name) w.text(k.file, *m.file_name);
    if (m.projection) w.integer(k.projection, *m.projection);
    if (m.tiling) w.integer(k.tiling, *m.tiling);
    if (m.auto_transform) w.integer(k.auto_transform, *m.auto_transform);
    if (m.transform) {
        for (double v : *m.transform) w.real(k.transform, v);
    }
}

void write_color(RecordWriter& w, int method_code, int factor_code, int color_code, const MaterialColor& m) {
    if (m.method) w.integer(method_code, *m.method);
    if (m.factor) w.real(factor_code, *m.factor);
    if (m.color) w.integer(color_code, *m.color);
}

void write_procedure(RecordWriter& w, const MaterialProcedure& p) {
    w.text(300, p.name);
    for (const MaterialProcedureValue& v : p.values) {
        switch (v.kind) {
            case MaterialProcedureValue::Kind::Boolean: w.boolean(291, v.boolean); break;
            case MaterialProcedureValue::Kind::Integer: w.integer(271, v.integer); break;
            case MaterialProcedureValue::Kind::Real:    w.real(469, v.real); break;
            case MaterialProcedureValue::Kind::Text:    w.text(301, v.text); break;
            case MaterialProcedureValue::Kind::Color:
                w.integer(62, v.color.aci);
                if (v.color.has_rgb()) w.integer(420, v.color.rgb);
                if (!v.color.name.empty()) w.text(430, v.color.name);
                break;
        }
    }
    if (p.table_end) w.boolean(292, *p.table_end);
}

} // namespace

void write_material(RecordWriter& w, const Object& o) {
    const MaterialData& d = o.as_material();
    w.object_head("MATERIAL", o);
    w.subclass("AcDbMaterial");
    w.text(1, d.name);
    if (d.description) w.text(2, *d.description);
    write_color(w, 70, 40, 90, d.ambient);
    write_color(w, 71, 41, 91, d.diffuse);
    write_map(w, kDiffuse, d.diffuse_map);
    if (d.gloss) w.real(44, *d.gloss);
    write_color(w, 76, 45, 92, d.specular);
    write_map(w, kSpecular, d.specular_map);
    write_map(w, kReflection, d.reflection_map);
    if (d.opacity) w.real(140, *d.opacity);
    write_map(w, kOpacity, d.opacity_map);
    write_map(w, kBump, d.bump_map);
    if (d.refraction_index) w.real(145, *d.refraction_index);
    write_map(w, kRefraction, d.refraction_map);
    if (d.color_bleed_scale) w.real(460, *d.color_bleed_scale);
    if (d.indirect_dump_scale) w.real(461, *d.indirect_dump_scale);
    if (d.reflectance_scale) w.real(462, *d.reflectance_scale);
    if (d.transmittance_scale) w.real(463, *d.transmittance_scale);
    if (d.two_sided) w.boolean(290, *d.two_sided);
    if (d.luminance) w.real(464, *d.luminance);
    if (d.luminance_mode) w.integer(270, *d.luminance_mode);
    if (d.normal_map_method) w.integer(271, *d.normal_map_method);
    if (d.normal_map_strength) w.real(465, *d.normal_map_strength);
    write_map(w, kNormal, d.normal_map);
    if (d.anonymous) w.boolean(293, *d.anonymous);
    if (d.global_illumination_mode) w.integer(272, *d.global_illumination_mode);
    if (d.final_gather_mode) w.integer(273, *d.final_gather_mode);
    for (const MaterialProcedure& p : d.procedures) write_procedure(w, p);
    if (d.map_u_tile) w.integer(270, *d.map_u_tile);
    if (d.translucence) w.real(148, *d.translucence);
    if (d.self_illumination) w.integer(90, *d.self_illumination);
    if (d.reflectivity) w.real(468, *d.reflectivity);
    if (d.illumination_model) w.integer(93, *d.illumination_model);
    if (d.channel_flags) w.integer(94, *d.channel_flags);
    for (const DxfGroup& g : d.trailing) w.raw(g.code, g.value);
    w.xdata(o.xdata);
}

// The XDATA a SUN or VISUALSTYLE of 2018 has: not the list ACAD with AcDbSavedByObjectVersion alone, which 2013 and
// earlier write on them (ACadSharp's sample_AC1027 against sample_AC1032; B77)
static std::vector<XdataList> without_saved_by_version(const std::vector<XdataList>& xdata) {
    std::vector<XdataList> out;
    for (const auto& x : xdata) {
        bool saved_by = x.app_name == "ACAD" && x.items.size() == 2 && x.items[0].code == 1000 &&
                        x.items[0].value == "AcDbSavedByObjectVersion" && x.items[1].code == 1070;
        if (!saved_by) out.push_back(x);
    }
    return out;
}

namespace {

// The value of a property in the group(s) its code and kind have in the two older forms
void write_old_value(RecordWriter& w, const VisualStyleEntry& e) {
    const VisualStyleValue& v = e.value;
    int code = visual_style_property_code(e.property);
    switch (v.kind) {
        case VisualStyleValueKind::Integer: w.integer(code, v.integer); break;
        case VisualStyleValueKind::Real:    w.real(code, v.real); break;
        case VisualStyleValueKind::Boolean: w.boolean(code, v.boolean); break;
        case VisualStyleValueKind::Text:    w.text(code, v.text); break;
        case VisualStyleValueKind::Color:
            // 62 (AutoCAD's older files, always 5) before 63, the index, the true color after it where the
            // property has one (421, 424, 425)
            if (v.color_tag) w.integer(62, *v.color_tag);
            w.integer(code, v.color.aci);
            if (v.color.has_rgb()) {
                int rgb_code = visual_style_true_color_code(e.property);
                if (rgb_code != 0) w.integer(rgb_code, v.color.rgb);
            }
            break;
    }
}

// The value in the newest form: 90, 40, 290, 62 with 420, or 1
void write_new_value(RecordWriter& w, const VisualStyleEntry& e) {
    const VisualStyleValue& v = e.value;
    switch (v.kind) {
        case VisualStyleValueKind::Integer: w.integer(90, v.integer); break;
        case VisualStyleValueKind::Real:    w.real(40, v.real); break;
        case VisualStyleValueKind::Boolean: w.boolean(290, v.boolean); break;
        case VisualStyleValueKind::Text:    w.text(1, v.text); break;
        case VisualStyleValueKind::Color:
            w.integer(62, v.color.aci);
            if (v.color.has_rgb()) w.integer(420, v.color.rgb);
            break;
    }
}

} // namespace

void write_visual_style(RecordWriter& w, const Object& o) {
    const VisualStyleData& d = o.as_visual_style();
    w.object_head("VISUALSTYLE", o);
    w.subclass("AcDbVisualStyle");
    w.text(2, d.description);
    w.integer(70, d.style_type);
    switch (d.form) {
        case VisualStyleForm::Form2007: {
            for (size_t id = 0; id < kVisualStyleOldPropertyCount; ++id) {
                const VisualStyleEntry* e = d.find(static_cast<VisualStyleProperty>(id));
                if (e) write_old_value(w, *e);
                if (id == static_cast<size_t>(VisualStyleProperty::EdgeHidePrecision) && d.edge_style_apply) {
                    w.integer(174, *d.edge_style_apply);
                }
            }
            w.boolean(291, d.internal_use);
            if (d.trailing_real) w.real(45, *d.trailing_real);
            break;
        }
        case VisualStyleForm::Form2010: {
            w.integer(177, d.format_version.value_or(2));
            w.boolean(291, d.internal_use);
            for (size_t id = 0; id < kVisualStyleOldPropertyCount; ++id) {
                const VisualStyleEntry* e = d.find(static_cast<VisualStyleProperty>(id));
                if (!e) continue;
                write_old_value(w, *e);
                if (e->operation) w.integer(176, *e->operation);
            }
            break;
        }
        case VisualStyleForm::Form2013: {
            w.integer(177, d.format_version.value_or(3));
            w.boolean(291, d.internal_use);
            // The values are positional: the properties are the ones of ids 0 … n - 1
            size_t n = 0;
            while (n < kVisualStylePropertyCount && d.find(static_cast<VisualStyleProperty>(n))) ++n;
            for (const VisualStyleEntry& e : d.properties) {
                if (static_cast<size_t>(e.property) >= n) {
                    w.fail("VISUALSTYLE " + d.description + ": the properties of the newest form are positional, but property " +
                           std::to_string(n) + " (" + visual_style_property_name(static_cast<VisualStyleProperty>(n)) +
                           ") is missing");
                    return;
                }
            }
            w.integer(70, static_cast<int64_t>(n));
            for (size_t id = 0; id < n; ++id) {
                const VisualStyleEntry* e = d.find(static_cast<VisualStyleProperty>(id));
                write_new_value(w, *e);
                if (e->operation) w.integer(176, *e->operation);
            }
            break;
        }
    }
    for (const DxfGroup& g : d.trailing) w.raw(g.code, g.value);
    w.xdata(w.version() >= DxfVersion::R2018 ? without_saved_by_version(o.xdata) : o.xdata);
}

} // namespace internal
} // namespace hdxf
