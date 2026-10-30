#pragma once

#include "DynamicBlockTypes.hpp"
#include <string>
#include <vector>

// The remaining dynamic block records (phase 9, step 9.13; plan 2026-10-27-fas-9-typer.md, F9.9, B109): the parameters, grips,
// actions and constraints that step 9.9 did not type. Internal.
//
// Every type is a "spec": a struct that names the record (`name`, `cpp_name`), its type and data, the subclass of its own
// (`subclass`, null when the record has none) and how that subclass is read and written (`read_own`, `write_own`). The generic
// functions below read and write the subclasses the records share (AcDbEvalExpr, AcDbBlockElement and then, by the kind of the
// record, AcDbBlockParameter + AcDbBlock1PtParameter or AcDbBlock2PtParameter, AcDbBlockGrip, AcDbBlockAction [+
// AcDbBlockActionWithBasePt], AcDbBlockConstraintParameter) with step 9.9's bases (DynamicBlockTypes.hpp). A spec's `read_own` takes
// the groups of its subclass in AutoCAD's order from the cursor (`require_*`: a missing group fails the record, which stays as
// read, B105); `write_own` writes them. The codecs of the kinds are made by make_codec().

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// The shared parts of the step (dynamic_block_parameters.cpp)
// ---------------------------------------------------------------------------

// A value set: the flags (`flags_code`), three limits (`first_limit_code` and the two codes after it), the count (`count_code`) and
// the values (`value_code`; 0: the set has no value group in AutoCAD's files, so a count above 0 fails the record)
void read_value_set(GroupCursor& c, int flags_code, int first_limit_code, int count_code, int value_code, BlockValueSet& set);
void write_value_set(RecordWriter& w, int flags_code, int first_limit_code, int count_code, int value_code, const BlockValueSet& set);

// The own part of the distance family: 305, 306, 140, 307 and the value set 96, 141 … 143, 175, 144
void read_distance_part(GroupCursor& c, BlockDistancePart& d);
void write_distance_part(RecordWriter& w, const BlockDistancePart& d);

// A 2D point (1011 + 1021) of a frame, the vector of a grip (140 141 142), a link of a property (id, name) written in groups of
// the codes asked for
void write_frame_points(RecordWriter& w, int count_code, int point_code, const std::vector<Point2>& points);
bool read_frame_points(GroupCursor& c, int count_code, int point_code, std::vector<Point2>& points);
void read_vector_140(GroupCursor& c, Point3& v);
void write_vector_140(RecordWriter& w, const Point3& v);
// Two or six links written as AutoCAD writes them: the id and then the name of each (92 301, 93 302 …) or all ids and then all names
void read_links_interleaved(GroupCursor& c, int first_id_code, int first_name_code, BlockPropertyLink* links, size_t n);
void write_links_interleaved(RecordWriter& w, int first_id_code, int first_name_code, const BlockPropertyLink* links, size_t n);
void read_links_grouped(GroupCursor& c, int first_id_code, int first_name_code, BlockPropertyLink* links, size_t n);
void write_links_grouped(RecordWriter& w, int first_id_code, int first_name_code, const BlockPropertyLink* links, size_t n);

// A record of step 9.13 in DXF R13/R14: no AutoCAD file shows the form there (fails the record, which stays as read)
inline bool dynamic_record_too_old(const RecordView& view, TypedReadContext& ctx) {
    if (ctx.version >= DxfVersion::R2000) return false;
    ctx.fail(describe_record(view) + " in DXF R13/R14: no AutoCAD file shows the form there");
    return true;
}

// The ObjectCodec of a spec with its class, its version matrix (2000 … 2018 native, R12 the named error, R13/R14 not claimed;
// `autocad_verified` false for a type no AutoCAD file has) and the step
ObjectCodec dynamic_codec(const char* dxf_name, ObjectType type, ObjectReadFn read, ObjectWriteFn write, const char* cpp_name,
                          bool autocad_verified = true, int proxy_flags = 1153);

