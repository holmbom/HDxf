#include "RenderTypes.hpp"
#include <initializer_list>
#include <string>
#include <vector>

// SUNSTUDY, SUN and LIGHT (step 9.11; plan 2026-10-27-fas-9-typer.md). SUN and LIGHT were typed before phase 9 (a reader in
// object_reader.cpp and dxf_reader.cpp, writers in dxf_writer_2000.cpp and dxf_writer_surface.cpp); since this step they
// are codecs like every other typed record, with the same groups in the same order (AutoCAD 2025's files in every
// version check the bytes). A LIGHT is native from DXF 2000: AutoCAD 2025 writes it in 2000 and 2004 too (doc/facit/
// fas9_render.scr), with its color as one group 90 there.
//
// SUNSTUDY has no record in any AutoCAD file (the core has no command; the Sun Study wizard makes it): its groups are the
// reference's, and the whole type is ej testat (F9.15). The hour is group 290, which is a flag in the binary format: HDxf
// reads and writes it as the number it is and never as a flag, in ASCII and binary alike. Whether AutoCAD itself keeps the
// number in that byte is what an AutoCAD ASCII/binary pair would show, and none can be made.

namespace hdxf {
namespace internal {

namespace {

std::string describe_record(const RecordView& view) {
    return view.type_name() + " " + view.head().handle;
}

void note_rest(GroupCursor& c, TypedReadContext& ctx, const RecordView& view) {
    if (!c.ok() || c.at_end()) return;
    size_t n = c.take_rest().size();
    ctx.warn(describe_record(view) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
             ") are kept as they were read");
}

// --- LIGHT ---

bool read_light(const RecordView& view, TypedReadContext& ctx, Entity& e) {
    GroupCursor c(view, "AcDbLight");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbLight subclass");
    LightData d;
    c.require_integer(90, d.version);
    c.require_string(1, d.name);
    c.require_integer(70, d.light_type);
    c.require_boolean(290, d.on);
    if (c.ok() && c.code() == 90) {
        // DXF 2000: the color is one group, an AcCmEntityColor
        int32_t raw = 0;
        c.integer(90, raw);
        if (c.ok() && !light_color_from_raw(raw, d.color)) {
            c.fail("the color (90) " + std::to_string(raw) + " is not an AcCmEntityColor of an index or a true color");
        }
    } else {
        c.require_integer(63, d.color.aci);
        c.integer(421, d.color.rgb);            // only when the color has a true color
    }
    c.require_boolean(291, d.plot_glyph);
    c.require_real(40, d.intensity);
    c.require_point3(10, d.position);
    c.require_point3(11, d.target);
    c.require_integer(72, d.attenuation_type);
    c.require_boolean(292, d.use_limits);
    c.require_real(41, d.start_limit);
    c.require_real(42, d.end_limit);
    c.require_real(50, d.hotspot_angle);
    c.require_real(51, d.falloff_angle);
    c.require_boolean(293, d.cast_shadows);
    c.require_integer(73, d.shadow_type);
    c.require_integer(91, d.shadow_map_size);
    c.require_integer(280, d.shadow_softness);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    e.data = Boxed<LightData>(std::move(d));
    return true;
}

// --- SUN ---

bool read_sun(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbSun");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbSun subclass");
    SunData d;
    d.color = Color::from_aci(7);
    c.require_integer(90, d.version);
    c.require_boolean(290, d.on);
    c.require_integer(63, d.color.aci);
    c.integer(421, d.color.rgb);
    c.require_real(40, d.intensity);
    c.require_boolean(291, d.shadows);
    c.require_integer(91, d.julian_day);
    c.require_integer(92, d.time);
    c.require_boolean(292, d.daylight_saving);
    c.require_integer(70, d.shadow_type);
    c.require_integer(71, d.shadow_map_size);
    c.require_integer(280, d.shadow_softness);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = std::move(d);
    return true;
}

// --- SUNSTUDY ---

bool read_sun_study(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbSunStudy");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbSunStudy subclass");
    SunStudyData d;
    c.require_integer(90, d.version);
    c.require_string(1, d.name);
    c.require_string(2, d.description);
    c.require_integer(70, d.output_type);
    std::string text;
    bool flag = false;
    if (c.string(3, text)) d.sheet_set_name = text;
    if (c.boolean(290, flag)) d.use_subset = flag;
    if (c.string(4, text)) d.sheet_subset_name = text;
    c.require_boolean(291, d.select_dates);
    size_t n = 0;
    if (!c.count(91, 2, n) && c.ok()) c.fail("the number of dates (91) is missing");
    for (size_t i = 0; c.ok() && i < n; ++i) {
        SunStudyDate date;
        c.require_integer(90, date.julian_day);
        c.require_integer(90, date.seconds);
        d.dates.push_back(date);
    }
    c.require_boolean(292, d.select_range);
    int32_t value = 0;
    if (c.integer(93, value)) d.start_time = value;
    if (c.integer(94, value)) d.end_time = value;
    if (c.integer(95, value)) d.interval = value;
    size_t hours = 0;
    if (!c.count(73, 1, hours) && c.ok()) c.fail("the number of hours (73) is missing");
    for (size_t i = 0; c.ok() && i < hours; ++i) {
        // The hour is an integer in a group that is a flag in the binary format: never read as a flag
        int32_t hour = 0;
        if (!c.integer(290, hour) && c.ok()) c.fail("hour " + std::to_string(i + 1) + " (290) is missing");
        d.hours.push_back(hour);
    }
    c.handle(340, d.page_setup_handle);
    c.handle(341, d.view_handle);
    c.handle(342, d.visual_style_handle);
    c.require_integer(74, d.shade_plot_type);
    c.require_integer(75, d.viewports_per_page);
    c.require_integer(76, d.rows);
    c.require_integer(77, d.columns);
    c.require_real(40, d.spacing);
    c.require_boolean(293, d.lock_viewports);
    c.require_boolean(294, d.label_viewports);
    if (c.handle(343, text)) d.text_style_handle = text;
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = Boxed<SunStudyData>(std::move(d));
    return true;
}

void sun_study_references(const Object& o, std::vector<TypedReference>& out) {
    const SunStudyData& d = o.as_sun_study();
    auto add = [&](const std::string& h) {
        if (!h.empty() && h != "0") out.push_back({h, RefKind::HardPointer, false});
    };
    add(d.page_setup_handle);
    add(d.view_handle);
    add(d.visual_style_handle);
    if (d.text_style_handle) add(*d.text_style_handle);
}

RefDescriptor sun_study_pointer(int code) {
    RefDescriptor r;
    r.subclass = "AcDbSunStudy";
    r.code = code;
    r.kind = RefKind::HardPointer;
    r.removal = RefRemoval::ZeroInPlace;
    return r;
}

VersionMatrix verified_in(VersionMatrix m, std::initializer_list<DxfVersion> versions) {
    for (DxfVersion v : versions) m.autocad(v, AutoCadStatus::Verified);
    return m;
}

} // namespace

