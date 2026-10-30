#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include <iostream>

// The other ACDBASSOC types built without a template (step 9.15; plan T): every type in every version and format, the
// constraint group in both of its forms, a variable with every kind of value, an array with its items, a history entry
// built from typed values, a change of one scalar and one list, and records the grammar does not fit.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

bool is_ext(const hdxf::Object& o) {
    const hdxf::internal::ObjectCodec* c = hdxf::internal::TypedRegistry::instance().find_object(o.type);
    return c && c->step == 15;
}

// A record of the type with the version-dependent groups as the drawing's version has them
hdxf::Object make_object(const hdxf::internal::ObjectCodec& c, DxfVersion version) {
    using namespace hdxf;
    const bool v2 = version >= DxfVersion::R2013;
    const int action_version = v2 ? 2 : 1;
    Object o;
    o.type = c.type;
    o.owner = "0";
    auto head = [&](AssocActionBodyHead& h) {
        h.version = action_version;
        if (!v2) h.param_based = AssocParamBasedActionBody();
    };
    auto param = [&](AssocActionParamBase& p) { p.version = v2 ? 1 : 0; };
    switch (c.type) {
        case ObjectType::AssocVariable: { AssocVariableData d; d.action.version = action_version; d.name = "d1"; d.expression = "80"; d.value.code = 90; d.value.integer = 80; o.data = Boxed<AssocVariableData>(d); break; }
        case ObjectType::AssocValueDependency: { AssocValueDependencyData d; d.value.code = 40; d.value.real = 80.5; o.data = Boxed<AssocValueDependencyData>(d); break; }
        case ObjectType::AssocDimDependencyBody: { AssocDimDependencyBodyData d; d.dimension_text = "d1=80.0000"; o.data = Boxed<AssocDimDependencyBodyData>(d); break; }
        case ObjectType::Assoc2dConstraintGroup: { Assoc2dConstraintGroupData d; d.action.version = action_version; o.data = Boxed<Assoc2dConstraintGroupData>(d); break; }
        case ObjectType::AssocOffsetSurfaceActionBody: { AssocOffsetSurfaceActionBodyData d; head(d.head); o.data = Boxed<AssocOffsetSurfaceActionBodyData>(d); break; }
        case ObjectType::AssocTrimSurfaceActionBody: { AssocTrimSurfaceActionBodyData d; head(d.head); o.data = Boxed<AssocTrimSurfaceActionBodyData>(d); break; }
        case ObjectType::AssocPatchSurfaceActionBody: { AssocPatchSurfaceActionBodyData d; head(d.head); o.data = Boxed<AssocPatchSurfaceActionBodyData>(d); break; }
        case ObjectType::AssocNetworkSurfaceActionBody: { AssocNetworkSurfaceActionBodyData d; head(d.head); o.data = Boxed<AssocNetworkSurfaceActionBodyData>(d); break; }
        case ObjectType::AssocBlendSurfaceActionBody: { AssocBlendSurfaceActionBodyData d; head(d.head); o.data = Boxed<AssocBlendSurfaceActionBodyData>(d); break; }
        case ObjectType::AssocExtendSurfaceActionBody: { AssocExtendSurfaceActionBodyData d; head(d.head); o.data = Boxed<AssocExtendSurfaceActionBodyData>(d); break; }
        case ObjectType::AssocFilletSurfaceActionBody: { AssocFilletSurfaceActionBodyData d; head(d.head); o.data = Boxed<AssocFilletSurfaceActionBodyData>(d); break; }
        case ObjectType::AssocEdgeFilletActionBody:
        case ObjectType::AssocEdgeChamferActionBody: { AssocEdgeFilletChamferActionBodyData d; head(d.head); o.data = Boxed<AssocEdgeFilletChamferActionBodyData>(d); break; }
        case ObjectType::AssocRestoreEntityStateActionBody: { AssocRestoreEntityStateActionBodyData d; d.head.version = action_version; if (!v2) d.head.param_based = AssocParamBasedActionBody(); o.data = Boxed<AssocRestoreEntityStateActionBodyData>(d); break; }
        case ObjectType::AssocImpliedSurfaceOrSolidActionBody: { AssocImpliedSurfaceOrSolidActionBodyData d; d.head.version = action_version; if (!v2) d.head.param_based = std::nullopt; d.version = 0; o.data = Boxed<AssocImpliedSurfaceOrSolidActionBodyData>(d); break; }
        case ObjectType::AssocRotatedDimActionBody: { AssocRotatedDimActionBodyData d; head(d.body.head); o.data = Boxed<AssocRotatedDimActionBodyData>(d); break; }
        case ObjectType::Assoc3PointAngularDimActionBody: { Assoc3PointAngularDimActionBodyData d; head(d.body.head); o.data = Boxed<Assoc3PointAngularDimActionBodyData>(d); break; }
        case ObjectType::AssocOrdinateDimActionBody: { AssocOrdinateDimActionBodyData d; head(d.body.head); o.data = Boxed<AssocOrdinateDimActionBodyData>(d); break; }
        case ObjectType::AssocDiametricDimActionBody: { AssocDiametricDimActionBodyData d; head(d.body.head); o.data = Boxed<AssocDiametricDimActionBodyData>(d); break; }
        case ObjectType::AssocRadialDimActionBody: { AssocRadialDimActionBodyData d; head(d.body.head); o.data = Boxed<AssocRadialDimActionBodyData>(d); break; }
        case ObjectType::AssocMLeaderActionBody: { AssocMLeaderActionBodyData d; head(d.body.head); o.data = Boxed<AssocMLeaderActionBodyData>(d); break; }
        case ObjectType::AssocEntityCloneActionBody: { AssocEntityCloneActionBodyData d; head(d.head); o.data = Boxed<AssocEntityCloneActionBodyData>(d); break; }
        case ObjectType::AssocAsmBodyActionParam: { AssocAsmBodyActionParamData d; param(d.param); o.data = Boxed<AssocAsmBodyActionParamData>(d); break; }
        case ObjectType::AssocFaceActionParam: { AssocFaceActionParamData d; param(d.param); o.data = Boxed<AssocFaceActionParamData>(d); break; }
        case ObjectType::AssocTrimmingPathActionParam: { AssocTrimmingPathActionParamData d; param(d.param); o.data = Boxed<AssocTrimmingPathActionParamData>(d); break; }
        case ObjectType::AssocTrimmingBodyActionParam: { AssocTrimmingBodyActionParamData d; param(d.face.param); d.face.param.name = "TrimmingFace"; d.face.value_90 = 1; d.value_280 = 1; o.data = Boxed<AssocTrimmingBodyActionParamData>(d); break; }
        case ObjectType::AssocCompoundActionParam: { AssocCompoundActionParamData d; param(d.param); o.data = Boxed<AssocCompoundActionParamData>(d); break; }
        case ObjectType::AssocObjectActionParam: { AssocObjectActionParamData d; param(d.param); o.data = Boxed<AssocObjectActionParamData>(d); break; }
        case ObjectType::AssocArrayActionBody: { AssocArrayActionBodyData d; head(d.head); d.array.matrix.assign(16, 0.0); o.data = Boxed<AssocArrayActionBodyData>(d); break; }
        default: { AssocArrayModifyActionBodyData d; head(d.head); d.array.matrix.assign(16, 0.0); d.array.parameters.kind = AssocArrayKind::Modify; o.data = Boxed<AssocArrayModifyActionBodyData>(d); break; }
    }
    return o;
}

