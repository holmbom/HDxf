#pragma once

#include "AssocTypes.hpp"
#include <initializer_list>
#include <unordered_set>

// The other ACDBASSOC classes (phase 9, step 9.15; plan 2026-10-27-fas-9-typer.md): what the four source files of the step
// (assoc_constraints.cpp, assoc_extended_params.cpp, assoc_surface_ops.cpp, assoc_dimensions_arrays.cpp) and the removal rule
// (assoc_removal.cpp) share. They build on 9.10's bases (AssocTypes.hpp) and change none of them.

namespace hdxf {
namespace internal {

// "all six versions, both formats" (AutoCAD's own files of doc/facit/fas9_assoc_extended.scr have every type in 2000 … 2018)
inline std::initializer_list<DxfVersion> assoc_all_versions() {
    static const std::initializer_list<DxfVersion> all = {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010,
                                                          DxfVersion::R2013, DxfVersion::R2018};
    return all;
}

// A pointer to add to a record's references
inline void assoc_add_reference(std::vector<TypedReference>& out, const std::string& h, RefKind kind, bool back_reactor = false) {
    if (!h.empty() && h != "0") out.push_back({h, kind, back_reactor});
}

// The reference descriptors every action body has: the parameters a version 1 body owns (a counted list), the pointers of its
// bases (the dependency the surface is written by)
inline std::vector<RefDescriptor> assoc_body_descriptors() {
    return {assoc_member_ref("AcDbAssocParamBasedActionBody", 360, RefKind::HardOwner),
            assoc_ref("AcDbAssocParamBasedActionBody", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace),
            assoc_ref("AcDbAssocSurfaceActionBody", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace)};
}

// A body of the head, then (when asked for) the surface and path-based bases
bool assoc_read_body_parts(const RecordView& view, TypedReadContext& ctx, AssocActionBodyHead& head, AssocSurfaceActionBody* surface,
                           AssocPathBasedSurfaceActionBody* path_based, const std::function<bool(GroupCursor&)>& tail = nullptr);
void assoc_write_body_parts(RecordWriter& w, const AssocActionBodyHead& head, const AssocSurfaceActionBody* surface,
                            const AssocPathBasedSurfaceActionBody* path_based, const std::function<void()>& tail = nullptr);

// AutoCAD's class name of each node type of a constraint group
template <typename T> struct AssocConstraintNodeName;
#define ASSOC_CONSTRAINT_NODE_NAME(T) \
    template <> struct AssocConstraintNodeName<T> { static constexpr const char* value = #T; };
ASSOC_CONSTRAINT_NODE_NAME(AcConstrainedBoundedLine)
ASSOC_CONSTRAINT_NODE_NAME(AcConstrainedImplicitPoint)
ASSOC_CONSTRAINT_NODE_NAME(AcConstrainedDatumLine)
ASSOC_CONSTRAINT_NODE_NAME(AcConstrained2PointsConstructionLine)
ASSOC_CONSTRAINT_NODE_NAME(AcConstrainedArc)
ASSOC_CONSTRAINT_NODE_NAME(AcConstrainedCircle)
ASSOC_CONSTRAINT_NODE_NAME(AcPointCurveConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcPointCoincidenceConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcParallelConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcPerpendicularConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcColinearConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcTangentConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcEqualLengthConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcEqualRadiusConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcMidPointConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcCenterPointConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcFixedConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcHorizontalConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcVerticalConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcDistanceConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcAngleConstraint)
ASSOC_CONSTRAINT_NODE_NAME(Ac3PointAngleConstraint)
ASSOC_CONSTRAINT_NODE_NAME(AcRadiusDiameterConstraint)
#undef ASSOC_CONSTRAINT_NODE_NAME

// The dimensional constraints of a group that go when the geometry they stand on is gone: (node number, dependency) for each
// of their two 340. A distance constraint's pair is the value dependency of its variable and the dependency on its
// dimension (assoc_constraints.cpp, assoc_removal.cpp)
std::vector<std::pair<int32_t, std::string>> assoc_group_removed_nodes(const Assoc2dConstraintGroupData& group,
                                                                       const std::function<bool(const std::string&)>& live);

// ---------------------------------------------------------------------------
// assoc_history.cpp: the history entries of the persistent subentity managers (the assoc manager's form and the base manager's)
// ---------------------------------------------------------------------------

// `count` entries from the cursor; false (with the message in `error`) when an entry does not fit the grammar derived from AutoCAD's files
bool read_persistent_history(GroupCursor& c, size_t count, bool new_form, std::vector<PersHistoryEntry>& out, std::string& error);
// The entries as they are written (the count before them is the caller's); false after w.fail()
bool write_persistent_history(RecordWriter& w, bool new_form, const std::vector<PersHistoryEntry>& entries);

// Registration of each file
void register_assoc_constraint_codecs(TypedRegistry& registry);
void register_assoc_extended_param_codecs(TypedRegistry& registry);
void register_assoc_surface_op_codecs(TypedRegistry& registry);
void register_assoc_dimension_array_codecs(TypedRegistry& registry);
// All of step 9.15 (typed_families.cpp calls this)
void register_assoc_extended_codecs(TypedRegistry& registry);

// ---------------------------------------------------------------------------
// assoc_removal.cpp: what AutoCAD removes with the geometry an action stands on (plan 9.15, F9.12)
// ---------------------------------------------------------------------------

// The associative actions of `dwg` that go when what they depend on is gone, in the drawing AutoCAD would have: an action whose
// dependency leads to an object that is not `live` (the profile, the surface it writes, a cutting curve, the solid of a
// dimension), and a restore-entity-state action whose chain successor goes. A 2D constraint group stays (it loses
// what stood on the geometry, assoc_constraints.cpp). The handles come back as values, not already in `dropped`.
std::vector<uint64_t> assoc_orphaned_actions(const Drawing& dwg, const std::function<bool(const std::string&)>& live,
                                             const std::unordered_set<uint64_t>& dropped);

} // namespace internal
} // namespace hdxf
