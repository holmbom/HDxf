#pragma once

#include "TypedRecordTypes.hpp"
#include <functional>
#include <string>
#include <vector>

// The ACDBASSOC framework (phase 9, step 9.10; plan 2026-10-27-fas-9-typer.md): what the concrete record types of
// step 9.10 share and step 9.15 builds on. Every function reads or writes one subclass of a record by its AutoCAD
// name, so a concrete type is its subclasses in AutoCAD's order:
//
//   ACDBASSOCACTION        AcDbAssocAction
//   ACDBASSOCNETWORK       AcDbAssocAction, AcDbAssocNetwork
//   ACDBASSOCDEPENDENCY    AcDbAssocDependency
//   ACDBASSOCGEOMDEPENDENCY AcDbAssocDependency, AcDbAssocGeomDependency (+ a persistent subentity id)
//   …PARAM types           AcDbAssocActionParam, then AcDbAssocCompoundActionParam or
//                          AcDbAssocSingleDependencyActionParam, then the type's own
//   …ACTIONBODY types      AcDbAssocActionBody [+ AcDbAssocParamBasedActionBody in a version 1 body],
//                          AcDbAssocSurfaceActionBody, AcDbAssocPathBasedSurfaceActionBody, the type's own
//
// A read function returns false after `ctx.fail()` (the record then stays as it was read, with a warning); a
// write function writes the subclass marker and its groups, drops what points to a record that is gone (B19) and
// counts what it writes.