std::vector<const hdxf::internal::ObjectCodec*> ext_codecs() {
    std::vector<const hdxf::internal::ObjectCodec*> out;
    for (const auto& c : hdxf::internal::TypedRegistry::instance().object_codecs()) if (c.step == 15) out.push_back(&c);
    return out;
}

} // namespace

TEST(assoc_extended_build_every_type_in_every_version_and_format) {
    int not_tested_total = 0, types = 0;
    for (const hdxf::internal::ObjectCodec* c : ext_codecs()) {
        ++types;
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
            std::string(c->dxf_name), c->versions,
            [&](hdxf::Drawing& d) { d.objects().push_back(make_object(*c, d.version())); },
            [&](const hdxf::Drawing& r, DxfVersion, DxfFormat) -> std::string {
                return count_objects(r, c->type) == 1 ? std::string() : "expected one typed record";
            });
        std::cout << c->dxf_name << ": " << report.generated << " generated, " << report.named_errors << " named errors, "
                  << report.not_established << " not established, AutoCAD verified " << report.autocad_verified << ", ej testat "
                  << report.autocad_not_tested << "\n";
        assert_eq(12, report.generated, std::string(c->dxf_name) + ": 2000 … 2018, both formats");
        assert_eq(2, report.named_errors, std::string(c->dxf_name) + ": R12");
        assert_eq(4, report.not_established, std::string(c->dxf_name) + ": R13 and R14");
        not_tested_total += report.autocad_not_tested;
    }
    assert_true(types >= 27, "the codecs of step 15");
    std::cout << "step 9.15: " << types << " types, cells AutoCAD has not saved (ej testat): " << not_tested_total << "\n";
}

