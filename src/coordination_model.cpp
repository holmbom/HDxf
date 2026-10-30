#include "TypedRecordTypes.hpp"
#include <string>
#include <vector>

// The coordination model and its definition (phase 9, step 9.6; plan 2026-10-27-fas-9-typer.md, "Xref och coordination
// model"; phase 6, B78): COORDINATION_MODEL (AcDbNavisworksModel) and NAVISWORKSMODELDEF (AcDbNavisworksModelDef, in
// ACAD_BIM_DEFINITIONS). The data types are the ones phase 6 made (CoordinationModelData, NavisworksModelDefData); since
// step 9.6 they are read and written by codecs of the registry like every other typed record, replacing the reader and
// writer functions that were here before (the same groups in the same order, checked against AutoCAD's core in phase 6).
//
// Groups, as Autodesk's 2018 reference has them plus the 70 AutoCAD's core requires after each subclass marker:
//   entity      AcDbNavisworksModel 70, 340 (the definition), 40 x 16 (the transform, row by row, the translation in the
//               fourth column), 40 (the insertion unit factor: the 17th)
//   definition  AcDbNavisworksModelDef 70, 1 (the file), 290 (persistent load), 10/20/30 (minimum extents), 11/21/31
//               (maximum), 290 (host drawing geometry visible): the two 290 are told apart by their place
// No AutoCAD file in the corpus has a coordination model: AutoCAD 2025's core rejects a file that is not a real Navisworks
// model ("does not point to a valid model file") and has no NWCOUT, so the whole family is ej testat against an AutoCAD
// save (F9.23). The model's graphics are Navisworks' own: a model read from a file keeps its proxy graphics as bytes, HDxf
// makes none, and its extents are the definition's box through the transform.

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

bool read_coordination_model(const RecordView& view, TypedReadContext& ctx, Entity& e) {
    GroupCursor c(view, "AcDbNavisworksModel");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbNavisworksModel subclass");
    CoordinationModelData d;
    int16_t version = static_cast<int16_t>(d.version);
    c.integer(70, version);
    c.handle(340, d.definition_handle);
    for (double& v : d.matrix) c.require_real(40, v);
    c.real(40, d.unit_factor);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    d.version = version;
    note_rest(c, ctx, view);
    EntityProperties scratch;
    std::string error;
    if (!read_entity_properties(view, scratch, &d.proxy_graphics, error)) return ctx.fail(error);
    e.data = Boxed<CoordinationModelData>(std::move(d));
    return true;
}

// A pointer to an object: the handle when the object is there (a drawing built in memory knows what was removed
// from it only by looking: every handle is "live" there), 0 when it is gone (B19). While the drawing as it was read
// is written for comparison the handle stays as it was read
std::string object_pointer(const RecordWriter& w, const std::string& handle) {
    if (handle.empty() || handle == "0") return "0";
    if (!w.filtering()) return handle;
    return w.live(handle) && w.drawing().find_object(handle) ? handle : "0";
}

void write_coordination_model(RecordWriter& w, const Entity& e, const std::string& owner) {
    const CoordinationModelData& d = e.as_coordination_model();
    const std::vector<uint8_t>* graphics =
        d.proxy_graphics.empty() || w.older() || !w.proxy_graphics_enabled() ? nullptr : &d.proxy_graphics;
    w.entity_head("COORDINATION_MODEL", e, owner, graphics);
    w.subclass("AcDbNavisworksModel");
    w.integer(70, d.version);
    const std::string definition = object_pointer(w, d.definition_handle);
    if (definition == "0" && !d.definition_handle.empty() && d.definition_handle != "0") {
        w.warn("COORDINATION_MODEL " + e.props.handle + ": the definition " + d.definition_handle + " no longer exists, set to none (0)");
    }
    w.handle(340, definition);
    for (double v : d.matrix) w.real(40, v);
    w.real(40, d.unit_factor);
    w.xdata(e.props.xdata);
}

void coordination_references(const Entity& e, std::vector<TypedReference>& out) {
    const std::string& h = e.as_coordination_model().definition_handle;
    if (!h.empty() && h != "0") out.push_back({h, RefKind::HardPointer, false});
}

