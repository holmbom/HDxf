#include "test_framework.hpp"
#include "hdxf.hpp"
#include "geometry/extents.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Which entities count in $EXTMIN/$EXTMAX: not one with group 60 set, nor what it inserts; not an ATTRIB with group 60 or the invisible
// flag of group 70, nor a constant ATTDEF with that flag. In model and paper space alike. Facit: AutoCAD 2025's own saved values of
// test/data/synthetic/extents_visibility.dxf (doc/facit/fas9_extents_visibility.scr says what each extreme stands for).
// ---------------------------------------------------------------------------

namespace {

using hdxf::internal::Extents;

struct Computed {
    Extents model;
    Extents paper;
};

Computed compute(const hdxf::Drawing& dwg) {
    Computed c;
    std::vector<std::string> warnings;
    hdxf::internal::drawing_extents(dwg, {}, {}, c.model, c.paper, warnings);
    return c;
}

void near(double a, double b, double tol, const std::string& what) {
    char buf[200];
    std::snprintf(buf, sizeof buf, "%s: %.17g != %.17g", what.c_str(), a, b);
    hdxf_test::assert_true(std::fabs(a - b) <= tol, buf);
}

hdxf::Entity line_of(const hdxf::Point3& a, const hdxf::Point3& b, bool visible = true) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Line;
    e.data = hdxf::LineData{a, b};
    e.props.visible = visible;
    return e;
}

hdxf::Entity insert_of(const std::string& block, double rotation, bool visible = true) {
    hdxf::Entity e;
    e.type = hdxf::EntityType::Insert;
    hdxf::InsertData d;
    d.block_name = block;
    d.rotation = rotation;
    e.data = d;
    e.props.visible = visible;
    return e;
}

// The line cases of the facit, with the entities that are invisible when `hide` is set (and visible, so that they count, when not)
hdxf::Drawing lines_drawing(bool hide) {
    hdxf::Drawing dwg;
    const bool visible = !hide;
    dwg.entities().push_back(line_of({0, 0, 0}, {10, 10, 0}));
    dwg.entities().push_back(line_of({0, 0, 0}, {1000, 0, 0}, visible));            // x max 1000
    // A line of a block that is invisible, in an INSERT turned 0 degrees
    hdxf::Block& bv = dwg.add_block("BV");
    bv.entities.push_back(line_of({0, 0, 0}, {1, 1, 0}));
    bv.entities.push_back(line_of({0, 0, 0}, {0, 2000, 0}, visible));               // y max 2000
    dwg.entities().push_back(insert_of("BV", 0.0));
    // An invisible INSERT: its block with it
    dwg.add_block("BH").entities.push_back(line_of({0, 0, 0}, {0, 0, 3000}));       // z max 3000
    dwg.entities().push_back(insert_of("BH", 0.0, visible));
    // A line of a block that is invisible, in an INSERT turned 30 degrees (walked entity by entity)
    hdxf::Block& br = dwg.add_block("BR");
    br.entities.push_back(line_of({0, 0, 0}, {1, 0, 0}));
    br.entities.push_back(line_of({0, 0, 0}, {0, 0, -7000}, visible));              // z min -7000
    dwg.entities().push_back(insert_of("BR", 30.0));
    // Paper space
    hdxf::Entity paper_line = line_of({0, 0, 0}, {5, 5, 0});
    paper_line.props.paper_space = 1;
    dwg.entities().push_back(paper_line);
    hdxf::Entity hidden_paper = line_of({0, 0, 0}, {500, 0, 0}, visible);           // paper x max 500
    hidden_paper.props.paper_space = 1;
    dwg.entities().push_back(hidden_paper);
    return dwg;
}

