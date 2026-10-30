#include "assoc_test_support.hpp"
#include "type_version_matrix.hpp"
#include <iostream>

// The ACDBASSOC types built without a template (step 9.10; plan T): a whole extrusion in every version and format, a
// change of one parameter in both forms, every curve kind, records the grammar does not fit, and the version matrix of
// every type.

using hdxf::DxfFormat;
using hdxf::DxfVersion;
using hdxf_test::assert_eq;
using hdxf_test::assert_false;
using hdxf_test::assert_true;
using namespace assoc_test;

namespace {

hdxf::Object object_of(hdxf::ObjectType type, const std::string& handle, const std::string& owner, hdxf::ObjectData data) {
    hdxf::Object o;
    o.type = type;
    o.handle = handle;
    o.owner = owner;
    o.data = std::move(data);
    return o;
}

hdxf::AssocValueParameter height_parameter(double value, const std::string& dependency) {
    hdxf::AssocValueParameter p;
    p.name = "ExtrusionHeight";
    p.unit_type = 1;
    hdxf::AssocParameterValue v;
    v.code = 40;
    v.real = value;
    p.values.push_back(v);
    p.dependency = dependency;
    return p;
}

// The line, the network, one action with its body, path and edge parameters, two dependencies: the 2010 form (version 1:
// the parameters in the body) or the 2013 form (version 2: in the action)
void add_extrusion(hdxf::Drawing& d, bool version2, double height) {
    using namespace hdxf;
    Entity& line = d.add_line({0, 0, 0}, {10, 0, 0});
    const std::string L = line.props.handle;
    const int v = version2 ? 2 : 1;
    AssocNetworkData net;
    net.action.version = v;
    net.max_action_index = 1;
    net.actions.push_back({"1103", true});
    d.objects().push_back(object_of(ObjectType::AssocNetwork, "1102", "0", Boxed<AssocNetworkData>(net)));

    AssocActionData act;
    act.action.version = v;
    act.action.owning_network = "1102";
    act.action.action_body = "1104";
    act.action.action_index = 1;
    act.action.max_dependency_index = 2;
    act.action.dependencies = {"1105", "1106"};
    if (version2) {
        act.action.params = {"1107"};
        act.action.value_params.push_back(height_parameter(height, "1105"));
    }
    d.objects().push_back(object_of(ObjectType::AssocAction, "1103", "1102", Boxed<AssocActionData>(act)));

    AssocExtrudedSurfaceActionBodyData body;
    body.head.version = v;
    if (!version2) {
        AssocParamBasedActionBody pb;
        pb.params = {"1107"};
        pb.value_params.push_back(height_parameter(height, "1105"));
        body.head.param_based = pb;
    }
    body.surface.result_dependency = "1105";
    body.surface.step_id = 1;
    d.objects().push_back(object_of(ObjectType::AssocExtrudedSurfaceActionBody, "1104", "1103", Boxed<AssocExtrudedSurfaceActionBodyData>(body)));

    AssocDependencyData result;
    result.dependency.is_read = false;
    result.dependency.order = INT32_MIN;
    result.dependency.dependent_on_object = L;
    result.dependency.index = 1;
    d.objects().push_back(object_of(ObjectType::AssocDependency, "1105", "1103", Boxed<AssocDependencyData>(result)));
    AssocDependencyData input;
    input.dependency.is_write = false;
    input.dependency.order = 0;
    input.dependency.dependent_on_object = L;
    input.dependency.index = 2;
    d.objects().push_back(object_of(ObjectType::AssocDependency, "1106", "1103", Boxed<AssocDependencyData>(input)));

    AssocPathActionParamData path;
    path.param.version = version2 ? 1 : 0;
    path.param.name = "ExtrudeProfile";
    path.compound.children = {"1108"};
    d.objects().push_back(object_of(ObjectType::AssocPathActionParam, "1107", "1103", Boxed<AssocPathActionParamData>(path)));

    AssocEdgeActionParamData edge;
    edge.param.version = version2 ? 1 : 0;
    edge.single.dependency = "1106";
    AssocLineCurve curve;
    curve.start = {0, 0, 0};
    curve.offset = {10, 0, 0};
    edge.curve.shape = curve;
    d.objects().push_back(object_of(ObjectType::AssocEdgeActionParam, "1108", "1107", Boxed<AssocEdgeActionParamData>(edge)));
}

} // namespace

