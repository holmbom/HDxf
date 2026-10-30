#include "AssocExtended.hpp"
#include <algorithm>
#include <set>

// The 2D constraint group, the variables and the value dependencies (phase 9, step 9.15; plan
// 2026-10-27-fas-9-typer.md, appendix B and F9.12): ACDBASSOC2DCONSTRAINTGROUP, ACDBASSOCVARIABLE and
// ACDBASSOCVALUEDEPENDENCY. What AutoCAD 2025's own files show: doc/facit/fas9_assoc_extended.scr (GCHORIZONTAL, DCLINEAR,
// DCANGULAR, GCCOINCIDENT, GCFIX, GCVERTICAL, DCVERTICAL, DCALIGNED, AUTOCONSTRAIN and -PARAMETERS, in 2000 … 2018 and both
// formats), the older doc/facit/acad_*_dxf_associative.dxf and vertexids files, and the dynamic block sample drawings of
// AutoCAD 2025 (34 constraint groups, 302 variables) opened and saved by AutoCAD's core:
//   ACDBASSOCVARIABLE  AcDbAssocAction (the action base), AcDbAssocVariable: 90 version (2), 1 name, 1 expression, 1 evaluator
//                      ("AcDbCalc:1.0"), 1 description, the value (40, 90 or 1), 290, 290, 90 count + (360, 90) each: the
//                      value dependencies the variable owns (a variable whose expression names another variable)
//   ACDBASSOCVALUEDEPENDENCY  AcDbAssocDependency, AcDbAssocValueDependency: 90 version, 1 name, the value (40 or 90)
//   ACDBASSOC2DCONSTRAINTGROUP  one subclass, AcDbAssocAction, with no marker of the group's own: the base (its dependency
//                      list empty), then 90 version, 70, the plane (three 10), 360, 90 count + 360 (the geometry dependencies
//                      the group owns), 90 (the highest node number), 90, 90, 290, 90 count of nodes, (1 class, 90 number) for
//                      each node, and then the nodes in that order. A node: 90 number, 90 count + 90 each of the nodes it is
//                      connected to, 70 flags, then for geometry 330 (its dependency) and 90, for a constraint 90, 290, 290,
//                      then the class's own groups (below). The dimensional constraints have two 340 (the value dependency
//                      of the dimension and the dependency on the dimension itself) and 280; a horizontal and a vertical
//                      constraint the number of their datum line

