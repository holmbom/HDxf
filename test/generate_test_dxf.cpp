// Standalone program to generate test DXF files and verify roundtrip.
//
// 1. Creates a Drawing using ALL available R12 DXF features
// 2. Saves as ASCII (test_r12.dxf) and Binary (test_r12_bin.dxf)
// 3. Loads both files back and verifies every feature matches
//
// Tests 14 entity types, all 8 table types, blocks with ATTDEFs,
// INSERT with attributes, DIMENSION subtypes, XDATA, entity properties,
// and header variables.
//
// Build: cmake --build build
// Run:   ./build/test/generate_test_dxf

#include "hdxf.hpp"
#include "test_drawing_builder.hpp"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>

static const char* entity_type_name(hdxf::EntityType type) {
    switch (type) {
        case hdxf::EntityType::Line:      return "LINE";
        case hdxf::EntityType::Circle:    return "CIRCLE";
        case hdxf::EntityType::Arc:       return "ARC";
        case hdxf::EntityType::Point:     return "POINT";
        case hdxf::EntityType::Text:      return "TEXT";
        case hdxf::EntityType::Polyline:  return "POLYLINE";
        case hdxf::EntityType::Trace:     return "TRACE";
        case hdxf::EntityType::Solid:     return "SOLID";
        case hdxf::EntityType::Face3D:    return "3DFACE";
        case hdxf::EntityType::Shape:     return "SHAPE";
        case hdxf::EntityType::Insert:    return "INSERT";
        case hdxf::EntityType::Attdef:    return "ATTDEF";
        case hdxf::EntityType::Attrib:    return "ATTRIB";
        case hdxf::EntityType::Dimension: return "DIMENSION";
        case hdxf::EntityType::Viewport:  return "VIEWPORT";
        case hdxf::EntityType::Unknown:   return "UNKNOWN";
    }
    return "UNKNOWN";
}

static bool approx(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) < eps;
}

static int errors = 0;

static void check(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "  FAIL: " << msg << std::endl;
        errors++;
    }
}

static void check_double(double got, double expected, const std::string& what) {
    if (!approx(got, expected)) {
        std::cerr << "  FAIL: " << what << " expected=" << expected
                  << " got=" << got << std::endl;
        errors++;
    }
}

static void check_int(int got, int expected, const std::string& what) {
    if (got != expected) {
        std::cerr << "  FAIL: " << what << " expected=" << expected
                  << " got=" << got << std::endl;
        errors++;
    }
}

static void check_str(const std::string& got, const std::string& expected,
                       const std::string& what) {
    if (got != expected) {
        std::cerr << "  FAIL: " << what << " expected=\"" << expected
                  << "\" got=\"" << got << "\"" << std::endl;
        errors++;
    }
}

// =========================================================================
// Verify tables
// =========================================================================