TEST(assoc_build_a_new_extrusion_is_written_and_read_in_every_version_and_format) {
    for (DxfVersion v : {DxfVersion::R2000, DxfVersion::R2004, DxfVersion::R2007, DxfVersion::R2010, DxfVersion::R2013, DxfVersion::R2018}) {
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            const bool version2 = v >= DxfVersion::R2013;
            hdxf::Drawing d(v);
            add_extrusion(d, version2, 42.5);
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), "not written: " + d.last_error());
            hdxf::Drawing r = reload(bytes);
            assert_eq(1, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocNetwork)), "network");
            assert_eq(1, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocAction)), "action");
            assert_eq(2, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocDependency)), "dependencies");
            assert_eq(1, static_cast<int>(count_objects(r, hdxf::ObjectType::AssocEdgeActionParam)), "edge");
            const hdxf::Object* act = find_object(r, "1103");
            assert_true(act != nullptr, "the action is read typed");
            const hdxf::AssocActionBase& a = act->as_assoc_action().action;
            assert_eq(2, static_cast<int>(a.dependencies.size()), "the dependency list");
            double height = 0.0;
            if (version2) {
                assert_eq(1, static_cast<int>(a.params.size()), "the parameter is in the action");
                height = a.value_params.at(0).values.at(0).real;
            } else {
                const hdxf::Object* body = find_object(r, "1104");
                assert_true(body && body->as_assoc_extruded_surface_action_body().head.param_based, "the parameter is in the body");
                height = body->as_assoc_extruded_surface_action_body().head.param_based->value_params.at(0).values.at(0).real;
            }
            assert_true(height == 42.5, "the height");
            assert_true(r.save_to_memory(f) == bytes, "written again unchanged is the same bytes");
        }
    }
}

TEST(assoc_build_a_change_of_a_parameter_alone_changes_that_record_alone_in_both_forms) {
    for (const char* version : {"2010", "2018"}) {
        std::filesystem::path path = hdxf_test::source_path(std::string("doc/facit/acad_") + version + "_dxf_assoc_core.dxf");
        if (!std::filesystem::exists(path)) hdxf_test::skip("no doc/facit/acad_*_dxf_assoc_core.dxf");
        std::vector<uint8_t> bytes = bytes_of(path);
        hdxf::Drawing d;
        assert_true(load_bytes(d, bytes), "load");
        std::string changed;
        for (auto& o : d.objects()) {
            if (!changed.empty()) break;
            if (o.type == hdxf::ObjectType::AssocAction && std::string(version) == "2018") {
                auto& vp = o.as_assoc_action().action.value_params;
                if (!vp.empty() && vp[0].name == "ExtrusionHeight") { vp[0].values[0].real = 77.0; changed = o.handle; }
            } else if (o.type == hdxf::ObjectType::AssocExtrudedSurfaceActionBody && std::string(version) == "2010") {
                auto& pb = o.as_assoc_extruded_surface_action_body().head.param_based;
                if (pb && !pb->value_params.empty() && pb->value_params[0].name == "ExtrusionHeight") { pb->value_params[0].values[0].real = 77.0; changed = o.handle; }
            }
        }
        assert_true(!changed.empty(), std::string(version) + ": an extrusion height to change");
        std::vector<uint8_t> out = d.save_to_memory(DxfFormat::Ascii);
        std::vector<std::string> a = split_lines(bytes), b = split_lines(out);
        assert_eq(static_cast<int>(a.size()), static_cast<int>(b.size()), std::string(version) + ": same number of lines");
        int differences = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            // (a drawing that was changed has new GUIDs)
            if (a[i] != b[i] && !same_number(a[i], b[i]) && !(a[i].size() > 2 && a[i].front() == '{' && b[i].size() > 2 && b[i].front() == '{')) {
                ++differences;
                assert_true(b[i] == "77.0" || b[i] == "77", std::string(version) + ": line " + std::to_string(i + 1) + " \"" + a[i] + "\" became \"" + b[i] + "\"");
            }
        }
        assert_eq(1, differences, std::string(version) + ": one group differs");
    }
}

