#pragma once

// The dynamic block elements.

#include "core.hpp"
#include "eval_expr.hpp"

namespace hdxf {

// --- 9.9 evaluation graph and dynamic block bases ---
//
// The evaluation graph (ACAD_EVALUATION_GRAPH) and the bases the dynamic block elements are made of (plan
// 2026-10-27-fas-9-typer.md, step 9.9; B109). AutoCAD's files are the only source of the group lists: the DXF reference
// does not document any of these records, so the field names are the ones the observed values suggest, and a group whose
// meaning no file shows keeps its code in its name (`flag_280`). The values AutoCAD writes in every file are the defaults.
//
// The record types the step types are the graph (EvaluationGraphData), BLOCKGRIPLOCATIONCOMPONENT, BLOCKLINEARPARAMETER,
// BLOCKLINEARGRIP, BLOCKVISIBILITYPARAMETER, BLOCKVISIBILITYGRIP, BLOCKSCALEACTION, ACDB_BLOCKREPRESENTATION_DATA and
// ACDB_DYNAMICBLOCKPURGEPREVENTER_VERSION. The element types are made of the shared parts below, one subclass each
// (AcDbEvalExpr is EvalExprData); step 9.13 builds the other parameters, grips and actions of the Block Editor from the
// same parts. By convention a data type that holds an AcDbEvalExpr has it as its member `eval`
// (Drawing::add_dynamic_block_element() sets its node id).
//
// Ids: a graph node has an `index` (its place in the graph's node list: the 91 of the node and what an edge's `from`
// and `to` are), an `id` (95: the node's own number, which stays when other nodes are removed; it is the 90 of the element
// the node owns, and what the elements' 91/92/93 … groups name) and the `object` (360: the handle of that element). A
// handle and an id are never the same thing.

// AcDbBlockElement: the part every dynamic block parameter, grip and action has after its AcDbEvalExpr
struct BlockElementData {
    std::string name;                   // 300: "Linear", "End Grip", "Visibility State"
    // 98 and 99 are the element's class version and build, which AutoCAD writes after the DXF version it saves: 25 and 104 in
    // 2000 and 2004, 33 and 327 from 2007 (AutoCAD 2025's files; 329 in the ACadSharp samples). Absent (a record built
    // in memory): the values of the version it is saved as
    std::optional<int32_t> version;     // 98
    std::optional<int32_t> maintenance; // 99
    int32_t flags = 0;                  // 1071 (class data, not XDATA): 0 in every AutoCAD file
};

// AcDbBlockParameter
struct BlockParameterData {
    int16_t show_properties = 1;        // 280: 1 in every AutoCAD file
    int16_t chain_actions = 0;          // 281: 0 in every AutoCAD file
};

// A grip's or an action's link to a property of a parameter: the id of the node it is connected to and the property's name
struct BlockPropertyLink {
    int32_t node_id = 0;                // 91 … 95 and 92 … 96 by the slot
    std::string name;                   // 301 … 305: "DisplacementX", "UpdatedBaseX"
};

// AcDbBlock1PtParameter: a parameter with one point. Its two slots are present (flag 170/171 not 0) with the
// connected node's id and the property's name (91/301, 92/302), or absent (a flag 0 and nothing after it)
struct Block1PtParameterData {
    Point3 position;                    // 1010
    int32_t grip_node_id = 0;           // 93: the node id of the grip
    std::array<std::optional<BlockPropertyLink>, 2> links;
};

// AcDbBlock2PtParameter: a parameter with two points. 170 is the number of 91 (grip node ids; 4 in every AutoCAD file);
// its four property slots are present or absent like the 1Pt ones (flags 171 … 174, ids 92 … 95, names 301 … 304)
struct Block2PtParameterData {
    Point3 first;                       // 1010
    Point3 second;                      // 1011
    std::vector<int32_t> grip_node_ids; // 170 (the count), 91
    std::array<std::optional<BlockPropertyLink>, 4> links;
    int16_t flag_177 = 0;               // 177
};

// AcDbBlockGrip: the node ids of the two components that hold the grip's location (BLOCKGRIPLOCATIONCOMPONENT) and where
// the grip is
struct BlockGripData {
    int32_t x_node_id = 0;              // 91
    int32_t y_node_id = 0;              // 92
    Point3 location;                    // 1010 (class data)
    int16_t flag_280 = 1;               // 280: 1 for linear, alignment and XY grips, 0 for rotation, flip and visibility
    int32_t value_93 = -1;              // 93: -1 in every AutoCAD file
};

// AcDbBlockAction: the graph nodes the action is connected to (91, counted by 70), the entities it works on (330, counted by
// 71) and where its glyph is. 70 is the number of 91 (step 9.13 found lists of them; step 9.9's files all had none)
struct BlockActionData {
    std::vector<int32_t> node_ids;      // 70 (the count), 91: node ids of the graph (the actions and parameters it is connected to)
    std::vector<std::string> selection; // 71 (the count), 330: handles of entities in the block (soft pointers)
    Point3 location;                    // 1010 (class data)
};

// AcDbBlockActionWithBasePt: an action with a base point; the two components 92/93 name hold its X and Y
struct BlockActionBasePointData {
    int32_t x_node_id = 0;              // 92
    int32_t y_node_id = 0;              // 93
    std::string x_name;                 // 301: "UpdatedBaseX"
    std::string y_name;                 // 302
    Point3 base_point;                  // 1011
    int16_t flag_280 = 1;               // 280
    Point3 point_1012;                  // 1012
};

// BLOCKGRIPLOCATIONCOMPONENT (AcDbEvalExpr, AcDbBlockGripExpr): one coordinate of a grip's location, which the graph
// computes from a property of a parameter. The expression is "" and its value the coordinate (a real: tag 40; a flip
// state is an int16: tag 70)
struct BlockGripLocationData {
    EvalExprData eval;
    int32_t parameter_node_id = 0;      // 91: the node id of the parameter whose property it is
    std::string property_name;          // 300: "UpdatedBaseX", "UpdatedEndY", "UpdatedFlip"
};

// BLOCKLINEARPARAMETER (AcDbBlockLinearParameter after the 2Pt parameter). The value set's list (175 and the groups 144) is
// typed since step 9.13: AutoCAD 2025's dynamic block samples have lists in the linear parameters and the linear constraints
struct BlockLinearParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string label;                  // 305: the distance's name ("HDXF_Length")
    std::string description;            // 306
    double label_offset = 0.0;          // 140
    std::string expression;             // 307
    int32_t value_set_flags = 1;        // 96
    std::array<double, 3> constraints = {0.0, 0.0, 0.0};    // 141, 142, 143
    std::vector<double> values;         // 175 (the count), 144: the list of values the parameter can have (step 9.13)
};

