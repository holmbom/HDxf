#include "RenderTypes.hpp"
#include <algorithm>
#include <initializer_list>
#include <string>
#include <vector>

// The render objects of step 9.11 (plan 2026-10-27-fas-9-typer.md; old plan "Render, LIGHTLIST och SUNSTUDY"): the
// base of the presets (RENDERSETTINGS), MENTALRAYRENDERSETTINGS, RAPIDRTRENDERSETTINGS, RENDERENVIRONMENT, RENDERGLOBAL,
// RAPIDRTRENDERENVIRONMENT and LIGHTLIST. The RAPIDRT preset and the IBL environment were typed in phase 6 (B78) with a
// reader in object_reader.cpp and a writer in dxf_writer_reference.cpp; since this step they are codecs like the others
// (the same groups in the same order: AutoCAD 2025's files and the existing tests check the bytes).
//
// The groups are read by position inside their subclass, in the order AutoCAD's files have them (the code repeats: 290 is
// four flags, 90 is a version and counts). Everything AutoCAD's files always have is required: a record that lacks it, or
// has a group where another one belongs (the reference's older table has the texture sampling as 90, not 290), stays
// untyped and is kept as it was read, with a warning (B105). The groups only some records have (the predefined flag, the
// last 290 and 40 of MentalRay) are optional.
//
// What AutoCAD 2025's files show (doc/facit, 2000 … 2018): MENTALRAYRENDERSETTINGS 2000 … 2013 (the preset in the root
// dictionary's ACAD_RENDER_ACTIVE_SETTINGS), RAPIDRTRENDERSETTINGS and RAPIDRTRENDERENVIRONMENT 2000 … 2018. RENDERSETTINGS,
// RENDERENVIRONMENT, RENDERGLOBAL and LIGHTLIST have no record in any file (AutoCAD 2025 writes none, its core has no command
// that makes them): they are written as the reference's groups say and are ej testat (F9.13, F9.14). RENDERENTRY has neither
// a record nor a table in any source: it stays an untyped, preserved record (no group is invented).