TEST(assoc_build_every_curve_kind_of_an_edge_parameter_round_trips_in_both_forms) {
    using namespace hdxf;
    AssocCompositeCurve composite;
    {
        AssocCurve line;
        AssocLineCurve l;
        l.start = {1, 2, 3};
        l.offset = {4, 5, 6};
        line.shape = l;
        composite.curves.push_back(line);
        AssocCurve arc;
        AssocCircleArcCurve a;
        a.center = {7, 8, 0};
        a.normal = {0, 0, 1};
        a.reference = {1, 0, 0};
        a.radius = 2.5;
        a.start_angle = 4.712388980384689;
        a.end_angle = 6.283185307179586;
        arc.shape = a;
        composite.curves.push_back(arc);
    }
    AssocSplineCurve spline;
    spline.knots = {0, 0, 0, 0, 1, 2, 2, 2, 2};
    spline.knots_lengths = {9, 8};
    spline.weights_lengths = {0, 8};
    spline.control_points = {{0, 0, 0}, {1, 1, 0}, {2, 0, 0}, {3, 1, 0}, {4, 0, 0}};
    spline.control_points_lengths = {8, 8};
    AssocEllipseArcCurve ellipse;
    ellipse.center = {1, 1, 0};
    ellipse.major_axis = {1, 0, 0};
    ellipse.minor_axis = {0, 1, 0};
    ellipse.major_radius = 5;
    ellipse.minor_radius = 3;
    ellipse.end_angle = 6.283185307179586;
    AssocCircleArcCurve circle;
    circle.radius = 1.25;
    circle.end_angle = 3.14159;
    for (int version : {0, 1}) {
        // the 2013 form has an extra value in every circle arc
        circle.trailing = version == 1 ? std::optional<double>(0.0) : std::nullopt;
        for (auto& c : composite.curves) {
            if (auto* a = std::get_if<AssocCircleArcCurve>(&c.shape)) a->trailing = circle.trailing;
        }
        std::vector<AssocCurve> curves(4);
        curves[0].shape = composite;
        curves[1].shape = spline;
        curves[2].shape = ellipse;
        curves[3].shape = circle;
        for (DxfFormat f : {DxfFormat::Ascii, DxfFormat::Binary}) {
            hdxf::Drawing d(version == 1 ? DxfVersion::R2013 : DxfVersion::R2010);
            int n = 0;
            for (const AssocCurve& c : curves) {
                AssocEdgeActionParamData edge;
                edge.param.version = version;
                edge.curve = c;
                d.objects().push_back(object_of(ObjectType::AssocEdgeActionParam, "2" + std::to_string(100 + n++), "0", Boxed<AssocEdgeActionParamData>(edge)));
            }
            std::vector<uint8_t> bytes = d.save_to_memory(f);
            assert_true(!bytes.empty(), "not written: " + d.last_error());
            hdxf::Drawing r = reload(bytes);
            std::vector<const Object*> edges;
            for (const auto& o : r.objects()) if (o.type == ObjectType::AssocEdgeActionParam) edges.push_back(&o);
            assert_eq(4, static_cast<int>(edges.size()), "four edge parameters");
            assert_true(edges[0]->as_assoc_edge_action_param().curve.kind() == AssocCurveKind::Composite &&
                            edges[1]->as_assoc_edge_action_param().curve.kind() == AssocCurveKind::Spline &&
                            edges[2]->as_assoc_edge_action_param().curve.kind() == AssocCurveKind::EllipseArc &&
                            edges[3]->as_assoc_edge_action_param().curve.kind() == AssocCurveKind::CircleArc, "the kinds");
            const auto& got = std::get<AssocSplineCurve>(edges[1]->as_assoc_edge_action_param().curve.shape);
            assert_eq(5, static_cast<int>(got.control_points.size()), "the control points");
            assert_eq(9, static_cast<int>(got.knots.size()), "the knots");
            assert_true(got.weights.empty(), "not rational");
            const auto& comp = std::get<AssocCompositeCurve>(edges[0]->as_assoc_edge_action_param().curve.shape);
            assert_eq(2, static_cast<int>(comp.curves.size()), "the composite's curves");
            const auto& arc = std::get<AssocCircleArcCurve>(edges[3]->as_assoc_edge_action_param().curve.shape);
            assert_true(arc.trailing.has_value() == (version == 1), "the 2013 form's extra value");
            assert_true(r.save_to_memory(f) == bytes, "written again unchanged");
        }
    }
}