// BLOCKLINEARGRIP (AcDbBlockLinearGrip): the grip's movement vector (140, 141, 142: not its location, which is 1010)
struct BlockLinearGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
    Point3 vector;                      // 140, 141, 142
};

// One visibility state of a BLOCKVISIBILITYPARAMETER: its name, the entities that are visible in it (332, counted by 94)
// and the elements (parameters, grips, actions) it applies to (333, counted by 95). A state can have none of either
struct BlockVisibilityState {
    std::string name;                   // 303
    std::vector<std::string> entities;  // 94 (the count), 332: handles of entities in the block
    std::vector<std::string> elements;  // 95 (the count), 333: handles of BLOCK* objects in the graph
};

// BLOCKVISIBILITYPARAMETER (AcDbBlockVisibilityParameter after the 1Pt parameter)
struct BlockVisibilityParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    int16_t flag_281 = 1;               // 281
    std::string name;                   // 301
    std::string description;            // 302
    int32_t value_91 = 0;               // 91
    std::vector<std::string> entities;  // 93 (the count), 331: the entities that have visibility states (soft pointers)
    std::vector<BlockVisibilityState> states;   // 92 (the count), then each state
};

// BLOCKVISIBILITYGRIP (AcDbBlockVisibilityGrip: no group of its own)
struct BlockVisibilityGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKSCALEACTION (AcDbBlockScaleAction after the action with a base point): the node ids and names of the three
// properties it sets (94/303 Scale, 95/304 XScale, 96/305 YScale)
struct BlockScaleActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    BlockActionBasePointData base_point_action;
    std::array<BlockPropertyLink, 3> properties;
};

