#include "test_framework.hpp"
#include "hdxf.hpp"

TEST(point2_to_point3_conversion) {
    hdxf::Point2 p2{3.0, 4.0};
    hdxf::Point3 p3 = p2;
    hdxf_test::assert_eq(p3.x, 3.0);
    hdxf_test::assert_eq(p3.y, 4.0);
    hdxf_test::assert_eq(p3.z, 0.0);
}

TEST(point2_aggregate_init) {
    hdxf::Point2 p{1.0, 2.0};
    hdxf_test::assert_eq(p.x, 1.0);
    hdxf_test::assert_eq(p.y, 2.0);
}

TEST(point3_aggregate_init) {
    hdxf::Point3 p{1.0, 2.0, 3.0};
    hdxf_test::assert_eq(p.x, 1.0);
    hdxf_test::assert_eq(p.y, 2.0);
    hdxf_test::assert_eq(p.z, 3.0);
}

TEST(point3_default_zero) {
    hdxf::Point3 p;
    hdxf_test::assert_eq(p.x, 0.0);
    hdxf_test::assert_eq(p.y, 0.0);
    hdxf_test::assert_eq(p.z, 0.0);
}

TEST(color_aci) {
    hdxf::Color c = hdxf::Color::from_aci(1);
    hdxf_test::assert_eq(c.aci, 1);
}

TEST(color_named) {
    hdxf_test::assert_eq(hdxf::Color::red().aci, 1);
    hdxf_test::assert_eq(hdxf::Color::yellow().aci, 2);
    hdxf_test::assert_eq(hdxf::Color::green().aci, 3);
    hdxf_test::assert_eq(hdxf::Color::cyan().aci, 4);
    hdxf_test::assert_eq(hdxf::Color::blue().aci, 5);
    hdxf_test::assert_eq(hdxf::Color::magenta().aci, 6);
    hdxf_test::assert_eq(hdxf::Color::white().aci, 7);
}

TEST(color_default_bylayer) {
    hdxf::Color c;
    hdxf_test::assert_eq(c.aci, 256);
    hdxf_test::assert_true(c.is_by_layer());
}

TEST(color_byblock) {
    hdxf::Color c = hdxf::Color::by_block();
    hdxf_test::assert_eq(c.aci, 0);
    hdxf_test::assert_true(c.is_by_block());
    hdxf_test::assert_false(c.is_by_layer());
}

TEST(color_bylayer) {
    hdxf::Color c = hdxf::Color::by_layer();
    hdxf_test::assert_eq(c.aci, 256);
    hdxf_test::assert_true(c.is_by_layer());
    hdxf_test::assert_false(c.is_by_block());
}

TEST(entity_line_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Line;
    e.data = hdxf::LineData{{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}};

    const auto& line = e.as_line();
    hdxf_test::assert_eq(line.start.x, 1.0);
    hdxf_test::assert_eq(line.end.z, 6.0);
}

TEST(entity_circle_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Circle;
    e.data = hdxf::CircleData{{10.0, 20.0, 0.0}, 5.0};

    const auto& circle = e.as_circle();
    hdxf_test::assert_eq(circle.center.x, 10.0);
    hdxf_test::assert_eq(circle.radius, 5.0);
}

TEST(entity_wrong_accessor_throws) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Line;
    e.data = hdxf::LineData{{0, 0, 0}, {1, 1, 0}};

    bool threw = false;
    try {
        e.as_circle();
    } catch (const std::bad_variant_access&) {
        threw = true;
    }
    hdxf_test::assert_true(threw, "Expected std::bad_variant_access");
}

TEST(entity_properties_defaults) {
    hdxf::EntityProperties p;
    hdxf_test::assert_eq(p.layer, std::string("0"));
    hdxf_test::assert_true(p.visible);
    hdxf_test::assert_eq(p.linetype_scale, 1.0);
    hdxf_test::assert_true(p.linetype.empty());
    hdxf_test::assert_eq(p.thickness, 0.0);
    hdxf_test::assert_eq(p.extrusion.x, 0.0);
    hdxf_test::assert_eq(p.extrusion.y, 0.0);
    hdxf_test::assert_eq(p.extrusion.z, 1.0);
    hdxf_test::assert_eq(p.paper_space, 0);
    hdxf_test::assert_true(p.handle.empty());
}

TEST(entity_polyline_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Polyline;
    hdxf::PolylineData pd;
    pd.flags = 1; // closed
    pd.vertices.push_back({{0, 0, 0}});
    pd.vertices.push_back({{10, 0, 0}});
    pd.vertices.push_back({{10, 10, 0}});
    e.data = std::move(pd);

    const auto& poly = e.as_polyline();
    hdxf_test::assert_eq(static_cast<int>(poly.vertices.size()), 3);
    hdxf_test::assert_true(poly.is_closed());
    hdxf_test::assert_eq(poly.vertices[1].position.x, 10.0);
}

