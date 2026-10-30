#include "test_framework.hpp"
#include "hdxf.hpp"
#include "group_io.hpp"
#include "dxf_structure.hpp"
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// The findings of the review of phase 6 (one test each): a multiline attribute in R12, removed MTEXT
// columns in a read 2018 file, a multiline attribute's extents, a frame's scale without a fill,
// a multiline attribute's background before 2018, a linked column's or frame's display properties.
// ---------------------------------------------------------------------------

namespace {

using Record = std::vector<std::pair<int, std::string>>;

std::vector<Record> records_of(const std::vector<uint8_t>& data) {
    std::string s(data.begin(), data.end());
    std::istringstream in(s, std::ios::binary);
    hdxf::internal::GroupReader reader(in);
    reader.detect_format();
    std::vector<Record> out;
    hdxf::internal::GroupCode gc;
    while (reader.read(gc)) {
        if (gc.code == 999) continue;
        if (gc.code == 0 || out.empty()) out.emplace_back();
        out.back().push_back({gc.code, dxf_structure::number_text(gc.code, gc.value)});
    }
    return out;
}

std::vector<uint8_t> save(hdxf::Drawing& dwg) {
    std::vector<uint8_t> out = dwg.save_to_memory(hdxf::DxfFormat::Ascii);
    hdxf_test::assert_false(out.empty(), "save: " + dwg.last_error());
    return out;
}

hdxf::Drawing load(const std::vector<uint8_t>& data) {
    hdxf::Drawing dwg;
    hdxf_test::assert_true(dwg.load_from_memory(data.data(), data.size()), dwg.last_error());
    return dwg;
}

hdxf::MultilineAttribute multiline(const std::string& text, hdxf::Point3 at) {
    hdxf::MultilineAttribute m;
    m.type = 2;
    m.alignment_point = at;
    m.text.insertion_point = at;
    m.text.height = 2.0;
    m.text.reference_width = 30.0;
    m.text.drawing_direction = 5;
    m.text.content = text;
    return m;
}

// A block with one INSERT whose attribute has an empty outer value and the text in the embedded MTEXT
hdxf::Drawing attribute_drawing(hdxf::DxfVersion version, hdxf::Point3 text_at) {
    hdxf::Drawing dwg(version);
    dwg.add_block("TITEL");
    hdxf::Entity& ins = dwg.add_insert("TITEL", {0, 0, 0});
    hdxf::InsertAttribute attr;
    attr.data.tag = "NOTE";
    attr.data.start_point = {0, 0, 0};
    attr.data.height = 2.0;
    attr.data.multiline = multiline("Rad ett\\PRad tv\xC3\xA5", text_at);
    ins.as_insert().attributes.push_back(attr);
    return dwg;
}

std::vector<hdxf::Entity*> linked_texts(hdxf::Drawing& dwg) {
    std::vector<hdxf::Entity*> out;
    for (auto& e : dwg.entities()) {
        if (e.type == hdxf::EntityType::MText && !e.as_mtext().column_parent.empty()) out.push_back(&e);
    }
    return out;
}

hdxf::Entity& two_column_text(hdxf::Drawing& dwg, const std::string& layer) {
    hdxf::Entity& mt = dwg.add_mtext({0, 0, 0}, "Ett\\NTv\xC3\xA5", 2.0, layer);
    mt.as_mtext().reference_width = 21.0;
    hdxf::MTextColumns c;
    c.type = 1;
    c.count = 2;
    c.width = 10.0;
    c.gutter = 1.0;
    c.heights = {};
    mt.as_mtext().columns = c;
    return mt;
}

} // namespace