namespace hdxf {

const char* assoc_constraint_node_class(const AssocConstraintNode& node) {
    return std::visit([](const auto& n) { return internal::AssocConstraintNodeName<std::decay_t<decltype(n)>>::value; }, node);
}

const AssocConstraintNodeHead& assoc_constraint_node_head(const AssocConstraintNode& node) {
    return std::visit([](const auto& n) -> const AssocConstraintNodeHead& { return n.node.head; }, node);
}

AssocConstraintNodeHead& assoc_constraint_node_head(AssocConstraintNode& node) {
    return std::visit([](auto& n) -> AssocConstraintNodeHead& { return n.node.head; }, node);
}

int32_t assoc_constraint_node_id(const AssocConstraintNode& node) { return assoc_constraint_node_head(node).id; }

namespace internal {

namespace {

template <typename Data>
Data& data_of(Object& o) { return *o.data.get<Boxed<Data>>(); }
template <typename Data>
const Data& data_of(const Object& o) { return *o.data.get<Boxed<Data>>(); }

// ---------------------------------------------------------------------------
// The value of a variable or a value dependency
// ---------------------------------------------------------------------------

bool read_value(GroupCursor& c, AssocEvalValue& v, bool allow_text) {
    v = AssocEvalValue();
    v.code = c.code();
    switch (v.code) {
        case 40: c.require_real(40, v.real); break;
        case 90: c.require_integer(90, v.integer); break;
        case 70: c.require_integer(70, v.integer); break;
        case 1:
            if (!allow_text) { c.fail("a string value is not established here"); return false; }
            c.require_string(1, v.text);
            break;
        default:
            c.fail("a value in group " + std::to_string(v.code) + " is not established (AutoCAD's files have 40, 90 and 1)");
            return false;
    }
    return c.ok();
}

void write_value(RecordWriter& w, const AssocEvalValue& v) {
    switch (v.code) {
        case 90: w.integer(90, v.integer); break;
        case 70: w.integer(70, v.integer); break;
        case 1: w.text(1, v.text); break;
        default: w.real(40, v.real); break;
    }
}

// ---------------------------------------------------------------------------
// ACDBASSOCVARIABLE
// ---------------------------------------------------------------------------

bool read_variable(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocVariableData d;
    if (!read_assoc_action_base(view, ctx, d.action)) return false;
    GroupCursor c(view, "AcDbAssocVariable");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocVariable subclass");
    c.require_integer(90, d.version);
    c.require_string(1, d.name);
    c.require_string(1, d.expression);
    c.require_string(1, d.evaluator);
    c.require_string(1, d.description);
    if (c.ok()) read_value(c, d.value, true);
    c.require_boolean(290, d.flag_290_a);
    c.require_boolean(290, d.flag_290_b);
    size_t n = 0;
    if (c.ok() && !c.count(90, 2, n)) {
        if (c.ok()) c.fail("the number of owned dependencies (90) is missing");
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        AssocVariableMember m;
        c.require_handle(360, m.dependency);
        c.require_integer(90, m.value_90);
        d.members.push_back(std::move(m));
    }
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocVariableData>(std::move(d));
    return true;
}

void write_variable(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocVariableData>(o);
    w.object_head("ACDBASSOCVARIABLE", o);
    write_assoc_action_base(w, d.action);
    w.subclass("AcDbAssocVariable");
    w.integer(90, d.version);
    w.text(1, d.name);
    w.text(1, d.expression);
    w.text(1, d.evaluator);
    w.text(1, d.description);
    write_value(w, d.value);
    w.boolean(290, d.flag_290_a);
    w.boolean(290, d.flag_290_b);
    std::vector<const AssocVariableMember*> live;
    for (const AssocVariableMember& m : d.members) {
        if (m.dependency.empty() || m.dependency == "0" || w.live(m.dependency)) live.push_back(&m);
        else w.warn("a pointer to " + m.dependency + ", which no longer exists, is left out of its list");
    }
    w.integer(90, static_cast<int64_t>(live.size()));
    for (const AssocVariableMember* m : live) {
        w.handle(360, m->dependency.empty() ? "0" : m->dependency);
        w.integer(90, m->value_90);
    }
    w.xdata(o.xdata);
}

void variable_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<AssocVariableData>(o);
    assoc_action_references(d.action, out);
    for (const AssocVariableMember& m : d.members) assoc_add_reference(out, m.dependency, RefKind::HardOwner);
}

// ---------------------------------------------------------------------------
// ACDBASSOCVALUEDEPENDENCY
// ---------------------------------------------------------------------------

bool read_value_dependency(const RecordView& view, TypedReadContext& ctx, Object& o) {
    AssocValueDependencyData d;
    if (!read_assoc_dependency_base(view, ctx, d.dependency)) return false;
    GroupCursor c(view, "AcDbAssocValueDependency");
    if (!c.valid()) return ctx.fail(assoc_describe(view) + " has no AcDbAssocValueDependency subclass");
    c.require_integer(90, d.version);
    c.require_string(1, d.name);
    if (c.ok()) read_value(c, d.value, true);
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<AssocValueDependencyData>(std::move(d));
    return true;
}

void write_value_dependency(RecordWriter& w, const Object& o) {
    const auto& d = data_of<AssocValueDependencyData>(o);
    w.object_head("ACDBASSOCVALUEDEPENDENCY", o);
    write_assoc_dependency_base(w, d.dependency);
    w.subclass("AcDbAssocValueDependency");
    w.integer(90, d.version);
    w.text(1, d.name);
    write_value(w, d.value);
    w.xdata(o.xdata);
}

void value_dependency_references(const Object& o, std::vector<TypedReference>& out) {
    assoc_dependency_references(data_of<AssocValueDependencyData>(o).dependency, out);
}

std::vector<RefDescriptor> dependency_descriptors() {
    // 330: the object depended on, the previous and the next dependency; 360: the body the dependency owns
    RefDescriptor on_object = assoc_ref("AcDbAssocDependency", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace, 0);
    on_object.back_reactor = true;
    return {on_object, assoc_ref("AcDbAssocDependency", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace),
            assoc_ref("AcDbAssocDependency", 360, RefKind::HardOwner, RefRemoval::ZeroInPlace)};
}

} // namespace

// ---------------------------------------------------------------------------
// The nodes of a constraint group
// ---------------------------------------------------------------------------

namespace {

// The head every node has, and what a geometry node and a constraint node have next
// Version 2 (2013 and later): 90 number, 90 count + 90 each, 70 flags. Version 1 (up to 2010, the action's version): 90 number,
// 70 flags, 90 count + 90 each, and every node is preceded by 1 (its class) and 330 (the group), with no class table
bool read_node_head(GroupCursor& c, int32_t expected_id, AssocConstraintNodeHead& h, bool v1) {
    c.require_integer(90, h.id);
    if (c.ok() && expected_id >= 0 && h.id != expected_id) {
        c.fail("the node number " + std::to_string(h.id) + " is not the " + std::to_string(expected_id) + " the class table lists");
        return false;
    }
    if (v1) c.require_integer(70, h.flags);
    size_t n = 0;
    if (c.ok() && !c.count(90, 1, n)) {
        if (c.ok()) c.fail("the number of connected nodes (90) is missing");
        return false;
    }
    h.connected.clear();
    for (size_t i = 0; i < n && c.ok(); ++i) {
        int32_t v = 0;
        c.require_integer(90, v);
        h.connected.push_back(v);
    }
    if (!v1) c.require_integer(70, h.flags);
    return c.ok();
}

void write_node_head(RecordWriter& w, const AssocConstraintNodeHead& h, bool v1) {
    w.integer(90, h.id);
    if (v1) w.integer(70, h.flags);
    w.integer(90, static_cast<int64_t>(h.connected.size()));
    for (int32_t v : h.connected) w.integer(90, v);
    if (!v1) w.integer(70, h.flags);
}

bool read_geometry_head(GroupCursor& c, int32_t id, AssocConstraintGeometryHead& g, bool v1) {
    if (!read_node_head(c, id, g.head, v1)) return false;
    c.require_handle(330, g.dependency);
    c.require_integer(90, g.head.reserved);
    return c.ok();
}

void write_geometry_head(RecordWriter& w, const AssocConstraintGeometryHead& g, bool v1) {
    write_node_head(w, g.head, v1);
    w.handle(330, assoc_pointer(w, g.dependency));
    w.integer(90, g.head.reserved);
}

bool read_constraint_head(GroupCursor& c, int32_t id, AssocConstraintHead& h, bool v1) {
    if (!read_node_head(c, id, h.head, v1)) return false;
    c.require_integer(90, h.head.reserved);
    c.require_boolean(290, h.flag_290_a);
    c.require_boolean(290, h.flag_290_b);
    return c.ok();
}

void write_constraint_head(RecordWriter& w, const AssocConstraintHead& h, bool v1) {
    write_node_head(w, h.head, v1);
    w.integer(90, h.head.reserved);
    w.boolean(290, h.flag_290_a);
    w.boolean(290, h.flag_290_b);
}

bool read_line_pair(GroupCursor& c, Point3& a, Point3& b) {
    c.require_point3(10, a);
    c.require_point3(10, b);
    return c.ok();
}

// A node of the class `name`: false (with the cursor's error) when its groups do not fit
bool read_node(GroupCursor& c, const std::string& name, int32_t id, AssocConstraintNode& out, bool v1) {
#define SIMPLE_CONSTRAINT(Type)                                                                                           \
    if (name == #Type) {                                                                                                  \
        Type n;                                                                                                           \
        if (!read_constraint_head(c, id, n.node, v1)) return false;                                                           \
        out = std::move(n);                                                                                               \
        return true;                                                                                                      \
    }
    SIMPLE_CONSTRAINT(AcPointCurveConstraint)
    SIMPLE_CONSTRAINT(AcPointCoincidenceConstraint)
    SIMPLE_CONSTRAINT(AcParallelConstraint)
    SIMPLE_CONSTRAINT(AcPerpendicularConstraint)
    SIMPLE_CONSTRAINT(AcColinearConstraint)
    SIMPLE_CONSTRAINT(AcTangentConstraint)
    SIMPLE_CONSTRAINT(AcEqualLengthConstraint)
    SIMPLE_CONSTRAINT(AcEqualRadiusConstraint)
    SIMPLE_CONSTRAINT(AcMidPointConstraint)
    SIMPLE_CONSTRAINT(AcCenterPointConstraint)
    SIMPLE_CONSTRAINT(AcFixedConstraint)
#undef SIMPLE_CONSTRAINT
#define DATUM_CONSTRAINT(Type)                                                                                            \
    if (name == #Type) {                                                                                                  \
        Type n;                                                                                                           \
        if (!read_constraint_head(c, id, n.node, v1)) return false;                                                           \
        c.require_integer(90, n.datum_line);                                                                              \
        if (!c.ok()) return false;                                                                                        \
        out = std::move(n);                                                                                               \
        return true;                                                                                                      \
    }
    DATUM_CONSTRAINT(AcHorizontalConstraint)
    DATUM_CONSTRAINT(AcVerticalConstraint)
#undef DATUM_CONSTRAINT
#define DIMENSIONAL_CONSTRAINT(Type)                                                                                      \
    if (name == #Type) {                                                                                                  \
        Type n;                                                                                                           \
        if (!read_constraint_head(c, id, n.node, v1)) return false;                                                           \
        c.require_handle(340, n.first_dependency);                                                                        \
        c.require_handle(340, n.second_dependency);                                                                       \
        c.require_integer(280, n.kind);                                                                                   \
        if (!c.ok()) return false;                                                                                        \
        DIMENSIONAL_TAIL                                                                                                  \
        out = std::move(n);                                                                                               \
        return true;                                                                                                      \
    }
#define DIMENSIONAL_TAIL
    DIMENSIONAL_CONSTRAINT(AcAngleConstraint)
    DIMENSIONAL_CONSTRAINT(Ac3PointAngleConstraint)
    DIMENSIONAL_CONSTRAINT(AcRadiusDiameterConstraint)
#undef DIMENSIONAL_TAIL
    if (name == "AcDistanceConstraint") {
        AcDistanceConstraint n;
        if (!read_constraint_head(c, id, n.node, v1)) return false;
        c.require_handle(340, n.first_dependency);
        c.require_handle(340, n.second_dependency);
        c.require_integer(280, n.kind);
        if (!c.ok()) return false;
        if (n.kind == 1) {
            Point3 p;
            c.require_point3(10, p);
            n.direction = p;
        }
        if (!c.ok()) return false;
        out = std::move(n);
        return true;
    }
#undef DIMENSIONAL_CONSTRAINT
    if (name == "AcConstrainedBoundedLine") {
        AcConstrainedBoundedLine n;
        if (!read_geometry_head(c, id, n.node, v1)) return false;
        c.require_point3(10, n.origin);
        c.require_point3(10, n.direction);
        c.require_boolean(290, n.bounded_flag);
        c.require_point3(10, n.start);
        c.require_point3(11, n.end);
        if (!c.ok()) return false;
        out = std::move(n);
        return true;
    }
    if (name == "AcConstrainedImplicitPoint") {
        AcConstrainedImplicitPoint n;
        if (!read_geometry_head(c, id, n.node, v1)) return false;
        c.require_integer(280, n.kind);
        c.require_integer(90, n.value_90);
        c.require_integer(90, n.curve);
        if (!c.ok()) return false;
        out = std::move(n);
        return true;
    }
    if (name == "AcConstrainedDatumLine") {
        AcConstrainedDatumLine n;
        if (!read_geometry_head(c, id, n.node, v1) || !read_line_pair(c, n.origin, n.direction)) return false;
        out = std::move(n);
        return true;
    }
    if (name == "AcConstrained2PointsConstructionLine") {
        AcConstrained2PointsConstructionLine n;
        if (!read_geometry_head(c, id, n.node, v1) || !read_line_pair(c, n.origin, n.direction)) return false;
        out = std::move(n);
        return true;
    }
    if (name == "AcConstrainedArc") {
        AcConstrainedArc n;
        if (!read_geometry_head(c, id, n.node, v1)) return false;
        c.require_point3(10, n.center);
        c.require_point3(10, n.normal);
        c.require_point3(10, n.reference);
        c.require_real(40, n.radius);
        c.require_real(40, n.start_angle);
        c.require_real(40, n.end_angle);
        if (!v1) c.require_real(40, n.value_40);   // 2000 ... 2010 (version 1) have three
        c.require_point3(10, n.start);
        c.require_point3(11, n.end);
        if (!c.ok()) return false;
        out = std::move(n);
        return true;
    }
    if (name == "AcConstrainedCircle") {
        AcConstrainedCircle n;
        if (!read_geometry_head(c, id, n.node, v1)) return false;
        c.require_point3(10, n.center);
        c.require_point3(10, n.normal);
        c.require_point3(10, n.reference);
        c.require_real(40, n.radius);
        c.require_real(40, n.start_angle);
        c.require_real(40, n.end_angle);
        if (!v1) c.require_real(40, n.value_40);   // 2000 ... 2010 (version 1) have three
        if (!c.ok()) return false;
        out = std::move(n);
        return true;
    }
    c.fail("the constraint group node class \"" + name + "\" is not established (AutoCAD's files have 23 classes)");
    return false;
}

void write_node(RecordWriter& w, const AssocConstraintNode& node, bool v1) {
    std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, AcConstrainedBoundedLine>) {
            write_geometry_head(w, n.node, v1);
            w.point3(10, n.origin);
            w.point3(10, n.direction);
            w.boolean(290, n.bounded_flag);
            w.point3(10, n.start);
            w.point3(11, n.end);
        } else if constexpr (std::is_same_v<T, AcConstrainedImplicitPoint>) {
            write_geometry_head(w, n.node, v1);
            w.integer(280, n.kind);
            w.integer(90, n.value_90);
            w.integer(90, n.curve);
        } else if constexpr (std::is_same_v<T, AcConstrainedDatumLine> || std::is_same_v<T, AcConstrained2PointsConstructionLine>) {
            write_geometry_head(w, n.node, v1);
            w.point3(10, n.origin);
            w.point3(10, n.direction);
        } else if constexpr (std::is_same_v<T, AcConstrainedArc>) {
            write_geometry_head(w, n.node, v1);
            w.point3(10, n.center);
            w.point3(10, n.normal);
            w.point3(10, n.reference);
            w.real(40, n.radius);
            w.real(40, n.start_angle);
            w.real(40, n.end_angle);
            if (!v1) w.real(40, n.value_40);
            w.point3(10, n.start);
            w.point3(11, n.end);
        } else if constexpr (std::is_same_v<T, AcConstrainedCircle>) {
            write_geometry_head(w, n.node, v1);
            w.point3(10, n.center);
            w.point3(10, n.normal);
            w.point3(10, n.reference);
            w.real(40, n.radius);
            w.real(40, n.start_angle);
            w.real(40, n.end_angle);
            if (!v1) w.real(40, n.value_40);
        } else if constexpr (std::is_same_v<T, AcHorizontalConstraint> || std::is_same_v<T, AcVerticalConstraint>) {
            write_constraint_head(w, n.node, v1);
            w.integer(90, n.datum_line);
        } else if constexpr (std::is_same_v<T, AcDistanceConstraint>) {
            write_constraint_head(w, n.node, v1);
            w.handle(340, assoc_pointer(w, n.first_dependency));
            w.handle(340, assoc_pointer(w, n.second_dependency));
            w.integer(280, n.kind);
            if (n.kind == 1) w.point3(10, n.direction ? *n.direction : Point3());
        } else if constexpr (std::is_same_v<T, AcAngleConstraint> || std::is_same_v<T, Ac3PointAngleConstraint> ||
                             std::is_same_v<T, AcRadiusDiameterConstraint>) {
            write_constraint_head(w, n.node, v1);
            w.handle(340, assoc_pointer(w, n.first_dependency));
            w.handle(340, assoc_pointer(w, n.second_dependency));
            w.integer(280, n.kind);
        } else {
            write_constraint_head(w, n.node, v1);
        }
    }, node);
}

