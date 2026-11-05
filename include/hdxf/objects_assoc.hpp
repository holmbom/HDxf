#pragma once

// The ACDBASSOC records.

#include "core.hpp"

namespace hdxf {

// ---------------------------------------------------------------------------
// ACDBASSOC (phase 9, step 9.10): the associative framework's network, actions, dependencies, parameters and action
// bodies, and the two persistent-subentity managers. The groups are AutoCAD's own (doc/facit/acad_<version>_dxf_
// associative.dxf, _surfaces.dxf, the corpus' Surface_2004, example_2007 … 2018 and 3Dlines); the DXF reference has
// nothing for them. Every handle is held as text ("0" is none), a count is the size of its list, and a group whose
// meaning AutoCAD's files do not show has a name of its place (reserved_…, first_…): no role is invented.
//
// The form follows the record's own version groups, as AutoCAD writes it: an action of version 1 (every file up to
// 2010) keeps its parameters in its action body (AssocParamBasedActionBody), version 2 (2013 and later) in the action
// (params, value_params), and an action parameter of version 1 has one more group than version 0.
// ---------------------------------------------------------------------------

// One value of an AssocValueParameter: the group that holds it is 40 (a double), 70 (an int16) or 90 (an int32), the
// type of AutoCAD's AcDbEvalVariant; every value is followed by a soft pointer (330), 0 in every file
struct AssocParameterValue {
    int code = 40;                      // 40 real, 70 integer (int16), 90 integer (int32)
    double real = 0.0;                  // 40
    int32_t integer = 0;                // 70, 90
    std::string pointer = "0";          // 330 after the value
};

// AcDbAssocValueParam: 90 version, 1 name, 90 unit type (0 none, 1 distance, 2 angle), 90 count + the values, 330 dependency
struct AssocValueParameter {
    int32_t version = 0;
    std::string name;                   // 1: "ExtrusionHeight", "RevolveAngle", "Continuity", "Bulge" …
    int32_t unit_type = 0;              // 90
    std::vector<AssocParameterValue> values;
    std::string dependency = "0";       // 330 after the values
};

// A member of a network's list of actions: 360 when the network owns the action, 330 when it only points to it
struct AssocActionMember {
    std::string handle = "0";
    bool owned = true;
};

// The AcDbAssocAction subclass, the base of every ACDBASSOCACTION and ACDBASSOCNETWORK (and of the other action
// classes of step 9.15)
struct AssocActionBase {
    int32_t version = 2;                // 90: 1 until 2010, 2 from 2013 (decides the form below)
    int32_t status = 0;                 // 90
    std::string owning_network = "0";   // 330
    std::string action_body = "0";      // 360
    int32_t action_index = 0;           // 90: the action's number in its network
    int32_t max_dependency_index = 0;   // 90: the highest index of its dependencies
    std::vector<std::string> dependencies;      // 90 count + 360 (hard owner)
    // version 2 (2013 and later) only
    int32_t reserved_after_dependencies = 0;    // 90
    std::vector<std::string> params;            // 90 count + 360: the action's parameters
    int32_t reserved_after_params = 0;          // 90
    std::vector<AssocValueParameter> value_params;     // 90 count + the value parameters
};

struct AssocActionData {                // ACDBASSOCACTION
    AssocActionBase action;
};

struct AssocNetworkData {               // ACDBASSOCNETWORK
    AssocActionBase action;
    int32_t version = 0;                // 90 (AcDbAssocNetwork)
    int32_t max_action_index = 0;       // 90: the highest index of its actions
    std::vector<AssocActionMember> actions;     // 90 count + 360/330
    std::vector<std::string> soft_actions;      // 90 count + 330 (AutoCAD fills it in 2000 … 2013, not in 2007 and 2018)
};

// The AcDbAssocDependency subclass, the base of ACDBASSOCDEPENDENCY, ACDBASSOCGEOMDEPENDENCY (and 9.15's value
// dependency): the object the action reads (is_read) or writes (is_write)
struct AssocDependencyBase {
    int32_t version = 2;                // 90
    int32_t status = 0;                 // 90
    bool is_read = true;                // 290
    bool is_write = true;               // 290
    bool is_object_state_dependent = true;      // 290
    bool is_delegating_to_owning_action = true; // 290
    int32_t order = 0;                  // 90: -10000, -2147483648 (the result) or 0
    std::string dependent_on_object = "0";      // 330: the geometry or object depended on (it gets this as reactor)
    bool depends_on_compound_object = false;    // 290
    std::string previous = "0";         // 330: the previous dependency of the chain
    std::string next = "0";             // 330
    std::string dependency_body = "0";  // 360 (hard owner)
    int32_t index = 0;                  // 90: the dependency's index in its action
};

struct AssocDependencyData {            // ACDBASSOCDEPENDENCY
    AssocDependencyBase dependency;
};

// A persistent subentity id (AcDbAssocPersSubentId): the classes AutoCAD's files have. An unknown class makes the
// record stay as it was read
enum class PersistentSubentityKind : uint8_t {
    SingleEdge,         // AcDbAssocSingleEdgePersSubentId: no data
    Edge,               // AcDbAssocEdgePersSubentId: 90 version, the ids of the edge's start and end vertices
    Simple,             // AcDbAssocSimplePersSubentId: 90 version, 90 subentity type (-1003), 90 subentity index
    AsmBasedEntity      // AcDbAssocAsmBasedEntityPersSubentId: the same three groups
};

struct PersistentSubentityId {
    PersistentSubentityKind kind = PersistentSubentityKind::SingleEdge;
    int32_t version = 0;                // 90 (every kind but SingleEdge)
    int32_t start_vertex = 0;           // Edge: 90
    int32_t end_vertex = 0;             // Edge: 90
    int32_t subentity_type = 0;         // Simple, AsmBasedEntity: 90
    int32_t subentity_index = 0;        // Simple, AsmBasedEntity: 90
};

// AutoCAD's class name of a kind ("AcDbAssocEdgePersSubentId") and the kind of a class name (false: not a class HDxf has)
const char* persistent_subentity_class_name(PersistentSubentityKind kind);
bool persistent_subentity_kind_of(const std::string& class_name, PersistentSubentityKind& kind);

struct AssocGeomDependencyData {        // ACDBASSOCGEOMDEPENDENCY
    AssocDependencyBase dependency;
    int32_t version = 0;                // 90 (AcDbAssocGeomDependency)
    bool has_subentity_id = true;       // 290 before the class name: 1 in every file
    PersistentSubentityId subentity_id; // 1 (the class name) and its groups
    bool trailing_flag = false;         // 290 last
};

// The AcDbAssocActionParam subclass: 90 version, [90 when version 1 or later], 1 name
struct AssocActionParamBase {
    int32_t version = 1;                // 90: 0 until 2010, 1 from 2013
    int32_t extra = 0;                  // 90 (version 1 and later)
    std::string name;                   // 1: "ExtrudeProfile", "CrossSection", "PathCurve", "basePoint" … (may be empty)
};

// AcDbAssocCompoundActionParam: 90 version, 90, 90 count + 360 children (hard owner)
struct AssocCompoundActionParamBase {
    int32_t version = 0;
    int32_t flags = 0;                  // 90
    std::vector<std::string> children;
};

// AcDbAssocSingleDependencyActionParam: 90 version, 330 dependency
struct AssocSingleDependencyActionParamBase {
    int32_t version = 0;
    std::string dependency = "0";
};

struct AssocPathActionParamData {       // ACDBASSOCPATHACTIONPARAM
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
    int32_t version = 0;                // 90 (AcDbAssocPathActionParam)
};

struct AssocVertexActionParamData {     // ACDBASSOCVERTEXACTIONPARAM
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;                // 90 (AcDbAssocVertexActionParam)
    Point3 point;                       // 10
};

// ACDBASSOCOSNAPPOINTREFACTIONPARAM: the subclass names are AutoCAD's own upper-case ones
struct AssocOsnapPointRefActionParamData {
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
    int32_t compound_extra_1 = 0;       // 90 after the children, in the compound subclass: only this class has them
    int32_t compound_extra_2 = 0;       // 90
    std::string compound_pointer = "0"; // 330
    int32_t version = 0;                // 90 (ACDBASSOCOSNAPPOINTREFACTIONPARAM)
    int32_t osnap_type = 0;             // 90
    double parameter = 0.0;             // 40
};

// A curve of an edge parameter (AcGe's curves, as AutoCAD stores them): the curve kind is the 90 before its groups
enum class AssocCurveKind : int32_t {
    CircleArc = 11,
    EllipseArc = 17,
    Line = 23,
    Spline = 42,
    Composite = 47
};

struct AssocLineCurve {                 // 23: two points
    Point3 start;                       // 10
    Point3 offset;                      // 10: the vector from the start to the end
};

struct AssocCircleArcCurve {            // 11
    Point3 center;                      // 10
    Point3 normal;                      // 10
    Point3 reference;                   // 10: the reference vector (the direction of the start angle's zero)
    double radius = 0.0;                // 40
    double start_angle = 0.0;           // 40: radians
    double end_angle = 0.0;             // 40: radians
    std::optional<double> trailing;     // 40: the 2013 form (an action parameter of version 1) has one more
};

struct AssocEllipseArcCurve {           // 17
    Point3 center;                      // 10
    Point3 major_axis;                  // 10
    Point3 minor_axis;                  // 10
    double major_radius = 0.0;          // 40
    double minor_radius = 0.0;          // 40
    double start_angle = 0.0;           // 40: radians
    double end_angle = 0.0;             // 40: radians
};

// An AcArray's lengths as AutoCAD writes them before its items: the logical length is the item count (not kept here),
// then the physical one and the growth length
struct AssocArrayLengths {
    int32_t physical = 0;               // 90
    int32_t growth = 8;                 // 90
};

struct AssocSplineCurve {               // 42
    int16_t first_flag = 0;             // 70
    int16_t second_flag = 0;            // 70
    int32_t degree = 3;                 // 90
    double knot_tolerance = 1e-9;       // 40
    std::vector<double> knots;          // 90 count, 90, 90, then 40 each
    AssocArrayLengths knots_lengths;
    std::vector<double> weights;        // the same for the weights (empty: not rational)
    AssocArrayLengths weights_lengths;
    std::vector<Point3> control_points; // the same for the control points: 10 20 30 each
    AssocArrayLengths control_points_lengths;
};

struct AssocCurve;

struct AssocCompositeCurve {            // 47: 90 count, then each curve with its own 90 kind
    std::vector<AssocCurve> curves;
};

struct AssocCurve {
    std::variant<AssocLineCurve, AssocCircleArcCurve, AssocEllipseArcCurve, AssocSplineCurve, AssocCompositeCurve> shape;
    AssocCurveKind kind() const;
};

struct AssocEdgeActionParamData {       // ACDBASSOCEDGEACTIONPARAM
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;                // 90 (AcDbAssocEdgeActionParam)
    std::string geometry_pointer = "0"; // 330
    AssocCurve curve;                   // 290 1 (the curve is there), 90 kind and its groups
};

// The AcDbAssocActionBody subclass (90 version) and what follows it
struct AssocParamBasedActionBody {      // AcDbAssocParamBasedActionBody: 2004 … 2010 files (action body version 1)
    int32_t version = 0;
    int32_t reserved_before_params = 0; // 90
    std::vector<std::string> params;    // 90 count + 360 (hard owner)
    int32_t reserved_after_params = 0;  // 90
    std::vector<AssocValueParameter> value_params;     // 90 count + the value parameters
};

struct AssocSurfaceActionBody {         // AcDbAssocSurfaceActionBody
    int32_t version = 0;                // 90
    std::string result_dependency = "0";    // 330: the dependency that writes the surface
    bool flag_after_result = false;     // 290
    int32_t step_id = 0;                // 90: the same number as the action's ACAD_ASSOCPERSSUBENTMANAGER XDATA
    bool flag_after_step = false;       // 290
    int16_t value_after_flag = 0;       // 70
};

struct AssocPathBasedSurfaceActionBody {    // AcDbAssocPathBasedSurfaceActionBody
    int32_t version = 0;                // 90
};

// The common head of the six action bodies: AcDbAssocActionBody's 90 version and, when it is 1, the parameters
struct AssocActionBodyHead {
    int32_t version = 2;                // 90: 1 until 2010, 2 from 2013
    std::optional<AssocParamBasedActionBody> param_based;   // present when version is 1
};

// ACDBASSOCEXTRUDEDSURFACEACTIONBODY: AcDbAssocExtrudedSurfaceActionBody 90 version
struct AssocExtrudedSurfaceActionBodyData {
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

struct AssocLoftedSurfaceActionBodyData {   // AcDbAssocLoftedSurfaceActionBody 90 version
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

struct AssocRevolvedSurfaceActionBodyData { // AcDbAssocRevolvedSurfaceActionBody 90 version, 290
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    bool flag = false;                  // 290
};

struct AssocSweptSurfaceActionBodyData {    // AcDbAssocSweptSurfaceActionBody 90 version
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

struct AssocPlaneSurfaceActionBodyData {    // AcDbAssocPlaneSurfaceActionBody 90 version
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};

// ACDBASSOCALIGNEDDIMACTIONBODY: the subclass name is AutoCAD's own upper-case one. A 90 and a 330 follow the
// action body's 90 (version 2) or the parameters (version 1)
struct AssocAlignedDimActionBodyData {
    AssocActionBodyHead head;
    int32_t dimension_flag = 0;         // 90
    std::string dimension_dependency = "0";     // 330
    int32_t version = 0;                // 90 (ACDBASSOCALIGNEDDIMACTIONBODY)
    std::string first_pointer = "0";    // 330
    std::string second_pointer = "0";   // 330
};

// --- 9.15 persistent subentity history (plan phase 9, step 9.15; assoc_history.cpp): the entries of the managers' history ---
// What AutoCAD's own files show (doc/facit/fas9_assoc_manager.scr, fas9_assoc_extended.scr, fas9_assoc_core.scr, the surfaces
// and solids files of 9.10, the corpus' Surface_2004 and example_*, in 2000 … 2018): every entry is an item (an edge, a
// vertex, a vertex of four faces, a pair of lists of ids …), a container of items, a trim (kind 8) or a surface fillet (kind
// 10). A number AutoCAD writes as 1, an
// id and a parameter is a PersHistoryId; a name record is an optional id, a kind (0 an edge, 1 a vertex), the ids of the
// faces that meet there and the slots after them (101000 is "none"). The sizes of the blocks, the counts of the
// items and the payload of a container follow from what is written.
struct PersHistoryId {
    int32_t id = 0;                     // 90 after the 1
    int32_t parameter = 0;              // 90: 0, 10, 20, 1001 …
    // A face of a name record named by more than one id: AutoCAD writes the face as `count, then id, parameter` for each, and
    // `1 id param` is the face of one id (a wall extruded from a polyline of two segments: `2, 1 21, 1 22`; the SURFFILLET of
    // doc/facit/fas9_assoc_history_surf.scr). The ids after the first; their own `more` stays empty
    std::vector<PersHistoryId> more;
};

struct PersHistoryRecord {              // a naming record: [id], 0, kind, count + faces, the slots
    std::optional<PersHistoryId> id;    // 1 and the id, or 0 when the record has none
    int32_t reserved = 0;               // 90 (0 in every file)
    int32_t kind = 0;                   // 90: 0 for an edge, 1 for a vertex
    std::vector<PersHistoryId> faces;   // 90 count, then 1 + id + parameter each
    std::vector<int32_t> slots;         // the ids after them (101000: none; 900, 301002, 103003 …); their number is the record's
};

struct PersHistoryList {                // a counted list of ids; a member is `count, id, parameter ...`, the ids after the first in `more`
    std::vector<PersHistoryId> ids;
};

// An item: AutoCAD's `kind, 0, count of the payload, the payload, the size of the block, the block, [new form] a count of
// doubles and the doubles`. The payload is a step id (0 inside a container) and then groups of ids that AutoCAD separates with
// 0. The block is name records (kinds 3, 4 and 5), two lists (kind 9) or zeros (kind 7)
struct PersHistoryItem {
    int32_t kind = 0;                   // 3, 4, 5, 7, 9 (the kinds 8 and 10 are PersHistoryTrim and PersHistoryFillet)
    int32_t reserved = 0;               // 90 after the kind (0 in every file)
    int32_t step = 0;                   // the first of the payload: the step the item belongs to, 0 in a container
    std::vector<std::vector<int32_t>> groups;   // the rest of the payload, the groups separated by 0
    std::vector<PersHistoryRecord> records;     // kinds 3, 4, 5: the block
    std::vector<PersHistoryList> lists;         // kind 9: the block's two lists
    int32_t zero_count = 0;             // kind 7: the number of zeros the block holds (6 in the new form, 4 in the old)
    std::vector<double> parameters;     // the doubles after the block (new form; 0 or 2 in AutoCAD's files)
};

// Kind 6: the payload is the container's step and the payloads of its items, the block three zeros (one in the old form), the
// number of items and the items without their payloads
struct PersHistoryContainer {
    int32_t step = 0;                   // the first of the payload
    int32_t reserved = 0;               // 90 after the kind
    std::vector<PersHistoryItem> items;
    std::vector<double> parameters;
};

// The kinds 8 and 10 (SURFTRIM and SURFFILLET; doc/facit/fas9_assoc_history_surf.scr, fas9_assoc_extended.scr, fas9_surfaces.scr)
// name subentities by paths: `90 count, then id, parameter` for each id of the path (no 1 before them). Every id of a path has
// one int of the entry's payload after the payload of what it belongs to: the step that made the subentity (the extruded wall's
// step for the face of that wall)
struct PersHistoryPathId {
    int32_t id = 0;                     // 90
    int32_t parameter = 0;              // 90: 0, 1, 1001 …
    int32_t step = 0;                   // its int in the entry's payload
};

struct PersHistoryPath {                // 90 count + the ids
    std::vector<PersHistoryPathId> ids;
};

// An item of a kind 8 entry: the item as in a container (kind, 0, n, S, block, 0 doubles) and a counted list of paths after it
struct PersHistoryTrimItem {
    PersHistoryItem item;               // the payload is the item's own n ints (step 0 and its groups)
    std::vector<PersHistoryPath> paths;
};

// Kind 8 (SURFTRIM, one entry per trimmed surface and cutting edge): the payload is the step, then for every item its payload and
// the steps of its paths' ids; the block three zeros, the number of items and the items (new form only, as AutoCAD's files have
// it). The doubles after the block are the entry's (2 per cutting edge in AutoCAD's files)
struct PersHistoryTrim {
    int32_t step = 0;                   // the first of the payload
    int32_t reserved = 0;               // 90 after the kind
    std::vector<PersHistoryTrimItem> items;
    std::vector<double> parameters;
};

// Kind 10 (SURFFILLET): the payload is the step and the steps of the ids of the surfaces' paths, in order; the block three zeros,
// 90 count of surfaces and for each a counted list of paths (its faces), then 90 count of groups, each a count of lists and
// the lists as `90 count + ints`: for each surface the indices of its paths the fillet stands on (a wall of two faces: 0 and 1)
struct PersHistoryFillet {
    int32_t step = 0;                   // the first of the payload
    int32_t reserved = 0;               // 90 after the kind
    std::vector<std::vector<PersHistoryPath>> surfaces;
    std::vector<std::vector<std::vector<int32_t>>> groups;
    std::vector<double> parameters;     // none in AutoCAD's files
};

using PersHistoryEntry = std::variant<PersHistoryItem, PersHistoryContainer, PersHistoryTrim, PersHistoryFillet>;

// ACDBASSOCPERSSUBENTMANAGER (AcDbAssocPersSubentManager): the drawing's table of persistent subentity ids and the history entries
// of what was done to the drawing's solids and surfaces (step 9.15; before it a manager with entries stayed as it was read)
struct AssocPersSubentManagerData {
    int32_t version = 2;                // 90: 1 until 2010, 2 from 2013
    int32_t header_1 = 3;               // 90
    int32_t header_2 = 0;               // 90
    int32_t header_3 = 2;               // 90
    int32_t next_id = 0;                // 90: the highest id given
    std::vector<PersHistoryEntry> history;      // 90 count + the entries (step 9.15)
    std::vector<int32_t> first_list;    // 90 count + ids
    std::vector<int32_t> second_list;   // 90 count + ids
    std::vector<int32_t> third_list;    // version 2 only: 90 count + ids (none in most of AutoCAD's files; [15] in the 2013 and
                                        // 2018 files of fas9_assoc_extended.scr, step 9.15)
    bool flag = false;                  // 290
};

// ACDBPERSSUBENTMANAGER (AcDbPersSubentManager): the base class's own record
struct PersSubentManagerData {
    int32_t version = 2;                // 90
    int32_t header_1 = 0;               // 90
    int32_t header_2 = 2;               // 90
    int32_t next_id = 0;                // 90
    std::vector<PersHistoryEntry> history;      // 90 count + the entries (step 9.15)
    std::vector<int32_t> ids;           // 90 count + ids
};

// ---------------------------------------------------------------------------
// --- 9.15 other ACDBASSOC classes (plan phase 9, step 9.15): the 2D constraint group, variables, the other
// action bodies and parameters, the dimension and multileader bodies, the arrays. AutoCAD's own files (the facit of
// doc/facit/fas9_assoc_extended.scr in 2000 … 2018 and both formats, Autodesk's installed sample drawings opened and saved
// by AutoCAD 2025's core, the corpus' acad_*_dxf_associative.dxf and _surfaces.dxf). Built on the 9.10 bases above: the form
// follows the record's own version groups (B110); a group whose meaning AutoCAD's files do not show is named by its code.
// ---------------------------------------------------------------------------

// The value of a variable or a value dependency (AcDbEvalVariant as these records write it): 40 real, 90 integer, 70 short,
// 1 string
struct AssocEvalValue {
    int code = 40;
    double real = 0.0;                  // 40
    int32_t integer = 0;                // 90, 70
    std::string text;                   // 1
};

// A member of a variable's list of the dependencies it owns: 360 and the 90 after it
struct AssocVariableMember {
    std::string dependency = "0";       // 360 (hard owner)
    int32_t value_90 = 0;               // 90 after it (0 in every file)
};

// ACDBASSOCVARIABLE: AcDbAssocAction (the action base) and AcDbAssocVariable. A user parameter (-PARAMETERS) or the value of a
// dimensional constraint (d1, ang1 …): name, the expression the user typed, the evaluator, the value
struct AssocVariableData {
    AssocActionBase action;
    int32_t version = 2;                // 90
    std::string name;                   // 1: "d1", "width", "ang1"
    std::string expression;             // 1: "80", "width*2" (empty for a parameter without one)
    std::string evaluator = "AcDbCalc:1.0";     // 1
    std::string description;            // 1
    AssocEvalValue value;               // 40 (real), 90 (integer) or 1 (string)
    bool flag_290_a = false;            // 290
    bool flag_290_b = false;            // 290
    std::vector<AssocVariableMember> members;   // 90 count + (360, 90) each: the value dependencies the variable owns
};

// ACDBASSOCVALUEDEPENDENCY: AcDbAssocDependency and AcDbAssocValueDependency
struct AssocValueDependencyData {
    AssocDependencyBase dependency;
    int32_t version = 0;                // 90
    std::string name;                   // 1 (empty in every file)
    AssocEvalValue value;               // 40 or 90
};

// ASSOCDIMDEPENDENCYBODY (the plan's ACDBASSOCDIMDEPENDENCYBODY: AutoCAD writes the record without the ACDB): the body of the
// dependency on a dimensional constraint's dimension (360 of the dependency). AcDbAssocDependencyBody 90, AcDbImpAssocDim
// DependencyBodyBase 90 and 1 (the dimension's text as "d1=80.0000" or "d2=width*2"), AcDbAssocDimDependencyBody 90
struct AssocDimDependencyBodyData {
    int32_t body_version = 1;           // 90 (AcDbAssocDependencyBody)
    int32_t base_version = 1;           // 90 (AcDbImpAssocDimDependencyBodyBase)
    std::string dimension_text;         // 1: name=expression
    int32_t version = 1;                // 90 (AcDbAssocDimDependencyBody)
};

// The nodes of a 2D constraint group (AcConstraintGroupNode's classes): a node is its AutoCAD class and its groups. Every
// node starts with its number (90), the numbers of the nodes it is connected to (90 count + 90 each) and 70 flags; a geometry
// node then has 330 (the geometry dependency it stands for) and 90, a constraint node 90 and two 290
struct AssocConstraintNodeHead {
    int32_t id = 0;                     // 90
    std::vector<int32_t> connected;     // 90 count + 90 each
    int16_t flags = 0;                  // 70: 0, 1 or 4
    int32_t reserved = 0;               // 90 (0 in every file)
};
struct AssocConstraintGeometryHead {
    AssocConstraintNodeHead head;
    std::string dependency = "0";       // 330: the ACDBASSOCGEOMDEPENDENCY of the geometry (0 for a datum line and an implicit point)
};
struct AssocConstraintHead {
    AssocConstraintNodeHead head;
    bool flag_290_a = false;            // 290
    bool flag_290_b = true;             // 290
};

struct AcConstrainedBoundedLine {       // a line of the drawing: its line and its two end points
    AssocConstraintGeometryHead node;
    Point3 origin;                      // 10
    Point3 direction;                   // 10
    bool bounded_flag = false;          // 290
    Point3 start;                       // 10
    Point3 end;                         // 11
};
struct AcConstrainedImplicitPoint {     // the end point (280: 0 start, 1 end, 2 middle, 3 centre) of a curve
    AssocConstraintGeometryHead node;
    int16_t kind = 0;                   // 280
    int32_t value_90 = -1;              // 90 (-1 in every file)
    int32_t curve = 0;                  // 90: the number of the curve the point belongs to
};
struct AcConstrainedDatumLine {         // the datum line of a horizontal or vertical constraint
    AssocConstraintGeometryHead node;
    Point3 origin;                      // 10
    Point3 direction;                   // 10
};
struct AcConstrained2PointsConstructionLine {
    AssocConstraintGeometryHead node;
    Point3 origin;                      // 10
    Point3 direction;                   // 10
};
struct AcConstrainedArc {
    AssocConstraintGeometryHead node;
    Point3 center;                      // 10
    Point3 normal;                      // 10
    Point3 reference;                   // 10
    double radius = 0.0;                // 40
    double start_angle = 0.0;           // 40
    double end_angle = 0.0;             // 40
    double value_40 = 0.0;              // 40, 2013 and later (version 2); 0 in every file
    Point3 start;                       // 10
    Point3 end;                         // 11
};
struct AcConstrainedCircle {
    AssocConstraintGeometryHead node;
    Point3 center;                      // 10
    Point3 normal;                      // 10
    Point3 reference;                   // 10
    double radius = 0.0;                // 40
    double start_angle = 0.0;           // 40
    double end_angle = 0.0;             // 40
    double value_40 = 0.0;              // 40, 2013 and later (version 2); 0 in every file
};
// The constraints with no groups of their own
struct AcPointCurveConstraint { AssocConstraintHead node; };
struct AcPointCoincidenceConstraint { AssocConstraintHead node; };
struct AcParallelConstraint { AssocConstraintHead node; };
struct AcPerpendicularConstraint { AssocConstraintHead node; };
struct AcColinearConstraint { AssocConstraintHead node; };
struct AcTangentConstraint { AssocConstraintHead node; };
struct AcEqualLengthConstraint { AssocConstraintHead node; };
struct AcEqualRadiusConstraint { AssocConstraintHead node; };
struct AcMidPointConstraint { AssocConstraintHead node; };
struct AcCenterPointConstraint { AssocConstraintHead node; };
struct AcFixedConstraint { AssocConstraintHead node; };
struct AcHorizontalConstraint {
    AssocConstraintHead node;
    int32_t datum_line = 0;             // 90: the number of the datum line node
};
struct AcVerticalConstraint {
    AssocConstraintHead node;
    int32_t datum_line = 0;             // 90
};
// The dimensional constraints: two 340 (the value dependency of the dimension and the one of its variable) and 280
struct AcDistanceConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280: 0 or 1; 1 has the direction
    std::optional<Point3> direction;    // 10 (when kind is 1)
};
struct AcAngleConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280
};
struct Ac3PointAngleConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280
};
struct AcRadiusDiameterConstraint {
    AssocConstraintHead node;
    std::string first_dependency = "0"; // 340
    std::string second_dependency = "0";// 340
    int16_t kind = 0;                   // 280
};

using AssocConstraintNode = std::variant<
    AcConstrainedBoundedLine, AcConstrainedImplicitPoint, AcConstrainedDatumLine, AcConstrained2PointsConstructionLine,
    AcConstrainedArc, AcConstrainedCircle, AcPointCurveConstraint, AcPointCoincidenceConstraint, AcParallelConstraint,
    AcPerpendicularConstraint, AcColinearConstraint, AcTangentConstraint, AcEqualLengthConstraint, AcEqualRadiusConstraint,
    AcMidPointConstraint, AcCenterPointConstraint, AcFixedConstraint, AcHorizontalConstraint, AcVerticalConstraint,
    AcDistanceConstraint, AcAngleConstraint, Ac3PointAngleConstraint, AcRadiusDiameterConstraint>;

// AutoCAD's class name of a node ("AcPointCurveConstraint") and the number of the node
const char* assoc_constraint_node_class(const AssocConstraintNode& node);
int32_t assoc_constraint_node_id(const AssocConstraintNode& node);
const AssocConstraintNodeHead& assoc_constraint_node_head(const AssocConstraintNode& node);
AssocConstraintNodeHead& assoc_constraint_node_head(AssocConstraintNode& node);

// ACDBASSOC2DCONSTRAINTGROUP: the base AcDbAssocAction (whose own dependency list is empty) and, in the same subclass, the
// group (AutoCAD writes no marker of its own): the plane, the geometry dependencies the group owns and the graph of nodes
struct Assoc2dConstraintGroupData {
    AssocActionBase action;
    int32_t version = 2;                // 90
    int16_t flags = 0;                  // 70
    Point3 plane_origin;                // 10
    Point3 plane_x_axis{1.0, 0.0, 0.0}; // 10
    Point3 plane_y_axis{0.0, 1.0, 0.0}; // 10
    std::string workplane = "0";        // 360
    std::vector<std::string> dependencies;  // 90 count + 360 (hard owner): the geometry dependencies
    int32_t next_id = 0;                // 90: the highest node number given
    int32_t reserved_1 = 0;             // 90
    int32_t reserved_2 = 0;             // 90
    bool flag = false;                  // 290
    std::vector<AssocConstraintNode> nodes;     // 90 count, then (1 class, 90 number) each, then every node
};

// ---- the other action bodies ----
struct AssocOffsetSurfaceActionBodyData {   // AcDbAssocOffsetSurfaceActionBody 90, 290
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    int32_t version = 0;
    bool flag = true;                   // 290
};
struct AssocTrimSurfaceActionBodyData {     // AcDbAssocTrimSurfaceActionBody 90, 290, 290, 40
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    bool flag_290_a = false;            // 290
    bool flag_290_b = false;            // 290
    double value = 0.0;                 // 40
};
struct AssocPatchSurfaceActionBodyData {    // AcDbAssocPatchSurfaceActionBody 90
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};
struct AssocNetworkSurfaceActionBodyData {  // AcDbAssocNetworkSurfaceActionBody 90
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
};
struct AssocBlendSurfaceActionBodyData {    // AcDbAssocBlendSurfaceActionBody 90, 290, 291, 292, 72, 293, 294, 73
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    bool flag_290 = false;
    bool flag_291 = false;
    bool flag_292 = false;
    int16_t value_72 = 0;
    bool flag_293 = false;
    bool flag_294 = false;
    int16_t value_73 = 0;
};
struct AssocExtendSurfaceActionBodyData {   // AcDbAssocExtendSurfaceActionBody 90, 280
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 0;
    int16_t value_280 = 0;
};
struct AssocFilletSurfaceActionBodyData {   // AcDbAssocFilletSurfaceActionBody 90, 70, two 2D points
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    int32_t version = 0;
    int16_t value_70 = 0;
    Point2 first_point;                 // 10
    Point2 second_point;                // 10
};
// ACDBASSOCEDGEFILLETACTIONBODY and ACDBASSOCEDGECHAMFERACTIONBODY share one subclass, AcDbAssocEdgeFilletChamferActionBody
struct AssocEdgeFilletChamferActionBodyData {
    AssocActionBodyHead head;
    AssocSurfaceActionBody surface;
    AssocPathBasedSurfaceActionBody path_based;
    int32_t version = 1;                // 90
    std::optional<PersistentSubentityId> subentity_id;  // 290 and the id (1 and its groups)
    std::vector<Point3> first_points;   // 90 count + 10
    std::vector<Point3> second_points;  // 90 count + 10
    std::vector<Point3> third_points;   // 90 count + 10
};
struct AssocRestoreEntityStateActionBodyData {  // AcDbAssocRestoreEntityStateActionBody 90, 330
    AssocActionBodyHead head;
    int32_t version = 0;
    std::string dependency = "0";       // 330
};
// ACDBASSOCIMPLIEDSURFACEORSOLIACTIONBODY (AutoCAD 2025's trim-surface result, doc/facit/*_surfaces_trim_trimmed.dxf): the
// action body's head (version 1 without parameters up to 2010, 2 from 2013) and AcDbAssocImpliedSurfaceOrSolidActionBody 90
struct AssocImpliedSurfaceOrSolidActionBodyData {
    AssocActionBodyHead head;
    int32_t version = 0;
};
// A body of AcDbAssocActionBody's own 90 and 330 after its version 2 form (or after the parameters of version 1), as the
// aligned dimension's: shared by the dimensions and the multileader
struct AssocDimensionBodyBase {
    AssocActionBodyHead head;
    int32_t dimension_flag = 0;         // 90
    std::string dimension_dependency = "0";     // 330
    int32_t version = 0;                // 90 (the own subclass)
};
struct AssocRotatedDimActionBodyData {      // ACDBASSOCROTATEDDIMACTIONBODY: 90, 330, 330
    AssocDimensionBodyBase body;
    std::string first_pointer = "0";
    std::string second_pointer = "0";
};
struct Assoc3PointAngularDimActionBodyData {    // ACDBASSOC3POINTANGULARDIMACTIONBODY: 90, 330, 330, 330
    AssocDimensionBodyBase body;
    std::string first_pointer = "0";
    std::string second_pointer = "0";
    std::string third_pointer = "0";
};
struct AssocOrdinateDimActionBodyData {     // ACDBASSOCORDINATEDIMACTIONBODY: AutoCAD's own subclass name is ACDBASSOC3POINTANGULARDIMACTIONBODY: 90, 330, 330
    AssocDimensionBodyBase body;
    std::string first_pointer = "0";
    std::string second_pointer = "0";
};
struct AssocDiametricDimActionBodyData {    // ACDBASSOCDIAMETRICDIMACTIONBODY: 90, 330
    AssocDimensionBodyBase body;
    std::string pointer = "0";
};
struct AssocRadialDimActionBodyData {       // ACDBASSOCRADIALDIMACTIONBODY: 90, 330
    AssocDimensionBodyBase body;
    std::string pointer = "0";
};
struct AssocMLeaderActionBodyData {         // ACDBASSOCMLEADERACTIONBODY: 90, 90, 90, 330
    AssocDimensionBodyBase body;
    int32_t value_90_a = 0;
    int32_t value_90_b = 0;
    std::string pointer = "0";
};

// ---- the other action parameters ----
struct AssocAsmBodyActionParamData {        // ACDBASSOCASMBODYACTIONPARAM: AcDbAssocAsmBodyActionParam 90, 290
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;
    bool flag = false;                  // 290
};
struct AssocFaceActionParamData {           // ACDBASSOCFACEACTIONPARAM: the ASM body part, then AcDbAssocFaceActionParam 90, 90
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t asm_version = 0;            // 90 (AcDbAssocAsmBodyActionParam)
    bool asm_flag = false;              // 290
    int32_t version = 0;                // 90
    int32_t value_90 = 0;               // 90
};
struct AssocTrimmingPathActionParamData {   // ACDBASSOCTRIMMINGPATHACTIONPARAM: compound, AcDbAssocPathActionParam 90, own 90 + point + 90
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
    int32_t path_version = 0;           // 90 (AcDbAssocPathActionParam)
    int32_t version = 0;                // 90
    Point3 direction;                   // 10
    int32_t value_90 = 0;               // 90
};
// ACDBASSOCTRIMMINGBODYACTIONPARAM: the parameter of a surface SURFTRIM cuts with (doc/facit/fas9_assoc_history_surf.scr, 2000 …
// 2018): the face parameter's subclasses (AcDbAssocAsmBodyActionParam 90, 290; AcDbAssocFaceActionParam 90, 90), then
// AcDbAssocTrimmingBodyActionParam 90 and 280 (0 for a plane cut by a vertical surface, 1 for two walls that trim each other)
struct AssocTrimmingBodyActionParamData {
    AssocFaceActionParamData face;      // the subclasses of ACDBASSOCFACEACTIONPARAM
    int32_t version = 0;                // 90 (AcDbAssocTrimmingBodyActionParam)
    int32_t value_280 = 0;              // 280
};
struct AssocEntityCloneActionBodyData {     // ACDBASSOCENTITYCLONEACTIONBODY: the body head and AcDbAssocEntityCloneActionBody 90 (format not established: no AutoCAD file has one)
    AssocActionBodyHead head;
    int32_t version = 0;
};
struct AssocCompoundActionParamData {       // ACDBASSOCCOMPOUNDACTIONPARAM: only the two base subclasses
    AssocActionParamBase param;
    AssocCompoundActionParamBase compound;
};
struct AssocObjectActionParamData {         // ACDBASSOCOBJECTACTIONPARAM: the base subclasses and AcDbAssocObjectActionParam 90
    AssocActionParamBase param;
    AssocSingleDependencyActionParamBase single;
    int32_t version = 0;
};

// ---- the arrays ----
// The parameters object an array body names (1, then 90): the class decides which array it is
enum class AssocArrayKind : uint8_t { Rectangular, Polar, Path, Modify };
const char* assoc_array_parameters_class(AssocArrayKind kind);
bool assoc_array_kind_of(const std::string& class_name, AssocArrayKind& kind);
struct AssocArrayParameters {
    AssocArrayKind kind = AssocArrayKind::Rectangular;
    int32_t version = 0;                // 90 after the class name
};
// AcDbAssocArrayItem: 90 version, 90 item, 90 row, 90 level, 90 flags (bit 4: the placement is a 4 x 4 matrix of 16 values (40),
// else a point (11); bit 16: a second pointer), then 330 for the item's object (and 330 for its modify body)
struct AssocArrayItem {
    int32_t version = 0;                // 90
    int32_t item_index = 0;             // 90: the item (column or position)
    int32_t row_index = 0;              // 90
    int32_t level_index = 0;            // 90
    int32_t flags = 8;                  // 90
    Point3 position;                    // 11 (flags without bit 4)
    std::vector<double> matrix;         // 16 x 40 (flags with bit 4): a 4 x 4 matrix, row by row
    std::string entity = "0";           // 330
    std::string modify_body = "0";      // 330 (flags with bit 16)
};
struct AssocArrayBody {                     // AcDbAssocArrayActionBody
    int32_t version = 1;                // 90
    AssocArrayParameters parameters;    // 1 class name, 90
    std::string item_class = "AcDbAssocArrayItem";   // 1
    std::vector<AssocArrayItem> items;  // 90 count, then a subclass of each
    std::vector<double> matrix;         // 16 x 40 after the last item (the array's placement)
};
struct AssocArrayActionBodyData {           // ACDBASSOCARRAYACTIONBODY
    AssocActionBodyHead head;
    AssocArrayBody array;
};
struct AssocArrayModifyActionBodyData {     // ACDBASSOCARRAYMODIFYACTIONBODY: the array part, then AcDbAssocArrayModifyActionBody 5 x 90
    AssocActionBodyHead head;
    AssocArrayBody array;
    int32_t version = 0;                // 90
    int32_t value_90_a = 1;
    int32_t value_90_b = 1;
    int32_t value_90_c = 0;
    int32_t value_90_d = 0;
};

} // namespace hdxf