// [P1] A multiline attribute cannot be written as R12 (no OBJECTS to hold its text): an error before
// anything is written, not a silent loss of the text
TEST(review_2018_multiline_attribute_refused_in_r12) {
    hdxf::Drawing dwg = attribute_drawing(hdxf::DxfVersion::R12, {5, 5, 0});
    hdxf_test::assert_true(dwg.save_to_memory().empty(), "refused");
    hdxf_test::assert_true(dwg.last_error().find("multiline attribute requires DXF 2000 or later") != std::string::npos,
                           dwg.last_error());
    // A definition in a block as well
    hdxf::Drawing def(hdxf::DxfVersion::R12);
    hdxf::Block& block = def.add_block("B");
    hdxf::AttdefData d;
    d.tag = "T";
    d.multiline = multiline("x", {0, 0, 0});
    hdxf::Entity e;
    e.type = hdxf::EntityType::Attdef;
    e.data = d;
    block.entities.push_back(e);
    hdxf_test::assert_true(def.save_to_memory().empty(), "definition refused");
    // 2000 and later keep it
    hdxf::Drawing ok = attribute_drawing(hdxf::DxfVersion::R2000, {5, 5, 0});
    hdxf_test::assert_false(ok.save_to_memory().empty(), ok.last_error());
}

// [P2] The columns of a text read from a 2018 file are taken away: they are gone when it is read again
TEST(review_2018_removed_columns_do_not_return) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2018);
    hdxf::Entity& mt = dwg.add_mtext({0, 0, 0}, "Kolumntext som g\xC3\xA5r \xC3\xA5t flera rader", 2.0);
    mt.as_mtext().reference_width = 21.0;
    hdxf::MTextColumns c;
    c.type = 2;
    c.count = 2;
    c.width = 10.0;
    c.gutter = 1.0;
    c.heights = {0.0};
    mt.as_mtext().columns = c;
    hdxf::Drawing back = load(save(dwg));
    hdxf_test::assert_true(back.entities()[0].as_mtext().columns.has_value(), "the columns are read");
    back.entities()[0].as_mtext().columns.reset();
    back.entities()[0].as_mtext().embedded.reset();
    hdxf::Drawing again = load(save(back));
    hdxf_test::assert_false(again.entities()[0].as_mtext().columns.has_value(), "the columns stay removed");
}

// [P2] A multiline attribute with an empty outer value is measured by its embedded text
TEST(review_2018_multiline_attribute_has_extents) {
    for (hdxf::DxfVersion v : {hdxf::DxfVersion::R2018, hdxf::DxfVersion::R2013}) {
        hdxf::Drawing dwg = attribute_drawing(v, {500, 400, 0});
        hdxf::Drawing back = load(save(dwg));
        const hdxf::HeaderVar* max = back.find_header("$EXTMAX");
        hdxf_test::assert_true(max != nullptr && max->values.size() >= 2, "$EXTMAX");
        hdxf_test::assert_true(std::atof(max->values[0].second.c_str()) >= 500.0, "x covers the embedded text");
        hdxf_test::assert_true(std::atof(max->values[1].second.c_str()) >= 400.0, "y covers the embedded text");
    }
    // A free ATTDEF
    hdxf::Drawing def(hdxf::DxfVersion::R2018);
    hdxf::AttdefData d;
    d.tag = "T";
    d.multiline = multiline("text", {300, 200, 0});
    hdxf::Entity e;
    e.type = hdxf::EntityType::Attdef;
    e.data = d;
    def.add_entity(e);
    hdxf::Drawing back = load(save(def));
    const hdxf::HeaderVar* max = back.find_header("$EXTMAX");
    hdxf_test::assert_true(max != nullptr && std::atof(max->values[0].second.c_str()) >= 300.0, "ATTDEF extents");
}

// [P2] A frame without a fill (flags 16) has no 45 before 2018: its scale is the XDATA's, the frame is linked
// and a second save does not make another one
TEST(review_2018_frame_scale_without_fill) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    hdxf::Entity& mt = dwg.add_mtext({0, 0, 0}, "Text in a frame", 2.0);
    mt.as_mtext().background.flags = 16;
    mt.as_mtext().background.scale = 2.0;
    std::vector<uint8_t> first = save(dwg);
    hdxf::Drawing back = load(first);
    int polylines = 0;
    for (const auto& e : back.entities()) polylines += e.type == hdxf::EntityType::LwPolyline;
    hdxf_test::assert_eq(polylines, 1, "one frame");
    hdxf_test::assert_eq(back.entities()[0].as_mtext().background.scale, 2.0, "the scale");
    hdxf_test::assert_eq(static_cast<int>(back.entities()[0].as_mtext().background.border_handles.size()), 1, "linked");
    back.entities()[0].as_mtext().height = 2.5;
    hdxf::Drawing again = load(save(back));
    int after = 0;
    for (const auto& e : again.entities()) after += e.type == hdxf::EntityType::LwPolyline;
    hdxf_test::assert_eq(after, 1, "still one frame");
    hdxf_test::assert_eq(again.entities()[0].as_mtext().background.scale, 2.0, "the scale is kept");
}