TEST(assoc_extended_build_a_constraint_group_a_variable_and_an_array_keep_what_they_were_given) {
    using namespace hdxf;
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const int av = v >= DxfVersion::R2013 ? 2 : 1;
            Drawing d(v);
            Assoc2dConstraintGroupData g;
            g.action.version = av;
            g.dependencies = {};
            AcConstrainedBoundedLine line;
            line.node.head.id = 1;
            line.node.head.connected = {2};
            line.node.head.flags = 1;
            line.start = {0, 0, 0};
            line.end = {80, 0, 0};
            AcConstrainedImplicitPoint point;
            point.node.head.id = 2;
            point.node.head.connected = {3};
            point.node.head.flags = 1;
            point.curve = 1;
            AcPointCurveConstraint on_curve;
            on_curve.node.head.id = 3;
            on_curve.node.head.connected = {2, 1};
            AcDistanceConstraint distance;
            distance.node.head.id = 4;
            distance.node.head.connected = {2};
            distance.kind = 1;
            distance.direction = Point3{-1, 0, 0};
            AcHorizontalConstraint horizontal;
            horizontal.node.head.id = 5;
            horizontal.node.head.connected = {1};
            horizontal.datum_line = 6;
            g.nodes = {line, point, on_curve, distance, horizontal};
            g.next_id = 6;
            Object go;
            go.type = ObjectType::Assoc2dConstraintGroup;
            go.handle = "2000";
            go.owner = "0";
            go.data = Boxed<Assoc2dConstraintGroupData>(g);
            d.objects().push_back(go);
            AssocVariableData var;
            var.action.version = av;
            var.name = "width";
            var.value.code = 1;
            var.value.text = "left";
            var.members.push_back({"0", 0});
            Object vo;
            vo.type = ObjectType::AssocVariable;
            vo.handle = "2001";
            vo.owner = "0";
            vo.data = Boxed<AssocVariableData>(var);
            d.objects().push_back(vo);
            AssocArrayActionBodyData a;
            a.head.version = av;
            if (av == 1) a.head.param_based = AssocParamBasedActionBody();
            a.array.parameters.kind = AssocArrayKind::Polar;
            AssocArrayItem i0;
            i0.position = {1, 2, 3};
            AssocArrayItem i1;
            i1.item_index = 1;
            i1.flags = 12;
            i1.matrix.assign(16, 0.5);
            a.array.items = {i0, i1};
            a.array.matrix.assign(16, 1.0);
            Object ao;
            ao.type = ObjectType::AssocArrayActionBody;
            ao.handle = "2002";
            ao.owner = "0";
            ao.data = Boxed<AssocArrayActionBodyData>(a);
            d.objects().push_back(ao);
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), "not written: " + d.last_error());
            Drawing r = reload(bytes);
            const Object* ro = find_object(r, "2000");
            assert_true(ro && ro->type == ObjectType::Assoc2dConstraintGroup, "the group is read typed");
            const auto& rg = ro->as_assoc_2d_constraint_group();
            assert_eq(5, static_cast<int>(rg.nodes.size()), "five nodes");
            assert_true(std::holds_alternative<AcDistanceConstraint>(rg.nodes[3]) && std::get<AcDistanceConstraint>(rg.nodes[3]).direction->x == -1.0, "the distance constraint's direction");
            assert_eq(std::string("left"), find_object(r, "2001")->as_assoc_variable().value.text, "the variable's string value");
            const auto& ra = find_object(r, "2002")->as_assoc_array_action_body().array;
            assert_eq(2, static_cast<int>(ra.items.size()), "two items");
            assert_eq(16, static_cast<int>(ra.items[1].matrix.size()), "the second item's matrix");
            assert_true(ra.parameters.kind == AssocArrayKind::Polar, "the parameters' class");
            assert_true(r.save_to_memory(f) == bytes, "written again unchanged is the same bytes");
        }
    }
}