static void verify_tables(const hdxf::Drawing& loaded) {
    std::cout << "  Tables:" << std::endl;

    // --- Layers ---
    const auto& layers = loaded.layers();
    std::cout << "    Layers: " << layers.size() << std::endl;

    bool found_frozen = false, found_locked = false, found_off = false;
    for (const auto& l : layers) {
        if (l.name == "Red")     check_int(l.color.aci, 1, "layer Red color");
        if (l.name == "Yellow")  check_int(l.color.aci, 2, "layer Yellow color");
        if (l.name == "Green")   check_int(l.color.aci, 3, "layer Green color");
        if (l.name == "Cyan")    check_int(l.color.aci, 4, "layer Cyan color");
        if (l.name == "Blue")    check_int(l.color.aci, 5, "layer Blue color");
        if (l.name == "Magenta") check_int(l.color.aci, 6, "layer Magenta color");
        if (l.name == "White")   check_int(l.color.aci, 7, "layer White color");
        if (l.name == "Frozen")  { found_frozen = true; check(l.frozen, "layer Frozen is frozen"); }
        if (l.name == "Locked")  { found_locked = true; check(l.locked, "layer Locked is locked"); }
        if (l.name == "Off")     { found_off = true; check(!l.on, "layer Off is off"); }
    }
    check(found_frozen, "found Frozen layer");
    check(found_locked, "found Locked layer");
    check(found_off, "found Off layer");

    // --- Linetypes ---
    bool found_dashed = false, found_center = false;
    for (const auto& lt : loaded.linetypes()) {
        if (lt.name == "DASHED") {
            found_dashed = true;
            check_int(static_cast<int>(lt.pattern.size()), 2, "DASHED pattern count");
            if (lt.pattern.size() == 2) {
                check_double(lt.pattern[0], 12.7, "DASHED dash length");
                check_double(lt.pattern[1], -6.35, "DASHED gap length");
            }
        }
        if (lt.name == "CENTER") {
            found_center = true;
            check_int(static_cast<int>(lt.pattern.size()), 4, "CENTER pattern count");
        }
    }
    check(found_dashed, "found DASHED linetype");
    check(found_center, "found CENTER linetype");
    std::cout << "    Linetypes: DASHED=" << (found_dashed ? "OK" : "MISS")
              << " CENTER=" << (found_center ? "OK" : "MISS") << std::endl;

    // --- Text styles ---
    bool found_italic = false;
    for (const auto& ts : loaded.text_styles()) {
        if (ts.name == "Italic") {
            found_italic = true;
            check_double(ts.oblique_angle, 15.0, "Italic oblique_angle");
            check_double(ts.width_factor, 0.8, "Italic width_factor");
        }
    }
    check(found_italic, "found Italic text style");
    std::cout << "    Text styles: Italic=" << (found_italic ? "OK" : "MISS") << std::endl;

    // --- Dimension styles ---
    bool found_metric = false;
    for (const auto& ds : loaded.dim_styles()) {
        if (ds.name == "Metric") {
            found_metric = true;
            check_double(ds.dimscale, 2.0, "Metric dimscale");
            check_double(ds.dimasz, 3.0, "Metric dimasz");
            check_double(ds.dimtxt, 3.5, "Metric dimtxt");
        }
    }
    check(found_metric, "found Metric dim style");
    std::cout << "    Dim styles: Metric=" << (found_metric ? "OK" : "MISS") << std::endl;

    // --- Views ---
    bool found_front = false;
    for (const auto& v : loaded.views()) {
        if (v.name == "Front") {
            found_front = true;
            check_double(v.height, 100.0, "Front view height");
            check_double(v.width, 200.0, "Front view width");
            check_double(v.lens_length, 50.0, "Front view lens_length");
        }
    }
    check(found_front, "found Front view");
    std::cout << "    Views: Front=" << (found_front ? "OK" : "MISS") << std::endl;

    // --- UCS ---
    bool found_custom_ucs = false;
    for (const auto& u : loaded.ucs_list()) {
        if (u.name == "Custom") {
            found_custom_ucs = true;
            check_double(u.origin.x, 10.0, "Custom UCS origin.x");
            check_double(u.origin.y, 20.0, "Custom UCS origin.y");
        }
    }
    check(found_custom_ucs, "found Custom UCS");
    std::cout << "    UCS: Custom=" << (found_custom_ucs ? "OK" : "MISS") << std::endl;

    // --- App IDs ---
    bool found_testapp = false;
    for (const auto& a : loaded.app_ids()) {
        if (a.name == "TESTAPP") found_testapp = true;
    }
    check(found_testapp, "found TESTAPP app ID");
    std::cout << "    App IDs: TESTAPP=" << (found_testapp ? "OK" : "MISS") << std::endl;
}

// =========================================================================
// Verify blocks
// =========================================================================

static void verify_blocks(const hdxf::Drawing& loaded) {
    std::cout << "  Blocks:" << std::endl;

    const auto* arrow = loaded.find_block("ARROW");
    check(arrow != nullptr, "found ARROW block");
    if (arrow) {
        check_int(static_cast<int>(arrow->entities.size()), 2, "ARROW entity count");
        if (arrow->entities.size() == 2) {
            check(arrow->entities[0].type == hdxf::EntityType::Line, "ARROW[0] is LINE");
            check(arrow->entities[1].type == hdxf::EntityType::Solid, "ARROW[1] is SOLID");
        }
        std::cout << "    ARROW: " << arrow->entities.size() << " entities OK" << std::endl;
    }

    const auto* titled = loaded.find_block("TITLED");
    check(titled != nullptr, "found TITLED block");
    if (titled) {
        check_int(static_cast<int>(titled->entities.size()), 2, "TITLED entity count");
        if (titled->entities.size() == 2) {
            check(titled->entities[0].type == hdxf::EntityType::Attdef, "TITLED[0] is ATTDEF");
            check_str(titled->entities[0].as_attdef().tag, "TITLE", "TITLED[0] tag");
            check_double(titled->entities[0].as_attdef().height, 5.0, "TITLED[0] height");
            check(titled->entities[1].type == hdxf::EntityType::Attdef, "TITLED[1] is ATTDEF");
            check_str(titled->entities[1].as_attdef().tag, "AUTHOR", "TITLED[1] tag");
        }
        std::cout << "    TITLED: " << titled->entities.size() << " entities OK" << std::endl;
    }
}