namespace hdxf {
namespace internal {

namespace {

std::string describe_record(const RecordView& view) {
    return view.type_name() + " " + view.head().handle;
}

// Groups after the ones HDxf knows (a later version's) are kept as they were read
void note_rest(GroupCursor& c, TypedReadContext& ctx, const RecordView& view) {
    if (!c.ok() || c.at_end()) return;
    size_t n = c.take_rest().size();
    ctx.warn(describe_record(view) + ": " + std::to_string(n) + " group(s) after the ones HDxf knows (subclass " + c.name() +
             ") are kept as they were read");
}

bool read_render_base(const RecordView& view, TypedReadContext& ctx, RenderSettingsData& d) {
    GroupCursor c(view, "AcDbRenderSettings");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbRenderSettings subclass");
    c.require_integer(90, d.version);
    c.require_string(1, d.name);
    c.require_boolean(290, d.materials);
    c.require_boolean(290, d.texture_sampling);
    c.require_boolean(290, d.back_faces);
    c.require_boolean(290, d.shadows);
    c.require_string(1, d.preview_file);
    c.require_string(1, d.description);
    c.require_integer(90, d.display_index);
    bool predefined = false;
    if (c.boolean(290, predefined)) d.predefined = predefined;
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    // The base of a MentalRay or RapidRT preset is the whole of its subclass; RENDERSETTINGS by itself too
    note_rest(c, ctx, view);
    return true;
}

// --- RENDERSETTINGS ---

bool read_render_settings(const RecordView& view, TypedReadContext& ctx, Object& o) {
    RenderSettingsData d;
    if (!read_render_base(view, ctx, d)) return false;
    o.data = Boxed<RenderSettingsData>(std::move(d));
    return true;
}

// --- MENTALRAYRENDERSETTINGS ---

bool read_mentalray_settings(const RecordView& view, TypedReadContext& ctx, Object& o) {
    MentalRayRenderSettingsData d;
    if (!read_render_base(view, ctx, d.base)) return false;
    GroupCursor c(view, "AcDbMentalRayRenderSettings");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbMentalRayRenderSettings subclass");
    int16_t filter = 0;
    c.require_integer(90, d.version);
    c.require_integer(90, d.min_samples);
    c.require_integer(90, d.max_samples);
    c.require_integer(70, filter);
    c.require_real(40, d.filter_width);
    c.require_real(40, d.filter_height);
    for (double& v : d.contrast) c.require_real(40, v);
    c.require_integer(70, d.shadow_mode);
    c.require_boolean(290, d.shadow_maps);
    c.require_boolean(290, d.ray_tracing);
    c.require_integer(90, d.trace_reflections);
    c.require_integer(90, d.trace_refractions);
    c.require_integer(90, d.trace_max);
    c.require_boolean(290, d.global_illumination);
    c.require_integer(90, d.photons_per_sample);
    c.require_boolean(290, d.use_radius);
    c.require_real(40, d.radius);
    c.require_integer(90, d.photons_per_light);
    c.require_integer(90, d.photon_reflections);
    c.require_integer(90, d.photon_refractions);
    c.require_integer(90, d.photon_max);
    c.require_boolean(290, d.final_gather);
    c.require_integer(90, d.final_gather_rays);
    c.require_boolean(290, d.use_min_radius);
    c.require_boolean(290, d.use_max_radius);
    c.require_boolean(290, d.radius_in_pixels);
    c.require_real(40, d.min_radius);
    c.require_real(40, d.max_radius);
    c.require_real(40, d.luminance_scale);
    c.require_integer(70, d.diagnostic_mode);
    c.require_integer(70, d.grid_mode);
    c.require_real(40, d.grid_size);
    c.require_integer(70, d.photon_mode);
    c.require_integer(70, d.bsp_mode);
    c.require_boolean(290, d.export_statistics);
    c.require_string(1, d.export_file);
    c.require_integer(90, d.tile_size);
    c.require_integer(70, d.tile_order);
    c.require_integer(90, d.memory_limit);
    // AutoCAD's own files end with a 290 and a 40 the reference's table lacks
    if (c.boolean(290, d.extra_flag)) c.real(40, d.extra_factor);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    d.filter = static_cast<SamplingFilter>(filter);
    note_rest(c, ctx, view);
    o.data = Boxed<MentalRayRenderSettingsData>(std::move(d));
    return true;
}

// --- RAPIDRTRENDERSETTINGS ---

bool read_rapidrt_settings(const RecordView& view, TypedReadContext& ctx, Object& o) {
    RenderSettingsData base;
    if (!read_render_base(view, ctx, base)) return false;
    GroupCursor c(view, "AcDbRapidRTRenderSettings");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbRapidRTRenderSettings subclass");
    RapidRtRenderSettingsData d;
    d.base_version = base.version;
    d.name = base.name;
    d.materials = base.materials;
    d.texture_sampling = base.texture_sampling;
    d.back_faces = base.back_faces;
    d.shadows = base.shadows;
    d.preview_file = base.preview_file;
    d.description = base.description;
    d.display_index = base.display_index;
    if (base.predefined) d.predefined = *base.predefined;
    int16_t duration_type = 0, accuracy = 0, filter = 0;
    c.require_integer(90, d.version);
    c.require_integer(70, duration_type);
    c.require_integer(90, d.levels);
    c.require_integer(90, d.duration);
    c.require_integer(70, accuracy);
    c.require_integer(70, filter);
    c.require_real(40, d.filter_width);
    c.require_real(40, d.filter_height);
    // AutoCAD's files have the predefined flag last; the reference after the display index (read above)
    bool predefined = false;
    if (c.boolean(290, predefined)) d.predefined = predefined;
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    d.duration_type = static_cast<RenderDurationType>(duration_type);
    d.accuracy = static_cast<RenderAccuracy>(accuracy);
    d.filter = static_cast<SamplingFilter>(filter);
    note_rest(c, ctx, view);
    o.data = std::move(d);
    return true;
}

// --- RENDERENVIRONMENT, RENDERGLOBAL ---

bool read_render_environment(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbRenderEnvironment");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbRenderEnvironment subclass");
    RenderEnvironmentData d;
    c.require_integer(90, d.version);
    c.require_boolean(290, d.fog_enabled);
    c.require_boolean(290, d.fog_background);
    for (uint8_t& channel : d.fog_color) c.require_integer(280, channel);
    c.require_real(40, d.near_density);
    c.require_real(40, d.far_density);
    c.require_real(40, d.near_distance);
    c.require_real(40, d.far_distance);
    c.require_boolean(290, d.use_image);
    c.require_string(1, d.image_file);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = Boxed<RenderEnvironmentData>(std::move(d));
    return true;
}

bool read_render_global(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbRenderGlobal");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbRenderGlobal subclass");
    RenderGlobalData d;
    c.require_integer(90, d.version);
    c.require_integer(90, d.procedure);
    c.require_integer(90, d.destination);
    c.require_boolean(290, d.save_to_file);
    c.require_string(1, d.file_name);
    c.require_integer(90, d.width);
    c.require_integer(90, d.height);
    c.require_boolean(290, d.predefined_first);
    c.require_boolean(290, d.high_info);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = Boxed<RenderGlobalData>(std::move(d));
    return true;
}

// --- RAPIDRTRENDERENVIRONMENT ---

bool read_rapidrt_environment(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbIBLBackground");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbIBLBackground subclass");
    RapidRtRenderEnvironmentData d;
    c.require_integer(90, d.version);
    c.require_boolean(290, d.enabled);
    c.require_string(1, d.ibl_file);
    c.require_real(40, d.rotation);
    c.require_boolean(290, d.ibl_as_background);
    c.handle(340, d.background_handle);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = std::move(d);
    return true;
}

