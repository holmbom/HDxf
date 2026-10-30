#include "TypedRecordTypes.hpp"
#include "SectionTypes.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

// The section family (phase 9, step 9.12; plan 2026-10-27-fas-9-typer.md, "SECTIONOBJECT, SECTION_MANAGER och
// SECTION_SETTINGS"): the entity SECTIONOBJECT (AcDbSection), the manager SECTION_MANAGER (AcDbSectionManager) and the
// settings SECTION_SETTINGS (AcDbSectionSettings with its nested type and geometry settings), as codecs of the registry.
//
// What AutoCAD's own files show (doc/facit/acad_<ver>_dxf_objects*.dxf, 2000 ... 2018, ASCII and binary; the sections of
// doc/facit/fas9_sections.scr made with AutoCAD 2025's core; the notes in doc/facit/fas9_notes/F9.16.md):
//   - SECTIONOBJECT: AcDbEntity with its proxy graphics (92 before 2010, 160 from 2010, then 310), AcDbSection 90 91 1
//     10 20 30 40 41 70, 62 (2004 and later: the indicator's color, 9 by default; 2000's record has none), 92 and its
//     vertices (11 21 31), 93 and its back vertices (12 22 32), 360 (the settings it owns), XDATA IsSlice and
//     ThicknessDepth. The reference's tables say 63/411 for the color; AutoCAD writes 62, which is followed
//   - SECTION_MANAGER: reactor and owner the NOD, AcDbSectionManager 70 (1) and, in files that list live sections, 90 and
//     330 each; AutoCAD 2025 lists none (its files have 70 only, whatever sections the drawing has)
//   - SECTION_SETTINGS: owned by its section, AcDbSectionSettings 90 (2) 91 (0): no settings of a type in any file AutoCAD
//     2025 makes; a file that has them (the old plan's B.67, AutoCAD 2018's LiveSection) has the type settings, each of them
//     1 "SectionTypeSettings" 90 91 92 (330 each) 331 1 93 and the geometries 2 "SectionGeometrySettings" 90 91 92 62 8 6
//     40 1 370 70 71 72 2 41 42 43 3 "SectionGeometrySettingsEnd", then 3 "SectionTypeSettingsEnd"
// The indicator (the proxy graphics) is made from the section when it is new or when it follows the shape AutoCAD gives it
// (section_graphics.cpp); a stream of another shape is kept as it was read.

