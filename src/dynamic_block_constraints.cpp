#include "DynamicBlockElements.hpp"
#include <string>
#include <vector>

// The constraints of step 9.13 (phase 9; plan 2026-10-27-fas-9-typer.md, F9.9, B109) and the two records that stand beside them:
//
//   BLOCKHORIZONTALCONSTRAINTPARAMETER  AcDbEvalExpr, AcDbBlockElement, AcDbBlockParameter, AcDbBlock2PtParameter,
//                                       AcDbBlockConstraintParameter (330: the ACDBASSOCDEPENDENCY), AcDbBlockLinearConstraintParameter
//                                       (the distance part: labels, offset, expression, value set with the values 144),
//                                       AcDbBlockHorizontalConstraintParameter (no group)
//   BLOCKVERTICALCONSTRAINTPARAMETER    the same, AcDbBlockVerticalConstraintParameter last
//   BLOCKRADIALCONSTRAINTPARAMETER      … AcDbBlockConstraintParameter, AcDbBlockRadialConstraintParameter (the distance part)
//   BLOCKALIGNEDCONSTRAINTPARAMETER     typed from the chain of the horizontal and vertical ones (AcDbBlockAlignedConstraintParameter
//                                       last); no AutoCAD file has a record of it (ej testat)
//   BLOCKLINEARCONSTRAINTPARAMETER      the chain of the horizontal one without its last subclass (the linear constraint is the base
//                                       of the three above); no AutoCAD file has a record of it (ej testat)
//   BLOCKPARAMDEPENDENCYBODY            AcDbAssocDependencyBody (90), AcDbImpAssocDimDependencyBodyBase (90, 1: "Head=7.18"),
//                                       AcDbBlockParameterDependencyBody (90): the body of the constraint's dependency; no AcDbEvalExpr
//   ACDB_DYNAMICBLOCKPROXYNODE          AcDbEvalExpr only
//
// Not typed: BLOCKANGULARCONSTRAINTPARAMETER, BLOCKANGULARCONSTRAINTPARAMETERENTITY and BLOCKDIAMETRICCONSTRAINTPARAMETER. No AutoCAD file has a
// record of them and their chain cannot be derived from a sibling (an angular constraint has three points: no sibling shows its base;
// the diametric class is not even registered in AutoCAD's CLASSES). They stay as read, preserved, as any record of an unknown class.
//
// The constraint parameter's 330 is a soft pointer to the ACDBASSOCDEPENDENCY of the constraint (step 9.10), which names the parameter as
// its object and is one of the parameter's reactors; the dependency owns the BLOCKPARAMDEPENDENCYBODY (360). The groups are AutoCAD 2025's own,
// the same in every version 2000 … 2018 (doc/facit/dyntypes, fas9_dynamic_types.scr).

namespace hdxf {
namespace internal {

namespace {

bool read_parameter_dependency_body(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (dynamic_record_too_old(view, ctx)) return false;
    BlockParameterDependencyBodyData d;
    if (!read_subclass(view, ctx, "AcDbAssocDependencyBody", [&](GroupCursor& c) { c.require_integer(90, d.body_version); })) return false;
    if (!read_subclass(view, ctx, "AcDbImpAssocDimDependencyBodyBase", [&](GroupCursor& c) {
            c.require_integer(90, d.base_version);
            c.require_string(1, d.expression);
        })) {
        return false;
    }
    if (!read_subclass(view, ctx, "AcDbBlockParameterDependencyBody", [&](GroupCursor& c) { c.require_integer(90, d.value_90); })) return false;
    o.data = Boxed<BlockParameterDependencyBodyData>(std::move(d));
    return true;
}

bool read_dynamic_block_proxy_node(const RecordView& view, TypedReadContext& ctx, Object& o) {
    if (dynamic_record_too_old(view, ctx)) return false;
    DynamicBlockProxyNodeData d;
    if (!read_subclass(view, ctx, "AcDbEvalExpr", [&](GroupCursor& c) { read_eval_expr(c, d.eval); })) return false;
    o.data = Boxed<DynamicBlockProxyNodeData>(std::move(d));
    return true;
}

template <class Spec>
void constraint_references(const Object& o, std::vector<TypedReference>& out) {
    const std::string& h = Spec::get(o).dependency;
    if (!h.empty() && h != "0") out.push_back({h, RefKind::SoftPointer, false});
}

template <class Spec>
ObjectCodec constraint_codec(bool verified) {
    ObjectCodec c = dynamic_codec(Spec::name, Spec::type, read_constraint<Spec>, write_constraint<Spec>, Spec::cpp_name, verified);
    c.references = constraint_references<Spec>;
    c.lists = two_point_lists();
    c.lists.push_back(codes_list_of(Spec::distance_subclass, {175, 144}, 1));
    c.refs = {zero_pointer("AcDbBlockConstraintParameter", 330)};
    return c;
}

} // namespace

void register_dynamic_constraint_codecs(TypedRegistry& registry) {
    registry.add(constraint_codec<HorizontalConstraintSpec>(true));
    registry.add(constraint_codec<VerticalConstraintSpec>(true));
    registry.add(constraint_codec<RadialConstraintSpec>(true));
    registry.add(constraint_codec<AlignedConstraintSpec>(false));
    registry.add(constraint_codec<LinearConstraintSpec>(false));
    // the dependency's body is registered by CLASSES with the proxy flags 1, not 1153
    registry.add(dynamic_codec("BLOCKPARAMDEPENDENCYBODY", ObjectType::BlockParameterDependencyBody, read_parameter_dependency_body,
                               write_block_parameter_dependency_body, "AcDbBlockParameterDependencyBody", true, 1));
    registry.add(dynamic_codec("ACDB_DYNAMICBLOCKPROXYNODE", ObjectType::DynamicBlockProxyNode, read_dynamic_block_proxy_node,
                               write_dynamic_block_proxy_node, "AcDbDynamicBlockProxyNode"));
}

} // namespace internal
} // namespace hdxf