bool is_geometry_node(const AssocConstraintNode& node) {
    return std::visit([](const auto& n) { return std::is_same_v<std::decay_t<decltype(n)>, AcConstrainedBoundedLine> ||
                                                 std::is_same_v<std::decay_t<decltype(n)>, AcConstrainedImplicitPoint> ||
                                                 std::is_same_v<std::decay_t<decltype(n)>, AcConstrainedDatumLine> ||
                                                 std::is_same_v<std::decay_t<decltype(n)>, AcConstrained2PointsConstructionLine> ||
                                                 std::is_same_v<std::decay_t<decltype(n)>, AcConstrainedArc> ||
                                                 std::is_same_v<std::decay_t<decltype(n)>, AcConstrainedCircle>; }, node);
}

// The geometry dependency of a node (empty for a node without one)
std::string node_dependency(const AssocConstraintNode& node) {
    return std::visit([](const auto& n) -> std::string {
        if constexpr (requires { n.node.dependency; }) return n.node.dependency;
        else return std::string();
    }, node);
}

// ---------------------------------------------------------------------------
// ACDBASSOC2DCONSTRAINTGROUP
// ---------------------------------------------------------------------------

bool read_group(const RecordView& view, TypedReadContext& ctx, Object& o) {
    Assoc2dConstraintGroupData d;
    if (!read_assoc_action_base(view, ctx, d.action)) return false;
    // The group's own groups follow the base in the same subclass: a cursor past the base's groups
    GroupCursor c(view, "AcDbAssocAction");
    const size_t base_groups = (d.action.version >= 2 ? 11 : 7) + d.action.dependencies.size() + d.action.params.size();
    if (d.action.version >= 2) {
        size_t value_groups = 0;
        for (const AssocValueParameter& p : d.action.value_params) value_groups += 5 + 2 * p.values.size();
        c.skip(base_groups + value_groups);
    } else {
        c.skip(base_groups);
    }
    c.require_integer(90, d.version);
    c.require_integer(70, d.flags);
    c.require_point3(10, d.plane_origin);
    c.require_point3(10, d.plane_x_axis);
    c.require_point3(10, d.plane_y_axis);
    c.require_handle(360, d.workplane);
    size_t n = 0;
    if (c.ok() && !c.count(90, 1, n)) {
        if (c.ok()) c.fail("the number of geometry dependencies (90) is missing");
    }
    for (size_t i = 0; i < n && c.ok(); ++i) {
        std::string h;
        c.require_handle(360, h);
        d.dependencies.push_back(std::move(h));
    }
    const bool v1 = d.action.version < 2;
    c.require_integer(90, d.next_id);
    if (!v1) {
        c.require_integer(90, d.reserved_1);
        c.require_integer(90, d.reserved_2);
        c.require_boolean(290, d.flag);
    }
    size_t nodes = 0;
    if (c.ok() && !c.count(90, v1 ? 6 : 2, nodes)) {
        if (c.ok()) c.fail("the number of nodes (90) is missing");
    }
    std::vector<std::pair<std::string, int32_t>> table;
    if (!v1) {
        for (size_t i = 0; i < nodes && c.ok(); ++i) {
            std::string name;
            int32_t id = 0;
            c.require_string(1, name);
            c.require_integer(90, id);
            table.emplace_back(std::move(name), id);
        }
    }
    for (size_t i = 0; i < nodes && c.ok(); ++i) {
        AssocConstraintNode node;
        if (v1) {
            std::string name, group;
            c.require_string(1, name);
            c.require_handle(330, group);
            if (!c.ok() || !read_node(c, name, -1, node, true)) break;
        } else if (!read_node(c, table[i].first, table[i].second, node, false)) {
            break;
        }
        d.nodes.push_back(std::move(node));
    }
    if (!c.ok()) return ctx.fail(c.error());
    assoc_note_rest(c, ctx);
    o.data = Boxed<Assoc2dConstraintGroupData>(std::move(d));
    return true;
}

