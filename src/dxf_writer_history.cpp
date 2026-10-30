#include "SolidHistoryTypes.hpp"
#include "DynamicBlockTypes.hpp"
#include "dxf_writer.hpp"
#include <string>
#include <vector>

// The writer of the step 9.14 records (phase 9; plan 2026-10-27-fas-9-typer.md, B19, B103, B104, B111): every record is written
// whole from its typed values in the order of AutoCAD's own files (see solid_history.cpp), with its head and, last, its XDATA.
// The writer is also what the B12 merge compares: the record as read against the record now, so a record that did not change
// is written as the file had it. A pointer to a record that is gone is 0 (the material), the groups that are numbers
// (the node ids, the edge ids) are written as they are.

namespace hdxf {
namespace internal {

namespace {

void write_part(RecordWriter& w, const ShPartVersion& p) {
    w.integer(90, p.version);
    w.integer(91, p.maintenance);
}

// A hard pointer: 0 for none and for a record that is gone (B19)
void write_pointer(RecordWriter& w, int code, const std::string& handle, const char* what, const Object& o) {
    if (handle.empty() || handle == "0") {
        w.text(code, "0");
    } else if (pointer_alive(w, handle)) {
        w.text(code, handle);
    } else {
        w.warn(std::string(what) + " " + o.handle + ": " + handle + " no longer exists, set to none (0)");
        w.text(code, "0");
    }
}

// The head of a node: the record's head, AcDbEvalExpr, AcDbShHistoryNode and, for the primitives, the empty AcDbShPrimitive
void write_node_head(RecordWriter& w, const char* dxf_name, const Object& o, const EvalExprData& eval,
                     const ShHistoryNodeData& node, bool primitive) {
    w.object_head(dxf_name, o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, eval);
    w.subclass("AcDbShHistoryNode");
    w.integer(90, node.version);
    w.integer(91, node.maintenance);
    for (size_t i = 0; i < node.matrix.size(); ++i) w.real(40 + static_cast<int>(i), node.matrix[i]);
    w.integer(62, node.color);
    w.integer(92, node.history_id);
    write_pointer(w, 347, node.material, dxf_name, o);
    if (primitive) w.subclass("AcDbShPrimitive");
}

// A source geometry: its class id, then its bit stream or its SAT lines (a line of 255 characters or more is its 3s of 255 and
// the 1 that ends it, the rest of it, perhaps empty, as AutoCAD's 3DSOLID has it)
void write_sat(RecordWriter& w, const ShSatSource& sat) {
    w.integer(70, sat.version);
    for (const std::string& line : sat.lines) {
        std::string rest = line;
        while (rest.size() >= 255) {
            w.text(3, rest.substr(0, 255));
            rest.erase(0, 255);
        }
        w.text(1, rest);
    }
}

void write_source(RecordWriter& w, int class_code, int bits_code, const ShSourceGeometry& g) {
    w.integer(class_code, g.class_id);
    if (const ShBitStream* bits = g.bit_stream()) {
        w.integer(bits_code, bits->bits);
        w.bytes(310, bits->data);
    } else if (const ShSatSource* sat = g.sat()) {
        write_sat(w, *sat);
    }
}

void write_matrix(RecordWriter& w, int code, const std::vector<double>& m) {
    for (int i = 0; i < 16; ++i) {
        const double v = i < static_cast<int>(m.size()) ? m[static_cast<size_t>(i)] : (i % 5 == 0 ? 1.0 : 0.0);
        w.real(code, v);
    }
}

void write_sweep_base(RecordWriter& w, const ShSweepBaseData& b) {
    w.subclass("AcDbShSweepBase");
    write_part(w, b.part);
    w.point3(10, b.vector);
    write_source(w, 92, 90, b.profile);
    write_source(w, 93, 90, b.path);
    const SurfaceSweep& s = b.options;
    w.real(42, s.draft_angle);
    w.real(43, s.draft_start);
    w.real(44, s.draft_end);
    w.real(45, s.twist);
    w.real(48, s.scale);
    w.real(49, s.align_angle);
    write_matrix(w, 46, s.sweep_matrix);
    write_matrix(w, 47, s.path_matrix);
    w.boolean(290, s.solid);
    w.integer(70, s.alignment);
    w.integer(71, s.option_71);
    w.boolean(292, s.align_start);
    w.boolean(293, s.bank);
    w.boolean(294, s.base_point_set);
    w.boolean(295, s.sweep_transform_computed);
    w.boolean(296, s.path_transform_computed);
    w.point3(11, s.reference_vector);
}

void write_cylinder_body(RecordWriter& w, const ShCylinderBody& b) {
    w.subclass("AcDbShCylinder");
    write_part(w, b.part);
    w.real(40, b.height);
    w.real(41, b.base_radius_1);
    w.real(42, b.base_radius_2);
    w.real(43, b.top_radius);
}

} // namespace

void write_sh_history(RecordWriter& w, const Object& o) {
    const ShHistoryData& d = o.as_sh_history();
    w.object_head("ACSH_HISTORY_CLASS", o);
    w.subclass("AcDbShHistory");
    w.integer(90, d.version);
    w.integer(91, d.maintenance);
    write_pointer(w, 360, d.graph, "ACSH_HISTORY_CLASS", o);
    w.integer(92, d.last_node_id);
    w.integer(280, d.flag_280);
    w.integer(281, d.flag_281);
    w.xdata(o.xdata);
}

void write_sh_box(RecordWriter& w, const Object& o) {
    const ShBoxData& d = o.as_sh_box();
    write_node_head(w, "ACSH_BOX_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShBox");
    write_part(w, d.part);
    w.real(40, d.length);
    w.real(41, d.width);
    w.real(42, d.height);
    w.xdata(o.xdata);
}

void write_sh_wedge(RecordWriter& w, const Object& o) {
    const ShWedgeData& d = o.as_sh_wedge();
    write_node_head(w, "ACSH_WEDGE_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShWedge");
    write_part(w, d.part);
    w.real(40, d.length);
    w.real(41, d.width);
    w.real(42, d.height);
    w.xdata(o.xdata);
}

void write_sh_sphere(RecordWriter& w, const Object& o) {
    const ShSphereData& d = o.as_sh_sphere();
    write_node_head(w, "ACSH_SPHERE_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShSphere");
    write_part(w, d.part);
    w.real(40, d.radius);
    w.xdata(o.xdata);
}

void write_sh_cylinder(RecordWriter& w, const Object& o) {
    const ShCylinderData& d = o.as_sh_cylinder();
    write_node_head(w, "ACSH_CYLINDER_CLASS", o, d.eval, d.node, true);
    write_cylinder_body(w, d.body);
    w.xdata(o.xdata);
}

void write_sh_cone(RecordWriter& w, const Object& o) {
    const ShConeData& d = o.as_sh_cone();
    write_node_head(w, "ACSH_CONE_CLASS", o, d.eval, d.node, true);
    write_cylinder_body(w, d.body);
    w.subclass("AcDbShCone");
    w.xdata(o.xdata);
}

void write_sh_torus(RecordWriter& w, const Object& o) {
    const ShTorusData& d = o.as_sh_torus();
    write_node_head(w, "ACSH_TORUS_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShTorus");
    write_part(w, d.part);
    w.real(40, d.major_radius);
    w.real(41, d.minor_radius);
    w.xdata(o.xdata);
}

void write_sh_pyramid(RecordWriter& w, const Object& o) {
    const ShPyramidData& d = o.as_sh_pyramid();
    write_node_head(w, "ACSH_PYRAMID_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShPyramid");
    write_part(w, d.part);
    w.real(40, d.height);
    w.integer(92, d.sides);
    w.real(41, d.radius);
    w.real(42, d.top_radius);
    w.xdata(o.xdata);
}

void write_sh_extrusion(RecordWriter& w, const Object& o) {
    const ShExtrusionData& d = o.as_sh_extrusion();
    write_node_head(w, "ACSH_EXTRUSION_CLASS", o, d.eval, d.node, true);
    write_sweep_base(w, d.base);
    w.subclass("AcDbShExtrusion");
    w.xdata(o.xdata);
}

void write_sh_sweep(RecordWriter& w, const Object& o) {
    const ShSweepData& d = o.as_sh_sweep();
    write_node_head(w, "ACSH_SWEEP_CLASS", o, d.eval, d.node, true);
    write_sweep_base(w, d.base);
    w.subclass("AcDbShSweep");
    w.xdata(o.xdata);
}

void write_sh_revolve(RecordWriter& w, const Object& o) {
    const ShRevolveData& d = o.as_sh_revolve();
    write_node_head(w, "ACSH_REVOLVE_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShRevolve");
    write_part(w, d.part);
    w.point3(10, d.axis_point);
    w.point3(11, d.axis_vector);
    w.real(40, d.angle);
    w.real(41, d.start_angle);
    write_source(w, 92, 90, d.profile);
    w.real(43, d.draft_angle);
    w.real(44, d.start_draft_distance);
    w.real(45, d.end_draft_distance);
    w.real(46, d.twist);
    w.boolean(290, d.solid);
    w.boolean(291, d.close_to_axis);
    w.xdata(o.xdata);
}

void write_sh_loft(RecordWriter& w, const Object& o) {
    const ShLoftData& d = o.as_sh_loft();
    write_node_head(w, "ACSH_LOFT_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShLoft");
    write_part(w, d.part);
    w.integer(92, static_cast<int64_t>(d.sections.size()));
    for (const ShSourceGeometry& g : d.sections) write_source(w, 93, 94, g);
    w.integer(95, static_cast<int64_t>(d.guides.size()));
    for (const ShSourceGeometry& g : d.guides) write_source(w, 96, 97, g);
    write_source(w, 98, 99, d.path);
    w.integer(70, d.plane_normal_lofting);
    w.real(41, d.start_draft_angle);
    w.real(42, d.end_draft_angle);
    w.real(43, d.start_draft_magnitude);
    w.real(44, d.end_draft_magnitude);
    w.boolean(290, d.arc_length_parameters);
    w.boolean(291, d.no_twist);
    w.boolean(292, d.align_direction);
    w.boolean(293, d.simple_surfaces);
    w.boolean(294, d.closed);
    w.boolean(295, d.solid);
    w.boolean(296, d.ruled);
    w.boolean(297, d.virtual_guide);
    w.xdata(o.xdata);
}

void write_sh_boolean(RecordWriter& w, const Object& o) {
    const ShBooleanData& d = o.as_sh_boolean();
    write_node_head(w, "ACSH_BOOLEAN_CLASS", o, d.eval, d.node, false);
    w.subclass("AcDbShBoolean");
    write_part(w, d.part);
    w.integer(280, static_cast<int16_t>(d.operation));
    w.integer(92, d.first_node_id);
    w.integer(93, d.second_node_id);
    w.xdata(o.xdata);
}

void write_sh_brep(RecordWriter& w, const Object& o) {
    const ShBrepData& d = o.as_sh_brep();
    write_node_head(w, "ACSH_BREP_CLASS", o, d.eval, d.node, true);
    w.subclass("AcDbShBrep");
    write_part(w, d.part);
    write_sat(w, d.sat);
    w.xdata(o.xdata);
}

void write_sh_chamfer(RecordWriter& w, const Object& o) {
    const ShChamferData& d = o.as_sh_chamfer();
    write_node_head(w, "ACSH_CHAMFER_CLASS", o, d.eval, d.node, false);
    w.subclass("AcDbShChamfer");
    write_part(w, d.part);
    w.integer(92, d.base_node_id);
    w.real(41, d.distance_1);
    w.real(42, d.distance_2);
    w.integer(93, static_cast<int64_t>(d.edge_ids.size()));
    for (int32_t id : d.edge_ids) w.integer(94, id);
    w.integer(95, d.face_id);
    w.xdata(o.xdata);
}

void write_sh_fillet(RecordWriter& w, const Object& o) {
    const ShFilletData& d = o.as_sh_fillet();
    write_node_head(w, "ACSH_FILLET_CLASS", o, d.eval, d.node, false);
    w.subclass("AcDbShFillet");
    write_part(w, d.part);
    w.integer(92, d.base_node_id);
    const int64_t n = static_cast<int64_t>(d.edges.size());
    w.integer(93, n);
    for (const ShFilletEdge& e : d.edges) w.integer(94, e.edge_id);
    w.integer(95, n);
    for (const ShFilletEdge& e : d.edges) w.real(41, e.radius);
    w.integer(96, n);
    for (const ShFilletEdge& e : d.edges) w.real(42, e.value_42);
    w.integer(97, n);
    for (const ShFilletEdge& e : d.edges) w.real(43, e.value_43);
    w.xdata(o.xdata);
}

} // namespace internal
} // namespace hdxf