// [P2] A loaded multiline attribute's background (colour, transparency) is part of its XRECORD
TEST(review_2018_multiline_attribute_background_updates_the_xrecord) {
    hdxf::Drawing dwg = attribute_drawing(hdxf::DxfVersion::R2013, {5, 5, 0});
    hdxf::Drawing back = load(save(dwg));
    hdxf::InsertAttribute* attr = nullptr;
    for (auto& e : back.entities()) {
        if (e.type == hdxf::EntityType::Insert && !e.as_insert().attributes.empty()) attr = &e.as_insert().attributes[0];
    }
    hdxf_test::assert_true(attr != nullptr && attr->data.multiline.has_value(), "the attribute");
    if (!attr || !attr->data.multiline) return;
    hdxf::MTextBackground& bg = attr->data.multiline->text.background;
    bg.flags = 1;
    bg.color = hdxf::Color::from_aci(5);
    std::vector<Record> recs = records_of(save(back));
    bool found = false;
    for (const auto& r : recs) {
        if (r[0].second != "XRECORD") continue;
        bool embedded = false, color = false;
        for (const auto& g : r) {
            embedded = embedded || (g.first == 1 && g.second == "Embedded Object");
            color = color || (g.first == 63 && g.second == "5");
        }
        found = found || (embedded && color);
    }
    hdxf_test::assert_true(found, "the XRECORD has the colour");
    // The colour changed again: the XRECORD follows
    bg.color = hdxf::Color::from_aci(3);
    recs = records_of(save(back));
    found = false;
    for (const auto& r : recs) {
        if (r[0].second != "XRECORD") continue;
        bool embedded = false, color = false;
        for (const auto& g : r) {
            embedded = embedded || (g.first == 1 && g.second == "Embedded Object");
            color = color || (g.first == 63 && g.second == "3");
        }
        found = found || (embedded && color);
    }
    hdxf_test::assert_true(found, "the XRECORD follows the second change");
}

// [P2] The parent text's layer is the linked columns' and the frame's after a change
TEST(review_2018_linked_entities_follow_the_parents_layer) {
    hdxf::Drawing dwg(hdxf::DxfVersion::R2013);
    dwg.add_layer("A");
    dwg.add_layer("B");
    hdxf::Entity& mt = two_column_text(dwg, "A");
    mt.as_mtext().background.flags = 16;
    hdxf::Drawing back = load(save(dwg));
    hdxf_test::assert_eq(static_cast<int>(linked_texts(back).size()), 1, "a second column");
    int frames = 0;
    for (const auto& e : back.entities()) frames += e.type == hdxf::EntityType::LwPolyline;
    hdxf_test::assert_eq(frames, 2, "a frame for each column");
    for (auto& e : back.entities()) {
        if (e.type == hdxf::EntityType::MText && e.as_mtext().column_parent.empty()) {
            e.props.layer = "B";
            e.props.color = hdxf::Color::from_aci(2);
        }
    }
    hdxf::Drawing again = load(save(back));
    int checked = 0;
    for (const auto& e : again.entities()) {
        bool linked = (e.type == hdxf::EntityType::MText) || (e.type == hdxf::EntityType::LwPolyline);
        if (!linked) continue;
        hdxf_test::assert_eq(e.props.layer, "B", "layer of a text, column or frame");
        hdxf_test::assert_eq(e.props.color.aci, 2, "colour");
        ++checked;
    }
    hdxf_test::assert_eq(checked, 4, "two columns and two frames");
}
