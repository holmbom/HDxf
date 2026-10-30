#include "DynamicBlockElements.hpp"
#include "dxf_writer.hpp"
#include <cstdint>
#include <string>
#include <vector>

// The writer of the step 9.13 records (phase 9; plan 2026-10-27-fas-9-typer.md, B109; B19, B103, B104): every record is written whole from
// its typed values, in the order of AutoCAD's files (see dynamic_block_parameters.cpp, dynamic_block_actions.cpp and
// dynamic_block_constraints.cpp), with its head and, last, its XDATA. The generic parts of a kind of record are templates in
// DynamicBlockElements.hpp; this file has the own subclasses and the three records that are not of a kind. A pointer to a record that
// is gone is left out of a list with its counts, or is 0 when it is a single pointer (B19); the writer is also what the B12 merge
// compares (the record as read against the record now), so a record that did not change is written as the file had it.

namespace hdxf {
namespace internal {

// ---------------------------------------------------------------------------
// The shared parts
// ---------------------------------------------------------------------------

void write_value_set(RecordWriter& w, int flags_code, int first_limit_code, int count_code, int value_code, const BlockValueSet& set) {
    w.integer(flags_code, set.flags);
    for (int k = 0; k < 3; ++k) w.real(first_limit_code + k, set.limits[static_cast<size_t>(k)]);
    w.integer(count_code, static_cast<int64_t>(set.values.size()));
    if (!set.values.empty() && value_code == 0) {
        w.fail("a value set of group " + std::to_string(flags_code) + " has values: no AutoCAD file shows the group of a value of this set");
        return;
    }
    for (double value : set.values) w.real(value_code, value);
}

void write_distance_part(RecordWriter& w, const BlockDistancePart& d) {
    w.text(305, d.label);
    w.text(306, d.description);
    w.real(140, d.label_offset);
    w.text(307, d.expression);
    write_value_set(w, 96, 141, 175, 144, d.value_set);
}

void write_frame_points(RecordWriter& w, int count_code, int point_code, const std::vector<Point2>& points) {
    w.integer(count_code, static_cast<int64_t>(points.size()));
    for (const Point2& p : points) w.point2(point_code, p);
}

void write_vector_140(RecordWriter& w, const Point3& v) {
    w.real(140, v.x);
    w.real(141, v.y);
    w.real(142, v.z);
}

void write_links_interleaved(RecordWriter& w, int first_id_code, int first_name_code, const BlockPropertyLink* links, size_t n) {
    for (size_t k = 0; k < n; ++k) {
        w.integer(first_id_code + static_cast<int>(k), links[k].node_id);
        w.text(first_name_code + static_cast<int>(k), links[k].name);
    }
}

void write_links_grouped(RecordWriter& w, int first_id_code, int first_name_code, const BlockPropertyLink* links, size_t n) {
    for (size_t k = 0; k < n; ++k) w.integer(first_id_code + static_cast<int>(k), links[k].node_id);
    for (size_t k = 0; k < n; ++k) w.text(first_name_code + static_cast<int>(k), links[k].name);
}

void write_dependency_pointer(RecordWriter& w, const std::string& handle, const std::string& record) {
    if (pointer_alive(w, handle)) {
        w.text(330, handle.empty() ? std::string("0") : handle);
    } else {
        w.warn("the dependency " + handle + " of the constraint parameter " + record + " no longer exists, set to none (0)");
        w.text(330, "0");
    }
}

namespace {

// A list of vertex indexes: the count and the indexes
void write_indexes(RecordWriter& w, int count_code, int code, const std::vector<int32_t>& indexes) {
    w.integer(count_code, static_cast<int64_t>(indexes.size()));
    for (int32_t i : indexes) w.integer(code, i);
}

void write_table_value(RecordWriter& w, const BlockTableValue& v) {
    w.integer(170, v.tag);
    if (v.tag == 40) {
        w.real(140, v.real);
    } else if (v.tag == 1) {
        w.text(300, v.text);
    } else if (v.tag != -9999) {
        w.fail("a value of a properties table has the tag " + std::to_string(v.tag) + ": AutoCAD's files have 40, 1 and -9999");
    }
}

} // namespace

// ---------------------------------------------------------------------------
// The own subclasses of the parameters and grips
// ---------------------------------------------------------------------------

void AlignmentParameterSpec::write_own(RecordWriter& w, const Data& d) { w.integer(280, d.flag_280); }

void BasepointParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.point3(1011, d.point_1011);
    w.point3(1012, d.point_1012);
}

void FlipParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.text(305, d.label);
    w.text(306, d.description);
    w.text(307, d.text_307);
    w.text(308, d.text_308);
    w.point3(1012, d.point_1012);
    w.text(309, d.property_name);
    w.integer(96, d.flag_96);
}

void LookupParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.text(303, d.label);
    w.text(304, d.description);
    w.integer(94, d.action_node_id);
}

void PointParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.text(303, d.label);
    w.text(304, d.description);
    w.point3(1011, d.point_1011);
}

void PolarParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.text(305, d.distance_label);
    w.text(306, d.distance_description);
    w.text(307, d.angle_label);
    w.text(308, d.angle_description);
    w.real(140, d.label_offset);
    w.text(309, d.text_309);
    write_value_set(w, 96, 141, 175, 0, d.set_96);
    w.text(410, d.text_410);
    write_value_set(w, 97, 145, 176, 0, d.set_97);
}

void RotationParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.text(305, d.label);
    w.text(306, d.description);
    w.point3(1011, d.point_1011);
    w.real(140, d.label_offset);
    w.text(307, d.expression);
    write_value_set(w, 96, 141, 175, 0, d.value_set);
}

void XYParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.text(305, d.first_label);
    w.text(306, d.second_label);
    w.text(307, d.first_description);
    w.text(308, d.second_description);
    w.real(141, d.offset_141);
    w.real(140, d.offset_140);
    w.text(309, d.text_309);
    write_value_set(w, 97, 146, 176, 0, d.set_97);
    w.text(410, d.text_410);
    write_value_set(w, 96, 142, 175, 0, d.set_96);
}

void UserParameterSpec::write_own(RecordWriter& w, const Data& d) {
    w.integer(90, d.value_90);
    // the soft pointer to the ACDBASSOCVARIABLE: 0 when that record is gone (B19)
    if (pointer_alive(w, d.variable)) {
        w.text(330, d.variable.empty() ? std::string("0") : d.variable);
    } else {
        w.warn("the variable " + d.variable + " of the user parameter no longer exists, set to none (0)");
        w.text(330, "0");
    }
    w.text(301, d.name);
    w.text(309, d.text_309);
    w.integer(170, d.parameter_type);
}

void AlignmentGripSpec::write_own(RecordWriter& w, const Data& d) { write_vector_140(w, d.vector); }

void FlipGripSpec::write_own(RecordWriter& w, const Data& d) {
    write_vector_140(w, d.vector);
    w.integer(93, d.value_93);
}

void LookupGripSpec::write_own(RecordWriter&, const Data&) {}
void PolarGripSpec::write_own(RecordWriter&, const Data&) {}
void PropertiesTableGripSpec::write_own(RecordWriter&, const Data&) {}
void RotationGripSpec::write_own(RecordWriter&, const Data&) {}
void XYGripSpec::write_own(RecordWriter&, const Data&) {}

// The properties table: a column whose parameter is gone leaves with its value in every row (B19), and the counts follow
void PropertiesTableSpec::write_own(RecordWriter& w, const Data& d) {
    std::vector<size_t> kept;
    for (size_t i = 0; i < d.columns.size(); ++i) {
        if (pointer_alive(w, d.columns[i].parameter)) kept.push_back(i);
    }
    if (kept.size() != d.columns.size()) {
        w.warn("the properties table " + d.name + ": " + std::to_string(d.columns.size() - kept.size()) +
               " column(s) of parameters that no longer exist were removed with their values");
    }
    for (const BlockTableRow& row : d.rows) {
        if (row.values.size() != d.columns.size()) {
            w.fail("a row of the properties table " + d.name + " has " + std::to_string(row.values.size()) + " values for " +
                   std::to_string(d.columns.size()) + " columns");
            return;
        }
    }
    w.integer(90, d.version);
    w.text(300, d.name);
    w.text(301, d.description);
    w.integer(91, static_cast<int64_t>(kept.size()));
    for (size_t i : kept) {
        const BlockTableColumn& c = d.columns[i];
        w.text(340, c.parameter.empty() ? std::string("0") : c.parameter);
        w.integer(170, c.flag_170);
        w.integer(171, c.flag_171);
        w.text(300, c.text_300);
        w.text(301, c.property_name);
        w.integer(90, c.node_id);
        w.integer(170, c.type_170);
        write_table_value(w, c.value);
        w.boolean(290, c.flag_290);
        w.boolean(291, c.flag_291);
        w.boolean(292, c.flag_292);
        w.boolean(293, c.flag_293);
        w.boolean(294, c.flag_294);
        w.text(302, c.text_302);
        w.text(340, c.pointer_340.empty() ? std::string("0") : c.pointer_340);
    }
    w.integer(92, static_cast<int64_t>(d.rows.size()));
    for (const BlockTableRow& row : d.rows) {
        w.integer(90, row.index);
        for (size_t i : kept) write_table_value(w, row.values[i]);
    }
    w.integer(93, d.value_93);
    w.boolean(290, d.flag_290);
    w.boolean(291, d.flag_291);
    w.boolean(292, d.flag_292);
}

// ---------------------------------------------------------------------------
// The own subclasses of the actions
// ---------------------------------------------------------------------------

void FlipActionSpec::write_own(RecordWriter& w, const Data& d) { write_links_grouped(w, 92, 301, d.links.data(), d.links.size()); }