// ---------------------------------------------------------------------------
// Generic reading by the kind of the record
// ---------------------------------------------------------------------------

template <class D>
bool read_element_head(const RecordView& view, TypedReadContext& ctx, D& d) {
    if (!read_subclass(view, ctx, "AcDbEvalExpr", [&](GroupCursor& c) { read_eval_expr(c, d.eval); })) return false;
    return read_subclass(view, ctx, "AcDbBlockElement", [&](GroupCursor& c) { read_block_element(c, d.element); });
}

template <class Spec>
bool read_own_subclass(const RecordView& view, TypedReadContext& ctx, typename Spec::Data& d) {
    if (Spec::subclass == nullptr) return true;
    return read_subclass(view, ctx, Spec::subclass, [&](GroupCursor& c) { Spec::read_own(c, d); });
}

template <class Spec>
bool read_one_point(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (dynamic_record_too_old(view, ctx)) return false;
    typename Spec::Data d;
    if (!read_element_head(view, ctx, d)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockParameter", [&](GroupCursor& c) { read_block_parameter(c, d.parameter); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlock1PtParameter", [&](GroupCursor& c) { read_block_1pt_parameter(c, d.one_point); })) return false;
    if (!read_own_subclass<Spec>(view, ctx, d)) return false;
    o.data = Boxed<typename Spec::Data>(std::move(d));
    return true;
}

template <class Spec>
bool read_two_point(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (dynamic_record_too_old(view, ctx)) return false;
    typename Spec::Data d;
    if (!read_element_head(view, ctx, d)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockParameter", [&](GroupCursor& c) { read_block_parameter(c, d.parameter); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlock2PtParameter", [&](GroupCursor& c) { read_block_2pt_parameter(c, d.two_point); })) return false;
    if (!read_own_subclass<Spec>(view, ctx, d)) return false;
    o.data = Boxed<typename Spec::Data>(std::move(d));
    return true;
}