TEST(entity_trace_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Trace;
    e.data = hdxf::TraceData{{0,0,0}, {10,0,0}, {10,5,0}, {0,5,0}};
    const auto& t = e.as_trace();
    hdxf_test::assert_eq(t.p0.x, 0.0);
    hdxf_test::assert_eq(t.p1.x, 10.0);
}

TEST(entity_solid_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Solid;
    e.data = hdxf::SolidData{{0,0,0}, {10,0,0}, {10,5,0}, {0,5,0}};
    const auto& s = e.as_solid();
    hdxf_test::assert_eq(s.p2.x, 10.0);
    hdxf_test::assert_eq(s.p2.y, 5.0);
}

TEST(entity_face3d_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Face3D;
    hdxf::Face3DData fd;
    fd.p0 = {0, 0, 0};
    fd.p1 = {10, 0, 0};
    fd.p2 = {10, 10, 5};
    fd.p3 = {0, 10, 5};
    fd.invisible_edges = 5; // edges 1 and 3 invisible
    e.data = fd;
    const auto& f = e.as_face3d();
    hdxf_test::assert_eq(f.p2.z, 5.0);
    hdxf_test::assert_eq(f.invisible_edges, 5);
}

TEST(entity_shape_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Shape;
    hdxf::ShapeData sd;
    sd.insertion_point = {5, 10, 0};
    sd.size = 3.0;
    sd.name = "ARROW";
    sd.rotation = 45.0;
    e.data = sd;
    const auto& s = e.as_shape();
    hdxf_test::assert_eq(s.name, std::string("ARROW"));
    hdxf_test::assert_eq(s.size, 3.0);
    hdxf_test::assert_eq(s.rotation, 45.0);
}

TEST(entity_insert_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Insert;
    hdxf::InsertData id;
    id.block_name = "MYBLOCK";
    id.insertion_point = {100, 200, 0};
    id.x_scale = 2.0;
    id.rotation = 90.0;
    e.data = id;
    const auto& ins = e.as_insert();
    hdxf_test::assert_eq(ins.block_name, std::string("MYBLOCK"));
    hdxf_test::assert_eq(ins.x_scale, 2.0);
}

TEST(entity_dimension_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Dimension;
    hdxf::DimensionData dd;
    dd.dim_type = hdxf::DimensionType::Aligned;
    dd.block_name = "*D1";
    dd.definition_point = {0, 0, 0};
    dd.text_midpoint = {5, 5, 0};
    e.data = dd;
    const auto& dim = e.as_dimension();
    hdxf_test::assert_true(dim.dim_type == hdxf::DimensionType::Aligned);
    hdxf_test::assert_eq(dim.block_name, std::string("*D1"));
}

TEST(entity_viewport_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Viewport;
    hdxf::ViewportData vd;
    vd.center = {0, 0, 0};
    vd.width = 200.0;
    vd.height = 150.0;
    vd.status = 1;
    vd.id = 2;
    e.data = vd;
    const auto& vp = e.as_viewport();
    hdxf_test::assert_eq(vp.width, 200.0);
    hdxf_test::assert_eq(vp.id, 2);
}

TEST(entity_unknown_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Unknown;
    hdxf::UnknownEntityData ud;
    ud.entity_type = "CUSTOMENT";
    ud.group_codes = {{10, "1.0"}, {20, "2.0"}};
    e.data = ud;
    const auto& u = e.as_unknown();
    hdxf_test::assert_eq(u.entity_type, std::string("CUSTOMENT"));
    hdxf_test::assert_eq(static_cast<int>(u.group_codes.size()), 2);
}

TEST(entity_attdef_accessor) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Attdef;
    hdxf::AttdefData ad;
    ad.tag = "NAME";
    ad.prompt = "Enter name:";
    ad.default_value = "Default";
    ad.flags = 2; // constant
    e.data = ad;
    const auto& a = e.as_attdef();
    hdxf_test::assert_eq(a.tag, std::string("NAME"));
    hdxf_test::assert_eq(a.prompt, std::string("Enter name:"));
    hdxf_test::assert_eq(a.flags, 2);
}

TEST(xdata_on_entity) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Line;
    e.data = hdxf::LineData{{0,0,0}, {10,10,0}};
    hdxf::XdataList xd;
    xd.app_name = "MYAPP";
    xd.items.push_back({1000, "test data"});
    xd.items.push_back({1070, "42"});
    e.props.xdata.push_back(xd);
    hdxf_test::assert_eq(static_cast<int>(e.props.xdata.size()), 1);
    hdxf_test::assert_eq(e.props.xdata[0].app_name, std::string("MYAPP"));
    hdxf_test::assert_eq(static_cast<int>(e.props.xdata[0].items.size()), 2);
}