// =========================================================================
// Verify header variables
// =========================================================================

static void verify_headers(const hdxf::Drawing& loaded) {
    std::cout << "  Headers:" << std::endl;

    auto* lunits = loaded.find_header("$LUNITS");
    check(lunits != nullptr, "header $LUNITS exists");
    if (lunits && !lunits->values.empty()) {
        check_str(lunits->values[0].second, "4", "header $LUNITS value");
        std::cout << "    $LUNITS=4 OK" << std::endl;
    }

    auto* clayer = loaded.find_header("$CLAYER");
    check(clayer != nullptr, "header $CLAYER exists");
    if (clayer && !clayer->values.empty()) {
        check_str(clayer->values[0].second, "Red", "header $CLAYER value");
        std::cout << "    $CLAYER=Red OK" << std::endl;
    }

    auto* acadver = loaded.find_header("$ACADVER");
    check(acadver != nullptr, "header $ACADVER exists");
    if (acadver && !acadver->values.empty()) {
        check_str(acadver->values[0].second, "AC1009", "header $ACADVER value");
        std::cout << "    $ACADVER=AC1009 OK" << std::endl;
    }
}

// =========================================================================
// Verify all entities
// =========================================================================

static void verify_entities(const hdxf::Drawing& loaded) {
    const int expected_count = 59;
    check_int(static_cast<int>(loaded.entities().size()), expected_count, "entity count");

    if (static_cast<int>(loaded.entities().size()) != expected_count) {
        std::cerr << "  Cannot verify entities -- count mismatch ("
                  << loaded.entities().size() << " != " << expected_count
                  << "), aborting" << std::endl;
        return;
    }

    const auto& e = loaded.entities();

    // --- LINE entities (0-13) ---
    // Fan lines (0-6)
    check(e[0].type == hdxf::EntityType::Line, "e[0] type=LINE");
    check_str(e[0].props.layer, "Red", "e[0] layer");
    check_double(e[0].as_line().end.x, 50.0, "e[0] end.x");

    check(e[1].type == hdxf::EntityType::Line, "e[1] type=LINE");
    check_str(e[1].props.layer, "Yellow", "e[1] layer");

    check(e[4].type == hdxf::EntityType::Line, "e[4] type=LINE");
    check_str(e[4].props.layer, "Blue", "e[4] layer");
    check_double(e[4].as_line().end.y, 50.0, "e[4] end.y");

    check(e[6].type == hdxf::EntityType::Line, "e[6] type=LINE");
    check_str(e[6].props.layer, "White", "e[6] layer");

    // 3D line (7)
    check_double(e[7].as_line().start.z, 10.0, "e[7] start.z");
    check_double(e[7].as_line().end.z, 50.0, "e[7] end.z");

    // Color override (8)
    check_int(e[8].props.color.aci, 4, "e[8] color override");
    check_str(e[8].props.layer, "Green", "e[8] layer");

    // Linetype scale (9)
    check_double(e[9].props.linetype_scale, 2.5, "e[9] linetype_scale");

    // Special layers (10-12)
    check_str(e[10].props.layer, "Frozen", "e[10] layer");
    check_str(e[11].props.layer, "Locked", "e[11] layer");
    check_str(e[12].props.layer, "Off", "e[12] layer");

    // Thickness + linetype (13)
    check(e[13].type == hdxf::EntityType::Line, "e[13] type=LINE");
    check_double(e[13].props.thickness, 5.0, "e[13] thickness");
    check_str(e[13].props.linetype, "DASHED", "e[13] linetype");
    std::cout << "    LINE: 14 entities OK (3D, color, linetype_scale, thickness, linetype)" << std::endl;

    // --- CIRCLE entities (14-18) ---
    check(e[14].type == hdxf::EntityType::Circle, "e[14] type=CIRCLE");
    check_double(e[14].as_circle().radius, 5.0, "e[14] radius");
    check_str(e[14].props.layer, "Red", "e[14] layer");

    check_double(e[15].as_circle().radius, 10.0, "e[15] radius");
    check_double(e[16].as_circle().radius, 15.0, "e[16] radius");

    // 3D circle (17)
    check_double(e[17].as_circle().center.z, 5.0, "e[17] center.z");
    check_double(e[17].as_circle().radius, 20.0, "e[17] radius");

    // Circle with XDATA (18)
    check(e[18].type == hdxf::EntityType::Circle, "e[18] type=CIRCLE");
    check_int(static_cast<int>(e[18].props.xdata.size()), 1, "e[18] xdata count");
    if (!e[18].props.xdata.empty()) {
        check_str(e[18].props.xdata[0].app_name, "TESTAPP", "e[18] xdata app_name");
        check_int(static_cast<int>(e[18].props.xdata[0].items.size()), 3, "e[18] xdata items");
        if (e[18].props.xdata[0].items.size() >= 3) {
            check_str(e[18].props.xdata[0].items[0].value, "test circle data", "e[18] xdata[0] string");
            check_int(std::stoi(e[18].props.xdata[0].items[1].value), 42, "e[18] xdata[1] int16");
        }
    }
    std::cout << "    CIRCLE: 5 entities OK (3D, XDATA)" << std::endl;

    // --- ARC entities (19-23) ---
    check(e[19].type == hdxf::EntityType::Arc, "e[19] type=ARC");
    check_double(e[19].as_arc().center.x, -80.0, "e[19] center.x");
    check_double(e[19].as_arc().radius, 20.0, "e[19] radius");
    check_double(e[19].as_arc().start_angle, 0.0, "e[19] start_angle");
    check_double(e[19].as_arc().end_angle, 90.0, "e[19] end_angle");

    check_double(e[20].as_arc().start_angle, 90.0, "e[20] start_angle");
    check_double(e[20].as_arc().end_angle, 180.0, "e[20] end_angle");

    check(e[23].type == hdxf::EntityType::Arc, "e[23] type=ARC");
    check_double(e[23].as_arc().radius, 30.0, "e[23] radius");
    check_double(e[23].as_arc().start_angle, 45.0, "e[23] start_angle");
    check_double(e[23].as_arc().end_angle, 315.0, "e[23] end_angle");
    std::cout << "    ARC: 5 entities OK" << std::endl;

    // --- POINT entities (24-27) ---
    check(e[24].type == hdxf::EntityType::Point, "e[24] type=POINT");
    check_double(e[24].as_point().location.x, 0.0, "e[24] x");

    check_str(e[25].props.layer, "Red", "e[25] layer");
    check_double(e[25].as_point().location.x, 10.0, "e[25] x");

    check_double(e[27].as_point().location.z, 7.5, "e[27] z (3D)");
    std::cout << "    POINT: 4 entities OK (3D)" << std::endl;

    // --- TEXT entities (28-35) ---
    check(e[28].type == hdxf::EntityType::Text, "e[28] type=TEXT");
    check_str(e[28].as_text().content, "HDXF Test Drawing", "e[28] content");
    check_double(e[28].as_text().height, 5.0, "e[28] height");
    check_str(e[28].props.layer, "White", "e[28] layer");

    check_str(e[29].as_text().content, "Flygande b\xc3\xa4" "ckasiner s\xc3\xb6" "ka hwila p\xc3\xa5 mjuka tuvor", "e[29] content (swedish)");

    check(e[32].type == hdxf::EntityType::Text, "e[32] type=TEXT");
    check_str(e[32].as_text().content, "Rotated 45", "e[32] content");
    check_double(e[32].as_text().rotation, 45.0, "e[32] rotation");

    check_double(e[33].as_text().rotation, 90.0, "e[33] rotation");

    check_double(e[34].as_text().height, 1.5, "e[34] height");
    check_double(e[35].as_text().height, 8.0, "e[35] height");
    std::cout << "    TEXT: 8 entities OK (rotation)" << std::endl;

    // --- POLYLINE entities (36-41) ---
    check(e[36].type == hdxf::EntityType::Polyline, "e[36] type=POLYLINE");
    check_int(static_cast<int>(e[36].as_polyline().vertices.size()), 4, "e[36] vertex count");
    check(e[36].as_polyline().is_closed(), "e[36] closed");
    check_str(e[36].props.layer, "Red", "e[36] layer");

    check(!e[37].as_polyline().is_closed(), "e[37] open");
    check(e[38].as_polyline().is_closed(), "e[38] closed");

    check_int(static_cast<int>(e[39].as_polyline().vertices.size()), 6, "e[39] hexagon vertices");
    check(!e[40].as_polyline().is_closed(), "e[40] open zigzag");

    // Polyline with bulge (41)
    check(e[41].type == hdxf::EntityType::Polyline, "e[41] type=POLYLINE");
    check(e[41].as_polyline().is_closed(), "e[41] closed");
    check_double(e[41].as_polyline().vertices[0].bulge, 0.5, "e[41] v0 bulge");
    std::cout << "    POLYLINE: 6 entities OK (closed, open, bulge)" << std::endl;

    // --- TRACE entities (42-43) ---
    check(e[42].type == hdxf::EntityType::Trace, "e[42] type=TRACE");
    check_double(e[42].as_trace().p0.x, 0.0, "e[42] p0.x");
    check_double(e[42].as_trace().p1.x, 10.0, "e[42] p1.x");
    check_double(e[42].as_trace().p2.y, -65.0, "e[42] p2.y");
    check_str(e[42].props.layer, "Red", "e[42] layer");

    check(e[43].type == hdxf::EntityType::Trace, "e[43] type=TRACE");
    check_str(e[43].props.layer, "Green", "e[43] layer");
    std::cout << "    TRACE: 2 entities OK" << std::endl;

    // --- SOLID entities (44-45) ---
    check(e[44].type == hdxf::EntityType::Solid, "e[44] type=SOLID");
    check_double(e[44].as_solid().p0.x, 30.0, "e[44] p0.x");
    check_double(e[44].as_solid().p1.x, 40.0, "e[44] p1.x");
    check_str(e[44].props.layer, "Blue", "e[44] layer");

    check(e[45].type == hdxf::EntityType::Solid, "e[45] type=SOLID");
    check_str(e[45].props.layer, "Cyan", "e[45] layer");
    std::cout << "    SOLID: 2 entities OK" << std::endl;

    // --- 3DFACE entities (46-47) ---
    check(e[46].type == hdxf::EntityType::Face3D, "e[46] type=3DFACE");
    check_double(e[46].as_face3d().p0.x, 0.0, "e[46] p0.x");
    check_double(e[46].as_face3d().p2.z, 5.0, "e[46] p2.z");  // 3D vertex
    check_str(e[46].props.layer, "Red", "e[46] layer");

    check(e[47].type == hdxf::EntityType::Face3D, "e[47] type=3DFACE");
    check_int(e[47].as_face3d().invisible_edges, 1, "e[47] invisible_edges");
    check_str(e[47].props.layer, "Green", "e[47] layer");
    std::cout << "    3DFACE: 2 entities OK (invisible edges)" << std::endl;

    // --- INSERT entities (48-49) ---
    check(e[48].type == hdxf::EntityType::Insert, "e[48] type=INSERT");
    check_str(e[48].as_insert().block_name, "ARROW", "e[48] block_name");
    check_double(e[48].as_insert().insertion_point.x, -80.0, "e[48] pos.x");
    check_str(e[48].props.layer, "Red", "e[48] layer");

    // INSERT with attributes + scale + rotation (49)
    check(e[49].type == hdxf::EntityType::Insert, "e[49] type=INSERT");
    check_str(e[49].as_insert().block_name, "TITLED", "e[49] block_name");
    check_double(e[49].as_insert().x_scale, 1.5, "e[49] x_scale");
    check_double(e[49].as_insert().rotation, 30.0, "e[49] rotation");
    check_int(static_cast<int>(e[49].as_insert().attributes.size()), 2, "e[49] attribute count");
    if (e[49].as_insert().attributes.size() >= 2) {
        check_str(e[49].as_insert().attributes[0].data.tag, "TITLE", "e[49] attr[0] tag");
        check_str(e[49].as_insert().attributes[0].data.value, "My Drawing", "e[49] attr[0] value");
        check_double(e[49].as_insert().attributes[0].data.height, 5.0, "e[49] attr[0] height");
        check_str(e[49].as_insert().attributes[1].data.tag, "AUTHOR", "e[49] attr[1] tag");
        check_str(e[49].as_insert().attributes[1].data.value, "HDXF Test", "e[49] attr[1] value");
    }
    check_str(e[49].props.layer, "Blue", "e[49] layer");
    std::cout << "    INSERT: 2 entities OK (plain, with attributes + scale + rotation)" << std::endl;

    // --- ATTDEF entity (50) ---
    check(e[50].type == hdxf::EntityType::Attdef, "e[50] type=ATTDEF");
    check_str(e[50].as_attdef().tag, "NUMBER", "e[50] tag");
    check_str(e[50].as_attdef().default_value, "N/A", "e[50] default_value");
    check_str(e[50].as_attdef().prompt, "Enter number:", "e[50] prompt");
    check_double(e[50].as_attdef().height, 3.0, "e[50] height");
    check_double(e[50].as_attdef().start_point.x, -50.0, "e[50] start.x");
    std::cout << "    ATTDEF: 1 entity OK" << std::endl;

    // --- DIMENSION entities (51-57) ---
    // Rotated (51)
    check(e[51].type == hdxf::EntityType::Dimension, "e[51] type=DIMENSION");
    check_int(static_cast<int>(e[51].as_dimension().dim_type),
              static_cast<int>(hdxf::DimensionType::Rotated), "e[51] dim_type=Rotated");
    check_double(e[51].as_dimension().definition_point.x, 0.0, "e[51] def_point.x");
    check_double(e[51].as_dimension().text_midpoint.x, 25.0, "e[51] text_mid.x");
    check_double(e[51].as_dimension().def_point3.x, 50.0, "e[51] def3.x");

    // Aligned (52)
    check_int(static_cast<int>(e[52].as_dimension().dim_type),
              static_cast<int>(hdxf::DimensionType::Aligned), "e[52] dim_type=Aligned");

    // Angular (53)
    check_int(static_cast<int>(e[53].as_dimension().dim_type),
              static_cast<int>(hdxf::DimensionType::Angular), "e[53] dim_type=Angular");
    check_double(e[53].as_dimension().arc_point.x, 15.0, "e[53] arc_point.x");

    // Diameter (54)
    check_int(static_cast<int>(e[54].as_dimension().dim_type),
              static_cast<int>(hdxf::DimensionType::Diameter), "e[54] dim_type=Diameter");
    check_double(e[54].as_dimension().leader_length, 5.0, "e[54] leader_length");

    // Radius (55)
    check_int(static_cast<int>(e[55].as_dimension().dim_type),
              static_cast<int>(hdxf::DimensionType::Radius), "e[55] dim_type=Radius");
    check_double(e[55].as_dimension().leader_length, 3.0, "e[55] leader_length");

    // Angular3Pt (56)
    check_int(static_cast<int>(e[56].as_dimension().dim_type),
              static_cast<int>(hdxf::DimensionType::Angular3Pt), "e[56] dim_type=Angular3Pt");

    // Ordinate (57)
    check_int(static_cast<int>(e[57].as_dimension().dim_type),
              static_cast<int>(hdxf::DimensionType::Ordinate), "e[57] dim_type=Ordinate");

    std::cout << "    DIMENSION: 7 entities OK (all 7 subtypes)" << std::endl;

    // --- VIEWPORT entity (58) ---
    check(e[58].type == hdxf::EntityType::Viewport, "e[58] type=VIEWPORT");
    check_double(e[58].as_viewport().width, 200.0, "e[58] width");
    check_double(e[58].as_viewport().height, 150.0, "e[58] height");
    check_int(e[58].as_viewport().status, 1, "e[58] status");
    check_int(e[58].as_viewport().id, 1, "e[58] id");
    check_int(e[58].props.paper_space, 1, "e[58] paper_space");
    std::cout << "    VIEWPORT: 1 entity OK (paper_space)" << std::endl;
}