template <class Spec>
bool read_grip(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (dynamic_record_too_old(view, ctx)) return false;
    typename Spec::Data d;
    if (!read_element_head(view, ctx, d)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockGrip", [&](GroupCursor& c) { read_block_grip(c, d.grip); })) return false;
    if (!read_own_subclass<Spec>(view, ctx, d)) return false;
    o.data = Boxed<typename Spec::Data>(std::move(d));
    return true;
}

template <class Spec>
bool read_action(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (dynamic_record_too_old(view, ctx)) return false;
    typename Spec::Data d;
    if (!read_element_head(view, ctx, d)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockAction", [&](GroupCursor& c) { read_block_action(c, d.action); })) return false;
    if constexpr (Spec::with_base_point) {
        if (!read_subclass(view, ctx, "AcDbBlockActionWithBasePt", [&](GroupCursor& c) {
                read_block_action_base_point(c, d.base_point_action);
            })) {
            return false;
        }
    }
    if (!read_own_subclass<Spec>(view, ctx, d)) return false;
    o.data = Boxed<typename Spec::Data>(std::move(d));
    return true;
}

// A constraint parameter: the 2Pt parameter, AcDbBlockConstraintParameter (330), the distance subclass and, for the horizontal,
// vertical and aligned constraints, a last subclass of their own without a group
template <class Spec>
bool read_constraint(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (dynamic_record_too_old(view, ctx)) return false;
    typename Spec::Data d;
    if (!read_element_head(view, ctx, d)) return false;
    if (!read_subclass(view, ctx, "AcDbBlockParameter", [&](GroupCursor& c) { read_block_parameter(c, d.parameter); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlock2PtParameter", [&](GroupCursor& c) { read_block_2pt_parameter(c, d.two_point); })) return false;
    if (!read_subclass(view, ctx, "AcDbBlockConstraintParameter", [&](GroupCursor& c) { c.require_handle(330, d.dependency); })) return false;
    if (!read_subclass(view, ctx, Spec::distance_subclass, [&](GroupCursor& c) { read_distance_part(c, d.distance); })) return false;
    if (Spec::subclass != nullptr && !read_subclass(view, ctx, Spec::subclass, [](GroupCursor&) {})) return false;
    o.data = Boxed<typename Spec::Data>(std::move(d));
    return true;
}

// ---------------------------------------------------------------------------
// Generic writing by the kind of the record
// ---------------------------------------------------------------------------

template <class Spec>
void write_own_subclass(RecordWriter& w, const typename Spec::Data& d) {
    if (Spec::subclass == nullptr) return;
    w.subclass(Spec::subclass);
    Spec::write_own(w, d);
}

template <class D>
void write_element_head(RecordWriter& w, const D& d) {
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, d.eval);
    w.subclass("AcDbBlockElement");
    write_block_element(w, d.element);
}

template <class Spec>
void write_one_point(RecordWriter& w, const Object& o) {
    const typename Spec::Data& d = Spec::get(o);
    w.object_head(Spec::name, o);
    write_element_head(w, d);
    w.subclass("AcDbBlockParameter");
    write_block_parameter(w, d.parameter);
    w.subclass("AcDbBlock1PtParameter");
    write_block_1pt_parameter(w, d.one_point);
    write_own_subclass<Spec>(w, d);
    w.xdata(o.xdata);
}

template <class Spec>
void write_two_point(RecordWriter& w, const Object& o) {
    const typename Spec::Data& d = Spec::get(o);
    w.object_head(Spec::name, o);
    write_element_head(w, d);
    w.subclass("AcDbBlockParameter");
    write_block_parameter(w, d.parameter);
    w.subclass("AcDbBlock2PtParameter");
    write_block_2pt_parameter(w, d.two_point);
    write_own_subclass<Spec>(w, d);
    w.xdata(o.xdata);
}

template <class Spec>
void write_grip(RecordWriter& w, const Object& o) {
    const typename Spec::Data& d = Spec::get(o);
    w.object_head(Spec::name, o);
    write_element_head(w, d);
    w.subclass("AcDbBlockGrip");
    write_block_grip(w, d.grip);
    write_own_subclass<Spec>(w, d);
    w.xdata(o.xdata);
}

template <class Spec>
void write_action(RecordWriter& w, const Object& o) {
    const typename Spec::Data& d = Spec::get(o);
    w.object_head(Spec::name, o);
    write_element_head(w, d);
    w.subclass("AcDbBlockAction");
    write_block_action(w, d.action);
    if constexpr (Spec::with_base_point) {
        w.subclass("AcDbBlockActionWithBasePt");
        write_block_action_base_point(w, d.base_point_action);
    }
    write_own_subclass<Spec>(w, d);
    w.xdata(o.xdata);
}

// The pointer of a constraint to its dependency (330): "0" when that record is gone (B19)
void write_dependency_pointer(RecordWriter& w, const std::string& handle, const std::string& record);

template <class Spec>
void write_constraint(RecordWriter& w, const Object& o) {
    const typename Spec::Data& d = Spec::get(o);
    w.object_head(Spec::name, o);
    write_element_head(w, d);
    w.subclass("AcDbBlockParameter");
    write_block_parameter(w, d.parameter);
    w.subclass("AcDbBlock2PtParameter");
    write_block_2pt_parameter(w, d.two_point);
    w.subclass("AcDbBlockConstraintParameter");
    write_dependency_pointer(w, d.dependency, o.handle);
    w.subclass(Spec::distance_subclass);
    write_distance_part(w, d.distance);
    if (Spec::subclass != nullptr) w.subclass(Spec::subclass);
    w.xdata(o.xdata);
}

// ---------------------------------------------------------------------------
// The specs (their read_own and write_own: dynamic_block_parameters.cpp reads, dxf_writer_dynamic_types.cpp writes)
// ---------------------------------------------------------------------------

#define HDXF_BLOCK_SPEC(SPEC, DATA, DXF, ENUM, ACCESSOR, SUBCLASS, CPP)                                        \
    struct SPEC {                                                                                              \
        using Data = DATA;                                                                                     \
        static constexpr const char* name = DXF;                                                               \
        static constexpr const char* cpp_name = CPP;                                                           \
        static constexpr const char* subclass = SUBCLASS;                                                      \
        static constexpr ObjectType type = ObjectType::ENUM;                                                   \
        static constexpr bool with_base_point = false;                                                         \
        static const Data& get(const Object& o) { return o.ACCESSOR(); }                                       \
        static void read_own(GroupCursor& c, Data& d);                                                         \
        static void write_own(RecordWriter& w, const Data& d);                                                 \
    }

// parameters
HDXF_BLOCK_SPEC(AlignmentParameterSpec, BlockAlignmentParameterData, "BLOCKALIGNMENTPARAMETER", BlockAlignmentParameter,
                as_block_alignment_parameter, "AcDbBlockAlignmentParameter", "AcDbBlockAlignmentParameter");
HDXF_BLOCK_SPEC(BasepointParameterSpec, BlockBasepointParameterData, "BLOCKBASEPOINTPARAMETER", BlockBasepointParameter,
                as_block_basepoint_parameter, "AcDbBlockBasepointParameter", "AcDbBlockBasepointParameter");
HDXF_BLOCK_SPEC(FlipParameterSpec, BlockFlipParameterData, "BLOCKFLIPPARAMETER", BlockFlipParameter, as_block_flip_parameter,
                "AcDbBlockFlipParameter", "AcDbBlockFlipParameter");
HDXF_BLOCK_SPEC(LookupParameterSpec, BlockLookupParameterData, "BLOCKLOOKUPPARAMETER", BlockLookupParameter,
                as_block_lookup_parameter, "AcDbBlockLookUpParameter", "AcDbBlockLookupParameter");
HDXF_BLOCK_SPEC(PointParameterSpec, BlockPointParameterData, "BLOCKPOINTPARAMETER", BlockPointParameter, as_block_point_parameter,
                "AcDbBlockPointParameter", "AcDbBlockPointParameter");
HDXF_BLOCK_SPEC(PolarParameterSpec, BlockPolarParameterData, "BLOCKPOLARPARAMETER", BlockPolarParameter, as_block_polar_parameter,
                "AcDbBlockPolarParameter", "AcDbBlockPolarParameter");
HDXF_BLOCK_SPEC(PropertiesTableSpec, BlockPropertiesTableData, "BLOCKPROPERTIESTABLE", BlockPropertiesTable,
                as_block_properties_table, "AcDbBlockPropertiesTable", "AcDbBlockPropertiesTable");
HDXF_BLOCK_SPEC(RotationParameterSpec, BlockRotationParameterData, "BLOCKROTATIONPARAMETER", BlockRotationParameter,
                as_block_rotation_parameter, "AcDbBlockRotationParameter", "AcDbBlockRotationParameter");
HDXF_BLOCK_SPEC(UserParameterSpec, BlockUserParameterData, "BLOCKUSERPARAMETER", BlockUserParameter, as_block_user_parameter,
                "AcDbBlockUserParameter", "AcDbBlockUserParameter");
HDXF_BLOCK_SPEC(XYParameterSpec, BlockXYParameterData, "BLOCKXYPARAMETER", BlockXYParameter, as_block_xy_parameter,
                "AcDbBlockXYParameter", "AcDbBlockXYParameter");

// grips
HDXF_BLOCK_SPEC(AlignmentGripSpec, BlockAlignmentGripData, "BLOCKALIGNMENTGRIP", BlockAlignmentGrip, as_block_alignment_grip,
                "AcDbBlockAlignmentGrip", "AcDbBlockAlignmentGrip");
HDXF_BLOCK_SPEC(FlipGripSpec, BlockFlipGripData, "BLOCKFLIPGRIP", BlockFlipGrip, as_block_flip_grip, "AcDbBlockFlipGrip",
                "AcDbBlockFlipGrip");
HDXF_BLOCK_SPEC(LookupGripSpec, BlockLookupGripData, "BLOCKLOOKUPGRIP", BlockLookupGrip, as_block_lookup_grip,
                "AcDbBlockLookUpGrip", "AcDbBlockLookupGrip");
HDXF_BLOCK_SPEC(PolarGripSpec, BlockPolarGripData, "BLOCKPOLARGRIP", BlockPolarGrip, as_block_polar_grip, "AcDbBlockPolarGrip",
                "AcDbBlockPolarGrip");
HDXF_BLOCK_SPEC(PropertiesTableGripSpec, BlockPropertiesTableGripData, "BLOCKPROPERTIESTABLEGRIP", BlockPropertiesTableGrip,
                as_block_properties_table_grip, nullptr, "AcDbBlockPropertiesTableGrip");
HDXF_BLOCK_SPEC(RotationGripSpec, BlockRotationGripData, "BLOCKROTATIONGRIP", BlockRotationGrip, as_block_rotation_grip,
                "AcDbBlockRotationGrip", "AcDbBlockRotationGrip");
HDXF_BLOCK_SPEC(XYGripSpec, BlockXYGripData, "BLOCKXYGRIP", BlockXYGrip, as_block_xy_grip, "AcDbBlockXYGrip", "AcDbBlockXYGrip");

// actions
HDXF_BLOCK_SPEC(FlipActionSpec, BlockFlipActionData, "BLOCKFLIPACTION", BlockFlipAction, as_block_flip_action,
                "AcDbBlockFlipAction", "AcDbBlockFlipAction");
HDXF_BLOCK_SPEC(LookupActionSpec, BlockLookupActionData, "BLOCKLOOKUPACTION", BlockLookupAction, as_block_lookup_action,
                "AcDbBlockLookupAction", "AcDbBlockLookupAction");
HDXF_BLOCK_SPEC(MoveActionSpec, BlockMoveActionData, "BLOCKMOVEACTION", BlockMoveAction, as_block_move_action,
                "AcDbBlockMoveAction", "AcDbBlockMoveAction");
HDXF_BLOCK_SPEC(PolarStretchActionSpec, BlockPolarStretchActionData, "BLOCKPOLARSTRETCHACTION", BlockPolarStretchAction,
                as_block_polar_stretch_action, "AcDbBlockPolarStretchAction", "AcDbBlockPolarStretchAction");
HDXF_BLOCK_SPEC(StretchActionSpec, BlockStretchActionData, "BLOCKSTRETCHACTION", BlockStretchAction, as_block_stretch_action,
                "AcDbBlockStretchAction", "AcDbBlockStretchAction");
HDXF_BLOCK_SPEC(ArrayActionSpec, BlockArrayActionData, "BLOCKARRAYACTION", BlockArrayAction, as_block_array_action,
                "AcDbBlockArrayAction", "AcDbBlockArrayAction");

// the rotate action has the action with a base point under it
struct RotateActionSpec {
    using Data = BlockRotateActionData;
    static constexpr const char* name = "BLOCKROTATEACTION";
    static constexpr const char* cpp_name = "AcDbBlockRotateAction";
    static constexpr const char* subclass = "AcDbBlockRotationAction";
    static constexpr ObjectType type = ObjectType::BlockRotateAction;
    static constexpr bool with_base_point = true;
    static const Data& get(const Object& o) { return o.as_block_rotate_action(); }
    static void read_own(GroupCursor& c, Data& d);
    static void write_own(RecordWriter& w, const Data& d);
};

// constraint parameters
#define HDXF_CONSTRAINT_SPEC(SPEC, DATA, DXF, ENUM, ACCESSOR, SUBCLASS, DISTANCE, CPP)                         \
    struct SPEC {                                                                                              \
        using Data = DATA;                                                                                     \
        static constexpr const char* name = DXF;                                                               \
        static constexpr const char* cpp_name = CPP;                                                           \
        static constexpr const char* subclass = SUBCLASS;                                                      \
        static constexpr const char* distance_subclass = DISTANCE;                                             \
        static constexpr ObjectType type = ObjectType::ENUM;                                                   \
        static const Data& get(const Object& o) { return o.ACCESSOR(); }                                       \
    }

HDXF_CONSTRAINT_SPEC(HorizontalConstraintSpec, BlockHorizontalConstraintParameterData, "BLOCKHORIZONTALCONSTRAINTPARAMETER",
                     BlockHorizontalConstraintParameter, as_block_horizontal_constraint_parameter,
                     "AcDbBlockHorizontalConstraintParameter", "AcDbBlockLinearConstraintParameter",
                     "AcDbBlockHorizontalConstraintParameter");
HDXF_CONSTRAINT_SPEC(VerticalConstraintSpec, BlockVerticalConstraintParameterData, "BLOCKVERTICALCONSTRAINTPARAMETER",
                     BlockVerticalConstraintParameter, as_block_vertical_constraint_parameter,
                     "AcDbBlockVerticalConstraintParameter", "AcDbBlockLinearConstraintParameter",
                     "AcDbBlockVerticalConstraintParameter");
HDXF_CONSTRAINT_SPEC(AlignedConstraintSpec, BlockAlignedConstraintParameterData, "BLOCKALIGNEDCONSTRAINTPARAMETER",
                     BlockAlignedConstraintParameter, as_block_aligned_constraint_parameter,
                     "AcDbBlockAlignedConstraintParameter", "AcDbBlockLinearConstraintParameter",
                     "AcDbBlockAlignedConstraintParameter");
HDXF_CONSTRAINT_SPEC(LinearConstraintSpec, BlockLinearConstraintParameterData, "BLOCKLINEARCONSTRAINTPARAMETER",
                     BlockLinearConstraintParameter, as_block_linear_constraint_parameter, nullptr,
                     "AcDbBlockLinearConstraintParameter", "AcDbBlockLinearConstraintParameter");
HDXF_CONSTRAINT_SPEC(RadialConstraintSpec, BlockRadialConstraintParameterData, "BLOCKRADIALCONSTRAINTPARAMETER",
                     BlockRadialConstraintParameter, as_block_radial_constraint_parameter, nullptr,
                     "AcDbBlockRadialConstraintParameter", "AcDbBlockRadialConstraintParameter");

// ---------------------------------------------------------------------------
// The codecs of the step (registered by register_dynamic_type_codecs() in typed_families.cpp) and what they write
// ---------------------------------------------------------------------------

void register_dynamic_type_codecs(TypedRegistry& registry);                 // dynamic_block_parameters.cpp
void register_dynamic_action_codecs(TypedRegistry& registry);               // dynamic_block_actions.cpp
void register_dynamic_constraint_codecs(TypedRegistry& registry);           // dynamic_block_constraints.cpp

void write_block_parameter_dependency_body(RecordWriter& w, const Object& o);       // dxf_writer_dynamic_types.cpp
void write_dynamic_block_proxy_node(RecordWriter& w, const Object& o);

// The lists of a 2Pt parameter's AcDbBlock2PtParameter (170 and its 91)
std::vector<ListSpec> two_point_lists();
// A list of the codes of a subclass; `id` tells two lists of one subclass apart
ListSpec codes_list_of(const char* subclass, std::vector<int> codes, int id);
// The whole subclass as one list
ListSpec whole_subclass_list(const char* subclass);
// The relation descriptor of a scalar pointer that becomes 0 when its target is gone
RefDescriptor zero_pointer(const char* subclass, int code);

} // namespace internal
} // namespace hdxf