void register_sun_study_codecs(TypedRegistry& registry) {
    {
        // AutoCAD 2025's SUN objects (doc/facit/acad_<version>_dxf_objects): 2007 … 2018; its 2000 and 2004 files have none
        ObjectCodec c;
        c.dxf_name = "SUN";
        c.type = ObjectType::Sun;
        c.read = read_sun;
        c.write = write_sun_record;
        c.cls.cpp_name = "AcDbSun";
        c.cls.application = "SCENEOE";
        c.cls.proxy_flags = 1153;
        c.versions = verified_in(VersionMatrix::native_from(DxfVersion::R2007),
                                 {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                                  DxfVersion::R2013, DxfVersion::R2018});
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        // AutoCAD 2025 writes a LIGHT in 2000 and 2004 as well (fas9_render.scr); the 2000 color is one group 90
        EntityCodec c;
        c.dxf_name = "LIGHT";
        c.type = EntityType::Light;
        c.read = read_light;
        c.write = write_light_record;
        c.cls.cpp_name = "AcDbLight";
        c.cls.application = "SCENEOE";
        c.cls.proxy_flags = 1025;
        c.versions = verified_in(VersionMatrix::native_from(DxfVersion::R2000),
                                 {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                                  DxfVersion::R2013, DxfVersion::R2018});
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "SUNSTUDY";
        c.type = ObjectType::SunStudy;
        c.read = read_sun_study;
        c.write = write_sun_study_record;
        c.references = sun_study_references;
        c.cls.cpp_name = "AcDbSunStudy";
        c.cls.application = "SCENEOE";
        c.cls.proxy_flags = 1024;
        // Autodesk's references: the 2007 reference has it, the 2004 reference's OBJECTS do not (no AutoCAD file has one, B35)
        c.versions = VersionMatrix::native_from(DxfVersion::R2007);
        for (int code : {340, 341, 342, 343}) c.refs.push_back(sun_study_pointer(code));
        // Counts and the hours (a 290 each) are lists whose groups mean something where they stand: written whole
        ListSpec whole;
        whole.subclass = "AcDbSunStudy";
        whole.shape = ListShape::WholeSubclass;
        c.lists.push_back(whole);
        c.step = 11;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