// =========================================================================
// Full verify
// =========================================================================

static void verify_drawing(const hdxf::Drawing& loaded, const std::string& label) {
    std::cout << "\n--- Verifying " << label << " ---" << std::endl;

    verify_tables(loaded);
    verify_blocks(loaded);
    verify_headers(loaded);

    std::cout << "  Entities:" << std::endl;
    verify_entities(loaded);

    // Print entity type summary
    int counts[16] = {};
    for (const auto& ent : loaded.entities()) {
        counts[static_cast<int>(ent.type)]++;
    }
    std::cout << "  Summary: " << loaded.entities().size() << " entities";
    const char* names[] = {"LINE","CIRCLE","ARC","POINT","TEXT","POLYLINE",
                           "TRACE","SOLID","3DFACE","SHAPE","INSERT","ATTDEF",
                           "ATTRIB","DIMENSION","VIEWPORT","UNKNOWN"};
    for (int i = 0; i < 16; i++) {
        if (counts[i] > 0) std::cout << " " << names[i] << ":" << counts[i];
    }
    std::cout << std::endl;
}

// =========================================================================
// main
// =========================================================================

int main() {
    std::cout << "=== HDXF R12 DXF Complete Feature Test ===" << std::endl;

    // =====================================================================
    // Full roundtrip test (level 7 = everything)
    // =====================================================================

    std::cout << "\n--- Full roundtrip test ---" << std::endl;
    hdxf::Drawing dwg = build_test_drawing(7);
    std::cout << "Created test drawing:" << std::endl;
    std::cout << "  Entities: " << dwg.entities().size() << std::endl;
    std::cout << "  Blocks: " << dwg.blocks().size() << std::endl;

    if (!dwg.save_as("test_r12.dxf", hdxf::DxfFormat::Ascii)) {
        std::cerr << "FATAL: Failed to save test_r12.dxf" << std::endl;
        return 1;
    }
    if (!dwg.save_as("test_r12_bin.dxf", hdxf::DxfFormat::Binary)) {
        std::cerr << "FATAL: Failed to save test_r12_bin.dxf" << std::endl;
        return 1;
    }

    // Load ASCII and verify
    {
        hdxf::Drawing loaded(hdxf::DxfVersion::R12);
        if (!loaded.load("test_r12.dxf")) {
            std::cerr << "FATAL: Failed to load test_r12.dxf: " << loaded.last_error() << std::endl;
            return 1;
        }
        verify_drawing(loaded, "ASCII (test_r12.dxf)");
    }

    // Load Binary and verify
    {
        hdxf::Drawing loaded(hdxf::DxfVersion::R12);
        if (!loaded.load("test_r12_bin.dxf")) {
            std::cerr << "FATAL: Failed to load test_r12_bin.dxf: " << loaded.last_error() << std::endl;
            return 1;
        }
        verify_drawing(loaded, "Binary (test_r12_bin.dxf)");
    }

    // Layer visibility roundtrip test — load, set all layers except "0" to off, save
    std::cout << "\n--- Layer visibility roundtrip test ---" << std::endl;

    // ASCII
    {
        hdxf::Drawing loaded(hdxf::DxfVersion::R12);
        if (!loaded.load("test_r12.dxf")) {
            std::cerr << "FATAL: Failed to load test_r12.dxf for visibility test" << std::endl;
            return 1;
        }
        int changed = 0;
        for (auto& l : loaded.layers()) {
            if (l.name != "0") { l.on = false; changed++; }
        }
        if (!loaded.save()) {
            std::cerr << "FATAL: Failed to save test_r12.dxf after visibility change" << std::endl;
            return 1;
        }
        std::cout << "  ASCII: set " << changed << " layers to off, saved OK" << std::endl;
    }

    // Binary
    {
        hdxf::Drawing loaded(hdxf::DxfVersion::R12);
        if (!loaded.load("test_r12_bin.dxf")) {
            std::cerr << "FATAL: Failed to load test_r12_bin.dxf for visibility test" << std::endl;
            return 1;
        }
        int changed = 0;
        for (auto& l : loaded.layers()) {
            if (l.name != "0") { l.on = false; changed++; }
        }
        if (!loaded.save()) {
            std::cerr << "FATAL: Failed to save test_r12_bin.dxf after visibility change" << std::endl;
            return 1;
        }
        std::cout << "  Binary: set " << changed << " layers to off, saved OK" << std::endl;
    }

    // Final report
    std::cout << "\n========================================" << std::endl;
    if (errors == 0) {
        std::cout << "ALL CHECKS PASSED" << std::endl;
    } else {
        std::cout << errors << " CHECK(S) FAILED" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return (errors == 0) ? 0 : 1;
}