// What the group loses when the geometry it stands on is gone (plan 9.15, F9.12; AutoCAD's own erase of a constrained line,
// doc/facit/acad_*_dxf_assoc_extended_constrained_line_erased.dxf): the geometry nodes whose dependency has no record any more
// go, with the point-on-curve constraints that tie them, the implicit points of the curve and every constraint that
// is connected to a node that goes; the numbers of the nodes that go leave the other nodes' lists
std::set<int32_t> dead_nodes(const Assoc2dConstraintGroupData& d, const std::function<bool(const std::string&)>& live) {
    std::set<int32_t> gone;
    for (const AssocConstraintNode& n : d.nodes) {
        std::string dep = node_dependency(n);
        if (!dep.empty() && dep != "0" && !live(dep) && is_geometry_node(n) &&
            !std::holds_alternative<AcConstrainedImplicitPoint>(n) && !std::holds_alternative<AcConstrainedDatumLine>(n)) {
            gone.insert(assoc_constraint_node_id(n));
        }
    }
    bool more = !gone.empty();
    while (more) {
        more = false;
        for (const AssocConstraintNode& n : d.nodes) {
            const int32_t id = assoc_constraint_node_id(n);
            if (gone.count(id)) continue;
            bool dies = false;
            if (const auto* point = std::get_if<AcConstrainedImplicitPoint>(&n)) {
                dies = gone.count(point->curve) > 0;
            } else if (!is_geometry_node(n)) {
                for (int32_t other : assoc_constraint_node_head(n).connected) dies = dies || gone.count(other) > 0;
            }
            if (dies) {
                gone.insert(id);
                more = true;
            }
        }
    }
    return gone;
}