// The attribute cases: an INSERT of a block with constant ATTDEFs, with three ATTRIBs. Hidden: the second ATTRIB has group 60, the
// third the invisible flag, the second ATTDEF the invisible flag
hdxf::Drawing attributes_drawing(bool hide) {
    hdxf::Drawing dwg;
    hdxf::Block& block = dwg.add_block("BA");
    block.flags = 2;
    auto attdef = [&](const std::string& tag, const std::string& value, int flags, double z) {
        hdxf::Entity e;
        e.type = hdxf::EntityType::Attdef;
        hdxf::AttdefData d;
        d.tag = tag;
        d.default_value = value;
        d.flags = flags;
        d.start_point = {0, 0, z};
        e.data = d;
        block.entities.push_back(e);
    };
    attdef("A1", "D", 0, 0.0);
    attdef("C1", "K", 2, 10.0);                                 // a constant one: shown with its value, at z 10
    attdef("C2", "K", hide ? 3 : 2, -6000.0);                   // a constant one with the invisible flag
    hdxf::Entity insert = insert_of("BA", 0.0);
    auto attrib = [&](const std::string& tag, double x, double y, int flags, bool visible) {
        hdxf::InsertAttribute a;
        a.data.tag = tag;
        a.data.value = "V";
        a.data.flags = flags;
        a.data.start_point = {x, y, 0};
        a.props.visible = visible;
        insert.as_insert().attributes.push_back(a);
    };
    attrib("A1", 50, 50, 0, true);
    attrib("A2", -4000, 50, 0, !hide);                          // group 60
    attrib("A3", 50, -5000, hide ? 1 : 0, true);                // group 70, bit 1
    dwg.entities().push_back(insert);
    return dwg;
}

hdxf::Point3 header_point(const hdxf::Drawing& dwg, const std::string& name) {
    const hdxf::HeaderVar* h = dwg.find_header(name);
    hdxf_test::assert_true(h != nullptr, name + " is missing");
    hdxf::Point3 p;
    for (const auto& [code, value] : h->values) {
        if (code == 10) p.x = std::stod(value);
        if (code == 20) p.y = std::stod(value);
        if (code == 30) p.z = std::stod(value);
    }
    return p;
}

void point_is(const hdxf::Point3& got, const hdxf::Point3& want, double tol, const std::string& what) {
    near(got.x, want.x, tol, what + " x");
    near(got.y, want.y, tol, what + " y");
    near(got.z, want.z, tol, what + " z");
}

// The extents against the four header variables a file holds
void matches_header(const Computed& c, const hdxf::Drawing& file, const std::string& what) {
    point_is(c.model.min, header_point(file, "$EXTMIN"), 1e-5, what + " $EXTMIN");
    point_is(c.model.max, header_point(file, "$EXTMAX"), 1e-5, what + " $EXTMAX");
    point_is(c.paper.min, header_point(file, "$PEXTMIN"), 1e-5, what + " $PEXTMIN");
    point_is(c.paper.max, header_point(file, "$PEXTMAX"), 1e-5, what + " $PEXTMAX");
}

} // namespace

// What is invisible is not in the extents, in model and paper space: and the same drawing with everything visible reaches every
// extreme the invisible ones stand for
TEST(hidden_entities_do_not_count) {
    Computed hidden = compute(lines_drawing(true));
    point_is(hidden.model.min, {0, 0, 0}, 1e-9, "model min");
    point_is(hidden.model.max, {10, 10, 0}, 1e-9, "model max");
    point_is(hidden.paper.min, {0, 0, 0}, 1e-9, "paper min");
    point_is(hidden.paper.max, {5, 5, 0}, 1e-9, "paper max");

    Computed shown = compute(lines_drawing(false));
    near(shown.model.max.x, 1000, 1e-9, "the line of the model");
    near(shown.model.max.y, 2000, 1e-9, "the line of the block");
    near(shown.model.max.z, 3000, 1e-9, "the INSERT");
    near(shown.model.min.z, -7000, 1e-9, "the line of the turned block");
    near(shown.paper.max.x, 500, 1e-9, "the line of paper space");
}