TEST(assoc_extended_build_a_history_entry_made_from_typed_values_is_written_and_read) {
    using namespace hdxf;
    for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
        Drawing d(DxfVersion::R2018);
        AssocPersSubentManagerData m;
        PersHistoryItem edge;
        edge.kind = 4;
        edge.step = 8;
        edge.groups = {{1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}};
        PersHistoryRecord rec;
        rec.id = PersHistoryId{5, 0};
        rec.kind = 0;
        rec.faces = {{3, 0}, {1, 0}};
        PersHistoryRecord vertex;
        vertex.id = PersHistoryId{25, 0};
        vertex.kind = 1;
        vertex.faces = {{1, 0}, {3, 0}, {18, 0}};
        vertex.slots = {0, 101000, 0, 101000, 0, 101000};
        edge.records = {rec, vertex};
        PersHistoryContainer container;
        container.step = 9;
        PersHistoryItem inner = edge;
        inner.step = 0;
        container.items = {inner};
        PersHistoryItem zero;
        zero.kind = 7;
        zero.step = 3;
        zero.zero_count = 6;
        m.history = {zero, edge, container};
        m.next_id = 9;
        m.first_list = {1, 2, 3};
        m.second_list = {};
        Object o;
        o.type = ObjectType::AssocPersSubentManager;
        o.handle = "2000";
        o.owner = "0";
        o.data = Boxed<AssocPersSubentManagerData>(m);
        d.objects().push_back(o);
        std::vector<uint8_t> bytes = d.save_to_memory(f);
        assert_true(!bytes.empty(), "not written: " + d.last_error());
        Drawing r = reload(bytes);
        const Object* ro = find_object(r, "2000");
        assert_true(ro && ro->type == ObjectType::AssocPersSubentManager, "the manager is typed with its history");
        const auto& h = ro->as_assoc_pers_subent_manager().history;
        assert_eq(3, static_cast<int>(h.size()), "three entries");
        assert_true(std::holds_alternative<PersHistoryContainer>(h[2]) && std::get<PersHistoryContainer>(h[2]).items.size() == 1, "the container");
        assert_eq(2, static_cast<int>(std::get<PersHistoryItem>(h[1]).records.size()), "the edge entry has an edge record and a vertex record");
        // a change of one scalar (the step) is a change of that entry's payload alone
        Drawing r2 = reload(bytes);
        for (auto& obj : r2.objects()) {
            if (obj.type == ObjectType::AssocPersSubentManager) std::get<PersHistoryItem>(obj.as_assoc_pers_subent_manager().history[1]).step = 10;
        }
        std::vector<uint8_t> changed = r2.save_to_memory(f);
        assert_true(!changed.empty() && changed != bytes, "the changed step is written");
        assert_eq(10, std::get<PersHistoryItem>(find_object(reload(changed), "2000")->as_assoc_pers_subent_manager().history[1]).step, "the new step");
    }
}

TEST(assoc_extended_build_records_the_grammar_does_not_fit_stay_untyped_with_a_warning) {
    using namespace hdxf;
    Drawing d(DxfVersion::R2018);
    AssocArrayActionBodyData a;
    a.head.version = 2;
    a.array.matrix.assign(16, 0.0);
    a.array.items = {AssocArrayItem()};
    Object ao;
    ao.type = ObjectType::AssocArrayActionBody;
    ao.handle = "2002";
    ao.owner = "0";
    ao.data = Boxed<AssocArrayActionBodyData>(a);
    d.objects().push_back(ao);
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    assert_true(!bytes.empty(), "written");
    // the number of items made larger than the item subclasses the record has
    std::string text(bytes.begin(), bytes.end());
    size_t at = text.find("100\r\nAcDbAssocArrayActionBody");
    if (at == std::string::npos) at = text.find("100\nAcDbAssocArrayActionBody");
    assert_true(at != std::string::npos, "the array body is in the file");
    // replace the count group (the third 90 after the subclass marker: version, parameters' version, count)
    size_t p = at;
    for (int i = 0; i < 3; ++i) p = text.find(" 90", p + 1);
    size_t value_start = text.find('\n', p) + 1;
    size_t value_end = text.find('\n', value_start);
    text.replace(value_start, value_end - value_start, "1000000");
    Drawing r;
    std::vector<uint8_t> bad(text.begin(), text.end());
    assert_true(r.load_from_memory(bad.data(), bad.size()), "the drawing still loads: " + r.last_error());
    bool untyped = false;
    for (const auto& o : r.objects()) untyped = untyped || (o.type == ObjectType::Unknown && o.as_unknown().object_type == "ACDBASSOCARRAYACTIONBODY");
    assert_true(untyped, "the array body with an impossible item count stays untyped");
    bool warned = false;
    for (const auto& w : r.warnings()) warned = warned || w.find("ACDBASSOCARRAYACTIONBODY") != std::string::npos;
    assert_true(warned, "and says so");
}