void write_group(RecordWriter& w, const Object& o) {
    const auto& d = data_of<Assoc2dConstraintGroupData>(o);
    w.object_head("ACDBASSOC2DCONSTRAINTGROUP", o);
    write_assoc_action_base(w, d.action);
    std::set<int32_t> gone;
    if (w.filtering() && w.read_from_file()) gone = dead_nodes(d, [&](const std::string& h) { return w.live(h); });
    std::vector<const AssocConstraintNode*> nodes;
    for (const AssocConstraintNode& n : d.nodes) {
        if (!gone.count(assoc_constraint_node_id(n))) nodes.push_back(&n);
    }
    // the same subclass: no marker of its own
    w.integer(90, d.version);
    w.integer(70, d.flags);
    w.point3(10, d.plane_origin);
    w.point3(10, d.plane_x_axis);
    w.point3(10, d.plane_y_axis);
    w.handle(360, assoc_pointer(w, d.workplane));
    std::vector<std::string> live = assoc_live(w, d.dependencies);
    w.integer(90, static_cast<int64_t>(live.size()));
    for (const std::string& h : live) w.handle(360, h);
    const bool v1 = d.action.version < 2;
    w.integer(90, d.next_id);
    if (!v1) {
        w.integer(90, d.reserved_1);
        w.integer(90, d.reserved_2);
        w.boolean(290, d.flag);
    }
    w.integer(90, static_cast<int64_t>(nodes.size()));
    if (!v1) {
        for (const AssocConstraintNode* n : nodes) {
            w.text(1, assoc_constraint_node_class(*n));
            w.integer(90, assoc_constraint_node_id(*n));
        }
    }
    for (const AssocConstraintNode* n : nodes) {
        if (v1) {
            w.text(1, assoc_constraint_node_class(*n));
            w.handle(330, o.handle);
        }
        if (gone.empty()) {
            write_node(w, *n, v1);
        } else {
            // the numbers of the nodes that went leave this node's list of the nodes it is connected to
            AssocConstraintNode copy = *n;
            auto& list = assoc_constraint_node_head(copy).connected;
            list.erase(std::remove_if(list.begin(), list.end(), [&](int32_t v) { return gone.count(v) > 0; }), list.end());
            write_node(w, copy, v1);
        }
    }
    w.xdata(o.xdata);
}