TEST(assoc_build_a_record_the_grammar_does_not_fit_stays_as_read_with_a_warning) {
    hdxf::Drawing d(DxfVersion::R2018);
    add_extrusion(d, true, 10.0);
    std::vector<uint8_t> bytes = d.save_to_memory(DxfFormat::Ascii);
    std::string text(bytes.begin(), bytes.end());
    auto stays_untyped = [&](const std::string& mutated, const std::string& type) {
        hdxf::Drawing r;
        assert_true(r.load_from_memory(reinterpret_cast<const uint8_t*>(mutated.data()), mutated.size()), "readable");
        bool untyped = false;
        for (const auto& o : r.objects()) untyped = untyped || (o.type == hdxf::ObjectType::Unknown && o.as_unknown().object_type == type);
        assert_true(untyped, type + " stays as read");
        assert_true(!r.warnings().empty(), type + " has a warning");
        assert_true(r.save_to_memory(DxfFormat::Ascii) == std::vector<uint8_t>(mutated.begin(), mutated.end()), type + " is written back as read");
    };
    // an action of a version no file has
    {
        std::string t = text;
        const std::string key = "AcDbAssocAction\r\n 90\r\n        2\r\n";
        size_t at = t.find(key, t.find("  0\r\nACDBASSOCACTION\r\n"));
        assert_true(at != std::string::npos, "the action");
        t.replace(at + key.size() - 3, 1, "7");
        stays_untyped(t, "ACDBASSOCACTION");
    }
    // an edge parameter with a curve kind no file has
    {
        std::string t = text;
        size_t at = t.find("AcDbAssocEdgeActionParam");
        const std::string kind_text = " 90\r\n       23\r\n";
        size_t kind = t.find(kind_text, at);
        assert_true(kind != std::string::npos, "the curve kind");
        t.replace(kind, kind_text.size(), " 90\r\n       99\r\n");
        stays_untyped(t, "ACDBASSOCEDGEACTIONPARAM");
    }
    // a count that is more than the record holds (no allocation)
    {
        std::string t = text;
        size_t at = t.find("AcDbAssocCompoundActionParam");
        size_t first = t.find(" 90\r\n", at);
        size_t second = t.find(" 90\r\n", first + 1);
        size_t count = t.find(" 90\r\n", second + 1);
        assert_true(count != std::string::npos, "the count");
        size_t value_end = t.find("\r\n", count + 5);
        t.replace(count + 5, value_end - (count + 5), "2000000000");
        stays_untyped(t, "ACDBASSOCPATHACTIONPARAM");
    }
}

