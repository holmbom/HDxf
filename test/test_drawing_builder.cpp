#include "test_drawing_builder.hpp"

// =========================================================================
// Build a drawing that exercises ALL R12 DXF features
// =========================================================================

hdxf::Drawing build_test_drawing(int level) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R12);

    // =================================================================
    // TABLES (always included)
    // =================================================================

    dwg.add_layer("Red",     hdxf::Color::red());
    dwg.add_layer("Yellow",  hdxf::Color::yellow());
    dwg.add_layer("Green",   hdxf::Color::green());
    dwg.add_layer("Cyan",    hdxf::Color::cyan());
    dwg.add_layer("Blue",    hdxf::Color::blue());
    dwg.add_layer("Magenta", hdxf::Color::magenta());
    dwg.add_layer("White",   hdxf::Color::white());

    auto& frozen_layer = dwg.add_layer("Frozen", hdxf::Color::from_aci(1));
    frozen_layer.frozen = true;

    auto& locked_layer = dwg.add_layer("Locked", hdxf::Color::from_aci(3));
    locked_layer.locked = true;

    auto& off_layer = dwg.add_layer("Off", hdxf::Color::from_aci(5));
    off_layer.on = false;

    dwg.add_linetype("DASHED", "Dashed __ __ __", {12.7, -6.35});
    dwg.add_linetype("CENTER", "Center ____ _ ____", {31.75, -6.35, 6.35, -6.35});

    auto& italic_style = dwg.add_text_style("Italic", "txt");
    italic_style.oblique_angle = 15.0;
    italic_style.width_factor = 0.8;

    auto& metric_dim = dwg.add_dim_style("Metric");
    metric_dim.dimscale = 2.0;
    metric_dim.dimasz = 3.0;
    metric_dim.dimtxt = 3.5;

    {
        hdxf::View v;
        v.name = "Front";
        v.height = 100.0;
        v.width = 200.0;
        v.center = {0.0, 0.0};
        v.direction = {0.0, 0.0, 1.0};
        v.target = {0.0, 0.0, 0.0};
        v.lens_length = 50.0;
        dwg.views().push_back(v);
    }

    {
        hdxf::Ucs u;
        u.name = "Custom";
        u.origin = {10.0, 20.0, 0.0};
        u.x_dir = {1.0, 0.0, 0.0};
        u.y_dir = {0.0, 1.0, 0.0};
        dwg.ucs_list().push_back(u);
    }

    dwg.add_app_id("TESTAPP");
    dwg.set_header_int("$LUNITS", 70, 4);
    dwg.set_header_string("$CLAYER", 8, "Red");

    // =================================================================
    // BLOCKS (level >= 4)
    // =================================================================

    if (level >= 4) {
        auto& blk = dwg.add_block("ARROW");
        hdxf::Entity line_ent;
        line_ent.type = hdxf::EntityType::Line;
        line_ent.data = hdxf::LineData{{0,0,0}, {10,0,0}};
        blk.entities.push_back(line_ent);
        hdxf::Entity solid_ent;
        solid_ent.type = hdxf::EntityType::Solid;
        solid_ent.data = hdxf::SolidData{{10,0,0}, {8,1,0}, {8,-1,0}, {10,0,0}};
        blk.entities.push_back(solid_ent);
    }

    if (level >= 5) {
        auto& blk = dwg.add_block("TITLED");
        hdxf::Entity attdef1;
        attdef1.type = hdxf::EntityType::Attdef;
        hdxf::AttdefData ad1;
        ad1.start_point = {0, 0, 0};
        ad1.height = 5.0;
        ad1.default_value = "Default Title";
        ad1.prompt = "Enter title:";
        ad1.tag = "TITLE";
        attdef1.data = ad1;
        blk.entities.push_back(attdef1);
        hdxf::Entity attdef2;
        attdef2.type = hdxf::EntityType::Attdef;
        hdxf::AttdefData ad2;
        ad2.start_point = {0, -10, 0};
        ad2.height = 3.0;
        ad2.default_value = "Unknown";
        ad2.prompt = "Enter author:";
        ad2.tag = "AUTHOR";
        attdef2.data = ad2;
        blk.entities.push_back(attdef2);
    }

    // =================================================================
    // ENTITIES
    // =================================================================

    // --- Level 1: LINE, CIRCLE, ARC, POINT, TEXT ---
    dwg.add_line({0, 0}, {50, 0},    "Red");
    dwg.add_line({0, 0}, {45, 20},   "Yellow");
    dwg.add_line({0, 0}, {35, 35},   "Green");
    dwg.add_line({0, 0}, {20, 45},   "Cyan");
    dwg.add_line({0, 0}, {0, 50},    "Blue");
    dwg.add_line({0, 0}, {-20, 45},  "Magenta");
    dwg.add_line({0, 0}, {-35, 35},  "White");
    dwg.add_line({0, 0, 10}, {100, 100, 50}, "Red");

    {
        auto& e = dwg.add_line({-50, -50}, {-40, -50}, "Green");
        e.props.color = hdxf::Color::from_aci(4);
    }
    {
        auto& e = dwg.add_line({-50, -55}, {-40, -55}, "Red");
        e.props.linetype_scale = 2.5;
    }

    dwg.add_line({-100, -80}, {-80, -80}, "Frozen");
    dwg.add_line({-100, -85}, {-80, -85}, "Locked");
    dwg.add_line({-100, -90}, {-80, -90}, "Off");

    {
        auto& e = dwg.add_line({-120, 0}, {-110, 0}, "Red");
        e.props.thickness = 5.0;
        e.props.linetype = "DASHED";
    }

    dwg.add_circle({80, 0}, 5.0,  "Red");
    dwg.add_circle({80, 0}, 10.0, "Yellow");
    dwg.add_circle({80, 0}, 15.0, "Green");
    dwg.add_circle({80, 0, 5}, 20.0, "Cyan");

    {
        auto& e = dwg.add_circle({-120, -20}, 8.0, "Green");
        hdxf::XdataList xd;
        xd.app_name = "TESTAPP";
        xd.items.push_back({1000, "test circle data"});
        xd.items.push_back({1070, "42"});
        xd.items.push_back({1040, "3.14159"});
        e.props.xdata.push_back(xd);
    }

    dwg.add_arc({-80, 0}, 20.0, 0.0, 90.0,    "Red");
    dwg.add_arc({-80, 0}, 20.0, 90.0, 180.0,  "Green");
    dwg.add_arc({-80, 0}, 20.0, 180.0, 270.0, "Blue");
    dwg.add_arc({-80, 0}, 20.0, 270.0, 360.0, "Magenta");
    dwg.add_arc({-80, 0}, 30.0, 45.0, 315.0,  "Yellow");

    dwg.add_point({0, 0});
    dwg.add_point({10, 10}, "Red");
    dwg.add_point({-10, 10}, "Blue");
    dwg.add_point({10, -10, 7.5}, "Green");

    dwg.add_text({0, 60}, "HDXF Test Drawing", 5.0, "White");
    // UTF-8 (plan B1); written as ANSI_1252 bytes
    dwg.add_text({0, 55}, "Flygande b\xc3\xa4" "ckasiner s\xc3\xb6" "ka hwila p\xc3\xa5 mjuka tuvor", 3.0, "Red");
    dwg.add_text({0, 51}, "\xc3\x85\xc3\x84\xc3\x96 \xc3\xa5\xc3\xa4\xc3\xb6 - Bl\xc3\xa5 mockasiner \xc3\xa4r gr\xc3\xb6" "na", 3.0, "Green");
    dwg.add_text({0, 47}, "Blue Text", 3.0, "Blue");

    {
        auto& e = dwg.add_text({30, -20}, "Rotated 45", 3.0, "Yellow");
        e.as_text().rotation = 45.0;
    }
    {
        auto& e = dwg.add_text({30, -30}, "Rotated 90", 3.0, "Cyan");
        e.as_text().rotation = 90.0;
    }

    dwg.add_text({-60, 60}, "Small (h=1.5)", 1.5, "White");
    dwg.add_text({-60, 55}, "Large (h=8)", 8.0, "White");

    // --- Level 2: POLYLINE ---
    if (level >= 2) {
        dwg.add_polyline({{60, 40}, {100, 40}, {100, 60}, {60, 60}}, true, "Red");
        dwg.add_polyline({{60, 30}, {80, 30}, {80, 20}}, false, "Green");
        dwg.add_polyline({{-40, -30}, {-20, -30}, {-30, -15}}, true, "Blue");
        dwg.add_polyline({
            {110, 0}, {115, 8.66}, {125, 8.66},
            {130, 0}, {125, -8.66}, {115, -8.66}
        }, true, "Cyan");
        dwg.add_polyline({
            {60, -20}, {65, -10}, {70, -20}, {75, -10}, {80, -20}
        }, false, "Magenta");
        {
            auto& e = dwg.add_polyline(
                {{-120,-40}, {-100,-40}, {-100,-50}, {-120,-50}}, true, "Cyan");
            e.as_polyline().vertices[0].bulge = 0.5;
        }
    }

    // --- Level 3: TRACE, SOLID, 3DFACE ---
    if (level >= 3) {
        dwg.add_trace({0,-70,0}, {10,-70,0}, {0,-65,0}, {10,-65,0}, "Red");
        dwg.add_trace({15,-70,0}, {25,-70,0}, {15,-65,0}, {25,-65,0}, "Green");
        dwg.add_solid({30,-70,0}, {40,-70,0}, {30,-65,0}, {40,-65,0}, "Blue");
        dwg.add_solid({45,-70,0}, {55,-70,0}, {45,-65,0}, {55,-65,0}, "Cyan");
        dwg.add_face3d({0,-80,0}, {10,-80,0}, {10,-75,5}, {0,-75,5}, "Red");
        {
            auto& e = dwg.add_face3d(
                {15,-80,0}, {25,-80,0}, {25,-75,0}, {15,-75,0}, "Green");
            e.as_face3d().invisible_edges = 1;
        }
    }

    // --- Level 4: INSERT (plain block) ---
    if (level >= 4) {
        dwg.add_insert("ARROW", {-80,-70,0}, "Red");
    }

    // --- Level 5: INSERT with attributes + standalone ATTDEF ---
    if (level >= 5) {
        {
            auto& e = dwg.add_insert("TITLED", {-80,-80,0}, "Blue");
            auto& ins = e.as_insert();
            ins.x_scale = 1.5;
            ins.rotation = 30.0;
            hdxf::InsertAttribute attr1;
            attr1.data.tag = "TITLE";
            attr1.data.value = "My Drawing";
            attr1.data.start_point = {0, 0, 0};
            attr1.data.height = 5.0;
            ins.attributes.push_back(attr1);
            hdxf::InsertAttribute attr2;
            attr2.data.tag = "AUTHOR";
            attr2.data.value = "HDXF Test";
            attr2.data.start_point = {0, -10, 0};
            attr2.data.height = 3.0;
            ins.attributes.push_back(attr2);
        }
        {
            hdxf::Entity e;
            e.type = hdxf::EntityType::Attdef;
            hdxf::AttdefData ad;
            ad.start_point = {-50, -85, 0};
            ad.height = 3.0;
            ad.default_value = "N/A";
            ad.prompt = "Enter number:";
            ad.tag = "NUMBER";
            e.data = ad;
            dwg.add_entity(e);
        }
    }

    // --- Level 6: DIMENSION (all 7 subtypes) ---
    if (level >= 6) {
        {
            auto& e = dwg.add_dimension(
                hdxf::DimensionType::Rotated, {0,-100,0}, {25,-95,0}, "Red");
            auto& dim = e.as_dimension();
            dim.def_point2 = {0, -100, 0};
            dim.def_point3 = {50, -100, 0};
        }
        {
            auto& e = dwg.add_dimension(
                hdxf::DimensionType::Aligned, {0,-110,0}, {25,-105,0}, "Green");
            auto& dim = e.as_dimension();
            dim.def_point2 = {0, -110, 0};
            dim.def_point3 = {50, -120, 0};
        }
        {
            auto& e = dwg.add_dimension(
                hdxf::DimensionType::Angular, {0,-130,0}, {15,-125,0}, "Blue");
            auto& dim = e.as_dimension();
            dim.def_point2 = {30, -130, 0};
            dim.def_point3 = {0, -140, 0};
            dim.def_point4 = {30, -140, 0};
            dim.arc_point = {15, -135, 0};
        }
        {
            auto& e = dwg.add_dimension(
                hdxf::DimensionType::Diameter, {80,-100,0}, {90,-100,0}, "Cyan");
            auto& dim = e.as_dimension();
            dim.def_point4 = {70, -100, 0};
            dim.leader_length = 5.0;
        }
        {
            auto& e = dwg.add_dimension(
                hdxf::DimensionType::Radius, {80,-115,0}, {90,-115,0}, "Magenta");
            auto& dim = e.as_dimension();
            dim.def_point4 = {70, -115, 0};
            dim.leader_length = 3.0;
        }
        {
            auto& e = dwg.add_dimension(
                hdxf::DimensionType::Angular3Pt, {0,-150,0}, {15,-145,0}, "Yellow");
            auto& dim = e.as_dimension();
            dim.def_point2 = {30, -150, 0};
            dim.def_point3 = {0, -160, 0};
            dim.def_point4 = {15, -155, 0};
        }
        {
            auto& e = dwg.add_dimension(
                hdxf::DimensionType::Ordinate, {80,-130,0}, {90,-135,0}, "White");
            auto& dim = e.as_dimension();
            dim.def_point2 = {80, -135, 0};
            dim.def_point3 = {90, -130, 0};
        }
    }

    // --- Level 7: VIEWPORT (paper space) ---
    if (level >= 7) {
        dwg.set_header_int("$TILEMODE", 70, 0);
        auto& e = dwg.add_viewport({0, 0, 0}, 200.0, 150.0, "0");
        e.as_viewport().status = 1;
        e.as_viewport().id = 1;
        e.props.paper_space = 1;
    }

    return dwg;
}

hdxf::Drawing r12_drawing_read_back() {
    hdxf::Drawing built = build_test_drawing(7);
    std::vector<uint8_t> data = built.save_to_memory(hdxf::DxfFormat::Ascii);
    hdxf::Drawing back;
    back.load_from_memory(data.data(), data.size());
    return back;
}