// What AutoCAD draws is the model; HDxf has the definition's box (its minimum and maximum, in the WCS), the eight corners
// of it through the model's transform (B78); the unit factor is not part of it
void coordination_extents(const Entity& e, TypedExtentsSink& sink) {
    const CoordinationModelData& d = e.as_coordination_model();
    if (!sink.drawing || d.definition_handle.empty() || d.definition_handle == "0") return;
    const Object* def = sink.drawing->find_object(d.definition_handle);
    if (!def || def->type != ObjectType::NavisworksModelDef) return;
    const NavisworksModelDefData& b = def->as_navisworks_model_def();
    for (int i = 0; i < 8; ++i) {
        const double x = (i & 1) ? b.max.x : b.min.x;
        const double y = (i & 2) ? b.max.y : b.min.y;
        const double z = (i & 4) ? b.max.z : b.min.z;
        const auto& m = d.matrix;
        sink.add_wcs(Point3{m[0] * x + m[1] * y + m[2] * z + m[3],
                            m[4] * x + m[5] * y + m[6] * z + m[7],
                            m[8] * x + m[9] * y + m[10] * z + m[11]});
    }
}

bool read_navisworks_model_def(const RecordView& view, TypedReadContext& ctx, Object& o) {
    GroupCursor c(view, "AcDbNavisworksModelDef");
    if (!c.valid()) return ctx.fail(describe_record(view) + " has no AcDbNavisworksModelDef subclass");
    NavisworksModelDefData d;
    int16_t version = static_cast<int16_t>(d.version);
    c.integer(70, version);
    c.string(1, d.path);
    c.boolean(290, d.load);
    c.point3(10, d.min);
    c.point3(11, d.max);
    c.boolean(290, d.host_visible);
    if (!c.ok()) return ctx.fail(describe_record(view) + ": " + c.error());
    d.version = version;
    note_rest(c, ctx, view);
    o.data = std::move(d);
    return true;
}

void write_navisworks_model_def(RecordWriter& w, const Object& o) {
    const NavisworksModelDefData& d = o.as_navisworks_model_def();
    w.object_head("NAVISWORKSMODELDEF", o);
    w.subclass("AcDbNavisworksModelDef");
    w.integer(70, d.version);
    w.text(1, d.path);
    w.boolean(290, d.load);
    w.point3(10, d.min);
    w.point3(11, d.max);
    w.boolean(290, d.host_visible);
    w.xdata(o.xdata);
}

std::vector<RefDescriptor> coordination_refs() {
    RefDescriptor definition;
    definition.subclass = "AcDbNavisworksModel";
    definition.code = 340;
    definition.kind = RefKind::HardPointer;
    definition.removal = RefRemoval::ZeroInPlace;
    return {definition};
}

// 2013 on (the references of 2013 and 2018 have them; before, an error: B78). No AutoCAD file has a record: ej testat
VersionMatrix coordination_matrix() { return VersionMatrix::native_from(DxfVersion::R2013); }

} // namespace

void register_coordination_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "NAVISWORKSMODELDEF";
        c.type = ObjectType::NavisworksModelDef;
        c.read = read_navisworks_model_def;
        c.write = write_navisworks_model_def;
        // AutoCAD's core (AcBIMUnderlayDbx), as the writer's built-in class list has had it since B78
        c.cls.cpp_name = "AcDbNavisworksModelDef";
        c.cls.application = "AcBIMUnderlayDbx";
        c.cls.proxy_flags = 1153;
        c.versions = coordination_matrix();
        c.step = 6;
        registry.add(std::move(c));
    }
    {
        EntityCodec c;
        c.dxf_name = "COORDINATION_MODEL";
        c.type = EntityType::CoordinationModel;
        c.read = read_coordination_model;
        c.write = write_coordination_model;
        c.references = coordination_references;
        c.extents = coordination_extents;
        c.cls.cpp_name = "AcDbNavisworksModel";
        c.cls.application = "AcBIMUnderlayDbx";
        c.cls.proxy_flags = 1025;
        c.versions = coordination_matrix();
        c.refs = coordination_refs();
        c.step = 6;
        registry.add(std::move(c));
    }
    // The reference's names for the two (Autodesk's 2018 DXF reference calls the entity "Coordination Model" and the
    // definition "AcDbNavisworksModelDef"), read as the types they are
    registry.add_alias("Coordination Model", "COORDINATION_MODEL");
    registry.add_alias("AcDbNavisworksModelDef", "NAVISWORKSMODELDEF");
}

} // namespace internal
} // namespace hdxf