TEST(assoc_build_version_matrix_of_every_type) {
    struct Case { const char* name; hdxf::ObjectType type; };
    const Case cases[] = {
        {"ACDBASSOCACTION", hdxf::ObjectType::AssocAction}, {"ACDBASSOCNETWORK", hdxf::ObjectType::AssocNetwork},
        {"ACDBASSOCDEPENDENCY", hdxf::ObjectType::AssocDependency}, {"ACDBASSOCGEOMDEPENDENCY", hdxf::ObjectType::AssocGeomDependency},
        {"ACDBASSOCPATHACTIONPARAM", hdxf::ObjectType::AssocPathActionParam}, {"ACDBASSOCVERTEXACTIONPARAM", hdxf::ObjectType::AssocVertexActionParam},
        {"ACDBASSOCOSNAPPOINTREFACTIONPARAM", hdxf::ObjectType::AssocOsnapPointRefActionParam}, {"ACDBASSOCEDGEACTIONPARAM", hdxf::ObjectType::AssocEdgeActionParam},
        {"ACDBASSOCEXTRUDEDSURFACEACTIONBODY", hdxf::ObjectType::AssocExtrudedSurfaceActionBody}, {"ACDBASSOCLOFTEDSURFACEACTIONBODY", hdxf::ObjectType::AssocLoftedSurfaceActionBody},
        {"ACDBASSOCREVOLVEDSURFACEACTIONBODY", hdxf::ObjectType::AssocRevolvedSurfaceActionBody}, {"ACDBASSOCSWEPTSURFACEACTIONBODY", hdxf::ObjectType::AssocSweptSurfaceActionBody},
        {"ACDBASSOCPLANESURFACEACTIONBODY", hdxf::ObjectType::AssocPlaneSurfaceActionBody}, {"ACDBASSOCALIGNEDDIMACTIONBODY", hdxf::ObjectType::AssocAlignedDimActionBody}};
    int not_tested_total = 0;
    for (const Case& c : cases) {
        const hdxf::internal::ObjectCodec* codec = hdxf::internal::TypedRegistry::instance().find_object(c.name);
        assert_true(codec != nullptr, std::string(c.name) + " has a codec");
        hdxf_test::MatrixReport report = hdxf_test::run_version_matrix(
            c.name, codec->versions,
            [&](hdxf::Drawing& d) {
                const int v = d.version() >= DxfVersion::R2013 ? 1 : 0;
                hdxf::Object o;
                o.type = c.type;
                o.owner = "0";
                auto body_head = [&](hdxf::AssocActionBodyHead& h) {
                    h.version = v + 1;
                    if (v == 0) h.param_based = hdxf::AssocParamBasedActionBody();
                };
                switch (c.type) {
                    case hdxf::ObjectType::AssocAction: { hdxf::AssocActionData x; x.action.version = v + 1; o.data = hdxf::Boxed<hdxf::AssocActionData>(x); break; }
                    case hdxf::ObjectType::AssocNetwork: { hdxf::AssocNetworkData x; x.action.version = v + 1; o.data = hdxf::Boxed<hdxf::AssocNetworkData>(x); break; }
                    case hdxf::ObjectType::AssocDependency: o.data = hdxf::Boxed<hdxf::AssocDependencyData>(); break;
                    case hdxf::ObjectType::AssocGeomDependency: o.data = hdxf::Boxed<hdxf::AssocGeomDependencyData>(); break;
                    case hdxf::ObjectType::AssocPathActionParam: { hdxf::AssocPathActionParamData x; x.param.version = v; o.data = hdxf::Boxed<hdxf::AssocPathActionParamData>(x); break; }
                    case hdxf::ObjectType::AssocVertexActionParam: { hdxf::AssocVertexActionParamData x; x.param.version = v; o.data = hdxf::Boxed<hdxf::AssocVertexActionParamData>(x); break; }
                    case hdxf::ObjectType::AssocOsnapPointRefActionParam: { hdxf::AssocOsnapPointRefActionParamData x; x.param.version = v; o.data = hdxf::Boxed<hdxf::AssocOsnapPointRefActionParamData>(x); break; }
                    case hdxf::ObjectType::AssocEdgeActionParam: { hdxf::AssocEdgeActionParamData x; x.param.version = v; o.data = hdxf::Boxed<hdxf::AssocEdgeActionParamData>(x); break; }
                    case hdxf::ObjectType::AssocAlignedDimActionBody: { hdxf::AssocAlignedDimActionBodyData x; body_head(x.head); o.data = hdxf::Boxed<hdxf::AssocAlignedDimActionBodyData>(x); break; }
                    case hdxf::ObjectType::AssocExtrudedSurfaceActionBody: { hdxf::AssocExtrudedSurfaceActionBodyData x; body_head(x.head); o.data = hdxf::Boxed<hdxf::AssocExtrudedSurfaceActionBodyData>(x); break; }
                    case hdxf::ObjectType::AssocLoftedSurfaceActionBody: { hdxf::AssocLoftedSurfaceActionBodyData x; body_head(x.head); o.data = hdxf::Boxed<hdxf::AssocLoftedSurfaceActionBodyData>(x); break; }
                    case hdxf::ObjectType::AssocRevolvedSurfaceActionBody: { hdxf::AssocRevolvedSurfaceActionBodyData x; body_head(x.head); o.data = hdxf::Boxed<hdxf::AssocRevolvedSurfaceActionBodyData>(x); break; }
                    case hdxf::ObjectType::AssocSweptSurfaceActionBody: { hdxf::AssocSweptSurfaceActionBodyData x; body_head(x.head); o.data = hdxf::Boxed<hdxf::AssocSweptSurfaceActionBodyData>(x); break; }
                    default: { hdxf::AssocPlaneSurfaceActionBodyData x; body_head(x.head); o.data = hdxf::Boxed<hdxf::AssocPlaneSurfaceActionBodyData>(x); break; }
                }
                d.objects().push_back(std::move(o));
            },
            [&](const hdxf::Drawing& r, DxfVersion, DxfFormat) -> std::string {
                return count_objects(r, c.type) == 1 ? std::string() : "expected one typed record";
            });
        std::cout << c.name << ": " << report.generated << " generated, " << report.named_errors << " named errors, " << report.not_established
                  << " not established, AutoCAD verified " << report.autocad_verified << ", ej testat " << report.autocad_not_tested << "\n";
        assert_eq(12, report.generated, std::string(c.name) + ": 2000 … 2018, both formats");
        assert_eq(2, report.named_errors, std::string(c.name) + ": R12");
        assert_eq(4, report.not_established, std::string(c.name) + ": R13 and R14");
        not_tested_total += report.autocad_not_tested;
    }
    std::cout << "ACDBASSOC cells AutoCAD has not saved (ej testat): " << not_tested_total << "\n";
}