void rapidrt_environment_references(const Object& o, std::vector<TypedReference>& out) {
    const std::string& h = o.as_rapidrt_environment().background_handle;
    if (!h.empty() && h != "0") out.push_back({h, RefKind::HardPointer, false});
}

// --- LIGHTLIST ---

bool read_light_list(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbLightList");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbLightList subclass");
    LightListData d;
    c.require_integer(90, d.version);
    size_t n = 0;
    if (!c.count(90, 2, n) && c.ok()) c.fail("the number of lights (90) is missing");
    for (size_t i = 0; c.ok() && i < n; ++i) {
        LightListEntry e;
        c.require_handle(5, e.light_handle);
        c.require_string(1, e.name);
        d.lights.push_back(std::move(e));
    }
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = Boxed<LightListData>(std::move(d));
    return true;
}

void light_list_references(const Object& o, std::vector<TypedReference>& out) {
    for (const LightListEntry& e : o.as_light_list().lights) {
        if (!e.light_handle.empty() && e.light_handle != "0") out.push_back({e.light_handle, RefKind::SoftPointer, false});
    }
}

// --- the matrices ---

VersionMatrix verified_in(VersionMatrix m, std::initializer_list<DxfVersion> versions) {
    for (DxfVersion v : versions) m.autocad(v, AutoCadStatus::Verified);
    return m;
}

// RAPIDRT: AutoCAD 2025 writes them in every version from 2000; an older version's error says 2013, as it did when
// they were typed in phase 6 (B78)
VersionMatrix rapidrt_matrix(const char* error) {
    VersionMatrix m = verified_in(VersionMatrix::native_from(DxfVersion::R2000),
                                  {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                                   DxfVersion::R2013, DxfVersion::R2018});
    m.error(DxfVersion::R12, error).error(DxfVersion::R13, error).error(DxfVersion::R14, error);
    return m;
}

ClassSpec scene_class(const char* cpp_name, int flags) {
    ClassSpec cls;
    cls.cpp_name = cpp_name;
    cls.application = "SCENEOE";
    cls.proxy_flags = flags;
    return cls;
}

} // namespace

void register_render_codecs(TypedRegistry& registry) {
    {
        // The base by itself: no AutoCAD file has a record of it (ej testat, F9.13); the class as its derived classes'
        ObjectCodec c;
        c.dxf_name = "RENDERSETTINGS";
        c.type = ObjectType::RenderSettings;
        c.read = read_render_settings;
        c.write = write_render_settings_record;
        c.cls = scene_class("AcDbRenderSettings", 1024);
        c.versions = VersionMatrix::native_from(DxfVersion::R2000);
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "MENTALRAYRENDERSETTINGS";
        c.type = ObjectType::MentalRayRenderSettings;
        c.read = read_mentalray_settings;
        c.write = write_mentalray_settings_record;
        c.cls = scene_class("AcDbMentalRayRenderSettings", 1024);
        // AutoCAD 2025's files have the record in 2000 … 2013 (its 2018 files none: the feature set is gone)
        c.versions = verified_in(VersionMatrix::native_from(DxfVersion::R2000),
                                 {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                                  DxfVersion::R2013});
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "RAPIDRTRENDERSETTINGS";
        c.type = ObjectType::RapidRtRenderSettings;
        c.read = read_rapidrt_settings;
        c.write = write_rapidrt_settings_record;
        c.cls.cpp_name = "AcDbRapidRTRenderSettings";
        c.cls.application = "ObjectDBX Classes";
        c.cls.proxy_flags = 1024;
        c.versions = rapidrt_matrix("A RAPIDRT render preset requires DXF 2013 or later");
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "RENDERENVIRONMENT";
        c.type = ObjectType::RenderEnvironment;
        c.read = read_render_environment;
        c.write = write_render_environment_record;
        c.cls = scene_class("AcDbRenderEnvironment", 1024);
        // Autodesk's references: the 2007 reference has it, the 2004 reference's OBJECTS do not (no AutoCAD file has one, B35)
        c.versions = VersionMatrix::native_from(DxfVersion::R2007);
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "RENDERGLOBAL";
        c.type = ObjectType::RenderGlobal;
        c.read = read_render_global;
        c.write = write_render_global_record;
        c.cls = scene_class("AcDbRenderGlobal", 1024);
        c.versions = VersionMatrix::native_from(DxfVersion::R2007);
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "RAPIDRTRENDERENVIRONMENT";
        c.type = ObjectType::RapidRtRenderEnvironment;
        c.read = read_rapidrt_environment;
        c.write = write_rapidrt_environment_record;
        c.references = rapidrt_environment_references;
        c.cls = scene_class("AcDbIBLBackground", 1024);
        c.versions = rapidrt_matrix("A RAPIDRT render environment requires DXF 2013 or later");
        RefDescriptor background;
        background.subclass = "AcDbIBLBackground";
        background.code = 340;
        background.kind = RefKind::HardPointer;
        background.removal = RefRemoval::ZeroInPlace;
        c.refs.push_back(background);
        c.step = 11;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "LIGHTLIST";
        c.type = ObjectType::LightList;
        c.read = read_light_list;
        c.write = write_light_list_record;
        c.references = light_list_references;
        c.cls = scene_class("AcDbLightList", 1024);
        c.versions = VersionMatrix::native_from(DxfVersion::R2007);
        // Each member is 5 (the light's handle: a pointer, not the record's own) and 1; a member whose light is gone
        // goes with its name and the count (90) follows
        RefDescriptor light;
        light.subclass = "AcDbLightList";
        light.code = 5;
        light.kind = RefKind::SoftPointer;
        light.removal = RefRemoval::DropPair;
        light.first_offset = 0;
        light.last_offset = 1;
        light.partner_code = 1;
        light.counter_code = 90;
        c.refs.push_back(light);
        // The count means something where it stands (the version is a 90 too): the members are written whole when they change
        ListSpec members;
        members.subclass = "AcDbLightList";
        members.shape = ListShape::WholeSubclass;
        c.lists.push_back(members);
        c.step = 11;
        registry.add(std::move(c));
    }
}