void group_references(const Object& o, std::vector<TypedReference>& out) {
    const auto& d = data_of<Assoc2dConstraintGroupData>(o);
    assoc_action_references(d.action, out);
    assoc_add_reference(out, d.workplane, RefKind::HardOwner);
    for (const std::string& h : d.dependencies) assoc_add_reference(out, h, RefKind::HardOwner);
    for (const AssocConstraintNode& n : d.nodes) {
        assoc_add_reference(out, node_dependency(n), RefKind::SoftPointer);
        std::visit([&](const auto& x) {
            if constexpr (requires { x.first_dependency; }) {
                assoc_add_reference(out, x.first_dependency, RefKind::SoftPointer);
                assoc_add_reference(out, x.second_dependency, RefKind::SoftPointer);
            }
        }, n);
    }
}

std::vector<RefDescriptor> group_descriptors() {
    // The base's pointers (the network, the body, the dependencies and parameters it counts), the group's own list of geometry
    // dependencies (hard owners of a counted list) and the dependency of each geometry node. The nodes are one list
    // (written whole when a node goes), so a pointer to a record that is gone is left to dead_nodes()
    return {assoc_ref("AcDbAssocAction", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace, 0),
            assoc_ref("AcDbAssocAction", 360, RefKind::HardOwner, RefRemoval::ZeroInPlace, 0),
            assoc_member_ref("AcDbAssocAction", 360, RefKind::HardOwner),
            assoc_ref("AcDbAssocAction", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace)};
}

} // namespace