namespace hdxf {
namespace internal {

// "TYPE handle" for messages
std::string assoc_describe(const RecordView& view);

// What follows the groups HDxf knows in a subclass (a later version): kept as read, with a warning (B105)
void assoc_note_rest(GroupCursor& c, TypedReadContext& ctx);

// A pointer held in a record: the handle, or "0" when it is gone (B19)
std::string assoc_pointer(RecordWriter& w, const std::string& handle);
// The handles of a list that still lead somewhere
std::vector<std::string> assoc_live(RecordWriter& w, const std::vector<std::string>& handles);

// ---------------------------------------------------------------------------
// assoc_action.cpp
// ---------------------------------------------------------------------------

bool read_assoc_value_parameter(GroupCursor& c, AssocValueParameter& out);
void write_assoc_value_parameter(RecordWriter& w, const AssocValueParameter& v);
// 90 count + the value parameters
bool read_assoc_value_parameters(GroupCursor& c, std::vector<AssocValueParameter>& out);
void write_assoc_value_parameters(RecordWriter& w, const std::vector<AssocValueParameter>& v);

// AcDbAssocAction
bool read_assoc_action_base(const RecordView& view, TypedReadContext& ctx, AssocActionBase& out);
void write_assoc_action_base(RecordWriter& w, const AssocActionBase& v);
void assoc_action_references(const AssocActionBase& v, std::vector<TypedReference>& out);

// ---------------------------------------------------------------------------
// assoc_dependency.cpp
// ---------------------------------------------------------------------------

// AcDbAssocDependency
bool read_assoc_dependency_base(const RecordView& view, TypedReadContext& ctx, AssocDependencyBase& out);
void write_assoc_dependency_base(RecordWriter& w, const AssocDependencyBase& v);
void assoc_dependency_references(const AssocDependencyBase& v, std::vector<TypedReference>& out);

// ---------------------------------------------------------------------------
// assoc_perssubent.cpp: the persistent subentity id (AcDbAssocGeomDependency's class name and groups)
// ---------------------------------------------------------------------------

bool read_persistent_subentity_id(GroupCursor& c, PersistentSubentityId& out);
void write_persistent_subentity_id(RecordWriter& w, const PersistentSubentityId& v);

// ---------------------------------------------------------------------------
// assoc_param.cpp: the action parameter bases
// ---------------------------------------------------------------------------

bool read_assoc_action_param_base(const RecordView& view, TypedReadContext& ctx, AssocActionParamBase& out);
void write_assoc_action_param_base(RecordWriter& w, const AssocActionParamBase& v);
bool read_assoc_compound_param_base(const RecordView& view, TypedReadContext& ctx, GroupCursor& c, AssocCompoundActionParamBase& out);
void write_assoc_compound_param_base(RecordWriter& w, const AssocCompoundActionParamBase& v);
bool read_assoc_single_dependency_param_base(const RecordView& view, TypedReadContext& ctx, AssocSingleDependencyActionParamBase& out);
void write_assoc_single_dependency_param_base(RecordWriter& w, const AssocSingleDependencyActionParamBase& v);

// ---------------------------------------------------------------------------
// assoc_geometry.cpp: the curve of an edge parameter
// ---------------------------------------------------------------------------

// `trailing_circle_value`: the circle arc has one more 40 (the form of an action parameter of version 1)
bool read_assoc_curve(GroupCursor& c, bool trailing_circle_value, AssocCurve& out);
void write_assoc_curve(RecordWriter& w, const AssocCurve& v);

// ---------------------------------------------------------------------------
// assoc_body.cpp: the action body bases
// ---------------------------------------------------------------------------

// AcDbAssocActionBody (+ AcDbAssocParamBasedActionBody when its version is 1)
// `tail` (a body with groups of its own right after them: the aligned dimension's) reads or writes them in the subclass
// that has them: AcDbAssocParamBasedActionBody in a version 1 body, AcDbAssocActionBody in a version 2 body
bool read_assoc_action_body_head(const RecordView& view, TypedReadContext& ctx, AssocActionBodyHead& out,
                                 const std::function<bool(GroupCursor&)>& tail = nullptr);
void write_assoc_action_body_head(RecordWriter& w, const AssocActionBodyHead& v, const std::function<void()>& tail = nullptr);
// AcDbAssocSurfaceActionBody and AcDbAssocPathBasedSurfaceActionBody
bool read_assoc_surface_body(const RecordView& view, TypedReadContext& ctx, AssocSurfaceActionBody& out);
void write_assoc_surface_body(RecordWriter& w, const AssocSurfaceActionBody& v);
bool read_assoc_path_based_body(const RecordView& view, TypedReadContext& ctx, AssocPathBasedSurfaceActionBody& out);
void write_assoc_path_based_body(RecordWriter& w, const AssocPathBasedSurfaceActionBody& v);
void assoc_body_references(const AssocActionBodyHead& head, std::vector<TypedReference>& out);

// ---------------------------------------------------------------------------
// The shared descriptors of the codecs (assoc_action.cpp)
// ---------------------------------------------------------------------------

// A reference descriptor of a handle group in a subclass
RefDescriptor assoc_ref(const char* subclass, int code, RefKind kind, RefRemoval removal, int ordinal = -1);
// A member of a counted list: the member goes and the count (90) follows
RefDescriptor assoc_member_ref(const char* subclass, int code, RefKind kind);
// Every group of a subclass is one list (B12): a change writes the subclass whole
ListSpec assoc_whole_subclass(const char* subclass);
// The CLASSES entry of an ACDBASSOC class (ObjectDBX Classes, proxy flags 1025; the managers 1024)
ClassSpec assoc_class(const char* cpp_name, int flags = 1025, const char* application = "ObjectDBX Classes");

// The version cells: R12 has no OBJECTS (named error), R13/R14 not established, 2000 … 2018 made. `verified`
// are the versions of which AutoCAD's own files (ASCII and binary) show the type, `verified_ascii_only` those that only
// have an ASCII file; the others are "ej testat"
VersionMatrix assoc_matrix(std::initializer_list<DxfVersion> verified, std::initializer_list<DxfVersion> verified_ascii_only = {});

// ---------------------------------------------------------------------------
// The registration of each file (assoc_action.cpp calls them all)
// ---------------------------------------------------------------------------

void register_assoc_dependency_codecs(TypedRegistry& registry);
void register_assoc_perssubent_codecs(TypedRegistry& registry);
void register_assoc_param_codecs(TypedRegistry& registry);
void register_assoc_body_codecs(TypedRegistry& registry);

} // namespace internal
} // namespace hdxf