// --- 9.13 dynamic blocks: the remaining parameters, grips, actions and constraints ---
//
// Step 9.13 of phase 9 (plan 2026-10-27-fas-9-typer.md, F9.9; B109): the Block Editor's other record types, each with its own
// data type made of the shared parts of step 9.9 (BlockElementData, BlockParameterData, Block1PtParameterData,
// Block2PtParameterData, BlockGripData, BlockActionData, BlockActionBasePointData) and the fields of its own subclass. The groups
// are what AutoCAD 2025's own dynamic block libraries have (the DWG samples of AutoCAD's Sample/Dynamic Blocks, saved by the core
// as DXF 2000 … 2018 in ASCII and binary: doc/facit/dyntypes, script fas9_dynamic_types.scr); the DXF reference documents none of
// them. A field without a role the files show has the code of its group in its name (`flag_280`, `text_309`). By the convention of
// step 9.9 a type that holds an AcDbEvalExpr has it as its member `eval`; the node ids a record holds are graph ids, never handles.
//
// Not typed, because no AutoCAD file has the record and no group list can be derived from its siblings: BLOCKANGULARCONSTRAINTPARAMETER,
// BLOCKANGULARCONSTRAINTPARAMETERENTITY, BLOCKDIAMETRICCONSTRAINTPARAMETER (registered in CLASSES or named by the plan only). The
// two constraint parameters that follow from a sibling's chain (BLOCKALIGNEDCONSTRAINTPARAMETER, BLOCKLINEARCONSTRAINTPARAMETER)
// are typed from it and are not verified by an AutoCAD record.

// A parameter's value set: the flags (96 or 97), three limits (minimum, maximum, increment) and a list of values. In the distance
// family (linear, the linear constraints, radial) the values are the groups 144 after the count 175; no file shows the group of
// a value in the angle and the XY/polar parameters, whose sets are read only when they have no value
struct BlockValueSet {
    int32_t flags = 0;                          // 96 / 97: 0 none, 1 list, 4 increment … as AutoCAD sets them
    std::array<double, 3> limits = {0.0, 0.0, 0.0};   // 141 142 143 (145 146 147, 146 147 148, 142 143 144 in the others)
    std::vector<double> values;                 // 175/176 (the count) and the values: 144
};

// The own part of the distance family (the linear parameter's AcDbBlockLinearParameter after the 2Pt parameter, the linear
// constraints', the radial constraint's): its labels, the label offset, the expression and the value set
struct BlockDistancePart {
    std::string label;                          // 305
    std::string description;                    // 306
    double label_offset = 0.0;                  // 140
    std::string expression;                     // 307
    BlockValueSet value_set;                    // 96, 141 142 143, 175, 144
};

// BLOCKALIGNMENTPARAMETER (AcDbBlockAlignmentParameter after the 2Pt parameter)
struct BlockAlignmentParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    int16_t flag_280 = 1;               // 280
};

// BLOCKALIGNMENTGRIP (AcDbBlockAlignmentGrip): the grip's movement vector (140 141 142)
struct BlockAlignmentGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
    Point3 vector;
};

// BLOCKBASEPOINTPARAMETER (AcDbBlockBasepointParameter after the 1Pt parameter)
struct BlockBasepointParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    Point3 point_1011;
    Point3 point_1012;
};

// BLOCKFLIPPARAMETER (AcDbBlockFlipParameter after the 2Pt parameter)
struct BlockFlipParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string label;                  // 305: "Side"
    std::string description;            // 306: "Flips the symbol"
    std::string text_307;               // "Other Side"
    std::string text_308;               // "Arrow Side"
    Point3 point_1012;
    std::string property_name;          // 309: "UpdatedFlip"
    int32_t flag_96 = 0;                // 96
};

// BLOCKFLIPGRIP (AcDbBlockFlipGrip): the movement vector (140 141 142) and a node id (93)
struct BlockFlipGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
    Point3 vector;
    int32_t value_93 = 0;               // 93 after the vector
};