// An ATTRIB with group 60 or the invisible flag, and a constant ATTDEF with the flag, are not in the extents. Text boxes depend on the
// fonts of the machine, so the bounds are loose: the extremes of the hidden ones are thousands away
TEST(hidden_attributes_do_not_count) {
    Computed hidden = compute(attributes_drawing(true));
    hdxf_test::assert_true(hidden.model.min.x > -10, "the ATTRIB with group 60 is out: min.x " + std::to_string(hidden.model.min.x));
    hdxf_test::assert_true(hidden.model.min.y > -10, "the ATTRIB with the flag is out: min.y " + std::to_string(hidden.model.min.y));
    hdxf_test::assert_true(hidden.model.min.z > -10, "the ATTDEF with the flag is out: min.z " + std::to_string(hidden.model.min.z));
    // What is visible counts: the ATTRIB at (50, 50) and the constant ATTDEF at z 10
    hdxf_test::assert_true(hidden.model.max.x >= 50 && hidden.model.max.y >= 50, "the ATTRIB that shows is in");
    near(hidden.model.max.z, 10, 1e-9, "the constant ATTDEF that shows");

    Computed shown = compute(attributes_drawing(false));
    hdxf_test::assert_true(shown.model.min.x < -3990, "shown: the ATTRIB with group 60 counts");
    hdxf_test::assert_true(shown.model.min.y < -4990, "shown: the ATTRIB with the flag counts");
    near(shown.model.min.z, -6000, 1e-9, "shown: the constant ATTDEF counts");
}

// Drawing::entity_extents has none for an entity that is invisible, or an INSERT of one
TEST(entity_extents_hidden_has_none) {
    hdxf::Drawing dwg;
    dwg.add_block("B").entities.push_back(line_of({0, 0, 0}, {3, 4, 0}));
    hdxf::Point3 lo, hi;

    hdxf_test::assert_true(dwg.entity_extents(line_of({0, 0, 0}, {10, 10, 0}), lo, hi), "visible line");
    point_is(hi, {10, 10, 0}, 1e-12, "visible line max");
    hdxf_test::assert_false(dwg.entity_extents(line_of({0, 0, 0}, {10, 10, 0}, false), lo, hi), "invisible line");

    hdxf_test::assert_true(dwg.entity_extents(insert_of("B", 0.0), lo, hi), "visible INSERT");
    point_is(hi, {3, 4, 0}, 1e-12, "visible INSERT max");
    hdxf_test::assert_false(dwg.entity_extents(insert_of("B", 0.0, false), lo, hi), "invisible INSERT");
    hdxf_test::assert_false(dwg.entity_extents(insert_of("B", 30.0, false), lo, hi), "invisible INSERT, turned");
}

// AutoCAD 2025's saved $EXTMIN/$EXTMAX/$PEXTMIN/$PEXTMAX of the facit files, R12 and 2000 … 2018, ASCII and binary: what is computed
// from the synthetic input is those values. A 2000 … 2018 file keeps the invisibility of its entities, so it is also computed from
// itself (R12 does not keep it)
TEST(extents_visibility_equals_autocad_saved_values) {
    const std::filesystem::path input =
        hdxf::path_from_utf8(HDXF_TEST_DATA_DIR) / "synthetic" / "extents_visibility.dxf";
    if (!std::filesystem::exists(input)) hdxf_test::skip("the synthetic input is missing");
    hdxf::Drawing source;
    hdxf_test::assert_true(source.load(input), source.last_error());
    const Computed computed = compute(source);
    // AutoCAD's own: about (0,0,0) … (100,100,10) in the model, (0,0,0) … (10,10,0) in paper space
    point_is(computed.model.min, {0, 0, 0}, 1e-5, "input model min");
    point_is(computed.model.max, {100, 100, 10}, 1e-5, "input model max");
    point_is(computed.paper.min, {0, 0, 0}, 1e-5, "input paper min");
    point_is(computed.paper.max, {10, 10, 0}, 1e-5, "input paper max");

    int compared = 0;
    for (const char* version : {"r12", "2000", "2004", "2007", "2010", "2013", "2018"}) {
        for (const char* suffix : {"", "_bin"}) {
            const std::string name = std::string("acad_") + version + "_dxf_extents_visibility" + suffix + ".dxf";
            const std::filesystem::path file = hdxf::path_from_utf8(HDXF_SOURCE_DIR) / "doc" / "facit" / name;
            if (!std::filesystem::exists(file)) continue;
            hdxf::Drawing saved;
            hdxf_test::assert_true(saved.load(file), name + ": " + saved.last_error());
            matches_header(computed, saved, name + " from the input");
            if (std::string(version) != "r12") matches_header(compute(saved), saved, name + " from itself");
            ++compared;
        }
    }
    if (compared == 0) hdxf_test::skip("no facit file of extents_visibility");
}