void MoveActionSpec::write_own(RecordWriter& w, const Data& d) {
    write_links_interleaved(w, 92, 301, d.links.data(), d.links.size());
    w.real(140, d.distance_multiplier);
    w.real(141, d.angle_offset);
    w.integer(280, d.flag_280);
}

void ArrayActionSpec::write_own(RecordWriter& w, const Data& d) {
    write_links_grouped(w, 92, 301, d.links.data(), d.links.size());
    w.real(140, d.value_140);
    w.real(141, d.value_141);
}

void RotateActionSpec::write_own(RecordWriter& w, const Data& d) {
    w.integer(94, d.angle.node_id);
    w.text(303, d.angle.name);
}

// The entities of a stretch whose record is gone leave with their vertices (B19); the counts follow
void StretchActionSpec::write_own(RecordWriter& w, const Data& d) {
    write_links_interleaved(w, 92, 301, d.links.data(), d.links.size());
    write_frame_points(w, 72, 1011, d.frame);
    std::vector<const BlockStretchEntity*> kept;
    for (const BlockStretchEntity& e : d.entities) {
        if (pointer_alive(w, e.entity)) kept.push_back(&e);
    }
    if (kept.size() != d.entities.size()) {
        w.warn(std::to_string(d.entities.size() - kept.size()) +
               " entity pointer(s) of a stretch action lead to records that no longer exist and are left out");
    }
    w.integer(73, static_cast<int64_t>(kept.size()));
    for (const BlockStretchEntity* e : kept) {
        w.text(331, e->entity.empty() ? std::string("0") : e->entity);
        write_indexes(w, 74, 94, e->vertices);
    }
    w.integer(75, static_cast<int64_t>(d.connections.size()));
    for (const BlockStretchConnection& k : d.connections) {
        w.integer(95, k.node_id);
        write_indexes(w, 76, 94, k.vertices);
    }
    w.real(140, d.distance_multiplier);
    w.real(141, d.angle_offset);
    w.integer(280, d.flag_280);
}

void PolarStretchActionSpec::write_own(RecordWriter& w, const Data& d) {
    write_links_interleaved(w, 92, 301, d.links.data(), d.links.size());
    write_frame_points(w, 73, 1011, d.frame);
    w.integer(72, d.flag_72);
    const std::vector<std::string> live = members_still_there(w, d.entities);
    if (live.size() != d.entities.size()) {
        w.warn(std::to_string(d.entities.size() - live.size()) +
               " entity pointer(s) of a polar stretch action lead to records that no longer exist and are left out");
    }
    w.integer(74, static_cast<int64_t>(live.size()));
    for (const std::string& h : live) w.text(332, h);
    w.integer(75, d.value_75);
    w.integer(76, d.value_76);
    w.integer(78, d.value_78);
    w.real(141, d.real_141);
    w.real(140, d.real_140);
    w.integer(77, d.flag_77);
}

void LookupActionSpec::write_own(RecordWriter& w, const Data& d) {
    for (const std::vector<std::string>& row : d.rows) {
        if (row.size() != d.columns.size()) {
            w.fail("a row of the lookup table has " + std::to_string(row.size()) + " cells for " + std::to_string(d.columns.size()) +
                   " columns");
            return;
        }
    }
    w.integer(92, static_cast<int64_t>(d.rows.size()));
    w.integer(93, static_cast<int64_t>(d.columns.size()));
    w.text(301, d.text_301);
    for (const std::vector<std::string>& row : d.rows) {
        for (const std::string& cell : row) w.text(302, cell);
    }
    for (const BlockLookupColumn& c : d.columns) {
        w.text(303, c.name);
        w.integer(94, c.node_id);
        w.integer(95, c.value_type);
        w.integer(96, c.unit_type);
        w.integer(282, c.flag_282);
        w.text(305, c.category);
        w.integer(281, c.flag_281);
        w.text(304, c.property_name);
    }
    w.integer(280, d.flag_280);
}

// ---------------------------------------------------------------------------
// The records that are not of a kind
// ---------------------------------------------------------------------------

void write_block_parameter_dependency_body(RecordWriter& w, const Object& o) {
    const BlockParameterDependencyBodyData& d = o.as_block_parameter_dependency_body();
    w.object_head("BLOCKPARAMDEPENDENCYBODY", o);
    w.subclass("AcDbAssocDependencyBody");
    w.integer(90, d.body_version);
    w.subclass("AcDbImpAssocDimDependencyBodyBase");
    w.integer(90, d.base_version);
    w.text(1, d.expression);
    w.subclass("AcDbBlockParameterDependencyBody");
    w.integer(90, d.value_90);
    w.xdata(o.xdata);
}

void write_dynamic_block_proxy_node(RecordWriter& w, const Object& o) {
    w.object_head("ACDB_DYNAMICBLOCKPROXYNODE", o);
    w.subclass("AcDbEvalExpr");
    write_eval_expr(w, o.as_dynamic_block_proxy_node().eval);
    w.xdata(o.xdata);
}

} // namespace internal
} // namespace hdxf