// ---------------------------------------------------------------------------
// The Drawing's render objects (declared in hdxf.hpp)
// ---------------------------------------------------------------------------

} // namespace internal

namespace {

DictionaryEntry* find_entry(DictionaryData& dict, const std::string& name) {
    for (DictionaryEntry& e : dict.entries) if (e.name == name) return &e;
    return nullptr;
}

} // namespace

// The one object of its type the root dictionary has under `entry_name` (its data replaced), or a new one: owned by the
// root and in it as AutoCAD's files have these (ACAD_RENDER_ACTIVE_SETTINGS names the MentalRay preset itself)
Object& Drawing::set_root_object(const std::string& entry_name, ObjectType type, ObjectData data) {
    ensure_r2000_structure();
    scan_handles();
    Object* root = nullptr;
    for (auto& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") { root = &o; break; }
    }
    const std::string root_handle = root ? root->handle : "0";
    if (root) {
        if (DictionaryEntry* entry = find_entry(root->as_dictionary(), entry_name)) {
            if (Object* o = find_object(entry->handle); o && o->type == type) {
                o->data = std::move(data);
                return *o;
            }
        }
    }
    const std::string handle = next_handle();
    if (root) {
        DictionaryData& dict = root->as_dictionary();
        if (DictionaryEntry* entry = find_entry(dict, entry_name)) {
            entry->handle = handle;
        } else {
            auto at = std::find_if(dict.entries.begin(), dict.entries.end(),
                                   [&](const DictionaryEntry& e) { return e.name > entry_name; });
            dict.entries.insert(at, {entry_name, handle, false});
        }
    }
    Object o;
    o.type = type;
    o.handle = handle;
    o.owner = root_handle;
    if (root_handle != "0") o.reactors.push_back(root_handle);
    o.data = std::move(data);
    objects_.push_back(std::move(o));
    return objects_.back();
}

Object& Drawing::set_mentalray_render_settings(const MentalRayRenderSettingsData& data) {
    return set_root_object("ACAD_RENDER_ACTIVE_SETTINGS", ObjectType::MentalRayRenderSettings, Boxed<MentalRayRenderSettingsData>(data));
}

Object& Drawing::set_render_environment(const RenderEnvironmentData& data) {
    return set_root_object("ACAD_RENDER_ENVIRONMENT", ObjectType::RenderEnvironment, Boxed<RenderEnvironmentData>(data));
}

Object& Drawing::set_render_global(const RenderGlobalData& data) {
    return set_root_object("ACAD_RENDER_GLOBAL", ObjectType::RenderGlobal, Boxed<RenderGlobalData>(data));
}

Object& Drawing::set_light_list(const LightListData& data) {
    return set_root_object("ACAD_LIGHT", ObjectType::LightList, Boxed<LightListData>(data));
}

} // namespace hdxf