std::vector<std::pair<int32_t, std::string>> assoc_group_removed_nodes(const Assoc2dConstraintGroupData& group,
                                                                       const std::function<bool(const std::string&)>& live) {
    std::vector<std::pair<int32_t, std::string>> out;
    std::set<int32_t> gone = dead_nodes(group, live);
    for (const AssocConstraintNode& n : group.nodes) {
        if (!gone.count(assoc_constraint_node_id(n))) continue;
        std::visit([&](const auto& x) {
            if constexpr (requires { x.first_dependency; }) {
                out.emplace_back(assoc_constraint_node_id(n), x.first_dependency);
                out.emplace_back(assoc_constraint_node_id(n), x.second_dependency);
            }
        }, n);
    }
    return out;
}

void register_assoc_constraint_codecs(TypedRegistry& registry) {
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCVARIABLE";
        c.type = ObjectType::AssocVariable;
        c.read = read_variable;
        c.write = write_variable;
        c.references = variable_references;
        c.cls = assoc_class("AcDbAssocVariable");
        c.versions = assoc_matrix(assoc_all_versions());
        c.refs = {assoc_ref("AcDbAssocAction", 330, RefKind::SoftPointer, RefRemoval::ZeroInPlace, 0),
                  assoc_ref("AcDbAssocAction", 360, RefKind::HardOwner, RefRemoval::ZeroInPlace, 0),
                  assoc_member_ref("AcDbAssocAction", 360, RefKind::HardOwner)};
        RefDescriptor member = assoc_ref("AcDbAssocVariable", 360, RefKind::HardOwner, RefRemoval::DropPair);
        member.first_offset = 0;
        member.last_offset = 1;
        member.partner_code = 90;
        member.counter_code = 90;
        c.refs.push_back(member);
        c.lists = {assoc_whole_subclass("AcDbAssocAction"), assoc_whole_subclass("AcDbAssocVariable")};
        c.step = 15;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOCVALUEDEPENDENCY";
        c.type = ObjectType::AssocValueDependency;
        c.read = read_value_dependency;
        c.write = write_value_dependency;
        c.references = value_dependency_references;
        c.cls = assoc_class("AcDbAssocValueDependency");
        c.versions = assoc_matrix(assoc_all_versions());
        c.refs = dependency_descriptors();
        c.step = 15;
        registry.add(std::move(c));
    }
    {
        ObjectCodec c;
        c.dxf_name = "ACDBASSOC2DCONSTRAINTGROUP";
        c.type = ObjectType::Assoc2dConstraintGroup;
        c.read = read_group;
        c.write = write_group;
        c.references = group_references;
        c.cls = assoc_class("AcDbAssoc2dConstraintGroup");
        c.versions = assoc_matrix(assoc_all_versions());
        c.refs = group_descriptors();
        // The base and the group are one subclass; the nodes and the dependency list change together: written whole
        c.lists = {assoc_whole_subclass("AcDbAssocAction")};
        c.step = 15;
        registry.add(std::move(c));
    }
}

} // namespace internal
} // namespace hdxf