namespace hdxf {

namespace {

const char* const kTypeMarker = "SectionTypeSettings";
const char* const kTypeEnd = "SectionTypeSettingsEnd";
const char* const kGeometryMarker = "SectionGeometrySettings";
const char* const kGeometryEnd = "SectionGeometrySettingsEnd";

bool same_name_ci(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

const Object* root_dictionary(const Drawing& dwg) {
    for (const Object& o : dwg.objects()) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") return &o;
    }
    return nullptr;
}

} // namespace

Entity& Drawing::add_section(const std::string& name, const std::vector<Point3>& vertices, const std::string& layer) {
    if (version_ >= DxfVersion::R2000) ensure_r2000_structure();
    else if (version_ >= DxfVersion::R13) ensure_older_structure();
    scan_handles();

    int sections = 0;
    auto count = [&](const std::vector<Entity>& list) {
        for (const Entity& e : list) {
            if (e.type == EntityType::SectionObject) ++sections;
        }
    };
    count(entities_);
    for (const Block& b : blocks_) count(b.entities);

    // Before 2000 only the entity is made: saving it is the named error, and nothing else of the drawing changes
    if (version_ < DxfVersion::R2000) {
        Entity old;
        old.type = EntityType::SectionObject;
        old.props.layer = layer;
        old.props.handle = next_handle();
        SectionObjectData data;
        data.name = name.empty() ? "Section Plane (" + std::to_string(sections + 1) + ")" : name;
        data.vertices = vertices;
        old.data = Boxed<SectionObjectData>(std::move(data));
        entities_.push_back(std::move(old));
        return entities_.back();
    }

    // AutoCAD's order of handles: the section, then the manager (the first time), then its settings
    const std::string section_handle = next_handle();
    std::string manager_handle;
    Object* root = nullptr;
    for (Object& o : objects_) {
        if (o.type == ObjectType::Dictionary && o.owner == "0") {
            root = &o;
            break;
        }
    }
    if (root) {
        for (const DictionaryEntry& entry : root->as_dictionary().entries) {
            if (entry.name == "ACAD_SECTION_MANAGER") {
                const Object* have = find_object(entry.handle);
                if (have && have->type == ObjectType::SectionManager) manager_handle = entry.handle;
                break;
            }
        }
        if (manager_handle.empty()) {
            manager_handle = next_handle();
            const std::string root_handle = root->handle;
            DictionaryData& entries = root->as_dictionary();
            entries.entries.erase(std::remove_if(entries.entries.begin(), entries.entries.end(),
                                                 [](const DictionaryEntry& e) { return e.name == "ACAD_SECTION_MANAGER"; }),
                                  entries.entries.end());
            auto at = std::find_if(entries.entries.begin(), entries.entries.end(),
                                   [](const DictionaryEntry& e) { return e.name > "ACAD_SECTION_MANAGER"; });
            entries.entries.insert(at, {"ACAD_SECTION_MANAGER", manager_handle, false});
            Object manager;
            manager.type = ObjectType::SectionManager;
            manager.handle = manager_handle;
            manager.owner = root_handle;
            manager.reactors.push_back(root_handle);
            manager.data = SectionManagerData{};
            objects_.push_back(std::move(manager));
        }
    }

    // The indicator's outline is on the layer Defpoints (AutoCAD's: color 7, Continuous, not plotted)
    if (std::none_of(layers_.begin(), layers_.end(), [](const Layer& l) { return same_name_ci(l.name, "Defpoints"); })) {
        Layer l;
        l.name = "Defpoints";
        l.color = Color::white();
        l.plot = false;
        layers_.push_back(std::move(l));
    }

    const std::string settings_handle = next_handle();
    Object settings;
    settings.type = ObjectType::SectionSettings;
    settings.handle = settings_handle;
    settings.owner = section_handle;
    settings.data = Boxed<SectionSettingsData>(SectionSettingsData{});
    objects_.push_back(std::move(settings));

    Entity e;
    e.type = EntityType::SectionObject;
    e.props.layer = layer;
    e.props.handle = section_handle;
    // AutoCAD's XDATA of a plane: IsSlice 0 0, ThicknessDepth -1
    XdataList is_slice;
    is_slice.app_name = "IsSlice";
    is_slice.items = {{1070, "0"}, {1070, "0"}};
    XdataList depth;
    depth.app_name = "ThicknessDepth";
    depth.items = {{1040, "-1.0"}};
    e.props.xdata = {is_slice, depth};
    SectionObjectData d;
    d.name = name.empty() ? "Section Plane (" + std::to_string(sections + 1) + ")" : name;
    d.vertices = vertices;
    d.settings_handle = settings_handle;
    e.data = Boxed<SectionObjectData>(std::move(d));
    entities_.push_back(std::move(e));
    return entities_.back();
}

Object* Drawing::section_settings(const Entity& section) {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->section_settings(section));
}

const Object* Drawing::section_settings(const Entity& section) const {
    if (section.type != EntityType::SectionObject) return nullptr;
    const std::string& handle = section.as_section_object().settings_handle;
    if (handle.empty() || handle == "0") return nullptr;
    const Object* o = find_object(handle);
    return o && o->type == ObjectType::SectionSettings ? o : nullptr;
}

Object* Drawing::section_manager() {
    return const_cast<Object*>(static_cast<const Drawing*>(this)->section_manager());
}

const Object* Drawing::section_manager() const {
    if (const Object* root = root_dictionary(*this)) {
        for (const DictionaryEntry& entry : root->as_dictionary().entries) {
            if (entry.name != "ACAD_SECTION_MANAGER") continue;
            const Object* o = find_object(entry.handle);
            if (o && o->type == ObjectType::SectionManager) return o;
        }
    }
    for (const Object& o : objects_) {
        if (o.type == ObjectType::SectionManager) return &o;
    }
    return nullptr;
}

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

// ---------------------------------------------------------------------------
// SECTIONOBJECT
// ---------------------------------------------------------------------------

bool read_vertices(GroupCursor& c, int count_code, int code, const char* what, std::vector<Point3>& out) {
    size_t n = 0;
    if (!c.count(count_code, 3, n)) {
        if (c.ok()) c.fail(std::string("the count of the ") + what + " (group " + std::to_string(count_code) + ") is missing");
        return false;
    }
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        Point3 p;
        if (!c.point3(code, p)) {
            c.fail(std::string(what) + " " + std::to_string(i) + " of " + std::to_string(n) + " is missing (group " + std::to_string(code) + ")");
            return false;
        }
        out.push_back(p);
    }
    return true;
}