// BLOCKFLIPACTION (AcDbBlockFlipAction): four property links (the ids 92 … 95, the names 301 … 304: "Flip", "UpdatedFlip",
// "UpdatedBase", "UpdatedEnd")
struct BlockFlipActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 4> links;
};

// BLOCKLOOKUPPARAMETER (AcDbBlockLookUpParameter after the 1Pt parameter)
struct BlockLookupParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    std::string label;                  // 303: "Size"
    std::string description;            // 304: "Select the nut size"
    int32_t action_node_id = 0;         // 94: the node id of the lookup action
};

// BLOCKLOOKUPGRIP (AcDbBlockLookUpGrip: no group of its own)
struct BlockLookupGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// One column of a lookup table: the property of a parameter whose value the row decides (303 94 95 96 282 305 281 304)
struct BlockLookupColumn {
    std::string name;                   // 303
    int32_t node_id = 0;                // 94: the node id of the parameter
    int32_t value_type = 40;            // 95: the type of the values (AcDbEvalVariant: 40 real, 1 text)
    int32_t unit_type = 0;              // 96: 2 for a distance, 0 none
    int16_t flag_282 = 0;               // 282: 1 for the column that is looked up, 0 for the inputs
    std::string category = "Custom";    // 305
    int16_t flag_281 = 0;               // 281
    std::string property_name;          // 304: "UpdatedDistance", "lookupString", "VisibilityState"
};

// BLOCKLOOKUPACTION (AcDbBlockLookupAction after the action): the lookup table. 92 is the number of rows and 93 the number of
// columns; the 302 are the cells row by row, every row with one cell per column
struct BlockLookupActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::string text_301;               // 301: "" in every file
    std::vector<std::vector<std::string>> rows;     // 92 (the count), 302 × the columns
    std::vector<BlockLookupColumn> columns;         // 93 (the count)
    int16_t flag_280 = 1;               // 280
};

// BLOCKMOVEACTION (AcDbBlockMoveAction): the two property links (92/301 and 93/302: "XDelta"/"YDelta" or "EndXDelta"/"EndYDelta"
// …), the distance multiplier and the angle offset of the action, and a flag
struct BlockMoveActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 2> links;     // 92 301 / 93 302
    double distance_multiplier = 1.0;   // 140
    double angle_offset = 0.0;          // 141: radians
    int16_t flag_280 = 0;               // 280
};

// BLOCKPOINTPARAMETER (AcDbBlockPointParameter after the 1Pt parameter)
struct BlockPointParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    std::string label;                  // 303: "Position"
    std::string description;            // 304
    Point3 point_1011;
};

// BLOCKPOLARPARAMETER (AcDbBlockPolarParameter after the 2Pt parameter): the distance and the angle with a labelled value set
// each; the value sets are the groups 96 … 175 and 97 … 176
struct BlockPolarParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string distance_label;         // 305: "Length"
    std::string distance_description;   // 306
    std::string angle_label;            // 307: "Angle"
    std::string angle_description;      // 308
    double label_offset = 0.0;          // 140
    std::string text_309;               // 309
    BlockValueSet set_96;               // 96, 141 142 143, 175
    std::string text_410;               // 410
    BlockValueSet set_97;               // 97, 145 146 147, 176
};

// BLOCKPOLARGRIP (AcDbBlockPolarGrip: no group of its own)
struct BlockPolarGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKPOLARSTRETCHACTION (AcDbBlockPolarStretchAction after the action): six property links (92/301 … 97/306), the frame (73
// and its 2D points 1011), the entities (74, 332), and the groups 72, 75, 76, 77, 78, 140 and 141 whose roles no file shows
struct BlockPolarStretchActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 6> links;     // 92 301, 93 302, 94 303, 95 304, 96 305, 97 306
    std::vector<Point2> frame;          // 73 (the count), 1011
    int16_t flag_72 = 0;                // 72
    std::vector<std::string> entities;  // 74 (the count), 332: entities of the block (soft pointers)
    int16_t value_75 = 0;               // 75
    int16_t value_76 = 0;               // 76
    int16_t value_78 = 0;               // 78
    double real_141 = 1.0;              // 141
    double real_140 = 0.0;              // 140
    int16_t flag_77 = 0;                // 77
};