bool read_section_object(const RecordView& view, TypedReadContext& ctx, Entity& e) {
    GroupCursor c(view, "AcDbSection");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbSection subclass");
    SectionObjectData d;
    c.require_integer(90, d.state);
    c.require_integer(91, d.flags);
    c.require_string(1, d.name);
    c.require_point3(10, d.vertical_direction);
    c.require_real(40, d.top_height);
    c.require_real(41, d.bottom_height);
    int16_t transparency = static_cast<int16_t>(d.indicator_transparency);
    c.require_integer(70, transparency);
    int16_t color = 0;
    if (c.integer(62, color)) d.indicator_color = color;
    if (c.ok()) read_vertices(c, 92, 11, "vertices", d.vertices);
    if (c.ok()) read_vertices(c, 93, 12, "back vertices", d.back_vertices);
    c.handle(360, d.settings_handle);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    d.indicator_transparency = transparency;
    note_rest(c, ctx, view);
    // The entity's own proxy graphics (AcDbEntity 92/160 + 310): the indicator
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &d.proxy_graphics, error)) return ctx.fail(error);
    e.data = Boxed<SectionObjectData>(std::move(d));
    return true;
}

// A pointer to an object: the handle when the object is there (a drawing built in memory knows what was removed from it
// only by looking: every handle is "live" there), 0 when it is gone (B19). While the drawing as it was read is written for
// comparison the handle stays as it was read
std::string object_pointer(const RecordWriter& w, const std::string& handle) {
    if (handle.empty() || handle == "0") return "0";
    if (!w.filtering()) return handle;
    return w.live(handle) && w.drawing().find_object(handle) ? handle : "0";
}