// A value of a properties table: AutoCAD's AcDbEvalVariant as the table writes it — the tag (170: 40 a real in 140, 1 a text in
// 300, -9999 none)
struct BlockTableValue {
    int16_t tag = -9999;                // 170
    double real = 0.0;                  // 140
    std::string text;                   // 300
};

// A column of a properties table: the parameter whose property it holds (340) and the column's own groups
struct BlockTableColumn {
    std::string parameter;              // 340: the parameter (a hard pointer; "0" none)
    int16_t flag_170 = 0;               // 170
    int16_t flag_171 = -1;              // 171
    std::string text_300;               // 300
    std::string property_name;          // 301: "UserVariable", "UpdatedDistance"
    int32_t node_id = 0;                // 90: the node id of the parameter
    int16_t type_170 = -9999;           // 170
    BlockTableValue value;              // 170, 300 | 140
    bool flag_290 = false;              // 290
    bool flag_291 = true;               // 291
    bool flag_292 = true;               // 292
    bool flag_293 = false;              // 293
    bool flag_294 = false;              // 294
    std::string text_302;               // 302
    std::string pointer_340 = "0";      // 340
};

// A row of a properties table: its number (90) and one value for each column
struct BlockTableRow {
    int32_t index = 0;                  // 90
    std::vector<BlockTableValue> values;
};

// BLOCKPROPERTIESTABLE (AcDbBlockPropertiesTable after the 1Pt parameter): the lookup table of a block table. 91 is the number
// of columns, 92 the number of rows
struct BlockPropertiesTableData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    int32_t version = 2;                // 90
    std::string name;                   // 300: "Block Table1"
    std::string description;            // 301
    std::vector<BlockTableColumn> columns;
    std::vector<BlockTableRow> rows;
    int32_t value_93 = 0;               // 93
    bool flag_290 = false;              // 290
    bool flag_291 = true;               // 291
    bool flag_292 = false;              // 292
};

// BLOCKPROPERTIESTABLEGRIP (no subclass of its own: AcDbBlockGrip is the last)
struct BlockPropertiesTableGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKROTATIONPARAMETER (AcDbBlockRotationParameter after the 2Pt parameter)
struct BlockRotationParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string label;                  // 305: "Arrow Rotation"
    std::string description;            // 306
    Point3 point_1011;
    double label_offset = 0.0;          // 140
    std::string expression;             // 307
    BlockValueSet value_set;            // 96, 141 142 143, 175: the limits are angles in radians
};

// BLOCKROTATIONGRIP (AcDbBlockRotationGrip: no group of its own)
struct BlockRotationGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKROTATEACTION (AcDbBlockRotationAction after the action with a base point): the property link 94/303 ("AngleDelta")
struct BlockRotateActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    BlockActionBasePointData base_point_action;
    BlockPropertyLink angle;            // 94 303
};

// What a stretch action holds for one entity of the block: the entity (331, a soft pointer) and the indexes (94) of its vertices
// the action moves (74 counts them)
struct BlockStretchEntity {
    std::string entity;                 // 331
    std::vector<int32_t> vertices;      // 74 (the count), 94
};

// What a stretch action holds for another action it is connected to: that action's node id (95) and the vertex indexes (76 counts
// the 94)
struct BlockStretchConnection {
    int32_t node_id = 0;                // 95
    std::vector<int32_t> vertices;      // 76 (the count), 94
};

// BLOCKSTRETCHACTION (AcDbBlockStretchAction after the action): the links 92/301 and 93/302, the stretch frame (72 and its 2D
// points 1011), the entities (73), the connections (75), the distance multiplier (140), the angle offset (141) and a flag (280)
struct BlockStretchActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 2> links;     // 92 301 / 93 302: "EndXDelta" / "EndYDelta"
    std::vector<Point2> frame;          // 72 (the count), 1011
    std::vector<BlockStretchEntity> entities;           // 73 (the count)
    std::vector<BlockStretchConnection> connections;    // 75 (the count)
    double distance_multiplier = 1.0;   // 140
    double angle_offset = 0.0;          // 141: radians
    int16_t flag_280 = 0;               // 280
};