void write_section_object(RecordWriter& w, const Entity& e, const std::string& owner) {
    const SectionObjectData& d = e.as_section_object();
    if (w.older() && e.raw.empty()) {
        w.fail("SECTIONOBJECT in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    // The indicator: made from the section when it is new or of AutoCAD's shape (it follows what changed: a vertex, a
    // height, the layer, the color), else the bytes as read; none before 2000 and with $PROXYGRAPHICS 0
    std::vector<uint8_t> made;
    const std::vector<uint8_t>* graphics = nullptr;
    if (!w.older() && w.proxy_graphics_enabled()) {
        if (d.proxy_graphics.empty() || section_graphics_standard(d.proxy_graphics)) {
            bool defpoints = true;
            made = section_indicator_graphics(d, e.props, w.proxy_tables(), &defpoints);
            if (!defpoints && w.filtering()) {
                w.warn("SECTIONOBJECT " + e.props.handle + ": the drawing has no layer Defpoints, the indicator's outline is numbered as layer 0");
            }
            if (!made.empty()) graphics = &made;
        } else {
            graphics = &d.proxy_graphics;
        }
    }
    w.entity_head("SECTIONOBJECT", e, owner, graphics);
    w.subclass("AcDbSection");
    w.integer(90, d.state);
    w.integer(91, d.flags);
    w.text(1, d.name);
    w.point3(10, d.vertical_direction);
    w.real(40, d.top_height);
    w.real(41, d.bottom_height);
    w.integer(70, d.indicator_transparency);
    // 2000's record has no color (AutoCAD's 2000 files); 2004 and later 62, 9 by default
    if (w.version() >= DxfVersion::R2004) w.integer(62, d.indicator_color.value_or(9));
    w.integer(92, static_cast<int64_t>(d.vertices.size()));
    for (const Point3& p : d.vertices) w.point3(11, p);
    w.integer(93, static_cast<int64_t>(d.back_vertices.size()));
    for (const Point3& p : d.back_vertices) w.point3(12, p);
    const std::string settings = object_pointer(w, d.settings_handle);
    if (settings == "0" && !d.settings_handle.empty() && d.settings_handle != "0") {
        w.warn("SECTIONOBJECT " + e.props.handle + ": the settings " + d.settings_handle + " no longer exist, set to none (0)");
    }
    w.handle(360, settings);
    w.xdata(e.props.xdata);
}

void section_references(const Entity& e, std::vector<TypedReference>& out) {
    const std::string& h = e.as_section_object().settings_handle;
    if (!h.empty() && h != "0") out.push_back({h, RefKind::HardOwner, false});
}

// What AutoCAD draws: the lines of the indicator (a plane's vertices; a volume's top and bottom too), in the WCS
void section_extents(const Entity& e, TypedExtentsSink& sink) {
    const SectionLines lines = section_lines(e.as_section_object());
    for (const auto* group : {&lines.primary, &lines.secondary}) {
        for (const auto& line : *group) {
            for (const Point3& p : line) sink.add_wcs(p);
        }
    }
}

std::vector<RefDescriptor> section_refs() {
    RefDescriptor settings;
    settings.subclass = "AcDbSection";
    settings.code = 360;
    settings.kind = RefKind::HardOwner;
    settings.removal = RefRemoval::ZeroInPlace;
    return {settings};
}

// The vertices and the back vertices are two lists, each with its count
std::vector<ListSpec> section_lists() {
    ListSpec vertices;
    vertices.subclass = "AcDbSection";
    vertices.id = 1;
    vertices.shape = ListShape::Codes;
    vertices.codes = {92, 11, 21, 31};
    ListSpec back;
    back.subclass = "AcDbSection";
    back.id = 2;
    back.shape = ListShape::Codes;
    back.codes = {93, 12, 22, 32};
    return {vertices, back};
}

// ---------------------------------------------------------------------------
// SECTION_MANAGER
// ---------------------------------------------------------------------------

bool read_section_manager(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbSectionManager");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbSectionManager subclass");
    SectionManagerData d;
    c.boolean(70, d.needs_update);
    size_t n = 0;
    if (c.count(90, 1, n)) {
        d.sections.reserve(n);
        for (size_t i = 0; i < n && c.ok(); ++i) {
            std::string handle;
            c.require_handle(330, handle);
            d.sections.push_back(std::move(handle));
        }
    }
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = std::move(d);
    return true;
}

void write_section_manager(RecordWriter& w, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail("SECTION_MANAGER in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    const SectionManagerData& d = o.as_section_manager();
    w.object_head("SECTION_MANAGER", o);
    w.subclass("AcDbSectionManager");
    w.boolean(70, d.needs_update);
    // The sections that are still there (B19): the count follows. AutoCAD 2025 writes no count for a manager without any
    std::vector<std::string> live;
    for (const std::string& h : d.sections) {
        if (!h.empty() && h != "0" && w.live(h)) live.push_back(h);
    }
    if (live.size() != d.sections.size() && w.filtering()) {
        w.warn("SECTION_MANAGER " + o.handle + ": " + std::to_string(d.sections.size() - live.size()) + " section(s) no longer exist, left out");
    }
    if (!live.empty()) {
        w.integer(90, static_cast<int64_t>(live.size()));
        for (const std::string& h : live) w.handle(330, h);
    }
    w.xdata(o.xdata);
}

void manager_references(const Object& o, std::vector<TypedReference>& out) {
    for (const std::string& h : o.as_section_manager().sections) {
        if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
    }
}

std::vector<RefDescriptor> manager_refs() {
    RefDescriptor section;
    section.subclass = "AcDbSectionManager";
    section.code = 330;
    section.kind = RefKind::SoftPointer;
    section.removal = RefRemoval::DropMember;
    section.counter_code = 90;
    return {section};
}

std::vector<ListSpec> manager_lists() {
    ListSpec sections;
    sections.subclass = "AcDbSectionManager";
    sections.id = 1;
    sections.shape = ListShape::Codes;
    sections.codes = {90, 330};
    return {sections};
}

// ---------------------------------------------------------------------------
// SECTION_SETTINGS
// ---------------------------------------------------------------------------

// A marker group: code `code` with the text the reference gives it
bool marker(GroupCursor& c, int code, const char* text) {
    std::string value;
    if (!c.string(code, value)) {
        if (c.ok()) c.fail(std::string("the marker ") + text + " (group " + std::to_string(code) + ") is missing");
        return false;
    }
    if (value != text) {
        c.fail(std::string("the marker ") + text + " (group " + std::to_string(code) + ") reads \"" + value + "\"");
        return false;
    }
    return true;
}

bool read_geometry(GroupCursor& c, SectionGeometrySettings& g) {
    if (!marker(c, 2, kGeometryMarker)) return false;
    c.integer(90, g.section_type);
    c.integer(91, g.geometry_count);
    c.integer(92, g.flags);
    c.integer(62, g.color);
    c.string(8, g.layer);
    c.string(6, g.linetype);
    c.real(40, g.linetype_scale);
    c.string(1, g.plot_style);
    c.integer(370, g.lineweight);
    c.integer(70, g.face_transparency);
    c.integer(71, g.edge_transparency);
    c.integer(72, g.hatch_type);
    c.string(2, g.hatch_name);
    c.real(41, g.hatch_angle);
    c.real(42, g.hatch_scale);
    c.real(43, g.hatch_spacing);
    return c.ok() && marker(c, 3, kGeometryEnd);
}

bool read_type_settings(GroupCursor& c, SectionTypeSettings& t) {
    if (!marker(c, 1, kTypeMarker)) return false;
    c.integer(90, t.section_type);
    c.integer(91, t.generation_flags);
    size_t sources = 0;
    if (c.count(92, 1, sources)) {
        t.sources.reserve(sources);
        for (size_t i = 0; i < sources && c.ok(); ++i) {
            std::string handle;
            c.require_handle(330, handle);
            t.sources.push_back(std::move(handle));
        }
    }
    c.handle(331, t.destination_block);
    c.string(1, t.destination_file);
    size_t geometries = 0;
    if (c.ok() && c.count(93, 18, geometries)) {
        t.geometries.reserve(geometries);
        for (size_t i = 0; i < geometries && c.ok(); ++i) {
            SectionGeometrySettings g;
            if (!read_geometry(c, g)) return false;
            t.geometries.push_back(std::move(g));
        }
    }
    return c.ok() && marker(c, 3, kTypeEnd);
}

bool read_section_settings(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbSectionSettings");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbSectionSettings subclass");
    SectionSettingsData d;
    c.integer(90, d.section_type);
    size_t n = 0;
    if (c.count(91, 8, n)) {
        d.types.reserve(n);
        for (size_t i = 0; i < n && c.ok(); ++i) {
            SectionTypeSettings t;
            if (!read_type_settings(c, t)) break;
            d.types.push_back(std::move(t));
        }
    }
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    note_rest(c, ctx, view);
    o.data = Boxed<SectionSettingsData>(std::move(d));
    return true;
}

// A pointer in the settings: the handle, 0 for none; a record that is gone gives 0 (B19), a source that is gone goes
std::string pointer_text(const RecordWriter& w, const std::string& handle) {
    if (handle.empty() || handle == "0") return "0";
    return w.live(handle) ? handle : "0";
}

void write_section_settings(RecordWriter& w, const Object& o) {
    if (w.older() && o.raw.empty()) {
        w.fail("SECTION_SETTINGS in DXF R13 or R14 is not established: no AutoCAD file has it (a record read from a file is kept as it was)");
        return;
    }
    const SectionSettingsData& d = o.as_section_settings();
    w.object_head("SECTION_SETTINGS", o);
    w.subclass("AcDbSectionSettings");
    w.integer(90, d.section_type);
    w.integer(91, static_cast<int64_t>(d.types.size()));
    for (const SectionTypeSettings& t : d.types) {
        w.text(1, kTypeMarker);
        w.integer(90, t.section_type);
        w.integer(91, t.generation_flags);
        std::vector<std::string> sources;
        for (const std::string& h : t.sources) {
            if (!h.empty() && h != "0" && w.live(h)) sources.push_back(h);
        }
        if (sources.size() != t.sources.size() && w.filtering()) {
            w.warn("SECTION_SETTINGS " + o.handle + ": " + std::to_string(t.sources.size() - sources.size()) + " source object(s) no longer exist, left out");
        }
        w.integer(92, static_cast<int64_t>(sources.size()));
        for (const std::string& h : sources) w.handle(330, h);
        const std::string block = pointer_text(w, t.destination_block);
        if (block == "0" && !t.destination_block.empty() && t.destination_block != "0" && w.filtering()) {
            w.warn("SECTION_SETTINGS " + o.handle + ": the destination block " + t.destination_block + " no longer exists, set to none (0)");
        }
        w.handle(331, block);
        w.text(1, t.destination_file);
        w.integer(93, static_cast<int64_t>(t.geometries.size()));
        for (const SectionGeometrySettings& g : t.geometries) {
            w.text(2, kGeometryMarker);
            w.integer(90, g.section_type);
            w.integer(91, g.geometry_count);
            w.integer(92, g.flags);
            w.integer(62, g.color);
            w.text(8, g.layer);
            w.text(6, g.linetype);
            w.real(40, g.linetype_scale);
            w.text(1, g.plot_style);
            w.integer(370, g.lineweight);
            w.integer(70, g.face_transparency);
            w.integer(71, g.edge_transparency);
            w.integer(72, g.hatch_type);
            w.text(2, g.hatch_name);
            w.real(41, g.hatch_angle);
            w.real(42, g.hatch_scale);
            w.real(43, g.hatch_spacing);
            w.text(3, kGeometryEnd);
        }
        w.text(3, kTypeEnd);
    }
    w.xdata(o.xdata);
}

void settings_references(const Object& o, std::vector<TypedReference>& out) {
    for (const SectionTypeSettings& t : o.as_section_settings().types) {
        for (const std::string& h : t.sources) {
            if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
        }
        if (!t.destination_block.empty() && t.destination_block != "0") {
            out.push_back({t.destination_block, RefKind::SoftPointer, false});
        }
    }
}

std::vector<RefDescriptor> settings_refs() {
    RefDescriptor source;
    source.subclass = "AcDbSectionSettings";
    source.code = 330;
    source.kind = RefKind::SoftPointer;
    source.removal = RefRemoval::DropMember;
    source.counter_code = 92;
    RefDescriptor block;
    block.subclass = "AcDbSectionSettings";
    block.code = 331;
    block.kind = RefKind::SoftPointer;
    block.removal = RefRemoval::ZeroInPlace;
    return {source, block};
}

// The nested settings are one unit: the markers, the counts and the repeated groups mean what their place says
std::vector<ListSpec> settings_lists() {
    ListSpec all;
    all.subclass = "AcDbSectionSettings";
    all.id = 1;
    all.shape = ListShape::WholeSubclass;
    return {all};
}

// ---------------------------------------------------------------------------
// Classes and versions
// ---------------------------------------------------------------------------

ClassSpec class_of(const char* cpp_name, int flags) {
    ClassSpec c;
    c.cpp_name = cpp_name;
    c.application = "ObjectDBX Classes";
    c.proxy_flags = flags;
    c.in_older = false;
    return c;
}

// 2000 ... 2018: AutoCAD's own files of every version, ASCII and binary, have the records (R12's files have none: AutoCAD
// drops them); R13 and R14 cannot be saved by AutoCAD 2025 (NotSavable): a new record is refused there, ej testat
VersionMatrix section_matrix() {
    VersionMatrix m = VersionMatrix::native_from(DxfVersion::R2000);
    // R13 and R14: no file shows whether they hold the records, nor their form there (Codex 9.S, 13): not established, not a
    // version error
    for (DxfVersion v : {DxfVersion::R13, DxfVersion::R14}) {
        m.set(v, Generation::None, ErrorBasis::NotEstablished);
        m.autocad(v, AutoCadStatus::NotSavable);
    }
    for (DxfVersion v : {DxfVersion::R12, DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                         DxfVersion::R2013, DxfVersion::R2018}) {
        m.autocad(v, AutoCadStatus::Verified);
    }
    return m;
}

} // namespace

void register_section_codecs(TypedRegistry& registry) {
    // The order of AutoCAD's own CLASSES: the entity, the manager, the settings
    {
        EntityCodec c;
        c.dxf_name = "SECTIONOBJECT";
        c.type = EntityType::SectionObject;
        c.read = read_section_object;
        c.write = write_section_object;
        c.references = section_references;
        c.extents = section_extents;
        c.cls = class_of("AcDbSection", 1025);
        c.versions = section_matrix();
        c.refs = section_refs();
        c.lists = section_lists();
        c.step = 12;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "SECTION_MANAGER";
        c.type = ObjectType::SectionManager;
        c.read = read_section_manager;
        c.write = write_section_manager;
        c.references = manager_references;
        c.cls = class_of("AcDbSectionManager", 1024);
        c.versions = section_matrix();
        c.refs = manager_refs();
        c.lists = manager_lists();
        c.step = 12;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "SECTION_SETTINGS";
        c.type = ObjectType::SectionSettings;
        c.read = read_section_settings;
        c.write = write_section_settings;
        c.references = settings_references;
        c.cls = class_of("AcDbSectionSettings", 1024);
        c.versions = section_matrix();
        c.refs = settings_refs();
        c.lists = settings_lists();
        c.step = 12;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