// BLOCKUSERPARAMETER (AcDbBlockUserParameter after the 1Pt parameter)
struct BlockUserParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block1PtParameterData one_point;
    int32_t value_90 = 0;               // 90
    std::string variable;               // 330: the ACDBASSOCVARIABLE that holds the value (a soft pointer; "0" none)
    std::string name;                   // 301
    std::string text_309 = "AcDbEvalVariant NULL = -9999";    // 309
    int16_t parameter_type = 0;         // 170
};

// BLOCKXYPARAMETER (AcDbBlockXYParameter after the 2Pt parameter): two labelled distances, each with a value set
struct BlockXYParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string first_label;            // 305: "Height"
    std::string second_label;           // 306: "Width"
    std::string first_description;      // 307
    std::string second_description;     // 308
    double offset_141 = 0.0;            // 141
    double offset_140 = 0.0;            // 140
    std::string text_309;               // 309
    BlockValueSet set_97;               // 97, 146 147 148, 176
    std::string text_410;               // 410
    BlockValueSet set_96;               // 96, 142 143 144, 175
};

// BLOCKXYGRIP (AcDbBlockXYGrip: no group of its own)
struct BlockXYGripData {
    EvalExprData eval;
    BlockElementData element;
    BlockGripData grip;
};

// BLOCKARRAYACTION (AcDbBlockArrayAction after the action): four property links (92 … 95, 301 … 304: "Base", "End",
// "UpdatedBase", "UpdatedEnd") and the two spacings (140, 141)
struct BlockArrayActionData {
    EvalExprData eval;
    BlockElementData element;
    BlockActionData action;
    std::array<BlockPropertyLink, 4> links;
    double value_140 = 1.0;             // 140
    double value_141 = 1.0;             // 141
};

// The constraint parameters (a dimensional constraint of a dynamic block: AcDbBlockConstraintParameter after the 2Pt parameter,
// then the distance part). `dependency` (330) is the ACDBASSOCDEPENDENCY of the constraint (a soft pointer; the dependency
// also has the parameter as its object and is one of the parameter's reactors)
struct BlockHorizontalConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";       // 330
    BlockDistancePart distance;         // AcDbBlockLinearConstraintParameter
};
struct BlockVerticalConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};
// (typed from the chain of the horizontal and vertical ones: AutoCAD's samples have no aligned constraint)
struct BlockAlignedConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};
// (the linear constraint itself, the base of the horizontal, vertical and aligned ones: no AutoCAD sample has a record of it)
struct BlockLinearConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};
// BLOCKRADIALCONSTRAINTPARAMETER (AcDbBlockRadialConstraintParameter after the constraint parameter: the distance part)
struct BlockRadialConstraintParameterData {
    EvalExprData eval;
    BlockElementData element;
    BlockParameterData parameter;
    Block2PtParameterData two_point;
    std::string dependency = "0";
    BlockDistancePart distance;
};

// BLOCKPARAMDEPENDENCYBODY: the body of the ACDBASSOCDEPENDENCY of a constraint parameter (AcDbAssocDependencyBody 90, then
// AcDbImpAssocDimDependencyBodyBase 90 and 1, then AcDbBlockParameterDependencyBody 90). It has no AcDbEvalExpr
struct BlockParameterDependencyBodyData {
    int32_t body_version = 1;           // AcDbAssocDependencyBody 90
    int32_t base_version = 1;           // AcDbImpAssocDimDependencyBodyBase 90
    std::string expression;             // 1: "Head=7.18", the parameter's name and value
    int32_t value_90 = 0;               // AcDbBlockParameterDependencyBody 90
};

// ACDB_DYNAMICBLOCKPROXYNODE (AcDbEvalExpr only): a node of the evaluation graph that stands for an object of an application
// that is not loaded
struct DynamicBlockProxyNodeData {
    EvalExprData eval;
};
// --- end 9.13 ---

} // namespace hdxf
